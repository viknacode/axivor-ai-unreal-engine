// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraphPin.h"

namespace BpGenUtils
{
	UECPSHELL_API FString AssembleTextFormat(const TArray<uint8>& PackedData, const FString& ValidationKey);

	UECPSHELL_API FString PinTypeToString(const FEdGraphPinType& PinType);

	UECPSHELL_API FLinearColor ParseColor(const FString& ColorStr);
}
