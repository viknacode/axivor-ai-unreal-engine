// Copyright 2026, BlueprintsLab, All rights reserved

#include "Tools/ArchitectRunnerTools.h"

#include "UECPCoreModule.h"
#include "Services/IUECPArchitectService.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace ArchitectRunnerTools
{

namespace
{
	void EmitJson(const TSharedPtr<FJsonObject>& Obj, FString& OutJsonString)
	{
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
		FJsonSerializer::Serialize(Obj.ToSharedRef(), Writer);
	}

	void EmitFailure(const FString& Msg, FString& OutJsonString, FString& OutError)
	{
		OutError = Msg;
		TSharedPtr<FJsonObject> Fail = MakeShareable(new FJsonObject);
		Fail->SetBoolField(TEXT("success"), false);
		Fail->SetStringField(TEXT("error"), Msg);
		EmitJson(Fail, OutJsonString);
	}
}

void HandleArchitectStartChatFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { EmitFailure(TEXT("Invalid args"), OutJsonString, OutError); return; }

	FString Prompt;
	Args->TryGetStringField(TEXT("prompt"), Prompt);
	if (Prompt.IsEmpty()) { EmitFailure(TEXT("'prompt' is required"), OutJsonString, OutError); return; }

	FString Mode;
	Args->TryGetStringField(TEXT("mode"), Mode);
	if (Mode.IsEmpty()) Mode = TEXT("auto");

	IUECPArchitectService& Architect = IUECPCoreModule::Get().GetArchitectService();

	Architect.NewChat();
	Architect.SetInteractionMode(Mode);

	const FString ChatId = Architect.GetActiveChatID();
	if (ChatId.IsEmpty())
	{
		EmitFailure(TEXT("Failed to start a new Architect chat — is the BP Generator window open?"),
			OutJsonString, OutError);
		return;
	}

	Architect.SendMessage(Prompt);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("chat_id"), ChatId);
	Result->SetStringField(TEXT("mode"), Mode);
	Result->SetBoolField(TEXT("started"), true);
	EmitJson(Result, OutJsonString);
}

void HandleArchitectStopChatFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { EmitFailure(TEXT("Invalid args"), OutJsonString, OutError); return; }

	IUECPArchitectService& Architect = IUECPCoreModule::Get().GetArchitectService();

	FString ChatId;
	Args->TryGetStringField(TEXT("chat_id"), ChatId);

	bool bStopAll = false;
	Args->TryGetBoolField(TEXT("all"), bStopAll);

	if (bStopAll)
	{
		Architect.StopAllGeneration();
		TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
		Result->SetBoolField(TEXT("success"), true);
		Result->SetBoolField(TEXT("stopped_all"), true);
		EmitJson(Result, OutJsonString);
		return;
	}

	if (ChatId.IsEmpty())
	{
		EmitFailure(TEXT("'chat_id' is required (or pass all=true to stop every chat)"), OutJsonString, OutError);
		return;
	}

	Architect.StopGenerationForChat(ChatId);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("chat_id"), ChatId);
	Result->SetBoolField(TEXT("stopped"), true);
	EmitJson(Result, OutJsonString);
}

void HandleGetArchitectChatStateFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { EmitFailure(TEXT("Invalid args"), OutJsonString, OutError); return; }

	FString ChatId;
	Args->TryGetStringField(TEXT("chat_id"), ChatId);

	IUECPArchitectService& Architect = IUECPCoreModule::Get().GetArchitectService();

	const FString ActiveId = Architect.GetActiveChatID();
	const FString TargetId = ChatId.IsEmpty() ? ActiveId : ChatId;

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("chat_id"), TargetId);
	Result->SetBoolField(TEXT("is_active_chat"), TargetId == ActiveId);

	Result->SetBoolField(TEXT("is_thinking"), Architect.IsThinkingForChat(TargetId));

	TArray<TSharedPtr<FJsonValue>> History = Architect.GetConversationHistoryForChat(TargetId);
	Result->SetNumberField(TEXT("message_count"), History.Num());
	Result->SetArrayField(TEXT("history"), History);

	EmitJson(Result, OutJsonString);
}

}
