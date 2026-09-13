// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace UECPScannerQuery
{
	enum class EResponseMode : uint8
	{

		Auto,

		AiOnly,

		IndexOnly,
	};

	struct UECPPROJECTSCANNER_API FResult
	{

		bool bHandled = false;

		FString ResponseMarkdown;

		FString StepInfo;
	};

	UECPPROJECTSCANNER_API FResult TryHandle(const FString& Question, const TSharedPtr<FJsonObject>& Index);

	UECPPROJECTSCANNER_API EResponseMode GetResponseMode();
}
