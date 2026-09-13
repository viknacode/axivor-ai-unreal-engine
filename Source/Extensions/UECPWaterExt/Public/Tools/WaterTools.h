// Copyright 2026, BlueprintsLab, All rights reserved.

#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace WaterTools
{
	UECPWATEREXT_API void HandleCreateWaterBodyRiver(const FString& ActorLabel, const TArray<FVector>& SplinePoints,
		FString& OutJsonString, FString& OutError);

	UECPWATEREXT_API void HandleCreateWaterBodyLake(const FString& ActorLabel, const FVector& Location, float Radius,
		FString& OutJsonString, FString& OutError);

	UECPWATEREXT_API void HandleCreateWaterBodyOcean(const FString& ActorLabel, const FVector& Location,
		FString& OutJsonString, FString& OutError);

	UECPWATEREXT_API void HandleSetWaterMaterial(const FString& ActorLabel, const FString& MaterialPath,
		FString& OutJsonString, FString& OutError);

	UECPWATEREXT_API void HandleSetWaterWaveSettings(const FString& ActorLabel,
		float Amplitude, float WaveLength, float DirectionDeg, float Steepness,
		FString& OutJsonString, FString& OutError);

	UECPWATEREXT_API void HandleGetWaterBodyInfo(const FString& ActorLabel, FString& OutJsonString, FString& OutError);

	UECPWATEREXT_API void HandleSetWaterBodyProperties(const FString& ActorLabel, const FString& PropertyName,
		const FString& PropertyValue, FString& OutJsonString, FString& OutError);

	UECPWATEREXT_API void HandleCreateWaterBodyRiverFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPWATEREXT_API void HandleCreateWaterBodyLakeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPWATEREXT_API void HandleCreateWaterBodyOceanFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPWATEREXT_API void HandleSetWaterMaterialFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPWATEREXT_API void HandleSetWaterWaveSettingsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPWATEREXT_API void HandleGetWaterBodyInfoFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPWATEREXT_API void HandleSetWaterBodyPropertiesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPWATEREXT_API void HandleSetWaterBodySplinePointsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPWATEREXT_API void HandleClearWaterWavesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
}
