// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace AssetVerificationTools
{

	UECPTOOLS_API void HandleVerifyAssetsInFolderFromArgs(
		const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
}
