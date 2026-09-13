// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace AssetPropertyTools
{

	UECPTOOLS_API void HandleCreatePhysicalMaterial(const FString& Name, const FString& SavePath, float Friction, float Restitution, float Density, const FString& SurfaceType, FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleSetPhysicalMaterialProperties(const FString& PhysMatPath, float Friction, float Restitution, float Density, const FString& SurfaceType, FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleSetStaticMeshProperties(const FString& MeshPath, const FString& EnableNanite, const FString& CollisionComplexity, int32 LodBias, FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleSetTextureProperties(const FString& TexturePath, const FString& SRGB, const FString& CompressionSettings, const FString& MipGenSettings, FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleAssignPhysicalMaterial(const FString& TargetPath, const FString& ComponentName, const FString& PhysMatPath, FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleCreateCurveTable(const FString& Name, const FString& SavePath, FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleAddCurveTableRow(const FString& TablePath, const FString& RowName,
		const TArray<float>& Times, const TArray<float>& Values, FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleAddMeshSocket(const FString& MeshPath, const FString& SocketName,
		const FVector& Location, const FRotator& Rotation, const FVector& Scale,
		const FString& BoneName, FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleRemoveMeshSocket(const FString& MeshPath, const FString& SocketName,
		FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleListMeshSockets(const FString& MeshPath, FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleListSkeletonBones(const FString& MeshPath, FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleGetStaticMeshInfo(const FString& MeshPath, FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleGetTextureInfo(const FString& TexturePath, FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleListMorphTargets(const FString& MeshPath, FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleGetSkeletalMeshInfo(const FString& MeshPath, FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleAssignPhysicsAssetToSkeletalMesh(const FString& MeshPath, const FString& PhysicsAssetPath,
		FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleListClothingAssets(const FString& MeshPath, FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleSplitSkeletalMesh(const FString& MeshPath, const TArray<FString>& BoneNames,
		const FString& OutputPath, float WeightThreshold,
		const TArray<FString>& PartitionNames,
		FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandlePreviewSplitSkeletalMesh(const FString& MeshPath, const TArray<FString>& BoneNames,
		float WeightThreshold, FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleGetClothConfigFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleSetClothConfigFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleAutoGenerateLODs(const FString& MeshPath, int32 LODCount,
		const TArray<float>& ReductionPercents, const TArray<float>& ScreenSizes,
		FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleSetLODScreenSize(const FString& MeshPath, int32 LODIndex, float ScreenSize,
		FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleSetTextureMaxSize(const FString& TexturePath, int32 MaxTextureSize,
		FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleAddMeshSocketFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPTOOLS_API void HandleRemoveMeshSocketFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPTOOLS_API void HandleCreatePhysicalMaterialFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPTOOLS_API void HandleSetPhysicalMaterialPropertiesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPTOOLS_API void HandleSetStaticMeshPropertiesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPTOOLS_API void HandleSetTexturePropertiesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPTOOLS_API void HandleAssignPhysicalMaterialFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPTOOLS_API void HandleCreateCurveTableFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPTOOLS_API void HandleAddCurveTableRowFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPTOOLS_API void HandleListMeshSocketsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPTOOLS_API void HandleListSkeletonBonesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPTOOLS_API void HandleGetStaticMeshInfoFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPTOOLS_API void HandleGetTextureInfoFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPTOOLS_API void HandleListMorphTargetsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPTOOLS_API void HandleGetSkeletalMeshInfoFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPTOOLS_API void HandleAssignPhysicsAssetToSkeletalMeshFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPTOOLS_API void HandleListClothingAssetsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPTOOLS_API void HandleSplitSkeletalMeshFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPTOOLS_API void HandlePreviewSplitSkeletalMeshFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPTOOLS_API void HandleAutoGenerateLODsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPTOOLS_API void HandleSetLODScreenSizeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPTOOLS_API void HandleSetTextureMaxSizeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
}
