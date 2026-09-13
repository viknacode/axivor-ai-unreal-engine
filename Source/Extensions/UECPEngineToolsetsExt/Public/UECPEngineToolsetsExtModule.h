// Axivor AI — Unreal 5.8 native Toolset Registry + File Sandbox bridge.
#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

DECLARE_LOG_CATEGORY_EXTERN(LogUECPEngineToolsetsExt, Log, All);

class FUECPEngineToolsetsExtModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
