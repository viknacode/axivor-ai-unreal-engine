// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "Widget/UUECPBridgeBase.h"
#include "UUECPScannerBridge.generated.h"

UCLASS()
class UUECPScannerBridge : public UUECPBridgeBase
{
	GENERATED_BODY()

public:
	UFUNCTION() void ScanProject();
	UFUNCTION() void GetPerformanceReport();
	UFUNCTION() void GetProjectOverview();
	UFUNCTION() void OpenDashboard();
	UFUNCTION() void RequestScannerTypeFilters();
	UFUNCTION() void SetScannerTypeFilter(const FString& TypeKey, bool bEnabled);
	UFUNCTION() void RequestScannerDirectories();
	UFUNCTION() void SetScannerDirectory(const FString& DirPath, bool bEnabled);
	UFUNCTION() void RequestScannerPlugins();
	UFUNCTION() void SetScannerPlugin(const FString& PluginName, bool bEnabled);
	UFUNCTION() void RequestScannerPluginSources();
	UFUNCTION() void SetScannerPluginSource(const FString& PluginName, bool bEnabled);
	UFUNCTION() void RequestScannerOptions();
	UFUNCTION() void SetScannerOption(const FString& Key, bool bEnabled);
	UFUNCTION() void RequestScannerResponseMode();
	UFUNCTION() void SetScannerResponseMode(const FString& Mode);
	UFUNCTION() void ResetScannerSettings();
	UFUNCTION() void ClearScannerCrashSkipList();
};
