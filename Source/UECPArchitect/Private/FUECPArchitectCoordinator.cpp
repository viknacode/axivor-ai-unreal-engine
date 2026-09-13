// Copyright 2026, BlueprintsLab, All rights reserved

#include "FUECPArchitectCoordinator.h"
#include "Misc/EngineVersionComparison.h"
#include "FUECPClaudeAgentLoop.h"
#include "FUECPGeminiAgentLoop.h"
#include "FUECPAgentLoopBase.h"
#include "Services/IUECPCrewService.h"
#include "Types/CrewTypes.h"
#include "SUECPMainWidget.h"
#include "MCP_EditorSubsystem.h"
#include "Widget/UUECPAppBridge.h"
#include "Managers/ChatHistoryManager.h"
#include "Managers/EditorProfileSync.h"
#include "Managers/FreeTierConfigManager.h"
#include "Managers/HttpCommunicationManager.h"
#include "Managers/PlanManager.h"
#include "Managers/UpdateManager.h"
#include "Managers/TaskManager.h"
#include "Managers/SettingsManager.h"
#include "Managers/UpdateManager.h"
#include "Managers/TelemetryManager.h"
#include "ApiKeyManager.h"
#include "Misc/ScopeExit.h"
#include "AssetReferenceManager.h"
#include "Services/IUECPLearningService.h"
#include "UECPCoreModule.h"
#include "UIConfigManager.h"
#include "UECPCoreModule.h"
#include "Services/IUECPArchitectService.h"
#include "Services/IUECPAiMemoryService.h"
#include "Services/IUECPGddService.h"
#include "Services/IUECPUserMcpService.h"
#include "Services/IUECPExtensionService.h"
#include "Services/IUECPToolDispatcher.h"
#include "Services/UECPToolSafety.h"
#include "Mcp/UECPMcpClient.h"
#include "AgentRunnerTypes.h"
#include "Interfaces/IPluginManager.h"
#include "UECPProjectConventions.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ConfigCacheIni.h"
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "HAL/PlatformTime.h"
#include "GenericPlatform/GenericPlatformHttp.h"

TArray<TSharedPtr<FJsonValue>>        FUECPArchitectCoordinator::EmptyHistory;
TArray<TSharedPtr<FConversationInfo>> FUECPArchitectCoordinator::EmptyList;
FString                               FUECPArchitectCoordinator::EmptyChatID;
TMap<FString, FString>                FUECPArchitectCoordinator::EmptyCheatsheetSections;

namespace
{

	bool IsReadOnlyUpstreamMcpTool(const FString& NamespacedName)
	{
		if (NamespacedName.IsEmpty() || !IUECPCoreModule::IsAvailable()) return false;
		const int32 SepIdx = NamespacedName.Find(TEXT("__"));
		if (SepIdx == INDEX_NONE) return false;

		bool bIsUpstream = false;
		const bool bConfirm = IUECPCoreModule::Get().GetUserMcpService()
			.ShouldConfirmUpstreamTool(NamespacedName, bIsUpstream);
		return bIsUpstream && !bConfirm;
	}

	bool IsDestructiveAuthoringTool(const FString& ToolName, const FString& DispStr,
		const FString& Action, const TSharedPtr<FJsonObject>& Args)
	{
		if (UECPToolSafety::IsDestructive(FName(*ToolName)) ||
			UECPToolSafety::IsDestructive(FName(*DispStr)) ||
			UECPToolSafety::IsDestructive(FName(*Action)))
		{
			return true;
		}

		if (ToolName == TEXT("build_blueprint_graph") || DispStr == TEXT("build_blueprint_graph"))
		{
			bool bClearBool = false; FString ClearStr;
			if (Args.IsValid())
			{
				Args->TryGetBoolField(TEXT("clear_before_build"), bClearBool);
				if (!bClearBool) Args->TryGetStringField(TEXT("clear_before_build"), ClearStr);
			}
			if (bClearBool || ClearStr.Equals(TEXT("true"), ESearchCase::IgnoreCase) || ClearStr == TEXT("1"))
			{
				return true;
			}
		}

		if (IUECPCoreModule::IsAvailable())
		{
			IUECPUserMcpService& UserMcp = IUECPCoreModule::Get().GetUserMcpService();
			bool bIsUpstream = false;
			if (UserMcp.ShouldConfirmUpstreamTool(ToolName, bIsUpstream)) return true;
			if (!bIsUpstream && UserMcp.ShouldConfirmUpstreamTool(DispStr, bIsUpstream)) return true;
		}
		return false;
	}

	bool IsReadOnlyArchitectTool(const FString& ToolName, const FString& DispStr, const FString& Action)
	{
		if (UECPToolSafety::IsReadOnly(FName(*ToolName)) ||
			UECPToolSafety::IsReadOnly(FName(*DispStr)) ||
			UECPToolSafety::IsReadOnly(FName(*Action)))
		{
			return true;
		}
		return IsReadOnlyUpstreamMcpTool(ToolName) || IsReadOnlyUpstreamMcpTool(DispStr);
	}

	bool IsPlanModeAllowedArchitectTool(const FString& ToolName, const FString& DispStr, const FString& Action)
	{
		if (UECPToolSafety::IsPlanModeAllowed(FName(*ToolName)) ||
			UECPToolSafety::IsPlanModeAllowed(FName(*DispStr)) ||
			UECPToolSafety::IsPlanModeAllowed(FName(*Action)))
		{
			return true;
		}
		return IsReadOnlyUpstreamMcpTool(ToolName) || IsReadOnlyUpstreamMcpTool(DispStr);
	}
}

void FUECPArchitectCoordinator::InitializeShellRefs(TWeakPtr<SUECPMainWidget> InShell,
	TWeakObjectPtr<UUECPAppBridge> InBridge)
{
	Shell  = InShell;
	Bridge = InBridge;
}

void FUECPArchitectCoordinator::MaybeAutoSaveStreamingChat(const FString& ChatID)
{
	if (ChatID.IsEmpty()) return;

	const TArray<TSharedPtr<FJsonValue>>* Hist = NativeDisplayHistoryByChat.Find(ChatID);
	if (!Hist || Hist->Num() == 0) return;

	const double Now = FPlatformTime::Seconds();
	double& Last = LastStreamSaveTimeByChat.FindOrAdd(ChatID, 0.0);
	if (Now - Last < 2.0) return;
	Last = Now;

	FChatHistoryManager::Get().SaveChatHistory(EConversationViewType::Architect, ChatID, *Hist);
}

bool FUECPArchitectCoordinator::MaybeAutoContinueArchitect(const FString& ChatID, EAIInteractionMode Mode)
{
	if (Mode != EAIInteractionMode::AutoEdit) return false;
	if (ChatID.IsEmpty()) return false;

	int32& Count = ArchitectAutoContinueCountByChat.FindOrAdd(ChatID, 0);
	if (Count >= AutoContinueLimit) return false;
	Count++;

	return FireSyntheticContinue(ChatID, Mode);
}

bool FUECPArchitectCoordinator::FireSyntheticContinue(const FString& ChatID, EAIInteractionMode Mode)
{
	return FireSyntheticUserMessage(ChatID, Mode, TEXT("Continue."));
}

bool FUECPArchitectCoordinator::FireSyntheticUserMessage(const FString& ChatID, EAIInteractionMode Mode, const FString& Text)
{
	TSharedPtr<SUECPMainWidget> W = Shell.Pin();
	if (!W.IsValid()) return false;

	auto AppendUserMessage = [&Text](TArray<TSharedPtr<FJsonValue>>& History)
	{
		TSharedPtr<FJsonObject> Msg = MakeShareable(new FJsonObject);
		Msg->SetStringField(TEXT("role"), TEXT("user"));
		TSharedPtr<FJsonObject> Part = MakeShareable(new FJsonObject);
		Part->SetStringField(TEXT("text"), Text);
		TArray<TSharedPtr<FJsonValue>> Parts;
		Parts.Add(MakeShareable(new FJsonValueObject(Part)));
		Msg->SetArrayField(TEXT("parts"), Parts);
		History.Add(MakeShareable(new FJsonValueObject(Msg)));
	};
	if (W->ActiveArchitectChatID == ChatID)
	{
		AppendUserMessage(W->ArchitectConversationHistory);
		W->RefreshArchitectChatView();
	}
	if (TArray<TSharedPtr<FJsonValue>>* LocalHist = NativeDisplayHistoryByChat.Find(ChatID))
	{
		AppendUserMessage(*LocalHist);
	}

	W->bIsArchitectThinking = true;
	W->ArchitectThinkingChats.Add(ChatID);
	W->PendingArchitectRequests.Add(ChatID);

	bAutoContinuingArchitect = true;

	int32 EffectiveSlot = -1;
	if (IUECPCoreModule::IsAvailable())
	{
		FGuid CrewRunId; FString CrewRoleId;
		IUECPCrewService& Crew = IUECPCoreModule::Get().GetCrewService();
		if (Crew.IsCrewChat(ChatID, CrewRunId, CrewRoleId))
		{
			FCrewRun Run;
			if (Crew.GetRun(CrewRunId, Run))
			{
				for (const FCrewRole& R : Run.Roles)
				{
					if (R.RoleId == CrewRoleId)
					{
						if (R.ApiKeySlotIndex >= 0) EffectiveSlot = R.ApiKeySlotIndex;
						break;
					}
				}
			}
		}
	}
	if (EffectiveSlot < 0)
	{
		if (const int32* PerChat = W->ArchitectApiKeySlotByChat.Find(ChatID))
		{
			if (*PerChat >= 0) EffectiveSlot = *PerChat;
		}
	}

	const int32 OriginalSlotIndex = (EffectiveSlot >= 0)
		? FApiKeyManager::Get().GetActiveSlotIndex()
		: -1;
	if (EffectiveSlot >= 0)
	{
		FApiKeyManager::Get().SetActiveSlot(EffectiveSlot);
	}
	ON_SCOPE_EXIT
	{
		if (OriginalSlotIndex >= 0) FApiKeyManager::Get().SetActiveSlot(OriginalSlotIndex);
	};

	SendChatRequest();
	return true;
}

bool FUECPArchitectCoordinator::RunAutoValidateForChat(const TSharedPtr<SUECPMainWidget>& W,
	const FString& ChatID, bool bActive, EAIInteractionMode Mode)
{
	if (!W.IsValid() || ChatID.IsEmpty()) return false;

	if (IUECPCoreModule::IsAvailable())
	{
		FGuid CrewRunId; FString CrewRoleId;
		if (IUECPCoreModule::Get().GetCrewService().IsCrewChat(ChatID, CrewRunId, CrewRoleId))
		{
			W->ArchitectModifiedBPsThisTurn.Empty();
			return false;
		}
	}

	bool bAutoValidate = true;
	GConfig->GetBool(TEXT("BpGeneratorUltimate"), TEXT("AutoValidateBlueprints"), bAutoValidate, FSettingsManager::GetGlobalConfigPath());
	if (!bAutoValidate) return false;

	if (W->ArchitectModifiedBPsThisTurn.Num() == 0) return false;
	if (!IUECPCoreModule::IsAvailable()) { W->ArchitectModifiedBPsThisTurn.Empty(); return false; }

	IUECPToolDispatcher& D = IUECPCoreModule::Get().GetToolDispatcher();
	if (!D.IsRegistered(TEXT("compile_blueprint"))) { W->ArchitectModifiedBPsThisTurn.Empty(); return false; }

	TArray<FString> Paths = W->ArchitectModifiedBPsThisTurn.Array();
	W->ArchitectModifiedBPsThisTurn.Empty();

	FString Msg = TEXT("[AUTO-VALIDATE] Compiling modified blueprints:\n");
	bool bHadError = false;
	bool bHadSilent = false;
	for (const FString& BpPath : Paths)
	{
		TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>();
		A->SetStringField(TEXT("blueprint_path"), BpPath);
		const FUECPToolResult R = D.ExecuteFromArgs(TEXT("compile_blueprint"), A);
		int32 ErrCount = 0, WarnCount = 0;

		TArray<FString> HealthExecDetails, PruneWarnDetails;
		if (!R.ResultJson.IsEmpty())
		{
			TSharedPtr<FJsonObject> RJ;
			TSharedRef<TJsonReader<>> Rd = TJsonReaderFactory<>::Create(R.ResultJson);
			if (FJsonSerializer::Deserialize(Rd, RJ) && RJ.IsValid())
			{
				RJ->TryGetNumberField(TEXT("error_count"), ErrCount);
				RJ->TryGetNumberField(TEXT("warning_count"), WarnCount);

				const TArray<TSharedPtr<FJsonValue>>* WarnArr = nullptr;
				if (RJ->TryGetArrayField(TEXT("warnings"), WarnArr) && WarnArr)
					for (const TSharedPtr<FJsonValue>& WV : *WarnArr)
					{
						FString WS; if (!WV->TryGetString(WS)) continue;
						if (WS.Contains(TEXT("prune")) || WS.Contains(TEXT("read as default")))
							PruneWarnDetails.Add(WS.TrimStartAndEnd());
					}

				const TArray<TSharedPtr<FJsonValue>>* HArr = nullptr;
				if (RJ->TryGetArrayField(TEXT("health_issues"), HArr) && HArr)
					for (const TSharedPtr<FJsonValue>& HV : *HArr)
					{
						const TSharedPtr<FJsonObject>* HO = nullptr;
						if (!HV->TryGetObject(HO) || !HO) continue;
						FString Issue, Node;
						(*HO)->TryGetStringField(TEXT("issue"), Issue);
						(*HO)->TryGetStringField(TEXT("node"), Node);
						if (Issue.Contains(TEXT("no exec input")) || Issue.Contains(TEXT("never execute")))
							HealthExecDetails.Add(FString::Printf(TEXT("%s — %s"), *Node, *Issue));
					}
			}
		}
		const TArray<FString>& SilentDetails = HealthExecDetails.Num() > 0 ? HealthExecDetails : PruneWarnDetails;

		const bool bErr = (!R.bSuccess || ErrCount > 0);
		const bool bSilent = (SilentDetails.Num() > 0);
		if (bErr)
		{
			bHadError = true;
			Msg += FString::Printf(TEXT("  %s: %d compile error(s)\n"), *BpPath, FMath::Max(ErrCount, 1));
		}
		else if (bSilent)
		{
			bHadSilent = true;
			Msg += FString::Printf(TEXT("  %s: compiles, but %d node(s) PRUNED — output read as default (SILENT failure):\n"), *BpPath, SilentDetails.Num());
			for (const FString& Det : SilentDetails)
				Msg += FString::Printf(TEXT("      - %s\n"), *Det);
		}
		else if (WarnCount > 0)
		{
			Msg += FString::Printf(TEXT("  %s: OK (%d warning(s))\n"), *BpPath, WarnCount);
		}
		else
		{
			Msg += FString::Printf(TEXT("  %s: OK\n"), *BpPath);
		}
	}

	const bool bNeedsFix = bHadError || bHadSilent;

	int32 MaxPasses = 2;
	GConfig->GetInt(TEXT("BpGeneratorUltimate"), TEXT("AutoValidateMaxPasses"), MaxPasses, FSettingsManager::GetGlobalConfigPath());
	MaxPasses = FMath::Clamp(MaxPasses, 0, 5);

	bool bWillContinue = false;
	int32* PassPtr = nullptr;
	if (bNeedsFix && bActive && Mode == EAIInteractionMode::AutoEdit && MaxPasses > 0)
	{
		int32& Pass = ArchitectAutoValidatePassByChat.FindOrAdd(ChatID, 0);
		if (Pass < MaxPasses) { bWillContinue = true; PassPtr = &Pass; }
	}

	if (bWillContinue)
	{
		if (bHadError)
			Msg += TEXT("Fix the compile errors above before completing.\n");
		if (bHadSilent)
			Msg += TEXT("A node above was PRUNED because an impure node's exec input was left unconnected — UE reads its output as a default (0 / null), so the logic silently does nothing even though it compiles. Route the prior node's `then` into the pruned node's `execute` pin (rebuild with clear_before_build=true), or — if it is a simple variable getter — mark it pure via set_function_pure so it needs no exec wire. Then recompile before completing.\n");
	}

	if (bActive)
	{
		TSharedPtr<FJsonObject> ValidateContent = MakeShareable(new FJsonObject);
		ValidateContent->SetStringField(TEXT("role"), TEXT("user"));
		TSharedPtr<FJsonObject> PartText = MakeShareable(new FJsonObject);
		PartText->SetStringField(TEXT("text"), Msg);
		TArray<TSharedPtr<FJsonValue>> Parts; Parts.Add(MakeShareable(new FJsonValueObject(PartText)));
		ValidateContent->SetArrayField(TEXT("parts"), Parts);
		W->ArchitectConversationHistory.Add(MakeShareable(new FJsonValueObject(ValidateContent)));

		if (!bWillContinue && W->AppBridgeObject)
		{
			const int32 MsgIdx = W->ArchitectConversationHistory.Num() - 1;
			const FString Accent    = bNeedsFix ? TEXT("#f85149") : TEXT("#2ea043");
			const FString IconClass = bNeedsFix ? TEXT("error")   : TEXT("success");
			const FString IconSym   = bNeedsFix ? TEXT("&#9888;") : TEXT("&#10003;");
			const FString Esc = Msg.Replace(TEXT("<"), TEXT("&lt;")).Replace(TEXT(">"), TEXT("&gt;"));
			const FString Bubble = FString::Printf(
				TEXT("<div class='tool-result' id='tool-%d' onclick='toggleTool(\"tool-%d\")'>"
					 "<div class='tool-header' style='border-left:3px solid %s;'>"
					 "<div class='tool-icon %s'>%s</div><span class='tool-name'>Auto-Validate</span>"
					 "<span class='tool-chevron'>&#8250;</span></div>"
					 "<div class='tool-content'><pre>%s</pre></div></div>"),
				MsgIdx, MsgIdx, *Accent, *IconClass, *IconSym, *Esc);
			W->AppBridgeObject->PushAppendMessage(TEXT("architect"), Bubble);
		}
	}

	if (bWillContinue)
	{
		(*PassPtr)++;
		return FireSyntheticContinue(ChatID, Mode);
	}
	return false;
}

bool FUECPArchitectCoordinator::RunVerificationGateForChat(const TSharedPtr<SUECPMainWidget>& W,
	const FString& ChatID, bool bActive, EAIInteractionMode Mode)
{
	if (!W.IsValid() || ChatID.IsEmpty() || !bActive) return false;
	if (Mode != EAIInteractionMode::AutoEdit && Mode != EAIInteractionMode::AskBeforeEdit) return false;

	// Crew members report to their orchestrator; the gate is for direct user chats only.
	if (IUECPCoreModule::IsAvailable())
	{
		FGuid CrewRunId; FString CrewRoleId;
		if (IUECPCoreModule::Get().GetCrewService().IsCrewChat(ChatID, CrewRunId, CrewRoleId))
			return false;
	}

	FString Feedback;
	if (!W->BuildArchitectVerificationFeedback(ChatID, Feedback) || Feedback.IsEmpty())
		return false;

	// Same mechanism as the auto-validate "Continue." bounce: the feedback becomes a synthetic
	// user turn that the next SendChatRequest appends to the loop's request history.
	return FireSyntheticUserMessage(ChatID, Mode, Feedback);
}

void FUECPArchitectCoordinator::SendMessage(const FString& Message)
{
	TSharedPtr<SUECPMainWidget> W = Shell.Pin();
	if (!W.IsValid()) return;
	SendMessageToChat(W->ActiveArchitectChatID, Message,  -1);
}

void FUECPArchitectCoordinator::SendMessageInterrupt(const FString& Message)
{
	TSharedPtr<SUECPMainWidget> W = Shell.Pin();
	if (!W.IsValid()) return;

	// Only interrupt when the active chat is actually mid-turn; otherwise StopGeneration
	// would inject a spurious cancel placeholder into an idle conversation.
	const FString ChatID = W->ActiveArchitectChatID;
	const bool bBusy =
		W->ArchitectThinkingChats.Contains(ChatID)
		|| W->PendingArchitectRequests.Contains(ChatID);
	if (bBusy)
	{
		StopGeneration();
	}

	SendMessageToChat(ChatID, Message, -1);
}

void FUECPArchitectCoordinator::StopGenerationForChat(const FString& ChatID)
{
	if (ChatID.IsEmpty()) return;
	if (TSharedPtr<FUECPClaudeAgentLoop> L = FindNativeLoop(ChatID))
	{
		L->Stop();
		NativeLoopsByChat.Remove(ChatID);
	}
	if (TSharedPtr<FUECPOpenAIAgentLoop> L = FindOpenAILoop(ChatID))
	{
		L->Stop();
		OpenAILoopsByChat.Remove(ChatID);
	}
	if (TSharedPtr<FUECPGeminiAgentLoop> L = FindGeminiLoop(ChatID))
	{
		L->Stop();
		GeminiLoopsByChat.Remove(ChatID);
	}
	if (TSharedPtr<SUECPMainWidget> W = Shell.Pin())
	{
		if (W->AgentInstances.Contains(ChatID))
		{
			W->StopAgentInstance(ChatID);
		}
	}
}

