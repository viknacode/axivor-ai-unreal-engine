// Copyright 2026, BlueprintsLab, All rights reserved

#include "SUECPMainWidget.h"
#include "MCP_EditorSubsystem.h"
#include "Widget/UUECPAppBridge.h"
#include "Containers/Ticker.h"
#include "Managers/PlanManager.h"
#include "Managers/EditorProfileSync.h"
#include "LearningManager.h"
#include "UIConfigManager.h"
#include "Managers/FreeTierConfigManager.h"
#include "Managers/ChatHistoryManager.h"
#include "UECPCoreModule.h"
#include "Services/IUECPAgentRunnerService.h"
#include "Services/IUECPAiMemoryService.h"
#include "Services/IUECPGddService.h"
#include "Services/IUECPAnalystService.h"
#include "Services/IUECPArchitectService.h"
#include "Services/IUECPCrewService.h"
#include "Services/UECPToolSafety.h"
#include "Describers/BpSummarizer.h"
#include "Managers/UpdateManager.h"
#include "Utils/WidgetUtils.h"
#include "Utils/TokenUtils.h"
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
#include "SUECPAssetMentionPopup.h"
#include "AssetReferenceManager.h"
#include "ApiKeyManager.h"
#include "UECPShellModule.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/Base64.h"
#include "Async/Async.h"
#include "SWebBrowser.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include <Editor/MaterialEditor/Private/MaterialEditor.h>
#include "Describers/MaterialNodeDescriber.h"

#if PLATFORM_WINDOWS
#include "Windows/WindowsPlatformMisc.h"
#include "Windows/AllowWindowsPlatformTypes.h"
#include <Windows.h>
#include "Windows/HideWindowsPlatformTypes.h"
#elif PLATFORM_MAC
#include "Mac/MacApplication.h"
#include "Cocoa/Cocoa.h"
#include <AppKit/AppKit.h>
#endif

#define LOCTEXT_NAMESPACE "SUECPMainWidget"

FReply SUECPMainWidget::OnAddArchitectAssetContextClicked()
{
	FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
	TArray<FAssetData> SelectedAssetsData;
	ContentBrowserModule.Get().GetSelectedAssets(SelectedAssetsData);

	if (SelectedAssetsData.Num() != 1)
	{
		SetArchitectInputText(UIConfig::GetValue(TEXT("error_select_one_asset"), TEXT("Error: Please select exactly one asset in Content Browser.")));
		return FReply::Handled();
	}

	UObject* SelectedObject = SelectedAssetsData[0].GetAsset();
	if (!SelectedObject)
	{
		SetArchitectInputText(UIConfig::GetValue(TEXT("error_invalid_asset"), TEXT("Error: The selected asset is invalid or could not be loaded.")));
		return FReply::Handled();
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
		FString ErrorMsg = UIConfig::GetValue(TEXT("error_unsupported_asset_type"), TEXT("Error: The selected asset ('{0}') is not a supported type for summary (Blueprint, Material, or Behavior Tree)."));
		ErrorMsg.ReplaceInline(TEXT("{0}"), *SelectedObject->GetClass()->GetName());
		SetArchitectInputText(ErrorMsg);
		return FReply::Handled();
	}

	if (RawSummary.IsEmpty())
	{
		SetArchitectInputText(UIConfig::GetValue(TEXT("error_empty_summary"), TEXT("Error: Could not generate a raw summary for selected asset. It might be empty or corrupted.")));
		return FReply::Handled();
	}

	PendingArchitectContext = FString::Printf(TEXT("Here is context for %s '%s':\n\n--- Data ---\n%s\n---"), *AssetTypeName, *SelectedObject->GetName(), *RawSummary);
	FString Msg = UIConfig::GetValue(TEXT("msg_asset_context_attached_architect"), TEXT("Context for asset '{0}' attached. Now, what would you like to do?"));
	Msg.ReplaceInline(TEXT("{0}"), *SelectedObject->GetName());
	SetArchitectInputText(Msg);

	return FReply::Handled();
}

FReply SUECPMainWidget::OnAddArchitectNodeContextClicked()
{
	UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
	if (!AssetEditorSubsystem) { return FReply::Handled(); }

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
		SetArchitectInputText(UIConfig::GetValue(TEXT("error_no_active_editor"), TEXT("Error: No active asset editor found. Please open a Blueprint, Material, or Behavior Tree.")));
		return FReply::Handled();
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
	else if (EditorName == FName(TEXT("MaterialEditor")))
	{
		FMaterialEditor* MaterialEditor = static_cast<FMaterialEditor*>(ActiveEditor);
		SelectedNodes = MaterialEditor->GetSelectedNodes();
		bFoundEditor = true;
		FMaterialNodeDescriber Describer;
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
		FString ErrorMsg = UIConfig::GetValue(TEXT("error_unsupported_editor"), TEXT("Error: The active editor ('{0}') is not supported for node selection."));
		ErrorMsg.ReplaceInline(TEXT("{0}"), *EditorName.ToString());
		SetArchitectInputText(ErrorMsg);
		return FReply::Handled();
	}

	if (SelectedNodes.Num() == 0)
	{
		SetArchitectInputText(UIConfig::GetValue(TEXT("error_no_nodes_selected"), TEXT("Error: No nodes are selected in active graph editor.")));
		return FReply::Handled();
	}

	PendingArchitectContext = FString::Printf(TEXT("Here is context for currently selected nodes:\n\n--- Node Data ---\n%s\n---"), *NodesSummary);
	SetArchitectInputText(UIConfig::GetValue(TEXT("msg_node_context_attached_architect"), TEXT("Context for selected nodes has been attached. Now, what would you like to do?")));

	return FReply::Handled();
}

FReply SUECPMainWidget::OnImportArchitectContextClicked()
{
	{ auto& _s = FEditorProfileSync::Get(); if (!_s.IsEditorHostActive() || !(_s.GetEditorStateHash() & 0x1B72)) return FReply::Handled(); }
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (!DesktopPlatform)
	{
		UE_LOG(LogUECPShell, Error, TEXT("Desktop platform not available."));
		return FReply::Handled();
	}

	TArray<FString> OutFiles;
	const FString FileTypes = TEXT("Text Files (*.txt)|*.txt|Markdown Files (*.md)|*.md|All Files (*.*)|*.*");

	bool bOpened = DesktopPlatform->OpenFileDialog(
		FSlateApplication::Get().GetActiveTopLevelWindow()->GetNativeWindow()->GetOSWindowHandle(),
		TEXT("Import Context File"),
		FPaths::ProjectSavedDir() / TEXT("Exported Conversations"),
		TEXT(""),
		FileTypes,
		EFileDialogFlags::None,
		OutFiles
	);

	if (bOpened && OutFiles.Num() > 0)
	{
		const FString& FilePath = OutFiles[0];
		FString FileContent;

		if (FFileHelper::LoadFileToString(FileContent, *FilePath))
		{
			FString FileName = FPaths::GetCleanFilename(FilePath);

			FAttachedFileContext NewFile;
			NewFile.FileName = FileName;
			NewFile.Content = FileContent;
			NewFile.CharCount = FileContent.Len();
			NewFile.bExpanded = false;

			ArchitectAttachedFiles.Add(NewFile);
			if (AppBridgeObject)
				AppBridgeObject->PushFileContexts(TEXT("architect"), UUECPAppBridge::BuildFileContextsJson(ArchitectAttachedFiles));

			FString Msg = UIConfig::GetValue(TEXT("msg_file_context_attached_architect"), TEXT("Context from file '{0}' attached. Now, what would you like to do?"));
			Msg.ReplaceInline(TEXT("{0}"), *FileName);
			SetArchitectInputText(Msg);

			{
				int32 CurrentTokens = 0;
				TSharedPtr<FConversationInfo>* CurrentConv = ArchitectConversationList.FindByPredicate([&](const TSharedPtr<FConversationInfo>& Info) { return Info->ID == ActiveArchitectChatID; });
				if (CurrentConv != nullptr)
				{
					CurrentTokens = (*CurrentConv)->TotalTokens;
				}
				for (const FAttachedFileContext& File : ArchitectAttachedFiles)
				{
					CurrentTokens += File.CharCount / 4;
				}
				PushArchitectTokenCount(CurrentTokens);
			}

			SaveArchitectChatHistory(ActiveArchitectChatID);
		}
		else
		{
			FString ErrorMsg = UIConfig::GetValue(TEXT("error_file_read"), TEXT("Error: Could not read the file '{0}'."));
			ErrorMsg.ReplaceInline(TEXT("{0}"), *FilePath);
			SetArchitectInputText(ErrorMsg);
		}
	}

	return FReply::Handled();
}

void SUECPMainWidget::LoadAndAttachImage(const FString& ImagePath, TArray<FAttachedImage>& Attachments)
{
	TArray<uint8> FileData;
	if (!FFileHelper::LoadFileToArray(FileData, *ImagePath))
	{
		UE_LOG(LogUECPShell, Error, TEXT("Failed to load image: %s"), *ImagePath);
		return;
	}

	const int64 MaxImageSize = 20 * 1024 * 1024;
	if (FileData.Num() > MaxImageSize)
	{
		UE_LOG(LogUECPShell, Warning, TEXT("Image file too large: %s (%d bytes, max is %d bytes)"), *ImagePath, FileData.Num(), MaxImageSize);
		return;
	}

	FString Base64Data = FBase64::Encode(FileData);
	FString Extension = FPaths::GetExtension(ImagePath).ToLower();
	FString MimeType = TEXT("image/png");
	if (Extension == TEXT("jpg") || Extension == TEXT("jpeg")) MimeType = TEXT("image/jpeg");
	else if (Extension == TEXT("gif")) MimeType = TEXT("image/gif");
	else if (Extension == TEXT("bmp")) MimeType = TEXT("image/bmp");
	else if (Extension == TEXT("webp")) MimeType = TEXT("image/webp");

	FAttachedImage NewImage;
	NewImage.Name = FPaths::GetBaseFilename(ImagePath);
	NewImage.MimeType = MimeType;
	NewImage.Base64Data = Base64Data;
	Attachments.Add(NewImage);
}

bool SUECPMainWidget::TryPasteImageFromClipboard(TArray<FAttachedImage>& Attachments)
{
#if PLATFORM_WINDOWS
	if (!::OpenClipboard(nullptr))
	{
		return false;
	}

	HGLOBAL hClipboardData = ::GetClipboardData(CF_DIB);
	if (!hClipboardData)
	{
		::CloseClipboard();
		return false;
	}

	BITMAPINFO* pBitmapInfo = (BITMAPINFO*)::GlobalLock(hClipboardData);
	if (!pBitmapInfo)
	{
		::GlobalUnlock(hClipboardData);
		::CloseClipboard();
		return false;
	}

	BITMAPINFOHEADER& bih = pBitmapInfo->bmiHeader;
	int32 Width = bih.biWidth;
	int32 Height = FMath::Abs(bih.biHeight);
	int32 Stride = ((Width * bih.biBitCount + 31) / 32) * 4;

	uint8* pPixels = (uint8*)pBitmapInfo + sizeof(BITMAPINFOHEADER);
	if (bih.biBitCount <= 8)
	{
		pPixels = (uint8*)pBitmapInfo + sizeof(BITMAPINFOHEADER) + (bih.biClrUsed * sizeof(RGBQUAD));
	}

	TArray<uint8> PNGData;
	bool bSuccess = false;

	IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
	TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);

	if (ImageWrapper.IsValid())
	{
		TArray<uint8> RawData;
		RawData.SetNumUninitialized(Stride * Height);

		if (bih.biHeight > 0)
		{
			for (int32 y = 0; y < Height; y++)
			{
				FMemory::Memcpy(RawData.GetData() + y * Stride, pPixels + (Height - 1 - y) * Stride, Stride);
			}
		}
		else
		{
			FMemory::Memcpy(RawData.GetData(), pPixels, RawData.Num());
		}

		ERGBFormat RGBFormat = ERGBFormat::BGRA;
		int32 BitDepth = 8;

		if (bih.biBitCount == 24)
		{
			TArray<uint8> BGRAData;
			BGRAData.SetNumUninitialized(Width * Height * 4);
			for (int32 y = 0; y < Height; y++)
			{
				for (int32 x = 0; x < Width; x++)
				{
					int32 SrcIdx = y * Stride + x * 3;
					int32 DstIdx = y * Width * 4 + x * 4;
					BGRAData[DstIdx + 0] = RawData[SrcIdx + 0];
					BGRAData[DstIdx + 1] = RawData[SrcIdx + 1];
					BGRAData[DstIdx + 2] = RawData[SrcIdx + 2];
					BGRAData[DstIdx + 3] = 255;
				}
			}
			RawData = MoveTemp(BGRAData);
		}

		if (ImageWrapper->SetRaw(RawData.GetData(), RawData.Num(), Width, Height, RGBFormat, BitDepth))
		{
			PNGData = ImageWrapper->GetCompressed();
			bSuccess = PNGData.Num() > 0;
		}
	}

	::GlobalUnlock(hClipboardData);
	::CloseClipboard();

	if (bSuccess && PNGData.Num() > 0)
	{
		const int64 MaxImageSize = 20 * 1024 * 1024;
		if (PNGData.Num() > MaxImageSize)
		{
			UE_LOG(LogUECPShell, Warning, TEXT("Clipboard image too large: %d bytes"), PNGData.Num());
			return false;
		}

		FAttachedImage NewImage;
		NewImage.Name = FString::Printf(TEXT("Clipboard_%d"), FDateTime::Now().GetTicks());
		NewImage.MimeType = TEXT("image/png");
		NewImage.Base64Data = FBase64::Encode(PNGData);
		Attachments.Add(NewImage);
		return true;
	}

	return false;
#elif PLATFORM_MAC
	NSPasteboard* Pasteboard = [NSPasteboard generalPasteboard];
	NSArray* Types = [Pasteboard types];
	if (![Types containsObject:NSPasteboardTypeTIFF] && ![Types containsObject:NSPasteboardTypePNG])
	{
		return false;
	}

	NSData* ImageData = nil;
	if ([Types containsObject:NSPasteboardTypePNG])
	{
		ImageData = [Pasteboard dataForType:NSPasteboardTypePNG];
	}
	else if ([Types containsObject:NSPasteboardTypeTIFF])
	{
		ImageData = [Pasteboard dataForType:NSPasteboardTypeTIFF];
	}

	if (!ImageData || [ImageData length] == 0)
	{
		return false;
	}

	const int64 MaxImageSize = 20 * 1024 * 1024;
	if ([ImageData length] > MaxImageSize)
	{
		UE_LOG(LogUECPShell, Warning, TEXT("Clipboard image too large: %llu bytes"), (unsigned long long)[ImageData length]);
		return false;
	}

	TArray<uint8> ImageBytes;
	ImageBytes.SetNumUninitialized([ImageData length]);
	FMemory::Memcpy(ImageBytes.GetData(), [ImageData bytes], [ImageData length]);

	FString MimeType = TEXT("image/png");
	if ([Types containsObject:NSPasteboardTypePNG] == NO)
	{
		IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
		TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::TIFF);

		if (ImageWrapper.IsValid() && ImageWrapper->SetCompressed(ImageBytes.GetData(), ImageBytes.Num()))
		{
			ImageBytes = ImageWrapper->GetCompressed();
			MimeType = TEXT("image/png");
		}
	}

	FAttachedImage NewImage;
	NewImage.Name = FString::Printf(TEXT("Clipboard_%d"), FDateTime::Now().GetTicks());
	NewImage.MimeType = MimeType;
	NewImage.Base64Data = FBase64::Encode(ImageBytes);
	Attachments.Add(NewImage);
	return true;
#else
	return false;
#endif
}

void SUECPMainWidget::RefreshAnalystImagePreview()
{
	if (AppBridgeObject)
		AppBridgeObject->PushImageAttachments(TEXT("analyst"), UUECPAppBridge::BuildImagesJson(AnalystAttachedImages));
}
void SUECPMainWidget::RefreshArchitectImagePreview()
{
	if (AppBridgeObject)
		AppBridgeObject->PushImageAttachments(TEXT("architect"), UUECPAppBridge::BuildImagesJson(ArchitectAttachedImages));
}
void SUECPMainWidget::RefreshProjectImagePreview()
{
	if (AppBridgeObject)
		AppBridgeObject->PushImageAttachments(TEXT("scanner"), UUECPAppBridge::BuildImagesJson(ProjectAttachedImages));
}

FReply SUECPMainWidget::OnAttachAnalystImageClicked()
{
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (!DesktopPlatform) return FReply::Handled();

	TArray<FString> OutFiles;
	const FString FileTypes = TEXT("Image Files|*.png;*.jpg;*.jpeg;*.gif;*.bmp;*.webp|");

	bool bOpened = DesktopPlatform->OpenFileDialog(
		FSlateApplication::Get().GetActiveTopLevelWindow()->GetNativeWindow()->GetOSWindowHandle(),
		TEXT("Select Images"),
		FPaths::ProjectSavedDir(),
		TEXT(""),
		FileTypes,
		EFileDialogFlags::Multiple,
		OutFiles
	);

	if (bOpened && OutFiles.Num() > 0)
	{
		for (const FString& File : OutFiles)
		{
			LoadAndAttachImage(File, AnalystAttachedImages);
		}
		RefreshAnalystImagePreview();
	}

	return FReply::Handled();
}

FReply SUECPMainWidget::OnAttachArchitectImageClicked()
{
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (!DesktopPlatform) return FReply::Handled();

	TArray<FString> OutFiles;
	const FString FileTypes = TEXT("Image Files|*.png;*.jpg;*.jpeg;*.gif;*.bmp;*.webp|");

	bool bOpened = DesktopPlatform->OpenFileDialog(
		FSlateApplication::Get().GetActiveTopLevelWindow()->GetNativeWindow()->GetOSWindowHandle(),
		TEXT("Select Images"),
		FPaths::ProjectSavedDir(),
		TEXT(""),
		FileTypes,
		EFileDialogFlags::Multiple,
		OutFiles
	);

	if (bOpened && OutFiles.Num() > 0)
	{
		for (const FString& File : OutFiles)
		{
			LoadAndAttachImage(File, ArchitectAttachedImages);
		}
		RefreshArchitectImagePreview();
	}

	return FReply::Handled();
}

FReply SUECPMainWidget::OnAttachProjectImageClicked()
{
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (!DesktopPlatform) return FReply::Handled();

	TArray<FString> OutFiles;
	const FString FileTypes = TEXT("Image Files|*.png;*.jpg;*.jpeg;*.gif;*.bmp;*.webp|");

	bool bOpened = DesktopPlatform->OpenFileDialog(
		FSlateApplication::Get().GetActiveTopLevelWindow()->GetNativeWindow()->GetOSWindowHandle(),
		TEXT("Select Images"),
		FPaths::ProjectSavedDir(),
		TEXT(""),
		FileTypes,
		EFileDialogFlags::Multiple,
		OutFiles
	);

	if (bOpened && OutFiles.Num() > 0)
	{
		for (const FString& File : OutFiles)
		{
			LoadAndAttachImage(File, ProjectAttachedImages);
		}
		RefreshProjectImagePreview();
	}

	return FReply::Handled();
}

FReply SConversationListRow::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		TSharedPtr<SUECPMainWidget> ScribeWidgetPtr = ScribeWidget.Pin();
		if (ScribeWidgetPtr.IsValid())
		{
			TSharedPtr<SWidget> ContextMenu = ScribeWidgetPtr->OnGetContextMenuForChatList(ViewType, ConversationInfo);
			if (ContextMenu.IsValid())
			{
				FSlateApplication::Get().PushMenu(
					AsShared(),
					FWidgetPath(),
					ContextMenu.ToSharedRef(),
					MouseEvent.GetScreenSpacePosition(),
					FPopupTransitionEffect(FPopupTransitionEffect::ContextMenu)
				);
				return FReply::Handled();
			}
		}
	}
	return STableRow<TSharedPtr<FConversationInfo>>::OnMouseButtonUp(MyGeometry, MouseEvent);
}

TSharedRef<ITableRow> SUECPMainWidget::OnGenerateRowForChatList(TSharedPtr<FConversationInfo> InItem, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(SConversationListRow, OwnerTable)
		.ConversationInfo(InItem)
		.ScribeWidget(StaticCastSharedRef<SUECPMainWidget>(AsShared()))
	    .ViewType(EAnalystView::AssetAnalyst);
}

void SUECPMainWidget::OnChatSelectionChanged(TSharedPtr<FConversationInfo> InItem, ESelectInfo::Type SelectInfo)
{
	IUECPCoreModule::Get().GetAnalystService().SelectChat(InItem, SelectInfo == ESelectInfo::Direct);
}

