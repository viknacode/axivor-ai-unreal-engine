// Copyright 2026, BlueprintsLab, All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace ProjectScanTools
{
	UECPTOOLS_API void HandleScanAndIndexProject(FString& OutError);

	UECPTOOLS_API bool QueryProjectIndex(const FString& Query, FString& OutResult, FString& OutError);

	UECPTOOLS_API void HandleScanAndIndexProjectFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleQueryProjectIndexFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
}
