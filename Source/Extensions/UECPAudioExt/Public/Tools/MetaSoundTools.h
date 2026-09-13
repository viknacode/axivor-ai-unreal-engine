// Copyright 2026, BlueprintsLab, All rights reserved.

#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace MetaSoundTools
{
	UECPAUDIOEXT_API void HandleCreateMetaSound(const FString& AssetName, const FString& SavePath,
		FString& OutJsonString, FString& OutError);

	UECPAUDIOEXT_API void HandleGetMetaSoundSummary(const FString& AssetPath,
		FString& OutJsonString, FString& OutError);

	UECPAUDIOEXT_API void HandleAddMetaSoundInput(const FString& AssetPath,
		const FString& InputName, const FString& DataType,
		FString& OutJsonString, FString& OutError);

	UECPAUDIOEXT_API void HandleAddMetaSoundOutput(const FString& AssetPath,
		const FString& OutputName, const FString& DataType,
		FString& OutJsonString, FString& OutError);

	UECPAUDIOEXT_API void HandleSetMetaSoundDefaultParameter(const FString& AssetPath,
		const FString& ParameterName, const FString& Value,
		FString& OutJsonString, FString& OutError);

	UECPAUDIOEXT_API void HandleDuplicateMetaSound(const FString& SourcePath, const FString& NewAssetName,
		const FString& SavePath,
		FString& OutJsonString, FString& OutError);

	UECPAUDIOEXT_API void HandleAddMetaSoundNode(const FString& AssetPath, const FString& NodeClassName,
		int32 MajorVersion,
		FString& OutJsonString, FString& OutError);

	UECPAUDIOEXT_API void HandleConnectMetaSoundNodes(const FString& AssetPath,
		const FString& SourceNodeId, const FString& SourceOutputName,
		const FString& DestNodeId, const FString& DestInputName,
		FString& OutJsonString, FString& OutError);

	UECPAUDIOEXT_API void HandleRemoveMetaSoundNode(const FString& AssetPath, const FString& NodeId,
		FString& OutJsonString, FString& OutError);

	UECPAUDIOEXT_API void HandleDisconnectMetaSoundNodes(const FString& AssetPath,
		const FString& SourceNodeId, const FString& SourceOutputName,
		const FString& DestNodeId, const FString& DestInputName,
		FString& OutJsonString, FString& OutError);

	UECPAUDIOEXT_API void HandleListMetaSoundNodeTypes(const FString& Filter, FString& OutJsonString, FString& OutError);

	UECPAUDIOEXT_API void HandleSetMetaSoundNodeInputDefault(const FString& AssetPath, const FString& NodeId,
		const FString& InputName, const FString& Value,
		FString& OutJsonString, FString& OutError);

	UECPAUDIOEXT_API void HandleRemoveMetaSoundInputFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPAUDIOEXT_API void HandleRemoveMetaSoundOutputFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPAUDIOEXT_API void HandleListMetaSoundNodesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPAUDIOEXT_API void HandleCreateMetaSoundFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPAUDIOEXT_API void HandleGetMetaSoundSummaryFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPAUDIOEXT_API void HandleAddMetaSoundInputFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPAUDIOEXT_API void HandleAddMetaSoundOutputFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPAUDIOEXT_API void HandleSetMetaSoundDefaultParameterFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPAUDIOEXT_API void HandleDuplicateMetaSoundFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPAUDIOEXT_API void HandleAddMetaSoundNodeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPAUDIOEXT_API void HandleConnectMetaSoundNodesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPAUDIOEXT_API void HandleRemoveMetaSoundNodeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPAUDIOEXT_API void HandleDisconnectMetaSoundNodesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPAUDIOEXT_API void HandleListMetaSoundNodeTypesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPAUDIOEXT_API void HandleSetMetaSoundNodeInputDefaultFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
}
