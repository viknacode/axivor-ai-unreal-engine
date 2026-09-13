// Copyright 2026, BlueprintsLab, All rights reserved

#include "UECPLearningModule.h"
#include "UECPLearningServiceImpl.h"
#include "UECPCoreModule.h"
#include "LearningManager.h"

DEFINE_LOG_CATEGORY(LogUECPLearning);

void FUECPLearningModule::StartupModule()
{
	UE_LOG(LogUECPLearning, Log, TEXT("FUECPLearningModule: StartupModule"));

	FLearningManager::Get().Initialize();

	if (IUECPCoreModule::IsAvailable())
	{
		IUECPCoreModule::Get().SetLearningService(MakeShared<FUECPLearningServiceImpl>());
	}
}

void FUECPLearningModule::ShutdownModule()
{
	UE_LOG(LogUECPLearning, Log, TEXT("FUECPLearningModule: ShutdownModule"));

	if (IUECPCoreModule::IsAvailable())
	{
		IUECPCoreModule::Get().SetLearningService(nullptr);
	}
}

IMPLEMENT_MODULE(FUECPLearningModule, UECPLearning)
