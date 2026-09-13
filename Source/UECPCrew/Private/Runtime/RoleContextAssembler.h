// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"

struct FCrewRun;
struct FCrewRole;
struct FCrewCheckpoint;
struct FCrewHandoff;

namespace UECPCrew::RoleContext
{

	FString BuildOrchestratorOpening(const FCrewRun& Run);

	FString BuildFirstInstructionForRole(const FCrewRun& Run, const FCrewRole& Role,
		const FCrewCheckpoint* Checkpoint, const FString& InstructionText);

	FString BuildInstructionMessage(const FCrewRun& Run, const FCrewRole& Role,
		const FCrewCheckpoint* Checkpoint, const FString& InstructionText);

	// Verification = report_back(verification=...) text; VerifiedBy = successful read-only tool actions
	// observed in the reporting turn; bExplicit=false marks a turn-end synthesised (unreported) report.
	FString BuildReportMessage(const FCrewRun& Run, const FCrewRole& FromRole,
		const FCrewCheckpoint* Checkpoint, const FString& Status, const FString& Summary,
		const FString& Verification, const TArray<FString>& VerifiedBy, bool bExplicit);

	FString BuildQuestionMessage(const FCrewRun& Run, const FCrewRole& FromRole,
		const FString& Question);
}
