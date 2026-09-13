// Copyright 2026, BlueprintsLab, All rights reserved

#include "UECPShellModule.h"
#include "UECPStyle.h"
#include "UECPCommands.h"
#include "UIConfigManager.h"
#include "Managers/FreeTierConfigManager.h"
#include "Managers/ProviderConfigManager.h"
#include "Managers/UpdateManager.h"
#include "Managers/TelemetryManager.h"
#include "Managers/CapabilityProfile.h"
#include "LearningManager.h"
#include "Misc/MessageDialog.h"
const TCHAR* GbB = TEXT("_pAL!v");
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
const TCHAR* GpPluginName = TEXT("BpGeneratorUltimate");
const TCHAR* GpPluginsDir = TEXT("Plugins");
const TCHAR* GpMarketplaceDir = TEXT("Marketplace");
#include "Engine/Blueprint.h"
#include "ToolMenus.h"
bool abc = true;
const TCHAR* GbC = TEXT("2_@");
const int32 GbD = 4;
#include "Kismet/KismetMathLibrary.h"
#include "Kismet/KismetStringLibrary.h"
#include "GameFramework/Actor.h"
const TCHAR* GbA = TEXT("bldr_#k7F");
#include "SUECPMainWidget.h"
#include "Managers/EditorProfileSync.h"
#include "Widgets/Input/SCheckBox.h"
#include "Misc/ConfigCacheIni.h"
const int32 GpluginVariable = 24;
#include "WebBrowserModule.h"
#include "Engine/Engine.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Interfaces/IPluginManager.h"
#include "Widgets/Docking/SDockTab.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWindow.h"
#include "SWebBrowser.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/Base64.h"
#include "Misc/FileHelper.h"
#include "GenericPlatform/GenericPlatformHttp.h"

static const FName BpGeneratorUltimateTabName("BpGeneratorUltimate");

#define LOCTEXT_NAMESPACE "FUECPShellModule"

void FUECPShellModule::GenerateFunctionLists()
{
    auto GetFunctionsForClass = [](UClass* InClass, TArray<FString>& OutFunctions)
    {
        if (!InClass) return;
        for (TFieldIterator<UFunction> FuncIt(InClass, EFieldIteratorFlags::IncludeSuper); FuncIt; ++FuncIt)
        {
            UFunction* Function = *FuncIt;
            if (Function->HasAnyFunctionFlags(FUNC_BlueprintCallable) &&
               !Function->HasAnyFunctionFlags(FUNC_BlueprintEvent | FUNC_Delegate) &&
               !Function->GetName().Contains("DEPRECATED"))
            {
                OutFunctions.Add(Function->GetName());
            }
        }
        OutFunctions.Sort();
    };

    TArray<FString> MathFuncs, StringFuncs, ActorFuncsList;

    GetFunctionsForClass(UKismetMathLibrary::StaticClass(), MathFuncs);
    KismetMathFunctions = FString::Join(MathFuncs, TEXT("\n- "));

    GetFunctionsForClass(UKismetStringLibrary::StaticClass(), StringFuncs);
    KismetStringFunctions = FString::Join(StringFuncs, TEXT("\n- "));

    GetFunctionsForClass(AActor::StaticClass(), ActorFuncsList);
    ActorFunctions = FString::Join(ActorFuncsList, TEXT("\n- "));
}

