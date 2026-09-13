// Copyright 2026, BlueprintsLab, All rights reserved

#include "Widget/UUECPAppBridge.h"
#include "Widget/UUECPSettingsBridge.h"
#include "UECPCoreModule.h"
#include "Services/IUECPBugReportService.h"
#include "Services/IUECPGddService.h"
#include "Services/IUECPAiMemoryService.h"
#include "Services/IUECPVoiceService.h"
#include "Services/IUECPAnalystService.h"
#include "Services/IUECPScannerService.h"
#include "Services/IUECPArchitectService.h"
#include "Services/IUECPAgentRunnerService.h"
#include "Services/IUECPArchitectService.h"
#include "Services/IUECPCrewService.h"
#include "Services/IUECPACPRegistryService.h"
#include "Services/IUECPExtensionService.h"
#include "Services/IUECPToolDispatcher.h"
#include "Types/CrewTypes.h"
#include "ApiKeyManager.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Managers/HttpCommunicationManager.h"
#include "Managers/FreeTierConfigManager.h"
#include "Managers/ChatHistoryManager.h"
#include "AssetReferenceManager.h"
#include "SUECPMainWidget.h"
#include "Managers/PlanManager.h"
#include "Managers/TaskManager.h"
#include "SWebBrowser.h"
#include "Misc/FileHelper.h"
#include "Misc/Base64.h"
#include "Interfaces/IPluginManager.h"
#include "Utils/MountResolver.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "AssetReferenceTypes.h"
#include "Managers/ChatHistoryManager.h"
#include "Managers/SettingsManager.h"
#include "Serialization/JsonSerializer.h"
#include "SBlueprintDiff.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonWriter.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Tools/AssetPropertyTools.h"
#include "Tools/ProfilerTools.h"
#include "MeshAssetManager.h"
#include "Async/Async.h"
#include "DesktopPlatformModule.h"
#include "IDesktopPlatform.h"
#include "Engine/SkeletalMesh.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "GenericPlatform/GenericPlatformHttp.h"
#include "Utils/DiagramUtils.h"
#include "Widgets/SWindow.h"
#include "Framework/Application/SlateApplication.h"
#include "Containers/Ticker.h"

#define LOCTEXT_NAMESPACE "UUECPAppBridge"

void UUECPAppBridge::ExecJs(const FString& Js)
{
	auto Browser = BrowserRef.Pin();
	if (Browser.IsValid())
		Browser->ExecuteJavascript(Js);
}

static FString EscJs(const FString& In)
{
	FString Out = In;
	Out.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
	Out.ReplaceInline(TEXT("\""), TEXT("\\\""));
	Out.ReplaceInline(TEXT("\n"), TEXT("\\n"));
	Out.ReplaceInline(TEXT("\r"), TEXT("\\r"));
	Out.ReplaceInline(TEXT("\t"), TEXT("\\t"));
	return Out;
}

FString UUECPAppBridge::BuildImagesJson(const TArray<FAttachedImage>& Images)
{
	TArray<TSharedPtr<FJsonValue>> Arr;
	for (const FAttachedImage& Img : Images)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("name"), Img.Name);
		Obj->SetStringField(TEXT("mime"), Img.MimeType);
		Obj->SetStringField(TEXT("data"), Img.Base64Data);
		Arr.Add(MakeShared<FJsonValueObject>(Obj));
	}
	FString Json;
	TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Json);
	FJsonSerializer::Serialize(Arr, Writer);
	return Json;
}

FString UUECPAppBridge::BuildFileContextsJson(const TArray<FAttachedFileContext>& Files)
{
	FString Json = TEXT("[");
	for (int32 i = 0; i < Files.Num(); i++)
	{
		if (i > 0) Json += TEXT(",");
		Json += FString::Printf(TEXT("{\"name\":\"%s\",\"chars\":%d}"),
			*EscJs(Files[i].FileName), Files[i].CharCount);
	}
	return Json + TEXT("]");
}

void UUECPAppBridge::PushBoneListFromWidget(SUECPMainWidget* W)
{
	if (!W) return;
	FString Json = TEXT("[");
	for (int32 i = 0; i < W->BoneList.Num(); i++)
	{
		if (i > 0) Json += TEXT(",");
		const FString& Bone = *W->BoneList[i];
		Json += FString::Printf(TEXT("{\"name\":\"%s\",\"selected\":%s}"),
			*EscJs(Bone), W->SelectedBones.Contains(Bone) ? TEXT("true") : TEXT("false"));
	}
	Json += TEXT("]");
	ExecJs(FString::Printf(TEXT("if(typeof onBoneList==='function')onBoneList(\"%s\",%s)"),
		*EscJs(W->LoadedMeshPath), *Json));
}

void UUECPAppBridge::SwitchView(const FString& ViewName)
{
	auto W = OwnerWidget.Pin();
	if (!W.IsValid()) return;

	if (ViewName == TEXT("architect"))
	{
		W->OnArchitectViewSelected();
		PushSlashContext();
	}
}

void UUECPAppBridge::OpenSettings()
{
	auto W = OwnerWidget.Pin();
	if (W.IsValid()) W->OnShowSettingsClicked();
}

void UUECPAppBridge::PushOverlayHtml(const FString& Html)
{
	ExecJs(FString::Printf(TEXT("if(typeof onOverlayContent==='function')onOverlayContent(\"%s\")"), *EscJs(Html)));
}

void UUECPAppBridge::PushPendingMemoryCount(int32 Count)
{
	ExecJs(FString::Printf(TEXT("if(typeof onPendingMemoryCount==='function')onPendingMemoryCount(%d)"), Count));
}

void UUECPAppBridge::ShowOverlay(const FString& OverlayName)
{
	if (OverlayName == TEXT("gdd"))
	{
		PushOverlayHtml(IUECPCoreModule::Get().GetGddService().BuildOverlayHtml());
	}
	else if (OverlayName == TEXT("memory"))
	{
		PushOverlayHtml(IUECPCoreModule::Get().GetAiMemoryService().BuildOverlayHtml());
	}
	else if (OverlayName == TEXT("instructions"))
	{
		FString Instructions = FSettingsManager::Get().LoadCustomInstructions();
		ExecJs(FString::Printf(TEXT("if(typeof onCustomInstructions==='function')onCustomInstructions(\"%s\")"), *EscJs(Instructions)));
	}
	else if (OverlayName == TEXT("bugreport"))
	{
		FString Html = TEXT(
			"<div style='font-size:13px'>"
			"<div style='margin-bottom:10px'><label style='color:var(--text2);font-size:11px;display:block;margin-bottom:4px'>Issue Type</label>"
			"<select id='bug-type' style='width:100%;background:var(--input);border:1px solid var(--bdr);color:var(--text);padding:5px 8px;border-radius:6px;font-size:12px'>"
			"<option value='bug'>Bug / Crash</option><option value='feedback'>General Feedback</option><option value='feature_request'>Feature Request</option>"
			"</select></div>"
			"<div style='margin-bottom:10px'><label style='color:var(--text2);font-size:11px;display:block;margin-bottom:4px'>Attach Conversation</label>"
			"<select id='bug-conv' style='width:100%;background:var(--input);border:1px solid var(--bdr);color:var(--text);padding:5px 8px;border-radius:6px;font-size:12px'>"
			"<option value=''>None (no conversation attached)</option>"
			"</select></div>"
			"<div style='margin-bottom:10px'><label style='color:var(--text2);font-size:11px;display:block;margin-bottom:4px'>Description</label>"
			"<textarea id='bug-desc' rows='4' style='width:100%;background:var(--input);border:1px solid var(--bdr);color:var(--text);padding:6px 10px;border-radius:6px;font:inherit;font-size:12px;resize:vertical;box-sizing:border-box'></textarea></div>"
			"<div style='margin-bottom:8px;display:flex;gap:4px;align-items:center;flex-wrap:wrap'>"
			"<button class='attach-btn' style='font-size:11px;padding:3px 8px' onclick=\"app('attachbugimage')\">&#128206; Image</button>"
			"<button class='attach-btn' style='font-size:11px;padding:3px 8px' onclick=\"app('pastebugimage')\">&#128203; Paste Image</button>"
			"<button class='attach-btn' style='font-size:11px;padding:3px 8px;color:var(--err)' onclick=\"app('clearbugimages')\">Clear</button>"
			"</div>"
			"<div id='bug-images' style='display:flex;flex-wrap:wrap;gap:4px;margin-bottom:8px'></div>"
			"<button class='send-btn' style='font-size:12px;padding:6px 14px' onclick=\"app('submitbugreport',JSON.stringify({type:document.getElementById('bug-type').value,description:document.getElementById('bug-desc').value,conversation_id:document.getElementById('bug-conv').value}));closeBugReport()\">Submit Report</button>"
			"</div>");
		ExecJs(FString::Printf(TEXT("document.getElementById('bugreport-body').innerHTML=\"%s\"; if(typeof populateBugReportConvs==='function')populateBugReportConvs();"), *EscJs(Html)));
	}
}

void UUECPAppBridge::HideOverlay()
{
}

void UUECPAppBridge::OpenExternalUrl(const FString& Url)
{
	FPlatformProcess::LaunchURL(*Url, nullptr, nullptr);
}

void UUECPAppBridge::SetAnnouncementsHidden(bool bHidden)
{
	GConfig->SetBool(TEXT("BpGeneratorUltimate"), TEXT("HideAnnouncements"),
		bHidden, FSettingsManager::GetGlobalConfigPath());
	GConfig->Flush(false, FSettingsManager::GetGlobalConfigPath());
}

void UUECPAppBridge::SendMessage(const FString& View, const FString& Text)
{
	auto W = OwnerWidget.Pin();
	if (!W.IsValid()) return;

	if (View == TEXT("architect"))
	{
		IUECPCoreModule::Get().GetArchitectService().SendMessage(Text);
	}
	else if (View == TEXT("analyst"))
	{
		IUECPCoreModule::Get().GetAnalystService().SendMessage(Text);
	}
	else if (View == TEXT("scanner"))
	{
		IUECPCoreModule::Get().GetScannerService().SendMessage(Text);
	}
}

void UUECPAppBridge::SendMessageNow(const FString& View, const FString& Text)
{
	auto W = OwnerWidget.Pin();
	if (!W.IsValid()) return;

	// "Send now" — interrupt a running turn and send immediately instead of queuing.
	// Only the Architect surface implements true interrupt; other views fall back to a
	// normal send (which still queues while busy).
	if (View == TEXT("architect"))
	{
		IUECPCoreModule::Get().GetArchitectService().SendMessageInterrupt(Text);
	}
	else if (View == TEXT("analyst"))
	{
		IUECPCoreModule::Get().GetAnalystService().SendMessage(Text);
	}
	else if (View == TEXT("scanner"))
	{
		IUECPCoreModule::Get().GetScannerService().SendMessage(Text);
	}
}

void UUECPAppBridge::StopGeneration(const FString& View)
{
	auto W = OwnerWidget.Pin();
	if (!W.IsValid()) return;

	if (View == TEXT("architect"))
		IUECPCoreModule::Get().GetArchitectService().StopGeneration();
	else if (View == TEXT("scanner"))
		IUECPCoreModule::Get().GetScannerService().StopGeneration();
}

void UUECPAppBridge::RemoveQueuedArchitectMessage(const FString& IndexStr)
{
	auto W = OwnerWidget.Pin();
	if (!W.IsValid()) return;
	const int32 Index = FCString::Atoi(*IndexStr);
	TArray<FQueuedArchitectMessage>* Queue =
		W->ArchitectMessageQueueByChat.Find(W->ActiveArchitectChatID);
	if (!Queue || !Queue->IsValidIndex(Index)) return;
	Queue->RemoveAt(Index);
	if (Queue->Num() == 0) W->ArchitectMessageQueueByChat.Remove(W->ActiveArchitectChatID);
	W->PushArchitectQueueStateToJs();
}

void UUECPAppBridge::SendQueuedNow(const FString& IndexStr)
{
	// "Send now" on a queued message: interrupt the running turn (if any) and dispatch
	// exactly that entry, attachments included - the JS side only holds previews.
	auto W = OwnerWidget.Pin();
	if (!W.IsValid()) return;
	const int32 Index = FCString::Atoi(*IndexStr);
	const FString ChatID = W->ActiveArchitectChatID;

	TArray<FQueuedArchitectMessage>* Queue = W->ArchitectMessageQueueByChat.Find(ChatID);
	if (!Queue || !Queue->IsValidIndex(Index)) return;

	// The drain always sends the FRONT entry; promote the chosen one first.
	if (Index > 0)
	{
		FQueuedArchitectMessage Entry = MoveTemp((*Queue)[Index]);
		Queue->RemoveAt(Index);
		Queue->Insert(MoveTemp(Entry), 0);
	}

	// Interrupt only when mid-turn: stopping an idle chat would inject a spurious
	// cancel bubble. StopGeneration clears the busy state synchronously, so the
	// drain below sends for real instead of re-queueing.
	const bool bBusy = W->ArchitectThinkingChats.Contains(ChatID)
		|| W->PendingArchitectRequests.Contains(ChatID);
	if (bBusy)
	{
		IUECPCoreModule::Get().GetArchitectService().StopGeneration();
	}

	W->DrainQueuedArchitectMessage(ChatID);
	W->PushArchitectQueueStateToJs();
}

void UUECPAppBridge::NewChat(const FString& View)
{
	auto W = OwnerWidget.Pin();
	if (!W.IsValid()) return;

	if (View == TEXT("architect"))
		IUECPCoreModule::Get().GetArchitectService().NewChat();
	else if (View == TEXT("analyst"))
		IUECPCoreModule::Get().GetAnalystService().NewChat();
	else if (View == TEXT("scanner"))
		IUECPCoreModule::Get().GetScannerService().NewChat();
}

void UUECPAppBridge::SwitchChat(const FString& View, const FString& ChatId)
{
	auto W = OwnerWidget.Pin();
	if (!W.IsValid()) return;

	if (View == TEXT("architect"))
	{
		IUECPCoreModule::Get().GetArchitectService().SwitchChat(ChatId);
		return;
	}
	else if (View == TEXT("analyst"))
	{
		for (const TSharedPtr<FConversationInfo>& Info : W->ConversationList)
		{
			if (Info.IsValid() && Info->ID == ChatId)
			{
				W->OnChatSelectionChanged(Info, ESelectInfo::OnMouseClick);
				break;
			}
		}
	}
	else if (View == TEXT("scanner"))
	{
		IUECPCoreModule::Get().GetScannerService().SwitchChat(ChatId);
		return;
	}
}

void UUECPAppBridge::DeleteAllChats(const FString& View, bool bIncludeCrew)
{
	auto W = OwnerWidget.Pin();
	if (!W.IsValid()) return;

	IUECPCoreModule::Get().GetAgentRunnerService().StopAllAgents();

	if (View == TEXT("architect"))
	{
		IUECPCoreModule::Get().GetArchitectService().StopAllGeneration();

		if (W->ActiveArchitectHttpRequest.IsValid())
		{
			W->ActiveArchitectHttpRequest->CancelRequest();
			W->ActiveArchitectHttpRequest.Reset();
		}

		IUECPCrewService& Crew = IUECPCoreModule::Get().GetCrewService();
		const FString Folder = FChatHistoryManager::GetViewFolder(EConversationViewType::Architect);

		TSet<FString> CrewChatIds;
		if (!bIncludeCrew)
		{
			for (const FCrewRun& Run : Crew.GetRuns())
				for (const TPair<FString, FString>& P : Run.RoleChatIds)
					if (!P.Value.IsEmpty()) CrewChatIds.Add(P.Value);
		}

		TArray<TSharedPtr<FConversationInfo>> Kept;
		for (const TSharedPtr<FConversationInfo>& Conv : W->ArchitectConversationList)
		{
			if (!Conv.IsValid() || Conv->ID.IsEmpty()) continue;
			if (CrewChatIds.Contains(Conv->ID))
			{
				Kept.Add(Conv);
				continue;
			}
			IFileManager::Get().Delete(*(Folder / Conv->ID + TEXT(".json")));
			if (TSharedPtr<IHttpRequest, ESPMode::ThreadSafe>* P = W->PendingArchitectRequests.Find(Conv->ID))
			{
				if (P->IsValid()) (*P)->CancelRequest();
			}
			W->PendingArchitectRequests.Remove(Conv->ID);
			W->ArchitectThinkingChats.Remove(Conv->ID);
			W->ArchitectInteractionModeByChat.Remove(Conv->ID);
			W->ArchitectApiKeySlotByChat.Remove(Conv->ID);
		}

		W->ArchitectConversationList = Kept;
		W->ArchitectConversationHistory.Empty();
		W->ActiveArchitectChatID.Empty();
		W->bIsArchitectThinking = false;

		W->SaveArchitectManifest();

		W->OnNewArchitectChatClicked();
	}
	else if (View == TEXT("scanner"))
	{
		for (auto& Pair : W->PendingProjectRequests)
		{
			if (Pair.Value.IsValid()) Pair.Value->CancelRequest();
		}
		W->PendingProjectRequests.Empty();
		W->bIsProjectThinking = false;

		FString Folder = FChatHistoryManager::GetViewFolder(EConversationViewType::Project);
		TArray<FString> Files;
		IFileManager::Get().FindFiles(Files, *(Folder / TEXT("*.json")), true, false);
		for (const FString& File : Files)
			IFileManager::Get().Delete(*(Folder / File));

		W->ProjectConversationList.Empty();
		W->ProjectConversationHistory.Empty();
		W->ActiveProjectChatID.Empty();
		W->SaveProjectManifest();
		W->OnNewProjectChatClicked();
	}
	else if (View == TEXT("analyst"))
	{
		FString Folder = FChatHistoryManager::GetViewFolder(EConversationViewType::Analyst);
		TArray<FString> Files;
		IFileManager::Get().FindFiles(Files, *(Folder / TEXT("*.json")), true, false);
		for (const FString& File : Files)
			IFileManager::Get().Delete(*(Folder / File));

		W->ConversationList.Empty();
		W->AnalystConversationHistory.Empty();
		W->ActiveChatID.Empty();
		W->SaveManifest();
		W->OnNewChatClicked();
	}
}

void UUECPAppBridge::DeleteChat(const FString& View, const FString& ChatId)
{
	auto W = OwnerWidget.Pin();
	if (!W.IsValid()) return;

	auto DoDelete = [&](TArray<TSharedPtr<FConversationInfo>>& List,
	                    FString& ActiveId,
	                    EConversationViewType VType,
	                    TFunction<void()> SaveManifestFn,
	                    TFunction<void(const FString&)> SwitchToFirstFn,
	                    TFunction<void()> NewChatFn,
	                    TFunction<void()> PushListFn)
	{
		int32 Idx = List.IndexOfByPredicate([&](const TSharedPtr<FConversationInfo>& I) { return I.IsValid() && I->ID == ChatId; });
		if (Idx == INDEX_NONE) return;
		List.RemoveAt(Idx);

		FString FilePath = FChatHistoryManager::GetViewFolder(VType) / ChatId + TEXT(".json");
		IFileManager::Get().Delete(*FilePath);

		SaveManifestFn();

		if (ActiveId == ChatId)
		{
			if (List.Num() > 0)
			{
				SwitchToFirstFn(List[0]->ID);
				PushListFn();
			}
			else
			{
				NewChatFn();
			}
		}
		else
		{
			PushListFn();
		}
	};

	auto CancelPendingRequest = [](TMap<FString, TSharedPtr<IHttpRequest, ESPMode::ThreadSafe>>& PendingMap, const FString& Id)
	{
		if (TSharedPtr<IHttpRequest, ESPMode::ThreadSafe>* Found = PendingMap.Find(Id))
		{
			if (Found->IsValid()) (*Found)->CancelRequest();
			PendingMap.Remove(Id);
		}
	};

	IUECPCoreModule::Get().GetAgentRunnerService().StopAgentForChat(ChatId);

	if (View == TEXT("architect"))
	{
		TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> CancelledReq;
		if (TSharedPtr<IHttpRequest, ESPMode::ThreadSafe>* Found = W->PendingArchitectRequests.Find(ChatId))
		{
			CancelledReq = *Found;
		}
		CancelPendingRequest(W->PendingArchitectRequests, ChatId);
		W->ArchitectThinkingChats.Remove(ChatId);
		W->ArchitectInteractionModeByChat.Remove(ChatId);
		W->ArchitectApiKeySlotByChat.Remove(ChatId);
		if (W->ActiveArchitectChatID == ChatId)
		{
			W->bIsArchitectThinking = false;
		}
		if (CancelledReq.IsValid() && W->ActiveArchitectHttpRequest == CancelledReq)
		{
			W->ActiveArchitectHttpRequest.Reset();
		}
		DoDelete(W->ArchitectConversationList, W->ActiveArchitectChatID, EConversationViewType::Architect,
			[&W] { W->SaveArchitectManifest(); },
			[this](const FString& Id) { SwitchChat(TEXT("architect"), Id); },
			[&W] { W->OnNewArchitectChatClicked(); },
			[this] { if (auto W2 = OwnerWidget.Pin()) W2->PushArchitectChatListToJs(); });
	}
	else if (View == TEXT("analyst"))
		DoDelete(W->ConversationList, W->ActiveChatID, EConversationViewType::Analyst,
			[&W] { W->SaveManifest(); },
			[this](const FString& Id) { SwitchChat(TEXT("analyst"), Id); },
			[&W] { W->OnNewChatClicked(); },
			[this] { if (auto W2 = OwnerWidget.Pin()) W2->PushAnalystChatListToJs(); });
	else if (View == TEXT("scanner"))
	{
		CancelPendingRequest(W->PendingProjectRequests, ChatId);
		if (W->ActiveProjectChatID == ChatId)
		{
			W->bIsProjectThinking = false;
		}
		DoDelete(W->ProjectConversationList, W->ActiveProjectChatID, EConversationViewType::Project,
			[&W] { W->SaveProjectManifest(); },
			[this](const FString& Id) { SwitchChat(TEXT("scanner"), Id); },
			[&W] { W->OnNewProjectChatClicked(); },
			[this] { if (auto W2 = OwnerWidget.Pin()) W2->PushScannerChatListToJs(); });
	}
}

