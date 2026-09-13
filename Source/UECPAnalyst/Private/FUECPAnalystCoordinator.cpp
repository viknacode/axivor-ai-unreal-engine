// Copyright 2026, BlueprintsLab, All rights reserved

#include "FUECPAnalystCoordinator.h"
#include "SUECPMainWidget.h"
#include "Widget/UUECPAppBridge.h"
#include "Managers/ChatHistoryManager.h"
#include "Managers/EditorProfileSync.h"
#include "Managers/FreeTierConfigManager.h"
#include "Managers/HttpCommunicationManager.h"
#include "Managers/SettingsManager.h"
#include "ApiKeyManager.h"
#include "UECPCoreModule.h"
#include "Describers/BpSummarizer.h"
#include "Services/IUECPGddService.h"
#include "Services/IUECPAiMemoryService.h"
#include "UIConfigManager.h"
#include "AssetReferenceTypes.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonReader.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ConfigCacheIni.h"
#include "DesktopPlatformModule.h"
#include "IDesktopPlatform.h"
#include "Framework/Application/SlateApplication.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "Engine/Blueprint.h"
#include "Materials/Material.h"
#include "BehaviorTree/BehaviorTree.h"
#include "Editor.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "BlueprintEditor.h"
#include "WidgetBlueprintEditor.h"
#include "BehaviorTreeEditor.h"
#include "Describers/BpGraphDescriber.h"
#include "Describers/MaterialGraphDescriber.h"
#include "Describers/BtGraphDescriber.h"
#include "Describers/MaterialNodeDescriber.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"

TArray<TSharedPtr<FJsonValue>>        FUECPAnalystCoordinator::EmptyHistory;
TArray<TSharedPtr<FConversationInfo>> FUECPAnalystCoordinator::EmptyList;
FString                               FUECPAnalystCoordinator::EmptyChatID;

void FUECPAnalystCoordinator::InitializeShellRefs(TWeakPtr<SUECPMainWidget> InShell,
	TWeakObjectPtr<UUECPAppBridge> InBridge)
{
	Shell  = InShell;
	Bridge = InBridge;
}

void FUECPAnalystCoordinator::SendMessage(const FString& Message)
{
	TSharedPtr<SUECPMainWidget> W = Shell.Pin();
	if (!W.IsValid()) return;

	FString UserInput = Message;
	if (UserInput.IsEmpty()) UserInput = W->GetAnalystPendingBridgeTextForExtraction();
	if (UserInput.IsEmpty()) return;
	if (W->GetAnalystThinkingForExtraction()) return;
	FString& ActiveID = W->GetAnalystActiveChatIDMutableForExtraction();
	if (ActiveID.IsEmpty()) return;

	W->ClearAnalystPendingBridgeTextForExtraction();
	W->AnalystSetInputTextForExtraction(TEXT(""));

	TArray<TSharedPtr<FJsonValue>>& History = W->GetAnalystHistoryMutableForExtraction();

	if (History.Num() <= 1)
	{
		FString NewTitle = UserInput;
		if (NewTitle.Len() > 40) NewTitle = NewTitle.Left(37) + TEXT("...");

		TArray<TSharedPtr<FConversationInfo>>& List = W->GetAnalystChatListMutableForExtraction();
		for (TSharedPtr<FConversationInfo>& Info : List)
		{
			if (Info.IsValid() && Info->ID == ActiveID)
			{
				Info->Title = NewTitle;
				SaveManifest();
				W->AnalystPushChatListToJsForExtraction();
				break;
			}
		}
	}

	FString FullPromptToAI = UserInput;
	FString& Pending = W->GetAnalystPendingContextMutableForExtraction();
	if (!Pending.IsEmpty())
	{
		FullPromptToAI = FString::Printf(TEXT("%s\n\n%s"), *Pending, *UserInput);
		Pending.Empty();
	}

	TArray<FAttachedFileContext>& Files = W->GetAnalystAttachedFilesMutableForExtraction();
	for (const FAttachedFileContext& File : Files)
	{
		TSharedPtr<FJsonObject> FileContextMsg = MakeShareable(new FJsonObject);
		FileContextMsg->SetStringField(TEXT("role"), TEXT("context"));
		FileContextMsg->SetStringField(TEXT("file_name"), File.FileName);
		FileContextMsg->SetStringField(TEXT("content"), File.Content);
		FileContextMsg->SetNumberField(TEXT("char_count"), File.CharCount);
		FileContextMsg->SetBoolField(TEXT("expanded"), File.bExpanded);

		TArray<TSharedPtr<FJsonValue>> FileParts;
		TSharedPtr<FJsonObject> FilePart = MakeShareable(new FJsonObject);
		FilePart->SetStringField(TEXT("text"),
			FString::Printf(TEXT("--- File: %s (%d chars, click to expand) ---"), *File.FileName, File.CharCount));
		FileParts.Add(MakeShareable(new FJsonValueObject(FilePart)));

		FileContextMsg->SetArrayField(TEXT("parts"), FileParts);
		History.Add(MakeShareable(new FJsonValueObject(FileContextMsg)));
	}
	Files.Empty();

	TSharedPtr<FJsonObject> UserContent = MakeShareable(new FJsonObject);
	UserContent->SetStringField(TEXT("role"), TEXT("user"));
	TArray<TSharedPtr<FJsonValue>> UserParts;
	TSharedPtr<FJsonObject> UserPartText = MakeShareable(new FJsonObject);
	UserPartText->SetStringField(TEXT("text"), FullPromptToAI);
	UserParts.Add(MakeShareable(new FJsonValueObject(UserPartText)));

	TArray<FAttachedImage>& Images = W->GetAnalystAttachedImagesMutableForExtraction();
	for (const FAttachedImage& Image : Images)
	{
		TSharedPtr<FJsonObject> ImagePart = MakeShareable(new FJsonObject);
		TSharedPtr<FJsonObject> InlineData = MakeShareable(new FJsonObject);
		InlineData->SetStringField(TEXT("mime_type"), Image.MimeType);
		InlineData->SetStringField(TEXT("data"), Image.Base64Data);
		ImagePart->SetObjectField(TEXT("inline_data"), InlineData);
		UserParts.Add(MakeShareable(new FJsonValueObject(ImagePart)));
	}
	UserContent->SetArrayField(TEXT("parts"), UserParts);
	History.Add(MakeShareable(new FJsonValueObject(UserContent)));

	SaveChatHistory(ActiveID);

	Images.Empty();
	W->RefreshAnalystImagePreviewForExtraction();

	W->SetAnalystThinkingForExtraction(true);
	RefreshChatHistoryView();

	SendChatRequest();
}

