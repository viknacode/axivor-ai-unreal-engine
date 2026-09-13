// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"

class IAssetRegistry;
struct FAssetData;

class FScannerAutoRefresh
{
public:
	FScannerAutoRefresh();
	~FScannerAutoRefresh();

	FScannerAutoRefresh(const FScannerAutoRefresh&) = delete;
	FScannerAutoRefresh& operator=(const FScannerAutoRefresh&) = delete;

private:
	void OnAssetUpdated(const FAssetData& Data);
	void OnAssetRenamed(const FAssetData& Data, const FString& OldPath);
	void OnAssetRemoved(const FAssetData& Data);

	bool TickDebounce(float DeltaTime);
	void ProcessPending();

	bool IsEnabled() const;
	bool IsBlueprintAsset(const FAssetData& Data) const;

	TSet<FString>     PendingUpdates;
	TSet<FString>     PendingRemovals;
	double            LastChangeTime = 0.0;
	FTSTicker::FDelegateHandle TickerHandle;

	FDelegateHandle   UpdateHandle;
	FDelegateHandle   RenameHandle;
	FDelegateHandle   RemoveHandle;
};
