// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace FileTools
{
	UECPTOOLS_API void HandleSelectFolder(const FString& FolderPath, FString& OutJsonString, FString& OutError);
	UECPTOOLS_API void HandleSelectFolderFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
}
