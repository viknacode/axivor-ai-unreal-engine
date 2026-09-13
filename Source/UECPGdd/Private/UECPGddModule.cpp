// Copyright 2026, BlueprintsLab, All rights reserved

#include "UECPGddModule.h"
#include "FUECPGddCoordinator.h"
#include "GddManager.h"
#include "UECPCoreModule.h"

DEFINE_LOG_CATEGORY(LogUECPGdd);

void FUECPGddModule::StartupModule()
{
	UE_LOG(LogUECPGdd, Log, TEXT("FUECPGddModule: StartupModule"));

	FGddManager::Get().LoadManifest();

	if (IUECPCoreModule::IsAvailable())
	{
		GddCoordinator = MakeShared<FUECPGddCoordinator>();
		IUECPCoreModule::Get().SetGddService(GddCoordinator);
	}
}

void FUECPGddModule::ShutdownModule()
{
	UE_LOG(LogUECPGdd, Log, TEXT("FUECPGddModule: ShutdownModule"));

	if (IUECPCoreModule::IsAvailable())
	{
		IUECPCoreModule::Get().SetGddService(nullptr);
	}
	GddCoordinator.Reset();
}

TSharedPtr<FUECPGddCoordinator> FUECPGddModule::GetGddCoordinator() const
{
	return GddCoordinator;
}

IMPLEMENT_MODULE(FUECPGddModule, UECPGdd)
