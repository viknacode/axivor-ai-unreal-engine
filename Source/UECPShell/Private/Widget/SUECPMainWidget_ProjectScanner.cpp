// Copyright 2026, BlueprintsLab, All rights reserved

#include "SUECPMainWidget.h"
#include "Managers/EditorProfileSync.h"
#include "Widget/UUECPAppBridge.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "Serialization/JsonSerializer.h"
#include "EditorAssetLibrary.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Blueprint.h"
#include "HAL/FileManager.h"
#include "Describers/BpGraphDescriber.h"
#include "Describers/BtGraphDescriber.h"
#include "Services/ProjectIndexSchema.h"
#include "Misc/FileHelper.h"
#include "Engine/UserDefinedEnum.h"
#include "Misc/EngineVersionComparison.h"
#if UE_VERSION_OLDER_THAN(5, 5, 0)
#include "Engine/UserDefinedStruct.h"
#else
#include "StructUtils/UserDefinedStruct.h"
#endif
#include "Engine/DataAsset.h"
#include "Engine/DataTable.h"
#include "BehaviorTree/BehaviorTree.h"
#include "Kismet2/StructureEditorUtils.h"
#include "UserDefinedStructure/UserDefinedStructEditorData.h"
#include "Misc/FileHelper.h"
#include "AssetCompilingManager.h"
#include "Misc/Paths.h"
#include "Interfaces/IPluginManager.h"
#include "GenericPlatform/GenericPlatformHttp.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "AssetReferenceManager.h"
#include "Async/Async.h"
#include "Utils/WidgetUtils.h"
#include "SWebBrowser.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "SUECPAssetMentionPopup.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "Engine/Texture.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "AgentRunnerTypes.h"
#include "Managers/FreeTierConfigManager.h"
#include "UECPCoreModule.h"
#include "Services/IUECPScannerService.h"

#define LOCTEXT_NAMESPACE "SUECPMainWidget"

FReply SUECPMainWidget::OnProjectInputKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (HandleGlobalKeyPress(InKeyEvent))
	{
		return FReply::Handled();
	}
	if (bAssetPickerOpen && ProjectAssetPickerPopup.IsValid())
	{
		if (InKeyEvent.GetKey() == EKeys::Escape || InKeyEvent.GetKey() == EKeys::Enter || InKeyEvent.GetKey() == EKeys::Up || InKeyEvent.GetKey() == EKeys::Down)
		{
			if (ProjectAssetPickerPopup->HandleKeyDown(InKeyEvent))
			{
				return FReply::Handled();
			}
		}
	}
	if (InKeyEvent.GetKey() == EKeys::Enter && !InKeyEvent.IsShiftDown())
	{
		OnSendProjectQuestionClicked();
		return FReply::Handled();
	}
	if (InKeyEvent.GetKey() == EKeys::V && InKeyEvent.IsControlDown() && !InKeyEvent.IsShiftDown() && !InKeyEvent.IsAltDown())
	{
		if (TryPasteImageFromClipboard(ProjectAttachedImages))
		{
			RefreshProjectImagePreview();
			return FReply::Handled();
		}
	}
	return FReply::Unhandled();
}

void SUECPMainWidget::UpdateScanStatus(const FString& Message, bool bIsError)
{
	if (ScanStatusTextBlock.IsValid())
	{
		ScanStatusTextBlock->SetText(FText::FromString(Message));
		ScanStatusTextBlock->SetColorAndOpacity(bIsError ? FLinearColor::Red : CurrentTheme.ScannerColor);
	}
	if (AppBridgeObject) AppBridgeObject->PushScanStatus(Message);
}

