// Copyright 2026, BlueprintsLab, All rights reserved.

#include "SUECPMainWidget.h"
#include "Widget/UUECPSettingsBridge.h"
#include "Widget/UUECPAppBridge.h"
#include "UECPCoreModule.h"
#include "Services/IUECPACPRegistryService.h"
#include "Services/IUECPAgentRunnerService.h"
#include "Services/IUECPMcpInfoService.h"
#include "Services/IUECPExtensionService.h"
#include "SWebBrowser.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Async/Async.h"
#include "DesktopPlatformModule.h"
#include "IDesktopPlatform.h"
#include "HAL/FileManager.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "Interfaces/IPluginManager.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Managers/ThemeManager.h"
#include "UIConfigManager.h"
#include "Managers/UpdateManager.h"

#define LOCTEXT_NAMESPACE "SUECPMainWidget"

TSharedRef<SWidget> SUECPMainWidget::CreateSettingsWidget()
{
	SettingsBridgeObject = NewObject<UUECPSettingsBridge>();
	SettingsBridgeObject->OwnerWidget = SharedThis(this);
	SettingsBridgeObject->AddToRoot();

	IUECPCoreModule::Get().GetACPRegistryService().OnCatalogChanged()
		.AddUObject(SettingsBridgeObject, &UUECPSettingsBridge::OnRegistryCatalogChanged);

	IUECPCoreModule::Get().GetMcpInfoService().OnBrokerStateChanged()
		.AddUObject(SettingsBridgeObject, &UUECPSettingsBridge::OnMcpBrokerStateChanged);

	IUECPCoreModule::Get().GetExtensionService().OnExtensionStateChanged()
		.AddUObject(SettingsBridgeObject, &UUECPSettingsBridge::OnExtensionStateChangedForUserMcp);

	SAssignNew(SettingsBrowser, SWebBrowser)
		.InitialURL(TEXT("about:blank"))
		.ShowControls(false)
		.BrowserFrameRate(24)
		.OnLoadCompleted(FSimpleDelegate::CreateSP(
			this, &SUECPMainWidget::OnSettingsBrowserLoaded));

	SettingsBridgeObject->BrowserRef = SettingsBrowser;
	SettingsBrowser->BindUObject(TEXT("bridge"), SettingsBridgeObject, true);
	SettingsBrowser->LoadURL(UUECPSettingsBridge::BuildSettingsHtmlDataUri());

	return SNew(SBox).HAlign(HAlign_Fill).VAlign(VAlign_Fill)
		[ SettingsBrowser.ToSharedRef() ];
}

void SUECPMainWidget::OnSettingsBrowserLoaded()
{
	if (!SettingsBrowser.IsValid() || !SettingsBridgeObject) return;
	FString Json = SettingsBridgeObject->LoadAllSettings();
	SettingsBrowser->ExecuteJavascript(TEXT("if(typeof initSettings==='function')initSettings(") + Json + TEXT(")"));
	SettingsBrowser->ExecuteJavascript(
		TEXT("if(typeof onAppearanceSettings==='function'&&typeof D!=='undefined'&&D.appearance)onAppearanceSettings(D.appearance)"));
	FString UIJson = FUIConfigManager::Get().GetAllValuesAsJson();
	SettingsBrowser->ExecuteJavascript(FString::Printf(
		TEXT("if(typeof applyUIConfig==='function')applyUIConfig(%s)"), *UIJson));
	FString PluginInfoJson = SettingsBridgeObject->RequestPluginInfo();
	SettingsBrowser->ExecuteJavascript(FString::Printf(
		TEXT("if(typeof onPluginInfo==='function')onPluginInfo(%s)"), *PluginInfoJson));
}

FReply SUECPMainWidget::OnShowSettingsClicked()
{
	if (MainSwitcher.IsValid())
		MainSwitcher->SetActiveWidgetIndex(1);

	if (SettingsBridgeObject && SettingsBrowser.IsValid())
	{
		FString Json = SettingsBridgeObject->LoadAllSettings();
		SettingsBrowser->ExecuteJavascript(TEXT("if(typeof initSettings==='function')initSettings(") + Json + TEXT(")"));
		FString PluginInfoJson = SettingsBridgeObject->RequestPluginInfo();
		SettingsBrowser->ExecuteJavascript(FString::Printf(
			TEXT("if(typeof onPluginInfo==='function')onPluginInfo(%s)"), *PluginInfoJson));
	}

	RefreshCliStatusAsync();

	FSlateApplication::Get().SetKeyboardFocus(SharedThis(this), EFocusCause::SetDirectly);

	return FReply::Handled();
}

FReply SUECPMainWidget::OnSaveSettingsClicked()
{
	if (MainSwitcher.IsValid())
		MainSwitcher->SetActiveWidgetIndex(0);
	return FReply::Handled();
}

void SUECPMainWidget::LoadSettings()
{
	CurrentSettings = FSettingsManager::Get().LoadSettings();
	CustomInstructions = FSettingsManager::Get().LoadCustomInstructions();
}

void SUECPMainWidget::SaveSettings()
{
	FSettingsManager::Get().SaveSettings(CurrentSettings);
}

