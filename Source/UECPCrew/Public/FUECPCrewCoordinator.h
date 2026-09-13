// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "Services/IUECPCrewService.h"
#include "Types/CrewTypes.h"
#include "Templates/UniquePtr.h"
#include "Containers/Ticker.h"

namespace UECPCrew { class FCrewHandoffRouter; }

class SUECPMainWidget;
class UUECPAppBridge;

class UECPCREW_API FUECPCrewCoordinator : public IUECPCrewService
{
public:
	FUECPCrewCoordinator();
	virtual ~FUECPCrewCoordinator() override;

	void Initialize();

	virtual void InitializeShellRefs(TWeakPtr<SUECPMainWidget> Shell,
		TWeakObjectPtr<UUECPAppBridge> Bridge) override;

	virtual TArray<FCrewTemplate> GetTemplates() const override;
	virtual bool                  GetTemplate(const FString& TemplateId, FCrewTemplate& OutTemplate) const override;
	virtual FString               CloneTemplate(const FString& SourceTemplateId, const FString& NewDisplayName) override;
	virtual FString               CreateTemplate(const FString& DisplayName) override;
	virtual bool                  UpdateTemplate(const FCrewTemplate& Template) override;
	virtual bool                  DeleteTemplate(const FString& TemplateId) override;

	virtual TArray<FCrewRun> GetRuns() const override;
	virtual bool             GetRun(const FGuid& RunId, FCrewRun& OutRun) const override;
	virtual FGuid            GetActiveRunId() const override;

	virtual FGuid CreateRun(const FString& TemplateId, const FString& DisplayName) override;
	virtual bool  SetRunRoles(const FGuid& RunId, const TArray<FCrewRole>& Roles) override;
	virtual bool  SetRunPlan(const FGuid& RunId, const TArray<FCrewCheckpoint>& Plan,
		TArray<FString>* OutErrors = nullptr) override;
	virtual bool  RetryCheckpoint(const FGuid& RunId, const FString& CheckpointId) override;
	virtual bool  SetRoleApiKeySlot(const FGuid& RunId, const FString& RoleId, int32 SlotIndex) override;
	virtual bool  AnswerEscalation(const FGuid& RunId, const FString& AnswerText) override;
	virtual FGuid ForkRun(const FGuid& SourceRunId, const FString& NewDisplayName) override;
	virtual bool  SetRunCaps(const FGuid& RunId, const FCrewRunCaps& Caps) override;
	virtual bool  SetRunDisplayName(const FGuid& RunId, const FString& DisplayName) override;

	virtual bool MarkRunReady(const FGuid& RunId) override;
	virtual bool StartRun(const FGuid& RunId, FString& OutReason) override;
	virtual bool PauseRun(const FGuid& RunId, const FString& Reason = FString()) override;
	virtual bool ResumeRun(const FGuid& RunId) override;
	virtual bool AbortRun(const FGuid& RunId, const FString& Reason) override;
	virtual bool CompleteRun(const FGuid& RunId, const FString& Summary) override;
	virtual bool DeleteRun(const FGuid& RunId) override;

	virtual bool AppendHandoff(const FGuid& RunId, const FCrewHandoff& Handoff) override;
	virtual bool SetCheckpointState(const FGuid& RunId, const FString& CheckpointId,
		ECheckpointState NewState, const FString& ResultSummary) override;

	virtual bool IsCrewChat(const FString& ChatId, FGuid& OutRunId, FString& OutRoleId) const override;
	virtual bool IsToolAllowedForChat(const FString& ChatId, FName ToolName, FString& OutDenyReason) const override;
	virtual bool SetRoleAllowlistOnRun(const FGuid& RunId, const FString& RoleId,
		const TArray<FString>& Allowlist) override;
	virtual bool ShouldAutoApproveDestructiveForChat(const FString& ChatId) const override;
	virtual bool ExportTemplate(const FString& TemplateId, FString& OutJson) const override;
	virtual FString ImportTemplate(const FString& Json) override;

	virtual bool RouteInstructionDispatch(const FGuid& RunId, const FString& FromRoleId,
		const FString& ToRoleId, const FString& Instruction, const FString& CheckpointId,
		FString& OutDenyReason) override;
	virtual bool RouteRoleReport(const FGuid& RunId, const FString& FromRoleId,
		const FString& CheckpointId, const FString& Status, const FString& Summary,
		FString& OutDenyReason) override;
	virtual bool RouteRoleQuestion(const FGuid& RunId, const FString& FromRoleId,
		const FString& Question, FString& OutDenyReason) override;

