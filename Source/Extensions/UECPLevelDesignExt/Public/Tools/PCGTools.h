// Copyright 2026, BlueprintsLab, All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace PCGTools
{

	UECPLEVELDESIGNEXT_API void HandleCreatePCGGraph(const FString& Name, const FString& SavePath, FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleGetPCGGraphSummary(const FString& GraphPath, FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleAddPCGNode(const FString& GraphPath, const FString& SettingsClass, int32 PosX, int32 PosY, FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleRemovePCGNode(const FString& GraphPath, int32 NodeIndex, FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleConnectPCGNodes(const FString& GraphPath, int32 FromNodeIndex, int32 ToNodeIndex, const FString& FromPin, const FString& ToPin, FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleSetPCGNodeProperty(const FString& GraphPath, int32 NodeIndex, const FString& PropertyName, const TSharedPtr<FJsonObject>& ValueJson, FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleSpawnPCGActor(const FString& GraphPath, const FString& ActorLabel,
		float LocationX, float LocationY, float LocationZ,
		float BoundsX, float BoundsY, float BoundsZ,
		FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleSetPCGActorBounds(const FString& ActorLabel, float BoundsX, float BoundsY, float BoundsZ, FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleGetPCGActorInfo(const FString& ActorLabel, FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleSetPCGMeshSpawner(const FString& GraphPath, int32 NodeIndex, const TArray<FString>& MeshPaths, const TArray<float>& Weights, FString& OutJsonString, FString& OutError);

	// Rotation ranges use FRotator(Pitch, Yaw, Roll): rot_*_z = yaw, rot_*_x = pitch, rot_*_y = roll (pitch/roll 0 = upright).
	UECPLEVELDESIGNEXT_API void HandleSetPCGTransformRandomizer(const FString& GraphPath, int32 NodeIndex,
		float ScaleMin, float ScaleMax, bool bUniformScale,
		float RotMinZ, float RotMaxZ,
		float OffsetMinZ, float OffsetMaxZ,
		float RotMinX, float RotMaxX, float RotMinY, float RotMaxY,
		bool bAbsoluteRotation,
		FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleGeneratePCG(const FString& ActorLabel, bool bForce, bool bForceOverBudget, FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleUpdatePCGActorGraph(const FString& ActorLabel, const FString& GraphPath, bool bGenerate, FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleListPCGNodeTypes(FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleDisconnectPCGNodes(const FString& GraphPath, int32 FromNodeIndex, int32 ToNodeIndex, const FString& FromPin, const FString& ToPin, FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleGetPCGNodeProperties(const FString& GraphPath, int32 NodeIndex, FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleBuildPCGGraph(const FString& GraphPath, const FString& NodesJson, const FString& EdgesJson, bool bClearExisting, FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleSetPCGSlopeFilter(const FString& GraphPath, int32 NormalDensityNodeIndex, int32 DensityFilterNodeIndex, float MaxSlopeAngleDegrees, bool bInvert, FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleSetPCGNoiseDensity(const FString& GraphPath, int32 NodeIndex, float Scale, float Brightness, float Contrast, const FString& Mode, int32 Iterations, FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleSetPCGSplineSampler(const FString& GraphPath, int32 NodeIndex, const FString& SamplingMode, float DistanceIncrement, int32 NumSamples, int32 SubdivisionsPerSegment, const FString& Dimension, FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleClearPCGGraph(const FString& GraphPath, FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleSetPCGComponentProperties(const FString& ActorLabel, const TSharedPtr<FJsonObject>& PropertiesJson, FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleDuplicatePCGNode(const FString& GraphPath, int32 SourceNodeIndex, int32 PosX, int32 PosY, FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleSetPCGNodeComment(const FString& GraphPath, int32 NodeIndex, const FString& Comment, bool bPinBubble, FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleAddPCGGraphParameter(const FString& GraphPath, const FString& ParamName, const FString& ParamType, FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleGetPCGGraphParameters(const FString& GraphPath, FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleSetPCGTextureSampler(const FString& GraphPath, int32 NodeIndex, const FString& TexturePath,
		const FString& ColorChannel, float TexelSize, bool bUseAdvancedTiling,
		float TilingX, float TilingY, FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleSetPCGDataFromActor(const FString& GraphPath, int32 NodeIndex,
		const FString& ActorFilter, const FString& ActorSelection, const FString& ActorSelectionTag,
		const FString& ActorSelectionClass, const FString& Mode,
		FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleSetPCGAttributeFilter(const FString& GraphPath, int32 NodeIndex,
		const FString& TargetAttribute, const FString& Operator, bool bUseConstantThreshold,
		const FString& ThresholdAttribute, float ThresholdConstant,
		FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleDuplicatePCGGraph(const FString& SourceGraphPath, const FString& DestName, const FString& DestPath, FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleCreatePCGSubgraphNode(const FString& GraphPath, const FString& SubgraphPath, int32 PosX, int32 PosY, FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleSetPCGSelfPruning(const FString& GraphPath, int32 NodeIndex,
		const FString& PruningType, float RadiusSimilarityFactor, bool bRandomizedPruning,
		FString& OutJsonString, FString& OutError);

	// Wires a Difference node in front of one or more StaticMeshSpawner nodes so vegetation/props
	// don't land inside houses, other PCG output, or along roads. Idempotent across repeat calls.
	UECPLEVELDESIGNEXT_API void HandleSetPCGExclusion(const FString& GraphPath, bool bWorldCollision,
		const TArray<FString>& ActorTags, const TArray<FString>& ActorClasses,
		const TArray<FString>& SplineTags, float SplineWidth, float Margin,
		const TArray<int32>& TargetNodes,
		FString& OutJsonString, FString& OutError);

	// Read-only overview of every PCG actor in the level: bounds, generation state, instance
	// counts, and pairwise overlaps — call before adding another layer.
	UECPLEVELDESIGNEXT_API void HandleGetPCGLevelSummary(FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleAddPCGNodeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleConnectPCGNodesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleSetPCGNodePropertyFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleSetPCGMeshSpawnerFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleCreatePCGGraphFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleGetPCGGraphSummaryFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleRemovePCGNodeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleSpawnPCGActorFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleSetPCGActorBoundsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleGetPCGActorInfoFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleSetPCGTransformRandomizerFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleGeneratePCGFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleUpdatePCGActorGraphFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleListPCGNodeTypesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleDisconnectPCGNodesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleGetPCGNodePropertiesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleBuildPCGGraphFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleSetPCGSlopeFilterFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleSetPCGNoiseDensityFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleSetPCGSplineSamplerFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleClearPCGGraphFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleSetPCGComponentPropertiesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleDuplicatePCGNodeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleSetPCGNodeCommentFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleAddPCGGraphParameterFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleGetPCGGraphParametersFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleSetPCGTextureSamplerFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleSetPCGDataFromActorFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleSetPCGAttributeFilterFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleDuplicatePCGGraphFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleCreatePCGSubgraphNodeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleSetPCGSelfPruningFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleSetPCGExclusionFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleGetPCGLevelSummaryFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleSetPCGCreateAttributeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleSetPCGAttributeMathFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
}