TSharedPtr<SWidget> SUECPMainWidget::OnGetContextMenuForChatList(EAnalystView ForView, TSharedPtr<FConversationInfo> TargetItem)
{
	TSharedPtr<FConversationInfo> SelectedItem;

	if (TargetItem.IsValid())
	{
		SelectedItem = TargetItem;
		if (ForView == EAnalystView::AssetAnalyst && ChatListView.IsValid())
			ChatListView->SetSelection(TargetItem);
		else if (ForView == EAnalystView::ProjectScanner && ProjectChatListView.IsValid())
			ProjectChatListView->SetSelection(TargetItem);
		else if (ArchitectChatListView.IsValid())
			ArchitectChatListView->SetSelection(TargetItem);
	}
	else
	{
		TArray<TSharedPtr<FConversationInfo>> SelectedItems;
		if (ForView == EAnalystView::AssetAnalyst)
			SelectedItems = ChatListView->GetSelectedItems();
		else if (ForView == EAnalystView::ProjectScanner)
			SelectedItems = ProjectChatListView->GetSelectedItems();
		else
			SelectedItems = ArchitectChatListView->GetSelectedItems();

		if (SelectedItems.Num() == 0) return nullptr;
		SelectedItem = SelectedItems[0];
	}

	const bool bShouldCloseWindowAfterMenuSelection = true;
	FMenuBuilder MenuBuilder(bShouldCloseWindowAfterMenuSelection, nullptr);

	MenuBuilder.AddMenuEntry(LOCTEXT("RenameChatLabel", "Rename"), FText(), FSlateIcon(), FUIAction(
		FExecuteAction::CreateLambda([this, SelectedItem, ForView]() {
			TSharedPtr<SListView<TSharedPtr<FConversationInfo>>> TargetListView;
			if (ForView == EAnalystView::AssetAnalyst)
			{
				TargetListView = ChatListView;
			}
			else if (ForView == EAnalystView::ProjectScanner)
			{
				TargetListView = ProjectChatListView;
			}
			else
			{
				TargetListView = ArchitectChatListView;
			}
			if (SelectedItem.IsValid() && TargetListView.IsValid())
			{
				TSharedPtr<ITableRow> RowWidget = TargetListView->WidgetFromItem(SelectedItem);
				if (RowWidget.IsValid())
				{
					StaticCastSharedPtr<SConversationListRow>(RowWidget)->EnterEditMode();
				}
			}
			})
	));

	MenuBuilder.AddMenuEntry(LOCTEXT("DeleteChatLabel", "Delete"), FText(), FSlateIcon(), FUIAction(
		FExecuteAction::CreateLambda([this, SelectedItem, ForView]() {
			if (!SelectedItem.IsValid()) return;

			EAppReturnType::Type Result = FMessageDialog::Open(
				EAppMsgType::YesNo,
				FText::Format(
					LOCTEXT("DeleteChatConfirm", "Delete conversation \"{0}\"?\n\nThis cannot be undone."),
					FText::FromString(SelectedItem->Title)
				)
			);
			if (Result != EAppReturnType::Yes) return;

			if (ForView == EAnalystView::AssetAnalyst)
			{
				ConversationList.Remove(SelectedItem);
				SaveManifest();
				PushAnalystChatListToJs();
				if (ChatListView.IsValid()) ChatListView->RequestListRefresh();
				FString FilePath = FPaths::ProjectSavedDir() / TEXT("CodeExplainer") / SelectedItem->ID + TEXT(".json");
				IFileManager::Get().Delete(*FilePath);
				if (ActiveChatID == SelectedItem->ID)
				{
					ActiveChatID.Empty();
					if (ConversationList.Num() > 0)
					{
						if (ChatListView.IsValid()) ChatListView->SetSelection(ConversationList[0]);
					}
					else
						OnNewChatClicked();
				}
			}
			else if (ForView == EAnalystView::ProjectScanner)
			{
				ProjectConversationList.Remove(SelectedItem);
				SaveProjectManifest();
				PushScannerChatListToJs();
				if (ProjectChatListView.IsValid()) ProjectChatListView->RequestListRefresh();
				FString FilePath = FPaths::ProjectSavedDir() / TEXT("CodeExplainer") / TEXT("project_chats") / SelectedItem->ID + TEXT(".json");
				IFileManager::Get().Delete(*FilePath);
				if (ActiveProjectChatID == SelectedItem->ID)
				{
					ActiveProjectChatID.Empty();
					if (ProjectConversationList.Num() > 0)
					{
						if (ProjectChatListView.IsValid()) ProjectChatListView->SetSelection(ProjectConversationList[0]);
					}
					else
						OnNewProjectChatClicked();
				}
			}
			else
			{
				{
					FGuid CrewRunId; FString CrewRoleId;
					if (IUECPCoreModule::IsAvailable()
						&& IUECPCoreModule::Get().GetCrewService().IsCrewChat(SelectedItem->ID, CrewRunId, CrewRoleId))
					{
						FMessageDialog::Open(EAppMsgType::Ok,
							LOCTEXT("CrewChatProtected",
								"This chat is bound to an active crew run. Abort or complete the run before deleting the chat."));
						return;
					}
				}

				StopAgentInstance(SelectedItem->ID);
				if (PendingArchitectRequests.Contains(SelectedItem->ID))
				{
					if (auto Req = PendingArchitectRequests.FindRef(SelectedItem->ID))
						Req->CancelRequest();
					PendingArchitectRequests.Remove(SelectedItem->ID);
				}
				ArchitectThinkingChats.Remove(SelectedItem->ID);
				ArchitectInteractionModeByChat.Remove(SelectedItem->ID);
				ArchitectApiKeySlotByChat.Remove(SelectedItem->ID);

				ArchitectConversationList.Remove(SelectedItem);
				SaveArchitectManifest();
				PushArchitectChatListToJs();
				if (ArchitectChatListView.IsValid()) ArchitectChatListView->RequestListRefresh();
				FString FilePath = FPaths::ProjectSavedDir() / TEXT("CodeExplainer") / TEXT("architect_chats") / SelectedItem->ID + TEXT(".json");
				IFileManager::Get().Delete(*FilePath);
				if (ActiveArchitectChatID == SelectedItem->ID)
				{
					bIsArchitectThinking = false;
					ActiveArchitectChatID.Empty();
					if (ArchitectConversationList.Num() > 0)
					{
						if (ArchitectChatListView.IsValid()) ArchitectChatListView->SetSelection(ArchitectConversationList[0]);
					}
					else
						OnNewArchitectChatClicked();
				}
			}
			})
	));

	MenuBuilder.BeginSection("ExportActions", LOCTEXT("ExportActionsSection", "Export"));
	{
		auto CanExportLambda = [this, SelectedItem, ForView]() {
			return SelectedItem.IsValid() &&
				((ForView == EAnalystView::AssetAnalyst && ActiveChatID == SelectedItem->ID) ||
					(ForView == EAnalystView::ProjectScanner && ActiveProjectChatID == SelectedItem->ID) ||
					(ForView == EAnalystView::BlueprintArchitect && ActiveArchitectChatID == SelectedItem->ID));
			};

		auto GetHistoryForView = [this, ForView]() -> TArray<TSharedPtr<FJsonValue>>* {
			if (ForView == EAnalystView::AssetAnalyst)
			{
				return &AnalystConversationHistory;
			}
			else if (ForView == EAnalystView::ProjectScanner)
			{
				return &ProjectConversationHistory;
			}
			else
			{
				return &ArchitectConversationHistory;
			}
		};

		MenuBuilder.AddMenuEntry(
			LOCTEXT("ExportTxtLabel", "Export as .txt"),
			LOCTEXT("ExportTxtTooltip", "Saves the conversation as a plain text file."),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([this, SelectedItem, GetHistoryForView]() {
					if (SelectedItem.IsValid())
					{
						TArray<TSharedPtr<FJsonValue>>* HistoryToExport = GetHistoryForView();
						FChatHistoryManager::ExportConversationToFile(SelectedItem->ID, SelectedItem->Title, TEXT("txt"), *HistoryToExport);
					}
					}),
				FCanExecuteAction::CreateLambda(CanExportLambda)
			)
		);

		MenuBuilder.AddMenuEntry(
			LOCTEXT("ExportMdLabel", "Export as .md"),
			LOCTEXT("ExportMdTooltip", "Saves the conversation as a Markdown file."),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([this, SelectedItem, GetHistoryForView]() {
					if (SelectedItem.IsValid())
					{
						TArray<TSharedPtr<FJsonValue>>* HistoryToExport = GetHistoryForView();
						FChatHistoryManager::ExportConversationToFile(SelectedItem->ID, SelectedItem->Title, TEXT("md"), *HistoryToExport);
					}
					}),
				FCanExecuteAction::CreateLambda(CanExportLambda)
			)
		);
	}
	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

void SUECPMainWidget::LoadProjectManifest()
{
	ProjectConversationList = FChatHistoryManager::Get().LoadManifest(EConversationViewType::Project);

	PushScannerChatListToJs();
	if (ProjectChatListView.IsValid())
		ProjectChatListView->RequestListRefresh();

	if (ProjectConversationList.Num() > 0)
	{
		OnProjectChatSelectionChanged(ProjectConversationList[0], ESelectInfo::OnMouseClick);
	}
	else
	{
		OnNewProjectChatClicked();
	}
}
FReply SUECPMainWidget::OnNewProjectChatClicked()
{
	FAssetReferenceManager::Get().UnlinkAll();

	static FThreadSafeCounter ProjectChatIdCounter;
	FString NewID = FString::Printf(TEXT("proj_%lld_%d"),
		FDateTime::UtcNow().GetTicks(),
		ProjectChatIdCounter.Increment());
	TSharedPtr<FConversationInfo> NewConversation = MakeShared<FConversationInfo>();
	NewConversation->ID = NewID;
	NewConversation->Title = TEXT("New Project Chat");
	NewConversation->LastUpdated = FDateTime::UtcNow().ToIso8601();
	ProjectConversationList.Insert(NewConversation, 0);

	ActiveProjectChatID = NewID;
	bIsProjectThinking = false;
	CurrentProjectStepInfo = TEXT("");
	ProjectConversationHistory.Empty();
	RefreshProjectChatView();
	PushScannerChatListToJs();

	if (ProjectChatListView.IsValid())
	{
		ProjectChatListView->RequestListRefresh();
		ProjectChatListView->SetSelection(NewConversation);
	}
	SaveProjectManifest();
	return FReply::Handled();
}
void SUECPMainWidget::SaveProjectManifest()
{
	FChatHistoryManager::Get().SaveManifest(EConversationViewType::Project, ProjectConversationList);
}
void SUECPMainWidget::OnProjectChatSelectionChanged(TSharedPtr<FConversationInfo> InItem, ESelectInfo::Type SelectInfo)
{
	if (InItem.IsValid())
	{
		if (SelectInfo == ESelectInfo::Direct && InItem->ID == ActiveProjectChatID)
			return;
		ActiveProjectChatID = InItem->ID;
		bIsProjectThinking = PendingProjectRequests.Contains(InItem->ID) || AgentInstances.Contains(InItem->ID);
		if (bIsProjectThinking) CurrentProjectStepInfo = TEXT("AI is working..");
		if (AppBridgeObject)
		{
			const FString Label = bIsProjectThinking ? CurrentProjectStepInfo : FString();
			AppBridgeObject->PushThinkingState(TEXT("scanner"), bIsProjectThinking, Label, FString());
		}
		LoadProjectChatHistory(ActiveProjectChatID);
	}
	else
	{
		if (!ActiveProjectChatID.IsEmpty())
		{
			for (const auto& Conv : ProjectConversationList)
			{
				if (Conv->ID == ActiveProjectChatID)
				{
					if (ProjectChatListView.IsValid())
						ProjectChatListView->SetSelection(Conv, ESelectInfo::Direct);
					return;
				}
			}
		}
		ActiveProjectChatID.Empty();
		ProjectConversationHistory.Empty();
		RefreshProjectChatView();
	}
}
TSharedRef<ITableRow> SUECPMainWidget::OnGenerateRowForProjectChatList(TSharedPtr<FConversationInfo> InItem, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(SConversationListRow, OwnerTable)
		.ConversationInfo(InItem)
		.ScribeWidget(StaticCastSharedRef<SUECPMainWidget>(AsShared()))
		.ViewType(EAnalystView::ProjectScanner);
}
void SUECPMainWidget::SaveProjectChatHistory(const FString& ChatID)
{
	if (ChatID.IsEmpty() || ProjectConversationHistory.Num() == 0)
	{
		return;
	}

	FChatHistoryManager::Get().SaveChatHistory(EConversationViewType::Project, ChatID, ProjectConversationHistory);
}

void SUECPMainWidget::LoadProjectChatHistory(const FString& ChatID)
{
	ProjectConversationHistory.Empty();
	if (ChatID.IsEmpty())
	{
		RefreshProjectChatView();
		return;
	}

	ProjectConversationHistory = FChatHistoryManager::Get().LoadChatHistory(EConversationViewType::Project, ChatID);

	if (ProjectConversationHistory.Num() >= 1)
	{
		static const FString LegacyGreeting = TEXT("Hello! You can ask me questions about your project now that it has been scanned.");
		TSharedPtr<FJsonObject> FirstObj = ProjectConversationHistory[0].IsValid()
			? ProjectConversationHistory[0]->AsObject() : nullptr;
		if (FirstObj.IsValid())
		{
			FString Role;
			FirstObj->TryGetStringField(TEXT("role"), Role);
			if (Role == TEXT("model"))
			{
				const TArray<TSharedPtr<FJsonValue>>* Parts = nullptr;
				if (FirstObj->TryGetArrayField(TEXT("parts"), Parts) && Parts->Num() > 0)
				{
					TSharedPtr<FJsonObject> PartObj = (*Parts)[0].IsValid() ? (*Parts)[0]->AsObject() : nullptr;
					if (PartObj.IsValid())
					{
						FString Text;
						PartObj->TryGetStringField(TEXT("text"), Text);
						if (Text == LegacyGreeting)
						{
							ProjectConversationHistory.RemoveAt(0);
						}
					}
				}
			}
		}
	}

	RefreshProjectChatView();
}

FReply SUECPMainWidget::OnSendProjectQuestionClicked()
{
	FString UserInput = ScannerPendingBridgeText;
	if (UserInput.IsEmpty() || bIsProjectThinking || ActiveProjectChatID.IsEmpty())
	{
		return FReply::Handled();
	}
	ScannerPendingBridgeText.Empty();
	SetScannerInputText(TEXT(""));

	if (ProjectConversationHistory.Num() <= 1)
	{
		FString NewTitle = UserInput;
		if (NewTitle.Len() > 40)
		{
			NewTitle = NewTitle.Left(37) + TEXT("...");
		}

		TSharedPtr<FConversationInfo>* FoundInfo = ProjectConversationList.FindByPredicate(
			[&](const TSharedPtr<FConversationInfo>& Info) { return Info->ID == ActiveProjectChatID; });

		if (FoundInfo)
		{
			(*FoundInfo)->Title = NewTitle;
			SaveProjectManifest();
			PushScannerChatListToJs();
			if (ProjectChatListView.IsValid())
				ProjectChatListView->RequestListRefresh();
		}
	}

	TSharedPtr<FJsonObject> UserContent = MakeShareable(new FJsonObject);
	UserContent->SetStringField(TEXT("role"), TEXT("user"));
	TArray<TSharedPtr<FJsonValue>> UserParts;
	TSharedPtr<FJsonObject> UserPartText = MakeShareable(new FJsonObject);
	UserPartText->SetStringField(TEXT("text"), UserInput);
	UserParts.Add(MakeShareable(new FJsonValueObject(UserPartText)));
	for (const FAttachedImage& Image : ProjectAttachedImages)
	{
		TSharedPtr<FJsonObject> ImagePart = MakeShareable(new FJsonObject);
		TSharedPtr<FJsonObject> InlineData = MakeShareable(new FJsonObject);
		InlineData->SetStringField(TEXT("mime_type"), Image.MimeType);
		InlineData->SetStringField(TEXT("data"), Image.Base64Data);
		ImagePart->SetObjectField(TEXT("inline_data"), InlineData);
		UserParts.Add(MakeShareable(new FJsonValueObject(ImagePart)));
	}
	UserContent->SetArrayField(TEXT("parts"), UserParts);
	ProjectConversationHistory.Add(MakeShareable(new FJsonValueObject(UserContent)));

	SaveProjectChatHistory(ActiveProjectChatID);

	ProjectAttachedImages.Empty();
	RefreshProjectImagePreview();

	bIsProjectThinking = true;
	RefreshProjectChatView();
	SendProjectChatRequest();

	return FReply::Handled();
}

FReply SUECPMainWidget::OnArchitectViewSelected()
{
	FetchTemplateCacheAsync();
	return FReply::Handled();
}

FReply SUECPMainWidget::OnNewArchitectChatClicked()
{
	FAssetReferenceManager::Get().UnlinkAll();

	static FThreadSafeCounter ArchitectChatIdCounter;
	FString NewID = FString::Printf(TEXT("arch_%lld_%d"),
		FDateTime::UtcNow().GetTicks(),
		ArchitectChatIdCounter.Increment());
	TSharedPtr<FConversationInfo> NewConversation = MakeShared<FConversationInfo>();
	NewConversation->ID = NewID;
	NewConversation->Title = TEXT("New Architect Chat");
	NewConversation->LastUpdated = FDateTime::UtcNow().ToIso8601();
	const EAIInteractionMode SeedMode = FSettingsManager::Get().LoadDefaultInteractionMode();
	NewConversation->Mode = InteractionModeToString(SeedMode);
	ArchitectInteractionModeByChat.Add(NewID, SeedMode);

	ArchitectConversationList.Insert(NewConversation, 0);

	ActiveArchitectChatID = NewID;
	ArchitectInteractionMode = SeedMode;
	bIsArchitectThinking = false;
	CurrentToolStepInfo = TEXT("");
	ArchitectConversationHistory.Empty();
	{
		int32 TokenCount = EstimateFullRequestTokens(ArchitectConversationHistory);
		PushArchitectTokenCount(TokenCount);
	}

	if (AppBridgeObject)
	{
		const FString ModeStr = InteractionModeToString(SeedMode);
		AppBridgeObject->PushViewState(FString::Printf(
			TEXT("{\"activeView\":\"architect\",\"interactionMode\":\"%s\",\"apiKeySlotIndex\":-1,\"chatId\":\"%s\",\"isCrewChat\":false}"),
			*ModeStr, *NewID));
	}

	RefreshArchitectChatView();
	NotifyPlanUpdated(ActiveArchitectChatID); NotifyTasksUpdated(ActiveArchitectChatID);
	PushArchitectChatListToJs();

	if (ArchitectChatListView.IsValid())
	{
		ArchitectChatListView->RequestListRefresh();
		ArchitectChatListView->SetSelection(NewConversation);
	}

	SaveArchitectManifest();
	return FReply::Handled();
}

void SUECPMainWidget::SaveArchitectManifest()
{
	FChatHistoryManager::Get().SaveManifest(EConversationViewType::Architect, ArchitectConversationList);
}

void SUECPMainWidget::LoadArchitectManifest()
{
	ArchitectConversationList = FChatHistoryManager::Get().LoadManifest(EConversationViewType::Architect);

	ArchitectInteractionModeByChat.Empty();
	ArchitectApiKeySlotByChat.Empty();
	for (const TSharedPtr<FConversationInfo>& Conv : ArchitectConversationList)
	{
		if (!Conv.IsValid() || Conv->ID.IsEmpty()) continue;
		if (!Conv->Mode.IsEmpty())
		{
			ArchitectInteractionModeByChat.Add(
				Conv->ID,
				StringToInteractionMode(Conv->Mode, FSettingsManager::Get().LoadDefaultInteractionMode()));
		}
		if (Conv->ApiKeySlotIndex >= 0)
		{
			ArchitectApiKeySlotByChat.Add(Conv->ID, Conv->ApiKeySlotIndex);
		}
	}

	PushArchitectChatListToJs();
	if (ArchitectChatListView.IsValid())
	{
		ArchitectChatListView->RequestListRefresh();
	}

	if (ArchitectConversationList.Num() > 0)
	{
		TSharedPtr<FConversationInfo> Initial;
		if (IUECPCoreModule::IsAvailable())
		{
			IUECPCrewService& Crew = IUECPCoreModule::Get().GetCrewService();
			for (const TSharedPtr<FConversationInfo>& Conv : ArchitectConversationList)
			{
				if (!Conv.IsValid() || Conv->ID.IsEmpty()) continue;
				FGuid CrewRunId; FString CrewRoleId;
				if (!Crew.IsCrewChat(Conv->ID, CrewRunId, CrewRoleId))
				{
					Initial = Conv;
					break;
				}
			}
		}
		if (!Initial.IsValid()) Initial = ArchitectConversationList[0];
		OnArchitectChatSelectionChanged(Initial, ESelectInfo::OnMouseClick);
	}
	else
	{
		OnNewArchitectChatClicked();
	}
}

