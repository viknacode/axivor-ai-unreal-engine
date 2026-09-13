// Copyright 2026, BlueprintsLab, All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace DataAssetTools
{
	UECPDATAEXT_API void HandleGetDataAssetDetails(const FString& AssetPath, FString& OutDetailsJson, FString& OutError);

	UECPDATAEXT_API void HandleEditDataAssetDefaults(const FString& AssetPath, const TArray<TSharedPtr<FJsonValue>>& Edits, FString& OutError);

	UECPDATAEXT_API void HandleCreateDataAsset(const FString& AssetName, const FString& ClassPath, const FString& SavePath, const TSharedPtr<FJsonObject>& Defaults, FString& OutJsonString, FString& OutError);

	UECPDATAEXT_API void HandleListDataAssetTypes(const FString& Filter, FString& OutJsonString, FString& OutError);

	UECPDATAEXT_API void HandleGetDataAssetDetailsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPDATAEXT_API void HandleEditDataAssetDefaultsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPDATAEXT_API void HandleCreateDataAssetFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPDATAEXT_API void HandleListDataAssetTypesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
}
