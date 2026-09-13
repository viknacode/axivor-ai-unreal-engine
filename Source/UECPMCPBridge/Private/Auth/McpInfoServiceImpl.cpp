// Copyright 2026, BlueprintsLab, All rights reserved

#include "McpInfoServiceImpl.h"

#include "SessionTokenStore.h"
#include "../Broker/BrokerLeaseManager.h"
#include "../Transport/HttpSseServer.h"

#include "Managers/SettingsManager.h"
#include "Misc/ConfigCacheIni.h"

namespace
{
	const TCHAR* kSection = TEXT("BpGeneratorUltimate.MCP");
	const TCHAR* kKey     = TEXT("AllowLanAccess");
}

int32 FMcpInfoServiceImpl::GetHttpPort() const
{
	return Http ? Http->GetBoundPort() : 0;
}

FString FMcpInfoServiceImpl::GetSessionToken() const
{
	return FSessionTokenStore::Get().GetToken();
}

bool FMcpInfoServiceImpl::IsLanAccessEnabled() const
{
	return ReadLanAccessSetting();
}

void FMcpInfoServiceImpl::SetLanAccessEnabled(bool bEnabled)
{
	GConfig->SetBool(kSection, kKey, bEnabled, FSettingsManager::GetGlobalConfigPath());
	GConfig->Flush(false, FSettingsManager::GetGlobalConfigPath());
}

bool FMcpInfoServiceImpl::ReadLanAccessSetting()
{
	bool bEnabled = false;
	GConfig->GetBool(kSection, kKey, bEnabled, FSettingsManager::GetGlobalConfigPath());
	return bEnabled;
}

void FMcpInfoServiceImpl::RotateSessionToken()
{
	FSessionTokenStore::Get().Rotate();
}

bool FMcpInfoServiceImpl::IsBroker() const
{
	return Broker && Broker->IsBroker();
}

int32 FMcpInfoServiceImpl::GetBrokerPort() const
{
	return Broker ? Broker->GetBoundPort() : 0;
}

int32 FMcpInfoServiceImpl::GetConfiguredBrokerPort() const
{
	return Broker ? Broker->GetConfiguredBrokerPort() : 0;
}

FUECPMcpBrokerStateChanged& FMcpInfoServiceImpl::OnBrokerStateChanged()
{
	if (Broker)
	{
		return const_cast<FBrokerLeaseManager*>(Broker)->OnBrokerStateChanged();
	}
	static FUECPMcpBrokerStateChanged Empty;
	return Empty;
}
