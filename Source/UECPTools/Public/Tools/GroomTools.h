// Copyright 2026, BlueprintsLab, All rights reserved.

#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace GroomTools
{

	UECPTOOLS_API void HandleGetGroomInfoFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPTOOLS_API void HandleSetGroomLODSettingsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPTOOLS_API void HandleCreateGroomBindingFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPTOOLS_API void HandleSetGroomPhysicsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPTOOLS_API void HandleSetGroomRenderingFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPTOOLS_API void HandleAssignGroomMaterialFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPTOOLS_API void HandleSpawnGroomComponentFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);
}
