// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

DECLARE_LOG_CATEGORY_EXTERN(LogUECPACP, Log, All);

class FUECPAgentRunnerCoordinator;
class FUECPACPRegistry;

class FUECPACPModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	UECPACP_API TSharedPtr<FUECPAgentRunnerCoordinator> GetAgentRunnerCoordinator() const;

private:
	TSharedPtr<FUECPAgentRunnerCoordinator> AgentRunnerCoordinator;
	TSharedPtr<FUECPACPRegistry>            RegistryService;
};
