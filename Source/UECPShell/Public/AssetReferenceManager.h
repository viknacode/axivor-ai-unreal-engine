// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "AssetReferenceTypes.h"

DECLARE_MULTICAST_DELEGATE(FOnAssetLinksChanged);

class UECPSHELL_API FAssetReferenceManager
{
public:
	static FAssetReferenceManager& Get();

	void LinkAsset(const FString& AssetPath, const FString& DisplayLabel, EAssetRefType Type);
	void UnlinkAsset(const FGuid& LinkId);
	void UnlinkAll();

	TArray<FLinkedAsset> GetLinkedAssets() const;
	FString GetSummaryForAI() const;
	FString FormatAsMarkdown() const;

	FOnAssetLinksChanged OnLinksChanged;

private:
	FAssetReferenceManager();
	~FAssetReferenceManager() = default;

	TArray<FLinkedAsset> LinkedAssets;

	void NotifyChange();
	static FString GenerateAssetSummary(const FString& AssetPath, EAssetRefType Type);
};
