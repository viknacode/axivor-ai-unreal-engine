// Copyright 2026, BlueprintsLab, All rights reserved

#include "UECPAnalystModule.h"
#include "FUECPAnalystCoordinator.h"
#include "UECPCoreModule.h"

DEFINE_LOG_CATEGORY(LogUECPAnalyst);

void FUECPAnalystModule::StartupModule()
{
	UE_LOG(LogUECPAnalyst, Log, TEXT("FUECPAnalystModule: StartupModule"));

	if (IUECPCoreModule::IsAvailable())
	{
		AnalystCoordinator = MakeShared<FUECPAnalystCoordinator>();
		IUECPCoreModule::Get().SetAnalystService(AnalystCoordinator);
	}
}

void FUECPAnalystModule::ShutdownModule()
{
	UE_LOG(LogUECPAnalyst, Log, TEXT("FUECPAnalystModule: ShutdownModule"));

	if (IUECPCoreModule::IsAvailable())
	{
		IUECPCoreModule::Get().SetAnalystService(nullptr);
	}
	AnalystCoordinator.Reset();
}

TSharedPtr<FUECPAnalystCoordinator> FUECPAnalystModule::GetAnalystCoordinator() const
{
	return AnalystCoordinator;
}

IMPLEMENT_MODULE(FUECPAnalystModule, UECPAnalyst)