void SUECPMainWidget::SaveArchitectChatHistory(const FString& ChatID)
{
	if (ChatID.IsEmpty() || ArchitectConversationHistory.Num() == 0)
	{
		return;
	}

	FChatHistoryManager::Get().SaveChatHistory(EConversationViewType::Architect, ChatID, ArchitectConversationHistory);
}

void SUECPMainWidget::DeleteMessageFromHistory(const FString& View, int32 MsgIndex)
{
	TArray<TSharedPtr<FJsonValue>>* History = nullptr;

	if (View == TEXT("architect"))
		History = &ArchitectConversationHistory;
	else if (View == TEXT("scanner"))
		History = &ProjectConversationHistory;
	else if (View == TEXT("analyst"))
		History = &AnalystConversationHistory;

	if (!History || MsgIndex < 0 || MsgIndex >= History->Num())
		return;

	History->RemoveAt(MsgIndex);

	if (View == TEXT("architect"))
	{
		if (LastRenderedArchitectHistoryCount > MsgIndex)
			LastRenderedArchitectHistoryCount--;

		LastRenderedArchitectRanges.Reset();
		ArchitectRangeHtmlCache.Reset();

		if (!ActiveArchitectChatID.IsEmpty())
			FChatHistoryManager::Get().SaveChatHistory(
				EConversationViewType::Architect, ActiveArchitectChatID, ArchitectConversationHistory);
	}
	else if (View == TEXT("scanner"))
	{
		if (!ActiveProjectChatID.IsEmpty())
			FChatHistoryManager::Get().SaveChatHistory(
				EConversationViewType::Project, ActiveProjectChatID, ProjectConversationHistory);
	}
}

bool SUECPMainWidget::MaybeFinalizeCompactReplacement(const FString& ChatID)
{
	if (CompactPendingChatID.IsEmpty() || CompactPendingChatID != ChatID)
		return false;

	if (ChatID != ActiveArchitectChatID)
	{
		CompactPendingChatID.Reset();
		return false;
	}

	FString SummaryText;
	for (int32 i = ArchitectConversationHistory.Num() - 1; i >= 0; --i)
	{
		const TSharedPtr<FJsonValue>& Val = ArchitectConversationHistory[i];
		if (!Val.IsValid() || Val->Type != EJson::Object) continue;
		TSharedPtr<FJsonObject> Msg = Val->AsObject();
		if (!Msg.IsValid()) continue;
		FString Role;
		Msg->TryGetStringField(TEXT("role"), Role);
		if (Role != TEXT("model")) continue;
		const TArray<TSharedPtr<FJsonValue>>* Parts = nullptr;
		if (!Msg->TryGetArrayField(TEXT("parts"), Parts) || !Parts || Parts->Num() == 0) continue;
		TSharedPtr<FJsonObject> P0 = (*Parts)[0].IsValid() ? (*Parts)[0]->AsObject() : nullptr;
		if (!P0.IsValid()) continue;
		FString Text;
		P0->TryGetStringField(TEXT("text"), Text);
		if (!Text.IsEmpty())
		{
			SummaryText = MoveTemp(Text);
			break;
		}
	}

	CompactPendingChatID.Reset();

	if (SummaryText.IsEmpty())
		return false;

	static const FString Banner = TEXT("**[Conversation compacted — context summary below]**\n\n");

	ArchitectConversationHistory.Empty();
	{
		TSharedPtr<FJsonObject> Msg = MakeShareable(new FJsonObject);
		Msg->SetStringField(TEXT("role"), TEXT("model"));
		TSharedPtr<FJsonObject> Part = MakeShareable(new FJsonObject);
		Part->SetStringField(TEXT("text"), Banner + SummaryText);
		TArray<TSharedPtr<FJsonValue>> Parts;
		Parts.Add(MakeShareable(new FJsonValueObject(Part)));
		Msg->SetArrayField(TEXT("parts"), Parts);
		ArchitectConversationHistory.Add(MakeShareable(new FJsonValueObject(Msg)));
	}

	IUECPCoreModule::Get().GetArchitectService().ClearNativeHistoryForChat(ChatID);

	bArchitectForceFullRebuildOnce = true;
	LastRenderedArchitectRanges.Reset();
	ArchitectRangeHtmlCache.Reset();
	LastRenderedArchitectHistoryCount = 0;

	return true;
}

void SUECPMainWidget::DrainQueuedArchitectMessage(const FString& ChatID)
{
	TArray<FQueuedArchitectMessage>* Queue = ArchitectMessageQueueByChat.Find(ChatID);
	if (!Queue || Queue->Num() == 0)
		return;

	const bool bIsActive = (ChatID == ActiveArchitectChatID);
	bool bIsCrewChat = false;
	if (!bIsActive && IUECPCoreModule::IsAvailable())
	{
		FGuid CrewRunId; FString CrewRoleId;
		bIsCrewChat = IUECPCoreModule::Get().GetCrewService().IsCrewChat(ChatID, CrewRunId, CrewRoleId);
	}
	if (!bIsActive && !bIsCrewChat)
		return;

	FQueuedArchitectMessage Next = MoveTemp((*Queue)[0]);
	Queue->RemoveAt(0);
	if (Queue->Num() == 0)
		ArchitectMessageQueueByChat.Remove(ChatID);

	if (bIsActive)
	{
		ArchitectPendingBridgeText = Next.Text;
		ArchitectAttachedFiles     = MoveTemp(Next.Files);
		ArchitectAttachedImages    = MoveTemp(Next.Images);
		SetArchitectInputText(Next.Text);
		RefreshArchitectImagePreview();
		PushArchitectQueueStateToJs();

		OnSendArchitectMessageClicked();
		return;
	}

	const FString OriginalActive = ActiveArchitectChatID;
	TArray<TSharedPtr<FJsonValue>> OriginalHistory = MoveTemp(ArchitectConversationHistory);
	const bool bOriginalSuppress = bSuppressChatViewRefresh;
	bSuppressChatViewRefresh = true;

	ActiveArchitectChatID = ChatID;
	ArchitectConversationHistory = FChatHistoryManager::Get().LoadChatHistory(
		EConversationViewType::Architect, ChatID);

	ArchitectPendingBridgeText = Next.Text;
	ArchitectAttachedFiles     = MoveTemp(Next.Files);
	ArchitectAttachedImages    = MoveTemp(Next.Images);
	OnSendArchitectMessageClicked();
	SaveArchitectChatHistory(ChatID);

	ActiveArchitectChatID = OriginalActive;
	ArchitectConversationHistory = MoveTemp(OriginalHistory);
	bIsArchitectThinking =
		ArchitectThinkingChats.Contains(OriginalActive)
		|| PendingArchitectRequests.Contains(OriginalActive);
	bSuppressChatViewRefresh = bOriginalSuppress;

	PushArchitectChatListToJs();
	RefreshArchitectChatView();
}

void SUECPMainWidget::PushArchitectQueueStateToJs()
{
	if (!AppBridgeObject) return;

	const TArray<FQueuedArchitectMessage>* Queue =
		ArchitectMessageQueueByChat.Find(ActiveArchitectChatID);

	auto JsEscape = [](const FString& In)
	{
		return FString(In)
			.Replace(TEXT("\\"), TEXT("\\\\"))
			.Replace(TEXT("'"),  TEXT("\\'"))
			.Replace(TEXT("\r"), TEXT(" "))
			.Replace(TEXT("\n"), TEXT(" "));
	};

	TStringBuilder<256> Previews;
	Previews.Append(TEXT("["));
	if (Queue)
	{
		for (int32 i = 0; i < Queue->Num(); ++i)
		{
			FString Preview = (*Queue)[i].Text;
			if (Preview.Len() > 80) Preview = Preview.Left(77) + TEXT("...");
			Previews.Appendf(TEXT("%s'%s'"), i == 0 ? TEXT("") : TEXT(","), *JsEscape(Preview));
		}
	}
	Previews.Append(TEXT("]"));

	const int32 Count = Queue ? Queue->Num() : 0;
	const FString Script = FString::Printf(
		TEXT("if(typeof onArchitectQueueState==='function')onArchitectQueueState(%d, %s)"),
		Count, Previews.ToString());
	AppBridgeObject->ExecJs(Script);
}

void SUECPMainWidget::SaveArchitectWorkingNotes(const FString& ChatID)
{
	if (ChatID.IsEmpty())
	{
		return;
	}

	const FString NotesDir = FPaths::ProjectSavedDir() / TEXT("BpGeneratorUltimate") / TEXT("architect_notes");
	IFileManager::Get().MakeDirectory(*NotesDir, true);
	const FString NotesPath = NotesDir / (ChatID + TEXT(".txt"));

	const FString* FoundNotes = ArchitectWorkingNotesByChat.Find(ChatID);
	const FString Notes = FoundNotes ? *FoundNotes : FString();
	if (Notes.TrimStartAndEnd().IsEmpty())
	{
		if (FPaths::FileExists(NotesPath))
		{
			IFileManager::Get().Delete(*NotesPath, false, true);
		}
		return;
	}

	FFileHelper::SaveStringToFile(Notes, *NotesPath);
}

void SUECPMainWidget::LoadArchitectWorkingNotes(const FString& ChatID)
{
	if (ChatID.IsEmpty())
	{
		return;
	}

	const FString NotesPath = FPaths::ProjectSavedDir() / TEXT("BpGeneratorUltimate") / TEXT("architect_notes") / (ChatID + TEXT(".txt"));
	FString Notes;
	if (FFileHelper::LoadFileToString(Notes, *NotesPath))
	{
		ArchitectWorkingNotesByChat.Add(ChatID, Notes);
	}
	else
	{
		ArchitectWorkingNotesByChat.Remove(ChatID);
	}
}

FString SUECPMainWidget::GetArchitectWorkingNotesForAI(const FString& ChatID) const
{
	const FString* FoundNotes = ArchitectWorkingNotesByChat.Find(ChatID);
	if (!FoundNotes || FoundNotes->TrimStartAndEnd().IsEmpty())
	{
		return FString();
	}

	return FString::Printf(
		TEXT("=== DURABLE WORKING NOTES ===\n%s\n=== END DURABLE WORKING NOTES ===\n"),
		**FoundNotes);
}

void SUECPMainWidget::LoadArchitectChatHistory(const FString& ChatID)
{
	ArchitectConversationHistory.Empty();
	InvalidatePromptCaches();
	if (ChatID.IsEmpty())
	{
		RefreshArchitectChatView();
		return;
	}

	ArchitectConversationHistory = FChatHistoryManager::Get().LoadChatHistory(EConversationViewType::Architect, ChatID);
	LoadArchitectWorkingNotes(ChatID);

	if (ArchitectConversationHistory.Num() >= 1)
	{
		static const FString LegacyGreeting = TEXT("Hello! I am the Blueprint Architect. Tell me what you want to create or modify.");
		TSharedPtr<FJsonObject> FirstObj = ArchitectConversationHistory[0].IsValid()
			? ArchitectConversationHistory[0]->AsObject() : nullptr;
		if (FirstObj.IsValid())
		{
			FString Role;
			FirstObj->TryGetStringField(TEXT("role"), Role);
			if (Role == TEXT("model"))
			{
				const TArray<TSharedPtr<FJsonValue>>* Parts = nullptr;
				if (FirstObj->TryGetArrayField(TEXT("parts"), Parts) && Parts->Num() > 0)
				{
					TSharedPtr<FJsonObject> PartObj = (*Parts)[0].IsValid() ? (*Parts)[0]->AsObject() : nullptr;
					if (PartObj.IsValid())
					{
						FString Text;
						PartObj->TryGetStringField(TEXT("text"), Text);
						if (Text == LegacyGreeting)
						{
							ArchitectConversationHistory.RemoveAt(0);
						}
					}
				}
			}
		}
	}

	RefreshArchitectChatView();
}

TSharedRef<ITableRow> SUECPMainWidget::OnGenerateRowForArchitectChatList(TSharedPtr<FConversationInfo> InItem, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(SConversationListRow, OwnerTable)
		.ConversationInfo(InItem)
		.ScribeWidget(StaticCastSharedRef<SUECPMainWidget>(AsShared()))
		.ViewType(EAnalystView::BlueprintArchitect);
}

void SUECPMainWidget::OnArchitectChatSelectionChanged(TSharedPtr<FConversationInfo> InItem, ESelectInfo::Type SelectInfo)
{
	if (InItem.IsValid())
	{
		if (SelectInfo == ESelectInfo::Direct && InItem->ID == ActiveArchitectChatID)
			return;

		if (!ActiveArchitectChatID.IsEmpty() && ArchitectConversationHistory.Num() > 0
			&& !NativeLoopStreamingChats.Contains(ActiveArchitectChatID))
		{
			SaveArchitectChatHistory(ActiveArchitectChatID);
		}

		bIsArchitectThinking = ArchitectThinkingChats.Contains(InItem->ID)
			|| PendingArchitectRequests.Contains(InItem->ID);
		if (!bIsArchitectThinking)
		{
			CurrentToolCallDepth = 0;
			ConsecutiveSameToolCount = 0;
			LastDispatchedToolName.Empty();
			LastDispatchedToolArgsHash.Empty();
			CurrentToolStepInfo.Empty();
		}

		ActiveArchitectChatID = InItem->ID;
		{
			EAIInteractionMode TargetMode;
			if (const EAIInteractionMode* Found = ArchitectInteractionModeByChat.Find(InItem->ID))
			{
				TargetMode = *Found;
			}
			else
			{
				TargetMode = InItem->Mode.IsEmpty()
					? FSettingsManager::Get().LoadDefaultInteractionMode()
					: StringToInteractionMode(InItem->Mode, FSettingsManager::Get().LoadDefaultInteractionMode());
				ArchitectInteractionModeByChat.Add(InItem->ID, TargetMode);
			}
			ArchitectInteractionMode = TargetMode;
			if (AppBridgeObject)
			{
				const FString ModeStr = InteractionModeToString(TargetMode);
				const int32 SlotIdx = ArchitectApiKeySlotByChat.Contains(InItem->ID)
					? ArchitectApiKeySlotByChat[InItem->ID]
					: -1;
				FGuid CrewRunId; FString CrewRoleId;
				const bool bIsCrew = IUECPCoreModule::IsAvailable()
					&& IUECPCoreModule::Get().GetCrewService().IsCrewChat(InItem->ID, CrewRunId, CrewRoleId);
				const FString ViewStateJson = FString::Printf(
					TEXT("{\"activeView\":\"architect\",\"interactionMode\":\"%s\",\"apiKeySlotIndex\":%d,\"chatId\":\"%s\",\"isCrewChat\":%s}"),
					*ModeStr, SlotIdx, *InItem->ID, bIsCrew ? TEXT("true") : TEXT("false"));
				AppBridgeObject->PushViewState(ViewStateJson);
			}
		}

		if (AppBridgeObject)
		{
			const FString Label = bIsArchitectThinking
				? (CurrentToolStepInfo.IsEmpty() ? FString(TEXT("AI is working...")) : CurrentToolStepInfo)
				: FString();
			AppBridgeObject->PushThinkingState(TEXT("architect"), bIsArchitectThinking, Label, FString());
		}
		FAgentRunnerInstance* RunningInst = GetAgentInstanceForChat(InItem->ID);
		if (RunningInst && RunningInst->SourceView != EAgentSourceView::ProjectScanner)
		{
			ArchitectConversationHistory = RunningInst->LocalHistory;
			RefreshArchitectChatView();
		}
		else if (NativeLoopStreamingChats.Contains(InItem->ID))
		{
			if (const TArray<TSharedPtr<FJsonValue>>* LiveHist =
				IUECPCoreModule::Get().GetArchitectService().GetStreamingDisplayHistory(InItem->ID))
			{
				ArchitectConversationHistory = *LiveHist;
			}
			RefreshArchitectChatView();
		}
		else
		{
			LoadArchitectChatHistory(ActiveArchitectChatID);
		}
		NotifyPlanUpdated(ActiveArchitectChatID); NotifyTasksUpdated(ActiveArchitectChatID);

		{
			int32 TokenCount = EstimateFullRequestTokens(ArchitectConversationHistory);
			PushArchitectTokenCount(TokenCount);
		}

		PushArchitectQueueStateToJs();
		if (!bIsArchitectThinking)
			DrainQueuedArchitectMessage(ActiveArchitectChatID);
	}
	else
	{
		if (!ActiveArchitectChatID.IsEmpty())
		{
			for (const auto& Conv : ArchitectConversationList)
			{
				if (Conv->ID == ActiveArchitectChatID)
				{
					if (ArchitectChatListView.IsValid())
						ArchitectChatListView->SetSelection(Conv, ESelectInfo::Direct);
					return;
				}
			}
		}
		ActiveArchitectChatID.Empty();
		ArchitectConversationHistory.Empty();
		RefreshArchitectChatView();
		if (AppBridgeObject) AppBridgeObject->ExecJs(TEXT("var p=document.getElementById('plan-panel-architect');if(p)p.style.display='none';"));
		PushArchitectTokenCount(0);
	}
}

FReply SUECPMainWidget::OnArchitectInputKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (HandleGlobalKeyPress(InKeyEvent))
	{
		return FReply::Handled();
	}
	if (bAssetPickerOpen && ArchitectAssetPickerPopup.IsValid())
	{
		if (InKeyEvent.GetKey() == EKeys::Escape || InKeyEvent.GetKey() == EKeys::Enter || InKeyEvent.GetKey() == EKeys::Up || InKeyEvent.GetKey() == EKeys::Down)
		{
			if (ArchitectAssetPickerPopup->HandleKeyDown(InKeyEvent))
			{
				return FReply::Handled();
			}
		}
	}
	if (InKeyEvent.GetKey() == EKeys::Enter && !InKeyEvent.IsShiftDown())
	{
		OnSendArchitectMessageClicked();
		return FReply::Handled();
	}
	if (InKeyEvent.GetKey() == EKeys::V && InKeyEvent.IsControlDown() && !InKeyEvent.IsShiftDown() && !InKeyEvent.IsAltDown())
	{
		if (TryPasteImageFromClipboard(ArchitectAttachedImages))
		{
			RefreshArchitectImagePreview();
			return FReply::Handled();
		}
	}
	return FReply::Unhandled();
}

void SUECPMainWidget::OnArchitectInputTextChanged(const FText& NewText)
{
	FString Text = NewText.ToString();

	if (bAssetPickerOpen && ArchitectAssetPickerPopup.IsValid() && AtSymbolPosition != INDEX_NONE)
	{
		if (AtSymbolPosition >= Text.Len() || Text[AtSymbolPosition] != TEXT('@'))
		{
			bAssetPickerOpen = false;
			AtSymbolPosition = INDEX_NONE;
			PreviousArchitectInputText = Text;
			return;
		}

		if (AtSymbolPosition < Text.Len() - 1)
		{
			FString SearchQuery = Text.RightChop(AtSymbolPosition + 1);
			if (SearchQuery.StartsWith(TEXT(" ")))
			{
				bAssetPickerOpen = false;
				AtSymbolPosition = INDEX_NONE;
				PreviousArchitectInputText = Text;
				return;
			}
			ArchitectAssetPickerPopup->UpdateSearchFilter(SearchQuery);
		}
		else
		{
			ArchitectAssetPickerPopup->UpdateSearchFilter(TEXT(""));
		}
		PreviousArchitectInputText = Text;
		return;
	}

	if (!bAssetPickerOpen)
	{
		int32 PrevLen = PreviousArchitectInputText.Len();
		if (Text.Len() > PrevLen)
		{
			int32 AtIndex = Text.Find(TEXT("@"), ESearchCase::IgnoreCase, ESearchDir::FromEnd);
			if (AtIndex != INDEX_NONE)
			{
				bool bWasJustTyped = false;
				if (AtIndex >= PrevLen || PreviousArchitectInputText[AtIndex] != TEXT('@'))
				{
					bWasJustTyped = true;
				}

				if (bWasJustTyped)
				{
					AtSymbolPosition = AtIndex;
					ShowAssetPicker();
				}
			}
		}
	}

	PreviousArchitectInputText = Text;
}

void SUECPMainWidget::OnProjectInputTextChanged(const FText& NewText)
{
	FString Text = NewText.ToString();

	if (bAssetPickerOpen && ProjectAssetPickerPopup.IsValid() && AtSymbolPosition != INDEX_NONE)
	{
		if (AtSymbolPosition >= Text.Len() || Text[AtSymbolPosition] != TEXT('@'))
		{
			bAssetPickerOpen = false;
			AtSymbolPosition = INDEX_NONE;
			PreviousProjectInputText = Text;
			return;
		}

		if (AtSymbolPosition < Text.Len() - 1)
		{
			FString SearchQuery = Text.RightChop(AtSymbolPosition + 1);
			if (SearchQuery.StartsWith(TEXT(" ")))
			{
				bAssetPickerOpen = false;
				AtSymbolPosition = INDEX_NONE;
				PreviousProjectInputText = Text;
				return;
			}
			ProjectAssetPickerPopup->UpdateSearchFilter(SearchQuery);
		}
		else
		{
			ProjectAssetPickerPopup->UpdateSearchFilter(TEXT(""));
		}
		PreviousProjectInputText = Text;
		return;
	}

	if (!bAssetPickerOpen)
	{
		int32 PrevLen = PreviousProjectInputText.Len();
		if (Text.Len() > PrevLen)
		{
			int32 AtIndex = Text.Find(TEXT("@"), ESearchCase::IgnoreCase, ESearchDir::FromEnd);
			if (AtIndex != INDEX_NONE)
			{
				bool bWasJustTyped = false;
				if (AtIndex >= PrevLen || PreviousProjectInputText[AtIndex] != TEXT('@'))
				{
					bWasJustTyped = true;
				}

				if (bWasJustTyped)
				{
					AtSymbolPosition = AtIndex;
					ShowAssetPickerForProject();
				}
			}
		}
	}

	PreviousProjectInputText = Text;
}

