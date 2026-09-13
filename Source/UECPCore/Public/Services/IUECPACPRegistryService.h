// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "Services/ACPTypes.h"

DECLARE_DELEGATE_TwoParams(FUECPACPInstallProgress,
	const FString& , float );

DECLARE_DELEGATE_TwoParams(FUECPACPInstallComplete,
	bool , const FUECPACPInstallMarker& );

class UECPCORE_API IUECPACPRegistryService
{
public:
	virtual ~IUECPACPRegistryService() = default;

	virtual const TArray<FUECPACPAgentEntry>& GetAgents() const = 0;

	virtual void RefreshCatalog(bool bForce) = 0;

	virtual bool IsInstalled(const FString& AgentId) const = 0;

	virtual FUECPACPInstallMarker GetInstallMarker(const FString& AgentId) const = 0;

	virtual void InstallAgent(const FString& AgentId,
		FUECPACPInstallProgress OnProgress,
		FUECPACPInstallComplete OnComplete) = 0;

	virtual bool UninstallAgent(const FString& AgentId) = 0;

	virtual TArray<FUECPACPAgentEntry> GetInstalledAgentsFromDisk() const = 0;

	DECLARE_MULTICAST_DELEGATE_OneParam(FOnCatalogChanged, bool );
	virtual FOnCatalogChanged& OnCatalogChanged() = 0;
};
