// Copyright 2026, BlueprintsLab, All rights reserved

#include "FUECPAgentRunnerCoordinator.h"
#include "Services/IUECPCrewService.h"
#include "SUECPMainWidget.h"
#include "Widget/UUECPAppBridge.h"
#include "Widget/UUECPSettingsBridge.h"
#include "AgentRunnerTypes.h"
#include "ApiKeyManager.h"
#include "Managers/PlanManager.h"
#include "Managers/SettingsManager.h"
#include "Managers/UpdateManager.h"
#include "Managers/ProviderConfigManager.h"
#include "HAL/PlatformFileManager.h"
#include "Utils/EditorRuntime.h"
#include "UECPCoreModule.h"
#include "Services/IUECPAiMemoryService.h"
#include "Services/IUECPACPRegistryService.h"
#include "Services/IUECPArchitectService.h"
#include "Services/IUECPExtensionService.h"
#include "Services/IUECPGddService.h"
#include "Services/IUECPMcpInfoService.h"
#include "Services/IUECPScannerService.h"
#include "Services/UECPCrewChatTokenStore.h"
#include "Services/IUECPNotificationService.h"
#include "Services/IUECPVoiceService.h"
#include "MCPToolsLog.h"
#include "ACP/FUECPACPSession.h"
#include "ACP/FUECPACPEventMapper.h"
#include "UECPACPModule.h"
#include "Containers/Ticker.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "IPAddress.h"
#include "Interfaces/IPluginManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "HAL/PlatformMisc.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ConfigCacheIni.h"
#include "Async/Async.h"
#include "Editor.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <Windows.h>
#include <excpt.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif

namespace
{
	void EnsureAntigravityConfigured(const FString& AgentId)
	{
		if (!AgentId.Contains(TEXT("antigravity"), ESearchCase::IgnoreCase))
		{
			return;
		}

		FString GeminiKey = FPlatformMisc::GetEnvironmentVariable(TEXT("GEMINI_API_KEY"));
		if (GeminiKey.IsEmpty())
		{
			// Check active slot first
			const FApiKeySlot ActiveSlot = FApiKeyManager::Get().GetActiveSlot();
			if ((ActiveSlot.Provider.Contains(TEXT("gemini"), ESearchCase::IgnoreCase) ||
				 ActiveSlot.Provider.Contains(TEXT("antigravity"), ESearchCase::IgnoreCase) ||
				 ActiveSlot.Provider.Contains(TEXT("google"), ESearchCase::IgnoreCase)) &&
				!ActiveSlot.ApiKey.IsEmpty())
			{
				GeminiKey = ActiveSlot.ApiKey;
			}

			// If not found in active slot, scan all slots
			if (GeminiKey.IsEmpty())
			{
				for (const FApiKeySlot& Slot : FApiKeyManager::Get().GetAllSlots())
				{
					if ((Slot.Provider.Contains(TEXT("gemini"), ESearchCase::IgnoreCase) ||
						 Slot.Provider.Contains(TEXT("antigravity"), ESearchCase::IgnoreCase) ||
						 Slot.Provider.Contains(TEXT("google"), ESearchCase::IgnoreCase)) &&
						!Slot.ApiKey.IsEmpty())
					{
						GeminiKey = Slot.ApiKey;
						break;
					}
				}
			}
		}

		if (!GeminiKey.IsEmpty())
		{
			FPlatformMisc::SetEnvironmentVar(TEXT("GEMINI_API_KEY"), *GeminiKey);
		}

		FString UserProfile = FPlatformMisc::GetEnvironmentVariable(TEXT("USERPROFILE"));
		if (UserProfile.IsEmpty())
		{
			UserProfile = FPlatformProcess::UserHomeDir();
		}
		if (!UserProfile.IsEmpty())
		{
			const FString SettingsDir = FPaths::Combine(UserProfile, TEXT(".gemini"), TEXT("antigravity-acp"));
			const FString SettingsFile = FPaths::Combine(SettingsDir, TEXT("settings.json"));

			IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
			if (!PlatformFile.DirectoryExists(*SettingsDir))
			{
				PlatformFile.CreateDirectoryTree(*SettingsDir);
			}

			// With auth.type set and no stored login, session/new blocks on a browser login it opens by itself.
			// That would undo a logout on the next spawn, so a missing login stays "needs sign-in" instead.
			if (GeminiKey.IsEmpty() && !PlatformFile.FileExists(*FPaths::Combine(SettingsDir, TEXT("acp_token.json"))))
			{
				FString SeededSettings;
				TSharedPtr<FJsonObject> SettingsObj;
				if (FFileHelper::LoadFileToString(SeededSettings, *SettingsFile) && SeededSettings.Contains(TEXT("oauth-personal"))
					&& FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(SeededSettings), SettingsObj) && SettingsObj.IsValid())
				{
					SettingsObj->RemoveField(TEXT("auth"));
					FString Cleared;
					const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Cleared);
					FJsonSerializer::Serialize(SettingsObj.ToSharedRef(), Writer);
					FFileHelper::SaveStringToFile(Cleared, *SettingsFile);
				}
				return;
			}

			const FString AuthType = (!GeminiKey.IsEmpty()) ? TEXT("gemini-api-key") : TEXT("oauth-personal");
			const FString JsonContent = FString::Printf(TEXT("{\n  \"auth\": {\n    \"type\": \"%s\"\n  }\n}\n"), *AuthType);

			FString ExistingContent;
			if (!FFileHelper::LoadFileToString(ExistingContent, *SettingsFile) || !ExistingContent.Contains(AuthType))
			{
				FFileHelper::SaveStringToFile(JsonContent, *SettingsFile);
			}
		}
	}

	void AppendAgentModelMessage(
		TArray<TSharedPtr<FJsonValue>>& History,
		const FString& Text)
	{
		TSharedPtr<FJsonObject> Msg = MakeShareable(new FJsonObject);
		Msg->SetStringField(TEXT("role"), TEXT("model"));
		TArray<TSharedPtr<FJsonValue>> Parts;
		TSharedPtr<FJsonObject> Part = MakeShareable(new FJsonObject);
		Part->SetStringField(TEXT("text"), Text);
		Parts.Add(MakeShareable(new FJsonValueObject(Part)));
		Msg->SetArrayField(TEXT("parts"), Parts);
		History.Add(MakeShareable(new FJsonValueObject(Msg)));
	}

	void StripMcpToolPrefixes(FString& Name)
	{
		static const TCHAR* const McpPrefixes[] = {
			TEXT("mcp__uecp__"),
			TEXT("mcp__unreal__"),
			TEXT("mcp__unreal_handshake__"),
			TEXT("uecp-"),
			TEXT("unreal-handshake-"),
		};
		for (const TCHAR* Prefix : McpPrefixes)
		{
			const FString P(Prefix);
			if (Name.StartsWith(P))
			{
				Name = Name.Mid(P.Len());
				break;
			}
		}
		if (Name.StartsWith(TEXT("Tool: ")))
			Name = Name.Mid(6);
		int32 SlashIdx = INDEX_NONE;
		if (Name.FindChar(TEXT('/'), SlashIdx))
			Name = Name.Mid(SlashIdx + 1);
	}

	void AppendAgentToolBubble(
		TArray<TSharedPtr<FJsonValue>>& History,
		const FString& Label,
		const FString& InputPreview)
	{
		FString Text = FString::Printf(TEXT("[TOOL_RESULT:%s:success]\n%s"), *Label, *InputPreview);
		TSharedPtr<FJsonObject> Msg = MakeShareable(new FJsonObject);
		Msg->SetStringField(TEXT("role"), TEXT("user"));
		TArray<TSharedPtr<FJsonValue>> Parts;
		TSharedPtr<FJsonObject> Part = MakeShareable(new FJsonObject);
		Part->SetStringField(TEXT("text"), Text);
		Parts.Add(MakeShareable(new FJsonValueObject(Part)));
		Msg->SetArrayField(TEXT("parts"), Parts);
		History.Add(MakeShareable(new FJsonValueObject(Msg)));
	}

	TArray<TSharedPtr<FJsonValue>> FilterHistoryForAgent(const TArray<TSharedPtr<FJsonValue>>& History)
	{
		TArray<TSharedPtr<FJsonValue>> Filtered;
		for (const TSharedPtr<FJsonValue>& Val : History)
		{
			if (!Val.IsValid() || Val->Type != EJson::Object)
			{
				Filtered.Add(Val);
				continue;
			}
			TSharedPtr<FJsonObject> Obj = Val->AsObject();
			FString Role;
			Obj->TryGetStringField(TEXT("role"), Role);

			if (Role == TEXT("agent_thinking") || Role == TEXT("context") || Role == TEXT("tool_bubble"))
				continue;

			if (Role == TEXT("user"))
			{
				const TArray<TSharedPtr<FJsonValue>>* Parts;
				if (Obj->TryGetArrayField(TEXT("parts"), Parts) && Parts->Num() > 0)
				{
					const TSharedPtr<FJsonObject>* PartObj;
					FString TextContent;
					if ((*Parts)[0]->TryGetObject(PartObj))
						(*PartObj)->TryGetStringField(TEXT("text"), TextContent);
					if (TextContent.StartsWith(TEXT("[TOOL_RESULT:")))
						continue;
				}
			}
			Filtered.Add(Val);
		}
		return Filtered;
	}
}

void FUECPAgentRunnerCoordinator::InitializeShellRefs(TWeakPtr<SUECPMainWidget> InShell,
	TWeakObjectPtr<UUECPAppBridge> InBridge)
{
	Shell  = InShell;
	Bridge = InBridge;
}

void FUECPAgentRunnerCoordinator::StopAllAgents()
{
	TSharedPtr<SUECPMainWidget> W = Shell.Pin();
	if (!W.IsValid()) return;

	TArray<FString> ChatIDs;
	W->AgentInstances.GetKeys(ChatIDs);
	for (const FString& ID : ChatIDs)
	{
		StopAgentForChat(ID);
	}
}

void FUECPAgentRunnerCoordinator::StopAgentForChat(const FString& ChatID)
{
	TSharedPtr<SUECPMainWidget> W = Shell.Pin();
	if (!W.IsValid()) return;

	TSharedPtr<FAgentRunnerInstance>* Found = W->AgentInstances.Find(ChatID);
	if (!Found) return;

	TSharedPtr<FAgentRunnerInstance> Inst = *Found;

	if (Inst->TickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(Inst->TickerHandle);
		Inst->TickerHandle.Reset();
	}

	if (Inst->ACPSession.IsValid())
	{
		Inst->ACPSession->Stop();
		Inst->ACPSession.Reset();
	}

	if (Inst->SourceView != EAgentSourceView::ProjectScanner && !Inst->LocalHistory.IsEmpty())
	{
		W->ArchitectConversationHistory = Inst->LocalHistory;
		W->SaveArchitectChatHistory(Inst->ChatID);
	}

	W->AgentInstances.Remove(ChatID);

	if (Inst->SourceView != EAgentSourceView::ProjectScanner)
	{
		W->ArchitectThinkingChats.Remove(ChatID);
		W->bIsArchitectThinking =
			W->ArchitectThinkingChats.Contains(W->ActiveArchitectChatID) ||
			W->PendingArchitectRequests.Contains(W->ActiveArchitectChatID);
		if (ChatID == W->ActiveArchitectChatID)
			W->RefreshArchitectChatView();
	}
}

bool FUECPAgentRunnerCoordinator::HasActiveAgents() const
{
	if (TSharedPtr<SUECPMainWidget> W = Shell.Pin()) return W->AgentInstances.Num() > 0;
	return false;
}

void FUECPAgentRunnerCoordinator::RefreshAgentView(const FAgentRunnerInstance& Inst)
{
	TSharedPtr<SUECPMainWidget> W = Shell.Pin();
	if (!W.IsValid()) return;

	if (Inst.SourceView == EAgentSourceView::ProjectScanner)
	{
		if (Inst.ChatID == W->ActiveProjectChatID)
		{
			W->RefreshProjectChatView();
		}
	}
	else
	{
		if (Inst.ChatID == W->ActiveArchitectChatID)
		{
			W->RefreshArchitectChatView();
		}
	}
}

void FUECPAgentRunnerCoordinator::SaveAgentChatHistory(const FAgentRunnerInstance& Inst)
{
	TSharedPtr<SUECPMainWidget> W = Shell.Pin();
	if (!W.IsValid()) return;
	if (Inst.SourceView == EAgentSourceView::ProjectScanner)
		W->SaveProjectChatHistory(Inst.ChatID);
	else
		W->SaveArchitectChatHistory(Inst.ChatID);
}

void FUECPAgentRunnerCoordinator::RefreshAgentLiveMessage(FAgentRunnerInstance& Inst, bool bProcessing)
{
	TSharedPtr<SUECPMainWidget> W = Shell.Pin();
	if (!W.IsValid()) return;

	FString LiveText = Inst.TextAccumulator;
	if (LiveText.IsEmpty())
		LiveText = bProcessing ? TEXT("*…*") : TEXT("Done.");

	auto& History = W->GetAgentHistory(Inst);

	if (!Inst.bLiveMessageActive)
	{
		AppendAgentModelMessage(History, LiveText);
		Inst.bLiveMessageActive = true;
	}
	else
	{
		for (int32 i = History.Num() - 1; i >= 0; --i)
		{
			if (TSharedPtr<FJsonObject> Obj = History[i]->AsObject())
			{
				FString Role;
				Obj->TryGetStringField(TEXT("role"), Role);
				if (Role == TEXT("model"))
				{
					const TArray<TSharedPtr<FJsonValue>>* Parts;
					if (Obj->TryGetArrayField(TEXT("parts"), Parts) && Parts->Num() > 0)
						if (TSharedPtr<FJsonObject> Part = (*Parts)[0]->AsObject())
							Part->SetStringField(TEXT("text"), LiveText);
					break;
				}
			}
		}
	}

	if (!W->bSuppressChatViewRefresh)
		RefreshAgentView(Inst);
}

