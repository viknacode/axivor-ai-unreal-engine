// Copyright 2026, BlueprintsLab, All rights reserved

#include "FUECPCrewCoordinator.h"
#include "Types/CrewTypes.h"
#include "BuiltIn/CrewBuiltInTemplates.h"
#include "Persistence/CrewManifest.h"
#include "Runtime/CrewHandoffRouter.h"
#include "UECPCrewModule.h"

#include "UECPCoreModule.h"
#include "Services/IUECPArchitectService.h"
#include "Services/IUECPToolDispatcher.h"
#include "Misc/CoreDelegates.h"
#include "Services/UECPCrewChatTokenStore.h"
#include "Services/UECPToolSafety.h"
#include "Managers/ChatHistoryManager.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace
{

	// Resets every per-attempt field on a checkpoint (used when a run is created / forked / retried).
	void ResetCheckpointAttemptState(FCrewCheckpoint& C)
	{
		C.State              = ECheckpointState::Pending;
		C.ResultSummary.Empty();
		C.InstructionTime    = FDateTime{};
		C.PausedSecondsTotal = 0;
		C.LastReportStatus.Empty();
		C.VerificationText.Empty();
		C.VerificationToolCalls.Reset();
		C.bGrounded          = false;
	}

	// First text part of a canonical history entry ({role, parts:[{text}]}); empty otherwise.
	bool ReadHistoryEntryText(const TSharedPtr<FJsonValue>& V, FString& OutRole, FString& OutText)
	{
		if (!V.IsValid() || V->Type != EJson::Object) return false;
		const TSharedPtr<FJsonObject> Msg = V->AsObject();
		if (!Msg.IsValid()) return false;
		Msg->TryGetStringField(TEXT("role"), OutRole);
		const TArray<TSharedPtr<FJsonValue>>* Parts = nullptr;
		if (!Msg->TryGetArrayField(TEXT("parts"), Parts) || !Parts || Parts->Num() == 0) return false;
		const TSharedPtr<FJsonObject> P0 = (*Parts)[0].IsValid() ? (*Parts)[0]->AsObject() : nullptr;
		if (!P0.IsValid()) return false;
		P0->TryGetStringField(TEXT("text"), OutText);
		return true;
	}

	// chars/4 over all text parts of one history entry — the fallback token estimate.
	int64 EstimateEntryTokens(const TSharedPtr<FJsonValue>& V)
	{
		if (!V.IsValid() || V->Type != EJson::Object) return 0;
		const TSharedPtr<FJsonObject> Msg = V->AsObject();
		if (!Msg.IsValid()) return 0;
		int64 Chars = 0;
		const TArray<TSharedPtr<FJsonValue>>* Parts = nullptr;
		if (Msg->TryGetArrayField(TEXT("parts"), Parts) && Parts)
		{
			for (const TSharedPtr<FJsonValue>& P : *Parts)
			{
				const TSharedPtr<FJsonObject> PO = P.IsValid() ? P->AsObject() : nullptr;
				if (!PO.IsValid()) continue;
				FString Text;
				if (PO->TryGetStringField(TEXT("text"), Text)) Chars += Text.Len();
			}
		}
		return Chars / 4;
	}

	FString MakeUserTemplateId()
	{
		const FString G = FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(12).ToLower();
		return FString::Printf(TEXT("crew.tpl.%s"), *G);
	}

	FString MakeRoleId()
	{
		const FString G = FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(8).ToLower();
		return FString::Printf(TEXT("role.%s"), *G);
	}
}

FUECPCrewCoordinator::FUECPCrewCoordinator() = default;

FUECPCrewCoordinator::~FUECPCrewCoordinator()
{
	if (TimeoutTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TimeoutTickerHandle);
		TimeoutTickerHandle.Reset();
	}
	if (PreExitHandle.IsValid())
	{
		FCoreDelegates::OnPreExit.Remove(PreExitHandle);
		PreExitHandle.Reset();
	}
	if (Router.IsValid())
	{
		Router->Unbind();
		Router.Reset();
	}
}

void FUECPCrewCoordinator::Initialize()
{
	BuiltInTemplates = UECPCrew::MakeAllBuiltInTemplates();
	UserTemplates    = UECPCrew::Manifest::LoadAllUserTemplates();
	Runs             = UECPCrew::Manifest::LoadAllRuns();

	int32 RecoveredCount = 0;
	int32 PausedPreservedCount = 0;
	for (FCrewRun& Run : Runs)
	{
		if (Run.State == ECrewRunState::Running)
		{
			Run.State       = ECrewRunState::Paused;
			Run.PauseReason = TEXT("session_restart");
			Run.PausedAt    = FDateTime::UtcNow();
			Run.PausedActiveRoleIds.Reset();
			UECPCrew::Manifest::SaveRun(Run);
			++RecoveredCount;
		}
		else if (Run.State == ECrewRunState::Paused)
		{
			Run.PausedActiveRoleIds.Reset();
			UECPCrew::Manifest::SaveRun(Run);
			++PausedPreservedCount;
		}
	}

	Router = MakeUnique<UECPCrew::FCrewHandoffRouter>(*this);
	Router->Bind();

	RefreshChatRoleMap();

	PreExitHandle = FCoreDelegates::OnPreExit.AddRaw(this, &FUECPCrewCoordinator::PauseAllRunningOnShutdown);

	TimeoutTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateRaw(this, &FUECPCrewCoordinator::TickCheckpointTimeouts),
		10.0f);

	UE_LOG(LogUECPCrew, Log,
		TEXT("FUECPCrewCoordinator: initialized — %d built-in templates, %d user templates, %d runs (%d recovered from prior-session Running → Paused, %d paused preserved for resume)"),
		BuiltInTemplates.Num(), UserTemplates.Num(), Runs.Num(), RecoveredCount, PausedPreservedCount);
}

void FUECPCrewCoordinator::InitializeShellRefs(TWeakPtr<SUECPMainWidget> ,
	TWeakObjectPtr<UUECPAppBridge> )
{
}

TArray<FCrewTemplate> FUECPCrewCoordinator::GetTemplates() const
{
	TArray<FCrewTemplate> Out;
	Out.Reserve(BuiltInTemplates.Num() + UserTemplates.Num());
	Out.Append(BuiltInTemplates);
	Out.Append(UserTemplates);
	return Out;
}

bool FUECPCrewCoordinator::GetTemplate(const FString& TemplateId, FCrewTemplate& OutTemplate) const
{
	if (const FCrewTemplate* Found = FindTemplateById(TemplateId))
	{
		OutTemplate = *Found;
		return true;
	}
	return false;
}

const FCrewTemplate* FUECPCrewCoordinator::FindTemplateById(const FString& TemplateId) const
{
	for (const FCrewTemplate& T : BuiltInTemplates) if (T.TemplateId == TemplateId) return &T;
	for (const FCrewTemplate& T : UserTemplates)    if (T.TemplateId == TemplateId) return &T;
	return nullptr;
}

FString FUECPCrewCoordinator::CloneTemplate(const FString& SourceTemplateId, const FString& NewDisplayName)
{
	const FCrewTemplate* Src = FindTemplateById(SourceTemplateId);
	if (!Src) return FString();

	FCrewTemplate Clone = *Src;
	Clone.TemplateId  = MakeUserTemplateId();
	Clone.DisplayName = NewDisplayName.IsEmpty() ? (Src->DisplayName + TEXT(" (copy)")) : NewDisplayName;
	Clone.bBuiltIn    = false;

	for (FCrewRole& R : Clone.Roles)
	{
		R.RoleId = MakeRoleId();
	}

	UserTemplates.Add(Clone);
	UECPCrew::Manifest::SaveTemplate(Clone);
	BroadcastTemplatesChanged();
	return Clone.TemplateId;
}

FString FUECPCrewCoordinator::CreateTemplate(const FString& DisplayName)
{
	FCrewTemplate T;
	T.TemplateId  = MakeUserTemplateId();
	T.DisplayName = DisplayName.IsEmpty() ? TEXT("New Crew") : DisplayName;
	T.bBuiltIn    = false;

	UserTemplates.Add(T);
	UECPCrew::Manifest::SaveTemplate(T);
	BroadcastTemplatesChanged();
	return T.TemplateId;
}

bool FUECPCrewCoordinator::UpdateTemplate(const FCrewTemplate& Template)
{
	if (Template.TemplateId.IsEmpty()) return false;

	for (const FCrewTemplate& Built : BuiltInTemplates)
	{
		if (Built.TemplateId == Template.TemplateId)
		{
			UE_LOG(LogUECPCrew, Warning, TEXT("UpdateTemplate: refusing to edit built-in '%s'"),
				*Template.TemplateId);
			return false;
		}
	}

	for (FCrewTemplate& T : UserTemplates)
	{
		if (T.TemplateId == Template.TemplateId)
		{
			T = Template;
			T.bBuiltIn = false;
			UECPCrew::Manifest::SaveTemplate(T);
			BroadcastTemplatesChanged();
			return true;
		}
	}
	return false;
}

