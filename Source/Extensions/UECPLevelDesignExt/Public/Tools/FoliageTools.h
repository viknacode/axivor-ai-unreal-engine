// Copyright 2026, BlueprintsLab, All rights reserved.

#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace FoliageTools
{
	UECPLEVELDESIGNEXT_API void HandleAddFoliageType(const FString& StaticMeshPath, const FString& SavePath,
		FString& OutJsonString, FString& OutError);

	// Honours the FoliageType's placement rules (Radius min-distance, GroundSlopeAngle, AlignToNormal/AlignMaxAngle,
	// RandomYaw, RandomPitchAngle, ZOffset, Scaling intervals). MaxInstances caps the candidate count (<=0 -> 2000).
	UECPLEVELDESIGNEXT_API void HandlePaintFoliage(const FString& FoliageTypePath, const FVector& Location,
		float Radius, float Density, int32 MaxInstances, FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleSetFoliageDensity(const FString& FoliageTypePath, float Density,
		float ScaleMin, float ScaleMax, FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleClearFoliage(const FString& FoliageTypePath, const FVector& Location,
		float Radius, FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleSetFoliageTypeProperties(const FString& FoliageTypePath,
		float CullDistanceMin, float CullDistanceMax,
		float ScaleMin, float ScaleMax,
		const FString& CollisionProfile,
		FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleAssignPhysicalMaterialToFoliage(const FString& FoliageTypePath,
		const FString& PhysMatPath,
		FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleGetFoliageSummary(FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleCreateProceduralFoliageSpawner(const FString& Name, const FString& SavePath,
		const TArray<FString>& FoliageTypePaths, FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleSpawnProceduralFoliageVolume(const FString& ActorLabel, const FString& SpawnerPath,
		float LocationX, float LocationY, float LocationZ,
		float ExtentX, float ExtentY, float ExtentZ,
		FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleAddFoliageTypeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandlePaintFoliageFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleSetFoliageDensityFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleClearFoliageFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleSetFoliageTypePropertiesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleAssignPhysicalMaterialToFoliageFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleGetFoliageSummaryFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleCreateProceduralFoliageSpawnerFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleSpawnProceduralFoliageVolumeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
}