void FUECPAnalystCoordinator::StopGeneration()
{
}

void FUECPAnalystCoordinator::NewChat()
{
	TSharedPtr<SUECPMainWidget> W = Shell.Pin();
	if (!W.IsValid()) return;

	const FString NewID = FString::Printf(TEXT("%lld"), FDateTime::UtcNow().ToUnixTimestamp());
	TSharedPtr<FConversationInfo> NewConversation = MakeShared<FConversationInfo>();
	NewConversation->ID          = NewID;
	NewConversation->Title       = TEXT("New Chat");
	NewConversation->LastUpdated = FDateTime::UtcNow().ToIso8601();

	W->GetAnalystChatListMutableForExtraction().Insert(NewConversation, 0);

	W->AnalystPushChatListToJsForExtraction();
	W->AnalystSelectChatForExtraction(NewConversation);

	SaveManifest();
}

void FUECPAnalystCoordinator::AddAssetContext()
{
	TSharedPtr<SUECPMainWidget> W = Shell.Pin();
	if (!W.IsValid()) return;

	FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
	TArray<FAssetData> SelectedAssetsData;
	ContentBrowserModule.Get().GetSelectedAssets(SelectedAssetsData);

	if (SelectedAssetsData.Num() != 1)
	{
		W->AnalystSetInputTextForExtraction(UIConfig::GetValue(TEXT("error_select_one_asset"),
			TEXT("Error: Please select exactly one asset in the Content Browser.")));
		return;
	}

	UObject* SelectedObject = SelectedAssetsData[0].GetAsset();
	if (!SelectedObject)
	{
		W->AnalystSetInputTextForExtraction(UIConfig::GetValue(TEXT("error_invalid_asset"),
			TEXT("Error: The selected asset is invalid or could not be loaded.")));
		return;
	}

	FString RawSummary;
	FString AssetTypeName;

	if (UBlueprint* Blueprint = Cast<UBlueprint>(SelectedObject))
	{
		RawSummary = FBpSummarizer().Summarize(Blueprint);
		AssetTypeName = TEXT("Blueprint");
	}
	else if (UMaterial* Material = Cast<UMaterial>(SelectedObject))
	{
		FMaterialGraphDescriber Describer;
		RawSummary = Describer.Describe(Material);
		AssetTypeName = TEXT("Material");
	}
	else if (UBehaviorTree* BehaviorTree = Cast<UBehaviorTree>(SelectedObject))
	{
		FBtGraphDescriber Describer;
		RawSummary = Describer.Describe(BehaviorTree);
		AssetTypeName = TEXT("Behavior Tree");
	}
	else
	{
		FString ErrorMsg = UIConfig::GetValue(TEXT("error_unsupported_asset_type"),
			TEXT("Error: The selected asset ('{0}') is not a supported type for summary (Blueprint, Material, or Behavior Tree)."));
		ErrorMsg.ReplaceInline(TEXT("{0}"), *SelectedObject->GetClass()->GetName());
		W->AnalystSetInputTextForExtraction(ErrorMsg);
		return;
	}

	if (RawSummary.IsEmpty())
	{
		W->AnalystSetInputTextForExtraction(UIConfig::GetValue(TEXT("error_empty_summary"),
			TEXT("Error: Could not generate a raw summary for the selected asset. It might be empty or corrupted.")));
		return;
	}

	W->GetAnalystPendingContextMutableForExtraction() = FString::Printf(
		TEXT("Here is the context for the %s '%s':\n\n--- Data ---\n%s\n---"),
		*AssetTypeName, *SelectedObject->GetName(), *RawSummary);

	FString Msg = UIConfig::GetValue(TEXT("msg_asset_context_attached"),
		TEXT("Context for asset '{0}' attached. Now, what would you like to know?"));
	Msg.ReplaceInline(TEXT("{0}"), *SelectedObject->GetName());
	W->AnalystSetInputTextForExtraction(Msg);
}

void FUECPAnalystCoordinator::AddNodeContext()
{
	TSharedPtr<SUECPMainWidget> W = Shell.Pin();
	if (!W.IsValid()) return;

	UAssetEditorSubsystem* AssetEditorSubsystem = GEditor ? GEditor->GetEditorSubsystem<UAssetEditorSubsystem>() : nullptr;
	if (!AssetEditorSubsystem) return;

	IAssetEditorInstance* ActiveEditor = nullptr;
	double LastActivationTime = 0.0;
	for (UObject* Asset : AssetEditorSubsystem->GetAllEditedAssets())
	{
		for (IAssetEditorInstance* Editor : AssetEditorSubsystem->FindEditorsForAsset(Asset))
		{
			if (Editor && Editor->GetLastActivationTime() > LastActivationTime)
			{
				LastActivationTime = Editor->GetLastActivationTime();
				ActiveEditor = Editor;
			}
		}
	}

	if (!ActiveEditor)
	{
		W->AnalystSetInputTextForExtraction(UIConfig::GetValue(TEXT("error_no_active_editor"),
			TEXT("Error: No active asset editor found. Please open a Blueprint, Material, or Behavior Tree.")));
		return;
	}

	const FName EditorName = ActiveEditor->GetEditorName();
	TSet<UObject*> SelectedNodes;
	FString NodesSummary;
	bool bFoundEditor = false;

	if (EditorName == FName(TEXT("BlueprintEditor")) || EditorName == FName(TEXT("AnimationBlueprintEditor")))
	{
		FBlueprintEditor* BlueprintEditor = static_cast<FBlueprintEditor*>(ActiveEditor);
		SelectedNodes = BlueprintEditor->GetSelectedNodes();
		bFoundEditor = true;
		FBpGraphDescriber Describer;
		NodesSummary = Describer.Describe(SelectedNodes);
	}
	else if (EditorName == FName(TEXT("WidgetBlueprintEditor")))
	{
		FWidgetBlueprintEditor* WidgetEditor = static_cast<FWidgetBlueprintEditor*>(ActiveEditor);
		SelectedNodes = WidgetEditor->GetSelectedNodes();
		bFoundEditor = true;
		FBpGraphDescriber Describer;
		NodesSummary = Describer.Describe(SelectedNodes);
	}
	else if (EditorName == FName(TEXT("Behavior Tree")))
	{
		FBehaviorTreeEditor* BehaviorTreeEditor = static_cast<FBehaviorTreeEditor*>(ActiveEditor);
		SelectedNodes = BehaviorTreeEditor->GetSelectedNodes();
		bFoundEditor = true;
		FBtGraphDescriber Describer;
		NodesSummary = Describer.DescribeSelection(SelectedNodes);
	}

	if (!bFoundEditor)
	{
		FString ErrorMsg = UIConfig::GetValue(TEXT("error_unsupported_editor"),
			TEXT("Error: The active editor ('{0}') is not supported for node selection."));
		ErrorMsg.ReplaceInline(TEXT("{0}"), *EditorName.ToString());
		W->AnalystSetInputTextForExtraction(ErrorMsg);
		return;
	}

	if (SelectedNodes.Num() == 0)
	{
		W->AnalystSetInputTextForExtraction(UIConfig::GetValue(TEXT("error_no_nodes_selected"),
			TEXT("Error: No nodes are selected in the active graph editor.")));
		return;
	}

	W->GetAnalystPendingContextMutableForExtraction() = FString::Printf(
		TEXT("Here is the context for the currently selected nodes:\n\n--- Node Data ---\n%s\n---"), *NodesSummary);
	W->AnalystSetInputTextForExtraction(UIConfig::GetValue(TEXT("msg_node_context_attached"),
		TEXT("Context for selected nodes has been attached. Now, what is your question?")));
}

