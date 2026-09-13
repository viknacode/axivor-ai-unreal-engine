// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

DECLARE_LOG_CATEGORY_EXTERN(LogUECPProjectScanner, Log, All);

class FUECPScannerCoordinator;
class FScannerAutoRefresh;

class FUECPProjectScannerModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	UECPPROJECTSCANNER_API TSharedPtr<FUECPScannerCoordinator> GetScannerCoordinator() const;

private:
	TSharedPtr<FUECPScannerCoordinator> ScannerCoordinator;
	TUniquePtr<FScannerAutoRefresh>     AutoRefresh;
};