	void MarkTurnTaken(const FGuid& RunId, const FString& RoleId, bool bSuccess);

	// ---- hardening API (module-internal: IUECPCrewService lives in UECPCore and is not extended) ----

	// Explicit crew.report_back with an optional verification statement. The router attaches the
	// read-only tool evidence observed in the role's current turn to the Result handoff.
	bool RouteRoleReportVerified(const FGuid& RunId, const FString& FromRoleId,
		const FString& CheckpointId, const FString& Status, const FString& Summary,
		const FString& Verification, FString& OutDenyReason);

	// RetryCheckpoint with a human-readable reason. Enforces Caps.MaxRetriesPerCheckpoint: beyond the
	// cap the checkpoint is marked Failed and the run is paused with a user escalation.
	bool RetryCheckpointEx(const FGuid& RunId, const FString& CheckpointId, FString& OutReason);

	// Read-only tool actions that completed successfully in the chat since the last crew instruction
	// (parsed from the chat's canonical history; crew.* and discovery helpers are excluded).
	TArray<FString> CollectReadOnlyToolEvidence(const FString& ChatId) const;

	// Oscillation detector hook — call before delivering an instruction. Returns false (and pauses the
	// run with an escalation) when the same normalised instruction repeats OscillationRepeatThreshold
	// times inside the RecentInstructionWindow.
	bool NoteInstructionDispatched(const FGuid& RunId, const FString& Instruction, FString& OutDenyReason);

	// Records the latest report's status / verification / evidence on the checkpoint (no broadcast —
	// the caller appends the handoff right after, which broadcasts).
	void RecordReportEvidence(const FGuid& RunId, const FString& CheckpointId, const FString& Status,
		const FString& Verification, const TArray<FString>& ToolCalls);

	// Marks a checkpoint's grounded flag (no broadcast — pair with SetCheckpointState).
	bool MarkCheckpointGrounded(const FGuid& RunId, const FString& CheckpointId, bool bGrounded);

	virtual FOnCrewRunChanged&        OnRunChanged()       override { return RunChangedEvent; }
	virtual FOnCrewTemplatesChanged&  OnTemplatesChanged() override { return TemplatesChangedEvent; }

private:
	// Adds a Question handoff addressed to the user and pauses with PauseReason "user_escalation:<Tag>"
	// (so AnswerEscalation accepts the reply). Safe to call when the run is already Paused.
	void EscalateAndPause(FCrewRun& Run, const FString& Tag, const FString& Question);

	// Folds the role chat's token usage since its last turn into Run.TotalTokensUsed.
	void AccumulateTurnTokens(FCrewRun& Run, const FString& RoleId);

	// Computes Run.ReportGrounding from the plan (called when a run reaches a terminal state).
	void FinaliseRunReport(FCrewRun& Run);

	struct FTokenCursor
	{
		int32 LastReportedTotal = 0;
		int32 LastHistoryNum    = 0;
	};
	TMap<FString, FTokenCursor> TokenCursorByChat;

	const FCrewTemplate* FindTemplateById(const FString& TemplateId) const;
	FCrewRun*            FindRunById(const FGuid& RunId);
	const FCrewRun*      FindRunById(const FGuid& RunId) const;

	bool CanTransition(const FCrewRun& Run, ECrewRunState Dst, FString& OutReason) const;

	void SaveRun_NoBroadcast(const FCrewRun& Run);
	void BroadcastRunChanged(const FCrewRun& Run);
	void BroadcastTemplatesChanged();

	void RefreshChatRoleMap();

	bool DefaultToolAllowedForKind(ECrewRoleKind Kind, FName ToolName) const;

	TArray<FCrewTemplate> BuiltInTemplates;

	TArray<FCrewTemplate> UserTemplates;

	TArray<FCrewRun> Runs;

	FOnCrewRunChanged       RunChangedEvent;
	FOnCrewTemplatesChanged TemplatesChangedEvent;

	struct FChatRoleBinding
	{
		FGuid    RunId;
		FString  RoleId;
	};
	TMap<FString, FChatRoleBinding> ChatToRoleMap;

	TUniquePtr<UECPCrew::FCrewHandoffRouter> Router;

	FTSTicker::FDelegateHandle TimeoutTickerHandle;
	bool TickCheckpointTimeouts(float DeltaSeconds);

	FDelegateHandle PreExitHandle;
	void PauseAllRunningOnShutdown();
};
