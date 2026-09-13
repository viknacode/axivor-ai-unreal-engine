// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

#include "../Pipeline/RequestPipeline.h"

struct FMcpInvocationContext
{
	TFunction<bool()> IsTransportAlive;

	FString CallerChatId;

	FString CallerChatToken;
};

namespace McpToolsCatalog
{

	TSharedRef<FJsonObject> BuildToolsListResult();

	TSharedRef<FJsonObject> HandleToolsCall(
		const TSharedPtr<FJsonObject>& CallParams,
		const FMcpInvocationContext& Context);
}