bool FUECPArchitectCoordinator::DrainQueueForChat(const FString& ChatID)
{
	TSharedPtr<SUECPMainWidget> W = Shell.Pin();
	if (!W.IsValid() || ChatID.IsEmpty()) return false;
	const TArray<FQueuedArchitectMessage>* Queue = W->ArchitectMessageQueueByChat.Find(ChatID);
	if (!Queue || Queue->Num() == 0) return false;

	W->DrainQueuedArchitectMessage(ChatID);
	return true;
}

void FUECPArchitectCoordinator::StopGeneration()
{
	TSharedPtr<SUECPMainWidget> W = Shell.Pin();
	const FString ChatID = W.IsValid() ? W->ActiveArchitectChatID : FString();
	StopGenerationForChat(ChatID);
	if (W.IsValid())
	{
		W->NativeLoopStreamingChats.Remove(W->ActiveArchitectChatID);
		W->OnStopClicked();
	}
}

void FUECPArchitectCoordinator::StopAllGeneration()
{
	TSet<FString> ChatsToStop;
	for (const auto& Pair : NativeLoopsByChat) ChatsToStop.Add(Pair.Key);
	for (const auto& Pair : OpenAILoopsByChat) ChatsToStop.Add(Pair.Key);
	for (const auto& Pair : GeminiLoopsByChat) ChatsToStop.Add(Pair.Key);

	for (const FString& ChatID : ChatsToStop)
	{
		StopGenerationForChat(ChatID);
	}

	NativeLoopsByChat.Empty();
	OpenAILoopsByChat.Empty();
	GeminiLoopsByChat.Empty();

	if (TSharedPtr<SUECPMainWidget> W = Shell.Pin())
	{
		W->NativeLoopStreamingChats.Empty();
		W->ArchitectThinkingChats.Empty();
		W->bIsArchitectThinking = false;
	}

	UE_LOG(LogTemp, Log, TEXT("[Architect] StopAllGeneration: cancelled %d loop(s)"), ChatsToStop.Num());
}

void FUECPArchitectCoordinator::NewChat()
{
	if (TSharedPtr<SUECPMainWidget> W = Shell.Pin()) W->OnNewArchitectChatClicked();
}

void FUECPArchitectCoordinator::SwitchChat(const FString& ChatID)
{
	TSharedPtr<SUECPMainWidget> W = Shell.Pin();
	if (!W.IsValid()) return;
	for (const TSharedPtr<FConversationInfo>& Info : W->ArchitectConversationList)
	{
		if (Info.IsValid() && Info->ID == ChatID)
		{
			W->OnArchitectChatSelectionChanged(Info, ESelectInfo::OnMouseClick);
			return;
		}
	}
}

void FUECPArchitectCoordinator::SendMessageToChat(const FString& ChatID, const FString& Message, int32 ApiKeySlotIndex)
{
	TSharedPtr<SUECPMainWidget> W = Shell.Pin();
	if (!W.IsValid()) return;

	if (ApiKeySlotIndex < 0 && !ChatID.IsEmpty())
	{
		if (const int32* PerChat = W->ArchitectApiKeySlotByChat.Find(ChatID))
		{
			if (*PerChat >= 0) ApiKeySlotIndex = *PerChat;
		}
	}

	const int32 OriginalSlotIndex = (ApiKeySlotIndex >= 0)
		? FApiKeyManager::Get().GetActiveSlotIndex()
		: -1;
	if (ApiKeySlotIndex >= 0)
	{
		FApiKeyManager::Get().SetActiveSlot(ApiKeySlotIndex);
	}
	ON_SCOPE_EXIT
	{
		if (OriginalSlotIndex >= 0) FApiKeyManager::Get().SetActiveSlot(OriginalSlotIndex);
	};

	if (ChatID.IsEmpty() || W->ActiveArchitectChatID == ChatID)
	{
		W->ArchitectPendingBridgeText = Message;
		W->OnSendArchitectMessageClicked();
		return;
	}

	const FString OriginalActive = W->ActiveArchitectChatID;
	TArray<TSharedPtr<FJsonValue>> OriginalHistory = MoveTemp(W->ArchitectConversationHistory);
	const bool bOriginalSuppress = W->bSuppressChatViewRefresh;
	W->bSuppressChatViewRefresh = true;

	W->ActiveArchitectChatID = ChatID;
	W->ArchitectConversationHistory = FChatHistoryManager::Get().LoadChatHistory(
		EConversationViewType::Architect, ChatID);

	W->ArchitectPendingBridgeText = Message;
	W->OnSendArchitectMessageClicked();

	W->SaveArchitectChatHistory(ChatID);

	W->ActiveArchitectChatID = OriginalActive;
	W->ArchitectConversationHistory = MoveTemp(OriginalHistory);
	W->bIsArchitectThinking =
		W->ArchitectThinkingChats.Contains(OriginalActive)
		|| W->PendingArchitectRequests.Contains(OriginalActive);
	W->bSuppressChatViewRefresh = bOriginalSuppress;

	W->PushArchitectChatListToJs();
	W->RefreshArchitectChatView();
}

void FUECPArchitectCoordinator::BroadcastTurnEnded(const FString& ChatID, bool bSuccess)
{
	TurnEndedEvent.Broadcast(ChatID, bSuccess);
	if (TSharedPtr<SUECPMainWidget> W = Shell.Pin())
	{
		W->PushArchitectChatListToJs();
	}
}

void FUECPArchitectCoordinator::SetChatDisplayName(const FString& ChatID, const FString& DisplayName)
{
	TSharedPtr<SUECPMainWidget> W = Shell.Pin();
	if (!W.IsValid() || ChatID.IsEmpty()) return;

	for (const TSharedPtr<FConversationInfo>& Info : W->ArchitectConversationList)
	{
		if (Info.IsValid() && Info->ID == ChatID)
		{
			Info->Title = DisplayName;
			W->SaveArchitectManifest();
			W->PushArchitectChatListToJs();
			if (W->ArchitectChatListView.IsValid())
			{
				W->ArchitectChatListView->RequestListRefresh();
			}
			return;
		}
	}
}

TArray<FUECPToolCatalogEntry> FUECPArchitectCoordinator::GetVisibleToolCatalog() const
{
	return FUECPAgentLoopBase::GetStandardToolEntries();
}

bool FUECPArchitectCoordinator::RegisterAsyncTask(const FUECPAsyncTaskInfo& Info)
{
	if (Info.TaskId.IsEmpty() || Info.ChatId.IsEmpty() || Info.ToolName.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("Architect: RegisterAsyncTask rejected (missing TaskId/ChatId/ToolName)"));
		return false;
	}
	if (PendingAsyncTasks.Contains(Info.TaskId))
	{
		UE_LOG(LogTemp, Warning, TEXT("Architect: async task %s already registered — refusing duplicate"), *Info.TaskId);
		return false;
	}

	FUECPAsyncTaskInfo Stored = Info;
	if (Stored.RegisteredAt.GetTicks() == 0)
	{
		Stored.RegisteredAt = FDateTime::UtcNow();
	}
	PendingAsyncTasks.Add(Stored.TaskId, Stored);
	UE_LOG(LogTemp, Log, TEXT("Architect: registered async task %s (tool=%s chat=%s)"),
		*Stored.TaskId, *Stored.ToolName, *Stored.ChatId);
	return true;
}

bool FUECPArchitectCoordinator::ResolveAsyncTask(const FString& TaskId, const FString& ResultJsonString, bool bSuccess)
{
	const FUECPAsyncTaskInfo* Found = PendingAsyncTasks.Find(TaskId);
	if (!Found)
	{
		UE_LOG(LogTemp, Warning, TEXT("Architect: ResolveAsyncTask %s — no matching pending task"), *TaskId);
		return false;
	}

	const FUECPAsyncTaskInfo Info = *Found;
	PendingAsyncTasks.Remove(TaskId);

	TSharedPtr<SUECPMainWidget> W = Shell.Pin();
	if (!W.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("Architect: ResolveAsyncTask %s — shell unavailable, dropping"), *TaskId);
		return false;
	}

	const FString Status = bSuccess ? TEXT("success") : TEXT("error");
	const FString Body   = ResultJsonString.IsEmpty() ? TEXT("{}") : ResultJsonString;
	const FString Message = FString::Printf(
		TEXT("[TOOL_RESULT:%s:%s]\nAsync task %s completed.\n```json\n%s\n```"),
		*Info.ToolName, *Status, *Info.TaskId, *Body);

	UE_LOG(LogTemp, Log, TEXT("Architect: resolving async task %s (tool=%s chat=%s status=%s)"),
		*Info.TaskId, *Info.ToolName, *Info.ChatId, *Status);

	SendMessageToChat(Info.ChatId, Message,  -1);
	return true;
}

bool FUECPArchitectCoordinator::CancelAsyncTask(const FString& TaskId)
{
	if (!PendingAsyncTasks.Contains(TaskId)) return false;
	PendingAsyncTasks.Remove(TaskId);
	UE_LOG(LogTemp, Log, TEXT("Architect: cancelled async task %s"), *TaskId);
	return true;
}

TArray<FUECPAsyncTaskInfo> FUECPArchitectCoordinator::GetPendingAsyncTasksForChat(const FString& ChatId) const
{
	TArray<FUECPAsyncTaskInfo> Out;
	for (const auto& Pair : PendingAsyncTasks)
	{
		if (Pair.Value.ChatId == ChatId)
		{
			Out.Add(Pair.Value);
		}
	}
	return Out;
}

void FUECPArchitectCoordinator::AttachImage()
{
	if (TSharedPtr<SUECPMainWidget> W = Shell.Pin()) W->OnAttachArchitectImageClicked();
}

void FUECPArchitectCoordinator::ImportFileContext()
{
	if (TSharedPtr<SUECPMainWidget> W = Shell.Pin()) W->OnImportArchitectContextClicked();
}

void FUECPArchitectCoordinator::SetPendingMessage(const FString& Message)
{
	if (TSharedPtr<SUECPMainWidget> W = Shell.Pin()) W->ArchitectPendingBridgeText = Message;
}

void FUECPArchitectCoordinator::SetInteractionMode(const FString& ModeKey)
{
	TSharedPtr<SUECPMainWidget> W = Shell.Pin();
	if (!W.IsValid()) return;

	if      (ModeKey == TEXT("chat") || ModeKey == TEXT("justchat") || ModeKey == TEXT("just-chat"))
		W->OnJustChatModeClicked(ECheckBoxState::Checked);
	else if (ModeKey == TEXT("ask")  || ModeKey == TEXT("askbeforeedit"))
		W->OnAskBeforeEditModeClicked(ECheckBoxState::Checked);
	else if (ModeKey == TEXT("auto") || ModeKey == TEXT("autoedit"))
		W->OnAutoEditModeClicked(ECheckBoxState::Checked);
	else if (ModeKey == TEXT("plan") || ModeKey == TEXT("planmode"))
		W->OnPlanModeClicked(ECheckBoxState::Checked);
}

void FUECPArchitectCoordinator::SetInteractionModeForChat(const FString& ChatID, const FString& ModeKey)
{
	TSharedPtr<SUECPMainWidget> W = Shell.Pin();
	if (!W.IsValid() || ChatID.IsEmpty()) return;

	EAIInteractionMode Mode = EAIInteractionMode::AutoEdit;
	if      (ModeKey == TEXT("chat") || ModeKey == TEXT("justchat") || ModeKey == TEXT("just-chat"))
		Mode = EAIInteractionMode::JustChat;
	else if (ModeKey == TEXT("ask")  || ModeKey == TEXT("askbeforeedit"))
		Mode = EAIInteractionMode::AskBeforeEdit;
	else if (ModeKey == TEXT("auto") || ModeKey == TEXT("autoedit"))
		Mode = EAIInteractionMode::AutoEdit;
	else if (ModeKey == TEXT("plan") || ModeKey == TEXT("planmode"))
		Mode = EAIInteractionMode::PlanMode;
	else return;

	W->ArchitectInteractionModeByChat.Add(ChatID, Mode);
	for (const TSharedPtr<FConversationInfo>& Info : W->ArchitectConversationList)
	{
		if (Info.IsValid() && Info->ID == ChatID)
		{
			Info->Mode = SUECPMainWidget::InteractionModeToString(Mode);
			break;
		}
	}
	if (W->ActiveArchitectChatID == ChatID)
	{
		W->ArchitectInteractionMode = Mode;
	}
}

void FUECPArchitectCoordinator::ConfirmTool(const FString& Action)
{
	TSharedPtr<SUECPMainWidget> W = Shell.Pin();
	if (!W.IsValid()) return;

	if (bNativeConfirmPending)
	{
		bNativeConfirmPending = false;
		const FString GateChatID = NativeConfirmChatID;
		TSharedPtr<FUECPOpenAIAgentLoop> OAL  = FindOpenAILoop(GateChatID);
		TSharedPtr<FUECPClaudeAgentLoop> NAL  = FindNativeLoop(GateChatID);
		TSharedPtr<FUECPGeminiAgentLoop> GEM  = FindGeminiLoop(GateChatID);

		if (Action == TEXT("proceed"))
		{
			bNativeConfirmAllowed = true;
			if      (OAL.IsValid()) OAL->TriggerPendingSendRound();
			else if (NAL.IsValid()) NAL->TriggerPendingSendRound();
			else if (GEM.IsValid()) GEM->TriggerPendingSendRound();
		}
		else if (Action == TEXT("skip"))
		{
			if      (OAL.IsValid()) OAL->SkipPausedDispatch();
			else if (NAL.IsValid()) NAL->SkipPausedDispatch();
			else if (GEM.IsValid()) GEM->SkipPausedDispatch();
		}
		else if (Action == TEXT("stop"))
		{
			if      (OAL.IsValid()) OAL->Stop();
			else if (NAL.IsValid()) NAL->Stop();
			else if (GEM.IsValid()) GEM->Stop();
		}
		if (!bNativeConfirmPending && Bridge.IsValid())
			Bridge->ExecJs(TEXT("if(typeof onToolConfirmDone==='function')onToolConfirmDone('") + Action + TEXT("')"));
		return;
	}

	if (W->bAgentConfirmPending) { W->OnAgentConfirmAction(Action); return; }
	if      (Action == TEXT("proceed")) W->OnConfirmToolProceed();
	else if (Action == TEXT("skip"))    W->OnConfirmToolSkip();
	else if (Action == TEXT("stop"))    W->OnConfirmToolStop();
}

TOptional<bool> FUECPArchitectCoordinator::ApplyGuideAutopilotGate(SUECPMainWidget* W, const FString& ToolName,
	FName DispatchName, const TSharedPtr<FJsonObject>& Args, const FString& CapChatID)
{
	if (!W) return {};
	const FString DispStr = DispatchName.ToString();
	const bool bIsGuideTool = (ToolName == TEXT("guide") || DispStr == TEXT("guide"));
	if (!bIsGuideTool || !Args.IsValid()) return {};

	FString GuideAction;
	Args->TryGetStringField(TEXT("action"), GuideAction);
	if (GuideAction.IsEmpty()) return {};

	if (GuideAction == TEXT("set_autopilot_mode"))
	{
		bool bEnabled = false;
		Args->TryGetBoolField(TEXT("enabled"), bEnabled);
		if (!bEnabled) GuideAutopilotApprovedChats.Remove(CapChatID);
		return {};
	}

	bool bIsRealInputAction = false;
	if (GuideAction == TEXT("synth_click"))
	{
		bIsRealInputAction = true;
	}
	else if (GuideAction == TEXT("move_cursor"))
	{
		bool bOsCursor = false;
		Args->TryGetBoolField(TEXT("os_cursor"), bOsCursor);
		bIsRealInputAction = bOsCursor;
	}
	if (!bIsRealInputAction) return {};

	const EAIInteractionMode Mode = W->GetActiveArchitectInteractionMode();
	if (Mode == EAIInteractionMode::JustChat) return false;

	if (Mode == EAIInteractionMode::AutoEdit)
	{
		if (GuideAutopilotApprovedChats.Contains(CapChatID))
			return true;
		if (!bNativeConfirmAllowed)
		{
			bNativeConfirmPending = true;
			NativeConfirmChatID = CapChatID;
			FString ArgsPreview;
			TSharedRef<TJsonWriter<>> Wr = TJsonWriterFactory<>::Create(&ArgsPreview);
			FJsonSerializer::Serialize(Args.ToSharedRef(), Wr);
			if (W->AppBridgeObject)
				W->AppBridgeObject->PushToolConfirmation(
					DispStr.IsEmpty() ? ToolName : DispStr,
					ArgsPreview.Left(8000));
			return false;
		}
		bNativeConfirmAllowed = false;
		GuideAutopilotApprovedChats.Add(CapChatID);
		return true;
	}

	return {};
}

