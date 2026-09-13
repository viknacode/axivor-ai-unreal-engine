// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace DependencyGraphTools
{

	UECPTOOLS_API void HandleGetDependencyGraph(
		const FString& RootPath,
		int32 MaxDepth,
		bool bIncludeEngine,
		FString& OutJsonString,
		FString& OutError
	);

	UECPTOOLS_API void HandleGetInheritanceTree(
		const FString& RootClass,
		const FString& FolderFilter,
		FString& OutNomnomlString,
		FString& OutError
	);

	UECPTOOLS_API void HandleGetDependencyGraphFromArgs(
		const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleGetInheritanceTreeFromArgs(
		const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
}
