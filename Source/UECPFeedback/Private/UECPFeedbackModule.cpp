// Copyright 2026, BlueprintsLab, All rights reserved

#include "UECPFeedbackModule.h"
#include "FUECPBugReportCoordinator.h"
#include "UECPCoreModule.h"

DEFINE_LOG_CATEGORY(LogUECPFeedback);

void FUECPFeedbackModule::StartupModule()
{
	UE_LOG(LogUECPFeedback, Log, TEXT("FUECPFeedbackModule: StartupModule"));

	if (IUECPCoreModule::IsAvailable())
	{
		BugReportCoordinator = MakeShared<FUECPBugReportCoordinator>();
		IUECPCoreModule::Get().SetBugReportService(BugReportCoordinator);
	}
}

void FUECPFeedbackModule::ShutdownModule()
{
	UE_LOG(LogUECPFeedback, Log, TEXT("FUECPFeedbackModule: ShutdownModule"));

	if (IUECPCoreModule::IsAvailable())
	{
		IUECPCoreModule::Get().SetBugReportService(nullptr);
	}
	BugReportCoordinator.Reset();
}

TSharedPtr<FUECPBugReportCoordinator> FUECPFeedbackModule::GetBugReportCoordinator() const
{
	return BugReportCoordinator;
}

IMPLEMENT_MODULE(FUECPFeedbackModule, UECPFeedback)
