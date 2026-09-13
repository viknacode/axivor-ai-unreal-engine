// Copyright 2026, BlueprintsLab, All rights reserved

#include "RequestPipeline.h"

#include "MCPToolsLog.h"
#include "UECPMCPBridgeModule.h"
#include "Managers/SettingsManager.h"
#include "Managers/TelemetryManager.h"
#include "UECPCoreModule.h"
#include "Utils/EditorRuntime.h"

#include "Services/IUECPArchitectService.h"
#include "Services/IUECPCrewService.h"
#include "Services/IUECPExtensionService.h"
#include "Services/IUECPLearningService.h"
#include "Services/IUECPNotificationService.h"
#include "Services/IUECPToolDispatcher.h"
#include "Services/UECPCrewChatTokenStore.h"
#include "Services/UECPToolSafety.h"
#include "Types/CallerContext.h"

#include "Async/Async.h"
#include "Async/TaskGraphInterfaces.h"
#include "HAL/PlatformProcess.h"
#include "Misc/ConfigCacheIni.h"
#include "Serialization/JsonSerializer.h"

namespace
{

// Axivor: a confirm prompt that nobody answers (or that cannot be shown at all) must not
// stall the agent. We wait for the single confirm slot, then decide on our own and report
// the reason to the AI so it keeps working instead of retrying a blocked call forever.
struct FUECPConfirmOutcome
{
	IUECPNotificationService::EConfirmDecision Decision = IUECPNotificationService::EConfirmDecision::Proceed;
	bool    bAutoDecided = false;
	FString Reason;
};

FUECPConfirmOutcome AskConfirmWithPolicy(const FString& CommandType, const FString& ArgsPreview, bool bAllowAutoProceed)
{
	using EConfirmDecision = IUECPNotificationService::EConfirmDecision;
	FUECPConfirmOutcome Out;

	int32 TimeoutSecs = 25;
	GConfig->GetInt(TEXT("BpGeneratorUltimate"), TEXT("ConfirmTimeoutSeconds"), TimeoutSecs, FSettingsManager::GetGlobalConfigPath());
	TimeoutSecs = FMath::Clamp(TimeoutSecs, 5, 600);
	bool bProceedWhenUnanswered = true;
	GConfig->GetBool(TEXT("BpGeneratorUltimate"), TEXT("ConfirmProceedWhenUnanswered"), bProceedWhenUnanswered, FSettingsManager::GetGlobalConfigPath());
	const bool bAutoProceed = bProceedWhenUnanswered && bAllowAutoProceed;

	EConfirmDecision Decision = EConfirmDecision::NoSink;
	if (IUECPCoreModule::IsAvailable())
	{
		const double Deadline = FPlatformTime::Seconds() + (double)TimeoutSecs;
		for (;;)
		{
			const double Remaining = Deadline - FPlatformTime::Seconds();
			Decision = IUECPCoreModule::Get().GetNotificationService()
				.RequestDestructiveConfirm(CommandType, ArgsPreview, FMath::Max(1.0, Remaining));
			// Busy = another prompt owns the single confirm slot; wait it out instead of failing.
			if (Decision != EConfirmDecision::Busy || Remaining <= 0.5) break;
			FPlatformProcess::Sleep(0.25f);
		}
	}

	if (Decision == EConfirmDecision::Proceed || Decision == EConfirmDecision::Skip || Decision == EConfirmDecision::Stop)
	{
		Out.Decision = Decision;
		return Out;
	}

	Out.bAutoDecided = true;
	Out.Reason =
		(Decision == EConfirmDecision::Busy)   ? FString(TEXT("another confirmation was still open")) :
		(Decision == EConfirmDecision::NoSink) ? FString(TEXT("no chat window was attached to show the prompt")) :
		                                         FString::Printf(TEXT("nobody answered within %ds"), TimeoutSecs);
	Out.Decision = bAutoProceed ? EConfirmDecision::Proceed : EConfirmDecision::Skip;
	UE_LOG(LogMCPTool, Warning, TEXT("[confirm] '%s' auto-decided (%s): %s"),
		*CommandType, *Out.Reason, bAutoProceed ? TEXT("proceeding") : TEXT("skipped"));
	return Out;
}

bool IsReadOnlyCommand(const FString& CT)        { return UECPToolSafety::IsReadOnly(FName(*CT)); }
bool IsDestructiveCommand(const FString& CT)     { return UECPToolSafety::IsDestructive(FName(*CT)); }
bool IsPlanModeAllowedCommand(const FString& CT) { return UECPToolSafety::IsPlanModeAllowed(FName(*CT)); }

bool IsLicenseSkipTool(const FString& CT)
{
	if (CT.StartsWith(TEXT("git_"))) return true;
	static const TSet<FString> SkipSet = {
		TEXT("ping"),
		TEXT("compile_project"),
		TEXT("edit_cpp_file"),
		TEXT("execute_python"),
		TEXT("search_engine_source"),
		TEXT("get_python_recipe"),
		TEXT("run_command"),
		TEXT("web_search"),
		TEXT("answer"),
		TEXT("mark_learning_step"),
	};
	return SkipSet.Contains(CT);
}

bool TryDispatch(
	const FString& CommandType,
	const TSharedPtr<FJsonObject>& JsonObject,
	FString& ResponseString)
{
	if (!IUECPCoreModule::IsAvailable()) return false;

	IUECPToolDispatcher& Dispatcher = IUECPCoreModule::Get().GetToolDispatcher();
	const FName ToolName(*CommandType);
	if (!Dispatcher.IsRegistered(ToolName))
	{
		IUECPExtensionService& Ext = IUECPCoreModule::Get().GetExtensionService();
		if (TOptional<FUECPExtensionDescriptor> Owner = Ext.FindExtensionByTool(ToolName); Owner.IsSet())
		{
			const EUECPExtensionState State = Ext.GetExtensionState(Owner->ExtensionId);
			if (State != EUECPExtensionState::Loaded)
			{
				TSharedRef<FJsonObject> Err = MakeShared<FJsonObject>();
				Err->SetBoolField(TEXT("success"), false);
				Err->SetStringField(TEXT("error"), FString::Printf(
					TEXT("Tool '%s' is part of the '%s' extension, which is not currently loaded "
					     "(state: %s). Open the editor's Settings → Extensions panel and enable "
					     "'%s' (the editor will offer to enable required plugins and restart). "
					     "Once loaded, retry this tool call."),
					*ToolName.ToString(),
					*Owner->ExtensionId.ToString(),
					LexToString(State),
					*Owner->DisplayName.ToString()));
				Err->SetStringField(TEXT("extension_required"), Owner->ExtensionId.ToString());
				Err->SetStringField(TEXT("extension_state"), LexToString(State));
				TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResponseString);
				FJsonSerializer::Serialize(Err, Writer);
				return true;
			}
		}
		return false;
	}

