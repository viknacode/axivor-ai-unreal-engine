// Copyright 2026, BlueprintsLab, All rights reserved

#include "Tools/PythonTools.h"
#include "UECPPythonExtModule.h"

#include "UECPCoreModule.h"
#include "Services/IUECPLearningService.h"

#include "IPythonScriptPlugin.h"
#include "PythonScriptTypes.h"

#include "HAL/PlatformProcess.h"
#include "Async/Async.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace PythonTools
{

void HandleExecutePythonFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString Code, Description;
	if (Args.IsValid())
	{
		Args->TryGetStringField(TEXT("code"), Code);
		if (Code.IsEmpty()) Args->TryGetStringField(TEXT("script"), Code);
		Args->TryGetStringField(TEXT("description"), Description);
	}

	if (Code.IsEmpty())
	{
		OutError = TEXT("Python code cannot be empty");
		return;
	}

	IPythonScriptPlugin* PythonPlugin = IPythonScriptPlugin::Get();
	if (!PythonPlugin)
	{
		OutError = TEXT("Python plugin not loaded. Enable the Python Editor Script Plugin in Edit > Plugins.");
		return;
	}
	if (!PythonPlugin->IsPythonAvailable())
	{
		OutError = TEXT("Python is not available. The Python Editor Script Plugin may need to be enabled and the editor restarted.");
		return;
	}

	FPythonCommandEx PythonCmd;
	PythonCmd.Command = Code;
	PythonCmd.ExecutionMode = EPythonCommandExecutionMode::ExecuteFile;
	PythonCmd.FileExecutionScope = EPythonFileExecutionScope::Private;

	bool bSuccess = false;

	if (IsInGameThread())
	{
		bSuccess = PythonPlugin->ExecPythonCommandEx(PythonCmd);
	}
	else
	{
		FGraphEventRef Task = FFunctionGraphTask::CreateAndDispatchWhenReady(
			[&PythonPlugin, &PythonCmd, &bSuccess]()
			{
				bSuccess = PythonPlugin->ExecPythonCommandEx(PythonCmd);
			},
			TStatId(), nullptr, ENamedThreads::GameThread);
		Task->Wait();
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), bSuccess);

	TArray<TSharedPtr<FJsonValue>> OutputArr;
	FString FullOutput;
	bool bHasErrors = false;

	for (const FPythonLogOutputEntry& Entry : PythonCmd.LogOutput)
	{
		TSharedPtr<FJsonObject> LogEntry = MakeShared<FJsonObject>();
		FString TypeStr;
		switch (Entry.Type)
		{
			case EPythonLogOutputType::Info:    TypeStr = TEXT("info"); break;
			case EPythonLogOutputType::Warning: TypeStr = TEXT("warning"); break;
			case EPythonLogOutputType::Error:   TypeStr = TEXT("error"); bHasErrors = true; break;
			default: TypeStr = TEXT("info"); break;
		}
		LogEntry->SetStringField(TEXT("type"),    TypeStr);
		LogEntry->SetStringField(TEXT("message"), Entry.Output);
		OutputArr.Add(MakeShared<FJsonValueObject>(LogEntry));

		if (!FullOutput.IsEmpty()) FullOutput += TEXT("\n");
		FullOutput += Entry.Output;
	}

	Result->SetArrayField(TEXT("output"),       OutputArr);
	Result->SetStringField(TEXT("output_text"), FullOutput);

	if (!PythonCmd.CommandResult.IsEmpty())
	{
		Result->SetStringField(TEXT("result"), PythonCmd.CommandResult);
	}

	if (bSuccess && !bHasErrors)
	{
		Result->SetStringField(TEXT("message"), TEXT("Python script executed successfully."));
	}
	else
	{
		Result->SetStringField(TEXT("message"),
			FString::Printf(TEXT("Python script %s."), bHasErrors ? TEXT("had errors") : TEXT("failed")));
		if (!bSuccess) OutError = TEXT("Python execution failed");
	}

	const bool bIsExploration = Description.IsEmpty()
		|| Description == TEXT("Redirected from exec_console_command")
		|| Code.Len() < 100;

	if (IUECPCoreModule::Get().GetLearningService().IsInitialized() && !bIsExploration && bSuccess && !bHasErrors)
	{
		TSharedPtr<FJsonObject> TrackData = MakeShared<FJsonObject>();
		TrackData->SetStringField(TEXT("description"),  Description);
		TrackData->SetStringField(TEXT("script"),       Code.Left(2000));
		TrackData->SetBoolField  (TEXT("success"),      true);
		TrackData->SetNumberField(TEXT("output_lines"), PythonCmd.LogOutput.Num());
		IUECPCoreModule::Get().GetLearningService().TrackEvent(TEXT("python_script_executed"), TrackData);
	}

	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
}

void HandleGetPythonRecipeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString Query;
	if (Args.IsValid()) Args->TryGetStringField(TEXT("query"), Query);

	if (Query.IsEmpty())
	{
		OutError = TEXT("Query cannot be empty");
		return;
	}

	TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("query"), Query);

	struct FRecipeState
	{
		FEvent* DoneEvent = nullptr;
		FString ResultJson;
		FString ResultError;
		FRecipeState() : DoneEvent(FPlatformProcess::GetSynchEventFromPool(true)) {}
		~FRecipeState() { if (DoneEvent) FPlatformProcess::ReturnSynchEventToPool(DoneEvent); }
	};
	TSharedRef<FRecipeState> State = MakeShared<FRecipeState>();

	IUECPCoreModule::Get().GetLearningService().WebsitePost(TEXT("get-recipe"), Body,
		[State](bool bOk, TSharedPtr<FJsonObject> Response)
		{
			if (bOk && Response.IsValid())
			{
				TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&State->ResultJson);
				FJsonSerializer::Serialize(Response.ToSharedRef(), Writer);
			}
			else
			{
				State->ResultError = TEXT("Recipe lookup failed");
			}
			State->DoneEvent->Trigger();
		});

	State->DoneEvent->Wait(10000);

	if (!State->ResultError.IsEmpty())
	{
		OutError = State->ResultError;
		return;
	}

	if (State->ResultJson.IsEmpty())
	{
		OutJsonString = TEXT("{\"success\":true,\"recipe\":null,\"message\":\"No recipe found.\"}");
		return;
	}

	OutJsonString = State->ResultJson;
}

}
