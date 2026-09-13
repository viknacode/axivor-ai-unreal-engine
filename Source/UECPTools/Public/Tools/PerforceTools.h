// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace PerforceTools
{

	UECPTOOLS_API void HandleP4ConnectionInfo(FString& OutJson, FString& OutError);

	UECPTOOLS_API void HandleP4OpenedFiles(FString& OutJson, FString& OutError);

	UECPTOOLS_API void HandleP4PendingChangelists(FString& OutJson, FString& OutError);

	UECPTOOLS_API void HandleP4Submit(const FString& Description, const TArray<FString>& Files, FString& OutJson, FString& OutError);

	UECPTOOLS_API void HandleP4Sync(bool bForce, FString& OutJson, FString& OutError);

	UECPTOOLS_API void HandleP4Shelve(const FString& Description, FString& OutJson, FString& OutError);

	UECPTOOLS_API void HandleP4Unshelve(const FString& ChangelistId, FString& OutJson, FString& OutError);

	UECPTOOLS_API void HandleP4Revert(const TArray<FString>& Files, bool bUnchangedOnly, FString& OutJson, FString& OutError);

	UECPTOOLS_API void HandleP4SubmittedChangelists(int32 MaxCount, FString& OutJson, FString& OutError);

	UECPTOOLS_API void HandleP4Checkout(const TArray<FString>& Files, FString& OutJson, FString& OutError);

	UECPTOOLS_API void HandleP4MarkForAdd(const TArray<FString>& Files, FString& OutJson, FString& OutError);
}