void SUECPMainWidget::ShowAssetPicker()
{
	if (!ArchitectAssetPickerPopup.IsValid())
	{
		return;
	}

	bAssetPickerOpen = true;
	AssetPickerSourceView = 0;

	FString SearchQuery;
	{
		FText CurrentText = FText::FromString(ArchitectPendingBridgeText);
		FString TextStr = CurrentText.ToString();

		if (AtSymbolPosition != INDEX_NONE && AtSymbolPosition < TextStr.Len() - 1)
		{
			SearchQuery = TextStr.RightChop(AtSymbolPosition + 1);
		}
	}

	ArchitectAssetPickerPopup->RebuildAssetList();
	ArchitectAssetPickerPopup->UpdateSearchFilter(SearchQuery);
}

void SUECPMainWidget::ShowAssetPickerForProject()
{
	if (!ProjectAssetPickerPopup.IsValid())
	{
		return;
	}

	bAssetPickerOpen = true;
	AssetPickerSourceView = 1;

	FString SearchQuery;
	{
		FString TextStr = ScannerPendingBridgeText;
		if (AtSymbolPosition != INDEX_NONE && AtSymbolPosition < TextStr.Len() - 1)
		{
			SearchQuery = TextStr.RightChop(AtSymbolPosition + 1);
		}
	}

	ProjectAssetPickerPopup->RebuildAssetList();
	ProjectAssetPickerPopup->UpdateSearchFilter(SearchQuery);
}

void SUECPMainWidget::OnAssetRefSelected(const FAssetRefItem& SelectedItem)
{
	FString TextStr;

	if (AssetPickerSourceView == 0)
	{
		TextStr = ArchitectPendingBridgeText;

		if (AtSymbolPosition != INDEX_NONE)
		{
			FString BeforeAt = TextStr.Left(AtSymbolPosition);
			FString AssetRef = FString::Printf(TEXT("@%s"), *SelectedItem.Label);
			TextStr = BeforeAt + AssetRef + TEXT(" ");
		}

		ArchitectPendingBridgeText = TextStr;
		SetArchitectInputText(TextStr);
		PreviousArchitectInputText = TextStr;
	}
	else if (AssetPickerSourceView == 1)
	{
		TextStr = ScannerPendingBridgeText;

		if (AtSymbolPosition != INDEX_NONE)
		{
			FString BeforeAt = TextStr.Left(AtSymbolPosition);
			FString AssetRef = FString::Printf(TEXT("@%s"), *SelectedItem.Label);
			TextStr = BeforeAt + AssetRef + TEXT(" ");
		}

		ScannerPendingBridgeText = TextStr;
		SetScannerInputText(TextStr);
		PreviousProjectInputText = TextStr;
	}

	bAssetPickerOpen = false;
	AtSymbolPosition = INDEX_NONE;

	FAssetReferenceManager::Get().LinkAsset(SelectedItem.AssetPath, SelectedItem.Label, SelectedItem.Type);
}

void SUECPMainWidget::OnAssetPickerDismissed()
{
	bAssetPickerOpen = false;
	AtSymbolPosition = INDEX_NONE;

	if (AssetPickerSourceView == 0)
	{
		PreviousArchitectInputText = ArchitectPendingBridgeText;
	}
	else if (AssetPickerSourceView == 1)
	{
		PreviousProjectInputText = ScannerPendingBridgeText;
	}
}

FReply SUECPMainWidget::OnSendArchitectMessageClicked()
{
	FString UserInput = ArchitectPendingBridgeText;
	if (UserInput.IsEmpty() || ActiveArchitectChatID.IsEmpty())
	{
		return FReply::Handled();
	}

	const bool bChatBusy =
		ArchitectThinkingChats.Contains(ActiveArchitectChatID)
		|| PendingArchitectRequests.Contains(ActiveArchitectChatID);
	if (bChatBusy)
	{
		FQueuedArchitectMessage Q;
		Q.Text   = MoveTemp(UserInput);
		Q.Files  = MoveTemp(ArchitectAttachedFiles);
		Q.Images = MoveTemp(ArchitectAttachedImages);
		ArchitectMessageQueueByChat.FindOrAdd(ActiveArchitectChatID).Add(MoveTemp(Q));

		ArchitectPendingBridgeText.Empty();
		ArchitectAttachedFiles.Empty();
		ArchitectAttachedImages.Empty();
		SetArchitectInputText(TEXT(""));
		RefreshArchitectImagePreview();
		PushArchitectQueueStateToJs();
		return FReply::Handled();
	}

	ArchitectPendingBridgeText.Empty();
	SetArchitectInputText(TEXT(""));

	{
		FGuid CrewRunId; FString CrewRoleId;
		const bool bIsCrewChat = IUECPCoreModule::IsAvailable()
			&& IUECPCoreModule::Get().GetCrewService().IsCrewChat(
				ActiveArchitectChatID, CrewRunId, CrewRoleId);

		if (!bIsCrewChat)
		{
			TSharedPtr<FConversationInfo>* ActiveInfo = ArchitectConversationList.FindByPredicate(
				[&](const TSharedPtr<FConversationInfo>& Info) { return Info.IsValid() && Info->ID == ActiveArchitectChatID; });
			if (ActiveInfo && (*ActiveInfo).IsValid())
			{
				(*ActiveInfo)->LastUpdated = FDateTime::UtcNow().ToIso8601();
				PushArchitectChatListToJs();
			}
		}
		else
		{
			PushArchitectChatListToJs();
		}
	}

	for (const FAttachedFileContext& File : ArchitectAttachedFiles)
	{
		TSharedPtr<FJsonObject> FileContextMsg = MakeShareable(new FJsonObject);
		FileContextMsg->SetStringField(TEXT("role"), TEXT("context"));
		FileContextMsg->SetStringField(TEXT("file_name"), File.FileName);
		FileContextMsg->SetStringField(TEXT("content"), File.Content);
		FileContextMsg->SetNumberField(TEXT("char_count"), File.CharCount);
		FileContextMsg->SetBoolField(TEXT("expanded"), File.bExpanded);

		TArray<TSharedPtr<FJsonValue>> FileParts;
		TSharedPtr<FJsonObject> FilePart = MakeShareable(new FJsonObject);
		FilePart->SetStringField(TEXT("text"), FString::Printf(TEXT("--- File: %s (%d chars, click to expand) ---"), *File.FileName, File.CharCount));
		FileParts.Add(MakeShareable(new FJsonValueObject(FilePart)));

		FileContextMsg->SetArrayField(TEXT("parts"), FileParts);
		ArchitectConversationHistory.Add(MakeShareable(new FJsonValueObject(FileContextMsg)));
	}

	ArchitectAttachedFiles.Empty();

	if (ArchitectConversationHistory.Num() <= 1)
	{
		TSharedPtr<FConversationInfo>* FoundInfo = ArchitectConversationList.FindByPredicate(
			[&](const TSharedPtr<FConversationInfo>& Info) { return Info->ID == ActiveArchitectChatID; });

		const bool bHasDefaultTitle = FoundInfo && (*FoundInfo).IsValid()
			&& (*FoundInfo)->Title == TEXT("New Architect Chat");

		if (FoundInfo && bHasDefaultTitle)
		{
			FString NewTitle = UserInput;
			if (NewTitle.Len() > 40)
			{
				NewTitle = NewTitle.Left(37) + TEXT("...");
			}
			(*FoundInfo)->Title = NewTitle;
			SaveArchitectManifest();
			PushArchitectChatListToJs();
			if (ArchitectChatListView.IsValid())
				ArchitectChatListView->RequestListRefresh();
		}
	}

	TSharedPtr<FJsonObject> UserContent = MakeShareable(new FJsonObject);
	UserContent->SetStringField(TEXT("role"), TEXT("user"));
	TArray<TSharedPtr<FJsonValue>> UserParts;
	TSharedPtr<FJsonObject> UserPartText = MakeShareable(new FJsonObject);
	UserPartText->SetStringField(TEXT("text"), UserInput);
	UserParts.Add(MakeShareable(new FJsonValueObject(UserPartText)));
	for (const FAttachedImage& Image : ArchitectAttachedImages)
	{
		TSharedPtr<FJsonObject> ImagePart = MakeShareable(new FJsonObject);
		TSharedPtr<FJsonObject> InlineData = MakeShareable(new FJsonObject);
		InlineData->SetStringField(TEXT("mime_type"), Image.MimeType);
		InlineData->SetStringField(TEXT("data"), Image.Base64Data);
		ImagePart->SetObjectField(TEXT("inline_data"), InlineData);
		UserParts.Add(MakeShareable(new FJsonValueObject(ImagePart)));
	}
	UserContent->SetArrayField(TEXT("parts"), UserParts);
	ArchitectConversationHistory.Add(MakeShareable(new FJsonValueObject(UserContent)));
	PushArchitectTokenCount(EstimateFullRequestTokens(ArchitectConversationHistory));

	if (TSharedPtr<FAgentRunnerInstance> ActiveInst = AgentInstances.FindRef(ActiveArchitectChatID))
	{
		ActiveInst->LocalHistory.Add(MakeShareable(new FJsonValueObject(UserContent)));
	}

	SaveArchitectChatHistory(ActiveArchitectChatID);

	ArchitectAttachedImages.Empty();
	RefreshArchitectImagePreview();
	SessionModifiedBlueprintPaths.Empty();
	ArchitectModifiedBPsThisTurn.Empty();
	SessionBlueprintsPendingStructuralInspection.Empty();
	SessionArchitectBlueprintVerificationIssues.Empty();
	SessionArchitectVerificationBounceCount = 0;

	LastArchitectUserMessageLength = UserInput.Len();
	bIsArchitectThinking = true;
	ArchitectThinkingChats.Add(ActiveArchitectChatID);
	RefreshArchitectChatView();
	SendArchitectChatRequest();
	return FReply::Handled();
}

FReply SUECPMainWidget::OnStopClicked()
{
	bUserCancelledRequest = true;

	const FString TargetChatID = ActiveArchitectChatID;

	TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> CancelledReq;
	if (TSharedPtr<IHttpRequest, ESPMode::ThreadSafe>* FoundReq = PendingArchitectRequests.Find(TargetChatID))
	{
		CancelledReq = *FoundReq;
		if (CancelledReq.IsValid()) CancelledReq->CancelRequest();
		PendingArchitectRequests.Remove(TargetChatID);
	}
	if (CancelledReq.IsValid() && ActiveArchitectHttpRequest == CancelledReq)
		ActiveArchitectHttpRequest.Reset();

	StopAgentInstance(TargetChatID);

	ArchitectThinkingChats.Remove(TargetChatID);

	TSharedPtr<FJsonObject> CancelMsg = MakeShareable(new FJsonObject);
	CancelMsg->SetStringField(TEXT("role"), TEXT("model"));
	TArray<TSharedPtr<FJsonValue>> CancelParts;
	TSharedPtr<FJsonObject> CancelPartText = MakeShareable(new FJsonObject);
	CancelPartText->SetStringField(TEXT("text"), TEXT("_What should I do instead?._"));
	CancelParts.Add(MakeShareable(new FJsonValueObject(CancelPartText)));
	CancelMsg->SetArrayField(TEXT("parts"), CancelParts);
	ArchitectConversationHistory.Add(MakeShareable(new FJsonValueObject(CancelMsg)));
	SaveArchitectChatHistory(TargetChatID);
	bIsArchitectThinking = false;
	RefreshArchitectChatView();
	return FReply::Handled();
}

void SUECPMainWidget::OnJustChatModeClicked(ECheckBoxState NewState)
{
	if (NewState == ECheckBoxState::Checked)
	{
		OnInteractionModeChanged(EAIInteractionMode::JustChat);
	}
}

void SUECPMainWidget::OnAskBeforeEditModeClicked(ECheckBoxState NewState)
{
	if (NewState == ECheckBoxState::Checked)
	{
		OnInteractionModeChanged(EAIInteractionMode::AskBeforeEdit);
	}
}

void SUECPMainWidget::OnAutoEditModeClicked(ECheckBoxState NewState)
{
	if (NewState == ECheckBoxState::Checked)
	{
		OnInteractionModeChanged(EAIInteractionMode::AutoEdit);
	}
}

void SUECPMainWidget::OnPlanModeClicked(ECheckBoxState NewState)
{
	if (NewState == ECheckBoxState::Checked)
	{
		OnInteractionModeChanged(EAIInteractionMode::PlanMode);
	}
}

FString SUECPMainWidget::InteractionModeToString(EAIInteractionMode Mode)
{
	switch (Mode)
	{
	case EAIInteractionMode::JustChat:      return TEXT("chat");
	case EAIInteractionMode::AskBeforeEdit: return TEXT("ask");
	case EAIInteractionMode::PlanMode:      return TEXT("plan");
	case EAIInteractionMode::AutoEdit:
	default:                                return TEXT("auto");
	}
}

EAIInteractionMode SUECPMainWidget::StringToInteractionMode(const FString& Str, EAIInteractionMode Fallback)
{
	if (Str.Equals(TEXT("chat"), ESearchCase::IgnoreCase)) return EAIInteractionMode::JustChat;
	if (Str.Equals(TEXT("ask"),  ESearchCase::IgnoreCase)) return EAIInteractionMode::AskBeforeEdit;
	if (Str.Equals(TEXT("plan"), ESearchCase::IgnoreCase)) return EAIInteractionMode::PlanMode;
	if (Str.Equals(TEXT("auto"), ESearchCase::IgnoreCase)) return EAIInteractionMode::AutoEdit;
	return Fallback;
}

EAIInteractionMode SUECPMainWidget::GetActiveArchitectInteractionMode() const
{
	return GetArchitectInteractionModeForChat(ActiveArchitectChatID);
}

EAIInteractionMode SUECPMainWidget::GetArchitectInteractionModeForChat(const FString& ChatID) const
{
	if (!ChatID.IsEmpty())
	{
		if (const EAIInteractionMode* Found = ArchitectInteractionModeByChat.Find(ChatID))
		{
			return *Found;
		}
	}
	return FSettingsManager::Get().LoadDefaultInteractionMode();
}

void SUECPMainWidget::OnInteractionModeChanged(EAIInteractionMode NewMode)
{
	ArchitectInteractionMode = NewMode;
	if (!ActiveArchitectChatID.IsEmpty())
	{
		ArchitectInteractionModeByChat.Add(ActiveArchitectChatID, NewMode);
		const FString ModeStr = InteractionModeToString(NewMode);
		for (const TSharedPtr<FConversationInfo>& Conv : ArchitectConversationList)
		{
			if (Conv.IsValid() && Conv->ID == ActiveArchitectChatID)
			{
				Conv->Mode = ModeStr;
				SaveArchitectManifest();
				break;
			}
		}

		if (AgentInstances.Contains(ActiveArchitectChatID))
		{
			IUECPCoreModule::Get().GetAgentRunnerService()
				.NotifyInteractionModeChanged(ActiveArchitectChatID);
		}

		if (AppBridgeObject)
		{
			AppBridgeObject->PushViewState(FString::Printf(
				TEXT("{\"interactionMode\":\"%s\",\"chatId\":\"%s\"}"),
				*ModeStr, *ActiveArchitectChatID));
		}
	}
}

bool SUECPMainWidget::IsReadOnlyTool(const FString& ToolName) const
{
	return UECPToolSafety::IsReadOnly(FName(*ToolName));
}

FString SUECPMainWidget::EscapeForJavascript(const FString& Input)
{
	FString Out = Input;
	Out.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
	Out.ReplaceInline(TEXT("'"), TEXT("\\'"));
	Out.ReplaceInline(TEXT("\n"), TEXT("\\n"));
	Out.ReplaceInline(TEXT("\r"), TEXT(""));
	Out.ReplaceInline(TEXT("</script>"), TEXT("<\\/script>"));
	return Out;
}

FString SUECPMainWidget::FormatHttpError(bool bWasSuccessful, FHttpResponsePtr Response, const FString& ProviderHint)
{
	const FString Provider = ProviderHint.IsEmpty() ? TEXT("the AI provider") : ProviderHint;
	const FString SupportLinks = TEXT("\n\n---\nFor help, open **Settings → Info** or press **Ctrl+K** for the Axivor AI command palette.");

	if (!bWasSuccessful || !Response.IsValid())
	{
		return FString::Printf(
			TEXT("**Connection failed**\n\n")
			TEXT("The request did not reach %s. This is usually a **timeout** or a temporary network issue.\n\n")
			TEXT("**Try:**\n- Send the message again\n- Check your internet connection\n- %s may be temporarily down"),
			*Provider, *Provider);
	}

	const int32 Code = Response->GetResponseCode();
	const FString Body = Response->GetContentAsString();

	FString Detail;
	auto TryStringField = [](const TSharedPtr<FJsonObject>& O, const TCHAR* Key, FString& Out) -> bool
	{
		if (O.IsValid() && O->TryGetStringField(Key, Out) && !Out.IsEmpty()) return true;
		Out.Reset();
		return false;
	};
	{
		TSharedPtr<FJsonObject> Obj;
		TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(Body);
		if (FJsonSerializer::Deserialize(R, Obj) && Obj.IsValid())
		{
			const TSharedPtr<FJsonObject>* ErrObj = nullptr;
			if (Obj->TryGetObjectField(TEXT("error"), ErrObj) && ErrObj && ErrObj->IsValid())
			{
				if (!TryStringField(*ErrObj, TEXT("message"), Detail))
				{
					const TSharedPtr<FJsonObject>* Inner = nullptr;
					if ((*ErrObj)->TryGetObjectField(TEXT("error"), Inner) && Inner && Inner->IsValid())
					{
						if (!TryStringField(*Inner, TEXT("message"), Detail))
							TryStringField(*Inner, TEXT("type"), Detail);
					}
				}
				FString ErrType;
				if (TryStringField(*ErrObj, TEXT("type"), ErrType) && ErrType.Len() <= 64
					&& !Detail.Contains(ErrType))
				{
					Detail = Detail.IsEmpty() ? ErrType : (ErrType + TEXT(": ") + Detail);
				}
				FString ErrCodeStr;
				if (Detail.IsEmpty()) TryStringField(*ErrObj, TEXT("code"), ErrCodeStr);
				if (!ErrCodeStr.IsEmpty()) Detail = ErrCodeStr;
			}
			if (Detail.IsEmpty()) TryStringField(Obj, TEXT("error"), Detail);
			if (Detail.IsEmpty()) TryStringField(Obj, TEXT("message"), Detail);
			if (Detail.IsEmpty()) TryStringField(Obj, TEXT("detail"), Detail);
			if (Detail.IsEmpty()) TryStringField(Obj, TEXT("status"), Detail);
		}
		if (Detail.IsEmpty() && !Body.IsEmpty())
		{
			FString Trimmed = Body.TrimStartAndEnd();
			Trimmed.ReplaceInline(TEXT("\r\n"), TEXT(" "));
			Trimmed.ReplaceInline(TEXT("\n"), TEXT(" "));
			Detail = Trimmed.Len() > 400 ? Trimmed.Left(400) + TEXT(" …") : Trimmed;
		}
	}
	const FString DetailLine = Detail.IsEmpty() ? TEXT("") : TEXT("\n\n> ") + Detail;

	switch (Code)
	{
	case 0:
		return FString::Printf(
			TEXT("**No response (code 0)**\n\n")
			TEXT("The server did not respond at all. Check your API endpoint URL in Settings.%s"),
			*DetailLine);

	case 401:
		return FString::Printf(
			TEXT("**Invalid API Key (401)**\n\n")
			TEXT("Your API key was rejected by %s. Open **Settings → API Keys** and check the key for the active slot is correct and hasn't expired.%s"),
			*Provider, *DetailLine);

	case 403:
		return FString::Printf(
			TEXT("**Access Denied (403)**\n\n")
			TEXT("Your API key doesn't have permission to use this model or endpoint. ")
			TEXT("Check your API plan, or try switching to a different model in Settings.%s"),
			*DetailLine);

	case 429:
	{
		FString Msg = FString::Printf(
			TEXT("**Rate Limited (429)**\n\n")
			TEXT("Too many requests — %s is throttling you. Wait a moment, then try again.%s"),
			*Provider, *DetailLine);
		Msg += SupportLinks;
		return Msg;
	}

	case 400:
	{
		const bool bContextOverflow =
			Detail.Contains(TEXT("token")) || Detail.Contains(TEXT("context")) ||
			Detail.Contains(TEXT("length")) || Detail.Contains(TEXT("size")) ||
			Detail.Contains(TEXT("exceed")) || Detail.Contains(TEXT("too long"));
		if (bContextOverflow)
		{
			return TEXT("**Context window exceeded (400)**\n\n")
				   TEXT("The conversation is too long for this model. Start a new chat or reduce the amount of context you're sending (e.g. remove large file attachments).");
		}
		return FString::Printf(
			TEXT("**Bad request (400)**\n\n")
			TEXT("The request was rejected, possibly due to an unsupported parameter or malformed content.%s"),
			*DetailLine);
	}

	case 413:
		return TEXT("**Request too large (413)**\n\n")
			   TEXT("The message or attached file is too large. Try removing attachments or starting a new conversation.");

	case 500:
		return FString::Printf(
			TEXT("**Server error (500)**\n\n")
			TEXT("%s returned an internal server error. This is usually temporary — try again in a moment.%s"),
			*Provider, *DetailLine);

	case 502:
		return FString::Printf(
			TEXT("**Bad gateway (502)**\n\n")
			TEXT("%s is temporarily unreachable. Try again in a moment."),
			*Provider);

	case 503:
		return FString::Printf(
			TEXT("**Service unavailable (503)**\n\n")
			TEXT("%s is currently overloaded or down for maintenance. Try again in a moment."),
			*Provider);

	case 529:
		return FString::Printf(
			TEXT("**Overloaded (529)**\n\n")
			TEXT("%s is currently overloaded with requests. Wait a moment and try again."),
			*Provider);

	default:
		return FString::Printf(
			TEXT("**API Error (%d)**\n\n")
			TEXT("Received an unexpected response from %s.%s"),
			Code, *Provider, *DetailLine);
	}
}

