// Copyright 2026, BlueprintsLab, All rights reserved

#include "Runtime/CrewHandoffRouter.h"
#include "Runtime/RoleContextAssembler.h"
#include "FUECPCrewCoordinator.h"
#include "Types/CrewTypes.h"
#include "UECPCrewModule.h"

#include "UECPCoreModule.h"
#include "Services/IUECPArchitectService.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace
{
	const FCrewRole* FindRole(const FCrewRun& Run, const FString& RoleId)
	{
		for (const FCrewRole& R : Run.Roles) if (R.RoleId == RoleId) return &R;
		return nullptr;
	}

	const FCrewRole* FindOrchestratorRole(const FCrewRun& Run)
	{
		for (const FCrewRole& R : Run.Roles) if (R.bIsOrchestrator) return &R;
		return nullptr;
	}

	const FCrewCheckpoint* FindCheckpoint(const FCrewRun& Run, const FString& CheckpointId)
	{
		if (CheckpointId.IsEmpty()) return nullptr;
		for (const FCrewCheckpoint& C : Run.Plan) if (C.CheckpointId == CheckpointId) return &C;
		return nullptr;
	}

	FString ExtractLastAssistantText(const TArray<TSharedPtr<FJsonValue>>& History)
	{
		for (int32 i = History.Num() - 1; i >= 0; --i)
		{
			const TSharedPtr<FJsonValue>& V = History[i];
			if (!V.IsValid() || V->Type != EJson::Object) continue;
			TSharedPtr<FJsonObject> Msg = V->AsObject();
			if (!Msg.IsValid()) continue;
			FString Role;
			Msg->TryGetStringField(TEXT("role"), Role);
			if (Role != TEXT("model")) continue;
			const TArray<TSharedPtr<FJsonValue>>* Parts = nullptr;
			if (!Msg->TryGetArrayField(TEXT("parts"), Parts) || !Parts || Parts->Num() == 0) continue;
			TSharedPtr<FJsonObject> P0 = (*Parts)[0].IsValid() ? (*Parts)[0]->AsObject() : nullptr;
			if (!P0.IsValid()) continue;
			FString Text;
			P0->TryGetStringField(TEXT("text"), Text);
			if (!Text.TrimStartAndEnd().IsEmpty()) return Text;
		}
		return FString();
	}
}

namespace UECPCrew
{
	FCrewHandoffRouter::FCrewHandoffRouter(FUECPCrewCoordinator& InOwner)
		: Owner(InOwner)
	{
	}

	FCrewHandoffRouter::~FCrewHandoffRouter()
	{
		Unbind();
	}

	void FCrewHandoffRouter::Bind()
	{
		if (TurnEndedHandle.IsValid()) return;
		if (!IUECPCoreModule::IsAvailable()) return;
		TurnEndedHandle = IUECPCoreModule::Get().GetArchitectService().OnTurnEnded()
			.AddRaw(this, &FCrewHandoffRouter::HandleTurnEnded);
	}

	void FCrewHandoffRouter::Unbind()
	{
		if (!TurnEndedHandle.IsValid()) return;
		if (IUECPCoreModule::IsAvailable())
		{
			IUECPCoreModule::Get().GetArchitectService().OnTurnEnded().Remove(TurnEndedHandle);
		}
		TurnEndedHandle.Reset();
	}

	void FCrewHandoffRouter::ForgetRun(const FGuid& RunId)
	{
		BriefedRolesByRun.Remove(RunId);
	}

	bool FCrewHandoffRouter::OpenRun(const FGuid& RunId, FString& OutReason)
	{
		FCrewRun Run;
		if (!Owner.GetRun(RunId, Run)) { OutReason = TEXT("run not found"); return false; }

		const FCrewRole* Orchestrator = FindOrchestratorRole(Run);
		if (!Orchestrator) { OutReason = TEXT("crew has no orchestrator role"); return false; }

		const FString* ChatIdPtr = Run.RoleChatIds.Find(Orchestrator->RoleId);
		if (!ChatIdPtr || ChatIdPtr->IsEmpty())
		{
			OutReason = TEXT("orchestrator role has no chat id — spawn first");
			return false;
		}

		const FString Opening = RoleContext::BuildOrchestratorOpening(Run);
		IUECPCoreModule::Get().GetArchitectService().SendMessageToChat(
			*ChatIdPtr, Opening, Orchestrator->ApiKeySlotIndex);

		BriefedRolesByRun.FindOrAdd(RunId).Add(Orchestrator->RoleId);
		return true;
	}

