// Copyright 2026, BlueprintsLab, All rights reserved.

#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace ControlRigAnimLayerTools
{

	UECPCINEMATICSEXT_API void HandleGetAnimLayersFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPCINEMATICSEXT_API void HandleAddAnimLayerFromSelectionFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPCINEMATICSEXT_API void HandleDeleteAnimLayerFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPCINEMATICSEXT_API void HandleDuplicateAnimLayerFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPCINEMATICSEXT_API void HandleMergeAnimLayersFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPCINEMATICSEXT_API void HandleSetLayeredModeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPCINEMATICSEXT_API void HandleIsLayeredControlRigFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPCINEMATICSEXT_API void HandleBakeToControlRigFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPCINEMATICSEXT_API void HandleCollapseAnimLayersFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPCINEMATICSEXT_API void HandleTweenControlRigFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);
}
