// Copyright 2026, BlueprintsLab, All rights reserved.

#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace ObjectPropertyTools
{

	UECPTOOLS_API void HandleListPropertiesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPTOOLS_API void HandleGetPropertiesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPTOOLS_API void HandleSetPropertiesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);
}
