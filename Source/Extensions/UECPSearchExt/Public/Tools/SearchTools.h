// Copyright 2026, BlueprintsLab, All rights reserved.

#pragma once

#include "CoreMinimal.h"

namespace SearchTools
{
	UECPSEARCHEXT_API void HandleWebSearch(const FString& Query, int32 Count,
		const FString& Filter, FString& OutJson, FString& OutError);

	UECPSEARCHEXT_API void HandleAnswer(const FString& Query,
		FString& OutJson, FString& OutError);

	UECPSEARCHEXT_API void HandleSearchFromArgs(const TSharedPtr<FJsonObject>& Args,
		FString& OutJson, FString& OutError);
}
