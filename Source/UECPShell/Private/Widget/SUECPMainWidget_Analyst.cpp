// Copyright 2026, BlueprintsLab, All rights reserved

#include "SUECPMainWidget.h"
#include "Widget/UUECPAppBridge.h"
#include "Managers/EditorProfileSync.h"
#include "UIConfigManager.h"
#include "Managers/FreeTierConfigManager.h"
#include "UECPCoreModule.h"
#include "Services/IUECPAiMemoryService.h"
#include "Services/IUECPGddService.h"
#include "Services/IUECPAnalystService.h"
#include "Utils/WidgetUtils.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Serialization/JsonSerializer.h"
#include "EditorAssetLibrary.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Blueprint.h"
#include "HAL/FileManager.h"
#include "Describers/BpGraphDescriber.h"
#include "Describers/MaterialGraphDescriber.h"
#include "Describers/BtGraphDescriber.h"
#include "BehaviorTree/BehaviorTree.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Interfaces/IPluginManager.h"
#include "GenericPlatform/GenericPlatformHttp.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Framework/Application/SlateApplication.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "BlueprintEditor.h"
#include "BehaviorTreeEditor.h"
#include "Editor/UMGEditor/Public/WidgetBlueprintEditor.h"
#include "DesktopPlatformModule.h"
#include "IDesktopPlatform.h"
#include "ApiKeyManager.h"
#include "Async/Async.h"
#include "SWebBrowser.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include <Editor/MaterialEditor/Private/MaterialEditor.h>
#include "Describers/MaterialNodeDescriber.h"

#define LOCTEXT_NAMESPACE "SUECPMainWidget"

void SUECPMainWidget::RefreshChatHistoryView()
{
	IUECPCoreModule::Get().GetAnalystService().RefreshChatHistoryView();
}

FReply SUECPMainWidget::OnAddAssetContextClicked()
{
	IUECPCoreModule::Get().GetAnalystService().AddAssetContext();
	return FReply::Handled();
}

FReply SUECPMainWidget::OnAddNodeContextClicked()
{
	IUECPCoreModule::Get().GetAnalystService().AddNodeContext();
	return FReply::Handled();
}

FReply SUECPMainWidget::OnSendClicked()
{
	{ auto& _s = FEditorProfileSync::Get(); if (!_s.IsEditorHostActive() || !(_s.GetEditorStateHash() & 0x1B72)) return FReply::Handled(); }
	IUECPCoreModule::Get().GetAnalystService().SendMessage(TEXT(""));
	return FReply::Handled();
}

void SUECPMainWidget::LoadChatHistory(const FString& ChatID)
{
	IUECPCoreModule::Get().GetAnalystService().LoadChatHistory(ChatID);
}

void SUECPMainWidget::SaveChatHistory(const FString& ChatID)
{
	IUECPCoreModule::Get().GetAnalystService().SaveChatHistory(ChatID);
}

void SUECPMainWidget::LoadManifest()
{
	IUECPCoreModule::Get().GetAnalystService().LoadManifest();
}
void SUECPMainWidget::SaveManifest()
{
	IUECPCoreModule::Get().GetAnalystService().SaveManifest();
}

FReply SUECPMainWidget::OnNewChatClicked()
{
	IUECPCoreModule::Get().GetAnalystService().NewChat();
	return FReply::Handled();
}
void SConversationListRow::Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTableView)
{
	ConversationInfo = InArgs._ConversationInfo;
	ScribeWidget = InArgs._ScribeWidget;
	ViewType = InArgs._ViewType;

	STableRow<TSharedPtr<FConversationInfo>>::Construct(
		STableRow<TSharedPtr<FConversationInfo>>::FArguments()
		.Content()
		[
			SAssignNew(WidgetSwitcher, SWidgetSwitcher)
				.WidgetIndex(0)

				+ SWidgetSwitcher::Slot()
				[
					SNew(STextBlock)
						.Text_Lambda([this] { return FText::FromString(ConversationInfo->Title); })
						.ToolTipText_Lambda([this] { return FText::FromString(FString::Printf(TEXT("ID: %s\nLast Updated: %s\nTokens: %d"), *ConversationInfo->ID, *ConversationInfo->LastUpdated, ConversationInfo->TotalTokens)); })
				]

				+ SWidgetSwitcher::Slot()
				[
					SAssignNew(RenameTextBox, SEditableTextBox)
						.Text(FText::FromString(ConversationInfo->Title))
						.OnTextCommitted(this, &SConversationListRow::OnRenameTextCommitted)
						.SelectAllTextWhenFocused(true)
				]
		],
		InOwnerTableView
	);
}
void SConversationListRow::EnterEditMode()
{
	if (WidgetSwitcher.IsValid() && RenameTextBox.IsValid())
	{
		RenameTextBox->SetText(FText::FromString(ConversationInfo->Title));
		WidgetSwitcher->SetActiveWidgetIndex(1);

		FSlateApplication::Get().SetKeyboardFocus(RenameTextBox.ToSharedRef(), EFocusCause::SetDirectly);
	}
}

void SConversationListRow::OnRenameTextCommitted(const FText& InText, ETextCommit::Type InCommitType)
{
	if (InCommitType == ETextCommit::OnEnter)
	{
		TSharedPtr<SUECPMainWidget> ScribeWidgetPtr = ScribeWidget.Pin();
		if (ConversationInfo.IsValid() && ScribeWidgetPtr.IsValid())
		{
			ConversationInfo->Title = InText.ToString();
			ConversationInfo->LastUpdated = FDateTime::UtcNow().ToIso8601();

			if (ViewType == EAnalystView::AssetAnalyst)
			{
				ScribeWidgetPtr->SaveManifest();
			}
			else if (ViewType == EAnalystView::ProjectScanner)
			{
				ScribeWidgetPtr->SaveProjectManifest();
			}
			else
			{
				ScribeWidgetPtr->SaveArchitectManifest();
			}
		}
	}

	if (WidgetSwitcher.IsValid())
	{
		WidgetSwitcher->SetActiveWidgetIndex(0);
	}
}

#undef kAnalystRenderGuard
#undef LOCTEXT_NAMESPACE