void SUECPMainWidget::LoadArchitectChatShell()
{
	RefreshArchitectChatView();
}

void SUECPMainWidget::UpdateArchitectThinkingState()
{
	RefreshArchitectChatView();
}

static FString EnsureMessageTimestampHHMM(const TSharedPtr<FJsonObject>& MessageObject)
{
	if (!MessageObject.IsValid()) return FString();
	int64 Epoch = 0;
	double EpochField = 0.0;
	if (MessageObject->TryGetNumberField(TEXT("timestamp"), EpochField) && EpochField > 0.0)
	{
		Epoch = (int64)EpochField;
	}
	else
	{
		Epoch = FDateTime::UtcNow().ToUnixTimestamp();
		MessageObject->SetNumberField(TEXT("timestamp"), (double)Epoch);
	}
	const FDateTime UtcDt = FDateTime::FromUnixTimestamp(Epoch);
	const FTimespan LocalOffset = FDateTime::Now() - FDateTime::UtcNow();
	const FDateTime LocalDt = UtcDt + LocalOffset;
	return FString::Printf(TEXT("%02d:%02d"), LocalDt.GetHour(), LocalDt.GetMinute());
}

static const TSet<FString>& GetHiddenToolResultNames()
{
	static const TSet<FString> Set = {
		TEXT("search_templates"), TEXT("apply_template"), TEXT("get_selected_assets"),
		TEXT("get_current_folder"),
		TEXT("project_plan"), TEXT("create_plan"), TEXT("update_step"), TEXT("get_plan"),
		TEXT("clear_plan"), TEXT("list_plans"), TEXT("import_plan"),
		TEXT("memory"), TEXT("add_memory"), TEXT("get_memories"), TEXT("delete_memory"),
		TEXT("ask_user"), TEXT("proceed_with_plan"),
				TEXT("task"), TEXT("set_tasks"), TEXT("add_task"), TEXT("update_task"), TEXT("edit_task"), TEXT("remove_task"), TEXT("reorder_task"), TEXT("clear_tasks"), TEXT("get_tasks"),
		TEXT("search_tools"), TEXT("find_tool"), TEXT("discover_tools"), TEXT("list_assets_in_folder"),
		TEXT("get_tool_docs"), TEXT("get_handle_reference")
	};
	return Set;
}

static FString ArchitectBaseToolName(const FString& RawName)
{
	int32 DotIdx;
	if (RawName.FindChar((TCHAR)0x00B7, DotIdx))
		return RawName.Left(DotIdx).TrimStartAndEnd();
	return RawName.TrimStartAndEnd();
}

static FString ArchitectToolActionName(const FString& RawName)
{
	int32 DotIdx;
	if (RawName.FindChar((TCHAR)0x00B7, DotIdx))
		return RawName.Mid(DotIdx + 1).TrimStartAndEnd();
	return FString();
}

static bool ArchitectToolHeaderHidden(const TSet<FString>& Hidden, const FString& RawName)
{
	if (Hidden.Contains(ArchitectBaseToolName(RawName))) return true;
	const FString Action = ArchitectToolActionName(RawName);
	return !Action.IsEmpty() && Hidden.Contains(Action);
}

static bool IsArchitectToolResultMessage(const TSharedPtr<FJsonObject>& MessageObject)
{
	if (!MessageObject.IsValid()) return false;
	FString Role;
	if (!MessageObject->TryGetStringField(TEXT("role"), Role) || Role != TEXT("user")) return false;

	const TArray<TSharedPtr<FJsonValue>>* Parts = nullptr;
	if (!MessageObject->TryGetArrayField(TEXT("parts"), Parts) || Parts->Num() == 0) return false;
	const TSharedPtr<FJsonObject> Part0 = (*Parts)[0]->AsObject();
	if (!Part0.IsValid()) return false;
	FString Content;
	if (!Part0->TryGetStringField(TEXT("text"), Content)) return false;

	if (!Content.StartsWith(TEXT("[TOOL_RESULT:"))) return false;

	FString Header = Content.Mid(13);
	int32 ColonIdx;
	if (!Header.FindChar(TEXT(':'), ColonIdx)) return false;
	const FString Name = Header.Left(ColonIdx);
	return !ArchitectToolHeaderHidden(GetHiddenToolResultNames(), Name);
}

static bool IsArchitectBatchTransparentMessage(const TSharedPtr<FJsonObject>& MessageObject)
{
	if (!MessageObject.IsValid()) return false;
	FString Role;
	if (!MessageObject->TryGetStringField(TEXT("role"), Role)) return false;

	FString Content;
	const TArray<TSharedPtr<FJsonValue>>* Parts = nullptr;
	if (MessageObject->TryGetArrayField(TEXT("parts"), Parts) && Parts->Num() > 0)
	{
		const TSharedPtr<FJsonObject> Part0 = (*Parts)[0]->AsObject();
		if (Part0.IsValid()) Part0->TryGetStringField(TEXT("text"), Content);
	}

	if (Role == TEXT("model"))
	{
		return Content.TrimStartAndEnd().IsEmpty();
	}

	if (Role == TEXT("user") && Content.StartsWith(TEXT("[TOOL_RESULT:")))
	{
		FString Header = Content.Mid(13);
		int32 ColonIdx;
		if (Header.FindChar(TEXT(':'), ColonIdx))
			return ArchitectToolHeaderHidden(GetHiddenToolResultNames(), Header.Left(ColonIdx));
	}

	return false;
}

static bool IsArchitectBatchedToolBlocksEnabled()
{
	bool bEnabled = true;
	GConfig->GetBool(TEXT("BpGeneratorUltimate"), TEXT("BatchedToolBlocks"),
		bEnabled, FSettingsManager::GetGlobalConfigPath());
	return bEnabled;
}

static bool IsArchitectMergeCallResultEnabled()
{
	bool bEnabled = false;
	GConfig->GetBool(TEXT("BpGeneratorUltimate"), TEXT("MergeToolCallAndResult"),
		bEnabled, FSettingsManager::GetGlobalConfigPath());
	return bEnabled;
}

struct FArchitectRenderRange
{
	int32 StartIndex = 0;
	int32 EndIndex   = 0;
	bool  bBatched   = false;
};

static void ComputeArchitectRenderRanges(
	const TArray<TSharedPtr<FJsonValue>>& History,
	bool bBatchEnabled,
	TArray<FArchitectRenderRange>& OutRanges)
{
	OutRanges.Reset();
	const int32 Num = History.Num();
	if (Num == 0) return;

	int32 i = 0;
	while (i < Num)
	{
		if (bBatchEnabled)
		{
			TSharedPtr<FJsonObject> Obj = History[i].IsValid() ? History[i]->AsObject() : nullptr;
			if (IsArchitectToolResultMessage(Obj))
			{
				int32 End = i;
				int32 LastVisible = i;
				int32 VisibleCount = 1;
				while (End + 1 < Num)
				{
					TSharedPtr<FJsonObject> Next = History[End + 1].IsValid() ? History[End + 1]->AsObject() : nullptr;
					if (IsArchitectToolResultMessage(Next))
					{
						End++;
						LastVisible = End;
						VisibleCount++;
					}
					else if (IsArchitectBatchTransparentMessage(Next))
					{
						End++;
					}
					else
					{
						break;
					}
				}
				End = LastVisible;

				FArchitectRenderRange Range;
				Range.StartIndex = i;
				Range.EndIndex   = End;
				Range.bBatched   = (VisibleCount >= 2);
				OutRanges.Add(Range);
				i = End + 1;
				continue;
			}
		}

		FArchitectRenderRange Single;
		Single.StartIndex = i;
		Single.EndIndex   = i;
		Single.bBatched   = false;
		OutRanges.Add(Single);
		i++;
	}
}

static FString BuildArchitectBatchedRowHtml(const FString& Content)
{
	if (!Content.StartsWith(TEXT("[TOOL_RESULT:"))) return FString();
	FString Header = Content.Mid(13);
	int32 ColonIdx, EndIdx;
	if (!Header.FindChar(TEXT(':'), ColonIdx)) return FString();
	if (!Header.FindChar(TEXT(']'), EndIdx) || EndIdx <= ColonIdx) return FString();

	const FString ToolName  = Header.Left(ColonIdx);
	const FString Status    = Header.Mid(ColonIdx + 1, EndIdx - ColonIdx - 1);
	const bool bSuccess     = Status == TEXT("success");
	FString Body            = Content.Mid(Content.Find(TEXT("]")) + 1).TrimStart();

	FString CallContent;
	FString ResultContent;
	const int32 SepIdx = Body.Find(TEXT("\n---\n"));
	if (SepIdx != INDEX_NONE)
	{
		CallContent   = Body.Left(SepIdx).TrimStartAndEnd();
		ResultContent = Body.Mid(SepIdx + 5).TrimStartAndEnd();
	}
	else
	{
		ResultContent = Body.TrimStartAndEnd();
	}

	FString DisplayName = ToolName;
	if (!CallContent.IsEmpty() && !ToolName.Contains(TEXT("\xB7")))
	{
		int32 ActionKeyIdx = CallContent.Find(TEXT("\"action\""));
		if (ActionKeyIdx != INDEX_NONE)
		{
			int32 Colon = CallContent.Find(TEXT(":"), ESearchCase::IgnoreCase, ESearchDir::FromStart, ActionKeyIdx + 8);
			if (Colon != INDEX_NONE)
			{
				int32 Q1 = CallContent.Find(TEXT("\""), ESearchCase::IgnoreCase, ESearchDir::FromStart, Colon + 1);
				if (Q1 != INDEX_NONE)
				{
					int32 Q2 = CallContent.Find(TEXT("\""), ESearchCase::IgnoreCase, ESearchDir::FromStart, Q1 + 1);
					if (Q2 != INDEX_NONE && Q2 > Q1 + 1)
					{
						FString Action = CallContent.Mid(Q1 + 1, Q2 - Q1 - 1);
						if (!Action.IsEmpty() && !Action.Contains(TEXT("\\")) && Action.Len() < 80)
							DisplayName = FString::Printf(TEXT("%s \xB7 %s"), *ToolName, *Action);
					}
				}
			}
		}
	}

	auto Clean = [](FString& S)
	{
		S.ReplaceInline(TEXT("```json\n"), TEXT(""));
		S.ReplaceInline(TEXT("```json"), TEXT(""));
		S.ReplaceInline(TEXT("\n```"), TEXT(""));
		S.ReplaceInline(TEXT("```"), TEXT(""));
		S = S.TrimStartAndEnd();
		S = S.Replace(TEXT("<"), TEXT("&lt;")).Replace(TEXT(">"), TEXT("&gt;"));
	};

	if (!ResultContent.IsEmpty()) Clean(ResultContent);
	const FString Encoded = FGenericPlatformHttp::UrlEncode(ResultContent);
	const FString IconColor = bSuccess ? TEXT("#2ea043") : TEXT("#f85149");
	const FString IconId    = bSuccess ? TEXT("#ic-check-circle") : TEXT("#ic-x-circle");

	return FString::Printf(
		TEXT("<div style='display:flex;align-items:flex-start;padding:6px 0;border-bottom:1px solid #2a2a2a;gap:8px;'>"
			"<div class='tool-result-icon-wrap' style='flex-shrink:0;margin-top:2px;'>"
			"<svg class='icon' width='13' height='13' viewBox='0 0 24 24' style='color:%s'><use href='%s'/></svg></div>"
			"<div style='flex:1;min-width:0;'>"
			"<span style='color:#c8c8c8;font-size:12px;font-weight:500;'>%s</span>"
			"<pre data-tool-json='%s' style='margin:4px 0 0 0;font-size:11px;white-space:pre-wrap;word-break:break-all;'></pre>"
			"</div></div>"),
		*IconColor, *IconId, *DisplayName, *Encoded);
}

static FString RenderArchitectBatchedToolResultsHtml(
	const TArray<TSharedPtr<FJsonValue>>& History,
	const TArray<int32>& Indices)
{
	if (Indices.Num() == 0) return FString();

	FString InnerHtml;
	int32 OkCount = 0, FailCount = 0;
	for (int32 Idx : Indices)
	{
		if (Idx < 0 || Idx >= History.Num()) continue;
		TSharedPtr<FJsonObject> Obj = History[Idx].IsValid() ? History[Idx]->AsObject() : nullptr;
		if (!Obj.IsValid()) continue;
		const TArray<TSharedPtr<FJsonValue>>* Parts = nullptr;
		if (!Obj->TryGetArrayField(TEXT("parts"), Parts) || Parts->Num() == 0) continue;
		TSharedPtr<FJsonObject> P = (*Parts)[0]->AsObject();
		if (!P.IsValid()) continue;
		FString Content;
		if (!P->TryGetStringField(TEXT("text"), Content)) continue;
		if (!Content.StartsWith(TEXT("[TOOL_RESULT:"))) continue;
		FString Hdr = Content.Mid(13);
		int32 Cidx; if (!Hdr.FindChar(TEXT(':'), Cidx)) continue;
		FString Name = Hdr.Left(Cidx);
		if (ArchitectToolHeaderHidden(GetHiddenToolResultNames(), Name)) continue;
		int32 Eidx; if (Hdr.FindChar(TEXT(']'), Eidx) && Eidx > Cidx)
		{
			const FString Status = Hdr.Mid(Cidx + 1, Eidx - Cidx - 1);
			if (Status == TEXT("success")) OkCount++; else FailCount++;
		}
		InnerHtml += BuildArchitectBatchedRowHtml(Content);
	}

	if (InnerHtml.IsEmpty()) return FString();

	const int32 Total = OkCount + FailCount;
	const bool bAllFailed     = FailCount > 0 && OkCount == 0;
	const bool bPartialFailed = FailCount > 0 && OkCount > 0;
	FString OuterClass;
	FString IconColor;
	FString IconId;
	if (bAllFailed)
	{
		OuterClass = TEXT("tool-result tool-result-block tool-result-err");
		IconColor  = TEXT("#f85149");
		IconId     = TEXT("#ic-x-circle");
	}
	else if (bPartialFailed)
	{
		OuterClass = TEXT("tool-result tool-result-block tool-result-warn");
		IconColor  = TEXT("#e09d35");
		IconId     = TEXT("#ic-info");
	}
	else
	{
		OuterClass = TEXT("tool-result tool-result-block");
		IconColor  = TEXT("#2ea043");
		IconId     = TEXT("#ic-check-circle");
	}
	FString Label;
	if (bAllFailed)
		Label = FString::Printf(TEXT("%d tool calls — all failed"), Total);
	else if (bPartialFailed)
		Label = FString::Printf(TEXT("%d tool calls (%d with issues)"), Total, FailCount);
	else
		Label = FString::Printf(TEXT("%d tool calls"), Total);
	const FString DomId = FString::Printf(TEXT("toolbatch-%d"), Indices[0]);

	return FString::Printf(
		TEXT("<div class='%s' id='%s' onclick='toggleTool(\"%s\")'>"
			"<div class='tool-header'>"
			"<div class='tool-result-icon-wrap'>"
			"<svg class='icon' width='13' height='13' viewBox='0 0 24 24' style='color:%s'><use href='%s'/></svg></div>"
			"<span class='tool-name'>%s</span>"
			"<span class='tool-result-tag'>result</span>"
			"<span class='tool-chevron' style='margin-left:auto'>&#8250;</span>"
			"</div><div class='tool-content'>%s</div></div>"),
		*OuterClass, *DomId, *DomId, *IconColor, *IconId, *Label, *InnerHtml);
}

static FString RenderArchitectRangeBatchHtml(
	const TArray<TSharedPtr<FJsonValue>>& History,
	int32 StartIndex,
	int32 EndIndex)
{
	TArray<int32> All;
	All.Reserve(EndIndex - StartIndex + 1);
	for (int32 i = StartIndex; i <= EndIndex; i++) All.Add(i);
	return RenderArchitectBatchedToolResultsHtml(History, All);
}

static uint32 ComputeArchitectRangeContentHash(
	const TArray<TSharedPtr<FJsonValue>>& History,
	const FArchitectRenderRange& Range)
{
	uint32 Hash = Range.bBatched ? 0x9E3779B9u : 0u;
	int32 RowCount = 0;
	for (int32 i = Range.StartIndex; i <= Range.EndIndex; i++)
	{
		if (i < 0 || i >= History.Num()) continue;
		TSharedPtr<FJsonObject> Obj = History[i].IsValid() ? History[i]->AsObject() : nullptr;
		if (!Obj.IsValid()) continue;
		const TArray<TSharedPtr<FJsonValue>>* Parts = nullptr;
		FString RowText;
		if (Obj->TryGetArrayField(TEXT("parts"), Parts) && Parts->Num() > 0)
			if (TSharedPtr<FJsonObject> P = (*Parts)[0]->AsObject())
				P->TryGetStringField(TEXT("text"), RowText);
		Hash = FCrc::StrCrc32(*RowText, Hash);
		Hash = HashCombine(Hash, (uint32)i);
		RowCount++;
	}
	Hash = HashCombine(Hash, (uint32)RowCount);
	return Hash;
}

