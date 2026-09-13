// Copyright 2026, BlueprintsLab, All rights reserved

#include "SUECPMainWidget.h"
#include "UECPCoreModule.h"
#include "Services/IUECPNotificationService.h"
#include "FUECPShellNotificationSink.h"
#include "Services/IUECPBugReportService.h"
#include "Services/IUECPGddService.h"
#include "Services/IUECPAiMemoryService.h"
#include "Services/IUECPVoiceService.h"
#include "Services/IUECPAnalystService.h"
#include "Services/IUECPScannerService.h"
#include "Services/IUECPArchitectService.h"
#include "Services/IUECPAgentRunnerService.h"
#include "Services/IUECPCrewService.h"
#include "Types/CrewTypes.h"
#include "Services/IUECPACPRegistryService.h"
#include "Widget/UUECPSettingsBridge.h"
#include "Widget/UUECPAppBridge.h"
#include "Widget/UUECPMeshyBridge.h"
#include "Widget/UUECPImageGenBridge.h"
#include "Widget/UUECPGddBridge.h"
#include "Widget/UUECPMemoryBridge.h"
#include "Widget/UUECPBugReportBridge.h"
#include "Widget/UUECPScannerBridge.h"
#include "Tools/AssetManagementTools.h"
#include "LearningManager.h"
#include "Tools/GitTools.h"
#include "Tools/ProfilerTools.h"
#include "Async/Async.h"
#include "UIConfigManager.h"
#include "Managers/UpdateManager.h"
#include "Managers/ProviderConfigManager.h"
#include "Managers/TelemetryManager.h"
#include "Utils/WidgetUtils.h"
#include "Utils/TokenUtils.h"
#include "Utils/DiagramUtils.h"
#include "LevelEditor.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SWindow.h"
#include "Framework/Application/SlateApplication.h"
#include "ToolMenus.h"
#include "Misc/Base64.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SScrollBox.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "EditorAssetLibrary.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Blueprint.h"
#include "HAL/FileManager.h"
#include "Describers/BpGraphDescriber.h"
#include "Components/PanelWidget.h"
#include "Blueprint/WidgetTree.h"
#include "WidgetBlueprint.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "K2Node_Event.h"
#include "EdGraphSchema_K2.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "BlueprintEditor.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "SWebBrowser.h"
#include "Interfaces/IPluginManager.h"
#include "Utils/MountResolver.h"
#include "GenericPlatform/GenericPlatformHttp.h"
#include "Internationalization/Regex.h"
#include "WebBrowserModule.h"
#include "Describers/MaterialGraphDescriber.h"
#include "Describers/MaterialNodeDescriber.h"
#include "Describers/BtGraphDescriber.h"
#include "Materials/Material.h"
#include "BehaviorTree/BehaviorTree.h"
#include "Engine/UserDefinedEnum.h"
#include "Misc/EngineVersionComparison.h"
#if UE_VERSION_OLDER_THAN(5, 5, 0)
#include "Engine/UserDefinedStruct.h"
#else
#include "StructUtils/UserDefinedStruct.h"
#endif
#include "Engine/DataAsset.h"
#include "Engine/DataTable.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "EnhancedInputComponent.h"
#include "UObject/Interface.h"
#include "Kismet2/StructureEditorUtils.h"
#include "IMaterialEditor.h"
#include "UECPShellModule.h"
#include "BehaviorTreeEditor.h"
#include "Editor/UMGEditor/Public/WidgetBlueprintEditor.h"
#include "SEditorSyncPanel.h"
#include "MaterialGraph/MaterialGraphNode.h"
#if WITH_EDITOR
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Engine/Texture.h"
#endif
#include <Editor/MaterialEditor/Private/MaterialEditor.h>
#include "Components/ContentWidget.h"
#include "UObject/TextProperty.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "DesktopPlatformModule.h"
#include "IDesktopPlatform.h"
#include "SUECPAssetMentionPopup.h"
#include "AssetReferenceManager.h"
#include "ApiKeyManager.h"
#include "TextureGenManager.h"
#include "MeshAssetManager.h"
#include "Widgets/Images/SImage.h"

#if PLATFORM_WINDOWS
#include "Windows/WindowsPlatformMisc.h"
#include "Windows/AllowWindowsPlatformTypes.h"
#include <Windows.h>
#include "Windows/HideWindowsPlatformTypes.h"
#elif PLATFORM_MAC
#include "Mac/MacApplication.h"
#include "Cocoa/Cocoa.h"
#include <AppKit/AppKit.h>

static id GBGAMacKeyMonitor = nil;

static void InstallMacClipboardMonitor()
{
	if (GBGAMacKeyMonitor != nil) return;
	GBGAMacKeyMonitor = [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskKeyDown
		handler:^NSEvent*(NSEvent* event) {
			NSEventModifierFlags mods = event.modifierFlags & NSEventModifierFlagDeviceIndependentFlagsMask;
			if (!(mods & NSEventModifierFlagCommand)) return event;
			if  (mods & NSEventModifierFlagOption)   return event;
			if  (mods & NSEventModifierFlagControl)  return event;

			NSString* ch = [event.charactersIgnoringModifiers lowercaseString];
			bool bShift = (mods & NSEventModifierFlagShift) != 0;

			SEL action = nil;
			if      ([ch isEqualToString:@"c"]) action = @selector(copy:);
			else if ([ch isEqualToString:@"x"]) action = @selector(cut:);
			else if ([ch isEqualToString:@"v"]) action = @selector(paste:);
			else if ([ch isEqualToString:@"a"]) action = @selector(selectAll:);
			else if ([ch isEqualToString:@"z"]) action = bShift ? @selector(redo:) : @selector(undo:);

			if (action == nil) return event;

			if ([NSApp sendAction:action to:nil from:nil]) return nil;
			return event;
		}];
}

static void RemoveMacClipboardMonitor()
{
	if (GBGAMacKeyMonitor == nil) return;
	[NSEvent removeMonitor:GBGAMacKeyMonitor];
	GBGAMacKeyMonitor = nil;
}
#endif
#include "HAL/PlatformApplicationMisc.h"

#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Modules/ModuleManager.h"
#include "Widgets/SOverlay.h"
#include "Async/Async.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "UObject/SavePackage.h"
#include "Engine/World.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "Engine/StaticMeshActor.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Factories/MaterialFactoryNew.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "AssetToolsModule.h"
#include "Misc/EngineVersionComparison.h"
#if UE_VERSION_OLDER_THAN(5, 5, 0)
#include "Engine/UserDefinedStruct.h"
#else
#include "StructUtils/UserDefinedStruct.h"
#endif
#include "Engine/UserDefinedEnum.h"
#include "Kismet2/StructureEditorUtils.h"
#include "UserDefinedStructure/UserDefinedStructEditorData.h"
#include "Subsystems/EditorActorSubsystem.h"
#include "Blueprint/UserWidget.h"
#include "Components/TextBlock.h"
#include "Framework/Text/TextLayout.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/PanelSlot.h"
#include "UObject/Package.h"
class SConversationListRow;
#include "Framework/Application/IInputProcessor.h"

class FBGAInputProcessor : public IInputProcessor
{
public:
	FBGAInputProcessor(SUECPMainWidget* InOwner) : Owner(InOwner) {}
	virtual void Tick(const float, FSlateApplication&, TSharedRef<ICursor>) override {}
	virtual bool HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) override
	{
		if (!Owner) return false;

		bool bPluginHasFocus = Owner->IsHovered()
			|| (Owner->AppBrowser.IsValid() && Owner->AppBrowser->IsHovered())
			|| (Owner->SettingsBrowser.IsValid() && Owner->SettingsBrowser->IsHovered());

		if (Owner->bCapturingKeybind)
		{
			FKey PressedKey = InKeyEvent.GetKey();

			if (PressedKey == EKeys::Escape)
			{
				Owner->bCapturingKeybind = false;
				return true;
			}

			if (PressedKey == EKeys::LeftAlt || PressedKey == EKeys::RightAlt
				|| PressedKey == EKeys::LeftControl || PressedKey == EKeys::RightControl
				|| PressedKey == EKeys::LeftShift || PressedKey == EKeys::RightShift)
			{
				return true;
			}

			FKey Modifier = EKeys::Invalid;
			if (InKeyEvent.IsLeftAltDown() || InKeyEvent.IsRightAltDown()) Modifier = EKeys::LeftAlt;
			else if (InKeyEvent.IsControlDown()) Modifier = EKeys::LeftControl;
			else if (InKeyEvent.IsShiftDown()) Modifier = EKeys::LeftShift;

			if (Owner->CapturingKeybindTarget == 1)
			{
				Owner->VoicePTTKeybind.ModifierKey = Modifier;
				Owner->VoicePTTKeybind.ActionKey = PressedKey;
			}
			else
			{
				Owner->ArrangeNodesKeybind.ModifierKey = Modifier;
				Owner->ArrangeNodesKeybind.ActionKey = PressedKey;
			}
			Owner->bCapturingKeybind = false;
			Owner->SaveKeybindConfig();
			return true;
		}

		if (Owner->bUiCapturingKeybind)
			return false;

		const auto& KB = Owner->ArrangeNodesKeybind;
		bool bModifierMatch = false;
		if (KB.ModifierKey == EKeys::Invalid)
		{
			bModifierMatch = !InKeyEvent.IsAltDown() && !InKeyEvent.IsControlDown() && !InKeyEvent.IsShiftDown();
		}
		else if (KB.ModifierKey == EKeys::LeftAlt)
		{
			bModifierMatch = (InKeyEvent.IsLeftAltDown() || InKeyEvent.IsRightAltDown())
				&& !InKeyEvent.IsControlDown() && !InKeyEvent.IsShiftDown();
		}
		else if (KB.ModifierKey == EKeys::LeftControl)
		{
			bModifierMatch = InKeyEvent.IsControlDown()
				&& !InKeyEvent.IsAltDown() && !InKeyEvent.IsShiftDown();
		}
		else if (KB.ModifierKey == EKeys::LeftShift)
		{
			bModifierMatch = InKeyEvent.IsShiftDown()
				&& !InKeyEvent.IsAltDown() && !InKeyEvent.IsControlDown();
		}

		if (bModifierMatch && InKeyEvent.GetKey() == KB.ActionKey)
		{
			Owner->ArrangeActiveGraph();
			return true;
		}

		{
			IUECPVoiceService& Voice = IUECPCoreModule::Get().GetVoiceService();
			if (Voice.IsEnabled() && !Voice.IsRecording())
			{
				const auto& VKB = Owner->VoicePTTKeybind;
				bool bVModMatch = false;
				if (VKB.ModifierKey == EKeys::Invalid)
					bVModMatch = !InKeyEvent.IsAltDown() && !InKeyEvent.IsControlDown() && !InKeyEvent.IsShiftDown();
				else if (VKB.ModifierKey == EKeys::LeftAlt)
					bVModMatch = (InKeyEvent.IsLeftAltDown() || InKeyEvent.IsRightAltDown());
				else if (VKB.ModifierKey == EKeys::LeftControl)
					bVModMatch = InKeyEvent.IsControlDown();
				else if (VKB.ModifierKey == EKeys::LeftShift)
					bVModMatch = InKeyEvent.IsShiftDown();

				if (bVModMatch && InKeyEvent.GetKey() == VKB.ActionKey)
				{
					Voice.ToggleMic();
					return true;
				}
			}
		}

		if (bPluginHasFocus && !InKeyEvent.IsControlDown() && !InKeyEvent.IsAltDown())
		{
			static const TSet<FKey> FnHotkeys = {
				EKeys::F1, EKeys::F2, EKeys::F3, EKeys::F4, EKeys::F5,
				EKeys::F6, EKeys::F7, EKeys::F8, EKeys::F9, EKeys::F10, EKeys::F11
			};
			if (FnHotkeys.Contains(InKeyEvent.GetKey()))
				return true;

#if !PLATFORM_MAC
			static const TSet<FKey> AlphaHotkeys = {
				EKeys::F, EKeys::G, EKeys::H, EKeys::W, EKeys::E, EKeys::R,
				EKeys::T, EKeys::L, EKeys::P, EKeys::O,
			};
			if (AlphaHotkeys.Contains(InKeyEvent.GetKey()))
				return true;
#endif
		}

		return false;
	}

	virtual bool HandleKeyUpEvent(FSlateApplication&, const FKeyEvent& InKeyEvent) override
	{
		if (!Owner) return false;

		{
			IUECPVoiceService& Voice = IUECPCoreModule::Get().GetVoiceService();
			if (Voice.IsRecording())
			{
				const auto& VKB = Owner->VoicePTTKeybind;
				if (InKeyEvent.GetKey() == VKB.ActionKey)
				{
					Voice.ToggleMic();
					return true;
				}
			}
		}

		return false;
	}

	virtual const TCHAR* GetDebugName() const override { return TEXT("BGA_InputProcessor"); }
private:
	SUECPMainWidget* Owner = nullptr;
};

DEFINE_LOG_CATEGORY(LogUECPShell);

FString AssembleTextFormat(const TArray<uint8>& PackedData, const FString& ValidationKey)
{
	FString AssembledString;
	TArray<uint8> KeyBytes;
	FTCHARToUTF8 Converter(*ValidationKey);
	KeyBytes.Append((uint8*)Converter.Get(), Converter.Length());
	if (KeyBytes.Num() == 0) return FString();
	for (int32 i = 0; i < PackedData.Num(); ++i)
	{
		AssembledString += (TCHAR)(PackedData[i] ^ KeyBytes[i % KeyBytes.Num()]);
	}
	return AssembledString;
}
const TCHAR* const kLastKnownStateKey = TEXT("LastKnownState");

const FString POS = TEXT("Y1YZY41FO2520KBS");
static const FName CodeExplainerTabName("CodeExplainer");

#define LOCTEXT_NAMESPACE "SUECPMainWidget"

void SUECPMainWidget::OnBrowserLoadError()
{
	UE_LOG(LogUECPShell, Error, TEXT("SWebBrowser failed to load its content. This is often a first-run timing issue."));

	const FString ErrorHtml = TEXT(
		"<!DOCTYPE html><html><head><style>"
		"body { font-family: sans-serif; background: #2a2a2a; color: #e0e0e0; padding: 20px; line-height: 1.6; }"
		"h3 { color: #ffa07a; }"
		"</style></head><body>"
		"<h3>Content Failed to Load</h3>"
		"<p>The browser component could not render the AI's response.</p>"
		"<p>This can sometimes happen when the plugin is first opened. Please try sending your message again or restarting the chat.</p>"
		"</body></html>"
	);

}