	FUECPToolResult Result = Dispatcher.ExecuteFromArgs(ToolName, JsonObject);

	if (!Result.bSuccess && Result.ResultJson.IsEmpty())
	{
		TSharedRef<FJsonObject> Err = MakeShared<FJsonObject>();
		Err->SetBoolField(TEXT("success"), false);
		Err->SetStringField(TEXT("error"), Result.ErrorMessage);
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResponseString);
		FJsonSerializer::Serialize(Err, Writer);
	}
	else
	{
		ResponseString = Result.ResultJson;
	}
	return true;
}

}

FMcpPipelineResponse FRequestPipeline::Process(const FMcpPipelineRequest& Request)
{
	FMcpPipelineResponse Response;

	const FString& CommandType = Request.CommandType;
	const TSharedPtr<FJsonObject>& JsonObject = Request.JsonObject;

	if (!EditorReadiness::IsMCPContextValid())
	{
		Response.Body = EditorReadiness::GetContextDeniedMessage(1);
		Response.bDenied = true;
		return Response;
	}

	const bool bCallerClaimsCrew = !Request.CallerChatId.IsEmpty();
	const bool bCrewTokenValid   = bCallerClaimsCrew && IUECPCoreModule::IsAvailable()
		&& FUECPCrewChatTokenStore::Get().ValidateToken(Request.CallerChatId, Request.CallerChatToken);
	if (bCallerClaimsCrew && !bCrewTokenValid)
	{
		UE_LOG(LogMCPTool, Warning,
			TEXT("MCP request claimed crew chat '%s' but token didn't validate — treating as non-crew"),
			*Request.CallerChatId);
	}
	if (bCrewTokenValid)
	{
		if (JsonObject.IsValid())
		{
			JsonObject->SetStringField(UECPCallerContext::CallerChatIdField, Request.CallerChatId);
		}

		FString CrewDeny;
		if (!IUECPCoreModule::Get().GetCrewService().IsToolAllowedForChat(
				Request.CallerChatId, FName(*CommandType), CrewDeny))
		{
			TSharedRef<FJsonObject> Err = MakeShared<FJsonObject>();
			Err->SetBoolField(TEXT("success"), false);
			Err->SetStringField(TEXT("error"),
				FString::Printf(TEXT("crew role denies tool: %s"), *CrewDeny));
			FString OutJson;
			TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJson);
			FJsonSerializer::Serialize(Err, Writer);
			Response.Body = MoveTemp(OutJson);
			Response.bDenied = true;
			return Response;
		}
	}

	const EAIInteractionMode Mode =
		IUECPCoreModule::Get().GetArchitectService().GetActiveInteractionMode();

	if (Mode == EAIInteractionMode::JustChat && !IsReadOnlyCommand(CommandType))
	{
		Response.Body = FString::Printf(
			TEXT("{\"success\":false,\"error\":\"JUST CHAT MODE: '%s' is blocked. Only read and inspect tools (get_*, list_*, find_*, search_*) are allowed.\"}"),
			*CommandType);
		Response.bDenied = true;
		return Response;
	}

	if (Mode == EAIInteractionMode::PlanMode && !IsPlanModeAllowedCommand(CommandType))
	{
		Response.Body = FString::Printf(
			TEXT("{\"success\":false,\"error\":\"PLAN MODE: '%s' is not allowed. Read, inspect, and plan tools only — switch to Auto Edit to create or modify assets.\"}"),
			*CommandType);
		Response.bDenied = true;
		return Response;
	}

	if (Mode == EAIInteractionMode::AutoEdit && IsDestructiveCommand(CommandType) && bCrewTokenValid)
	{
		bool bDestructiveOpsConfirm = true;
		GConfig->GetBool(TEXT("BpGeneratorUltimate"), TEXT("DestructiveOpsConfirm"),
			bDestructiveOpsConfirm, FSettingsManager::GetGlobalConfigPath());
		if (bDestructiveOpsConfirm)
		{
			FString ArgsPreview;
			{
				TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&ArgsPreview);
				FJsonSerializer::Serialize(JsonObject.ToSharedRef(), W);
				W->Close();
			}
			if (ArgsPreview.Len() > 200) ArgsPreview = ArgsPreview.Left(200) + TEXT("...");

			using EConfirmDecision = IUECPNotificationService::EConfirmDecision;
			const FUECPConfirmOutcome Outcome = AskConfirmWithPolicy(CommandType, ArgsPreview,  true);

			if (Outcome.Decision != EConfirmDecision::Proceed)
			{
				const FString Reason = Outcome.bAutoDecided
					? FString::Printf(TEXT("%s, so it was skipped automatically. Do not retry the same call: pick a safer alternative yourself, or say in chat what you need approved"), *Outcome.Reason)
					: FString((Outcome.Decision == EConfirmDecision::Stop)
						? TEXT("the user cancelled the operation. Stop and ask what they want instead")
						: TEXT("the user skipped this tool. Continue without it"));
				Response.Body = FString::Printf(
					TEXT("{\"success\":false,\"error\":\"DESTRUCTIVE OP NOT RUN: '%s' — %s.\"}"),
					*CommandType, *Reason);
				Response.bDenied = true;
				return Response;
			}
		}
	}

	if (Mode == EAIInteractionMode::AskBeforeEdit && bCrewTokenValid && !IsPlanModeAllowedCommand(CommandType))
	{
		FString ArgsPreview;
		{
			TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&ArgsPreview);
			FJsonSerializer::Serialize(JsonObject.ToSharedRef(), W);
			W->Close();
		}
		if (ArgsPreview.Len() > 200) ArgsPreview = ArgsPreview.Left(200) + TEXT("...");

		using EConfirmDecision = IUECPNotificationService::EConfirmDecision;
		const FUECPConfirmOutcome Outcome = AskConfirmWithPolicy(CommandType, ArgsPreview,  false);

		if (Outcome.Decision != EConfirmDecision::Proceed)
		{
			const FString Reason = Outcome.bAutoDecided
				? FString::Printf(TEXT("%s, so it was skipped. Ask Before Edit needs a human yes: keep working on what does not need approval and say in chat which edit is waiting"), *Outcome.Reason)
				: FString((Outcome.Decision == EConfirmDecision::Stop)
					? TEXT("the user cancelled the operation")
					: TEXT("the user skipped this edit"));
			Response.Body = FString::Printf(
				TEXT("{\"success\":false,\"error\":\"EDIT NOT RUN (Ask Before Edit): '%s' — %s.\"}"),
				*CommandType, *Reason);
			Response.bDenied = true;
			return Response;
		}
	}

	if (Request.WriteUnlocked)
	{
		if (IsReadOnlyCommand(CommandType))
		{
			Request.WriteUnlocked->store(true, std::memory_order_relaxed);
		}
		else if (!Request.WriteUnlocked->load(std::memory_order_relaxed))
		{
			UE_LOG(LogTemp, Warning,
				TEXT("MCP Write-Lock: Rejected zombie command '%s' — no read-only command received yet this session"),
				*CommandType);
			Response.Body = TEXT("{\"success\":false,\"error\":\"Rejected: editor was restarted. This tool call is from a previous session. Start a new conversation.\"}");
			Response.bDenied = true;
			return Response;
		}
	}

	if (Request.IsTransportAlive && !Request.IsTransportAlive())
	{
		UE_LOG(LogMCPTool, Log,
			TEXT("MCP: dropping '%s' — client gone (agent stopped or disconnected before dispatch)"),
			*CommandType);
		Response.Body = TEXT("{\"success\":false,\"error\":\"client disconnected\"}");
		Response.bDenied = true;
		return Response;
	}

	const bool bSkipLicenseValidation = IsLicenseSkipTool(CommandType);
	if (!TryDispatch(CommandType, JsonObject, Response.Body))
	{
		Response.Body = FString::Printf(
			TEXT("{\"success\":false, \"error\":\"Unknown command type: %s\"}"),
			*CommandType);
	}

	if (!CommandType.IsEmpty() && !CommandType.Equals(TEXT("ping")))
	{
		IUECPLearningService& LM = IUECPCoreModule::Get().GetLearningService();
		if (LM.IsInitialized())
		{
			// Read the tool's own top-level "success" field rather than substring-matching
			// the whole body: a nested payload (asset dump, log excerpt, transcript) can
			// legitimately contain "success":true/false and skew the heuristic either way.
			bool bToolSuccess = false;
			FString ErrMsg;
			TSharedPtr<FJsonObject> RespObj;
			TSharedRef<TJsonReader<>> RR = TJsonReaderFactory<>::Create(Response.Body);
			if (FJsonSerializer::Deserialize(RR, RespObj) && RespObj.IsValid())
			{
				if (!RespObj->TryGetBoolField(TEXT("success"), bToolSuccess))
				{
					bToolSuccess =
						Response.Body.Contains(TEXT("\"success\":true")) ||
						Response.Body.Contains(TEXT("\"success\": true"));
				}
				if (!bToolSuccess)
				{
					RespObj->TryGetStringField(TEXT("error"), ErrMsg);
				}
			}
			else
			{
				bToolSuccess =
					Response.Body.Contains(TEXT("\"success\":true")) ||
					Response.Body.Contains(TEXT("\"success\": true"));
			}
			LM.TrackToolCompletion(CommandType, JsonObject, bToolSuccess, Response.Body, ErrMsg, TEXT("mcp"));
		}
	}

	if (!IsReadOnlyCommand(CommandType) && !bSkipLicenseValidation)
	{
		// The legacy licence gate (FEditorProfileSync) is disabled — all its predicates
		// are hardcoded true — so the old check could only ever corrupt an already-applied
		// tool result into a false "Internal error" when an unrelated telemetry-cache
		// predicate flickered. Report the telemetry sample; never overwrite the response.
		FTelemetryManager::Get().ReportToolSample(CommandType, Response.Body, true, [](bool){});
	}

	return Response;
}
