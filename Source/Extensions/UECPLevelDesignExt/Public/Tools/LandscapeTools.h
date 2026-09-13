// Copyright 2026, BlueprintsLab, All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace LandscapeTools
{

	UECPLEVELDESIGNEXT_API void HandleCreateLandscape(
		const FString& ActorLabel, const FString& Preset,
		float CenterFlatRadius, float MountainHeight, int32 SizePreset,
		float ScaleXY, float ScaleZ, float LocationX, float LocationY, float LocationZ,
		int32 Octaves, float Lacunarity, float Persistence,
		int32 ErosionIterations, float ErosionStrength, float SeaLevel,
		float IslandFalloff, int32 Seed, int32 Resolution,
		FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleCreateLandscapeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleSetLandscapeMaterial(
		const FString& ActorLabel,
		const FString& MaterialPath,
		FString& OutJsonString,
		FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleGetLandscapeInfo(const FString& ActorLabel, FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleSetLandscapeProperties(const FString& ActorLabel, const FString& PropertyName,
		const FString& PropertyValue, FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleSetLandscapeLOD(const FString& ActorLabel, int32 StaticLightingLOD,
		int32 LODBias, float LODDistanceFactor, FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleExportLandscapeHeightmap(const FString& ActorLabel, const FString& FilePath,
		FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleAddLandscapeLayerInfo(const FString& ActorLabel, const FString& LayerName,
		const FString& SavePath, FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandlePopulateLandscape(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleSetLandscapeMaterialFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleGetLandscapeInfoFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleSetLandscapePropertiesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleSetLandscapeLODFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleExportLandscapeHeightmapFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleAddLandscapeLayerInfoFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
}