FReply SUECPMainWidget::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Escape && MainSwitcher.IsValid() && MainSwitcher->GetActiveWidgetIndex() == 1)
	{
		OnSaveSettingsClicked();
		return FReply::Handled();
	}

	if (HandleGlobalKeyPress(InKeyEvent))
		return FReply::Handled();
	return FReply::Unhandled();
}

void SUECPMainWidget::LoadKeybindConfig()
{
	FString ModStr, KeyStr;
	if (GConfig->GetString(TEXT("BpGeneratorUltimate"), TEXT("ArrangeNodesModifier"), ModStr, FSettingsManager::GetGlobalConfigPath()))
	{
		if (ModStr == TEXT("None"))
			ArrangeNodesKeybind.ModifierKey = EKeys::Invalid;
		else
			ArrangeNodesKeybind.ModifierKey = FKey(FName(*ModStr));
	}
	if (GConfig->GetString(TEXT("BpGeneratorUltimate"), TEXT("ArrangeNodesKey"), KeyStr, FSettingsManager::GetGlobalConfigPath()))
	{
		ArrangeNodesKeybind.ActionKey = FKey(FName(*KeyStr));
	}

	VoicePTTKeybind.ModifierKey = EKeys::LeftAlt;
	VoicePTTKeybind.ActionKey = EKeys::V;
	FString VModStr, VKeyStr;
	if (GConfig->GetString(TEXT("BpGeneratorUltimate"), TEXT("VoicePTTModifier"), VModStr, FSettingsManager::GetGlobalConfigPath()))
	{
		VoicePTTKeybind.ModifierKey = (VModStr == TEXT("None")) ? EKeys::Invalid : FKey(FName(*VModStr));
	}
	if (GConfig->GetString(TEXT("BpGeneratorUltimate"), TEXT("VoicePTTKey"), VKeyStr, FSettingsManager::GetGlobalConfigPath()))
	{
		VoicePTTKeybind.ActionKey = FKey(FName(*VKeyStr));
	}
}

void SUECPMainWidget::SaveKeybindConfig()
{
	FString ModStr = (ArrangeNodesKeybind.ModifierKey == EKeys::Invalid)
		? TEXT("None")
		: ArrangeNodesKeybind.ModifierKey.GetFName().ToString();
	FString KeyStr = ArrangeNodesKeybind.ActionKey.GetFName().ToString();
	GConfig->SetString(TEXT("BpGeneratorUltimate"), TEXT("ArrangeNodesModifier"), *ModStr, FSettingsManager::GetGlobalConfigPath());
	GConfig->SetString(TEXT("BpGeneratorUltimate"), TEXT("ArrangeNodesKey"), *KeyStr, FSettingsManager::GetGlobalConfigPath());

	FString VModStr = (VoicePTTKeybind.ModifierKey == EKeys::Invalid)
		? TEXT("None")
		: VoicePTTKeybind.ModifierKey.GetFName().ToString();
	FString VKeyStr = VoicePTTKeybind.ActionKey.GetFName().ToString();
	GConfig->SetString(TEXT("BpGeneratorUltimate"), TEXT("VoicePTTModifier"), *VModStr, FSettingsManager::GetGlobalConfigPath());
	GConfig->SetString(TEXT("BpGeneratorUltimate"), TEXT("VoicePTTKey"), *VKeyStr, FSettingsManager::GetGlobalConfigPath());

	GConfig->Flush(false, FSettingsManager::GetGlobalConfigPath());
}

void SUECPMainWidget::LoadThemeSettings()
{
	CurrentTheme = FThemeManager::Get().LoadTheme();
}

void SUECPMainWidget::SaveThemeSettings()
{
	FThemeManager::Get().SaveTheme(CurrentTheme);
}

void SUECPMainWidget::ApplyTheme()
{
}

TSharedPtr<SBorder> SUECPMainWidget::CreateThemedBorder(TSharedPtr<SWidget> Content, const FMargin& Padding)
{
	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.BorderBackgroundColor(CurrentTheme.BorderBackgroundColor)
		.Padding(Padding)
		[
			Content.IsValid() ? Content.ToSharedRef() : SNew(SBox)
		];
}

TSharedPtr<SButton> SUECPMainWidget::CreateThemedButton(FText Text, FOnClicked OnClicked, const FLinearColor& OverrideColor)
{
	FLinearColor ButtonColor = OverrideColor.A > 0.01f ? OverrideColor : CurrentTheme.PrimaryColor;
	return SNew(SButton)
		.Text(Text)
		.OnClicked(OnClicked)
		.ButtonColorAndOpacity(FSlateColor(ButtonColor))
		.ForegroundColor(FSlateColor(FLinearColor::White));
}

TSharedPtr<STextBlock> SUECPMainWidget::CreateThemedTextBlock(FText Text, bool bIsPrimary)
{
	return SNew(STextBlock)
		.Text(Text)
		.ColorAndOpacity(FSlateColor(bIsPrimary ? CurrentTheme.PrimaryTextColor : CurrentTheme.SecondaryTextColor));
}