void FUECPArchitectCoordinator::SendChatRequest()
{
	TSharedPtr<SUECPMainWidget> W = Shell.Pin();
	if (!W.IsValid()) return;

	if (!W->ActiveArchitectChatID.IsEmpty())
	{
		if (!bAutoContinuingArchitect)
		{
			ArchitectAutoContinueCountByChat.Remove(W->ActiveArchitectChatID);
			ArchitectAutoValidatePassByChat.Remove(W->ActiveArchitectChatID);
		}
	}
	bAutoContinuingArchitect = false;

	auto& _sm = FEditorProfileSync::Get();
	const uint32 _vc = _sm.GetEditorStateHash();
	if (!_sm.IsClearanceSatisfied() || !_sm.IsRefreshFresh() || !(_vc & 0xA3F1)) return;

	if (UMCP_EditorSubsystem* MCP = GEditor ? GEditor->GetEditorSubsystem<UMCP_EditorSubsystem>() : nullptr)
	{
		if (_vc & 0x1B72)
			MCP->bMCPWriteUnlocked.store(true, std::memory_order_relaxed);
	}

	IUECPLearningService& LM = IUECPCoreModule::Get().GetLearningService();
	if (LM.IsInitialized())
	{
		LM.IncrementPromptCount();

		if (W->ArchitectConversationHistory.Num() == 0)
		{
			TSharedPtr<FJsonObject> D = MakeShareable(new FJsonObject);
			FApiKeySlot Slot = FApiKeyManager::Get().GetActiveSlot();
			D->SetStringField(TEXT("provider"), Slot.Provider);
			D->SetStringField(TEXT("model"), Slot.CustomModelName.IsEmpty() ? Slot.Provider : Slot.CustomModelName);
			LM.TrackEvent(TEXT("conversation_started"), D);
		}
	}

	if (!W->ActiveArchitectChatID.IsEmpty())
	{
		FGuid _CrewRunId; FString _CrewRoleId;
		const bool bIsCrewChat = IUECPCoreModule::IsAvailable()
			&& IUECPCoreModule::Get().GetCrewService().IsCrewChat(W->ActiveArchitectChatID, _CrewRunId, _CrewRoleId);
		if (!bIsCrewChat)
		{
			if (const int32* PinnedSlot = W->ArchitectApiKeySlotByChat.Find(W->ActiveArchitectChatID))
			{
				if (*PinnedSlot >= 0 && *PinnedSlot != FApiKeyManager::Get().GetActiveSlotIndex())
					FApiKeyManager::Get().SetActiveSlot(*PinnedSlot);
			}
		}
	}

	FApiKeySlot ActiveSlot = FApiKeyManager::Get().GetActiveSlot();
	FString ApiKeyToUse = ActiveSlot.ApiKey;
	FString ProviderStr = ActiveSlot.Provider;
	FString BaseURL = ActiveSlot.CustomBaseURL;
	bool bIsFreeTierRequest = false;
	FString ModelName = FApiKeyManager::Get().GetActiveModelName();

	if (ProviderStr == TEXT("Free"))
	{
		if (FFreeTierConfigManager::Get().IsBlocked())
		{
			FString BlockMsg = FFreeTierConfigManager::Get().GetBlockMessage();
			TSharedPtr<FJsonObject> ModelContent = MakeShareable(new FJsonObject);
			ModelContent->SetStringField(TEXT("role"), TEXT("model"));
			TArray<TSharedPtr<FJsonValue>> ModelParts;
			TSharedPtr<FJsonObject> ModelPartText = MakeShareable(new FJsonObject);
			ModelPartText->SetStringField(TEXT("text"), BlockMsg);
			ModelParts.Add(MakeShareable(new FJsonValueObject(ModelPartText)));
			ModelContent->SetArrayField(TEXT("parts"), ModelParts);
			W->ArchitectConversationHistory.Add(MakeShareable(new FJsonValueObject(ModelContent)));
			W->bIsArchitectThinking =
				W->ArchitectThinkingChats.Contains(W->ActiveArchitectChatID)
				|| W->PendingArchitectRequests.Contains(W->ActiveArchitectChatID);
			W->SaveArchitectChatHistory(W->ActiveArchitectChatID);
			W->RefreshArchitectChatView();
			return;
		}

		ApiKeyToUse        = FFreeTierConfigManager::Get().GetServiceRegistrationKey();
		BaseURL            = FFreeTierConfigManager::Get().GetActiveSlotEndpoint();
		ModelName          = FFreeTierConfigManager::Get().GetActiveSlotModel();
		ProviderStr        = TEXT("Custom");
		bIsFreeTierRequest = true;
	}

	bool bCustomParamsRequestStream = false;
	if (!ActiveSlot.CustomParams.IsEmpty())
	{
		TSharedPtr<FJsonObject> CustomCheckObj;
		TSharedRef<TJsonReader<>> CheckReader = TJsonReaderFactory<>::Create(ActiveSlot.CustomParams);
		if (FJsonSerializer::Deserialize(CheckReader, CustomCheckObj) && CustomCheckObj.IsValid())
		{
			bool bStreamVal = false;
			if (CustomCheckObj->TryGetBoolField(TEXT("stream"), bStreamVal) && bStreamVal)
				bCustomParamsRequestStream = true;
		}
	}
	const bool bUseStreaming = bCustomParamsRequestStream;

	if (FAgentProviderConfig* AgentConfig = W->GetActiveAgentProvider())
	{
		W->PendingArchitectRequests.Remove(W->ActiveArchitectChatID);

		FString LatestUserMsg;
		for (int32 i = W->ArchitectConversationHistory.Num() - 1; i >= 0; --i)
		{
			const TSharedPtr<FJsonValue>& Val = W->ArchitectConversationHistory[i];
			if (!Val.IsValid() || Val->Type != EJson::Object) continue;
			TSharedPtr<FJsonObject> Obj = Val->AsObject();
			FString Role;
			Obj->TryGetStringField(TEXT("role"), Role);
			if (Role == TEXT("user"))
			{
				const TArray<TSharedPtr<FJsonValue>>* Parts = nullptr;
				if (Obj->TryGetArrayField(TEXT("parts"), Parts) && Parts && Parts->Num() > 0)
				{
					if ((*Parts)[0].IsValid() && (*Parts)[0]->Type == EJson::Object)
						(*Parts)[0]->AsObject()->TryGetStringField(TEXT("text"), LatestUserMsg);
				}
				break;
			}
		}

		if (TSharedPtr<FAgentRunnerInstance> Existing = W->AgentInstances.FindRef(W->ActiveArchitectChatID))
		{
			W->ArchitectThinkingChats.Add(W->ActiveArchitectChatID);
			W->bIsArchitectThinking = true;
			W->SendQueryToInstance(*Existing, *AgentConfig, LatestUserMsg);
			return;
		}

		W->SpawnAgentInstance(W->ActiveArchitectChatID, *AgentConfig, LatestUserMsg);
		return;
	}

	W->PendingArchitectChatID = W->ActiveArchitectChatID;

	W->ActiveArchitectHttpRequest = FHttpModule::Get().CreateRequest();
	W->PendingArchitectRequests.Add(W->ActiveArchitectChatID, W->ActiveArchitectHttpRequest);
	W->ActiveArchitectHttpRequest->SetVerb(TEXT("POST"));
	W->ActiveArchitectHttpRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json"));

	double RequestStartTime = FPlatformTime::Seconds();
	double PromptBuildStartTime = FPlatformTime::Seconds();

	TSharedPtr<FJsonObject> JsonPayload = MakeShareable(new FJsonObject);
	FString RequestBody;

	const FString ActiveChatID = W->ActiveArchitectChatID;
	EAIInteractionMode ChatMode = W->GetActiveArchitectInteractionMode();
	if (const EAIInteractionMode* Found = W->ArchitectInteractionModeByChat.Find(ActiveChatID))
		ChatMode = *Found;

	FString SystemPrompt = BuildFullSystemPrompt(ChatMode, ActiveChatID);

	int32 EstimatedTokens = W->EstimateFullRequestTokens(W->ArchitectConversationHistory);
	double PromptBuildMs = (FPlatformTime::Seconds() - PromptBuildStartTime) * 1000.0;

	FString ArchitectFileContextForAPI;
	for (const TSharedPtr<FJsonValue>& Message : W->ArchitectConversationHistory)
	{
		const TSharedPtr<FJsonObject>& MsgObj = Message->AsObject();
		FString Role;
		if (MsgObj->TryGetStringField(TEXT("role"), Role) && Role == TEXT("context"))
		{
			FString FileName, FileContent;
			MsgObj->TryGetStringField(TEXT("file_name"), FileName);
			MsgObj->TryGetStringField(TEXT("content"), FileContent);
			ArchitectFileContextForAPI += FString::Printf(TEXT("--- File: %s ---\n%s\n\n"), *FileName, *FileContent);
		}
	}

	static constexpr int32 MaxArchitectHistoryChars = 320000;
	const TArray<TSharedPtr<FJsonValue>> WindowedHistory =
		SUECPMainWidget::WindowConversationHistoryForExtraction(W->ArchitectConversationHistory, MaxArchitectHistoryChars);

	{
		FString OAUrl;
		bool bIsOACompat = false;

		if (ProviderStr == TEXT("OpenAI"))
		{
			bIsOACompat = true;
			OAUrl = FHttpCommunicationManager::BuildOpenAIUrl();
		}
		else if (ProviderStr == TEXT("DeepSeek"))
		{
			bIsOACompat = true;
			OAUrl = FHttpCommunicationManager::BuildDeepSeekUrl();
		}
		else if (ProviderStr == TEXT("Custom"))
		{
			FString ComputedURL = FHttpCommunicationManager::BuildCustomUrl(BaseURL);
			if (!ComputedURL.Contains(TEXT("anthropic.com")))
			{
				bIsOACompat = true;
				OAUrl = ComputedURL;
			}
		}

		if (bIsOACompat)
		{
			W->ActiveArchitectHttpRequest = nullptr;
			W->PendingArchitectRequests.Remove(ActiveChatID);

			if (TSharedPtr<FUECPOpenAIAgentLoop> PrevOA = FindOpenAILoop(ActiveChatID))
			{
				PrevOA->Stop();
				OpenAILoopsByChat.Remove(ActiveChatID);
			}
			if (TSharedPtr<FUECPClaudeAgentLoop> PrevNL = FindNativeLoop(ActiveChatID))
			{
				PrevNL->Stop();
				NativeLoopsByChat.Remove(ActiveChatID);
			}

			TArray<TSharedPtr<FJsonValue>>& OAHistory = OpenAIHistoryByChat.FindOrAdd(ActiveChatID);
			if (OAHistory.IsEmpty())
			{
				bool bFileContextAdded = false;
				for (const TSharedPtr<FJsonValue>& Entry : WindowedHistory)
				{
					TArray<TSharedPtr<FJsonValue>> Converted =
						FUECPOpenAIAgentLoop::ConvertHistoryEntryToOpenAI(Entry);

					if (!bFileContextAdded && !ArchitectFileContextForAPI.IsEmpty())
					{
						for (TSharedPtr<FJsonValue>& CV : Converted)
						{
							TSharedPtr<FJsonObject> CVObj = CV.IsValid() ? CV->AsObject() : nullptr;
							FString CVRole;
							if (!CVObj || !CVObj->TryGetStringField(TEXT("role"), CVRole) || CVRole != TEXT("user"))
								continue;
							FString Txt;
							if (CVObj->TryGetStringField(TEXT("content"), Txt))
							{
								CVObj->SetStringField(TEXT("content"), ArchitectFileContextForAPI + Txt);
								bFileContextAdded = true;
							}
							break;
						}
					}
					OAHistory.Append(Converted);
				}
			}
			else
			{
				for (int32 i = WindowedHistory.Num() - 1; i >= 0; --i)
				{
					TSharedPtr<FJsonObject> EntryObj = WindowedHistory[i].IsValid() ? WindowedHistory[i]->AsObject() : nullptr;
					FString EntryRole;
					if (!EntryObj || !EntryObj->TryGetStringField(TEXT("role"), EntryRole) || EntryRole != TEXT("user"))
						continue;
					TArray<TSharedPtr<FJsonValue>> Converted =
						FUECPOpenAIAgentLoop::ConvertHistoryEntryToOpenAI(WindowedHistory[i]);
					OAHistory.Append(Converted);
					break;
				}
			}

			FString OAProviderName = bIsFreeTierRequest ? TEXT("the free tier")
				: (ProviderStr == TEXT("DeepSeek") ? TEXT("DeepSeek") : TEXT("OpenAI"));

			TSharedPtr<FUECPOpenAIAgentLoop> Loop = MakeShared<FUECPOpenAIAgentLoop>();
			Loop->ApiKey        = ApiKeyToUse;
			Loop->EndpointURL   = OAUrl;
			Loop->ModelName     = ModelName.TrimStartAndEnd().IsEmpty() ? TEXT("gpt-4o") : ModelName.TrimStartAndEnd();
			Loop->ProviderName  = OAProviderName;
			Loop->SystemPrompt  = BuildFullSystemPrompt(ChatMode, ActiveChatID);
			Loop->ChatID        = ActiveChatID;
			Loop->InteractionMode = ChatMode;
			Loop->History       = OAHistory;
			{ int32 R = 200; GConfig->GetInt(TEXT("BpGeneratorUltimate"), TEXT("MaxAgentRounds"), R, GEditorIni); Loop->MaxRounds = R > 0 ? R : 200; }

			if (bIsFreeTierRequest)
			{
				FString LicTok = FEditorProfileSync::Get().GetSyncKey();
				if (!LicTok.IsEmpty())
					Loop->ExtraHeaders.Add(TEXT("x-license-token"), LicTok);
			}

			OpenAILoopsByChat.Add(ActiveChatID, Loop);
			NativeLoopToolBubbleMapByChat.FindOrAdd(ActiveChatID).Reset();
			NativeDisplayHistoryByChat.Add(ActiveChatID, W->ArchitectConversationHistory);

			W->NativeLoopStreamingChats.Add(ActiveChatID);
			W->PendingArchitectRequests.Add(ActiveChatID, nullptr);

			TWeakPtr<FUECPOpenAIAgentLoop> WeakLoop = Loop;
			TWeakPtr<SUECPMainWidget>      WeakW    = Shell;
			FString                        CapChatID = ActiveChatID;

			Loop->OnRoundResponse = [WeakW, CapChatID, ProviderStr](FHttpResponsePtr Resp)
			{
				TSharedPtr<SUECPMainWidget> W2 = WeakW.Pin();
				if (!W2.IsValid()) return;
				W2->ParseAndPushFreeTierRateLimit(Resp);
				W2->ParseAndCacheProviderRateLimit(Resp, ProviderStr);
			};

			Loop->OnTextDelta = [WeakW, CapChatID, this](const FString& Chunk)
			{
				TSharedPtr<SUECPMainWidget> W2 = WeakW.Pin();
				if (!W2.IsValid()) return;
				TArray<TSharedPtr<FJsonValue>>* LocalHist = NativeDisplayHistoryByChat.Find(CapChatID);
				if (!LocalHist) return;
				const bool bActive = W2->ActiveArchitectChatID == CapChatID;
				if (LocalHist->Num() > 0)
				{
					TSharedPtr<FJsonObject> LastObj = LocalHist->Last()->AsObject();
					FString LastRole;
					LastObj->TryGetStringField(TEXT("role"), LastRole);
					if (LastRole == TEXT("model"))
					{
						const TArray<TSharedPtr<FJsonValue>>* Parts = nullptr;
						if (LastObj->TryGetArrayField(TEXT("parts"), Parts) && Parts->Num() > 0)
						{
							TSharedPtr<FJsonObject> Part = (*Parts)[0]->AsObject();
							FString Cur; Part->TryGetStringField(TEXT("text"), Cur);
							Part->SetStringField(TEXT("text"), Cur + Chunk);
						}
						if (bActive) W2->RefreshArchitectChatView();
						MaybeAutoSaveStreamingChat(CapChatID);
						return;
					}
				}
				TSharedPtr<FJsonObject> Msg = MakeShareable(new FJsonObject);
				Msg->SetStringField(TEXT("role"), TEXT("model"));
				TSharedPtr<FJsonObject> Part = MakeShareable(new FJsonObject);
				Part->SetStringField(TEXT("text"), Chunk);
				TArray<TSharedPtr<FJsonValue>> Parts;
				Parts.Add(MakeShareable(new FJsonValueObject(Part)));
				Msg->SetArrayField(TEXT("parts"), Parts);
				TSharedPtr<FJsonValue> MsgVal = MakeShareable(new FJsonValueObject(Msg));
				LocalHist->Add(MsgVal);
				if (bActive)
				{
					W2->ArchitectConversationHistory.Add(MsgVal);
					W2->RefreshArchitectChatView();
				}
				MaybeAutoSaveStreamingChat(CapChatID);
			};

			Loop->OnToolStart = [WeakW, CapChatID, this]
				(const FString& ToolId, const FString& Label, const FString& ArgsPreview)
			{
				TSharedPtr<SUECPMainWidget> W2 = WeakW.Pin();
				if (!W2.IsValid()) return;
				TArray<TSharedPtr<FJsonValue>>* LocalHist = NativeDisplayHistoryByChat.Find(CapChatID);
				if (!LocalHist) return;
				FString Text = FString::Printf(TEXT("[TOOL_RESULT:%s:success]\n%s"), *Label, *ArgsPreview);
				TSharedPtr<FJsonObject> Msg = MakeShareable(new FJsonObject);
				Msg->SetStringField(TEXT("role"), TEXT("user"));
				TSharedPtr<FJsonObject> Part = MakeShareable(new FJsonObject);
				Part->SetStringField(TEXT("text"), Text);
				TArray<TSharedPtr<FJsonValue>> Parts;
				Parts.Add(MakeShareable(new FJsonValueObject(Part)));
				Msg->SetArrayField(TEXT("parts"), Parts);
				TSharedPtr<FJsonValue> MsgVal = MakeShareable(new FJsonValueObject(Msg));
				LocalHist->Add(MsgVal);
				NativeLoopToolBubbleMapByChat.FindOrAdd(CapChatID).Add(ToolId, LocalHist->Num() - 1);
				if (W2->ActiveArchitectChatID == CapChatID)
				{
					W2->ArchitectConversationHistory.Add(MsgVal);
					W2->RefreshArchitectChatView();
				}
				LastStreamSaveTimeByChat.Remove(CapChatID);
				MaybeAutoSaveStreamingChat(CapChatID);
			};

			Loop->OnBeforeDispatch = [WeakW, CapChatID, this]
				(const FString& ToolName, const TSharedPtr<FJsonObject>& Args, FName DispatchName, FString& OutDenyReason) -> bool
			{
				TSharedPtr<SUECPMainWidget> W2 = WeakW.Pin();
				if (!W2.IsValid()) return true;

				static const TSet<FString> GraphModTools = {
					TEXT("build_blueprint_graph"), TEXT("clear_blueprint_graph"),
					TEXT("place_node"), TEXT("connect_pins"), TEXT("set_pin_default"),
					TEXT("remove_node"), TEXT("delete_nodes")
				};
				FString Action;
				if (Args.IsValid()) Args->TryGetStringField(TEXT("action"), Action);
				const FString DispStr = DispatchName.ToString();
				if (GraphModTools.Contains(ToolName) || GraphModTools.Contains(Action) || GraphModTools.Contains(DispStr))
				{
					FString BpPath;
					if (Args.IsValid() && Args->TryGetStringField(TEXT("blueprint_path"), BpPath))
					{
						W2->CreateBlueprintDiffSnapshot(BpPath);
					}
				}

				if (TOptional<bool> GuideDecision = ApplyGuideAutopilotGate(W2.Get(), ToolName, DispatchName, Args, CapChatID))
					return *GuideDecision;

				bool bDestructiveConfirm = true;
				GConfig->GetBool(TEXT("BpGeneratorUltimate"), TEXT("DestructiveOpsConfirm"), bDestructiveConfirm, FSettingsManager::GetGlobalConfigPath());
		{ bool bAxTurbo = false; GConfig->GetBool(TEXT("BpGeneratorUltimate"), TEXT("TurboMode"), bAxTurbo, FSettingsManager::GetGlobalConfigPath()); if (bAxTurbo) bDestructiveConfirm = false; } // Axivor Turbo: no destructive confirmations
				if (IUECPCoreModule::IsAvailable()
					&& IUECPCoreModule::Get().GetCrewService().ShouldAutoApproveDestructiveForChat(CapChatID))
				{
					bDestructiveConfirm = false;
				}
				if (bDestructiveConfirm && W2->GetArchitectInteractionModeForChat(CapChatID) == EAIInteractionMode::AutoEdit)
				{
					if (IsDestructiveAuthoringTool(ToolName, DispStr, Action, Args))
					{
						if (!bNativeConfirmAllowed)
						{
							bNativeConfirmPending = true;
							NativeConfirmChatID = CapChatID;
							FString ArgsPreview;
							if (Args.IsValid()) { TSharedRef<TJsonWriter<>> Wr = TJsonWriterFactory<>::Create(&ArgsPreview); FJsonSerializer::Serialize(Args.ToSharedRef(), Wr); }
							if (W2->AppBridgeObject)
								W2->AppBridgeObject->PushToolConfirmation(DispStr.IsEmpty() ? ToolName : DispStr, ArgsPreview.Left(8000));
							return false;
						}
						bNativeConfirmAllowed = false;
					}
				}

				{
					const EAIInteractionMode CurMode = W2->GetArchitectInteractionModeForChat(CapChatID);
					if (CurMode == EAIInteractionMode::JustChat)
					{
						if (!IsReadOnlyArchitectTool(ToolName, DispStr, Action))
						{
							OutDenyReason = FString::Printf(
								TEXT("{\"error\":\"Tool blocked: '%s' is an authoring tool and the chat is in Just Chat mode (read-only). Switch the mode to Auto Edit or Ask Before Edit to run this. Read-only tools (get_*, list_*, find_*, search_*, validate_*, get_tool_docs, classify_intent) are still available.\"}"),
								DispStr.IsEmpty() ? *ToolName : *DispStr);
							return false;
						}
					}
					if (CurMode == EAIInteractionMode::PlanMode)
					{
						if (!IsPlanModeAllowedArchitectTool(ToolName, DispStr, Action))
						{
							OutDenyReason = FString::Printf(
								TEXT("{\"error\":\"PLAN MODE: '%s' is plan-only. Finish the plan, ask the user 'Shall I proceed?', then call proceed_with_plan to switch into execution mode and build. Read/inspect tools + project_plan / memory / ask_user are allowed here.\"}"),
								DispStr.IsEmpty() ? *ToolName : *DispStr);
							return false;
						}
					}
					if (CurMode == EAIInteractionMode::AskBeforeEdit)
					{
						const bool bNeedsEditConfirm = !IsPlanModeAllowedArchitectTool(ToolName, DispStr, Action);
						if (bNeedsEditConfirm)
						{
							if (!bNativeConfirmAllowed)
							{
								bNativeConfirmPending = true;
								NativeConfirmChatID = CapChatID;
								FString ArgsPreview;
								if (Args.IsValid()) { TSharedRef<TJsonWriter<>> Wr = TJsonWriterFactory<>::Create(&ArgsPreview); FJsonSerializer::Serialize(Args.ToSharedRef(), Wr); }
								if (W2->AppBridgeObject)
									W2->AppBridgeObject->PushToolConfirmation(DispStr.IsEmpty() ? ToolName : DispStr, ArgsPreview.Left(8000));
								return false;
							}
							bNativeConfirmAllowed = false;
						}
					}
				}
				return true;
			};

			Loop->OnBeforeNextRound = [this]() -> bool { return !bNativeConfirmPending; };

			Loop->OnWidgetTool = [WeakW](const FString& Name, const TSharedPtr<FJsonObject>& Args, FUECPToolResult& Out) -> bool
			{
				TSharedPtr<SUECPMainWidget> W2 = WeakW.Pin();
				if (!W2.IsValid()) return false;
				FToolExecutionResult ExecResult;
				if (!W2->TryDispatchWidgetOwnedTool(Name, Args, ExecResult)) return false;
				Out.bSuccess    = ExecResult.bSuccess;
				Out.ResultJson  = ExecResult.ResultJson;
				Out.SummaryJson = ExecResult.SummaryJson;
				Out.ErrorMessage = ExecResult.ErrorMessage;
				return true;
			};

			Loop->OnToolResult = [WeakW, CapChatID, this]
				(const FString& ToolId, const FString& Label, bool bSuccess, const FString& ResultJson)
			{
				TSharedPtr<SUECPMainWidget> W2 = WeakW.Pin();
				if (!W2.IsValid()) return;
				W2->NoteArchitectToolResultForVerification(Label, bSuccess, ResultJson);
				TArray<TSharedPtr<FJsonValue>>* LocalHist = NativeDisplayHistoryByChat.Find(CapChatID);
				if (!LocalHist) return;
				TMap<FString, int32>* BubbleMap = NativeLoopToolBubbleMapByChat.Find(CapChatID);
				const int32* FoundIdx = BubbleMap ? BubbleMap->Find(ToolId) : nullptr;
				if (FoundIdx && *FoundIdx < LocalHist->Num())
				{
					TSharedPtr<FJsonObject> Obj = (*LocalHist)[*FoundIdx]->AsObject();
					const TArray<TSharedPtr<FJsonValue>>* Parts = nullptr;
					if (Obj.IsValid() && Obj->TryGetArrayField(TEXT("parts"), Parts) && Parts->Num() > 0)
					{
						TSharedPtr<FJsonObject> Part = (*Parts)[0]->AsObject();
						FString Cur; Part->TryGetStringField(TEXT("text"), Cur);
						if (!bSuccess) Cur = Cur.Replace(TEXT(":success]"), TEXT(":error]"));
						Part->SetStringField(TEXT("text"), Cur + TEXT("\n---\n") + ResultJson);
					}
					if (W2->ActiveArchitectChatID == CapChatID)
					{
						FString RenderedHtml = W2->RenderSingleArchitectMessageHtml(Obj, *FoundIdx);
						if (!RenderedHtml.IsEmpty() && W2->AppBridgeObject)
						{
							RenderedHtml = FString::Printf(TEXT("<div data-hist-idx='%d'>%s</div>"), *FoundIdx, *RenderedHtml);
							FString Enc = FGenericPlatformHttp::UrlEncode(RenderedHtml);
							W2->AppBridgeObject->ExecJs(FString::Printf(
								TEXT("if(typeof updateArchitectMsgAt==='function')updateArchitectMsgAt(%d,decodeURIComponent('%s'))"),
								*FoundIdx, *Enc));
						}
					}
				}
				FTelemetryManager::Get().ReportToolSample(Label, ResultJson, bSuccess, [](bool) {});
				if (W2->ActiveArchitectChatID == CapChatID)
				{
					W2->RefreshArchitectChatView();
					W2->PushArchitectTokenCount(W2->EstimateFullRequestTokens(W2->ArchitectConversationHistory));
				}
				LastStreamSaveTimeByChat.Remove(CapChatID);
				MaybeAutoSaveStreamingChat(CapChatID);
			};

			Loop->OnDone = [WeakW, WeakLoop, CapChatID, this](bool bSuccess, const FString& ErrorMsg)
			{
				TSharedPtr<SUECPMainWidget> W2 = WeakW.Pin();
				if (!W2.IsValid()) return;
				W2->NativeLoopStreamingChats.Remove(CapChatID);
				bool bStalledNeedsContinue = false;
				if (TSharedPtr<FUECPOpenAIAgentLoop> L = WeakLoop.Pin())
				{
					OpenAIHistoryByChat.Add(CapChatID, L->History);
					if (L->FinalInputTokens > 0 || L->FinalOutputTokens > 0)
						W2->UpdateConversationTokens(CapChatID, L->FinalInputTokens, L->FinalOutputTokens);
					bStalledNeedsContinue = L->bStalledNeedsContinue;
				}
				if (FindOpenAILoop(CapChatID) == WeakLoop.Pin()) OpenAILoopsByChat.Remove(CapChatID);
				NativeLoopToolBubbleMapByChat.Remove(CapChatID);

				TArray<TSharedPtr<FJsonValue>>* LocalHist = NativeDisplayHistoryByChat.Find(CapChatID);
				if (!ErrorMsg.IsEmpty())
				{
					TSharedPtr<FJsonObject> ErrMsg = MakeShareable(new FJsonObject);
					ErrMsg->SetStringField(TEXT("role"), TEXT("model"));
					ErrMsg->SetBoolField(TEXT("is_error"),
						!bSuccess && !ErrorMsg.Contains(TEXT("round"), ESearchCase::IgnoreCase));
					TSharedPtr<FJsonObject> Part = MakeShareable(new FJsonObject);
					Part->SetStringField(TEXT("text"), ErrorMsg);
					TArray<TSharedPtr<FJsonValue>> Parts;
					Parts.Add(MakeShareable(new FJsonValueObject(Part)));
					ErrMsg->SetArrayField(TEXT("parts"), Parts);
					TSharedPtr<FJsonValue> ErrVal = MakeShareable(new FJsonValueObject(ErrMsg));
					if (LocalHist) LocalHist->Add(ErrVal);
					if (W2->ActiveArchitectChatID == CapChatID)
						W2->ArchitectConversationHistory.Add(ErrVal);
				}
				W2->ArchitectThinkingChats.Remove(CapChatID);
				W2->PendingArchitectRequests.Remove(CapChatID);
				W2->bIsArchitectThinking =
					W2->ArchitectThinkingChats.Contains(W2->ActiveArchitectChatID)
					|| W2->PendingArchitectRequests.Contains(W2->ActiveArchitectChatID);

				const bool bActive = W2->ActiveArchitectChatID == CapChatID;
				if (bActive)
				{
					W2->MaybeFinalizeCompactReplacement(CapChatID);
					W2->SaveArchitectChatHistory(CapChatID);
				}
				else if (LocalHist && LocalHist->Num() > 0)
				{
					FChatHistoryManager::Get().SaveChatHistory(
						EConversationViewType::Architect, CapChatID, *LocalHist);
				}
				NativeDisplayHistoryByChat.Remove(CapChatID);

				if (bActive)
				{
					W2->RefreshArchitectChatView();
					if (W2->DiffSnapshots.Num() > 0)
					{
						W2->PendingDiffHtml = W2->BuildDiffBarHtml();
						if (!W2->PendingDiffHtml.IsEmpty())
						{
							W2->SaveDiffSnapshotsToDisk();
							if (W2->AppBridgeObject)
								W2->AppBridgeObject->PushAppendMessage(TEXT("architect"), W2->PendingDiffHtml);
							W2->PendingDiffHtml.Empty();
						}
					}
					W2->PushArchitectTokenCount(W2->EstimateFullRequestTokens(W2->ArchitectConversationHistory));
				}

				bool bAutoContinued = false;
				if (RunAutoValidateForChat(W2, CapChatID, bActive, W2->GetArchitectInteractionModeForChat(CapChatID)))
					bAutoContinued = true;
				if (!bAutoContinued && bSuccess)
					bAutoContinued = RunVerificationGateForChat(W2, CapChatID, bActive, W2->GetArchitectInteractionModeForChat(CapChatID));
				if (!bAutoContinued && bStalledNeedsContinue && bActive)
				{
					bAutoContinued = MaybeAutoContinueArchitect(
						CapChatID, W2->GetArchitectInteractionModeForChat(CapChatID));
				}

				if (W2->AppBridgeObject && bActive && !W2->bIsArchitectThinking && !bAutoContinued)
					W2->AppBridgeObject->ExecJs(TEXT("if(typeof onGenerationDone==='function')onGenerationDone('architect')"));

				BroadcastTurnEnded(CapChatID, bSuccess);

				if (!bAutoContinued)
					W2->DrainQueuedArchitectMessage(CapChatID);
			};

			Loop->Start();
			return;
		}
	}

	if (ProviderStr == TEXT("Gemini"))
	{
		W->ActiveArchitectHttpRequest = nullptr;
		W->PendingArchitectRequests.Remove(ActiveChatID);

		if (TSharedPtr<FUECPGeminiAgentLoop> Prev = FindGeminiLoop(ActiveChatID))
		{
			Prev->Stop();
			GeminiLoopsByChat.Remove(ActiveChatID);
		}
		if (TSharedPtr<FUECPClaudeAgentLoop> Prev = FindNativeLoop(ActiveChatID))
		{
			Prev->Stop();
			NativeLoopsByChat.Remove(ActiveChatID);
		}
		if (TSharedPtr<FUECPOpenAIAgentLoop> Prev = FindOpenAILoop(ActiveChatID))
		{
			Prev->Stop();
			OpenAILoopsByChat.Remove(ActiveChatID);
		}

		TArray<TSharedPtr<FJsonValue>>& GHistory = GeminiHistoryByChat.FindOrAdd(ActiveChatID);
		if (GHistory.IsEmpty())
		{
			bool bFileContextAdded = false;
			for (const TSharedPtr<FJsonValue>& Entry : WindowedHistory)
			{
				TSharedPtr<FJsonValue> Filtered = FUECPGeminiAgentLoop::FilterHistoryEntry(Entry);
				if (!Filtered.IsValid()) continue;

				if (!bFileContextAdded && !ArchitectFileContextForAPI.IsEmpty())
				{
					TSharedPtr<FJsonObject> EObj = Filtered->AsObject();
					FString ERole;
					if (EObj.IsValid() && EObj->TryGetStringField(TEXT("role"), ERole) && ERole == TEXT("user"))
					{
						const TArray<TSharedPtr<FJsonValue>>* EParts = nullptr;
						if (EObj->TryGetArrayField(TEXT("parts"), EParts) && EParts && EParts->Num() > 0)
						{
							TSharedPtr<FJsonObject> P = (*EParts)[0]->AsObject();
							FString T;
							if (P.IsValid() && P->TryGetStringField(TEXT("text"), T))
							{
								P->SetStringField(TEXT("text"), ArchitectFileContextForAPI + T);
								bFileContextAdded = true;
							}
						}
					}
				}
				GHistory.Add(Filtered);
			}
		}
		else
		{
			for (int32 i = WindowedHistory.Num() - 1; i >= 0; --i)
			{
				TSharedPtr<FJsonValue> Filtered = FUECPGeminiAgentLoop::FilterHistoryEntry(WindowedHistory[i]);
				if (!Filtered.IsValid()) continue;
				TSharedPtr<FJsonObject> EObj = Filtered->AsObject();
				FString ERole;
				if (!EObj || !EObj->TryGetStringField(TEXT("role"), ERole) || ERole != TEXT("user")) continue;
				GHistory.Add(Filtered);
				break;
			}
		}

		FString GeminiModel = FApiKeyManager::Get().GetActiveGeminiModel();
		FString GeminiUrl   = FString::Printf(
			TEXT("https://generativelanguage.googleapis.com/v1beta/models/%s:streamGenerateContent?key=%s&alt=sse"),
			*GeminiModel, *ApiKeyToUse);

		int32 GMaxRounds = 200;
		GConfig->GetInt(TEXT("BpGeneratorUltimate"), TEXT("MaxAgentRounds"), GMaxRounds, GEditorIni);
		if (GMaxRounds <= 0) GMaxRounds = 200;

		TSharedPtr<FUECPGeminiAgentLoop> GLoop = MakeShared<FUECPGeminiAgentLoop>();
		GLoop->ApiKey       = ApiKeyToUse;
		GLoop->EndpointURL  = GeminiUrl;
		GLoop->ModelName    = GeminiModel;
		GLoop->SystemPrompt = BuildFullSystemPrompt(ChatMode, ActiveChatID);
		GLoop->ChatID       = ActiveChatID;
		GLoop->InteractionMode = ChatMode;
		GLoop->History      = GHistory;
		GLoop->MaxRounds    = GMaxRounds;

		GeminiLoopsByChat.Add(ActiveChatID, GLoop);
		NativeLoopToolBubbleMapByChat.FindOrAdd(ActiveChatID).Reset();
		NativeDisplayHistoryByChat.Add(ActiveChatID, W->ArchitectConversationHistory);

		W->NativeLoopStreamingChats.Add(ActiveChatID);
		W->PendingArchitectRequests.Add(ActiveChatID, nullptr);

		TWeakPtr<FUECPGeminiAgentLoop> WeakGLoop = GLoop;
		TWeakPtr<SUECPMainWidget>       WeakWG    = Shell;
		FString                         GCapChatID = ActiveChatID;

		GLoop->OnRoundResponse = [WeakWG, GCapChatID, ProviderStr](FHttpResponsePtr Resp)
		{
			TSharedPtr<SUECPMainWidget> W2 = WeakWG.Pin();
			if (!W2.IsValid()) return;
			W2->ParseAndPushFreeTierRateLimit(Resp);
			W2->ParseAndCacheProviderRateLimit(Resp, ProviderStr);
		};

		GLoop->OnTextDelta = [WeakWG, GCapChatID, this](const FString& Chunk)
		{
			TSharedPtr<SUECPMainWidget> W2 = WeakWG.Pin();
			if (!W2.IsValid()) return;
			TArray<TSharedPtr<FJsonValue>>* LocalHist = NativeDisplayHistoryByChat.Find(GCapChatID);
			if (!LocalHist) return;
			const bool bActive = W2->ActiveArchitectChatID == GCapChatID;
			if (LocalHist->Num() > 0)
			{
				TSharedPtr<FJsonObject> LastObj = LocalHist->Last()->AsObject();
				FString LastRole;
				LastObj->TryGetStringField(TEXT("role"), LastRole);
				if (LastRole == TEXT("model"))
				{
					const TArray<TSharedPtr<FJsonValue>>* Parts = nullptr;
					if (LastObj->TryGetArrayField(TEXT("parts"), Parts) && Parts->Num() > 0)
					{
						TSharedPtr<FJsonObject> Part = (*Parts)[0]->AsObject();
						FString Cur; Part->TryGetStringField(TEXT("text"), Cur);
						Part->SetStringField(TEXT("text"), Cur + Chunk);
					}
					if (bActive) W2->RefreshArchitectChatView();
					MaybeAutoSaveStreamingChat(GCapChatID);
					return;
				}
			}
			TSharedPtr<FJsonObject> Msg = MakeShareable(new FJsonObject);
			Msg->SetStringField(TEXT("role"), TEXT("model"));
			TSharedPtr<FJsonObject> Part = MakeShareable(new FJsonObject);
			Part->SetStringField(TEXT("text"), Chunk);
			TArray<TSharedPtr<FJsonValue>> GParts;
			GParts.Add(MakeShareable(new FJsonValueObject(Part)));
			Msg->SetArrayField(TEXT("parts"), GParts);
			TSharedPtr<FJsonValue> MsgVal = MakeShareable(new FJsonValueObject(Msg));
			LocalHist->Add(MsgVal);
			if (bActive)
			{
				W2->ArchitectConversationHistory.Add(MsgVal);
				W2->RefreshArchitectChatView();
			}
			MaybeAutoSaveStreamingChat(GCapChatID);
		};

		GLoop->OnToolStart = [WeakWG, GCapChatID, this]
			(const FString& ToolId, const FString& Label, const FString& ArgsPreview)
		{
			TSharedPtr<SUECPMainWidget> W2 = WeakWG.Pin();
			if (!W2.IsValid()) return;
			TArray<TSharedPtr<FJsonValue>>* LocalHist = NativeDisplayHistoryByChat.Find(GCapChatID);
			if (!LocalHist) return;
			FString Text = FString::Printf(TEXT("[TOOL_RESULT:%s:success]\n%s"), *Label, *ArgsPreview);
			TSharedPtr<FJsonObject> Msg = MakeShareable(new FJsonObject);
			Msg->SetStringField(TEXT("role"), TEXT("user"));
			TSharedPtr<FJsonObject> Part = MakeShareable(new FJsonObject);
			Part->SetStringField(TEXT("text"), Text);
			TArray<TSharedPtr<FJsonValue>> GParts;
			GParts.Add(MakeShareable(new FJsonValueObject(Part)));
			Msg->SetArrayField(TEXT("parts"), GParts);
			TSharedPtr<FJsonValue> MsgVal = MakeShareable(new FJsonValueObject(Msg));
			LocalHist->Add(MsgVal);
			NativeLoopToolBubbleMapByChat.FindOrAdd(GCapChatID).Add(ToolId, LocalHist->Num() - 1);
			if (W2->ActiveArchitectChatID == GCapChatID)
			{
				W2->ArchitectConversationHistory.Add(MsgVal);
				W2->RefreshArchitectChatView();
			}
			LastStreamSaveTimeByChat.Remove(GCapChatID);
			MaybeAutoSaveStreamingChat(GCapChatID);
		};

		GLoop->OnBeforeDispatch = [WeakWG, GCapChatID, this]
			(const FString& ToolName, const TSharedPtr<FJsonObject>& Args, FName DispatchName, FString& OutDenyReason) -> bool
		{
			TSharedPtr<SUECPMainWidget> W2 = WeakWG.Pin();
			if (!W2.IsValid()) return true;

			static const TSet<FString> GraphModTools = {
				TEXT("build_blueprint_graph"), TEXT("clear_blueprint_graph"),
				TEXT("place_node"), TEXT("connect_pins"), TEXT("set_pin_default"),
				TEXT("remove_node"), TEXT("delete_nodes")
			};
			FString Action;
			if (Args.IsValid()) Args->TryGetStringField(TEXT("action"), Action);
			const FString DispStr = DispatchName.ToString();
			if (GraphModTools.Contains(ToolName) || GraphModTools.Contains(Action) || GraphModTools.Contains(DispStr))
			{
				FString BpPath;
				if (Args.IsValid() && Args->TryGetStringField(TEXT("blueprint_path"), BpPath))
				{
					W2->CreateBlueprintDiffSnapshot(BpPath);
				}
			}

			if (TOptional<bool> GuideDecision = ApplyGuideAutopilotGate(W2.Get(), ToolName, DispatchName, Args, GCapChatID))
				return *GuideDecision;

			bool bDestructiveConfirm = true;
			GConfig->GetBool(TEXT("BpGeneratorUltimate"), TEXT("DestructiveOpsConfirm"), bDestructiveConfirm, FSettingsManager::GetGlobalConfigPath());
		{ bool bAxTurbo = false; GConfig->GetBool(TEXT("BpGeneratorUltimate"), TEXT("TurboMode"), bAxTurbo, FSettingsManager::GetGlobalConfigPath()); if (bAxTurbo) bDestructiveConfirm = false; } // Axivor Turbo: no destructive confirmations
			if (IUECPCoreModule::IsAvailable()
				&& IUECPCoreModule::Get().GetCrewService().ShouldAutoApproveDestructiveForChat(GCapChatID))
			{
				bDestructiveConfirm = false;
			}
			if (bDestructiveConfirm && W2->GetArchitectInteractionModeForChat(GCapChatID) == EAIInteractionMode::AutoEdit)
			{
				if (IsDestructiveAuthoringTool(ToolName, DispStr, Action, Args))
				{
					if (!bNativeConfirmAllowed)
					{
						bNativeConfirmPending = true;
						NativeConfirmChatID = GCapChatID;
						FString ArgsPreview;
						if (Args.IsValid()) { TSharedRef<TJsonWriter<>> Wr = TJsonWriterFactory<>::Create(&ArgsPreview); FJsonSerializer::Serialize(Args.ToSharedRef(), Wr); }
						if (W2->AppBridgeObject)
							W2->AppBridgeObject->PushToolConfirmation(DispStr.IsEmpty() ? ToolName : DispStr, ArgsPreview.Left(8000));
						return false;
					}
					bNativeConfirmAllowed = false;
				}
			}

				{
					const EAIInteractionMode CurMode = W2->GetArchitectInteractionModeForChat(GCapChatID);
					if (CurMode == EAIInteractionMode::JustChat)
					{
						if (!IsReadOnlyArchitectTool(ToolName, DispStr, Action))
						{
							OutDenyReason = FString::Printf(
								TEXT("{\"error\":\"Tool blocked: '%s' is an authoring tool and the chat is in Just Chat mode (read-only). Switch the mode to Auto Edit or Ask Before Edit to run this. Read-only tools (get_*, list_*, find_*, search_*, validate_*, get_tool_docs, classify_intent) are still available.\"}"),
								DispStr.IsEmpty() ? *ToolName : *DispStr);
							return false;
						}
					}
					if (CurMode == EAIInteractionMode::PlanMode)
					{
						if (!IsPlanModeAllowedArchitectTool(ToolName, DispStr, Action))
						{
							OutDenyReason = FString::Printf(
								TEXT("{\"error\":\"PLAN MODE: '%s' is plan-only. Finish the plan, ask the user 'Shall I proceed?', then call proceed_with_plan to switch into execution mode and build. Read/inspect tools + project_plan / memory / ask_user are allowed here.\"}"),
								DispStr.IsEmpty() ? *ToolName : *DispStr);
							return false;
						}
					}
					if (CurMode == EAIInteractionMode::AskBeforeEdit)
					{
						const bool bNeedsEditConfirm = !IsPlanModeAllowedArchitectTool(ToolName, DispStr, Action);
						if (bNeedsEditConfirm)
						{
							if (!bNativeConfirmAllowed)
							{
								bNativeConfirmPending = true;
								NativeConfirmChatID = GCapChatID;
								FString ArgsPreview;
								if (Args.IsValid()) { TSharedRef<TJsonWriter<>> Wr = TJsonWriterFactory<>::Create(&ArgsPreview); FJsonSerializer::Serialize(Args.ToSharedRef(), Wr); }
								if (W2->AppBridgeObject)
									W2->AppBridgeObject->PushToolConfirmation(DispStr.IsEmpty() ? ToolName : DispStr, ArgsPreview.Left(8000));
								return false;
							}
							bNativeConfirmAllowed = false;
						}
					}
				}
			return true;
		};

		GLoop->OnBeforeNextRound = [this]() -> bool { return !bNativeConfirmPending; };

		GLoop->OnWidgetTool = [WeakWG](const FString& Name, const TSharedPtr<FJsonObject>& Args, FUECPToolResult& Out) -> bool
		{
			TSharedPtr<SUECPMainWidget> W2 = WeakWG.Pin();
			if (!W2.IsValid()) return false;
			FToolExecutionResult ExecResult;
			if (!W2->TryDispatchWidgetOwnedTool(Name, Args, ExecResult)) return false;
			Out.bSuccess     = ExecResult.bSuccess;
			Out.ResultJson   = ExecResult.ResultJson;
			Out.SummaryJson  = ExecResult.SummaryJson;
			Out.ErrorMessage = ExecResult.ErrorMessage;
			return true;
		};

		GLoop->OnToolResult = [WeakWG, GCapChatID, this]
			(const FString& ToolId, const FString& Label, bool bSuccess, const FString& ResultJson)
		{
			TSharedPtr<SUECPMainWidget> W2 = WeakWG.Pin();
			if (!W2.IsValid()) return;
			W2->NoteArchitectToolResultForVerification(Label, bSuccess, ResultJson);
			TArray<TSharedPtr<FJsonValue>>* LocalHist = NativeDisplayHistoryByChat.Find(GCapChatID);
			if (!LocalHist) return;
			TMap<FString, int32>* BubbleMap = NativeLoopToolBubbleMapByChat.Find(GCapChatID);
			const int32* FoundIdx = BubbleMap ? BubbleMap->Find(ToolId) : nullptr;
			if (FoundIdx && *FoundIdx < LocalHist->Num())
			{
				TSharedPtr<FJsonObject> Obj = (*LocalHist)[*FoundIdx]->AsObject();
				const TArray<TSharedPtr<FJsonValue>>* Parts = nullptr;
				if (Obj.IsValid() && Obj->TryGetArrayField(TEXT("parts"), Parts) && Parts->Num() > 0)
				{
					TSharedPtr<FJsonObject> Part = (*Parts)[0]->AsObject();
					FString Cur; Part->TryGetStringField(TEXT("text"), Cur);
					if (!bSuccess) Cur = Cur.Replace(TEXT(":success]"), TEXT(":error]"));
					Part->SetStringField(TEXT("text"), Cur + TEXT("\n---\n") + ResultJson);
				}
				if (W2->ActiveArchitectChatID == GCapChatID)
				{
					FString RenderedHtml = W2->RenderSingleArchitectMessageHtml(Obj, *FoundIdx);
					if (!RenderedHtml.IsEmpty() && W2->AppBridgeObject)
					{
						RenderedHtml = FString::Printf(TEXT("<div data-hist-idx='%d'>%s</div>"), *FoundIdx, *RenderedHtml);
						FString Enc = FGenericPlatformHttp::UrlEncode(RenderedHtml);
						W2->AppBridgeObject->ExecJs(FString::Printf(
							TEXT("if(typeof updateArchitectMsgAt==='function')updateArchitectMsgAt(%d,decodeURIComponent('%s'))"),
							*FoundIdx, *Enc));
					}
				}
			}
			FTelemetryManager::Get().ReportToolSample(Label, ResultJson, bSuccess, [](bool) {});
			if (W2->ActiveArchitectChatID == GCapChatID)
			{
				W2->RefreshArchitectChatView();
				W2->PushArchitectTokenCount(W2->EstimateFullRequestTokens(W2->ArchitectConversationHistory));
			}
			LastStreamSaveTimeByChat.Remove(GCapChatID);
			MaybeAutoSaveStreamingChat(GCapChatID);
		};

		GLoop->OnDone = [WeakWG, WeakGLoop, GCapChatID, this](bool bSuccess, const FString& ErrorMsg)
		{
			TSharedPtr<SUECPMainWidget> W2 = WeakWG.Pin();
			if (!W2.IsValid()) return;
			W2->NativeLoopStreamingChats.Remove(GCapChatID);
			bool bStalledNeedsContinue = false;
			if (TSharedPtr<FUECPGeminiAgentLoop> L = WeakGLoop.Pin())
			{
				GeminiHistoryByChat.Add(GCapChatID, L->History);
				if (L->FinalInputTokens > 0 || L->FinalOutputTokens > 0)
					W2->UpdateConversationTokens(GCapChatID, L->FinalInputTokens, L->FinalOutputTokens);
				bStalledNeedsContinue = L->bStalledNeedsContinue;
			}
			if (FindGeminiLoop(GCapChatID) == WeakGLoop.Pin()) GeminiLoopsByChat.Remove(GCapChatID);
			NativeLoopToolBubbleMapByChat.Remove(GCapChatID);

			TArray<TSharedPtr<FJsonValue>>* LocalHist = NativeDisplayHistoryByChat.Find(GCapChatID);
			if (!ErrorMsg.IsEmpty())
			{
				TSharedPtr<FJsonObject> ErrMsg = MakeShareable(new FJsonObject);
				ErrMsg->SetStringField(TEXT("role"), TEXT("model"));
				ErrMsg->SetBoolField(TEXT("is_error"),
					!bSuccess && !ErrorMsg.Contains(TEXT("round"), ESearchCase::IgnoreCase));
				TSharedPtr<FJsonObject> Part = MakeShareable(new FJsonObject);
				Part->SetStringField(TEXT("text"), ErrorMsg);
				TArray<TSharedPtr<FJsonValue>> GParts;
				GParts.Add(MakeShareable(new FJsonValueObject(Part)));
				ErrMsg->SetArrayField(TEXT("parts"), GParts);
				TSharedPtr<FJsonValue> ErrVal = MakeShareable(new FJsonValueObject(ErrMsg));
				if (LocalHist) LocalHist->Add(ErrVal);
				if (W2->ActiveArchitectChatID == GCapChatID)
					W2->ArchitectConversationHistory.Add(ErrVal);
			}
			W2->ArchitectThinkingChats.Remove(GCapChatID);
			W2->PendingArchitectRequests.Remove(GCapChatID);
			W2->bIsArchitectThinking =
				W2->ArchitectThinkingChats.Contains(W2->ActiveArchitectChatID)
				|| W2->PendingArchitectRequests.Contains(W2->ActiveArchitectChatID);

			const bool bActive = W2->ActiveArchitectChatID == GCapChatID;
			if (bActive)
			{
				W2->MaybeFinalizeCompactReplacement(GCapChatID);
				W2->SaveArchitectChatHistory(GCapChatID);
			}
			else if (LocalHist && LocalHist->Num() > 0)
			{
				FChatHistoryManager::Get().SaveChatHistory(
					EConversationViewType::Architect, GCapChatID, *LocalHist);
			}
			NativeDisplayHistoryByChat.Remove(GCapChatID);

			if (bActive)
			{
				W2->RefreshArchitectChatView();
				if (W2->DiffSnapshots.Num() > 0)
				{
					W2->PendingDiffHtml = W2->BuildDiffBarHtml();
					if (!W2->PendingDiffHtml.IsEmpty())
					{
						W2->SaveDiffSnapshotsToDisk();
						if (W2->AppBridgeObject)
							W2->AppBridgeObject->PushAppendMessage(TEXT("architect"), W2->PendingDiffHtml);
						W2->PendingDiffHtml.Empty();
					}
				}
				W2->PushArchitectTokenCount(W2->EstimateFullRequestTokens(W2->ArchitectConversationHistory));
			}

			bool bAutoContinued = false;
			if (RunAutoValidateForChat(W2, GCapChatID, bActive, W2->GetArchitectInteractionModeForChat(GCapChatID)))
				bAutoContinued = true;
			if (!bAutoContinued && bSuccess)
				bAutoContinued = RunVerificationGateForChat(W2, GCapChatID, bActive, W2->GetArchitectInteractionModeForChat(GCapChatID));
			if (!bAutoContinued && bStalledNeedsContinue && bActive)
			{
				bAutoContinued = MaybeAutoContinueArchitect(
					GCapChatID, W2->GetArchitectInteractionModeForChat(GCapChatID));
			}

			if (W2->AppBridgeObject && bActive && !W2->bIsArchitectThinking && !bAutoContinued)
				W2->AppBridgeObject->ExecJs(TEXT("if(typeof onGenerationDone==='function')onGenerationDone('architect')"));

			BroadcastTurnEnded(GCapChatID, bSuccess);

			if (!bAutoContinued)
				W2->DrainQueuedArchitectMessage(GCapChatID);
		};

		GLoop->Start();
		return;
	}

	if (ProviderStr == TEXT("Custom") &&
		FHttpCommunicationManager::BuildCustomUrl(BaseURL).Contains(TEXT("anthropic.com")))
	{
		ProviderStr = TEXT("Claude");
	}

	if (ProviderStr == TEXT("Custom"))
	{
		FString CustomURL = FHttpCommunicationManager::BuildCustomUrl(BaseURL);
		W->ActiveArchitectHttpRequest->SetURL(CustomURL);
		if (CustomURL.Contains(TEXT("anthropic.com")))
		{
			W->ActiveArchitectHttpRequest->SetHeader(TEXT("x-api-key"), ApiKeyToUse);
			W->ActiveArchitectHttpRequest->SetHeader(TEXT("anthropic-version"), TEXT("2023-06-01"));
		}
		else
		{
			W->ActiveArchitectHttpRequest->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *ApiKeyToUse));
		}
		if (bIsFreeTierRequest)
		{
			FString LicTok = FEditorProfileSync::Get().GetSyncKey();
			if (!LicTok.IsEmpty())
				W->ActiveArchitectHttpRequest->SetHeader(TEXT("x-license-token"), LicTok);
		}

		FString CustomModel = ModelName.TrimStartAndEnd();
		if (CustomModel.IsEmpty())
		{
			CustomModel = TEXT("gpt-3.5-turbo");
		}
		JsonPayload->SetStringField(TEXT("model"), CustomModel);

		TArray<TSharedPtr<FJsonValue>> CustomMessages;
		TSharedPtr<FJsonObject> CustomSystemMessage = MakeShareable(new FJsonObject);
		CustomSystemMessage->SetStringField("role", "system");
		CustomSystemMessage->SetStringField("content", SystemPrompt);
		CustomMessages.Add(MakeShareable(new FJsonValueObject(CustomSystemMessage)));
		bool bFileContextAddedCustom = false;
		for (const TSharedPtr<FJsonValue>& Message : WindowedHistory)
		{
			const TSharedPtr<FJsonObject>& MsgObj = Message->AsObject();
			FString Role = MsgObj->GetStringField(TEXT("role"));
			if (Role == TEXT("context")) continue;

			const TArray<TSharedPtr<FJsonValue>>* PartsArray = nullptr;
			if (!MsgObj->TryGetArrayField(TEXT("parts"), PartsArray) || !PartsArray || PartsArray->Num() == 0) continue;

			TSharedPtr<FJsonObject> CustomMessage = MakeShareable(new FJsonObject);
			CustomMessage->SetStringField("role", Role == TEXT("model") ? TEXT("assistant") : Role);

			TArray<TSharedPtr<FJsonValue>> ContentArray;
			for (const TSharedPtr<FJsonValue>& PartValue : *PartsArray)
			{
				const TSharedPtr<FJsonObject>& PartObj = PartValue->AsObject();
				FString TextContent;
				if (PartObj->TryGetStringField(TEXT("text"), TextContent))
				{
					if (!bFileContextAddedCustom && !ArchitectFileContextForAPI.IsEmpty() && Role == TEXT("user"))
					{
						TextContent = ArchitectFileContextForAPI + TextContent;
						bFileContextAddedCustom = true;
					}
					TSharedPtr<FJsonObject> TextPart = MakeShareable(new FJsonObject);
					TextPart->SetStringField(TEXT("type"), TEXT("text"));
					TextPart->SetStringField(TEXT("text"), TextContent);
					ContentArray.Add(MakeShareable(new FJsonValueObject(TextPart)));
				}
				const TSharedPtr<FJsonObject, ESPMode::ThreadSafe>* InlineData = nullptr;
				if (PartObj->TryGetObjectField(TEXT("inline_data"), InlineData))
				{
					FString MimeType = (*InlineData)->GetStringField(TEXT("mime_type"));
					FString Data = (*InlineData)->GetStringField(TEXT("data"));
					TSharedPtr<FJsonObject> ImagePart = MakeShareable(new FJsonObject);
					ImagePart->SetStringField(TEXT("type"), TEXT("image_url"));
					TSharedPtr<FJsonObject> ImageUrl = MakeShareable(new FJsonObject);
					ImageUrl->SetStringField(TEXT("url"), FString::Printf(TEXT("data:%s;base64,%s"), *MimeType, *Data));
					ImagePart->SetObjectField(TEXT("image_url"), ImageUrl);
					ContentArray.Add(MakeShareable(new FJsonValueObject(ImagePart)));
				}
			}
			if (ContentArray.Num() > 0)
			{
				CustomMessage->SetArrayField(TEXT("content"), ContentArray);
				if (CustomMessages.Num() > 0)
				{
					TSharedPtr<FJsonObject> PrevMsg = CustomMessages.Last()->AsObject();
					FString PrevRole;
					if (PrevMsg.IsValid() && PrevMsg->TryGetStringField(TEXT("role"), PrevRole))
					{
						FString ThisRole;
						CustomMessage->TryGetStringField(TEXT("role"), ThisRole);
						if (PrevRole == ThisRole)
						{
							const TArray<TSharedPtr<FJsonValue>>* PrevContent = nullptr;
							if (PrevMsg->TryGetArrayField(TEXT("content"), PrevContent) && PrevContent)
							{
								TArray<TSharedPtr<FJsonValue>> Merged = *PrevContent;
								TSharedPtr<FJsonObject> Sep = MakeShareable(new FJsonObject);
								Sep->SetStringField(TEXT("type"), TEXT("text"));
								Sep->SetStringField(TEXT("text"), TEXT("\n\n"));
								Merged.Add(MakeShareable(new FJsonValueObject(Sep)));
								Merged.Append(ContentArray);
								PrevMsg->SetArrayField(TEXT("content"), Merged);
								continue;
							}
						}
					}
				}
				CustomMessages.Add(MakeShareable(new FJsonValueObject(CustomMessage)));
			}
		}
		{
			TArray<FString> SystemTexts;
			for (int32 i = CustomMessages.Num() - 1; i >= 0; --i)
			{
				TSharedPtr<FJsonObject> O = CustomMessages[i].IsValid() ? CustomMessages[i]->AsObject() : nullptr;
				FString R;
				if (O.IsValid()) O->TryGetStringField(TEXT("role"), R);
				if (R == TEXT("system"))
				{
					FString PlainContent;
					if (O->TryGetStringField(TEXT("content"), PlainContent) && !PlainContent.IsEmpty())
					{
						SystemTexts.Insert(PlainContent, 0);
					}
					else
					{
						const TArray<TSharedPtr<FJsonValue>>* ContentArr = nullptr;
						if (O->TryGetArrayField(TEXT("content"), ContentArr) && ContentArr)
						{
							for (const TSharedPtr<FJsonValue>& PV : *ContentArr)
							{
								TSharedPtr<FJsonObject> P = PV.IsValid() ? PV->AsObject() : nullptr;
								FString T;
								if (P.IsValid() && P->TryGetStringField(TEXT("text"), T)) SystemTexts.Insert(T, 0);
							}
						}
					}
					CustomMessages.RemoveAt(i);
				}
			}

			int32 FirstUserIdx = INDEX_NONE;
			for (int32 i = 0; i < CustomMessages.Num(); ++i)
			{
				TSharedPtr<FJsonObject> O = CustomMessages[i].IsValid() ? CustomMessages[i]->AsObject() : nullptr;
				FString R;
				if (O.IsValid()) O->TryGetStringField(TEXT("role"), R);
				if (R == TEXT("user")) { FirstUserIdx = i; break; }
			}
			if (FirstUserIdx > 0)
			{
				CustomMessages.RemoveAt(0, FirstUserIdx);
			}

			if (SystemTexts.Num() > 0)
			{
				TSharedPtr<FJsonObject> SysMsg = MakeShareable(new FJsonObject);
				SysMsg->SetStringField(TEXT("role"), TEXT("system"));
				TArray<TSharedPtr<FJsonValue>> SysContent;
				TSharedPtr<FJsonObject> SysPart = MakeShareable(new FJsonObject);
				SysPart->SetStringField(TEXT("type"), TEXT("text"));
				SysPart->SetStringField(TEXT("text"), FString::Join(SystemTexts, TEXT("\n\n")));
				SysContent.Add(MakeShareable(new FJsonValueObject(SysPart)));
				SysMsg->SetArrayField(TEXT("content"), SysContent);
				CustomMessages.Insert(MakeShareable(new FJsonValueObject(SysMsg)), 0);
			}
		}

		for (const TSharedPtr<FJsonValue>& Val : CustomMessages)
		{
			TSharedPtr<FJsonObject> MsgObj = Val.IsValid() ? Val->AsObject() : nullptr;
			if (!MsgObj.IsValid()) continue;
			const TArray<TSharedPtr<FJsonValue>>* ContentArr = nullptr;
			if (!MsgObj->TryGetArrayField(TEXT("content"), ContentArr) || !ContentArr) continue;
			bool bHasImage = false;
			FString Combined;
			for (const TSharedPtr<FJsonValue>& PartVal : *ContentArr)
			{
				TSharedPtr<FJsonObject> Part = PartVal.IsValid() ? PartVal->AsObject() : nullptr;
				if (!Part.IsValid()) continue;
				FString T;
				Part->TryGetStringField(TEXT("type"), T);
				if (T == TEXT("image_url")) { bHasImage = true; break; }
				FString Txt;
				if (Part->TryGetStringField(TEXT("text"), Txt)) Combined += Txt;
			}
			if (!bHasImage)
			{
				MsgObj->RemoveField(TEXT("content"));
				MsgObj->SetStringField(TEXT("content"), Combined);
			}
		}
		JsonPayload->SetArrayField(TEXT("messages"), CustomMessages);
		if (bUseStreaming)
			JsonPayload->SetBoolField(TEXT("stream"), true);
	}
	else if (ProviderStr == TEXT("Gemini"))
	{
		FString GeminiModel = FApiKeyManager::Get().GetActiveGeminiModel();
		W->ActiveArchitectHttpRequest->SetURL(FHttpCommunicationManager::BuildGeminiUrl(GeminiModel, ApiKeyToUse));

		TArray<TSharedPtr<FJsonValue>> GeminiContents;
		TSharedPtr<FJsonObject> SystemTurn = MakeShareable(new FJsonObject);
		SystemTurn->SetStringField(TEXT("role"), TEXT("user"));
		TArray<TSharedPtr<FJsonValue>> SystemParts;
		TSharedPtr<FJsonObject> SystemPart = MakeShareable(new FJsonObject);
		SystemPart->SetStringField(TEXT("text"), SystemPrompt);
		SystemParts.Add(MakeShareable(new FJsonValueObject(SystemPart)));
		SystemTurn->SetArrayField(TEXT("parts"), SystemParts);
		GeminiContents.Add(MakeShareable(new FJsonValueObject(SystemTurn)));
		TSharedPtr<FJsonObject> ModelAck = MakeShareable(new FJsonObject);
		ModelAck->SetStringField(TEXT("role"), TEXT("model"));
		TArray<TSharedPtr<FJsonValue>> AckParts;
		TSharedPtr<FJsonObject> AckPart = MakeShareable(new FJsonObject);
		AckPart->SetStringField(TEXT("text"), TEXT("Understood. I will generate Blueprint pseudo-code and use tools as needed."));
		AckParts.Add(MakeShareable(new FJsonValueObject(AckPart)));
		ModelAck->SetArrayField(TEXT("parts"), AckParts);
		GeminiContents.Add(MakeShareable(new FJsonValueObject(ModelAck)));

		bool bFileContextAdded = false;
		for (const TSharedPtr<FJsonValue>& Message : WindowedHistory)
		{
			const TSharedPtr<FJsonObject>& MsgObj = Message->AsObject();
			FString Role = MsgObj->GetStringField(TEXT("role"));
			if (Role == TEXT("context")) continue;

			if (!bFileContextAdded && !ArchitectFileContextForAPI.IsEmpty() && Role == TEXT("user"))
			{
				const TArray<TSharedPtr<FJsonValue>>* PartsArray;
				if (MsgObj->TryGetArrayField(TEXT("parts"), PartsArray) && PartsArray->Num() > 0)
				{
					FString OriginalText;
					if ((*PartsArray)[0]->AsObject()->TryGetStringField(TEXT("text"), OriginalText))
					{
						TSharedPtr<FJsonObject> ModifiedMessage = MakeShareable(new FJsonObject);
						ModifiedMessage->SetStringField(TEXT("role"), TEXT("user"));
						TArray<TSharedPtr<FJsonValue>> NewParts;
						TSharedPtr<FJsonObject> NewPart = MakeShareable(new FJsonObject);
						NewPart->SetStringField(TEXT("text"), ArchitectFileContextForAPI + OriginalText);
						NewParts.Add(MakeShareable(new FJsonValueObject(NewPart)));
						ModifiedMessage->SetArrayField(TEXT("parts"), NewParts);
						GeminiContents.Add(MakeShareable(new FJsonValueObject(ModifiedMessage)));
						bFileContextAdded = true;
						continue;
					}
				}
			}
			TSharedPtr<FJsonObject> CleanMsg = MakeShareable(new FJsonObject);
			CleanMsg->SetStringField(TEXT("role"), Role);
			const TArray<TSharedPtr<FJsonValue>>* ExistingParts;
			if (MsgObj->TryGetArrayField(TEXT("parts"), ExistingParts))
				CleanMsg->SetArrayField(TEXT("parts"), *ExistingParts);
			GeminiContents.Add(MakeShareable(new FJsonValueObject(CleanMsg)));
		}
		JsonPayload->SetArrayField(TEXT("contents"), GeminiContents);
	}
	else if (ProviderStr == TEXT("OpenAI"))
	{
		W->ActiveArchitectHttpRequest->SetURL(FHttpCommunicationManager::BuildOpenAIUrl());
		W->ActiveArchitectHttpRequest->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *ApiKeyToUse));
		FString OpenAIModel = FApiKeyManager::Get().GetActiveOpenAIModel();
		JsonPayload->SetStringField("model", OpenAIModel);
		if (bUseStreaming)
			JsonPayload->SetBoolField(TEXT("stream"), true);

		TArray<TSharedPtr<FJsonValue>> OpenAiMessages;
		TSharedPtr<FJsonObject> SystemMessage = MakeShareable(new FJsonObject);
		SystemMessage->SetStringField("role", "system");
		SystemMessage->SetStringField("content", SystemPrompt);
		OpenAiMessages.Add(MakeShareable(new FJsonValueObject(SystemMessage)));
		bool bFileContextAddedOpenAI = false;
		for (const TSharedPtr<FJsonValue>& Message : WindowedHistory)
		{
			const TSharedPtr<FJsonObject>& MsgObj = Message->AsObject();
			FString Role = MsgObj->GetStringField(TEXT("role"));
			if (Role == TEXT("context")) continue;

			const TArray<TSharedPtr<FJsonValue>>* PartsArray = nullptr;
			if (!MsgObj->TryGetArrayField(TEXT("parts"), PartsArray) || !PartsArray || PartsArray->Num() == 0) continue;

			TSharedPtr<FJsonObject> OpenAiMessage = MakeShareable(new FJsonObject);
			OpenAiMessage->SetStringField("role", Role == TEXT("model") ? TEXT("assistant") : Role);

			TArray<TSharedPtr<FJsonValue>> ContentArray;
			for (const TSharedPtr<FJsonValue>& PartValue : *PartsArray)
			{
				const TSharedPtr<FJsonObject>& PartObj = PartValue->AsObject();
				FString TextContent;
				if (PartObj->TryGetStringField(TEXT("text"), TextContent))
				{
					if (!bFileContextAddedOpenAI && !ArchitectFileContextForAPI.IsEmpty() && Role == TEXT("user"))
					{
						TextContent = ArchitectFileContextForAPI + TextContent;
						bFileContextAddedOpenAI = true;
					}
					TSharedPtr<FJsonObject> TextPart = MakeShareable(new FJsonObject);
					TextPart->SetStringField(TEXT("type"), TEXT("text"));
					TextPart->SetStringField(TEXT("text"), TextContent);
					ContentArray.Add(MakeShareable(new FJsonValueObject(TextPart)));
				}
				const TSharedPtr<FJsonObject, ESPMode::ThreadSafe>* InlineData = nullptr;
				if (PartObj->TryGetObjectField(TEXT("inline_data"), InlineData))
				{
					FString MimeType = (*InlineData)->GetStringField(TEXT("mime_type"));
					FString Data = (*InlineData)->GetStringField(TEXT("data"));
					TSharedPtr<FJsonObject> ImagePart = MakeShareable(new FJsonObject);
					ImagePart->SetStringField(TEXT("type"), TEXT("image_url"));
					TSharedPtr<FJsonObject> ImageUrl = MakeShareable(new FJsonObject);
					ImageUrl->SetStringField(TEXT("url"), FString::Printf(TEXT("data:%s;base64,%s"), *MimeType, *Data));
					ImagePart->SetObjectField(TEXT("image_url"), ImageUrl);
					ContentArray.Add(MakeShareable(new FJsonValueObject(ImagePart)));
				}
			}
			if (ContentArray.Num() > 0)
			{
				OpenAiMessage->SetArrayField(TEXT("content"), ContentArray);
				OpenAiMessages.Add(MakeShareable(new FJsonValueObject(OpenAiMessage)));
			}
		}
		JsonPayload->SetArrayField(TEXT("messages"), OpenAiMessages);
	}
	else if (ProviderStr == TEXT("Claude"))
	{
		W->ActiveArchitectHttpRequest = nullptr;
		W->PendingArchitectRequests.Remove(ActiveChatID);

		if (TSharedPtr<FUECPClaudeAgentLoop> Prev = FindNativeLoop(ActiveChatID))
		{
			Prev->Stop();
			NativeLoopsByChat.Remove(ActiveChatID);
		}

		TArray<TSharedPtr<FJsonValue>>& NativeHistory = NativeHistoryByChat.FindOrAdd(ActiveChatID);
		if (NativeHistory.IsEmpty())
		{
			bool bFileContextAdded = false;
			for (const TSharedPtr<FJsonValue>& Entry : WindowedHistory)
			{
				TArray<TSharedPtr<FJsonValue>> Converted =
					FUECPClaudeAgentLoop::ConvertHistoryEntryToAnthropic(Entry);

				if (!bFileContextAdded && !ArchitectFileContextForAPI.IsEmpty())
				{
					for (TSharedPtr<FJsonValue>& CV : Converted)
					{
						TSharedPtr<FJsonObject> CVObj = CV.IsValid() ? CV->AsObject() : nullptr;
						FString CVRole;
						if (!CVObj || !CVObj->TryGetStringField(TEXT("role"), CVRole) || CVRole != TEXT("user"))
							continue;
						const TArray<TSharedPtr<FJsonValue>>* Content = nullptr;
						if (!CVObj->TryGetArrayField(TEXT("content"), Content) || !Content || Content->IsEmpty())
							continue;
						TSharedPtr<FJsonObject> FirstBlock = (*Content)[0]->AsObject();
						FString BlockType, BlockText;
						if (FirstBlock &&
							FirstBlock->TryGetStringField(TEXT("type"), BlockType) && BlockType == TEXT("text") &&
							FirstBlock->TryGetStringField(TEXT("text"), BlockText))
						{
							FirstBlock->SetStringField(TEXT("text"), ArchitectFileContextForAPI + BlockText);
							bFileContextAdded = true;
						}
						break;
					}
				}
				NativeHistory.Append(Converted);
			}
		}
		else
		{
			for (int32 i = WindowedHistory.Num() - 1; i >= 0; --i)
			{
				TSharedPtr<FJsonObject> EntryObj = WindowedHistory[i].IsValid() ? WindowedHistory[i]->AsObject() : nullptr;
				FString EntryRole;
				if (!EntryObj || !EntryObj->TryGetStringField(TEXT("role"), EntryRole) || EntryRole != TEXT("user"))
					continue;
				TArray<TSharedPtr<FJsonValue>> Converted =
					FUECPClaudeAgentLoop::ConvertHistoryEntryToAnthropic(WindowedHistory[i]);
				NativeHistory.Append(Converted);
				break;
			}
		}

		FString ClaudeModel = FApiKeyManager::Get().GetActiveClaudeModel();

		TSharedPtr<FUECPClaudeAgentLoop> Loop = MakeShared<FUECPClaudeAgentLoop>();
		Loop->ApiKey     = ApiKeyToUse;
		Loop->ModelName  = ClaudeModel;
		Loop->SystemPrompt = BuildFullSystemPrompt(ChatMode, ActiveChatID);
		Loop->ChatID     = ActiveChatID;
		Loop->InteractionMode = ChatMode;
		Loop->History    = NativeHistory;
		{ int32 R = 200; GConfig->GetInt(TEXT("BpGeneratorUltimate"), TEXT("MaxAgentRounds"), R, GEditorIni); Loop->MaxRounds = R > 0 ? R : 200; }
		NativeLoopsByChat.Add(ActiveChatID, Loop);
		NativeLoopToolBubbleMapByChat.FindOrAdd(ActiveChatID).Reset();
		NativeDisplayHistoryByChat.Add(ActiveChatID, W->ArchitectConversationHistory);

		W->NativeLoopStreamingChats.Add(ActiveChatID);
		W->PendingArchitectRequests.Add(ActiveChatID, nullptr);

		TWeakPtr<FUECPClaudeAgentLoop> WeakLoop = Loop;
		TWeakPtr<SUECPMainWidget>      WeakW    = Shell;
		FString                        CapChatID = ActiveChatID;

		Loop->OnRoundResponse = [WeakW, CapChatID, ProviderStr](FHttpResponsePtr Resp)
		{
			TSharedPtr<SUECPMainWidget> W2 = WeakW.Pin();
			if (!W2.IsValid()) return;
			W2->ParseAndPushFreeTierRateLimit(Resp);
			W2->ParseAndCacheProviderRateLimit(Resp, ProviderStr);
		};

		Loop->OnTextDelta = [WeakW, CapChatID, this](const FString& Chunk)
		{
			TSharedPtr<SUECPMainWidget> W2 = WeakW.Pin();
			if (!W2.IsValid()) return;
			TArray<TSharedPtr<FJsonValue>>* LocalHist = NativeDisplayHistoryByChat.Find(CapChatID);
			if (!LocalHist) return;
			const bool bActive = W2->ActiveArchitectChatID == CapChatID;
			if (LocalHist->Num() > 0)
			{
				TSharedPtr<FJsonObject> LastObj = LocalHist->Last()->AsObject();
				FString LastRole;
				LastObj->TryGetStringField(TEXT("role"), LastRole);
				if (LastRole == TEXT("model"))
				{
					const TArray<TSharedPtr<FJsonValue>>* Parts = nullptr;
					if (LastObj->TryGetArrayField(TEXT("parts"), Parts) && Parts->Num() > 0)
					{
						TSharedPtr<FJsonObject> Part = (*Parts)[0]->AsObject();
						FString Cur;
						Part->TryGetStringField(TEXT("text"), Cur);
						Part->SetStringField(TEXT("text"), Cur + Chunk);
					}
					if (bActive) W2->RefreshArchitectChatView();
					MaybeAutoSaveStreamingChat(CapChatID);
					return;
				}
			}
			TSharedPtr<FJsonObject> ModelMsg = MakeShareable(new FJsonObject);
			ModelMsg->SetStringField(TEXT("role"), TEXT("model"));
			TSharedPtr<FJsonObject> Part = MakeShareable(new FJsonObject);
			Part->SetStringField(TEXT("text"), Chunk);
			TArray<TSharedPtr<FJsonValue>> Parts;
			Parts.Add(MakeShareable(new FJsonValueObject(Part)));
			ModelMsg->SetArrayField(TEXT("parts"), Parts);
			TSharedPtr<FJsonValue> MsgVal = MakeShareable(new FJsonValueObject(ModelMsg));
			LocalHist->Add(MsgVal);
			if (bActive)
			{
				W2->ArchitectConversationHistory.Add(MsgVal);
				W2->RefreshArchitectChatView();
			}
			MaybeAutoSaveStreamingChat(CapChatID);
		};

		Loop->OnToolStart = [WeakW, CapChatID, this]
			(const FString& ToolId, const FString& Label, const FString& ArgsPreview)
		{
			TSharedPtr<SUECPMainWidget> W2 = WeakW.Pin();
			if (!W2.IsValid()) return;
			TArray<TSharedPtr<FJsonValue>>* LocalHist = NativeDisplayHistoryByChat.Find(CapChatID);
			if (!LocalHist) return;

			FString Text = FString::Printf(TEXT("[TOOL_RESULT:%s:success]\n%s"), *Label, *ArgsPreview);
			TSharedPtr<FJsonObject> BubbleMsg = MakeShareable(new FJsonObject);
			BubbleMsg->SetStringField(TEXT("role"), TEXT("user"));
			TSharedPtr<FJsonObject> Part = MakeShareable(new FJsonObject);
			Part->SetStringField(TEXT("text"), Text);
			TArray<TSharedPtr<FJsonValue>> Parts;
			Parts.Add(MakeShareable(new FJsonValueObject(Part)));
			BubbleMsg->SetArrayField(TEXT("parts"), Parts);
			TSharedPtr<FJsonValue> MsgVal = MakeShareable(new FJsonValueObject(BubbleMsg));
			LocalHist->Add(MsgVal);
			NativeLoopToolBubbleMapByChat.FindOrAdd(CapChatID).Add(ToolId, LocalHist->Num() - 1);
			if (W2->ActiveArchitectChatID == CapChatID)
			{
				W2->ArchitectConversationHistory.Add(MsgVal);
				W2->RefreshArchitectChatView();
			}
			LastStreamSaveTimeByChat.Remove(CapChatID);
			MaybeAutoSaveStreamingChat(CapChatID);
		};

		Loop->OnBeforeDispatch = [WeakW, CapChatID, this]
			(const FString& ToolName, const TSharedPtr<FJsonObject>& Args, FName DispatchName, FString& OutDenyReason) -> bool
		{
			TSharedPtr<SUECPMainWidget> W2 = WeakW.Pin();
			if (!W2.IsValid()) return true;

			static const TSet<FString> GraphModTools = {
				TEXT("build_blueprint_graph"), TEXT("clear_blueprint_graph"),
				TEXT("place_node"), TEXT("connect_pins"), TEXT("set_pin_default"),
				TEXT("remove_node"), TEXT("delete_nodes")
			};
			FString Action;
			if (Args.IsValid()) Args->TryGetStringField(TEXT("action"), Action);
			const FString DispStr = DispatchName.ToString();
			if (GraphModTools.Contains(ToolName) || GraphModTools.Contains(Action) || GraphModTools.Contains(DispStr))
			{
				FString BpPath;
				if (Args.IsValid() && Args->TryGetStringField(TEXT("blueprint_path"), BpPath))
				{
					W2->CreateBlueprintDiffSnapshot(BpPath);
				}
			}

			if (TOptional<bool> GuideDecision = ApplyGuideAutopilotGate(W2.Get(), ToolName, DispatchName, Args, CapChatID))
				return *GuideDecision;

			bool bDestructiveConfirm = true;
			GConfig->GetBool(TEXT("BpGeneratorUltimate"), TEXT("DestructiveOpsConfirm"), bDestructiveConfirm, FSettingsManager::GetGlobalConfigPath());
		{ bool bAxTurbo = false; GConfig->GetBool(TEXT("BpGeneratorUltimate"), TEXT("TurboMode"), bAxTurbo, FSettingsManager::GetGlobalConfigPath()); if (bAxTurbo) bDestructiveConfirm = false; } // Axivor Turbo: no destructive confirmations
			if (IUECPCoreModule::IsAvailable()
				&& IUECPCoreModule::Get().GetCrewService().ShouldAutoApproveDestructiveForChat(CapChatID))
			{
				bDestructiveConfirm = false;
			}
			if (bDestructiveConfirm && W2->GetArchitectInteractionModeForChat(CapChatID) == EAIInteractionMode::AutoEdit)
			{
				if (IsDestructiveAuthoringTool(ToolName, DispStr, Action, Args))
				{
					if (!bNativeConfirmAllowed)
					{
						bNativeConfirmPending = true;
						NativeConfirmChatID = CapChatID;
						FString ArgsPreview;
						if (Args.IsValid()) { TSharedRef<TJsonWriter<>> Wr = TJsonWriterFactory<>::Create(&ArgsPreview); FJsonSerializer::Serialize(Args.ToSharedRef(), Wr); }
						if (W2->AppBridgeObject)
							W2->AppBridgeObject->PushToolConfirmation(DispStr.IsEmpty() ? ToolName : DispStr, ArgsPreview.Left(8000));
						return false;
					}
					bNativeConfirmAllowed = false;
				}
			}

				{
					const EAIInteractionMode CurMode = W2->GetArchitectInteractionModeForChat(CapChatID);
					if (CurMode == EAIInteractionMode::JustChat)
					{
						if (!IsReadOnlyArchitectTool(ToolName, DispStr, Action))
						{
							OutDenyReason = FString::Printf(
								TEXT("{\"error\":\"Tool blocked: '%s' is an authoring tool and the chat is in Just Chat mode (read-only). Switch the mode to Auto Edit or Ask Before Edit to run this. Read-only tools (get_*, list_*, find_*, search_*, validate_*, get_tool_docs, classify_intent) are still available.\"}"),
								DispStr.IsEmpty() ? *ToolName : *DispStr);
							return false;
						}
					}
					if (CurMode == EAIInteractionMode::PlanMode)
					{
						if (!IsPlanModeAllowedArchitectTool(ToolName, DispStr, Action))
						{
							OutDenyReason = FString::Printf(
								TEXT("{\"error\":\"PLAN MODE: '%s' is plan-only. Finish the plan, ask the user 'Shall I proceed?', then call proceed_with_plan to switch into execution mode and build. Read/inspect tools + project_plan / memory / ask_user are allowed here.\"}"),
								DispStr.IsEmpty() ? *ToolName : *DispStr);
							return false;
						}
					}
					if (CurMode == EAIInteractionMode::AskBeforeEdit)
					{
						const bool bNeedsEditConfirm = !IsPlanModeAllowedArchitectTool(ToolName, DispStr, Action);
						if (bNeedsEditConfirm)
						{
							if (!bNativeConfirmAllowed)
							{
								bNativeConfirmPending = true;
								NativeConfirmChatID = CapChatID;
								FString ArgsPreview;
								if (Args.IsValid()) { TSharedRef<TJsonWriter<>> Wr = TJsonWriterFactory<>::Create(&ArgsPreview); FJsonSerializer::Serialize(Args.ToSharedRef(), Wr); }
								if (W2->AppBridgeObject)
									W2->AppBridgeObject->PushToolConfirmation(DispStr.IsEmpty() ? ToolName : DispStr, ArgsPreview.Left(8000));
								return false;
							}
							bNativeConfirmAllowed = false;
						}
					}
				}
			return true;
		};

		Loop->OnBeforeNextRound = [this]() -> bool { return !bNativeConfirmPending; };

		Loop->OnWidgetTool = [WeakW](const FString& Name, const TSharedPtr<FJsonObject>& Args, FUECPToolResult& Out) -> bool
		{
			TSharedPtr<SUECPMainWidget> W2 = WeakW.Pin();
			if (!W2.IsValid()) return false;
			FToolExecutionResult ExecResult;
			if (!W2->TryDispatchWidgetOwnedTool(Name, Args, ExecResult)) return false;
			Out.bSuccess     = ExecResult.bSuccess;
			Out.ResultJson   = ExecResult.ResultJson;
			Out.SummaryJson  = ExecResult.SummaryJson;
			Out.ErrorMessage = ExecResult.ErrorMessage;
			return true;
		};

		Loop->OnToolResult = [WeakW, CapChatID, this]
			(const FString& ToolId, const FString& Label, bool bSuccess, const FString& ResultJson)
		{
			TSharedPtr<SUECPMainWidget> W2 = WeakW.Pin();
			if (!W2.IsValid()) return;
			W2->NoteArchitectToolResultForVerification(Label, bSuccess, ResultJson);
			TArray<TSharedPtr<FJsonValue>>* LocalHist = NativeDisplayHistoryByChat.Find(CapChatID);
			if (!LocalHist) return;

			TMap<FString, int32>* BubbleMap = NativeLoopToolBubbleMapByChat.Find(CapChatID);
			const int32* FoundIdx = BubbleMap ? BubbleMap->Find(ToolId) : nullptr;
			if (FoundIdx && *FoundIdx < LocalHist->Num())
			{
				TSharedPtr<FJsonObject> Obj = (*LocalHist)[*FoundIdx]->AsObject();
				const TArray<TSharedPtr<FJsonValue>>* Parts = nullptr;
				if (Obj.IsValid() && Obj->TryGetArrayField(TEXT("parts"), Parts) && Parts->Num() > 0)
				{
					TSharedPtr<FJsonObject> Part = (*Parts)[0]->AsObject();
					FString Cur;
					Part->TryGetStringField(TEXT("text"), Cur);
					if (!bSuccess)
						Cur = Cur.Replace(TEXT(":success]"), TEXT(":error]"));
					Part->SetStringField(TEXT("text"), Cur + TEXT("\n---\n") + ResultJson);
				}
				if (W2->ActiveArchitectChatID == CapChatID)
				{
					FString RenderedHtml = W2->RenderSingleArchitectMessageHtml(Obj, *FoundIdx);
					if (!RenderedHtml.IsEmpty() && W2->AppBridgeObject)
					{
						RenderedHtml = FString::Printf(TEXT("<div data-hist-idx='%d'>%s</div>"), *FoundIdx, *RenderedHtml);
						FString Enc = FGenericPlatformHttp::UrlEncode(RenderedHtml);
						W2->AppBridgeObject->ExecJs(FString::Printf(
							TEXT("if(typeof updateArchitectMsgAt==='function')updateArchitectMsgAt(%d,decodeURIComponent('%s'))"),
							*FoundIdx, *Enc));
					}
				}
			}

			FTelemetryManager::Get().ReportToolSample(Label, ResultJson, bSuccess, [](bool) {});

			if (W2->ActiveArchitectChatID == CapChatID)
			{
				W2->RefreshArchitectChatView();
				W2->PushArchitectTokenCount(W2->EstimateFullRequestTokens(W2->ArchitectConversationHistory));
			}
			LastStreamSaveTimeByChat.Remove(CapChatID);
			MaybeAutoSaveStreamingChat(CapChatID);
		};

		Loop->OnDone = [WeakW, WeakLoop, CapChatID, this](bool bSuccess, const FString& ErrorMsg)
		{
			TSharedPtr<SUECPMainWidget> W2 = WeakW.Pin();
			if (!W2.IsValid()) return;

			W2->NativeLoopStreamingChats.Remove(CapChatID);

			bool bStalledNeedsContinue = false;
			if (TSharedPtr<FUECPClaudeAgentLoop> L = WeakLoop.Pin())
			{
				NativeHistoryByChat.Add(CapChatID, L->History);
				if (L->FinalInputTokens > 0 || L->FinalOutputTokens > 0)
					W2->UpdateConversationTokens(CapChatID, L->FinalInputTokens, L->FinalOutputTokens);
				bStalledNeedsContinue = L->bStalledNeedsContinue;
			}

			if (FindNativeLoop(CapChatID) == WeakLoop.Pin())
				NativeLoopsByChat.Remove(CapChatID);
			NativeLoopToolBubbleMapByChat.Remove(CapChatID);

			TArray<TSharedPtr<FJsonValue>>* LocalHist = NativeDisplayHistoryByChat.Find(CapChatID);
			if (!ErrorMsg.IsEmpty())
			{
				TSharedPtr<FJsonObject> ErrMsg = MakeShareable(new FJsonObject);
				ErrMsg->SetStringField(TEXT("role"), TEXT("model"));
				ErrMsg->SetBoolField(TEXT("is_error"),
					!bSuccess && !ErrorMsg.Contains(TEXT("round"), ESearchCase::IgnoreCase));
				TSharedPtr<FJsonObject> Part = MakeShareable(new FJsonObject);
				Part->SetStringField(TEXT("text"), ErrorMsg);
				TArray<TSharedPtr<FJsonValue>> Parts;
				Parts.Add(MakeShareable(new FJsonValueObject(Part)));
				ErrMsg->SetArrayField(TEXT("parts"), Parts);
				TSharedPtr<FJsonValue> ErrVal = MakeShareable(new FJsonValueObject(ErrMsg));
				if (LocalHist) LocalHist->Add(ErrVal);
				if (W2->ActiveArchitectChatID == CapChatID)
					W2->ArchitectConversationHistory.Add(ErrVal);
			}

			W2->ArchitectThinkingChats.Remove(CapChatID);
			W2->PendingArchitectRequests.Remove(CapChatID);
			W2->bIsArchitectThinking =
				W2->ArchitectThinkingChats.Contains(W2->ActiveArchitectChatID)
				|| W2->PendingArchitectRequests.Contains(W2->ActiveArchitectChatID);

			const bool bActive = W2->ActiveArchitectChatID == CapChatID;
			if (bActive)
			{
				W2->MaybeFinalizeCompactReplacement(CapChatID);
				W2->SaveArchitectChatHistory(CapChatID);
			}
			else if (LocalHist && LocalHist->Num() > 0)
			{
				FChatHistoryManager::Get().SaveChatHistory(
					EConversationViewType::Architect, CapChatID, *LocalHist);
			}
			NativeDisplayHistoryByChat.Remove(CapChatID);

			if (bActive)
			{
				W2->RefreshArchitectChatView();
				if (W2->DiffSnapshots.Num() > 0)
				{
					W2->PendingDiffHtml = W2->BuildDiffBarHtml();
					if (!W2->PendingDiffHtml.IsEmpty())
					{
						W2->SaveDiffSnapshotsToDisk();
						if (W2->AppBridgeObject)
							W2->AppBridgeObject->PushAppendMessage(TEXT("architect"), W2->PendingDiffHtml);
						W2->PendingDiffHtml.Empty();
					}
				}
				W2->PushArchitectTokenCount(W2->EstimateFullRequestTokens(W2->ArchitectConversationHistory));
			}

			bool bAutoContinued = false;
			if (RunAutoValidateForChat(W2, CapChatID, bActive, W2->GetArchitectInteractionModeForChat(CapChatID)))
				bAutoContinued = true;
			if (!bAutoContinued && bSuccess)
				bAutoContinued = RunVerificationGateForChat(W2, CapChatID, bActive, W2->GetArchitectInteractionModeForChat(CapChatID));
			if (!bAutoContinued && bStalledNeedsContinue && bActive)
			{
				bAutoContinued = MaybeAutoContinueArchitect(
					CapChatID, W2->GetArchitectInteractionModeForChat(CapChatID));
			}

			if (W2->AppBridgeObject && bActive && !W2->bIsArchitectThinking && !bAutoContinued)
				W2->AppBridgeObject->ExecJs(TEXT("if(typeof onGenerationDone==='function')onGenerationDone('architect')"));

			BroadcastTurnEnded(CapChatID, bSuccess);

			if (!bAutoContinued)
				W2->DrainQueuedArchitectMessage(CapChatID);
		};

		Loop->Start();
		return;
	}
	else if (ProviderStr == TEXT("DeepSeek"))
	{
		W->ActiveArchitectHttpRequest->SetURL(FHttpCommunicationManager::BuildDeepSeekUrl());
		W->ActiveArchitectHttpRequest->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *ApiKeyToUse));
		JsonPayload->SetStringField(TEXT("model"), TEXT("deepseek-chat"));
		if (bUseStreaming)
			JsonPayload->SetBoolField(TEXT("stream"), true);

		TArray<TSharedPtr<FJsonValue>> DeepSeekMessages;
		TSharedPtr<FJsonObject> SystemMessage = MakeShareable(new FJsonObject);
		SystemMessage->SetStringField("role", "system");
		SystemMessage->SetStringField("content", SystemPrompt);
		DeepSeekMessages.Add(MakeShareable(new FJsonValueObject(SystemMessage)));
		bool bFileContextAddedDeepSeek = false;
		for (const TSharedPtr<FJsonValue>& Message : WindowedHistory)
		{
			const TSharedPtr<FJsonObject>& MsgObj = Message->AsObject();
			FString Role = MsgObj->GetStringField(TEXT("role"));
			if (Role == TEXT("context")) continue;

			const TArray<TSharedPtr<FJsonValue>>* PartsArray = nullptr;
			if (!MsgObj->TryGetArrayField(TEXT("parts"), PartsArray) || !PartsArray || PartsArray->Num() == 0) continue;

			TSharedPtr<FJsonObject> DeepSeekMessage = MakeShareable(new FJsonObject);
			DeepSeekMessage->SetStringField("role", Role == TEXT("model") ? TEXT("assistant") : Role);

			TArray<TSharedPtr<FJsonValue>> ContentArray;
			for (const TSharedPtr<FJsonValue>& PartValue : *PartsArray)
			{
				const TSharedPtr<FJsonObject>& PartObj = PartValue->AsObject();
				FString TextContent;
				if (PartObj->TryGetStringField(TEXT("text"), TextContent))
				{
					if (!bFileContextAddedDeepSeek && !ArchitectFileContextForAPI.IsEmpty() && Role == TEXT("user"))
					{
						TextContent = ArchitectFileContextForAPI + TextContent;
						bFileContextAddedDeepSeek = true;
					}
					TSharedPtr<FJsonObject> TextPart = MakeShareable(new FJsonObject);
					TextPart->SetStringField(TEXT("type"), TEXT("text"));
					TextPart->SetStringField(TEXT("text"), TextContent);
					ContentArray.Add(MakeShareable(new FJsonValueObject(TextPart)));
				}
				const TSharedPtr<FJsonObject, ESPMode::ThreadSafe>* InlineData = nullptr;
				if (PartObj->TryGetObjectField(TEXT("inline_data"), InlineData))
				{
					FString MimeType = (*InlineData)->GetStringField(TEXT("mime_type"));
					FString Data = (*InlineData)->GetStringField(TEXT("data"));
					TSharedPtr<FJsonObject> ImagePart = MakeShareable(new FJsonObject);
					ImagePart->SetStringField(TEXT("type"), TEXT("image_url"));
					TSharedPtr<FJsonObject> ImageUrl = MakeShareable(new FJsonObject);
					ImageUrl->SetStringField(TEXT("url"), FString::Printf(TEXT("data:%s;base64,%s"), *MimeType, *Data));
					ImagePart->SetObjectField(TEXT("image_url"), ImageUrl);
					ContentArray.Add(MakeShareable(new FJsonValueObject(ImagePart)));
				}
			}
			if (ContentArray.Num() > 0)
			{
				DeepSeekMessage->SetArrayField(TEXT("content"), ContentArray);
				DeepSeekMessages.Add(MakeShareable(new FJsonValueObject(DeepSeekMessage)));
			}
		}
		JsonPayload->SetArrayField(TEXT("messages"), DeepSeekMessages);
	}

	if (!ActiveSlot.CustomParams.IsEmpty())
	{
		TSharedPtr<FJsonObject> CustomObj;
		TSharedRef<TJsonReader<>> CustomReader = TJsonReaderFactory<>::Create(ActiveSlot.CustomParams);
		if (FJsonSerializer::Deserialize(CustomReader, CustomObj) && CustomObj.IsValid())
		{
			for (const auto& Pair : CustomObj->Values)
			{
				JsonPayload->SetField(FString(*Pair.Key), Pair.Value);
			}
			UE_LOG(LogTemp, Log, TEXT("BP Gen Architect: Merged %d custom API parameters"), CustomObj->Values.Num());
		}
	}

	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&RequestBody);
	FJsonSerializer::Serialize(JsonPayload.ToSharedRef(), Writer);
	W->ActiveArchitectHttpRequest->SetContentAsString(RequestBody);

	FArchitectRequestMetrics Metrics;
	Metrics.ChatID = W->ActiveArchitectChatID;
	Metrics.Provider = ProviderStr;
	Metrics.RequestStartTime = RequestStartTime;
	Metrics.PromptBuildMs = PromptBuildMs;
	Metrics.RequestChars = RequestBody.Len();
	Metrics.EstimatedTokens = EstimatedTokens;
	ArchitectRequestMetrics.Add(W->ActiveArchitectHttpRequest.Get(), Metrics);

	if (bUseStreaming)
	{
		IHttpRequest* RequestKey = W->ActiveArchitectHttpRequest.Get();
		TSharedPtr<FArchitectStreamingState> StreamingState = MakeShared<FArchitectStreamingState>();
		StreamingState->ChatID = W->ActiveArchitectChatID;
		StreamingState->Provider = ProviderStr;
		ArchitectStreamingStates.Add(RequestKey, StreamingState);

		TWeakPtr<FUECPArchitectCoordinator> CoordWeak = AsShared();
#if !UE_VERSION_OLDER_THAN(5, 5, 0)
		W->ActiveArchitectHttpRequest->SetResponseBodyReceiveStreamDelegateV2(
			FHttpRequestStreamDelegateV2::CreateLambda([CoordWeak, StreamingState, RequestKey](void* Ptr, int64& InOutLength)
			{
				if (!StreamingState.IsValid() || !Ptr || InOutLength <= 0)
				{
					return;
				}

				FUTF8ToTCHAR Converter(reinterpret_cast<const UTF8CHAR*>(Ptr), static_cast<int32>(InOutLength));
				FString Chunk(Converter.Length(), Converter.Get());
				Chunk.ReplaceInline(TEXT("\r"), TEXT(""));

				if (TSharedPtr<FUECPArchitectCoordinator> InnerCoord = CoordWeak.Pin())
				{
					if (FArchitectRequestMetrics* RequestMetrics = InnerCoord->ArchitectRequestMetrics.Find(RequestKey))
					{
						if (RequestMetrics->FirstByteTime <= 0.0)
						{
							RequestMetrics->FirstByteTime = FPlatformTime::Seconds();
						}
					}
				}

				FScopeLock Lock(&StreamingState->Mutex);
				StreamingState->RawResponseText += Chunk;
				StreamingState->PendingSseBuffer += Chunk;
			}));
#endif

		W->ActiveArchitectHttpRequest->OnRequestProgress64().BindLambda(
			[](FHttpRequestPtr Req, uint64 BytesSent, uint64 BytesReceived)
			{
				IUECPCoreModule::Get().GetArchitectService().HandleStreamingProgress(Req, BytesSent, BytesReceived);
			});
	}

	FString CapturedChatID = W->ActiveArchitectChatID;
	W->ActiveArchitectHttpRequest->OnProcessRequestComplete().BindLambda(
		[](FHttpRequestPtr Req, FHttpResponsePtr Resp, bool bWasSuccessful)
		{
			IUECPCoreModule::Get().GetArchitectService().OnApiResponseReceived(Req, Resp, bWasSuccessful);
		});
	W->PendingArchitectChatID = CapturedChatID;

	int32 RequestSize = RequestBody.Len();
	UE_LOG(LogTemp, Log, TEXT("BP Gen Architect: Sending request to %s — payload %d KB (~%d tokens), prompt build %.1f ms"),
		*ProviderStr, RequestSize / 1024, RequestSize / 4, PromptBuildMs);

	W->ActiveArchitectHttpRequest->ProcessRequest();
}

