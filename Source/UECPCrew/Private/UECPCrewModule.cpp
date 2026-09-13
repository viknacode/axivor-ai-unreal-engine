// Copyright 2026, BlueprintsLab, All rights reserved

#include "UECPCrewModule.h"
#include "FUECPCrewCoordinator.h"
#include "Tools/CrewTools.h"
#include "UECPCoreModule.h"
#include "Services/IUECPToolDispatcher.h"

DEFINE_LOG_CATEGORY(LogUECPCrew);

void FUECPCrewModule::StartupModule()
{
	UE_LOG(LogUECPCrew, Log, TEXT("FUECPCrewModule: StartupModule"));

	if (IUECPCoreModule::IsAvailable())
	{
		CrewCoordinator = MakeShared<FUECPCrewCoordinator>();
		CrewCoordinator->Initialize();
		IUECPCoreModule::Get().SetCrewService(CrewCoordinator);

		UECPCrew::Tools::RegisterAll(IUECPCoreModule::Get().GetToolDispatcher());
	}
}

void FUECPCrewModule::ShutdownModule()
{
	UE_LOG(LogUECPCrew, Log, TEXT("FUECPCrewModule: ShutdownModule"));

	if (IUECPCoreModule::IsAvailable())
	{
		UECPCrew::Tools::UnregisterAll(IUECPCoreModule::Get().GetToolDispatcher());
		IUECPCoreModule::Get().SetCrewService(nullptr);
	}
	CrewCoordinator.Reset();
}

TSharedPtr<FUECPCrewCoordinator> FUECPCrewModule::GetCrewCoordinator() const
{
	return CrewCoordinator;
}

IMPLEMENT_MODULE(FUECPCrewModule, UECPCrew)
