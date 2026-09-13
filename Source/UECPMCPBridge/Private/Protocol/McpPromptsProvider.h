// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace McpPromptsProvider
{

	TSharedRef<FJsonObject> BuildPromptsListResult();

	TSharedRef<FJsonObject> HandlePromptsGet(const TSharedPtr<FJsonObject>& Params);
}
