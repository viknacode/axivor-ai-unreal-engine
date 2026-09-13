// Copyright 2026, BlueprintsLab, All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace MassEntityTools
{
	UECPMASSENTITYEXT_API void HandleCreateMassEntityConfigFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPMASSENTITYEXT_API void HandleAddMassTraitFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPMASSENTITYEXT_API void HandleSetMassTraitPropertyFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPMASSENTITYEXT_API void HandleGetMassEntityConfigSummaryFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPMASSENTITYEXT_API void HandlePlaceMassSpawnerFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPMASSENTITYEXT_API void HandleConfigureMassSpawnerFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPMASSENTITYEXT_API void HandleAddMassAgentComponentFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPMASSENTITYEXT_API void HandleListMassTraitClassesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPMASSENTITYEXT_API void HandleValidateMassEntityConfigFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPMASSENTITYEXT_API void HandleRemoveMassTraitFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPMASSENTITYEXT_API void HandleGetMassSpawnerSummaryFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPMASSENTITYEXT_API void HandleSetSpawnerEntityTypesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
}