bool FUECPCrewCoordinator::DeleteTemplate(const FString& TemplateId)
{
	for (const FCrewTemplate& Built : BuiltInTemplates)
	{
		if (Built.TemplateId == TemplateId)
		{
			UE_LOG(LogUECPCrew, Warning, TEXT("DeleteTemplate: refusing to delete built-in '%s'"),
				*TemplateId);
			return false;
		}
	}

	const int32 Removed = UserTemplates.RemoveAll(
		[&](const FCrewTemplate& T) { return T.TemplateId == TemplateId; });
	if (Removed == 0) return false;

	UECPCrew::Manifest::DeleteTemplate(TemplateId);
	BroadcastTemplatesChanged();
	return true;
}

TArray<FCrewRun> FUECPCrewCoordinator::GetRuns() const
{
	return Runs;
}

bool FUECPCrewCoordinator::GetRun(const FGuid& RunId, FCrewRun& OutRun) const
{
	if (const FCrewRun* Found = FindRunById(RunId))
	{
		OutRun = *Found;
		return true;
	}
	return false;
}

FGuid FUECPCrewCoordinator::GetActiveRunId() const
{
	for (const FCrewRun& R : Runs)
	{
		if (R.State == ECrewRunState::Running || R.State == ECrewRunState::Paused)
		{
			return R.RunId;
		}
	}
	return FGuid();
}

FCrewRun* FUECPCrewCoordinator::FindRunById(const FGuid& RunId)
{
	for (FCrewRun& R : Runs) if (R.RunId == RunId) return &R;
	return nullptr;
}

const FCrewRun* FUECPCrewCoordinator::FindRunById(const FGuid& RunId) const
{
	for (const FCrewRun& R : Runs) if (R.RunId == RunId) return &R;
	return nullptr;
}

FGuid FUECPCrewCoordinator::CreateRun(const FString& TemplateId, const FString& DisplayName)
{
	const FCrewTemplate* T = FindTemplateById(TemplateId);
	if (!T) return FGuid();

	FCrewRun Run;
	Run.DisplayName     = DisplayName.IsEmpty() ? (T->DisplayName + TEXT(" run")) : DisplayName;
	Run.CrewTemplateId  = TemplateId;
	Run.Roles           = T->Roles;
	Run.State           = ECrewRunState::Draft;
	Run.Plan = T->DefaultPlan;
	for (FCrewCheckpoint& C : Run.Plan)
	{
		ResetCheckpointAttemptState(C);
		C.RetryCount = 0;
	}

	Runs.Insert(Run, 0);
	UECPCrew::Manifest::SaveRun(Run);
	BroadcastRunChanged(Run);
	return Run.RunId;
}

bool FUECPCrewCoordinator::SetRunRoles(const FGuid& RunId, const TArray<FCrewRole>& Roles)
{
	FCrewRun* Run = FindRunById(RunId);
	if (!Run) return false;
	if (Run->State != ECrewRunState::Draft && Run->State != ECrewRunState::AwaitingPlanApproval)
	{
		UE_LOG(LogUECPCrew, Warning, TEXT("SetRunRoles: run %s is not editable (state=%s)"),
			*RunId.ToString(), *UECPCrew::RunStateToString(Run->State));
		return false;
	}
	Run->Roles = Roles;
	SaveRun_NoBroadcast(*Run);
	BroadcastRunChanged(*Run);
	return true;
}

bool FUECPCrewCoordinator::SetRunPlan(const FGuid& RunId, const TArray<FCrewCheckpoint>& Plan,
	TArray<FString>* OutErrors)
{
	FCrewRun* Run = FindRunById(RunId);
	if (!Run) return false;
	if (Run->State != ECrewRunState::Draft && Run->State != ECrewRunState::AwaitingPlanApproval)
	{
		UE_LOG(LogUECPCrew, Warning, TEXT("SetRunPlan: run %s is not editable (state=%s)"),
			*RunId.ToString(), *UECPCrew::RunStateToString(Run->State));
		return false;
	}

	TArray<FString> Errors;
	if (!IUECPCrewService::ValidatePlan(Plan, Errors))
	{
		for (const FString& E : Errors)
			UE_LOG(LogUECPCrew, Warning, TEXT("SetRunPlan: plan rejected — %s"), *E);
		if (OutErrors) *OutErrors = MoveTemp(Errors);
		return false;
	}

	Run->Plan = Plan;
	SaveRun_NoBroadcast(*Run);
	BroadcastRunChanged(*Run);
	return true;
}

bool FUECPCrewCoordinator::TickCheckpointTimeouts(float )
{
	const FDateTime Now = FDateTime::UtcNow();
	IUECPArchitectService& Arch = IUECPCoreModule::Get().GetArchitectService();

	for (FCrewRun& Run : Runs)
	{
		if (Run.State != ECrewRunState::Running) continue;

		for (FCrewCheckpoint& C : Run.Plan)
		{
			if (C.TimeoutMinutes <= 0) continue;
			if (C.State != ECheckpointState::InProgress) continue;
			if (C.InstructionTime.GetTicks() == 0) continue;

			const FTimespan Wall = Now - C.InstructionTime;
			const double EffectiveMinutes = (Wall.GetTotalSeconds() - (double)C.PausedSecondsTotal) / 60.0;
			if (EffectiveMinutes < (double)C.TimeoutMinutes) continue;

			const FString Reason = FString::Printf(TEXT("(timed out after %dm)"), C.TimeoutMinutes);
			C.State         = ECheckpointState::Failed;
			C.ResultSummary = Reason;

			FCrewHandoff Sys;
			Sys.Type         = EHandoffType::System;
			Sys.CheckpointId = C.CheckpointId;
			Sys.Content      = FString::Printf(TEXT("Checkpoint '%s' %s — run paused. Use crew.checkpoint_retry to try again or crew.abort to give up."),
				*C.CheckpointId, *Reason);
			Run.Handoffs.Add(Sys);

			Run.State        = ECrewRunState::Paused;
			Run.PauseReason  = FString::Printf(TEXT("cp_timeout:%s"), *C.CheckpointId);
			Run.PausedAt     = Now;
			Run.PausedActiveRoleIds.Reset();
			for (const TPair<FString, FString>& Pair : Run.RoleChatIds)
			{
				if (!Pair.Value.IsEmpty() && Arch.IsThinkingForChat(Pair.Value))
					Run.PausedActiveRoleIds.Add(Pair.Key);
			}
			for (const TPair<FString, FString>& Pair : Run.RoleChatIds)
			{
				if (!Pair.Value.IsEmpty()) Arch.StopGenerationForChat(Pair.Value);
			}

			SaveRun_NoBroadcast(Run);
			BroadcastRunChanged(Run);
			UE_LOG(LogUECPCrew, Warning, TEXT("TickCheckpointTimeouts: %s · %s timed out (%.1fm > %dm)"),
				*Run.DisplayName, *C.CheckpointId, EffectiveMinutes, C.TimeoutMinutes);
			break;
		}
	}
	return true;
}

bool FUECPCrewCoordinator::AnswerEscalation(const FGuid& RunId, const FString& AnswerText)
{
	FCrewRun* Run = FindRunById(RunId);
	if (!Run) return false;
	if (Run->State != ECrewRunState::Paused || !Run->PauseReason.StartsWith(TEXT("user_escalation")))
	{
		UE_LOG(LogUECPCrew, Warning,
			TEXT("AnswerEscalation: run %s is not awaiting an escalation (state=%s reason=%s)"),
			*RunId.ToString(), *UECPCrew::RunStateToString(Run->State), *Run->PauseReason);
		return false;
	}

	const FCrewRole* Orchestrator = nullptr;
	for (const FCrewRole& R : Run->Roles)
		if (R.bIsOrchestrator) { Orchestrator = &R; break; }
	if (!Orchestrator || Run->OrchestratorChatId.IsEmpty())
	{
		UE_LOG(LogUECPCrew, Warning, TEXT("AnswerEscalation: run %s has no orchestrator chat"), *RunId.ToString());
		return false;
	}

	const FString Body = AnswerText.IsEmpty()
		? TEXT("(no answer provided — proceed using your best judgement)")
		: AnswerText;

	FCrewHandoff H;
	H.FromRoleId = TEXT("user");
	H.ToRoleId   = Orchestrator->RoleId;
	H.Type       = EHandoffType::Answer;
	H.Content    = Body;
	Run->Handoffs.Add(H);

	Run->PauseReason.Empty();
	Run->PausedActiveRoleIds.Remove(Orchestrator->RoleId);
	SaveRun_NoBroadcast(*Run);
	BroadcastRunChanged(*Run);

	if (!ResumeRun(RunId)) return false;

	IUECPArchitectService& Arch = IUECPCoreModule::Get().GetArchitectService();
	const FString WrappedAnswer = FString::Printf(TEXT("[CREW ESCALATION ANSWER from user]\n%s"), *Body);
	Arch.SendMessageToChat(Run->OrchestratorChatId, WrappedAnswer, Orchestrator->ApiKeySlotIndex);

	return true;
}

