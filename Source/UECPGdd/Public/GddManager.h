// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "AssetReferenceTypes.h"

class FGddManager
{
public:
	static UECPGDD_API FGddManager& Get();

	TArray<TSharedPtr<FGddFileEntry>>& GetGddFiles() { return GddFiles; }
	const TArray<TSharedPtr<FGddFileEntry>>& GetGddFiles() const { return GddFiles; }

	UECPGDD_API void SaveManifest();
	UECPGDD_API void LoadManifest();

	UECPGDD_API FString GetContentForAI() const;
	UECPGDD_API int32 GetTokenCount() const;

private:
	FGddManager() = default;
	FGddManager(const FGddManager&) = delete;
	FGddManager& operator=(const FGddManager&) = delete;

	TArray<TSharedPtr<FGddFileEntry>> GddFiles;
};
