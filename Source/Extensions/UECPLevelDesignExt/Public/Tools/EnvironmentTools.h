// Copyright 2026, BlueprintsLab, All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace EnvironmentTools
{

	UECPLEVELDESIGNEXT_API void HandleSetSkyLightProperties(const FString& ActorLabel, float Intensity, const FString& LightColor,
		const FString& SourceType, const FString& Mobility, FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleSetExponentialHeightFogProperties(const FString& ActorLabel, float FogDensity, float HeightFalloff,
		const FString& InscatteringColor, float StartDistance, float MaxOpacity, bool bVolumetricFog,
		bool bDensitySet, bool bFalloffSet, bool bStartDistSet, bool bMaxOpacitySet, bool bVolumetricSet,
		FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleSetSkyAtmosphereProperties(const FString& ActorLabel, const FString& PropertyName,
		const FString& PropertyValue, FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleSetVolumetricCloudProperties(const FString& ActorLabel, const FString& PropertyName,
		const FString& PropertyValue, FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleSpawnEnvironmentActor(const FString& ActorType, const FString& ActorLabel,
		float LocationX, float LocationY, float LocationZ,
		FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleGetEnvironmentSummary(FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleSetMPCParameterValue(const FString& AssetPath, const FString& ParamName,
		float ScalarValue, float VectorR, float VectorG, float VectorB, float VectorA,
		bool bIsVector, FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleSetDirectionalLightProperties(const FString& ActorLabel, float Intensity,
		const FString& LightColor, float RotationPitch, float RotationYaw,
		const FString& Mobility, bool bCastShadows,
		bool bIntensitySet, bool bColorSet, bool bRotationSet, bool bMobilitySet, bool bShadowsSet,
		FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleCreateRuntimeVirtualTexture(const FString& Name, const FString& SavePath,
		int32 TileSize, const FString& MaterialType,
		FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleSpawnRVTVolume(const FString& ActorLabel, const FString& RVTAssetPath,
		float LocationX, float LocationY, float LocationZ,
		float ExtentX, float ExtentY, float ExtentZ,
		FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleSetSkyLightPropertiesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleSetExponentialHeightFogPropertiesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleSetSkyAtmospherePropertiesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleSetVolumetricCloudPropertiesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleSpawnEnvironmentActorFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleGetEnvironmentSummaryFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleSetMPCParameterValueFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleSetDirectionalLightPropertiesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleCreateRuntimeVirtualTextureFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleSpawnRVTVolumeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
}
