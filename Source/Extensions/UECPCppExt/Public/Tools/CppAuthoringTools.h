// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace CppAuthoringTools
{

	UECPCPPEXT_API void HandleCreateUClassFromArgs        (const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPCPPEXT_API void HandleCreateActorFromArgs         (const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPCPPEXT_API void HandleCreateActorComponentFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPCPPEXT_API void HandleCreateUStructFromArgs       (const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPCPPEXT_API void HandleCreateUEnumFromArgs         (const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPCPPEXT_API void HandleCreateUInterfaceFromArgs    (const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
}