bool FUECPCrewCoordinator::SetRoleApiKeySlot(const FGuid& RunId, const FString& RoleId, int32 SlotIndex)
{
	FCrewRun* Run = FindRunById(RunId);
	if (!Run) return false;
	if (Run->State == ECrewRunState::Completed || Run->State == ECrewRunState::Aborted)
	{
		UE_LOG(LogUECPCrew, Warning, TEXT("SetRoleApiKeySlot: run %s is terminal (state=%s)"),
			*RunId.ToString(), *UECPCrew::RunStateToString(Run->State));
		return false;
	}
	for (FCrewRole& R : Run->Roles)
	{
		if (R.RoleId == RoleId)
		{
			R.ApiKeySlotIndex = (SlotIndex < 0) ? -1 : SlotIndex;
			SaveRun_NoBroadcast(*Run);
			BroadcastRunChanged(*Run);
			UE_LOG(LogUECPCrew, Log, TEXT("SetRoleApiKeySlot: %s · %s → slot %d"),
				*Run->DisplayName, *R.Name, R.ApiKeySlotIndex);
			return true;
		}
	}
	return false;
}

bool FUECPCrewCoordinator::RetryCheckpoint(const FGuid& RunId, const FString& CheckpointId)
{
	FString Reason;
	const bool bOk = RetryCheckpointEx(RunId, CheckpointId, Reason);
	if (!bOk)
	{
		UE_LOG(LogUECPCrew, Warning, TEXT("RetryCheckpoint: %s · %s — %s"),
			*RunId.ToString(), *CheckpointId, *Reason);
	}
	return bOk;
}

bool FUECPCrewCoordinator::RetryCheckpointEx(const FGuid& RunId, const FString& CheckpointId, FString& OutReason)
{
	FCrewRun* Run = FindRunById(RunId);
	if (!Run) { OutReason = TEXT("run not found"); return false; }
	if (Run->State != ECrewRunState::Running && Run->State != ECrewRunState::Paused)
	{
		OutReason = FString::Printf(TEXT("run is not running/paused (state=%s)"),
			*UECPCrew::RunStateToString(Run->State));
		return false;
	}

	FCrewCheckpoint* Target = nullptr;
	for (FCrewCheckpoint& C : Run->Plan)
		if (C.CheckpointId == CheckpointId) { Target = &C; break; }
	if (!Target) { OutReason = TEXT("checkpoint not found"); return false; }

	const int32 Cap = Run->Caps.MaxRetriesPerCheckpoint;
	if (Cap > 0 && Target->RetryCount >= Cap)
	{
		const FString LastStatus = Target->LastReportStatus.IsEmpty()
			? FString(UECPCrew::ReportStatusUnreported) : Target->LastReportStatus;

		Target->State         = ECheckpointState::Failed;
		Target->ResultSummary = FString::Printf(
			TEXT("(retry cap reached: %d/%d retries without passing — marked failed and escalated)"),
			Target->RetryCount, Cap);

		FCrewHandoff Sys;
		Sys.Type         = EHandoffType::System;
		Sys.CheckpointId = CheckpointId;
		Sys.Content      = FString::Printf(
			TEXT("Checkpoint '%s' exceeded its retry cap (%d). It is now FAILED and the run is paused for user escalation."),
			*CheckpointId, Cap);
		Run->Handoffs.Add(Sys);

		EscalateAndPause(*Run, FString::Printf(TEXT("retry_cap:%s"), *CheckpointId),
			FString::Printf(
				TEXT("Checkpoint '%s' (%s) has been retried %d times (cap %d) without passing, so it has been marked FAILED.\n")
				TEXT("Last report status: %s.\nSuccess criteria: %s\n")
				TEXT("How should the crew proceed? (Give new guidance for this checkpoint, tell the orchestrator to move on, or abort the run.)"),
				*CheckpointId, *Target->Description, Target->RetryCount, Cap,
				*LastStatus,
				Target->SuccessCriteria.IsEmpty() ? TEXT("(none given)") : *Target->SuccessCriteria));

		OutReason = FString::Printf(
			TEXT("checkpoint '%s' exceeded its retry cap (%d/%d) — it has been marked failed and the run is paused for user escalation; do not retry again"),
			*CheckpointId, Target->RetryCount, Cap);
		UE_LOG(LogUECPCrew, Warning, TEXT("RetryCheckpointEx: %s · %s"), *Run->DisplayName, *OutReason);
		return false;
	}

	++Target->RetryCount;
	ResetCheckpointAttemptState(*Target);
	Target->RetriedAt = FDateTime::UtcNow();
	SaveRun_NoBroadcast(*Run);
	BroadcastRunChanged(*Run);
	UE_LOG(LogUECPCrew, Log, TEXT("RetryCheckpointEx: %s · %s reset to Pending (retry %d%s)"),
		*Run->DisplayName, *CheckpointId, Target->RetryCount,
		Cap > 0 ? *FString::Printf(TEXT("/%d"), Cap) : TEXT(""));
	return true;
}

FGuid FUECPCrewCoordinator::ForkRun(const FGuid& SourceRunId, const FString& NewDisplayName)
{
	FCrewRun* Source = FindRunById(SourceRunId);
	if (!Source) return FGuid();

	FCrewRun Fork;
	Fork.DisplayName    = NewDisplayName.IsEmpty()
		? FString::Printf(TEXT("%s (fork)"), *Source->DisplayName)
		: NewDisplayName;
	Fork.CrewTemplateId = Source->CrewTemplateId;
	Fork.Roles          = Source->Roles;
	Fork.Plan           = Source->Plan;
	Fork.Caps           = Source->Caps;
	Fork.State          = ECrewRunState::Draft;
	for (FCrewCheckpoint& C : Fork.Plan)
	{
		ResetCheckpointAttemptState(C);
		C.RetriedAt  = FDateTime{};
		C.RetryCount = 0;
	}

	Runs.Add(Fork);
	SaveRun_NoBroadcast(Runs.Last());
	BroadcastRunChanged(Runs.Last());
	UE_LOG(LogUECPCrew, Log, TEXT("ForkRun: %s → %s (%s)"),
		*Source->DisplayName, *Fork.DisplayName, *Fork.RunId.ToString());
	return Fork.RunId;
}

bool FUECPCrewCoordinator::SetRunCaps(const FGuid& RunId, const FCrewRunCaps& Caps)
{
	FCrewRun* Run = FindRunById(RunId);
	if (!Run) return false;
	Run->Caps = Caps;
	SaveRun_NoBroadcast(*Run);
	BroadcastRunChanged(*Run);
	return true;
}

bool FUECPCrewCoordinator::SetRoleAllowlistOnRun(const FGuid& RunId, const FString& RoleId,
	const TArray<FString>& Allowlist)
{
	FCrewRun* Run = FindRunById(RunId);
	if (!Run) return false;
	if (Run->State == ECrewRunState::Completed || Run->State == ECrewRunState::Aborted)
	{
		UE_LOG(LogUECPCrew, Warning, TEXT("SetRoleAllowlistOnRun: run %s is terminal (state=%s) — edit ignored"),
			*RunId.ToString(), *UECPCrew::RunStateToString(Run->State));
		return false;
	}
	for (FCrewRole& R : Run->Roles)
	{
		if (R.RoleId == RoleId)
		{
			R.ToolAllowlist = Allowlist;
			SaveRun_NoBroadcast(*Run);
			BroadcastRunChanged(*Run);
			return true;
		}
	}
	return false;
}

bool FUECPCrewCoordinator::ShouldAutoApproveDestructiveForChat(const FString& ChatId) const
{
	if (ChatId.IsEmpty()) return false;
	const FChatRoleBinding* Bind = ChatToRoleMap.Find(ChatId);
	if (!Bind) return false;
	const FCrewRun* Run = FindRunById(Bind->RunId);
	if (!Run) return false;
	return Run->Caps.bAutoApproveDestructive;
}