void FUECPAnalystCoordinator::ImportFileContext()
{
	TSharedPtr<SUECPMainWidget> W = Shell.Pin();
	if (!W.IsValid()) return;

	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (!DesktopPlatform) return;

	TArray<FString> OutFiles;
	const FString FileTypes = TEXT("Text Files (*.txt)|*.txt|Markdown Files (*.md)|*.md|All Files (*.*)|*.*");

	void* ParentHandle = nullptr;
	if (FSlateApplication::IsInitialized())
	{
		if (TSharedPtr<SWindow> ActiveWindow = FSlateApplication::Get().GetActiveTopLevelWindow())
			if (ActiveWindow->GetNativeWindow().IsValid())
				ParentHandle = ActiveWindow->GetNativeWindow()->GetOSWindowHandle();
	}

	const bool bOpened = DesktopPlatform->OpenFileDialog(
		ParentHandle,
		TEXT("Import Context File"),
		FPaths::ProjectSavedDir() / TEXT("Exported Conversations"),
		TEXT(""),
		FileTypes,
		EFileDialogFlags::None,
		OutFiles);

	if (!bOpened || OutFiles.Num() == 0) return;

	const FString& FilePath = OutFiles[0];
	FString FileContent;

	if (!FFileHelper::LoadFileToString(FileContent, *FilePath))
	{
		FString ErrorMsg = UIConfig::GetValue(TEXT("error_file_read"), TEXT("Error: Could not read the file '{0}'."));
		ErrorMsg.ReplaceInline(TEXT("{0}"), *FilePath);
		W->AnalystSetInputTextForExtraction(ErrorMsg);
		return;
	}

	const FString FileName = FPaths::GetCleanFilename(FilePath);

	FAttachedFileContext NewFile;
	NewFile.FileName  = FileName;
	NewFile.Content   = FileContent;
	NewFile.CharCount = FileContent.Len();
	NewFile.bExpanded = false;

	TArray<FAttachedFileContext>& Files = W->GetAnalystAttachedFilesMutableForExtraction();
	Files.Add(NewFile);

	if (UUECPAppBridge* B = Bridge.Get())
		B->PushFileContexts(TEXT("analyst"), UUECPAppBridge::BuildFileContextsJson(Files));

	FString Msg = UIConfig::GetValue(TEXT("msg_file_context_attached"), TEXT("Context from file '{0}' attached. Now, what is your question?"));
	Msg.ReplaceInline(TEXT("{0}"), *FileName);
	W->AnalystSetInputTextForExtraction(Msg);

	int32 CurrentTokens = FChatHistoryManager::EstimateConversationTokens(W->GetAnalystHistoryForExtraction());
	for (const FAttachedFileContext& File : Files)
		CurrentTokens += File.CharCount / 4;
	W->AnalystPushTokenCountForExtraction(CurrentTokens);

	SaveChatHistory(W->GetAnalystActiveChatIDForExtraction());
}

void FUECPAnalystCoordinator::AttachImage()
{
	if (TSharedPtr<SUECPMainWidget> W = Shell.Pin()) W->AnalystAttachImageForExtraction();
}

void FUECPAnalystCoordinator::RefreshChatHistoryView()
{
	TSharedPtr<SUECPMainWidget> W = Shell.Pin();
	if (!W.IsValid()) return;
	UUECPAppBridge* B = Bridge.Get();
	if (!B) return;

	const TArray<TSharedPtr<FJsonValue>>& History = W->GetAnalystHistoryForExtraction();

	TArray<TSharedPtr<FJsonValue>> Messages;
	for (int32 i = 0; i < History.Num(); ++i)
	{
		const TSharedPtr<FJsonObject> MsgObj = History[i]->AsObject();
		if (!MsgObj.IsValid()) continue;
		FString Role;
		MsgObj->TryGetStringField(TEXT("role"), Role);
		FString Content;
		const TArray<TSharedPtr<FJsonValue>>* Parts;
		if (MsgObj->TryGetArrayField(TEXT("parts"), Parts) && Parts->Num() > 0)
		{
			const TSharedPtr<FJsonObject>* PartObj;
			if ((*Parts)[0]->TryGetObject(PartObj))
				(*PartObj)->TryGetStringField(TEXT("text"), Content);
		}
		if (Content.IsEmpty()) continue;
		TSharedPtr<FJsonObject> Msg = MakeShareable(new FJsonObject);
		Msg->SetStringField(TEXT("role"), Role);
		Msg->SetStringField(TEXT("content"), Content);
		Messages.Add(MakeShareable(new FJsonValueObject(Msg)));
	}
	FString Json;
	TSharedRef<TJsonWriter<>> JsonWriter = TJsonWriterFactory<>::Create(&Json);
	FJsonSerializer::Serialize(Messages, JsonWriter);
	B->PushChatHistory(TEXT("analyst"), Json);

	if (W->GetAnalystThinkingForExtraction())
	{
		B->PushThinkingState(TEXT("analyst"), true, TEXT("AI is working.."), TEXT(""));
	}
	else
	{
		B->ExecJs(TEXT("if(typeof onGenerationDone==='function')onGenerationDone('analyst')"));
	}
}

