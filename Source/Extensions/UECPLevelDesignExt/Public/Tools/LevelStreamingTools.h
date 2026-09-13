// Copyright 2026, BlueprintsLab, All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace LevelStreamingTools
{

	UECPLEVELDESIGNEXT_API void HandleAddStreamingLevel(const FString& LevelPackagePath, const FString& StreamingType,
		float OffsetX, float OffsetY, float OffsetZ,
		FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleSetStreamingLevelTransform(const FString& LevelPackagePath,
		float OffsetX, float OffsetY, float OffsetZ,
		float RotPitch, float RotYaw, float RotRoll,
		float ScaleX, float ScaleY, float ScaleZ,
		FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleCreateLevelStreamingVolume(const FString& LevelPackagePath,
		float LocX, float LocY, float LocZ,
		float ExtentX, float ExtentY, float ExtentZ,
		const FString& Usage, FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleRemoveStreamingLevel(const FString& LevelPackagePath, FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleGetStreamingLevels(FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleSetStreamingLevelVisibility(const FString& LevelPackagePath, bool bVisible, FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleAddStreamingLevelFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleSetStreamingLevelTransformFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleCreateLevelStreamingVolumeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleRemoveStreamingLevelFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleGetStreamingLevelsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleSetStreamingLevelVisibilityFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleSetStreamingLevelLoadedFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
}