bool FUECPCrewCoordinator::ExportTemplate(const FString& TemplateId, FString& OutJson) const
{
	const FCrewTemplate* Tpl = FindTemplateById(TemplateId);
	if (!Tpl) return false;

	TSharedPtr<FJsonObject> O = UECPCrew::Manifest::TemplateToJson(*Tpl);
	if (!O.IsValid()) return false;
	O->RemoveField(TEXT("built_in"));
	O->SetStringField(TEXT("exported_at"), FDateTime::UtcNow().ToIso8601());
	O->SetStringField(TEXT("exported_from"), Tpl->DisplayName);

	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJson);
	return FJsonSerializer::Serialize(O.ToSharedRef(), W);
}

FString FUECPCrewCoordinator::ImportTemplate(const FString& Json)
{
	TSharedPtr<FJsonObject> O;
	TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(Json);
	if (!FJsonSerializer::Deserialize(R, O) || !O.IsValid())
	{
		UE_LOG(LogUECPCrew, Warning, TEXT("ImportTemplate: JSON parse failed"));
		return FString();
	}

	FCrewTemplate Parsed;
	if (!UECPCrew::Manifest::TemplateFromJson(O, Parsed))
	{
		UE_LOG(LogUECPCrew, Warning, TEXT("ImportTemplate: TemplateFromJson rejected payload"));
		return FString();
	}

	Parsed.TemplateId = FGuid::NewGuid().ToString(EGuidFormats::DigitsLower);
	Parsed.bBuiltIn   = false;
	for (FCrewRole& Role : Parsed.Roles)
	{
		Role.RoleId = FString::Printf(TEXT("role.%s"),
			*FGuid::NewGuid().ToString(EGuidFormats::DigitsLower).Left(8));
	}

	UserTemplates.Add(Parsed);
	UECPCrew::Manifest::SaveTemplate(Parsed);
	BroadcastTemplatesChanged();
	return Parsed.TemplateId;
}

bool FUECPCrewCoordinator::SetRunDisplayName(const FGuid& RunId, const FString& DisplayName)
{
	FCrewRun* Run = FindRunById(RunId);
	if (!Run) return false;
	Run->DisplayName = DisplayName;
	SaveRun_NoBroadcast(*Run);
	BroadcastRunChanged(*Run);
	return true;
}

bool FUECPCrewCoordinator::CanTransition(const FCrewRun& Run, ECrewRunState Dst, FString& OutReason) const
{
	switch (Run.State)
	{
	case ECrewRunState::Draft:
		if (Dst == ECrewRunState::AwaitingPlanApproval || Dst == ECrewRunState::Running ||
			Dst == ECrewRunState::Aborted) return true;
		OutReason = TEXT("draft can only transition to awaiting_plan_approval / running / aborted");
		return false;

	case ECrewRunState::AwaitingPlanApproval:
		if (Dst == ECrewRunState::Draft || Dst == ECrewRunState::Running || Dst == ECrewRunState::Aborted) return true;
		OutReason = TEXT("awaiting_plan_approval can only transition to draft / running / aborted");
		return false;

	case ECrewRunState::Running:
		if (Dst == ECrewRunState::Paused || Dst == ECrewRunState::Completed || Dst == ECrewRunState::Aborted) return true;
		OutReason = TEXT("running can only transition to paused / completed / aborted");
		return false;

	case ECrewRunState::Paused:
		if (Dst == ECrewRunState::Running || Dst == ECrewRunState::Completed || Dst == ECrewRunState::Aborted) return true;
		OutReason = TEXT("paused can only transition to running / completed / aborted");
		return false;

	case ECrewRunState::Completed:
	case ECrewRunState::Aborted:
		OutReason = TEXT("run is in a terminal state");
		return false;
	}
	OutReason = TEXT("unknown source state");
	return false;
}

bool FUECPCrewCoordinator::MarkRunReady(const FGuid& RunId)
{
	FCrewRun* Run = FindRunById(RunId);
	if (!Run) return false;
	FString Reason;
	if (!CanTransition(*Run, ECrewRunState::AwaitingPlanApproval, Reason))
	{
		UE_LOG(LogUECPCrew, Warning, TEXT("MarkRunReady: %s"), *Reason);
		return false;
	}
	Run->State = ECrewRunState::AwaitingPlanApproval;
	SaveRun_NoBroadcast(*Run);
	BroadcastRunChanged(*Run);
	return true;
}

bool FUECPCrewCoordinator::StartRun(const FGuid& RunId, FString& OutReason)
{
	FCrewRun* Run = FindRunById(RunId);
	if (!Run) { OutReason = TEXT("run not found"); return false; }
	if (!CanTransition(*Run, ECrewRunState::Running, OutReason)) return false;

	if (Run->Plan.Num() == 0)
	{
		OutReason = TEXT("run plan is empty — add at least one checkpoint before starting");
		return false;
	}

	bool bHasOrchestrator = false;
	for (const FCrewRole& R : Run->Roles)
	{
		if (R.bIsOrchestrator) { bHasOrchestrator = true; break; }
	}
	if (!bHasOrchestrator)
	{
		OutReason = TEXT("crew has no orchestrator role — exactly one role must have is_orchestrator=true");
		return false;
	}

	IUECPArchitectService& Arch = IUECPCoreModule::Get().GetArchitectService();
	TMap<FString, FString> StagedRoleChatIds;
	FString StagedOrchestratorChatId;
	TSet<FString> SeenChatIds;
	for (FCrewRole& Role : Run->Roles)
	{
		Arch.NewChat();
		const FString NewChatId = Arch.GetActiveChatID();
		if (NewChatId.IsEmpty())
		{
			OutReason = FString::Printf(
				TEXT("failed to create chat for role '%s' (architect service unavailable?)"),
				*Role.Name);
			return false;
		}
		if (SeenChatIds.Contains(NewChatId))
		{
			OutReason = FString::Printf(
				TEXT("chat id collision spawning role '%s' (id '%s' already used by another role)"),
				*Role.Name, *NewChatId);
			return false;
		}
		SeenChatIds.Add(NewChatId);

		StagedRoleChatIds.Add(Role.RoleId, NewChatId);
		if (Role.bIsOrchestrator) StagedOrchestratorChatId = NewChatId;

		Arch.SetChatDisplayName(NewChatId,
			FString::Printf(TEXT("[%s] %s"), *Run->DisplayName, *Role.Name));

		Arch.SetInteractionModeForChat(NewChatId, TEXT("auto"));

		UE_LOG(LogUECPCrew, Log, TEXT("StartRun: spawned role '%s' (orch=%d) → chat '%s'"),
			*Role.Name, Role.bIsOrchestrator ? 1 : 0, *NewChatId);
	}

	Run->RoleChatIds         = MoveTemp(StagedRoleChatIds);
	Run->OrchestratorChatId  = StagedOrchestratorChatId;

	Run->State = ECrewRunState::Running;
	Run->StartedAt = FDateTime::UtcNow();
	Run->PauseReason.Empty();
	SaveRun_NoBroadcast(*Run);
	BroadcastRunChanged(*Run);
	UE_LOG(LogUECPCrew, Log, TEXT("StartRun: %s now running (%d role chats)"),
		*Run->DisplayName, Run->RoleChatIds.Num());

	if (!Run->OrchestratorChatId.IsEmpty())
	{
		IUECPCoreModule::Get().GetArchitectService().SwitchChat(Run->OrchestratorChatId);
	}

	if (Router.IsValid())
	{
		FString OpenReason;
		if (!Router->OpenRun(Run->RunId, OpenReason))
		{
			UE_LOG(LogUECPCrew, Warning, TEXT("StartRun: OpenRun failed: %s"), *OpenReason);
		}
	}
	return true;
}

bool FUECPCrewCoordinator::PauseRun(const FGuid& RunId, const FString& Reason)
{
	FCrewRun* Run = FindRunById(RunId);
	if (!Run) return false;
	FString WhyNot;
	if (!CanTransition(*Run, ECrewRunState::Paused, WhyNot))
	{
		UE_LOG(LogUECPCrew, Warning, TEXT("PauseRun: %s"), *WhyNot);
		return false;
	}
	Run->State = ECrewRunState::Paused;
	if (!Reason.IsEmpty())             Run->PauseReason = Reason;
	else if (Run->PauseReason.IsEmpty()) Run->PauseReason = TEXT("user_pause");
	Run->PausedAt = FDateTime::UtcNow();

	IUECPArchitectService& Arch = IUECPCoreModule::Get().GetArchitectService();

	Run->PausedActiveRoleIds.Reset();
	for (const TPair<FString, FString>& Pair : Run->RoleChatIds)
	{
		if (!Pair.Value.IsEmpty() && Arch.IsThinkingForChat(Pair.Value))
		{
			Run->PausedActiveRoleIds.Add(Pair.Key);
		}
	}

	for (const TPair<FString, FString>& Pair : Run->RoleChatIds)
	{
		if (!Pair.Value.IsEmpty()) Arch.StopGenerationForChat(Pair.Value);
	}

	SaveRun_NoBroadcast(*Run);
	BroadcastRunChanged(*Run);
	return true;
}

