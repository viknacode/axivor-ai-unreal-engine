// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"

class UECPCORE_API FUpdateManager
{
public:
	static FUpdateManager& Get();

	void Initialize();

	void CheckForUpdates(TFunction<void(bool bHasUpdates, int32 NumUpdates)> Callback = nullptr);

	bool IsChecking() const;

	FString GetStatusJson() const;

	void    _InvalidateAssetCache();

	FString LoadCachedRevision(const FString& LogicalName) const;

	static FString ReadCachedPath(const FString& AbsolutePath);

	DECLARE_MULTICAST_DELEGATE_OneParam(FOnAssetUpdated, FString );
	FOnAssetUpdated OnAssetUpdated;

	DECLARE_MULTICAST_DELEGATE(FOnCheckCompleted);
	FOnCheckCompleted OnCheckCompleted;

private:
	FUpdateManager() = default;
};
