// Copyright 2026, BlueprintsLab, All rights reserved

#include "Runtime/RoleContextAssembler.h"
#include "Types/CrewTypes.h"

namespace
{
	const FCrewCheckpoint* FindCheckpoint(const FCrewRun& Run, const FString& CheckpointId)
	{
		if (CheckpointId.IsEmpty()) return nullptr;
		for (const FCrewCheckpoint& C : Run.Plan)
		{
			if (C.CheckpointId == CheckpointId) return &C;
		}
		return nullptr;
	}

	const FCrewRole* FindOrchestrator(const FCrewRun& Run)
	{
		for (const FCrewRole& R : Run.Roles)
		{
			if (R.bIsOrchestrator) return &R;
		}
		return nullptr;
	}

	FString CheckpointStateGlyph(ECheckpointState S)
	{
		switch (S)
		{
		case ECheckpointState::Passed:     return TEXT("[x]");
		case ECheckpointState::InProgress: return TEXT("[>]");
		case ECheckpointState::Failed:     return TEXT("[!]");
		case ECheckpointState::Skipped:    return TEXT("[-]");
		case ECheckpointState::Pending:
		default:                           return TEXT("[ ]");
		}
	}

	FString FormatRoleNames(const FCrewRun& Run, const TArray<FString>& RoleIds)
	{
		if (RoleIds.Num() == 0) return FString();
		TStringBuilder<128> Out;
		for (int32 i = 0; i < RoleIds.Num(); ++i)
		{
			const FString* Match = nullptr;
			for (const FCrewRole& R : Run.Roles)
			{
				if (R.RoleId == RoleIds[i]) { Match = &R.Name; break; }
			}
			if (i > 0) Out.Append(TEXT(" → "));
			Out.Append(Match ? *Match : RoleIds[i]);
		}
		return Out.ToString();
	}

	FString FormatPlan(const FCrewRun& Run)
	{
		if (Run.Plan.Num() == 0) return TEXT("  (no checkpoints — empty plan)");

		TStringBuilder<2048> Out;
		for (const FCrewCheckpoint& C : Run.Plan)
		{
			Out.Appendf(TEXT("  %s %s — %s"),
				*CheckpointStateGlyph(C.State),
				*C.CheckpointId,
				*C.Description);
			if (!C.SuccessCriteria.IsEmpty())
			{
				Out.Appendf(TEXT("\n      success: %s"), *C.SuccessCriteria);
			}
			if (C.RequiredRoleIds.Num() > 0)
			{
				Out.Appendf(TEXT("\n      required dispatches (in order): %s"),
					*FormatRoleNames(Run, C.RequiredRoleIds));
			}
			if (!C.LastReportStatus.IsEmpty())
			{
				Out.Appendf(TEXT("\n      last report: %s%s"),
					*C.LastReportStatus,
					C.VerificationToolCalls.Num() > 0
						? *FString::Printf(TEXT(" (verified by: %s)"), *FString::Join(C.VerificationToolCalls, TEXT(", ")))
						: TEXT(" (no read-back evidence yet)"));
			}
			if (C.RetryCount > 0)
			{
				if (Run.Caps.MaxRetriesPerCheckpoint > 0)
					Out.Appendf(TEXT("\n      retries: %d/%d"), C.RetryCount, Run.Caps.MaxRetriesPerCheckpoint);
				else
					Out.Appendf(TEXT("\n      retries: %d"), C.RetryCount);
			}
			Out.Append(TEXT("\n"));
		}
		return Out.ToString();
	}

	FString FormatTeam(const FCrewRun& Run)
	{
		TStringBuilder<512> Out;
		for (const FCrewRole& R : Run.Roles)
		{
			Out.Appendf(TEXT("  - %s (role_id=%s, kind=%s%s)\n"),
				*R.Name,
				*R.RoleId,
				*UECPCrew::RoleKindToString(R.Kind),
				R.bIsOrchestrator ? TEXT(", orchestrator") : TEXT(""));
		}
		return Out.ToString();
	}
}

