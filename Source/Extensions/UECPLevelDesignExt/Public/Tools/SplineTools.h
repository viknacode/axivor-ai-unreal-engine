// Copyright 2026, BlueprintsLab, All rights reserved.

#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace SplineTools
{
	UECPLEVELDESIGNEXT_API void HandleAddSplineComponent(const FString& BlueprintPath, const FString& ComponentName,
		bool bClosedLoop, FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleSetSplinePoints(const FString& BlueprintPath, const FString& ComponentName,
		const TArray<FVector>& Points, FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleSpawnSplineActor(const FString& ActorLabel, const FString& SavePath,
		float LocationX, float LocationY, float LocationZ,
		const TArray<FVector>& Points, bool bClosedLoop,
		FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleGetSplineInfo(const FString& BlueprintPath, const FString& ComponentName,
		FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleSetSplinePointTangent(const FString& BlueprintPath, const FString& ComponentName,
		int32 PointIndex, const FVector& ArriveTangent, const FVector& LeaveTangent,
		const FString& TangentType, FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleAddSplinePoint(const FString& BlueprintPath, const FString& ComponentName,
		const FVector& Point, int32 InsertIndex, FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleRemoveSplinePoint(const FString& BlueprintPath, const FString& ComponentName,
		int32 PointIndex, FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleSetSplineProperties(const FString& BlueprintPath, const FString& ComponentName,
		bool bClosedLoop, const FString& SplineType, FString& OutJsonString, FString& OutError);

	// Builds one USplineMeshComponent per spline segment on a LEVEL actor (found by ActorLabel, or the first level
	// instance of BlueprintPath). Components are tagged "AxivorSplineMesh" and replaced on every call.
	UECPLEVELDESIGNEXT_API void HandleSetSplineMesh(const FString& ActorLabel, const FString& BlueprintPath,
		const FString& SplineComponentName, const FString& MeshPath, const FString& ForwardAxis,
		const FString& MaterialPath, bool bCollision, FString& OutJsonString, FString& OutError);

	// RotationMode: align_to_spline (default) | yaw_only | random | none | fixed (uses FixedRotation).
	UECPLEVELDESIGNEXT_API void HandleScatterActorsAlongSpline(const FString& ActorLabel, const FString& MeshPath,
		float Spacing, float RandomOffset, const FString& RotationMode,
		bool bAlignToGround, const FRotator& FixedRotation,
		FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleSetSplineMeshComponentProperties(
		const FString& BlueprintPath, const FString& ComponentName,
		const FString& ForwardAxis,
		float StartOffsetX, float StartOffsetY, float EndOffsetX, float EndOffsetY,
		float StartScaleX, float StartScaleY, float EndScaleX, float EndScaleY,
		float StartRoll, float EndRoll,
		FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleAddSplinePointFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleSetSplinePointTangentFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleAddSplineComponentFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleSetSplinePointsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleSpawnSplineActorFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleGetSplineInfoFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleRemoveSplinePointFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleSetSplinePropertiesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleSetSplineMeshFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleSetSplineMeshComponentPropertiesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleScatterActorsAlongSplineFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
}
