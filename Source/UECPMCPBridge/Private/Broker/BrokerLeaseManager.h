// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Services/IUECPMcpInfoService.h"

class FBrokerHttpServer;
class FInstanceRegistryWatcher;

class FBrokerLeaseManager
{
public:
	FBrokerLeaseManager();
	~FBrokerLeaseManager();

	void Initialize(int32 InBrokerPort = 9876, float InPollIntervalSec = 5.0f);

	void Shutdown();

	bool IsBroker() const { return bIsBroker; }

	int32 GetBoundPort() const;

	int32 GetConfiguredBrokerPort() const { return BrokerPort; }

	FUECPMcpBrokerStateChanged& OnBrokerStateChanged() { return BrokerStateChanged; }

private:
	bool TryBecomeBroker();
	bool OnPollTick(float DeltaTime);
	void StopPoller();

	int32                                       BrokerPort       = 9876;
	float                                       PollIntervalSec  = 5.0f;
	bool                                        bIsBroker        = false;
	bool                                        bShutdown        = false;
	TUniquePtr<FInstanceRegistryWatcher>        Watcher;
	TUniquePtr<FBrokerHttpServer>               Server;
	FTSTicker::FDelegateHandle                  TickerHandle;
	FUECPMcpBrokerStateChanged                  BrokerStateChanged;
};
