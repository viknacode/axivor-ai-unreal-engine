// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once
#include "CoreMinimal.h"

namespace EditorReadiness
{
	UECPCORE_API bool IsSessionActive();
	UECPCORE_API bool IsGraphContextReady();
	UECPCORE_API bool IsCompilerContextReady();
	UECPCORE_API bool IsMCPContextValid();
	UECPCORE_API FString GetContextDeniedMessage(int32 Idx);
}
