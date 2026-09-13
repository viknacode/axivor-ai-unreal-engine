// Copyright 2026, BlueprintsLab, All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace UserInteractionTools
{

	UECPTOOLS_API void HandleAskUserFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleProceedWithPlanFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
}
