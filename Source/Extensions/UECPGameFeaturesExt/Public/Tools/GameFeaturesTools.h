// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace GameFeaturesTools
{

	UECPGAMEFEATURESEXT_API void HandleCreateGameFeaturePluginFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPGAMEFEATURESEXT_API void HandleAddGameFeatureActionAddComponentsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPGAMEFEATURESEXT_API void HandleAddGameFeatureActionFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPGAMEFEATURESEXT_API void HandleListGameFeatureActionsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPGAMEFEATURESEXT_API void HandleListGameFeaturesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPGAMEFEATURESEXT_API void HandleSetGameFeatureStateFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);
}