bool FUECPCrewCoordinator::ResumeRun(const FGuid& RunId)
{
	FCrewRun* Run = FindRunById(RunId);
	if (!Run) return false;
	FString Reason;
	if (!CanTransition(*Run, ECrewRunState::Running, Reason))
	{
		UE_LOG(LogUECPCrew, Warning, TEXT("ResumeRun: %s"), *Reason);
		return false;
	}
	if (Run->PausedAt.GetTicks() > 0)
	{
		const FDateTime ResumeNow = FDateTime::UtcNow();
		const int64 PausedSeconds = FMath::Max<int64>(0, (ResumeNow - Run->PausedAt).GetTotalSeconds());
		if (PausedSeconds > 0)
		{
			for (FCrewCheckpoint& C : Run->Plan)
			{
				if (C.State == ECheckpointState::InProgress && C.TimeoutMinutes > 0)
					C.PausedSecondsTotal += PausedSeconds;
			}
		}
	}

	Run->State = ECrewRunState::Running;
	Run->PauseReason.Empty();
	Run->PausedAt = FDateTime{};

	IUECPArchitectService& Arch = IUECPCoreModule::Get().GetArchitectService();

	TSet<FString> DrainedChats;
	for (const TPair<FString, FString>& Pair : Run->RoleChatIds)
	{
		if (Pair.Value.IsEmpty()) continue;
		if (Arch.DrainQueueForChat(Pair.Value)) DrainedChats.Add(Pair.Value);
	}

	TMap<FString, FString> NameById;
	for (const FCrewRole& R : Run->Roles) NameById.Add(R.RoleId, R.Name);

	TArray<FString> RolesToWake = Run->PausedActiveRoleIds;
	if (RolesToWake.Num() == 0 && DrainedChats.Num() == 0)
	{
		if (Run->Handoffs.Num() > 0)
		{
			const FCrewHandoff& Last = Run->Handoffs.Last();
			if (!Last.ToRoleId.IsEmpty()) RolesToWake.Add(Last.ToRoleId);
		}
		if (RolesToWake.Num() == 0)
		{
			for (const FCrewRole& R : Run->Roles)
			{
				if (R.bIsOrchestrator) { RolesToWake.Add(R.RoleId); break; }
			}
		}
	}
	for (const FString& WakeRoleId : RolesToWake)
	{
		const FString* WakeChatPtr = Run->RoleChatIds.Find(WakeRoleId);
		if (!WakeChatPtr || WakeChatPtr->IsEmpty()) continue;
		if (DrainedChats.Contains(*WakeChatPtr)) continue;

		const FCrewRole* WakeRole = nullptr;
		for (const FCrewRole& R : Run->Roles)
			if (R.RoleId == WakeRoleId) { WakeRole = &R; break; }
		if (!WakeRole) continue;

		TArray<const FCrewHandoff*> Recent;
		for (int32 i = Run->Handoffs.Num() - 1; i >= 0 && Recent.Num() < 3; --i)
		{
			const FCrewHandoff& H = Run->Handoffs[i];
			if (H.FromRoleId == WakeRoleId || H.ToRoleId == WakeRoleId)
				Recent.Insert(&H, 0);
		}

		FString Wake = TEXT("[CREW RESUMED]\n");
		if (Recent.Num() > 0)
		{
			Wake += TEXT("Recent activity:\n");
			for (const FCrewHandoff* H : Recent)
			{
				const FString From = NameById.Contains(H->FromRoleId) ? NameById[H->FromRoleId] : H->FromRoleId;
				const FString To   = NameById.Contains(H->ToRoleId)   ? NameById[H->ToRoleId]   : H->ToRoleId;
				const FString Type = UECPCrew::HandoffTypeToString(H->Type);
				FString Preview = H->Content;
				Preview.ReplaceInline(TEXT("\n"), TEXT(" "));
				if (Preview.Len() > 200) Preview = Preview.Left(200) + TEXT("…");
				Wake += FString::Printf(TEXT("  %s → %s [%s]: %s\n"), *From, *To, *Type, *Preview);
			}
			Wake += TEXT("\n");
		}
		Wake += TEXT("Continue from where you left off.");

		Arch.SendMessageToChat(*WakeChatPtr, Wake, WakeRole->ApiKeySlotIndex);
	}

	Run->PausedActiveRoleIds.Reset();
	SaveRun_NoBroadcast(*Run);
	BroadcastRunChanged(*Run);
	return true;
}

void FUECPCrewCoordinator::PauseAllRunningOnShutdown()
{
	if (!IUECPCoreModule::IsAvailable()) return;

	const FDateTime Now = FDateTime::UtcNow();
	int32 PausedCount = 0;
	for (FCrewRun& Run : Runs)
	{
		if (Run.State != ECrewRunState::Running) continue;

		Run.State       = ECrewRunState::Paused;
		Run.PauseReason = TEXT("editor_close");
		Run.PausedAt    = Now;

		Run.PausedActiveRoleIds.Reset();
		if (IUECPCoreModule::IsAvailable())
		{
			IUECPArchitectService& Arch = IUECPCoreModule::Get().GetArchitectService();
			for (const TPair<FString, FString>& Pair : Run.RoleChatIds)
			{
				if (!Pair.Value.IsEmpty() && Arch.IsThinkingForChat(Pair.Value))
				{
					Run.PausedActiveRoleIds.Add(Pair.Key);
				}
			}
		}

		UECPCrew::Manifest::SaveRun(Run);
		++PausedCount;
	}

	if (PausedCount > 0)
	{
		UE_LOG(LogUECPCrew, Log,
			TEXT("OnPreExit: auto-paused %d Running run(s) for graceful next-session resume"),
			PausedCount);
	}
}

bool FUECPCrewCoordinator::AbortRun(const FGuid& RunId, const FString& Reason)
{
	FCrewRun* Run = FindRunById(RunId);
	if (!Run) return false;
	FString WhyNot;
	if (!CanTransition(*Run, ECrewRunState::Aborted, WhyNot))
	{
		UE_LOG(LogUECPCrew, Warning, TEXT("AbortRun: %s"), *WhyNot);
		return false;
	}
	Run->State       = ECrewRunState::Aborted;
	Run->EndedAt     = FDateTime::UtcNow();
	Run->PauseReason = Reason.IsEmpty() ? FString(TEXT("user_abort")) : Reason;
	FinaliseRunReport(*Run);

	IUECPArchitectService& Arch = IUECPCoreModule::Get().GetArchitectService();
	for (const TPair<FString, FString>& Pair : Run->RoleChatIds)
	{
		if (!Pair.Value.IsEmpty()) Arch.StopGenerationForChat(Pair.Value);
	}

	for (const TPair<FString, FString>& Pair : Run->RoleChatIds)
		FUECPCrewChatTokenStore::Get().ClearToken(Pair.Value);
	SaveRun_NoBroadcast(*Run);
	BroadcastRunChanged(*Run);
	if (Router.IsValid()) Router->ForgetRun(RunId);
	return true;
}

bool FUECPCrewCoordinator::CompleteRun(const FGuid& RunId, const FString& Summary)
{
	FCrewRun* Run = FindRunById(RunId);
	if (!Run) return false;
	FString WhyNot;
	if (!CanTransition(*Run, ECrewRunState::Completed, WhyNot))
	{
		UE_LOG(LogUECPCrew, Warning, TEXT("CompleteRun: %s"), *WhyNot);
		return false;
	}
	Run->State             = ECrewRunState::Completed;
	Run->EndedAt           = FDateTime::UtcNow();
	Run->PauseReason.Empty();
	FinaliseRunReport(*Run);
	// The orchestrator's summary is a claim; the grounded per-checkpoint report is appended so the
	// UI's completionSummary (and anyone reading the run JSON) sees what was actually verified.
	Run->CompletionSummary = Summary.IsEmpty()
		? UECPCrew::Manifest::FormatRunReportText(*Run)
		: FString::Printf(TEXT("%s\n\n%s"), *Summary, *UECPCrew::Manifest::FormatRunReportText(*Run));
	for (const TPair<FString, FString>& Pair : Run->RoleChatIds)
		FUECPCrewChatTokenStore::Get().ClearToken(Pair.Value);
	SaveRun_NoBroadcast(*Run);
	BroadcastRunChanged(*Run);
	if (Router.IsValid()) Router->ForgetRun(RunId);
	UE_LOG(LogUECPCrew, Log, TEXT("CompleteRun: %s completed (%s) — %s"),
		*Run->DisplayName, *Run->ReportGrounding, *Summary);
	return true;
}

