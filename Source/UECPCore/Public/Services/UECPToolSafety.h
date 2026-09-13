// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"

enum class EUECPToolSafety : uint8
{
	Read,
	PlanWrite,
	Write,
	Destructive
};

namespace UECPToolSafety
{

	UECPCORE_API void RegisterToolSafety(FName ToolName, EUECPToolSafety Safety);

	UECPCORE_API EUECPToolSafety GetToolSafety(FName ToolName);

	UECPCORE_API bool IsReadOnly(FName ToolName);
	UECPCORE_API bool IsDestructive(FName ToolName);
	UECPCORE_API bool IsPlanModeAllowed(FName ToolName);
}
