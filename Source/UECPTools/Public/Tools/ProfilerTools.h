// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace ProfilerTools
{

	UECPTOOLS_API void HandleProfileProject(
		float DurationSeconds,
		const FString& Channels,
		TFunction<void(const FString&)> ProgressCallback,
		FString& OutJsonString,
		FString& OutError
	);

	UECPTOOLS_API void HandleAnalyzeTrace(
		const FString& TracePath,
		FString& OutJsonString,
		FString& OutError
	);

	UECPTOOLS_API void HandleProfileProjectFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPTOOLS_API void HandleAnalyzeTraceFromArgs (const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
}
