// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"

struct FProviderModel
{
	FString Id;
	FString DisplayName;
	bool    bSupportsVision = true;
};

struct FProviderInfo
{
	FString              ProviderId;
	FString              DisplayName;
	FString              ApiEndpoint;
	FString              ApiFormat;
	TArray<FProviderModel> Models;
	FString              DefaultModel;
	bool                 bIsActive  = true;
	int32                SortOrder  = 0;
};

struct FAgentInfo
{
	FString              AgentId;
	FString              DisplayName;
	FString              DefaultModel;
	TArray<FProviderModel> Models;
	bool                 bIsActive = true;
};

class UECPCORE_API FProviderConfigManager
{
public:
	static FProviderConfigManager& Get();

	void Initialize();
	void RefreshConfig();

	TArray<FString> GetModelsForProvider(const FString& ProviderId) const;

	FString GetDefaultModelForProvider(const FString& ProviderId) const;

	FString GetEndpointForProvider(const FString& ProviderId) const;

	bool IsModelVisionCapable(const FString& ProviderId, const FString& ModelId) const;

	FString GetAgentDefaultModel(const FString& AgentId) const;

	TArray<FString> GetAgentModels(const FString& AgentId) const;

	FString GetSoundGenProvidersJson() const;

	bool HasValidData() const;

private:
	FProviderConfigManager();
	FProviderConfigManager(const FProviderConfigManager&) = delete;
	FProviderConfigManager& operator=(const FProviderConfigManager&) = delete;

	mutable FCriticalSection Lock;
	TArray<FProviderInfo>    CachedProviders;
	TArray<FAgentInfo>       CachedAgents;
	FString                  CachedSoundGenJson;
	FDateTime                LastFetchTime;
	bool                     bInitialized  = false;
	bool                     bHasValidData = false;
	bool                     bFetchInFlight = false;

	static constexpr int32 CacheTTLSeconds = 86400;

	void FetchRemoteConfig();
	void ProcessProviderResponse(const FString& JsonBody);
	void ProcessAgentResponse(const FString& JsonBody);
	void SaveCache() const;
	void LoadCache();
	bool IsCacheStale() const;
	static FString GetCachePath();

	TArray<FProviderInfo> GetHardcodedProviders() const;
	TArray<FAgentInfo>    GetHardcodedAgents() const;
};
