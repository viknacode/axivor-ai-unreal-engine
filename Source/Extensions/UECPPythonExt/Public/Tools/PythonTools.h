// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace PythonTools
{
	UECPPYTHONEXT_API void HandleExecutePythonFromArgs   (const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPPYTHONEXT_API void HandleGetPythonRecipeFromArgs (const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
}
