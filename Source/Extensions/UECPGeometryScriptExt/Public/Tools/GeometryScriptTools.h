// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace GeometryScriptTools
{
	UECPGEOMETRYSCRIPTEXT_API void HandleSpawnDynamicMeshActorFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPGEOMETRYSCRIPTEXT_API void HandleAppendBoxFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPGEOMETRYSCRIPTEXT_API void HandleAppendSphereFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPGEOMETRYSCRIPTEXT_API void HandleAppendCylinderFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPGEOMETRYSCRIPTEXT_API void HandleAppendConeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPGEOMETRYSCRIPTEXT_API void HandleCopyMeshFromStaticMeshFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPGEOMETRYSCRIPTEXT_API void HandleApplyBooleanOperationFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPGEOMETRYSCRIPTEXT_API void HandleBakeToStaticMeshFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPGEOMETRYSCRIPTEXT_API void HandleClearMeshFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPGEOMETRYSCRIPTEXT_API void HandleGetMeshInfoFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
}
