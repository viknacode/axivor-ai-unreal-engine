// Copyright 2026, BlueprintsLab, All rights reserved

#include "UECPProjectScannerModule.h"
#include "FUECPScannerCoordinator.h"
#include "ScannerAutoRefresh.h"
#include "UECPCoreModule.h"

DEFINE_LOG_CATEGORY(LogUECPProjectScanner);

void FUECPProjectScannerModule::StartupModule()
{
	UE_LOG(LogUECPProjectScanner, Log, TEXT("FUECPProjectScannerModule: StartupModule"));

	if (IUECPCoreModule::IsAvailable())
	{
		ScannerCoordinator = MakeShared<FUECPScannerCoordinator>();
		IUECPCoreModule::Get().SetScannerService(ScannerCoordinator);
	}

	AutoRefresh = MakeUnique<FScannerAutoRefresh>();
}

void FUECPProjectScannerModule::ShutdownModule()
{
	UE_LOG(LogUECPProjectScanner, Log, TEXT("FUECPProjectScannerModule: ShutdownModule"));

	AutoRefresh.Reset();

	if (IUECPCoreModule::IsAvailable())
	{
		IUECPCoreModule::Get().SetScannerService(nullptr);
	}
	ScannerCoordinator.Reset();
}

TSharedPtr<FUECPScannerCoordinator> FUECPProjectScannerModule::GetScannerCoordinator() const
{
	return ScannerCoordinator;
}

IMPLEMENT_MODULE(FUECPProjectScannerModule, UECPProjectScanner)