void FUECPShellModule::CleanupZombieAgentProcesses()
{
#if PLATFORM_WINDOWS
    FString PsCmd = FString::Printf(
        TEXT("-NoProfile -NonInteractive -WindowStyle Hidden -Command \"")
        TEXT("$myPid=%d; ")
        TEXT("netstat -ano | Select-String 'LISTENING' | ForEach-Object { ")
        TEXT("  if($_ -match ':(1987[89]|1988[0-9]|1989[0-9])\\s.*LISTENING\\s+(\\d+)') { ")
        TEXT("    $pid=[int]$matches[2]; if($pid -ne $myPid -and $pid -gt 0) { Stop-Process -Id $pid -Force -ErrorAction SilentlyContinue } ")
        TEXT("  } ")
        TEXT("}\""),
        FPlatformProcess::GetCurrentProcessId());
    FPlatformProcess::CreateProc(TEXT("powershell.exe"), *PsCmd, true, true, true, nullptr, 0, nullptr, nullptr, nullptr);
#elif PLATFORM_MAC || PLATFORM_LINUX
    FString ShCmd = FString::Printf(
        TEXT("-c \"for p in $(seq 19878 19899); do pid=$(lsof -ti :$p 2>/dev/null); ")
        TEXT("[ -n \\\"$pid\\\" ] && [ \\\"$pid\\\" != \\\"%d\\\" ] && kill -9 $pid 2>/dev/null; done\""),
        FPlatformProcess::GetCurrentProcessId());
    FPlatformProcess::CreateProc(TEXT("/bin/sh"), *ShCmd, true, true, true, nullptr, 0, nullptr, nullptr, nullptr);
#endif
}

void FUECPShellModule::EnsureHttpTimeoutsConfigured()
{
    const FString IniPath = FPaths::ProjectConfigDir() / TEXT("DefaultEngine.ini");

    FString Content;
    FFileHelper::LoadFileToString(Content, *IniPath);

    struct FEntry { const TCHAR* Key; const TCHAR* Value; };
    const FEntry Required[] = {
        { TEXT("HttpConnectionTimeout"), TEXT("600") },
        { TEXT("HttpActivityTimeout"),   TEXT("0")   },
        { TEXT("HttpTotalTimeout"),      TEXT("0")   },
    };

    FString ToInsert;
    for (const FEntry& E : Required)
        if (!Content.Contains(E.Key))
            ToInsert += FString::Printf(TEXT("%s=%s\n"), E.Key, E.Value);

    if (ToInsert.IsEmpty()) return;

    if (Content.Contains(TEXT("[HTTP]")))
        Content = Content.Replace(TEXT("[HTTP]"), *FString::Printf(TEXT("[HTTP]\n%s"), *ToInsert));
    else
        Content += FString::Printf(TEXT("\n[HTTP]\n%s"), *ToInsert);

    FFileHelper::SaveStringToFile(Content, *IniPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    UE_LOG(LogTemp, Warning, TEXT("BpGeneratorUltimate: Added [HTTP] timeout settings to DefaultEngine.ini — restart editor for changes to take effect."));
}

void FUECPShellModule::StartupModule()
{
    IWebBrowserModule::Get();
    TSharedPtr<IPlugin> ThisPlugin = IPluginManager::Get().FindPlugin(GpPluginName);
    if (ThisPlugin.IsValid())
    {
        const FString PluginBaseDir = ThisPlugin->GetBaseDir();
        const FString EngineDir = FPaths::EngineDir();
        const FString ExpectedMarketplacePath = FPaths::Combine(EngineDir, GpPluginsDir, GpMarketplaceDir);

        if (FPaths::IsUnderDirectory(PluginBaseDir, ExpectedMarketplacePath))
        {
            abc = true;
        }
    }

    EnsureHttpTimeoutsConfigured();

    if (!abc)
    {
        return;
    }

    FUECPStyle::Initialize();
    FUECPStyle::ReloadTextures();
    FUECPCommands::Register();

    // Open the actual workspace directly. Welcome and announcement windows remain
    // available to the implementation, but are never injected into editor startup.
    MainInterfaceTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
        FTickerDelegate::CreateRaw(this, &FUECPShellModule::OnOpenMainInterface), 1.0f);

    PluginCommands = MakeShareable(new FUICommandList);
    PluginCommands->MapAction(
        FUECPCommands::Get().PluginAction,
        FExecuteAction::CreateRaw(this, &FUECPShellModule::PluginButtonClicked),
        FCanExecuteAction());

    UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FUECPShellModule::RegisterMenus));

    FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
    ContentBrowserSelectionChangedHandle = ContentBrowserModule.GetOnAssetSelectionChanged().AddRaw(this, &FUECPShellModule::OnContentBrowserSelectionChanged);

    GenerateFunctionLists();

    CleanupZombieAgentProcesses();

    FCoreDelegates::OnShutdownAfterError.AddLambda([]()
    {
        UE_LOG(LogTemp, Error, TEXT("BpGeneratorUltimate: SHUTDOWN AFTER ERROR — flushing logs"));
        GLog->Flush();
        GLog->FlushThreadedLogs();
    });

    FEditorProfileSync::Get().InitializeSync();

    FUIConfigManager::Get().Initialize();
    FFreeTierConfigManager::Get().Initialize();
    FProviderConfigManager::Get().Initialize();
    FTelemetryManager::Get().Initialize();
    FUpdateManager::Get().Initialize();
    FCapabilityProfile::Get().Initialize();

    FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
        BpGeneratorUltimateTabName,
        FOnSpawnTab::CreateRaw(this, &FUECPShellModule::OnSpawnPluginTab))
        .SetDisplayName(LOCTEXT("BpGeneratorUltimateTabTitle", "Axivor AI"))
        .SetIcon(FSlateIcon(FUECPStyle::GetStyleSetName(), "BpGeneratorUltimate.PluginAction"));
}

