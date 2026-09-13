// Copyright 2026, BlueprintsLab, All rights reserved

#include "UECPAiMemoryModule.h"
#include "FUECPAiMemoryCoordinator.h"
#include "AiMemoryManager.h"
#include "UECPCoreModule.h"

DEFINE_LOG_CATEGORY(LogUECPAiMemory);

void FUECPAiMemoryModule::StartupModule()
{
	UE_LOG(LogUECPAiMemory, Log, TEXT("FUECPAiMemoryModule: StartupModule"));

	FAiMemoryManager::Get().LoadManifest();

	if (IUECPCoreModule::IsAvailable())
	{
		AiMemoryCoordinator = MakeShared<FUECPAiMemoryCoordinator>();
		IUECPCoreModule::Get().SetAiMemoryService(AiMemoryCoordinator);
	}
}

void FUECPAiMemoryModule::ShutdownModule()
{
	UE_LOG(LogUECPAiMemory, Log, TEXT("FUECPAiMemoryModule: ShutdownModule"));

	if (IUECPCoreModule::IsAvailable())
	{
		IUECPCoreModule::Get().SetAiMemoryService(nullptr);
	}
	AiMemoryCoordinator.Reset();
}

TSharedPtr<FUECPAiMemoryCoordinator> FUECPAiMemoryModule::GetAiMemoryCoordinator() const
{
	return AiMemoryCoordinator;
}

IMPLEMENT_MODULE(FUECPAiMemoryModule, UECPAiMemory)