FString SUECPMainWidget::RenderSingleArchitectMessageHtml(const TSharedPtr<FJsonObject>& MessageObject, int32 MessageIndex)
{
	FString Role, Content;
	if (!MessageObject->TryGetStringField(TEXT("role"), Role)) return FString();
	const FString MessageTimeHHMM = EnsureMessageTimestampHHMM(MessageObject);
	const FString TsSpan = FString::Printf(TEXT("<span class='msg-timestamp'>%s</span>"), *MessageTimeHHMM);

	const TArray<TSharedPtr<FJsonValue>>* PartsArray;
	if (!MessageObject->TryGetArrayField(TEXT("parts"), PartsArray) || PartsArray->Num() == 0) return FString();
	if (!(*PartsArray)[0]->AsObject()->TryGetStringField(TEXT("text"), Content)) return FString();

	TStringBuilder<4096> Html;

	if (Role == TEXT("agent_thinking"))
	{
		FString BlockId;
		MessageObject->TryGetStringField(TEXT("block_id"), BlockId);
		FString ElemId    = BlockId.IsEmpty() ? FString::Printf(TEXT("think-%d"),     MessageIndex) : FString::Printf(TEXT("tblk-%s"), *BlockId);
		FString PreElemId = BlockId.IsEmpty() ? FString::Printf(TEXT("think-pre-%d"), MessageIndex) : FString::Printf(TEXT("tpre-%s"), *BlockId);

		FString HtmlContent = Content
			.Replace(TEXT("&"), TEXT("&amp;"))
			.Replace(TEXT("<"), TEXT("&lt;"))
			.Replace(TEXT(">"), TEXT("&gt;"));
		Html.Appendf(TEXT("<div class='sys-msg' id='%s' onclick='this.classList.toggle(\"expanded\")'>"
			"<div class='sys-hdr'>"
				"<svg class='icon' width='11' height='11' viewBox='0 0 24 24' fill='none' stroke='currentColor' stroke-width='2' stroke-linecap='round' stroke-linejoin='round'><use href='#ic-brain'/></svg>"
				"<span class='sys-label'>Reasoning</span>"
				"<span class='sys-chev'><svg class='icon' width='11' height='11'><use href='#ic-chevron-right'/></svg></span>"
			"</div>"
			"<div class='sys-body'>"
				"<pre id='%s' style='margin:0;font-family:Consolas,monospace;font-size:11.5px;white-space:pre-wrap;color:var(--text2);background:none;border:none;padding:0'>%s</pre>"
			"</div></div>"),
			*ElemId, *PreElemId, *HtmlContent);
		return Html.ToString();
	}

	if (Role == TEXT("context"))
	{
		FString FileName, FileContent;
		int32 CharCount = 0;
		bool bExpanded = false;
		MessageObject->TryGetStringField(TEXT("file_name"), FileName);
		MessageObject->TryGetStringField(TEXT("content"), FileContent);
		MessageObject->TryGetNumberField(TEXT("char_count"), CharCount);
		MessageObject->TryGetBoolField(TEXT("expanded"), bExpanded);
		FString EscapedContent = FileContent.Replace(TEXT("<"), TEXT("&lt;")).Replace(TEXT(">"), TEXT("&gt;"));
		Html.Appendf(TEXT("<div class='tool-result%s' id='file-%d' onclick='toggleTool(\"file-%d\")'><div class='tool-header'><div class='tool-icon'>&#128196;</div><span class='tool-name'>%s (%d chars)</span><span class='tool-chevron'>&#8250;</span></div><div class='tool-content' style='display:%s'><pre>%s</pre></div></div>"),
			bExpanded ? TEXT(" expanded") : TEXT(""), MessageIndex, MessageIndex, *FileName, CharCount, bExpanded ? TEXT("block") : TEXT("none"), *EscapedContent);
		return Html.ToString();
	}

	FString Speaker = (Role == TEXT("user")) ? TEXT("You") : TEXT("Architect");
	FString DivClass = (Role == TEXT("user")) ? TEXT("user-msg") : TEXT("ai-msg");

	if (Role == TEXT("user"))
	{
		FString SysLabel;
		if      (Content.StartsWith(TEXT("[COMPACT REQUEST]")))     SysLabel = TEXT("Compaction requested");
		else if (Content.StartsWith(TEXT("[CREW BRIEFING")))        SysLabel = TEXT("Crew briefing");
		else if (Content.StartsWith(TEXT("[CREW INSTRUCTION")))     SysLabel = TEXT("Crew instruction");
		else if (Content.StartsWith(TEXT("[CREW RESUMED")))         SysLabel = TEXT("Crew resumed");
		else if (Content.StartsWith(TEXT("[CREW REPORT")))          SysLabel = TEXT("Crew report");

		if (!SysLabel.IsEmpty())
		{
			const FString UrlEncodedContent = FGenericPlatformHttp::UrlEncode(Content);
			Html.Appendf(TEXT(
				"<div class='sys-msg' onclick='this.classList.toggle(\"expanded\")'>"
				  "<div class='sys-hdr'>"
				    "<svg class='icon' width='11' height='11' viewBox='0 0 24 24' fill='none' stroke='currentColor' stroke-width='2' stroke-linecap='round' stroke-linejoin='round'><use href='#ic-info'/></svg>"
				    "<span class='sys-label'>%s</span>"
				    "%s"
				    "<span class='sys-chev'><svg class='icon' width='11' height='11'><use href='#ic-chevron-right'/></svg></span>"
				  "</div>"
				  "<div class='sys-body'><div data-md='%s'></div></div>"
				"</div>"),
				*SysLabel, *TsSpan, *UrlEncodedContent);
			return Html.ToString();
		}
	}

	auto TryExtractJsonQuotedField = [](const FString& Source, const FString& FieldName, FString& OutValue) -> bool
	{
		const FString Needle = FString::Printf(TEXT("\"%s\""), *FieldName);
		const int32 FieldPos = Source.Find(Needle, ESearchCase::CaseSensitive);
		if (FieldPos == INDEX_NONE)
		{
			return false;
		}

		int32 ColonPos = Source.Find(TEXT(":"), ESearchCase::CaseSensitive, ESearchDir::FromStart, FieldPos + Needle.Len());
		if (ColonPos == INDEX_NONE)
		{
			return false;
		}

		int32 QuotePos = INDEX_NONE;
		for (int32 i = ColonPos + 1; i < Source.Len(); ++i)
		{
			if (!FChar::IsWhitespace(Source[i]))
			{
				if (Source[i] != TEXT('\"'))
				{
					return false;
				}
				QuotePos = i;
				break;
			}
		}

		if (QuotePos == INDEX_NONE)
		{
			return false;
		}

		FString Extracted;
		bool bEscaped = false;
		for (int32 i = QuotePos + 1; i < Source.Len(); ++i)
		{
			const TCHAR Ch = Source[i];
			if (bEscaped)
			{
				switch (Ch)
				{
				case TEXT('n'): Extracted.AppendChar(TEXT('\n')); break;
				case TEXT('r'): Extracted.AppendChar(TEXT('\r')); break;
				case TEXT('t'): Extracted.AppendChar(TEXT('\t')); break;
				case TEXT('\\'): Extracted.AppendChar(TEXT('\\')); break;
				case TEXT('\"'): Extracted.AppendChar(TEXT('\"')); break;
				default: Extracted.AppendChar(Ch); break;
				}
				bEscaped = false;
				continue;
			}

			if (Ch == TEXT('\\'))
			{
				bEscaped = true;
				continue;
			}

			if (Ch == TEXT('\"'))
			{
				OutValue = Extracted.TrimStartAndEnd();
				return !OutValue.IsEmpty();
			}

			Extracted.AppendChar(Ch);
		}

		return false;
	};

	auto SanitizeEnvelopeFragment = [&](const FString& Fragment) -> FString
	{
		FString Sanitized = Fragment;

		const bool bLooksLikeEnvelope = Sanitized.Contains(TEXT("\"assistant_text\""))
			|| Sanitized.Contains(TEXT("\"tool_calls\""))
			|| Sanitized.TrimStart().StartsWith(TEXT("```json"));

		if (bLooksLikeEnvelope)
		{
			Sanitized.ReplaceInline(TEXT("```json"), TEXT(""));
			Sanitized.ReplaceInline(TEXT("```"), TEXT(""));
		}

		Sanitized = Sanitized.TrimStartAndEnd();
		if (Sanitized.IsEmpty())
		{
			return FString();
		}

		FString AssistantTextFromFragment;
		if (Sanitized.Contains(TEXT("\"assistant_text\"")))
		{
			if (TryExtractJsonQuotedField(Sanitized, TEXT("assistant_text"), AssistantTextFromFragment)
				|| TryExtractJsonQuotedField(Sanitized, TEXT("text"), AssistantTextFromFragment))
			{
				return AssistantTextFromFragment.TrimStartAndEnd();
			}
		}

		bool bOnlyClosers = true;
		for (int32 i = 0; i < Sanitized.Len(); ++i)
		{
			const TCHAR Ch = Sanitized[i];
			if (!FChar::IsWhitespace(Ch) && Ch != TEXT(']') && Ch != TEXT('}') && Ch != TEXT(',') && Ch != TEXT('`'))
			{
				bOnlyClosers = false;
				break;
			}
		}
		if (bOnlyClosers)
		{
			return FString();
		}

		if (Sanitized.StartsWith(TEXT("{")) && Sanitized.Contains(TEXT("\"tool_calls\"")))
		{
			return FString();
		}

		{
			const int32 TrailMarker = Sanitized.Find(TEXT("\",\"done\""), ESearchCase::IgnoreCase);
			if (TrailMarker != INDEX_NONE)
			{
				Sanitized = Sanitized.Left(TrailMarker).TrimStartAndEnd();
			}
			else
			{
				const int32 ToolCallsMarker = Sanitized.Find(TEXT("\",\"tool_calls\""), ESearchCase::IgnoreCase);
				if (ToolCallsMarker != INDEX_NONE)
				{
					Sanitized = Sanitized.Left(ToolCallsMarker).TrimStartAndEnd();
				}
			}
			const FString LeadPrefix = TEXT("{\"assistant_text\":\"");
			if (Sanitized.StartsWith(LeadPrefix, ESearchCase::IgnoreCase))
			{
				Sanitized = Sanitized.RightChop(LeadPrefix.Len()).TrimStartAndEnd();
			}
		}
		if (Sanitized.IsEmpty())
		{
			return FString();
		}

		if (Sanitized.StartsWith(TEXT("{")) && Sanitized.EndsWith(TEXT("}")))
		{
			TSharedPtr<FJsonObject> MaybeToolObj;
			TSharedRef<TJsonReader<>> ChkReader = TJsonReaderFactory<>::Create(Sanitized);
			if (FJsonSerializer::Deserialize(ChkReader, MaybeToolObj) && MaybeToolObj.IsValid()
				&& MaybeToolObj->Values.Num() >= 1)
			{
				bool bAllObjs = true;
				for (auto& Pair : MaybeToolObj->Values)
				{
					if (!Pair.Value.IsValid() || Pair.Value->Type != EJson::Object)
					{
						bAllObjs = false;
						break;
					}
				}
				if (bAllObjs)
					return FString();
			}
		}

		return Sanitized;
	};

	if (Role == TEXT("model"))
	{
		auto SerializeJsonObject = [](const TSharedPtr<FJsonObject>& Obj) -> FString
		{
			if (!Obj.IsValid())
			{
				return FString();
			}

			FString Out;
			TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
			FJsonSerializer::Serialize(Obj.ToSharedRef(), Writer);
			return Out;
		};

		auto TryNormalizeEnvelope = [&](const FString& JsonContent, const FString& TextBefore, const FString& TextAfter, FString& OutNormalized) -> bool
		{
			TSharedPtr<FJsonObject> JsonObject;
			TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonContent);
			if (!(FJsonSerializer::Deserialize(Reader, JsonObject) && JsonObject.IsValid()))
			{
				return false;
			}

			FString AssistantText;
			JsonObject->TryGetStringField(TEXT("assistant_text"), AssistantText);
			if (AssistantText.IsEmpty())
			{
				JsonObject->TryGetStringField(TEXT("text"), AssistantText);
			}

			const TArray<TSharedPtr<FJsonValue>>* ToolCalls = nullptr;
			const bool bHasToolCalls = JsonObject->TryGetArrayField(TEXT("tool_calls"), ToolCalls) && ToolCalls;
			bool bHasDoneField = false;
			bool bDoneValue = false;
			if (JsonObject->TryGetBoolField(TEXT("done"), bDoneValue))
			{
				bHasDoneField = true;
			}
			bool bHasContinueField = false;
			bool bContinueValue = false;
			if (JsonObject->TryGetBoolField(TEXT("continue"), bContinueValue))
			{
				bHasContinueField = true;
			}

			if (!bHasToolCalls && AssistantText.IsEmpty() && !bHasDoneField && !bHasContinueField)
			{
				return false;
			}

			auto AppendFragment = [](FString& Target, const FString& Fragment)
			{
				const FString Clean = Fragment.TrimStartAndEnd();
				if (Clean.IsEmpty())
				{
					return;
				}
				if (!Target.IsEmpty())
				{
					Target += TEXT("\n\n");
				}
				Target += Clean;
			};

			auto ChooseVisibleAssistantText = [](const FString& VisibleText, const FString& EnvelopeAssistantText) -> FString
			{
				const FString Visible = VisibleText.TrimStartAndEnd();
				const FString Envelope = EnvelopeAssistantText.TrimStartAndEnd();
				if (Visible.IsEmpty())
				{
					return Envelope;
				}
				if (Envelope.IsEmpty())
				{
					return Visible;
				}

				const FString VisibleLower = Visible.ToLower();
				const FString EnvelopeLower = Envelope.ToLower();
				if (VisibleLower.Contains(EnvelopeLower))
				{
					return Visible;
				}
				if (EnvelopeLower.Contains(VisibleLower))
				{
					return Envelope;
				}

				return Visible;
			};

			FString Normalized;
			AppendFragment(Normalized, ChooseVisibleAssistantText(TextBefore, AssistantText));

			if (bHasToolCalls)
			{
				for (const TSharedPtr<FJsonValue>& ToolCallValue : *ToolCalls)
				{
					const TSharedPtr<FJsonObject> ToolCallObject = ToolCallValue.IsValid() ? ToolCallValue->AsObject() : nullptr;
					if (!ToolCallObject.IsValid())
					{
						continue;
					}

					const FString ToolJson = SerializeJsonObject(ToolCallObject);
					if (ToolJson.IsEmpty())
					{
						continue;
					}

					AppendFragment(Normalized, ToolJson);
				}
			}

			AppendFragment(Normalized, TextAfter);
			OutNormalized = Normalized;
			return true;
		};

		const FString JsonBlockStart = TEXT("```json");
		const FString JsonBlockEnd = TEXT("```");
		int32 JsonBlockStartPos = Content.Find(JsonBlockStart, ESearchCase::IgnoreCase);
		if (JsonBlockStartPos != INDEX_NONE)
		{
			const int32 ContentStart = JsonBlockStartPos + JsonBlockStart.Len();
			const int32 JsonBlockEndPos = Content.Find(JsonBlockEnd, ESearchCase::CaseSensitive, ESearchDir::FromStart, ContentStart);
			if (JsonBlockEndPos != INDEX_NONE)
			{
				const FString JsonContent = Content.Mid(ContentStart, JsonBlockEndPos - ContentStart).TrimStartAndEnd();
				const FString TextBefore = Content.Left(JsonBlockStartPos).TrimEnd();
				const FString TextAfter = Content.Mid(JsonBlockEndPos + JsonBlockEnd.Len()).TrimStart();
				FString Normalized;
				if (TryNormalizeEnvelope(JsonContent, TextBefore, TextAfter, Normalized))
				{
					Content = Normalized;
				}
			}
		}
		else
		{
			const int32 BraceStart = Content.Find(TEXT("{"), ESearchCase::CaseSensitive);
			if (BraceStart != INDEX_NONE)
			{
				const int32 BraceEnd = FindMatchingClosingBrace(Content, BraceStart);
				if (BraceEnd != INDEX_NONE)
				{
					const FString JsonContent = Content.Mid(BraceStart, BraceEnd - BraceStart + 1);
					const FString TextBefore = Content.Left(BraceStart).TrimEnd();
					const FString TextAfter = Content.Mid(BraceEnd + 1).TrimStart();
					FString Normalized;
					if (TryNormalizeEnvelope(JsonContent, TextBefore, TextAfter, Normalized))
					{
						Content = Normalized;
					}
				}
			}
		}
	}

	if (Role == TEXT("user"))
	{
		if (Content.StartsWith(TEXT("[TOOL_RESULT:")) || Content.StartsWith(TEXT("[BATCH_RESULT:")) || Content.StartsWith(TEXT("[AUTO-VALIDATE]")) || Content.StartsWith(TEXT("[HANDLE_REFERENCE:")))
		{
			static const TSet<FString> HiddenToolResults = {
				TEXT("search_templates"), TEXT("apply_template"), TEXT("get_selected_assets"),
				TEXT("get_current_folder"),
				TEXT("project_plan"), TEXT("create_plan"), TEXT("update_step"), TEXT("get_plan"), TEXT("clear_plan"), TEXT("list_plans"), TEXT("import_plan"),
				TEXT("memory"), TEXT("add_memory"), TEXT("get_memories"), TEXT("delete_memory"),
				TEXT("ask_user"), TEXT("proceed_with_plan"),
				TEXT("task"), TEXT("set_tasks"), TEXT("add_task"), TEXT("update_task"), TEXT("edit_task"), TEXT("remove_task"), TEXT("reorder_task"), TEXT("clear_tasks"), TEXT("get_tasks"),
				TEXT("search_tools"), TEXT("find_tool"), TEXT("discover_tools"), TEXT("list_assets_in_folder"),
				TEXT("get_tool_docs"), TEXT("get_handle_reference")
			};
			if (Content.StartsWith(TEXT("[TOOL_RESULT:")))
			{
				FString ToolResultInfo = Content.Mid(13);
				int32 HiddenColon = ToolResultInfo.Find(TEXT(":"));
				FString HiddenToolName = (HiddenColon != INDEX_NONE) ? ToolResultInfo.Left(HiddenColon) : ToolResultInfo;
				if (ArchitectToolHeaderHidden(HiddenToolResults, HiddenToolName))
					return FString();
			}

			if (Content.StartsWith(TEXT("[BATCH_RESULT:")))
			{
				FString BatchHeader = Content.Mid(1, Content.Find(TEXT("]")) - 1);
				FString BatchBody = Content.Mid(Content.Find(TEXT("]")) + 1).TrimStart();
				bool bBatchHasError = BatchBody.Contains(TEXT(":error]"));

				FString InnerHtml;
				int32 SearchPos = 0;
				while (SearchPos < BatchBody.Len())
				{
					int32 ToolStart = BatchBody.Find(TEXT("[TOOL_RESULT:"), ESearchCase::CaseSensitive, ESearchDir::FromStart, SearchPos);
					if (ToolStart == INDEX_NONE) break;
					int32 NextStart = BatchBody.Find(TEXT("[TOOL_RESULT:"), ESearchCase::CaseSensitive, ESearchDir::FromStart, ToolStart + 13);
					FString SingleResult = (NextStart != INDEX_NONE) ? BatchBody.Mid(ToolStart, NextStart - ToolStart).TrimEnd() : BatchBody.Mid(ToolStart).TrimEnd();
					FString TR = SingleResult;
					TR.RemoveFromStart(TEXT("[TOOL_RESULT:"));
					int32 SIdx = TR.Find(TEXT(":"));
					int32 EIdx = TR.Find(TEXT("]"));
					if (SIdx != INDEX_NONE && EIdx != INDEX_NONE)
					{
						FString TN = TR.Left(SIdx);
						if (!ArchitectToolHeaderHidden(HiddenToolResults, TN))
						{
							FString TS = TR.Mid(SIdx + 1, EIdx - SIdx - 1);
							FString TC = SingleResult.Mid(SingleResult.Find(TEXT("]")) + 1).TrimStart();
							TC.ReplaceInline(TEXT("```json\n"), TEXT(""));
							TC.ReplaceInline(TEXT("```json"), TEXT(""));
							TC.ReplaceInline(TEXT("\n```"), TEXT(""));
							TC.ReplaceInline(TEXT("```"), TEXT(""));
							TC = TC.TrimStartAndEnd();
							if ((TC.StartsWith(TEXT("{")) || TC.StartsWith(TEXT("["))))
							{
								TSharedPtr<FJsonValue> Parsed;
								TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(TC);
								if (FJsonSerializer::Deserialize(R, Parsed) && Parsed.IsValid())
								{
									FString Pretty;
									TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> W = TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Pretty);
									FJsonSerializer::Serialize(Parsed, FString(), W);
									if (!Pretty.IsEmpty()) TC = Pretty;
								}
							}
							bool bOK = TS == TEXT("success");
							FString TCEncoded = FGenericPlatformHttp::UrlEncode(TC);
							FString RowIconColor = bOK ? TEXT("#2ea043") : TEXT("#f85149");
							FString RowIconId    = bOK ? TEXT("#ic-check-circle") : TEXT("#ic-x-circle");
							InnerHtml += FString::Printf(
								TEXT("<div style='display:flex;align-items:flex-start;padding:6px 0;border-bottom:1px solid #2a2a2a;gap:8px;'>"
									"<div class='tool-result-icon-wrap' style='flex-shrink:0;margin-top:2px;'><svg class='icon' width='13' height='13' viewBox='0 0 24 24' style='color:%s'><use href='%s'/></svg></div>"
									"<div style='flex:1;'><span style='color:#c8c8c8;font-size:12px;font-weight:500;'>%s</span><pre data-tool-json='%s' style='margin:4px 0 0 0;font-size:11px;white-space:pre-wrap;word-break:break-all;'></pre></div></div>"),
								*RowIconColor, *RowIconId, *TN, *TCEncoded);
						}
					}
					SearchPos = (NextStart != INDEX_NONE) ? NextStart : BatchBody.Len();
				}
				FString BatchIconColor = bBatchHasError ? TEXT("#f85149") : TEXT("#2ea043");
				FString BatchSvgIcon   = bBatchHasError ? TEXT("#ic-x-circle") : TEXT("#ic-check-circle");
				FString BatchBlockClass = bBatchHasError
					? TEXT("tool-result tool-result-block tool-result-err")
					: TEXT("tool-result tool-result-block");
				Html.Appendf(
					TEXT("<div class='%s' id='batch-%d' onclick='toggleTool(\"batch-%d\")'>"
						"<div class='tool-header'>"
						"<div class='tool-result-icon-wrap'><svg class='icon' width='13' height='13' viewBox='0 0 24 24' style='color:%s'><use href='%s'/></svg></div>"
						"<span class='tool-name'>%s</span>"
						"<span class='tool-result-tag'>result</span>"
						"<span class='tool-chevron' style='margin-left:auto'>&#8250;</span>"
						"</div><div class='tool-content'>%s</div></div>"),
					*BatchBlockClass, MessageIndex, MessageIndex, *BatchIconColor, *BatchSvgIcon, *BatchHeader, *InnerHtml);
				return Html.ToString();
			}
			else if (Content.StartsWith(TEXT("[AUTO-VALIDATE]")))
			{
				FString ResultContent = Content.Mid(15).TrimStart();
				bool bHasError = ResultContent.Contains(TEXT("error")) || ResultContent.Contains(TEXT("Error"));
				FString IconClass = bHasError ? TEXT("error") : TEXT("success");
				FString IconSymbol = bHasError ? TEXT("&#9888;") : TEXT("&#10003;");
				ResultContent = ResultContent.Replace(TEXT("<"), TEXT("&lt;")).Replace(TEXT(">"), TEXT("&gt;"));
				FString AutoValidateColor = bHasError ? TEXT("#f85149") : TEXT("#2ea043");
				Html.Appendf(TEXT("<div class='tool-result' id='tool-%d' onclick='toggleTool(\"tool-%d\")'><div class='tool-header' style='border-left:3px solid %s;'><div class='tool-icon %s'>%s</div><span class='tool-name'>Auto-Validate</span><span class='tool-chevron'>&#8250;</span></div><div class='tool-content'><pre>%s</pre></div></div>"),
					MessageIndex, MessageIndex, *AutoValidateColor, *IconClass, *IconSymbol, *ResultContent);
				return Html.ToString();
			}
			else if (Content.StartsWith(TEXT("[HANDLE_REFERENCE:")))
			{
				return FString();
			}
			else
			{
				FString ToolInfo = Content;
				ToolInfo.RemoveFromStart(TEXT("[TOOL_RESULT:"));
				int32 StatusIndex = ToolInfo.Find(TEXT(":"));
				int32 EndIndex = ToolInfo.Find(TEXT("]"));
				FString ToolResultName = ToolInfo.Left(StatusIndex);
				FString Status = ToolInfo.Mid(StatusIndex + 1, EndIndex - StatusIndex - 1);
				FString FullBody = Content.Mid(Content.Find(TEXT("]")) + 1).TrimStart();
				bool bIsSuccess = Status == TEXT("success");
				FString IconClass = bIsSuccess ? TEXT("success") : TEXT("error");
				FString IconSymbol = bIsSuccess ? TEXT("&#10003;") : TEXT("&#10007;");

				FString CallContent;
				FString ResultContent;
				int32 SepIdx = FullBody.Find(TEXT("\n---\n"));
				if (SepIdx != INDEX_NONE)
				{
					CallContent = FullBody.Left(SepIdx).TrimStartAndEnd();
					ResultContent = FullBody.Mid(SepIdx + 5).TrimStartAndEnd();
				}
				else
				{
					ResultContent = FullBody.TrimStartAndEnd();
				}

				FString ToolDisplayName = ToolResultName;
				if (!CallContent.IsEmpty() && !ToolResultName.Contains(TEXT("\xB7")))
				{
					int32 ActionKeyIdx = CallContent.Find(TEXT("\"action\""));
					if (ActionKeyIdx != INDEX_NONE)
					{
						int32 ColonIdx = CallContent.Find(TEXT(":"), ESearchCase::IgnoreCase, ESearchDir::FromStart, ActionKeyIdx + 8);
						if (ColonIdx != INDEX_NONE)
						{
							int32 QuoteStart = CallContent.Find(TEXT("\""), ESearchCase::IgnoreCase, ESearchDir::FromStart, ColonIdx + 1);
							if (QuoteStart != INDEX_NONE)
							{
								int32 QuoteEnd = CallContent.Find(TEXT("\""), ESearchCase::IgnoreCase, ESearchDir::FromStart, QuoteStart + 1);
								if (QuoteEnd != INDEX_NONE && QuoteEnd > QuoteStart + 1)
								{
									FString CallAction = CallContent.Mid(QuoteStart + 1, QuoteEnd - QuoteStart - 1);
									if (!CallAction.IsEmpty() && !CallAction.Contains(TEXT("\\")) && CallAction.Len() < 80)
										ToolDisplayName = FString::Printf(TEXT("%s \xB7 %s"), *ToolResultName, *CallAction);
								}
							}
						}
					}
				}

				auto CleanForDisplay = [](FString& S) {
					S.ReplaceInline(TEXT("```json\n"), TEXT(""));
					S.ReplaceInline(TEXT("```json"), TEXT(""));
					S.ReplaceInline(TEXT("\n```"), TEXT(""));
					S.ReplaceInline(TEXT("```"), TEXT(""));
					S = S.TrimStartAndEnd();
					S = S.Replace(TEXT("<"), TEXT("&lt;")).Replace(TEXT(">"), TEXT("&gt;"));
				};

				FString ResultBlockClass = bIsSuccess
					? TEXT("tool-result tool-result-block")
					: TEXT("tool-result tool-result-block tool-result-err");
				FString ResultIconColor = bIsSuccess ? TEXT("#2ea043") : TEXT("#f85149");
				FString ResultIconId    = bIsSuccess ? TEXT("#ic-check-circle") : TEXT("#ic-x-circle");

				const bool bMergeMode = IsArchitectMergeCallResultEnabled()
					&& !CallContent.IsEmpty()
					&& !ResultContent.IsEmpty();

				if (bMergeMode)
				{
					CleanForDisplay(ResultContent);
					const FString CallEncoded   = FGenericPlatformHttp::UrlEncode(CallContent);
					const FString ResultEncoded = FGenericPlatformHttp::UrlEncode(ResultContent);
					const FString MergedClass = bIsSuccess
						? TEXT("tool-result tool-result-block tool-merged-block")
						: TEXT("tool-result tool-result-block tool-merged-block tool-result-err");
					Html.Appendf(
						TEXT("<div class='%s' id='tool-merged-%d' onclick='toggleTool(\"tool-merged-%d\")'>"
							"<div class='tool-header'>"
							"<div class='tool-result-icon-wrap'><svg class='icon' width='13' height='13' viewBox='0 0 24 24' style='color:%s'><use href='%s'/></svg></div>"
							"<span class='tool-name'>%s</span>"
							"<span class='tool-merged-tag'>call &middot; result</span>"
							"<span class='tool-chevron' style='margin-left:auto'>&#8250;</span>"
							"</div>"
							"<div class='tool-content'>"
							"<div class='tool-merged-section tool-merged-section-call'>"
							"<div class='tool-merged-section-label'>"
							"<svg viewBox='0 0 24 24' fill='none' stroke='currentColor' stroke-width='2'><use href='#ic-wrench'/></svg>Arguments</div>"
							"<pre data-tool-json='%s'></pre></div>"
							"<div class='tool-merged-section tool-merged-section-result'>"
							"<div class='tool-merged-section-label'>"
							"<svg viewBox='0 0 24 24' fill='none' stroke='currentColor' stroke-width='2'><use href='%s'/></svg>Result</div>"
							"<pre data-tool-json='%s'></pre></div>"
							"</div></div>"),
						*MergedClass, MessageIndex, MessageIndex,
						*ResultIconColor, *ResultIconId,
						*ToolDisplayName,
						*CallEncoded,
						*ResultIconId, *ResultEncoded);
				}
				else if (!CallContent.IsEmpty())
				{
					FString CallEncoded = FGenericPlatformHttp::UrlEncode(CallContent);
					Html.Appendf(
						TEXT("<div class='tool-result tool-call-block' id='tool-call-%d' onclick='toggleTool(\"tool-call-%d\")'>"
							"<div class='tool-header'>"
							"<div class='tool-icon-wrap'><svg class='icon' width='13' height='13' viewBox='0 0 24 24' style='color:#e09d35'><use href='#ic-wrench'/></svg></div>"
							"<span class='tool-name'>%s</span>"
							"<span class='tool-call-tag'>call</span>"
							"<span class='tool-chevron' style='margin-left:auto'>&#8250;</span>"
							"</div><div class='tool-content'><pre data-tool-json='%s'></pre></div></div>"),
						MessageIndex, MessageIndex, *ToolDisplayName, *CallEncoded);
				}

				if (!bMergeMode && !ResultContent.IsEmpty())
				{
					CleanForDisplay(ResultContent);
					FString ResultEncoded = FGenericPlatformHttp::UrlEncode(ResultContent);
					Html.Appendf(
						TEXT("<div class='%s' id='tool-%d' onclick='toggleTool(\"tool-%d\")'>"
							"<div class='tool-header'>"
							"<div class='tool-result-icon-wrap'><svg class='icon' width='13' height='13' viewBox='0 0 24 24' style='color:%s'><use href='%s'/></svg></div>"
							"<span class='tool-name'>%s</span>"
							"<span class='tool-result-tag'>result</span>"
							"<span class='tool-chevron' style='margin-left:auto'>&#8250;</span>"
							"</div><div class='tool-content'><pre data-tool-json='%s'></pre></div></div>"),
						*ResultBlockClass, MessageIndex + 10000, MessageIndex + 10000,
						*ResultIconColor, *ResultIconId, *ToolDisplayName, *ResultEncoded);
				}
				else if (!bMergeMode && CallContent.IsEmpty())
				{
					FString PendingContent = FullBody.TrimStartAndEnd();
					FString PendingEncoded = FGenericPlatformHttp::UrlEncode(PendingContent);
					Html.Appendf(
						TEXT("<div class='tool-result tool-call-block' id='tool-call-%d' onclick='toggleTool(\"tool-call-%d\")'>"
							"<div class='tool-header'>"
							"<div class='tool-icon-wrap'><svg class='icon' width='13' height='13' viewBox='0 0 24 24' style='color:#e09d35'><use href='#ic-wrench'/></svg></div>"
							"<span class='tool-name'>%s</span>"
							"<span class='tool-call-tag'>call</span>"
							"<span class='tool-chevron' style='margin-left:auto'>&#8250;</span>"
							"</div><div class='tool-content'><pre data-tool-json='%s'></pre></div></div>"),
						MessageIndex, MessageIndex, *ToolDisplayName, *PendingEncoded);
				}
				return Html.ToString();
			}
		}
		else
		{
			FString UrlEncoded = FGenericPlatformHttp::UrlEncode(Content);
			FString ThumbHtml;
			for (int32 PartIdx = 1; PartIdx < PartsArray->Num(); PartIdx++)
			{
				const TSharedPtr<FJsonObject>* PartObjPtr;
				if (!(*PartsArray)[PartIdx]->TryGetObject(PartObjPtr)) continue;
				const TSharedPtr<FJsonObject>* InlineDataPtr;
				if (!(*PartObjPtr)->TryGetObjectField(TEXT("inline_data"), InlineDataPtr)) continue;
				FString MimeType, Data;
				(*InlineDataPtr)->TryGetStringField(TEXT("mime_type"), MimeType);
				(*InlineDataPtr)->TryGetStringField(TEXT("data"), Data);
				if (Data.IsEmpty()) continue;
				ThumbHtml += FString::Printf(
					TEXT("<img src='data:%s;base64,%s' class='msg-img-thumb' onclick='openImgLightbox(this.src)'>"),
					*MimeType, *Data);
			}
			if (ThumbHtml.IsEmpty())
				Html.Appendf(TEXT("<div class='%s'><div class='speaker'>%s:%s</div><div data-md='%s'></div></div>"), *DivClass, *Speaker, *TsSpan, *UrlEncoded);
			else
				Html.Appendf(TEXT("<div class='%s'><div class='speaker'>%s:%s</div><div data-md='%s'></div><div class='msg-img-row'>%s</div></div>"), *DivClass, *Speaker, *TsSpan, *UrlEncoded, *ThumbHtml);
			return Html.ToString();
		}
	}
	else
	{
		static const TSet<FString> HiddenToolNames = {
			TEXT("get_selected_assets"), TEXT("get_current_folder"),
				TEXT("ask_user"), TEXT("proceed_with_plan"),
				TEXT("task"), TEXT("set_tasks"), TEXT("add_task"), TEXT("update_task"), TEXT("edit_task"), TEXT("remove_task"), TEXT("reorder_task"), TEXT("clear_tasks"), TEXT("get_tasks"),
			TEXT("search_tools"), TEXT("find_tool"), TEXT("discover_tools"), TEXT("list_assets_in_folder"),
			TEXT("get_tool_docs"), TEXT("get_handle_reference")
		};
		static const TSet<FString> HiddenToolActions = { TEXT("search_templates"), TEXT("apply_template") };

		auto FindMatchingBrace = [](const FString& Str, int32 OpenPos) -> int32
		{
			if (OpenPos >= Str.Len() || Str[OpenPos] != TEXT('{')) return INDEX_NONE;
			int32 Depth = 0;
			bool bInStr = false;
			bool bEsc = false;
			for (int32 i = OpenPos; i < Str.Len(); i++)
			{
				const TCHAR Ch = Str[i];
				if (bEsc) { bEsc = false; continue; }
				if (Ch == TEXT('\\') && bInStr) { bEsc = true; continue; }
				if (Ch == TEXT('"')) { bInStr = !bInStr; continue; }
				if (bInStr) continue;
				if (Ch == TEXT('{')) Depth++;
				else if (Ch == TEXT('}')) { Depth--; if (Depth == 0) return i; }
			}
			return INDEX_NONE;
		};

		struct FToolCallBlock { int32 Start; int32 End; TSharedPtr<FJsonObject> Json; FString Name; FString Action; };
		TArray<FToolCallBlock> ToolBlocks;
		int32 ToolContentCoveredUpTo = -1;
		{
			int32 SearchFrom = 0;
			while (SearchFrom < Content.Len())
			{
				int32 KeyPos = Content.Find(TEXT("\"tool_name\""), ESearchCase::CaseSensitive, ESearchDir::FromStart, SearchFrom);
				if (KeyPos == INDEX_NONE) break;

				int32 ScanBack = KeyPos - 1;
				while (ScanBack >= 0 && FChar::IsWhitespace(Content[ScanBack])) ScanBack--;
				if (ScanBack < 0 || Content[ScanBack] != TEXT('{'))
				{
					SearchFrom = KeyPos + 11;
					continue;
				}
				int32 Pos = ScanBack;

				int32 ClosePos = FindMatchingBrace(Content, Pos);
				if (ClosePos == INDEX_NONE)
				{
					ToolContentCoveredUpTo = Content.Len() - 1;
					break;
				}

				FString JsonStr = Content.Mid(Pos, ClosePos - Pos + 1);
				TSharedPtr<FJsonObject> ParsedJson;
				TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonStr);
				if (FJsonSerializer::Deserialize(Reader, ParsedJson) && ParsedJson.IsValid())
				{
					FString TName, TAction;
					ParsedJson->TryGetStringField(TEXT("tool_name"), TName);
					const TSharedPtr<FJsonObject>* TArgs;
					if (ParsedJson->TryGetObjectField(TEXT("arguments"), TArgs))
						(*TArgs)->TryGetStringField(TEXT("action"), TAction);

					if (!HiddenToolNames.Contains(TName) && !HiddenToolNames.Contains(TAction) && !HiddenToolActions.Contains(TAction))
					{
						ToolBlocks.Add({ Pos, ClosePos, ParsedJson, TName, TAction });
					}
					else if (ToolBlocks.Num() == 0)
					{
						return FString();
					}
				}
				ToolContentCoveredUpTo = ClosePos;
				SearchFrom = ClosePos + 1;
			}
		}

		static const TSet<FString> KnownToolNames = {
			TEXT("blueprint"), TEXT("niagara"), TEXT("pcg"), TEXT("sequencer"), TEXT("behavior_tree"),
			TEXT("animation"), TEXT("ik_retarget"), TEXT("control_rig"), TEXT("gas"), TEXT("environment"),
			TEXT("audio"), TEXT("landscape"), TEXT("foliage"), TEXT("material"), TEXT("level_actor"),
			TEXT("widget"), TEXT("input_system"), TEXT("spline"), TEXT("curve"), TEXT("data"),
			TEXT("physics"), TEXT("eqs"), TEXT("navmesh_ai"), TEXT("gameplay_tags"), TEXT("state_tree"),
			TEXT("pose_search"), TEXT("chooser"), TEXT("editor_utility"), TEXT("mesh"),
			TEXT("asset_management"), TEXT("play_test"), TEXT("vehicle"), TEXT("common_ui"),
			TEXT("get_tool_docs"), TEXT("get_current_folder"), TEXT("get_selected_assets"),
			TEXT("generate_texture"), TEXT("generate_pbr_material"), TEXT("retexture_mesh"),
			TEXT("generate_sound"),
		};

		if (ToolBlocks.Num() == 0)
		{

			int32 SecondSearchFrom = 0;
			while (SecondSearchFrom < Content.Len())
			{
				int32 BracePos = Content.Find(TEXT("{"), ESearchCase::CaseSensitive, ESearchDir::FromStart, SecondSearchFrom);
				if (BracePos == INDEX_NONE) break;

				int32 ClosePos = FindMatchingBrace(Content, BracePos);
				if (ClosePos == INDEX_NONE)
				{
					ToolContentCoveredUpTo = Content.Len() - 1;
					break;
				}

				FString JsonStr = Content.Mid(BracePos, ClosePos - BracePos + 1);
				TSharedPtr<FJsonObject> ParsedJson;
				TSharedRef<TJsonReader<>> Reader2 = TJsonReaderFactory<>::Create(JsonStr);
				if (FJsonSerializer::Deserialize(Reader2, ParsedJson) && ParsedJson.IsValid()
					&& ParsedJson->Values.Num() == 1)
				{
					auto It = ParsedJson->Values.CreateConstIterator();
					const FString FirstKey(*It->Key);
					if (It->Value.IsValid() && It->Value->Type == EJson::Object
						&& KnownToolNames.Contains(FirstKey))
					{
						FString TName = FirstKey;
						TSharedPtr<FJsonObject> TArgsObj = It->Value->AsObject();
						FString TAction;
						TArgsObj->TryGetStringField(TEXT("action"), TAction);

						if (!HiddenToolNames.Contains(TName) && !HiddenToolNames.Contains(TAction) && !HiddenToolActions.Contains(TAction))
						{
							TSharedPtr<FJsonObject> Synthetic = MakeShareable(new FJsonObject);
							Synthetic->SetStringField(TEXT("tool_name"), TName);
							Synthetic->SetObjectField(TEXT("arguments"), TArgsObj);
							ToolBlocks.Add({ BracePos, ClosePos, Synthetic, TName, TAction });
						}
						ToolContentCoveredUpTo = ClosePos;
					}
				}
				SecondSearchFrom = ClosePos + 1;
			}
		}

		if (ToolBlocks.Num() == 0)
		{
			int32 ThirdSearchFrom = 0;
			while (ThirdSearchFrom < Content.Len())
			{
				int32 BracePos = Content.Find(TEXT("{"), ESearchCase::CaseSensitive, ESearchDir::FromStart, ThirdSearchFrom);
				if (BracePos == INDEX_NONE) break;

				int32 ClosePos = FindMatchingBrace(Content, BracePos);
				if (ClosePos == INDEX_NONE)
				{
					ToolContentCoveredUpTo = Content.Len() - 1;
					break;
				}

				FString JsonStr = Content.Mid(BracePos, ClosePos - BracePos + 1);
				TSharedPtr<FJsonObject> ParsedJson;
				TSharedRef<TJsonReader<>> Reader3 = TJsonReaderFactory<>::Create(JsonStr);
				if (FJsonSerializer::Deserialize(Reader3, ParsedJson) && ParsedJson.IsValid())
				{
					FString TName;
					const TSharedPtr<FJsonObject>* TArgsField;
					if (ParsedJson->TryGetStringField(TEXT("name"), TName)
						&& ParsedJson->TryGetObjectField(TEXT("arguments"), TArgsField)
						&& KnownToolNames.Contains(TName))
					{
						FString TAction;
						(*TArgsField)->TryGetStringField(TEXT("action"), TAction);
						if (!HiddenToolNames.Contains(TName) && !HiddenToolNames.Contains(TAction) && !HiddenToolActions.Contains(TAction))
						{
							TSharedPtr<FJsonObject> Synthetic = MakeShareable(new FJsonObject);
							Synthetic->SetStringField(TEXT("tool_name"), TName);
							Synthetic->SetObjectField(TEXT("arguments"), *TArgsField);
							ToolBlocks.Add({ BracePos, ClosePos, Synthetic, TName, TAction });
						}
						ToolContentCoveredUpTo = ClosePos;
					}
				}
				ThirdSearchFrom = ClosePos + 1;
			}
		}

		if (ToolBlocks.Num() == 0)
		{
			FString DisplayContent = SanitizeEnvelopeFragment(Content);
			if (DisplayContent.IsEmpty())
			{
				return FString();
			}
			FString UrlEncodedContent = FGenericPlatformHttp::UrlEncode(DisplayContent);
			bool bIsError = false;
			MessageObject->TryGetBoolField(TEXT("is_error"), bIsError);
			if (bIsError)
			{
				Html.Appendf(TEXT("<div class='%s' data-error='1'><div class='speaker'>%s:%s</div><div data-md='%s'></div>"
					"<div class='retry-bar'><button class='retry-btn' onclick='retryLastMessage()'>&#8635; Retry</button></div>"
					"</div>"),
					*DivClass, *Speaker, *TsSpan, *UrlEncodedContent);
			}
			else
			{
				Html.Appendf(TEXT("<div class='%s'><div class='speaker'>%s:%s</div><div data-md='%s'></div></div>"),
					*DivClass, *Speaker, *TsSpan, *UrlEncodedContent);
			}
			return Html.ToString();
		}

		FString PreText = SanitizeEnvelopeFragment(Content.Left(ToolBlocks[0].Start));
		if (!PreText.IsEmpty())
		{
			FString UrlEncoded = FGenericPlatformHttp::UrlEncode(PreText);
			Html.Appendf(TEXT("<div class='ai-msg'><div class='speaker'>Architect:%s</div><div data-md='%s'></div></div>"), *TsSpan, *UrlEncoded);
		}

		auto BuildArgsPretty = [&Content](const FToolCallBlock& Block) -> FString
		{
			TSharedPtr<FJsonValue> ArgsValue;
			const TSharedPtr<FJsonObject>* ArgsObj;
			FString ArgsAsStr;
			if (Block.Json->TryGetObjectField(TEXT("arguments"), ArgsObj) && ArgsObj && ArgsObj->IsValid())
			{
				ArgsValue = MakeShared<FJsonValueObject>(*ArgsObj);
			}
			else if (Block.Json->TryGetStringField(TEXT("arguments"), ArgsAsStr) && !ArgsAsStr.IsEmpty())
			{
				TSharedPtr<FJsonValue> Parsed;
				TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(ArgsAsStr);
				if (FJsonSerializer::Deserialize(R, Parsed) && Parsed.IsValid())
					ArgsValue = Parsed;
			}
			else
			{
				for (const TCHAR* Alias : { TEXT("args"), TEXT("parameters"), TEXT("input") })
				{
					const TSharedPtr<FJsonObject>* AliasObj;
					if (Block.Json->TryGetObjectField(Alias, AliasObj) && AliasObj && AliasObj->IsValid())
					{
						ArgsValue = MakeShared<FJsonValueObject>(*AliasObj);
						break;
					}
				}
			}

			FString Out;
			if (ArgsValue.IsValid() && ArgsValue->Type == EJson::Object)
			{
				TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> PW =
					TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Out);
				FJsonSerializer::Serialize(ArgsValue->AsObject().ToSharedRef(), PW);
				PW->Close();
			}
			if (Out.IsEmpty() && ArgsValue.IsValid())
			{
				TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> CW =
					TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
				FJsonSerializer::Serialize(ArgsValue, TEXT(""), CW);
				CW->Close();
			}
			if (Out.IsEmpty())
			{
				const int32 SourceStart = Block.Start;
				const int32 SourceEnd   = Block.End;
				if (SourceStart >= 0 && SourceEnd > SourceStart)
				{
					const FString Raw = Content.Mid(SourceStart, SourceEnd - SourceStart + 1);
					const int32 Key = Raw.Find(TEXT("\"arguments\""));
					if (Key != INDEX_NONE)
					{
						int32 Colon = Raw.Find(TEXT(":"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Key);
						if (Colon != INDEX_NONE)
						{
							Out = Raw.Mid(Colon + 1).TrimStartAndEnd();
							if (Out.EndsWith(TEXT("}"))) Out.RemoveFromEnd(TEXT("}"));
							Out = Out.TrimEnd();
						}
					}
					else if (!ArgsAsStr.IsEmpty())
					{
						Out = ArgsAsStr;
					}
				}
			}
			if (Out.IsEmpty()) Out = TEXT("{}");
			return Out.Replace(TEXT("<"), TEXT("&lt;")).Replace(TEXT(">"), TEXT("&gt;"));
		};

		if (ToolBlocks.Num() > 1)
		{
			FString InnerCallHtml;
			for (int32 i = 0; i < ToolBlocks.Num(); i++)
			{
				const FToolCallBlock& Block = ToolBlocks[i];
				FString DisplayLabel = !Block.Action.IsEmpty()
					? FString::Printf(TEXT("%s \xB7 %s"), *Block.Name, *Block.Action) : Block.Name;

				FString ArgsStr = BuildArgsPretty(Block);

				InnerCallHtml += FString::Printf(
					TEXT("<div style='display:flex;align-items:flex-start;padding:6px 0;border-bottom:1px solid #2e2b28;gap:8px;'>"
						"<div class='tool-icon-wrap' style='flex-shrink:0;margin-top:2px;'><svg class='icon' width='13' height='13' viewBox='0 0 24 24' style='color:#e09d35'><use href='#ic-wrench'/></svg></div>"
						"<div style='flex:1;'>"
						"<span style='color:#e0c070;font-size:12px;font-weight:500;'>%s</span>"
						"<pre style='margin:4px 0 0 0;font-size:11px;color:#9e9a95;white-space:pre-wrap;word-break:break-all;'>%s</pre>"
						"</div></div>"),
					*DisplayLabel, *ArgsStr);
			}

			FString GroupId = FString::Printf(TEXT("callgroup-%d"), MessageIndex);
			Html.Appendf(
				TEXT("<div class='tool-result tool-call-block' id='%s' onclick='toggleTool(\"%s\")'>"
					"<div class='tool-header'>"
					"<div class='tool-icon-wrap'><svg class='icon' width='13' height='13' viewBox='0 0 24 24' style='color:#e09d35'><use href='#ic-wrench'/></svg></div>"
					"<span class='tool-name'>%d tool calls</span>"
					"<span class='tool-call-tag'>call</span>"
					"<span class='tool-chevron' style='margin-left:auto'>&#8250;</span>"
					"</div>"
					"<div class='tool-content'>%s</div>"
					"</div>"),
				*GroupId, *GroupId, ToolBlocks.Num(), *InnerCallHtml);
		}
		else
		{
			const FToolCallBlock& Block = ToolBlocks[0];
			FString DisplayLabel = !Block.Action.IsEmpty()
				? FString::Printf(TEXT("%s \xB7 %s"), *Block.Name, *Block.Action) : Block.Name;

			FString ArgsStr = BuildArgsPretty(Block);

			FString BlockId = FString::Printf(TEXT("call-%d-0"), MessageIndex);
			Html.Appendf(
				TEXT("<div class='tool-result tool-call-block' id='%s' onclick='toggleTool(\"%s\")'>"
					"<div class='tool-header'>"
					"<div class='tool-icon-wrap'><svg class='icon' width='13' height='13' viewBox='0 0 24 24' style='color:#e09d35'><use href='#ic-wrench'/></svg></div>"
					"<span class='tool-name'>%s</span>"
					"<span class='tool-call-tag'>call</span>"
					"<span class='tool-chevron' style='margin-left:auto'>&#8250;</span>"
					"</div>"
					"<div class='tool-content'><pre>%s</pre></div>"
					"</div>"),
				*BlockId, *BlockId, *DisplayLabel, *ArgsStr);
		}

		FString TrailingText = SanitizeEnvelopeFragment(Content.Mid(ToolContentCoveredUpTo + 1));
		if (!TrailingText.IsEmpty())
		{
			FString UrlEncoded = FGenericPlatformHttp::UrlEncode(TrailingText);
			Html.Appendf(TEXT("<div class='ai-msg' style='padding:6px 14px;margin-bottom:4px;'><div data-md='%s'></div></div>"), *UrlEncoded);
		}

		return Html.ToString();
	}
}