	bool FCrewHandoffRouter::DispatchInstruction(const FGuid& RunId, const FString& FromRoleId,
		const FString& ToRoleId, const FString& Instruction, const FString& CheckpointId,
		FString& OutDenyReason)
	{
		FCrewRun Run;
		if (!Owner.GetRun(RunId, Run)) { OutDenyReason = TEXT("run not found"); return false; }
		if (Run.State != ECrewRunState::Running)
		{
			OutDenyReason = FString::Printf(TEXT("run is not running (state=%s)"),
				*RunStateToString(Run.State));
			return false;
		}

		const FCrewRole* From = FindRole(Run, FromRoleId);
		const FCrewRole* To   = FindRole(Run, ToRoleId);
		if (!From) { OutDenyReason = TEXT("from role not found"); return false; }
		if (!To)   { OutDenyReason = TEXT("to role not found");   return false; }
		if (!From->bIsOrchestrator)
		{
			OutDenyReason = TEXT("only the orchestrator may dispatch instructions");
			return false;
		}
		if (To->bIsOrchestrator)
		{
			OutDenyReason = TEXT("cannot dispatch to the orchestrator itself");
			return false;
		}

		const FString* ChatIdPtr = Run.RoleChatIds.Find(ToRoleId);
		if (!ChatIdPtr || ChatIdPtr->IsEmpty())
		{
			OutDenyReason = TEXT("target role has no chat id — spawn first");
			return false;
		}

		// Oscillation guard: the same normalised instruction repeated inside the recent window pauses
		// the run with an escalation instead of being delivered again.
		if (!Owner.NoteInstructionDispatched(RunId, Instruction, OutDenyReason))
		{
			return false;
		}

		FCrewHandoff H;
		H.FromRoleId   = FromRoleId;
		H.ToRoleId     = ToRoleId;
		H.Type         = EHandoffType::Instruction;
		H.CheckpointId = CheckpointId;
		H.Content      = Instruction;
		Owner.AppendHandoff(RunId, H);

		if (!CheckpointId.IsEmpty())
		{
			Owner.SetCheckpointState(RunId, CheckpointId, ECheckpointState::InProgress, FString());
		}

		Owner.GetRun(RunId, Run);
		SendInstructionToRoleChat(Run, *To, Instruction, CheckpointId);
		return true;
	}

