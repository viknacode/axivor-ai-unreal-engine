// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

DECLARE_LOG_CATEGORY_EXTERN(LogUECPCrew, Log, All);

class FUECPCrewCoordinator;

class FUECPCrewModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	UECPCREW_API TSharedPtr<FUECPCrewCoordinator> GetCrewCoordinator() const;

private:
	TSharedPtr<FUECPCrewCoordinator> CrewCoordinator;
};
