// Copyright 2026, BlueprintsLab, All rights reserved

#include "Persistence/CrewManifest.h"
#include "Types/CrewTypes.h"
#include "UECPCrewModule.h"

#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"

namespace UECPCrew::Manifest
{

	FString CrewRoot()
	{
		return FPaths::ProjectSavedDir() / TEXT("BpGeneratorUltimate") / TEXT("crew");
	}

	FString TemplatesDir()
	{
		return CrewRoot() / TEXT("templates");
	}

	FString RunsDir()
	{
		return CrewRoot() / TEXT("runs");
	}

	FString TemplateFilePath(const FString& TemplateId)
	{
		return TemplatesDir() / (TemplateId + TEXT(".json"));
	}

	FString RunDir(const FGuid& RunId)
	{
		return RunsDir() / RunId.ToString(EGuidFormats::DigitsWithHyphens);
	}

	FString RunFilePath(const FGuid& RunId)
	{
		return RunDir(RunId) / TEXT("run.json");
	}

	static bool EnsureDirectoryExists(const FString& Dir)
	{
		IPlatformFile& PF = FPlatformFileManager::Get().GetPlatformFile();
		return PF.DirectoryExists(*Dir) || PF.CreateDirectoryTree(*Dir);
	}

	static bool AtomicWriteString(const FString& FinalPath, const FString& Content)
	{
		const FString TmpPath = FinalPath + TEXT(".tmp");
		if (!FFileHelper::SaveStringToFile(Content, *TmpPath))
		{
			UE_LOG(LogUECPCrew, Warning, TEXT("CrewManifest: failed to write tmp '%s'"), *TmpPath);
			return false;
		}
		IPlatformFile& PF = FPlatformFileManager::Get().GetPlatformFile();
		if (PF.FileExists(*FinalPath))
		{
			PF.DeleteFile(*FinalPath);
		}
		if (!PF.MoveFile(*FinalPath, *TmpPath))
		{
			UE_LOG(LogUECPCrew, Warning, TEXT("CrewManifest: rename '%s' → '%s' failed"), *TmpPath, *FinalPath);
			return false;
		}
		return true;
	}

	static FString SerializeObject(const TSharedRef<FJsonObject>& Obj)
	{
		FString Out;
		TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
		FJsonSerializer::Serialize(Obj, W);
		return Out;
	}

	static TSharedPtr<FJsonObject> ParseObjectFromFile(const FString& Path)
	{
		FString Raw;
		if (!FFileHelper::LoadFileToString(Raw, *Path)) return nullptr;
		TSharedPtr<FJsonObject> Obj;
		TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(Raw);
		if (!FJsonSerializer::Deserialize(R, Obj) || !Obj.IsValid()) return nullptr;
		return Obj;
	}