void FUECPAgentRunnerCoordinator::OnAgentConfirmAction(const FString& Decision)
{
	TSharedPtr<SUECPMainWidget> W = Shell.Pin();
	if (!W.IsValid()) return;

	W->bAgentConfirmPending = false;

	FAgentRunnerInstance* Inst = nullptr;
	for (auto& Pair : W->AgentInstances)
	{
		if (Pair.Value.IsValid() && Pair.Value->bConfirmPending)
		{
			Inst = Pair.Value.Get();
			break;
		}
	}

	if (!Inst || !Inst->ACPSession.IsValid()) return;

	const FString ToolCallId = Inst->ConfirmToolCallId;
	const FString ProceedId  = Inst->ConfirmProceedOptionId;
	Inst->bConfirmPending = false;
	Inst->ConfirmToolCallId.Reset();
	Inst->ConfirmProceedOptionId.Reset();

	const FString OptionId = (Decision == TEXT("proceed")) ? ProceedId : FString();
	Inst->ACPSession->RespondToPermission(ToolCallId, OptionId);

	if (W->AppBridgeObject)
		W->AppBridgeObject->ExecJs(TEXT("if(typeof onToolConfirmDone==='function')onToolConfirmDone()"));

	if (Decision == TEXT("stop")) StopAgentForChat(W->ActiveArchitectChatID);
}

void FUECPAgentRunnerCoordinator::SendQueryToInstance(FAgentRunnerInstance& Inst, FAgentProviderConfig& , const FString& Prompt)
{
	TSharedPtr<SUECPMainWidget> W = Shell.Pin();
	if (!W.IsValid() || !Inst.ACPSession.IsValid()) return;

	FString FullPrompt;
	if (!Inst.PendingModeChangeNotice.IsEmpty())
	{
		FullPrompt = Inst.PendingModeChangeNotice + Prompt;
		Inst.PendingModeChangeNotice.Reset();
	}
	else
	{
		FullPrompt = Prompt;
	}

	auto ExtractInlineImages = [](const TArray<TSharedPtr<FJsonValue>>& History) -> TArray<TPair<FString, FString>>
	{
		TArray<TPair<FString, FString>> Out;
		for (int32 i = History.Num() - 1; i >= 0; --i)
		{
			TSharedPtr<FJsonObject> Msg = History[i].IsValid() ? History[i]->AsObject() : nullptr;
			if (!Msg.IsValid()) continue;
			FString Role;
			Msg->TryGetStringField(TEXT("role"), Role);
			if (Role != TEXT("user")) continue;
			const TArray<TSharedPtr<FJsonValue>>* Parts = nullptr;
			if (!Msg->TryGetArrayField(TEXT("parts"), Parts) || !Parts) return Out;
			for (const TSharedPtr<FJsonValue>& PV : *Parts)
			{
				TSharedPtr<FJsonObject> P = PV.IsValid() ? PV->AsObject() : nullptr;
				if (!P.IsValid()) continue;
				const TSharedPtr<FJsonObject>* Inline = nullptr;
				if (!P->TryGetObjectField(TEXT("inline_data"), Inline) || !Inline) continue;
				FString Mime, Data;
				(*Inline)->TryGetStringField(TEXT("mime_type"), Mime);
				(*Inline)->TryGetStringField(TEXT("data"), Data);
				if (!Mime.IsEmpty() && !Data.IsEmpty()) Out.Emplace(Mime, Data);
			}
			return Out;
		}
		return Out;
	};

	if (Inst.SourceView != EAgentSourceView::ProjectScanner)
	{
		const TArray<TSharedPtr<FJsonValue>>& History = W->ArchitectConversationHistory;
		Inst.PendingACPImages = ExtractInlineImages(History);
	}

	Inst.PendingACPPrompt = MoveTemp(FullPrompt);
}

bool FUECPAgentRunnerCoordinator::OnAgentInstanceTick(TSharedPtr<FAgentRunnerInstance> Inst, float )
{
	TSharedPtr<SUECPMainWidget> W = Shell.Pin();
	if (!W.IsValid() || !Inst.IsValid() || !Inst->ACPSession.IsValid()) return false;
	return OnACPInstanceTick(Inst);
}

