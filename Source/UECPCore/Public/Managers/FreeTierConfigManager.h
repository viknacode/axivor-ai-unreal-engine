// Copyright 2026, BlueprintsLab, All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"

struct FFreeTierConfig
{
	bool    bIsBlocked     = false;
	FString BlockMessage;
	bool    bLimitsEnabled = false;

	FString ActiveSlot       = TEXT("slot1");
	FString Slot1Endpoint;
	FString Slot1Model       = TEXT("deepseek-v4-pro");
	int64   Slot1TokenLimit  = 5000000;
	int32   Slot1ResetHours  = 24;
	FString Slot2Endpoint;
	FString Slot2Model       = TEXT("google/gemma-4-26b-a4b-it");
	int64   Slot2TokenLimit  = 300000;
	int32   Slot2ResetHours  = 6;
	TArray<TPair<FString,FString>> Slot2Models;
	FString Slot3Endpoint;
	FString Slot3Model       = TEXT("gemini-2.0-flash");
};

class UECPCORE_API FFreeTierConfigManager
{
public:
	static FFreeTierConfigManager& Get()
	{
		static FFreeTierConfigManager Instance;
		return Instance;
	}

	void Initialize();
	void RefreshConfig();

	bool    IsBlocked()              const;
	FString GetBlockMessage()        const;
	bool    IsLimitsEnabled()        const;
	FString GetProxyEndpoint()       const;
	FString GetServiceRegistrationKey() const;
	FString GetRemoteBaseUrl()       const;

	FString GetActiveSlotEndpoint()  const;
	FString GetActiveSlotModel()     const;
	TArray<TPair<FString,FString>> GetSlot2Models() const;

private:
	FFreeTierConfigManager();
	FFreeTierConfigManager(const FFreeTierConfigManager&) = delete;
	FFreeTierConfigManager& operator=(const FFreeTierConfigManager&) = delete;

	mutable FCriticalSection Lock;
	FFreeTierConfig  CachedConfig;
	FDateTime        LastFetchTime;
	bool             bInitialized  = false;
	bool             bHasValidData = false;

	static constexpr int32 CacheTTLSeconds = 1800;

	void    FetchRemoteConfig();
	void    ProcessResponse(const FString& JsonBody);
	void    SaveCache()  const;
	void    LoadCache();
	bool    IsCacheStale() const;

	static FString ComposeDispatchString(const TArray<uint8>& Encoded, const FString& Key);
	static FString GetCachePath();
};