void FUECPArchitectCoordinator::OnApiResponseReceived(FHttpRequestPtr, FHttpResponsePtr, bool)
{
}

void FUECPArchitectCoordinator::HandleStreamingProgress(FHttpRequestPtr, uint64, uint64)
{
}

const TArray<TSharedPtr<FJsonValue>>& FUECPArchitectCoordinator::GetConversationHistory() const
{
	if (TSharedPtr<SUECPMainWidget> W = Shell.Pin()) return W->GetArchitectChatHistoryForExtraction();
	return EmptyHistory;
}

const TArray<TSharedPtr<FConversationInfo>>& FUECPArchitectCoordinator::GetConversationList() const
{
	if (TSharedPtr<SUECPMainWidget> W = Shell.Pin()) return W->GetArchitectChatListForExtraction();
	return EmptyList;
}

const FString& FUECPArchitectCoordinator::GetActiveChatID() const
{
	if (TSharedPtr<SUECPMainWidget> W = Shell.Pin()) return W->GetArchitectActiveChatIDForExtraction();
	return EmptyChatID;
}

bool FUECPArchitectCoordinator::IsThinking() const
{
	if (TSharedPtr<SUECPMainWidget> W = Shell.Pin()) return W->ArchitectIsThinkingForExtraction();
	return false;
}

