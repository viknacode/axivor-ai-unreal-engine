// Copyright 2026, BlueprintsLab, All rights reserved.

#pragma once

#include "CoreMinimal.h"

namespace BpHandleKnowledge
{

	struct FCompanionRule
	{
		FString TriggerHandle;
		FString RequiredPinName;
		FString DefaultSourceHandle;
		FString DefaultSourcePin;
		FString Reason;
	};

	FString LookupCanonicalPinName(const FString& Handle, const FString& BadPinName);

	bool IsForbiddenInputPin(const FString& Handle, const FString& PinName, FString& OutReason);

	const TMap<FString, FString>& GetHandleRedirects();

	TArray<FCompanionRule> GetCompanionRules(const FString& Handle);

	bool IsKnownPure(const FString& Handle);

	bool IsKnownImpure(const FString& Handle);

	FString CanonicalLibraryFor(const FString& FunctionName);
}
