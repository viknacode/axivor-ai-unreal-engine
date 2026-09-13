// Copyright 2026, BlueprintsLab, All rights reserved

#include "Tools/CrewTools.h"
#include "UECPCrewModule.h"
#include "FUECPCrewCoordinator.h"

#include "UECPCoreModule.h"
#include "Services/IUECPArchitectService.h"
#include "Services/IUECPCrewService.h"
#include "Services/IUECPToolDispatcher.h"
#include "Types/CrewTypes.h"
#include "Types/CallerContext.h"
#include "Modules/ModuleManager.h"

#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
	using FResult = FUECPToolResult;

	// The hardening API (verification, evidence, retry cap) is module-internal on the concrete
	// coordinator — IUECPCrewService lives in UECPCore and is not extended.
	FUECPCrewCoordinator* GetCoordinator()
	{
		FUECPCrewModule* Mod = FModuleManager::GetModulePtr<FUECPCrewModule>(TEXT("UECPCrew"));
		return Mod ? Mod->GetCrewCoordinator().Get() : nullptr;
	}

	FResult MakeToolError(const FString& Message)
	{
		FResult R;
		R.bSuccess     = false;
		R.ErrorMessage = Message;
		return R;
	}

	FResult MakeToolOk(const FString& Json)
	{
		FResult R;
		R.bSuccess   = true;
		R.ResultJson = Json;
		return R;
	}

	FString MakeAckJson(const TArray<TPair<FString, FString>>& KV)
	{
		FString Out;
		TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
		W->WriteObjectStart();
		W->WriteValue(TEXT("ok"), true);
		for (const TPair<FString, FString>& P : KV)
		{
			W->WriteValue(P.Key, P.Value);
		}
		W->WriteObjectEnd();
		W->Close();
		return Out;
	}

	struct FCrewCallerInfo
	{
		FGuid    RunId;
		FString  RoleId;
		bool     bIsOrchestrator = false;
		FString  RoleName;
	};

	FString NormaliseReportStatus(const FString& Raw)
	{
		const FString S = Raw.TrimStartAndEnd().ToLower();
		if (S.IsEmpty()) return TEXT("success");
		if (S == TEXT("success") || S == TEXT("pass") || S == TEXT("passed")
		 || S == TEXT("ok")      || S == TEXT("done") || S == TEXT("complete")
		 || S == TEXT("completed"))
		{
			return TEXT("success");
		}
		if (S == TEXT("fail")   || S == TEXT("failed") || S == TEXT("failure")
		 || S == TEXT("error")  || S == TEXT("blocked") || S == TEXT("rejected"))
		{
			return TEXT("fail");
		}
		return TEXT("fail");
	}

	bool ResolveCaller(const TSharedPtr<FJsonObject>& Args, FCrewCallerInfo& Out, FString& OutError)
	{
		const FString CallerChat = UECPCallerContext::ReadCallerChatId(Args);
		if (CallerChat.IsEmpty())
		{
			OutError = TEXT("crew tools must be called from inside a crew role chat (caller chat id missing)");
			return false;
		}
		IUECPCrewService& Crew = IUECPCoreModule::Get().GetCrewService();
		if (!Crew.IsCrewChat(CallerChat, Out.RunId, Out.RoleId))
		{
			OutError = TEXT("caller chat is not a crew role chat");
			return false;
		}
		FCrewRun Run;
		if (!Crew.GetRun(Out.RunId, Run))
		{
			OutError = TEXT("crew run not found");
			return false;
		}
		for (const FCrewRole& R : Run.Roles)
		{
			if (R.RoleId == Out.RoleId)
			{
				Out.bIsOrchestrator = R.bIsOrchestrator;
				Out.RoleName        = R.Name;
				return true;
			}
		}
		OutError = TEXT("caller role id not found in run");
		return false;
	}

	FResult Crew_DispatchTo(const TSharedPtr<FJsonObject>& Args)
	{
		FCrewCallerInfo Caller;
		FString Err;
		if (!ResolveCaller(Args, Caller, Err)) return MakeToolError(Err);
		if (!Caller.bIsOrchestrator)
		{
			return MakeToolError(TEXT("crew.dispatch_to may only be called by the orchestrator role"));
		}

		FString TargetRoleId, Instruction, CheckpointId;
		Args->TryGetStringField(TEXT("role_id"),       TargetRoleId);
		Args->TryGetStringField(TEXT("instruction"),   Instruction);
		Args->TryGetStringField(TEXT("checkpoint_id"), CheckpointId);

		if (TargetRoleId.IsEmpty())  return MakeToolError(TEXT("missing role_id"));
		if (Instruction.IsEmpty())   return MakeToolError(TEXT("missing instruction"));

		FString DenyReason;
		IUECPCrewService& Crew = IUECPCoreModule::Get().GetCrewService();
		if (!Crew.RouteInstructionDispatch(Caller.RunId, Caller.RoleId, TargetRoleId,
				Instruction, CheckpointId, DenyReason))
		{
			return MakeToolError(DenyReason);
		}

		return MakeToolOk(MakeAckJson({
			{ TEXT("dispatched_to"),  TargetRoleId },
			{ TEXT("checkpoint_id"),  CheckpointId },
			{ TEXT("note"),           TEXT("orchestrator should end its turn; the role's report will arrive as the next user message") }
		}));
	}

	FResult Crew_ReportBack(const TSharedPtr<FJsonObject>& Args)
	{
		FCrewCallerInfo Caller;
		FString Err;
		if (!ResolveCaller(Args, Caller, Err)) return MakeToolError(Err);
		if (Caller.bIsOrchestrator)
		{
			return MakeToolError(TEXT("crew.report_back may only be called by a worker / verifier — orchestrators do not report"));
		}

		FString Status   = TEXT("success");
		FString Summary, CheckpointId, Verification;
		Args->TryGetStringField(TEXT("status"),        Status);
		Args->TryGetStringField(TEXT("summary"),       Summary);
		Args->TryGetStringField(TEXT("checkpoint_id"), CheckpointId);
		Args->TryGetStringField(TEXT("verification"),  Verification);
		Verification.TrimStartAndEndInline();

		if (Summary.IsEmpty())
		{
			Summary = (NormaliseReportStatus(Status) == TEXT("success"))
				? TEXT("(role reported success with no summary text)")
				: TEXT("(role reported failure with no summary text)");
		}

		Status = NormaliseReportStatus(Status);

		FUECPCrewCoordinator* Coord = GetCoordinator();
		if (!Coord) return MakeToolError(TEXT("crew coordinator unavailable"));

		const TArray<FString> Evidence =
			Coord->CollectReadOnlyToolEvidence(UECPCallerContext::ReadCallerChatId(Args));

		FString DenyReason;
		if (!Coord->RouteRoleReportVerified(Caller.RunId, Caller.RoleId, CheckpointId, Status, Summary,
				Verification, DenyReason))
		{
			return MakeToolError(DenyReason);
		}

		TArray<TPair<FString, FString>> Ack = {
			{ TEXT("reported_status"),       Status },
			{ TEXT("checkpoint_id"),         CheckpointId },
			{ TEXT("verification_recorded"), Verification.IsEmpty() ? TEXT("no") : TEXT("yes") },
			{ TEXT("verified_by"),           Evidence.Num() > 0 ? FString::Join(Evidence, TEXT(", ")) : FString(TEXT("(none)")) },
		};
		if (Status == UECPCrew::ReportStatusSuccess && Evidence.Num() == 0)
		{
			Ack.Add({ TEXT("warning"),
				TEXT("no successful read-only tool call was observed in this turn, so this success report "
				     "carries no read-back evidence and the orchestrator cannot pass the checkpoint on it. "
				     "Inspect your result with a read-only tool (get_* / list_* / validate_*) and report again.") });
		}
		return MakeToolOk(MakeAckJson(Ack));
	}

	FResult Crew_AskOrchestrator(const TSharedPtr<FJsonObject>& Args)
	{
		FCrewCallerInfo Caller;
		FString Err;
		if (!ResolveCaller(Args, Caller, Err)) return MakeToolError(Err);
		if (Caller.bIsOrchestrator)
		{
			return MakeToolError(TEXT("crew.ask_orchestrator is for workers / verifiers, not the orchestrator itself"));
		}

		FString Question;
		Args->TryGetStringField(TEXT("question"), Question);
		if (Question.IsEmpty()) return MakeToolError(TEXT("missing question"));

		FString DenyReason;
		IUECPCrewService& Crew = IUECPCoreModule::Get().GetCrewService();
		if (!Crew.RouteRoleQuestion(Caller.RunId, Caller.RoleId, Question, DenyReason))
		{
			return MakeToolError(DenyReason);
		}
		return MakeToolOk(MakeAckJson({
			{ TEXT("question_routed"), TEXT("orchestrator") }
		}));
	}

	FResult Crew_Complete(const TSharedPtr<FJsonObject>& Args)
	{
		FCrewCallerInfo Caller;
		FString Err;
		if (!ResolveCaller(Args, Caller, Err)) return MakeToolError(Err);
		if (!Caller.bIsOrchestrator)
		{
			return MakeToolError(TEXT("crew.complete may only be called by the orchestrator"));
		}

		FString Summary;
		Args->TryGetStringField(TEXT("summary"), Summary);

		IUECPCrewService& Crew = IUECPCoreModule::Get().GetCrewService();

		FCrewHandoff H;
		H.FromRoleId = Caller.RoleId;
		H.Type       = EHandoffType::System;
		H.Content    = FString::Printf(TEXT("crew.complete by %s: %s"), *Caller.RoleName, *Summary);
		Crew.AppendHandoff(Caller.RunId, H);

		if (!Crew.CompleteRun(Caller.RunId, Summary))
		{
			return MakeToolError(TEXT("failed to mark run completed"));
		}
		return MakeToolOk(MakeAckJson({ { TEXT("state"), TEXT("completed") } }));
	}

	FResult Crew_Escalate(const TSharedPtr<FJsonObject>& Args)
	{
		FCrewCallerInfo Caller;
		FString Err;
		if (!ResolveCaller(Args, Caller, Err)) return MakeToolError(Err);
		if (!Caller.bIsOrchestrator)
		{
			return MakeToolError(TEXT("crew.escalate may only be called by the orchestrator"));
		}

		FString Question;
		Args->TryGetStringField(TEXT("question"), Question);
		if (Question.IsEmpty()) return MakeToolError(TEXT("missing question"));

		IUECPCrewService& Crew = IUECPCoreModule::Get().GetCrewService();
		FCrewHandoff H;
		H.FromRoleId = Caller.RoleId;
		H.Type       = EHandoffType::Question;
		H.Content    = Question;
		Crew.AppendHandoff(Caller.RunId, H);

		if (!Crew.PauseRun(Caller.RunId, TEXT("user_escalation")))
		{
			return MakeToolError(TEXT("failed to pause run for escalation"));
		}
		return MakeToolOk(MakeAckJson({
			{ TEXT("state"),    TEXT("paused") },
			{ TEXT("question"), Question }
		}));
	}

	FResult Crew_CheckpointPass(const TSharedPtr<FJsonObject>& Args)
	{
		FCrewCallerInfo Caller;
		FString Err;
		if (!ResolveCaller(Args, Caller, Err)) return MakeToolError(Err);
		if (!Caller.bIsOrchestrator)
		{
			return MakeToolError(TEXT("crew.checkpoint_pass may only be called by the orchestrator"));
		}

		FString CheckpointId, Summary;
		Args->TryGetStringField(TEXT("checkpoint_id"), CheckpointId);
		Args->TryGetStringField(TEXT("summary"),       Summary);
		if (CheckpointId.IsEmpty()) return MakeToolError(TEXT("missing checkpoint_id"));

		IUECPCrewService& Crew = IUECPCoreModule::Get().GetCrewService();
		FUECPCrewCoordinator* Coord = GetCoordinator();
		if (!Coord) return MakeToolError(TEXT("crew coordinator unavailable"));

		FCrewRun Run;
		if (!Crew.GetRun(Caller.RunId, Run)) return MakeToolError(TEXT("run not found"));

		FString         GroundedVerification;
		TArray<FString> GroundedEvidence;
		{
			const FCrewCheckpoint* Cp = nullptr;
			for (const FCrewCheckpoint& C : Run.Plan)
				if (C.CheckpointId == CheckpointId) { Cp = &C; break; }
			if (!Cp) return MakeToolError(TEXT("checkpoint not found"));

			if (Cp->DependsOn.Num() > 0)
			{
				TMap<FString, ECheckpointState> StateById;
				for (const FCrewCheckpoint& C : Run.Plan) StateById.Add(C.CheckpointId, C.State);

				TArray<FString> UnmetDeps;
				for (const FString& Dep : Cp->DependsOn)
				{
					const ECheckpointState* DepState = StateById.Find(Dep);
					if (!DepState) continue;
					if (*DepState != ECheckpointState::Passed) UnmetDeps.Add(Dep);
				}
				if (UnmetDeps.Num() > 0)
				{
					FString DepList;
					for (int32 i = 0; i < UnmetDeps.Num(); ++i)
					{
						if (i > 0) DepList += TEXT(", ");
						DepList += UnmetDeps[i];
					}
					return MakeToolError(FString::Printf(
						TEXT("checkpoint_pass denied: '%s' depends on unfinished checkpoint(s): %s — pass them first"),
						*CheckpointId, *DepList));
				}
			}

			// --- grounding gate ---------------------------------------------------------------
			// A pass needs, per role checked, the role's LATEST Result handoff for this checkpoint
			// (since the last retry) to be (a) explicit — filed via crew.report_back, not synthesised
			// at turn end — (b) status=success and (c) backed by at least one successful read-only
			// tool result in that same turn. Roles checked: every live required role when the
			// checkpoint lists them, otherwise the role that reported most recently.
			const int64 RetryFloor = (Cp->RetriedAt.GetTicks() > 0) ? Cp->RetriedAt.ToUnixTimestamp() : 0;

			TMap<FString, FString> RoleNameById;
			for (const FCrewRole& R : Run.Roles) RoleNameById.Add(R.RoleId, R.Name);

			TMap<FString, const FCrewHandoff*> LatestByRole;
			const FCrewHandoff* LatestOverall = nullptr;
			for (const FCrewHandoff& H : Run.Handoffs)
			{
				if (H.Type != EHandoffType::Result) continue;
				if (H.CheckpointId != CheckpointId) continue;
				if (H.TimestampUnix < RetryFloor) continue;
				LatestByRole.Add(H.FromRoleId, &H);
				LatestOverall = &H;
			}

			const bool bHasRequiredRoles = Cp->RequiredRoleIds.Num() > 0;
			TArray<FString> RolesToCheck;
			if (bHasRequiredRoles)
			{
				for (const FString& ReqRoleId : Cp->RequiredRoleIds)
					if (RoleNameById.Contains(ReqRoleId)) RolesToCheck.Add(ReqRoleId);
			}
			else if (LatestOverall)
			{
				RolesToCheck.Add(LatestOverall->FromRoleId);
			}

			TArray<FString> Problems;
			TArray<FString> VerificationTexts;
			if (RolesToCheck.Num() == 0)
			{
				Problems.Add(FString::Printf(
					TEXT("no role has reported on '%s' yet — dispatch a role and wait for its crew.report_back"),
					*CheckpointId));
			}
			for (const FString& RoleId : RolesToCheck)
			{
				const FString* NamePtr = RoleNameById.Find(RoleId);
				const FString  RoleName = NamePtr ? *NamePtr : RoleId;
				const FCrewHandoff* const* Found = LatestByRole.Find(RoleId);
				const FCrewHandoff* H = Found ? *Found : nullptr;
				if (!H)
				{
					Problems.Add(FString::Printf(
						TEXT("%s has not reported on '%s' since the last retry — dispatch it"),
						*RoleName, *CheckpointId));
					continue;
				}
				const FString ReportStatus = UECPCrew::ExtractReportStatus(H->Content);
				if (!H->bExplicitReport || ReportStatus == UECPCrew::ReportStatusUnreported)
				{
					Problems.Add(FString::Printf(
						TEXT("%s's latest report was synthesised (status=unreported): it ended its turn without calling "
						     "crew.report_back — re-dispatch and require crew(action='report_back', status='success', "
						     "summary=..., verification=...)"),
						*RoleName));
					continue;
				}
				if (ReportStatus != UECPCrew::ReportStatusSuccess)
				{
					Problems.Add(FString::Printf(
						TEXT("%s's latest report is status=%s — fix the reported issue and re-dispatch before passing"),
						*RoleName, *ReportStatus));
					continue;
				}
				if (H->VerificationToolCalls.Num() == 0)
				{
					Problems.Add(FString::Printf(
						TEXT("%s reported success but made no successful read-only tool call in that turn (no read-back "
						     "evidence) — re-dispatch and require it to inspect the result with get_* / list_* / "
						     "validate_* tools before reporting"),
						*RoleName));
					continue;
				}
				if (!H->Verification.IsEmpty())
					VerificationTexts.Add(FString::Printf(TEXT("%s: %s"), *RoleName, *H->Verification));
				for (const FString& T : H->VerificationToolCalls) GroundedEvidence.AddUnique(T);
			}

			if (Problems.Num() > 0)
			{
				return MakeToolError(FString::Printf(
					TEXT("checkpoint_pass denied for '%s': %s. Required: an explicit crew.report_back with "
					     "status=success AND at least one successful read-only verification tool result in the "
					     "same turn%s. Success criteria (informational): %s"),
					*CheckpointId,
					*FString::Join(Problems, TEXT("; ")),
					bHasRequiredRoles ? TEXT(", from every required role") : TEXT(""),
					Cp->SuccessCriteria.IsEmpty() ? TEXT("(none given)") : *Cp->SuccessCriteria));
			}
			GroundedVerification = FString::Join(VerificationTexts, TEXT(" | "));
		}

		Coord->RecordReportEvidence(Caller.RunId, CheckpointId, UECPCrew::ReportStatusSuccess,
			GroundedVerification, GroundedEvidence);
		Coord->MarkCheckpointGrounded(Caller.RunId, CheckpointId, true);
		if (!Crew.SetCheckpointState(Caller.RunId, CheckpointId, ECheckpointState::Passed, Summary))
		{
			return MakeToolError(TEXT("checkpoint not found"));
		}
		return MakeToolOk(MakeAckJson({
			{ TEXT("checkpoint_id"), CheckpointId },
			{ TEXT("state"),         TEXT("passed") },
			{ TEXT("grounded"),      TEXT("true") },
			{ TEXT("verified_by"),   FString::Join(GroundedEvidence, TEXT(", ")) }
		}));
	}

	FResult Crew_CheckpointFail(const TSharedPtr<FJsonObject>& Args)
	{
		FCrewCallerInfo Caller;
		FString Err;
		if (!ResolveCaller(Args, Caller, Err)) return MakeToolError(Err);
		if (!Caller.bIsOrchestrator)
		{
			return MakeToolError(TEXT("crew.checkpoint_fail may only be called by the orchestrator"));
		}

		FString CheckpointId, Reason;
		Args->TryGetStringField(TEXT("checkpoint_id"), CheckpointId);
		Args->TryGetStringField(TEXT("reason"),        Reason);
		if (CheckpointId.IsEmpty()) return MakeToolError(TEXT("missing checkpoint_id"));

		IUECPCrewService& Crew = IUECPCoreModule::Get().GetCrewService();
		if (!Crew.SetCheckpointState(Caller.RunId, CheckpointId, ECheckpointState::Failed, Reason))
		{
			return MakeToolError(TEXT("checkpoint not found"));
		}
		return MakeToolOk(MakeAckJson({
			{ TEXT("checkpoint_id"), CheckpointId },
			{ TEXT("state"),         TEXT("failed") }
		}));
	}

	FResult Crew_SystemMessage(const TSharedPtr<FJsonObject>& Args)
	{
		FCrewCallerInfo Caller;
		FString Err;
		if (!ResolveCaller(Args, Caller, Err)) return MakeToolError(Err);
		if (!Caller.bIsOrchestrator)
		{
			return MakeToolError(TEXT("crew.system_message may only be called by the orchestrator — use crew.report_back / crew.ask_orchestrator from worker/verifier roles instead"));
		}

		FString TargetRoleId, Message;
		Args->TryGetStringField(TEXT("role_id"), TargetRoleId);
		Args->TryGetStringField(TEXT("message"), Message);
		if (TargetRoleId.IsEmpty()) return MakeToolError(TEXT("missing role_id"));
		if (Message.IsEmpty())      return MakeToolError(TEXT("missing message"));

		IUECPCrewService& Crew = IUECPCoreModule::Get().GetCrewService();
		FCrewRun Run;
		if (!Crew.GetRun(Caller.RunId, Run)) return MakeToolError(TEXT("run not found"));

		const FCrewRole* Target = nullptr;
		for (const FCrewRole& R : Run.Roles)
			if (R.RoleId == TargetRoleId) { Target = &R; break; }
		if (!Target)
			return MakeToolError(FString::Printf(TEXT("role '%s' not found in this run"), *TargetRoleId));
		if (Target->bIsOrchestrator)
			return MakeToolError(TEXT("can't send a system_message to the orchestrator itself"));

		const FString* ChatIdPtr = Run.RoleChatIds.Find(TargetRoleId);
		if (!ChatIdPtr || ChatIdPtr->IsEmpty())
			return MakeToolError(TEXT("target role has no chat id"));

		FCrewHandoff H;
		H.FromRoleId   = Caller.RoleId;
		H.ToRoleId     = TargetRoleId;
		H.Type         = EHandoffType::System;
		H.Content      = Message;
		Crew.AppendHandoff(Caller.RunId, H);

		IUECPArchitectService& Arch = IUECPCoreModule::Get().GetArchitectService();
		const FString Wrapped = FString::Printf(TEXT("[CREW SYSTEM MESSAGE from %s]\n%s"),
			*Caller.RoleId, *Message);
		Arch.SendMessageToChat(*ChatIdPtr, Wrapped, Target->ApiKeySlotIndex);

		return MakeToolOk(MakeAckJson({
			{ TEXT("delivered_to"), Target->Name },
		}));
	}

	FResult Crew_CheckpointRetry(const TSharedPtr<FJsonObject>& Args)
	{
		FCrewCallerInfo Caller;
		FString Err;
		if (!ResolveCaller(Args, Caller, Err)) return MakeToolError(Err);
		if (!Caller.bIsOrchestrator)
		{
			return MakeToolError(TEXT("crew.checkpoint_retry may only be called by the orchestrator"));
		}

		FString CheckpointId;
		Args->TryGetStringField(TEXT("checkpoint_id"), CheckpointId);
		if (CheckpointId.IsEmpty()) return MakeToolError(TEXT("missing checkpoint_id"));

		FUECPCrewCoordinator* Coord = GetCoordinator();
		if (!Coord) return MakeToolError(TEXT("crew coordinator unavailable"));

		FString Reason;
		if (!Coord->RetryCheckpointEx(Caller.RunId, CheckpointId, Reason))
			return MakeToolError(Reason);

		FCrewRun Run;
		int32 RetryCount = 0, RetryCap = 0;
		if (Coord->GetRun(Caller.RunId, Run))
		{
			RetryCap = Run.Caps.MaxRetriesPerCheckpoint;
			for (const FCrewCheckpoint& C : Run.Plan)
				if (C.CheckpointId == CheckpointId) { RetryCount = C.RetryCount; break; }
		}

		return MakeToolOk(MakeAckJson({
			{ TEXT("checkpoint_id"), CheckpointId },
			{ TEXT("state"),         TEXT("pending") },
			{ TEXT("retry_count"),   LexToString(RetryCount) },
			{ TEXT("retry_cap"),     RetryCap > 0 ? LexToString(RetryCap) : FString(TEXT("unlimited")) }
		}));
	}

	FResult Crew_RequestVerify(const TSharedPtr<FJsonObject>& Args)
	{
		FCrewCallerInfo Caller;
		FString Err;
		if (!ResolveCaller(Args, Caller, Err)) return MakeToolError(Err);
		if (Caller.bIsOrchestrator)
			return MakeToolError(TEXT("crew.request_verify is for worker roles — orchestrators dispatch directly via crew.dispatch_to"));

		FString CheckpointId, ReportSummary;
		Args->TryGetStringField(TEXT("checkpoint_id"), CheckpointId);
		Args->TryGetStringField(TEXT("summary"),       ReportSummary);
		if (CheckpointId.IsEmpty()) return MakeToolError(TEXT("missing checkpoint_id"));

		IUECPCrewService& Crew = IUECPCoreModule::Get().GetCrewService();
		FCrewRun Run;
		if (!Crew.GetRun(Caller.RunId, Run)) return MakeToolError(TEXT("run not found"));

		const FCrewRole* Verifier = nullptr;
		for (const FCrewRole& R : Run.Roles)
		{
			if (R.Kind == ECrewRoleKind::Verifier) { Verifier = &R; break; }
		}
		if (!Verifier)
			return MakeToolError(TEXT("no verifier role in this run — use crew.report_back to send a report back to the supervisor instead"));

		const FCrewRole* Orchestrator = nullptr;
		for (const FCrewRole& R : Run.Roles)
			if (R.bIsOrchestrator) { Orchestrator = &R; break; }
		if (!Orchestrator)
			return MakeToolError(TEXT("run has no orchestrator role (shouldn't happen — was the run mutated mid-flight?)"));

		FString DenyR;
		FCrewHandoff SysH;
		SysH.FromRoleId   = Caller.RoleId;
		SysH.ToRoleId     = Orchestrator->RoleId;
		SysH.Type         = EHandoffType::System;
		SysH.CheckpointId = CheckpointId;
		SysH.Content      = FString::Printf(
			TEXT("Worker shortcut: dispatched directly to verifier for checkpoint '%s'.\n%s"),
			*CheckpointId,
			ReportSummary.IsEmpty() ? TEXT("(no summary provided)") : *ReportSummary);
		Crew.AppendHandoff(Caller.RunId, SysH);

		const FString Instruction = FString::Printf(
			TEXT("Worker '%s' has completed checkpoint '%s' and requested verification. ")
			TEXT("Worker report:\n%s\n\nInspect the result against the checkpoint's success criteria and report back."),
			*Caller.RoleId, *CheckpointId,
			ReportSummary.IsEmpty() ? TEXT("(no summary provided)") : *ReportSummary);

		if (!Crew.RouteInstructionDispatch(Caller.RunId, Orchestrator->RoleId, Verifier->RoleId,
				Instruction, CheckpointId, DenyR))
		{
			return MakeToolError(DenyR);
		}

		return MakeToolOk(MakeAckJson({
			{ TEXT("checkpoint_id"),  CheckpointId },
			{ TEXT("dispatched_to"),  Verifier->Name },
			{ TEXT("shortcut"),       TEXT("worker_to_verifier") }
		}));
	}
}