TArray<TSharedPtr<FJsonValue>> FUECPArchitectCoordinator::GetConversationHistoryForChat(const FString& ChatID) const
{
	if (ChatID.IsEmpty()) return TArray<TSharedPtr<FJsonValue>>();

	TSharedPtr<SUECPMainWidget> W = Shell.Pin();
	if (!W.IsValid()) return TArray<TSharedPtr<FJsonValue>>();

	if (W->ActiveArchitectChatID == ChatID)
		return W->ArchitectConversationHistory;

	if (const TArray<TSharedPtr<FJsonValue>>* InMem = NativeDisplayHistoryByChat.Find(ChatID))
		return *InMem;

	return FChatHistoryManager::Get().LoadChatHistory(EConversationViewType::Architect, ChatID);
}

bool FUECPArchitectCoordinator::IsThinkingForChat(const FString& ChatID) const
{
	if (ChatID.IsEmpty()) return false;
	TSharedPtr<SUECPMainWidget> W = Shell.Pin();
	if (!W.IsValid()) return false;
	return W->ArchitectThinkingChats.Contains(ChatID)
		|| W->PendingArchitectRequests.Contains(ChatID)
		|| W->NativeLoopStreamingChats.Contains(ChatID)
		|| W->AgentInstances.Contains(ChatID);
}