	bool FCrewHandoffRouter::DeliverReport(const FGuid& RunId, const FString& FromRoleId,
		const FString& CheckpointId, const FString& Status, const FString& Summary,
		const FString& Verification, bool bExplicit, FString& OutDenyReason)
	{
		FCrewRun Run;
		if (!Owner.GetRun(RunId, Run)) { OutDenyReason = TEXT("run not found"); return false; }
		if (Run.State != ECrewRunState::Running)
		{
			OutDenyReason = FString::Printf(TEXT("run is not running (state=%s)"),
				*RunStateToString(Run.State));
			return false;
		}

		const FCrewRole* From = FindRole(Run, FromRoleId);
		const FCrewRole* Orchestrator = FindOrchestratorRole(Run);
		if (!From)         { OutDenyReason = TEXT("from role not found");        return false; }
		if (!Orchestrator) { OutDenyReason = TEXT("no orchestrator on this crew"); return false; }

		const FString* OrchChatIdPtr = Run.RoleChatIds.Find(Orchestrator->RoleId);
		if (!OrchChatIdPtr || OrchChatIdPtr->IsEmpty())
		{
			OutDenyReason = TEXT("orchestrator chat id missing");
			return false;
		}

		// Evidence = successful read-only tool results in the reporting role's chat since its last
		// instruction. Collected at report time so only read-backs that precede the claim count.
		const FString* FromChatIdPtr = Run.RoleChatIds.Find(FromRoleId);
		const TArray<FString> Evidence = (FromChatIdPtr && !FromChatIdPtr->IsEmpty())
			? Owner.CollectReadOnlyToolEvidence(*FromChatIdPtr)
			: TArray<FString>{};

		FCrewHandoff H;
		H.FromRoleId            = FromRoleId;
		H.ToRoleId              = Orchestrator->RoleId;
		H.Type                  = EHandoffType::Result;
		H.CheckpointId          = CheckpointId;
		H.bExplicitReport       = bExplicit;
		H.Verification          = Verification;
		H.VerificationToolCalls = Evidence;
		// First line stays "status=<x>" for anything that still string-matches on it.
		H.Content = FString::Printf(TEXT("status=%s\n%s"), *Status, *Summary);
		if (!Verification.IsEmpty())
			H.Content += FString::Printf(TEXT("\n\nverification: %s"), *Verification);
		H.Content += FString::Printf(TEXT("\nverified_by: %s"),
			Evidence.Num() > 0 ? *FString::Join(Evidence, TEXT(", ")) : TEXT("(none)"));

		Owner.RecordReportEvidence(RunId, CheckpointId, Status, Verification, Evidence);
		Owner.AppendHandoff(RunId, H);

		Owner.GetRun(RunId, Run);
		const FCrewCheckpoint* C = FindCheckpoint(Run, CheckpointId);
		const FString Wake = RoleContext::BuildReportMessage(Run, *From, C, Status, Summary,
			Verification, Evidence, bExplicit);
		IUECPCoreModule::Get().GetArchitectService().SendMessageToChat(
			*OrchChatIdPtr, Wake, Orchestrator->ApiKeySlotIndex);
		return true;
	}

	bool FCrewHandoffRouter::DeliverQuestion(const FGuid& RunId, const FString& FromRoleId,
		const FString& Question, FString& OutDenyReason)
	{
		FCrewRun Run;
		if (!Owner.GetRun(RunId, Run)) { OutDenyReason = TEXT("run not found"); return false; }
		if (Run.State != ECrewRunState::Running)
		{
			OutDenyReason = FString::Printf(TEXT("run is not running (state=%s)"),
				*RunStateToString(Run.State));
			return false;
		}

		const FCrewRole* From = FindRole(Run, FromRoleId);
		const FCrewRole* Orchestrator = FindOrchestratorRole(Run);
		if (!From)         { OutDenyReason = TEXT("from role not found");        return false; }
		if (!Orchestrator) { OutDenyReason = TEXT("no orchestrator on this crew"); return false; }

		const FString* OrchChatIdPtr = Run.RoleChatIds.Find(Orchestrator->RoleId);
		if (!OrchChatIdPtr || OrchChatIdPtr->IsEmpty())
		{
			OutDenyReason = TEXT("orchestrator chat id missing");
			return false;
		}

		FCrewHandoff H;
		H.FromRoleId = FromRoleId;
		H.ToRoleId   = Orchestrator->RoleId;
		H.Type       = EHandoffType::Question;
		H.Content    = Question;
		Owner.AppendHandoff(RunId, H);

		const FString Wake = RoleContext::BuildQuestionMessage(Run, *From, Question);
		IUECPCoreModule::Get().GetArchitectService().SendMessageToChat(
			*OrchChatIdPtr, Wake, Orchestrator->ApiKeySlotIndex);
		return true;
	}

	void FCrewHandoffRouter::HandleTurnEnded(const FString& ChatId, bool bSuccess)
	{
		FGuid   RunId;
		FString RoleId;
		if (!Owner.IsCrewChat(ChatId, RunId, RoleId)) return;

		Owner.MarkTurnTaken(RunId, RoleId, bSuccess);

		FCrewRun Run;
		if (!Owner.GetRun(RunId, Run)) return;
		if (Run.State != ECrewRunState::Running) return;

		const FCrewRole* Role = FindRole(Run, RoleId);
		if (!Role) return;

		if (Role->bIsOrchestrator) return;

		bool bAlreadyReported = false;
		for (int32 i = Run.Handoffs.Num() - 1; i >= 0; --i)
		{
			const FCrewHandoff& H = Run.Handoffs[i];
			if (H.Type == EHandoffType::Result && H.FromRoleId == RoleId)
			{
				bAlreadyReported = true;
				break;
			}
			if (H.Type == EHandoffType::Instruction && H.ToRoleId == RoleId)
			{
				break;
			}
		}
		if (bAlreadyReported) return;

		SynthesiseAndDeliverReport(Run, *Role, bSuccess);
	}

