// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace ValidationTools
{
	UECPVALIDATIONEXT_API void HandleValidateAssetFromArgs   (const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPVALIDATIONEXT_API void HandleValidateAssetsFromArgs  (const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPVALIDATIONEXT_API void HandleValidateProjectFromArgs (const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
}
