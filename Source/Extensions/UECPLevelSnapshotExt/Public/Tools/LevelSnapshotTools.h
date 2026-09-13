// Copyright 2026, BlueprintsLab, All rights reserved.

#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace LevelSnapshotTools
{

	UECPLEVELSNAPSHOTEXT_API void HandleTakeLevelSnapshot(const TSharedPtr<FJsonObject>& Args,
		FString& OutJsonString, FString& OutError);

	UECPLEVELSNAPSHOTEXT_API void HandleRestoreLevelSnapshot(const TSharedPtr<FJsonObject>& Args,
		FString& OutJsonString, FString& OutError);

	UECPLEVELSNAPSHOTEXT_API void HandleListLevelSnapshots(FString& OutJsonString, FString& OutError);
	UECPLEVELSNAPSHOTEXT_API void HandleListLevelSnapshotsFromArgs(const TSharedPtr<FJsonObject>& Args,
		FString& OutJsonString, FString& OutError);

	UECPLEVELSNAPSHOTEXT_API void HandleDeleteLevelSnapshotFromArgs(const TSharedPtr<FJsonObject>& Args,
		FString& OutJsonString, FString& OutError);

	UECPLEVELSNAPSHOTEXT_API void HandleCompareSnapshotToWorld(const TSharedPtr<FJsonObject>& Args,
		FString& OutJsonString, FString& OutError);

	UECPLEVELSNAPSHOTEXT_API void HandleRestoreSpecificActors(const TSharedPtr<FJsonObject>& Args,
		FString& OutJsonString, FString& OutError);
}