void SUECPMainWidget::RefreshAllChatViews()
{
	RefreshArchitectChatView();
	RefreshProjectChatView();
	RefreshChatHistoryView();
	NotifyPlanUpdated(ActiveArchitectChatID); NotifyTasksUpdated(ActiveArchitectChatID);
}

void SUECPMainWidget::RefreshArchitectChatView()
{
	if (bSuppressChatViewRefresh) return;
	if (!AppBridgeObject) return;

	const TArray<TSharedPtr<FJsonValue>>* ActiveHistory = &ArchitectConversationHistory;
	bool bFromAgent = false;
	if (FAgentRunnerInstance* ActiveInst = AgentInstances.FindRef(ActiveArchitectChatID).Get())
	{
		if (ActiveInst->SourceView != EAgentSourceView::ProjectScanner)
		{
			ActiveHistory = &ActiveInst->LocalHistory;
			bFromAgent = true;
		}
	}

	if (ArchitectUiActiveChatOverride.IsEmpty())
	{
		if (bIsArchitectThinking)
			AppBridgeObject->PushThinkingState(TEXT("architect"), true, TEXT("AI is working..."), TEXT(""));
		else
			AppBridgeObject->ExecJs(TEXT("if(typeof onGenerationDone==='function')onGenerationDone('architect')"));
	}

	{
		const double Now = FPlatformTime::Seconds();
		const bool bSourceChanged = (bFromAgent != LastRenderedArchitectFromAgent);
		const bool bChatSwitched  = (ActiveArchitectChatID != LastRenderedArchitectChatId);
		const bool bSkipThrottle  = bSourceChanged || bChatSwitched;
		const double Elapsed = Now - LastArchitectRefreshSeconds;
		if (!bSkipThrottle && Elapsed < 0.040)
		{
			if (!bArchitectRefreshPending)
			{
				bArchitectRefreshPending = true;
				const float DelaySeconds = FMath::Max(0.001f, 0.040f - (float)Elapsed);
				TWeakPtr<SWidget> DeferredRefreshTarget = AsWeak();
				FTSTicker::GetCoreTicker().AddTicker(
					FTickerDelegate::CreateLambda([DeferredRefreshTarget](float) -> bool
					{
						if (TSharedPtr<SWidget> Pinned = DeferredRefreshTarget.Pin())
						{
							SUECPMainWidget* Self = static_cast<SUECPMainWidget*>(Pinned.Get());
							Self->bArchitectRefreshPending = false;
							Self->RefreshArchitectChatView();
						}
						return false;
					}),
					DelaySeconds);
			}
			return;
		}
		LastArchitectRefreshSeconds = Now;
	}

	const int32 CurrentCount = ActiveHistory->Num();
	const bool bSameChat   = (ActiveArchitectChatID == LastRenderedArchitectChatId);
	const bool bSameSource = bSameChat && (bFromAgent == LastRenderedArchitectFromAgent);

	const bool bBatchToolBlocks = IsArchitectBatchedToolBlocksEnabled();
	const bool bAgentStreaming = AgentInstances.Contains(ActiveArchitectChatID)
		|| NativeLoopStreamingChats.Contains(ActiveArchitectChatID);

	TArray<FArchitectRenderRange> NewRanges;
	ComputeArchitectRenderRanges(*ActiveHistory, bBatchToolBlocks, NewRanges);

	TArray<uint32> NewHashes;
	NewHashes.SetNumUninitialized(NewRanges.Num());
	for (int32 r = 0; r < NewRanges.Num(); r++)
		NewHashes[r] = ComputeArchitectRangeContentHash(*ActiveHistory, NewRanges[r]);

	const int32 LiveTailRangeIdx = bAgentStreaming ? (NewRanges.Num() - 1) : INDEX_NONE;

	auto RenderRangeHtml = [this, ActiveHistory](const FArchitectRenderRange& Range, uint32 ContentHash, bool bAllowCache) -> FString
	{
		if (bAllowCache)
		{
			if (const FArchitectRangeCacheEntry* Hit = ArchitectRangeHtmlCache.Find(Range.StartIndex))
			{
				if (Hit->End == Range.EndIndex && Hit->ContentHash == ContentHash)
					return Hit->Html;
			}
		}

		FString Inner;
		if (Range.bBatched)
		{
			Inner = RenderArchitectRangeBatchHtml(*ActiveHistory, Range.StartIndex, Range.EndIndex);
		}
		else
		{
			const TSharedPtr<FJsonObject>& MsgObj = (*ActiveHistory)[Range.StartIndex]->AsObject();
			Inner = RenderSingleArchitectMessageHtml(MsgObj, Range.StartIndex);
		}
		if (Inner.IsEmpty()) return FString();
		FString Html = FString::Printf(TEXT("<div id='arch-msg-%d' data-hist-idx='%d'>%s</div>"),
			Range.StartIndex, Range.StartIndex, *Inner);

		if (bAllowCache)
		{
			FArchitectRangeCacheEntry& Entry = ArchitectRangeHtmlCache.FindOrAdd(Range.StartIndex);
			Entry.End         = Range.EndIndex;
			Entry.ContentHash = ContentHash;
			Entry.Html        = Html;
		}
		return Html;
	};

	const bool bShrunk        = bSameSource && (CurrentCount < LastRenderedArchitectHistoryCount);
	const bool bForcedRebuild = bArchitectForceFullRebuildOnce;
	bArchitectForceFullRebuildOnce = false;
	const bool bFullRebuild   = !bSameSource || bShrunk || bForcedRebuild;

	if (bFullRebuild)
	{
		if (!bSameSource || bShrunk)
			ArchitectRangeHtmlCache.Reset();

		TArray<TSharedPtr<FJsonValue>> Messages;
		for (int32 r = 0; r < NewRanges.Num(); r++)
		{
			const FArchitectRenderRange& Range = NewRanges[r];
			const bool bAllowCache = (r != LiveTailRangeIdx);
			FString Html = RenderRangeHtml(Range, NewHashes[r], bAllowCache);
			if (Html.IsEmpty()) continue;
			TSharedPtr<FJsonObject> Msg = MakeShareable(new FJsonObject);
			Msg->SetStringField(TEXT("html"), Html);
			Messages.Add(MakeShareable(new FJsonValueObject(Msg)));
		}

		FString Json;
		TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Json);
		FJsonSerializer::Serialize(Messages, W);

		AppBridgeObject->PushChatHistory(TEXT("architect"), Json);
	}
	else
	{

		TMap<int32, FArchitectRangeKey> OldByStart;
		OldByStart.Reserve(LastRenderedArchitectRanges.Num());
		for (const FArchitectRangeKey& K : LastRenderedArchitectRanges)
			OldByStart.Add(K.Start, K);

		TSet<int32> SeenOldStarts;
		const int32 LastNewIdx = NewRanges.Num() - 1;

		for (int32 r = 0; r < NewRanges.Num(); r++)
		{
			const FArchitectRenderRange& Range = NewRanges[r];
			const FArchitectRangeKey* Old = OldByStart.Find(Range.StartIndex);
			if (Old) SeenOldStarts.Add(Range.StartIndex);

			const bool bIsLast = (r == LastNewIdx);
			const bool bForceUpdate = bIsLast && bAgentStreaming;
			const uint32 NewHash = NewHashes[r];
			const bool bRangeChanged = !Old || Old->End != Range.EndIndex || Old->ContentHash != NewHash;

			if (!bForceUpdate && !bRangeChanged)
				continue;

			const bool bAllowCache = (r != LiveTailRangeIdx);
			FString Html = RenderRangeHtml(Range, NewHash, bAllowCache);
			if (Html.IsEmpty())
			{
				if (Old)
				{
					ArchitectRangeHtmlCache.Remove(Range.StartIndex);
					AppBridgeObject->ExecJs(FString::Printf(
						TEXT("if(typeof deleteArchitectMsgAt==='function')deleteArchitectMsgAt(%d)"), Range.StartIndex));
				}
				continue;
			}

			if (Old)
			{
				FString Enc = FGenericPlatformHttp::UrlEncode(Html);
				AppBridgeObject->ExecJs(FString::Printf(
					TEXT("if(typeof updateArchitectMsgAt==='function')updateArchitectMsgAt(%d,decodeURIComponent('%s'))"),
					Range.StartIndex, *Enc));
			}
			else
			{
				AppBridgeObject->PushAppendMessage(TEXT("architect"), Html);
			}
		}

		for (const FArchitectRangeKey& K : LastRenderedArchitectRanges)
		{
			if (!SeenOldStarts.Contains(K.Start))
			{
				ArchitectRangeHtmlCache.Remove(K.Start);
				AppBridgeObject->ExecJs(FString::Printf(
					TEXT("if(typeof deleteArchitectMsgAt==='function')deleteArchitectMsgAt(%d)"), K.Start));
			}
		}
	}

	if (bAppShellInitialized)
	{
		LastRenderedArchitectHistoryCount = CurrentCount;
		LastRenderedArchitectChatId       = ActiveArchitectChatID;
		LastRenderedArchitectFromAgent    = bFromAgent;
		LastRenderedArchitectRanges.Reset(NewRanges.Num());
		for (int32 r = 0; r < NewRanges.Num(); r++)
		{
			const FArchitectRenderRange& Range = NewRanges[r];
			FArchitectRangeKey K;
			K.Start       = Range.StartIndex;
			K.End         = Range.EndIndex;
			K.ContentHash = NewHashes[r];
			LastRenderedArchitectRanges.Add(K);
		}
	}

}

