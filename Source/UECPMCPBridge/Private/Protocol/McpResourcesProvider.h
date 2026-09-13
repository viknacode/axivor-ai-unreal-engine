// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace McpResourcesProvider
{

	TSharedRef<FJsonObject> BuildResourcesListResult(const TSharedPtr<FJsonObject>& Params);

	TSharedRef<FJsonObject> HandleResourcesRead(const TSharedPtr<FJsonObject>& Params);
}