void UUECPAppBridge::RenameChat(const FString& View, const FString& ChatId, const FString& NewName)
{
	auto W = OwnerWidget.Pin();
	if (!W.IsValid() || NewName.IsEmpty()) return;

	auto DoRename = [&](TArray<TSharedPtr<FConversationInfo>>& List,
	                    TFunction<void()> SaveManifestFn,
	                    TFunction<void()> PushListFn)
	{
		for (TSharedPtr<FConversationInfo>& Info : List)
		{
			if (Info.IsValid() && Info->ID == ChatId)
			{
				Info->Title = NewName;
				SaveManifestFn();
				PushListFn();
				return;
			}
		}
	};

	if (View == TEXT("architect"))
		DoRename(W->ArchitectConversationList,
			[&W] { W->SaveArchitectManifest(); },
			[&W] { W->PushArchitectChatListToJs(); });
	else if (View == TEXT("analyst"))
		DoRename(W->ConversationList,
			[&W] { W->SaveManifest(); },
			[&W] { W->PushAnalystChatListToJs(); });
	else if (View == TEXT("scanner"))
		DoRename(W->ProjectConversationList,
			[&W] { W->SaveProjectManifest(); },
			[&W] { W->PushScannerChatListToJs(); });
}

void UUECPAppBridge::SetInteractionMode(const FString& Mode)
{
	auto W = OwnerWidget.Pin();
	if (!W.IsValid()) return;

	IUECPCoreModule::Get().GetArchitectService().SetInteractionMode(Mode);
}

void UUECPAppBridge::SetChatSlot(int32 SlotIndex)
{
	auto W = OwnerWidget.Pin();
	if (!W.IsValid()) return;
	const FString ChatId = W->ActiveArchitectChatID;
	if (ChatId.IsEmpty()) return;

	const int32 Clamped = (SlotIndex < 0) ? -1 : SlotIndex;
	if (Clamped < 0)
	{
		W->ArchitectApiKeySlotByChat.Remove(ChatId);
	}
	else
	{
		W->ArchitectApiKeySlotByChat.Add(ChatId, Clamped);
	}

	for (const TSharedPtr<FConversationInfo>& Conv : W->ArchitectConversationList)
	{
		if (Conv.IsValid() && Conv->ID == ChatId)
		{
			Conv->ApiKeySlotIndex = Clamped;
			W->SaveArchitectManifest();
			break;
		}
	}

	if (W->AgentInstances.Contains(ChatId))
	{
		IUECPCoreModule::Get().GetArchitectService().StopGenerationForChat(ChatId);
	}

	ExecJs(FString::Printf(
		TEXT("if(typeof onChatApiKeySlot==='function')onChatApiKeySlot(\"%s\",%d)"),
		*EscJs(ChatId), Clamped));
}

void UUECPAppBridge::ConfirmTool(const FString& Action)
{
	IUECPCoreModule::Get().GetArchitectService().ConfirmTool(Action);
}

void UUECPAppBridge::AnswerUserQuestions(const FString& AnswerJson)
{
	auto W = OwnerWidget.Pin();
	if (W.IsValid()) W->OnUserQuestionsAnswered(AnswerJson);
}

void UUECPAppBridge::AnalyzePlugin()
{
	auto W = OwnerWidget.Pin();
	if (W.IsValid()) W->OnAnalyzePluginClicked();
}

void UUECPAppBridge::AttachImage(const FString& View)
{
	auto W = OwnerWidget.Pin();
	if (!W.IsValid()) return;

	if (View == TEXT("architect"))      IUECPCoreModule::Get().GetArchitectService().AttachImage();
	else if (View == TEXT("analyst"))   IUECPCoreModule::Get().GetAnalystService().AttachImage();
	else if (View == TEXT("scanner"))   IUECPCoreModule::Get().GetScannerService().AttachImage();
}

void UUECPAppBridge::RemoveImage(const FString& View, int32 Index)
{
	auto W = OwnerWidget.Pin();
	if (!W.IsValid()) return;

	if (View == TEXT("architect") && W->ArchitectAttachedImages.IsValidIndex(Index))
	{
		W->ArchitectAttachedImages.RemoveAt(Index);
		PushImageAttachments(TEXT("architect"), BuildImagesJson(W->ArchitectAttachedImages));
	}
	else if (View == TEXT("analyst") && W->AnalystAttachedImages.IsValidIndex(Index))
	{
		W->AnalystAttachedImages.RemoveAt(Index);
		PushImageAttachments(TEXT("analyst"), BuildImagesJson(W->AnalystAttachedImages));
	}
	else if (View == TEXT("scanner") && W->ProjectAttachedImages.IsValidIndex(Index))
	{
		W->ProjectAttachedImages.RemoveAt(Index);
		PushImageAttachments(TEXT("scanner"), BuildImagesJson(W->ProjectAttachedImages));
	}
}

void UUECPAppBridge::ImportFileContext(const FString& View)
{
	auto W = OwnerWidget.Pin();
	if (!W.IsValid()) return;

	if (View == TEXT("architect"))
		IUECPCoreModule::Get().GetArchitectService().ImportFileContext();
}

void UUECPAppBridge::RemoveFileContext(const FString& View, int32 Index)
{
	auto W = OwnerWidget.Pin();
	if (!W.IsValid()) return;

	if (View == TEXT("architect") && W->ArchitectAttachedFiles.IsValidIndex(Index))
	{
		W->ArchitectAttachedFiles.RemoveAt(Index);
		PushFileContexts(TEXT("architect"), BuildFileContextsJson(W->ArchitectAttachedFiles));
	}
	else if (View == TEXT("analyst") && W->AnalystAttachedFiles.IsValidIndex(Index))
	{
		W->AnalystAttachedFiles.RemoveAt(Index);
		PushFileContexts(TEXT("analyst"), BuildFileContextsJson(W->AnalystAttachedFiles));
	}
}

void UUECPAppBridge::AddAssetContext()
{
	IUECPCoreModule::Get().GetAnalystService().AddAssetContext();
}

void UUECPAppBridge::AddNodeContext()
{
	IUECPCoreModule::Get().GetAnalystService().AddNodeContext();
}

void UUECPAppBridge::CopyToClipboard(const FString& Text)
{
	FPlatformApplicationMisc::ClipboardCopy(*Text);
}

void UUECPAppBridge::ExportConversation(const FString& View, const FString& Format)
{
	auto W = OwnerWidget.Pin();
	if (!W.IsValid()) return;

	FString ChatID, Title;
	TArray<TSharedPtr<FJsonValue>>* History = nullptr;

	if (View == TEXT("architect"))
	{
		ChatID = W->ActiveArchitectChatID;
		History = &W->ArchitectConversationHistory;
		auto* I = W->ArchitectConversationList.FindByPredicate(
			[&](const TSharedPtr<FConversationInfo>& Info) { return Info.IsValid() && Info->ID == ChatID; });
		if (I) Title = (*I)->Title;
	}
	else if (View == TEXT("analyst"))
	{
		ChatID = W->ActiveChatID;
		History = &W->AnalystConversationHistory;
		auto* I = W->ConversationList.FindByPredicate(
			[&](const TSharedPtr<FConversationInfo>& Info) { return Info.IsValid() && Info->ID == ChatID; });
		if (I) Title = (*I)->Title;
	}
	else if (View == TEXT("scanner"))
	{
		ChatID = W->ActiveProjectChatID;
		History = &W->ProjectConversationHistory;
		auto* I = W->ProjectConversationList.FindByPredicate(
			[&](const TSharedPtr<FConversationInfo>& Info) { return Info.IsValid() && Info->ID == ChatID; });
		if (I) Title = (*I)->Title;
	}

	if (!ChatID.IsEmpty() && History)
	{
		const FString Saved = FChatHistoryManager::ExportConversationToFile(ChatID, Title, Format, *History);
		if (!Saved.IsEmpty())
		{
			PushToast(FString::Printf(TEXT("Conversation saved to:\n%s"), *Saved), TEXT("success"));
		}
		else
		{
			PushToast(TEXT("Failed to save conversation."), TEXT("error"));
		}
	}
}

void UUECPAppBridge::SearchAssets(const FString& View, const FString& Query)
{
	FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AR = ARM.Get();

	struct FEntry { FTopLevelAssetPath Path; FString Label; };
	TArray<FEntry> Classes = {
		{ UBlueprint::StaticClass()->GetClassPathName(),        TEXT("Blueprint") },
		{ USkeletalMesh::StaticClass()->GetClassPathName(),     TEXT("SkeletalMesh") },
		{ UStaticMesh::StaticClass()->GetClassPathName(),       TEXT("StaticMesh") },
		{ FTopLevelAssetPath(TEXT("/Script/Engine"), TEXT("Material")),               TEXT("Material") },
		{ FTopLevelAssetPath(TEXT("/Script/Engine"), TEXT("MaterialInstanceConstant")), TEXT("MaterialInstance") },
		{ FTopLevelAssetPath(TEXT("/Script/Engine"), TEXT("DataTable")),              TEXT("DataTable") },
		{ FTopLevelAssetPath(TEXT("/Script/Engine"), TEXT("Texture2D")),              TEXT("Texture") },
		{ FTopLevelAssetPath(TEXT("/Script/Engine"), TEXT("SoundCue")),               TEXT("SoundCue") },
		{ FTopLevelAssetPath(TEXT("/Script/Niagara"), TEXT("NiagaraSystem")),         TEXT("NiagaraSystem") },
		{ FTopLevelAssetPath(TEXT("/Script/LevelSequence"), TEXT("LevelSequence")),   TEXT("LevelSequence") },
	};

	FString LowerQuery = Query.ToLower();
	TArray<TSharedPtr<FJsonObject>> Results;
	const TArray<FString> ContentMounts = UECPMountResolver::GetUserContentMounts();

	for (const FEntry& E : Classes)
	{
		TArray<FAssetData> Assets;
		AR.GetAssetsByClass(E.Path, Assets, true);
		for (const FAssetData& Asset : Assets)
		{
			if (!UECPMountResolver::IsPathUnderMount(Asset.PackagePath.ToString(), ContentMounts)) continue;
			FString Name = Asset.AssetName.ToString();
			if (!LowerQuery.IsEmpty() && !Name.ToLower().Contains(LowerQuery)) continue;

			TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
			Item->SetStringField(TEXT("name"), Name);
			Item->SetStringField(TEXT("path"), Asset.PackageName.ToString());
			Item->SetStringField(TEXT("class"), E.Label);
			Results.Add(Item);
			if (Results.Num() >= 20) break;
		}
		if (Results.Num() >= 20) break;
	}

	if (Results.Num() < 20)
	{
		FString SourceDir = FPaths::Combine(FPaths::ProjectDir(), TEXT("Source"));
		if (FPaths::DirectoryExists(SourceDir))
		{
			TArray<FString> SourceFiles;
			IFileManager::Get().FindFilesRecursive(SourceFiles, *SourceDir, TEXT("*.*"), true, false);
			for (const FString& FilePath : SourceFiles)
			{
				FString Ext = FPaths::GetExtension(FilePath).ToLower();
				if (Ext != TEXT("cpp") && Ext != TEXT("h") && Ext != TEXT("hpp")) continue;

				FString FileName = FPaths::GetCleanFilename(FilePath);
				if (!LowerQuery.IsEmpty() && !FileName.ToLower().Contains(LowerQuery)) continue;

				FString RelPath = FilePath;
				FPaths::MakePathRelativeTo(RelPath, *FPaths::ProjectDir());

				FString ClassLabel = Ext == TEXT("h") || Ext == TEXT("hpp") ? TEXT("Header") : TEXT("C++");

				TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
				Item->SetStringField(TEXT("name"), FileName);
				Item->SetStringField(TEXT("path"), RelPath);
				Item->SetStringField(TEXT("class"), ClassLabel);
				Results.Add(Item);
				if (Results.Num() >= 30) break;
			}
		}
	}

	FString JsonArray = TEXT("[");
	for (int32 i = 0; i < Results.Num(); i++)
	{
		if (i > 0) JsonArray += TEXT(",");
		FString ItemStr;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ItemStr);
		FJsonSerializer::Serialize(Results[i].ToSharedRef(), Writer);
		JsonArray += ItemStr;
	}
	JsonArray += TEXT("]");

	PushAssetSearchResults(View, JsonArray);
}

void UUECPAppBridge::SelectAssetRef(const FString& View, const FString& AssetPath, const FString& DisplayName, const FString& ClassLabel)
{
	static const TMap<FString, EAssetRefType> ClassMap = {
		{ TEXT("Blueprint"),       EAssetRefType::Blueprint       },
		{ TEXT("WidgetBlueprint"), EAssetRefType::WidgetBlueprint },
		{ TEXT("AnimBlueprint"),   EAssetRefType::AnimBlueprint   },
		{ TEXT("Material"),        EAssetRefType::Material        },
		{ TEXT("MaterialInstance"),EAssetRefType::MaterialInstance},
		{ TEXT("DataTable"),       EAssetRefType::DataTable       },
		{ TEXT("Texture"),         EAssetRefType::Texture         },
		{ TEXT("StaticMesh"),      EAssetRefType::StaticMesh      },
		{ TEXT("SkeletalMesh"),    EAssetRefType::SkeletalMesh    },
		{ TEXT("SoundCue"),        EAssetRefType::Sound           },
		{ TEXT("NiagaraSystem"),   EAssetRefType::NiagaraSystem   },
		{ TEXT("LevelSequence"),   EAssetRefType::LevelSequence   },
		{ TEXT("Header"),          EAssetRefType::CppHeader       },
		{ TEXT("C++"),             EAssetRefType::CppSource       },
	};
	const EAssetRefType* Found = ClassMap.Find(ClassLabel);
	EAssetRefType Type = Found ? *Found : EAssetRefType::Other;

	FAssetReferenceManager::Get().LinkAsset(AssetPath, DisplayName, Type);

	ExecJs(FString::Printf(TEXT("if(typeof onInsertAssetRef==='function')onInsertAssetRef(\"%s\",\"%s\",\"%s\")"),
		*View, *EscJs(DisplayName), *EscJs(AssetPath)));
}

void UUECPAppBridge::DismissAssetPicker(const FString& View)
{
}

void UUECPAppBridge::ToggleMic()
{
	IUECPCoreModule::Get().GetVoiceService().ToggleMic();
}

void UUECPAppBridge::PasteImageFromClipboard(const FString& View)
{
	auto W = OwnerWidget.Pin();
	if (!W.IsValid()) return;

	if (View == TEXT("architect"))
	{
		if (W->TryPasteImageFromClipboard(W->ArchitectAttachedImages))
			PushImageAttachments(TEXT("architect"), BuildImagesJson(W->ArchitectAttachedImages));
	}
	else if (View == TEXT("scanner"))
	{
		if (W->TryPasteImageFromClipboard(W->ProjectAttachedImages))
			PushImageAttachments(TEXT("scanner"), BuildImagesJson(W->ProjectAttachedImages));
	}
}

void UUECPAppBridge::AddImageFromData(const FString& View, const FString& Base64, const FString& Mime)
{
	auto W = OwnerWidget.Pin();
	if (!W.IsValid() || Base64.IsEmpty()) return;

	FAttachedImage Img;
	Img.Name = TEXT("pasted_image.png");
	Img.MimeType = Mime.IsEmpty() ? TEXT("image/png") : Mime;
	Img.Base64Data = Base64;

	if (View == TEXT("architect"))
	{
		W->ArchitectAttachedImages.Add(Img);
		PushImageAttachments(TEXT("architect"), BuildImagesJson(W->ArchitectAttachedImages));
	}
	else if (View == TEXT("scanner"))
	{
		W->ProjectAttachedImages.Add(Img);
		PushImageAttachments(TEXT("scanner"), BuildImagesJson(W->ProjectAttachedImages));
	}
}

void UUECPAppBridge::SaveCustomInstructions(const FString& Text)
{
	auto W = OwnerWidget.Pin();
	if (!W.IsValid()) return;
	W->CustomInstructions = Text;
	FSettingsManager::Get().SaveCustomInstructions(Text);
	ShowNotification(TEXT("Custom instructions saved"));
}

void UUECPAppBridge::LoadCustomInstructions()
{
	FString Instructions = FSettingsManager::Get().LoadCustomInstructions();
	ExecJs(FString::Printf(TEXT("if(typeof onCustomInstructions==='function')onCustomInstructions(\"%s\")"), *EscJs(Instructions)));
}

void UUECPAppBridge::BrowseSkeletalMesh()
{
	auto W = OwnerWidget.Pin();
	if (!W.IsValid()) return;

	FOpenAssetDialogConfig Config;
	Config.DialogTitleOverride = FText::FromString(TEXT("Select Skeletal Mesh"));
	Config.AssetClassNames.Add(USkeletalMesh::StaticClass()->GetClassPathName());
	Config.bAllowMultipleSelection = false;

	FContentBrowserModule& CBModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
	TArray<FAssetData> Selected = CBModule.Get().CreateModalOpenAssetDialog(Config);
	if (Selected.Num() == 0) return;

	FString AssetPath = Selected[0].GetObjectPathString();
	W->LoadedMeshPath = AssetPath;
	W->OnMeshSplitterLoadBones();

	ExecJs(FString::Printf(TEXT("if(typeof onMeshPath==='function')onMeshPath(\"%s\")"), *EscJs(W->LoadedMeshPath)));
	PushBoneListFromWidget(W.Get());
}

void UUECPAppBridge::LoadBones(const FString& MeshPath)
{
	auto W = OwnerWidget.Pin();
	if (!W.IsValid()) return;

	W->LoadedMeshPath = MeshPath;
	W->OnMeshSplitterLoadBones();

	ExecJs(FString::Printf(TEXT("if(typeof onMeshPath==='function')onMeshPath(\"%s\")"), *EscJs(W->LoadedMeshPath)));
	PushBoneListFromWidget(W.Get());
}

void UUECPAppBridge::PreviewSplitMesh(const FString& Json)
{
	auto W = OwnerWidget.Pin();
	if (!W.IsValid()) return;

	TSharedPtr<FJsonObject> Obj;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	if (FJsonSerializer::Deserialize(Reader, Obj) && Obj.IsValid())
	{
		W->SelectedBones.Empty();
		const TArray<TSharedPtr<FJsonValue>>* BonesArr = nullptr;
		if (Obj->TryGetArrayField(TEXT("selected_bones"), BonesArr))
			for (const auto& V : *BonesArr) W->SelectedBones.Add(V->AsString());
	}

	TArray<FString> BoneNames = W->SelectedBones.Array();
	FString OutJson, OutError;
	AssetPropertyTools::HandlePreviewSplitSkeletalMesh(W->LoadedMeshPath, BoneNames, 0.5f, OutJson, OutError);

	FString Result = OutError.IsEmpty() ? OutJson
		: FString::Printf(TEXT("{\"error\":\"%s\"}"), *EscJs(OutError));
	ExecJs(FString::Printf(TEXT("if(typeof onSplitPreview==='function')onSplitPreview(%s)"), *Result));
}

void UUECPAppBridge::ExecuteSplitMesh(const FString& Json)
{
	auto W = OwnerWidget.Pin();
	if (!W.IsValid()) return;

	FString OutputPath, NamesStr;
	TSharedPtr<FJsonObject> Obj;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	if (FJsonSerializer::Deserialize(Reader, Obj) && Obj.IsValid())
	{
		W->SelectedBones.Empty();
		const TArray<TSharedPtr<FJsonValue>>* BonesArr = nullptr;
		if (Obj->TryGetArrayField(TEXT("selected_bones"), BonesArr))
			for (const auto& V : *BonesArr) W->SelectedBones.Add(V->AsString());

		Obj->TryGetStringField(TEXT("output_path"), OutputPath);
		Obj->TryGetStringField(TEXT("names"),       NamesStr);
	}

	TArray<FString> BoneNames = W->SelectedBones.Array();
	TArray<FString> PartNames;
	if (!NamesStr.IsEmpty()) { NamesStr.ParseIntoArray(PartNames, TEXT(",")); for (FString& S : PartNames) S.TrimStartAndEndInline(); }

	FString OutJson, OutError;
	AssetPropertyTools::HandleSplitSkeletalMesh(W->LoadedMeshPath, BoneNames, OutputPath, 0.5f, PartNames, OutJson, OutError);

	FString Result = OutError.IsEmpty() ? OutJson
		: FString::Printf(TEXT("{\"error\":\"%s\"}"), *EscJs(OutError));
	ExecJs(FString::Printf(TEXT("if(typeof onSplitResult==='function')onSplitResult(%s)"), *Result));
}

void UUECPAppBridge::RunProfiler(int32 DurationSeconds)
{
	auto W = OwnerWidget.Pin();
	if (!W.IsValid()) return;
	if (W->bIsProfiling) return;

	W->bIsProfiling = true;
	ExecJs(TEXT("if(typeof onProfilerStatus==='function')onProfilerStatus('running')"));

	float Duration = DurationSeconds > 0 ? static_cast<float>(DurationSeconds) : 5.0f;
	TWeakObjectPtr<UUECPAppBridge> WeakSelf(this);
	TWeakPtr<SUECPMainWidget> WeakWidget = W;

	Async(EAsyncExecution::Thread, [WeakSelf, WeakWidget, Duration]()
	{
		FString Json, Err;
		ProfilerTools::HandleProfileProject(Duration, TEXT("cpu,gpu,frame,bookmark,counters"), nullptr, Json, Err);

		AsyncTask(ENamedThreads::GameThread, [WeakSelf, WeakWidget, Json, Err]()
		{
			if (auto WidgetPin = WeakWidget.Pin()) { WidgetPin->bIsProfiling = false; if (!Json.IsEmpty()) WidgetPin->LastProfileResultJson = Json; }
			UUECPAppBridge* Self = WeakSelf.Get();
			if (!Self) return;
			if (Err.IsEmpty())
				Self->ExecJs(FString::Printf(TEXT("if(typeof onProfilerResult==='function')onProfilerResult(%s)"), *Json));
			else
				Self->ExecJs(FString::Printf(TEXT("if(typeof onProfilerStatus==='function')onProfilerStatus('error: %s')"), *EscJs(Err)));
		});
	});
}

