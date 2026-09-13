// Copyright 2026, BlueprintsLab, All rights reserved

#include "BrokerLeaseManager.h"

#include "BrokerHttpServer.h"
#include "InstanceRegistryWatcher.h"
#include "MCPToolsLog.h"
#include "../Util/PortProbe.h"

FBrokerLeaseManager::FBrokerLeaseManager() = default;

FBrokerLeaseManager::~FBrokerLeaseManager()
{
	Shutdown();
}

void FBrokerLeaseManager::Initialize(int32 InBrokerPort, float InPollIntervalSec)
{
	BrokerPort      = InBrokerPort;
	PollIntervalSec = InPollIntervalSec;

	if (!TryBecomeBroker())
	{
		UE_LOG(LogMCPTool, Log, TEXT("Broker: another editor holds port %d; running as worker (polling every %.1fs)"),
			BrokerPort, PollIntervalSec);
		TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateRaw(this, &FBrokerLeaseManager::OnPollTick),
			PollIntervalSec);
	}
}

void FBrokerLeaseManager::Shutdown()
{
	bShutdown = true;
	StopPoller();
	if (Server.IsValid())
	{
		Server->Stop();
		Server.Reset();
	}
	if (Watcher.IsValid())
	{
		Watcher->Stop();
		Watcher.Reset();
	}
	bIsBroker = false;
}

int32 FBrokerLeaseManager::GetBoundPort() const
{
	return Server.IsValid() ? Server->GetBoundPort() : 0;
}

bool FBrokerLeaseManager::TryBecomeBroker()
{
	if (UECP::IsLocalPortHeld(BrokerPort))
	{
		return false;
	}

	TUniquePtr<FInstanceRegistryWatcher> NewWatcher = MakeUnique<FInstanceRegistryWatcher>();
	NewWatcher->Start();

	TUniquePtr<FBrokerHttpServer> NewServer = MakeUnique<FBrokerHttpServer>(BrokerPort, NewWatcher.Get());
	if (!NewServer->Start())
	{
		NewWatcher->Stop();
		return false;
	}

	Watcher   = MoveTemp(NewWatcher);
	Server    = MoveTemp(NewServer);
	bIsBroker = true;
	UE_LOG(LogMCPTool, Log, TEXT("Broker: this editor is now the broker on port %d"), BrokerPort);
	BrokerStateChanged.Broadcast();
	return true;
}

bool FBrokerLeaseManager::OnPollTick(float )
{
	if (bShutdown)        return false;
	if (bIsBroker)        return false;

	if (TryBecomeBroker())
	{
		return false;
	}
	return true;
}

void FBrokerLeaseManager::StopPoller()
{
	if (TickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
		TickerHandle.Reset();
	}
}