void SUECPMainWidget::SendArchitectChatRequest()
{
	IUECPCoreModule::Get().GetArchitectService().SendChatRequest();
}

void SUECPMainWidget::InvalidatePromptCaches()
{
	IUECPCoreModule::Get().GetArchitectService().InvalidatePromptCaches();
}

void SUECPMainWidget::OnAssetUpdated(FString AssetName)
{
	IUECPCoreModule::Get().GetArchitectService().OnPromptAssetUpdated(AssetName);
}

void SUECPMainWidget::OnUpdateCheckCompleted()
{
}

int32 SUECPMainWidget::EstimateFullRequestTokens(const TArray<TSharedPtr<FJsonValue>>& Messages)
{
	int32 StaticChars = 0;
	int32 AssetRefChars = 0;
	int32 GddChars = 0;
	int32 MemoryChars = 0;
	int32 CustomInstrChars = 0;
	int32 HistoryChars = 0;

	StaticChars = IUECPCoreModule::Get().GetArchitectService().GetCachedStaticPromptChars();

	AssetRefChars = FAssetReferenceManager::Get().GetSummaryForAI().Len();
	GddChars = IUECPCoreModule::Get().GetGddService().GetContentForAI().Len();
	MemoryChars = IUECPCoreModule::Get().GetAiMemoryService().GetContentForAI().Len();
	CustomInstrChars = CustomInstructions.Len();

	for (const TSharedPtr<FJsonValue>& MsgValue : Messages)
	{
		const TSharedPtr<FJsonObject>& Msg = MsgValue->AsObject();
		FString Role, Content;

		if (Msg->TryGetStringField(TEXT("role"), Role))
		{
			HistoryChars += Role.Len();
		}

		if (Msg->TryGetStringField(TEXT("content"), Content))
		{
			HistoryChars += Content.Len();
		}
		else
		{
			const TArray<TSharedPtr<FJsonValue>>* Parts;
			if (Msg->TryGetArrayField(TEXT("parts"), Parts))
			{
				for (const TSharedPtr<FJsonValue>& Part : *Parts)
				{
					const TSharedPtr<FJsonObject>& PartObj = Part->AsObject();
					FString Text;
					if (PartObj->TryGetStringField(TEXT("text"), Text))
					{
						HistoryChars += Text.Len();
					}
					const TSharedPtr<FJsonObject, ESPMode::ThreadSafe>* InlineDataObj = nullptr;
					if (PartObj->TryGetObjectField(TEXT("inline_data"), InlineDataObj))
					{
						HistoryChars += 4000;
					}
				}
			}
		}
	}

	int32 TotalChars = StaticChars + AssetRefChars + GddChars + MemoryChars + CustomInstrChars + HistoryChars;
	int32 TotalTokens = TotalChars / 4;

	return TotalTokens;
}

int32 SUECPMainWidget::FindMatchingClosingBrace(const FString& Haystack, int32 StartIndex)
{
	if (StartIndex >= Haystack.Len() || Haystack[StartIndex] != '{')
	{
		return INDEX_NONE;
	}

	int32 BraceCount = 0;
	bool bInString = false;
	bool bEscaped = false;

	for (int32 i = StartIndex; i < Haystack.Len(); ++i)
	{
		TCHAR Char = Haystack[i];

		if (bEscaped)
		{
			bEscaped = false;
			continue;
		}

		if (Char == '\\')
		{
			bEscaped = true;
			continue;
		}

		if (Char == '"')
		{
			bInString = !bInString;
			continue;
		}

		if (!bInString)
		{
			if (Char == '{')
			{
				BraceCount++;
			}
			else if (Char == '}')
			{
				BraceCount--;
				if (BraceCount == 0)
				{
					return i;
				}
			}
		}
	}

	return INDEX_NONE;
}

#undef LOCTEXT_NAMESPACE