bool SUECPMainWidget::OnBrowserLoadUrl(const FString& Method, const FString& Url, FString& Response)
{
	if (Url.StartsWith(TEXT("ue://diagram-popout?data=")))
	{
		FString EncodedData = Url.Mid(25);
		FString DiagramData = FGenericPlatformHttp::UrlDecode(EncodedData);

		TSharedPtr<SWindow> DiagramWindow = DiagramUtils::CreateDiagramWindow(DiagramData);

		TSharedPtr<SWindow> ParentWindow = FSlateApplication::Get().GetActiveTopLevelWindow();
		if (ParentWindow.IsValid())
		{
			FSlateApplication::Get().AddWindowAsNativeChild(DiagramWindow.ToSharedRef(), ParentWindow.ToSharedRef());
		}
		else
		{
			FSlateApplication::Get().AddWindow(DiagramWindow.ToSharedRef());
		}

		Response = DiagramUtils::GetDiagramCloseResponseHtml();
		return true;
	}

	if (Url == TEXT("ue://ai-analyze-perf") || Url == TEXT("ue://ai-analyze-perf/"))
	{
		if (!LastProfileResultJson.IsEmpty())
		{
			FString AiPrompt = TEXT("**INSTRUCTIONS — MUST FOLLOW:** Analyze the performance data below using ONLY the data provided. DO NOT call the `search` tool. DO NOT call any web research, fetch, or browse tool. DO NOT look up Unreal Engine documentation or forum posts. The trace data is complete and self-contained — your job is to interpret it directly. If you need conceptual context, rely on your training knowledge. Tool calls for this analysis will be rejected as wasted time.\n\n");
			AiPrompt += TEXT("Analyze this Unreal Engine performance profile and provide actionable optimization recommendations. Focus on:\n");
			AiPrompt += TEXT("1. Frame budget analysis (target: 60fps = 16.6ms per frame)\n");
			AiPrompt += TEXT("2. Blueprint-related hotspots (functions containing BP_, Receive, Blueprint)\n");
			AiPrompt += TEXT("3. CPU vs GPU bottleneck identification\n");
			AiPrompt += TEXT("4. Memory optimization opportunities\n");
			AiPrompt += TEXT("5. Specific actionable steps to improve performance\n\n");
			AiPrompt += TEXT("Here is the profiling data:\n\n");

			TSharedPtr<FJsonObject> PerfObj;
			TSharedRef<TJsonReader<>> PerfReader = TJsonReaderFactory<>::Create(LastProfileResultJson);
			if (FJsonSerializer::Deserialize(PerfReader, PerfObj) && PerfObj.IsValid())
			{
				if (PerfObj->HasField(TEXT("frame_stats")))
				{
					auto FS = PerfObj->GetObjectField(TEXT("frame_stats"));
					AiPrompt += FString::Printf(TEXT("## Frame Statistics\n- Frames: %.0f\n- Avg: %.2fms (%.1f FPS)\n- Min: %.2fms\n- Max: %.2fms\n- P99: %.2fms\n\n"),
						FS->GetNumberField(TEXT("game_frame_count")),
						FS->GetNumberField(TEXT("avg_ms")), FS->GetNumberField(TEXT("avg_fps")),
						FS->GetNumberField(TEXT("min_ms")), FS->GetNumberField(TEXT("max_ms")),
						FS->GetNumberField(TEXT("p99_ms")));
				}
				const TArray<TSharedPtr<FJsonValue>>* CpuFns;
				if (PerfObj->TryGetArrayField(TEXT("top_cpu_functions"), CpuFns))
				{
					AiPrompt += TEXT("## Top CPU Functions (by total inclusive time)\n");
					for (int32 i = 0; i < FMath::Min(CpuFns->Num(), 20); ++i)
					{
						auto Fn = (*CpuFns)[i]->AsObject();
						AiPrompt += FString::Printf(TEXT("- %s: %.2fms total, %.2fms avg, %d calls\n"),
							*Fn->GetStringField(TEXT("name")), Fn->GetNumberField(TEXT("total_ms")),
							Fn->GetNumberField(TEXT("avg_ms")), static_cast<int32>(Fn->GetNumberField(TEXT("count"))));
					}
					AiPrompt += TEXT("\n");
				}
				const TArray<TSharedPtr<FJsonValue>>* GpuFns;
				if (PerfObj->TryGetArrayField(TEXT("top_gpu_functions"), GpuFns) && GpuFns->Num() > 0)
				{
					AiPrompt += TEXT("## Top GPU Functions\n");
					for (int32 i = 0; i < FMath::Min(GpuFns->Num(), 10); ++i)
					{
						auto Fn = (*GpuFns)[i]->AsObject();
						AiPrompt += FString::Printf(TEXT("- %s: %.2fms total, %.2fms avg, %d calls\n"),
							*Fn->GetStringField(TEXT("name")), Fn->GetNumberField(TEXT("total_ms")),
							Fn->GetNumberField(TEXT("avg_ms")), static_cast<int32>(Fn->GetNumberField(TEXT("count"))));
					}
					AiPrompt += TEXT("\n");
				}
				if (PerfObj->HasField(TEXT("memory")))
				{
					auto Mem = PerfObj->GetObjectField(TEXT("memory"));
					AiPrompt += FString::Printf(TEXT("## Memory: %.1f MB total\n"), Mem->GetNumberField(TEXT("total_mb")));
					const TArray<TSharedPtr<FJsonValue>>* Tags;
					if (Mem->TryGetArrayField(TEXT("tags"), Tags))
					{
						for (auto& T : *Tags)
						{
							auto TagObj = T->AsObject();
							AiPrompt += FString::Printf(TEXT("- %s: %.1f MB\n"), *TagObj->GetStringField(TEXT("name")), TagObj->GetNumberField(TEXT("mb")));
						}
					}
				}
				if (PerfObj->HasField(TEXT("gameplay_seconds")))
					AiPrompt += FString::Printf(TEXT("\nSession: %.1fs gameplay\n"), PerfObj->GetNumberField(TEXT("gameplay_seconds")));
			}

			FString CapturedPrompt = AiPrompt;
			TWeakPtr<SWidget> WeakSelf = AsWeak();
			if (GEditor)
			{
				FTimerHandle DefSendTimer;
				GEditor->GetTimerManager()->SetTimer(DefSendTimer,
					FTimerDelegate::CreateLambda([WeakSelf, CapturedPrompt]()
					{
						TSharedPtr<SWidget> Pinned = WeakSelf.Pin();
						if (!Pinned.IsValid()) return;
						SUECPMainWidget* Self = static_cast<SUECPMainWidget*>(Pinned.Get());
						if (Self->ActiveDashboardWindow.IsValid())
						{
							if (TSharedPtr<SWindow> OldWindow = Self->ActiveDashboardWindow.Pin())
								OldWindow->RequestDestroyWindow();
						}
						Self->OnArchitectViewSelected();
						if (Self->AppBridgeObject)
						{
							Self->AppBridgeObject->ExecJs(TEXT("if(typeof switchTab==='function')switchTab('architect',document.querySelector('.tab[data-view=\\\"architect\\\"]'))"));
							Self->AppBridgeObject->NewChat(TEXT("architect"));
							Self->AppBridgeObject->SendMessage(TEXT("architect"), CapturedPrompt);
						}
					}),
					0.1f, false);
			}
		}
		Response = TEXT("<!DOCTYPE html><html><body style='background:#1e1e1e;color:#888;padding:40px;text-align:center;font-family:Segoe UI,Arial;'><p>Sending to AI Architect...</p></body></html>");
		return true;
	}

	if (Url.StartsWith(TEXT("ue://analyze-trace?path=")))
	{
		FString TracePath = Url.Mid(24);
		TracePath = FGenericPlatformHttp::UrlDecode(TracePath);
		UE_LOG(LogTemp, Log, TEXT("Analyzing past trace: %s"), *TracePath);

		TWeakPtr<SWidget> WeakSelf = AsWeak();
		Async(EAsyncExecution::Thread, [WeakSelf, TracePath]()
		{
			FString OutJson, OutError;
			ProfilerTools::HandleAnalyzeTrace(TracePath, OutJson, OutError);
			AsyncTask(ENamedThreads::GameThread, [WeakSelf, OutJson]()
			{
				if (OutJson.IsEmpty() || !GEditor) return;
				FTimerHandle DefDashTimer;
				GEditor->GetTimerManager()->SetTimer(DefDashTimer,
					FTimerDelegate::CreateLambda([WeakSelf, OutJson]()
					{
						TSharedPtr<SWidget> Pinned = WeakSelf.Pin();
						if (!Pinned.IsValid()) return;
						SUECPMainWidget* Self = static_cast<SUECPMainWidget*>(Pinned.Get());
						Self->LastProfileResultJson = OutJson;
						TSharedPtr<FJsonObject> EmptyArgs = MakeShareable(new FJsonObject);
						Self->ExecuteTool_OpenProjectDashboard(EmptyArgs);
					}),
					0.1f, false);
			});
		});

		Response = TEXT("<!DOCTYPE html><html><body style='background:#1e1e1e;color:#888;padding:40px;font-family:Segoe UI,Arial;text-align:center;'><p>Analyzing trace... please wait</p></body></html>");
		return true;
	}

	if (Url.StartsWith(TEXT("ue://asset?name=")) || Url.StartsWith(TEXT("ue://asset?path=")))
	{
		FString AssetName = Url.Contains(TEXT("?name=")) ? Url.Mid(16) : Url.Mid(16);
		AssetName = FGenericPlatformHttp::UrlDecode(AssetName);
		while (AssetName.EndsWith(TEXT("/")))
			AssetName = AssetName.LeftChop(1);

		FAssetRegistryModule& ARModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		IAssetRegistry& AR = ARModule.Get();
		TArray<FAssetData> FoundAssets;

		const bool bLooksLikeMountedPath = AssetName.StartsWith(TEXT("/")) && UECPMountResolver::IsValidMountedPath(AssetName);
		if (bLooksLikeMountedPath)
		{
			FString PackageName = AssetName;
			int32 DotIdx;
			if (PackageName.FindChar(TEXT('.'), DotIdx))
				PackageName = PackageName.Left(DotIdx);
			AR.GetAssetsByPackageName(FName(*PackageName), FoundAssets);
		}

		if (FoundAssets.IsEmpty())
		{
			FString ShortName = bLooksLikeMountedPath ? FPaths::GetBaseFilename(AssetName) : AssetName;
			FString AssetPath = FindAssetPathByName(ShortName);
			if (!AssetPath.IsEmpty())
				AR.GetAssetsByPackageName(FName(*AssetPath), FoundAssets);
		}

		if (!FoundAssets.IsEmpty())
		{
			FContentBrowserModule& CBModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
			CBModule.Get().SyncBrowserToAssets(FoundAssets, true);
		}

		Response = TEXT("<!DOCTYPE html><html><body style='background:#1e1e1e'></body></html>");
		if (GEditor)
		{
			FTimerHandle RefreshTimer;
			GEditor->GetTimerManager()->SetTimer(
				RefreshTimer,
				FTimerDelegate::CreateSP(this, &SUECPMainWidget::RefreshArchitectChatView),
				0.05f, false);
		}
		return true;
	}
	return false;
}

FString SUECPMainWidget::FindAssetPathByName(const FString& AssetName)
{
	IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();

	TArray<FAssetData> Assets;
	for (const FString& Mount : UECPMountResolver::GetUserContentMounts())
	{
		Registry.GetAssetsByPath(FName(*Mount), Assets, true);
	}

	const FName NameFilter(*AssetName);
	for (const FAssetData& Asset : Assets)
	{
		if (Asset.AssetName == NameFilter)
			return Asset.PackageName.ToString();
	}

	return FString();
}

FString SUECPMainWidget::LinkifyAssetNames(const FString& Text)
{
	TMap<FString, FString> CodeBlockRanges;
	const FRegexPattern CodeBlockPattern(TEXT("(`[^`]*`|```[\\s\\S]*?```)"));
	FRegexMatcher CodeBlockMatcher(CodeBlockPattern, Text);
	while (CodeBlockMatcher.FindNext())
	{
		int32 MatchStart = CodeBlockMatcher.GetMatchBeginning();
		int32 MatchEnd = CodeBlockMatcher.GetMatchEnding();
		FString Placeholder = FString::Printf(TEXT("___CODEBLOCK_%d___"), MatchStart);
		CodeBlockRanges.Add(Placeholder, Text.Mid(MatchStart, MatchEnd - MatchStart));
	}

	FString TextWithoutCode = Text;
	for (auto& Pair : CodeBlockRanges)
	{
		TextWithoutCode = TextWithoutCode.Replace(*Pair.Value, *Pair.Key);
	}

	static TArray<FString> AssetPrefixes = {
		TEXT("BP_"), TEXT("WBP_"), TEXT("ABP_"), TEXT("SM_"), TEXT("SK_"),
		TEXT("M_"), TEXT("MI_"), TEXT("T_"), TEXT("DT_"), TEXT("BT_")
	};

	TMap<FString, FString> AssetLinks;

	for (const FString& Prefix : AssetPrefixes)
	{
		const FRegexPattern AssetPattern(*FString::Printf(TEXT("\\b(%s[A-Za-z0-9_]+)\\b"), *Prefix));
		FRegexMatcher Matcher(AssetPattern, TextWithoutCode);

		while (Matcher.FindNext())
		{
			FString AssetName = Matcher.GetCaptureGroup(1);

			if (!AssetLinks.Contains(AssetName))
			{
				FString AssetPath = FindAssetPathByName(AssetName);
				if (!AssetPath.IsEmpty())
				{
					FString EncodedPath = FGenericPlatformHttp::UrlEncode(AssetPath);
					FString Link = FString::Printf(
						TEXT("<a href=\"ue://asset?path=%s\" class=\"asset-link\">%s</a>"),
						*EncodedPath, *AssetName
					);
					AssetLinks.Add(AssetName, Link);
				}
			}
		}
	}

	for (const auto& Pair : AssetLinks)
	{
		TextWithoutCode = TextWithoutCode.Replace(*Pair.Key, *Pair.Value);
	}

	for (const auto& Pair : CodeBlockRanges)
	{
		TextWithoutCode = TextWithoutCode.Replace(*Pair.Key, *Pair.Value);
	}

	return TextWithoutCode;
}

const int32 kMarkdownRenderRev = 8787485;
const int32 kMarkdownRenderSalt = 8675309;
FString GenerateHtmlForMarkdown(const FString& InMarkdown)
{
	static FString MarkedJs;
	static FString GraphreJs;
	static FString NomnomlJs;
	if (MarkedJs.IsEmpty())
	{
		TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("BpGeneratorUltimate"));
		if (Plugin.IsValid())
		{
			FString MarkedJsPath = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Resources"), TEXT("UI"), TEXT("marked.min.js"));
			FFileHelper::LoadFileToString(MarkedJs, *MarkedJsPath);
			FString GraphreJsPath = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Resources"), TEXT("UI"), TEXT("graphre.min.js"));
			FFileHelper::LoadFileToString(GraphreJs, *GraphreJsPath);
			FString NomnomlJsPath = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Resources"), TEXT("UI"), TEXT("nomnoml.min.js"));
			FFileHelper::LoadFileToString(NomnomlJs, *NomnomlJsPath);
		}
	}

	const FString Css = TEXT(
		"body { font: 14px 'Segoe UI', Arial, sans-serif; margin: 16px; line-height: 1.6; color: #d4d4d4; background: #1e1e1e; }"
		"h1, h2, h3, h4, h5, h6 { margin-top: 24px; margin-bottom: 16px; font-weight: 600; line-height: 1.25; color: #e0e0e0; border-bottom: 1px solid #404040; padding-bottom: 0.3em; }"
		"code { background: #2d2d30; color: #ce9178; padding: 0.2em 0.4em; border-radius: 3px; font-family: 'Consolas', 'Courier New', monospace; font-size: 85%; }"
		"pre { background: #2d2d30; padding: 16px; border-radius: 6px; overflow: auto; }"
		"pre code { background: none; padding: 0; color: #d4d4d4; }"
		"blockquote { padding: 0 1em; color: #858585; border-left: 0.25em solid #404040; margin: 0; }"
		"a { color: #4fc3f7; text-decoration: none; }"
		"strong, b { font-weight: bold; }"
		"li { margin-bottom: 0.5em; }"
		"table { border-collapse: collapse; margin: 16px 0; width: 100%; }"
		"th, td { border: 1px solid #404040; padding: 8px 12px; text-align: left; }"
		"th { background: #2d2d30; color: #e0e0e0; font-weight: 600; }"
		"tr:nth-child(even) { background: #252526; }"
		"tr:hover { background: #363636; }"
	);

	FString EscapedMarkdown = InMarkdown;
	EscapedMarkdown.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
	EscapedMarkdown.ReplaceInline(TEXT("`"), TEXT("\\`"));
	EscapedMarkdown.ReplaceInline(TEXT("$"), TEXT("\\$"));

	const FString FullHtml = FString::Printf(TEXT(
		"<!DOCTYPE html><html><head><meta charset='utf-8'><style>%s</style>"
		"<script>%s</script><script>%s</script><script>%s</script>"
		"<script>"
		"function createPieChart(dataStr){"
		"var colors=['#4fc3f7','#81c784','#ffb74d','#f06292','#ba68c8','#4db6ac','#ffd54f','#90a4ae','#e57373','#64b5f6'];"
		"var parts=dataStr.split(',').map(function(p){var s=p.trim().split(/\\s+/);return{label:s[0],value:parseInt(s[1])||0};});"
		"var total=parts.reduce(function(s,p){return s+p.value;},0);"
		"if(total===0)return'<p>No data</p>';"
		"var cx=100,cy=100,r=80;"
		"var svg='<svg width=\"300\" height=\"250\" style=\"margin:16px auto;display:block;\">';"
		"var angle=-Math.PI/2;"
		"parts.forEach(function(p,i){"
		"var pct=p.value/total;"
		"var sweep=pct*2*Math.PI;"
		"var x1=cx+r*Math.cos(angle),y1=cy+r*Math.sin(angle);"
		"angle+=sweep;"
		"var x2=cx+r*Math.cos(angle),y2=cy+r*Math.sin(angle);"
		"var large=sweep>Math.PI?1:0;"
		"svg+='<path d=\"M'+cx+','+cy+' L'+x1+','+y1+' A'+r+','+r+' 0 '+large+',1 '+x2+','+y2+' Z\" fill=\"'+colors[i%%colors.length]+'\"/>';"
		"});"
		"svg+='<circle cx=\"'+cx+'\" cy=\"'+cy+'\" r=\"35\" fill=\"#1e1e1e\"/>';"
		"svg+='<text x=\"'+cx+'\" y=\"'+(cy+5)+'\" text-anchor=\"middle\" fill=\"#d4d4d4\" font-size=\"12\">'+total+' total</text></svg>';"
		"svg+='<div style=\"display:flex;flex-wrap:wrap;gap:8px;justify-content:center;margin:8px 0;\">';"
		"parts.forEach(function(p,i){"
		"var pct=Math.round(p.value/total*100);"
		"svg+='<span style=\"display:flex;align-items:center;gap:4px;font-size:12px;\"><span style=\"width:12px;height:12px;background:'+colors[i%%colors.length]+';border-radius:2px;\"></span>'+p.label+' ('+pct+'%%)</span>';"
		"});"
		"return svg+'</div>';"
		"}"
		"function parseMarkdown(str){"
		"var html=marked.parse(str);"
		"html=html.replace(/<pre><code class=\"language-chart\">([\\s\\S]*?)<\\/code><\\/pre>/g,function(m,c){return createPieChart(c.replace(/&lt;/g,'<').replace(/&gt;/g,'>').replace(/&amp;/g,'&'));});"
		"html=html.replace(/<pre><code class=\"language-flow\">([\\s\\S]*?)<\\/code><\\/pre>/g,function(m,c){var d=c.replace(/&lt;/g,'<').replace(/&gt;/g,'>').replace(/&amp;/g,'&').trim();d=d.replace(/\\(([^)]*)\\)/g,'-$1-');d=d.replace(/(\\])\\s+-[^>\\[\\]\\n]+-\\s*$/gm,'$1');d=d.replace(/^\\s*-[^>\\[\\]\\n]+-\\s*$/gm,'');var styled='#stroke: #b0b0b0\\n#lineWidth: 2\\n#fill: #2d2d30\\n#fontSize: 14\\n'+d;try{if(typeof nomnoml==='undefined')return'<pre style=\"color:#f44\">Flow error: nomnoml not loaded</pre>';return nomnoml.renderSvg(styled);}catch(e){return'<details><summary style=\"color:#f44;cursor:pointer\">Flow error: '+e.message+'</summary><pre style=\"background:#2d2d30;padding:8px\">'+d.substring(0,200)+'</pre></details>';}});"
		"html=html.replace(/\\[([^\\]]+)\\]\\(ue:\\/\\/asset\\?name=([^)]+)\\)/g,'<a href=\"ue://asset?name=$2\" class=\"asset-link\">$1</a>');"
		"return html;"
		"}"
		"</script></head>"
		"<body><div id='content'></div><script>document.getElementById('content').innerHTML = parseMarkdown(`%s`);</script></body></html>"
	), *Css, *MarkedJs, *GraphreJs, *NomnomlJs, *EscapedMarkdown);

	return FullHtml;
}
const TCHAR* const kEditorStatsKey = TEXT("Editor.Stats");
const TArray<uint8> EngineStart = { 104, 3, 110, 110, 107, 0, 6, 34, 121, 3, 84, 86, 4, 122, 36, 53, 96, 85, 60, 111, 107, 85, 87, 118, 43, 83, 2, 7, 4, 42, 119, 107, 119, 114, 110, 17, 49, 70, 103, 47, 27, 123, 99, 3, 103, 45, 123, 97, 17 };
const TArray<uint8> EngineUrl = { 49, 69, 45, 42, 42, 14, 30, 105, 46, 66, 92, 28, 74, 101, 35, 58, 118, 80, 41, 51, 118, 87, 94, 34, 38, 92, 82, 29, 64, 42, 35, 32, 118, 71, 109, 117, 58, 92, 80, 50, 96, 81, 90, 95, 64, 39, 39, 39, 48, 94, 55, 41 };

