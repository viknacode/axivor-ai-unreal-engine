// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace FolderTools
{
	UECPTOOLS_API void HandleCreateProjectFolder(const FString& FolderPath, FString& OutJsonString, FString& OutError);
}
