// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "Services/IUECPACPRegistryService.h"
#include "Interfaces/IHttpRequest.h"

class FUECPACPRegistry final : public IUECPACPRegistryService
{
public:
	FUECPACPRegistry();
	virtual ~FUECPACPRegistry() override;

	virtual const TArray<FUECPACPAgentEntry>& GetAgents() const override;
	virtual void  RefreshCatalog(bool bForce) override;
	virtual bool  IsInstalled(const FString& AgentId) const override;
	virtual FUECPACPInstallMarker GetInstallMarker(const FString& AgentId) const override;
	virtual void  InstallAgent(const FString& AgentId,
		FUECPACPInstallProgress OnProgress,
		FUECPACPInstallComplete OnComplete) override;
	virtual bool  UninstallAgent(const FString& AgentId) override;
	virtual TArray<FUECPACPAgentEntry> GetInstalledAgentsFromDisk() const override;
	virtual FOnCatalogChanged& OnCatalogChanged() override { return CatalogChangedDelegate; }

private:
	FOnCatalogChanged CatalogChangedDelegate;
	void LoadCacheFromDisk();
	void SaveCacheToDisk() const;
	bool IsCacheStale() const;
	FString CacheFilePath() const;
	FString InstallMarkerPath(const FString& AgentId) const;

	void OnCatalogResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bOk);
	bool ParseCatalogJson(const FString& JsonText);

	TArray<FUECPACPAgentEntry> Agents;
	FDateTime                  CacheTimestampUtc;
	bool                       bFetchInFlight = false;
};