void FUECPAgentRunnerCoordinator::ProcessAgentMessage(FAgentRunnerInstance& Inst, const FString& JsonStr)
{
	TSharedPtr<SUECPMainWidget> W = Shell.Pin();
	if (!W.IsValid()) return;

	TSharedPtr<FJsonObject> Msg;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonStr);
	if (!FJsonSerializer::Deserialize(Reader, Msg) || !Msg.IsValid())
		return;

	FString Type;
	Msg->TryGetStringField(TEXT("type"), Type);

	UE_LOG(LogTemp, Verbose, TEXT("AGENT_MSG: type='%s' chat='%s' bg=%d histLen=%d"),
		*Type, *Inst.ChatID,
		(Inst.ChatID != W->ActiveArchitectChatID) ? 1 : 0,
		Inst.LocalHistory.Num());

	auto& History = W->GetAgentHistory(Inst);
	const bool bIsProjectScanner = (Inst.SourceView == EAgentSourceView::ProjectScanner);

	if (Type == TEXT("text"))
	{
		FString Content;
		Msg->TryGetStringField(TEXT("content"), Content);
		bool bIsStream = false;
		Msg->TryGetBoolField(TEXT("is_stream"), bIsStream);

		Inst.bThinkingInProgress = false;
		if (!bIsStream && !Inst.TextAccumulator.IsEmpty())
			Inst.TextAccumulator += TEXT("\n\n");
		Inst.TextAccumulator += Content;
		RefreshAgentLiveMessage(Inst, true);
	}
	else if (Type == TEXT("tool_start"))
	{
		FString ToolName, Action, InputPreview, ToolUseId;
		Msg->TryGetStringField(TEXT("name"), ToolName);
		Msg->TryGetStringField(TEXT("action"), Action);
		Msg->TryGetStringField(TEXT("input_preview"), InputPreview);
		Msg->TryGetStringField(TEXT("tool_use_id"), ToolUseId);

		static const TSet<FString> HiddenTools = {
			TEXT("ToolSearch"), TEXT("AskUserQuestion"),
			TEXT("Read"), TEXT("Grep"), TEXT("Glob"), TEXT("Bash"),
			TEXT("Write"), TEXT("Edit"), TEXT("Agent"), TEXT("Skill"),
			TEXT("WebFetch"), TEXT("WebSearch"), TEXT("NotebookEdit"),
			TEXT("TodoWrite"), TEXT("SendMessage")
		};
		if (HiddenTools.Contains(ToolName))
		{
			if (!ToolUseId.IsEmpty())
				Inst.HiddenToolUseIds.Add(ToolUseId);
			return;
		}

		StripMcpToolPrefixes(ToolName);

		static const TSet<FString> HiddenMcpTools = {
			TEXT("ask_user"), TEXT("proceed_with_plan"),
			TEXT("task"), TEXT("set_tasks"), TEXT("add_task"), TEXT("update_task"),
			TEXT("edit_task"), TEXT("remove_task"), TEXT("reorder_task"), TEXT("clear_tasks"), TEXT("get_tasks")
		};
		if (HiddenMcpTools.Contains(ToolName))
		{
			if (!ToolUseId.IsEmpty())
				Inst.HiddenToolUseIds.Add(ToolUseId);
			return;
		}

		Inst.bThinkingInProgress = false;
		Inst.ThinkingHistoryIndex = -1;
		Inst.ThinkingBlockId.Empty();

		FString Label = Action.IsEmpty()
			? ToolName
			: FString::Printf(TEXT("%s \xB7 %s"), *ToolName, *Action);

		if (Inst.bLiveMessageActive)
		{
			Inst.bLiveMessageActive = false;
			Inst.TextAccumulator.Empty();
		}

		AppendAgentToolBubble(History, Label, InputPreview);

		if (!ToolUseId.IsEmpty())
		{
			Inst.ToolBubbleMap.Add(ToolUseId, History.Num() - 1);
			if (!Action.IsEmpty())
				Inst.PendingToolActions.Add(ToolUseId, Action);
		}

		Inst.bHasImportantUpdate = true;
		if (!W->bSuppressChatViewRefresh)
			RefreshAgentView(Inst);
	}
	else if (Type == TEXT("tool_result"))
	{
		FString Content, ToolUseId, LateAction, LateRawInput;
		Msg->TryGetStringField(TEXT("content"), Content);
		Msg->TryGetStringField(TEXT("tool_use_id"), ToolUseId);
		Msg->TryGetStringField(TEXT("action"),      LateAction);
		Msg->TryGetStringField(TEXT("raw_input"),   LateRawInput);

		UE_LOG(LogUECPACP, Log,
			TEXT("tool_result inst=%s tool_use_id=%s action='%s' rawInputLen=%d contentLen=%d"),
			*Inst.ChatID, *ToolUseId, *LateAction, LateRawInput.Len(), Content.Len());

		if (!ToolUseId.IsEmpty() && Inst.HiddenToolUseIds.Contains(ToolUseId))
		{
			Inst.HiddenToolUseIds.Remove(ToolUseId);
			return;
		}

		bool bIsError = false;
		Msg->TryGetBoolField(TEXT("is_error"), bIsError);

		if (!bIsError)
		{
			FString ContentLower = Content.ToLower();
			FString TopLevel = ContentLower.Left(100);
			if (TopLevel.Contains(TEXT("\"success\":false")) ||
				TopLevel.Contains(TEXT("\"success\": false")) ||
				TopLevel.Contains(TEXT("\\\"success\\\": false")) ||
				TopLevel.Contains(TEXT("\\\"success\\\":false")))
				bIsError = true;
		}

		if (!Content.IsEmpty())
		{
			int32 BubbleIdx = INDEX_NONE;
			if (!ToolUseId.IsEmpty())
			{
				if (const int32* Found = Inst.ToolBubbleMap.Find(ToolUseId))
					BubbleIdx = *Found;
			}

			auto AppendResultToBubble = [&](int32 Idx)
			{
				TSharedPtr<FJsonObject> Obj = History[Idx]->AsObject();
				if (!Obj.IsValid()) return;
				const TArray<TSharedPtr<FJsonValue>>* Parts;
				if (!Obj->TryGetArrayField(TEXT("parts"), Parts) || Parts->Num() == 0) return;
				TSharedPtr<FJsonObject> Part = (*Parts)[0]->AsObject();
				if (!Part.IsValid()) return;
				FString CurrentText;
				Part->TryGetStringField(TEXT("text"), CurrentText);
				if (!CurrentText.StartsWith(TEXT("[TOOL_RESULT:"))) return;
				if (!LateAction.IsEmpty() && !CurrentText.Contains(FString(TEXT(" \xB7 ")) + LateAction + TEXT(":")))
				{
					if (CurrentText.Contains(TEXT(" \xB7 other:")))
					{
						CurrentText = CurrentText.Replace(
							TEXT(" \xB7 other:"),
							*FString::Printf(TEXT(" \xB7 %s:"), *LateAction));
					}
					else
					{
						auto InsertActionBefore = [&](const TCHAR* Status)
						{
							const int32 Idx = CurrentText.Find(Status, ESearchCase::IgnoreCase, ESearchDir::FromEnd);
							if (Idx == INDEX_NONE) return false;
							CurrentText = CurrentText.Left(Idx)
								+ FString::Printf(TEXT(" \xB7 %s"), *LateAction)
								+ CurrentText.Mid(Idx);
							return true;
						};
						if (!InsertActionBefore(TEXT(":success]"))) InsertActionBefore(TEXT(":error]"));
					}
				}
				if (!LateRawInput.IsEmpty() && !CurrentText.Contains(TEXT("\n---\n")))
				{
					int32 HeaderEnd = CurrentText.Find(TEXT("]"));
					if (HeaderEnd != INDEX_NONE)
					{
						CurrentText = CurrentText.Left(HeaderEnd + 1) + TEXT("\n") + LateRawInput;
					}
				}
				if (bIsError)
					CurrentText = CurrentText.Replace(TEXT(":success]"), TEXT(":error]"));
				Part->SetStringField(TEXT("text"), CurrentText + TEXT("\n---\n") + Content);
			};

			if (BubbleIdx != INDEX_NONE)
			{
				AppendResultToBubble(BubbleIdx);
			}
			else
			{
				for (int32 i = History.Num() - 1; i >= 0; --i)
				{
					TSharedPtr<FJsonObject> Obj = History[i]->AsObject();
					if (!Obj.IsValid()) continue;
					FString Role;
					Obj->TryGetStringField(TEXT("role"), Role);
					if (Role != TEXT("user")) continue;
					const TArray<TSharedPtr<FJsonValue>>* Parts;
					if (!Obj->TryGetArrayField(TEXT("parts"), Parts) || Parts->Num() == 0) continue;
					TSharedPtr<FJsonObject> Part = (*Parts)[0]->AsObject();
					if (!Part.IsValid()) continue;
					FString CurrentText;
					Part->TryGetStringField(TEXT("text"), CurrentText);
					if (!CurrentText.StartsWith(TEXT("[TOOL_RESULT:"))) continue;
					Part->SetStringField(TEXT("text"), CurrentText + TEXT("\n---\n") + Content);
					break;
				}
			}
			Inst.bHasImportantUpdate = true;
			if (!W->bSuppressChatViewRefresh)
				RefreshAgentView(Inst);
		}

		static const TSet<FString> PlanWriteActions = {
			TEXT("create_plan"), TEXT("update_step"), TEXT("clear_plan"), TEXT("load_plan")
		};
		if (PlanWriteActions.Contains(LateAction))
		{
			if (LateAction == TEXT("clear_plan"))
				FPlanManager::Get().ClearPlan(Inst.ChatID);
			else
				FPlanManager::Get().ImportPlan(TEXT("mcp"), Inst.ChatID);
			W->NotifyPlanUpdated(Inst.ChatID);
		}
	}
	else if (Type == TEXT("thinking"))
	{
		FString ThinkContent;
		Msg->TryGetStringField(TEXT("content"), ThinkContent);
		if (!ThinkContent.IsEmpty())
		{
			if (!Inst.bThinkingInProgress)
			{
				Inst.bThinkingInProgress = true;

				if (!Inst.ThinkingBlockId.IsEmpty() && Inst.ThinkingHistoryIndex >= 0 && Inst.ThinkingHistoryIndex < History.Num())
				{
					TSharedPtr<FJsonObject> Existing = History[Inst.ThinkingHistoryIndex]->AsObject();
					FString ExistingRole;
					if (Existing.IsValid() && Existing->TryGetStringField(TEXT("role"), ExistingRole) && ExistingRole == TEXT("agent_thinking"))
					{
						UE_LOG(LogMCPTool, Log, TEXT("[Thinking] REUSE block: idx=%d id=%s histLen=%d"),
							Inst.ThinkingHistoryIndex, *Inst.ThinkingBlockId, History.Num());
						Inst.ThinkingContent += TEXT("\n\n---\n\n");
						Inst.ThinkingContent += ThinkContent;

						const TArray<TSharedPtr<FJsonValue>>* Parts;
						if (Existing->TryGetArrayField(TEXT("parts"), Parts) && Parts->Num() > 0)
							if (TSharedPtr<FJsonObject> Part = (*Parts)[0]->AsObject())
								Part->SetStringField(TEXT("text"), Inst.ThinkingContent);

						if (!W->bSuppressChatViewRefresh && W->AppBridgeObject)
						{
							FString Delta = TEXT("\n\n---\n\n") + ThinkContent;
							FString EscDelta = Delta
								.Replace(TEXT("\\"), TEXT("\\\\"))
								.Replace(TEXT("'"), TEXT("\\'"))
								.Replace(TEXT("\n"), TEXT("\\n"))
								.Replace(TEXT("\r"), TEXT(""));
							FString JS = FString::Printf(
								TEXT("(function(){var p=document.getElementById('tpre-%s');if(p){p.textContent+='%s';p.scrollTop=p.scrollHeight;}})()"),
								*Inst.ThinkingBlockId, *EscDelta);
							W->AppBridgeObject->ExecJs(JS);
						}
						return;
					}
				}

				Inst.ThinkingContent = ThinkContent;
				Inst.ThinkingHistoryIndex = History.Num();
				Inst.ThinkingBlockId = FGuid::NewGuid().ToString().Replace(TEXT("-"), TEXT(""));
				UE_LOG(LogMCPTool, Log, TEXT("[Thinking] NEW block: idx=%d id=%s histLen=%d"),
					Inst.ThinkingHistoryIndex, *Inst.ThinkingBlockId, History.Num());

				TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
				Obj->SetStringField(TEXT("role"), TEXT("agent_thinking"));
				Obj->SetStringField(TEXT("block_id"), Inst.ThinkingBlockId);
				TArray<TSharedPtr<FJsonValue>> Parts;
				TSharedPtr<FJsonObject> Part = MakeShared<FJsonObject>();
				Part->SetStringField(TEXT("text"), Inst.ThinkingContent);
				Parts.Add(MakeShared<FJsonValueObject>(Part));
				Obj->SetArrayField(TEXT("parts"), Parts);
				History.Add(MakeShared<FJsonValueObject>(Obj));

				if (!W->bSuppressChatViewRefresh)
					RefreshAgentView(Inst);
			}
			else
			{
				Inst.ThinkingContent += ThinkContent;

				if (Inst.ThinkingHistoryIndex >= 0 && Inst.ThinkingHistoryIndex < History.Num())
				{
					if (TSharedPtr<FJsonObject> Existing = History[Inst.ThinkingHistoryIndex]->AsObject())
					{
						const TArray<TSharedPtr<FJsonValue>>* Parts;
						if (Existing->TryGetArrayField(TEXT("parts"), Parts) && Parts->Num() > 0)
							if (TSharedPtr<FJsonObject> Part = (*Parts)[0]->AsObject())
								Part->SetStringField(TEXT("text"), Inst.ThinkingContent);
					}
				}

				if (!W->bSuppressChatViewRefresh && W->AppBridgeObject && !Inst.ThinkingBlockId.IsEmpty())
				{
					FString EscDelta = ThinkContent
						.Replace(TEXT("\\"), TEXT("\\\\"))
						.Replace(TEXT("'"), TEXT("\\'"))
						.Replace(TEXT("\n"), TEXT("\\n"))
						.Replace(TEXT("\r"), TEXT(""));
					FString JS = FString::Printf(
						TEXT("(function(){var p=document.getElementById('tpre-%s');if(p){p.textContent+='%s';p.scrollTop=p.scrollHeight;}})()"),
						*Inst.ThinkingBlockId, *EscDelta);
					W->AppBridgeObject->ExecJs(JS);
				}
			}
		}
	}
	else if (Type == TEXT("tool_confirm_request"))
	{
		FString ToolName, Action, Preview;
		Msg->TryGetStringField(TEXT("name"), ToolName);
		Msg->TryGetStringField(TEXT("action"), Action);
		Msg->TryGetStringField(TEXT("input_preview"), Preview);

		FString Label = Action.IsEmpty() ? ToolName : FString::Printf(TEXT("%s \xB7 %s"), *ToolName, *Action);

		Inst.bConfirmPending = true;
		Inst.ConfirmToolName = Label;
		Inst.ConfirmPreview  = Preview.Left(500);
		W->bAgentConfirmPending = true;

		if (W->AppBridgeObject)
			W->AppBridgeObject->PushToolConfirmation(Label, Preview.Left(8000));
	}
	else if (Type == TEXT("status"))
	{
	}
	else if (Type == TEXT("done"))
	{
		Inst.bHasImportantUpdate = true;
		if (!Inst.TextAccumulator.IsEmpty() || Inst.bLiveMessageActive)
			RefreshAgentLiveMessage(Inst, false);

		if (!Inst.TextAccumulator.IsEmpty())
		{
			UE_LOG(LogTemp, Verbose, TEXT("BP Gen Agent: Final answer reached — calling PlayTTSForResponse (%d chars)"), Inst.TextAccumulator.Len());
			IUECPCoreModule::Get().GetVoiceService().PlayTTSForResponse(Inst.TextAccumulator);
		}

		FTimerHandle MemoryExtractionTimer;
		GEditor->GetTimerManager()->SetTimer(
			MemoryExtractionTimer,
			FTimerDelegate::CreateLambda([]()
			{
				IUECPCoreModule::Get().GetAiMemoryService().ExtractMemoriesFromConversation();
			}),
			2.0f, false);

		int32 In = 0, Out = 0;
		const TSharedPtr<FJsonObject>* UsageObj;
		if (Msg->TryGetObjectField(TEXT("usage"), UsageObj) && UsageObj->IsValid())
		{
			(*UsageObj)->TryGetNumberField(TEXT("input_tokens"), In);
			(*UsageObj)->TryGetNumberField(TEXT("output_tokens"), Out);
		}
		if (In == 0 && Out == 0)
		{
			int32 HistoryChars = 0;
			for (const TSharedPtr<FJsonValue>& V : History)
			{
				TSharedPtr<FJsonObject> O = V.IsValid() ? V->AsObject() : nullptr;
				if (!O.IsValid()) continue;
				const TArray<TSharedPtr<FJsonValue>>* Parts;
				if (O->TryGetArrayField(TEXT("parts"), Parts) && Parts->Num() > 0)
				{
					TSharedPtr<FJsonObject> P = (*Parts)[0]->AsObject();
					FString T;
					if (P.IsValid()) P->TryGetStringField(TEXT("text"), T);
					HistoryChars += T.Len();
				}
			}
			In = FMath::Max(In, HistoryChars / 4);
			Out = FMath::Max(Out, Inst.TextAccumulator.Len() / 4);
		}

		auto& Totals = W->AgentTokenTotals.FindOrAdd(Inst.ProviderName);
		Totals.Key += In;
		Totals.Value += Out;

		W->UpdateConversationTokens(Inst.ChatID, In, Out);

		if (!bIsProjectScanner && Inst.ChatID == W->ActiveArchitectChatID)
		{
			const int32 TokenCount = W->EstimateFullRequestTokens(Inst.LocalHistory);
			W->PushArchitectTokenCount(TokenCount);
		}

		Inst.bLiveMessageActive = false;
		Inst.TextAccumulator.Empty();
		Inst.ToolBubbleMap.Empty();
		Inst.bThinkingInProgress = false;
		Inst.ThinkingHistoryIndex = -1;
		Inst.ThinkingContent.Empty();
		Inst.ThinkingBlockId.Empty();
		W->CurrentToolCallDepth = 0;

		FString FinishedChatID = Inst.ChatID;
		W->ArchitectThinkingChats.Remove(FinishedChatID);

		if (bIsProjectScanner)
			W->bIsProjectThinking = false;
		else
			W->bIsArchitectThinking = W->ArchitectThinkingChats.Contains(W->ActiveArchitectChatID) || W->PendingArchitectRequests.Contains(W->ActiveArchitectChatID);

		if (!bIsProjectScanner)
		{
			const bool bActive = (FinishedChatID == W->ActiveArchitectChatID);

			if (bActive)
			{
				TArray<TSharedPtr<FJsonValue>> Saved = MoveTemp(W->ArchitectConversationHistory);
				W->ArchitectConversationHistory = Inst.LocalHistory;
				if (W->MaybeFinalizeCompactReplacement(FinishedChatID))
					Inst.LocalHistory = W->ArchitectConversationHistory;
				W->ArchitectConversationHistory = MoveTemp(Saved);
			}

			TArray<TSharedPtr<FJsonValue>> Saved = MoveTemp(W->ArchitectConversationHistory);
			W->ArchitectConversationHistory = Inst.LocalHistory;
			W->SaveArchitectChatHistory(FinishedChatID);
			W->ArchitectConversationHistory = MoveTemp(Saved);
			if (bActive)
			{
				W->ArchitectConversationHistory = Inst.LocalHistory;
				W->RefreshArchitectChatView();
			}
		}
		else
		{
			SaveAgentChatHistory(Inst);
		}
		if (!W->bSuppressChatViewRefresh && !bIsProjectScanner)
			;
		else if (!W->bSuppressChatViewRefresh)
			RefreshAgentView(Inst);

		if (!bIsProjectScanner)
			IUECPCoreModule::Get().GetArchitectService().OnTurnEnded().Broadcast(FinishedChatID, true);

		if (!bIsProjectScanner)
			W->DrainQueuedArchitectMessage(FinishedChatID);
	}
	else if (Type == TEXT("error"))
	{
		Inst.bHasImportantUpdate = true;
		FString ErrorMsg;
		Msg->TryGetStringField(TEXT("message"), ErrorMsg);

		if (Inst.bLiveMessageActive && History.Num() > 0)
		{
			History.RemoveAt(History.Num() - 1);
			Inst.bLiveMessageActive = false;
		}

		Inst.TextAccumulator.Empty();
		Inst.ToolBubbleMap.Empty();
		Inst.ThinkingContent.Empty();

		AppendAgentModelMessage(History,
			FString::Printf(TEXT("**%s Error:**\n%s"), *Inst.ProviderName, *ErrorMsg));

		FString ErrorChatID = Inst.ChatID;
		W->AgentInstances.Remove(ErrorChatID);
		W->ArchitectThinkingChats.Remove(ErrorChatID);

		if (bIsProjectScanner)
			W->bIsProjectThinking = false;
		else
			W->bIsArchitectThinking = W->AgentInstances.Contains(W->ActiveArchitectChatID) || W->PendingArchitectRequests.Contains(W->ActiveArchitectChatID);

		if (!bIsProjectScanner)
		{
			TArray<TSharedPtr<FJsonValue>> Saved = MoveTemp(W->ArchitectConversationHistory);
			W->ArchitectConversationHistory = Inst.LocalHistory;
			W->SaveArchitectChatHistory(ErrorChatID);
			W->ArchitectConversationHistory = MoveTemp(Saved);
			if (ErrorChatID == W->ActiveArchitectChatID)
			{
				W->ArchitectConversationHistory = Inst.LocalHistory;
				W->RefreshArchitectChatView();
			}
		}
		else
		{
			SaveAgentChatHistory(Inst);
		}
		if (!W->bSuppressChatViewRefresh && bIsProjectScanner)
			RefreshAgentView(Inst);

		if (!bIsProjectScanner)
			IUECPCoreModule::Get().GetArchitectService().OnTurnEnded().Broadcast(ErrorChatID, false);
	}
}

void FUECPAgentRunnerCoordinator::SpawnAgentInstance(const FString& ChatID, FAgentProviderConfig& Config, const FString& Prompt, EAgentSourceView SourceView)
{
	TSharedPtr<SUECPMainWidget> W = Shell.Pin();
	if (!W.IsValid()) return;

	if (W->AgentInstances.Contains(ChatID)) return;

	if (Config.ACPCommand.IsEmpty())
	{
		auto& H = (SourceView == EAgentSourceView::ProjectScanner)
			? W->ProjectConversationHistory : W->ArchitectConversationHistory;
		AppendAgentModelMessage(H, TEXT("**Agent not installed.** Open Settings \u2192 AI Agent and install an ACP agent from the catalog."));
		if (SourceView == EAgentSourceView::ProjectScanner) { W->bIsProjectThinking = false; W->RefreshProjectChatView(); }
		else { W->bIsArchitectThinking = false; W->RefreshArchitectChatView(); }
		return;
	}

	SpawnACPInstance(ChatID, Config, Prompt, SourceView);
}

namespace
{
	FString SynthesizeAgentJsonFromACPEvent(const FUECPACPStreamEvent& Ev)
	{
		auto Write = [](const TSharedRef<FJsonObject>& Obj) -> FString
		{
			FString Out;
			const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> W =
				TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
			FJsonSerializer::Serialize(Obj, W);
			W->Close();
			return Out;
		};

		switch (Ev.Kind)
		{
		case EUECPACPStreamEventKind::Text:
		{
			const TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetStringField(TEXT("type"),    TEXT("text"));
			O->SetStringField(TEXT("content"), Ev.Text);
			O->SetBoolField  (TEXT("is_stream"), true);
			return Write(O);
		}
		case EUECPACPStreamEventKind::Thinking:
		{
			const TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetStringField(TEXT("type"),    TEXT("thinking"));
			O->SetStringField(TEXT("content"), Ev.Text);
			O->SetBoolField  (TEXT("is_stream"), true);
			return Write(O);
		}
		case EUECPACPStreamEventKind::ToolStart:
		{
			UE_LOG(LogUECPACP, Log, TEXT("tool_start id=%s kind=%s title=%s action=%s"),
				*Ev.ToolCallId, *Ev.ToolKind, *Ev.ToolTitle, *Ev.ToolAction);

			const FString LabelAction = Ev.ToolAction;

			const FString InputPreview = !Ev.ToolRawInputJson.IsEmpty()
				? Ev.ToolRawInputJson
				: Ev.ToolContentText;

			const TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetStringField(TEXT("type"),          TEXT("tool_start"));
			O->SetStringField(TEXT("name"),          Ev.ToolTitle.IsEmpty() ? Ev.ToolKind : Ev.ToolTitle);
			O->SetStringField(TEXT("action"),        LabelAction);
			O->SetStringField(TEXT("input_preview"), InputPreview);
			O->SetStringField(TEXT("tool_use_id"),   Ev.ToolCallId);
			return Write(O);
		}
		case EUECPACPStreamEventKind::ToolResult:
		{
			UE_LOG(LogUECPACP, Log, TEXT("tool_result id=%s action=%s %s"),
				*Ev.ToolCallId, *Ev.ToolAction, Ev.bIsError ? TEXT("ERROR") : TEXT("ok"));
			const TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetStringField(TEXT("type"),        TEXT("tool_result"));
			O->SetStringField(TEXT("content"),     Ev.ToolContentText);
			O->SetStringField(TEXT("tool_use_id"), Ev.ToolCallId);
			O->SetStringField(TEXT("action"),      Ev.ToolAction);
			O->SetStringField(TEXT("raw_input"),   Ev.ToolRawInputJson);
			O->SetBoolField  (TEXT("is_error"),    Ev.bIsError);
			return Write(O);
		}
		case EUECPACPStreamEventKind::ToolUpdate:
		case EUECPACPStreamEventKind::UsageUpdate:
		case EUECPACPStreamEventKind::SlashCommands:
		case EUECPACPStreamEventKind::Unknown:
		default:
			return FString();
		}
	}
}