static FString CheckRuntimeVersion(const FString& WindowsExe, const TArray<FString>& UnixCandidates)
{
	auto ExtractVersion = [](FString Raw) -> FString
	{
		Raw = Raw.TrimStartAndEnd();
		for (const FString& Prefix : TArray<FString>{ TEXT("Python "), TEXT("python "), TEXT("pip "), TEXT("v") })
			Raw.RemoveFromStart(Prefix);
		int32 Idx;
		if (Raw.FindChar(TEXT(' '), Idx)) Raw = Raw.Left(Idx);
		if (Raw.FindChar(TEXT(','), Idx)) Raw = Raw.Left(Idx);
		return Raw.TrimStartAndEnd();
	};

#if PLATFORM_WINDOWS
	auto TryWindows = [&](const FString& Cmd) -> FString
	{
		void* PipeRead = nullptr, *PipeWrite = nullptr;
		FPlatformProcess::CreatePipe(PipeRead, PipeWrite);

		FString Args = FString::Printf(TEXT("/c %s 2>&1"), *Cmd);
		FProcHandle Proc = FPlatformProcess::CreateProc(
			TEXT("cmd.exe"), *Args,
			false,
			true,
			true,
			nullptr, 0, nullptr,
			PipeWrite, nullptr);

		if (!Proc.IsValid())
		{
			FPlatformProcess::ClosePipe(PipeRead, PipeWrite);
			return TEXT("");
		}

		FString Out;
		double Start = FPlatformTime::Seconds();
		while (FPlatformProcess::IsProcRunning(Proc) && FPlatformTime::Seconds() - Start < 5.0)
		{
			Out += FPlatformProcess::ReadPipe(PipeRead);
			FPlatformProcess::Sleep(0.05f);
		}
		if (FPlatformProcess::IsProcRunning(Proc))
			FPlatformProcess::TerminateProc(Proc);
		Out += FPlatformProcess::ReadPipe(PipeRead);

		int32 Code = -1;
		FPlatformProcess::GetProcReturnCode(Proc, &Code);
		FPlatformProcess::CloseProc(Proc);
		FPlatformProcess::ClosePipe(PipeRead, PipeWrite);

		if (Code == 0)
		{
			FString V = Out.TrimStartAndEnd();
			if (!V.IsEmpty()) return ExtractVersion(V);
		}
		return TEXT("");
	};

	const bool bIsPython = WindowsExe.Equals(TEXT("python"), ESearchCase::IgnoreCase);
	if (bIsPython)
	{
		FString LauncherVer = TryWindows(TEXT("py -3 --version"));
		if (!LauncherVer.IsEmpty()) return LauncherVer;
	}

	FString Ver = TryWindows(WindowsExe + TEXT(" --version"));
	if (Ver.IsEmpty() && !WindowsExe.EndsWith(TEXT("3")))
		Ver = TryWindows(WindowsExe + TEXT("3 --version"));
	return Ver;
#else
	for (const FString& Candidate : UnixCandidates)
	{
		if (!FPaths::FileExists(Candidate)) continue;
		FString Out, Err;
		int32 Code = -1;
		FString ShellArgs = FString::Printf(TEXT("-l -c \"%s --version\""), *Candidate);
		FPlatformProcess::ExecProcess(TEXT("/bin/bash"), *ShellArgs, &Code, &Out, &Err, nullptr, true);
		if (Code == 0)
		{
			FString V = Out.TrimStartAndEnd();
			if (V.IsEmpty()) V = Err.TrimStartAndEnd();
			if (!V.IsEmpty()) return ExtractVersion(V);
		}
	}
	return TEXT("");
#endif
}

