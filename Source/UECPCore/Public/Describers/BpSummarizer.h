// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "Describers/BpIssue.h"

class UBlueprint;

class UECPCORE_API FBpSummarizer
{
public:

	FString Summarize(UBlueprint* Blueprint);

	FString Summarize(UBlueprint* Blueprint, int32 MaxChars);

	FString SummarizeWithIssues(UBlueprint* Blueprint, TArray<FBpIssue>& OutIssues);

	int32 ComputeComplexityScore(UBlueprint* Blueprint) const;

	FString ComputeShortSummary(UBlueprint* Blueprint) const;
};