static void ForwardACPSlashCommands(UUECPAppBridge* Bridge, const FString& AgentId, const FUECPACPStreamEvent& Ev)
{
	if (!Bridge || !Ev.RawUpdate.IsValid()) return;

	const TArray<TSharedPtr<FJsonValue>>* Commands = nullptr;
	if (!Ev.RawUpdate->TryGetArrayField(TEXT("availableCommands"), Commands) || !Commands) return;

	TArray<TSharedPtr<FJsonValue>> Out;
	for (const TSharedPtr<FJsonValue>& V : *Commands)
	{
		const TSharedPtr<FJsonObject> Obj = V.IsValid() ? V->AsObject() : nullptr;
		if (!Obj.IsValid()) continue;
		const TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
		FString Name, Description;
		Obj->TryGetStringField(TEXT("name"),        Name);
		Obj->TryGetStringField(TEXT("description"), Description);
		Entry->SetStringField(TEXT("name"),        Name);
		Entry->SetStringField(TEXT("description"), Description);
		if (!Name.IsEmpty()) Out.Add(MakeShared<FJsonValueObject>(Entry));
	}
	FString Body;
	const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Body);
	FJsonSerializer::Serialize(Out, Writer); Writer->Close();
	Bridge->PushACPSlashCommands(AgentId, Body);
}