int32 FUECPArchitectCoordinator::GetQueuedMessageCountForChat(const FString& ChatID) const
{
	if (ChatID.IsEmpty()) return 0;
	TSharedPtr<SUECPMainWidget> W = Shell.Pin();
	if (!W.IsValid()) return 0;
	if (const TArray<FQueuedArchitectMessage>* Queue = W->ArchitectMessageQueueByChat.Find(ChatID))
		return Queue->Num();
	return 0;
}

EAIInteractionMode FUECPArchitectCoordinator::GetActiveInteractionMode() const
{
	if (TSharedPtr<SUECPMainWidget> W = Shell.Pin()) return W->GetActiveArchitectInteractionMode();
	return EAIInteractionMode::AutoEdit;
}

bool FUECPArchitectCoordinator::MaybeApplyCachedTemplate(
	const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	static thread_local bool bApplying = false;
	if (bApplying) return false;

	TSharedPtr<SUECPMainWidget> W = Shell.Pin();
	if (!W.IsValid() || !Args.IsValid()) return false;
	if (W->CachedTemplateMetadata.Num() == 0) return false;

	FString AutoQuery;
	const TArray<TSharedPtr<FJsonValue>>* Comments = nullptr;
	if (Args->TryGetArrayField(TEXT("comments"), Comments) && Comments->Num() > 0)
	{
		const TSharedPtr<FJsonObject>* CO = nullptr;
		if ((*Comments)[0]->TryGetObject(CO))
			(*CO)->TryGetStringField(TEXT("text"), AutoQuery);
	}
	if (AutoQuery.IsEmpty())
	{
		const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
		if (Args->TryGetArrayField(TEXT("nodes"), Nodes))
		{
			for (const auto& NV : *Nodes)
			{
				const TSharedPtr<FJsonObject>* NodeObjPtr = nullptr;
				if (!NV->TryGetObject(NodeObjPtr)) continue;

				FString CN;
				if ((*NodeObjPtr)->TryGetStringField(TEXT("custom_name"), CN) && !CN.IsEmpty())
				{ AutoQuery = CN.Replace(TEXT("_"), TEXT(" ")); break; }

				FString Handle;
				(*NodeObjPtr)->TryGetStringField(TEXT("handle"), Handle);
				if (Handle == TEXT("ev.CustomEvent") || Handle.Contains(TEXT("CustomEvent")))
				{
					FString NodeId;
					if ((*NodeObjPtr)->TryGetStringField(TEXT("id"), NodeId) && !NodeId.IsEmpty())
					{ AutoQuery = NodeId.Replace(TEXT("_"), TEXT(" ")); break; }
				}
			}
		}
	}

	if (AutoQuery.IsEmpty()) return false;

	TSharedPtr<FJsonObject> SearchArgs = MakeShareable(new FJsonObject());
	SearchArgs->SetStringField(TEXT("query"), AutoQuery);
	const FToolExecutionResult SR = W->ExecuteTool_SearchBlueprintTemplates(SearchArgs);
	if (!SR.bSuccess) return false;

	TSharedPtr<FJsonObject> SJ;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(SR.ResultJson);
	if (!FJsonSerializer::Deserialize(Reader, SJ) || !SJ.IsValid()) return false;

	double Count = 0;
	SJ->TryGetNumberField(TEXT("count"), Count);
	if (Count <= 0) return false;

	const TArray<TSharedPtr<FJsonValue>>* Sols = nullptr;
	if (!SJ->TryGetArrayField(TEXT("solutions"), Sols) || Sols->Num() == 0) return false;

	const TSharedPtr<FJsonObject>* Top = nullptr;
	if (!(*Sols)[0]->TryGetObject(Top)) return false;

	FString SolId; (*Top)->TryGetStringField(TEXT("solution_id"), SolId);
	FString BpPath; Args->TryGetStringField(TEXT("blueprint_path"), BpPath);

	TSharedPtr<FJsonObject> ApplyArgs = MakeShareable(new FJsonObject());
	ApplyArgs->SetStringField(TEXT("solution_id"), SolId);
	ApplyArgs->SetStringField(TEXT("blueprint_path"), BpPath);
	ApplyArgs->SetStringField(TEXT("graph_type"), TEXT("auto"));

	TGuardValue<bool> ReentryGuard(bApplying, true);
	const FToolExecutionResult AR = W->ExecuteTool_ApplyBlueprintTemplate(ApplyArgs);
	OutJsonString = AR.ResultJson;
	OutError = AR.ErrorMessage;
	return true;
}

