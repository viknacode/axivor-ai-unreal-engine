// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "Templates/UniquePtr.h"

class IMcpTransport;
class FBrokerLeaseManager;

DECLARE_LOG_CATEGORY_EXTERN(LogUECPMCPBridge, Log, All);

class FUECPMCPBridgeModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:

	TArray<TUniquePtr<IMcpTransport>> Transports;

	TUniquePtr<FBrokerLeaseManager>   BrokerLease;
};