void UUECPAppBridge::BrowseModelSourceImage()
{
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (!DesktopPlatform) return;

	TArray<FString> Files;
	DesktopPlatform->OpenFileDialog(nullptr, TEXT("Select Source Image"),
		TEXT(""), TEXT(""), TEXT("Image Files (*.png;*.jpg)|*.png;*.jpg"), 0, Files);

	if (Files.Num() == 0) return;
	TArray<uint8> FileData;
	if (!FFileHelper::LoadFileToArray(FileData, *Files[0])) return;

	FString Base64 = FBase64::Encode(FileData);
	FString MimeType = Files[0].EndsWith(TEXT(".png")) ? TEXT("image/png") : TEXT("image/jpeg");
	FString Name = FPaths::GetCleanFilename(Files[0]);

	auto W = OwnerWidget.Pin();
	if (W.IsValid())
	{
		W->SourceImageAttachment.Base64Data = Base64;
		W->SourceImageAttachment.MimeType = MimeType;
		W->SourceImageAttachment.Name = Name;
	}

	ExecJs(FString::Printf(TEXT("if(typeof onModelSourceImage==='function')onModelSourceImage(\"%s\",\"%s\",\"%s\")"),
		*EscJs(Name), *MimeType, *Base64));
}

static FString GetMimeFromExt(const FString& Path)
{
	if (Path.EndsWith(TEXT(".png"))) return TEXT("image/png");
	if (Path.EndsWith(TEXT(".gif"))) return TEXT("image/gif");
	if (Path.EndsWith(TEXT(".webp"))) return TEXT("image/webp");
	return TEXT("image/jpeg");
}

void UUECPAppBridge::RequestImageUpload()
{
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (!DesktopPlatform) return;

	TArray<FString> Files;
	DesktopPlatform->OpenFileDialog(nullptr, TEXT("Select Image"),
		TEXT(""), TEXT(""),
		TEXT("Image Files (*.png;*.jpg;*.jpeg;*.gif;*.webp)|*.png;*.jpg;*.jpeg;*.gif;*.webp"),
		0, Files);

	if (Files.Num() == 0) return;

	TArray<uint8> FileData;
	if (!FFileHelper::LoadFileToArray(FileData, *Files[0])) return;

	if (FileData.Num() > 5 * 1024 * 1024)
	{
		ExecJs(TEXT("var iframe=document.querySelector('iframe');if(iframe)iframe.contentWindow.postMessage({type:'imageError',error:'Image exceeds 5MB limit'},'*')"));
		return;
	}

	FString Base64 = FBase64::Encode(FileData);
	FString MimeType = GetMimeFromExt(Files[0]);
	FString FileName = FPaths::GetCleanFilename(Files[0]);

	FString JS = FString::Printf(
		TEXT("(function(){var d={type:'imageReady',base64:'%s',filename:'%s',mimetype:'%s'};"
			 "var iframes=document.querySelectorAll('iframe');"
			 "for(var i=0;i<iframes.length;i++){try{iframes[i].contentWindow.postMessage(d,'*')}catch(e){}}"
			 "})()"),
		*Base64, *EscJs(FileName), *MimeType);
	ExecJs(JS);
}

void UUECPAppBridge::RequestClipboardImage()
{
	auto W = OwnerWidget.Pin();
	if (!W.IsValid()) return;

	TArray<FAttachedImage> TempImages;
	if (W->TryPasteImageFromClipboard(TempImages) && TempImages.Num() > 0)
	{
		const FAttachedImage& Img = TempImages[0];
		FString JS = FString::Printf(
			TEXT("(function(){var d={type:'imageReady',base64:'%s',filename:'%s',mimetype:'%s'};"
				 "var iframes=document.querySelectorAll('iframe');"
				 "for(var i=0;i<iframes.length;i++){try{iframes[i].contentWindow.postMessage(d,'*')}catch(e){}}"
				 "})()"),
			*Img.Base64Data, *EscJs(Img.Name), *Img.MimeType);
		ExecJs(JS);
	}
}

void UUECPAppBridge::ReviewChanges()
{
	ReviewChangesForBP(TEXT(""));
}

void UUECPAppBridge::ReviewChangesBP(const FString& BpPath)
{
	ReviewChangesForBP(BpPath);
}

void UUECPAppBridge::ReviewChangesForBP(const FString& BpPath)
{
	auto W = OwnerWidget.Pin();
	if (!W.IsValid() || W->DiffSnapshots.Num() == 0)
	{
		ExecJs(TEXT("showToast('Diff snapshots expired — only available during the session where changes were made.','info')"));
		return;
	}

	FRevisionInfo OldRev, NewRev;
	OldRev.Revision = TEXT("Before AI");
	NewRev.Revision = TEXT("After AI");

	if (!BpPath.IsEmpty())
	{
		TObjectPtr<UBlueprint>* SnapshotPtr = W->DiffSnapshots.Find(BpPath);
		if (!SnapshotPtr) return;
		UBlueprint* CurrentBP = LoadObject<UBlueprint>(nullptr, *BpPath);
		if (!CurrentBP || !*SnapshotPtr) return;

		SBlueprintDiff::CreateDiffWindow(
			FText::FromString(FString::Printf(TEXT("AI Changes: %s"), *FPaths::GetBaseFilename(BpPath))),
			*SnapshotPtr, CurrentBP, OldRev, NewRev);
		return;
	}

	for (auto& Pair : W->DiffSnapshots)
	{
		UBlueprint* CurrentBP = LoadObject<UBlueprint>(nullptr, *Pair.Key);
		UBlueprint* SnapshotBP = Pair.Value;
		if (!CurrentBP || !SnapshotBP) continue;

		SBlueprintDiff::CreateDiffWindow(
			FText::FromString(FString::Printf(TEXT("AI Changes: %s"), *FPaths::GetBaseFilename(Pair.Key))),
			SnapshotBP, CurrentBP, OldRev, NewRev);
	}
}

void UUECPAppBridge::OpenAssetLink(const FString& AssetRef)
{
	FString Decoded = FGenericPlatformHttp::UrlDecode(AssetRef);

	while (Decoded.EndsWith(TEXT("/")))
		Decoded = Decoded.LeftChop(1);

	IAssetRegistry& AR = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
	TArray<FAssetData> Found;

	if (Decoded.StartsWith(TEXT("/Game/")))
		AR.GetAssetsByPackageName(FName(*Decoded), Found);

	if (Found.IsEmpty())
	{
		auto W = OwnerWidget.Pin();
		if (W.IsValid())
		{
			FString Path = W->FindAssetPathByName(Decoded.StartsWith(TEXT("/Game/")) ? FPaths::GetBaseFilename(Decoded) : Decoded);
			if (!Path.IsEmpty())
			{
				AR.GetAssetsByPackageName(FName(*Path), Found);
			}
		}
	}

	if (!Found.IsEmpty())
	{
		FContentBrowserModule& CB = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
		CB.Get().SyncBrowserToAssets(Found, true);
	}
}

void UUECPAppBridge::SaveDiagramPng(const FString& Base64Png, const FString& Kind)
{
	FString Payload = Base64Png;
	const int32 CommaIdx = Payload.Find(TEXT(","));
	if (Payload.StartsWith(TEXT("data:")) && CommaIdx != INDEX_NONE)
	{
		Payload = Payload.Mid(CommaIdx + 1);
	}

	TArray<uint8> Bytes;
	if (!FBase64::Decode(Payload, Bytes) || Bytes.Num() == 0)
	{
		FNotificationInfo Info(FText::FromString(TEXT("Diagram export failed: invalid PNG data")));
		Info.ExpireDuration = 4.0f;
		FSlateNotificationManager::Get().AddNotification(Info);
		return;
	}

	const FString SafeKind = (Kind.IsEmpty() ? TEXT("diagram") : Kind).Replace(TEXT(" "), TEXT("_"));
	const FString Timestamp = FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S"));
	const FString OutDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("BpGenDiagrams"));
	const FString FileName = FString::Printf(TEXT("%s_%s.png"), *SafeKind, *Timestamp);
	const FString FullPath = OutDir / FileName;

	IPlatformFile::GetPlatformPhysical().CreateDirectoryTree(*OutDir);

	if (!FFileHelper::SaveArrayToFile(Bytes, *FullPath))
	{
		FNotificationInfo Info(FText::FromString(FString::Printf(TEXT("Diagram export failed: cannot write %s"), *FullPath)));
		Info.ExpireDuration = 4.0f;
		FSlateNotificationManager::Get().AddNotification(Info);
		return;
	}

	FNotificationInfo Info(FText::FromString(FString::Printf(TEXT("Diagram exported: %s"), *FileName)));
	Info.ExpireDuration = 6.0f;
	Info.bUseLargeFont = false;
	Info.Hyperlink = FSimpleDelegate::CreateLambda([OutDir]()
	{
		FPlatformProcess::ExploreFolder(*OutDir);
	});
	Info.HyperlinkText = FText::FromString(TEXT("Open Folder"));
	FSlateNotificationManager::Get().AddNotification(Info);
}

void UUECPAppBridge::OpenDiagramWindow(const FString& SvgHtml)
{
	if (SvgHtml.IsEmpty()) return;

	TSharedPtr<SWindow> DiagramWindow = DiagramUtils::CreateDiagramWindow(SvgHtml);
	if (!DiagramWindow.IsValid()) return;

	TSharedPtr<SWindow> ParentWindow = FSlateApplication::Get().GetActiveTopLevelWindow();
	if (ParentWindow.IsValid())
	{
		FSlateApplication::Get().AddWindowAsNativeChild(DiagramWindow.ToSharedRef(), ParentWindow.ToSharedRef());
	}
	else
	{
		FSlateApplication::Get().AddWindow(DiagramWindow.ToSharedRef());
	}
}

void UUECPAppBridge::GetClipboardText()
{
	FString ClipText;
	FPlatformApplicationMisc::ClipboardPaste(ClipText);
	ClipText.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
	ClipText.ReplaceInline(TEXT("'"), TEXT("\\'"));
	ClipText.ReplaceInline(TEXT("\r\n"), TEXT("\\n"));
	ClipText.ReplaceInline(TEXT("\n"), TEXT("\\n"));
	ClipText.ReplaceInline(TEXT("\r"), TEXT("\\n"));
	ExecJs(FString::Printf(TEXT("onClipboardText('%s')"), *ClipText));
}

void UUECPAppBridge::ShowNotification(const FString& Message)
{
	FNotificationInfo Info(FText::FromString(Message));
	Info.ExpireDuration = 3.0f;
	FSlateNotificationManager::Get().AddNotification(Info);
}

void UUECPAppBridge::PushChatList(const FString& View, const FString& JsonArray)
{
	ExecJs(FString::Printf(TEXT("if(typeof onChatList==='function')onChatList(\"%s\",%s)"), *View, *JsonArray));
}

void UUECPAppBridge::PushChatHistory(const FString& View, const FString& JsonArray)
{
	ExecJs(FString::Printf(TEXT("if(typeof onChatHistory==='function')onChatHistory(\"%s\",%s)"), *View, *JsonArray));
}

void UUECPAppBridge::PushAppendMessage(const FString& View, const FString& Html)
{
	ExecJs(FString::Printf(TEXT("if(typeof onAppendMessage==='function')onAppendMessage(\"%s\",\"%s\")"), *View, *EscJs(Html)));
}

void UUECPAppBridge::PushUpdateLastMessage(const FString& View, const FString& Html)
{
	ExecJs(FString::Printf(TEXT("if(typeof onUpdateLastMessage==='function')onUpdateLastMessage(\"%s\",\"%s\")"), *View, *EscJs(Html)));
}

void UUECPAppBridge::PushTokenCount(const FString& View, int32 Count)
{
	ExecJs(FString::Printf(TEXT("if(typeof onTokenCount==='function')onTokenCount(\"%s\",%d)"), *View, Count));
}

void UUECPAppBridge::PushThinkingState(const FString& View, bool bShow, const FString& Label, const FString& Content)
{
	ExecJs(FString::Printf(TEXT("if(typeof onThinking==='function')onThinking(\"%s\",%s,\"%s\",\"%s\")"),
		*View, bShow ? TEXT("true") : TEXT("false"), *EscJs(Label), *EscJs(Content)));
}

void UUECPAppBridge::PushLoadingState(const FString& View, bool bShow, const FString& Message)
{
	ExecJs(FString::Printf(TEXT("if(typeof onLoading==='function')onLoading(\"%s\",%s,\"%s\")"),
		*View, bShow ? TEXT("true") : TEXT("false"), *EscJs(Message)));
}

void UUECPAppBridge::PushToolConfirmation(const FString& ToolName, const FString& ArgsPreview)
{
	ExecJs(FString::Printf(TEXT("if(typeof onToolConfirm==='function')onToolConfirm(\"%s\",\"%s\")"),
		*EscJs(ToolName), *EscJs(ArgsPreview)));
}

void UUECPAppBridge::PushUserQuestions(const FString& QuestionsJson)
{
	ExecJs(FString::Printf(
		TEXT("if(typeof onUserQuestions==='function')onUserQuestions(%s)"),
		*QuestionsJson));
}

void UUECPAppBridge::PushACPSlashCommands(const FString& AgentId, const FString& JsonArray)
{
	PushSlashContext();
	ExecJs(FString::Printf(
		TEXT("if(typeof onACPSlashCommands==='function')onACPSlashCommands(\"%s\",%s)"),
		*EscJs(AgentId), *JsonArray));
}

void UUECPAppBridge::PushAssetSearchResults(const FString& View, const FString& JsonArray)
{
	ExecJs(FString::Printf(TEXT("if(typeof onAssetResults==='function')onAssetResults(\"%s\",%s)"), *View, *JsonArray));
}

void UUECPAppBridge::PushImageAttachments(const FString& View, const FString& JsonArray)
{
	ExecJs(FString::Printf(TEXT("if(typeof onImageAttachments==='function')onImageAttachments(\"%s\",%s)"), *View, *JsonArray));
}

void UUECPAppBridge::PushFileContexts(const FString& View, const FString& JsonArray)
{
	ExecJs(FString::Printf(TEXT("if(typeof onFileContexts==='function')onFileContexts(\"%s\",%s)"), *View, *JsonArray));
}

void UUECPAppBridge::PushScanStatus(const FString& StatusText)
{
	ExecJs(FString::Printf(TEXT("if(typeof onScanStatus==='function')onScanStatus(\"%s\")"), *EscJs(StatusText)));
}

void UUECPAppBridge::PushScanState(bool bScanning)
{
	ExecJs(FString::Printf(TEXT("if(typeof onScanState==='function')onScanState(%s)"),
		bScanning ? TEXT("true") : TEXT("false")));
}

void UUECPAppBridge::PushToast(const FString& Message, const FString& Type)
{
	ExecJs(FString::Printf(TEXT("if(typeof showToast==='function')showToast(\"%s\",\"%s\")"),
		*EscJs(Message), *EscJs(Type)));
}

void UUECPAppBridge::PushMeshProgress(const FString& Id, const FString& Name, const FString& Phase, int32 Progress, const FString& ErrorMsg)
{
	ExecJs(FString::Printf(
		TEXT("if(typeof updateMeshProgress==='function')updateMeshProgress(\"%s\",\"%s\",\"%s\",%d,\"%s\")"),
		*EscJs(Id), *EscJs(Name), *EscJs(Phase), Progress, *EscJs(ErrorMsg)));
}

void UUECPAppBridge::PushBanner(const FString& Message)
{
	ExecJs(FString::Printf(TEXT("if(typeof showBanner==='function')showBanner(\"%s\")"),
		*EscJs(Message)));
}

void UUECPAppBridge::PushViewState(const FString& Json)
{
	ExecJs(FString::Printf(TEXT("if(typeof onViewState==='function')onViewState(%s)"), *Json));
}

void UUECPAppBridge::PushSlashContext()
{
	auto W = OwnerWidget.Pin();
	if (!W.IsValid()) return;

	FAgentProviderConfig* Cfg = W->GetActiveAgentProvider();

	bool bIsAgent = (Cfg != nullptr);
	FString ProviderKey = TEXT("api");
	bool bFast = false, bEffort = false, bLogin = false;

	if (Cfg)
	{
		const FString& Name = Cfg->ProviderName;
		if      (Name.Contains(TEXT("Claude")))  { ProviderKey = TEXT("claude");  bFast = true; bEffort = true; bLogin = true; }
		else if (Name.Contains(TEXT("Codex")))   { ProviderKey = TEXT("codex");   bFast = true; bEffort = true; bLogin = true; }
		else if (Name.Contains(TEXT("Copilot"))) { ProviderKey = TEXT("copilot"); bLogin = true; }
		else if (Name.Contains(TEXT("Gemini")))  { ProviderKey = TEXT("gemini");  bLogin = true; }
		else                                     { ProviderKey = TEXT("agent");   bLogin = true; }
	}

	const FString AgentIdKey = Cfg
		? (Cfg->AgentId.IsEmpty() ? Cfg->ProviderName : Cfg->AgentId)
		: FString();

	const FString Json = FString::Printf(
		TEXT("{\"mode\":\"%s\",\"provider\":\"%s\",\"agentId\":\"%s\",\"fast\":%s,\"effort\":%s,\"login\":%s}"),
		bIsAgent ? TEXT("agent") : TEXT("api"),
		*ProviderKey,
		*EscJs(AgentIdKey),
		bFast   ? TEXT("true") : TEXT("false"),
		bEffort ? TEXT("true") : TEXT("false"),
		bLogin  ? TEXT("true") : TEXT("false"));

	ExecJs(FString::Printf(TEXT("if(typeof onSlashContext==='function')onSlashContext(%s)"), *Json));
}

FString UUECPAppBridge::BuildAppHtmlDataUri()
{
	FString Html;
	TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("BpGeneratorUltimate"));
	if (Plugin.IsValid())
	{
		FString HtmlPath = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Resources"), TEXT("UI"), TEXT("app_shell.html"));
		if (!FFileHelper::LoadFileToString(Html, *HtmlPath))
		{
			Html = TEXT("<html><body style='background:#1c1b1a;color:#e06060;font-family:sans-serif;padding:40px'><h2>app_shell.html not found</h2><p>Expected: ") + HtmlPath + TEXT("</p></body></html>");
		}

		auto InjectLib = [&](const FString& Placeholder, const FString& FileName)
		{
			FString LibPath = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Resources"), TEXT("UI"), FileName);
			FString LibContent;
			if (FFileHelper::LoadFileToString(LibContent, *LibPath))
				Html.ReplaceInline(*Placeholder, *LibContent);
		};
		InjectLib(TEXT("%%MARKED_JS%%"), TEXT("marked.min.js"));
		InjectLib(TEXT("%%GRAPHRE_JS%%"), TEXT("graphre.min.js"));
		InjectLib(TEXT("%%NOMNOML_JS%%"), TEXT("nomnoml.min.js"));
		InjectLib(TEXT("%%VISNETWORK_JS%%"), TEXT("vis-network.min.js"));
	}
	else
	{
		Html = TEXT("<html><body style='background:#1c1b1a;color:#e06060;padding:40px'>Plugin not found</body></html>");
	}

	FString TempDir = FPaths::ProjectSavedDir() / TEXT("BpGeneratorUltimate");
	IPlatformFile& PF = FPlatformFileManager::Get().GetPlatformFile();
	PF.CreateDirectoryTree(*TempDir);
	FString TempPath = TempDir / TEXT("app_shell_live.html");
	FFileHelper::SaveStringToFile(Html, *TempPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	FString AbsPath = IFileManager::Get().ConvertToAbsolutePathForExternalAppForRead(*TempPath);
	AbsPath.ReplaceInline(TEXT("\\"), TEXT("/"));
	return FString::Printf(TEXT("file:///%s"), *AbsPath);
}

void UUECPAppBridge::RetryLastMessage()
{
	auto W = OwnerWidget.Pin();
	if (!W.IsValid()) return;

	auto& History = W->ArchitectConversationHistory;

	while (History.Num() > 0)
	{
		TSharedPtr<FJsonObject> Last = History.Last()->AsObject();
		if (!Last.IsValid()) break;
		FString Role;
		Last->TryGetStringField(TEXT("role"), Role);
		if (Role != TEXT("model")) break;
		bool bIsError = false;
		Last->TryGetBoolField(TEXT("is_error"), bIsError);
		if (!bIsError) break;
		History.RemoveAt(History.Num() - 1);
	}

	FString LastUserText;
	for (int32 i = History.Num() - 1; i >= 0; --i)
	{
		TSharedPtr<FJsonObject> Msg = History[i]->AsObject();
		if (!Msg.IsValid()) continue;
		FString Role;
		Msg->TryGetStringField(TEXT("role"), Role);
		if (Role == TEXT("user"))
		{
			const TArray<TSharedPtr<FJsonValue>>* Parts;
			if (Msg->TryGetArrayField(TEXT("parts"), Parts) && Parts->Num() > 0)
			{
				TSharedPtr<FJsonObject> FirstPart = (*Parts)[0]->AsObject();
				if (FirstPart.IsValid())
					FirstPart->TryGetStringField(TEXT("text"), LastUserText);
			}
			break;
		}
	}

	if (LastUserText.IsEmpty()) return;

	for (int32 i = History.Num() - 1; i >= 0; --i)
	{
		TSharedPtr<FJsonObject> Msg = History[i]->AsObject();
		if (!Msg.IsValid()) continue;
		FString Role;
		Msg->TryGetStringField(TEXT("role"), Role);
		if (Role == TEXT("user"))
		{
			History.RemoveAt(i);
			break;
		}
	}

	IUECPCoreModule::Get().GetArchitectService().ClearNativeHistoryForChat(W->ActiveArchitectChatID);

	W->ArchitectPendingBridgeText = LastUserText;
	W->OnSendArchitectMessageClicked();
}

void UUECPAppBridge::DeleteMessage(const FString& View, int32 MsgIndex)
{
	auto W = OwnerWidget.Pin();
	if (!W.IsValid()) return;
	W->DeleteMessageFromHistory(View, MsgIndex);
}

void UUECPAppBridge::CompactConversation(const FString& View)
{
	auto W = OwnerWidget.Pin();
	if (!W.IsValid()) return;

	if (View == TEXT("architect"))
	{
		if (W->ArchitectConversationHistory.Num() < 2)
		{
			PushToast(TEXT("Nothing to compact yet"), TEXT("info"));
			return;
		}
		const bool bChatBusy =
			W->ArchitectThinkingChats.Contains(W->ActiveArchitectChatID)
			|| W->PendingArchitectRequests.Contains(W->ActiveArchitectChatID);
		if (bChatBusy)
		{
			PushToast(TEXT("Wait for the current turn to finish before compacting"), TEXT("info"));
			return;
		}
		W->CompactPendingChatID = W->ActiveArchitectChatID;
		W->ArchitectPendingBridgeText = TEXT(
			"[COMPACT REQUEST] Please analyze our entire conversation and produce a comprehensive context summary. "
			"Include: what has been built, key decisions made, current project state, any issues encountered and "
			"their solutions, and what was actively being worked on. Be thorough — this summary will replace the "
			"full conversation history as the starting context for continuing our work. "
			"IMPORTANT: Respond with plain prose text ONLY. Do NOT call any tools, do NOT output JSON, do NOT use the tool_calls format.");
		W->OnSendArchitectMessageClicked();
	}
	else
	{
		PushToast(TEXT("Compaction is available in the Architect view"), TEXT("info"));
	}
}

