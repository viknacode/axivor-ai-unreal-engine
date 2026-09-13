// Copyright 2026, BlueprintsLab, All rights reserved.

#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace SequencerWorkflowTools
{

	UECPCINEMATICSEXT_API void HandleCopyBindingsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPCINEMATICSEXT_API void HandlePasteBindingsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPCINEMATICSEXT_API void HandleCopyTracksFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPCINEMATICSEXT_API void HandlePasteTracksFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPCINEMATICSEXT_API void HandleCopySectionsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPCINEMATICSEXT_API void HandlePasteSectionsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPCINEMATICSEXT_API void HandleAddEventTriggerSectionFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPCINEMATICSEXT_API void HandleAddEventRepeaterSectionFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPCINEMATICSEXT_API void HandleSetSectionConditionFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPCINEMATICSEXT_API void HandleGetSectionConditionFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPCINEMATICSEXT_API void HandleSetTrackConditionFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPCINEMATICSEXT_API void HandleGetTrackConditionFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPCINEMATICSEXT_API void HandleSetTrackRowConditionFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPCINEMATICSEXT_API void HandleGetTrackRowConditionFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);
}