void FUECPShellModule::ShutdownModule()
{
    if (!abc)
    {
        return;
    }
    FLearningManager::Get().FlushEventBuffer();

    FEditorProfileSync::Get().ShutdownSync();
    FTSTicker::GetCoreTicker().RemoveTicker(MainInterfaceTickerHandle);

    if (FModuleManager::Get().IsModuleLoaded("ContentBrowser"))
    {
        FContentBrowserModule& ContentBrowserModule = FModuleManager::GetModuleChecked<FContentBrowserModule>("ContentBrowser");
        ContentBrowserModule.GetOnAssetSelectionChanged().Remove(ContentBrowserSelectionChangedHandle);
    }

    UToolMenus::UnRegisterStartupCallback(this);
    UToolMenus::UnregisterOwner(this);
    FUECPStyle::Shutdown();
    FUECPCommands::Unregister();

    FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(BpGeneratorUltimateTabName);

    if (TSharedPtr<SDockTab> Tab = PluginTabPtr.Pin())
    {
        Tab->RequestCloseTab();
        PluginTabPtr.Reset();
    }
}

void FUECPShellModule::OnContentBrowserSelectionChanged(const TArray<FAssetData>& SelectedAssets, bool bIsPrimaryBrowser)
{
    if (bIsPrimaryBrowser && SelectedAssets.Num() == 1)
    {
        UObject* SelectedObject = SelectedAssets[0].GetAsset();
        if (UBlueprint* Blueprint = Cast<UBlueprint>(SelectedObject))
        {
            TargetBlueprint = Blueprint;
            return;
        }
    }

    TargetBlueprint = nullptr;
}

void FUECPShellModule::PluginButtonClicked()
{
    FGlobalTabmanager::Get()->TryInvokeTab(BpGeneratorUltimateTabName);
}

TSharedRef<SDockTab> FUECPShellModule::OnSpawnPluginTab(const FSpawnTabArgs& SpawnTabArgs)
{
    TSharedRef<SDockTab> NewTab = SNew(SDockTab)
        .TabRole(ETabRole::NomadTab)
        .Label(LOCTEXT("BpGeneratorUltimateTabTitle", "Axivor AI"))
        [
            SNew(SUECPMainWidget)
        ];

    PluginTabPtr = NewTab;
    return NewTab;
}

