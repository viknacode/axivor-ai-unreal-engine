// Copyright 2026, BlueprintsLab, All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

/**
 * Material graph node handles.
 *
 * Every tool that addresses an expression accepts a node handle string: the expression's
 * MaterialExpressionGuid ("node_id", 32 hex digits, also accepted hyphenated/braced), or a
 * legacy integer index ("3", "n3", "node[3]"). Results always echo node_id + node_index.
 * The int32 overloads below are kept for callers compiled against the index-based API.
 */
namespace MaterialGraphTools
{
	UECPMATERIALEXT_API void HandleConnectMaterialNodes(
		const FString& MaterialPath,
		const FString& FromNode,
		const FString& FromOutput,
		const FString& ToNode,          // empty / "-1" / "material" => material output slot named by ToInput
		const FString& ToInput,
		FString& OutJsonString, FString& OutError);

	UECPMATERIALEXT_API void HandleSetMaterialNodeValue(
		const FString& MaterialPath,
		const FString& NodeHandle,
		const FString& PropertyName,
		const TSharedPtr<FJsonObject>& ValueJson,
		FString& OutJsonString, FString& OutError);

	UECPMATERIALEXT_API void HandleDeleteMaterialNode(
		const FString& MaterialPath,
		const FString& NodeHandle,
		FString& OutJsonString, FString& OutError);

	UECPMATERIALEXT_API void HandleDisconnectMaterialPin(
		const FString& MaterialPath,
		const FString& NodeHandle,
		const FString& InputPinName,
		FString& OutJsonString, FString& OutError);

	UECPMATERIALEXT_API void HandleMoveMaterialNode(
		const FString& MaterialPath,
		const FString& NodeHandle,
		int32 NewPosX, int32 NewPosY,
		FString& OutJsonString, FString& OutError);

	UECPMATERIALEXT_API void HandleDuplicateMaterialNode(
		const FString& MaterialPath,
		const FString& SourceNodeHandle,
		int32 OffsetX, int32 OffsetY,
		FString& OutJsonString, FString& OutError);

	UECPMATERIALEXT_API void HandleAddMaterialNode(
		const FString& MaterialPath,
		const FString& NodeType,
		int32 PosX, int32 PosY,
		const FString& Desc,
		FString& OutJsonString, FString& OutError);

	UECPMATERIALEXT_API void HandleConnectMaterialNodes(
		const FString& MaterialPath,
		int32 FromNodeIndex,
		const FString& FromOutput,
		int32 ToNodeIndex,
		const FString& ToInput,
		FString& OutJsonString, FString& OutError);

	UECPMATERIALEXT_API void HandleSetMaterialNodeValue(
		const FString& MaterialPath,
		int32 NodeIndex,
		const FString& PropertyName,
		const TSharedPtr<FJsonObject>& ValueJson,
		FString& OutJsonString, FString& OutError);

	UECPMATERIALEXT_API void HandleSetMaterialProperty(
		const FString& MaterialPath,
		const TSharedPtr<FJsonObject>& Params,
		FString& OutJsonString, FString& OutError);

	UECPMATERIALEXT_API void HandleGetMaterialNodes(
		const FString& MaterialPath,
		FString& OutJsonString, FString& OutError);

	UECPMATERIALEXT_API void HandleCreateMaterialFunction(
		const FString& Name,
		const FString& SavePath,
		FString& OutJsonString, FString& OutError);

	UECPMATERIALEXT_API void HandleDeleteMaterialNode(
		const FString& MaterialPath,
		int32 NodeIndex,
		FString& OutJsonString, FString& OutError);

	UECPMATERIALEXT_API void HandleDisconnectMaterialPin(
		const FString& MaterialPath,
		int32 NodeIndex,
		const FString& InputPinName,
		FString& OutJsonString, FString& OutError);

	UECPMATERIALEXT_API void HandleMoveMaterialNode(
		const FString& MaterialPath,
		int32 NodeIndex,
		int32 NewPosX, int32 NewPosY,
		FString& OutJsonString, FString& OutError);

	UECPMATERIALEXT_API void HandleDuplicateMaterialNode(
		const FString& MaterialPath,
		int32 SourceNodeIndex,
		int32 OffsetX, int32 OffsetY,
		FString& OutJsonString, FString& OutError);

	UECPMATERIALEXT_API void HandleConnectMaterialNodesBulk(
		const FString& MaterialPath,
		const TArray<TSharedPtr<FJsonValue>>& Connections,
		FString& OutJsonString, FString& OutError);

	UECPMATERIALEXT_API void HandleAutoLayoutMaterial(
		const FString& MaterialPath,
		FString& OutJsonString, FString& OutError);

	UECPMATERIALEXT_API void HandleCreateMaterialLayer(const FString& Name, const FString& SavePath,
		FString& OutJsonString, FString& OutError);

	UECPMATERIALEXT_API void HandleCreateMaterialLayerBlend(const FString& Name, const FString& SavePath,
		FString& OutJsonString, FString& OutError);

	UECPMATERIALEXT_API void HandleFindMaterialNode(
		const FString& MaterialPath,
		const FString& Desc,
		const FString& ClassName,
		const FString& ParameterName,
		FString& OutJsonString, FString& OutError);

	UECPMATERIALEXT_API void HandleAddMaterialNodeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPMATERIALEXT_API void HandleConnectMaterialNodesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPMATERIALEXT_API void HandleSetMaterialNodeValueFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPMATERIALEXT_API void HandleDeleteMaterialNodeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPMATERIALEXT_API void HandleSetMaterialPropertyFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPMATERIALEXT_API void HandleGetMaterialNodesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPMATERIALEXT_API void HandleCreateMaterialFunctionFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPMATERIALEXT_API void HandleDisconnectMaterialPinFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPMATERIALEXT_API void HandleMoveMaterialNodeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPMATERIALEXT_API void HandleDuplicateMaterialNodeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPMATERIALEXT_API void HandleConnectMaterialNodesBulkFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPMATERIALEXT_API void HandleAutoLayoutMaterialFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPMATERIALEXT_API void HandleCreateMaterialLayerFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPMATERIALEXT_API void HandleCreateMaterialLayerBlendFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPMATERIALEXT_API void HandleFindMaterialNodeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPMATERIALEXT_API void HandleGetMaterialGraphSummaryFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPMATERIALEXT_API void HandleRejectMaterialParameterAliasFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPMATERIALEXT_API void HandleValidateMaterialFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPMATERIALEXT_API void HandleGetMaterialCompilationStatsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPMATERIALEXT_API void HandleExportMaterialGraphFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPMATERIALEXT_API void HandleImportMaterialGraphFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
}