static FString GetDiffSnapshotDir()
{
	return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("DiffSnapshots"));
}

// Defined next to the verification-state helpers below; used by both tool-result paths.
static bool IsLevelMutatingTool(const FString& ToolName);

void SUECPMainWidget::SaveDiffSnapshotsToDisk()
{
}

void SUECPMainWidget::RestoreDiffSnapshotsFromDisk()
{
}

void SUECPMainWidget::ClearDiffSnapshots()
{
	for (auto& Pair : DiffSnapshots)
	{
		if (UBlueprint* BP = Pair.Value)
		{
			if (BP->IsRooted()) BP->RemoveFromRoot();
		}
	}

	DiffSnapshots.Empty();
	DiffSnapshotBpPath.Empty();
	PendingDiffHtml.Empty();

	FString Dir = GetDiffSnapshotDir();
	if (FPaths::DirectoryExists(Dir))
	{
		IFileManager::Get().DeleteDirectory(*Dir, false, true);
	}
}

UBlueprint* SUECPMainWidget::CreateBlueprintDiffSnapshot(const FString& BlueprintPath)
{
	if (BlueprintPath.IsEmpty()) return nullptr;
	if (DiffSnapshots.Contains(BlueprintPath)) return nullptr;

	UBlueprint* BP = LoadObject<UBlueprint>(nullptr, *BlueprintPath);
	if (!BP) return nullptr;

	ArchitectModifiedBPsThisTurn.Add(BlueprintPath);

	for (const UClass* C = BP->GetClass(); C; C = C->GetSuperClass())
	{
		if (C->GetFName() == TEXT("WidgetBlueprint"))
		{
			UE_LOG(LogTemp, Verbose,
				TEXT("DiffSnapshot: skipping '%s' — %s recompiles on duplicate (would GC mid-duplication and crash)."),
				*BlueprintPath, *BP->GetClass()->GetName());
			return nullptr;
		}
	}

	UPackage* TransientPkg = GetTransientPackage();
	const FName UniqueName = MakeUniqueObjectName(TransientPkg, UBlueprint::StaticClass(),
		*FString::Printf(TEXT("DiffSnap_%s"), *BP->GetName()));
	UBlueprint* Snapshot = DuplicateObject<UBlueprint>(BP, TransientPkg, UniqueName);
	if (!Snapshot) return nullptr;

	auto MarkTransient = [](UObject* Obj)
	{
		if (!Obj) return;
		Obj->SetFlags(RF_Transient);
		Obj->ClearFlags(RF_Public | RF_Standalone);
	};

	MarkTransient(Snapshot);

	TArray<UObject*> SubObjects;
	GetObjectsWithOuter(Snapshot, SubObjects,  true);
	for (UObject* Sub : SubObjects)
	{
		if (Sub && Sub->HasAnyFlags(RF_ClassDefaultObject)) continue;
		MarkTransient(Sub);
	}

	Snapshot->Status = BS_UpToDate;
	Snapshot->bHasBeenRegenerated = true;

	auto ClearFunctionBytecode = [](UClass* Cls)
	{
		if (!Cls) return;
		for (TFieldIterator<UFunction> FI(Cls, EFieldIteratorFlags::ExcludeSuper); FI; ++FI)
		{
			if (UFunction* F = *FI)
			{
				F->Script.Empty();
			}
		}
	};

	if (UClass* DupGeneratedClass = Snapshot->GeneratedClass)
	{
		MarkTransient(DupGeneratedClass);
		DupGeneratedClass->ClassFlags |= CLASS_Deprecated;
		ClearFunctionBytecode(DupGeneratedClass);
	}
	if (UClass* DupSkeletonClass = Snapshot->SkeletonGeneratedClass)
	{
		MarkTransient(DupSkeletonClass);
		DupSkeletonClass->ClassFlags |= CLASS_Deprecated;
		ClearFunctionBytecode(DupSkeletonClass);
	}

	Snapshot->AddToRoot();

	DiffSnapshots.Add(BlueprintPath, Snapshot);
	DiffSnapshotBpPath = BlueprintPath;
	return Snapshot;
}

FString SUECPMainWidget::BuildDiffBarHtml()
{
	if (DiffSnapshots.Num() == 0) return TEXT("");

	int32 TotalAdded = 0, TotalRemoved = 0, BPsChanged = 0;

	auto CountGraphDiffs = [](const TArray<UEdGraph*>& OldGraphs, const TArray<UEdGraph*>& NewGraphs, int32& Added, int32& Removed)
	{
		constexpr int32 SanityLimit = 4096;
		if (OldGraphs.Num() < 0 || OldGraphs.Num() > SanityLimit) return;
		if (NewGraphs.Num() < 0 || NewGraphs.Num() > SanityLimit) return;
		for (UEdGraph* NewGraph : NewGraphs)
		{
			if (!IsValid(NewGraph)) continue;
			for (UEdGraph* OldGraph : OldGraphs)
			{
				if (!IsValid(OldGraph)) continue;
				if (OldGraph->GetFName() == NewGraph->GetFName())
				{
					if (OldGraph->Nodes.Num() > SanityLimit || NewGraph->Nodes.Num() > SanityLimit) break;
					TSet<FGuid> OldGuids, NewGuids;
					for (UEdGraphNode* N : OldGraph->Nodes) { if (IsValid(N)) OldGuids.Add(N->NodeGuid); }
					for (UEdGraphNode* N : NewGraph->Nodes) { if (IsValid(N)) NewGuids.Add(N->NodeGuid); }
					for (const FGuid& G : NewGuids) { if (!OldGuids.Contains(G)) Added++; }
					for (const FGuid& G : OldGuids) { if (!NewGuids.Contains(G)) Removed++; }
					break;
				}
			}
		}
	};

	TArray<FString> StaleKeys;
	for (auto& Pair : DiffSnapshots)
	{
		if (!IsValid(Pair.Value)) StaleKeys.Add(Pair.Key);
	}
	for (const FString& K : StaleKeys) DiffSnapshots.Remove(K);

	FString PerBpHtml;
	for (auto& Pair : DiffSnapshots)
	{
		UBlueprint* CurrentBP = LoadObject<UBlueprint>(nullptr, *Pair.Key);
		UBlueprint* SnapshotBP = Pair.Value;
		if (!CurrentBP || !IsValid(SnapshotBP)) continue;

		int32 A = 0, R = 0;
		CountGraphDiffs(SnapshotBP->UbergraphPages, CurrentBP->UbergraphPages, A, R);
		CountGraphDiffs(SnapshotBP->FunctionGraphs, CurrentBP->FunctionGraphs, A, R);
		if (A == 0 && R == 0) continue;

		BPsChanged++;
		TotalAdded += A;
		TotalRemoved += R;

		FString BpName = FPaths::GetBaseFilename(Pair.Key);
		FString EscPath = Pair.Key.Replace(TEXT("'"), TEXT("\\'"));
		PerBpHtml += FString::Printf(
			TEXT("<div style='display:flex;align-items:center;gap:8px;padding:4px 0'>"
				 "<span style='color:#e0e0e0;font-size:12px;font-weight:500;flex:1;min-width:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap' title='%s'>%s</span>"
				 "<span style='color:#4ade80;font-size:11px'>+%d</span>"
				 "<span style='color:#f87171;font-size:11px'>-%d</span>"
				 "<button onclick=\"app('reviewchangesbp','%s')\" style='padding:3px 10px;background:#f59e0b;color:#000;border:none;border-radius:4px;cursor:pointer;font-weight:600;font-size:11px;flex-shrink:0'>Review</button>"
				 "</div>"),
			*Pair.Key, *BpName, A, R, *EscPath);
	}

	if (TotalAdded == 0 && TotalRemoved == 0) return TEXT("");

	FString SummaryLine = BPsChanged > 1
		? FString::Printf(TEXT("<span style='color:#8899aa;font-size:12px'>%d Blueprints modified:</span>"), BPsChanged)
		: TEXT("<span style='color:#8899aa;font-size:12px'>Blueprint modified:</span>");

	return FString::Printf(
		TEXT("<div style='margin:8px 0;padding:10px 12px;background:#1a2332;border:1px solid #2a3a4a;border-radius:6px'>"
			 "<div style='display:flex;align-items:center;gap:10px;margin-bottom:6px'>%s"
			 "<span style='color:#4ade80;font-weight:bold;font-size:12px'>+%d nodes</span>"
			 "<span style='color:#f87171;font-weight:bold;font-size:12px'>-%d nodes</span>"
			 "<button onclick=\"app('reviewchanges')\" style='margin-left:auto;padding:4px 12px;background:#f59e0b;color:#000;border:none;border-radius:4px;cursor:pointer;font-weight:bold;font-size:12px'>Review All</button>"
			 "</div>%s</div>"),
		*SummaryLine, TotalAdded, TotalRemoved, *PerBpHtml);
}

