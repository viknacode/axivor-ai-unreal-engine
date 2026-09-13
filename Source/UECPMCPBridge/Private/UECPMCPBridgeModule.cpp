// Copyright 2026, BlueprintsLab, All rights reserved

#include "UECPMCPBridgeModule.h"

#include "Auth/InstanceRegistryFile.h"
#include "Auth/McpInfoServiceImpl.h"
#include "Auth/SessionTokenStore.h"
#include "Broker/BrokerLeaseManager.h"
#include "Transport/HttpSseServer.h"

#include "UECPCoreModule.h"

#include "HAL/PlatformProcess.h"
#include "Misc/App.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/DateTime.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY(LogUECPMCPBridge);

void FUECPMCPBridgeModule::StartupModule()
{
	UE_LOG(LogUECPMCPBridge, Log, TEXT("FUECPMCPBridgeModule: StartupModule"));

	FSessionTokenStore::Get().Initialize();

	if (FMcpInfoServiceImpl::ReadLanAccessSetting())
	{
		GConfig->SetString(TEXT("HTTPServer.Listeners"), TEXT("DefaultBindAddress"),
			TEXT("any"), GEngineIni);
	}

	FHttpSseServer* HttpRaw = nullptr;
	TUniquePtr<FHttpSseServer> Http = MakeUnique<FHttpSseServer>();
	int32 BoundHttpPort = 0;
	if (Http->Start())
	{
		BoundHttpPort = Http->GetBoundPort();
		HttpRaw = Http.Get();
		Transports.Add(MoveTemp(Http));
	}

	FInstanceRegistryEntry Entry;
	Entry.ProjectPath  = FPaths::GetProjectFilePath();
	Entry.ProjectName  = FApp::GetProjectName();
	Entry.McpHttpPort  = BoundHttpPort;
	Entry.ProcessId    = (int32)FPlatformProcess::GetCurrentProcessId();
	Entry.StartedAt    = FDateTime::UtcNow().ToIso8601();
	InstanceRegistry::WriteEntry(Entry);

	BrokerLease = MakeUnique<FBrokerLeaseManager>();
	BrokerLease->Initialize();

	if (IUECPCoreModule::IsAvailable())
	{
		IUECPCoreModule::Get().SetMcpInfoService(
			MakeShared<FMcpInfoServiceImpl>(HttpRaw, BrokerLease.Get()));
	}
}

void FUECPMCPBridgeModule::ShutdownModule()
{
	UE_LOG(LogUECPMCPBridge, Log, TEXT("FUECPMCPBridgeModule: ShutdownModule"));

	if (IUECPCoreModule::IsAvailable())
	{
		IUECPCoreModule::Get().SetMcpInfoService(nullptr);
	}

	if (BrokerLease.IsValid())
	{
		BrokerLease->Shutdown();
		BrokerLease.Reset();
	}

	for (TUniquePtr<IMcpTransport>& T : Transports)
	{
		if (T.IsValid()) T->Stop();
	}
	Transports.Reset();

	InstanceRegistry::RemoveEntry();
}

IMPLEMENT_MODULE(FUECPMCPBridgeModule, UECPMCPBridge)