void UUECPAppBridge::ToggleFastMode(const FString& View)
{
	auto W = OwnerWidget.Pin();
	if (!W.IsValid()) return;

	FAgentProviderConfig* Cfg = W->GetActiveAgentProvider();
	if (!Cfg)
	{
		PushToast(TEXT("Fast mode applies to agent providers — switch to Claude or Codex agent"), TEXT("info"));
		return;
	}

	const FString& Name = Cfg->ProviderName;
	const bool bSupported = Name.Contains(TEXT("Claude")) || Name.Contains(TEXT("Codex"));
	if (!bSupported)
	{
		PushToast(FString::Printf(TEXT("Fast mode not supported for %s"), *Name), TEXT("info"));
		return;
	}

	if (Cfg->ThinkingEffort == TEXT("low"))
	{
		Cfg->ThinkingEffort = W->PreFastModeEffort.IsEmpty() ? TEXT("medium") : W->PreFastModeEffort;
		W->PreFastModeEffort.Empty();
		PushToast(FString::Printf(TEXT("Fast mode off — effort: %s"), *Cfg->ThinkingEffort), TEXT("info"));
	}
	else
	{
		W->PreFastModeEffort = Cfg->ThinkingEffort;
		Cfg->ThinkingEffort = TEXT("low");
		PushToast(TEXT("Fast mode on — thinking effort: low"), TEXT("success"));
	}
}

void UUECPAppBridge::SwitchModel(const FString& View, const FString& ModelName)
{
	auto W = OwnerWidget.Pin();
	if (!W.IsValid() || ModelName.IsEmpty()) return;

	FAgentProviderConfig* Cfg = W->GetActiveAgentProvider();
	if (Cfg)
	{
		Cfg->ModelName = ModelName;
		PushToast(FString::Printf(TEXT("Model → %s"), *ModelName), TEXT("success"));
	}
	else
	{
		PushToast(TEXT("Model switching via /model applies to agent mode — use Settings for API mode"), TEXT("info"));
	}
}

void UUECPAppBridge::SetThinkingEffort(const FString& View, const FString& Effort)
{
	auto W = OwnerWidget.Pin();
	if (!W.IsValid() || Effort.IsEmpty()) return;

	static const TArray<FString> ValidEfforts = { TEXT("low"), TEXT("medium"), TEXT("high"), TEXT("max"), TEXT("adaptive") };
	if (!ValidEfforts.Contains(Effort.ToLower()))
	{
		PushToast(TEXT("Effort must be: low, medium, high, max, or adaptive"), TEXT("error"));
		return;
	}

	FAgentProviderConfig* Cfg = W->GetActiveAgentProvider();
	if (Cfg)
	{
		Cfg->ThinkingEffort = Effort.ToLower();
		W->PreFastModeEffort.Empty();
		PushToast(FString::Printf(TEXT("Thinking effort → %s"), *Effort.ToLower()), TEXT("success"));
	}
	else
	{
		PushToast(TEXT("/effort applies to agent mode — use Settings for API mode"), TEXT("info"));
	}
}

void UUECPAppBridge::LoginAgent(const FString& AgentName)
{
	auto W = OwnerWidget.Pin();
	if (!W.IsValid()) return;

	const FString Agent = AgentName.IsEmpty() ? TEXT("claude") : AgentName.ToLower();

	FString CliBinary = Agent;
	if (Agent == TEXT("copilot")) CliBinary = TEXT("gh");

	FString ExePath = W->FindCliFullPath(CliBinary);
	if (ExePath.IsEmpty()) ExePath = CliBinary;

	FString Args;
	if (Agent == TEXT("claude") || Agent == TEXT("codex") || Agent == TEXT("copilot"))
		Args = TEXT("auth login");

#if PLATFORM_WINDOWS
	Args    = FString::Printf(TEXT("/c %s %s"), *ExePath, *Args);
	ExePath = TEXT("cmd.exe");
#endif

	PushToast(FString::Printf(TEXT("Starting %s authentication…"), *Agent), TEXT("info"));

	TWeakObjectPtr<UUECPAppBridge> WeakSelf(this);
	FString ExeCapture  = ExePath;
	FString ArgsCapture = Args;
	FString AgentCapture = Agent;

	Async(EAsyncExecution::Thread, [WeakSelf, ExeCapture, ArgsCapture, AgentCapture]()
	{
		void* PipeRead  = nullptr;
		void* PipeWrite = nullptr;
		FPlatformProcess::CreatePipe(PipeRead, PipeWrite);

		FProcHandle Proc = FPlatformProcess::CreateProc(
			*ExeCapture, *ArgsCapture, false, true, true, nullptr, 0, nullptr, PipeWrite, nullptr);

		FPlatformProcess::ClosePipe(nullptr, PipeWrite);
		PipeWrite = nullptr;

		if (!Proc.IsValid())
		{
			FPlatformProcess::ClosePipe(PipeRead, nullptr);
			AsyncTask(ENamedThreads::GameThread, [WeakSelf, AgentCapture]()
			{
				auto Self = WeakSelf.Get();
				if (Self) Self->PushToast(FString::Printf(TEXT("Could not launch %s CLI — is it installed?"), *AgentCapture), TEXT("error"));
			});
			return;
		}

		bool bUrlOpened = false;
		FString Buffer;
		const double Deadline = FPlatformTime::Seconds() + 120.0;

		while (FPlatformProcess::IsProcRunning(Proc) && FPlatformTime::Seconds() < Deadline)
		{
			FString Chunk = FPlatformProcess::ReadPipe(PipeRead);
			if (Chunk.IsEmpty()) { FPlatformProcess::Sleep(0.1f); continue; }
			Buffer += Chunk;

			if (!bUrlOpened)
			{
				int32 HttpIdx = Buffer.Find(TEXT("https://"));
				if (HttpIdx >= 0)
				{
					FString Url = Buffer.Mid(HttpIdx);
					int32 End = Url.Len();
					for (int32 i = 0; i < Url.Len(); i++)
					{
						if (FChar::IsWhitespace(Url[i])) { End = i; break; }
					}
					Url = Url.Left(End);
					if (Url.Len() > 12)
					{
						bUrlOpened = true;
						AsyncTask(ENamedThreads::GameThread, [WeakSelf, AgentCapture]()
						{
							auto Self = WeakSelf.Get();
							if (Self) Self->PushToast(
								FString::Printf(TEXT("%s: complete sign-in in the browser, then return here"), *AgentCapture),
								TEXT("info"));
						});
					}
				}
			}
		}

		FPlatformProcess::ClosePipe(PipeRead, nullptr);

		int32 ExitCode = -1;
		FPlatformProcess::GetProcReturnCode(Proc, &ExitCode);
		FPlatformProcess::CloseProc(Proc);

		AsyncTask(ENamedThreads::GameThread, [WeakSelf, AgentCapture, ExitCode]()
		{
			auto Self = WeakSelf.Get();
			if (!Self) return;
			auto W2 = Self->OwnerWidget.Pin();

			if (ExitCode == 0)
			{
				Self->PushToast(FString::Printf(TEXT("%s authentication successful!"), *AgentCapture), TEXT("success"));
			}
			else
			{
				Self->PushToast(FString::Printf(TEXT("%s auth ended (code %d) — check terminal if login is incomplete"), *AgentCapture, ExitCode), TEXT("info"));
			}

			if (W2.IsValid())
			{
				TWeakPtr<SUECPMainWidget> WeakW2 = W2;
				FTSTicker::GetCoreTicker().AddTicker(
					FTickerDelegate::CreateLambda([WeakW2](float) -> bool
					{
						auto Pinned = WeakW2.Pin();
						if (Pinned.IsValid()) Pinned->RefreshCliStatusAsync();
						return false;
					}), 2.0f);
			}
		});
	});
}

void UUECPAppBridge::ClearActivePlan()
{
	auto W = OwnerWidget.Pin();
	if (!W.IsValid()) return;
	FPlanManager::Get().ClearPlan(W->ActiveArchitectChatID);
	ExecJs(TEXT("if(typeof onPlanUpdate==='function')onPlanUpdate('');"));
}

void UUECPAppBridge::DeletePlan(const FString& ConvID)
{
	if (ConvID.IsEmpty()) return;
	FPlanManager::Get().ClearPlan(ConvID);
	LoadPlanPicker();
}

void UUECPAppBridge::LoadPlanPicker()
{
	auto W = OwnerWidget.Pin();
	if (!W.IsValid()) return;

	TArray<FPlanManager::FPlanSummary> Summaries = FPlanManager::Get().GetAllPlanSummaries();

	const FString ActiveID = W->ActiveArchitectChatID;

	TArray<TSharedPtr<FJsonValue>> PlansArr;
	for (const FPlanManager::FPlanSummary& S : Summaries)
	{
		TSharedPtr<FJsonObject> P = MakeShareable(new FJsonObject);
		P->SetStringField(TEXT("conv_id"), S.ConvID);
		P->SetStringField(TEXT("title"), S.Title);
		P->SetStringField(TEXT("created_at"), S.CreatedAt);
		P->SetStringField(TEXT("context_snippet"), S.ContextSnippet);
		P->SetBoolField(TEXT("is_current"), S.ConvID == ActiveID);
		PlansArr.Add(MakeShareable(new FJsonValueObject(P)));
	}

	FString JsonStr;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonStr);
	FJsonSerializer::Serialize(PlansArr, Writer);

	JsonStr.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
	JsonStr.ReplaceInline(TEXT("'"), TEXT("\\'"));
	JsonStr.ReplaceInline(TEXT("\n"), TEXT("\\n"));
	JsonStr.ReplaceInline(TEXT("\r"), TEXT(""));

	ExecJs(FString::Printf(TEXT("if(typeof onPlanList==='function')onPlanList('%s');"), *JsonStr));
}

void UUECPAppBridge::ImportActivePlan(const FString& SourceConvID)
{
	auto W = OwnerWidget.Pin();
	if (!W.IsValid() || SourceConvID.IsEmpty()) return;
	FPlanManager::Get().ImportPlan(SourceConvID, W->ActiveArchitectChatID);
	W->NotifyPlanUpdated(W->ActiveArchitectChatID);
}

void UUECPAppBridge::SetPlanBrief(const FString& Brief)
{
	auto W = OwnerWidget.Pin();
	if (!W.IsValid() || W->ActiveArchitectChatID.IsEmpty()) return;
	FPlanManager::Get().SetBrief(W->ActiveArchitectChatID, Brief);
	W->NotifyPlanUpdated(W->ActiveArchitectChatID);
}

void UUECPAppBridge::EditTask(const FString& IndexStr, const FString& NewContent)
{
	auto W = OwnerWidget.Pin();
	if (!W.IsValid() || W->ActiveArchitectChatID.IsEmpty()) return;
	FTaskManager::Get().EditTaskContent(W->ActiveArchitectChatID, FCString::Atoi(*IndexStr), NewContent);
	W->NotifyTasksUpdated(W->ActiveArchitectChatID);
}

void UUECPAppBridge::AddTask(const FString& AtIndexStr, const FString& Content)
{
	auto W = OwnerWidget.Pin();
	if (!W.IsValid() || W->ActiveArchitectChatID.IsEmpty()) return;
	FTaskManager::Get().AddTask(W->ActiveArchitectChatID, FCString::Atoi(*AtIndexStr), Content);
	W->NotifyTasksUpdated(W->ActiveArchitectChatID);
}

void UUECPAppBridge::RemoveTask(const FString& IndexStr)
{
	auto W = OwnerWidget.Pin();
	if (!W.IsValid() || W->ActiveArchitectChatID.IsEmpty()) return;
	FTaskManager::Get().RemoveTask(W->ActiveArchitectChatID, FCString::Atoi(*IndexStr));
	W->NotifyTasksUpdated(W->ActiveArchitectChatID);
}

void UUECPAppBridge::ReorderTask(const FString& FromStr, const FString& ToStr)
{
	auto W = OwnerWidget.Pin();
	if (!W.IsValid() || W->ActiveArchitectChatID.IsEmpty()) return;
	FTaskManager::Get().ReorderTask(W->ActiveArchitectChatID, FCString::Atoi(*FromStr), FCString::Atoi(*ToStr));
	W->NotifyTasksUpdated(W->ActiveArchitectChatID);
}

void UUECPAppBridge::SetTaskStatus(const FString& IndexStr, const FString& Status)
{
	auto W = OwnerWidget.Pin();
	if (!W.IsValid() || W->ActiveArchitectChatID.IsEmpty()) return;
	FTaskManager::Get().UpdateTaskStatus(W->ActiveArchitectChatID, FCString::Atoi(*IndexStr), Status);
	W->NotifyTasksUpdated(W->ActiveArchitectChatID);
}

void UUECPAppBridge::ClearTasks()
{
	auto W = OwnerWidget.Pin();
	if (!W.IsValid() || W->ActiveArchitectChatID.IsEmpty()) return;
	FTaskManager::Get().ClearTasks(W->ActiveArchitectChatID);
	W->NotifyTasksUpdated(W->ActiveArchitectChatID);
}

void UUECPAppBridge::PreviewPlan(const FString& SourceConvID)
{
	if (SourceConvID.IsEmpty()) return;
	FString PlanJson = FPlanManager::Get().GetPlanJson(SourceConvID);
	PlanJson.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
	PlanJson.ReplaceInline(TEXT("'"), TEXT("\\'"));
	PlanJson.ReplaceInline(TEXT("\n"), TEXT("\\n"));
	PlanJson.ReplaceInline(TEXT("\r"), TEXT(""));
	ExecJs(FString::Printf(TEXT("if(typeof onPlanPreview==='function')onPlanPreview('%s');"), *PlanJson));
}

namespace
{
	FString CrewPreviewSnip(const FString& Full, int32 Max = 80)
	{
		FString S = Full;
		S.ReplaceInline(TEXT("\r"), TEXT(" "));
		S.ReplaceInline(TEXT("\n"), TEXT(" "));
		if (S.Len() > Max) S = S.Left(Max - 1) + TEXT("…");
		return S;
	}

	TSharedPtr<FJsonObject> CrewTemplateSummaryJson(const FCrewTemplate& T)
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("id"),       T.TemplateId);
		O->SetStringField(TEXT("name"),     T.DisplayName);
		O->SetBoolField  (TEXT("builtIn"),  T.bBuiltIn);
		O->SetNumberField(TEXT("roleCount"), T.Roles.Num());
		TArray<TSharedPtr<FJsonValue>> Roles;
		for (const FCrewRole& Role : T.Roles)
		{
			TSharedPtr<FJsonObject> RO = MakeShared<FJsonObject>();
			RO->SetStringField(TEXT("id"),             Role.RoleId);
			RO->SetStringField(TEXT("name"),           Role.Name);
			RO->SetStringField(TEXT("kind"),           UECPCrew::RoleKindToString(Role.Kind));
			RO->SetBoolField  (TEXT("isOrchestrator"), Role.bIsOrchestrator);
			TArray<TSharedPtr<FJsonValue>> AL;
			for (const FString& T2 : Role.ToolAllowlist) AL.Add(MakeShared<FJsonValueString>(T2));
			RO->SetArrayField(TEXT("toolAllowlist"),   AL);
			Roles.Add(MakeShared<FJsonValueObject>(RO));
		}
		O->SetArrayField(TEXT("roles"), Roles);
		return O;
	}

	TSharedPtr<FJsonObject> CrewRunSummaryJson(const FCrewRun& R)
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("id"),    R.RunId.ToString(EGuidFormats::DigitsWithHyphens));
		O->SetStringField(TEXT("name"),  R.DisplayName);
		O->SetStringField(TEXT("state"), UECPCrew::RunStateToString(R.State));
		return O;
	}

	TSharedPtr<FJsonObject> CrewRunDetailJson(const FCrewRun& R)
	{
		TSharedPtr<FJsonObject> O = CrewRunSummaryJson(R);
		O->SetStringField(TEXT("templateId"),  R.CrewTemplateId);
		O->SetStringField(TEXT("pauseReason"), R.PauseReason);
		O->SetStringField(TEXT("createdAt"),   R.CreatedAt.ToIso8601());
		O->SetStringField(TEXT("startedAt"),   R.StartedAt.ToIso8601());
		O->SetStringField(TEXT("endedAt"),     R.EndedAt.ToIso8601());
		O->SetStringField(TEXT("pausedAt"),    R.PausedAt.ToIso8601());
		O->SetNumberField(TEXT("totalTurns"),  R.TotalTurnsTaken);
		O->SetStringField(TEXT("completionSummary"), R.CompletionSummary);

		TSharedPtr<FJsonObject> Caps = MakeShared<FJsonObject>();
		Caps->SetNumberField(TEXT("maxTurns"),               R.Caps.MaxTotalTurns);
		Caps->SetNumberField(TEXT("maxWallMinutes"),         R.Caps.MaxWallMinutes);
		Caps->SetNumberField(TEXT("maxConsecErrors"),        R.Caps.MaxConsecutiveErrors);
		Caps->SetNumberField(TEXT("maxConsecErrorsPerRole"), R.Caps.MaxConsecutiveErrorsPerRole);
		Caps->SetBoolField  (TEXT("autoApproveDestructive"), R.Caps.bAutoApproveDestructive);
		Caps->SetNumberField(TEXT("maxRetriesPerCheckpoint"), R.Caps.MaxRetriesPerCheckpoint);
		Caps->SetNumberField(TEXT("maxTotalTokens"),         (double)R.Caps.MaxTotalTokens);
		O->SetObjectField(TEXT("caps"), Caps);
		O->SetNumberField(TEXT("totalTokensUsed"), (double)R.TotalTokensUsed);
		O->SetStringField(TEXT("reportGrounding"), R.ReportGrounding);

		{
			IUECPArchitectService& ArchSvc = IUECPCoreModule::Get().GetArchitectService();

			TMap<FString, int64> LastActivityByRole;
			for (int32 i = R.Handoffs.Num() - 1; i >= 0; --i)
			{
				const FCrewHandoff& H = R.Handoffs[i];
				if (!LastActivityByRole.Contains(H.FromRoleId)) LastActivityByRole.Add(H.FromRoleId, H.TimestampUnix);
				if (!LastActivityByRole.Contains(H.ToRoleId))   LastActivityByRole.Add(H.ToRoleId,   H.TimestampUnix);
			}

			TArray<TSharedPtr<FJsonValue>> Roles;
			for (const FCrewRole& Role : R.Roles)
			{
				TSharedPtr<FJsonObject> RO = MakeShared<FJsonObject>();
				RO->SetStringField(TEXT("id"),               Role.RoleId);
				RO->SetStringField(TEXT("name"),             Role.Name);
				RO->SetStringField(TEXT("kind"),             UECPCrew::RoleKindToString(Role.Kind));
				RO->SetBoolField  (TEXT("isOrchestrator"),   Role.bIsOrchestrator);
				RO->SetNumberField(TEXT("apiKeySlotIndex"),  Role.ApiKeySlotIndex);
				const FString* ChatIdPtr = R.RoleChatIds.Find(Role.RoleId);
				const FString  ChatId    = ChatIdPtr ? *ChatIdPtr : FString();
				RO->SetStringField(TEXT("chatId"),           ChatId);
				TArray<TSharedPtr<FJsonValue>> AL;
				for (const FString& T : Role.ToolAllowlist) AL.Add(MakeShared<FJsonValueString>(T));
				RO->SetArrayField(TEXT("toolAllowlist"),     AL);

				const bool bThinking = !ChatId.IsEmpty() && ArchSvc.IsThinkingForChat(ChatId);
				const int32 Queued   = ChatId.IsEmpty() ? 0 : ArchSvc.GetQueuedMessageCountForChat(ChatId);
				RO->SetBoolField  (TEXT("thinking"),         bThinking);
				RO->SetNumberField(TEXT("queuedMessages"),   Queued);
				if (const int64* TS = LastActivityByRole.Find(Role.RoleId))
					RO->SetStringField(TEXT("lastActivityUnix"), LexToString(*TS));

				Roles.Add(MakeShared<FJsonValueObject>(RO));
			}
			O->SetArrayField(TEXT("roles"), Roles);
		}

		{
			TArray<TSharedPtr<FJsonValue>> Plan;
			for (const FCrewCheckpoint& C : R.Plan)
			{
				TSharedPtr<FJsonObject> CO = MakeShared<FJsonObject>();
				CO->SetStringField(TEXT("id"),              C.CheckpointId);
				CO->SetStringField(TEXT("description"),     C.Description);
				CO->SetStringField(TEXT("successCriteria"), C.SuccessCriteria);
				CO->SetStringField(TEXT("state"),           UECPCrew::CheckpointStateToString(C.State));
				CO->SetStringField(TEXT("resultSummary"),   C.ResultSummary);
				CO->SetNumberField(TEXT("retryCount"),      C.RetryCount);
				CO->SetStringField(TEXT("lastReportStatus"), C.LastReportStatus);
				CO->SetStringField(TEXT("verificationText"), C.VerificationText);
				CO->SetBoolField  (TEXT("grounded"),        C.bGrounded);
				{
					TArray<TSharedPtr<FJsonValue>> VArr;
					for (const FString& V : C.VerificationToolCalls) VArr.Add(MakeShared<FJsonValueString>(V));
					CO->SetArrayField(TEXT("verificationToolCalls"), VArr);
				}
				TArray<TSharedPtr<FJsonValue>> Reqs;
				for (const FString& RoleId : C.RequiredRoleIds)
					Reqs.Add(MakeShared<FJsonValueString>(RoleId));
				CO->SetArrayField(TEXT("requiredRoleIds"), Reqs);
				TArray<TSharedPtr<FJsonValue>> Deps;
				for (const FString& D : C.DependsOn)
					Deps.Add(MakeShared<FJsonValueString>(D));
				CO->SetArrayField(TEXT("dependsOn"), Deps);
				CO->SetNumberField(TEXT("timeoutMinutes"), C.TimeoutMinutes);
				TArray<TSharedPtr<FJsonValue>> Tools;
				for (const FString& T : C.ToolAllowlistOverride)
					Tools.Add(MakeShared<FJsonValueString>(T));
				CO->SetArrayField(TEXT("toolAllowlistOverride"), Tools);
				Plan.Add(MakeShared<FJsonValueObject>(CO));
			}
			O->SetArrayField(TEXT("plan"), Plan);
		}

		{
			TMap<FString, FString> RoleNameById;
			for (const FCrewRole& Role : R.Roles) RoleNameById.Add(Role.RoleId, Role.Name);

			TArray<TSharedPtr<FJsonValue>> Handoffs;
			for (const FCrewHandoff& H : R.Handoffs)
			{
				TSharedPtr<FJsonObject> HO = MakeShared<FJsonObject>();
				HO->SetStringField(TEXT("id"),           H.Id.ToString(EGuidFormats::DigitsWithHyphens));
				const FString* FromName = RoleNameById.Find(H.FromRoleId);
				const FString* ToName   = RoleNameById.Find(H.ToRoleId);
				HO->SetStringField(TEXT("from"),         FromName ? *FromName : H.FromRoleId);
				HO->SetStringField(TEXT("to"),           ToName   ? *ToName   : H.ToRoleId);
				HO->SetStringField(TEXT("type"),         UECPCrew::HandoffTypeToString(H.Type));
				HO->SetStringField(TEXT("checkpointId"), H.CheckpointId);
				HO->SetStringField(TEXT("preview"),      CrewPreviewSnip(H.Content));
				HO->SetStringField(TEXT("content"),      H.Content);
				HO->SetNumberField(TEXT("timestamp"),    (double)H.TimestampUnix);
				HO->SetBoolField  (TEXT("explicitReport"), H.bExplicitReport);
				HO->SetStringField(TEXT("verification"),   H.Verification);
				{
					TArray<TSharedPtr<FJsonValue>> VArr;
					for (const FString& V : H.VerificationToolCalls) VArr.Add(MakeShared<FJsonValueString>(V));
					HO->SetArrayField(TEXT("verificationToolCalls"), VArr);
				}
				Handoffs.Add(MakeShared<FJsonValueObject>(HO));
			}
			O->SetArrayField(TEXT("handoffs"), Handoffs);
		}

		return O;
	}

	FString SerializeJson(const TSharedRef<FJsonObject>& Obj)
	{
		FString Out;
		TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
		FJsonSerializer::Serialize(Obj, W);
		return Out;
	}

	FString EscapeJsonForJsString(const FString& In)
	{
		return FString(In)
			.Replace(TEXT("\\"), TEXT("\\\\"))
			.Replace(TEXT("'"),  TEXT("\\'"))
			.Replace(TEXT("\n"), TEXT("\\n"))
			.Replace(TEXT("\r"), TEXT(""));
	}
}

