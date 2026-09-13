// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace MeshyTools
{

	UECPASSETGEN_API void HandleCreate3DFromTextFromArgs (const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPASSETGEN_API void HandleCreate3DFromImageFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPASSETGEN_API void HandleMeshyRemeshFromArgs      (const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPASSETGEN_API void HandleMeshyRetextureFromArgs   (const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPASSETGEN_API void HandleMeshyRigModelFromArgs    (const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPASSETGEN_API void HandleMeshyAnimateModelFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPASSETGEN_API void HandleMeshyTextToImageFromArgs (const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPASSETGEN_API void HandleMeshyListAnimationsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPASSETGEN_API void HandleMeshyGetBalanceFromArgs  (const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPASSETGEN_API void HandleMeshyGetTaskStatusFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPASSETGEN_API void HandleMeshyCancelTaskFromArgs  (const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
}
