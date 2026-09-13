// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace PluginTools
{
	UECPTOOLS_API void HandleListPlugins(bool bIncludeEngine, bool bIncludeDirectory, FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleFindPlugin(const FString& SearchPattern, FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleListPluginFiles(const FString& PluginName, const FString& RelativePath, FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleListPluginsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPTOOLS_API void HandleFindPluginFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPTOOLS_API void HandleListPluginFilesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
}
