// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"

#include "McpToolsCatalog.h"

class FMcpProtocol
{
public:

	static constexpr const TCHAR* ProtocolVersion = TEXT("2025-11-25");

	static constexpr const TCHAR* ServerName    = TEXT("uecp");
	static constexpr const TCHAR* ServerVersion = TEXT("2.0.0");

	static FString HandleMessage(const FString& JsonRpcMessage,
	                             const FMcpInvocationContext& Context = {});
};