void FUECPShellModule::RegisterMenus()
{
    FToolMenuOwnerScoped OwnerScoped(this);

    const TArray<FName> WindowMenuNames = {
        "LevelEditor.MainMenu.Window",
        "BlueprintEditor.MainMenu.Window",
        "AnimationBlueprintEditor.MainMenu.Window",
        "MaterialEditor.MainMenu.Window",
        "BehaviorTreeEditor.MainMenu.Window",
        "WidgetBlueprintEditor.MainMenu.Window"
    };

    for (const FName& MenuName : WindowMenuNames)
    {
        if (UToolMenu* Menu = UToolMenus::Get()->ExtendMenu(MenuName))
        {
            FToolMenuSection& Section = Menu->FindOrAddSection("WindowLayout");
            Section.AddMenuEntryWithCommandList(FUECPCommands::Get().PluginAction, PluginCommands);
        }
    }

    {
        UToolMenu* ToolbarMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.LevelEditorToolBar.PlayToolBar");
        if (ToolbarMenu)
        {
            FToolMenuSection& Section = ToolbarMenu->FindOrAddSection("UltimateBpGen_Tool");
            FToolMenuEntry& Entry = Section.AddEntry(FToolMenuEntry::InitToolBarButton(FUECPCommands::Get().PluginAction));
            Entry.SetCommandList(PluginCommands);
        }
    }

    const TArray<FName> ToolbarNames = {
        "BlueprintEditor.ToolBar",
        "WidgetBlueprintEditor.ToolBar",
        "AnimationBlueprintEditor.ToolBar",
        "MaterialEditor.ToolBar",
        "BehaviorTreeEditor.ToolBar",
        "MaterialInstanceEditor.ToolBar",
        "Persona.ToolBar"
    };

    for (const FName& ToolbarName : ToolbarNames)
    {
        if (UToolMenu* ToolbarMenu = UToolMenus::Get()->ExtendMenu(ToolbarName))
        {
            FToolMenuSection& Section = ToolbarMenu->FindOrAddSection("Asset");
            FToolMenuEntry& Entry = Section.AddEntry(FToolMenuEntry::InitToolBarButton(FUECPCommands::Get().PluginAction));
            Entry.SetCommandList(PluginCommands);
        }
    }

}

bool FUECPShellModule::OnOpenMainInterface(float DeltaTime)
{
    if (!GEditor || !GEditor->GetEditorWorldContext().World())
        return true;

    FGlobalTabmanager::Get()->TryInvokeTab(BpGeneratorUltimateTabName);
    UE_LOG(LogTemp, Display, TEXT("Axivor AI: main interface opened directly; startup popups disabled."));
    return false;
}

void FUECPShellModule::ShowWelcomePopup()
{
    FetchAndShowWelcomeScreen();
}

static FString BuildWelcomeHtmlDataUri()
{
    TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("BpGeneratorUltimate"));
    if (!Plugin.IsValid()) return FString();
    FString HtmlPath = Plugin->GetBaseDir() / TEXT("Resources/UI/welcome_screen.html");
    FString Html;
    if (!FFileHelper::LoadFileToString(Html, *HtmlPath)) return FString();
    FString Encoded = FBase64::Encode(reinterpret_cast<const uint8*>(TCHAR_TO_UTF8(*Html)), FTCHARToUTF8_Convert::ConvertedLength(*Html, Html.Len()));
    return FString::Printf(TEXT("data:text/html;base64,%s"), *Encoded);
}

static FString EscapeJsonForJs(const FString& In)
{
    FString Out = In;
    Out.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
    Out.ReplaceInline(TEXT("'"), TEXT("\\'"));
    Out.ReplaceInline(TEXT("\n"), TEXT("\\n"));
    Out.ReplaceInline(TEXT("\r"), TEXT(""));
    return Out;
}

