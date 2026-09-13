// Copyright 2026, BlueprintsLab, All rights reserved

#include "BuiltIn/CrewBuiltInTemplates.h"
#include "Types/CrewTypes.h"

namespace UECPCrew
{
	const TCHAR* const BuildVerifyTemplateId = TEXT("builtin.build_verify");

	FCrewTemplate MakeBuildVerifyTemplate()
	{
		FCrewTemplate T;
		T.TemplateId  = BuildVerifyTemplateId;
		T.DisplayName = TEXT("Build & Verify (S + W + V)");
		T.bBuiltIn    = true;
		T.DefaultPlanScaffold = TEXT(
			"# Crew plan\n"
			"\n"
			"List each step the worker should complete. The verifier will check the success\n"
			"criteria after each one. Keep steps small enough that one worker turn can finish.\n"
			"\n"
			"Example:\n"
			"  - Step 1: Add ApplyDamage(float Amount) function to BP_Character.\n"
			"    Success: function exists, compiles, signature matches.\n"
			"  - Step 2: Wire ApplyDamage to OnHit event.\n"
			"    Success: event graph routes hit → ApplyDamage with hit damage.\n");

		{
			FCrewRole R;
			R.RoleId             = TEXT("role.supervisor");
			R.Name               = TEXT("Supervisor");
			R.Kind               = ECrewRoleKind::Supervisor;
			R.bIsOrchestrator    = true;
			T.Roles.Add(R);
		}

		{
			FCrewRole R;
			R.RoleId = TEXT("role.worker");
			R.Name   = TEXT("Worker");
			R.Kind   = ECrewRoleKind::Worker;
			T.Roles.Add(R);
		}

		{
			FCrewRole R;
			R.RoleId = TEXT("role.verifier");
			R.Name   = TEXT("Verifier");
			R.Kind   = ECrewRoleKind::Verifier;
			T.Roles.Add(R);
		}

		return T;
	}

	TArray<FCrewTemplate> MakeAllBuiltInTemplates()
	{
		TArray<FCrewTemplate> Out;
		Out.Add(MakeBuildVerifyTemplate());
		return Out;
	}
}