namespace UECPCrew::RoleContext
{
	FString BuildOrchestratorOpening(const FCrewRun& Run)
	{
		return FString::Printf(TEXT(
			"[CREW BRIEFING — you are the orchestrator]\n"
			"\n"
			"Crew: %s\n"
			"\n"
			"Your team:\n"
			"%s"
			"\n"
			"Your plan (checkpoint id — description):\n"
			"%s"
			"\n"
			"You are the orchestrator. Your only tool is the `crew` umbrella — do not call any "
			"blueprint, asset, file, or other authoring tools. Dispatch one clear instruction at "
			"a time to a role, wait for the report, then advance the plan.\n"
			"\n"
			"**Default flow per checkpoint:** dispatch to every non-orchestrator role in the team, "
			"in their listed order, before marking the checkpoint pass. For Build & Verify that "
			"means: Worker builds and reports, then Verifier inspects against the success criteria "
			"and reports, then you call checkpoint_pass. Never skip a verifier — the run is not "
			"complete on the worker's word alone.\n"
			"\n"
			"**Required dispatches** are listed under each checkpoint above. If listed, the runtime "
			"will deny checkpoint_pass until every required role has returned a successful report "
			"for that checkpoint id.\n"
			"\n"
			"**Grounding rule (enforced by the runtime):** checkpoint_pass is accepted only when the "
			"reporting role (every required role, if listed) called crew.report_back with "
			"status='success' AND made at least one successful read-only tool call (get_* / list_* / "
			"validate_* / inspect_* …) in that same turn. A report showing STATUS: unreported was "
			"synthesised because the role ended its turn without calling report_back — it can never "
			"satisfy checkpoint_pass; re-dispatch and require an explicit report_back with a "
			"verification= statement. Reports without read-back evidence are claims, not proof.\n"
			"\n"
			"**Retries are capped** (crew.checkpoint_retry, default 3 per checkpoint). Beyond the cap "
			"the checkpoint is marked failed and the run pauses for the user. Re-dispatching the same "
			"instruction repeatedly also pauses the run as an oscillation.\n"
			"\n"
			"**When a role reports fail:** do NOT mark the checkpoint pass, and do NOT immediately "
			"checkpoint_fail. Re-dispatch the Worker with the SPECIFIC reason from the failing report "
			"(quote what was wrong) so it can fix that exact issue, then re-dispatch the Verifier to "
			"re-check. This fix→re-verify loop is the normal path — a first-pass fail is expected, not "
			"a dead end. Cap it: after about 2-3 fix attempts on the SAME checkpoint with no progress, "
			"stop looping — either checkpoint_fail with the blocking reason (the run moves on to the "
			"next checkpoint) or escalate to the user if you're genuinely stuck. Never re-dispatch the "
			"same unchanged instruction twice in a row.\n"
			"\n"
			"Call the crew umbrella as `crew(action='<action>', ...)`. Available actions:\n"
			"  crew(action='dispatch_to', role_id=<id>, instruction=<text>, checkpoint_id=<id>) — send instruction to a role\n"
			"  crew(action='checkpoint_pass', checkpoint_id=<id>, summary=<text>) — mark a checkpoint complete\n"
			"  crew(action='checkpoint_fail', checkpoint_id=<id>, reason=<text>) — mark a checkpoint failed (run continues)\n"
			"  crew(action='checkpoint_retry', checkpoint_id=<id>) — reset a failed checkpoint to pending (capped)\n"
			"  crew(action='complete', summary=<text>) — terminate the run successfully (all checkpoints should be passed first)\n"
			"  crew(action='escalate', question=<text>) — pause and ask the user a question\n"
			"\n"
			"After each dispatch you will receive the role's report as your next user message "
			"([CREW REPORT]). Decide the next action from there. Begin by dispatching the first "
			"instruction now."),
			*Run.DisplayName,
			*FormatTeam(Run),
			*FormatPlan(Run));
	}

