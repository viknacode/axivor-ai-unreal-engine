// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once
#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
DECLARE_LOG_CATEGORY_EXTERN(LogUECPGeometryScriptExt, Log, All);
class FUECPGeometryScriptExtModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
