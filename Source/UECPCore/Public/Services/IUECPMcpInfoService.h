// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "Delegates/Delegate.h"

DECLARE_MULTICAST_DELEGATE(FUECPMcpBrokerStateChanged);

class UECPCORE_API IUECPMcpInfoService
{
public:
	virtual ~IUECPMcpInfoService() = default;

	virtual int32 GetHttpPort() const = 0;

	virtual FString GetSessionToken() const = 0;

	virtual bool IsLanAccessEnabled() const = 0;

	virtual void SetLanAccessEnabled(bool bEnabled) = 0;

	virtual void RotateSessionToken() = 0;

	virtual bool IsBroker() const = 0;

	virtual int32 GetBrokerPort() const = 0;

	virtual int32 GetConfiguredBrokerPort() const = 0;

	virtual FUECPMcpBrokerStateChanged& OnBrokerStateChanged() = 0;
};
