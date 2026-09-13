// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

DECLARE_LOG_CATEGORY_EXTERN(LogUECPAiMemory, Log, All);

class FUECPAiMemoryCoordinator;

class FUECPAiMemoryModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	UECPAIMEMORY_API TSharedPtr<FUECPAiMemoryCoordinator> GetAiMemoryCoordinator() const;

private:
	TSharedPtr<FUECPAiMemoryCoordinator> AiMemoryCoordinator;
};