void FUECPAnalystCoordinator::SelectChat(TSharedPtr<FConversationInfo> InItem, bool bDirect)
{
	TSharedPtr<SUECPMainWidget> W = Shell.Pin();
	if (!W.IsValid()) return;

	FString& ActiveID = W->GetAnalystActiveChatIDMutableForExtraction();

	if (InItem.IsValid())
	{
		if (bDirect && InItem->ID == ActiveID) return;

		ActiveID = InItem->ID;
		LoadChatHistory(ActiveID);

		int32 TokenCount = InItem->TotalTokens;
		const TArray<TSharedPtr<FJsonValue>>& History = W->GetAnalystHistoryForExtraction();
		if (TokenCount == 0 && History.Num() > 0)
		{
			TokenCount = FChatHistoryManager::EstimateConversationTokens(History);
		}
		W->AnalystPushTokenCountForExtraction(TokenCount);
	}
	else
	{
		bool bStillPresent = false;
		for (const TSharedPtr<FConversationInfo>& Conv : W->GetAnalystChatListForExtraction())
		{
			if (Conv.IsValid() && Conv->ID == ActiveID) { bStillPresent = true; break; }
		}
		if (!bStillPresent)
		{
			ActiveID.Empty();
			W->GetAnalystHistoryMutableForExtraction().Empty();
			RefreshChatHistoryView();
			W->AnalystPushTokenCountForExtraction(0);
		}
	}
}

void FUECPAnalystCoordinator::LoadChatHistory(const FString& ChatID)
{
	TSharedPtr<SUECPMainWidget> W = Shell.Pin();
	if (!W.IsValid()) return;

	TArray<TSharedPtr<FJsonValue>>& History = W->GetAnalystHistoryMutableForExtraction();
	History.Empty();

	if (ChatID.IsEmpty())
	{
		RefreshChatHistoryView();
		return;
	}

	History = FChatHistoryManager::Get().LoadChatHistory(EConversationViewType::Analyst, ChatID);

	if (History.Num() == 0)
	{
		const FString Greeting = TEXT("Hello! Select a Blueprint, Material, Widget, AnimBP or Behavior Tree and use the context buttons below to get started, or simply ask me a question.");
		TSharedPtr<FJsonObject> ModelContent = MakeShareable(new FJsonObject);
		ModelContent->SetStringField(TEXT("role"), TEXT("model"));
		TArray<TSharedPtr<FJsonValue>> ModelParts;
		TSharedPtr<FJsonObject> ModelPartText = MakeShareable(new FJsonObject);
		ModelPartText->SetStringField(TEXT("text"), Greeting);
		ModelParts.Add(MakeShareable(new FJsonValueObject(ModelPartText)));
		ModelContent->SetArrayField(TEXT("parts"), ModelParts);
		History.Add(MakeShareable(new FJsonValueObject(ModelContent)));
	}

	RefreshChatHistoryView();
}

void FUECPAnalystCoordinator::SaveChatHistory(const FString& ChatID)
{
	TSharedPtr<SUECPMainWidget> W = Shell.Pin();
	if (!W.IsValid() || ChatID.IsEmpty()) return;
	const TArray<TSharedPtr<FJsonValue>>& History = W->GetAnalystHistoryForExtraction();
	if (History.Num() == 0) return;
	FChatHistoryManager::Get().SaveChatHistory(EConversationViewType::Analyst, ChatID, History);
}

void FUECPAnalystCoordinator::LoadManifest()
{
	TSharedPtr<SUECPMainWidget> W = Shell.Pin();
	if (!W.IsValid()) return;

	TArray<TSharedPtr<FConversationInfo>>& List = W->GetAnalystChatListMutableForExtraction();
	List = FChatHistoryManager::Get().LoadManifest(EConversationViewType::Analyst);

	W->AnalystPushChatListToJsForExtraction();

	if (List.Num() > 0)
	{
		W->AnalystSelectChatForExtraction(List[0]);
	}
	else
	{
		NewChat();
	}
}

void FUECPAnalystCoordinator::SaveManifest()
{
	TSharedPtr<SUECPMainWidget> W = Shell.Pin();
	if (!W.IsValid()) return;
	FChatHistoryManager::Get().SaveManifest(EConversationViewType::Analyst, W->GetAnalystChatListForExtraction());
}

const TArray<TSharedPtr<FJsonValue>>& FUECPAnalystCoordinator::GetConversationHistory() const
{
	if (TSharedPtr<SUECPMainWidget> W = Shell.Pin()) return W->GetAnalystHistoryForExtraction();
	return EmptyHistory;
}

const TArray<TSharedPtr<FConversationInfo>>& FUECPAnalystCoordinator::GetConversationList() const
{
	if (TSharedPtr<SUECPMainWidget> W = Shell.Pin()) return W->GetAnalystChatListForExtraction();
	return EmptyList;
}

const FString& FUECPAnalystCoordinator::GetActiveChatID() const
{
	if (TSharedPtr<SUECPMainWidget> W = Shell.Pin()) return W->GetAnalystActiveChatIDForExtraction();
	return EmptyChatID;
}

void FUECPAnalystCoordinator::ClearHistory()
{
	if (TSharedPtr<SUECPMainWidget> W = Shell.Pin()) W->ClearAnalystHistoryForExtraction();
}

