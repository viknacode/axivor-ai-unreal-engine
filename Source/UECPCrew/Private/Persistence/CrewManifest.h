// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"

struct FCrewRole;
struct FCrewTemplate;
struct FCrewCheckpoint;
struct FCrewHandoff;
struct FCrewRunCaps;
struct FCrewRun;

namespace UECPCrew::Manifest
{
	FString CrewRoot();
	FString TemplatesDir();
	FString RunsDir();
	FString TemplateFilePath(const FString& TemplateId);
	FString RunDir(const FGuid& RunId);
	FString RunFilePath(const FGuid& RunId);

	bool SaveTemplate(const FCrewTemplate& Template);
	bool DeleteTemplate(const FString& TemplateId);
	TArray<FCrewTemplate> LoadAllUserTemplates();

	bool SaveRun(const FCrewRun& Run);
	bool DeleteRun(const FGuid& RunId);
	TArray<FCrewRun> LoadAllRuns();

	TSharedPtr<FJsonObject> RoleToJson(const FCrewRole& R);
	bool                    RoleFromJson(const TSharedPtr<FJsonObject>& O, FCrewRole& Out);

	TSharedPtr<FJsonObject> TemplateToJson(const FCrewTemplate& T);
	bool                    TemplateFromJson(const TSharedPtr<FJsonObject>& O, FCrewTemplate& Out);

	TSharedPtr<FJsonObject> CheckpointToJson(const FCrewCheckpoint& C);
	bool                    CheckpointFromJson(const TSharedPtr<FJsonObject>& O, FCrewCheckpoint& Out);

	TSharedPtr<FJsonObject> HandoffToJson(const FCrewHandoff& H);
	bool                    HandoffFromJson(const TSharedPtr<FJsonObject>& O, FCrewHandoff& Out);

	TSharedPtr<FJsonObject> CapsToJson(const FCrewRunCaps& C);
	bool                    CapsFromJson(const TSharedPtr<FJsonObject>& O, FCrewRunCaps& Out);

	TSharedPtr<FJsonObject> RunToJson(const FCrewRun& R);
	bool                    RunFromJson(const TSharedPtr<FJsonObject>& O, FCrewRun& Out);

	// --- final run report (grounding) ---

	// "grounded" when every Passed checkpoint has read-back verification, "partially_grounded" when
	// at least one Passed checkpoint lacks it, "no_passed_checkpoints" when nothing passed.
	FString ComputeReportGrounding(const FCrewRun& R);

	// Structured report: per-checkpoint explicit status, verification text, verification tool calls,
	// retry count and grounded flag, plus run-level grounding / token / turn totals. Embedded in
	// RunToJson under "run_report" so any consumer of the run JSON gets it for free.
	TSharedPtr<FJsonObject> RunReportToJson(const FCrewRun& R);

	// Human-readable rendering of the same report (appended to CompletionSummary on completion).
	FString FormatRunReportText(const FCrewRun& R);
}
