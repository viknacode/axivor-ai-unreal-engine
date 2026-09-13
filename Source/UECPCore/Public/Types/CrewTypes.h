// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"

enum class ECrewRoleKind : uint8
{
	Supervisor,
	Worker,
	Verifier,
	Custom
};

enum class ECheckpointState : uint8
{
	Pending,
	InProgress,
	Passed,
	Failed,
	Skipped
};

enum class EHandoffType : uint8
{
	Instruction,
	Result,
	Question,
	Answer,
	CheckpointStatus,
	System
};

enum class ECrewRunState : uint8
{
	Draft,
	AwaitingPlanApproval,
	Running,
	Paused,
	Completed,
	Aborted
};

struct FCrewRole
{
	FString             RoleId;
	FString             Name;
	ECrewRoleKind       Kind = ECrewRoleKind::Worker;
	FString             ProviderName;
	FString             ModelOverride;
	FString             SystemPromptOverride;
	TArray<FString>     ToolAllowlist;
	bool                bIsOrchestrator = false;

	int32               ApiKeySlotIndex = -1;

	FCrewRole() = default;
};

struct FCrewCheckpoint
{
	FString             CheckpointId;
	FString             Description;
	FString             SuccessCriteria;
	ECheckpointState    State = ECheckpointState::Pending;
	FString             ResultSummary;

	TArray<FString>     RequiredRoleIds;

	TArray<FString>     DependsOn;

	int32               TimeoutMinutes = 0;

	TArray<FString>     ToolAllowlistOverride;

	FDateTime           InstructionTime;

	int64               PausedSecondsTotal = 0;

	FDateTime           RetriedAt;

	// --- grounding / hardening (all defaulted so pre-existing run JSON loads unchanged) ---

	// Times crew.checkpoint_retry (or the UI) reset this checkpoint. Capped by
	// FCrewRunCaps::MaxRetriesPerCheckpoint; exceeding the cap fails the checkpoint and escalates.
	int32               RetryCount = 0;

	// Status of the most recent report filed against this checkpoint:
	// "success" | "fail" (explicit crew.report_back) | "unreported" (synthesised at turn end) | "" (none yet).
	FString             LastReportStatus;

	// Free text from crew.report_back(verification=...) naming the read-back results behind the claim.
	FString             VerificationText;

	// Read-only tool actions that completed successfully in the reporting turn(s) since the last retry.
	TArray<FString>     VerificationToolCalls;

	// True only when the checkpoint was passed on an explicit success report backed by read-back evidence.
	bool                bGrounded = false;

	FCrewCheckpoint() = default;
};

struct FCrewTemplate
{
	FString             TemplateId;
	FString             DisplayName;
	bool                bBuiltIn = false;
	TArray<FCrewRole>   Roles;
	FString             DefaultPlanScaffold;

	TArray<FCrewCheckpoint> DefaultPlan;

	FCrewTemplate() = default;
};

struct FCrewHandoff
{
	FGuid           Id;
	FString         FromRoleId;
	FString         ToRoleId;
	EHandoffType    Type = EHandoffType::Instruction;
	FString         CheckpointId;
	FString         Content;
	int64           TimestampUnix = 0;

	// Result handoffs only. True when the role called crew.report_back itself; false for reports the
	// router synthesised at turn end (those carry status=unreported and never satisfy checkpoint_pass).
	bool            bExplicitReport = false;

	// Result handoffs only: report_back(verification=...) text and the successful read-only tool
	// actions observed in the reporting turn — the evidence checkpoint_pass requires.
	FString         Verification;
	TArray<FString> VerificationToolCalls;

	FCrewHandoff()
		: Id(FGuid::NewGuid())
		, TimestampUnix(FDateTime::UtcNow().ToUnixTimestamp())
	{
	}
};

struct FCrewRunCaps
{
	int32   MaxTotalTurns       = 200;
	int32   MaxConsecutiveErrors = 5;