	void FCrewHandoffRouter::SynthesiseAndDeliverReport(const FCrewRun& Run, const FCrewRole& FromRole, bool bSuccess)
	{
		IUECPArchitectService& Arch = IUECPCoreModule::Get().GetArchitectService();

		const FString* ChatIdPtr = Run.RoleChatIds.Find(FromRole.RoleId);
		const TArray<TSharedPtr<FJsonValue>> Hist = (ChatIdPtr && !ChatIdPtr->IsEmpty())
			? Arch.GetConversationHistoryForChat(*ChatIdPtr)
			: TArray<TSharedPtr<FJsonValue>>{};

		FString Summary = ExtractLastAssistantText(Hist);
		if (Summary.IsEmpty())
		{
			Summary = bSuccess
				? TEXT("(role ended its turn with no text output and without calling crew.report_back)")
				: TEXT("(role turn ended with an error and no text)");
		}
		else if (!bSuccess)
		{
			Summary = TEXT("(turn ended with an API/tool-loop error) ") + Summary;
		}

		FString CheckpointId;
		for (int32 i = Run.Handoffs.Num() - 1; i >= 0; --i)
		{
			const FCrewHandoff& H = Run.Handoffs[i];
			if (H.Type == EHandoffType::Instruction && H.ToRoleId == FromRole.RoleId)
			{
				CheckpointId = H.CheckpointId;
				break;
			}
		}
		if (CheckpointId.IsEmpty())
		{
			for (const FCrewCheckpoint& C : Run.Plan)
			{
				if (C.State == ECheckpointState::InProgress) { CheckpointId = C.CheckpointId; break; }
			}
		}

		if (const FCrewCheckpoint* SynthCp = FindCheckpoint(Run, CheckpointId))
		{
			if (SynthCp->State == ECheckpointState::Passed
				|| SynthCp->State == ECheckpointState::Failed
				|| SynthCp->State == ECheckpointState::Skipped)
			{
				UE_LOG(LogUECPCrew, Verbose,
					TEXT("HandoffRouter: dropping redundant synth report for already-resolved checkpoint %s (role=%s)"),
					*CheckpointId, *FromRole.RoleId);
				return;
			}
		}

		// bSuccess only means "no API / tool-loop error" — it says nothing about the work. A role
		// that never called crew.report_back is therefore reported as *unreported*, never success.
		const FString Status = UECPCrew::ReportStatusUnreported;

		FString DenyReason;
		if (!DeliverReport(Run.RunId, FromRole.RoleId, CheckpointId, Status, Summary,
				FString(), /*bExplicit=*/false, DenyReason))
		{
			UE_LOG(LogUECPCrew, Warning,
				TEXT("HandoffRouter: synth report delivery failed (run=%s role=%s): %s"),
				*Run.RunId.ToString(), *FromRole.RoleId, *DenyReason);
		}
	}

	void FCrewHandoffRouter::SendInstructionToRoleChat(const FCrewRun& Run, const FCrewRole& Role,
		const FString& InstructionText, const FString& CheckpointId)
	{
		const FString* ChatIdPtr = Run.RoleChatIds.Find(Role.RoleId);
		if (!ChatIdPtr || ChatIdPtr->IsEmpty()) return;

		const FCrewCheckpoint* C = FindCheckpoint(Run, CheckpointId);

		TSet<FString>& Briefed = BriefedRolesByRun.FindOrAdd(Run.RunId);
		const bool bAlreadyBriefed = Briefed.Contains(Role.RoleId);
		const FString Msg = bAlreadyBriefed
			? RoleContext::BuildInstructionMessage(Run, Role, C, InstructionText)
			: RoleContext::BuildFirstInstructionForRole(Run, Role, C, InstructionText);
		Briefed.Add(Role.RoleId);

		IUECPCoreModule::Get().GetArchitectService().SendMessageToChat(
			*ChatIdPtr, Msg, Role.ApiKeySlotIndex);
	}
}