void UUECPAppBridge::SubscribeToCrewEvents()
{
	if (!IUECPCoreModule::IsAvailable()) return;
	IUECPCrewService& Crew = IUECPCoreModule::Get().GetCrewService();

	TWeakObjectPtr<UUECPAppBridge> WeakSelf(this);
	Crew.OnRunChanged().AddLambda([WeakSelf](const FCrewRun& ChangedRun)
	{
		UUECPAppBridge* B = WeakSelf.Get();
		if (!B) return;
		if (B->SelectedCrewRunId.IsValid() && ChangedRun.RunId == B->SelectedCrewRunId)
			B->PushCrewActiveRunDetail();
		else
			B->PushCrewState();
	});
	Crew.OnTemplatesChanged().AddLambda([WeakSelf]()
	{
		if (UUECPAppBridge* B = WeakSelf.Get()) B->PushCrewState();
	});

	IUECPCoreModule::Get().GetExtensionService().OnExtensionStateChanged()
		.AddLambda([WeakSelf](FName)
	{
		if (UUECPAppBridge* B = WeakSelf.Get()) B->CrewListAllTools();
	});
}

void UUECPAppBridge::PushCrewState()
{
	if (!IUECPCoreModule::IsAvailable()) return;
	IUECPCrewService& Crew = IUECPCoreModule::Get().GetCrewService();

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();

	{
		TArray<TSharedPtr<FJsonValue>> Arr;
		for (const FCrewTemplate& T : Crew.GetTemplates())
		{
			Arr.Add(MakeShared<FJsonValueObject>(CrewTemplateSummaryJson(T)));
		}
		Root->SetArrayField(TEXT("templates"), Arr);
	}

	{
		TArray<TSharedPtr<FJsonValue>> Arr;
		for (const FCrewRun& R : Crew.GetRuns())
		{
			Arr.Add(MakeShared<FJsonValueObject>(CrewRunSummaryJson(R)));
		}
		Root->SetArrayField(TEXT("runs"), Arr);
	}

	if (!SelectedCrewRunId.IsValid())
	{
		SelectedCrewRunId = Crew.GetActiveRunId();
	}

	Root->SetStringField(TEXT("activeRunId"),
		SelectedCrewRunId.IsValid() ? SelectedCrewRunId.ToString(EGuidFormats::DigitsWithHyphens) : FString());

	FCrewRun ActiveRun;
	if (SelectedCrewRunId.IsValid() && Crew.GetRun(SelectedCrewRunId, ActiveRun))
	{
		Root->SetObjectField(TEXT("activeRun"), CrewRunDetailJson(ActiveRun));
	}

	const FString Json = SerializeJson(Root);
	const FString Escaped = EscapeJsonForJsString(Json);
	ExecJs(FString::Printf(TEXT("if(typeof onCrewState==='function')onCrewState('%s')"), *Escaped));
}

void UUECPAppBridge::PushCrewActiveRunDetail()
{
	if (!IUECPCoreModule::IsAvailable()) return;
	IUECPCrewService& Crew = IUECPCoreModule::Get().GetCrewService();

	if (!SelectedCrewRunId.IsValid())
	{
		PushCrewState();
		return;
	}
	FCrewRun ActiveRun;
	if (!Crew.GetRun(SelectedCrewRunId, ActiveRun))
	{
		PushCrewState();
		return;
	}

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	{
		TArray<TSharedPtr<FJsonValue>> Arr;
		for (const FCrewRun& R : Crew.GetRuns())
			Arr.Add(MakeShared<FJsonValueObject>(CrewRunSummaryJson(R)));
		Root->SetArrayField(TEXT("runs"), Arr);
	}
	Root->SetStringField(TEXT("activeRunId"), SelectedCrewRunId.ToString(EGuidFormats::DigitsWithHyphens));
	Root->SetObjectField(TEXT("activeRun"),   CrewRunDetailJson(ActiveRun));

	const FString Json = SerializeJson(Root);
	const FString Escaped = EscapeJsonForJsString(Json);
	ExecJs(FString::Printf(TEXT("if(typeof onCrewActiveRunUpdate==='function')onCrewActiveRunUpdate('%s')"), *Escaped));
}

void UUECPAppBridge::CrewRefresh()
{
	PushCrewState();
}

void UUECPAppBridge::CrewSelectRun(const FString& RunId)
{
	FGuid Parsed;
	if (!FGuid::Parse(RunId, Parsed)) Parsed.Invalidate();
	SelectedCrewRunId = Parsed;
	PushCrewState();
}

void UUECPAppBridge::CrewOpenRoleChat(const FString& RunId, const FString& RoleId)
{
	FGuid Parsed;
	if (!FGuid::Parse(RunId, Parsed)) return;
	FCrewRun Run;
	if (!IUECPCoreModule::Get().GetCrewService().GetRun(Parsed, Run)) return;
	const FString* ChatIdPtr = Run.RoleChatIds.Find(RoleId);
	if (!ChatIdPtr || ChatIdPtr->IsEmpty()) return;

	IUECPCoreModule::Get().GetArchitectService().SwitchChat(*ChatIdPtr);
	ExecJs(TEXT("if(typeof switchTab==='function')switchTab('architect', document.querySelector(\"[data-view='architect']\"))"));
}

void UUECPAppBridge::CrewCreateRun(const FString& TemplateId, const FString& DisplayName, const FString& PerRoleSlotsJson)
{
	IUECPCrewService& Crew = IUECPCoreModule::Get().GetCrewService();
	const FGuid NewId = Crew.CreateRun(TemplateId, DisplayName);
	if (!NewId.IsValid())
	{
		PushToast(TEXT("Failed to create run (template missing?)"), TEXT("warn"));
		PushCrewState();
		return;
	}
	SelectedCrewRunId = NewId;

	if (!PerRoleSlotsJson.IsEmpty())
	{
		TSharedPtr<FJsonObject> Picks;
		TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(PerRoleSlotsJson);
		if (FJsonSerializer::Deserialize(R, Picks) && Picks.IsValid())
		{
			FCrewRun Run;
			if (Crew.GetRun(NewId, Run))
			{
				bool bAnyChange = false;
				for (FCrewRole& Role : Run.Roles)
				{
					double SlotIdx = -1;
					if (Picks->TryGetNumberField(Role.RoleId, SlotIdx))
					{
						Role.ApiKeySlotIndex = (int32)SlotIdx;
						bAnyChange = true;
					}
				}
				if (bAnyChange) Crew.SetRunRoles(NewId, Run.Roles);
			}
		}
	}

	PushToast(TEXT("New crew run created — add checkpoints, then click Run"), TEXT("info"));
	PushCrewState();
}

void UUECPAppBridge::CrewListSlots()
{
	TArray<FApiKeySlot> Slots = FApiKeyManager::Get().GetAllSlots();
	int32 PopulatedCount = 0;
	TArray<TSharedPtr<FJsonValue>> Arr;
	for (int32 i = 0; i < Slots.Num(); ++i)
	{
		const FApiKeySlot& S = Slots[i];
		const bool bAnyData =
			!S.Provider.IsEmpty() || !S.ApiKey.IsEmpty() || !S.Name.IsEmpty() ||
			!S.CustomModelName.IsEmpty() || !S.CustomBaseURL.IsEmpty() ||
			!S.ClaudeModel.IsEmpty() || !S.OpenAIModel.IsEmpty() || !S.GeminiModel.IsEmpty();
		if (!bAnyData) continue;

		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		const int32 ResolvedIndex = (S.SlotIndex >= 0 && S.SlotIndex < Slots.Num()) ? S.SlotIndex : i;
		O->SetNumberField(TEXT("index"),    ResolvedIndex);
		O->SetStringField(TEXT("name"),     S.Name);
		O->SetStringField(TEXT("provider"), S.Provider);
		const bool bIsACPProvider = !S.Provider.IsEmpty()
			&& IUECPCoreModule::IsAvailable()
			&& IUECPCoreModule::Get().GetACPRegistryService().IsInstalled(S.Provider);
		FString Model;
		if (bIsACPProvider)
		{
			Model = S.AgentModel;
		}
		else
		{
			Model = S.CustomModelName;
			if (Model.IsEmpty())
			{
				if      (S.Provider == TEXT("Claude") && !S.ClaudeModel.IsEmpty())  Model = S.ClaudeModel;
				else if (S.Provider == TEXT("OpenAI") && !S.OpenAIModel.IsEmpty())  Model = S.OpenAIModel;
				else if (S.Provider == TEXT("Gemini") && !S.GeminiModel.IsEmpty())  Model = S.GeminiModel;
			}
		}
		O->SetStringField(TEXT("model"), Model);
		O->SetBoolField  (TEXT("isAcp"), bIsACPProvider);
		Arr.Add(MakeShared<FJsonValueObject>(O));
		++PopulatedCount;
	}

	FString Json;
	TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> W =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Json);
	FJsonSerializer::Serialize(Arr, W);

	const int32 ActiveIdx = FApiKeyManager::Get().GetActiveSlotIndex();

	UE_LOG(LogTemp, Log,
		TEXT("CrewListSlots: serialised %d populated slot(s) (of %d total), active=%d"),
		PopulatedCount, Slots.Num(), ActiveIdx);

	const FString Escaped = FString(Json)
		.Replace(TEXT("\\"), TEXT("\\\\"))
		.Replace(TEXT("'"),  TEXT("\\'"))
		.Replace(TEXT("\n"), TEXT("\\n"))
		.Replace(TEXT("\r"), TEXT(""));
	ExecJs(FString::Printf(
		TEXT("if(typeof onCrewSlots==='function')onCrewSlots('%s', %d)"),
		*Escaped, ActiveIdx));
}

void UUECPAppBridge::CrewSetRunRoleSlot(const FString& RunId, const FString& RoleId, const FString& SlotIndex)
{
	FGuid Parsed;
	if (!FGuid::Parse(RunId, Parsed)) return;
	const int32 Idx = FCString::Atoi(*SlotIndex);
	IUECPCoreModule::Get().GetCrewService().SetRoleApiKeySlot(Parsed, RoleId, Idx);
}

void UUECPAppBridge::CrewStartRun(const FString& RunId)
{
	FGuid Parsed;
	if (!FGuid::Parse(RunId, Parsed)) return;
	FString Reason;
	if (!IUECPCoreModule::Get().GetCrewService().StartRun(Parsed, Reason))
	{
		PushToast(FString::Printf(TEXT("StartRun failed: %s"), *Reason), TEXT("warn"));
	}
}

void UUECPAppBridge::CrewPauseRun(const FString& RunId)
{
	FGuid Parsed;
	if (!FGuid::Parse(RunId, Parsed)) return;
	IUECPCoreModule::Get().GetCrewService().PauseRun(Parsed);
}

void UUECPAppBridge::CrewResumeRun(const FString& RunId)
{
	FGuid Parsed;
	if (!FGuid::Parse(RunId, Parsed)) return;
	IUECPCoreModule::Get().GetCrewService().ResumeRun(Parsed);
}

void UUECPAppBridge::CrewAbortRun(const FString& RunId)
{
	FGuid Parsed;
	if (!FGuid::Parse(RunId, Parsed)) return;
	IUECPCoreModule::Get().GetCrewService().AbortRun(Parsed, TEXT("user_abort_ui"));
}

void UUECPAppBridge::CrewDeleteRun(const FString& RunId, bool bDeleteChats)
{
	FGuid Parsed;
	if (!FGuid::Parse(RunId, Parsed)) return;

	IUECPCrewService& Crew = IUECPCoreModule::Get().GetCrewService();

	FCrewRun Run;
	const bool bHaveRun = Crew.GetRun(Parsed, Run);

	if (bHaveRun && (Run.State == ECrewRunState::Running || Run.State == ECrewRunState::Paused))
	{
		Crew.AbortRun(Parsed, TEXT("user_delete"));
	}

	if (bDeleteChats && bHaveRun)
	{
		for (const TPair<FString, FString>& P : Run.RoleChatIds)
		{
			if (!P.Value.IsEmpty()) DeleteChat(TEXT("architect"), P.Value);
		}
	}

	if (SelectedCrewRunId == Parsed) SelectedCrewRunId.Invalidate();
	Crew.DeleteRun(Parsed);
	PushCrewState();
}

void UUECPAppBridge::CrewDeleteRunChats(const FString& RunId)
{
	FGuid Parsed;
	if (!FGuid::Parse(RunId, Parsed)) return;

	IUECPCrewService& Crew = IUECPCoreModule::Get().GetCrewService();
	FCrewRun Run;
	if (Crew.GetRun(Parsed, Run))
	{
		for (const TPair<FString, FString>& P : Run.RoleChatIds)
		{
			if (!P.Value.IsEmpty()) DeleteChat(TEXT("architect"), P.Value);
		}
	}
	PushCrewState();
}

void UUECPAppBridge::CrewCreateTemplate(const FString& DisplayName)
{
	const FString NewId = IUECPCoreModule::Get().GetCrewService().CreateTemplate(DisplayName);
	if (NewId.IsEmpty())
	{
		PushToast(TEXT("Failed to create template"), TEXT("warn"));
	}
	else
	{
		PushToast(FString::Printf(TEXT("Template '%s' created"), *DisplayName), TEXT("info"));
	}
}

void UUECPAppBridge::CrewUpdateTemplate(const FString& TemplateId, const FString& TemplateJson)
{
	IUECPCrewService& Crew = IUECPCoreModule::Get().GetCrewService();
	FCrewTemplate Existing;
	if (!Crew.GetTemplate(TemplateId, Existing))
	{
		PushToast(TEXT("Template not found"), TEXT("warn"));
		return;
	}
	if (Existing.bBuiltIn)
	{
		PushToast(TEXT("Built-in templates can't be edited — duplicate first"), TEXT("warn"));
		return;
	}

	TSharedPtr<FJsonObject> O;
	TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(TemplateJson);
	if (!FJsonSerializer::Deserialize(R, O) || !O.IsValid())
	{
		PushToast(TEXT("Template JSON parse failed"), TEXT("warn"));
		return;
	}

	FString NewName;
	if (O->TryGetStringField(TEXT("name"), NewName) && !NewName.IsEmpty())
	{
		Existing.DisplayName = NewName;
	}

	const TArray<TSharedPtr<FJsonValue>>* RolesArr = nullptr;
	if (O->TryGetArrayField(TEXT("roles"), RolesArr) && RolesArr)
	{
		TArray<FCrewRole> NewRoles;
		TMap<FString, const FCrewRole*> ByOldId;
		for (const FCrewRole& Old : Existing.Roles) ByOldId.Add(Old.RoleId, &Old);

		for (const TSharedPtr<FJsonValue>& V : *RolesArr)
		{
			if (!V.IsValid() || V->Type != EJson::Object) continue;
			TSharedPtr<FJsonObject> RO = V->AsObject();
			FCrewRole Role;
			RO->TryGetStringField(TEXT("id"),             Role.RoleId);
			RO->TryGetStringField(TEXT("name"),           Role.Name);
			FString KindStr;
			RO->TryGetStringField(TEXT("kind"),           KindStr);
			Role.Kind = UECPCrew::RoleKindFromString(KindStr);
			RO->TryGetBoolField  (TEXT("isOrchestrator"), Role.bIsOrchestrator);

			if (const FCrewRole** PrevPtr = ByOldId.Find(Role.RoleId))
			{
				const FCrewRole* Prev = *PrevPtr;
				Role.ApiKeySlotIndex = Prev->ApiKeySlotIndex;
				Role.ToolAllowlist   = Prev->ToolAllowlist;
				Role.SystemPromptOverride = Prev->SystemPromptOverride;
			}
			NewRoles.Add(Role);
		}

		int32 OrchCount = 0;
		for (const FCrewRole& R2 : NewRoles) if (R2.bIsOrchestrator) ++OrchCount;
		if (OrchCount == 0 && NewRoles.Num() > 0)
		{
			NewRoles[0].bIsOrchestrator = true;
		}
		else if (OrchCount > 1)
		{
			bool bSeen = false;
			for (FCrewRole& R2 : NewRoles)
			{
				if (R2.bIsOrchestrator)
				{
					if (bSeen) R2.bIsOrchestrator = false;
					else       bSeen = true;
				}
			}
		}

		Existing.Roles = NewRoles;
	}

	if (!Crew.UpdateTemplate(Existing))
	{
		PushToast(TEXT("Update failed"), TEXT("warn"));
	}
}

void UUECPAppBridge::CrewDeleteTemplate(const FString& TemplateId)
{
	IUECPCrewService& Crew = IUECPCoreModule::Get().GetCrewService();
	FCrewTemplate Existing;
	if (Crew.GetTemplate(TemplateId, Existing) && Existing.bBuiltIn)
	{
		PushToast(TEXT("Built-in templates can't be deleted"), TEXT("warn"));
		return;
	}
	if (!Crew.DeleteTemplate(TemplateId))
	{
		PushToast(TEXT("Delete failed"), TEXT("warn"));
	}
}

void UUECPAppBridge::CrewDuplicateTemplate(const FString& TemplateId, const FString& NewName)
{
	IUECPCrewService& Crew = IUECPCoreModule::Get().GetCrewService();
	FCrewTemplate Source;
	if (!Crew.GetTemplate(TemplateId, Source))
	{
		PushToast(TEXT("Source template not found"), TEXT("warn"));
		return;
	}

	const FString DupName = NewName.IsEmpty()
		? FString::Printf(TEXT("%s (copy)"), *Source.DisplayName)
		: NewName;
	const FString NewId = Crew.CreateTemplate(DupName);
	if (NewId.IsEmpty())
	{
		PushToast(TEXT("Failed to create duplicate"), TEXT("warn"));
		return;
	}

	FCrewTemplate Dup;
	if (!Crew.GetTemplate(NewId, Dup)) return;
	Dup.Roles = Source.Roles;
	for (FCrewRole& R : Dup.Roles)
	{
		R.RoleId = FString::Printf(TEXT("role.%s"), *FGuid::NewGuid().ToString(EGuidFormats::DigitsLower).Left(8));
	}
	Crew.UpdateTemplate(Dup);
	PushToast(FString::Printf(TEXT("Duplicated as '%s'"), *DupName), TEXT("ok"));
}

void UUECPAppBridge::CrewSavePlanAsTemplateDefault(const FString& RunId)
{
	FGuid Parsed;
	if (!FGuid::Parse(RunId, Parsed)) return;

	IUECPCrewService& Crew = IUECPCoreModule::Get().GetCrewService();
	FCrewRun Run;
	if (!Crew.GetRun(Parsed, Run))
	{
		PushToast(TEXT("Run not found"), TEXT("warn"));
		return;
	}
	if (Run.CrewTemplateId.IsEmpty())
	{
		PushToast(TEXT("Run has no source template"), TEXT("warn"));
		return;
	}

	FCrewTemplate Tpl;
	if (!Crew.GetTemplate(Run.CrewTemplateId, Tpl))
	{
		PushToast(TEXT("Source template not found"), TEXT("warn"));
		return;
	}
	if (Tpl.bBuiltIn)
	{
		PushToast(TEXT("Can't overwrite a built-in template — duplicate it first"), TEXT("warn"));
		return;
	}

	Tpl.DefaultPlan = Run.Plan;
	for (FCrewCheckpoint& C : Tpl.DefaultPlan)
	{
		C.State = ECheckpointState::Pending;
		C.ResultSummary.Empty();
	}

	if (Crew.UpdateTemplate(Tpl))
	{
		PushToast(FString::Printf(TEXT("Saved plan to template '%s' (%d checkpoints)"),
			*Tpl.DisplayName, Tpl.DefaultPlan.Num()), TEXT("ok"));
	}
	else
	{
		PushToast(TEXT("Failed to save plan to template"), TEXT("warn"));
	}
}

