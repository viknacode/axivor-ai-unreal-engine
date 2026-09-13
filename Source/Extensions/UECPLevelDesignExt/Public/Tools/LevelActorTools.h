// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "AssetReferenceTypes.h"

namespace LevelActorTools
{
	UECPLEVELDESIGNEXT_API void HandleGetAllSceneActors(const FString& ClassFilter, FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleSetActorMaterial(const FString& ActorLabel, const FString& MaterialPath, FString& OutError, int32 SlotIndex = -1);

	UECPLEVELDESIGNEXT_API void HandleSetMultipleActorMaterials(const TArray<FString>& ActorLabels, const FString& MaterialPath, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleSetSelectedActors(const TArray<FString>& ActorLabels, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleSpawnActorInLevel(const FString& ActorClass, const FVector& Location, const FRotator& Rotation, const FVector& Scale, const FString& ActorLabel, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleSpawnColorCorrectionRegion(const FString& Shape, const FString& ActorLabel,
		float LocationX, float LocationY, float LocationZ,
		FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleSpawnColorCorrectionWindow(const FString& Shape, const FString& ActorLabel,
		float LocationX, float LocationY, float LocationZ,
		FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleSpawnMultipleActorsInLevel(const TArray<FSpawnRequest>& SpawnRequests, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleAdvancedSpawn(
		int32 Count,
		const FString& ActorClass,
		const FString& BoundingActorLabel,
		const FVector& LocMin,
		const FVector& LocMax,
		const FVector& ScaleMin,
		const FVector& ScaleMax,
		const FRotator& RotMin,
		const FRotator& RotMax,
		FString& OutError
	);

	struct FAdvancedSpawnOptions
	{
		bool bAlignToGround = true;   // line-trace each sample down to ECC_WorldStatic; skip on miss
		bool bAlignToNormal = false;  // orient the actor's up axis along the hit normal (needs bAlignToGround)
		bool bAllowTilt = false;      // permit random pitch/roll from RotMin/RotMax; otherwise yaw-only
		bool bUniformScale = true;    // one random scalar for XYZ (from ScaleMin.X/ScaleMax.X)
		FString SpawnMode = TEXT("actors"); // "actors" | "ism" (single HISM actor, ActorClass must resolve to a StaticMesh)
		FString Label;                // optional label prefix (actors) / holder label (ism)
	};

	UECPLEVELDESIGNEXT_API void HandleAdvancedSpawnEx(
		int32 Count,
		const FString& ActorClass,
		const FString& BoundingActorLabel,
		const FVector& LocMin,
		const FVector& LocMax,
		const FVector& ScaleMin,
		const FVector& ScaleMax,
		const FRotator& RotMin,
		const FRotator& RotMax,
		const FAdvancedSpawnOptions& Options,
		FString& OutJsonString,
		FString& OutError
	);

	UECPLEVELDESIGNEXT_API void HandleSetActorTransform(const FString& ActorLabel,
		bool bSetLoc, const FVector& Loc,
		bool bSetRot, const FRotator& Rot,
		bool bSetScale, const FVector& Scale,
		FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleGetActorDetails(const FString& ActorLabel,
		FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleDeleteActors(const TArray<FString>& ActorLabels,
		FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleDuplicateActor(const FString& ActorLabel,
		const FVector& Offset, const FString& NewLabel,
		FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleSetActorProperty(const FString& ActorLabel,
		const FString& PropertyName, const FString& PropertyValue,
		FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleRenameActor(const FString& OldLabel, const FString& NewLabel,
		FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleGetActorsByClass(const FString& ClassName, FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleGetActorsInBox(const FVector& Center, const FVector& Extent, FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleGetActorComponents(const FString& ActorLabel, FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleGetLevelInfo(FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleSetActorVisibility(const TArray<FString>& ActorLabels, bool bVisible, FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleBulkTransformActors(const TArray<TSharedPtr<FJsonValue>>& Actors, FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleAlignActorsToFloor(const TArray<FString>& ActorLabels, float TraceOffset, FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleAttachActor(const FString& ActorLabel, const FString& ParentLabel, bool bKeepWorldTransform, const FString& SocketName, FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleDetachActor(const FString& ActorLabel, FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleMoveActorToFolder(const TArray<FString>& ActorLabels, const FString& FolderPath, FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleSetStaticMesh(const FString& ActorLabel, const FString& MeshPath, FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleSetActorPhysics(const FString& ActorLabel, bool bSimulate,
		bool bSetGravity, bool bEnableGravity,
		float Mass, float LinearDamping, float AngularDamping,
		FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleSetLightProperty(const FString& ActorLabel, const TSharedPtr<FJsonObject>& Params, FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleAddActorTag(const FString& ActorLabel, const FString& Tag, FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleRemoveActorTag(const FString& ActorLabel, const FString& Tag, FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleGetActorTags(const FString& ActorLabel, FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleSetWorldSettings(const TSharedPtr<FJsonObject>& Params, FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleFocusViewportOnActor(const FString& ActorLabel, FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleReplaceActor(const FString& ActorLabel, const FString& NewClassOrMesh, FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleFindActorsByBounds(const FVector& Center, const FVector& Extent,
		FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleBeginPlayInEditor(FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleStopPlayInEditor(FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleGetAssetReferences(const FString& AssetPath, FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleCreateDataLayer(const FString& LayerName, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleAssignActorToDataLayer(const FString& ActorLabel, const FString& LayerName, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleSetDataLayerState(const FString& LayerName, const FString& State, FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleListDataLayers(FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleGetDataLayerActors(const FString& LayerName, FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleEnableWorldPartition(FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleDisableWorldPartition(FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleGetWorldPartitionInfo(FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleCreateHLODLayer(const FString& Name, const FString& SavePath,
		const FString& LayerType, int32 CellSize,
		FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleSetHLODLayerProperties(const FString& AssetPath,
		const FString& LayerType, int32 CellSize,
		FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleListHLODLayers(FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleCreateLevelInstance(const FString& ActorLabel, const FString& LevelPath,
		float LocationX, float LocationY, float LocationZ,
		FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleGetLevelInstances(FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleSetActorsPropertyByFilter(const FString& ClassFilter,
		const FString& PropertyName, const FString& PropertyValue,
		FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleSetActorReplication(
		const FString& ActorLabel,
		const FString& Replicate,
		const FString& ReplicateMovement,
		float NetUpdateFrequency,
		float NetCullDistance,
		FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleSetCineCameraProperties(
		const FString& ActorLabel,
		float FocalLength,
		float Aperture,
		float FocusDistance,
		float FilmbackWidth,
		float FilmbackHeight,
		FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleSpawnActorFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleSpawnColorCorrectionRegionFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleSpawnColorCorrectionWindowFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleDeleteActorFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleDuplicateActorFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleSetActorTransformFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleSetActorPropertyFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleAttachActorFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleAddActorTagFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleRemoveActorTagFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleSetLightPropertyFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleSetActorMaterialsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleGetAllSceneActorsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleGetSelectedLevelActorsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleSelectActorsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleGetActorDetailsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleRenameActorFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleGetActorsByClassFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleGetActorsInBoxFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleGetActorComponentsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleGetLevelInfoFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleSetActorVisibilityFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleBulkTransformActorsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleAlignActorsToFloorFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleDetachActorFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleMoveActorToFolderFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleSetStaticMeshFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleSetActorPhysicsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleGetActorTagsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleSetWorldSettingsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleFocusViewportOnActorFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleReplaceActorFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleSetActorReplicationFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleFindActorsByBoundsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleBeginPlayInEditorFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleStopPlayInEditorFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleOpenLevelFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleSaveCurrentLevelFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleSaveCurrentLevelAsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleGetAssetReferencesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleCreateDataLayerFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleAssignActorToDataLayerFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleSetDataLayerStateFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleListDataLayersFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleGetDataLayerActorsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleCreateLevelInstanceFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleGetLevelInstancesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleEnableWorldPartitionFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleDisableWorldPartitionFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleGetWorldPartitionInfoFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleCreateHLODLayerFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleSetHLODLayerPropertiesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleListHLODLayersFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleSetActorsPropertyByFilterFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleSetCineCameraPropertiesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleSetSelectedActorsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleAdvancedSpawnFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
}
