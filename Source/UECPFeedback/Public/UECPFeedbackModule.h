// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

DECLARE_LOG_CATEGORY_EXTERN(LogUECPFeedback, Log, All);

class FUECPBugReportCoordinator;

class FUECPFeedbackModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	UECPFEEDBACK_API TSharedPtr<FUECPBugReportCoordinator> GetBugReportCoordinator() const;

private:
	TSharedPtr<FUECPBugReportCoordinator> BugReportCoordinator;
};
