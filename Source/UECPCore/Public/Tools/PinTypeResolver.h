// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"

struct FEdGraphPinType;

namespace UECPPinTypes
{
	UECPCORE_API bool ResolvePinTypeFromString(const FString& TypeStrRaw, FEdGraphPinType& OutPinType);
}