	TSharedPtr<FJsonObject> RoleToJson(const FCrewRole& R)
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("role_id"),               R.RoleId);
		O->SetStringField(TEXT("name"),                  R.Name);
		O->SetStringField(TEXT("kind"),                  RoleKindToString(R.Kind));
		O->SetStringField(TEXT("provider_name"),         R.ProviderName);
		O->SetStringField(TEXT("model_override"),        R.ModelOverride);
		O->SetStringField(TEXT("system_prompt_override"), R.SystemPromptOverride);
		O->SetBoolField  (TEXT("is_orchestrator"),       R.bIsOrchestrator);
		O->SetNumberField(TEXT("api_key_slot_index"),    R.ApiKeySlotIndex);
		TArray<TSharedPtr<FJsonValue>> Allow;
		for (const FString& T : R.ToolAllowlist) Allow.Add(MakeShared<FJsonValueString>(T));
		O->SetArrayField(TEXT("tool_allowlist"), Allow);
		return O;
	}

	bool RoleFromJson(const TSharedPtr<FJsonObject>& O, FCrewRole& Out)
	{
		if (!O.IsValid()) return false;
		Out = FCrewRole{};
		O->TryGetStringField(TEXT("role_id"),               Out.RoleId);
		O->TryGetStringField(TEXT("name"),                  Out.Name);
		FString KindStr;
		O->TryGetStringField(TEXT("kind"), KindStr);
		Out.Kind = RoleKindFromString(KindStr);
		O->TryGetStringField(TEXT("provider_name"),         Out.ProviderName);
		O->TryGetStringField(TEXT("model_override"),        Out.ModelOverride);
		O->TryGetStringField(TEXT("system_prompt_override"), Out.SystemPromptOverride);
		O->TryGetBoolField  (TEXT("is_orchestrator"),       Out.bIsOrchestrator);
		double SlotIdx = -1;
		if (O->TryGetNumberField(TEXT("api_key_slot_index"), SlotIdx)) Out.ApiKeySlotIndex = (int32)SlotIdx;
		const TArray<TSharedPtr<FJsonValue>>* Allow = nullptr;
		if (O->TryGetArrayField(TEXT("tool_allowlist"), Allow) && Allow)
		{
			for (const TSharedPtr<FJsonValue>& V : *Allow)
			{
				if (V.IsValid()) Out.ToolAllowlist.Add(V->AsString());
			}
		}
		return !Out.RoleId.IsEmpty();
	}

	TSharedPtr<FJsonObject> TemplateToJson(const FCrewTemplate& T)
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("template_id"),            T.TemplateId);
		O->SetStringField(TEXT("display_name"),           T.DisplayName);
		O->SetBoolField  (TEXT("built_in"),               T.bBuiltIn);
		O->SetStringField(TEXT("default_plan_scaffold"),  T.DefaultPlanScaffold);
		TArray<TSharedPtr<FJsonValue>> Roles;
		for (const FCrewRole& R : T.Roles)
		{
			Roles.Add(MakeShared<FJsonValueObject>(RoleToJson(R)));
		}
		O->SetArrayField(TEXT("roles"), Roles);

		TArray<TSharedPtr<FJsonValue>> DefPlan;
		for (const FCrewCheckpoint& C : T.DefaultPlan)
		{
			DefPlan.Add(MakeShared<FJsonValueObject>(CheckpointToJson(C)));
		}
		O->SetArrayField(TEXT("default_plan"), DefPlan);

		return O;
	}

	bool TemplateFromJson(const TSharedPtr<FJsonObject>& O, FCrewTemplate& Out)
	{
		if (!O.IsValid()) return false;
		Out = FCrewTemplate{};
		O->TryGetStringField(TEXT("template_id"),            Out.TemplateId);
		O->TryGetStringField(TEXT("display_name"),           Out.DisplayName);
		O->TryGetBoolField  (TEXT("built_in"),               Out.bBuiltIn);
		O->TryGetStringField(TEXT("default_plan_scaffold"),  Out.DefaultPlanScaffold);
		const TArray<TSharedPtr<FJsonValue>>* Roles = nullptr;
		if (O->TryGetArrayField(TEXT("roles"), Roles) && Roles)
		{
			for (const TSharedPtr<FJsonValue>& V : *Roles)
			{
				FCrewRole R;
				if (V.IsValid() && RoleFromJson(V->AsObject(), R))
				{
					Out.Roles.Add(MoveTemp(R));
				}
			}
		}
		const TArray<TSharedPtr<FJsonValue>>* DefPlan = nullptr;
		if (O->TryGetArrayField(TEXT("default_plan"), DefPlan) && DefPlan)
		{
			for (const TSharedPtr<FJsonValue>& V : *DefPlan)
			{
				FCrewCheckpoint C;
				if (V.IsValid() && CheckpointFromJson(V->AsObject(), C))
				{
					Out.DefaultPlan.Add(MoveTemp(C));
				}
			}
		}
		return !Out.TemplateId.IsEmpty();
	}

	TSharedPtr<FJsonObject> CheckpointToJson(const FCrewCheckpoint& C)
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("checkpoint_id"),    C.CheckpointId);
		O->SetStringField(TEXT("description"),      C.Description);
		O->SetStringField(TEXT("success_criteria"), C.SuccessCriteria);
		O->SetStringField(TEXT("state"),            CheckpointStateToString(C.State));
		O->SetStringField(TEXT("result_summary"),   C.ResultSummary);
		O->SetNumberField(TEXT("timeout_minutes"),  C.TimeoutMinutes);
		O->SetStringField(TEXT("instruction_time"), C.InstructionTime.ToIso8601());
		O->SetStringField(TEXT("retried_at"),       C.RetriedAt.ToIso8601());
		O->SetStringField(TEXT("paused_seconds_total"), LexToString(C.PausedSecondsTotal));
		TArray<TSharedPtr<FJsonValue>> Reqs;
		for (const FString& R : C.RequiredRoleIds) Reqs.Add(MakeShared<FJsonValueString>(R));
		O->SetArrayField(TEXT("required_role_ids"), Reqs);
		TArray<TSharedPtr<FJsonValue>> Deps;
		for (const FString& D : C.DependsOn) Deps.Add(MakeShared<FJsonValueString>(D));
		O->SetArrayField(TEXT("depends_on"), Deps);
		TArray<TSharedPtr<FJsonValue>> Tools;
		for (const FString& T : C.ToolAllowlistOverride) Tools.Add(MakeShared<FJsonValueString>(T));
		O->SetArrayField(TEXT("tool_allowlist_override"), Tools);

		// grounding / hardening
		O->SetNumberField(TEXT("retry_count"),        C.RetryCount);
		O->SetStringField(TEXT("last_report_status"), C.LastReportStatus);
		O->SetStringField(TEXT("verification_text"),  C.VerificationText);
		TArray<TSharedPtr<FJsonValue>> VerifyCalls;
		for (const FString& T : C.VerificationToolCalls) VerifyCalls.Add(MakeShared<FJsonValueString>(T));
		O->SetArrayField(TEXT("verification_tool_calls"), VerifyCalls);
		O->SetBoolField  (TEXT("grounded"),           C.bGrounded);
		return O;
	}

	bool CheckpointFromJson(const TSharedPtr<FJsonObject>& O, FCrewCheckpoint& Out)
	{
		if (!O.IsValid()) return false;
		Out = FCrewCheckpoint{};
		O->TryGetStringField(TEXT("checkpoint_id"),    Out.CheckpointId);
		O->TryGetStringField(TEXT("description"),      Out.Description);
		O->TryGetStringField(TEXT("success_criteria"), Out.SuccessCriteria);
		FString S;
		O->TryGetStringField(TEXT("state"), S);
		Out.State = CheckpointStateFromString(S);
		O->TryGetStringField(TEXT("result_summary"),   Out.ResultSummary);
		O->TryGetNumberField(TEXT("timeout_minutes"),  Out.TimeoutMinutes);
		FString InstStr;
		if (O->TryGetStringField(TEXT("instruction_time"), InstStr))
			FDateTime::ParseIso8601(*InstStr, Out.InstructionTime);
		FString RetryStr;
		if (O->TryGetStringField(TEXT("retried_at"), RetryStr))
			FDateTime::ParseIso8601(*RetryStr, Out.RetriedAt);
		FString PausedSecStr;
		if (O->TryGetStringField(TEXT("paused_seconds_total"), PausedSecStr))
			Out.PausedSecondsTotal = FCString::Atoi64(*PausedSecStr);
		const TArray<TSharedPtr<FJsonValue>>* Reqs = nullptr;
		if (O->TryGetArrayField(TEXT("required_role_ids"), Reqs) && Reqs)
		{
			for (const TSharedPtr<FJsonValue>& V : *Reqs)
			{
				if (V.IsValid()) Out.RequiredRoleIds.Add(V->AsString());
			}
		}
		const TArray<TSharedPtr<FJsonValue>>* Deps = nullptr;
		if (O->TryGetArrayField(TEXT("depends_on"), Deps) && Deps)
		{
			for (const TSharedPtr<FJsonValue>& V : *Deps)
			{
				if (V.IsValid()) Out.DependsOn.Add(V->AsString());
			}
		}
		const TArray<TSharedPtr<FJsonValue>>* Tools = nullptr;
		if (O->TryGetArrayField(TEXT("tool_allowlist_override"), Tools) && Tools)
		{
			for (const TSharedPtr<FJsonValue>& V : *Tools)
			{
				if (V.IsValid()) Out.ToolAllowlistOverride.Add(V->AsString());
			}
		}

		// grounding / hardening — all optional; absent fields keep the struct defaults
		O->TryGetNumberField(TEXT("retry_count"),        Out.RetryCount);
		O->TryGetStringField(TEXT("last_report_status"), Out.LastReportStatus);
		O->TryGetStringField(TEXT("verification_text"),  Out.VerificationText);
		const TArray<TSharedPtr<FJsonValue>>* VerifyCalls = nullptr;
		if (O->TryGetArrayField(TEXT("verification_tool_calls"), VerifyCalls) && VerifyCalls)
		{
			for (const TSharedPtr<FJsonValue>& V : *VerifyCalls)
			{
				if (V.IsValid() && V->Type == EJson::String) Out.VerificationToolCalls.Add(V->AsString());
			}
		}
		O->TryGetBoolField(TEXT("grounded"), Out.bGrounded);
		return !Out.CheckpointId.IsEmpty();
	}

	TSharedPtr<FJsonObject> HandoffToJson(const FCrewHandoff& H)
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("id"),             H.Id.ToString(EGuidFormats::DigitsWithHyphens));
		O->SetStringField(TEXT("from_role_id"),   H.FromRoleId);
		O->SetStringField(TEXT("to_role_id"),     H.ToRoleId);
		O->SetStringField(TEXT("type"),           HandoffTypeToString(H.Type));
		O->SetStringField(TEXT("checkpoint_id"),  H.CheckpointId);
		O->SetStringField(TEXT("content"),        H.Content);
		O->SetStringField(TEXT("timestamp_unix"), LexToString(H.TimestampUnix));
		O->SetBoolField  (TEXT("explicit_report"), H.bExplicitReport);
		O->SetStringField(TEXT("verification"),    H.Verification);
		TArray<TSharedPtr<FJsonValue>> VerifyCalls;
		for (const FString& T : H.VerificationToolCalls) VerifyCalls.Add(MakeShared<FJsonValueString>(T));
		O->SetArrayField(TEXT("verification_tool_calls"), VerifyCalls);
		return O;
	}

	bool HandoffFromJson(const TSharedPtr<FJsonObject>& O, FCrewHandoff& Out)
	{
		if (!O.IsValid()) return false;
		Out = FCrewHandoff{};
		FString IdStr;
		O->TryGetStringField(TEXT("id"), IdStr);
		FGuid::Parse(IdStr, Out.Id);
		O->TryGetStringField(TEXT("from_role_id"),  Out.FromRoleId);
		O->TryGetStringField(TEXT("to_role_id"),    Out.ToRoleId);
		FString TypeStr;
		O->TryGetStringField(TEXT("type"), TypeStr);
		Out.Type = HandoffTypeFromString(TypeStr);
		O->TryGetStringField(TEXT("checkpoint_id"), Out.CheckpointId);
		O->TryGetStringField(TEXT("content"),       Out.Content);
		FString TsStr;
		if (O->TryGetStringField(TEXT("timestamp_unix"), TsStr))
		{
			LexFromString(Out.TimestampUnix, *TsStr);
		}
		// Absent on pre-hardening handoffs → false, i.e. treated as unverified (intended).
		O->TryGetBoolField  (TEXT("explicit_report"), Out.bExplicitReport);
		O->TryGetStringField(TEXT("verification"),    Out.Verification);
		const TArray<TSharedPtr<FJsonValue>>* VerifyCalls = nullptr;
		if (O->TryGetArrayField(TEXT("verification_tool_calls"), VerifyCalls) && VerifyCalls)
		{
			for (const TSharedPtr<FJsonValue>& V : *VerifyCalls)
			{
				if (V.IsValid() && V->Type == EJson::String) Out.VerificationToolCalls.Add(V->AsString());
			}
		}
		return true;
	}

	TSharedPtr<FJsonObject> CapsToJson(const FCrewRunCaps& C)
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetNumberField(TEXT("max_total_turns"),                C.MaxTotalTurns);
		O->SetNumberField(TEXT("max_consecutive_errors"),         C.MaxConsecutiveErrors);
		O->SetNumberField(TEXT("max_consecutive_errors_per_role"), C.MaxConsecutiveErrorsPerRole);
		O->SetNumberField(TEXT("max_wall_minutes"),               C.MaxWallMinutes);
		O->SetBoolField  (TEXT("auto_approve_destructive"),       C.bAutoApproveDestructive);
		O->SetNumberField(TEXT("max_retries_per_checkpoint"),     C.MaxRetriesPerCheckpoint);
		O->SetStringField(TEXT("max_total_tokens"),               LexToString(C.MaxTotalTokens));
		return O;
	}

	bool CapsFromJson(const TSharedPtr<FJsonObject>& O, FCrewRunCaps& Out)
	{
		if (!O.IsValid()) return false;
		Out = FCrewRunCaps{};
		O->TryGetNumberField(TEXT("max_total_turns"),                Out.MaxTotalTurns);
		O->TryGetNumberField(TEXT("max_consecutive_errors"),         Out.MaxConsecutiveErrors);
		O->TryGetNumberField(TEXT("max_consecutive_errors_per_role"), Out.MaxConsecutiveErrorsPerRole);
		O->TryGetNumberField(TEXT("max_wall_minutes"),               Out.MaxWallMinutes);
		// Older run JSON carries the previous default (true); a persisted value is honoured as-is —
		// only the struct default for new runs changed to false.
		O->TryGetBoolField  (TEXT("auto_approve_destructive"),       Out.bAutoApproveDestructive);
		O->TryGetNumberField(TEXT("max_retries_per_checkpoint"),     Out.MaxRetriesPerCheckpoint);
		FString MaxTokStr;
		if (O->TryGetStringField(TEXT("max_total_tokens"), MaxTokStr))
			Out.MaxTotalTokens = FCString::Atoi64(*MaxTokStr);
		return true;
	}

	TSharedPtr<FJsonObject> RunToJson(const FCrewRun& R)
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("run_id"),                R.RunId.ToString(EGuidFormats::DigitsWithHyphens));
		O->SetStringField(TEXT("display_name"),          R.DisplayName);
		O->SetStringField(TEXT("crew_template_id"),      R.CrewTemplateId);
		O->SetStringField(TEXT("state"),                 RunStateToString(R.State));
		O->SetStringField(TEXT("orchestrator_chat_id"),  R.OrchestratorChatId);
		O->SetStringField(TEXT("pause_reason"),          R.PauseReason);
		O->SetStringField(TEXT("completion_summary"),    R.CompletionSummary);
		O->SetStringField(TEXT("created_at"),            R.CreatedAt.ToIso8601());
		O->SetStringField(TEXT("started_at"),            R.StartedAt.ToIso8601());
		O->SetStringField(TEXT("ended_at"),              R.EndedAt.ToIso8601());
		O->SetStringField(TEXT("paused_at"),             R.PausedAt.ToIso8601());
		O->SetNumberField(TEXT("consecutive_error_count"), R.ConsecutiveErrorCount);
		O->SetNumberField(TEXT("total_turns_taken"),     R.TotalTurnsTaken);
		TSharedPtr<FJsonObject> ErrByRole = MakeShared<FJsonObject>();
		for (const TPair<FString, int32>& P : R.ConsecutiveErrorsByRole)
			ErrByRole->SetNumberField(P.Key, P.Value);
		O->SetObjectField(TEXT("consecutive_errors_by_role"), ErrByRole);

		TArray<TSharedPtr<FJsonValue>> PausedRoles;
		for (const FString& Rid : R.PausedActiveRoleIds)
			PausedRoles.Add(MakeShared<FJsonValueString>(Rid));
		O->SetArrayField(TEXT("paused_active_role_ids"), PausedRoles);

		// grounding / hardening
		TArray<TSharedPtr<FJsonValue>> RecentInstr;
		for (const FString& S : R.RecentInstructions)
			RecentInstr.Add(MakeShared<FJsonValueString>(S));
		O->SetArrayField(TEXT("recent_instructions"), RecentInstr);
		O->SetStringField(TEXT("total_tokens_used"),  LexToString(R.TotalTokensUsed));
		O->SetStringField(TEXT("report_grounding"),   R.ReportGrounding);
		O->SetObjectField(TEXT("run_report"),         RunReportToJson(R));

		TArray<TSharedPtr<FJsonValue>> Roles;
		for (const FCrewRole& Ro : R.Roles)
		{
			Roles.Add(MakeShared<FJsonValueObject>(RoleToJson(Ro)));
		}
		O->SetArrayField(TEXT("roles"), Roles);

		TArray<TSharedPtr<FJsonValue>> Plan;
		for (const FCrewCheckpoint& C : R.Plan)
		{
			Plan.Add(MakeShared<FJsonValueObject>(CheckpointToJson(C)));
		}
		O->SetArrayField(TEXT("plan"), Plan);

		TArray<TSharedPtr<FJsonValue>> Handoffs;
		for (const FCrewHandoff& H : R.Handoffs)
		{
			Handoffs.Add(MakeShared<FJsonValueObject>(HandoffToJson(H)));
		}
		O->SetArrayField(TEXT("handoffs"), Handoffs);

		O->SetObjectField(TEXT("caps"), CapsToJson(R.Caps));

		TSharedPtr<FJsonObject> ChatIds = MakeShared<FJsonObject>();
		for (const TPair<FString, FString>& P : R.RoleChatIds)
		{
			ChatIds->SetStringField(P.Key, P.Value);
		}
		O->SetObjectField(TEXT("role_chat_ids"), ChatIds);

		return O;
	}

	bool RunFromJson(const TSharedPtr<FJsonObject>& O, FCrewRun& Out)
	{
		if (!O.IsValid()) return false;
		Out = FCrewRun{};

		FString IdStr;
		O->TryGetStringField(TEXT("run_id"), IdStr);
		FGuid::Parse(IdStr, Out.RunId);

		O->TryGetStringField(TEXT("display_name"),          Out.DisplayName);
		O->TryGetStringField(TEXT("crew_template_id"),      Out.CrewTemplateId);
		FString StateStr;
		O->TryGetStringField(TEXT("state"), StateStr);
		Out.State = RunStateFromString(StateStr);
		O->TryGetStringField(TEXT("orchestrator_chat_id"),  Out.OrchestratorChatId);
		O->TryGetStringField(TEXT("pause_reason"),          Out.PauseReason);
		O->TryGetStringField(TEXT("completion_summary"),    Out.CompletionSummary);

		FString CreatedStr, StartedStr, EndedStr, PausedStr;
		if (O->TryGetStringField(TEXT("created_at"), CreatedStr)) FDateTime::ParseIso8601(*CreatedStr, Out.CreatedAt);
		if (O->TryGetStringField(TEXT("started_at"), StartedStr)) FDateTime::ParseIso8601(*StartedStr, Out.StartedAt);
		if (O->TryGetStringField(TEXT("ended_at"),   EndedStr))   FDateTime::ParseIso8601(*EndedStr,   Out.EndedAt);
		if (O->TryGetStringField(TEXT("paused_at"),  PausedStr))  FDateTime::ParseIso8601(*PausedStr,  Out.PausedAt);

		O->TryGetNumberField(TEXT("consecutive_error_count"), Out.ConsecutiveErrorCount);
		O->TryGetNumberField(TEXT("total_turns_taken"),       Out.TotalTurnsTaken);
		const TSharedPtr<FJsonObject>* ErrByRoleObj = nullptr;
		if (O->TryGetObjectField(TEXT("consecutive_errors_by_role"), ErrByRoleObj) && ErrByRoleObj && ErrByRoleObj->IsValid())
		{
			for (const auto& Pair : (*ErrByRoleObj)->Values)
			{
				const FString RoleKey(*Pair.Key);
				int32 Count = 0;
				if (Pair.Value.IsValid() && Pair.Value->TryGetNumber(Count))
					Out.ConsecutiveErrorsByRole.Add(RoleKey, Count);
			}
		}

		const TArray<TSharedPtr<FJsonValue>>* PausedRolesArr = nullptr;
		if (O->TryGetArrayField(TEXT("paused_active_role_ids"), PausedRolesArr) && PausedRolesArr)
		{
			for (const TSharedPtr<FJsonValue>& V : *PausedRolesArr)
			{
				if (V.IsValid() && V->Type == EJson::String) Out.PausedActiveRoleIds.Add(V->AsString());
			}
		}
		else
		{
			FString LegacyId;
			if (O->TryGetStringField(TEXT("paused_active_role_id"), LegacyId) && !LegacyId.IsEmpty())
				Out.PausedActiveRoleIds.Add(LegacyId);
		}

		// grounding / hardening — optional ("run_report" is derived, never read back)
		const TArray<TSharedPtr<FJsonValue>>* RecentInstr = nullptr;
		if (O->TryGetArrayField(TEXT("recent_instructions"), RecentInstr) && RecentInstr)
		{
			for (const TSharedPtr<FJsonValue>& V : *RecentInstr)
			{
				if (V.IsValid() && V->Type == EJson::String) Out.RecentInstructions.Add(V->AsString());
			}
		}
		FString TokStr;
		if (O->TryGetStringField(TEXT("total_tokens_used"), TokStr))
			Out.TotalTokensUsed = FCString::Atoi64(*TokStr);
		O->TryGetStringField(TEXT("report_grounding"), Out.ReportGrounding);

		const TArray<TSharedPtr<FJsonValue>>* Roles = nullptr;
		if (O->TryGetArrayField(TEXT("roles"), Roles) && Roles)
		{
			for (const TSharedPtr<FJsonValue>& V : *Roles)
			{
				FCrewRole R;
				if (V.IsValid() && RoleFromJson(V->AsObject(), R)) Out.Roles.Add(MoveTemp(R));
			}
		}

		const TArray<TSharedPtr<FJsonValue>>* Plan = nullptr;
		if (O->TryGetArrayField(TEXT("plan"), Plan) && Plan)
		{
			for (const TSharedPtr<FJsonValue>& V : *Plan)
			{
				FCrewCheckpoint C;
				if (V.IsValid() && CheckpointFromJson(V->AsObject(), C)) Out.Plan.Add(MoveTemp(C));
			}
		}

		const TArray<TSharedPtr<FJsonValue>>* Handoffs = nullptr;
		if (O->TryGetArrayField(TEXT("handoffs"), Handoffs) && Handoffs)
		{
			for (const TSharedPtr<FJsonValue>& V : *Handoffs)
			{
				FCrewHandoff H;
				if (V.IsValid() && HandoffFromJson(V->AsObject(), H)) Out.Handoffs.Add(MoveTemp(H));
			}
		}

		const TSharedPtr<FJsonObject>* CapsObj = nullptr;
		if (O->TryGetObjectField(TEXT("caps"), CapsObj) && CapsObj && CapsObj->IsValid())
		{
			CapsFromJson(*CapsObj, Out.Caps);
		}

		const TSharedPtr<FJsonObject>* ChatIds = nullptr;
		if (O->TryGetObjectField(TEXT("role_chat_ids"), ChatIds) && ChatIds && ChatIds->IsValid())
		{
			for (const auto& P : (*ChatIds)->Values)
			{
				if (P.Value.IsValid()) Out.RoleChatIds.Add(FString(*P.Key), P.Value->AsString());
			}
		}

		return Out.RunId.IsValid();
	}

	FString ComputeReportGrounding(const FCrewRun& R)
	{
		int32 Passed = 0;
		int32 PassedUngrounded = 0;
		for (const FCrewCheckpoint& C : R.Plan)
		{
			if (C.State != ECheckpointState::Passed) continue;
			++Passed;
			if (!C.bGrounded) ++PassedUngrounded;
		}
		if (Passed == 0)           return TEXT("no_passed_checkpoints");
		if (PassedUngrounded > 0)  return TEXT("partially_grounded");
		return TEXT("grounded");
	}

	TSharedPtr<FJsonObject> RunReportToJson(const FCrewRun& R)
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("run_id"),       R.RunId.ToString(EGuidFormats::DigitsWithHyphens));
		O->SetStringField(TEXT("display_name"), R.DisplayName);
		O->SetStringField(TEXT("state"),        RunStateToString(R.State));
		O->SetStringField(TEXT("grounding"),
			R.ReportGrounding.IsEmpty() ? ComputeReportGrounding(R) : R.ReportGrounding);
		O->SetStringField(TEXT("total_tokens_used"), LexToString(R.TotalTokensUsed));
		O->SetNumberField(TEXT("total_turns"),  R.TotalTurnsTaken);

		TArray<TSharedPtr<FJsonValue>> Cps;
		for (const FCrewCheckpoint& C : R.Plan)
		{
			TSharedPtr<FJsonObject> CO = MakeShared<FJsonObject>();
			CO->SetStringField(TEXT("checkpoint_id"),    C.CheckpointId);
			CO->SetStringField(TEXT("description"),      C.Description);
			CO->SetStringField(TEXT("success_criteria"), C.SuccessCriteria);
			CO->SetStringField(TEXT("state"),            CheckpointStateToString(C.State));
			CO->SetStringField(TEXT("explicit_status"),
				C.LastReportStatus.IsEmpty() ? FString(ReportStatusUnreported) : C.LastReportStatus);
			CO->SetStringField(TEXT("verification"),     C.VerificationText);
			TArray<TSharedPtr<FJsonValue>> Calls;
			for (const FString& T : C.VerificationToolCalls) Calls.Add(MakeShared<FJsonValueString>(T));
			CO->SetArrayField (TEXT("verification_tool_calls"), Calls);
			CO->SetNumberField(TEXT("retry_count"),      C.RetryCount);
			CO->SetBoolField  (TEXT("grounded"),         C.bGrounded);
			CO->SetStringField(TEXT("result_summary"),   C.ResultSummary);
			Cps.Add(MakeShared<FJsonValueObject>(CO));
		}
		O->SetArrayField(TEXT("checkpoints"), Cps);
		return O;
	}

	FString FormatRunReportText(const FCrewRun& R)
	{
		const FString Grounding = R.ReportGrounding.IsEmpty() ? ComputeReportGrounding(R) : R.ReportGrounding;
		TStringBuilder<2048> Out;
		Out.Appendf(TEXT("[Run report — grounding: %s | tokens used: %lld | turns: %d]"),
			*Grounding, (long long)R.TotalTokensUsed, R.TotalTurnsTaken);
		for (const FCrewCheckpoint& C : R.Plan)
		{
			const FString Status = C.LastReportStatus.IsEmpty() ? FString(ReportStatusUnreported) : C.LastReportStatus;
			Out.Appendf(TEXT("\n  - %s: %s, explicit_status=%s, retries=%d, grounded=%s"),
				*C.CheckpointId, *CheckpointStateToString(C.State), *Status, C.RetryCount,
				C.bGrounded ? TEXT("yes") : TEXT("NO"));
			if (C.VerificationToolCalls.Num() > 0)
				Out.Appendf(TEXT(", verified_by=%s"), *FString::Join(C.VerificationToolCalls, TEXT(", ")));
			else
				Out.Append(TEXT(", no verification tool results"));
			if (!C.VerificationText.IsEmpty())
				Out.Appendf(TEXT(", verification=\"%s\""), *C.VerificationText);
		}
		return Out.ToString();
	}

	bool SaveTemplate(const FCrewTemplate& Template)
	{
		if (Template.bBuiltIn)
		{
			UE_LOG(LogUECPCrew, Warning, TEXT("CrewManifest: refusing to write built-in template '%s'"),
				*Template.TemplateId);
			return false;
		}
		if (Template.TemplateId.IsEmpty())
		{
			UE_LOG(LogUECPCrew, Warning, TEXT("CrewManifest: refusing to write template with empty id"));
			return false;
		}
		if (!EnsureDirectoryExists(TemplatesDir())) return false;

		const FString Path = TemplateFilePath(Template.TemplateId);
		const FString Json = SerializeObject(TemplateToJson(Template).ToSharedRef());
		return AtomicWriteString(Path, Json);
	}

	bool DeleteTemplate(const FString& TemplateId)
	{
		if (TemplateId.IsEmpty()) return false;
		const FString Path = TemplateFilePath(TemplateId);
		IPlatformFile& PF = FPlatformFileManager::Get().GetPlatformFile();
		if (!PF.FileExists(*Path)) return false;
		return PF.DeleteFile(*Path);
	}

	TArray<FCrewTemplate> LoadAllUserTemplates()
	{
		TArray<FCrewTemplate> Out;
		const FString Dir = TemplatesDir();
		IPlatformFile& PF = FPlatformFileManager::Get().GetPlatformFile();
		if (!PF.DirectoryExists(*Dir)) return Out;

		TArray<FString> Files;
		IFileManager::Get().FindFiles(Files, *(Dir / TEXT("*.json")), true, false);
		for (const FString& F : Files)
		{
			const FString Full = Dir / F;
			TSharedPtr<FJsonObject> Obj = ParseObjectFromFile(Full);
			if (!Obj.IsValid()) continue;
			FCrewTemplate T;
			if (TemplateFromJson(Obj, T))
			{
				T.bBuiltIn = false;
				Out.Add(MoveTemp(T));
			}
		}
		return Out;
	}

	bool SaveRun(const FCrewRun& Run)
	{
		if (!Run.RunId.IsValid()) return false;
		const FString Dir = RunDir(Run.RunId);
		if (!EnsureDirectoryExists(Dir)) return false;

		const FString Path = RunFilePath(Run.RunId);
		const FString Json = SerializeObject(RunToJson(Run).ToSharedRef());
		return AtomicWriteString(Path, Json);
	}

	bool DeleteRun(const FGuid& RunId)
	{
		if (!RunId.IsValid()) return false;
		const FString Dir = RunDir(RunId);
		IPlatformFile& PF = FPlatformFileManager::Get().GetPlatformFile();
		if (!PF.DirectoryExists(*Dir)) return false;
		return PF.DeleteDirectoryRecursively(*Dir);
	}

	TArray<FCrewRun> LoadAllRuns()
	{
		TArray<FCrewRun> Out;
		const FString Dir = RunsDir();
		IPlatformFile& PF = FPlatformFileManager::Get().GetPlatformFile();
		if (!PF.DirectoryExists(*Dir)) return Out;

		TArray<FString> Subdirs;
		IFileManager::Get().FindFiles(Subdirs, *(Dir / TEXT("*")), false, true);
		for (const FString& Sub : Subdirs)
		{
			const FString RunJson = Dir / Sub / TEXT("run.json");
			if (!PF.FileExists(*RunJson)) continue;
			TSharedPtr<FJsonObject> Obj = ParseObjectFromFile(RunJson);
			if (!Obj.IsValid()) continue;
			FCrewRun R;
			if (RunFromJson(Obj, R)) Out.Add(MoveTemp(R));
		}

		Out.Sort([](const FCrewRun& A, const FCrewRun& B) { return A.CreatedAt > B.CreatedAt; });
		return Out;
	}
}