void SUECPMainWidget::RefreshCliStatusAsync()
{
	TWeakPtr<SUECPMainWidget> WeakSelf = SharedThis(this);
	Async(EAsyncExecution::Thread, [WeakSelf]()
	{
		bool bClaude = CheckCliInPath(TEXT("claude"));
		bool bCodex  = CheckCliInPath(TEXT("codex"));
		bool bCopilot = CheckCliInPath(TEXT("copilot"));
		bool bGemini = CheckCliInPath(TEXT("gemini"));

#if !PLATFORM_WINDOWS
		FString Home = FPlatformMisc::GetEnvironmentVariable(TEXT("HOME"));
		if (Home.IsEmpty()) Home = FString(TEXT("/Users/")) + FPlatformProcess::UserName(false);

		TArray<FString> NvmNodePaths, NvmNpmPaths;
		{
			FString NvmBase = Home / TEXT(".nvm/versions/node");
			IFileManager& FM = IFileManager::Get();
			if (FM.DirectoryExists(*NvmBase))
			{
				TArray<FString> Versions;
				FM.FindFiles(Versions, *(NvmBase / TEXT("*")), false, true);
				for (const FString& V : Versions)
				{
					NvmNodePaths.Add(NvmBase / V / TEXT("bin/node"));
					NvmNpmPaths.Add(NvmBase / V / TEXT("bin/npm"));
				}
			}
		}

		TArray<FString> NodeCandidates = {
			TEXT("/opt/homebrew/bin/node"), TEXT("/usr/local/bin/node"),
			TEXT("/usr/bin/node"),
			Home / TEXT(".local/bin/node"),  Home / TEXT(".volta/bin/node"),
			Home / TEXT(".fnm/aliases/default/bin/node"), Home / TEXT(".asdf/shims/node"),
		};
		NodeCandidates.Append(NvmNodePaths);
		TArray<FString> NpmCandidates = {
			TEXT("/opt/homebrew/bin/npm"), TEXT("/usr/local/bin/npm"),
			TEXT("/usr/bin/npm"),
			Home / TEXT(".local/bin/npm"),  Home / TEXT(".volta/bin/npm"),
			Home / TEXT(".fnm/aliases/default/bin/npm"), Home / TEXT(".asdf/shims/npm"),
		};
		NpmCandidates.Append(NvmNpmPaths);
#else
		TArray<FString> NodeCandidates, NpmCandidates;
#endif
		FString NodeVer   = CheckRuntimeVersion(TEXT("node"),   NodeCandidates);
		FString NpmVer    = CheckRuntimeVersion(TEXT("npm"),    NpmCandidates);

		bool bClaudeAuth = false;
		if (bClaude)
		{
#if PLATFORM_WINDOWS
			FString UserProfile = FPlatformMisc::GetEnvironmentVariable(TEXT("USERPROFILE"));
			if (UserProfile.IsEmpty())
				UserProfile = FPlatformMisc::GetEnvironmentVariable(TEXT("HOMEDRIVE")) + FPlatformMisc::GetEnvironmentVariable(TEXT("HOMEPATH"));
			FString CredsPath = UserProfile / TEXT(".claude") / TEXT(".credentials.json");
#else
			FString CredsPath = Home / TEXT(".claude") / TEXT(".credentials.json");
#endif
			bClaudeAuth = IFileManager::Get().FileExists(*CredsPath);
		}

		bool bGhAuthed = false;
		{
			void* GhPipeRead = nullptr;
			void* GhPipeWrite = nullptr;
			FPlatformProcess::CreatePipe(GhPipeRead, GhPipeWrite);

			FProcHandle GhProc = FPlatformProcess::CreateProc(
				TEXT("gh"), TEXT("auth status"), false, true, true, nullptr, 0, nullptr, GhPipeWrite, nullptr);

			if (GhProc.IsValid())
			{
				double GhStart = FPlatformTime::Seconds();
				while (FPlatformProcess::IsProcRunning(GhProc) && FPlatformTime::Seconds() - GhStart < 5.0)
					FPlatformProcess::Sleep(0.05f);

				int32 ReturnCode = -1;
				FPlatformProcess::GetProcReturnCode(GhProc, &ReturnCode);
				bGhAuthed = (ReturnCode == 0);
				FPlatformProcess::CloseProc(GhProc);
			}
			FPlatformProcess::ClosePipe(GhPipeRead, GhPipeWrite);
		}

		AsyncTask(ENamedThreads::GameThread, [WeakSelf, bClaude, bClaudeAuth, bCodex, bCopilot, bGemini, bGhAuthed,
			NodeVer, NpmVer]()
		{
			auto This = WeakSelf.Pin();
			if (!This.IsValid()) return;
			This->bClaudeCliFound  = bClaude;
			This->bCodexCliFound   = bCodex;
			This->bCopilotCliFound = bCopilot;
			This->bGeminiCliFound  = bGemini;
			This->bGitHubAuthed    = bGhAuthed;
			This->NodeVersion   = NodeVer;
			This->NpmVersion    = NpmVer;

			if (This->SettingsBridgeObject)
				This->SettingsBridgeObject->PushCliStatusToJs(bClaude, bClaudeAuth, bCodex, bCopilot, bGemini, bGhAuthed,
					NodeVer, NpmVer);
		});
	});
}

void SUECPMainWidget::InstallCliAsync(const FString& ProviderName, const FString& Command, const FString& Args,
	const FString& FollowUpCommand, const FString& FollowUpArgs)
{
	if (bCliInstallInProgress) return;

	bCliInstallInProgress = true;

	TWeakPtr<SUECPMainWidget> WeakSelf = SharedThis(this);

	Async(EAsyncExecution::Thread, [WeakSelf, ProviderName, Command, Args, FollowUpCommand, FollowUpArgs]()
	{
		auto RunOne = [](const FString& InCmd, const FString& InArgs, int32& OutCode, FString& OutStdOut, FString& OutStdErr)
		{
#if PLATFORM_WINDOWS
			FString FullArgs = FString::Printf(TEXT("/c %s %s"), *InCmd, *InArgs);
			FPlatformProcess::ExecProcess(TEXT("cmd.exe"), *FullArgs, &OutCode, &OutStdOut, &OutStdErr, nullptr, true);
#else
			FString ShellArgs = FString::Printf(TEXT("-l -c \"%s %s\""), *InCmd, *InArgs);
			FPlatformProcess::ExecProcess(TEXT("/bin/bash"), *ShellArgs, &OutCode, &OutStdOut, &OutStdErr, nullptr, true);
#endif
		};

		int32 ReturnCode = -1;
		FString StdOut, StdErr;
		RunOne(Command, Args, ReturnCode, StdOut, StdErr);
		bool bStep1Success = (ReturnCode == 0);

		bool bStep2Success = true;
		FString Step2Out, Step2Err;
		if (bStep1Success && !FollowUpCommand.IsEmpty())
		{
			int32 Step2Code = -1;
			RunOne(FollowUpCommand, FollowUpArgs, Step2Code, Step2Out, Step2Err);
			bStep2Success = (Step2Code == 0);
		}

		bool bSuccess = bStep1Success && bStep2Success;

		FString CombinedErr;
		if (!bStep1Success)
		{
			CombinedErr = StdErr.TrimEnd();
			if (CombinedErr.IsEmpty()) CombinedErr = StdOut.TrimEnd();
		}
		else if (!bStep2Success)
		{
			CombinedErr = FString::Printf(TEXT("CLI installed OK, but Python SDK install failed:\n%s"),
				*(Step2Err.TrimEnd().IsEmpty() ? Step2Out.TrimEnd() : Step2Err.TrimEnd()));
		}

		AsyncTask(ENamedThreads::GameThread, [WeakSelf, ProviderName, bSuccess, CombinedErr]()
		{
			auto This = WeakSelf.Pin();
			if (!This.IsValid()) return;

			This->bCliInstallInProgress = false;
			This->RefreshCliStatusAsync();

			if (This->SettingsBridgeObject)
			{
				FString Msg;
				if (bSuccess)
				{
					Msg = FString::Printf(TEXT("%s installed successfully!"), *ProviderName);
				}
				else
				{
					FString ErrDetail = CombinedErr.Left(300);
					if (ErrDetail.IsEmpty()) ErrDetail = TEXT("Unknown error. Try running the install command manually in a terminal.");
					Msg = FString::Printf(TEXT("%s install failed:\n%s"), *ProviderName, *ErrDetail);
				}
				This->SettingsBridgeObject->PushInstallResultToJs(ProviderName, bSuccess, Msg);
			}
		});
	});
}

