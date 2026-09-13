// Copyright 2026, BlueprintsLab, All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace ChaosMoverTools
{
	UECPCHAOSMOVEREXT_API void HandleSetupChaosMoverCharacterFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPCHAOSMOVEREXT_API void HandleConfigureChaosMoverSettingsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPCHAOSMOVEREXT_API bool CheckChaosPhysicsProjectSettings(TArray<FString>& OutMissing);
}