void UUECPAppBridge::CrewExportTemplate(const FString& TemplateId)
{
	IUECPCrewService& Crew = IUECPCoreModule::Get().GetCrewService();
	FCrewTemplate Tpl;
	if (!Crew.GetTemplate(TemplateId, Tpl))
	{
		PushToast(TEXT("Template not found"), TEXT("warn"));
		return;
	}

	FString Json;
	if (!Crew.ExportTemplate(TemplateId, Json))
	{
		PushToast(TEXT("Export failed"), TEXT("warn"));
		return;
	}

	IDesktopPlatform* Desktop = FDesktopPlatformModule::Get();
	if (!Desktop) return;

	FString SafeName;
	for (TCHAR C : Tpl.DisplayName)
	{
		if (FChar::IsAlnum(C) || C == TEXT('-') || C == TEXT('_')) SafeName.AppendChar(C);
		else if (C == TEXT(' ')) SafeName.AppendChar(TEXT('_'));
	}
	if (SafeName.IsEmpty()) SafeName = TEXT("crew_template");

	TArray<FString> OutFiles;
	const bool bSaved = Desktop->SaveFileDialog(
		nullptr,
		TEXT("Export Crew Template"),
		FPaths::ProjectSavedDir(),
		SafeName + TEXT(".crew.json"),
		TEXT("Crew Template (*.crew.json;*.json)|*.crew.json;*.json"),
		EFileDialogFlags::None,
		OutFiles);
	if (!bSaved || OutFiles.Num() == 0) return;

	if (FFileHelper::SaveStringToFile(Json, *OutFiles[0]))
	{
		PushToast(FString::Printf(TEXT("Exported '%s'"), *Tpl.DisplayName), TEXT("ok"));
	}
	else
	{
		PushToast(TEXT("Failed to write file"), TEXT("warn"));
	}
}

void UUECPAppBridge::CrewImportTemplate()
{
	IDesktopPlatform* Desktop = FDesktopPlatformModule::Get();
	if (!Desktop) return;

	TArray<FString> OutFiles;
	const bool bOpened = Desktop->OpenFileDialog(
		nullptr,
		TEXT("Import Crew Template"),
		FPaths::ProjectSavedDir(),
		FString(),
		TEXT("Crew Template (*.crew.json;*.json)|*.crew.json;*.json"),
		EFileDialogFlags::None,
		OutFiles);
	if (!bOpened || OutFiles.Num() == 0) return;

	FString Json;
	if (!FFileHelper::LoadFileToString(Json, *OutFiles[0]))
	{
		PushToast(TEXT("Failed to read file"), TEXT("warn"));
		return;
	}

	const FString NewId = IUECPCoreModule::Get().GetCrewService().ImportTemplate(Json);
	if (NewId.IsEmpty())
	{
		PushToast(TEXT("Import failed — not a valid crew template"), TEXT("warn"));
		return;
	}
	PushToast(TEXT("Template imported"), TEXT("ok"));
}

static FString CrewBuildPlanPrompt(const FString& GoalText, int32 MinCheckpoints, int32 MaxCheckpoints)
{
	const int32 Lo = FMath::Clamp(MinCheckpoints, 1, 30);
	const int32 Hi = FMath::Clamp(MaxCheckpoints, Lo, 30);

	const FString RangeLine = (Lo == Hi)
		? FString::Printf(TEXT("Produce exactly %d checkpoint%s."), Lo, (Lo == 1 ? TEXT("") : TEXT("s")))
		: FString::Printf(TEXT("Aim for %d-%d checkpoints."), Lo, Hi);

	return FString::Printf(
		TEXT("You are planning work for an autonomous crew of AI assistants that operate INSIDE Unreal Engine 5 "
		     "through the Ultimate Engine Co-Pilot plugin. The crew builds and edits UE assets directly in the "
		     "editor — Blueprints (graphs, variables, components, functions, interfaces), materials, Niagara "
		     "systems, levels and actors, animation, Control Rig, GAS, data tables, widgets, and more — using the "
		     "plugin's tools. A Worker performs the build steps in-editor; a Verifier inspects the result with "
		     "read-only inspection tools (reads the asset's structure, variables, graph nodes/wiring, and compile "
		     "status) and confirms it matches the intent.\n\n"
		     "Break this goal into an ordered set of concrete, buildable checkpoints. Each checkpoint must be a "
		     "milestone a Worker can complete in the editor and a Verifier can confirm by inspecting the actual "
		     "asset. Think in terms of UE assets and Blueprint graphs, not generic software-project tasks — avoid "
		     "steps like \"set up the environment\" or \"write documentation\"; prefer concrete actions like "
		     "\"Create BP_Door (Actor) with a StaticMesh + Box collision\" or \"Add an OnInteract interface event "
		     "that toggles the door\".\n\n"
		     "successCriteria MUST be assertions the Verifier can confirm by READING the asset — about its class/"
		     "parent, the variables and their types/defaults, the functions and their signatures, the graph nodes "
		     "and connections, and whether it compiles cleanly. Do NOT write UI-state or editor-window checks: the "
		     "Verifier cannot see what is open in the editor. NEVER phrase criteria as \"open X in the editor\", "
		     "\"open in the widget designer\", \"confirm the tab is open\", or similar — phrase them as facts about "
		     "the asset itself (e.g. \"BP_Item has variables ItemID (int32=0), MaxStack (int32=1); compiles with 0 "
		     "errors\").\n\n"
		     "Goal:\n%s\n\n"
		     "Return ONLY a JSON object (no prose, no markdown fences) in this exact shape:\n"
		     "{\"checkpoints\":[{\"id\":\"cp_1\",\"description\":\"what a worker should build in-editor\",\"successCriteria\":\"inspectable facts about the asset a verifier confirms by reading it\"}]}\n\n"
		     "%s ids must be cp_1, cp_2, etc. Descriptions must be concrete UE build actions; successCriteria must be verifiable by reading the asset (never by checking what is open in the editor)."),
		*GoalText, *RangeLine);
}

void UUECPAppBridge::CancelInFlightDraft(const FString& RunId, bool bNotifyJs)
{
	TUniquePtr<FCrewDraftInFlight>* Found = ActiveDrafts.Find(RunId);
	if (!Found || !Found->IsValid()) return;

	TUniquePtr<FCrewDraftInFlight> Snap = MoveTemp(*Found);
	ActiveDrafts.Remove(RunId);

	if (Snap->bIsAcp)
	{
		IUECPArchitectService& Arch = IUECPCoreModule::Get().GetArchitectService();
		if (Snap->AcpHandle.IsValid())
		{
			Arch.OnTurnEnded().Remove(Snap->AcpHandle);
		}
		if (!Snap->AcpDraftChatId.IsEmpty())
		{
			Arch.StopGenerationForChat(Snap->AcpDraftChatId);
			if (!Snap->AcpOriginalActive.IsEmpty()) Arch.SwitchChat(Snap->AcpOriginalActive);
			DeleteChat(TEXT("architect"), Snap->AcpDraftChatId);
		}
	}
	else
	{
		if (TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> Req = Snap->HttpReq.Pin())
		{
			Req->CancelRequest();
		}
	}

	if (bNotifyJs)
	{
		FString EscRun = RunId; EscRun.ReplaceInline(TEXT("'"), TEXT("\\'"));
		ExecJs(FString::Printf(
			TEXT("if(typeof onCrewPlanDraftCancelled==='function')onCrewPlanDraftCancelled('%s')"),
			*EscRun));
	}
}

void UUECPAppBridge::CrewCancelPlanDraft(const FString& RunId)
{
	CancelInFlightDraft(RunId, true);
}

void UUECPAppBridge::CrewDraftPlanFromGoal_ViaArchitect(const FString& RunId, const FString& GoalText,
	int32 MinCheckpoints, int32 MaxCheckpoints)
{
	auto W = OwnerWidget.Pin();
	if (!W.IsValid()) return;

	CancelInFlightDraft(RunId, false);

	IUECPArchitectService& Arch = IUECPCoreModule::Get().GetArchitectService();

	const FString OriginalActive = W->ActiveArchitectChatID;
	Arch.NewChat();
	const FString DraftChatId = Arch.GetActiveChatID();
	if (DraftChatId.IsEmpty() || DraftChatId == OriginalActive)
	{
		PushToast(TEXT("Failed to create draft chat"), TEXT("warn"));
		{
			FString EscRun = RunId; EscRun.ReplaceInline(TEXT("'"), TEXT("\\'"));
			ExecJs(FString::Printf(TEXT("if(typeof onCrewPlanDraftFailed==='function')onCrewPlanDraftFailed('%s','chat-create-failed')"), *EscRun));
		}
		return;
	}
	Arch.SetChatDisplayName(DraftChatId, TEXT("[Drafting crew plan…]"));

	{
		TUniquePtr<FCrewDraftInFlight> Tracker = MakeUnique<FCrewDraftInFlight>();
		Tracker->RunId = RunId;
		Tracker->bIsAcp = true;
		Tracker->AcpDraftChatId = DraftChatId;
		Tracker->AcpOriginalActive = OriginalActive;
		ActiveDrafts.Add(RunId, MoveTemp(Tracker));
	}

	TWeakObjectPtr<UUECPAppBridge> WeakSelf(this);
	const FString CapturedRunId   = RunId;
	const FString CapturedDraftId = DraftChatId;
	const FString CapturedOrigId  = OriginalActive;

	const FDelegateHandle TurnHandle = Arch.OnTurnEnded().AddLambda(
		[WeakSelf, CapturedDraftId, CapturedRunId, CapturedOrigId]
		(const FString& ChatID, bool bSuccess)
	{
		if (ChatID != CapturedDraftId) return;

		const FString LocalDraftId = CapturedDraftId;
		const FString LocalRunId   = CapturedRunId;
		const FString LocalOrigId  = CapturedOrigId;
		TWeakObjectPtr<UUECPAppBridge> LocalWeak = WeakSelf;

		UUECPAppBridge* B = LocalWeak.Get();
		if (!B) return;

		TUniquePtr<FCrewDraftInFlight>* Slot = B->ActiveDrafts.Find(LocalRunId);
		if (!Slot || !Slot->IsValid() || (*Slot)->AcpDraftChatId != LocalDraftId)
		{
			UE_LOG(LogTemp, Log, TEXT("CrewDraftPlanFromGoal[ACP]: dropping stale turn-end (runId=%s)"), *LocalRunId);
			return;
		}

		IUECPArchitectService& Arch2 = IUECPCoreModule::Get().GetArchitectService();
		const FDelegateHandle HandleCopy = (*Slot)->AcpHandle;
		B->ActiveDrafts.Remove(LocalRunId);
		Arch2.OnTurnEnded().Remove(HandleCopy);

		FString LastText;
		const TArray<TSharedPtr<FJsonValue>> Hist =
			Arch2.GetConversationHistoryForChat(LocalDraftId);
		for (int32 i = Hist.Num() - 1; i >= 0; --i)
		{
			if (!Hist[i].IsValid() || Hist[i]->Type != EJson::Object) continue;
			TSharedPtr<FJsonObject> O = Hist[i]->AsObject();
			FString Role; O->TryGetStringField(TEXT("role"), Role);
			if (Role != TEXT("assistant") && Role != TEXT("model")) continue;

			FString StringContent;
			if (O->TryGetStringField(TEXT("content"), StringContent) && !StringContent.IsEmpty())
			{
				LastText = StringContent;
			}
			else
			{
				const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
				if (O->TryGetArrayField(TEXT("content"), Arr) && Arr)
				{
					for (const TSharedPtr<FJsonValue>& V : *Arr)
					{
						if (!V.IsValid() || V->Type != EJson::Object) continue;
						FString T;
						V->AsObject()->TryGetStringField(TEXT("text"), T);
						if (!T.IsEmpty()) LastText += T;
					}
				}
				if (LastText.IsEmpty() && O->TryGetArrayField(TEXT("parts"), Arr) && Arr)
				{
					for (const TSharedPtr<FJsonValue>& V : *Arr)
					{
						if (!V.IsValid() || V->Type != EJson::Object) continue;
						FString T;
						V->AsObject()->TryGetStringField(TEXT("text"), T);
						if (!T.IsEmpty()) LastText += T;
					}
				}
			}
			if (!LastText.IsEmpty()) break;
		}

		UE_LOG(LogTemp, Log, TEXT("CrewDraftPlanFromGoal[ACP]: extracted %d chars from chat %s"),
			LastText.Len(), *LocalDraftId);

		if (!LocalOrigId.IsEmpty()) Arch2.SwitchChat(LocalOrigId);
		B->DeleteChat(TEXT("architect"), LocalDraftId);

		FString Esc = LastText;
		Esc.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
		Esc.ReplaceInline(TEXT("'"),  TEXT("\\'"));
		Esc.ReplaceInline(TEXT("\n"), TEXT("\\n"));
		Esc.ReplaceInline(TEXT("\r"), TEXT("\\r"));
		FString EscRun = LocalRunId; EscRun.ReplaceInline(TEXT("'"), TEXT("\\'"));
		B->ExecJs(FString::Printf(
			TEXT("if(typeof onCrewPlanDraft==='function')onCrewPlanDraft('%s','%s',%s)"),
			*EscRun, *Esc, (bSuccess && !LastText.IsEmpty()) ? TEXT("true") : TEXT("false")));
	});

	if (TUniquePtr<FCrewDraftInFlight>* Slot = ActiveDrafts.Find(RunId))
	{
		(*Slot)->AcpHandle = TurnHandle;
	}

	Arch.SendMessage(CrewBuildPlanPrompt(GoalText, MinCheckpoints, MaxCheckpoints));
}

void UUECPAppBridge::CrewDraftPlanFromGoal(const FString& RunId, const FString& GoalText,
	int32 MinCheckpoints, int32 MaxCheckpoints)
{
	auto W = OwnerWidget.Pin();
	if (!W.IsValid()) return;
	if (GoalText.TrimStartAndEnd().IsEmpty())
	{
		PushToast(TEXT("Goal can't be empty"), TEXT("warn"));
		return;
	}

	FApiKeyManager& Keys = FApiKeyManager::Get();
	const FApiKeySlot ActiveSlot = Keys.GetActiveSlot();

	auto IsApiProvider = [](const FString& P)
	{
		return P == TEXT("Claude") || P == TEXT("Anthropic") || P == TEXT("OpenAI")
		    || P == TEXT("DeepSeek") || P == TEXT("Gemini") || P == TEXT("Custom");
	};
	auto IsAcpProvider = [](const FString& P)
	{
		return P == TEXT("Claude Agent") || P == TEXT("Codex")
		    || P == TEXT("Copilot")       || P == TEXT("Gemini Agent");
	};

	const FString JsRunId = RunId.Replace(TEXT("'"), TEXT("\\'"));
	ExecJs(FString::Printf(TEXT("if(typeof onCrewPlanDraftStarted==='function')onCrewPlanDraftStarted('%s')"), *JsRunId));

	bool bUseFreeTier = (ActiveSlot.Provider == TEXT("Free"));

	if (IsAcpProvider(ActiveSlot.Provider))
	{
		const bool bFreeAvailable = !FFreeTierConfigManager::Get().IsBlocked()
			&& !FFreeTierConfigManager::Get().GetActiveSlotEndpoint().IsEmpty();
		if (bFreeAvailable)
		{
			bUseFreeTier = true;
		}
		else
		{
			CrewDraftPlanFromGoal_ViaArchitect(RunId, GoalText, MinCheckpoints, MaxCheckpoints);
			return;
		}
	}

	FApiKeySlot UseSlot = ActiveSlot;

	if (!bUseFreeTier && (!IsApiProvider(UseSlot.Provider) || UseSlot.ApiKey.IsEmpty()))
	{
		bool bFound = false;
		for (const FApiKeySlot& S : Keys.GetAllSlots())
		{
			if (IsApiProvider(S.Provider) && !S.ApiKey.IsEmpty())
			{
				UseSlot = S;
				bFound = true;
				break;
			}
		}
		if (!bFound)
		{
			PushToast(TEXT("No API key or Free tier slot available for drafting"), TEXT("warn"));
			ExecJs(FString::Printf(TEXT("if(typeof onCrewPlanDraftFailed==='function')onCrewPlanDraftFailed('%s','no-credentials')"), *JsRunId));
			return;
		}
	}

	FString Provider, ApiKey, BaseURL;
	if (bUseFreeTier)
	{
		if (FFreeTierConfigManager::Get().IsBlocked())
		{
			PushToast(FFreeTierConfigManager::Get().GetBlockMessage(), TEXT("warn"));
			ExecJs(FString::Printf(TEXT("if(typeof onCrewPlanDraftFailed==='function')onCrewPlanDraftFailed('%s','free-tier-blocked')"), *JsRunId));
			return;
		}
		Provider = TEXT("Custom");
		ApiKey   = FFreeTierConfigManager::Get().GetServiceRegistrationKey();
		BaseURL  = FFreeTierConfigManager::Get().GetActiveSlotEndpoint();
		UseSlot.CustomModelName = FFreeTierConfigManager::Get().GetActiveSlotModel();
	}
	else
	{
		Provider = UseSlot.Provider;
		ApiKey   = UseSlot.ApiKey;
		BaseURL  = UseSlot.CustomBaseURL;
	}

	FString Url;
	FString Body;
	TArray<TPair<FString, FString>> Headers;

	const FString Prompt = CrewBuildPlanPrompt(GoalText, MinCheckpoints, MaxCheckpoints);

	enum class EParseShape : uint8 { Anthropic, OpenAICompat, Gemini };
	EParseShape ParseShape = EParseShape::OpenAICompat;

	auto WriteJsonString = [](const FString& In)
	{
		FString Out;
		Out.Reserve(In.Len() + 8);
		for (TCHAR C : In)
		{
			switch (C)
			{
			case '\\': Out += TEXT("\\\\"); break;
			case '"':  Out += TEXT("\\\""); break;
			case '\n': Out += TEXT("\\n");  break;
			case '\r': Out += TEXT("\\r");  break;
			case '\t': Out += TEXT("\\t");  break;
			default:
				if (C < 0x20) Out += FString::Printf(TEXT("\\u%04x"), (int32)C);
				else          Out.AppendChar(C);
			}
		}
		return Out;
	};
	const FString PromptEsc = WriteJsonString(Prompt);

	if (Provider == TEXT("Claude") || Provider == TEXT("Anthropic"))
	{
		Url = FHttpCommunicationManager::BuildClaudeUrl();
		const FString Model = UseSlot.ClaudeModel;
		if (Model.IsEmpty())
		{
			PushToast(TEXT("No Claude model set on slot — pick one in Settings"), TEXT("warn"));
			ExecJs(FString::Printf(TEXT("if(typeof onCrewPlanDraftFailed==='function')onCrewPlanDraftFailed('%s','no-model')"), *JsRunId));
			return;
		}
		Body = FString::Printf(
			TEXT("{\"model\":\"%s\",\"max_tokens\":2048,\"messages\":[{\"role\":\"user\",\"content\":\"%s\"}]}"),
			*Model, *PromptEsc);
		Headers.Add({ TEXT("x-api-key"), ApiKey });
		Headers.Add({ TEXT("anthropic-version"), TEXT("2023-06-01") });
		Headers.Add({ TEXT("Content-Type"), TEXT("application/json") });
		ParseShape = EParseShape::Anthropic;
	}
	else if (Provider == TEXT("Gemini"))
	{
		const FString Model = UseSlot.GeminiModel;
		if (Model.IsEmpty())
		{
			PushToast(TEXT("No Gemini model set on slot — pick one in Settings"), TEXT("warn"));
			ExecJs(FString::Printf(TEXT("if(typeof onCrewPlanDraftFailed==='function')onCrewPlanDraftFailed('%s','no-model')"), *JsRunId));
			return;
		}
		Url = FHttpCommunicationManager::BuildGeminiUrl(Model, ApiKey);
		Body = FString::Printf(
			TEXT("{\"contents\":[{\"role\":\"user\",\"parts\":[{\"text\":\"%s\"}]}]}"),
			*PromptEsc);
		Headers.Add({ TEXT("Content-Type"), TEXT("application/json") });
		ParseShape = EParseShape::Gemini;
	}
	else
	{
		FString Model;
		if (Provider == TEXT("OpenAI"))
		{
			Url = FHttpCommunicationManager::BuildOpenAIUrl();
			Model = UseSlot.OpenAIModel;
		}
		else if (Provider == TEXT("DeepSeek"))
		{
			Url = FHttpCommunicationManager::BuildDeepSeekUrl();
			Model = UseSlot.DeepSeekModel;
		}
		else
		{
			Url = FHttpCommunicationManager::BuildCustomUrl(BaseURL);
			Model = UseSlot.CustomModelName;
		}
		if (Model.IsEmpty())
		{
			PushToast(FString::Printf(TEXT("No %s model set on slot — pick one in Settings"), *Provider), TEXT("warn"));
			ExecJs(FString::Printf(TEXT("if(typeof onCrewPlanDraftFailed==='function')onCrewPlanDraftFailed('%s','no-model')"), *JsRunId));
			return;
		}
		Body = FString::Printf(
			TEXT("{\"model\":\"%s\",\"messages\":[{\"role\":\"user\",\"content\":\"%s\"}],\"response_format\":{\"type\":\"json_object\"}}"),
			*Model, *PromptEsc);
		Headers.Add({ TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *ApiKey) });
		Headers.Add({ TEXT("Content-Type"), TEXT("application/json") });
		ParseShape = EParseShape::OpenAICompat;
	}

	if (Url.IsEmpty())
	{
		PushToast(TEXT("Couldn't build a request URL — provider config missing"), TEXT("warn"));
		ExecJs(FString::Printf(TEXT("if(typeof onCrewPlanDraftFailed==='function')onCrewPlanDraftFailed('%s','no-url')"), *JsRunId));
		return;
	}

	CancelInFlightDraft(RunId, false);

	TWeakObjectPtr<UUECPAppBridge> WeakSelf(this);
	const FString CapturedRunId = RunId;
	const uint8 CapturedShape = (uint8)ParseShape;

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = FHttpModule::Get().CreateRequest();
	Req->SetVerb(TEXT("POST"));
	Req->SetURL(Url);
	for (const TPair<FString, FString>& H : Headers) Req->SetHeader(H.Key, H.Value);
	Req->SetContentAsString(Body);

	{
		TUniquePtr<FCrewDraftInFlight> Tracker = MakeUnique<FCrewDraftInFlight>();
		Tracker->RunId = RunId;
		Tracker->bIsAcp = false;
		Tracker->HttpReq = Req;
		ActiveDrafts.Add(RunId, MoveTemp(Tracker));
	}

	Req->OnProcessRequestComplete().BindLambda(
		[WeakSelf, CapturedRunId, CapturedShape](FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasOk)
	{
		UUECPAppBridge* B = WeakSelf.Get();
		if (!B) return;

		TUniquePtr<FCrewDraftInFlight>* Slot = B->ActiveDrafts.Find(CapturedRunId);
		if (!Slot || !Slot->IsValid() || (*Slot)->HttpReq.Pin() != Request)
		{
			UE_LOG(LogTemp, Log, TEXT("CrewDraftPlanFromGoal: dropping stale HTTP response (runId=%s)"), *CapturedRunId);
			return;
		}
		B->ActiveDrafts.Remove(CapturedRunId);

		auto PushResult = [B, &CapturedRunId](const FString& Text, bool bOk)
		{
			FString Esc = Text;
			Esc.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
			Esc.ReplaceInline(TEXT("'"),  TEXT("\\'"));
			Esc.ReplaceInline(TEXT("\n"), TEXT("\\n"));
			Esc.ReplaceInline(TEXT("\r"), TEXT("\\r"));
			FString EscRun = CapturedRunId; EscRun.ReplaceInline(TEXT("'"), TEXT("\\'"));
			B->ExecJs(FString::Printf(
				TEXT("if(typeof onCrewPlanDraft==='function')onCrewPlanDraft('%s','%s',%s)"),
				*EscRun, *Esc, bOk ? TEXT("true") : TEXT("false")));
		};

		if (!bWasOk || !Response.IsValid())
		{
			UE_LOG(LogTemp, Warning, TEXT("CrewDraftPlanFromGoal: HTTP failed"));
			PushResult(TEXT(""), false);
			return;
		}
		const int32 Code = Response->GetResponseCode();
		const FString RawBody = Response->GetContentAsString();
		if (Code < 200 || Code >= 300)
		{
			UE_LOG(LogTemp, Warning, TEXT("CrewDraftPlanFromGoal: HTTP %d — %s"), Code, *RawBody.Left(500));
			PushResult(FString::Printf(TEXT("[HTTP %d]\n%s"), Code, *RawBody), false);
			return;
		}

		TSharedPtr<FJsonObject> Root;
		TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(RawBody);
		if (!FJsonSerializer::Deserialize(R, Root) || !Root.IsValid())
		{
			UE_LOG(LogTemp, Warning, TEXT("CrewDraftPlanFromGoal: response not JSON"));
			PushResult(RawBody, false);
			return;
		}

		FString Text;
		const uint8 Shape = CapturedShape;
		if (Shape == 0)
		{
			const TArray<TSharedPtr<FJsonValue>>* Content = nullptr;
			if (Root->TryGetArrayField(TEXT("content"), Content) && Content)
			{
				for (const TSharedPtr<FJsonValue>& V : *Content)
				{
					if (!V.IsValid() || V->Type != EJson::Object) continue;
					FString T;
					V->AsObject()->TryGetStringField(TEXT("text"), T);
					if (!T.IsEmpty()) Text += T;
				}
			}
		}
		else if (Shape == 2)
		{
			const TArray<TSharedPtr<FJsonValue>>* Candidates = nullptr;
			if (Root->TryGetArrayField(TEXT("candidates"), Candidates) && Candidates && Candidates->Num() > 0)
			{
				TSharedPtr<FJsonObject> First = (*Candidates)[0]->AsObject();
				const TSharedPtr<FJsonObject>* ContentObj = nullptr;
				if (First.IsValid() && First->TryGetObjectField(TEXT("content"), ContentObj) && ContentObj)
				{
					const TArray<TSharedPtr<FJsonValue>>* Parts = nullptr;
					if ((*ContentObj)->TryGetArrayField(TEXT("parts"), Parts) && Parts)
					{
						for (const TSharedPtr<FJsonValue>& V : *Parts)
						{
							if (!V.IsValid() || V->Type != EJson::Object) continue;
							FString T;
							V->AsObject()->TryGetStringField(TEXT("text"), T);
							if (!T.IsEmpty()) Text += T;
						}
					}
				}
			}
		}
		else
		{
			const TArray<TSharedPtr<FJsonValue>>* Choices = nullptr;
			if (Root->TryGetArrayField(TEXT("choices"), Choices) && Choices && Choices->Num() > 0)
			{
				TSharedPtr<FJsonObject> First = (*Choices)[0]->AsObject();
				const TSharedPtr<FJsonObject>* MsgObj = nullptr;
				if (First.IsValid() && First->TryGetObjectField(TEXT("message"), MsgObj) && MsgObj)
				{
					(*MsgObj)->TryGetStringField(TEXT("content"), Text);
				}
			}
		}

		UE_LOG(LogTemp, Log, TEXT("CrewDraftPlanFromGoal: parsed %d chars from %s response"),
			Text.Len(), Shape == 0 ? TEXT("Anthropic") : (Shape == 2 ? TEXT("Gemini") : TEXT("OpenAI-compat")));
		PushResult(Text, !Text.IsEmpty());
	});

	Req->ProcessRequest();
}

