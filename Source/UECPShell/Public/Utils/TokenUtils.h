// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace TokenUtils
{
	UECPSHELL_API int32 EstimateFromString(const FString& Text);

	UECPSHELL_API int32 EstimateFromJsonArray(const TArray<TSharedPtr<FJsonValue>>& ConversationHistory);

	UECPSHELL_API int32 EstimateFromStringArray(const TArray<FString>& Strings);
}