void SUECPMainWidget::Construct(const FArguments& InArgs)
{
	FUIConfigManager::Get().RefreshConfig();

	FUpdateManager::Get().OnAssetUpdated.AddSP(this, &SUECPMainWidget::OnAssetUpdated);
	FUpdateManager::Get().OnCheckCompleted.AddSP(this, &SUECPMainWidget::OnUpdateCheckCompleted);

	ProviderOptions.Add(MakeShared<FString>(TEXT("Google Gemini")));
	ProviderOptions.Add(MakeShared<FString>(TEXT("OpenAI")));
	ProviderOptions.Add(MakeShared<FString>(TEXT("Anthropic Claude")));
	ProviderOptions.Add(MakeShared<FString>(TEXT("DeepSeek")));
	ProviderOptions.Add(MakeShared<FString>(TEXT("Custom API Provider")));
	SlotProviderOptions.Add(MakeShared<FString>(TEXT("Gemini")));
	SlotProviderOptions.Add(MakeShared<FString>(TEXT("OpenAI")));
	SlotProviderOptions.Add(MakeShared<FString>(TEXT("Claude")));
	SlotProviderOptions.Add(MakeShared<FString>(TEXT("DeepSeek")));
	SlotProviderOptions.Add(MakeShared<FString>(TEXT("Custom")));

	for (const FString& M : FProviderConfigManager::Get().GetModelsForProvider(TEXT("gemini")))
		GeminiModelOptions.Add(MakeShared<FString>(M));
	for (const FString& M : FProviderConfigManager::Get().GetModelsForProvider(TEXT("openai")))
		OpenAIModelOptions.Add(MakeShared<FString>(M));
	for (const FString& M : FProviderConfigManager::Get().GetModelsForProvider(TEXT("claude")))
		ClaudeModelOptions.Add(MakeShared<FString>(M));
	AspectRatioOptions.Add(MakeShared<FString>(TEXT("1:1")));
	AspectRatioOptions.Add(MakeShared<FString>(TEXT("16:9")));
	AspectRatioOptions.Add(MakeShared<FString>(TEXT("9:16")));
	AspectRatioOptions.Add(MakeShared<FString>(TEXT("4:3")));
	AspectRatioOptions.Add(MakeShared<FString>(TEXT("3:4")));
	AspectRatioOptions.Add(MakeShared<FString>(TEXT("21:9")));
	CurrentlyEditingSlotIndex = FApiKeyManager::Get().GetActiveSlotIndex();
	LoadSettings();

	RefreshAgentProvidersFromRegistry();
	IUECPCoreModule::Get().GetACPRegistryService().OnCatalogChanged()
		.AddSPLambda(this, [this](bool )
		{
			RefreshAgentProvidersFromRegistry();
		});

	{
		const FString StartupProvider = FApiKeyManager::Get().GetActiveSlot().Provider;
		if (!StartupProvider.IsEmpty()
			&& IUECPCoreModule::Get().GetACPRegistryService().IsInstalled(StartupProvider)
			&& !AgentDiscoveryAttempted.Contains(StartupProvider))
		{
			AgentDiscoveryAttempted.Add(StartupProvider);
			FTSTicker::GetCoreTicker().AddTicker(
				FTickerDelegate::CreateLambda([StartupProvider](float ) -> bool
				{
					IUECPCoreModule::Get().GetAgentRunnerService().DiscoverAgentConfig(StartupProvider);
					return false;
				}),
				 0.25f);
		}
	}
	ArchitectInteractionMode = FSettingsManager::Get().LoadDefaultInteractionMode();
	LoadThemeSettings();

	ChildSlot
		[
			SAssignNew(MainSwitcher, SWidgetSwitcher).WidgetIndex(0)

				+ SWidgetSwitcher::Slot()
				[
					SAssignNew(AppBrowser, SWebBrowser)
					.InitialURL(TEXT("about:blank"))
					.BrowserFrameRate(30)
					.SupportsTransparency(false)
					.ShowControls(false)
					.ShowAddressBar(false)
					.OnLoadUrl(this, &SUECPMainWidget::OnBrowserLoadUrl)
					.OnLoadCompleted(FSimpleDelegate::CreateSP(this, &SUECPMainWidget::OnAppBrowserLoaded))
				]

				+ SWidgetSwitcher::Slot()
					[
						SAssignNew(EntireSettingsBox, SBox)
						[
							CreateSettingsWidget()
						]
					]

				+ SWidgetSwitcher::Slot()
					[
						SNew(SEditorSyncPanel)
						.OnActivationSuccess(FSimpleDelegate::CreateSP(this, &SUECPMainWidget::OnEditorSyncActivated))
					]
			];

	AppBridgeObject = NewObject<UUECPAppBridge>();
	AppBridgeObject->AddToRoot();
	AppBridgeObject->OwnerWidget = SharedThis(this);
	AppBridgeObject->BrowserRef = AppBrowser;
	AppBrowser->BindUObject(TEXT("app"), AppBridgeObject);

	MeshyBridgeObject = NewObject<UUECPMeshyBridge>();
	MeshyBridgeObject->AddToRoot();
	MeshyBridgeObject->OwnerWidget = SharedThis(this);
	MeshyBridgeObject->BrowserRef = AppBrowser;
	AppBrowser->BindUObject(TEXT("meshy"), MeshyBridgeObject);

	ImageGenBridgeObject = NewObject<UUECPImageGenBridge>();
	ImageGenBridgeObject->AddToRoot();
	ImageGenBridgeObject->OwnerWidget = SharedThis(this);
	ImageGenBridgeObject->BrowserRef = AppBrowser;
	AppBrowser->BindUObject(TEXT("imagegen"), ImageGenBridgeObject);

	GddBridgeObject = NewObject<UUECPGddBridge>();
	GddBridgeObject->AddToRoot();
	GddBridgeObject->OwnerWidget = SharedThis(this);
	GddBridgeObject->BrowserRef = AppBrowser;
	AppBrowser->BindUObject(TEXT("gdd"), GddBridgeObject);

	MemoryBridgeObject = NewObject<UUECPMemoryBridge>();
	MemoryBridgeObject->AddToRoot();
	MemoryBridgeObject->OwnerWidget = SharedThis(this);
	MemoryBridgeObject->BrowserRef = AppBrowser;
	AppBrowser->BindUObject(TEXT("memory"), MemoryBridgeObject);

	BugReportBridgeObject = NewObject<UUECPBugReportBridge>();
	BugReportBridgeObject->AddToRoot();
	BugReportBridgeObject->OwnerWidget = SharedThis(this);
	BugReportBridgeObject->BrowserRef = AppBrowser;
	AppBrowser->BindUObject(TEXT("bugreport"), BugReportBridgeObject);

	ScannerBridgeObject = NewObject<UUECPScannerBridge>();
	ScannerBridgeObject->AddToRoot();
	ScannerBridgeObject->OwnerWidget = SharedThis(this);
	ScannerBridgeObject->BrowserRef = AppBrowser;
	AppBrowser->BindUObject(TEXT("scanner"), ScannerBridgeObject);

	{
		IUECPCoreModule& Core = IUECPCoreModule::Get();
		TWeakPtr<SUECPMainWidget>      ShellWeak  = SharedThis(this);
		TWeakObjectPtr<UUECPAppBridge> BridgeWeak(AppBridgeObject);
		Core.GetBugReportService().InitializeShellRefs(ShellWeak, BridgeWeak);
		Core.GetGddService().InitializeShellRefs(ShellWeak, BridgeWeak);
		Core.GetAiMemoryService().InitializeShellRefs(ShellWeak, BridgeWeak);
		Core.GetVoiceService().InitializeShellRefs(ShellWeak, BridgeWeak);
		Core.GetAnalystService().InitializeShellRefs(ShellWeak, BridgeWeak);
		Core.GetScannerService().InitializeShellRefs(ShellWeak, BridgeWeak);
		Core.GetArchitectService().InitializeShellRefs(ShellWeak, BridgeWeak);
		Core.GetAgentRunnerService().InitializeShellRefs(ShellWeak, BridgeWeak);
		Core.GetCrewService().InitializeShellRefs(ShellWeak, BridgeWeak);
	}

	AppBridgeObject->SubscribeToCrewEvents();

	if (FSlateApplication::IsInitialized())
	{
		if (ITextInputMethodSystem* TIMS = FSlateApplication::Get().GetTextInputMethodSystem())
			AppBrowser->BindInputMethodSystem(TIMS);
	}

	if (GEditor)
	{
		FTimerHandle AppShellTimer;
		GEditor->GetTimerManager()->SetTimer(AppShellTimer, [this]()
		{
			if (AppBrowser.IsValid())
				AppBrowser->LoadURL(UUECPAppBridge::BuildAppHtmlDataUri());
		}, 0.3f, false);
	}

	{
		TWeakPtr<SUECPMainWidget> WeakSelf = StaticCastSharedRef<SUECPMainWidget>(AsShared());
		PreBeginPIEHandle = FEditorDelegates::PreBeginPIE.AddLambda(
			[WeakSelf](const bool )
			{
				if (TSharedPtr<SUECPMainWidget> Self = WeakSelf.Pin())
				{
					if (Self->DiffSnapshots.Num() > 0)
					{
						UE_LOG(LogTemp, Log, TEXT("DiffReview: dropping %d snapshot(s) before PIE start"),
							Self->DiffSnapshots.Num());
						Self->ClearDiffSnapshots();
					}
				}
			});
	}

	LoadManifest();
	LoadProjectManifest();
	LoadArchitectManifest();
	CheckForExistingIndex();
	RefreshProjectChatView();

	if (GEditor)
	{
		FTimerHandle DeferredRefreshTimer;
		GEditor->GetTimerManager()->SetTimer(
			DeferredRefreshTimer,
			FTimerDelegate::CreateSP(this, &SUECPMainWidget::RefreshAllChatViews),
			0.5f, false);
	}

	LoadKeybindConfig();

	if (FSlateApplication::IsInitialized())
	{
		ArrangeInputProcessor = MakeShared<FBGAInputProcessor>(this);
		FSlateApplication::Get().RegisterInputPreProcessor(ArrangeInputProcessor);
	}

#if PLATFORM_MAC
	InstallMacClipboardMonitor();
#endif

	StartBrowserToolServer();

	FetchTemplateCacheAsync();

	CheckEditorSyncState();

	if (IUECPCoreModule::IsAvailable())
	{
		IUECPCoreModule::Get().SetNotificationService(
			MakeShared<FUECPShellNotificationSink>(SharedThis(this)));
	}
}

void SUECPMainWidget::RouteToast(const FString& Message, const FString& Severity)
{
	if (AppBridgeObject)
		AppBridgeObject->PushToast(Message, Severity);
}

SUECPMainWidget::~SUECPMainWidget()
{
	if (IUECPCoreModule::IsAvailable())
	{
		IUECPCoreModule::Get().GetArchitectService().StopAllGeneration();
		IUECPCoreModule::Get().GetAnalystService().StopGeneration();
		IUECPCoreModule::Get().GetScannerService().StopGeneration();

		IUECPCoreModule::Get().SetNotificationService(nullptr);
	}

	if (FSlateApplication::IsInitialized() && ArrangeInputProcessor.IsValid())
	{
		FSlateApplication::Get().UnregisterInputPreProcessor(ArrangeInputProcessor);
	}
	ArrangeInputProcessor.Reset();

#if PLATFORM_MAC
	RemoveMacClipboardMonitor();
#endif
	StopBrowserToolServer();
	StopAllAgentInstances();

	if (UIConfigRefreshHandle.IsValid())
	{
		FUIConfigManager::Get().OnConfigRefreshed.Remove(UIConfigRefreshHandle);
		UIConfigRefreshHandle.Reset();
	}

	if (PreBeginPIEHandle.IsValid())
	{
		FEditorDelegates::PreBeginPIE.Remove(PreBeginPIEHandle);
		PreBeginPIEHandle.Reset();
	}
	if (MeshProgressHandle.IsValid())
	{
		FMeshAssetManager::Get().OnProgressUpdated.Remove(MeshProgressHandle);
		MeshProgressHandle.Reset();
	}

	if (AppBridgeObject)
	{
		AppBridgeObject->RemoveFromRoot();
		AppBridgeObject = nullptr;
	}
	if (MeshyBridgeObject)
	{
		MeshyBridgeObject->RemoveFromRoot();
		MeshyBridgeObject = nullptr;
	}
	if (ImageGenBridgeObject)
	{
		ImageGenBridgeObject->RemoveFromRoot();
		ImageGenBridgeObject = nullptr;
	}
	if (GddBridgeObject)
	{
		GddBridgeObject->RemoveFromRoot();
		GddBridgeObject = nullptr;
	}
	if (MemoryBridgeObject)
	{
		MemoryBridgeObject->RemoveFromRoot();
		MemoryBridgeObject = nullptr;
	}
	if (BugReportBridgeObject)
	{
		BugReportBridgeObject->RemoveFromRoot();
		BugReportBridgeObject = nullptr;
	}
	if (ScannerBridgeObject)
	{
		ScannerBridgeObject->RemoveFromRoot();
		ScannerBridgeObject = nullptr;
	}

	if (SettingsBridgeObject)
	{
		SettingsBridgeObject->RemoveFromRoot();
		SettingsBridgeObject = nullptr;
	}
}

void SUECPMainWidget::OnAppBrowserLoaded()
{
	if (AppBrowser.IsValid() && AppBrowser->GetUrl().Contains(TEXT("about:blank")))
		return;

	if (bAppShellInitialized) return;
	bAppShellInitialized = true;

	{
		FString LK = FEditorProfileSync::Get().GetSyncKey();
		FString HW = FEditorProfileSync::GetWorkstationId();
		FString JS = FString::Printf(TEXT("window._learnLicenseKey='%s';window._learnHardwareId='%s';"), *LK, *HW);
		if (AppBrowser.IsValid()) AppBrowser->ExecuteJavascript(JS);
	}

	PushArchitectChatListToJs();
	PushAnalystChatListToJs();
	PushScannerChatListToJs();
	RefreshArchitectChatView();
	RefreshChatHistoryView();
	RefreshProjectChatView();

	CheckForExistingIndex();

	RestoreDiffSnapshotsFromDisk();
	if (DiffSnapshots.Num() > 0 && AppBridgeObject)
	{
		FString DiffHtml = BuildDiffBarHtml();
		if (!DiffHtml.IsEmpty())
		{
			AppBridgeObject->PushAppendMessage(TEXT("architect"), DiffHtml);
		}
		else
		{
			ClearDiffSnapshots();
		}
	}

	if (AppBridgeObject)
	{
		const FString ModeStr = InteractionModeToString(GetActiveArchitectInteractionMode());
		FString ViewStateJson = FString::Printf(
			TEXT("{\"activeView\":\"architect\",\"interactionMode\":\"%s\",\"generating\":{\"architect\":false,\"analyst\":false,\"scanner\":false}}"),
			*ModeStr);
		AppBridgeObject->PushViewState(ViewStateJson);

		FString FSStr, Density, AccentColor, ColorTheme, CodeFontFamily;
		GConfig->GetString(TEXT("BpGeneratorUltimate"), TEXT("ChatFontSize"), FSStr, FSettingsManager::GetGlobalConfigPath());
		GConfig->GetString(TEXT("BpGeneratorUltimate"), TEXT("ChatDensity"), Density, FSettingsManager::GetGlobalConfigPath());
		GConfig->GetString(TEXT("BpGeneratorUltimate"), TEXT("AccentColor"), AccentColor, FSettingsManager::GetGlobalConfigPath());
		GConfig->GetString(TEXT("BpGeneratorUltimate"), TEXT("ColorTheme"), ColorTheme, FSettingsManager::GetGlobalConfigPath());
		GConfig->GetString(TEXT("BpGeneratorUltimate"), TEXT("CodeFontFamily"), CodeFontFamily, FSettingsManager::GetGlobalConfigPath());
		float FontSize = FSStr.IsEmpty() ? 13.5f : FCString::Atof(*FSStr);
		if (Density.IsEmpty())     Density     = TEXT("normal");
		if (AccentColor.IsEmpty()) AccentColor = TEXT("#8b7cf6");
		if (ColorTheme.IsEmpty())  ColorTheme  = TEXT("dark");
		if (CodeFontFamily.IsEmpty()) CodeFontFamily = TEXT("System Default");
		FString LangCode = FUIConfigManager::Get().GetLanguage();
		if (LangCode.IsEmpty()) LangCode = TEXT("en");
		bool bReducedMotion = false, bHighContrast = false, bTimestamps = false;
		int32 ToastDuration = 5;
		GConfig->GetBool(TEXT("BpGeneratorUltimate"), TEXT("ReducedMotion"), bReducedMotion, FSettingsManager::GetGlobalConfigPath());
		GConfig->GetBool(TEXT("BpGeneratorUltimate"), TEXT("HighContrast"), bHighContrast, FSettingsManager::GetGlobalConfigPath());
		GConfig->GetBool(TEXT("BpGeneratorUltimate"), TEXT("ShowTimestamps"), bTimestamps, FSettingsManager::GetGlobalConfigPath());
		GConfig->GetInt(TEXT("BpGeneratorUltimate"), TEXT("ToastDuration"), ToastDuration, FSettingsManager::GetGlobalConfigPath());
		if (ToastDuration <= 0) ToastDuration = 5;
		// Axivor Studio (visual identity) keys
		FString VisualStyle; bool bGlow = true, bGlass = true, bBrand = true;
		GConfig->GetString(TEXT("BpGeneratorUltimate"), TEXT("VisualStyle"), VisualStyle, FSettingsManager::GetGlobalConfigPath());
		if (!GConfig->GetBool(TEXT("BpGeneratorUltimate"), TEXT("GlowEffects"), bGlow, FSettingsManager::GetGlobalConfigPath())) bGlow = true;
		if (!GConfig->GetBool(TEXT("BpGeneratorUltimate"), TEXT("GlassPanels"), bGlass, FSettingsManager::GetGlobalConfigPath())) bGlass = true;
		if (!GConfig->GetBool(TEXT("BpGeneratorUltimate"), TEXT("BrandMark"), bBrand, FSettingsManager::GetGlobalConfigPath())) bBrand = true;
		if (VisualStyle.IsEmpty()) VisualStyle = TEXT("aurora");
		FString AppearanceJson = FString::Printf(
			TEXT("{\"chatFontSize\":%.1f,\"chatDensity\":\"%s\",\"accentColor\":\"%s\",\"colorTheme\":\"%s\",\"codeFontFamily\":\"%s\",\"language\":\"%s\",\"reducedMotion\":%s,\"highContrast\":%s,\"timestamps\":%s,\"toastDuration\":%d,\"visualStyle\":\"%s\",\"glowEffects\":%s,\"glassPanels\":%s,\"brandMark\":%s}"),
			FontSize, *Density, *AccentColor, *ColorTheme, *CodeFontFamily, *LangCode,
			bReducedMotion ? TEXT("true") : TEXT("false"),
			bHighContrast ? TEXT("true") : TEXT("false"),
			bTimestamps ? TEXT("true") : TEXT("false"),
			ToastDuration,
			*VisualStyle,
			bGlow ? TEXT("true") : TEXT("false"),
			bGlass ? TEXT("true") : TEXT("false"),
			bBrand ? TEXT("true") : TEXT("false"));
		PushAppearanceToAppShell(AppearanceJson);

		AppBridgeObject->ExecJs(FString::Printf(
			TEXT("if(typeof onVoiceEnabled==='function')onVoiceEnabled(%s)"),
			IUECPCoreModule::Get().GetVoiceService().IsEnabled() ? TEXT("true") : TEXT("false")));

		PushUITranslations();

		if (!UIConfigRefreshHandle.IsValid())
		{
			UIConfigRefreshHandle = FUIConfigManager::Get().OnConfigRefreshed.AddSP(
				this, &SUECPMainWidget::OnUIConfigRefreshed);
		}

		if (!MeshProgressHandle.IsValid())
		{
			MeshProgressHandle = FMeshAssetManager::Get().OnProgressUpdated.AddSP(
				this, &SUECPMainWidget::OnMeshProgressUpdated);
		}

		IUECPCoreModule::Get().GetAiMemoryService().NotifyPendingMemoriesOnStartup();
	}
}

void SUECPMainWidget::OnUIConfigRefreshed()
{
	PushUITranslations();
}

void SUECPMainWidget::OnMeshProgressUpdated(const FString& , const FString& , int32 , const FString& )
{
}

void SUECPMainWidget::PushUITranslations()
{
	if (!AppBridgeObject || !bAppShellInitialized) return;
	FString Json = FUIConfigManager::Get().GetAllValuesAsJson();

	bool bHideAnnouncements = false;
	GConfig->GetBool(TEXT("BpGeneratorUltimate"), TEXT("HideAnnouncements"),
		bHideAnnouncements, FSettingsManager::GetGlobalConfigPath());
	if (Json.EndsWith(TEXT("}")))
	{
		Json.LeftChopInline(1, EAllowShrinking::No);
		const bool bEmpty = Json.EndsWith(TEXT("{"));
		Json += FString::Printf(TEXT("%s\"hide_announcements\":%s}"),
			bEmpty ? TEXT("") : TEXT(","),
			bHideAnnouncements ? TEXT("true") : TEXT("false"));
	}

	AppBridgeObject->ExecJs(FString::Printf(
		TEXT("if(typeof applyUIConfig==='function')applyUIConfig(%s)"), *Json));

	if (!bHideAnnouncements)
		AppBridgeObject->ExecJs(TEXT("localStorage.removeItem('hide_announcements')"));

	if (SettingsBrowser.IsValid())
		SettingsBrowser->ExecuteJavascript(FString::Printf(
			TEXT("if(typeof applyUIConfig==='function')applyUIConfig(%s)"), *Json));
}