	int32   MaxConsecutiveErrorsPerRole = 0;
	int32   MaxWallMinutes      = 60;

	// Retries allowed per checkpoint via crew.checkpoint_retry / UI. 0 = unlimited.
	int32   MaxRetriesPerCheckpoint = 3;

	// Token budget for the whole run (summed over role chats). 0 = off. Exceeding it aborts the run.
	int64   MaxTotalTokens      = 0;

	// Off by default: destructive tools in crew role chats go through the normal confirmation unless
	// the settings UI explicitly turns this on for the run.
	bool    bAutoApproveDestructive = false;

	FCrewRunCaps() = default;
};

struct FCrewRun
{
	FGuid                       RunId;
	FString                     DisplayName;
	FString                     CrewTemplateId;
	TArray<FCrewRole>           Roles;
	TArray<FCrewCheckpoint>     Plan;
	TArray<FCrewHandoff>        Handoffs;
	FCrewRunCaps                Caps;
	ECrewRunState               State = ECrewRunState::Draft;

	FString                     OrchestratorChatId;
	TMap<FString, FString>      RoleChatIds;

	FDateTime                   CreatedAt;
	FDateTime                   StartedAt;
	FDateTime                   EndedAt;
	FDateTime                   PausedAt;

	int32                       ConsecutiveErrorCount = 0;
	int32                       TotalTurnsTaken = 0;

	TMap<FString, int32>        ConsecutiveErrorsByRole;

	FString                     PauseReason;

	FString                     CompletionSummary;

	TArray<FString>             PausedActiveRoleIds;

	// Oscillation detector: the last UECPCrew::RecentInstructionWindow normalised instruction texts
	// dispatched on this run (newest last). Cleared when an oscillation escalation fires.
	TArray<FString>             RecentInstructions;

	// Tokens accumulated across role chats (provider-reported when available, else chars/4 estimate).
	int64                       TotalTokensUsed = 0;

	// Set when the run ends: "grounded" | "partially_grounded" | "no_passed_checkpoints".
	// partially_grounded = at least one Passed checkpoint lacks read-back verification.
	FString                     ReportGrounding;

	FCrewRun()
		: RunId(FGuid::NewGuid())
		, CreatedAt(FDateTime::UtcNow())
	{
	}
};

namespace UECPCrew
{
	// Oscillation detection: keep this many recent instructions per run; pause + escalate when the
	// same normalised instruction appears this many times inside the window.
	constexpr int32 RecentInstructionWindow    = 6;
	constexpr int32 OscillationRepeatThreshold = 3;

	// Report status vocabulary shared by the router, tools and report builders.
	inline const TCHAR* const ReportStatusSuccess    = TEXT("success");
	inline const TCHAR* const ReportStatusFail       = TEXT("fail");
	inline const TCHAR* const ReportStatusUnreported = TEXT("unreported");

	// Trim, lowercase, collapse internal whitespace — the key used by the oscillation detector.
	UECPCORE_API FString NormaliseInstructionText(const FString& Instruction);

	// "status=<x>\n..." → "<x>" (lowercased, trimmed); empty when the content has no status line.
	UECPCORE_API FString ExtractReportStatus(const FString& HandoffContent);

	UECPCORE_API FString RoleKindToString(ECrewRoleKind Kind);
	UECPCORE_API ECrewRoleKind RoleKindFromString(const FString& S);

	UECPCORE_API FString CheckpointStateToString(ECheckpointState S);
	UECPCORE_API ECheckpointState CheckpointStateFromString(const FString& S);

	UECPCORE_API FString HandoffTypeToString(EHandoffType T);
	UECPCORE_API EHandoffType HandoffTypeFromString(const FString& S);

	UECPCORE_API FString RunStateToString(ECrewRunState S);
	UECPCORE_API ECrewRunState RunStateFromString(const FString& S);
}