bool FUECPCrewCoordinator::DeleteRun(const FGuid& RunId)
{
	FCrewRun* Run = FindRunById(RunId);
	if (!Run) return false;
	if (Run->State == ECrewRunState::Running || Run->State == ECrewRunState::Paused)
	{
		UE_LOG(LogUECPCrew, Warning,
			TEXT("DeleteRun: run %s is in-flight (state=%s) — abort it first"),
			*RunId.ToString(), *UECPCrew::RunStateToString(Run->State));
		return false;
	}
	UECPCrew::Manifest::DeleteRun(RunId);
	Runs.RemoveAll([&](const FCrewRun& R) { return R.RunId == RunId; });
	FCrewRun Stub;
	Stub.RunId = RunId;
	Stub.State = ECrewRunState::Aborted;
	BroadcastRunChanged(Stub);
	return true;
}

bool FUECPCrewCoordinator::AppendHandoff(const FGuid& RunId, const FCrewHandoff& Handoff)
{
	FCrewRun* Run = FindRunById(RunId);
	if (!Run) return false;
	Run->Handoffs.Add(Handoff);
	SaveRun_NoBroadcast(*Run);
	BroadcastRunChanged(*Run);
	return true;
}

bool FUECPCrewCoordinator::SetCheckpointState(const FGuid& RunId, const FString& CheckpointId,
	ECheckpointState NewState, const FString& ResultSummary)
{
	FCrewRun* Run = FindRunById(RunId);
	if (!Run) return false;
	for (FCrewCheckpoint& C : Run->Plan)
	{
		if (C.CheckpointId == CheckpointId)
		{
			if (NewState == ECheckpointState::InProgress && C.InstructionTime.GetTicks() == 0)
				C.InstructionTime = FDateTime::UtcNow();
			C.State         = NewState;
			C.ResultSummary = ResultSummary;
			SaveRun_NoBroadcast(*Run);
			BroadcastRunChanged(*Run);
			return true;
		}
	}
	return false;
}

bool FUECPCrewCoordinator::IsCrewChat(const FString& ChatId, FGuid& OutRunId, FString& OutRoleId) const
{
	if (ChatId.IsEmpty()) return false;
	if (const FChatRoleBinding* Found = ChatToRoleMap.Find(ChatId))
	{
		OutRunId  = Found->RunId;
		OutRoleId = Found->RoleId;
		return true;
	}
	return false;
}

bool FUECPCrewCoordinator::DefaultToolAllowedForKind(ECrewRoleKind Kind, FName ToolName) const
{
	const FString Name = ToolName.ToString();

	const bool bIsCrewTool = (Name == TEXT("crew")) || Name.StartsWith(TEXT("crew."));

	switch (Kind)
	{
	case ECrewRoleKind::Supervisor:
	{
		static const TSet<FString> DiscoveryTools = {
			TEXT("get_tool_docs"), TEXT("search_tools"), TEXT("find_tool"), TEXT("discover_tools")
		};
		return bIsCrewTool || DiscoveryTools.Contains(Name);
	}

	case ECrewRoleKind::Verifier:
		return bIsCrewTool || UECPToolSafety::IsReadOnly(ToolName);

	case ECrewRoleKind::Worker:
		return true;

	case ECrewRoleKind::Custom:
		return false;
	}
	return false;
}

bool FUECPCrewCoordinator::IsToolAllowedForChat(const FString& ChatId, FName ToolName, FString& OutDenyReason) const
{
	// Fail closed whenever the caller *could* be a crew chat but cannot be resolved. A chat with no
	// binding is a plain (non-crew) chat — this gate is consulted for every chat by the agent loops,
	// so "no binding" must stay allowed or every normal chat would lose its tools.
	if (ChatId.IsEmpty())
	{
		OutDenyReason = TEXT("caller chat id is missing — crew tool gating cannot identify the caller (fail-closed)");
		return false;
	}
	const FChatRoleBinding* Bind = ChatToRoleMap.Find(ChatId);
	if (!Bind) return true;

	const FCrewRun* Run = FindRunById(Bind->RunId);
	if (!Run)
	{
		OutDenyReason = FString::Printf(
			TEXT("crew chat is bound to run %s which no longer exists — tools denied (fail-closed); end your turn"),
			*Bind->RunId.ToString(EGuidFormats::DigitsWithHyphens));
		return false;
	}

	if (Run->State != ECrewRunState::Running)
	{
		OutDenyReason = FString::Printf(
			TEXT("crew run is %s — stop calling tools and end your turn; you'll be woken when the run resumes"),
			*UECPCrew::RunStateToString(Run->State));
		return false;
	}

	const FCrewRole* Role = nullptr;
	for (const FCrewRole& R : Run->Roles) if (R.RoleId == Bind->RoleId) { Role = &R; break; }
	if (!Role)
	{
		OutDenyReason = FString::Printf(
			TEXT("crew chat is bound to role '%s' which is not on run '%s' — tools denied (fail-closed); end your turn"),
			*Bind->RoleId, *Run->DisplayName);
		return false;
	}

	const FCrewCheckpoint* ActiveCp = nullptr;
	for (int32 i = Run->Handoffs.Num() - 1; i >= 0; --i)
	{
		const FCrewHandoff& H = Run->Handoffs[i];
		if (H.Type == EHandoffType::Instruction && H.ToRoleId == Role->RoleId)
		{
			for (const FCrewCheckpoint& C : Run->Plan)
				if (C.CheckpointId == H.CheckpointId && C.State == ECheckpointState::InProgress) { ActiveCp = &C; break; }
			break;
		}
	}
	if (ActiveCp && ActiveCp->ToolAllowlistOverride.Num() > 0)
	{
		const FString Name = ToolName.ToString();
		if (Name == TEXT("crew") || Name.StartsWith(TEXT("crew."))) return true;
		if (ActiveCp->ToolAllowlistOverride.Contains(Name)) return true;
		OutDenyReason = FString::Printf(
			TEXT("checkpoint '%s' restricts tools to its override list — '%s' is not on it. "
			     "Either use a permitted tool or report status='fail' explaining the gap."),
			*ActiveCp->CheckpointId, *Name);
		return false;
	}

	if (Role->ToolAllowlist.Num() > 0)
	{
		const FString Name = ToolName.ToString();
		if (Role->ToolAllowlist.Contains(Name)) return true;
		OutDenyReason = FString::Printf(
			TEXT("role '%s' allowlist does not include '%s'. If this is the right action and you need access, "
			     "report status='fail' explaining the missing permission. If you're not sure the action exists, "
			     "call get_tool_docs(categories=['<umbrella>']) to see real action names — never guess."),
			*Role->Name, *Name);
		return false;
	}

	if (DefaultToolAllowedForKind(Role->Kind, ToolName)) return true;

	const bool bRegistered = IUECPCoreModule::IsAvailable()
		&& IUECPCoreModule::Get().GetToolDispatcher().IsRegistered(ToolName);
	if (!bRegistered)
	{
		OutDenyReason = FString::Printf(
			TEXT("no action named '%s' is registered — this looks like a guessed action. "
			     "Call get_tool_docs(categories=['<umbrella>']) to see the real action list, then "
			     "re-call with a valid name. If no real action fits the task, end your turn with "
			     "crew(action='report_back', status='fail', summary='...') explaining the gap."),
			*ToolName.ToString());
		return false;
	}

	const FString KindRestriction = (Role->Kind == ECrewRoleKind::Supervisor)
		? TEXT("Orchestrators may only call the crew umbrella plus read-only discovery helpers "
		       "(get_tool_docs / search_tools / find_tool / discover_tools) — dispatch the work to a role instead.")
		: TEXT("This role is restricted to read-only inspection (get_*, list_*, find_*, inspect_*, verify_*, validate_*).");
	OutDenyReason = FString::Printf(
		TEXT("role '%s' (kind=%s) does not permit '%s'. %s If this is genuinely needed, "
		     "report status='fail' explaining the gap — don't try variants. To see what this "
		     "role CAN call, fetch get_tool_docs(categories=['<umbrella>'])."),
		*Role->Name,
		*UECPCrew::RoleKindToString(Role->Kind),
		*ToolName.ToString(),
		*KindRestriction);
	return false;
}

