// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace BlueprintAssetTools
{
	UECPBLUEPRINTEXT_API void HandleAddMacroFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPBLUEPRINTEXT_API void HandleDeleteMacroFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPBLUEPRINTEXT_API void HandleAddMacroParameterFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPBLUEPRINTEXT_API void HandleGetMacroSummaryFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPBLUEPRINTEXT_API void HandleImplementBlueprintInterfaceFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPBLUEPRINTEXT_API void HandleUnimplementBlueprintInterfaceFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPBLUEPRINTEXT_API void HandleRenameFunctionFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
}
