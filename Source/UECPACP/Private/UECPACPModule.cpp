// Copyright 2026, BlueprintsLab, All rights reserved

#include "UECPACPModule.h"
#include "FUECPAgentRunnerCoordinator.h"
#include "Registry/FUECPACPRegistry.h"
#include "UECPCoreModule.h"

DEFINE_LOG_CATEGORY(LogUECPACP);

void FUECPACPModule::StartupModule()
{
	UE_LOG(LogUECPACP, Log, TEXT("FUECPACPModule: StartupModule"));

	if (!IUECPCoreModule::IsAvailable()) return;

	AgentRunnerCoordinator = MakeShared<FUECPAgentRunnerCoordinator>();
	IUECPCoreModule::Get().SetAgentRunnerService(AgentRunnerCoordinator);

	RegistryService = MakeShared<FUECPACPRegistry>();
	IUECPCoreModule::Get().SetACPRegistryService(RegistryService);

	RegistryService->RefreshCatalog( false);
}

void FUECPACPModule::ShutdownModule()
{
	UE_LOG(LogUECPACP, Log, TEXT("FUECPACPModule: ShutdownModule"));

	if (IUECPCoreModule::IsAvailable())
	{
		IUECPCoreModule::Get().SetAgentRunnerService(nullptr);
		IUECPCoreModule::Get().SetACPRegistryService(nullptr);
	}
	AgentRunnerCoordinator.Reset();
	RegistryService.Reset();
}

TSharedPtr<FUECPAgentRunnerCoordinator> FUECPACPModule::GetAgentRunnerCoordinator() const
{
	return AgentRunnerCoordinator;
}

IMPLEMENT_MODULE(FUECPACPModule, UECPACP)
