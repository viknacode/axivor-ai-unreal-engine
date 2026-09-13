// Copyright 2026, BlueprintsLab, All rights reserved.

#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace SequencerOutlinerTools
{

	UECPCINEMATICSEXT_API void HandleAddMarkedFrameFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPCINEMATICSEXT_API void HandleDeleteMarkedFrameFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPCINEMATICSEXT_API void HandleDeleteAllMarkedFramesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPCINEMATICSEXT_API void HandleFindMarkedFrameByLabelFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPCINEMATICSEXT_API void HandleGetMarkedFramesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPCINEMATICSEXT_API void HandleSetMarkedFramesLockedFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPCINEMATICSEXT_API void HandleSetNodeMutedFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPCINEMATICSEXT_API void HandleSetNodeSoloFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPCINEMATICSEXT_API void HandleSetNodePinnedFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPCINEMATICSEXT_API void HandleGetOutlinerStateFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPCINEMATICSEXT_API void HandleSetSectionLockedFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPCINEMATICSEXT_API void HandleSetPlaybackRangeLockedFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPCINEMATICSEXT_API void HandleFocusSubSequenceFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPCINEMATICSEXT_API void HandleFocusParentSequenceFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPCINEMATICSEXT_API void HandleGetSubSequenceHierarchyFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);
}