void FUECPShellModule::ShowWelcomeScreen(const FString& JsonData, const FString& Mode)
{
    TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("BpGeneratorUltimate"));
    if (!Plugin.IsValid()) return;
    FString HtmlPath = Plugin->GetBaseDir() / TEXT("Resources/UI/welcome_screen.html");
    FString Html;
    if (!FFileHelper::LoadFileToString(Html, *HtmlPath))
    {
        UE_LOG(LogTemp, Warning, TEXT("Welcome: Could not load welcome_screen.html"));
        return;
    }

    FString Lang = TEXT("en");
    FString SavedLang;
    if (GConfig->GetString(TEXT("BpGeneratorUltimate"), TEXT("Language"), SavedLang, FSettingsManager::GetGlobalConfigPath()) && !SavedLang.IsEmpty())
        Lang = SavedLang;

    FString InitFn = (Mode == TEXT("announcement")) ? TEXT("initAnnouncement") : TEXT("initWelcome");

    FString HtmlSafeJson = JsonData;
    HtmlSafeJson.ReplaceInline(TEXT("&"), TEXT("&amp;"));
    HtmlSafeJson.ReplaceInline(TEXT("<"), TEXT("&lt;"));
    HtmlSafeJson.ReplaceInline(TEXT(">"), TEXT("&gt;"));
    HtmlSafeJson.ReplaceInline(TEXT("\""), TEXT("&quot;"));

    FString InjectBlock = FString::Printf(
        TEXT("<textarea id=\"_injected_json\" style=\"display:none\">%s</textarea>")
        TEXT("<script>")
        TEXT("(function(){")
        TEXT("var el=document.getElementById('_injected_json');")
        TEXT("if(el){try{var fn='%s'==='initAnnouncement'?initAnnouncement:initWelcome;fn(JSON.parse(el.value));setLanguage('%s');}")
        TEXT("catch(e){document.getElementById('body-content').innerHTML='<p style=\"color:#e05555\">Error: '+e.message+'</p>';initWelcome(_fallbackData);}}")
        TEXT("else{initWelcome(_fallbackData);}")
        TEXT("})();")
        TEXT("</script></body>"),
        *HtmlSafeJson, *InitFn, *Lang);
    Html.ReplaceInline(TEXT("</body>"), *InjectBlock);

    TArray<uint8> HtmlBytes;
    FTCHARToUTF8 Conv(*Html);
    HtmlBytes.Append((const uint8*)Conv.Get(), Conv.Length());
    TSharedPtr<FString> DataUriPtr = MakeShared<FString>(TEXT("data:text/html;base64,") + FBase64::Encode(HtmlBytes));

    TSharedRef<SWindow> WelcomeWindow = SNew(SWindow)
        .Title(FText::FromString(TEXT("Axivor AI")))
        .ClientSize(FVector2D(700, 620))
        .SupportsMinimize(true)
        .SupportsMaximize(false)
        .HasCloseButton(true)
        .IsTopmostWindow(true)
        .FocusWhenFirstShown(true);

    TSharedPtr<SWebBrowser> Browser;
    TWeakPtr<SWindow> WeakWindow = WelcomeWindow;

    WelcomeWindow->SetContent(
        SAssignNew(Browser, SWebBrowser)
            .InitialURL(TEXT("about:blank"))
            .ShowControls(false)
            .ShowAddressBar(false)
            .ShowErrorMessage(false)
            .SupportsTransparency(false)
            .OnBeforeNavigation_Lambda([WeakWindow](const FString& Url, const FWebNavigationRequest& ) -> bool
            {
                if (!Url.StartsWith(TEXT("ue://welcome/"))) return false;

                FString Path = Url.Mid(13);
                if (Path.StartsWith(TEXT("close")))
                {
                    if (auto W = WeakWindow.Pin()) W->RequestDestroyWindow();
                    return true;
                }
                if (Path.StartsWith(TEXT("dismiss/")))
                {
                    FString DismissMode = Path.Mid(8);
                    if (DismissMode == TEXT("welcome"))
                    {
                        GConfig->SetString(TEXT("BpGeneratorUltimate"), TEXT("DismissedWelcomeVersion"), TEXT("1.0"), FSettingsManager::GetGlobalConfigPath());
                        GConfig->Flush(false, FSettingsManager::GetGlobalConfigPath());
                    }
                    else if (DismissMode.StartsWith(TEXT("announcement%3A")) || DismissMode.StartsWith(TEXT("announcement:")))
                    {
                        FString AnnId = DismissMode.Contains(TEXT("%3A")) ? DismissMode.Mid(15) : DismissMode.Mid(13);
                        GConfig->SetString(TEXT("BpGeneratorUltimate"), TEXT("LastDismissedAnnouncementId"), *AnnId, FSettingsManager::GetGlobalConfigPath());
                        GConfig->Flush(false, FSettingsManager::GetGlobalConfigPath());
                    }
                    if (auto W = WeakWindow.Pin()) W->RequestDestroyWindow();
                    return true;
                }
                if (Path.StartsWith(TEXT("openurl/")))
                {
                    FString DecodedUrl = FGenericPlatformHttp::UrlDecode(Path.Mid(8));
                    FPlatformProcess::LaunchURL(*DecodedUrl, nullptr, nullptr);
                    return true;
                }
                if (Path.StartsWith(TEXT("setlanguage/")))
                {
                    FString LangCode = Path.Mid(12);
                    GConfig->SetString(TEXT("BpGeneratorUltimate"), TEXT("Language"), *LangCode, FSettingsManager::GetGlobalConfigPath());
                    FUIConfigManager::Get().SetLanguage(LangCode);
                    return true;
                }
                return false;
            })
    );

    if (Browser.IsValid() && GEditor)
    {
        TWeakPtr<SWebBrowser> WeakBrowser = Browser;
        FTimerHandle WelcomeLoadTimer;
        GEditor->GetTimerManager()->SetTimer(WelcomeLoadTimer, [WeakBrowser, DataUriPtr]()
        {
            if (auto B = WeakBrowser.Pin())
                B->LoadURL(*DataUriPtr);
        }, 0.3f, false);
    }
    else if (Browser.IsValid())
    {
        Browser->LoadURL(*DataUriPtr);
    }

    WelcomeWindow->GetOnWindowClosedEvent().AddLambda([this](const TSharedRef<SWindow>&)
    {
        FTimerHandle AnnouncementTimer;
        GEditor->GetTimerManager()->SetTimer(
            AnnouncementTimer,
            FTimerDelegate::CreateLambda([this]() { FetchAndShowAnnouncements(); }),
            3.0f, false);
    });

    FSlateApplication::Get().AddWindow(WelcomeWindow);

    if (Browser.IsValid())
    {
        FSlateApplication::Get().SetKeyboardFocus(Browser);
    }
}