void FUECPAgentRunnerCoordinator::SpawnACPInstance(const FString& ChatID, FAgentProviderConfig& Config,
	const FString& Prompt, EAgentSourceView SourceView)
{
	TSharedPtr<SUECPMainWidget> W = Shell.Pin();
	if (!W.IsValid()) return;

	auto& TargetHistory = (SourceView == EAgentSourceView::ProjectScanner)
		? W->ProjectConversationHistory : W->ArchitectConversationHistory;
	auto AbortWithError = [W, SourceView, &TargetHistory](const FString& Msg)
	{
		AppendAgentModelMessage(TargetHistory, Msg);
		if (SourceView == EAgentSourceView::ProjectScanner) { W->bIsProjectThinking = false; W->RefreshProjectChatView(); }
		else { W->bIsArchitectThinking = false; W->RefreshArchitectChatView(); }
	};

	if (!EditorReadiness::IsSessionActive())
	{
		AbortWithError(TEXT("**Error:** Session inactive. Verify your plugin licence in Settings."));
		return;
	}

	if (Config.ACPCommand.IsEmpty() || !FPaths::FileExists(Config.ACPCommand))
	{
		AbortWithError(FString::Printf(
			TEXT("**Error:** ACP agent binary not found: `%s`. Install the agent or update the provider config."),
			*Config.ACPCommand));
		return;
	}

	TSharedPtr<FAgentRunnerInstance> Inst = MakeShared<FAgentRunnerInstance>();
	Inst->ChatID       = ChatID;
	Inst->ProviderName = Config.ProviderName;
	Inst->SourceView   = SourceView;
	Inst->bStarting    = true;

	if (FPlanManager::Get().HasActivePlan(ChatID))
		FPlanManager::Get().ImportPlan(ChatID, TEXT("mcp"));
	else
		FPlanManager::Get().ClearPlan(TEXT("mcp"));

	EAIInteractionMode Mode = W->GetActiveArchitectInteractionMode();
	if (const EAIInteractionMode* Found = W->ArchitectInteractionModeByChat.Find(ChatID))
		Mode = *Found;
	else
		W->ArchitectInteractionModeByChat.Add(ChatID, Mode);

	FString SystemPrompt = (SourceView == EAgentSourceView::ProjectScanner)
		? FString(UECPScannerPrompt::GetSystemPrompt())
		: IUECPCoreModule::Get().GetArchitectService().BuildFullSystemPrompt(Mode, ChatID,  true);

	FString HistoryPrefix;
	{
		const TArray<TSharedPtr<FJsonValue>>& H =
			(SourceView == EAgentSourceView::ProjectScanner)
			? W->ProjectConversationHistory : W->ArchitectConversationHistory;
		const int32 PriorCount = H.Num() - 1;
		if (PriorCount > 0)
		{
			FString Turns;
			constexpr int32 MaxHistoryChars = 8000;
			for (int32 i = 0; i < PriorCount && Turns.Len() < MaxHistoryChars; ++i)
			{
				const TSharedPtr<FJsonObject> Msg = H[i]->AsObject();
				if (!Msg.IsValid()) continue;
				FString Role;
				Msg->TryGetStringField(TEXT("role"), Role);
				if (Role != TEXT("user") && Role != TEXT("model")) continue;
				const TArray<TSharedPtr<FJsonValue>>* Parts = nullptr;
				if (!Msg->TryGetArrayField(TEXT("parts"), Parts) || !Parts || Parts->IsEmpty()) continue;
				const TSharedPtr<FJsonObject> Part = (*Parts)[0]->AsObject();
				if (!Part.IsValid()) continue;
				FString Text;
				if (!Part->TryGetStringField(TEXT("text"), Text) || Text.IsEmpty()) continue;
				if (Text.StartsWith(TEXT("[TOOL_")) || Text == TEXT("*\u2026*") || Text == TEXT("Done.")) continue;
				const FString Label = (Role == TEXT("user")) ? TEXT("User") : TEXT("Assistant");
				Turns += FString::Printf(TEXT("%s: %s\n\n"), *Label, *Text.Left(1000));
			}
			if (!Turns.IsEmpty())
				HistoryPrefix = FString::Printf(TEXT("<prior_conversation>\n%s</prior_conversation>\n\n"), *Turns);
		}
	}

	const FString FullPrompt = SystemPrompt.IsEmpty()
		? (HistoryPrefix + Prompt)
		: FString::Printf(TEXT("<system>\n%s\n</system>\n\n%s%s"), *SystemPrompt, *HistoryPrefix, *Prompt);
	Inst->PendingACPPrompt = FullPrompt;

	if (SourceView != EAgentSourceView::ProjectScanner)
	{
		const TArray<TSharedPtr<FJsonValue>>& History = W->ArchitectConversationHistory;
		for (int32 i = History.Num() - 1; i >= 0; --i)
		{
			TSharedPtr<FJsonObject> Msg = History[i].IsValid() ? History[i]->AsObject() : nullptr;
			if (!Msg.IsValid()) continue;
			FString Role;
			Msg->TryGetStringField(TEXT("role"), Role);
			if (Role != TEXT("user")) continue;
			const TArray<TSharedPtr<FJsonValue>>* Parts = nullptr;
			if (!Msg->TryGetArrayField(TEXT("parts"), Parts) || !Parts) break;
			for (const TSharedPtr<FJsonValue>& PV : *Parts)
			{
				TSharedPtr<FJsonObject> P = PV.IsValid() ? PV->AsObject() : nullptr;
				if (!P.IsValid()) continue;
				const TSharedPtr<FJsonObject>* Inline = nullptr;
				if (!P->TryGetObjectField(TEXT("inline_data"), Inline) || !Inline) continue;
				FString Mime, Data;
				(*Inline)->TryGetStringField(TEXT("mime_type"), Mime);
				(*Inline)->TryGetStringField(TEXT("data"), Data);
				if (!Mime.IsEmpty() && !Data.IsEmpty()) Inst->PendingACPImages.Emplace(Mime, Data);
			}
			break;
		}
	}

	if (SourceView != EAgentSourceView::ProjectScanner)
		Inst->LocalHistory = W->ArchitectConversationHistory;
	W->AgentInstances.Add(ChatID, Inst);

	TSharedPtr<FUECPACPSession> Session = MakeShared<FUECPACPSession>();
	Inst->ACPSession = Session;

	TWeakPtr<SUECPMainWidget> WeakW = Shell;
	TWeakPtr<FAgentRunnerInstance> WeakInst = Inst;

	const FString AgentIdForStream = Config.AgentId.IsEmpty() ? Config.ProviderName : Config.AgentId;
	Session->OnStreamEvent.BindLambda([this, WeakW, WeakInst, AgentIdForStream](const FUECPACPStreamEvent& Ev)
	{
		auto W2 = WeakW.Pin(); auto I = WeakInst.Pin();
		if (!W2.IsValid() || !I.IsValid()) return;

		if (Ev.Kind == EUECPACPStreamEventKind::SlashCommands)
		{
			ForwardACPSlashCommands(W2->AppBridgeObject, AgentIdForStream, Ev);
			return;
		}

		const FString Json = SynthesizeAgentJsonFromACPEvent(Ev);
		if (!Json.IsEmpty()) ProcessAgentMessage(*I, Json);
	});

	FString AgentIdForCfg = Config.AgentId.IsEmpty() ? Config.ProviderName : Config.AgentId;
	Session->OnAgentConfig.BindLambda([WeakW, AgentIdForCfg](const UECPACPSchema::FConfigSnapshot& Snap)
	{
		auto W2 = WeakW.Pin();
		if (!W2.IsValid()) return;

		FUECPACPAgentConfigSnapshot Cached;
		Cached.AgentId      = AgentIdForCfg;
		Cached.CurrentModel = Snap.CurrentModel;
		Cached.CurrentMode  = Snap.CurrentMode;
		for (const auto& M : Snap.Models) Cached.Models.Add({ M.Id, M.Name, M.Description });
		for (const auto& M : Snap.Modes)  Cached.Modes.Add({ M.Id, M.Name, M.Description });
		for (const auto& S : Snap.OtherSections)
		{
			FUECPACPAgentConfigSection Out;
			Out.Id           = S.Id;
			Out.Name         = S.Name;
			Out.CurrentValue = S.CurrentValue;
			for (const auto& O : S.Options) Out.Options.Add({ O.Id, O.Name, O.Description });
			Cached.OtherSections.Add(MoveTemp(Out));
		}
		W2->CachedAgentConfigs.Add(AgentIdForCfg, Cached);

		if (W2->SettingsBridgeObject) W2->SettingsBridgeObject->PushACPAgentConfig(AgentIdForCfg);
		if (W2->AppBridgeObject) W2->AppBridgeObject->PushModelPicker();
	});

	Session->OnPermissionRequest.BindLambda([this, WeakW, WeakInst](
		const FString& ToolCallId, const FString& ToolTitle, const FString& ToolKind,
		const FUECPACPSession::FPermissionOptionList& Options)
	{
		auto W2 = WeakW.Pin(); auto I = WeakInst.Pin();
		if (!W2.IsValid() || !I.IsValid()) return;

		if (const int32* BubbleIdx = I->ToolBubbleMap.Find(ToolCallId))
		{
			auto& History = W2->GetAgentHistory(*I);
			if (History.IsValidIndex(*BubbleIdx))
			{
				if (TSharedPtr<FJsonObject> Obj = History[*BubbleIdx]->AsObject())
				{
					const TArray<TSharedPtr<FJsonValue>>* Parts;
					if (Obj->TryGetArrayField(TEXT("parts"), Parts) && Parts->Num() > 0)
					{
						if (TSharedPtr<FJsonObject> Part = (*Parts)[0]->AsObject())
						{
							FString CurrentText;
							Part->TryGetStringField(TEXT("text"), CurrentText);
							if (CurrentText.StartsWith(TEXT("[TOOL_RESULT:MCP: tool")))
							{
								FString Better = ToolTitle;
								StripMcpToolPrefixes(Better);
								int32 ColonIdx = INDEX_NONE;
								if (Better.FindChar(TEXT(':'), ColonIdx))
									Better = Better.Left(ColonIdx).TrimEnd();
								if (!Better.IsEmpty())
								{
									CurrentText.RemoveFromStart(TEXT("[TOOL_RESULT:MCP: tool"));
									CurrentText = FString::Printf(TEXT("[TOOL_RESULT:%s%s"), *Better, *CurrentText);
									Part->SetStringField(TEXT("text"), CurrentText);
									I->bHasImportantUpdate = true;
									if (!W2->bSuppressChatViewRefresh)
										RefreshAgentView(*I);
								}
							}
						}
					}
				}
			}
		}

		FString ProceedOptionId;
		for (const TPair<FString, FString>& O : Options)
		{
			if (O.Value == TEXT("allow_once") || O.Value == TEXT("allow_always"))
			{
				ProceedOptionId = O.Key; break;
			}
		}

		EAIInteractionMode ChatMode = EAIInteractionMode::AutoEdit;
		if (const EAIInteractionMode* Found = W2->ArchitectInteractionModeByChat.Find(I->ChatID))
			ChatMode = *Found;
		else
			ChatMode = W2->GetActiveArchitectInteractionMode();

		const FString KindLower = ToolKind.ToLower();
		const FString TitleLower = ToolTitle.ToLower();

		auto HasReadPrefix = [](const FString& Name) -> bool
		{
			FString N = Name;
			const int32 LastSep = N.Find(TEXT("__"), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
			if (LastSep != INDEX_NONE) N = N.RightChop(LastSep + 2);
			return N.StartsWith(TEXT("get_"))    || N.StartsWith(TEXT("list_")) ||
			       N.StartsWith(TEXT("find_"))   || N.StartsWith(TEXT("search_"));
		};
		const FString* StoredActionPtr = I->PendingToolActions.Find(ToolCallId);
		const FString  StoredActionLower = StoredActionPtr ? StoredActionPtr->ToLower() : FString();
		const bool bBenign =
			KindLower == TEXT("read")   || KindLower == TEXT("search") ||
			KindLower == TEXT("fetch")  || KindLower == TEXT("think")  ||
			HasReadPrefix(TitleLower) ||
			(!StoredActionLower.IsEmpty() && HasReadPrefix(StoredActionLower));

		bool bDestructive = KindLower == TEXT("edit") || KindLower == TEXT("delete") || KindLower == TEXT("move");
		if (!bDestructive && !bBenign && !StoredActionLower.IsEmpty())
		{
			bDestructive = StoredActionLower.StartsWith(TEXT("delete_")) || StoredActionLower.StartsWith(TEXT("clear_")) ||
			               StoredActionLower.StartsWith(TEXT("remove_")) || StoredActionLower.StartsWith(TEXT("destroy_")) ||
			               StoredActionLower.StartsWith(TEXT("purge_"));
		}

		const bool bPlanMeta =
			TitleLower.Contains(TEXT("project_plan")) ||
			TitleLower.Contains(TEXT("working_notes")) ||
			TitleLower.Contains(TEXT("memory"));

		bool bDestructiveConfirm = true;
		GConfig->GetBool(TEXT("BpGeneratorUltimate"), TEXT("DestructiveOpsConfirm"),
			bDestructiveConfirm, FSettingsManager::GetGlobalConfigPath());
		{ bool bAxTurbo = false; GConfig->GetBool(TEXT("BpGeneratorUltimate"), TEXT("TurboMode"), bAxTurbo, FSettingsManager::GetGlobalConfigPath()); if (bAxTurbo) bDestructiveConfirm = false; } // Axivor Turbo: no destructive confirmations
		if (IUECPCoreModule::IsAvailable()
			&& IUECPCoreModule::Get().GetCrewService().ShouldAutoApproveDestructiveForChat(I->ChatID))
		{
			bDestructiveConfirm = false;
		}

		bool bShouldPrompt = true;
		if (ChatMode == EAIInteractionMode::AutoEdit)
		{
			if (bDestructive) bShouldPrompt = bDestructiveConfirm;
			else              bShouldPrompt = false;
		}
		else if (ChatMode == EAIInteractionMode::AskBeforeEdit)
		{
			bShouldPrompt = !bBenign;
		}
		else if (ChatMode == EAIInteractionMode::PlanMode)
		{
			bShouldPrompt = !bBenign && !bPlanMeta;
		}
		else
		{
			bShouldPrompt = !bBenign;
		}

		{ // Axivor Turbo: auto-approve every agent permission request (except in Just Chat)
			bool bAxTurbo = false;
			GConfig->GetBool(TEXT("BpGeneratorUltimate"), TEXT("TurboMode"), bAxTurbo, FSettingsManager::GetGlobalConfigPath());
			if (bAxTurbo && ChatMode != EAIInteractionMode::JustChat) bShouldPrompt = false;
		}
		if (!bShouldPrompt && !ProceedOptionId.IsEmpty() && I->ACPSession.IsValid())
		{
			UE_LOG(LogUECPACP, Log, TEXT("permission auto-approve id=%s kind=%s title='%s' (mode=%d)"),
				*ToolCallId, *ToolKind, *ToolTitle, (int32)ChatMode);
			I->ACPSession->RespondToPermission(ToolCallId, ProceedOptionId);
			return;
		}

		I->bConfirmPending        = true;
		I->ConfirmToolName        = ToolTitle;
		I->ConfirmPreview         = ToolTitle;
		I->ConfirmToolCallId      = ToolCallId;
		I->ConfirmProceedOptionId = ProceedOptionId;

		W2->bAgentConfirmPending = true;

		UE_LOG(LogUECPACP, Log, TEXT("permission request id=%s kind=%s title='%s' (mode=%d destructive-confirm=%s) — awaiting user click"),
			*ToolCallId, *ToolKind, *ToolTitle, (int32)ChatMode,
			bDestructiveConfirm ? TEXT("on") : TEXT("off"));

		if (UUECPAppBridge* B = Bridge.Get())
		{
			B->PushToolConfirmation(ToolTitle, ToolTitle);
		}
	});

	Session->OnTurnComplete.BindLambda([this, WeakW, WeakInst](const FString& StopReason)
	{
		auto W2 = WeakW.Pin(); auto I = WeakInst.Pin();
		if (!W2.IsValid() || !I.IsValid()) return;
		const TSharedRef<FJsonObject> Done = MakeShared<FJsonObject>();
		Done->SetStringField(TEXT("type"), TEXT("done"));
		Done->SetStringField(TEXT("stop_reason"), StopReason);
		FString Out;
		const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Wr =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
		FJsonSerializer::Serialize(Done, Wr); Wr->Close();
		ProcessAgentMessage(*I, Out);
	});

	Session->OnError.BindLambda([this, WeakW, WeakInst](const FString& Message)
	{
		auto W2 = WeakW.Pin(); auto I = WeakInst.Pin();
		if (!W2.IsValid() || !I.IsValid()) return;
		const TSharedRef<FJsonObject> Err = MakeShared<FJsonObject>();
		Err->SetStringField(TEXT("type"), TEXT("error"));
		Err->SetStringField(TEXT("message"), Message);
		FString Out;
		const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Wr =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
		FJsonSerializer::Serialize(Err, Wr); Wr->Close();
		ProcessAgentMessage(*I, Out);
	});

	FUECPACPSession::FSpawnArgs Args;
	Args.Command    = Config.ACPCommand;
	Args.Args       = Config.ACPArgs;
	Args.Cwd        = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
	Args.Cwd.ReplaceInline(TEXT("\\"), TEXT("/"));
	Args.LogTag     = Config.AgentId.IsEmpty() ? Config.ProviderName : Config.AgentId;

	FString McpFailureReason;
	FString UecpHttpUrl;
	FString UecpSessionToken;
	const bool bSkipMcpInjection = (SourceView == EAgentSourceView::ProjectScanner);
	if (!bSkipMcpInjection && IUECPCoreModule::IsAvailable())
	{
		IUECPMcpInfoService& McpInfo = IUECPCoreModule::Get().GetMcpInfoService();
		const int32 Port  = McpInfo.GetHttpPort();
		UecpSessionToken  = McpInfo.GetSessionToken();
		if (Port > 0 && !UecpSessionToken.IsEmpty())
		{
			UecpHttpUrl = FString::Printf(TEXT("http://localhost:%d/mcp"), Port);

			UECPACPSchema::FMcpServerSpec Spec;
			Spec.Name        = TEXT("uecp");
			Spec.HttpUrl     = UecpHttpUrl;
			const FString CrewChatToken = FUECPCrewChatTokenStore::Get().IssueToken(ChatID);
			Spec.HttpHeaders = {
				{ TEXT("Authorization"),    FString::Printf(TEXT("Bearer %s"), *UecpSessionToken) },
				{ TEXT("X-Crew-Chat-Id"),   ChatID },
				{ TEXT("X-Crew-Chat-Token"), CrewChatToken },
			};
			Args.McpSpecs.Add(MoveTemp(Spec));
			UE_LOG(LogUECPACP, Log, TEXT("MCP injection: HTTP %s (bearer auth, chat=%s, crew token=%d chars)"),
				*UecpHttpUrl, *ChatID, CrewChatToken.Len());
		}
		else
		{
			McpFailureReason = TEXT("UECP MCP HTTP transport is not bound — restart the editor or check Settings → MCP Server.");
			UE_LOG(LogUECPACP, Warning, TEXT("MCP injection skipped — port=%d, token-present=%d"), Port, UecpSessionToken.IsEmpty() ? 0 : 1);
		}
	}
	else
	{
		McpFailureReason = TEXT("UECP Core module not available — cannot resolve MCP transport.");
		UE_LOG(LogUECPACP, Warning, TEXT("MCP injection skipped — Core unavailable"));
	}

	if (Config.AgentId == TEXT("github-copilot-cli") && !UecpHttpUrl.IsEmpty())
	{
		FString HomeDir = FPlatformMisc::GetEnvironmentVariable(TEXT("USERPROFILE"));
		if (HomeDir.IsEmpty())
			HomeDir = FPlatformProcess::UserHomeDir();
		if (!HomeDir.IsEmpty())
		{
			const FString CfgPath = FPaths::Combine(HomeDir, TEXT(".copilot"), TEXT("mcp-config.json"));
			IFileManager::Get().MakeDirectory(*FPaths::GetPath(CfgPath),  true);

			TSharedPtr<FJsonObject> Root;
			{
				FString Existing;
				if (FFileHelper::LoadFileToString(Existing, *CfgPath))
				{
					const TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(Existing);
					FJsonSerializer::Deserialize(R, Root);
				}
			}
			if (!Root.IsValid()) Root = MakeShared<FJsonObject>();

			TSharedPtr<FJsonObject> Servers;
			{
				const TSharedPtr<FJsonObject>* Ptr = nullptr;
				if (Root->TryGetObjectField(TEXT("mcpServers"), Ptr) && Ptr && Ptr->IsValid())
					Servers = *Ptr;
			}
			if (!Servers.IsValid())
			{
				Servers = MakeShared<FJsonObject>();
				Root->SetObjectField(TEXT("mcpServers"), Servers);
			}

			const TSharedRef<FJsonObject> Uecp = MakeShared<FJsonObject>();
			Uecp->SetStringField(TEXT("type"), TEXT("http"));
			Uecp->SetStringField(TEXT("url"),  UecpHttpUrl);
			const TSharedRef<FJsonObject> Hdrs = MakeShared<FJsonObject>();
			Hdrs->SetStringField(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *UecpSessionToken));
			Uecp->SetObjectField(TEXT("headers"), Hdrs);
			Servers->SetObjectField(TEXT("uecp"), Uecp);

			FString Body;
			const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Body);
			FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);
			Writer->Close();
			FFileHelper::SaveStringToFile(Body, *CfgPath);
			UE_LOG(LogUECPACP, Log, TEXT("Copilot mcp-config.json updated at %s (HTTP transport)"), *CfgPath);
		}
	}

	if (!bSkipMcpInjection && IUECPCoreModule::IsAvailable())
	{
		IUECPExtensionService& Ext = IUECPCoreModule::Get().GetExtensionService();
		for (const FUECPExtensionDescriptor& D : Ext.GetExtensions())
		{
			if (!D.McpServer.IsSet()) continue;
			if (Ext.GetExtensionState(D.ExtensionId) != EUECPExtensionState::Loaded) continue;

			const FUECPMcpServerSpec& Src = D.McpServer.GetValue();
			UECPACPSchema::FMcpServerSpec Spec;
			Spec.Name = D.ExtensionId.ToString();

			if (Src.IsHttp())
			{
				Spec.HttpUrl     = Src.Url;
				Spec.HttpHeaders = Src.Headers;
				UE_LOG(LogUECPACP, Log,
					TEXT("MCP injection: extension '%s' http url=%s headers=%d"),
					*D.ExtensionId.ToString(), *Src.Url, Src.Headers.Num());
			}
			else
			{
				Spec.StdioCommand = Src.Command;
				Spec.StdioArgs    = Src.Args;
				Spec.StdioEnv     = Src.Env;
				UE_LOG(LogUECPACP, Log,
					TEXT("MCP injection: extension '%s' stdio command=%s args=%d env=%d"),
					*D.ExtensionId.ToString(), *Src.Command, Src.Args.Num(), Src.Env.Num());
			}
			Args.McpSpecs.Add(MoveTemp(Spec));
		}
	}

	if (!McpFailureReason.IsEmpty())
	{
		const FString Banner = FString::Printf(
			TEXT("> ⚠️ **UECP tools unavailable for this CLI agent session.** %s"),
			*McpFailureReason);

		if (SourceView != EAgentSourceView::ProjectScanner)
		{
			AppendAgentModelMessage(W->ArchitectConversationHistory, Banner);
			Inst->LocalHistory = W->ArchitectConversationHistory;
			if (!W->bSuppressChatViewRefresh)
				W->RefreshArchitectChatView();
		}
		else
		{
			AppendAgentModelMessage(W->ProjectConversationHistory, Banner);
			if (!W->bSuppressChatViewRefresh)
				W->RefreshProjectChatView();
		}

		IUECPCoreModule::Get().GetNotificationService().PushToast(
			TEXT("UECP MCP tools unavailable — see chat for details"),
			TEXT("warning"));
	}

	FPlatformMisc::SetEnvironmentVar(TEXT("CLAUDECODE"), nullptr);
	FPlatformMisc::SetEnvironmentVar(TEXT("CLAUDE_CODE_ENTRYPOINT"), nullptr);

	FPlatformMisc::SetEnvironmentVar(TEXT("CLAUDE_CODE_EFFORT_LEVEL"),              nullptr);
	FPlatformMisc::SetEnvironmentVar(TEXT("CLAUDE_CODE_DISABLE_ADAPTIVE_THINKING"), nullptr);

	FString ExistingRustLog = FPlatformMisc::GetEnvironmentVariable(TEXT("RUST_LOG"));
	if (ExistingRustLog.IsEmpty())
	{
		FPlatformMisc::SetEnvironmentVar(TEXT("RUST_LOG"), TEXT("error"));
	}
	FPlatformMisc::SetEnvironmentVar(TEXT("NO_COLOR"), TEXT("1"));

	EnsureAntigravityConfigured(Config.ProviderName);

	const FString ChatIDCopy  = ChatID;
	const FString CmdCopy     = Config.ACPCommand;
	const FString ProvCopy    = Config.ProviderName;
	const EAgentSourceView SrcView = SourceView;
	Async(EAsyncExecution::Thread, [Session, Args, Inst, WeakInst,
		ChatIDCopy, WeakW = Shell, SrcView, CmdCopy, ProvCopy]() mutable
	{
		if (!Session->Start(Args))
		{
			UE_LOG(LogUECPACP, Error, TEXT("ACP spawn failed: chat=%s cmd=%s"), *ChatIDCopy, *CmdCopy);
			AsyncTask(ENamedThreads::GameThread, [WeakW, ChatIDCopy, SrcView]()
			{
				auto W2 = WeakW.Pin();
				if (!W2.IsValid()) return;
				auto& TgtHist = (SrcView == EAgentSourceView::ProjectScanner)
					? W2->ProjectConversationHistory : W2->ArchitectConversationHistory;
				AppendAgentModelMessage(TgtHist,
					TEXT("**Error:** Could not spawn ACP agent process. See Output Log for details."));
				if (SrcView == EAgentSourceView::ProjectScanner)
					{ W2->bIsProjectThinking = false; W2->RefreshProjectChatView(); }
				else
					{ W2->bIsArchitectThinking = false; W2->RefreshArchitectChatView(); }
				W2->AgentInstances.Remove(ChatIDCopy);
			});
			return;
		}

		AsyncTask(ENamedThreads::GameThread, [Inst, WeakInst, ChatIDCopy, CmdCopy, ProvCopy]()
		{
			Inst->bStarted  = true;
			Inst->bStarting = false;

			Inst->TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
				FTickerDelegate::CreateLambda([WeakInst](float DT) -> bool
				{
					auto I = WeakInst.Pin();
					if (!I.IsValid()) return false;
					return IUECPCoreModule::Get().GetAgentRunnerService().OnAgentInstanceTick(I, DT);
				}),
				0.05f);

			UE_LOG(LogUECPACP, Log, TEXT("ACP instance spawned: chat=%s provider=%s cmd=%s"),
				*ChatIDCopy, *ProvCopy, *CmdCopy);
		});
	});
}

