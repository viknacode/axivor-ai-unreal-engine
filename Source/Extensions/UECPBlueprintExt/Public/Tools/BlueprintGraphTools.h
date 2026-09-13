// Copyright 2026, BlueprintsLab, All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace BlueprintGraphTools
{
	UECPBLUEPRINTEXT_API void HandleDiscoverNodes(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPBLUEPRINTEXT_API void HandlePlaceNode(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPBLUEPRINTEXT_API void HandleConnectPins(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPBLUEPRINTEXT_API void HandleSetPinDefault(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPBLUEPRINTEXT_API void HandleRemoveNode(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPBLUEPRINTEXT_API void HandleRemoveNodes(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPBLUEPRINTEXT_API void HandleConnectPinsBulk(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPBLUEPRINTEXT_API void HandleBuildGraph(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPBLUEPRINTEXT_API void HandleBuildGraphFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError, FString& OutSummary);

	UECPBLUEPRINTEXT_API void HandleGetGraphNodes(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPBLUEPRINTEXT_API void HandleDisconnectPins(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPBLUEPRINTEXT_API void HandleArrangeNodes(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPBLUEPRINTEXT_API void HandleAddFunction(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPBLUEPRINTEXT_API void HandleAddFunctionsBulk(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPBLUEPRINTEXT_API void HandleOverrideFunction(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPBLUEPRINTEXT_API void HandleAddTimeline(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPBLUEPRINTEXT_API void HandleClearGraph(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPBLUEPRINTEXT_API void HandleUndoLastClear(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPBLUEPRINTEXT_API void HandleAddBlueprintComment(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPBLUEPRINTEXT_API void HandleDeleteBlueprintComment(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPBLUEPRINTEXT_API void HandleUpdateBlueprintComment(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPBLUEPRINTEXT_API void HandleSetTimelineProperties(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPBLUEPRINTEXT_API void HandleAddTimelineTrack(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPBLUEPRINTEXT_API void HandlePlaceNodeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPBLUEPRINTEXT_API void HandleSetPinDefaultFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPBLUEPRINTEXT_API void HandleGetBlueprintSkeletonFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPBLUEPRINTEXT_API void HandleGetBlueprintSubgraphFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
}
