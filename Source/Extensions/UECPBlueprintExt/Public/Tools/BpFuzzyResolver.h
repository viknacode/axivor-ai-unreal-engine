// Copyright 2026, BlueprintsLab, All rights reserved.

#pragma once

#include "CoreMinimal.h"

namespace BpFuzzyResolver
{

	FString NormalizeForSearch(const FString& Input);

	float ScoreMatch(const FString& Query, const FString& Candidate);

	struct FResolvedCandidate
	{
		FString CandidateKey;
		float Confidence;
	};

	TArray<FResolvedCandidate> RankCandidates(const FString& Query, const TArray<FString>& Candidates, int32 TopN = 3);

	enum class EPreFlightMode : uint8 { Off = 0, Conservative = 1, Aggressive = 2 };

	EPreFlightMode GetPreFlightMode();

	float GetMinConfidence();
}