FReply SUECPMainWidget::OnAnalyzePluginClicked()
{
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (!DesktopPlatform) return FReply::Handled();

	FString EngineDir = FPaths::EngineDir();
	FString MarketplacePath = EngineDir / TEXT("Plugins") / TEXT("Marketplace");
	if (!IFileManager::Get().DirectoryExists(*MarketplacePath))
		MarketplacePath = EngineDir;

	FString SelectedFolder;
	bool bOpened = DesktopPlatform->OpenDirectoryDialog(
		FSlateApplication::Get().GetActiveTopLevelWindow()->GetNativeWindow()->GetOSWindowHandle(),
		TEXT("Select Plugin Folder to Analyze"),
		MarketplacePath,
		SelectedFolder
	);

	if (bOpened && !SelectedFolder.IsEmpty())
		AnalyzePluginSource(SelectedFolder);

	return FReply::Handled();
}

void SUECPMainWidget::AnalyzePluginSource(const FString& PluginPath)
{
	FString SourcePath = PluginPath / TEXT("Source");

	if (!IFileManager::Get().DirectoryExists(*SourcePath))
	{
		if (PluginPath.EndsWith(TEXT("Source")) || PluginPath.Contains(TEXT("Source")))
		{
			SourcePath = PluginPath;
		}
		else
		{
			TArray<FString> FoundDirs;
			IFileManager::Get().FindFilesRecursive(FoundDirs, *PluginPath, TEXT("Source"), true, true, false);
			if (FoundDirs.Num() > 0)
			{
				SourcePath = FoundDirs[0];
			}
			else
			{
				TSharedPtr<FJsonObject> UserMsg = MakeShareable(new FJsonObject);
				UserMsg->SetStringField(TEXT("role"), TEXT("user"));
				TArray<TSharedPtr<FJsonValue>> UserParts;
				TSharedPtr<FJsonObject> UserPartText = MakeShareable(new FJsonObject);
				UserPartText->SetStringField(TEXT("text"),
					FString::Printf(TEXT("I want to analyze the plugin at: %s\n\nThis plugin doesn't appear to have a Source folder with C++ code. It may be a Blueprint-only plugin or the Source folder is in a different location."),
					*PluginPath));
				UserParts.Add(MakeShareable(new FJsonValueObject(UserPartText)));
				UserMsg->SetArrayField(TEXT("parts"), UserParts);
				ArchitectConversationHistory.Add(MakeShareable(new FJsonValueObject(UserMsg)));
				RefreshArchitectChatView();
				return;
			}
		}
	}

	TArray<FString> HeaderFiles;
	TArray<FString> CppFiles;
	IFileManager::Get().FindFilesRecursive(HeaderFiles, *SourcePath, TEXT("*.h"),   true, false);
	IFileManager::Get().FindFilesRecursive(CppFiles,    *SourcePath, TEXT("*.cpp"), true, false);

	FString PluginName = FPaths::GetCleanFilename(PluginPath);
	FString FileInfo;
	FileInfo += FString::Printf(TEXT("Plugin Folder: %s\n"), *PluginName);
	FileInfo += FString::Printf(TEXT("Location: %s\n"), *PluginPath);
	FileInfo += FString::Printf(TEXT("Source: %s\n\n"), *SourcePath);
	FileInfo += FString::Printf(TEXT("=== FILES (%d headers, %d sources) ===\n"), HeaderFiles.Num(), CppFiles.Num());

	for (const FString& File : HeaderFiles)
		FileInfo += FString::Printf(TEXT("[H] %s\n"), *File.Mid(SourcePath.Len() + 1));
	for (const FString& File : CppFiles)
		FileInfo += FString::Printf(TEXT("[C] %s\n"), *File.Mid(SourcePath.Len() + 1));

	FString ContentPath = PluginPath / TEXT("Content");
	TArray<FString> UassetFiles;
	IFileManager::Get().FindFilesRecursive(UassetFiles, *ContentPath, TEXT("*.uasset"), true, false);

	bool bPluginEnabled = false;
	FString PluginVirtualPath;
	FString PluginShortName;
	FString PluginFriendlyName;
	TArray<FString> UpluginFiles;
	IFileManager::Get().FindFiles(UpluginFiles, *PluginPath, TEXT("*.uplugin"));
	if (UpluginFiles.Num() > 0)
	{
		PluginShortName = FPaths::GetBaseFilename(UpluginFiles[0]);
		FString UpluginPath = FPaths::Combine(PluginPath, UpluginFiles[0]);
		FString UpluginContent;
		if (FFileHelper::LoadFileToString(UpluginContent, *UpluginPath))
		{
			TSharedPtr<FJsonObject> UpluginJson;
			TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(UpluginContent);
			if (FJsonSerializer::Deserialize(Reader, UpluginJson) && UpluginJson.IsValid())
				UpluginJson->TryGetStringField(TEXT("FriendlyName"), PluginFriendlyName);
		}
		if (PluginFriendlyName.IsEmpty())
			PluginFriendlyName = PluginShortName;

		TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(PluginShortName);
		if (Plugin.IsValid())
		{
			bPluginEnabled = Plugin->IsEnabled();
			PluginVirtualPath = FString::Printf(TEXT("/%s"), *PluginShortName);
		}
	}
	else
	{
		PluginFriendlyName = PluginName;
		PluginShortName    = PluginName;
	}

	FileInfo += TEXT("\n=== PLUGIN INFO ===\n");
	FileInfo += FString::Printf(TEXT("Friendly Name: %s\n"), *PluginFriendlyName);
	FileInfo += FString::Printf(TEXT("Internal Name: %s\n"), *PluginShortName);
	FileInfo += FString::Printf(TEXT("Enabled: %s\n"), bPluginEnabled ? TEXT("Yes") : TEXT("No"));
	if (bPluginEnabled && !PluginVirtualPath.IsEmpty())
	{
		FileInfo += FString::Printf(TEXT("Virtual Path: %s\n"), *PluginVirtualPath);
		FileInfo += TEXT("PATH FORMAT: For UE virtual paths, the 'Content' folder is MAPPED to the plugin root.\n");
		FileInfo += TEXT("Example: Content/Demo/Abilities/BP.uasset becomes /PluginName/Demo/Abilities/BP.BP\n");
		FileInfo += TEXT("DO NOT include 'Content' in the virtual path!\n");
	}

	if (UassetFiles.Num() > 0)
	{
		FileInfo += FString::Printf(TEXT("\n=== BLUEPRINTS (%d files) ===\n"), UassetFiles.Num());
		if (bPluginEnabled)
		{
			FileInfo += FString::Printf(TEXT("STATUS: Plugin is ENABLED - Use list_assets_in_folder with path \"%s\" to find blueprints\n"), *PluginVirtualPath);
			FileInfo += TEXT("To load a blueprint, convert the disk path to virtual path:\n");
		}
		else
		{
			FileInfo += TEXT("STATUS: Plugin is NOT enabled - Blueprints cannot be analyzed (binary .uasset files)\n");
		}
		for (const FString& File : UassetFiles)
		{
			FString RelativePath = File.Mid(PluginPath.Len() + 1);
			FString VirtualPath;
			if (bPluginEnabled && RelativePath.StartsWith(TEXT("Content/")))
			{
				VirtualPath = PluginVirtualPath + TEXT("/") + RelativePath.Mid(8);
				VirtualPath.RemoveFromEnd(TEXT(".uasset"));
				FString BpName = FPaths::GetBaseFilename(VirtualPath);
				VirtualPath = VirtualPath + TEXT(".") + BpName;
			}
			FileInfo += FString::Printf(TEXT("[BP] %s -> %s\n"), *RelativePath, *VirtualPath);
		}
	}

	if (ArchitectConversationHistory.Num() <= 1)
	{
		FString NewTitle = FString::Printf(TEXT("Plugin: %s"), *PluginFriendlyName);
		if (NewTitle.Len() > 40)
			NewTitle = NewTitle.Left(37) + TEXT("...");

		TSharedPtr<FConversationInfo>* FoundInfo = ArchitectConversationList.FindByPredicate(
			[&](const TSharedPtr<FConversationInfo>& Info) { return Info->ID == ActiveArchitectChatID; });
		if (FoundInfo)
		{
			(*FoundInfo)->Title = NewTitle;
			SaveArchitectManifest();
			if (ArchitectChatListView.IsValid())
				ArchitectChatListView->RequestListRefresh();
		}
	}

	const FString DisplayLabel = FString::Printf(
		TEXT("Plugin: %s (%d C++, %d BP)"),
		*PluginFriendlyName, HeaderFiles.Num() + CppFiles.Num(), UassetFiles.Num());

	TSharedPtr<FJsonObject> ContextMsg = MakeShareable(new FJsonObject);
	ContextMsg->SetStringField(TEXT("role"), TEXT("context"));
	ContextMsg->SetStringField(TEXT("file_name"), DisplayLabel);
	ContextMsg->SetStringField(TEXT("content"), FileInfo);
	ContextMsg->SetNumberField(TEXT("char_count"), FileInfo.Len());
	ContextMsg->SetBoolField(TEXT("expanded"), false);

	TArray<TSharedPtr<FJsonValue>> ContextParts;
	TSharedPtr<FJsonObject> ContextPart = MakeShareable(new FJsonObject);
	ContextPart->SetStringField(TEXT("text"),
		FString::Printf(TEXT("--- %s (%d chars, click to expand) ---"),
			*DisplayLabel, FileInfo.Len()));
	ContextParts.Add(MakeShareable(new FJsonValueObject(ContextPart)));
	ContextMsg->SetArrayField(TEXT("parts"), ContextParts);

	ArchitectConversationHistory.Add(MakeShareable(new FJsonValueObject(ContextMsg)));

	PendingArchitectContext.Empty();
	SaveArchitectChatHistory(ActiveArchitectChatID);
	RefreshArchitectChatView();

}

