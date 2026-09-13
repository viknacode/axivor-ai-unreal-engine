// Copyright 2026, BlueprintsLab, All rights reserved

#include "UECPCrewModule.h"
#include "Types/CrewTypes.h"
#include "BuiltIn/CrewBuiltInTemplates.h"

#include "UECPCoreModule.h"
#include "Services/IUECPCrewService.h"

#include "HAL/IConsoleManager.h"

namespace
{
	FDelegateHandle GSmokeRunChangedHandle;

	void OnSmokeRunChanged(const FCrewRun& Run)
	{
		UE_LOG(LogUECPCrew, Log, TEXT("[smoke] run '%s' state=%s turns=%d %s"),
			*Run.DisplayName,
			*UECPCrew::RunStateToString(Run.State),
			Run.TotalTurnsTaken,
			*Run.PauseReason);

		if (Run.State == ECrewRunState::Completed ||
			Run.State == ECrewRunState::Aborted)
		{
			if (IUECPCoreModule::IsAvailable() && GSmokeRunChangedHandle.IsValid())
			{
				IUECPCoreModule::Get().GetCrewService().OnRunChanged().Remove(GSmokeRunChangedHandle);
				GSmokeRunChangedHandle.Reset();
			}
			UE_LOG(LogUECPCrew, Log, TEXT("[smoke] terminal — handoffs=%d plan=%d"),
				Run.Handoffs.Num(), Run.Plan.Num());
		}
	}

	void RunSmokeCommand(const TArray<FString>& Args, UWorld* )
	{
		if (!IUECPCoreModule::IsAvailable())
		{
			UE_LOG(LogUECPCrew, Warning, TEXT("[smoke] UECPCore not loaded — aborting"));
			return;
		}

		IUECPCrewService& Crew = IUECPCoreModule::Get().GetCrewService();
		const FString DisplayName = TEXT("Smoke Test");
		const FGuid RunId = Crew.CreateRun(UECPCrew::BuildVerifyTemplateId, DisplayName);
		if (!RunId.IsValid())
		{
			UE_LOG(LogUECPCrew, Warning, TEXT("[smoke] CreateRun failed (template missing?)"));
			return;
		}

		TArray<FCrewCheckpoint> Plan;
		{
			FCrewCheckpoint A;
			A.CheckpointId    = TEXT("cp_intro");
			A.Description     = TEXT("Say hello and announce the crew is ready.");
			A.SuccessCriteria = TEXT("Worker replies with a short greeting; verifier confirms it is a greeting.");
			Plan.Add(A);

			FCrewCheckpoint B;
			B.CheckpointId    = TEXT("cp_count");
			B.Description     = TEXT("Count from 1 to 3 on three separate lines, no other text.");
			B.SuccessCriteria = TEXT("Verifier confirms three lines exist and each line is a single digit 1, 2, 3 in order.");
			Plan.Add(B);
		}
		Crew.SetRunPlan(RunId, Plan);

		if (GSmokeRunChangedHandle.IsValid())
		{
			Crew.OnRunChanged().Remove(GSmokeRunChangedHandle);
		}
		GSmokeRunChangedHandle = Crew.OnRunChanged().AddStatic(&OnSmokeRunChanged);

		FString StartReason;
		if (!Crew.StartRun(RunId, StartReason))
		{
			UE_LOG(LogUECPCrew, Warning, TEXT("[smoke] StartRun failed: %s"), *StartReason);
			Crew.OnRunChanged().Remove(GSmokeRunChangedHandle);
			GSmokeRunChangedHandle.Reset();
			return;
		}

		UE_LOG(LogUECPCrew, Log,
			TEXT("[smoke] launched run %s — three role chats now visible in Architect view; watch LogUECPCrew for state transitions"),
			*RunId.ToString());
	}
}

static FAutoConsoleCommandWithWorldAndArgs GUECPCrewSmoke(
	TEXT("uecp.crew.smoke"),
	TEXT("Spawn a tiny Build & Verify crew run with a 2-checkpoint plan to exercise the runtime end-to-end."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunSmokeCommand));

namespace
{
	void AbortAllRunsCommand(const TArray<FString>& , UWorld* )
	{
		if (!IUECPCoreModule::IsAvailable()) return;
		IUECPCrewService& Crew = IUECPCoreModule::Get().GetCrewService();
		int32 Aborted = 0;
		for (const FCrewRun& Run : Crew.GetRuns())
		{
			if (Run.State == ECrewRunState::Running || Run.State == ECrewRunState::Paused)
			{
				Crew.AbortRun(Run.RunId, TEXT("user_console_abort"));
				++Aborted;
			}
		}
		UE_LOG(LogUECPCrew, Log, TEXT("[abort_all] aborted %d active run(s)"), Aborted);
	}
}

static FAutoConsoleCommandWithWorldAndArgs GUECPCrewAbortAll(
	TEXT("uecp.crew.abort_all"),
	TEXT("Abort every Running / Paused crew run. Use when a previous run is stuck and blocking a new one."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&AbortAllRunsCommand));