void FUECPShellModule::FetchAndShowWelcomeScreen()
{
    const FString WelcomeSupabaseUrl = FFreeTierConfigManager::Get().GetRemoteBaseUrl();
    const FString WelcomeAnonKey     = FFreeTierConfigManager::Get().GetServiceRegistrationKey();
    FString Endpoint = WelcomeSupabaseUrl + TEXT("/rest/v1/welcome_screen?select=*&id=eq.1&is_active=eq.true");

    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
    Request->SetURL(Endpoint);
    Request->SetVerb(TEXT("GET"));
    Request->SetHeader(TEXT("apikey"), WelcomeAnonKey);
    Request->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *WelcomeAnonKey));
    Request->SetHeader(TEXT("Accept"), TEXT("application/json"));
    Request->SetTimeout(10.0f);

    Request->OnProcessRequestComplete().BindLambda([this](FHttpRequestPtr, FHttpResponsePtr Response, bool bOk)
    {

        FString Json;
        if (bOk && Response.IsValid() && Response->GetResponseCode() == 200)
        {
            FString Body = Response->GetContentAsString();
            TArray<TSharedPtr<FJsonValue>> Arr;
            TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
            if (FJsonSerializer::Deserialize(Reader, Arr) && Arr.Num() > 0)
            {
                TSharedPtr<FJsonObject> Obj = Arr[0]->AsObject();
                if (Obj.IsValid())
                {
                    FString Version;
                    Obj->TryGetStringField(TEXT("version"), Version);
                    FString DismissedVersion;
                    GConfig->GetString(TEXT("BpGeneratorUltimate"), TEXT("DismissedWelcomeVersion"), DismissedVersion, FSettingsManager::GetGlobalConfigPath());
                    if (!DismissedVersion.IsEmpty() && DismissedVersion == Version)
                    {
                        FetchAndShowAnnouncements();
                        return;
                    }

                    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
                    FJsonSerializer::Serialize(Obj.ToSharedRef(), Writer);
                }
            }
        }

        if (Json.IsEmpty())
        {
            UE_LOG(LogTemp, Log, TEXT("Welcome: Using fallback English content (remote unavailable or empty)"));
            Json = TEXT("{\"version\":\"2.0\",\"body_html_en\":\"\",\"cta_buttons\":[]}");
        }

        AsyncTask(ENamedThreads::GameThread, [this, Json]()
        {
            ShowWelcomeScreen(Json, TEXT("welcome"));
        });
    });

    Request->ProcessRequest();
}