void UUECPAppBridge::CrewOpenPlanEditor(const FString& RunId)
{
	PushToast(TEXT("Inline plan editor coming next — for now the smoke test plan is preset"), TEXT("info"));
}

void UUECPAppBridge::SendCrewSetPlanResult(const FString& RunId, bool bOk, const TArray<FString>& Errors)
{
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("runId"), RunId);
	Payload->SetBoolField(TEXT("ok"), bOk);
	TArray<TSharedPtr<FJsonValue>> ErrArr;
	for (const FString& E : Errors) ErrArr.Add(MakeShared<FJsonValueString>(E));
	Payload->SetArrayField(TEXT("errors"), ErrArr);
	FString Json;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
	FJsonSerializer::Serialize(Payload.ToSharedRef(), Writer);
	ExecJs(FString::Printf(TEXT("if (window.onCrewSetPlanResult) onCrewSetPlanResult(%s);"), *Json));
}

void UUECPAppBridge::CrewSetRunPlan(const FString& RunId, const FString& PlanJson)
{
	FGuid Parsed;
	if (!FGuid::Parse(RunId, Parsed)) return;
	TArray<TSharedPtr<FJsonValue>> Arr;
	TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(PlanJson);
	if (!FJsonSerializer::Deserialize(R, Arr)) return;

	TArray<FCrewCheckpoint> Plan;
	for (const TSharedPtr<FJsonValue>& V : Arr)
	{
		if (!V.IsValid() || V->Type != EJson::Object) continue;
		TSharedPtr<FJsonObject> O = V->AsObject();
		FCrewCheckpoint C;
		O->TryGetStringField(TEXT("id"),              C.CheckpointId);
		O->TryGetStringField(TEXT("description"),     C.Description);
		O->TryGetStringField(TEXT("successCriteria"), C.SuccessCriteria);
		const TArray<TSharedPtr<FJsonValue>>* Reqs = nullptr;
		if (O->TryGetArrayField(TEXT("requiredRoleIds"), Reqs) && Reqs)
		{
			for (const TSharedPtr<FJsonValue>& RV : *Reqs)
			{
				if (RV.IsValid()) C.RequiredRoleIds.Add(RV->AsString());
			}
		}
		const TArray<TSharedPtr<FJsonValue>>* Deps = nullptr;
		if (O->TryGetArrayField(TEXT("dependsOn"), Deps) && Deps)
		{
			for (const TSharedPtr<FJsonValue>& DV : *Deps)
			{
				if (DV.IsValid()) C.DependsOn.Add(DV->AsString());
			}
		}
		int32 TmInt = 0;
		if (O->TryGetNumberField(TEXT("timeoutMinutes"), TmInt))
			C.TimeoutMinutes = FMath::Max(0, TmInt);
		const TArray<TSharedPtr<FJsonValue>>* Tools = nullptr;
		if (O->TryGetArrayField(TEXT("toolAllowlistOverride"), Tools) && Tools)
		{
			for (const TSharedPtr<FJsonValue>& TV : *Tools)
			{
				if (TV.IsValid()) C.ToolAllowlistOverride.Add(TV->AsString());
			}
		}
		Plan.Add(C);
	}

	TArray<FString> Errors;
	if (!IUECPCoreModule::Get().GetCrewService().SetRunPlan(Parsed, Plan, &Errors))
	{
		if (Errors.Num() > 0) SendCrewSetPlanResult(RunId, false, Errors);
		else                  SendCrewSetPlanResult(RunId, false, { TEXT("plan rejected — see editor log for details") });
		return;
	}
	SendCrewSetPlanResult(RunId, true, {});
}

void UUECPAppBridge::CrewAppendRunCheckpoints(const FString& RunId, const FString& CheckpointsJson)
{
	FGuid Parsed;
	if (!FGuid::Parse(RunId, Parsed)) return;

	FCrewRun Existing;
	if (!IUECPCoreModule::Get().GetCrewService().GetRun(Parsed, Existing)) return;

	TArray<TSharedPtr<FJsonValue>> Incoming;
	TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(CheckpointsJson);
	if (!FJsonSerializer::Deserialize(R, Incoming)) return;

	TArray<FCrewCheckpoint> Merged = Existing.Plan;

	TSet<FString> TakenIds;
	for (const FCrewCheckpoint& C : Merged) TakenIds.Add(C.CheckpointId);

	TArray<FString> DefaultRequiredRoles;
	for (const FCrewRole& Role : Existing.Roles)
	{
		if (!Role.bIsOrchestrator) DefaultRequiredRoles.Add(Role.RoleId);
	}

	int32 NextN = Merged.Num() + 1;
	int32 AddedCount = 0;
	for (const TSharedPtr<FJsonValue>& V : Incoming)
	{
		if (!V.IsValid() || V->Type != EJson::Object) continue;
		TSharedPtr<FJsonObject> O = V->AsObject();

		FCrewCheckpoint C;
		O->TryGetStringField(TEXT("id"),              C.CheckpointId);
		O->TryGetStringField(TEXT("description"),     C.Description);
		O->TryGetStringField(TEXT("successCriteria"), C.SuccessCriteria);
		C.CheckpointId = C.CheckpointId.TrimStartAndEnd();
		C.Description = C.Description.TrimStartAndEnd();
		C.SuccessCriteria = C.SuccessCriteria.TrimStartAndEnd();
		C.RequiredRoleIds = DefaultRequiredRoles;

		if (C.CheckpointId.IsEmpty() || TakenIds.Contains(C.CheckpointId))
		{
			FString Candidate = FString::Printf(TEXT("cp_%d"), NextN);
			while (TakenIds.Contains(Candidate)) { ++NextN; Candidate = FString::Printf(TEXT("cp_%d"), NextN); }
			C.CheckpointId = Candidate;
		}
		TakenIds.Add(C.CheckpointId);
		Merged.Add(C);
		++AddedCount;
	}

	if (AddedCount == 0)
	{
		SendCrewSetPlanResult(RunId, false, { TEXT("draft contained no usable checkpoints") });
		return;
	}

	TArray<FString> Errors;
	if (!IUECPCoreModule::Get().GetCrewService().SetRunPlan(Parsed, Merged, &Errors))
	{
		if (Errors.Num() > 0) SendCrewSetPlanResult(RunId, false, Errors);
		else                  SendCrewSetPlanResult(RunId, false, { TEXT("plan rejected — see editor log for details") });
		return;
	}
	SendCrewSetPlanResult(RunId, true, {});
}

void UUECPAppBridge::CrewSetRunCaps(const FString& RunId, const FString& CapsJson)
{
	FGuid Parsed;
	if (!FGuid::Parse(RunId, Parsed)) return;

	TSharedPtr<FJsonObject> O;
	TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(CapsJson);
	if (!FJsonSerializer::Deserialize(R, O) || !O.IsValid()) return;

	FCrewRun Existing;
	if (!IUECPCoreModule::Get().GetCrewService().GetRun(Parsed, Existing)) return;
	FCrewRunCaps Caps = Existing.Caps;

	int32 I = 0; bool B = false;
	if (O->TryGetNumberField(TEXT("maxTurns"),               I)) Caps.MaxTotalTurns                = FMath::Max(1, I);
	if (O->TryGetNumberField(TEXT("maxWallMinutes"),         I)) Caps.MaxWallMinutes               = FMath::Max(0, I);
	if (O->TryGetNumberField(TEXT("maxConsecErrors"),        I)) Caps.MaxConsecutiveErrors         = FMath::Max(0, I);
	if (O->TryGetNumberField(TEXT("maxConsecErrorsPerRole"), I)) Caps.MaxConsecutiveErrorsPerRole  = FMath::Max(0, I);
	if (O->TryGetBoolField  (TEXT("autoApproveDestructive"), B)) Caps.bAutoApproveDestructive      = B;
	if (O->TryGetNumberField(TEXT("maxRetriesPerCheckpoint"), I)) Caps.MaxRetriesPerCheckpoint    = FMath::Max(0, I);
	{
		double D = 0.0;
		if (O->TryGetNumberField(TEXT("maxTotalTokens"), D)) Caps.MaxTotalTokens = (int64)FMath::Max(0.0, D);
	}

	IUECPCoreModule::Get().GetCrewService().SetRunCaps(Parsed, Caps);
}

void UUECPAppBridge::CrewSetRoleSlot(const FString& RunId, const FString& RoleId, int32 SlotIndex)
{
	FGuid Parsed;
	if (!FGuid::Parse(RunId, Parsed)) return;
	if (RoleId.IsEmpty()) return;
	IUECPCoreModule::Get().GetCrewService().SetRoleApiKeySlot(Parsed, RoleId, SlotIndex);
}

void UUECPAppBridge::CrewAnswerEscalation(const FString& RunId, const FString& AnswerText)
{
	FGuid Parsed;
	if (!FGuid::Parse(RunId, Parsed)) return;
	IUECPCoreModule::Get().GetCrewService().AnswerEscalation(Parsed, AnswerText);
}

void UUECPAppBridge::CrewRetryCheckpoint(const FString& RunId, const FString& CheckpointId)
{
	FGuid Parsed;
	if (!FGuid::Parse(RunId, Parsed)) return;
	if (CheckpointId.IsEmpty()) return;
	IUECPCoreModule::Get().GetCrewService().RetryCheckpoint(Parsed, CheckpointId);
}

void UUECPAppBridge::CrewForkRun(const FString& SourceRunId, const FString& NewDisplayName)
{
	FGuid Parsed;
	if (!FGuid::Parse(SourceRunId, Parsed)) return;
	const FGuid NewId = IUECPCoreModule::Get().GetCrewService().ForkRun(Parsed, NewDisplayName);
	if (NewId.IsValid())
	{
		ExecJs(FString::Printf(
			TEXT("if (window.crewSelectRun) crewSelectRun('%s');"),
			*NewId.ToString(EGuidFormats::DigitsWithHyphens)));
	}
}

void UUECPAppBridge::CrewListAllTools()
{
	IUECPExtensionService* ExtSvc = IUECPCoreModule::IsAvailable()
		? &IUECPCoreModule::Get().GetExtensionService() : nullptr;
	IUECPToolDispatcher& Dispatcher = IUECPCoreModule::Get().GetToolDispatcher();

	struct FToolEntry { FString Name; FString Description; bool bIsUmbrella; };
	TMap<FString, TArray<FToolEntry>> ByGroup;
	TSet<FName> Claimed;

	if (ExtSvc)
	{
		for (const FUECPExtensionDescriptor& E : ExtSvc->GetExtensions())
		{
			if (ExtSvc->GetExtensionState(E.ExtensionId) != EUECPExtensionState::Loaded) continue;

			const FString Group = E.DisplayName.ToString();
			TArray<FToolEntry>& Bucket = ByGroup.FindOrAdd(Group);

			for (const FName& U : E.OwnedUmbrellas)
			{
				Bucket.Add({ U.ToString(), FString(), true });
				Claimed.Add(U);
			}
			for (const FName& T : E.OwnedTools)
			{
				Bucket.Add({ T.ToString(), FString(), false });
				Claimed.Add(T);
			}
		}
	}

	const TArray<FName> AllRegistered = Dispatcher.ListTools();
	TArray<FToolEntry>& CoreBucket = ByGroup.FindOrAdd(TEXT("Core"));
	for (const FName& N : AllRegistered)
	{
		if (Claimed.Contains(N)) continue;
		const FString S = N.ToString();
		if (S.Contains(TEXT("."))) continue;
		CoreBucket.Add({ S, FString(), false });
	}

	for (auto It = ByGroup.CreateIterator(); It; ++It)
	{
		if (It->Value.Num() == 0) It.RemoveCurrent();
	}

	TArray<FString> GroupKeys;
	ByGroup.GetKeys(GroupKeys);
	GroupKeys.Sort();
	if (int32 CoreIdx = GroupKeys.IndexOfByKey(TEXT("Core")); CoreIdx > 0)
	{
		GroupKeys.Swap(0, CoreIdx);
	}

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> Groups;
	for (const FString& G : GroupKeys)
	{
		TArray<FToolEntry>& Entries = ByGroup[G];
		Entries.Sort([](const FToolEntry& A, const FToolEntry& B) {
			if (A.bIsUmbrella != B.bIsUmbrella) return A.bIsUmbrella;
			return A.Name < B.Name;
		});

		TSharedPtr<FJsonObject> GO = MakeShared<FJsonObject>();
		GO->SetStringField(TEXT("umbrella"), G);
		TArray<TSharedPtr<FJsonValue>> ToolArr;
		for (const FToolEntry& T : Entries)
		{
			TSharedPtr<FJsonObject> TO = MakeShared<FJsonObject>();
			TO->SetStringField(TEXT("name"),        T.Name);
			TO->SetStringField(TEXT("description"), T.Description);
			TO->SetBoolField  (TEXT("isUmbrella"),  T.bIsUmbrella);
			ToolArr.Add(MakeShared<FJsonValueObject>(TO));
		}
		GO->SetArrayField(TEXT("tools"), ToolArr);
		Groups.Add(MakeShared<FJsonValueObject>(GO));
	}
	Root->SetArrayField(TEXT("groups"), Groups);

	FString OutStr;
	TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> W =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&OutStr);
	FJsonSerializer::Serialize(Root, W);

	FString Esc = OutStr;
	Esc.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
	Esc.ReplaceInline(TEXT("'"),  TEXT("\\'"));
	Esc.ReplaceInline(TEXT("\n"), TEXT("\\n"));
	Esc.ReplaceInline(TEXT("\r"), TEXT("\\r"));
	ExecJs(FString::Printf(TEXT("if(typeof onCrewTools==='function')onCrewTools('%s')"), *Esc));
}

void UUECPAppBridge::CrewSetRoleAllowlist(const FString& RunId, const FString& RoleId,
	const FString& Scope, const FString& AllowlistJson)
{
	TArray<FString> Allowlist;
	{
		TArray<TSharedPtr<FJsonValue>> Arr;
		TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(AllowlistJson);
		if (FJsonSerializer::Deserialize(R, Arr))
		{
			for (const TSharedPtr<FJsonValue>& V : Arr)
			{
				if (V.IsValid() && V->Type == EJson::String) Allowlist.Add(V->AsString());
			}
		}
	}

	IUECPCrewService& Crew = IUECPCoreModule::Get().GetCrewService();

	if (Scope == TEXT("run"))
	{
		FGuid Parsed;
		if (!FGuid::Parse(RunId, Parsed)) return;
		Crew.SetRoleAllowlistOnRun(Parsed, RoleId, Allowlist);
		return;
	}

	if (Scope == TEXT("template"))
	{
		FCrewTemplate Tpl;
		if (!Crew.GetTemplate(RunId, Tpl)) return;
		for (FCrewRole& R : Tpl.Roles)
		{
			if (R.RoleId == RoleId) { R.ToolAllowlist = Allowlist; break; }
		}
		Crew.UpdateTemplate(Tpl);
	}
}

