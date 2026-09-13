// Copyright 2026, BlueprintsLab, All rights reserved.

#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace PhysicsFoliageTools
{
	UECPTOOLS_API void HandleCreatePhysicsAsset(const FString& SkeletalMeshPath, const FString& SavePath,
		FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleGetPhysicsAssetSummary(const FString& AssetPath,
		FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleAddPhysicsBody(const FString& PhysicsAssetPath, const FString& BoneName,
		const FString& ShapeType, FString& OutJsonString, FString& OutError);
	UECPTOOLS_API void HandleAddPhysicsBodyFromArgs(const TSharedPtr<FJsonObject>& Args,
		FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleSetPhysicsBodyProperties(const FString& PhysicsAssetPath, const FString& BoneName,
		float Mass, float LinearDamping, float AngularDamping, FString& OutJsonString, FString& OutError);
	UECPTOOLS_API void HandleSetPhysicsBodyPropertiesFromArgs(const TSharedPtr<FJsonObject>& Args,
		FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleAddPhysicsConstraint(const FString& PhysicsAssetPath, const FString& Bone1,
		const FString& Bone2, FString& OutJsonString, FString& OutError);
	UECPTOOLS_API void HandleAddPhysicsConstraintFromArgs(const TSharedPtr<FJsonObject>& Args,
		FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleSetCollisionPreset(const FString& BlueprintPath, const FString& ComponentName,
		const FString& PresetName, FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleSetCollisionResponse(const FString& BlueprintPath, const FString& ComponentName,
		const FString& Channel, const FString& Response, FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleGetCollisionInfo(const FString& BlueprintPath, const FString& ComponentName,
		FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleSetCollisionEnabled(const FString& BlueprintPath, const FString& ComponentName,
		const FString& CollisionMode, bool bGenerateOverlapEvents, bool bOverlapSet,
		FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleCreateGeometryCollection(const FString& Name, const FString& SavePath,
		const FString& StaticMeshPath, FString& OutJson, FString& OutError);

	UECPTOOLS_API void HandleSetGeometryCollectionProperties(const FString& AssetPath,
		float DamageThreshold, bool bEnableClustering, int32 MaxClusterLevel,
		FString& OutJson, FString& OutError);

	UECPTOOLS_API void HandleSetPhysicsConstraintProperties(
		const FString& BlueprintPath, const FString& ComponentName,
		const FString& LinearXMotion, const FString& LinearYMotion, const FString& LinearZMotion, float LinearLimit,
		const FString& Swing1Motion, const FString& Swing2Motion, float Swing1Limit, float Swing2Limit,
		const FString& TwistMotion, float TwistLimit,
		FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandlePlaceGCActorFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPTOOLS_API void HandleConfigureGCActorFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPTOOLS_API void HandleGetGCSummaryFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPTOOLS_API void HandleCreateCollisionChannel(const FString& ChannelName, const FString& DefaultResponse,
		bool bIsTraceChannel, FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleListCollisionChannels(FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleCreatePhysicsAssetFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPTOOLS_API void HandleGetPhysicsAssetSummaryFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPTOOLS_API void HandleSetCollisionPresetFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPTOOLS_API void HandleSetCollisionResponseFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPTOOLS_API void HandleGetCollisionInfoFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPTOOLS_API void HandleSetCollisionEnabledFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPTOOLS_API void HandleCreateGeometryCollectionFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPTOOLS_API void HandleSetGeometryCollectionPropertiesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPTOOLS_API void HandleSetPhysicsConstraintPropertiesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPTOOLS_API void HandleCreateCollisionChannelFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPTOOLS_API void HandleListCollisionChannelsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleFractureUniformFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPTOOLS_API void HandleFractureVoronoiFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPTOOLS_API void HandleFractureClusteredFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPTOOLS_API void HandleSetFractureAutoClusterFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPTOOLS_API void HandleGetFractureSummaryFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);
}