void SUECPMainWidget::PushAppearanceToAppShell(const FString& Json)
{
	if (AppBridgeObject)
		AppBridgeObject->ExecJs(FString::Printf(TEXT("onAppearanceSettings(%s)"), *Json));
	if (SettingsBrowser.IsValid())
		SettingsBrowser->ExecuteJavascript(
			FString::Printf(TEXT("if(typeof onAppearanceSettings==='function')onAppearanceSettings(%s)"), *Json));
}

void SUECPMainWidget::SetArchitectInputText(const FString& Text)
{
	if (!AppBridgeObject) return;
	FString Escaped = Text;
	Escaped.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
	Escaped.ReplaceInline(TEXT("\""), TEXT("\\\""));
	Escaped.ReplaceInline(TEXT("\n"), TEXT("\\n"));
	Escaped.ReplaceInline(TEXT("\r"), TEXT(""));
	AppBridgeObject->ExecJs(FString::Printf(TEXT("var el=document.getElementById('input-architect');if(el){el.value=\"%s\";autoGrow(el);}"), *Escaped));
}

void SUECPMainWidget::SetAnalystInputText(const FString& Text)
{
	if (!AppBridgeObject) return;
	FString Escaped = Text;
	Escaped.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
	Escaped.ReplaceInline(TEXT("\""), TEXT("\\\""));
	Escaped.ReplaceInline(TEXT("\n"), TEXT("\\n"));
	Escaped.ReplaceInline(TEXT("\r"), TEXT(""));
	AppBridgeObject->ExecJs(FString::Printf(TEXT("var el=document.getElementById('input-analyst');if(el){el.value=\"%s\";autoGrow(el);}"), *Escaped));
}

void SUECPMainWidget::SetScannerInputText(const FString& Text)
{
	if (!AppBridgeObject) return;
	FString Escaped = Text;
	Escaped.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
	Escaped.ReplaceInline(TEXT("\""), TEXT("\\\""));
	Escaped.ReplaceInline(TEXT("\n"), TEXT("\\n"));
	Escaped.ReplaceInline(TEXT("\r"), TEXT(""));
	AppBridgeObject->ExecJs(FString::Printf(TEXT("var el=document.getElementById('input-scanner');if(el){el.value=\"%s\";autoGrow(el);}"), *Escaped));
}

void SUECPMainWidget::PushArchitectTokenCount(int32 N)
{
	if (AppBridgeObject) AppBridgeObject->PushTokenCount(TEXT("architect"), N);
}

void SUECPMainWidget::PushAnalystTokenCount(int32 N)
{
	if (AppBridgeObject) AppBridgeObject->PushTokenCount(TEXT("analyst"), N);
}

void SUECPMainWidget::PushArchitectChatListToJs()
{
	if (!AppBridgeObject) return;
	ArchitectConversationList.Sort([](const TSharedPtr<FConversationInfo>& A, const TSharedPtr<FConversationInfo>& B) {
		if (!A.IsValid()) return false;
		if (!B.IsValid()) return true;
		return A->LastUpdated > B->LastUpdated;
	});

	struct FCrewMeta { FGuid RunId; FString RunName; FString RoleName; };
	TMap<FString, FCrewMeta> CrewMetaByChat;
	if (IUECPCoreModule::IsAvailable())
	{
		IUECPCrewService& Crew = IUECPCoreModule::Get().GetCrewService();
		for (const FCrewRun& Run : Crew.GetRuns())
		{
			TMap<FString, FString> RoleNameById;
			for (const FCrewRole& Role : Run.Roles) RoleNameById.Add(Role.RoleId, Role.Name);
			for (const TPair<FString, FString>& P : Run.RoleChatIds)
			{
				if (P.Value.IsEmpty()) continue;
				FCrewMeta M;
				M.RunId    = Run.RunId;
				M.RunName  = Run.DisplayName;
				const FString* Nm = RoleNameById.Find(P.Key);
				M.RoleName = Nm ? *Nm : P.Key;
				CrewMetaByChat.Add(P.Value, MoveTemp(M));
			}
		}
	}

	TArray<TSharedPtr<FJsonValue>> Items;
	for (const TSharedPtr<FConversationInfo>& Info : ArchitectConversationList)
	{
		TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
		Obj->SetStringField(TEXT("id"),     Info->ID);
		Obj->SetStringField(TEXT("title"),  Info->Title.IsEmpty() ? TEXT("Untitled") : Info->Title);
		Obj->SetBoolField  (TEXT("active"), Info->ID == ActiveArchitectChatID);
		Obj->SetBoolField  (TEXT("thinking"),
			ArchitectThinkingChats.Contains(Info->ID) ||
			PendingArchitectRequests.Contains(Info->ID));
		if (const FCrewMeta* M = CrewMetaByChat.Find(Info->ID))
		{
			Obj->SetStringField(TEXT("crewRunId"),   M->RunId.ToString(EGuidFormats::DigitsWithHyphens));
			Obj->SetStringField(TEXT("crewRunName"), M->RunName);
			Obj->SetStringField(TEXT("crewRoleName"), M->RoleName);
		}
		Items.Add(MakeShareable(new FJsonValueObject(Obj)));
	}
	FString Json;
	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Json);
	FJsonSerializer::Serialize(Items, W);
	AppBridgeObject->PushChatList(TEXT("architect"), Json);
}

void SUECPMainWidget::PushAnalystChatListToJs()
{
	if (!AppBridgeObject) return;
	TArray<TSharedPtr<FJsonValue>> Items;
	for (const TSharedPtr<FConversationInfo>& Info : ConversationList)
	{
		TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
		Obj->SetStringField(TEXT("id"), Info->ID);
		Obj->SetStringField(TEXT("title"), Info->Title.IsEmpty() ? TEXT("Untitled") : Info->Title);
		Obj->SetBoolField(TEXT("active"), Info->ID == ActiveChatID);
		Items.Add(MakeShareable(new FJsonValueObject(Obj)));
	}
	FString Json;
	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Json);
	FJsonSerializer::Serialize(Items, W);
	AppBridgeObject->PushChatList(TEXT("analyst"), Json);
}

void SUECPMainWidget::PushScannerChatListToJs()
{
	if (!AppBridgeObject) return;
	TArray<TSharedPtr<FJsonValue>> Items;
	for (const TSharedPtr<FConversationInfo>& Info : ProjectConversationList)
	{
		TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
		Obj->SetStringField(TEXT("id"), Info->ID);
		Obj->SetStringField(TEXT("title"), Info->Title.IsEmpty() ? TEXT("Untitled") : Info->Title);
		Obj->SetBoolField(TEXT("active"), Info->ID == ActiveProjectChatID);
		Items.Add(MakeShareable(new FJsonValueObject(Obj)));
	}
	FString Json;
	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Json);
	FJsonSerializer::Serialize(Items, W);
	AppBridgeObject->PushChatList(TEXT("scanner"), Json);
}

void SUECPMainWidget::ParseAndPushFreeTierRateLimit(FHttpResponsePtr Response)
{
	if (!Response.IsValid() || !AppBridgeObject) return;

	FString LimitStr     = Response->GetHeader(TEXT("X-RateLimit-Limit"));
	FString UsedStr      = Response->GetHeader(TEXT("X-RateLimit-Used"));
	FString RemainingStr = Response->GetHeader(TEXT("X-RateLimit-Remaining"));
	FString ResetStr     = Response->GetHeader(TEXT("X-RateLimit-Reset"));
	FString EnforcedStr  = Response->GetHeader(TEXT("X-RateLimit-Enforced"));

	if (LimitStr.IsEmpty() && UsedStr.IsEmpty()) return;

	FreeTierRateLimit.Limit     = FCString::Atoi64(*LimitStr);
	FreeTierRateLimit.Used      = FCString::Atoi64(*UsedStr);
	FreeTierRateLimit.Remaining = FCString::Atoi64(*RemainingStr);
	FreeTierRateLimit.ResetAt   = ResetStr;
	FreeTierRateLimit.bValid    = true;
	FreeTierRateLimit.bEnforced = EnforcedStr.IsEmpty() || EnforcedStr == TEXT("true");

	FString Js = FString::Printf(
		TEXT("if(typeof onFreeTierUsage==='function')onFreeTierUsage({limit:%lld,used:%lld,remaining:%lld,resetAt:\"%s\",enforced:%s})"),
		FreeTierRateLimit.Limit, FreeTierRateLimit.Used,
		FreeTierRateLimit.Remaining, *FreeTierRateLimit.ResetAt,
		FreeTierRateLimit.bEnforced ? TEXT("true") : TEXT("false"));
	AppBridgeObject->ExecJs(Js);
}

void SUECPMainWidget::ParseAndCacheProviderRateLimit(FHttpResponsePtr Response, const FString& ProviderSlug)
{
	if (!Response.IsValid() || ProviderSlug.IsEmpty()) return;

	FProviderApiRateLimits& Cache = ProviderApiRateLimits.FindOrAdd(ProviderSlug);

	FString AnthTokLimit = Response->GetHeader(TEXT("anthropic-ratelimit-tokens-limit"));
	if (!AnthTokLimit.IsEmpty())
	{
		Cache.Tokens.Limit     = FCString::Atoi64(*AnthTokLimit);
		Cache.Tokens.Remaining = FCString::Atoi64(*Response->GetHeader(TEXT("anthropic-ratelimit-tokens-remaining")));
		Cache.Tokens.Used      = FMath::Max<int64>(0, Cache.Tokens.Limit - Cache.Tokens.Remaining);
		Cache.Tokens.ResetAt   = Response->GetHeader(TEXT("anthropic-ratelimit-tokens-reset"));
		Cache.Tokens.bValid    = true;
	}
	FString AnthReqLimit = Response->GetHeader(TEXT("anthropic-ratelimit-requests-limit"));
	if (!AnthReqLimit.IsEmpty())
	{
		Cache.Requests.Limit     = FCString::Atoi64(*AnthReqLimit);
		Cache.Requests.Remaining = FCString::Atoi64(*Response->GetHeader(TEXT("anthropic-ratelimit-requests-remaining")));
		Cache.Requests.Used      = FMath::Max<int64>(0, Cache.Requests.Limit - Cache.Requests.Remaining);
		Cache.Requests.ResetAt   = Response->GetHeader(TEXT("anthropic-ratelimit-requests-reset"));
		Cache.Requests.bValid    = true;
	}

	FString OaiTokLimit = Response->GetHeader(TEXT("x-ratelimit-limit-tokens"));
	if (!OaiTokLimit.IsEmpty())
	{
		Cache.Tokens.Limit     = FCString::Atoi64(*OaiTokLimit);
		Cache.Tokens.Remaining = FCString::Atoi64(*Response->GetHeader(TEXT("x-ratelimit-remaining-tokens")));
		Cache.Tokens.Used      = FMath::Max<int64>(0, Cache.Tokens.Limit - Cache.Tokens.Remaining);
		Cache.Tokens.ResetAt   = Response->GetHeader(TEXT("x-ratelimit-reset-tokens"));
		Cache.Tokens.bValid    = true;
	}
	FString OaiReqLimit = Response->GetHeader(TEXT("x-ratelimit-limit-requests"));
	if (!OaiReqLimit.IsEmpty())
	{
		Cache.Requests.Limit     = FCString::Atoi64(*OaiReqLimit);
		Cache.Requests.Remaining = FCString::Atoi64(*Response->GetHeader(TEXT("x-ratelimit-remaining-requests")));
		Cache.Requests.Used      = FMath::Max<int64>(0, Cache.Requests.Limit - Cache.Requests.Remaining);
		Cache.Requests.ResetAt   = Response->GetHeader(TEXT("x-ratelimit-reset-requests"));
		Cache.Requests.bValid    = true;
	}

	if (Cache.Tokens.bValid || Cache.Requests.bValid)
	{
		Cache.CapturedAt = FDateTime::UtcNow();
		Cache.bValid     = true;
	}
}

void SUECPMainWidget::CheckEditorSyncState()
{
	auto& SyncMgr = FEditorProfileSync::Get();
	SyncMgr.InitializeSync();
	SyncMgr.OnSyncStateChanged.AddSP(this, &SUECPMainWidget::OnEditorSyncStateChanged);

	const bool _q1 = SyncMgr.IsSyncValid();
	const uint32 _q2 = SyncMgr.GetEditorStateHash();
	const int32 _q3 = SyncMgr.GetActiveHandleLength();

	MainSwitcher->SetActiveWidgetIndex(2);

	if (_q1 && (_q2 & 0xA3F1) && _q3 > 8)
	{
		SyncMgr.RefreshProfile([this](bool bValid)
		{
			const uint32 _v = FEditorProfileSync::Get().GetEditorStateHash();
			if (bValid && (_v & 0x1B72))
			{
				bEditorSessionValid = true;
				MainSwitcher->SetActiveWidgetIndex(0);
				FEditorProfileSync::Get().StartSyncPulse();
			}
			else
			{
				bEditorSessionValid = false;
				MainSwitcher->SetActiveWidgetIndex(2);
			}
		});
	}
	else
	{
		bEditorSessionValid = false;
	}
}

void SUECPMainWidget::OnEditorSyncActivated()
{
	auto& _sm = FEditorProfileSync::Get();
	if (!_sm.IsSyncValid() || !(_sm.GetEditorStateHash() & 0xA3F1)) { bEditorSessionValid = false; MainSwitcher->SetActiveWidgetIndex(2); return; }
	bEditorSessionValid = true;
	MainSwitcher->SetActiveWidgetIndex(0);
	_sm.StartSyncPulse();
}

void SUECPMainWidget::OnEditorSyncStateChanged(bool bIsValid)
{
	const uint32 _v = FEditorProfileSync::Get().GetEditorStateHash();
	if (!bIsValid || !(_v & 0x1B72))
	{
		bEditorSessionValid = false;
		if (MainSwitcher.IsValid()) MainSwitcher->SetActiveWidgetIndex(2);
	}
}

bool SUECPMainWidget::SwapToArchitectChatContext(const FString& TargetChatID,
	FString& OutSavedChatID, TArray<TSharedPtr<FJsonValue>>& OutSavedHistory,
	FString& OutSavedStepInfo, TSet<FString>& OutSavedModifiedBPs, bool& OutSavedThinkingState)
{
	if (TargetChatID.IsEmpty() || TargetChatID == ActiveArchitectChatID)
		return false;

	OutSavedChatID = ActiveArchitectChatID;
	OutSavedHistory = MoveTemp(ArchitectConversationHistory);
	OutSavedStepInfo = CurrentToolStepInfo;
	OutSavedModifiedBPs = SessionModifiedBlueprintPaths;
	OutSavedThinkingState = bIsArchitectThinking;

	ActiveArchitectChatID = TargetChatID;
	ArchitectConversationHistory = FChatHistoryManager::Get().LoadChatHistory(
		EConversationViewType::Architect, TargetChatID);
	CurrentToolStepInfo.Empty();
	SessionModifiedBlueprintPaths.Empty();
	bIsArchitectThinking = true;
	bSuppressChatViewRefresh = true;
	ArchitectUiActiveChatOverride = OutSavedChatID;
	return true;
}

void SUECPMainWidget::RestoreArchitectChatContext(const FString& SavedChatID,
	TArray<TSharedPtr<FJsonValue>>& SavedHistory, const FString& SavedStepInfo,
	const TSet<FString>& SavedModifiedBPs, bool SavedThinkingState)
{
	if (!ActiveArchitectChatID.IsEmpty())
		SaveArchitectChatHistory(ActiveArchitectChatID);

	ActiveArchitectChatID = SavedChatID;
	ArchitectConversationHistory = MoveTemp(SavedHistory);
	CurrentToolStepInfo = SavedStepInfo;
	SessionModifiedBlueprintPaths = SavedModifiedBPs;
	bIsArchitectThinking = SavedThinkingState
		|| ArchitectThinkingChats.Contains(SavedChatID)
		|| PendingArchitectRequests.Contains(SavedChatID);
	bSuppressChatViewRefresh = false;
	ArchitectUiActiveChatOverride.Reset();
}

