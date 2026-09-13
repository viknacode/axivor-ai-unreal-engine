// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace BlueprintDeletionTools
{
	UECPBLUEPRINTEXT_API void HandleDeleteVariable(const FString& BpPath, const FString& VarName, FString& OutError);

	UECPBLUEPRINTEXT_API void HandleDeleteComponent(const FString& BpPath, const FString& ComponentName, FString& OutError);

	UECPBLUEPRINTEXT_API void HandleDeleteNodes(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPBLUEPRINTEXT_API void HandleDeleteUnusedVariables(const FString& BpPath, FString& OutJsonString, FString& OutError);

	UECPBLUEPRINTEXT_API void HandleDeleteFunction(const FString& BpPath, const FString& FunctionName, FString& OutError);
	UECPBLUEPRINTEXT_API void HandleDeleteFunctionWithForce(const FString& BpPath, const FString& FunctionName, bool bForce, FString& OutError);

	UECPBLUEPRINTEXT_API void HandleDeleteVariableFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPBLUEPRINTEXT_API void HandleDeleteFunctionFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPBLUEPRINTEXT_API void HandleDeleteComponentFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPBLUEPRINTEXT_API void HandleDeleteUnusedVariablesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
}