void UUECPAppBridge::CrewGetRunReport(const FString& RunId)
{
	FGuid RunGuid;
	if (!FGuid::Parse(RunId, RunGuid)) return;

	FCrewRun Run;
	if (!IUECPCoreModule::Get().GetCrewService().GetRun(RunGuid, Run)) return;

	TMap<FString, FString> NameById;
	for (const FCrewRole& R : Run.Roles) NameById.Add(R.RoleId, R.Name);

	auto FormatTime = [](const FDateTime& T)
	{
		return (T.GetTicks() <= 0) ? FString(TEXT("—")) : T.ToString();
	};
	auto FormatDuration = [](const FTimespan& D)
	{
		const int32 Hours = (int32)D.GetTotalHours();
		const int32 Mins  = D.GetMinutes();
		const int32 Secs  = D.GetSeconds();
		return Hours > 0
			? FString::Printf(TEXT("%dh %02dm %02ds"), Hours, Mins, Secs)
			: FString::Printf(TEXT("%dm %02ds"), Mins, Secs);
	};

	const FDateTime EndTime = (Run.EndedAt.GetTicks() > 0) ? Run.EndedAt : FDateTime::UtcNow();
	const FTimespan Elapsed = EndTime - Run.StartedAt;

	FString Md;
	Md += FString::Printf(TEXT("# %s\n\n"), *Run.DisplayName);
	Md += FString::Printf(TEXT("**State:** %s%s  \n"),
		*UECPCrew::RunStateToString(Run.State),
		Run.PauseReason.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" (%s)"), *Run.PauseReason));
	Md += FString::Printf(TEXT("**Started:** %s  \n"), *FormatTime(Run.StartedAt));
	Md += FString::Printf(TEXT("**Ended:** %s  \n"),   *FormatTime(Run.EndedAt));
	Md += FString::Printf(TEXT("**Duration:** %s  \n"), *FormatDuration(Elapsed));
	Md += FString::Printf(TEXT("**Turns:** %d / %d  \n"), Run.TotalTurnsTaken, Run.Caps.MaxTotalTurns);
	if (Run.ConsecutiveErrorCount > 0)
	{
		Md += FString::Printf(TEXT("**Final error streak:** %d\n"), Run.ConsecutiveErrorCount);
	}
	Md += TEXT("\n## Plan outcome\n\n");
	for (const FCrewCheckpoint& C : Run.Plan)
	{
		const TCHAR* Icon = TEXT("[ ]");
		if      (C.State == ECheckpointState::Passed)     Icon = TEXT("[x]");
		else if (C.State == ECheckpointState::Failed)     Icon = TEXT("[!]");
		else if (C.State == ECheckpointState::InProgress) Icon = TEXT("[>]");
		else if (C.State == ECheckpointState::Skipped)    Icon = TEXT("[-]");
		Md += FString::Printf(TEXT("- %s **%s** — %s\n"), Icon, *C.CheckpointId, *C.Description);
		if (!C.ResultSummary.IsEmpty())
		{
			Md += FString::Printf(TEXT("  - Result: %s\n"), *C.ResultSummary);
		}
	}

	Md += TEXT("\n## Roles\n\n");
	for (const FCrewRole& R : Run.Roles)
	{
		int32 InstrSent = 0, InstrReceived = 0, ReportSent = 0, QSent = 0;
		for (const FCrewHandoff& H : Run.Handoffs)
		{
			if (H.Type == EHandoffType::Instruction && H.FromRoleId == R.RoleId) ++InstrSent;
			if (H.Type == EHandoffType::Instruction && H.ToRoleId   == R.RoleId) ++InstrReceived;
			if (H.Type == EHandoffType::Result      && H.FromRoleId == R.RoleId) ++ReportSent;
			if (H.Type == EHandoffType::Question    && H.FromRoleId == R.RoleId) ++QSent;
		}
		Md += FString::Printf(TEXT("- **%s** (%s%s): dispatched %d, received %d, reported %d, asked %d question(s)\n"),
			*R.Name,
			*UECPCrew::RoleKindToString(R.Kind),
			R.bIsOrchestrator ? TEXT(", orch") : TEXT(""),
			InstrSent, InstrReceived, ReportSent, QSent);
	}

	Md += FString::Printf(TEXT("\n## Handoff log (%d entries)\n\n"), Run.Handoffs.Num());
	int32 Idx = 0;
	for (const FCrewHandoff& H : Run.Handoffs)
	{
		++Idx;
		const FString From = NameById.Contains(H.FromRoleId) ? NameById[H.FromRoleId] : H.FromRoleId;
		const FString To   = NameById.Contains(H.ToRoleId)   ? NameById[H.ToRoleId]   : H.ToRoleId;
		const FString Type = UECPCrew::HandoffTypeToString(H.Type);
		FString Preview = H.Content;
		Preview.ReplaceInline(TEXT("\n"), TEXT(" "));
		if (Preview.Len() > 240) Preview = Preview.Left(240) + TEXT("…");
		Md += FString::Printf(TEXT("%d. **%s → %s** [%s]%s — %s\n"),
			Idx, *From, *To, *Type,
			H.CheckpointId.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" (%s)"), *H.CheckpointId),
			*Preview);
	}

	FString Esc = Md;
	Esc.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
	Esc.ReplaceInline(TEXT("'"),  TEXT("\\'"));
	Esc.ReplaceInline(TEXT("\n"), TEXT("\\n"));
	Esc.ReplaceInline(TEXT("\r"), TEXT("\\r"));
	ExecJs(FString::Printf(TEXT("if(typeof onCrewRunReport==='function')onCrewRunReport('%s')"), *Esc));
}

void UUECPAppBridge::CrewGetHandoff(const FString& RunId, const FString& HandoffId)
{
	FGuid RunGuid;
	FGuid HandoffGuid;
	if (!FGuid::Parse(RunId, RunGuid)) return;
	if (!FGuid::Parse(HandoffId, HandoffGuid)) return;

	FCrewRun Run;
	if (!IUECPCoreModule::Get().GetCrewService().GetRun(RunGuid, Run)) return;

	const FCrewHandoff* Found = nullptr;
	for (const FCrewHandoff& H : Run.Handoffs)
	{
		if (H.Id == HandoffGuid) { Found = &H; break; }
	}
	if (!Found) return;

	TMap<FString, FString> RoleNameById;
	for (const FCrewRole& Role : Run.Roles) RoleNameById.Add(Role.RoleId, Role.Name);

	TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetStringField(TEXT("id"),           Found->Id.ToString(EGuidFormats::DigitsWithHyphens));
	const FString* FromName = RoleNameById.Find(Found->FromRoleId);
	const FString* ToName   = RoleNameById.Find(Found->ToRoleId);
	O->SetStringField(TEXT("from"),         FromName ? *FromName : Found->FromRoleId);
	O->SetStringField(TEXT("to"),           ToName   ? *ToName   : Found->ToRoleId);
	O->SetStringField(TEXT("fromRoleId"),   Found->FromRoleId);
	O->SetStringField(TEXT("toRoleId"),     Found->ToRoleId);
	O->SetStringField(TEXT("type"),         UECPCrew::HandoffTypeToString(Found->Type));
	O->SetStringField(TEXT("checkpointId"), Found->CheckpointId);
	O->SetStringField(TEXT("content"),      Found->Content);
	O->SetNumberField(TEXT("timestamp"),    (double)Found->TimestampUnix);

	FString OutStr;
	TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> W =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&OutStr);
	FJsonSerializer::Serialize(O, W);

	FString Esc = OutStr;
	Esc.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
	Esc.ReplaceInline(TEXT("'"),  TEXT("\\'"));
	Esc.ReplaceInline(TEXT("\n"), TEXT("\\n"));
	Esc.ReplaceInline(TEXT("\r"), TEXT("\\r"));
	ExecJs(FString::Printf(TEXT("if(typeof onCrewHandoff==='function')onCrewHandoff('%s')"), *Esc));
}


// ═══════════════════════════════════════════════════════════════════════════
// Axivor AI — in-chat model picker
// The composer chip lets the user pick the provider profile (slot), and for ACP
// agents the model + agent options (effort, fast mode, ...) without opening
// Settings. Persistence reuses the exact keys Settings writes:
//   FApiKeyManager slots            → active/default profile, per-chat override
//   [BpGeneratorUltimate.ACP.SlotN] → <agent>.Model / <agent>.Option.<id>
// ═══════════════════════════════════════════════════════════════════════════
static FString AxPickerSlotSection(int32 SlotIndex)
{
	return FString::Printf(TEXT("BpGeneratorUltimate.ACP.Slot%d"), SlotIndex);
}

// Free function: cannot touch SUECPMainWidget privates, so the caller (a friend) passes the values in.
static int32 AxPickerResolveSlot(const FString& ChatId, const TMap<FString, int32>& SlotByChat)
{
	if (!ChatId.IsEmpty())
	{
		if (const int32* PerChat = SlotByChat.Find(ChatId))
		{
			if (*PerChat >= 0 && *PerChat < MAX_API_KEY_SLOTS) return *PerChat;
		}
	}
	return FApiKeyManager::Get().GetActiveSlotIndex();
}

void UUECPAppBridge::RequestModelPicker()
{
	PushModelPicker();
}

void UUECPAppBridge::PushModelPicker()
{
	auto W = OwnerWidget.Pin();
	if (!W.IsValid()) return;

	const int32 ActiveIdx   = FApiKeyManager::Get().GetActiveSlotIndex();
	const int32 ResolvedIdx = AxPickerResolveSlot(W->ActiveArchitectChatID, W->ArchitectApiKeySlotByChat);
	int32 ChatIdx = -1;
	if (!W->ActiveArchitectChatID.IsEmpty())
	{
		if (const int32* PerChat = W->ArchitectApiKeySlotByChat.Find(W->ActiveArchitectChatID))
			ChatIdx = *PerChat;
	}

	IUECPACPRegistryService& Reg = IUECPCoreModule::Get().GetACPRegistryService();
	TMap<FString, FString> AgentNames;
	for (const FUECPACPAgentEntry& Entry : Reg.GetAgents())
		AgentNames.Add(Entry.Id, Entry.Name);

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("activeSlot"),   ActiveIdx);
	Root->SetNumberField(TEXT("chatSlot"),     ChatIdx);
	Root->SetNumberField(TEXT("resolvedSlot"), ResolvedIdx);
	Root->SetStringField(TEXT("chatId"),       W->ActiveArchitectChatID);

	TArray<TSharedPtr<FJsonValue>> SlotArr;
	TSet<FString> AgentsInUse;
	const TArray<FApiKeySlot> Slots = FApiKeyManager::Get().GetAllSlots();
	for (int32 i = 0; i < Slots.Num(); ++i)
	{
		const FApiKeySlot& S = Slots[i];
		const bool bIsAcp = !S.Provider.IsEmpty() && Reg.IsInstalled(S.Provider);
		const bool bFree  = S.Provider.IsEmpty() || S.Provider.Equals(TEXT("Free"), ESearchCase::IgnoreCase);
		const bool bAnyData = !S.ApiKey.IsEmpty() || !S.Name.IsEmpty() || bIsAcp || !S.CustomModelName.IsEmpty();
		// Slot 0 is always listed (it is the built-in default); other empty slots are hidden.
		if (i != 0 && !bAnyData && bFree) continue;

		FString Kind = bIsAcp ? TEXT("agent") : (bFree ? TEXT("free") : TEXT("api"));
		FString Model;
		if (bIsAcp)
		{
			GConfig->GetString(*AxPickerSlotSection(i), *FString::Printf(TEXT("%s.Model"), *S.Provider), Model, FSettingsManager::GetGlobalConfigPath());
			if (Model.IsEmpty()) Model = S.AgentModel;
			AgentsInUse.Add(S.Provider);
		}
		else if (!bFree)
		{
			Model = S.CustomModelName;
			if (Model.IsEmpty())
			{
				if      (S.Provider == TEXT("Claude"))   Model = S.ClaudeModel;
				else if (S.Provider == TEXT("OpenAI"))   Model = S.OpenAIModel;
				else if (S.Provider == TEXT("Gemini"))   Model = S.GeminiModel;
				else if (S.Provider == TEXT("DeepSeek")) Model = S.DeepSeekModel;
			}
		}

		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetNumberField(TEXT("index"),    i);
		O->SetStringField(TEXT("name"),     S.Name);
		O->SetStringField(TEXT("provider"), S.Provider);
		O->SetStringField(TEXT("kind"),     Kind);
		O->SetStringField(TEXT("model"),    Model);
		O->SetBoolField  (TEXT("hasKey"),   !S.ApiKey.IsEmpty());
		const FString* DisplayName = AgentNames.Find(S.Provider);
		O->SetStringField(TEXT("agentName"), DisplayName ? *DisplayName : S.Provider);
		SlotArr.Add(MakeShared<FJsonValueObject>(O));
	}
	Root->SetArrayField(TEXT("slots"), SlotArr);

	// Installed agents that are not yet attached to any slot — offered as "add" entries.
	TArray<TSharedPtr<FJsonValue>> InstalledArr;
	for (const FUECPACPAgentEntry& Entry : Reg.GetAgents())
	{
		if (!Reg.IsInstalled(Entry.Id)) continue;
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("id"),   Entry.Id);
		O->SetStringField(TEXT("name"), Entry.Name);
		O->SetBoolField  (TEXT("inUse"), AgentsInUse.Contains(Entry.Id));
		InstalledArr.Add(MakeShared<FJsonValueObject>(O));
	}
	Root->SetArrayField(TEXT("installedAgents"), InstalledArr);

	// Agent catalogs (models + option sections) with the values saved for the resolved slot.
	TSharedRef<FJsonObject> Agents = MakeShared<FJsonObject>();
	const FString Section = AxPickerSlotSection(ResolvedIdx);
	auto MakeArr = [](const TArray<FUECPACPConfigOption>& Opts)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		for (const FUECPACPConfigOption& Opt : Opts)
		{
			TSharedRef<FJsonObject> V = MakeShared<FJsonObject>();
			V->SetStringField(TEXT("id"),          Opt.Id);
			V->SetStringField(TEXT("name"),        Opt.Name);
			V->SetStringField(TEXT("description"), Opt.Description);
			Out.Add(MakeShared<FJsonValueObject>(V));
		}
		return Out;
	};
	for (const FUECPACPAgentEntry& Entry : Reg.GetAgents())
	{
		if (!Reg.IsInstalled(Entry.Id)) continue;
		TSharedRef<FJsonObject> A = MakeShared<FJsonObject>();
		A->SetStringField(TEXT("name"), Entry.Name);
		const FUECPACPAgentConfigSnapshot* Snap = W->CachedAgentConfigs.Find(Entry.Id);
		const SUECPMainWidget::FACPAgentAuthState* Auth = W->AgentAuthStates.Find(Entry.Id);
		A->SetBoolField(TEXT("authRequired"), Auth && Auth->bRequired);
		A->SetBoolField(TEXT("discovering"),  !Snap && W->AgentDiscoveryAttempted.Contains(Entry.Id));
		A->SetBoolField(TEXT("ready"),        Snap != nullptr && Snap->Models.Num() > 0);
		if (Snap)
		{
			FString SavedModel;
			GConfig->GetString(*Section, *FString::Printf(TEXT("%s.Model"), *Entry.Id), SavedModel, FSettingsManager::GetGlobalConfigPath());
			A->SetStringField(TEXT("currentModel"), SavedModel.IsEmpty() ? Snap->CurrentModel : SavedModel);
			A->SetArrayField (TEXT("models"), MakeArr(Snap->Models));
			TArray<TSharedPtr<FJsonValue>> SecArr;
			for (const FUECPACPAgentConfigSection& Sec : Snap->OtherSections)
			{
				FString SavedVal;
				GConfig->GetString(*Section, *FString::Printf(TEXT("%s.Option.%s"), *Entry.Id, *Sec.Id), SavedVal, FSettingsManager::GetGlobalConfigPath());
				TSharedRef<FJsonObject> SObj = MakeShared<FJsonObject>();
				SObj->SetStringField(TEXT("id"),           Sec.Id);
				SObj->SetStringField(TEXT("name"),         Sec.Name);
				SObj->SetStringField(TEXT("currentValue"), SavedVal.IsEmpty() ? Sec.CurrentValue : SavedVal);
				SObj->SetArrayField (TEXT("options"),      MakeArr(Sec.Options));
				SecArr.Add(MakeShared<FJsonValueObject>(SObj));
			}
			A->SetArrayField(TEXT("sections"), SecArr);
		}
		Agents->SetObjectField(Entry.Id, A);
	}
	Root->SetObjectField(TEXT("agents"), Agents);

	FString Json;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
	FJsonSerializer::Serialize(Root, Writer);
	Writer->Close();

	const FString Escaped = Json
		.Replace(TEXT("\\"), TEXT("\\\\"))
		.Replace(TEXT("'"),  TEXT("\\'"))
		.Replace(TEXT("\n"), TEXT("\\n"))
		.Replace(TEXT("\r"), TEXT(""));
	ExecJs(FString::Printf(TEXT("if(typeof onModelPicker==='function')onModelPicker('%s')"), *Escaped));
}

void UUECPAppBridge::PickerSelectSlot(int32 SlotIndex, bool bPinToChat)
{
	auto W = OwnerWidget.Pin();
	if (!W.IsValid()) return;
	if (SlotIndex < 0 || SlotIndex >= MAX_API_KEY_SLOTS) return;

	if (bPinToChat)
	{
		SetChatSlot(SlotIndex);
	}
	else
	{
		// Make it the default for every chat and drop the per-chat override so
		// the current conversation follows the new default immediately.
		FApiKeyManager::Get().SetActiveSlot(SlotIndex);
		if (!W->ActiveArchitectChatID.IsEmpty() && W->ArchitectApiKeySlotByChat.Contains(W->ActiveArchitectChatID))
			SetChatSlot(-1);
		else if (W->AgentInstances.Contains(W->ActiveArchitectChatID))
			IUECPCoreModule::Get().GetArchitectService().StopGenerationForChat(W->ActiveArchitectChatID);
		if (W->SettingsBridgeObject) W->SettingsBridgeObject->PushACPCatalog();
	}

	// Kick off model discovery for ACP agents that have never been probed.
	const FApiKeySlot Slot = FApiKeyManager::Get().GetSlot(SlotIndex);
	if (!Slot.Provider.IsEmpty() && IUECPCoreModule::Get().GetACPRegistryService().IsInstalled(Slot.Provider))
		PickerDiscoverAgent(Slot.Provider);

	PushSlashContext();
	CrewListSlots();
	PushModelPicker();
}

void UUECPAppBridge::PickerSetAgentOption(const FString& AgentId, const FString& Key, const FString& Value)
{
	auto W = OwnerWidget.Pin();
	if (!W.IsValid() || AgentId.IsEmpty() || Key.IsEmpty()) return;

	const int32 SlotIdx = AxPickerResolveSlot(W->ActiveArchitectChatID, W->ArchitectApiKeySlotByChat);
	const FString Section = AxPickerSlotSection(SlotIdx);
	const FString KeyName = (Key == TEXT("model"))
		? FString::Printf(TEXT("%s.Model"), *AgentId)
		: FString::Printf(TEXT("%s.Option.%s"), *AgentId, *Key);
	GConfig->SetString(*Section, *KeyName, *Value, FSettingsManager::GetGlobalConfigPath());
	GConfig->Flush(false, FSettingsManager::GetGlobalConfigPath());

	// Mirror onto the slot struct — GetActiveAgentProvider() reads AgentModel/AgentEffort from it.
	FApiKeySlot Slot = FApiKeyManager::Get().GetSlot(SlotIdx);
	bool bSlotDirty = false;
	if (Key == TEXT("model"))  { Slot.AgentModel  = Value; bSlotDirty = true; }
	if (Key == TEXT("effort")) { Slot.AgentEffort = Value; bSlotDirty = true; }
	if (bSlotDirty) FApiKeyManager::Get().SetSlot(SlotIdx, Slot);

	if (FUECPACPAgentConfigSnapshot* Snap = W->CachedAgentConfigs.Find(AgentId))
	{
		if (Key == TEXT("model")) Snap->CurrentModel = Value;
		else
		{
			for (FUECPACPAgentConfigSection& Sec : Snap->OtherSections)
				if (Sec.Id == Key) { Sec.CurrentValue = Value; break; }
		}
	}

	// A live session applies prefs only at spawn — stop it so the next message
	// restarts the agent with the new model/options (same semantics as switching slot).
	if (!W->ActiveArchitectChatID.IsEmpty() && W->AgentInstances.Contains(W->ActiveArchitectChatID))
		IUECPCoreModule::Get().GetArchitectService().StopGenerationForChat(W->ActiveArchitectChatID);

	UE_LOG(LogTemp, Log, TEXT("[Axivor] picker: slot %d agent '%s' %s = %s"), SlotIdx, *AgentId, *Key, *Value);

	if (W->SettingsBridgeObject) W->SettingsBridgeObject->PushACPAgentConfig(AgentId);
	PushSlashContext();
	CrewListSlots();
	PushModelPicker();
}

void UUECPAppBridge::PickerDiscoverAgent(const FString& AgentId)
{
	auto W = OwnerWidget.Pin();
	if (!W.IsValid() || AgentId.IsEmpty()) return;
	if (W->CachedAgentConfigs.Contains(AgentId)) { PushModelPicker(); return; }
	const SUECPMainWidget::FACPAgentAuthState* Auth = W->AgentAuthStates.Find(AgentId);
	if (Auth && Auth->bRequired) { PushModelPicker(); return; }
	if (W->AgentDiscoveryAttempted.Contains(AgentId)) return;
	if (!IUECPCoreModule::Get().GetACPRegistryService().IsInstalled(AgentId)) return;

	W->AgentDiscoveryAttempted.Add(AgentId);
	const FString AgentIdCopy = AgentId;
	FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateLambda([AgentIdCopy](float) -> bool
		{
			if (IUECPCoreModule::IsAvailable())
				IUECPCoreModule::Get().GetAgentRunnerService().DiscoverAgentConfig(AgentIdCopy);
			return false;
		}), 0.25f);
	PushModelPicker();
}

// ═══════════════════════════════════════════════════════════════════════════
// Axivor Turbo — File Sandbox (UE 5.8) status bar controls
// ═══════════════════════════════════════════════════════════════════════════
void UUECPAppBridge::SandboxCommand(const FString& Action)
{
	if (!IUECPCoreModule::IsAvailable()) return;
	IUECPToolDispatcher& D = IUECPCoreModule::Get().GetToolDispatcher();
	static const FName SandboxTool(TEXT("engine_sandbox"));
	if (!D.IsRegistered(SandboxTool))
	{
		ExecJs(TEXT("if(typeof onSandboxState==='function')onSandboxState('{\"available\":false}')"));
		return;
	}
	const FString Act = Action.IsEmpty() ? TEXT("status") : Action;
	if (Act != TEXT("status"))
	{
		TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("action"), Act);
		const FUECPToolResult R = D.ExecuteFromArgs(SandboxTool, Args);
		if (!R.bSuccess) PushToast(FString::Printf(TEXT("Sandbox: %s"), *R.ErrorMessage), TEXT("error"));
		else if (Act == TEXT("persist")) PushToast(TEXT("Sandbox: alterações aplicadas ao projeto."), TEXT("success"));
		else if (Act == TEXT("discard")) PushToast(TEXT("Sandbox: alterações descartadas."), TEXT("info"));
	}
	PushSandboxState();
}

void UUECPAppBridge::PushSandboxState()
{
	if (!IUECPCoreModule::IsAvailable()) return;
	IUECPToolDispatcher& D = IUECPCoreModule::Get().GetToolDispatcher();
	static const FName SandboxTool(TEXT("engine_sandbox"));
	if (!D.IsRegistered(SandboxTool)) return;
	TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
	Args->SetStringField(TEXT("action"), TEXT("status"));
	const FUECPToolResult R = D.ExecuteFromArgs(SandboxTool, Args);
	FString Json = R.bSuccess ? R.ResultJson : TEXT("{\"available\":false}");
	if (R.bSuccess && Json.StartsWith(TEXT("{"))) Json = TEXT("{\"available\":true,") + Json.Mid(1);
	bool bTurbo = false;
	GConfig->GetBool(TEXT("BpGeneratorUltimate"), TEXT("TurboMode"), bTurbo, FSettingsManager::GetGlobalConfigPath());
	if (Json.StartsWith(TEXT("{"))) Json = FString::Printf(TEXT("{\"turbo\":%s,"), bTurbo ? TEXT("true") : TEXT("false")) + Json.Mid(1);
	const FString Escaped = Json.Replace(TEXT("\\"), TEXT("\\\\")).Replace(TEXT("'"), TEXT("\\'")).Replace(TEXT("\n"), TEXT("\\n")).Replace(TEXT("\r"), TEXT(""));
	ExecJs(FString::Printf(TEXT("if(typeof onSandboxState==='function')onSandboxState('%s')"), *Escaped));
}

#undef LOCTEXT_NAMESPACE
