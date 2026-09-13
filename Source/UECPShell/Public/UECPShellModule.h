// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "Tickable.h"
#include "Framework/Docking/TabManager.h"

class FToolBarBuilder;
class FMenuBuilder;
extern const TCHAR* GpluginWindow;
extern const TCHAR* GpluginModule;
extern const int32 GpluginVariable;
class SUECPMainWidget;
extern const TCHAR* GbA;
extern const TCHAR* GbB;
extern const TCHAR* GbC;
extern const int32 GbD;
class UBlueprint;
struct FAssetData;
extern const TCHAR* GpluginState;
class SCheckBox;
class SDockTab;
class SWindow;

class FUECPShellModule : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

    void PluginButtonClicked();

    UBlueprint* GetTargetBlueprint() const { return TargetBlueprint.Get(); }

    const FString& GetKismetMathFunctions() const { return KismetMathFunctions; }
    const FString& GetKismetStringFunctions() const { return KismetStringFunctions; }
    const FString& GetActorFunctions() const { return ActorFunctions; }

private:
    void RegisterMenus();
    void OnContentBrowserSelectionChanged(const TArray<FAssetData>& SelectedAssets, bool bIsPrimaryBrowser);

    void GenerateFunctionLists();
    void EnsureHttpTimeoutsConfigured();
    void CleanupZombieAgentProcesses();

    TSharedRef<SDockTab> OnSpawnPluginTab(const FSpawnTabArgs& SpawnTabArgs);

    TSharedPtr<class FUICommandList> PluginCommands;
    FDelegateHandle ContentBrowserSelectionChangedHandle;
    TWeakPtr<SDockTab> PluginTabPtr;
    TWeakObjectPtr<UBlueprint> TargetBlueprint;
    bool OnOpenMainInterface(float DeltaTime);
    FTSTicker::FDelegateHandle MainInterfaceTickerHandle;
    void ShowWelcomePopup();
    void ShowWelcomeScreen(const FString& JsonData, const FString& Mode = TEXT("welcome"));
    void FetchAndShowWelcomeScreen();
    void FetchAndShowAnnouncements();
    bool bAnnouncementShownThisSession = false;

    FString KismetMathFunctions;
    FString KismetStringFunctions;
    FString ActorFunctions;
};
