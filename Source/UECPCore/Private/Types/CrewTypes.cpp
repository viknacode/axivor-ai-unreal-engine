// Copyright 2026, BlueprintsLab, All rights reserved

#include "Types/CrewTypes.h"

namespace UECPCrew
{
	FString NormaliseInstructionText(const FString& Instruction)
	{
		const FString Lower = Instruction.ToLower();
		FString Out;
		Out.Reserve(Lower.Len());
		bool bPendingSpace = false;
		for (const TCHAR C : Lower)
		{
			if (FChar::IsWhitespace(C))
			{
				bPendingSpace = Out.Len() > 0;
				continue;
			}
			if (bPendingSpace)
			{
				Out.AppendChar(TEXT(' '));
				bPendingSpace = false;
			}
			Out.AppendChar(C);
		}
		return Out;
	}

	FString ExtractReportStatus(const FString& HandoffContent)
	{
		static const FString Prefix = TEXT("status=");
		if (!HandoffContent.StartsWith(Prefix)) return FString();
		FString Line = HandoffContent.Mid(Prefix.Len());
		int32 NewlineIdx = INDEX_NONE;
		if (Line.FindChar(TEXT('\n'), NewlineIdx)) Line.LeftInline(NewlineIdx);
		return Line.TrimStartAndEnd().ToLower();
	}

	FString RoleKindToString(ECrewRoleKind Kind)
	{
		switch (Kind)
		{
		case ECrewRoleKind::Supervisor: return TEXT("supervisor");
		case ECrewRoleKind::Worker:     return TEXT("worker");
		case ECrewRoleKind::Verifier:   return TEXT("verifier");
		case ECrewRoleKind::Custom:     return TEXT("custom");
		}
		return TEXT("custom");
	}

	ECrewRoleKind RoleKindFromString(const FString& S)
	{
		const FString Lower = S.ToLower();
		if (Lower == TEXT("supervisor")) return ECrewRoleKind::Supervisor;
		if (Lower == TEXT("worker"))     return ECrewRoleKind::Worker;
		if (Lower == TEXT("verifier"))   return ECrewRoleKind::Verifier;
		return ECrewRoleKind::Custom;
	}

	FString CheckpointStateToString(ECheckpointState S)
	{
		switch (S)
		{
		case ECheckpointState::Pending:    return TEXT("pending");
		case ECheckpointState::InProgress: return TEXT("in_progress");
		case ECheckpointState::Passed:     return TEXT("passed");
		case ECheckpointState::Failed:     return TEXT("failed");
		case ECheckpointState::Skipped:    return TEXT("skipped");
		}
		return TEXT("pending");
	}

	ECheckpointState CheckpointStateFromString(const FString& S)
	{
		const FString Lower = S.ToLower();
		if (Lower == TEXT("pending"))     return ECheckpointState::Pending;
		if (Lower == TEXT("in_progress")) return ECheckpointState::InProgress;
		if (Lower == TEXT("passed"))      return ECheckpointState::Passed;
		if (Lower == TEXT("failed"))      return ECheckpointState::Failed;
		if (Lower == TEXT("skipped"))     return ECheckpointState::Skipped;
		return ECheckpointState::Pending;
	}

	FString HandoffTypeToString(EHandoffType T)
	{
		switch (T)
		{
		case EHandoffType::Instruction:      return TEXT("instruction");
		case EHandoffType::Result:           return TEXT("result");
		case EHandoffType::Question:         return TEXT("question");
		case EHandoffType::Answer:           return TEXT("answer");
		case EHandoffType::CheckpointStatus: return TEXT("checkpoint_status");
		case EHandoffType::System:           return TEXT("system");
		}
		return TEXT("system");
	}

	EHandoffType HandoffTypeFromString(const FString& S)
	{
		const FString Lower = S.ToLower();
		if (Lower == TEXT("instruction"))       return EHandoffType::Instruction;
		if (Lower == TEXT("result"))            return EHandoffType::Result;
		if (Lower == TEXT("question"))          return EHandoffType::Question;
		if (Lower == TEXT("answer"))            return EHandoffType::Answer;
		if (Lower == TEXT("checkpoint_status")) return EHandoffType::CheckpointStatus;
		return EHandoffType::System;
	}

	FString RunStateToString(ECrewRunState S)
	{
		switch (S)
		{
		case ECrewRunState::Draft:                 return TEXT("draft");
		case ECrewRunState::AwaitingPlanApproval:  return TEXT("awaiting_plan_approval");
		case ECrewRunState::Running:               return TEXT("running");
		case ECrewRunState::Paused:                return TEXT("paused");
		case ECrewRunState::Completed:             return TEXT("completed");
		case ECrewRunState::Aborted:               return TEXT("aborted");
		}
		return TEXT("draft");
	}

	ECrewRunState RunStateFromString(const FString& S)
	{
		const FString Lower = S.ToLower();
		if (Lower == TEXT("draft"))                  return ECrewRunState::Draft;
		if (Lower == TEXT("awaiting_plan_approval")) return ECrewRunState::AwaitingPlanApproval;
		if (Lower == TEXT("running"))                return ECrewRunState::Running;
		if (Lower == TEXT("paused"))                 return ECrewRunState::Paused;
		if (Lower == TEXT("completed"))              return ECrewRunState::Completed;
		if (Lower == TEXT("aborted"))                return ECrewRunState::Aborted;
		return ECrewRunState::Draft;
	}
}
