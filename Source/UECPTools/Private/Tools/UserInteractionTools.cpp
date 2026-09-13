// Copyright 2026, BlueprintsLab, All rights reserved.

#include "Tools/UserInteractionTools.h"
#include "UECPCoreModule.h"
#include "Services/IUECPNotificationService.h"
#include "Services/IUECPArchitectService.h"
#include "Types/CallerContext.h"
#include "Managers/SettingsManager.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Misc/ConfigCacheIni.h"
#include "Async/Async.h"
#include "HAL/PlatformProcess.h"
#include "HAL/Event.h"

namespace UserInteractionTools
{
	static FString SerializeCondensed(const TSharedRef<FJsonObject>& Obj)
	{
		FString Out;
		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
		FJsonSerializer::Serialize(Obj, Writer);
		Writer->Close();
		return Out;
	}

	void HandleAskUserFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
	{
		if (!Args.IsValid())
		{
			OutError = TEXT("ask_user: missing arguments.");
			return;
		}

		const TArray<TSharedPtr<FJsonValue>>* Questions = nullptr;
		if (!Args->TryGetArrayField(TEXT("questions"), Questions) || !Questions || Questions->Num() == 0)
		{
			OutError = TEXT("ask_user requires a non-empty 'questions' array. Each item: { header, question, multiSelect(bool), options:[{label, description?}] }.");
			return;
		}

		TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetArrayField(TEXT("questions"), *Questions);
		const FString PayloadJson = SerializeCondensed(Payload);

		double TimeoutSecs = 600.0;
		Args->TryGetNumberField(TEXT("timeout_secs"), TimeoutSecs);
		TimeoutSecs = FMath::Clamp(TimeoutSecs, 30.0, 1800.0);

		FString AnswerJson;
		if (IUECPCoreModule::IsAvailable())
		{
			AnswerJson = IUECPCoreModule::Get().GetNotificationService().RequestUserQuestions(PayloadJson, TimeoutSecs);
		}

		if (AnswerJson.IsEmpty())
		{
			TSharedRef<FJsonObject> Fallback = MakeShared<FJsonObject>();
			Fallback->SetStringField(TEXT("status"), TEXT("no_interactive_ui"));
			Fallback->SetStringField(TEXT("message"),
				TEXT("No answer was captured (no question UI is attached, the prompt timed out, or another question is already in flight). Ask your question(s) inline as plain text in your next message and wait for the user's reply."));
			OutJsonString = SerializeCondensed(Fallback);
			return;
		}

		OutJsonString = AnswerJson;
	}

	static void SwitchInteractionMode(const FString& ModeKey)
	{
		auto Apply = [ModeKey]()
		{
			if (IUECPCoreModule::IsAvailable())
				IUECPCoreModule::Get().GetArchitectService().SetInteractionMode(ModeKey);
		};
		if (IsInGameThread()) { Apply(); return; }
		FEvent* Done = FPlatformProcess::GetSynchEventFromPool( false);
		AsyncTask(ENamedThreads::GameThread, [Apply, Done]() { Apply(); if (Done) Done->Trigger(); });
		if (Done) { Done->Wait(); FPlatformProcess::ReturnSynchEventToPool(Done); }
	}

	void HandleProceedWithPlanFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
	{
		FString Pref;
		GConfig->GetString(TEXT("BpGeneratorUltimate"), TEXT("PlanExecuteMode"), Pref, FSettingsManager::GetGlobalConfigPath());
		Pref = Pref.TrimStartAndEnd().ToLower();
		FString ModeKey = (Pref == TEXT("auto")) ? TEXT("auto") : (Pref == TEXT("ask")) ? TEXT("ask") : FString();

		if (ModeKey.IsEmpty())
		{
			TSharedRef<FJsonObject> Q = MakeShared<FJsonObject>();
			Q->SetStringField(TEXT("header"), TEXT("Execution mode"));
			Q->SetStringField(TEXT("question"), TEXT("How should I run the build from here? I'll remember your choice for next time — you can change it any time in Settings -> AI Behaviour."));
			Q->SetBoolField(TEXT("multiSelect"), false);
			TArray<TSharedPtr<FJsonValue>> Opts;
			{ TSharedRef<FJsonObject> O = MakeShared<FJsonObject>(); O->SetStringField(TEXT("label"), TEXT("Auto Edit")); O->SetStringField(TEXT("description"), TEXT("I build without asking before each change.")); Opts.Add(MakeShared<FJsonValueObject>(O)); }
			{ TSharedRef<FJsonObject> O = MakeShared<FJsonObject>(); O->SetStringField(TEXT("label"), TEXT("Ask Before Edit")); O->SetStringField(TEXT("description"), TEXT("I summarise and confirm each change first.")); Opts.Add(MakeShared<FJsonValueObject>(O)); }
			Q->SetArrayField(TEXT("options"), Opts);
			TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
			TArray<TSharedPtr<FJsonValue>> Qs; Qs.Add(MakeShared<FJsonValueObject>(Q));
			Payload->SetArrayField(TEXT("questions"), Qs);
			Payload->SetBoolField(TEXT("allowSkip"), false);
			Payload->SetBoolField(TEXT("allowOther"), false);
			Payload->SetBoolField(TEXT("allowFeedback"), false);

			FString AnswerJson;
			if (IUECPCoreModule::IsAvailable())
				AnswerJson = IUECPCoreModule::Get().GetNotificationService().RequestUserQuestions(SerializeCondensed(Payload), 600.0);

			FString Chosen;
			if (!AnswerJson.IsEmpty())
			{
				TSharedPtr<FJsonObject> A;
				TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(AnswerJson);
				if (FJsonSerializer::Deserialize(R, A) && A.IsValid())
				{
					const TArray<TSharedPtr<FJsonValue>>* Answers = nullptr;
					if (A->TryGetArrayField(TEXT("answers"), Answers) && Answers && Answers->Num() > 0)
					{
						TSharedPtr<FJsonObject> AO = (*Answers)[0]->AsObject();
						const TArray<TSharedPtr<FJsonValue>>* Sel = nullptr;
						if (AO.IsValid() && AO->TryGetArrayField(TEXT("selected"), Sel) && Sel && Sel->Num() > 0)
							Chosen = (*Sel)[0]->AsString();
					}
				}
			}

			if (Chosen.Contains(TEXT("Auto")))     ModeKey = TEXT("auto");
			else if (Chosen.Contains(TEXT("Ask"))) ModeKey = TEXT("ask");

			if (!ModeKey.IsEmpty())
			{
				GConfig->SetString(TEXT("BpGeneratorUltimate"), TEXT("PlanExecuteMode"), *ModeKey, FSettingsManager::GetGlobalConfigPath());
				GConfig->Flush(false, FSettingsManager::GetGlobalConfigPath());
			}
			else
			{
				ModeKey = TEXT("ask");
			}
		}

		SwitchInteractionMode(ModeKey);

		const FString ModeLabel = (ModeKey == TEXT("auto")) ? TEXT("Auto Edit") : TEXT("Ask Before Edit");
		TSharedRef<FJsonObject> Resp = MakeShared<FJsonObject>();
		Resp->SetStringField(TEXT("switched_to"), ModeLabel);
		Resp->SetStringField(TEXT("message"), FString::Printf(TEXT("Switched to %s. Proceed with building the plan now."), *ModeLabel));
		OutJsonString = SerializeCondensed(Resp);
	}
}
