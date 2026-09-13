// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

DECLARE_LOG_CATEGORY_EXTERN(LogUECPVoice, Log, All);

class FUECPVoiceCoordinator;

class FUECPVoiceModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	UECPVOICE_API TSharedPtr<FUECPVoiceCoordinator> GetVoiceCoordinator() const;

private:
	TSharedPtr<FUECPVoiceCoordinator> VoiceCoordinator;
};