void SUECPMainWidget::CheckForExistingIndex()
{
	const FString SavePath = FPaths::ProjectSavedDir() / TEXT("AI") / TEXT("ProjectIndex.json");
	if (!FPaths::FileExists(SavePath))
	{
		UpdateScanStatus(TEXT("Project index not found. Please scan the project to begin."));
		return;
	}

	FString Header;
	if (FFileHelper::LoadFileToString(Header, *SavePath))
	{
		const FString VersionTag = FString::Printf(TEXT("\"%s\""), UECPProjectIndex::MetaVersion);
		const int32 VersionIdx = Header.Find(VersionTag, ESearchCase::CaseSensitive);
		if (VersionIdx == INDEX_NONE)
		{
			UpdateScanStatus(TEXT("Project index is from an older format. Please re-scan to enable queries."), true);
			return;
		}
		const int32 ColonIdx = Header.Find(TEXT(":"), ESearchCase::CaseSensitive, ESearchDir::FromStart, VersionIdx);
		if (ColonIdx != INDEX_NONE)
		{
			const FString Tail = Header.Mid(ColonIdx + 1, 20).TrimStart();
			if (FCString::Atoi(*Tail) != UECPProjectIndex::SchemaVersion)
			{
				UpdateScanStatus(TEXT("Project index is from an older format. Please re-scan to enable queries."), true);
				return;
			}
		}
	}

	UpdateScanStatus(TEXT("Project index found. You can ask questions or re-scan for updates."));
}

void SUECPMainWidget::RefreshProjectChatView()
{
	if (!AppBridgeObject) return;

	TArray<TSharedPtr<FJsonValue>> Messages;
	for (int32 i = 0; i < ProjectConversationHistory.Num(); ++i)
	{
		const TSharedPtr<FJsonObject> MsgObj = ProjectConversationHistory[i]->AsObject();
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
		FString Source;
		if (MsgObj->TryGetStringField(TEXT("source"), Source) && !Source.IsEmpty())
		{
			Msg->SetStringField(TEXT("source"), Source);
			double Ms = 0.0;
			if (MsgObj->TryGetNumberField(TEXT("source_ms"), Ms))
				Msg->SetNumberField(TEXT("source_ms"), Ms);
		}
		Messages.Add(MakeShareable(new FJsonValueObject(Msg)));
	}

	FString Json;
	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Json);
	FJsonSerializer::Serialize(Messages, W);

	AppBridgeObject->PushChatHistory(TEXT("scanner"), Json);

	if (bIsProjectThinking)
	{
		FString StatusText = CurrentProjectStepInfo.IsEmpty()
			? TEXT("AI is working..")
			: FString::Printf(TEXT("AI is working — %s"), *CurrentProjectStepInfo);
		AppBridgeObject->PushThinkingState(TEXT("scanner"), true, StatusText, TEXT(""));
	}
	else
	{
		AppBridgeObject->ExecJs(TEXT("if(typeof onGenerationDone==='function')onGenerationDone('scanner')"));
	}
}

FReply SUECPMainWidget::OnStopProjectClicked()
{
	if (TSharedPtr<IHttpRequest, ESPMode::ThreadSafe>* FoundReq = PendingProjectRequests.Find(ActiveProjectChatID))
	{
		if (FoundReq->IsValid()) (*FoundReq)->CancelRequest();
		PendingProjectRequests.Remove(ActiveProjectChatID);
	}
	StopAgentInstance(ActiveProjectChatID);
	bIsProjectThinking = false;
	CurrentProjectStepInfo = TEXT("");

	TSharedPtr<FJsonObject> CancelMsg = MakeShared<FJsonObject>();
	CancelMsg->SetStringField(TEXT("role"), TEXT("model"));
	TSharedPtr<FJsonObject> CancelPart = MakeShared<FJsonObject>();
	CancelPart->SetStringField(TEXT("text"), TEXT("_Request cancelled by user._"));
	TArray<TSharedPtr<FJsonValue>> Parts;
	Parts.Add(MakeShared<FJsonValueObject>(CancelPart));
	CancelMsg->SetArrayField(TEXT("parts"), Parts);
	ProjectConversationHistory.Add(MakeShared<FJsonValueObject>(CancelMsg));

	SaveProjectChatHistory(ActiveProjectChatID);
	RefreshProjectChatView();
	return FReply::Handled();
}

void SUECPMainWidget::SendProjectChatRequest()
{
	IUECPCoreModule::Get().GetScannerService().SendChatRequest();
}

void SUECPMainWidget::OnProjectApiResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful)
{
	IUECPCoreModule::Get().GetScannerService().OnApiResponseReceived(Request, Response, bWasSuccessful);
}

#undef LOCTEXT_NAMESPACE