	FString BuildFirstInstructionForRole(const FCrewRun& Run, const FCrewRole& Role,
		const FCrewCheckpoint* Checkpoint, const FString& InstructionText)
	{
		const FCrewRole* Orchestrator = FindOrchestrator(Run);
		const FString OrchestratorName = Orchestrator ? Orchestrator->Name : TEXT("Supervisor");

		const TCHAR* KindRules = TEXT("");
		switch (Role.Kind)
		{
		case ECrewRoleKind::Worker:
			KindRules = TEXT(
				"You are a Worker. Execute one instruction at a time, then report back. You "
				"have full authoring tools. When done, report via the `crew` umbrella — NOT "
				"`project_plan`, `working_notes`, `memory`, or any other umbrella. The exact "
				"call is:\n"
				"  crew(action='report_back', checkpoint_id=<id>, status='success'|'fail', summary=<text>, verification=<text>)\n"
				"Before reporting success, READ BACK what you changed with a read-only tool "
				"(get_* / list_* / validate_* …) and name those results in verification=. The "
				"runtime records the read-only tool calls that succeeded in this turn as evidence; "
				"a success report with no such evidence cannot pass the checkpoint. If you end your "
				"turn without calling report_back, the runtime files a status=unreported report that "
				"blocks the checkpoint. Do not advance the plan yourself — that's the orchestrator's job.");
			break;
		case ECrewRoleKind::Verifier:
			KindRules = TEXT(
				"You are a Verifier. You inspect the Worker's output against the checkpoint's "
				"success criteria. You have read-only tools (get_*, list_*, discover_*, find_*, "
				"inspect_*, verify_*, validate_*). Call get_tool_docs(categories=['<umbrella>']) "
				"BEFORE any umbrella action you haven't already used this turn — never guess "
				"action names like 'inspect' or 'validate' (the real ones are get_blueprint_graph, "
				"validate_assets, etc.). If no real action fits the verification, report status='fail' "
				"explaining the gap; don't keep trying variants. Report via the `crew` umbrella — "
				"NOT `project_plan`, `working_notes`, `memory`, or any other umbrella:\n"
				"  crew(action='report_back', checkpoint_id=<id>, status='success'|'fail', summary=<text>, verification=<text>)\n"
				"verification= must name the read-back results (which tool, what it showed) that "
				"support your status. Only successful read-only tool calls made in this turn count as "
				"evidence — a status='success' with none is rejected at checkpoint_pass. Ending your "
				"turn without report_back files a status=unreported report. Do not modify any assets.");
			break;
		case ECrewRoleKind::Custom:
			KindRules = TEXT(
				"You are a custom role. Follow the instruction precisely and report back when "
				"done via the `crew` umbrella (NOT any other umbrella):\n"
				"  crew(action='report_back', checkpoint_id=<id>, status='success'|'fail', summary=<text>, verification=<text>)\n"
				"Read back your result with a read-only tool before reporting success and name it in "
				"verification=; ending your turn without report_back files a status=unreported report "
				"that cannot pass the checkpoint.");
			break;
		default:
			break;
		}

		const FString CheckpointTag = Checkpoint
			? FString::Printf(TEXT("checkpoint=%s"), *Checkpoint->CheckpointId)
			: TEXT("ad-hoc (no checkpoint)");

		const FString CriteriaLine = (Checkpoint && !Checkpoint->SuccessCriteria.IsEmpty())
			? FString::Printf(TEXT("\nSuccess criteria: %s"), *Checkpoint->SuccessCriteria)
			: FString();

		return FString::Printf(TEXT(
			"[CREW BRIEFING — you are %s in crew '%s']\n"
			"\n"
			"%s\n"
			"\n"
			"[CREW INSTRUCTION from %s for %s]%s\n"
			"\n"
			"%s"),
			*Role.Name, *Run.DisplayName,
			KindRules,
			*OrchestratorName, *CheckpointTag,
			*CriteriaLine,
			*InstructionText);
	}

	FString BuildInstructionMessage(const FCrewRun& Run, const FCrewRole& Role,
		const FCrewCheckpoint* Checkpoint, const FString& InstructionText)
	{
		const FCrewRole* Orchestrator = FindOrchestrator(Run);
		const FString OrchestratorName = Orchestrator ? Orchestrator->Name : TEXT("Supervisor");
		const FString CheckpointTag = Checkpoint
			? FString::Printf(TEXT("checkpoint=%s"), *Checkpoint->CheckpointId)
			: TEXT("ad-hoc (no checkpoint)");
		const FString CriteriaLine = (Checkpoint && !Checkpoint->SuccessCriteria.IsEmpty())
			? FString::Printf(TEXT("\nSuccess criteria: %s"), *Checkpoint->SuccessCriteria)
			: FString();

		return FString::Printf(TEXT(
			"[CREW INSTRUCTION from %s for %s]%s\n"
			"\n"
			"%s"),
			*OrchestratorName, *CheckpointTag, *CriteriaLine, *InstructionText);
	}

