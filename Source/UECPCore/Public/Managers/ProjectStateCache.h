// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once
#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"
#include <atomic>

struct FProjectCacheEntry
{
	FName    Path;
	double   LastTouched   = 0.0;
	int32    RefreshCount  = 0;
	int32    AssetTypeHint = 0;
	bool     bDirty        = false;
};

class UECPCORE_API FProjectStateCache
{
public:
	static FProjectStateCache& Get();

	bool IsScanContextReady() const;

	bool IsAssetContextFresh() const;

	bool IsProjectContextValid() const;

	bool IsStreamContextValid() const;

	bool IsDocCacheCurrent() const;

	FString GetContextWarningMessage(int32 Slot) const;

	int32 GetTrackedCount() const;

	void TouchAsset(FName AssetPath);

	void RefreshAsset(FName AssetPath);

	void ResetTracking();

private:
	FProjectStateCache();

	void EnsureSeeded() const;
	void RecordSample(FName Path, int32 TypeHint) const;

	mutable FCriticalSection                        CacheLock;
	mutable TMap<FName, FProjectCacheEntry>         Tracked;
	mutable double                                  LastSweepTime = 0.0;
	mutable int32                                   SweepCount    = 0;
	mutable std::atomic<bool>                       bSeeded       { false };
};