bool FUECPCrewCoordinator::RouteInstructionDispatch(const FGuid& RunId, const FString& FromRoleId,
	const FString& ToRoleId, const FString& Instruction, const FString& CheckpointId,
	FString& OutDenyReason)
{
	if (!Router.IsValid()) { OutDenyReason = TEXT("router not initialised"); return false; }
	return Router->DispatchInstruction(RunId, FromRoleId, ToRoleId, Instruction, CheckpointId, OutDenyReason);
}

bool FUECPCrewCoordinator::RouteRoleReport(const FGuid& RunId, const FString& FromRoleId,
	const FString& CheckpointId, const FString& Status, const FString& Summary,
	FString& OutDenyReason)
{
	// Interface path (no verification text). Still an explicit report — only the router's turn-end
	// synthesis files non-explicit ones.
	return RouteRoleReportVerified(RunId, FromRoleId, CheckpointId, Status, Summary, FString(), OutDenyReason);
}

bool FUECPCrewCoordinator::RouteRoleReportVerified(const FGuid& RunId, const FString& FromRoleId,
	const FString& CheckpointId, const FString& Status, const FString& Summary,
	const FString& Verification, FString& OutDenyReason)
{
	if (!Router.IsValid()) { OutDenyReason = TEXT("router not initialised"); return false; }
	return Router->DeliverReport(RunId, FromRoleId, CheckpointId, Status, Summary,
		Verification, /*bExplicit=*/true, OutDenyReason);
}

bool FUECPCrewCoordinator::RouteRoleQuestion(const FGuid& RunId, const FString& FromRoleId,
	const FString& Question, FString& OutDenyReason)
{
	if (!Router.IsValid()) { OutDenyReason = TEXT("router not initialised"); return false; }
	return Router->DeliverQuestion(RunId, FromRoleId, Question, OutDenyReason);
}

void FUECPCrewCoordinator::MarkTurnTaken(const FGuid& RunId, const FString& RoleId, bool bSuccess)
{
	FCrewRun* Run = FindRunById(RunId);
	if (!Run) return;
	if (Run->State != ECrewRunState::Running) return;

	++Run->TotalTurnsTaken;

	if (bSuccess) Run->ConsecutiveErrorCount = 0;
	else          ++Run->ConsecutiveErrorCount;

	int32& RoleCount = Run->ConsecutiveErrorsByRole.FindOrAdd(RoleId);
	if (bSuccess) RoleCount = 0;
	else          ++RoleCount;

	AccumulateTurnTokens(*Run, RoleId);
	if (Run->Caps.MaxTotalTokens > 0 && Run->TotalTokensUsed >= Run->Caps.MaxTotalTokens)
	{
		const FString TokenReason = FString::Printf(TEXT("cap_hit:tokens (%lld >= %lld)"),
			(long long)Run->TotalTokensUsed, (long long)Run->Caps.MaxTotalTokens);
		FCrewHandoff Sys;
		Sys.Type    = EHandoffType::System;
		Sys.Content = FString::Printf(
			TEXT("Run aborted: token budget exceeded (%lld used, cap %lld)."),
			(long long)Run->TotalTokensUsed, (long long)Run->Caps.MaxTotalTokens);
		Run->Handoffs.Add(Sys);
		SaveRun_NoBroadcast(*Run);
		UE_LOG(LogUECPCrew, Warning, TEXT("MarkTurnTaken: %s aborted — %s"), *Run->DisplayName, *TokenReason);
		const FGuid AbortId = Run->RunId;
		AbortRun(AbortId, TokenReason);
		return;
	}

	FString CapReason;
	if (Run->TotalTurnsTaken >= Run->Caps.MaxTotalTurns)
	{
		CapReason = FString::Printf(TEXT("cap_hit:turns (%d)"), Run->TotalTurnsTaken);
	}
	else if (Run->Caps.MaxConsecutiveErrors > 0
	      && Run->ConsecutiveErrorCount >= Run->Caps.MaxConsecutiveErrors)
	{
		CapReason = FString::Printf(TEXT("cap_hit:consecutive_errors (%d)"),
			Run->ConsecutiveErrorCount);
	}
	else
	{
		const int32 PerRoleCap = Run->Caps.MaxConsecutiveErrorsPerRole > 0
			? Run->Caps.MaxConsecutiveErrorsPerRole
			: Run->Caps.MaxConsecutiveErrors;
		if (PerRoleCap > 0 && RoleCount >= PerRoleCap)
		{
			CapReason = FString::Printf(TEXT("cap_hit:role_errors:%s (%d)"), *RoleId, RoleCount);
		}
	}

	if (CapReason.IsEmpty() && Run->Caps.MaxWallMinutes > 0)
	{
		const FTimespan Elapsed = FDateTime::UtcNow() - Run->StartedAt;
		if (Elapsed.GetTotalMinutes() >= Run->Caps.MaxWallMinutes)
		{
			CapReason = FString::Printf(TEXT("cap_hit:wall_time (%.1f min)"), Elapsed.GetTotalMinutes());
		}
	}

	if (!CapReason.IsEmpty())
	{
		Run->State       = ECrewRunState::Paused;
		Run->PauseReason = CapReason;
		Run->PausedAt    = FDateTime::UtcNow();
		UE_LOG(LogUECPCrew, Log, TEXT("MarkTurnTaken: %s auto-paused — %s"),
			*Run->DisplayName, *CapReason);

		IUECPArchitectService& Arch = IUECPCoreModule::Get().GetArchitectService();
		Run->PausedActiveRoleIds.Reset();
		for (const TPair<FString, FString>& Pair : Run->RoleChatIds)
		{
			if (!Pair.Value.IsEmpty() && Arch.IsThinkingForChat(Pair.Value))
				Run->PausedActiveRoleIds.Add(Pair.Key);
		}
		for (const TPair<FString, FString>& Pair : Run->RoleChatIds)
		{
			if (!Pair.Value.IsEmpty()) Arch.StopGenerationForChat(Pair.Value);
		}
	}

	SaveRun_NoBroadcast(*Run);
	BroadcastRunChanged(*Run);
}

void FUECPCrewCoordinator::EscalateAndPause(FCrewRun& Run, const FString& Tag, const FString& Question)
{
	const FCrewRole* Orchestrator = nullptr;
	for (const FCrewRole& R : Run.Roles)
		if (R.bIsOrchestrator) { Orchestrator = &R; break; }

	FCrewHandoff H;
	H.FromRoleId = Orchestrator ? Orchestrator->RoleId : FString(TEXT("system"));
	H.ToRoleId   = TEXT("user");
	H.Type       = EHandoffType::Question;
	H.Content    = Question;
	Run.Handoffs.Add(H);

	// "user_escalation" prefix is what AnswerEscalation checks; the tag documents why.
	Run.PauseReason = FString::Printf(TEXT("user_escalation:%s"), *Tag);

	if (Run.State == ECrewRunState::Running)
	{
		Run.State    = ECrewRunState::Paused;
		Run.PausedAt = FDateTime::UtcNow();
		Run.PausedActiveRoleIds.Reset();
		if (IUECPCoreModule::IsAvailable())
		{
			IUECPArchitectService& Arch = IUECPCoreModule::Get().GetArchitectService();
			for (const TPair<FString, FString>& Pair : Run.RoleChatIds)
			{
				if (!Pair.Value.IsEmpty() && Arch.IsThinkingForChat(Pair.Value))
					Run.PausedActiveRoleIds.Add(Pair.Key);
			}
			for (const TPair<FString, FString>& Pair : Run.RoleChatIds)
			{
				if (!Pair.Value.IsEmpty()) Arch.StopGenerationForChat(Pair.Value);
			}
		}
	}

	SaveRun_NoBroadcast(Run);
	BroadcastRunChanged(Run);
	UE_LOG(LogUECPCrew, Warning, TEXT("EscalateAndPause: %s — %s"), *Run.DisplayName, *Run.PauseReason);
}