	FString BuildReportMessage(const FCrewRun& Run, const FCrewRole& FromRole,
		const FCrewCheckpoint* Checkpoint, const FString& Status, const FString& Summary,
		const FString& Verification, const TArray<FString>& VerifiedBy, bool bExplicit)
	{
		const FString CheckpointTag = Checkpoint
			? FString::Printf(TEXT("checkpoint=%s"), *Checkpoint->CheckpointId)
			: TEXT("ad-hoc (no checkpoint)");

		FString StaleBanner;
		if (Checkpoint)
		{
			const bool bResolved = Checkpoint->State == ECheckpointState::Passed
				|| Checkpoint->State == ECheckpointState::Failed
				|| Checkpoint->State == ECheckpointState::Skipped;
			if (bResolved)
			{
				const TCHAR* StatePhrase =
					  Checkpoint->State == ECheckpointState::Passed ? TEXT("passed")
					: Checkpoint->State == ECheckpointState::Failed ? TEXT("failed")
					:                                                 TEXT("skipped");
				const bool bFail = Status.StartsWith(TEXT("fail")) || Status.StartsWith(TEXT("error"));
				if (bFail && Checkpoint->State == ECheckpointState::Passed)
				{
					StaleBanner = FString::Printf(TEXT(
						"[ATTENTION: checkpoint %s is already PASSED but this is a late FAIL report -- a role is "
						"disagreeing after the pass. Act only if it is a genuine regression "
						"(crew(action='checkpoint_retry', checkpoint_id='%s')); otherwise ignore it.]\n\n"),
						*Checkpoint->CheckpointId, *Checkpoint->CheckpointId);
				}
				else
				{
					StaleBanner = FString::Printf(TEXT(
						"[NOTE: checkpoint %s is already %s -- this is a late-arriving / duplicate report. "
						"No action needed; do NOT re-dispatch for it.]\n\n"),
						*Checkpoint->CheckpointId, StatePhrase);
				}
			}
		}

		const FString PlanReminder = FString::Printf(TEXT(
			"\n\nCurrent plan state:\n%s\n"
			"Decide the next action: dispatch again, mark this checkpoint pass/fail, escalate, or complete the run."),
			*FormatPlan(Run));

		const FString VerificationLine = Verification.IsEmpty()
			? FString(TEXT("VERIFICATION: (none provided)"))
			: FString::Printf(TEXT("VERIFICATION: %s"), *Verification);
		const FString EvidenceLine = VerifiedBy.Num() > 0
			? FString::Printf(TEXT("VERIFIED BY (successful read-only tool results this turn): %s"),
				*FString::Join(VerifiedBy, TEXT(", ")))
			: FString(TEXT("VERIFIED BY: (no successful read-only tool result this turn — no read-back evidence)"));

		FString GroundingNote;
		if (!bExplicit)
		{
			GroundingNote = TEXT(
				"\n[NOTE: this report was SYNTHESISED — the role ended its turn without calling "
				"crew.report_back, so its status is 'unreported'. It cannot satisfy checkpoint_pass. "
				"Re-dispatch and require an explicit crew(action='report_back', status=..., summary=..., "
				"verification=...) backed by a read-only tool call.]");
		}
		else if (Status == ReportStatusSuccess && VerifiedBy.Num() == 0)
		{
			GroundingNote = TEXT(
				"\n[NOTE: success claimed with no read-back evidence — checkpoint_pass will be denied on "
				"this report. Re-dispatch and require the role to inspect its result with a read-only "
				"tool before reporting.]");
		}

		return FString::Printf(TEXT(
			"%s[CREW REPORT from %s on %s]\n"
			"STATUS: %s\n"
			"SUMMARY: %s\n"
			"%s\n"
			"%s%s%s"),
			*StaleBanner, *FromRole.Name, *CheckpointTag,
			*Status, *Summary,
			*VerificationLine,
			*EvidenceLine,
			*GroundingNote,
			*PlanReminder);
	}

	FString BuildQuestionMessage(const FCrewRun& Run, const FCrewRole& FromRole,
		const FString& Question)
	{
		return FString::Printf(TEXT(
			"[CREW QUESTION from %s]\n"
			"%s\n\n"
			"Answer by calling crew(action='dispatch_to', role_id='%s', instruction=<your answer>)."),
			*FromRole.Name, *Question, *FromRole.RoleId);
	}
}