void FUECPAgentRunnerCoordinator::NotifyInteractionModeChanged(const FString& ChatID)
{
	TSharedPtr<SUECPMainWidget> W = Shell.Pin();
	if (!W.IsValid() || ChatID.IsEmpty()) return;

	const TSharedPtr<FAgentRunnerInstance>* InstPtr = W->AgentInstances.Find(ChatID);
	if (!InstPtr || !InstPtr->IsValid()) return;
	FAgentRunnerInstance& Inst = **InstPtr;
	if (!Inst.ACPSession.IsValid()) return;

	const EAIInteractionMode* FoundMode = W->ArchitectInteractionModeByChat.Find(ChatID);
	if (!FoundMode) return;
	const EAIInteractionMode NewMode = *FoundMode;

	bool bDestructiveConfirmCfg = true;
	GConfig->GetBool(TEXT("BpGeneratorUltimate"), TEXT("DestructiveOpsConfirm"),
		bDestructiveConfirmCfg, FSettingsManager::GetGlobalConfigPath());
		{ bool bAxTurbo = false; GConfig->GetBool(TEXT("BpGeneratorUltimate"), TEXT("TurboMode"), bAxTurbo, FSettingsManager::GetGlobalConfigPath()); if (bAxTurbo) bDestructiveConfirmCfg = false; } // Axivor Turbo: no destructive confirmations
	if (IUECPCoreModule::IsAvailable()
		&& IUECPCoreModule::Get().GetCrewService().ShouldAutoApproveDestructiveForChat(ChatID))
	{
		bDestructiveConfirmCfg = false;
	}

	TArray<FString> Candidates;
	switch (NewMode)
	{
	case EAIInteractionMode::JustChat:
		Candidates = { TEXT("read-only"), TEXT("readonly"), TEXT("dontask"), TEXT("dont_ask") };
		break;
	case EAIInteractionMode::AskBeforeEdit:
		Candidates = { TEXT("on-request"), TEXT("default"), TEXT("agent"), TEXT("ask") };
		break;
	case EAIInteractionMode::PlanMode:
	case EAIInteractionMode::AutoEdit:
	default:
		if (bDestructiveConfirmCfg)
			Candidates = { TEXT("on-request"), TEXT("default"), TEXT("agent"), TEXT("ask") };
		else
			Candidates = { TEXT("acceptedits"), TEXT("autoedit"), TEXT("autopilot"), TEXT("yolo"), TEXT("auto"), TEXT("bypass"), TEXT("code") };
		break;
	}

	FString SelectedMode;
	if (const FUECPACPAgentConfigSnapshot* Snap = W->CachedAgentConfigs.Find(Inst.ProviderName))
	{
		for (const FString& Candidate : Candidates)
		{
			if (!SelectedMode.IsEmpty()) break;
			for (const FUECPACPConfigOption& M : Snap->Modes)
			{
				const FString Haystack = (M.Id + TEXT(" ") + M.Name).ToLower();
				if (Haystack.Contains(Candidate)) { SelectedMode = M.Id; break; }
			}
		}
	}

	if (!SelectedMode.IsEmpty())
	{
		Inst.ACPSession->ApplyUserPrefs(SelectedMode, FString(), {});
		UE_LOG(LogUECPACP, Log, TEXT("Mid-session mode change chat=%s uiMode=%d → agentMode=%s"),
			*ChatID, (int32)NewMode, *SelectedMode);
	}

	const TCHAR* ModeName =
		(NewMode == EAIInteractionMode::JustChat)      ? TEXT("Just Chat (read-only)") :
		(NewMode == EAIInteractionMode::AskBeforeEdit) ? TEXT("Ask Before Edit") :
		(NewMode == EAIInteractionMode::PlanMode)      ? TEXT("Plan") :
		                                                 TEXT("Auto Edit");
	Inst.PendingModeChangeNotice = FString::Printf(
		TEXT("<system>The user has switched the UECP interaction mode to **%s**. ")
		TEXT("The dispatcher gate has been updated — disregard any earlier mode directive ")
		TEXT("from the original system prompt and continue under the new mode's rules.</system>\n\n"),
		ModeName);
}

void FUECPAgentRunnerCoordinator::DiscoverAgentConfig(const FString& AgentId)
{
	auto W = Shell.Pin();
	if (!W.IsValid()) return;

	// Pre-seed cached config from hardcoded models if missing so UI can show models immediately
	if (!W->CachedAgentConfigs.Contains(AgentId))
	{
		TArray<FString> HardcodedModels = FProviderConfigManager::Get().GetAgentModels(AgentId);
		if (HardcodedModels.Num() == 0 && AgentId.EndsWith(TEXT("-acp")))
		{
			HardcodedModels = FProviderConfigManager::Get().GetAgentModels(AgentId.LeftChop(4));
		}
		if (HardcodedModels.Num() > 0)
		{
			FUECPACPAgentConfigSnapshot Preseed;
			Preseed.AgentId = AgentId;
			Preseed.CurrentModel = HardcodedModels[0];
			for (const FString& M : HardcodedModels)
			{
				Preseed.Models.Add({ M, M, TEXT("") });
			}
			W->CachedAgentConfigs.Add(AgentId, Preseed);
			if (W->SettingsBridgeObject) W->SettingsBridgeObject->PushACPAgentConfig(AgentId);
			if (W->AppBridgeObject) W->AppBridgeObject->PushModelPicker();
		}
	}

	IUECPACPRegistryService& Reg = IUECPCoreModule::Get().GetACPRegistryService();
	const FUECPACPInstallMarker Marker = Reg.GetInstallMarker(AgentId);
	if (Marker.AgentId.IsEmpty() || Marker.EntrypointCommand.IsEmpty())
	{
		UE_LOG(LogUECPACP, Warning, TEXT("DiscoverAgentConfig: no install marker for '%s'"), *AgentId);
		return;
	}

	TSharedPtr<FUECPACPSession> Session = MakeShared<FUECPACPSession>();

	FUECPACPSession::FSpawnArgs Args;
	Args.Command = Marker.EntrypointCommand;
	Args.Args    = Marker.EntrypointArgs;
	Args.Cwd     = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
	Args.Cwd.ReplaceInline(TEXT("\\"), TEXT("/"));
	Args.LogTag  = FString::Printf(TEXT("%s-discovery"), *AgentId);

	TWeakPtr<SUECPMainWidget> WeakW = Shell;
	const FString AgentIdCopy = AgentId;

	TSharedPtr<bool>   bGotSlashCommands = MakeShared<bool>(false);
	TSharedPtr<double> ReadyAt           = MakeShared<double>(0.0);
	Session->OnStreamEvent.BindLambda([WeakW, AgentIdCopy, bGotSlashCommands](const FUECPACPStreamEvent& Ev)
	{
		if (Ev.Kind != EUECPACPStreamEventKind::SlashCommands) return;
		auto W2 = WeakW.Pin();
		if (!W2.IsValid()) return;
		ForwardACPSlashCommands(W2->AppBridgeObject, AgentIdCopy, Ev);
		*bGotSlashCommands = true;
	});

	Session->OnAgentConfig.BindLambda([WeakW, AgentIdCopy](const UECPACPSchema::FConfigSnapshot& Snap)
	{
		auto W2 = WeakW.Pin();
		if (!W2.IsValid()) return;

		FUECPACPAgentConfigSnapshot Cached;
		Cached.AgentId      = AgentIdCopy;
		Cached.CurrentModel = Snap.CurrentModel;
		Cached.CurrentMode  = Snap.CurrentMode;
		for (const auto& M : Snap.Models) Cached.Models.Add({ M.Id, M.Name, M.Description });
		for (const auto& M : Snap.Modes)  Cached.Modes.Add({ M.Id, M.Name, M.Description });
		for (const auto& S : Snap.OtherSections)
		{
			FUECPACPAgentConfigSection Out;
			Out.Id           = S.Id;
			Out.Name         = S.Name;
			Out.CurrentValue = S.CurrentValue;
			for (const auto& O : S.Options) Out.Options.Add({ O.Id, O.Name, O.Description });
			Cached.OtherSections.Add(MoveTemp(Out));
		}
		W2->CachedAgentConfigs.Add(AgentIdCopy, Cached);

		const bool bWasAuthFlagged = W2->AgentAuthStates.Remove(AgentIdCopy) > 0;

		if (W2->SettingsBridgeObject) W2->SettingsBridgeObject->PushACPAgentConfig(AgentIdCopy);
		if (W2->AppBridgeObject) W2->AppBridgeObject->PushModelPicker();
		if (bWasAuthFlagged && W2->SettingsBridgeObject) W2->SettingsBridgeObject->PushACPCatalog();
		UE_LOG(LogUECPACP, Log, TEXT("Discovery captured config for '%s': models=%d modes=%d other=%d"),
			*AgentIdCopy, Cached.Models.Num(), Cached.Modes.Num(), Cached.OtherSections.Num());
	});

	TSharedPtr<FUECPACPSession> SessionWeak = Session;
	Session->OnAuthRequired.BindLambda([WeakW, AgentIdCopy, SessionWeak](const FString& MethodId)
	{
		AsyncTask(ENamedThreads::GameThread, [WeakW, AgentIdCopy, MethodId, SessionWeak]()
		{
			auto W2 = WeakW.Pin();
			if (!W2.IsValid()) return;
			SUECPMainWidget::FACPAgentAuthState State;
			State.bRequired = true;
			State.MethodId  = MethodId;
			W2->AgentAuthStates.Add(AgentIdCopy, State);

			if (!W2->CachedAgentConfigs.Contains(AgentIdCopy))
			{
				TArray<FString> DefModels = FProviderConfigManager::Get().GetAgentModels(AgentIdCopy);
				if (DefModels.Num() == 0 && AgentIdCopy.EndsWith(TEXT("-acp")))
				{
					DefModels = FProviderConfigManager::Get().GetAgentModels(AgentIdCopy.LeftChop(4));
				}
				if (DefModels.Num() > 0)
				{
					FUECPACPAgentConfigSnapshot Preseed;
					Preseed.AgentId = AgentIdCopy;
					Preseed.CurrentModel = DefModels[0];
					for (const FString& MId : DefModels)
					{
						Preseed.Models.Add({ MId, MId, TEXT("") });
					}
					W2->CachedAgentConfigs.Add(AgentIdCopy, Preseed);
					if (W2->SettingsBridgeObject) W2->SettingsBridgeObject->PushACPAgentConfig(AgentIdCopy);
					if (W2->AppBridgeObject) W2->AppBridgeObject->PushModelPicker();
				}
			}

			if (W2->SettingsBridgeObject) W2->SettingsBridgeObject->PushACPCatalog();
			UE_LOG(LogUECPACP, Log,
				TEXT("Discovery: '%s' requires sign-in (methodId='%s') — flagged in catalog"),
				*AgentIdCopy, *MethodId);
			if (SessionWeak.IsValid()) SessionWeak->Stop();
		});
	});

	FPlatformMisc::SetEnvironmentVar(TEXT("CLAUDECODE"), nullptr);
	FPlatformMisc::SetEnvironmentVar(TEXT("CLAUDE_CODE_ENTRYPOINT"), nullptr);
	FString ExistingRustLog = FPlatformMisc::GetEnvironmentVariable(TEXT("RUST_LOG"));
	if (ExistingRustLog.IsEmpty()) FPlatformMisc::SetEnvironmentVar(TEXT("RUST_LOG"), TEXT("error"));
	FPlatformMisc::SetEnvironmentVar(TEXT("NO_COLOR"), TEXT("1"));

	EnsureAntigravityConfigured(AgentId);

	const double SpawnRequestTime = FPlatformTime::Seconds();
	Async(EAsyncExecution::Thread, [Session, Args, AgentIdCopy, SpawnRequestTime, bGotSlashCommands, ReadyAt]() mutable
	{
		if (!Session->Start(Args))
		{
			UE_LOG(LogUECPACP, Warning, TEXT("DiscoverAgentConfig: failed to spawn '%s'"), *AgentIdCopy);
			return;
		}

		AsyncTask(ENamedThreads::GameThread, [Session, AgentIdCopy, SpawnRequestTime, bGotSlashCommands, ReadyAt]()
		{
			FTSTicker::GetCoreTicker().AddTicker(
				FTickerDelegate::CreateLambda([Session, AgentIdCopy, SpawnRequestTime, bGotSlashCommands, ReadyAt](float ) -> bool
				{
					const bool bAlive = Session->Tick();
					if (!bAlive) return false;

					const double Elapsed = FPlatformTime::Seconds() - SpawnRequestTime;
					if (Session->GetState() == FUECPACPSession::EState::Ready)
					{
						if (*ReadyAt == 0.0) *ReadyAt = FPlatformTime::Seconds();
						if (*bGotSlashCommands || (FPlatformTime::Seconds() - *ReadyAt) >= 1.5)
						{
							Session->Stop();
							return false;
						}
						return true;
					}
					if (Elapsed > 60.0)
					{
						UE_LOG(LogUECPACP, Warning, TEXT("DiscoverAgentConfig: '%s' timed out after 60s"), *AgentIdCopy);
						Session->Stop();
						return false;
					}
					return true;
				}),
				0.1f);
		});
	});
}