void FUECPAnalystCoordinator::SendChatRequest()
{
	{ auto& _sm = FEditorProfileSync::Get(); if (!_sm.IsEditorHostActive() || (!_sm.HasEngineContext() && !(_sm.GetEditorStateHash() & 0xA3F1))) return; }

	TSharedPtr<SUECPMainWidget> W = Shell.Pin();
	if (!W.IsValid()) return;

	FApiKeySlot ActiveSlot = FApiKeyManager::Get().GetActiveSlot();
	FString ApiKeyToUse = ActiveSlot.ApiKey;
	FString ProviderStr = ActiveSlot.Provider;
	FString BaseURL     = ActiveSlot.CustomBaseURL;
	FString ModelName   = ActiveSlot.CustomModelName;
	bool bIsFreeTierRequest = false;

	if (ProviderStr == TEXT("Free"))
	{
		if (FFreeTierConfigManager::Get().IsBlocked())
		{
			const FString BlockMsg = FFreeTierConfigManager::Get().GetBlockMessage();
			TSharedPtr<FJsonObject> ModelContent = MakeShareable(new FJsonObject);
			ModelContent->SetStringField(TEXT("role"), TEXT("model"));
			TArray<TSharedPtr<FJsonValue>> ModelParts;
			TSharedPtr<FJsonObject> ModelPartText = MakeShareable(new FJsonObject);
			ModelPartText->SetStringField(TEXT("text"), BlockMsg);
			ModelParts.Add(MakeShareable(new FJsonValueObject(ModelPartText)));
			ModelContent->SetArrayField(TEXT("parts"), ModelParts);
			W->GetAnalystHistoryMutableForExtraction().Add(MakeShareable(new FJsonValueObject(ModelContent)));
			W->SetAnalystThinkingForExtraction(false);
			RefreshChatHistoryView();
			return;
		}

		ApiKeyToUse        = FFreeTierConfigManager::Get().GetServiceRegistrationKey();
		BaseURL            = FFreeTierConfigManager::Get().GetActiveSlotEndpoint();
		ModelName          = FFreeTierConfigManager::Get().GetActiveSlotModel();
		ProviderStr        = TEXT("Custom");
		bIsFreeTierRequest = true;
	}

	TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> HttpRequest = FHttpModule::Get().CreateRequest();
	HttpRequest->SetVerb(TEXT("POST"));
	HttpRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json"));

	TSharedPtr<FJsonObject> JsonPayload = MakeShareable(new FJsonObject);

	FString FormattingInstruction = TEXT("You are an expert Unreal Engine assistant. Analyze the provided context. You must format all of your responses using Markdown.\n\n**MANDATORY: CLICKABLE ASSET LINKS**\nWhen mentioning ANY asset name (blueprints, materials, textures, widgets, etc.), you MUST wrap it in a clickable link using this EXACT format: `[AssetName](ue://asset?name=AssetName)`\n\nExamples:\n- Write `[BP_EnemyAI](ue://asset?name=BP_EnemyAI)` not just BP_EnemyAI\n- Write `[M_Wood](ue://asset?name=M_Wood)` not just M_Wood\n- Write `[WBP_Inventory](ue://asset?name=WBP_Inventory)` not just WBP_Inventory\n\nRules:\n- Link EVERY asset name mentioned in plain text\n- Do NOT link inside code blocks (```code```)\n- Use the asset name only, not the full path");

	const FString& CustomInstr = W->GetCustomInstructionsForExtraction();
	if (!CustomInstr.IsEmpty())
	{
		FormattingInstruction += TEXT("\n\n=== USER CUSTOM INSTRUCTIONS ===\n");
		FormattingInstruction += CustomInstr;
		FormattingInstruction += TEXT("\n=== END CUSTOM INSTRUCTIONS ===");
	}

	const FString GddContent = IUECPCoreModule::Get().GetGddService().GetContentForAI();
	if (!GddContent.IsEmpty())
	{
		FormattingInstruction += TEXT("\n\n");
		FormattingInstruction += GddContent;
	}

	const FString AiMemoryContent = IUECPCoreModule::Get().GetAiMemoryService().GetContentForAI();
	if (!AiMemoryContent.IsEmpty())
	{
		FormattingInstruction += TEXT("\n\n");
		FormattingInstruction += AiMemoryContent;
	}

	const TArray<TSharedPtr<FJsonValue>>& FullHistory = W->GetAnalystHistoryForExtraction();
	static constexpr int32 MaxAnalystHistoryChars = 320000;
	const TArray<TSharedPtr<FJsonValue>> WindowedHistory =
		SUECPMainWidget::WindowConversationHistoryForExtraction(FullHistory, MaxAnalystHistoryChars);

	if (ProviderStr == TEXT("Custom"))
	{
		const FString CustomURL = FHttpCommunicationManager::BuildCustomUrl(BaseURL);
		HttpRequest->SetURL(CustomURL);
		if (CustomURL.Contains(TEXT("anthropic.com")))
		{
			HttpRequest->SetHeader(TEXT("x-api-key"), ApiKeyToUse);
			HttpRequest->SetHeader(TEXT("anthropic-version"), TEXT("2023-06-01"));
		}
		else
		{
			HttpRequest->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *ApiKeyToUse));
		}
		if (bIsFreeTierRequest)
		{
			const FString LicTok = FEditorProfileSync::Get().GetSyncKey();
			if (!LicTok.IsEmpty()) HttpRequest->SetHeader(TEXT("x-license-token"), LicTok);
		}

		FString CustomModel = ModelName.TrimStartAndEnd();
		if (CustomModel.IsEmpty()) CustomModel = TEXT("gpt-3.5-turbo");
		JsonPayload->SetStringField(TEXT("model"), CustomModel);

		TArray<TSharedPtr<FJsonValue>> OpenAiMessages;
		TSharedPtr<FJsonObject> SystemMessage = MakeShareable(new FJsonObject);
		SystemMessage->SetStringField("role", "system");
		SystemMessage->SetStringField("content", FormattingInstruction);
		OpenAiMessages.Add(MakeShareable(new FJsonValueObject(SystemMessage)));

		FString FileContext;
		bool bFileContextAdded = false;
		for (const TSharedPtr<FJsonValue>& MessageValue : WindowedHistory)
		{
			const TSharedPtr<FJsonObject>& Msg = MessageValue->AsObject();
			FString Role; Msg->TryGetStringField(TEXT("role"), Role);
			if (Role == TEXT("context"))
			{
				FString FileName, FileContent;
				Msg->TryGetStringField(TEXT("file_name"), FileName);
				Msg->TryGetStringField(TEXT("content"), FileContent);
				FileContext += FString::Printf(TEXT("--- File: %s ---\n%s\n\n"), *FileName, *FileContent);
				continue;
			}

			const TArray<TSharedPtr<FJsonValue>>* PartsArray;
			if (!Msg->TryGetArrayField(TEXT("parts"), PartsArray) || PartsArray->Num() == 0) continue;

			TSharedPtr<FJsonObject> OpenAiMessage = MakeShareable(new FJsonObject);
			OpenAiMessage->SetStringField("role", (Role == TEXT("model")) ? TEXT("assistant") : Role);

			TArray<TSharedPtr<FJsonValue>> ContentArray;
			for (const TSharedPtr<FJsonValue>& PartValue : *PartsArray)
			{
				const TSharedPtr<FJsonObject>& PartObj = PartValue->AsObject();
				FString TextContent;
				if (PartObj->TryGetStringField(TEXT("text"), TextContent))
				{
					if (!bFileContextAdded && !FileContext.IsEmpty() && Role == TEXT("user"))
					{
						TextContent = FileContext + TextContent;
						bFileContextAdded = true;
					}
					TSharedPtr<FJsonObject> TextPart = MakeShareable(new FJsonObject);
					TextPart->SetStringField(TEXT("type"), TEXT("text"));
					TextPart->SetStringField(TEXT("text"), TextContent);
					ContentArray.Add(MakeShareable(new FJsonValueObject(TextPart)));
				}
				const TSharedPtr<FJsonObject, ESPMode::ThreadSafe>* InlineData = nullptr;
				if (PartObj->TryGetObjectField(TEXT("inline_data"), InlineData))
				{
					const FString MimeType = (*InlineData)->GetStringField(TEXT("mime_type"));
					const FString Data     = (*InlineData)->GetStringField(TEXT("data"));
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
	else if (ProviderStr == TEXT("Gemini"))
	{
		FString FileContextToPrepend;
		TArray<TSharedPtr<FJsonValue>> GeminiContents;
		for (const TSharedPtr<FJsonValue>& Message : WindowedHistory)
		{
			const TSharedPtr<FJsonObject>& MsgObj = Message->AsObject();
			FString Role;
			if (!MsgObj->TryGetStringField(TEXT("role"), Role)) continue;
			if (Role == TEXT("context"))
			{
				FString FileContent;
				if (MsgObj->TryGetStringField(TEXT("content"), FileContent))
				{
					FString FileName;
					MsgObj->TryGetStringField(TEXT("file_name"), FileName);
					FileContextToPrepend += FString::Printf(TEXT("--- File: %s ---\n%s\n\n"), *FileName, *FileContent);
				}
				continue;
			}
			TSharedPtr<FJsonObject> CleanMsg = MakeShareable(new FJsonObject);
			CleanMsg->SetStringField(TEXT("role"), Role);
			const TArray<TSharedPtr<FJsonValue>>* ExistingParts;
			if (MsgObj->TryGetArrayField(TEXT("parts"), ExistingParts))
				CleanMsg->SetArrayField(TEXT("parts"), *ExistingParts);
			else
			{
				FString Content;
				if (MsgObj->TryGetStringField(TEXT("content"), Content))
				{
					TArray<TSharedPtr<FJsonValue>> Parts;
					TSharedPtr<FJsonObject> Part = MakeShareable(new FJsonObject);
					Part->SetStringField(TEXT("text"), Content);
					Parts.Add(MakeShareable(new FJsonValueObject(Part)));
					CleanMsg->SetArrayField(TEXT("parts"), Parts);
				}
			}
			GeminiContents.Add(MakeShareable(new FJsonValueObject(CleanMsg)));
		}

		for (int32 i = 0; i < GeminiContents.Num(); i++)
		{
			const TSharedPtr<FJsonObject>& MsgObj = GeminiContents[i]->AsObject();
			FString Role;
			if (MsgObj->TryGetStringField(TEXT("role"), Role) && Role == TEXT("user"))
			{
				const TArray<TSharedPtr<FJsonValue>>* PartsArray;
				if (MsgObj->TryGetArrayField(TEXT("parts"), PartsArray) && PartsArray->Num() > 0)
				{
					FString OriginalText;
					if ((*PartsArray)[0]->AsObject()->TryGetStringField(TEXT("text"), OriginalText))
					{
						const FString NewText = FormattingInstruction +
							TEXT("\n\n--- CONTEXT & QUESTION ---\n") + FileContextToPrepend + OriginalText;
						TSharedPtr<FJsonObject> ModifiedMessage = MakeShareable(new FJsonObject);
						ModifiedMessage->SetStringField(TEXT("role"), TEXT("user"));
						TArray<TSharedPtr<FJsonValue>> NewParts;
						TSharedPtr<FJsonObject> NewPart = MakeShareable(new FJsonObject);
						NewPart->SetStringField(TEXT("text"), NewText);
						NewParts.Add(MakeShareable(new FJsonValueObject(NewPart)));
						ModifiedMessage->SetArrayField(TEXT("parts"), NewParts);
						GeminiContents[i] = MakeShareable(new FJsonValueObject(ModifiedMessage));
						break;
					}
				}
			}
		}

		const FString GeminiModel = FApiKeyManager::Get().GetActiveGeminiModel();
		HttpRequest->SetURL(FHttpCommunicationManager::BuildGeminiUrl(GeminiModel, ApiKeyToUse));
		JsonPayload->SetArrayField(TEXT("contents"), GeminiContents);
	}
	else if (ProviderStr == TEXT("OpenAI") || ProviderStr == TEXT("DeepSeek"))
	{
		if (ProviderStr == TEXT("OpenAI"))
		{
			HttpRequest->SetURL(FHttpCommunicationManager::BuildOpenAIUrl());
			JsonPayload->SetStringField("model", FApiKeyManager::Get().GetActiveOpenAIModel());
		}
		else
		{
			HttpRequest->SetURL(FHttpCommunicationManager::BuildDeepSeekUrl());
			JsonPayload->SetStringField(TEXT("model"), TEXT("deepseek-chat"));
		}
		HttpRequest->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *ApiKeyToUse));

		TArray<TSharedPtr<FJsonValue>> Messages;
		TSharedPtr<FJsonObject> SystemMessage = MakeShareable(new FJsonObject);
		SystemMessage->SetStringField("role", "system");
		SystemMessage->SetStringField("content", FormattingInstruction);
		Messages.Add(MakeShareable(new FJsonValueObject(SystemMessage)));

		FString FileContext;
		bool bFileContextAdded = false;
		for (const TSharedPtr<FJsonValue>& MessageValue : WindowedHistory)
		{
			const TSharedPtr<FJsonObject>& Msg = MessageValue->AsObject();
			FString Role; Msg->TryGetStringField(TEXT("role"), Role);
			if (Role == TEXT("context"))
			{
				FString FileName, FileContent;
				Msg->TryGetStringField(TEXT("file_name"), FileName);
				Msg->TryGetStringField(TEXT("content"), FileContent);
				FileContext += FString::Printf(TEXT("--- File: %s ---\n%s\n\n"), *FileName, *FileContent);
				continue;
			}

			const TArray<TSharedPtr<FJsonValue>>* PartsArray;
			if (!Msg->TryGetArrayField(TEXT("parts"), PartsArray) || PartsArray->Num() == 0) continue;

			TSharedPtr<FJsonObject> OutMsg = MakeShareable(new FJsonObject);
			OutMsg->SetStringField("role", (Role == TEXT("model")) ? TEXT("assistant") : TEXT("user"));

			TArray<TSharedPtr<FJsonValue>> ContentArray;
			for (const TSharedPtr<FJsonValue>& PartValue : *PartsArray)
			{
				const TSharedPtr<FJsonObject>& PartObj = PartValue->AsObject();
				FString TextContent;
				if (PartObj->TryGetStringField(TEXT("text"), TextContent))
				{
					if (!bFileContextAdded && !FileContext.IsEmpty() && Role == TEXT("user"))
					{
						TextContent = FileContext + TextContent;
						bFileContextAdded = true;
					}
					TSharedPtr<FJsonObject> TextPart = MakeShareable(new FJsonObject);
					TextPart->SetStringField("type", "text");
					TextPart->SetStringField("text", TextContent);
					ContentArray.Add(MakeShareable(new FJsonValueObject(TextPart)));
				}
				const TSharedPtr<FJsonObject, ESPMode::ThreadSafe>* InlineData = nullptr;
				if (PartObj->TryGetObjectField(TEXT("inline_data"), InlineData))
				{
					const FString MimeType = (*InlineData)->GetStringField(TEXT("mime_type"));
					const FString Data     = (*InlineData)->GetStringField(TEXT("data"));
					TSharedPtr<FJsonObject> ImagePart = MakeShareable(new FJsonObject);
					ImagePart->SetStringField("type", "image_url");
					TSharedPtr<FJsonObject> ImageUrl = MakeShareable(new FJsonObject);
					ImageUrl->SetStringField("url", FString::Printf(TEXT("data:%s;base64,%s"), *MimeType, *Data));
					ImagePart->SetObjectField("image_url", ImageUrl);
					ContentArray.Add(MakeShareable(new FJsonValueObject(ImagePart)));
				}
			}
			if (ContentArray.Num() > 0)
			{
				OutMsg->SetArrayField("content", ContentArray);
				Messages.Add(MakeShareable(new FJsonValueObject(OutMsg)));
			}
		}
		JsonPayload->SetArrayField(TEXT("messages"), Messages);
	}
	else if (ProviderStr == TEXT("Claude"))
	{
		HttpRequest->SetURL(FHttpCommunicationManager::BuildClaudeUrl());
		HttpRequest->SetHeader(TEXT("x-api-key"), ApiKeyToUse);
		HttpRequest->SetHeader(TEXT("anthropic-version"), TEXT("2023-06-01"));
		JsonPayload->SetStringField(TEXT("model"), FApiKeyManager::Get().GetActiveClaudeModel());
		JsonPayload->SetNumberField(TEXT("max_tokens"), 4096);
		JsonPayload->SetStringField(TEXT("system"), FormattingInstruction);

		TArray<TSharedPtr<FJsonValue>> ClaudeMessages;
		FString FileContext;
		bool bFileContextAdded = false;
		for (const TSharedPtr<FJsonValue>& MessageValue : WindowedHistory)
		{
			const TSharedPtr<FJsonObject>& Msg = MessageValue->AsObject();
			FString Role; Msg->TryGetStringField(TEXT("role"), Role);
			if (Role == TEXT("context"))
			{
				FString FileName, FileContent;
				Msg->TryGetStringField(TEXT("file_name"), FileName);
				Msg->TryGetStringField(TEXT("content"), FileContent);
				FileContext += FString::Printf(TEXT("--- File: %s ---\n%s\n\n"), *FileName, *FileContent);
				continue;
			}

			const TArray<TSharedPtr<FJsonValue>>* PartsArray;
			if (!Msg->TryGetArrayField(TEXT("parts"), PartsArray) || PartsArray->Num() == 0) continue;

			TSharedPtr<FJsonObject> ClaudeMessage = MakeShareable(new FJsonObject);
			ClaudeMessage->SetStringField("role", (Role == TEXT("model")) ? TEXT("assistant") : TEXT("user"));

			TArray<TSharedPtr<FJsonValue>> ContentArray;
			for (const TSharedPtr<FJsonValue>& PartValue : *PartsArray)
			{
				const TSharedPtr<FJsonObject>& PartObj = PartValue->AsObject();
				FString TextContent;
				if (PartObj->TryGetStringField(TEXT("text"), TextContent))
				{
					if (!bFileContextAdded && !FileContext.IsEmpty() && Role == TEXT("user"))
					{
						TextContent = FileContext + TextContent;
						bFileContextAdded = true;
					}
					TSharedPtr<FJsonObject> TextPart = MakeShareable(new FJsonObject);
					TextPart->SetStringField("type", "text");
					TextPart->SetStringField("text", TextContent);
					ContentArray.Add(MakeShareable(new FJsonValueObject(TextPart)));
				}
				const TSharedPtr<FJsonObject, ESPMode::ThreadSafe>* InlineData = nullptr;
				if (PartObj->TryGetObjectField(TEXT("inline_data"), InlineData))
				{
					const FString MimeType = (*InlineData)->GetStringField(TEXT("mime_type"));
					const FString Data     = (*InlineData)->GetStringField(TEXT("data"));
					TSharedPtr<FJsonObject> ImagePart = MakeShareable(new FJsonObject);
					ImagePart->SetStringField("type", "image");
					TSharedPtr<FJsonObject> ImageSource = MakeShareable(new FJsonObject);
					ImageSource->SetStringField("type", "base64");
					ImageSource->SetStringField("media_type", MimeType);
					ImageSource->SetStringField("data", Data);
					ImagePart->SetObjectField("source", ImageSource);
					ContentArray.Add(MakeShareable(new FJsonValueObject(ImagePart)));
				}
			}
			if (ContentArray.Num() > 0)
			{
				ClaudeMessage->SetArrayField("content", ContentArray);
				ClaudeMessages.Add(MakeShareable(new FJsonValueObject(ClaudeMessage)));
			}
		}
		JsonPayload->SetArrayField(TEXT("messages"), ClaudeMessages);
	}

	FString RequestBody;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&RequestBody);
	FJsonSerializer::Serialize(JsonPayload.ToSharedRef(), Writer);
	HttpRequest->SetContentAsString(RequestBody);

	HttpRequest->OnProcessRequestComplete().BindLambda(
		[](FHttpRequestPtr Req, FHttpResponsePtr Resp, bool bWasSuccessful)
		{
			IUECPCoreModule::Get().GetAnalystService();
			if (FUECPAnalystCoordinator* Coord = static_cast<FUECPAnalystCoordinator*>(
				&IUECPCoreModule::Get().GetAnalystService()))
			{
				Coord->OnApiResponseReceived(Req, Resp, bWasSuccessful);
			}
		});
	HttpRequest->ProcessRequest();
}

