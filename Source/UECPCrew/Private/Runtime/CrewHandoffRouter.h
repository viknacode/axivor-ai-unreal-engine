// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"

class FUECPCrewCoordinator;
struct FCrewRun;
struct FCrewRole;

namespace UECPCrew
{

	class FCrewHandoffRouter
	{
	public:
		explicit FCrewHandoffRouter(FUECPCrewCoordinator& InOwner);
		~FCrewHandoffRouter();

		void Bind();

		void Unbind();

		bool OpenRun(const FGuid& RunId, FString& OutReason);

		bool DispatchInstruction(const FGuid& RunId, const FString& FromRoleId,
			const FString& ToRoleId, const FString& Instruction, const FString& CheckpointId,
			FString& OutDenyReason);

		// Files a Result handoff and wakes the orchestrator. bExplicit=true for crew.report_back;
		// false for the turn-end synthesis (which always carries status=unreported). The read-only
		// tool evidence observed in the reporting role's current turn is attached to the handoff.
		bool DeliverReport(const FGuid& RunId, const FString& FromRoleId,
			const FString& CheckpointId, const FString& Status, const FString& Summary,
			const FString& Verification, bool bExplicit, FString& OutDenyReason);

		bool DeliverQuestion(const FGuid& RunId, const FString& FromRoleId,
			const FString& Question, FString& OutDenyReason);

		void ForgetRun(const FGuid& RunId);

	private:

		void HandleTurnEnded(const FString& ChatId, bool bSuccess);

		void SynthesiseAndDeliverReport(const FCrewRun& Run, const FCrewRole& FromRole, bool bSuccess);

		void SendInstructionToRoleChat(const FCrewRun& Run, const FCrewRole& Role,
			const FString& InstructionText, const FString& CheckpointId);

		FUECPCrewCoordinator& Owner;
		FDelegateHandle       TurnEndedHandle;

		TMap<FGuid, TSet<FString>> BriefedRolesByRun;
	};
}