namespace
{

	FResult Crew_Umbrella(const TSharedPtr<FJsonObject>& Args)
	{
		FString Action;
		if (Args.IsValid()) Args->TryGetStringField(TEXT("action"), Action);
		if (Action.IsEmpty())
		{
			return MakeToolError(TEXT("crew umbrella requires action — one of: "
				"dispatch_to, report_back, complete, escalate, checkpoint_pass, "
				"checkpoint_fail, checkpoint_retry, ask_orchestrator, request_verify, "
				"system_message"));
		}

		if (Action == TEXT("dispatch_to"))      return Crew_DispatchTo(Args);
		if (Action == TEXT("report_back"))      return Crew_ReportBack(Args);
		if (Action == TEXT("ask_orchestrator")) return Crew_AskOrchestrator(Args);
		if (Action == TEXT("complete"))         return Crew_Complete(Args);
		if (Action == TEXT("escalate"))         return Crew_Escalate(Args);
		if (Action == TEXT("checkpoint_pass"))  return Crew_CheckpointPass(Args);
		if (Action == TEXT("checkpoint_fail"))  return Crew_CheckpointFail(Args);
		if (Action == TEXT("checkpoint_retry")) return Crew_CheckpointRetry(Args);
		if (Action == TEXT("request_verify"))   return Crew_RequestVerify(Args);
		if (Action == TEXT("system_message"))   return Crew_SystemMessage(Args);

		return MakeToolError(FString::Printf(TEXT("unknown crew action: %s"), *Action));
	}
}