void FUECPArchitectCoordinator::ClearNativeHistoryForChat(const FString& ChatID)
{
	NativeHistoryByChat.Remove(ChatID);
	OpenAIHistoryByChat.Remove(ChatID);
	GeminiHistoryByChat.Remove(ChatID);
}

void FUECPArchitectCoordinator::InvalidatePromptCaches()
{
	TSharedPtr<SUECPMainWidget> W = Shell.Pin();
	if (!W.IsValid()) return;

	bStaticPromptCacheValid = false;
	bHandleCheatsheetCacheValid = false;
	CachedStaticSystemPrompt.Empty();
	CachedHandleCheatsheet.Empty();
	CachedStaticPromptHash = 0;
}

void FUECPArchitectCoordinator::OnPromptAssetUpdated(const FString& AssetName)
{
	TSharedPtr<SUECPMainWidget> W = Shell.Pin();
	if (!W.IsValid()) return;

	if (AssetName == TEXT("node_handle_reference"))
	{
		bHandleCheatsheetCacheValid = false;
		bCheatsheetSectionsParsed   = false;
	}
	if (AssetName == TEXT("system_prompt") || AssetName == TEXT("tool_docs"))
	{
		bStaticPromptCacheValid = false;
	}
}

bool FUECPArchitectCoordinator::ShouldInvalidateStaticCache() const
{
	TSharedPtr<SUECPMainWidget> W = Shell.Pin();
	if (!W.IsValid()) return true;

	FString CurrentProvider = FApiKeyManager::Get().GetActiveSlot().Provider;
	if (CurrentProvider != CachedProviderForPrompt)
		return true;

	if (W->GetActiveArchitectInteractionMode() != CachedInteractionMode)
		return true;

	int32 CurrentMaxBatch = 5;
	GConfig->GetInt(TEXT("BpGeneratorUltimate"), TEXT("MaxBatchSize"), CurrentMaxBatch, FSettingsManager::GetGlobalConfigPath());
	if (CurrentMaxBatch != CachedMaxBatchSizeForPrompt)
		return true;

	return false;
}

FString FUECPArchitectCoordinator::GetSystemPrompt()
{
	return GetCachedStaticSystemPrompt();
}

int32 FUECPArchitectCoordinator::GetCachedStaticPromptChars() const
{
	return CachedStaticSystemPrompt.Len();
}