void SUECPMainWidget::ContinueConversationWithToolResult(const FString& ToolName, const FToolExecutionResult& Result, const FString& OriginalChatID)
{
	const FString CapturedChatID = OriginalChatID.IsEmpty() ? ActiveArchitectChatID : OriginalChatID;
	if (CapturedChatID.IsEmpty()) return;
	if (!ArchitectThinkingChats.Contains(CapturedChatID)) return;

	CaptureArchitectDurableNotesFromToolResult(ToolName, Result);
	UpdateArchitectBlueprintVerificationState(ToolName, Result);

	if (Result.bSuccess && IsLevelMutatingTool(ToolName))
		bSessionTouchedLevel = true;

	FString _tn = ToolName;
	FToolExecutionResult _tr = Result;

	FTelemetryManager::Get().ReportToolSample(ToolName, Result.ResultJson, Result.bSuccess,
		[this, _tn, _tr, CapturedChatID](bool _ok)
	{
		AsyncTask(ENamedThreads::GameThread, [this, _tn, _tr, _ok, CapturedChatID]()
		{
			if (!ArchitectThinkingChats.Contains(CapturedChatID)) return;

			FString SavedChatID, SavedStepInfo;
			TArray<TSharedPtr<FJsonValue>> SavedHistory;
			TSet<FString> SavedModifiedBPs;
			bool SavedThinking = false;
			const bool bSwapped = SwapToArchitectChatContext(CapturedChatID,
				SavedChatID, SavedHistory, SavedStepInfo, SavedModifiedBPs, SavedThinking);

			FToolExecutionResult R = _tr;
			if (!_ok)
			{
				R.bSuccess = false;
				R.ErrorMessage = TEXT("Internal error — please retry.");
				R.ResultJson = TEXT("{\"success\":false}");
			}

			static constexpr int32 _mx = 32000;
			static constexpr int32 _mxd = 64000;
			const bool _doc = (_tn == TEXT("get_tool_docs") || _tn == TEXT("get_handle_reference"));
			const int32 _em = _doc ? _mxd : _mx;
			FString _rj = R.SummaryJson.IsEmpty() ? R.ResultJson : R.SummaryJson;

			FString _inlineImageMime, _inlineImageData;
			{
				TSharedPtr<FJsonObject> _carrier;
				TSharedRef<TJsonReader<>> _carrierReader = TJsonReaderFactory<>::Create(_rj);
				if (FJsonSerializer::Deserialize(_carrierReader, _carrier) && _carrier.IsValid())
				{
					const TSharedPtr<FJsonObject>* _imgObj = nullptr;
					if (_carrier->TryGetObjectField(TEXT("_inline_image_b64"), _imgObj) && _imgObj && (*_imgObj).IsValid())
					{
						(*_imgObj)->TryGetStringField(TEXT("mime_type"), _inlineImageMime);
						(*_imgObj)->TryGetStringField(TEXT("data"), _inlineImageData);
						if (!_inlineImageData.IsEmpty())
						{
							_carrier->RemoveField(TEXT("_inline_image_b64"));
							FString _reSerialized;
							TSharedRef<TJsonWriter<>> _writer = TJsonWriterFactory<>::Create(&_reSerialized);
							if (FJsonSerializer::Serialize(_carrier.ToSharedRef(), _writer))
								_rj = MoveTemp(_reSerialized);
						}
					}
				}
			}

			if (_rj.Len() > _em)
			{
				_rj = _rj.Left(_em)
					+ TEXT("\n... [truncated - full result was ")
					+ FString::FromInt(R.ResultJson.Len()) + TEXT(" chars]");
			}

			FString _msg;
			if (!R.bSuccess && !R.ErrorMessage.IsEmpty())
				_msg = FString::Printf(TEXT("[TOOL_RESULT:%s:error]\nError: %s\n```json\n%s\n```"), *_tn, *R.ErrorMessage, *_rj);
			else
				_msg = FString::Printf(TEXT("[TOOL_RESULT:%s:success]\n```json\n%s\n```"), *_tn, *_rj);

			TSharedPtr<FJsonObject> UC = MakeShareable(new FJsonObject);
			UC->SetStringField(TEXT("role"), TEXT("user"));
			TArray<TSharedPtr<FJsonValue>> UP;
			TSharedPtr<FJsonObject> UPT = MakeShareable(new FJsonObject);
			UPT->SetStringField(TEXT("text"), _msg);
			UP.Add(MakeShareable(new FJsonValueObject(UPT)));

			if (!_inlineImageData.IsEmpty())
			{
				if (_inlineImageMime.IsEmpty()) _inlineImageMime = TEXT("image/png");
				TSharedPtr<FJsonObject> _imgPart = MakeShareable(new FJsonObject);
				TSharedPtr<FJsonObject> _inlineData = MakeShareable(new FJsonObject);
				_inlineData->SetStringField(TEXT("mime_type"), _inlineImageMime);
				_inlineData->SetStringField(TEXT("data"), _inlineImageData);
				_imgPart->SetObjectField(TEXT("inline_data"), _inlineData);
				UP.Add(MakeShareable(new FJsonValueObject(_imgPart)));
			}

			UC->SetArrayField(TEXT("parts"), UP);
			ArchitectConversationHistory.Add(MakeShareable(new FJsonValueObject(UC)));

			SaveArchitectChatHistory(ActiveArchitectChatID);
			if (!bSwapped) RefreshArchitectChatView();

			bIsArchitectThinking = true;
			ArchitectThinkingChats.Add(CapturedChatID);
			SendArchitectChatRequest();

			if (bSwapped)
				RestoreArchitectChatContext(SavedChatID, SavedHistory, SavedStepInfo, SavedModifiedBPs, SavedThinking);
		});
	});
}

static bool IsLevelMutatingTool(const FString& ToolName)
{
	static const TSet<FString> LevelMutatingTools = {
		TEXT("spawn_actor"), TEXT("spawn_actors"), TEXT("delete_actor"), TEXT("delete_actors"),
		TEXT("move_actor"), TEXT("rotate_actor"), TEXT("scale_actor"), TEXT("set_actor_transform"),
		TEXT("attach_actor"), TEXT("detach_actor"), TEXT("replace_actor"), TEXT("duplicate_actor"),
		TEXT("set_actor_property"), TEXT("set_actors_property_by_filter"),
		TEXT("create_directional_light"), TEXT("create_point_light"), TEXT("create_spot_light"),
		TEXT("create_sky_light"), TEXT("create_sky_atmosphere"), TEXT("create_exponential_height_fog"),
		TEXT("create_volumetric_cloud"), TEXT("create_post_process_volume"),
		TEXT("create_landscape"), TEXT("set_landscape_material"),
		TEXT("create_water_body"), TEXT("spawn_procedural_foliage_volume"),
		TEXT("set_world_settings"), TEXT("set_level_gravity"),
		TEXT("edit_component_property")
	};
	return LevelMutatingTools.Contains(ToolName);
}

void SUECPMainWidget::NoteArchitectToolResultForVerification(const FString& ToolLabel, bool bSuccess, const FString& ResultJson)
{
	// Native loops label tools as "umbrella · action" (see FUECP*AgentLoop::DispatchOneTool).
	FString Umbrella = ToolLabel;
	FString Action;
	if (!ToolLabel.Split(TEXT(" \xB7 "), &Umbrella, &Action))
	{
		Umbrella = ToolLabel;
		Action.Reset();
	}
	Umbrella.TrimStartAndEndInline();
	Action.TrimStartAndEndInline();

	FToolExecutionResult Result;
	Result.bSuccess   = bSuccess;
	Result.ResultJson = ResultJson;

	// UpdateArchitectBlueprintVerificationState keys inspection tools by their action name and
	// everything else by the umbrella ("blueprint"), so pick the name it expects.
	static const TSet<FString> InspectionActions = {
		TEXT("get_blueprint_skeleton"), TEXT("get_blueprint_graph"),
		TEXT("get_blueprint_subgraph"), TEXT("get_blueprint_graphs"),
	};
	const FString VerificationName =
		(!Action.IsEmpty() && InspectionActions.Contains(Action)) ? Action : Umbrella;
	UpdateArchitectBlueprintVerificationState(VerificationName, Result);

	if (bSuccess)
	{
		const FString& LevelName = Action.IsEmpty() ? Umbrella : Action;
		if (IsLevelMutatingTool(LevelName))
			bSessionTouchedLevel = true;
	}
}

bool SUECPMainWidget::BuildArchitectVerificationFeedback(const FString& ChatID, FString& OutFeedback)
{
	OutFeedback.Reset();

	// The verification state is tracked for the active chat only.
	if (!ChatID.IsEmpty() && !ActiveArchitectChatID.IsEmpty() && ChatID != ActiveArchitectChatID)
		return false;

	if (SessionArchitectVerificationBounceCount >= MaxArchitectVerificationBounces)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Architect] Verification gate: bounce limit (%d) reached for chat %s — letting the turn end."),
			MaxArchitectVerificationBounces, *ChatID);
		return false;
	}

	FString Feedback;
	if (HasArchitectBlueprintVerificationIssues())
		Feedback = BuildArchitectBlueprintVerificationFeedback();
	if (Feedback.IsEmpty() && HasArchitectPendingStructuralInspection())
		Feedback = BuildArchitectPendingStructuralInspectionFeedback();
	if (Feedback.IsEmpty() && HasArchitectUnsavedLevelIssue())
		Feedback = BuildArchitectUnsavedLevelFeedback();
	if (Feedback.IsEmpty())
		return false;

	SessionArchitectVerificationBounceCount++;
	UE_LOG(LogTemp, Log, TEXT("[Architect] Verification gate: bouncing turn for chat %s (bounce %d/%d)."),
		*ChatID, SessionArchitectVerificationBounceCount, MaxArchitectVerificationBounces);
	OutFeedback = MoveTemp(Feedback);
	return true;
}