struct FUECPAgentRunnerCoordinator::FAgentAuthOp
{
	FString AgentId;
	FString Kind;
	FString MethodId;
	FString AfterOp;
	FString CarriedError;

	TSharedPtr<FUECPACPSession> Session;
	FProcHandle                 TerminalProc;
	FTSTicker::FDelegateHandle  Ticker;

	double Deadline = 0.0;
	double ReadyAt  = 0.0;
	bool   bStarted  = false;
	bool   bFinished = false;

	bool bGotCaps      = false;
	bool bLogout       = false;
	bool bPushesStatus = false;
	TArray<FUECPACPAuthMethod> Methods;

	bool    bGotStatus = false;
	FString StatusKind;
	FString Email;
	FString Label;
	FString TierName;

	FString SessionError;
	int32   SessionErrorCode = 0;
	FString Error;
};

namespace
{
	const TCHAR* const AuthOpProbe  = TEXT("probe");
	const TCHAR* const AuthOpLogin  = TEXT("login");
	const TCHAR* const AuthOpLogout = TEXT("logout");

	bool IsAuthFailureMessage(const FString& Message)
	{
		for (const TCHAR* Needle : { TEXT("auth"), TEXT("api key"), TEXT("credential"), TEXT("log in"), TEXT("login"), TEXT("sign in") })
		{
			if (Message.Contains(Needle, ESearchCase::IgnoreCase)) return true;
		}
		return false;
	}

	FString ExtractLoginUrl(const FString& Line)
	{
		const bool bLooksLikeLogin = Line.Contains(TEXT("authenticate"), ESearchCase::IgnoreCase)
			|| Line.Contains(TEXT("log in"), ESearchCase::IgnoreCase)
			|| Line.Contains(TEXT("login"), ESearchCase::IgnoreCase)
			|| Line.Contains(TEXT("sign in"), ESearchCase::IgnoreCase);
		const int32 Start = Line.Find(TEXT("https://"));
		if (!bLooksLikeLogin || Start == INDEX_NONE) return FString();

		FString Url = Line.Mid(Start);
		for (int32 i = 0; i < Url.Len(); ++i)
		{
			if (FChar::IsWhitespace(Url[i])) { Url.LeftInline(i); break; }
		}
		return Url;
	}
}

FUECPAgentRunnerCoordinator::~FUECPAgentRunnerCoordinator()
{
	TArray<FString> AgentIds;
	AuthOps.GetKeys(AgentIds);
	for (const FString& AgentId : AgentIds) AbortAuthOp(AgentId);
}

const FUECPACPAuthStatus* FUECPAgentRunnerCoordinator::GetAgentAuthStatus(const FString& AgentId) const
{
	return AuthStatuses.Find(AgentId);
}

void FUECPAgentRunnerCoordinator::PushAgentAuthStatus(const FString& AgentId)
{
	if (auto W = Shell.Pin())
	{
		if (W->SettingsBridgeObject) W->SettingsBridgeObject->PushACPAuthStatus(AgentId);
	}
}

void FUECPAgentRunnerCoordinator::FailAuthRequest(const FString& AgentId, const FString& Message)
{
	FUECPACPAuthStatus& Status = AuthStatuses.FindOrAdd(AgentId);
	Status.BusyOp.Empty();
	Status.Error = Message;
	PushAgentAuthStatus(AgentId);
}

void FUECPAgentRunnerCoordinator::ProbeAgentAuth(const FString& AgentId)
{
	if (AuthOps.Contains(AgentId))
	{
		PushAgentAuthStatus(AgentId);
		return;
	}
	StartAuthOp(AgentId, AuthOpProbe, FString());
}

bool FUECPAgentRunnerCoordinator::StopIdleChatsForAgent(const FString& AgentId)
{
	TSharedPtr<SUECPMainWidget> W = Shell.Pin();
	if (!W.IsValid()) return true;

	TArray<FString> ChatIDs;
	for (const auto& Pair : W->AgentInstances)
	{
		if (Pair.Value->ProviderName != AgentId || !Pair.Value->ACPSession.IsValid()) continue;
		if (Pair.Value->ACPSession->GetState() == FUECPACPSession::EState::Prompting) return false;
		ChatIDs.Add(Pair.Key);
	}
	// Live sessions keep the old credentials in memory; the next prompt spawns under the new login.
	for (const FString& ChatID : ChatIDs) StopAgentForChat(ChatID);
	return true;
}

void FUECPAgentRunnerCoordinator::LoginAgent(const FString& AgentId, const FString& MethodId)
{
	const FUECPACPAuthStatus* Status = AuthStatuses.Find(AgentId);
	const FUECPACPAuthMethod* Method = nullptr;
	if (Status)
	{
		Method = MethodId.IsEmpty() ? (Status->Methods.Num() > 0 ? &Status->Methods[0] : nullptr)
			: Status->Methods.FindByPredicate([&](const FUECPACPAuthMethod& M) { return M.Id == MethodId; });
	}
	if (!Method)
	{
		FailAuthRequest(AgentId, TEXT("Este agente não informou um método de login. Clique em Atualizar e tente de novo."));
		return;
	}
	if (!StopIdleChatsForAgent(AgentId))
	{
		FailAuthRequest(AgentId, TEXT("Há um turno em andamento neste agente. Aguarde terminar ou pare o chat antes de logar."));
		return;
	}

	if (Method->bTerminal)
	{
		const FUECPACPAuthMethod MethodCopy = *Method;
		StartTerminalLogin(AgentId, MethodCopy);
	}
	else
	{
		const FString Id = Method->Id;
		StartAuthOp(AgentId, AuthOpLogin, Id);
	}
}

void FUECPAgentRunnerCoordinator::LogoutAgent(const FString& AgentId)
{
	if (!StopIdleChatsForAgent(AgentId))
	{
		FailAuthRequest(AgentId, TEXT("Há um turno em andamento neste agente. Aguarde terminar ou pare o chat antes de deslogar."));
		return;
	}
	StartAuthOp(AgentId, AuthOpLogout, FString());
}

void FUECPAgentRunnerCoordinator::CancelAgentAuth(const FString& AgentId)
{
	const TSharedPtr<FAgentAuthOp>* Found = AuthOps.Find(AgentId);
	if (!Found) return;
	const bool bWasLogin = (*Found)->Kind == AuthOpLogin;
	AbortAuthOp(AgentId);

	if (bWasLogin)
	{
		StartAuthOp(AgentId, AuthOpProbe, FString());
	}
	else
	{
		AuthStatuses.FindOrAdd(AgentId).BusyOp.Empty();
		PushAgentAuthStatus(AgentId);
	}
}

void FUECPAgentRunnerCoordinator::AbortAuthOp(const FString& AgentId)
{
	TSharedPtr<FAgentAuthOp> Op;
	if (!AuthOps.RemoveAndCopyValue(AgentId, Op)) return;

	if (Op->Ticker.IsValid()) FTSTicker::GetCoreTicker().RemoveTicker(Op->Ticker);
	// A session still inside Start() is stopped by its own continuation once the op is gone.
	if (Op->Session.IsValid() && Op->bStarted) Op->Session->Stop();
	if (Op->TerminalProc.IsValid())
	{
		FPlatformProcess::TerminateProc(Op->TerminalProc, true);
		FPlatformProcess::CloseProc(Op->TerminalProc);
	}
}

void FUECPAgentRunnerCoordinator::AddAuthOpTicker(const TSharedPtr<FAgentAuthOp>& Op)
{
	TWeakPtr<FAgentAuthOp> WeakOp = Op;
	Op->Ticker = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateLambda([this, WeakOp](float ) -> bool
		{
			const TSharedPtr<FAgentAuthOp> Pinned = WeakOp.Pin();
			return Pinned.IsValid() && TickAuthOp(Pinned);
		}),
		0.1f);
}

void FUECPAgentRunnerCoordinator::StartTerminalLogin(const FString& AgentId, const FUECPACPAuthMethod& Method)
{
	AbortAuthOp(AgentId);

	IUECPACPRegistryService& Reg = IUECPCoreModule::Get().GetACPRegistryService();
	const FUECPACPInstallMarker Marker = Reg.GetInstallMarker(AgentId);

	FString Command = Method.TerminalCommand;
	TArray<FString> CmdArgs;
	if (Command.IsEmpty())
	{
		Command = Marker.EntrypointCommand;
		CmdArgs = Marker.EntrypointArgs;
	}
	CmdArgs.Append(Method.TerminalArgs);

	FString Params;
	for (const FString& A : CmdArgs)
	{
		if (!Params.IsEmpty()) Params.AppendChar(TEXT(' '));
		Params += A.Contains(TEXT(" ")) ? FString::Printf(TEXT("\"%s\""), *A) : A;
	}

	// Not detached: the agent's console window is what the user signs in through.
	FProcHandle Proc = FPlatformProcess::CreateProc(*Command, *Params, false, false, false,
		nullptr, 0, nullptr, nullptr, nullptr);
	if (!Proc.IsValid())
	{
		FailAuthRequest(AgentId, TEXT("Não foi possível abrir a janela de login do agente."));
		return;
	}

	const TSharedPtr<FAgentAuthOp> Op = MakeShared<FAgentAuthOp>();
	Op->AgentId      = AgentId;
	Op->Kind         = AuthOpLogin;
	Op->MethodId     = Method.Id;
	Op->TerminalProc = Proc;
	Op->bStarted     = true;
	Op->Deadline     = FPlatformTime::Seconds() + 600.0;
	AuthOps.Add(AgentId, Op);

	FUECPACPAuthStatus& Status = AuthStatuses.FindOrAdd(AgentId);
	Status.BusyOp = AuthOpLogin;
	Status.Error.Empty();
	Status.LoginUrl.Empty();
	PushAgentAuthStatus(AgentId);

	AddAuthOpTicker(Op);
}

void FUECPAgentRunnerCoordinator::StartAuthOp(const FString& AgentId, const FString& Kind, const FString& MethodId,
	const FString& AfterOp, const FString& CarriedError)
{
	AbortAuthOp(AgentId);

	FUECPACPAuthStatus& Status = AuthStatuses.FindOrAdd(AgentId);
	IUECPACPRegistryService& Reg = IUECPCoreModule::Get().GetACPRegistryService();
	const FUECPACPInstallMarker Marker = Reg.GetInstallMarker(AgentId);
	if (Marker.AgentId.IsEmpty() || Marker.EntrypointCommand.IsEmpty())
	{
		Status.State = EUECPACPAuthState::Unknown;
		FailAuthRequest(AgentId, TEXT("Agente não instalado."));
		return;
	}

	Status.BusyOp = Kind;
	Status.Error  = CarriedError;
	if (Kind == AuthOpLogin) Status.LoginUrl.Empty();
	PushAgentAuthStatus(AgentId);

	const TSharedPtr<FAgentAuthOp> Op = MakeShared<FAgentAuthOp>();
	Op->AgentId      = AgentId;
	Op->Kind         = Kind;
	Op->MethodId     = MethodId;
	Op->AfterOp      = AfterOp;
	Op->CarriedError = CarriedError;
	Op->Deadline     = FPlatformTime::Seconds() + (Kind == AuthOpLogin ? 300.0 : 45.0);
	Op->Session      = MakeShared<FUECPACPSession>();
	AuthOps.Add(AgentId, Op);

	// The op owns the session and is only destroyed outside its Tick, so a raw pointer is safe in these delegates.
	FAgentAuthOp* Raw = Op.Get();
	Op->Session->OnAuthCapabilities.BindLambda(
		[Raw](const TArray<FUECPACPAuthMethod>& Methods, bool bLogout, bool bPushesStatus)
		{
			Raw->Methods       = Methods;
			Raw->bLogout       = bLogout;
			Raw->bPushesStatus = bPushesStatus;
			Raw->bGotCaps      = true;

			TFunction<void(const FString&)> Done = [Raw](const FString& Error)
			{
				Raw->Error     = Error;
				Raw->bFinished = true;
			};

			if (Raw->Kind == AuthOpProbe)
			{
				Raw->Session->RequestSessionNew();
			}
			else if (Raw->Kind == AuthOpLogin)
			{
				const TSharedRef<FJsonObject> Params = MakeShared<FJsonObject>();
				Params->SetStringField(TEXT("methodId"), Raw->MethodId);
				Raw->Session->SendAuthRequest(TEXT("authenticate"), Params, MoveTemp(Done));
			}
			else if (!bLogout)
			{
				Done(TEXT("Este agente não oferece logout pelo protocolo."));
			}
			else
			{
				Raw->Session->SendAuthRequest(TEXT("logout"), MakeShared<FJsonObject>(), MoveTemp(Done));
			}
		});
	Op->Session->OnAuthStatusUpdate.BindLambda([Raw](const FString& Kind, const FString& Email, const FString& Label)
	{
		Raw->bGotStatus = true;
		Raw->StatusKind = Kind;
		Raw->Email      = Email;
		Raw->Label      = Label;
	});
	Op->Session->OnAuthSessionReady.BindLambda([Raw](const FString& TierName)
	{
		Raw->TierName = TierName;
		Raw->ReadyAt  = FPlatformTime::Seconds();
	});
	Op->Session->OnAuthSessionFailed.BindLambda([Raw](const FString& Message, int32 Code)
	{
		Raw->SessionError     = Message;
		Raw->SessionErrorCode = Code;
		Raw->bFinished        = true;
	});
	Op->Session->OnStderrLine.BindLambda([this, Raw](const FString& Line)
	{
		if (Raw->Kind != AuthOpLogin) return;
		FUECPACPAuthStatus* S = AuthStatuses.Find(Raw->AgentId);
		if (!S || !S->LoginUrl.IsEmpty()) return;
		S->LoginUrl = ExtractLoginUrl(Line);
		if (!S->LoginUrl.IsEmpty()) PushAgentAuthStatus(Raw->AgentId);
	});

	FUECPACPSession::FSpawnArgs Args;
	Args.Command = Marker.EntrypointCommand;
	Args.Args    = Marker.EntrypointArgs;
	Args.Cwd     = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
	Args.Cwd.ReplaceInline(TEXT("\\"), TEXT("/"));
	Args.LogTag  = FString::Printf(TEXT("%s-auth"), *AgentId);
	Args.bAuthConnection = true;

	FPlatformMisc::SetEnvironmentVar(TEXT("CLAUDECODE"), nullptr);
	FPlatformMisc::SetEnvironmentVar(TEXT("CLAUDE_CODE_ENTRYPOINT"), nullptr);
	FPlatformMisc::SetEnvironmentVar(TEXT("NO_COLOR"), TEXT("1"));
	EnsureAntigravityConfigured(AgentId);

	TSharedPtr<FUECPACPSession> Session = Op->Session;
	TWeakPtr<FAgentAuthOp> WeakOp = Op;
	Async(EAsyncExecution::Thread, [this, Session, Args, WeakOp]()
	{
		const bool bSpawned = Session->Start(Args);
		AsyncTask(ENamedThreads::GameThread, [this, Session, WeakOp, bSpawned]()
		{
			const TSharedPtr<FAgentAuthOp> Pinned = WeakOp.Pin();
			if (!Pinned.IsValid())
			{
				if (bSpawned) Session->Stop();
				return;
			}
			Pinned->bStarted = true;
			if (bSpawned)
			{
				AddAuthOpTicker(Pinned);
				return;
			}
			Pinned->Error = TEXT("Não foi possível iniciar o agente.");
			FinishAuthOp(Pinned);
		});
	});
}