void FUECPAnalystCoordinator::OnApiResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
{
	TSharedPtr<SUECPMainWidget> W = Shell.Pin();
	if (!W.IsValid()) return;

	W->SetAnalystThinkingForExtraction(false);

	FString AiMessage;
	int32 TotalCharsInInteraction = 0;

	W->ParseAndPushFreeTierRateLimitForExtraction(Response);

	const FString& ActiveID = W->GetAnalystActiveChatIDForExtraction();

	if (bWasSuccessful && Response.IsValid() && Response->GetResponseCode() == 200)
	{
		TSharedPtr<FJsonObject> JsonObject;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response->GetContentAsString());
		if (FJsonSerializer::Deserialize(Reader, JsonObject) && JsonObject.IsValid())
		{
			const TArray<TSharedPtr<FJsonValue>>* Choices = nullptr;
			const TArray<TSharedPtr<FJsonValue>>* Candidates = nullptr;
			if (JsonObject->TryGetArrayField(TEXT("choices"), Choices) && Choices->Num() > 0)
			{
				const TSharedPtr<FJsonObject>& ChoiceObject = (*Choices)[0]->AsObject();
				const TSharedPtr<FJsonObject>* MessageObject = nullptr;
				if (ChoiceObject->TryGetObjectField(TEXT("message"), MessageObject))
				{
					(*MessageObject)->TryGetStringField(TEXT("content"), AiMessage);
				}
				const TSharedPtr<FJsonObject>* UsageObj = nullptr;
				if (JsonObject->TryGetObjectField(TEXT("usage"), UsageObj))
				{
					int32 PromptTokens = 0, CompletionTokens = 0;
					(*UsageObj)->TryGetNumberField(TEXT("prompt_tokens"), PromptTokens);
					(*UsageObj)->TryGetNumberField(TEXT("completion_tokens"), CompletionTokens);
					W->UpdateConversationTokensForExtraction(ActiveID, PromptTokens, CompletionTokens);
				}
			}
			else if (JsonObject->TryGetArrayField(TEXT("candidates"), Candidates) && Candidates->Num() > 0)
			{
				const TSharedPtr<FJsonObject>& CandidateObject = (*Candidates)[0]->AsObject();
				const TSharedPtr<FJsonObject>* Content = nullptr;
				if (CandidateObject->TryGetObjectField(TEXT("content"), Content))
				{
					const TArray<TSharedPtr<FJsonValue>>* Parts = nullptr;
					if ((*Content)->TryGetArrayField(TEXT("parts"), Parts) && Parts->Num() > 0)
					{
						(*Parts)[0]->AsObject()->TryGetStringField(TEXT("text"), AiMessage);
					}
				}
				const TSharedPtr<FJsonObject>* UsageMetadata = nullptr;
				if (CandidateObject->TryGetObjectField(TEXT("usageMetadata"), UsageMetadata))
				{
					int32 TotalTokenCount = 0;
					if ((*UsageMetadata)->TryGetNumberField(TEXT("totalTokenCount"), TotalTokenCount))
					{
						W->UpdateConversationTokensForExtraction(ActiveID, TotalTokenCount, TotalTokenCount);
					}
				}
			}
			else
			{
				const TArray<TSharedPtr<FJsonValue>>* ContentBlocks = nullptr;
				if (JsonObject->TryGetArrayField(TEXT("content"), ContentBlocks) && ContentBlocks->Num() > 0)
				{
					const TSharedPtr<FJsonObject>& FirstBlock = (*ContentBlocks)[0]->AsObject();
					FirstBlock->TryGetStringField(TEXT("text"), AiMessage);
				}
				const TSharedPtr<FJsonObject>* UsageObj = nullptr;
				if (JsonObject->TryGetObjectField(TEXT("usage"), UsageObj))
				{
					int32 InputTokens = 0, OutputTokens = 0;
					(*UsageObj)->TryGetNumberField(TEXT("input_tokens"), InputTokens);
					(*UsageObj)->TryGetNumberField(TEXT("output_tokens"), OutputTokens);
					W->UpdateConversationTokensForExtraction(ActiveID, InputTokens, OutputTokens);
				}
			}
		}
	}

	if (AiMessage.IsEmpty())
	{
		if (!Response.IsValid() || !bWasSuccessful)
		{
			UE_LOG(LogTemp, Error, TEXT("BP Gen Analyst: HTTP request failed (provider=%s)"),
				*FApiKeyManager::Get().GetActiveProvider());
		}
		else
		{
			const FString BodyPreview = Response->GetContentAsString().Left(300);
			const FString ProviderName = FApiKeyManager::Get().GetActiveProvider();
			UE_LOG(LogTemp, Error, TEXT("BP Gen Analyst: API error %d (provider=%s, body=%s)"),
				Response->GetResponseCode(), *ProviderName, *BodyPreview);
		}
		AiMessage = SUECPMainWidget::FormatHttpErrorForExtraction(bWasSuccessful, Response, FApiKeyManager::Get().GetActiveProvider());
	}

	TArray<TSharedPtr<FJsonValue>>& History = W->GetAnalystHistoryMutableForExtraction();

	if (FApiKeyManager::Get().GetActiveProvider() == TEXT("Free") && FFreeTierConfigManager::Get().IsLimitsEnabled())
	{
		if (History.Num() > 0)
		{
			const TSharedPtr<FJsonObject>& LastUserMessage = History.Last()->AsObject();
			const TArray<TSharedPtr<FJsonValue>>* PartsArray;
			if (LastUserMessage->TryGetArrayField(TEXT("parts"), PartsArray) && PartsArray->Num() > 0)
			{
				FString Content;
				if ((*PartsArray)[0]->AsObject()->TryGetStringField(TEXT("text"), Content))
					TotalCharsInInteraction += Content.Len();
			}
		}
		TotalCharsInInteraction += AiMessage.Len();

		FString EncryptedUsageString;
		int32 CurrentCharacters = 0;
		if (GConfig->GetString(TEXT("Editor.Stats"), TEXT("LastKnownState"), EncryptedUsageString,
			FSettingsManager::GetGlobalConfigPath()) && !EncryptedUsageString.IsEmpty())
			CurrentCharacters = FCString::Atoi(*EncryptedUsageString) ^ 8675309;
		CurrentCharacters += TotalCharsInInteraction;
		GConfig->SetString(TEXT("Editor.Stats"), TEXT("LastKnownState"),
			*FString::FromInt(CurrentCharacters ^ 8675309), FSettingsManager::GetGlobalConfigPath());
		GConfig->Flush(false, FSettingsManager::GetGlobalConfigPath());
	}

	TSharedPtr<FJsonObject> ModelContent = MakeShareable(new FJsonObject);
	ModelContent->SetStringField(TEXT("role"), TEXT("model"));
	TArray<TSharedPtr<FJsonValue>> ModelParts;
	TSharedPtr<FJsonObject> ModelPartText = MakeShareable(new FJsonObject);
	ModelPartText->SetStringField(TEXT("text"), AiMessage);
	ModelParts.Add(MakeShareable(new FJsonValueObject(ModelPartText)));
	ModelContent->SetArrayField(TEXT("parts"), ModelParts);
	History.Add(MakeShareable(new FJsonValueObject(ModelContent)));

	SaveChatHistory(ActiveID);
	RefreshChatHistoryView();

	const int32 TokenCount = FChatHistoryManager::EstimateConversationTokens(History);
	W->AnalystPushTokenCountForExtraction(TokenCount);
}