FString FUECPArchitectCoordinator::GetCachedStaticSystemPrompt()
{
	TSharedPtr<SUECPMainWidget> W = Shell.Pin();
	if (!W.IsValid()) return FString();

	if (ShouldInvalidateStaticCache())
	{
		InvalidatePromptCaches();
	}

	CachedProviderForPrompt = FApiKeyManager::Get().GetActiveSlot().Provider;
	CachedInteractionMode = W->GetActiveArchitectInteractionMode();
	GConfig->GetInt(TEXT("BpGeneratorUltimate"), TEXT("MaxBatchSize"), CachedMaxBatchSizeForPrompt, FSettingsManager::GetGlobalConfigPath());

	if (bStaticPromptCacheValid && !CachedStaticSystemPrompt.IsEmpty())
	{
		return CachedStaticSystemPrompt;
	}

	CachedStaticSystemPrompt = GenerateStaticSystemPromptInternal();
	bStaticPromptCacheValid = true;
	return CachedStaticSystemPrompt;
}

FString FUECPArchitectCoordinator::GenerateStaticSystemPromptInternal()
{
	TSharedPtr<SUECPMainWidget> W = Shell.Pin();
	if (!W.IsValid()) return FString();

	TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("BpGeneratorUltimate"));
	if (Plugin.IsValid())
	{
		FString PromptBody = FUpdateManager::Get().LoadCachedRevision(TEXT("system_prompt"));
		if (!PromptBody.TrimStart().IsEmpty())
		{
			int32 CurrentMaxBatch = 5;
			GConfig->GetInt(TEXT("BpGeneratorUltimate"), TEXT("MaxBatchSize"), CurrentMaxBatch, FSettingsManager::GetGlobalConfigPath());
			PromptBody = PromptBody.Replace(TEXT("{MAX_TOOLS}"), *FString::FromInt(CurrentMaxBatch));
			return PromptBody;
		}
	}

	FUpdateManager::Get().CheckForUpdates();
	return FString();
}

FString FUECPArchitectCoordinator::GetCachedHandleCheatsheet() const
{
	TSharedPtr<SUECPMainWidget> W = Shell.Pin();
	if (!W.IsValid()) return FString();
	if (bHandleCheatsheetCacheValid) return CachedHandleCheatsheet;

	CachedHandleCheatsheet = FUpdateManager::Get().LoadCachedRevision(TEXT("node_handle_reference"));

	bHandleCheatsheetCacheValid = true;
	return CachedHandleCheatsheet;
}

const TMap<FString, FString>& FUECPArchitectCoordinator::GetCheatsheetSections() const
{
	TSharedPtr<SUECPMainWidget> W = Shell.Pin();
	if (!W.IsValid()) return EmptyCheatsheetSections;
	if (bCheatsheetSectionsParsed) return CachedCheatsheetSections;

	FString Full = GetCachedHandleCheatsheet();
	if (Full.IsEmpty())
	{
		bCheatsheetSectionsParsed = true;
		return CachedCheatsheetSections;
	}

	TArray<FString> Lines;
	Full.ParseIntoArrayLines(Lines);

	FString CurrentSection;
	FString CurrentContent;
	FString PreambleContent;

	for (const FString& Line : Lines)
	{
		if (Line.StartsWith(TEXT("--- ")) && Line.EndsWith(TEXT(" ---")))
		{
			if (!CurrentSection.IsEmpty())
				CachedCheatsheetSections.Add(CurrentSection, CurrentContent);
			else if (!PreambleContent.IsEmpty())
				CachedCheatsheetSections.Add(TEXT("Critical Rules (Preamble)"), PreambleContent);

			CurrentSection = Line.Mid(4, Line.Len() - 8);
			CurrentContent = Line + TEXT("\n");
		}
		else if (Line.StartsWith(TEXT("=== ")) && Line.EndsWith(TEXT(" ===")))
		{
			if (!CurrentSection.IsEmpty())
				continue;
			PreambleContent += Line + TEXT("\n");
		}
		else
		{
			if (CurrentSection.IsEmpty())
				PreambleContent += Line + TEXT("\n");
			else
				CurrentContent += Line + TEXT("\n");
		}
	}
	if (!CurrentSection.IsEmpty())
		CachedCheatsheetSections.Add(CurrentSection, CurrentContent);
	else if (!PreambleContent.IsEmpty())
		CachedCheatsheetSections.Add(TEXT("Critical Rules (Preamble)"), PreambleContent);

	bCheatsheetSectionsParsed = true;
	return CachedCheatsheetSections;
}

FString FUECPArchitectCoordinator::GetHandleReferenceSections(const FString& SectionsCSV) const
{
	const TMap<FString, FString>& Sections = GetCheatsheetSections();

	FString CleanCSV = SectionsCSV.TrimStartAndEnd();
	if (CleanCSV.StartsWith(TEXT("[")))
	{
		CleanCSV = CleanCSV.Replace(TEXT("["), TEXT("")).Replace(TEXT("]"), TEXT(""))
		                   .Replace(TEXT("\""), TEXT("")).Replace(TEXT("'"), TEXT("")).TrimStartAndEnd();
	}

	if (CleanCSV.ToLower() == TEXT("list"))
	{
		TArray<FString> SortedNames;
		for (const auto& Pair : Sections) SortedNames.Add(Pair.Key);
		SortedNames.Sort();

		FString List = TEXT("Available handle reference sections (pass comma-separated names to 'sections' param):\n");
		for (const FString& Name : SortedNames)
		{
			const FString* Val = Sections.Find(Name);
			const int32 CharCount = Val ? Val->Len() : 0;
			List += FString::Printf(TEXT("  - %s (%d chars)\n"), *Name, CharCount);
		}
		List += FString::Printf(TEXT("\nTotal: %d sections.\n"), Sections.Num());
		List += TEXT("\nExamples:\n");
		List += TEXT("  get_handle_reference(sections='Event Handles (for EventGraph nodes[]), Actor, Character, Flow Control')\n");
		List += TEXT("  get_handle_reference(sections='State Tree, AI')   — for AI/BT/State Tree work\n");
		List += TEXT("  get_handle_reference(sections='Variables, Casting, Flow Control')   — for basic Blueprint logic\n");
		List += TEXT("\nNOTES:\n");
		List += TEXT("  - Use the EXACT section name (fuzzy contains-match is allowed; case-insensitive).\n");
		List += TEXT("  - 'State Tree' covers ev.ReceiveEnterState / ev.ReceiveTickAI / ev.ReceiveTestCondition etc. on Task/Evaluator/Condition BPs.\n");
		List += TEXT("  - 'Event Handles (for EventGraph nodes[])' lists every ev.* handle for EventGraph events.\n");
		return List;
	}

	TArray<FString> Requested;
	CleanCSV.ParseIntoArray(Requested, TEXT(","));

	FString Result;
	int32 FoundCount = 0;
	TArray<FString> NotFound;
	TSet<FString> IncludedSectionKeys;

	auto AppendSection = [&](const FString& Key, const FString& Content)
	{
		if (!IncludedSectionKeys.Contains(Key))
		{
			IncludedSectionKeys.Add(Key);
			Result += Content;
			Result += TEXT("\n");
			FoundCount++;
		}
	};

	for (FString& Req : Requested)
	{
		Req = Req.TrimStartAndEnd();
		if (Req.IsEmpty()) continue;

		FString MatchedKey;
		const FString* Found = Sections.Find(Req);
		if (Found) MatchedKey = Req;
		if (!Found)
		{
			FString ReqLower = Req.ToLower();
			for (const auto& Pair : Sections)
			{
				if (Pair.Key.ToLower().Contains(ReqLower))
				{
					Found = &Pair.Value;
					MatchedKey = Pair.Key;
					break;
				}
			}
		}

		if (Found)
		{
			AppendSection(MatchedKey, *Found);

			FString MatchedKeyLower = MatchedKey.ToLower();
			if (MatchedKeyLower.Contains(TEXT("event")) && !MatchedKeyLower.Contains(TEXT("dispatcher")))
			{
				for (const auto& Pair : Sections)
				{
					if (Pair.Key.ToLower().Contains(TEXT("dispatcher")))
					{
						AppendSection(Pair.Key, Pair.Value);
						break;
					}
				}
			}
		}
		else
		{
			NotFound.Add(Req);
		}
	}

	if (NotFound.Num() > 0)
	{
		Result += FString::Printf(TEXT("\nNOT FOUND: %s\n"), *FString::Join(NotFound, TEXT(", ")));
		for (const FString& Missing : NotFound)
		{
			const FString MissingLower = Missing.ToLower();
			TArray<FString> Suggestions;
			for (const auto& Pair : Sections)
			{
				const FString KeyLower = Pair.Key.ToLower();
				TArray<FString> MissingWords;
				MissingLower.ParseIntoArray(MissingWords, TEXT(" "));
				for (const FString& Word : MissingWords)
				{
					if (Word.Len() >= 3 && KeyLower.Contains(Word))
					{
						Suggestions.AddUnique(Pair.Key);
						break;
					}
				}
			}
			if (Suggestions.Num() > 0)
			{
				Result += FString::Printf(TEXT("  '%s' → did you mean: %s ?\n"), *Missing, *FString::Join(Suggestions, TEXT(", ")));
			}
		}
		Result += TEXT("\nCall get_handle_reference(sections='list') (or with no args) to see all available section names.\n");
		Result += TEXT("Use comma-separated names; JSON arrays and [brackets] are stripped automatically.\n");
	}

	return Result;
}

FString FUECPArchitectCoordinator::GenerateSystemPrompt()
{
	auto W = Shell.Pin();
	if (!W.IsValid()) return FString();
	return BuildFullSystemPrompt(W->GetActiveArchitectInteractionMode(), W->ActiveArchitectChatID);
}

FString FUECPArchitectCoordinator::BuildFullSystemPrompt(EAIInteractionMode Mode, const FString& ChatID, bool bIsCliAgent)
{
	TSharedPtr<SUECPMainWidget> W = Shell.Pin();
	if (!W.IsValid()) return FString();

	const FDateTime Today = FDateTime::Today();
	FString Ret = FString::Printf(
		TEXT("You are the Blueprint Architect for Unreal Engine 5. Today's date is %d-%02d-%02d — use the `search` tool for anything time-sensitive (recent API changes, plugin versions).\n\n"),
		Today.GetYear(), Today.GetMonth(), Today.GetDay());

	FString StaticPrompt = GetCachedStaticSystemPrompt();
	Ret += StaticPrompt;

	if (IUECPCoreModule::IsAvailable())
	{
		TArray<FUECPToolMeta> AlwaysShow;
		IUECPCoreModule::Get().GetToolDispatcher().GetAllToolMetadata(AlwaysShow);
		AlwaysShow.RemoveAll([](const FUECPToolMeta& M) { return !M.bAlwaysShow; });
		if (AlwaysShow.Num() > 0)
		{
			AlwaysShow.Sort([](const FUECPToolMeta& A, const FUECPToolMeta& B)
			{ return A.Action.ToString() < B.Action.ToString(); });
			Ret += TEXT("\n\n=== ALWAYS AVAILABLE (no get_tool_docs needed — invoke exactly as shown) ===\n");
			for (const FUECPToolMeta& M : AlwaysShow)
			{
				const FString Act = M.Action.ToString();
				const FString Umb = M.Umbrella.ToString();
				FString Call;
				if (Umb == TEXT("discovery"))
					Call = M.Params.IsEmpty() ? FString::Printf(TEXT("%s(...)"), *Act)
					                          : FString::Printf(TEXT("%s(%s)"), *Act, *M.Params);
				else
					Call = M.Params.IsEmpty() ? FString::Printf(TEXT("%s(action='%s')"), *Umb, *Act)
					                          : FString::Printf(TEXT("%s(action='%s', %s)"), *Umb, *Act, *M.Params);
				Ret += FString::Printf(TEXT("- %s — %s\n"), *Call, *M.Summary);
			}
		}
	}

	if (bIsCliAgent)
	{
		Ret += TEXT("\n\n=== CLI AGENT — READ THIS FIRST ===\n"
			"You are working inside an open Unreal Engine 5 editor. The project is a `.uproject` with binary `.uasset` files — **you cannot read or write project state through the filesystem or shell.** Listing directories, opening `.uasset` files, running `ls`/`Get-ChildItem`/`find`/`grep`, or trying to \"explore the workspace\" tells you nothing about Blueprints, materials, or levels. The editor's live state is only visible through the **UECP tools** in this session.\n\n"
			"**Use UECP tools, NOT your native tools.** Your runtime's built-in `Read` / `Write` / `Edit` / `Bash` / shell / `Grep` / `Glob` / web-fetch tools must NOT be used for project work — they can't see Unreal state, and any edit made outside UECP bypasses the editor and the build. The ONLY native tools you should reach for are your own planning / todo / task tools (e.g. TodoWrite) — those keep you organised without touching project state, so use them freely. Everything else goes through UECP.\n\n"
			"**Map every instinct to a UECP tool:**\n"
			"- list / read / write / find assets → `asset_management`, `find_asset_by_name`, `get_asset_summary`, `list_assets_in_folder`\n"
			"- read / write / edit C++ source → the **`cpp_tools`** umbrella (`read_cpp_file`, `write_cpp_file`, `edit_cpp_file`, `find_class_definition`, `get_class_summary`, `compile_project`). Do NOT open `.cpp`/`.h` with your native Read/Write/Edit even though they're text on disk — cpp_tools is wired into the module structure + UE build (`compile_project`); native edits bypass that.\n"
			"- explore the codebase / find the right action → `search_tools`, `get_tool_docs`\n"
			"- run a shell command against the project → there's a UECP tool for it (git → `git_tools`; editor console → `editor_utility(action='exec_console_command')`)\n\n"
			"Only fall back to a native shell/git tool if the user **explicitly** asks for a raw terminal action (e.g. \"git push\"). Otherwise, route everything through UECP.\n\n"
			"**MCP namespace:** UECP tools appear under the `uecp` server in your tool list — depending on your client, you may see them as `uecp/blueprint`, `uecp__blueprint`, `mcp__uecp__blueprint`, etc. Treat any tool whose name contains an umbrella keyword (`blueprint`, `level_actor`, `material`, `widget`, `niagara`, `animation`, …) as the corresponding UECP tool.");
	}

	if (!W->CustomInstructions.IsEmpty())
	{
		Ret += TEXT("\n\n=== USER CUSTOM INSTRUCTIONS ===\n");
		Ret += W->CustomInstructions;
		Ret += TEXT("\n=== END CUSTOM INSTRUCTIONS ===\n");
	}

	// Axivor: auto-detected project conventions (grid, defaults, folders, naming, plugins).
	Ret += UECPProjectConventions::BuildBlock();

	FString AssetSummary = FAssetReferenceManager::Get().GetSummaryForAI();
	if (!AssetSummary.IsEmpty())
	{
		Ret += TEXT("\n\n");
		Ret += AssetSummary;
	}

	FString GddContent = IUECPCoreModule::Get().GetGddService().GetContentForAI();
	if (!GddContent.IsEmpty())
	{
		// Axivor: the GDD was the only unbounded prompt section — cap it so long design docs
		// cannot starve the tool definitions and history of context.
		constexpr int32 MaxGddChars = 24000;
		if (GddContent.Len() > MaxGddChars)
		{
			const int32 Omitted = GddContent.Len() - MaxGddChars;
			GddContent = GddContent.Left(MaxGddChars);
			GddContent += FString::Printf(TEXT("\n[GDD truncated: %d more characters omitted — ask the user to trim the enabled GDD files or use the gdd tool to read specific sections.]\n"), Omitted);
		}
		Ret += TEXT("\n\n");
		Ret += GddContent;
	}

	FString AiMemoryContent = IUECPCoreModule::Get().GetAiMemoryService().GetContentForAI();
	if (!AiMemoryContent.IsEmpty())
	{
		Ret += TEXT("\n\n");
		Ret += AiMemoryContent;
	}

	FString WorkingNotesContent = W->GetArchitectWorkingNotesForAI(ChatID);
	if (!WorkingNotesContent.IsEmpty())
	{
		Ret += TEXT("\n\n");
		Ret += WorkingNotesContent;
	}

	FString PlanContent = FPlanManager::Get().GetPlanForAI(ChatID);
	if (!PlanContent.IsEmpty())
	{
		Ret += TEXT("\n\n");
		Ret += PlanContent;
	}
	FString TaskContent = FTaskManager::Get().GetTasksForAI(ChatID);
	if (!TaskContent.IsEmpty())
	{
		Ret += TEXT("\n\n");
		Ret += TaskContent;
	}

	FString UserLang = FUIConfigManager::Get().GetLanguage();
	if (!UserLang.IsEmpty() && UserLang != TEXT("en"))
	{
		FString LangName;
		if (UserLang == TEXT("es")) LangName = TEXT("Spanish");
		else if (UserLang == TEXT("fr")) LangName = TEXT("French");
		else if (UserLang == TEXT("de")) LangName = TEXT("German");
		else if (UserLang == TEXT("zh")) LangName = TEXT("Chinese");
		else if (UserLang == TEXT("ja")) LangName = TEXT("Japanese");
		else if (UserLang == TEXT("ru")) LangName = TEXT("Russian");
		else if (UserLang == TEXT("pt")) LangName = TEXT("Portuguese");
		else if (UserLang == TEXT("ko")) LangName = TEXT("Korean");
		else if (UserLang == TEXT("it")) LangName = TEXT("Italian");
		else if (UserLang == TEXT("ar")) LangName = TEXT("Arabic");
		else if (UserLang == TEXT("nl")) LangName = TEXT("Dutch");
		else if (UserLang == TEXT("tr")) LangName = TEXT("Turkish");
		else LangName = UserLang;

		Ret += FString::Printf(TEXT(
			"\n\n=== LANGUAGE ===\n"
			"The user's language is %s.\n"
			"CONVERSATIONAL TEXT: Write all conversational responses (explanations, summaries, status updates) in %s.\n"
			"COMMENT BOXES: Write the 'text' field in comments arrays in %s.\n"
			"TOOL JSON: ALL tool call JSON STRUCTURAL parameters (handles, pin names, node IDs, variable names, custom_name, action, type fields) MUST remain in English/ASCII. "
			"NEVER translate handle names (fn.KismetMathLibrary.*), pin names (execute, then, ReturnValue), node IDs, or variable_type values. "
			"Only the comment 'text' field and your conversational text should be in %s.\n"
			"CRITICAL: custom_name MUST be provided for EVERY ev.CustomEvent — use PascalCase (TakeDamage, StartSprint, OnPlayerDeath). NEVER use the 'id' as the event name. NEVER translate event names.\n"
			"NAMING: All custom events and functions MUST use PascalCase (TakeDamage, CalculateScore, NOT take_damage, ev_crouch). This matches Unreal Engine conventions.\n"
			"REPLICATION RULE: Do NOT add Server_/Client_/Multicast_ prefixes or set_function_replication UNLESS the user EXPLICITLY asks for multiplayer, networking, or replication. By default, create simple local custom events (e.g. 'ToggleCrouch', 'StartSprint') with NO replication.\n"
		), *LangName, *LangName, *LangName, *LangName);
	}

	Ret += TEXT("\n\n**Asset links:** wrap every asset name you mention as `[AssetName](ue://asset?name=AssetName)` (plain text only, never inside code blocks).");

	switch (Mode)
	{
	case EAIInteractionMode::JustChat:
		Ret += TEXT("\n\n=== INTERACTION MODE: JUST CHAT ===\n"
			"Do NOT call any tools that modify or create assets. Answer questions, explain code, and discuss approaches only. Read-only inspection tools are fine if the user asks.\n"
			"=== END MODE ===");
		break;
	case EAIInteractionMode::AskBeforeEdit:
		Ret += TEXT("\n\n=== INTERACTION MODE: ASK BEFORE EDIT ===\n"
			"Before calling any tool that creates, modifies, or deletes assets, summarise WHY and WHAT the tool call will do in one or two sentences. The plugin's permission gate will show the user a confirmation — your summary becomes the explanation they read.\n"
			"=== END MODE ===");
		break;
	case EAIInteractionMode::PlanMode:
		Ret += TEXT("\n\n=== INTERACTION MODE: PLAN ===\n"
			"PLAN-ONLY. Do NOT call any build/create/modify tools.\n"
			"- If the request is vague or has meaningful open choices, prefer calling ask_user with structured questions (single/multi-select + options) over typing questions as plain text — it blocks for the user's answer and is far easier for them to respond to. Keep it to the few decisions that actually shape the plan.\n"
			"- Record the plan BRIEF with project_plan(action='create_plan', title, context). The brief is the durable 'what + why': the goal, exact asset names + their paths, conventions/guidelines to follow, key decisions (incl. anything from ask_user answers) and the reasoning. It is NOT a checklist — a fresh session must be able to execute from it without re-deriving everything.\n"
			"- Lay out the work as a checklist with task(action='set_tasks', items=[{content, status}]) — concrete, atomic, verifiable tasks (status defaults to pending).\n"
			"- End by asking: 'Shall I proceed?'. When the user confirms, call proceed_with_plan (switches the chat into their execution mode automatically), then start building and keep the tasks current (task update_task as you finish each). Do NOT tell them to switch modes manually.\n"
			"- Revision requests: call create_plan again (brief) and/or set_tasks again (checklist).\n"
			"=== END MODE ===");
		break;
	case EAIInteractionMode::AutoEdit:
	default:
		Ret += TEXT("\n\n=== INTERACTION MODE: AUTO EDIT ===\n"
			"Proceed autonomously — no permission summaries needed. Call tools directly. Show the user what you did after the fact, not before.\n"
			"=== END MODE ===");
		break;
	}

	return Ret;
}