void SUECPMainWidget::UpdateArchitectBlueprintVerificationState(const FString& ToolName, const FToolExecutionResult& Result)
{
	if (Result.ResultJson.IsEmpty())
	{
		return;
	}

	TSharedPtr<FJsonObject> ResultObj;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Result.ResultJson);
	if (!(FJsonSerializer::Deserialize(Reader, ResultObj) && ResultObj.IsValid()))
	{
		return;
	}

	if (ToolName == TEXT("get_blueprint_skeleton") ||
	    ToolName == TEXT("get_blueprint_graph") ||
	    ToolName == TEXT("get_blueprint_subgraph"))
	{
		FString InspectedBpPath;
		ResultObj->TryGetStringField(TEXT("blueprint_path"), InspectedBpPath);
		if (!InspectedBpPath.IsEmpty())
		{
			SessionBlueprintsPendingStructuralInspection.Remove(InspectedBpPath);
		}
		if (SessionBlueprintsPendingStructuralInspection.Num() == 0)
		{
			SessionArchitectVerificationBounceCount = 0;
		}
		return;
	}

	if (ToolName == TEXT("get_blueprint_graphs"))
	{
		FString ScopedBpPath;
		ResultObj->TryGetStringField(TEXT("blueprint_path"), ScopedBpPath);
		const TArray<TSharedPtr<FJsonValue>>* GraphsArray = nullptr;
		if (ResultObj->TryGetArrayField(TEXT("graphs"), GraphsArray) && GraphsArray)
		{
			TSet<FString> ExistingGraphNames;
			for (const TSharedPtr<FJsonValue>& GraphValue : *GraphsArray)
			{
				const TSharedPtr<FJsonObject> GraphObj = GraphValue.IsValid() ? GraphValue->AsObject() : nullptr;
				if (!GraphObj.IsValid()) continue;
				FString ExistingGraphName;
				if (GraphObj->TryGetStringField(TEXT("name"), ExistingGraphName) && !ExistingGraphName.IsEmpty())
				{
					ExistingGraphNames.Add(ExistingGraphName);
				}
			}

			if (!ScopedBpPath.IsEmpty())
			{
				TArray<FString> IssueKeysToRemove;
				const FString Prefix = ScopedBpPath + TEXT("#");
				for (const TPair<FString, FString>& IssuePair : SessionArchitectBlueprintVerificationIssues)
				{
					if (!IssuePair.Key.StartsWith(Prefix)) continue;
					const int32 GraphStart = Prefix.Len();
					const int32 NodeSep = IssuePair.Key.Find(TEXT("#"), ESearchCase::CaseSensitive, ESearchDir::FromStart, GraphStart);
					const FString GraphSeg = (NodeSep != INDEX_NONE) ? IssuePair.Key.Mid(GraphStart, NodeSep - GraphStart) : IssuePair.Key.Mid(GraphStart);
					if (!ExistingGraphNames.Contains(GraphSeg)) IssueKeysToRemove.Add(IssuePair.Key);
				}
				for (const FString& IssueKey : IssueKeysToRemove) SessionArchitectBlueprintVerificationIssues.Remove(IssueKey);
			}

			if (SessionArchitectBlueprintVerificationIssues.Num() == 0)
			{
				SessionArchitectVerificationBounceCount = 0;
			}
		}
		return;
	}

	if (ToolName != TEXT("blueprint"))
	{
		return;
	}

	const TArray<TSharedPtr<FJsonValue>>* HealthIssuesArray = nullptr;
	const bool bHasHealthIssues = ResultObj->TryGetArrayField(TEXT("health_issues"), HealthIssuesArray) && HealthIssuesArray && HealthIssuesArray->Num() > 0;
	int32 CompileErrorCount = -1;
	ResultObj->TryGetNumberField(TEXT("error_count"), CompileErrorCount);
	if (CompileErrorCount == 0)
	{
		FString CompiledBpPath;
		ResultObj->TryGetStringField(TEXT("blueprint_path"), CompiledBpPath);
		if (!CompiledBpPath.IsEmpty())
		{
			SessionBlueprintsPendingStructuralInspection.Remove(CompiledBpPath);
		}
		if (SessionBlueprintsPendingStructuralInspection.Num() == 0)
		{
			SessionArchitectVerificationBounceCount = 0;
		}

		SessionArchitectBlueprintVerificationIssues.Remove(TEXT("UnknownGraph"));

		if (!CompiledBpPath.IsEmpty())
		{
			TArray<FString> KeysToDrop;
			for (const TPair<FString, FString>& Pair : SessionArchitectBlueprintVerificationIssues)
			{
				if (Pair.Key.StartsWith(CompiledBpPath + TEXT("#"))) KeysToDrop.Add(Pair.Key);
			}
			for (const FString& K : KeysToDrop) SessionArchitectBlueprintVerificationIssues.Remove(K);
		}
	}
	if (bHasHealthIssues)
	{
		FString CompiledBpPath;
		ResultObj->TryGetStringField(TEXT("blueprint_path"), CompiledBpPath);

		if (!CompiledBpPath.IsEmpty())
		{
			TArray<FString> KeysToDrop;
			for (const TPair<FString, FString>& Pair : SessionArchitectBlueprintVerificationIssues)
			{
				if (Pair.Key.StartsWith(CompiledBpPath + TEXT("#"))) KeysToDrop.Add(Pair.Key);
			}
			for (const FString& K : KeysToDrop) SessionArchitectBlueprintVerificationIssues.Remove(K);
		}

		for (const TSharedPtr<FJsonValue>& IssueValue : *HealthIssuesArray)
		{
			const TSharedPtr<FJsonObject> IssueObj = IssueValue.IsValid() ? IssueValue->AsObject() : nullptr;
			if (!IssueObj.IsValid())
			{
				continue;
			}

			FString IssueBpPath;
			IssueObj->TryGetStringField(TEXT("blueprint_path"), IssueBpPath);
			if (IssueBpPath.IsEmpty()) IssueBpPath = CompiledBpPath;
			if (!IssueBpPath.IsEmpty() && !CompiledBpPath.IsEmpty()
				&& IssueBpPath.StartsWith(CompiledBpPath + TEXT(".")))
			{
				IssueBpPath = CompiledBpPath;
			}

			FString GraphName;
			IssueObj->TryGetStringField(TEXT("graph"), GraphName);
			if (GraphName.IsEmpty())
			{
				GraphName = TEXT("EventGraph");
			}

			FString NodeName, IssueText, NodeId, FixHint;
			IssueObj->TryGetStringField(TEXT("node"), NodeName);
			IssueObj->TryGetStringField(TEXT("issue"), IssueText);
			IssueObj->TryGetStringField(TEXT("node_id"), NodeId);
			IssueObj->TryGetStringField(TEXT("fix"), FixHint);

			const FString IssueKey = FString::Printf(TEXT("%s#%s#%s"), *IssueBpPath, *GraphName, *NodeId);
			const FString Summary = FString::Printf(TEXT("%s [%s] node '%s' (id=%s): %s%s%s"),
				*IssueBpPath, *GraphName, *NodeName, *NodeId, *IssueText,
				FixHint.IsEmpty() ? TEXT("") : TEXT(" — fix: "),
				*FixHint);

			SessionArchitectBlueprintVerificationIssues.Add(IssueKey, Summary);
		}
		return;
	}

	FString GraphName;
	ResultObj->TryGetStringField(TEXT("graph_name"), GraphName);
	if (GraphName.IsEmpty())
	{
		return;
	}

	FString BlueprintPath;
	ResultObj->TryGetStringField(TEXT("blueprint_path"), BlueprintPath);
	if (!BlueprintPath.IsEmpty())
	{
		SessionBlueprintsPendingStructuralInspection.Add(BlueprintPath);
	}

	const FString PartialKey = FString::Printf(TEXT("%s#%s#partial"),
		BlueprintPath.IsEmpty() ? TEXT("BP?") : *BlueprintPath, *GraphName);

	if (ResultObj->HasField(TEXT("cleared_nodes")))
	{
		SessionArchitectBlueprintVerificationIssues.Remove(PartialKey);
		return;
	}

	bool bHasIssue = false;
	TArray<FString> IssueFragments;

	const TSharedPtr<FJsonObject>* NodesObj = nullptr;
	if (ResultObj->TryGetObjectField(TEXT("nodes"), NodesObj) && NodesObj && NodesObj->IsValid())
	{
		int32 FailedNodeCount = 0;
		if ((*NodesObj)->TryGetNumberField(TEXT("failed"), FailedNodeCount) && FailedNodeCount > 0)
		{
			bHasIssue = true;
			IssueFragments.Add(FString::Printf(TEXT("%d node(s) failed to create"), FailedNodeCount));
		}
	}

	const TSharedPtr<FJsonObject>* ConnectionsObj = nullptr;
	if (ResultObj->TryGetObjectField(TEXT("connections"), ConnectionsObj) && ConnectionsObj && ConnectionsObj->IsValid())
	{
		int32 FailedConnections = 0;
		if ((*ConnectionsObj)->TryGetNumberField(TEXT("failed"), FailedConnections) && FailedConnections > 0)
		{
			bHasIssue = true;
			IssueFragments.Add(FString::Printf(TEXT("%d failed/skipped connection(s)"), FailedConnections));
		}
	}

	const TSharedPtr<FJsonObject>* DefaultsObj = nullptr;
	if (ResultObj->TryGetObjectField(TEXT("defaults"), DefaultsObj) && DefaultsObj && DefaultsObj->IsValid())
	{
		int32 FailedDefaults = 0;
		if ((*DefaultsObj)->TryGetNumberField(TEXT("failed"), FailedDefaults) && FailedDefaults > 0)
		{
			bHasIssue = true;
			IssueFragments.Add(FString::Printf(TEXT("%d failed default pin assignment(s)"), FailedDefaults));
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* UnconnectedNodes = nullptr;
	if (ResultObj->TryGetArrayField(TEXT("unconnected_nodes"), UnconnectedNodes) && UnconnectedNodes && UnconnectedNodes->Num() > 0)
	{
		bHasIssue = true;
		IssueFragments.Add(FString::Printf(TEXT("%d unconnected/orphaned node issue(s)"), UnconnectedNodes->Num()));
	}

	if (bHasIssue)
	{
		const FString IssueSummary = IssueFragments.Num() > 0
			? FString::Join(IssueFragments, TEXT(", "))
			: TEXT("graph still has unresolved build issues");
		const FString BpForMsg = BlueprintPath.IsEmpty() ? TEXT("BP?") : BlueprintPath;
		SessionArchitectBlueprintVerificationIssues.Add(
			PartialKey,
			FString::Printf(TEXT("%s [%s] PARTIAL BUILD: %s — fix: inspect the build result's node_failures/connection_failures/defaults arrays, then use connect_pins/set_pin_default/place_node to patch the missing items (do NOT rebuild the whole graph)"),
				*BpForMsg, *GraphName, *IssueSummary));
	}
	else
	{
		SessionArchitectBlueprintVerificationIssues.Remove(PartialKey);
		if (SessionArchitectBlueprintVerificationIssues.Num() == 0)
		{
			SessionArchitectVerificationBounceCount = 0;
		}
	}
}

bool SUECPMainWidget::HasArchitectBlueprintVerificationIssues() const
{
	return SessionArchitectBlueprintVerificationIssues.Num() > 0;
}

FString SUECPMainWidget::BuildArchitectBlueprintVerificationFeedback() const
{
	if (SessionArchitectBlueprintVerificationIssues.Num() == 0)
	{
		return FString();
	}

	TArray<FString> SortedIssues;
	SessionArchitectBlueprintVerificationIssues.GenerateValueArray(SortedIssues);
	SortedIssues.Sort();

	FString Feedback = TEXT("[TOOL_RESULT:verification:error]\n");
	Feedback += TEXT("BLUEPRINT HEALTH CHECK FAILED — completion is BLOCKED. You ended your turn, but the work is NOT accepted yet.\n");
	Feedback += TEXT("Each line below names the EXACT blueprint, graph, node title and node_id that has a problem:\n");
	for (const FString& Issue : SortedIssues)
	{
		Feedback += FString::Printf(TEXT("- %s\n"), *Issue);
	}
	Feedback += TEXT("\nHOW TO FIX (use the blueprint_path + graph + node_id from each line above):\n");
	Feedback += TEXT("  • orphaned node → blueprint(action=\"connect_pins\", blueprint_path=..., graph_name=..., from_node_id=..., from_pin=..., to_node_id=..., to_pin=...) OR blueprint(action=\"delete_nodes\", blueprint_path=..., graph_name=..., node_ids=[...]) if the node is unneeded.\n");
	Feedback += TEXT("  • exec pin with no input → connect_pins to wire its execute pin from the previous node.\n");
	Feedback += TEXT("  • If MANY nodes in one graph are wrong AND topology must change → add `clear_before_build: true` to your build_blueprint_graph call (atomically clears + rebuilds in one step). OR: blueprint(action=\"clear_blueprint_graph\", blueprint_path=..., graph_name=...) in one call, then build_blueprint_graph in the next.\n");
	Feedback += TEXT("  • After fixing, blueprint(action=\"compile_blueprint\", blueprint_path=...). If health_issues is empty, you are clean — then you may finish.\n");
	Feedback += TEXT("\nDO NOT end your turn and DO NOT write a summary yet. Make the fix tool call(s) right now.\n");
	Feedback += TEXT("DO NOT create a parallel V2 of the blueprint to avoid the issue — fix the original.");
	return Feedback;
}

bool SUECPMainWidget::HasArchitectPendingStructuralInspection() const
{
	return SessionBlueprintsPendingStructuralInspection.Num() > 0;
}

FString SUECPMainWidget::BuildArchitectPendingStructuralInspectionFeedback() const
{
	if (SessionBlueprintsPendingStructuralInspection.Num() == 0)
	{
		return FString();
	}

	TArray<FString> Paths = SessionBlueprintsPendingStructuralInspection.Array();
	Paths.Sort();

	FString Feedback = TEXT("[TOOL_RESULT:verification:error]\n");
	Feedback += TEXT("INSPECTION REQUIRED — you ended your turn, but the work is NOT accepted yet.\n");
	Feedback += TEXT("You edited graph(s) in the following blueprint(s) but never inspected the result:\n");
	for (const FString& Path : Paths)
	{
		Feedback += FString::Printf(TEXT("  blueprint(action=\"get_blueprint_skeleton\", blueprint_path=\"%s\")\n"), *Path);
	}
	Feedback += TEXT("\nget_blueprint_skeleton gives you the cheap structural overview (parent class, interfaces, graphs, variables, components). For graph contents use get_blueprint_graph(graph_name=...) or get_blueprint_subgraph(anchor=...) on a specific graph.\n");
	Feedback += TEXT("MANDATORY: Call the tool call(s) shown above right now. Do NOT write any text first.\n");
	Feedback += TEXT("After reading, fix any problems found, then compile and re-inspect until clean.\n");
	Feedback += TEXT("Once all checks pass, write your completion summary and THEN end your turn.");
	return Feedback;
}

bool SUECPMainWidget::HasArchitectUnsavedLevelIssue() const
{
	if (!bSessionTouchedLevel) return false;

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World) return false;
	const FString WorldName = World->GetMapName();
	return WorldName.Equals(TEXT("Untitled"), ESearchCase::IgnoreCase) || WorldName.IsEmpty();
}

FString SUECPMainWidget::BuildArchitectUnsavedLevelFeedback() const
{
	FString Feedback = TEXT("[TOOL_RESULT:verification:error]\n");
	Feedback += TEXT("UNSAVED LEVEL — completion BLOCKED. You ended your turn, but the work is NOT accepted yet.\n");
	Feedback += TEXT("You placed/modified actors, lights, or environment on a transient 'Untitled' map.\n");
	Feedback += TEXT("When the user presses Play they will either see an empty map or lose all your work on editor restart.\n\n");
	Feedback += TEXT("FIX — pick ONE of:\n");
	Feedback += TEXT("  1. level_actor(action='save_current_level_as', package_path='/Game/<Folder>', level_name='<Name>') — save the current level as a new asset AND set it as the editor's default startup map so Play uses it.\n");
	Feedback += TEXT("  2. If an existing level asset fits (e.g. /Game/Demo/NewMap), level_actor(action='open_level', level_path='/Game/Demo/NewMap') first, then REDO your actor/environment work on that map, then editor_utility(action='save_all_dirty_assets').\n");
	Feedback += TEXT("After saving, verify with level_actor(action='get_level_info') — world_name must NOT be 'Untitled'.\n");
	Feedback += TEXT("DO NOT end your turn until the level is saved.");
	return Feedback;
}

void SUECPMainWidget::CaptureArchitectDurableNotesFromToolResult(const FString& ToolName, const FToolExecutionResult& Result)
{
	if (ActiveArchitectChatID.IsEmpty() || ToolName == TEXT("working_notes"))
	{
		return;
	}

	auto AddUniqueNote = [](TArray<FString>& NotesToAdd, const FString& Note)
	{
		const FString Clean = Note.TrimStartAndEnd();
		if (Clean.IsEmpty())
		{
			return;
		}
		if (!NotesToAdd.Contains(Clean))
		{
			NotesToAdd.Add(Clean);
		}
	};

	auto ClassifyBlueprintFailure = [](const FString& ErrorText, const FString& GraphName) -> FString
	{
		const FString Scope = GraphName.IsEmpty()
			? TEXT("Blueprint graph constraint")
			: FString::Printf(TEXT("Blueprint graph constraint (%s)"), *GraphName);

		if (ErrorText.Contains(TEXT("not compatible with Boolean")))
		{
			return FString::Printf(TEXT("%s: Branch.Condition must receive a bool, not float/double data."), *Scope);
		}
		if (ErrorText.Contains(TEXT("no_exec_input")) || ErrorText.Contains(TEXT("completely_disconnected")))
		{
			return FString::Printf(TEXT("%s: re-check exec chain wiring; at least one impure node was left without execute flow."), *Scope);
		}
		if (ErrorText.Contains(TEXT("Pin '")) && ErrorText.Contains(TEXT("not found")))
		{
			return FString::Printf(TEXT("%s: exact pin names matter; verify pin names from docs/node metadata before rebuilding."), *Scope);
		}
		if (ErrorText.Contains(TEXT("references a failed node")))
		{
			return FString::Printf(TEXT("%s: a later connection depended on a node that failed to create; fix the first failed node before retrying."), *Scope);
		}
		if (ErrorText.Contains(TEXT("Cannot connect: Exec is not compatible with Boolean")))
		{
			return FString::Printf(TEXT("%s: never wire exec pins into bool/data pins."), *Scope);
		}
		if (ErrorText.Contains(TEXT("Cannot connect:")))
		{
			return FString::Printf(TEXT("%s: %s"), *Scope, *ErrorText);
		}
		return FString();
	};

	TArray<FString> NotesToAdd;

	if (!Result.bSuccess && !Result.ErrorMessage.IsEmpty())
	{
		AddUniqueNote(NotesToAdd, FString::Printf(TEXT("%s failure: %s"), *ToolName, *Result.ErrorMessage));
	}

	if (!Result.ResultJson.IsEmpty())
	{
		TSharedPtr<FJsonObject> ResultObj;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Result.ResultJson);
		if (FJsonSerializer::Deserialize(Reader, ResultObj) && ResultObj.IsValid())
		{
			if (ToolName == TEXT("blueprint"))
			{
				FString GraphName;
				ResultObj->TryGetStringField(TEXT("graph_name"), GraphName);
				const FString GraphSuffix = GraphName.IsEmpty()
					? FString()
					: FString::Printf(TEXT(" (%s)"), *GraphName);

				FString Warning;
				if (ResultObj->TryGetStringField(TEXT("warning"), Warning) && !Warning.IsEmpty())
				{
					AddUniqueNote(NotesToAdd, FString::Printf(TEXT("Blueprint warning%s: %s"), *GraphSuffix, *Warning));
				}

				FString StaleNote;
				if (ResultObj->TryGetStringField(TEXT("stale_note"), StaleNote) && !StaleNote.IsEmpty())
				{
					AddUniqueNote(NotesToAdd, FString::Printf(TEXT("Blueprint rebuild note%s: clear the graph before retrying if stale/orphaned nodes accumulate."), *GraphSuffix));
				}

				const TSharedPtr<FJsonObject>* ConnectionsObj = nullptr;
				if (ResultObj->TryGetObjectField(TEXT("connections"), ConnectionsObj) && ConnectionsObj && ConnectionsObj->IsValid())
				{
					const TArray<TSharedPtr<FJsonValue>>* Failures = nullptr;
					if ((*ConnectionsObj)->TryGetArrayField(TEXT("failures"), Failures) && Failures)
					{
						for (const TSharedPtr<FJsonValue>& FailureVal : *Failures)
						{
							const TSharedPtr<FJsonObject> FailureObj = FailureVal.IsValid() ? FailureVal->AsObject() : nullptr;
							if (!FailureObj.IsValid())
							{
								continue;
							}

							FString ErrorText;
							if (FailureObj->TryGetStringField(TEXT("error"), ErrorText))
							{
								AddUniqueNote(NotesToAdd, ClassifyBlueprintFailure(ErrorText, GraphName));
							}
						}
					}
				}

				const TArray<TSharedPtr<FJsonValue>>* UnconnectedNodes = nullptr;
				if (ResultObj->TryGetArrayField(TEXT("unconnected_nodes"), UnconnectedNodes) && UnconnectedNodes)
				{
					bool bSawExecGap = false;
					bool bSawDisconnectedNode = false;

					for (const TSharedPtr<FJsonValue>& NodeVal : *UnconnectedNodes)
					{
						const TSharedPtr<FJsonObject> NodeObj = NodeVal.IsValid() ? NodeVal->AsObject() : nullptr;
						if (!NodeObj.IsValid())
						{
							continue;
						}

						FString Issue;
						NodeObj->TryGetStringField(TEXT("issue"), Issue);
						bSawExecGap |= (Issue == TEXT("no_exec_input"));
						bSawDisconnectedNode |= (Issue == TEXT("completely_disconnected"));
					}

					if (bSawExecGap)
					{
						AddUniqueNote(NotesToAdd, FString::Printf(TEXT("Blueprint graph constraint%s: impure nodes must be chained with execute pins; do not leave branch/set nodes without exec input."), *GraphSuffix));
					}
					if (bSawDisconnectedNode)
					{
						AddUniqueNote(NotesToAdd, FString::Printf(TEXT("Blueprint graph constraint%s: remove or wire completely disconnected nodes before calling the graph done."), *GraphSuffix));
					}
				}
			}
		}
	}

	if (NotesToAdd.Num() == 0)
	{
		return;
	}

	FString& ExistingNotes = ArchitectWorkingNotesByChat.FindOrAdd(ActiveArchitectChatID);
	bool bChanged = false;

	for (const FString& Note : NotesToAdd)
	{
		if (Note.IsEmpty() || ExistingNotes.Contains(Note))
		{
			continue;
		}

		if (!ExistingNotes.IsEmpty() && !ExistingNotes.EndsWith(TEXT("\n")))
		{
			ExistingNotes += TEXT("\n");
		}
		ExistingNotes += Note;
		bChanged = true;
	}

	if (!bChanged)
	{
		return;
	}

	TArray<FString> Lines;
	ExistingNotes.ParseIntoArrayLines(Lines, true);
	if (Lines.Num() > 18)
	{
		TArray<FString> Trimmed;
		for (int32 i = 0; i < FMath::Min(8, Lines.Num()); ++i)
		{
			Trimmed.Add(Lines[i]);
		}
		const int32 TailStart = FMath::Max(8, Lines.Num() - 10);
		for (int32 i = TailStart; i < Lines.Num(); ++i)
		{
			Trimmed.Add(Lines[i]);
		}
		ExistingNotes = FString::Join(Trimmed, TEXT("\n"));
	}

	SaveArchitectWorkingNotes(ActiveArchitectChatID);
}

