// Copyright 2026, BlueprintsLab, All rights reserved.

#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace PhysicsAssetTools
{

	UECPTOOLS_API void HandleAddBodyFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPTOOLS_API void HandleRemoveBodyFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPTOOLS_API void HandleGetBodyNamesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPTOOLS_API void HandleGetBodyShapesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPTOOLS_API void HandleSetSphereFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPTOOLS_API void HandleSetCapsuleFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPTOOLS_API void HandleSetBoxFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPTOOLS_API void HandleRemoveShapeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPTOOLS_API void HandleSetBodyPhysicsModeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPTOOLS_API void HandleGetBodyPhysicsModeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPTOOLS_API void HandleSetBodyMassScaleFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPTOOLS_API void HandleGetBodyMassScaleFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPTOOLS_API void HandleGetConstraintsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPTOOLS_API void HandleSetConstraintLimitsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPTOOLS_API void HandleRemoveConstraintFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);
}