void FUECPCrewCoordinator::AccumulateTurnTokens(FCrewRun& Run, const FString& RoleId)
{
	const FString* ChatIdPtr = Run.RoleChatIds.Find(RoleId);
	if (!ChatIdPtr || ChatIdPtr->IsEmpty()) return;
	if (!IUECPCoreModule::IsAvailable()) return;
	IUECPArchitectService& Arch = IUECPCoreModule::Get().GetArchitectService();

	const bool bFirstSighting = !TokenCursorByChat.Contains(*ChatIdPtr);
	FTokenCursor& Cursor = TokenCursorByChat.FindOrAdd(*ChatIdPtr);

	// Preferred: provider-reported totals the shell keeps per conversation (FConversationInfo).
	int32 Reported = 0;
	for (const TSharedPtr<FConversationInfo>& Conv : Arch.GetConversationList())
	{
		if (Conv.IsValid() && Conv->ID == *ChatIdPtr) { Reported = Conv->TotalTokens; break; }
	}

	const TArray<TSharedPtr<FJsonValue>> Hist = Arch.GetConversationHistoryForChat(*ChatIdPtr);

	// A run resumed after an editor restart already carries a persisted total; the in-memory cursor
	// is fresh, so its first sighting only sets the baseline — otherwise the whole chat would be
	// counted a second time.
	if (bFirstSighting && Run.TotalTokensUsed > 0)
	{
		Cursor.LastReportedTotal = Reported;
		Cursor.LastHistoryNum    = Hist.Num();
		return;
	}

	int64 Delta = 0;
	if (Reported > Cursor.LastReportedTotal)
	{
		Delta = (int64)(Reported - Cursor.LastReportedTotal);
		Cursor.LastReportedTotal = Reported;
	}
	else
	{
		// Fallback: chars/4 over the history entries appended since the previous turn of this chat.
		for (int32 i = FMath::Max(0, Cursor.LastHistoryNum); i < Hist.Num(); ++i)
			Delta += EstimateEntryTokens(Hist[i]);
	}
	Cursor.LastHistoryNum = Hist.Num();
	Run.TotalTokensUsed += FMath::Max<int64>(0, Delta);
}

void FUECPCrewCoordinator::FinaliseRunReport(FCrewRun& Run)
{
	Run.ReportGrounding = UECPCrew::Manifest::ComputeReportGrounding(Run);
}

TArray<FString> FUECPCrewCoordinator::CollectReadOnlyToolEvidence(const FString& ChatId) const
{
	TArray<FString> Out;
	if (ChatId.IsEmpty() || !IUECPCoreModule::IsAvailable()) return Out;

	const TArray<TSharedPtr<FJsonValue>> Hist =
		IUECPCoreModule::Get().GetArchitectService().GetConversationHistoryForChat(ChatId);

	// The turn starts at the most recent crew instruction / briefing delivered to this chat.
	int32 StartIdx = 0;
	for (int32 i = Hist.Num() - 1; i >= 0; --i)
	{
		FString Role, Text;
		if (!ReadHistoryEntryText(Hist[i], Role, Text)) continue;
		if (Role != TEXT("user")) continue;
		if (Text.StartsWith(TEXT("[CREW INSTRUCTION")) || Text.StartsWith(TEXT("[CREW BRIEFING")))
		{
			StartIdx = i + 1;
			break;
		}
	}

	// Tool bubbles are written by the architect coordinator as
	//   "[TOOL_RESULT:<label>:success|error]\n<args preview>\n---\n<result>"
	// where <label> is "<umbrella> · <action>" or a bare tool name. The result separator only
	// appears once the tool has actually returned.
	static const FString BubblePrefix = TEXT("[TOOL_RESULT:");
	static const FString ResultSep    = TEXT("\n---\n");
	const FString ActionSep = FString::Printf(TEXT(" %c "), (TCHAR)0xB7);
	static const TSet<FString> NonEvidenceTools = {
		TEXT("get_tool_docs"), TEXT("search_tools"), TEXT("find_tool"), TEXT("discover_tools")
	};

	for (int32 i = StartIdx; i < Hist.Num(); ++i)
	{
		FString Role, Text;
		if (!ReadHistoryEntryText(Hist[i], Role, Text)) continue;
		if (Role != TEXT("user") || !Text.StartsWith(BubblePrefix)) continue;
		if (!Text.Contains(ResultSep)) continue;

		FString FirstLine = Text;
		int32 NewlineIdx = INDEX_NONE;
		if (FirstLine.FindChar(TEXT('\n'), NewlineIdx)) FirstLine.LeftInline(NewlineIdx);
		if (!FirstLine.EndsWith(TEXT("]"))) continue;

		const FString Inner = FirstLine.Mid(BubblePrefix.Len(), FirstLine.Len() - BubblePrefix.Len() - 1);
		int32 LastColon = INDEX_NONE;
		if (!Inner.FindLastChar(TEXT(':'), LastColon)) continue;
		if (Inner.Mid(LastColon + 1) != TEXT("success")) continue;

		FString Action = Inner.Left(LastColon);
		const int32 SepIdx = Action.Find(ActionSep);
		if (SepIdx != INDEX_NONE) Action = Action.Mid(SepIdx + ActionSep.Len());
		Action.TrimStartAndEndInline();
		if (Action.IsEmpty()) continue;

		if (Action == TEXT("crew") || Action.StartsWith(TEXT("crew."))) continue;
		if (NonEvidenceTools.Contains(Action)) continue;
		if (!UECPToolSafety::IsReadOnly(FName(*Action))) continue;

		Out.AddUnique(Action);
	}
	return Out;
}

bool FUECPCrewCoordinator::NoteInstructionDispatched(const FGuid& RunId, const FString& Instruction, FString& OutDenyReason)
{
	FCrewRun* Run = FindRunById(RunId);
	if (!Run) return true;

	const FString Norm = UECPCrew::NormaliseInstructionText(Instruction);
	Run->RecentInstructions.Add(Norm);
	while (Run->RecentInstructions.Num() > UECPCrew::RecentInstructionWindow)
		Run->RecentInstructions.RemoveAt(0);

	int32 Repeats = 0;
	for (const FString& S : Run->RecentInstructions) if (S == Norm) ++Repeats;

	if (Repeats >= UECPCrew::OscillationRepeatThreshold)
	{
		FString Snip = Instruction;
		Snip.ReplaceInline(TEXT("\r"), TEXT(" "));
		Snip.ReplaceInline(TEXT("\n"), TEXT(" "));
		if (Snip.Len() > 200) Snip = Snip.Left(199) + TEXT("…");

		Run->RecentInstructions.Reset();   // so a resumed run is not re-tripped by the same window
		EscalateAndPause(*Run, TEXT("oscillation"), FString::Printf(
			TEXT("The orchestrator has dispatched the same instruction %d times within its last %d dispatches — the crew is looping without progress.\n")
			TEXT("Repeated instruction: \"%s\"\n")
			TEXT("How should the crew proceed? (Give different guidance, tell the orchestrator to mark the checkpoint failed and move on, or abort the run.)"),
			Repeats, UECPCrew::RecentInstructionWindow, *Snip));

		OutDenyReason = FString::Printf(
			TEXT("oscillation detected: this same instruction has now been dispatched %d times — the run is paused and escalated to the user; do not re-send it"),
			Repeats);
		return false;
	}

	SaveRun_NoBroadcast(*Run);
	return true;
}

void FUECPCrewCoordinator::RecordReportEvidence(const FGuid& RunId, const FString& CheckpointId, const FString& Status,
	const FString& Verification, const TArray<FString>& ToolCalls)
{
	FCrewRun* Run = FindRunById(RunId);
	if (!Run || CheckpointId.IsEmpty()) return;
	for (FCrewCheckpoint& C : Run->Plan)
	{
		if (C.CheckpointId != CheckpointId) continue;
		C.LastReportStatus = Status;
		if (!Verification.IsEmpty()) C.VerificationText = Verification;
		for (const FString& T : ToolCalls) C.VerificationToolCalls.AddUnique(T);
		SaveRun_NoBroadcast(*Run);
		return;
	}
}

bool FUECPCrewCoordinator::MarkCheckpointGrounded(const FGuid& RunId, const FString& CheckpointId, bool bGrounded)
{
	FCrewRun* Run = FindRunById(RunId);
	if (!Run) return false;
	for (FCrewCheckpoint& C : Run->Plan)
	{
		if (C.CheckpointId != CheckpointId) continue;
		C.bGrounded = bGrounded;
		SaveRun_NoBroadcast(*Run);
		return true;
	}
	return false;
}

void FUECPCrewCoordinator::RefreshChatRoleMap()
{
	ChatToRoleMap.Reset();
	for (const FCrewRun& Run : Runs)
	{
		if (Run.State != ECrewRunState::Running && Run.State != ECrewRunState::Paused) continue;

		for (const TPair<FString, FString>& P : Run.RoleChatIds)
		{
			if (P.Value.IsEmpty()) continue;
			FChatRoleBinding B;
			B.RunId  = Run.RunId;
			B.RoleId = P.Key;
			ChatToRoleMap.Add(P.Value, B);
		}
	}
}

void FUECPCrewCoordinator::SaveRun_NoBroadcast(const FCrewRun& Run)
{
	UECPCrew::Manifest::SaveRun(Run);
}

void FUECPCrewCoordinator::BroadcastRunChanged(const FCrewRun& Run)
{
	RefreshChatRoleMap();
	RunChangedEvent.Broadcast(Run);
}

void FUECPCrewCoordinator::BroadcastTemplatesChanged()
{
	TemplatesChangedEvent.Broadcast();
}