FReply SUECPMainWidget::OnSwitchToSlotClicked(int32 SlotIndex)
{
	if (SlotIndex < 0 || SlotIndex >= MAX_API_KEY_SLOTS) return FReply::Handled();

	FApiKeyManager::Get().SetActiveSlot(SlotIndex);
	CurrentlyEditingSlotIndex = SlotIndex;

	if (SettingsBridgeObject && SettingsBrowser.IsValid())
	{
		FString Json = SettingsBridgeObject->LoadAllSettings();
		SettingsBrowser->ExecuteJavascript(TEXT("if(typeof initSettings==='function')initSettings(") + Json + TEXT(")"));
	}

	FApiKeySlot Slot = FApiKeyManager::Get().GetSlot(SlotIndex);
	FString DisplayName = Slot.GetDisplayName();
	if (AppBridgeObject)
	{
		AppBridgeObject->PushToast(FString::Printf(TEXT("Switched to: %s"), *DisplayName), TEXT("info"));
		AppBridgeObject->PushSlashContext();
		AppBridgeObject->CrewListSlots();
	}

	if (!Slot.Provider.IsEmpty()
		&& IUECPCoreModule::Get().GetACPRegistryService().IsInstalled(Slot.Provider)
		&& !AgentDiscoveryAttempted.Contains(Slot.Provider))
	{
		AgentDiscoveryAttempted.Add(Slot.Provider);
		IUECPCoreModule::Get().GetAgentRunnerService().DiscoverAgentConfig(Slot.Provider);
	}

	return FReply::Handled();
}

