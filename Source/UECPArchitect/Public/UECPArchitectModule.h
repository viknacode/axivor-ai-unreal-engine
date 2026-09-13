// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

DECLARE_LOG_CATEGORY_EXTERN(LogUECPArchitect, Log, All);

class FUECPArchitectCoordinator;

class FUECPArchitectModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	UECPARCHITECT_API TSharedPtr<FUECPArchitectCoordinator> GetArchitectCoordinator() const;

private:
	TSharedPtr<FUECPArchitectCoordinator> ArchitectCoordinator;
};