namespace UECPCrew::Tools
{
	static const TArray<FName> ToolNames = {
		TEXT("crew"),
		TEXT("crew.dispatch_to"),
		TEXT("crew.report_back"),
		TEXT("crew.ask_orchestrator"),
		TEXT("crew.complete"),
		TEXT("crew.escalate"),
		TEXT("crew.checkpoint_pass"),
		TEXT("crew.checkpoint_fail"),
		TEXT("crew.checkpoint_retry"),
		TEXT("crew.request_verify"),
		TEXT("crew.system_message"),
	};

	void RegisterAll(IUECPToolDispatcher& Dispatcher)
	{
		Dispatcher.RegisterHandler(TEXT("crew"),
			[](const TSharedPtr<FJsonObject>& A) { return Crew_Umbrella(A); });
		Dispatcher.RegisterHandler(TEXT("crew.dispatch_to"),
			[](const TSharedPtr<FJsonObject>& A) { return Crew_DispatchTo(A); });
		Dispatcher.RegisterHandler(TEXT("crew.report_back"),
			[](const TSharedPtr<FJsonObject>& A) { return Crew_ReportBack(A); });
		Dispatcher.RegisterHandler(TEXT("crew.ask_orchestrator"),
			[](const TSharedPtr<FJsonObject>& A) { return Crew_AskOrchestrator(A); });
		Dispatcher.RegisterHandler(TEXT("crew.complete"),
			[](const TSharedPtr<FJsonObject>& A) { return Crew_Complete(A); });
		Dispatcher.RegisterHandler(TEXT("crew.escalate"),
			[](const TSharedPtr<FJsonObject>& A) { return Crew_Escalate(A); });
		Dispatcher.RegisterHandler(TEXT("crew.checkpoint_pass"),
			[](const TSharedPtr<FJsonObject>& A) { return Crew_CheckpointPass(A); });
		Dispatcher.RegisterHandler(TEXT("crew.checkpoint_fail"),
			[](const TSharedPtr<FJsonObject>& A) { return Crew_CheckpointFail(A); });
		Dispatcher.RegisterHandler(TEXT("crew.checkpoint_retry"),
			[](const TSharedPtr<FJsonObject>& A) { return Crew_CheckpointRetry(A); });
		Dispatcher.RegisterHandler(TEXT("crew.request_verify"),
			[](const TSharedPtr<FJsonObject>& A) { return Crew_RequestVerify(A); });
		Dispatcher.RegisterHandler(TEXT("crew.system_message"),
			[](const TSharedPtr<FJsonObject>& A) { return Crew_SystemMessage(A); });
		UE_LOG(LogUECPCrew, Log, TEXT("CrewTools: registered %d handlers (umbrella + aliases)"),
			ToolNames.Num());
	}

	void UnregisterAll(IUECPToolDispatcher& Dispatcher)
	{
		for (const FName& N : ToolNames)
		{
			Dispatcher.UnregisterHandler(N);
		}
	}
}