bool SUECPMainWidget::HandleGlobalKeyPress(const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.IsLeftAltDown() && !InKeyEvent.IsShiftDown() && !InKeyEvent.IsControlDown())
	{
		FName KeyName = InKeyEvent.GetKey().GetFName();
		if (KeyName == TEXT("One"))   { OnSwitchToSlotClicked(0); return true; }
		if (KeyName == TEXT("Two"))   { OnSwitchToSlotClicked(1); return true; }
		if (KeyName == TEXT("Three")) { OnSwitchToSlotClicked(2); return true; }
		if (KeyName == TEXT("Four"))  { OnSwitchToSlotClicked(3); return true; }
		if (KeyName == TEXT("Five"))  { OnSwitchToSlotClicked(4); return true; }
		if (KeyName == TEXT("Six"))   { OnSwitchToSlotClicked(5); return true; }
		if (KeyName == TEXT("Seven")) { OnSwitchToSlotClicked(6); return true; }
		if (KeyName == TEXT("Eight")) { OnSwitchToSlotClicked(7); return true; }
		if (KeyName == TEXT("Nine"))  { OnSwitchToSlotClicked(8); return true; }
	}
	return false;
}

bool SUECPMainWidget::CheckCliInPath(const FString& CliName)
{
#if PLATFORM_WINDOWS
	{
		FString Cmd  = TEXT("cmd");
		FString Args = FString::Printf(TEXT("/c where %s >nul 2>nul"), *CliName);
		void* PipeRead  = nullptr;
		void* PipeWrite = nullptr;
		FPlatformProcess::CreatePipe(PipeRead, PipeWrite);
		FProcHandle Proc = FPlatformProcess::CreateProc(
			*Cmd, *Args, false, true, true, nullptr, 0, nullptr, PipeWrite, nullptr);
		if (Proc.IsValid())
		{
			double Start = FPlatformTime::Seconds();
			while (FPlatformProcess::IsProcRunning(Proc) && FPlatformTime::Seconds() - Start < 5.0)
				FPlatformProcess::Sleep(0.05f);
			int32 ReturnCode = -1;
			FPlatformProcess::GetProcReturnCode(Proc, &ReturnCode);
			FPlatformProcess::CloseProc(Proc);
			FPlatformProcess::ClosePipe(PipeRead, PipeWrite);
			if (ReturnCode == 0) return true;
		}
		else
		{
			FPlatformProcess::ClosePipe(PipeRead, PipeWrite);
		}
	}
	const FString AppData       = FPlatformMisc::GetEnvironmentVariable(TEXT("APPDATA"));
	const FString LocalAppData  = FPlatformMisc::GetEnvironmentVariable(TEXT("LOCALAPPDATA"));
	const FString ProgramData   = FPlatformMisc::GetEnvironmentVariable(TEXT("ProgramData"));
	const FString UserProfile   = FPlatformMisc::GetEnvironmentVariable(TEXT("USERPROFILE"));

	TArray<FString> Probes;
	auto AddVariants = [&](const FString& Dir)
	{
		if (Dir.IsEmpty()) return;
		for (const TCHAR* Ext : { TEXT(".cmd"), TEXT(".exe"), TEXT(".ps1") })
			Probes.Add(Dir / (CliName + Ext));
	};
	if (!AppData.IsEmpty())       AddVariants(AppData / TEXT("npm"));
	if (!LocalAppData.IsEmpty())  AddVariants(LocalAppData / TEXT("pnpm"));
	if (!UserProfile.IsEmpty())
	{
		AddVariants(UserProfile / TEXT("scoop/shims"));
		AddVariants(UserProfile / TEXT("AppData/Local/Yarn/bin"));
	}
	if (!ProgramData.IsEmpty())   AddVariants(ProgramData / TEXT("chocolatey/bin"));
	if (!UserProfile.IsEmpty() && CliName.Equals(TEXT("claude"), ESearchCase::IgnoreCase))
	{
		if (!LocalAppData.IsEmpty())
		{
			Probes.Add(LocalAppData / TEXT("AnthropicClaude/claude.exe"));
			Probes.Add(LocalAppData / TEXT("Programs/claude/bin/claude.cmd"));
		}
		Probes.Add(UserProfile / TEXT(".claude/local/bin/claude.cmd"));
		Probes.Add(UserProfile / TEXT(".claude/local/bin/claude.exe"));
	}

	for (const FString& P : Probes)
	{
		if (FPaths::FileExists(P)) return true;
	}
	return false;
#else
	FString Home = FPlatformMisc::GetEnvironmentVariable(TEXT("HOME"));
	if (Home.IsEmpty()) Home = FString(TEXT("/Users/")) + FPlatformProcess::UserName(false);

	TArray<FString> Candidates = {
		Home / TEXT(".claude/local/bin") / CliName,
		Home / TEXT(".local/bin") / CliName,
		Home / TEXT("bin") / CliName,
		Home / TEXT(".npm-global/bin") / CliName,
		Home / TEXT(".yarn/bin") / CliName,
		Home / TEXT(".bun/bin") / CliName,
		TEXT("/opt/homebrew/bin") / CliName,
		TEXT("/usr/local/bin") / CliName,
		TEXT("/opt/local/bin") / CliName,
		TEXT("/usr/bin") / CliName,
		TEXT("/bin") / CliName,
		TEXT("/snap/bin") / CliName,
		TEXT("/var/lib/snapd/snap/bin") / CliName,
		TEXT("/home/linuxbrew/.linuxbrew/bin") / CliName,
	};

	FString NvmBase = Home / TEXT(".nvm/versions/node");
	IFileManager& FM = IFileManager::Get();
	if (FM.DirectoryExists(*NvmBase))
	{
		TArray<FString> NodeVersions;
		FM.FindFiles(NodeVersions, *(NvmBase / TEXT("*")), false, true);
		for (const FString& Ver : NodeVersions)
		{
			Candidates.Add(NvmBase / Ver / TEXT("bin") / CliName);
		}
	}

	Candidates.Add(Home / TEXT(".volta/bin") / CliName);
	Candidates.Add(Home / TEXT(".fnm/aliases/default/bin") / CliName);
	Candidates.Add(Home / TEXT(".asdf/shims") / CliName);

	for (const FString& Path : Candidates)
	{
		if (FPaths::FileExists(Path))
			return true;
	}
	return false;
#endif
}

