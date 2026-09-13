// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"

namespace UECPMountResolver
{

	UECPCORE_API TArray<FString> GetUserContentMounts();

	UECPCORE_API TArray<FString> GetAllContentMounts();

	UECPCORE_API bool IsPathUnderMount(const FString& PackagePath, const TArray<FString>& Mounts);

	UECPCORE_API bool IsValidMountedPath(const FString& PackagePath);
}
