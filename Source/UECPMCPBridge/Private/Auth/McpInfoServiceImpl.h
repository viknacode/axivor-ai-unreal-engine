// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "Services/IUECPMcpInfoService.h"

class FBrokerLeaseManager;
class FHttpSseServer;

class FMcpInfoServiceImpl final : public IUECPMcpInfoService
{
public:
	FMcpInfoServiceImpl(const FHttpSseServer* InHttp, const FBrokerLeaseManager* InBroker)
		: Http(InHttp), Broker(InBroker) {}

	virtual int32   GetHttpPort() const override;
	virtual FString GetSessionToken() const override;
	virtual bool    IsLanAccessEnabled() const override;
	virtual void    SetLanAccessEnabled(bool bEnabled) override;
	virtual void    RotateSessionToken() override;
	virtual bool    IsBroker() const override;
	virtual int32   GetBrokerPort() const override;
	virtual int32   GetConfiguredBrokerPort() const override;
	virtual FUECPMcpBrokerStateChanged& OnBrokerStateChanged() override;

	static bool ReadLanAccessSetting();

private:
	const FHttpSseServer*       Http   = nullptr;
	const FBrokerLeaseManager*  Broker = nullptr;
};
