// Copyright 2026, BlueprintsLab, All rights reserved

#include "UECPArchitectModule.h"
#include "FUECPArchitectCoordinator.h"
#include "UECPCoreModule.h"

DEFINE_LOG_CATEGORY(LogUECPArchitect);

void FUECPArchitectModule::StartupModule()
{
	UE_LOG(LogUECPArchitect, Log, TEXT("FUECPArchitectModule: StartupModule"));

	if (IUECPCoreModule::IsAvailable())
	{
		ArchitectCoordinator = MakeShared<FUECPArchitectCoordinator>();
		IUECPCoreModule::Get().SetArchitectService(ArchitectCoordinator);
	}
}

void FUECPArchitectModule::ShutdownModule()
{
	UE_LOG(LogUECPArchitect, Log, TEXT("FUECPArchitectModule: ShutdownModule"));

	if (IUECPCoreModule::IsAvailable())
	{
		IUECPCoreModule::Get().SetArchitectService(nullptr);
	}
	ArchitectCoordinator.Reset();
}

TSharedPtr<FUECPArchitectCoordinator> FUECPArchitectModule::GetArchitectCoordinator() const
{
	return ArchitectCoordinator;
}

IMPLEMENT_MODULE(FUECPArchitectModule, UECPArchitect)