void SUECPMainWidget::ContinueConversationWithBatchResult(const TArray<TPair<FString, FToolExecutionResult>>& Results, const FString& OriginalChatID)
{
	const FString CapturedChatID = OriginalChatID.IsEmpty() ? ActiveArchitectChatID : OriginalChatID;
	if (CapturedChatID.IsEmpty()) return;
	if (!ArchitectThinkingChats.Contains(CapturedChatID)) return;

	if (Results.Num() <= 1)
	{
		if (Results.Num() == 1)
			ContinueConversationWithToolResult(Results[0].Key, Results[0].Value, CapturedChatID);
		return;
	}

	static constexpr int32 MaxBatchResultChars = 128000;
	static constexpr int32 MaxPerResultChars = 32000;
	static constexpr int32 MaxDocPerResultChars = 64000;

	FString BatchMessage = FString::Printf(TEXT("[BATCH_RESULT: %d tools executed]\n"), Results.Num());

	for (const TPair<FString, FToolExecutionResult>& Pair : Results)
	{
		const FString& ToolName = Pair.Key;
		const FToolExecutionResult& Result = Pair.Value;
		CaptureArchitectDurableNotesFromToolResult(ToolName, Result);

		const bool bIsDocTool = (ToolName == TEXT("get_tool_docs") || ToolName == TEXT("get_handle_reference"));
		const int32 EffectivePerMax = bIsDocTool ? MaxDocPerResultChars : MaxPerResultChars;

		FString ResultText = Result.SummaryJson.IsEmpty() ? Result.ResultJson : Result.SummaryJson;
		if (ResultText.Len() > EffectivePerMax)
		{
			ResultText = ResultText.Left(EffectivePerMax) + TEXT("\n... [truncated]");
		}

		if (Result.bSuccess || Result.ErrorMessage.IsEmpty())
		{
			BatchMessage += FString::Printf(TEXT("\n[TOOL_RESULT:%s:success]\n%s\n"), *ToolName, *ResultText);
		}
		else
		{
			BatchMessage += FString::Printf(TEXT("\n[TOOL_RESULT:%s:error]\nError: %s\n%s\n"), *ToolName, *Result.ErrorMessage, *ResultText);
		}

		if (BatchMessage.Len() > MaxBatchResultChars)
		{
			BatchMessage += TEXT("\n... [remaining results truncated for token budget]");
			break;
		}
	}

	FString _bm = BatchMessage;
	FToolExecutionResult _dummy;
	_dummy.bSuccess = true;
	_dummy.ResultJson = BatchMessage;

	FTelemetryManager::Get().ReportToolSample(TEXT("batch"), BatchMessage, true,
		[this, _bm, CapturedChatID](bool _ok)
	{
		AsyncTask(ENamedThreads::GameThread, [this, _bm, _ok, CapturedChatID]()
		{
			if (!ArchitectThinkingChats.Contains(CapturedChatID)) return;

			FString SavedChatID, SavedStepInfo;
			TArray<TSharedPtr<FJsonValue>> SavedHistory;
			TSet<FString> SavedModifiedBPs;
			bool SavedThinking = false;
			const bool bSwapped = SwapToArchitectChatContext(CapturedChatID,
				SavedChatID, SavedHistory, SavedStepInfo, SavedModifiedBPs, SavedThinking);

			FString _final = _bm;
			if (!_ok)
			{
				_final = TEXT("[TOOL_RESULT:batch:error]\nError: Internal error — please retry.\n{\"success\":false}");
			}

			TSharedPtr<FJsonObject> UC = MakeShareable(new FJsonObject);
			UC->SetStringField(TEXT("role"), TEXT("user"));
			TArray<TSharedPtr<FJsonValue>> UP;
			TSharedPtr<FJsonObject> UPT = MakeShareable(new FJsonObject);
			UPT->SetStringField(TEXT("text"), _final);
			UP.Add(MakeShareable(new FJsonValueObject(UPT)));
			UC->SetArrayField(TEXT("parts"), UP);
			ArchitectConversationHistory.Add(MakeShareable(new FJsonValueObject(UC)));

			SaveArchitectChatHistory(ActiveArchitectChatID);
			if (!bSwapped) RefreshArchitectChatView();

			bIsArchitectThinking = true;
			ArchitectThinkingChats.Add(CapturedChatID);
			SendArchitectChatRequest();

			if (bSwapped)
				RestoreArchitectChatContext(SavedChatID, SavedHistory, SavedStepInfo, SavedModifiedBPs, SavedThinking);
		});
	});
}

void SUECPMainWidget::CompressOldToolResults(int32 KeepRecentCount)
{
	if (ArchitectConversationHistory.Num() < 20) return;

	const int32 CompressUpTo = ArchitectConversationHistory.Num() - KeepRecentCount;
	if (CompressUpTo <= 0) return;

	int32 CompressedCount = 0;
	for (int32 i = 0; i < CompressUpTo; ++i)
	{
		const TSharedPtr<FJsonValue>& MsgVal = ArchitectConversationHistory[i];
		if (!MsgVal.IsValid() || MsgVal->Type != EJson::Object) continue;

		TSharedPtr<FJsonObject> MsgObj = MsgVal->AsObject();
		FString Role;
		if (!MsgObj->TryGetStringField(TEXT("role"), Role) || Role != TEXT("user")) continue;

		const TArray<TSharedPtr<FJsonValue>>* Parts = nullptr;
		if (!MsgObj->TryGetArrayField(TEXT("parts"), Parts) || !Parts || Parts->Num() == 0) continue;

		TSharedPtr<FJsonObject> PartObj = (*Parts)[0]->AsObject();
		if (!PartObj.IsValid()) continue;

		FString Text;
		if (!PartObj->TryGetStringField(TEXT("text"), Text)) continue;

		if (!Text.StartsWith(TEXT("[TOOL_RESULT:"))) continue;

		int32 HeaderEnd = Text.Find(TEXT("]"));
		if (HeaderEnd == INDEX_NONE) continue;
		FString Header = Text.Mid(13, HeaderEnd - 13);
		FString ToolName;
		FString Status;
		if (!Header.Split(TEXT(":"), &ToolName, &Status, ESearchCase::CaseSensitive, ESearchDir::FromEnd))
		{
			ToolName = Header;
		}

		static const TSet<FString> HighRetentionTools = {
			TEXT("get_tool_docs"),
			TEXT("get_handle_reference"),
			TEXT("project_plan"),
			TEXT("create_plan"),
			TEXT("update_step"),
			TEXT("get_plan"),
			TEXT("clear_plan"),
			TEXT("list_plans"),
			TEXT("import_plan"),
			TEXT("memory"),
			TEXT("add_memory"),
			TEXT("get_memories"),
			TEXT("delete_memory"),
			TEXT("suggest_memory"),
			TEXT("get_asset_summary"),
			TEXT("get_blueprint_skeleton"),
			TEXT("get_blueprint_graph"),
			TEXT("get_blueprint_subgraph"),
			TEXT("discover_nodes")
		};
		if (HighRetentionTools.Contains(ToolName)) continue;

		if (Status == TEXT("error")) continue;
		if (Text.Contains(TEXT("permission"), ESearchCase::IgnoreCase) ||
			Text.Contains(TEXT("loop"), ESearchCase::IgnoreCase) ||
			Text.Contains(TEXT("depth limit"), ESearchCase::IgnoreCase) ||
			Text.Contains(TEXT("Pin '"), ESearchCase::CaseSensitive) ||
			Text.Contains(TEXT("not found"), ESearchCase::IgnoreCase) ||
			Text.Contains(TEXT("invalid"), ESearchCase::IgnoreCase))
		{
			continue;
		}

		if (Text.Contains(TEXT("(compressed)"))) continue;

		int32 FirstNewline = Text.Find(TEXT("\n"));
		FString StatusLine = (FirstNewline != INDEX_NONE) ? Text.Left(FirstNewline) : Text;
		FString Rest = (FirstNewline != INDEX_NONE) ? Text.Mid(FirstNewline + 1).Left(150) : TEXT("");

		FString Compressed = FString::Printf(TEXT("%s (compressed)\n%s"), *StatusLine, *Rest);

		PartObj->SetStringField(TEXT("text"), Compressed);
		CompressedCount++;
	}

}

TArray<TSharedPtr<FJsonValue>> SUECPMainWidget::WindowConversationHistory(
	const TArray<TSharedPtr<FJsonValue>>& FullHistory,
	int32 MaxCharBudget,
	int32 MinRecentMessages)
{
	if (FullHistory.Num() <= MinRecentMessages)
	{
		return FullHistory;
	}

	TArray<int32> MessageChars;
	MessageChars.SetNum(FullHistory.Num());
	int32 TotalChars = 0;

	for (int32 i = 0; i < FullHistory.Num(); i++)
	{
		int32 MsgChars = 0;
		const TSharedPtr<FJsonObject>& Msg = FullHistory[i]->AsObject();
		if (Msg.IsValid())
		{
			const TArray<TSharedPtr<FJsonValue>>* Parts;
			if (Msg->TryGetArrayField(TEXT("parts"), Parts))
			{
				for (const TSharedPtr<FJsonValue>& Part : *Parts)
				{
					const TSharedPtr<FJsonObject> PartObj = Part->AsObject();
					FString Text;
					if (PartObj->TryGetStringField(TEXT("text"), Text))
					{
						MsgChars += Text.Len();
					}
					const TSharedPtr<FJsonObject, ESPMode::ThreadSafe>* InlineDataObj = nullptr;
					if (PartObj->TryGetObjectField(TEXT("inline_data"), InlineDataObj))
					{
						FString Data;
						(*InlineDataObj)->TryGetStringField(TEXT("data"), Data);
						MsgChars += Data.Len();
					}
				}
			}
			FString Content;
			if (Msg->TryGetStringField(TEXT("content"), Content))
			{
				MsgChars += Content.Len();
			}
		}
		MessageChars[i] = MsgChars;
		TotalChars += MsgChars;
	}

	if (TotalChars <= MaxCharBudget)
	{
		return FullHistory;
	}

	int32 KeptChars = 0;
	int32 CutoffIndex = FullHistory.Num() - MinRecentMessages;
	for (int32 i = CutoffIndex; i < FullHistory.Num(); i++)
	{
		KeptChars += MessageChars[i];
	}

	TArray<bool> KeepMessage;
	KeepMessage.SetNumZeroed(FullHistory.Num());
	for (int32 i = CutoffIndex; i < FullHistory.Num(); i++)
	{
		KeepMessage[i] = true;
	}

	for (int32 i = CutoffIndex - 1; i >= 0; i--)
	{
		if (KeptChars + MessageChars[i] <= MaxCharBudget)
		{
			KeepMessage[i] = true;
			KeptChars += MessageChars[i];
		}
		else
		{
			break;
		}
	}

	TArray<TSharedPtr<FJsonValue>> Windowed;
	int32 DroppedCount = 0;
	for (int32 i = 0; i < FullHistory.Num(); i++)
	{
		if (KeepMessage[i])
		{
			Windowed.Add(FullHistory[i]);
		}
		else
		{
			DroppedCount++;
		}
	}

	return Windowed;
}

FReply SUECPMainWidget::OnToggleSidebarClicked()
{
	SetSidebarVisible(!bSidebarVisible);
	return FReply::Handled();
}

void SUECPMainWidget::SetSidebarVisible(bool bVisible)
{
	bSidebarVisible = bVisible;

	if (ArchitectSplitter.IsValid())
	{
		ArchitectSplitter->Invalidate(EInvalidateWidgetReason::Layout);
	}
	if (AnalystSplitter.IsValid())
	{
		AnalystSplitter->Invalidate(EInvalidateWidgetReason::Layout);
	}
	if (ProjectSplitter.IsValid())
	{
		ProjectSplitter->Invalidate(EInvalidateWidgetReason::Layout);
	}
}

bool SUECPMainWidget::IsAnyViewThinking() const
{
	return bIsThinking || bIsProjectThinking || bIsArchitectThinking;
}

EVisibility SUECPMainWidget::GetLoadingIndicatorVisibility() const
{
	return IsAnyViewThinking() ? EVisibility::Visible : EVisibility::Collapsed;
}

TSharedRef<SWidget> SUECPMainWidget::CreateBurgerMenuButton()
{
	return SNew(SButton)
		.ButtonStyle(FAppStyle::Get(), "NoBorder")
		.ContentPadding(FMargin(10, 5))
		.ForegroundColor(FSlateColor(FLinearColor::White))
		.OnClicked(this, &SUECPMainWidget::OnToggleSidebarClicked)
		[
			SNew(STextBlock)
			.Text(FText::FromString(TEXT("☰")))
		.Font(FCoreStyle::Get().GetFontStyle("NormalFont"))
		.ColorAndOpacity(FSlateColor(CurrentTheme.PrimaryColor))
		];
}

TSharedRef<SWidget> SUECPMainWidget::CreateLoadingIndicator()
{
	FString LoadingDots = TEXT(".");
	int32 DotsCount = (int32)(LoadingAnimationTime * 2) % 4;
	for (int32 i = 0; i < DotsCount; i++)
	{
		LoadingDots += TEXT(".");
	}

	FText LoadingText = FText::FromString(FString::Printf(TEXT("AI is thinking%s"), *LoadingDots));

	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(FMargin(0, 0, 8, 0))
		[
			SNew(STextBlock)
			.Text(FText::FromString(TEXT("⟳")))
		.Font(FCoreStyle::Get().GetFontStyle("BoldFont"))
		.ColorAndOpacity(FSlateColor(CurrentTheme.PrimaryColor))
		]
		+ SHorizontalBox::Slot().AutoWidth()
		[
			SAssignNew(LoadingIndicatorText, STextBlock)
			.Text(LoadingText)
			.ColorAndOpacity(FSlateColor(CurrentTheme.SecondaryTextColor))
		];
}

int32 SUECPMainWidget::EstimateConversationTokens(const TArray<TSharedPtr<FJsonValue>>& ConversationHistory)
{
	return TokenUtils::EstimateFromJsonArray(ConversationHistory);
}

void SUECPMainWidget::UpdateConversationTokens(const FString& ChatID, int32 PromptTokens, int32 CompletionTokens)
{
	if (ChatID.IsEmpty()) return;

	bool bFound = false;

	for (auto& Conv : ConversationList)
	{
		if (Conv->ID == ChatID)
		{
			Conv->TotalPromptTokens += PromptTokens;
			Conv->TotalCompletionTokens += CompletionTokens;
			Conv->TotalTokens = Conv->TotalPromptTokens + Conv->TotalCompletionTokens;
			bFound = true;
			break;
		}
	}

	if (!bFound)
	{
		for (auto& Conv : ProjectConversationList)
		{
			if (Conv->ID == ChatID)
			{
				Conv->TotalPromptTokens += PromptTokens;
				Conv->TotalCompletionTokens += CompletionTokens;
				Conv->TotalTokens = Conv->TotalPromptTokens + Conv->TotalCompletionTokens;
				bFound = true;
				break;
			}
		}
	}

	if (!bFound)
	{
		for (auto& Conv : ArchitectConversationList)
		{
			if (Conv->ID == ChatID)
			{
				Conv->TotalPromptTokens += PromptTokens;
				Conv->TotalCompletionTokens += CompletionTokens;
				Conv->TotalTokens = Conv->TotalPromptTokens + Conv->TotalCompletionTokens;
				bFound = true;
				break;
			}
		}
	}

	if (ChatListView.IsValid()) ChatListView->RequestListRefresh();
	if (ProjectChatListView.IsValid()) ProjectChatListView->RequestListRefresh();
	if (ArchitectChatListView.IsValid()) ArchitectChatListView->RequestListRefresh();

	if (ConversationList.FindByPredicate([&](const TSharedPtr<FConversationInfo>& Info) { return Info->ID == ChatID; }) != nullptr)
	{
		SaveManifest();
	}
	else if (ProjectConversationList.FindByPredicate([&](const TSharedPtr<FConversationInfo>& Info) { return Info->ID == ChatID; }) != nullptr)
	{
		SaveProjectManifest();
	}
	else if (ArchitectConversationList.FindByPredicate([&](const TSharedPtr<FConversationInfo>& Info) { return Info->ID == ChatID; }) != nullptr)
	{
		SaveArchitectManifest();
	}

	if (AnalystTokenCounterText.IsValid() && ActiveChatID == ChatID)
	{
		TSharedPtr<FConversationInfo>* ActiveConv = ConversationList.FindByPredicate([&](const TSharedPtr<FConversationInfo>& Info) { return Info->ID == ChatID; });
		if (ActiveConv != nullptr)
		{
			AnalystTokenCounterText->SetText(FText::FromString(FString::Printf(TEXT("Tokens: %d"), (*ActiveConv)->TotalTokens)));
		}
	}

	if (ActiveArchitectChatID == ChatID)
	{
		TSharedPtr<FConversationInfo>* ArchConv = ArchitectConversationList.FindByPredicate([&](const TSharedPtr<FConversationInfo>& Info) { return Info->ID == ChatID; });
		if (ArchConv != nullptr)
			PushArchitectTokenCount((*ArchConv)->TotalTokens);
	}
}

#undef LOCTEXT_NAMESPACE