void FUECPShellModule::FetchAndShowAnnouncements()
{
    if (bAnnouncementShownThisSession) return;
    bAnnouncementShownThisSession = true;

    FString LastDismissed;
    GConfig->GetString(TEXT("BpGeneratorUltimate"), TEXT("LastDismissedAnnouncementId"), LastDismissed, FSettingsManager::GetGlobalConfigPath());

    const FString WelcomeSupabaseUrl = FFreeTierConfigManager::Get().GetRemoteBaseUrl();
    const FString WelcomeAnonKey     = FFreeTierConfigManager::Get().GetServiceRegistrationKey();
    FString Endpoint = WelcomeSupabaseUrl + TEXT("/rest/v1/admin_announcements?select=*&is_active=eq.true&order=priority.desc&limit=1");

    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
    Request->SetURL(Endpoint);
    Request->SetVerb(TEXT("GET"));
    Request->SetHeader(TEXT("apikey"), WelcomeAnonKey);
    Request->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *WelcomeAnonKey));
    Request->SetHeader(TEXT("Accept"), TEXT("application/json"));
    Request->SetTimeout(10.0f);

    Request->OnProcessRequestComplete().BindLambda([this, LastDismissed](FHttpRequestPtr, FHttpResponsePtr Response, bool bOk)
    {
        if (!bOk || !Response.IsValid() || Response->GetResponseCode() != 200) return;

        TArray<TSharedPtr<FJsonValue>> Arr;
        TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response->GetContentAsString());
        if (!FJsonSerializer::Deserialize(Reader, Arr)) return;

        for (const TSharedPtr<FJsonValue>& Val : Arr)
        {
            TSharedPtr<FJsonObject> Obj = Val->AsObject();
            if (!Obj.IsValid()) continue;

            FString AnnId;
            Obj->TryGetStringField(TEXT("announcement_id"), AnnId);

            if (!LastDismissed.IsEmpty() && LastDismissed == AnnId) continue;

            FString ExpiresStr;
            if (Obj->TryGetStringField(TEXT("expires_at"), ExpiresStr) && !ExpiresStr.IsEmpty())
            {
                FDateTime Expires;
                if (FDateTime::ParseIso8601(*ExpiresStr, Expires) && FDateTime::UtcNow() > Expires)
                    continue;
            }

            FString Json;
            TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
            FJsonSerializer::Serialize(Obj.ToSharedRef(), Writer);

            AsyncTask(ENamedThreads::GameThread, [this, Json, AnnId]()
            {
                ShowWelcomeScreen(Json, TEXT("announcement"));
            });
            return;
        }
    });

    Request->ProcessRequest();
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FUECPShellModule, UECPShell)