FString SUECPMainWidget::FindCliFullPath(const FString& CliName)
{
#if PLATFORM_WINDOWS
	return TEXT("");
#else
	FString Home = FPlatformMisc::GetEnvironmentVariable(TEXT("HOME"));
	if (Home.IsEmpty()) Home = FString(TEXT("/Users/")) + FPlatformProcess::UserName(false);

	TArray<FString> Candidates = {
		Home / TEXT(".claude/local/bin") / CliName,
		Home / TEXT(".local/bin") / CliName,
		Home / TEXT("bin") / CliName,
		TEXT("/opt/homebrew/bin") / CliName,
		TEXT("/usr/local/bin") / CliName,
		TEXT("/usr/bin") / CliName,
		TEXT("/bin") / CliName,
	};

	FString NvmBase = Home / TEXT(".nvm/versions/node");
	IFileManager& FM = IFileManager::Get();
	if (FM.DirectoryExists(*NvmBase))
	{
		TArray<FString> Vers;
		FM.FindFiles(Vers, *(NvmBase / TEXT("*")), false, true);
		for (const FString& Ver : Vers)
			Candidates.Add(NvmBase / Ver / TEXT("bin") / CliName);
	}
	Candidates.Add(Home / TEXT(".volta/bin") / CliName);
	Candidates.Add(Home / TEXT(".fnm/aliases/default/bin") / CliName);
	Candidates.Add(Home / TEXT(".asdf/shims") / CliName);

	for (const FString& Path : Candidates)
		if (FPaths::FileExists(Path)) return Path;

	return TEXT("");
#endif
}

#undef LOCTEXT_NAMESPACE
