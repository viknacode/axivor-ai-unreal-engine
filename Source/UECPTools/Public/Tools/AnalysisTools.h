// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"

class FJsonObject;

namespace AnalysisTools
{
	UECPTOOLS_API void HandleGetMaterialGraphSummary(const FString& MaterialPath, FString& OutSummary, FString& OutError);

	UECPTOOLS_API void HandleGetAssetSummary(const FString& AssetPath, FString& OutSummary, FString& OutError);
	UECPTOOLS_API void HandleGetAssetSummaryFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleGetSelectedNodesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
}
