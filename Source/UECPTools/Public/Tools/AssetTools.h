// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace AssetTools
{
	UECPTOOLS_API void HandleGetFocusedContentBrowserPath(FString& OutPath, FString& OutError);

	UECPTOOLS_API void HandleListAssetsInFolder(const FString& FolderPath, FString& OutJsonString, FString& OutError, bool bRecursive = false);

	UECPTOOLS_API void HandleGetSelectedContentBrowserAssets(FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleFindBlueprintsByParent(const FString& ParentClassName, const FString& SearchPath, FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleGetCurrentFolderFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPTOOLS_API void HandleListAssetsInFolderFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPTOOLS_API void HandleGetSelectedAssetsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPTOOLS_API void HandleFindBlueprintsByParentFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPTOOLS_API void HandleGetFocusedContentBrowserPathFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPTOOLS_API void HandleGetSelectedContentBrowserAssetsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleGetSelectedBlueprintPathFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleListExtensionToolsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
}
