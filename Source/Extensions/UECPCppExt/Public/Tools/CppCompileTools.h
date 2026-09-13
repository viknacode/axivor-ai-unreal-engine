// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace CppCompileTools
{
	UECPCPPEXT_API void HandleCheckProjectSourceFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPCPPEXT_API void HandleCompileProjectFromArgs    (const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
}
