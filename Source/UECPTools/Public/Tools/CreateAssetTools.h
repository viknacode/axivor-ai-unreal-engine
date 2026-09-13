// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace CreateAssetTools
{
	UECPTOOLS_API void HandleCreateAssetFromArgs(
		const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleListCreateAssetTypesFromArgs(
		const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
}