bool FUECPAgentRunnerCoordinator::TickAuthOp(const TSharedPtr<FAgentAuthOp>& Op)
{
	const double Now = FPlatformTime::Seconds();

	if (Op->TerminalProc.IsValid())
	{
		if (FPlatformProcess::IsProcRunning(Op->TerminalProc))
		{
			if (Now <= Op->Deadline) return true;
			FPlatformProcess::TerminateProc(Op->TerminalProc, true);
			Op->Error = TEXT("O login expirou.");
		}
		else
		{
			int32 ExitCode = 0;
			FPlatformProcess::GetProcReturnCode(Op->TerminalProc, &ExitCode);
			if (ExitCode != 0) Op->Error = FString::Printf(TEXT("O login terminou com o código %d."), ExitCode);
		}
		FPlatformProcess::CloseProc(Op->TerminalProc);
		Op->TerminalProc.Reset();
		FinishAuthOp(Op);
		return false;
	}

	const bool bAlive = Op->Session->Tick();

	// An agent that pushes its identity gets a moment after session/new to do so.
	if (Op->ReadyAt > 0.0 && !Op->bFinished
		&& (!Op->bPushesStatus || Op->bGotStatus || Now - Op->ReadyAt >= 4.0))
	{
		Op->bFinished = true;
	}
	if (!Op->bFinished && !bAlive)
	{
		Op->bFinished = true;
		Op->Error = TEXT("O agente encerrou antes de responder.");
	}
	if (!Op->bFinished && Now > Op->Deadline)
	{
		Op->bFinished = true;
		Op->Error = Op->Kind == AuthOpLogin
			? TEXT("O login não foi concluído a tempo.")
			: TEXT("O agente não respondeu a tempo.");
	}
	if (!Op->bFinished) return true;

	FinishAuthOp(Op);
	return false;
}

void FUECPAgentRunnerCoordinator::FinishAuthOp(const TSharedPtr<FAgentAuthOp>& Op)
{
	const FString AgentId = Op->AgentId;
	AuthOps.Remove(AgentId);
	if (Op->Session.IsValid()) Op->Session->Stop();

	UE_LOG(LogUECPACP, Log, TEXT("Agent auth op '%s' finished for '%s'%s"),
		*Op->Kind, *AgentId, Op->Error.IsEmpty() ? TEXT("") : TEXT(" with an error"));

	if (Op->Kind != AuthOpProbe)
	{
		StartAuthOp(AgentId, AuthOpProbe, FString(), Op->Kind, Op->Error);
		return;
	}

	FUECPACPAuthStatus& Status = AuthStatuses.FindOrAdd(AgentId);
	Status.BusyOp.Empty();
	Status.LastProbeAt = FPlatformTime::Seconds();
	Status.Error = Op->CarriedError;
	Status.Email.Empty();
	Status.Plan.Empty();
	if (Op->bGotCaps)
	{
		Status.Methods         = Op->Methods;
		Status.bSupportsLogout = Op->bLogout;
	}

	if (Op->bGotStatus && Op->StatusKind == TEXT("none"))
	{
		Status.State = EUECPACPAuthState::Disconnected;
	}
	else if (Op->bGotStatus || Op->ReadyAt > 0.0)
	{
		Status.State = EUECPACPAuthState::Connected;
		Status.Email = Op->Email;
		Status.Plan  = Op->Label.IsEmpty() ? Op->TierName : Op->Label;
		if (Status.Plan.IsEmpty() && AgentId.Contains(TEXT("antigravity"), ESearchCase::IgnoreCase)
			&& !FPlatformMisc::GetEnvironmentVariable(TEXT("GEMINI_API_KEY")).IsEmpty())
		{
			Status.Plan = TEXT("Chave de API do Gemini");
		}
	}
	else if (!Op->SessionError.IsEmpty() && Op->SessionErrorCode == -32000 && IsAuthFailureMessage(Op->SessionError))
	{
		Status.State = EUECPACPAuthState::Disconnected;
	}
	else
	{
		Status.State = EUECPACPAuthState::Unknown;
		if (Status.Error.IsEmpty()) Status.Error = Op->SessionError.IsEmpty() ? Op->Error : Op->SessionError;
	}

	if (Status.Error.IsEmpty() && Op->AfterOp == AuthOpLogin && Status.State == EUECPACPAuthState::Disconnected)
	{
		Status.Error = TEXT("O login terminou, mas o agente continua desconectado.");
	}
	if (Status.Error.IsEmpty() && Op->AfterOp == AuthOpLogout && Status.State == EUECPACPAuthState::Connected)
	{
		Status.Error = TEXT("O agente continua conectado: a sessão pode vir de uma chave de API ou variável de ambiente.");
	}

	const EUECPACPAuthState State = Status.State;
	PushAgentAuthStatus(AgentId);

	auto W = Shell.Pin();
	if (!W.IsValid()) return;

	const bool bFlagged = W->AgentAuthStates.Contains(AgentId);
	if (State == EUECPACPAuthState::Disconnected && !bFlagged)
	{
		SUECPMainWidget::FACPAgentAuthState Required;
		Required.bRequired = true;
		W->AgentAuthStates.Add(AgentId, Required);
		if (W->SettingsBridgeObject) W->SettingsBridgeObject->PushACPCatalog();
		if (W->AppBridgeObject) W->AppBridgeObject->PushModelPicker();
	}
	else if (State == EUECPACPAuthState::Connected && (bFlagged || Op->AfterOp == AuthOpLogin))
	{
		W->AgentAuthStates.Remove(AgentId);
		W->AgentDiscoveryAttempted.Add(AgentId);
		if (W->SettingsBridgeObject) W->SettingsBridgeObject->PushACPCatalog();
		DiscoverAgentConfig(AgentId);
	}
}

bool FUECPAgentRunnerCoordinator::OnACPInstanceTick(TSharedPtr<FAgentRunnerInstance> Inst)
{
	TSharedPtr<SUECPMainWidget> W = Shell.Pin();
	if (!W.IsValid() || !Inst.IsValid() || !Inst->ACPSession.IsValid()) return false;

	const bool bAlive = Inst->ACPSession->Tick();

	if (bAlive && Inst->ACPSession->GetState() == FUECPACPSession::EState::Ready)
	{
		if (!Inst->bPrefsApplied)
		{
			Inst->bPrefsApplied = true;

			int32 Slot = -1;
			if (const int32* PerChat = W->ArchitectApiKeySlotByChat.Find(Inst->ChatID))
			{
				if (*PerChat >= 0) Slot = *PerChat;
			}
			if (Slot < 0) Slot = FApiKeyManager::Get().GetActiveSlotIndex();
			const FString Section = FString::Printf(TEXT("BpGeneratorUltimate.ACP.Slot%d"), Slot);
			FString SavedModel;
			GConfig->GetString(*Section, *FString::Printf(TEXT("%s.Model"), *Inst->ProviderName),
				SavedModel, FSettingsManager::GetGlobalConfigPath());

			EAIInteractionMode ChatMode = EAIInteractionMode::AutoEdit;
			if (const EAIInteractionMode* Found = W->ArchitectInteractionModeByChat.Find(Inst->ChatID))
				ChatMode = *Found;
			else
				ChatMode = W->GetActiveArchitectInteractionMode();

			bool bDestructiveConfirmCfg = true;
			GConfig->GetBool(TEXT("BpGeneratorUltimate"), TEXT("DestructiveOpsConfirm"),
				bDestructiveConfirmCfg, FSettingsManager::GetGlobalConfigPath());
		{ bool bAxTurbo = false; GConfig->GetBool(TEXT("BpGeneratorUltimate"), TEXT("TurboMode"), bAxTurbo, FSettingsManager::GetGlobalConfigPath()); if (bAxTurbo) bDestructiveConfirmCfg = false; } // Axivor Turbo: no destructive confirmations
			if (IUECPCoreModule::IsAvailable()
				&& IUECPCoreModule::Get().GetCrewService().ShouldAutoApproveDestructiveForChat(Inst->ChatID))
			{
				bDestructiveConfirmCfg = false;
			}

			TArray<FString> Candidates;
			switch (ChatMode)
			{
			case EAIInteractionMode::JustChat:
				Candidates = { TEXT("read-only"), TEXT("readonly"), TEXT("dontask"), TEXT("dont_ask") };
				break;
			case EAIInteractionMode::AskBeforeEdit:
				Candidates = { TEXT("on-request"), TEXT("default"), TEXT("agent"), TEXT("ask") };
				break;
			case EAIInteractionMode::PlanMode:
			case EAIInteractionMode::AutoEdit:
			default:
				if (bDestructiveConfirmCfg)
				{
					Candidates = { TEXT("on-request"), TEXT("default"), TEXT("agent"), TEXT("ask") };
				}
				else
				{
					Candidates = { TEXT("acceptedits"), TEXT("autoedit"), TEXT("autopilot"), TEXT("yolo"), TEXT("auto"), TEXT("bypass"), TEXT("code") };
				}
				break;
			}

			FString SelectedMode;
			if (const FUECPACPAgentConfigSnapshot* Snap = W->CachedAgentConfigs.Find(Inst->ProviderName))
			{
				for (const FString& Candidate : Candidates)
				{
					if (!SelectedMode.IsEmpty()) break;
					for (const FUECPACPConfigOption& M : Snap->Modes)
					{
						const FString Haystack = (M.Id + TEXT(" ") + M.Name).ToLower();
						if (Haystack.Contains(Candidate)) { SelectedMode = M.Id; break; }
					}
				}
			}

			TArray<TPair<FString, FString>> OtherPrefs;
			if (const FUECPACPAgentConfigSnapshot* SnapForOther = W->CachedAgentConfigs.Find(Inst->ProviderName))
			{
				for (const FUECPACPAgentConfigSection& Sec : SnapForOther->OtherSections)
				{
					if (Sec.Id.IsEmpty()) continue;
					FString SavedVal;
					const FString Key = FString::Printf(TEXT("%s.Option.%s"), *Inst->ProviderName, *Sec.Id);
					GConfig->GetString(*Section, *Key, SavedVal, FSettingsManager::GetGlobalConfigPath());
					if (!SavedVal.IsEmpty()) OtherPrefs.Emplace(Sec.Id, SavedVal);
				}
			}

			if (!SelectedMode.IsEmpty() || !SavedModel.IsEmpty() || OtherPrefs.Num() > 0)
			{
				UE_LOG(LogUECPACP, Log, TEXT("Applying session prefs chat=%s agent=%s uiMode=%d → agentMode=%s model=%s other=%d"),
					*Inst->ChatID, *Inst->ProviderName, (int32)ChatMode, *SelectedMode, *SavedModel, OtherPrefs.Num());

				if (Inst->ProviderName.Equals(TEXT("kilo"), ESearchCase::IgnoreCase)
					&& SavedModel.Contains(TEXT("-auto/"), ESearchCase::IgnoreCase))
				{
					UE_LOG(LogUECPACP, Warning,
						TEXT("[%s] kilo auto-routing model `%s` selected — Kilo's gateway frequently hangs on these via ACP. "
						     "If the agent doesn't respond, pick a specific model (e.g. `anthropic/claude-sonnet-4-5/free`) in Settings instead of `*-auto/*`."),
						*Inst->ProviderName, *SavedModel);
				}
				Inst->ACPSession->ApplyUserPrefs(SelectedMode, SavedModel, OtherPrefs);
			}
		}

		if (!Inst->PendingACPPrompt.IsEmpty())
		{
			const FString ToSend = Inst->PendingACPPrompt;
			Inst->PendingACPPrompt.Reset();

			if (Inst->PendingACPImages.Num() > 0)
			{
				TArray<UECPACPSchema::FPromptContentBlock> Blocks;
				Blocks.Reserve(1 + Inst->PendingACPImages.Num());
				{
					UECPACPSchema::FPromptContentBlock TextBlock;
					TextBlock.Type = TEXT("text");
					TextBlock.Text = ToSend;
					Blocks.Add(MoveTemp(TextBlock));
				}
				for (const TPair<FString, FString>& Img : Inst->PendingACPImages)
				{
					UECPACPSchema::FPromptContentBlock ImgBlock;
					ImgBlock.Type       = TEXT("image");
					ImgBlock.MimeType   = Img.Key;
					ImgBlock.Base64Data = Img.Value;
					Blocks.Add(MoveTemp(ImgBlock));
				}
				const int32 ImageCount = Inst->PendingACPImages.Num();
				Inst->PendingACPImages.Reset();
				UE_LOG(LogUECPACP, Log, TEXT("Dispatching prompt (rich) chat=%s agent=%s promptLen=%d images=%d"),
					*Inst->ChatID, *Inst->ProviderName, ToSend.Len(), ImageCount);
				Inst->ACPSession->PromptRich(Blocks);
			}
			else
			{
				UE_LOG(LogUECPACP, Log, TEXT("Dispatching prompt chat=%s agent=%s promptLen=%d"),
					*Inst->ChatID, *Inst->ProviderName, ToSend.Len());
				Inst->ACPSession->Prompt(ToSend);
			}
		}
	}

	if (!bAlive)
	{
		if (Inst->SourceView != EAgentSourceView::ProjectScanner)
		{
			W->ArchitectConversationHistory = Inst->LocalHistory;
			W->SaveArchitectChatHistory(Inst->ChatID);
		}
		W->AgentInstances.Remove(Inst->ChatID);
	}
	return bAlive;
}
