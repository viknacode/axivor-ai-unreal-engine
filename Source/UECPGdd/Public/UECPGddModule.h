// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

DECLARE_LOG_CATEGORY_EXTERN(LogUECPGdd, Log, All);

class FUECPGddCoordinator;

class FUECPGddModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	UECPGDD_API TSharedPtr<FUECPGddCoordinator> GetGddCoordinator() const;

private:
	TSharedPtr<FUECPGddCoordinator> GddCoordinator;
};
