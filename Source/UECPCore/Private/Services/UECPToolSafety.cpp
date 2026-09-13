// Copyright 2026, BlueprintsLab, All rights reserved

#include "Services/UECPToolSafety.h"
#include "Containers/Map.h"
#include "Misc/ScopeLock.h"

namespace UECPToolSafety
{
namespace
{
	FCriticalSection& Lock()
	{
		static FCriticalSection L;
		return L;
	}

	TMap<FName, EUECPToolSafety>& Map()
	{
		static TMap<FName, EUECPToolSafety> M;
		return M;
	}

	EUECPToolSafety HeuristicFor(const FString& Name)
	{
		static const TSet<FString> ReadExact = {
			TEXT("ping"), TEXT("answer"), TEXT("web_search"),
			TEXT("get_tool_docs"), TEXT("get_handle_reference"),
			TEXT("compile_project"),
			TEXT("get_output_log"), TEXT("get_map_check_errors"),
			TEXT("classify_intent"), TEXT("ask_user"), TEXT("proceed_with_plan"),
			TEXT("get_tasks"),
		};
		if (ReadExact.Contains(Name)) return EUECPToolSafety::Read;

		// Explicit Write: these look read-only by name but touch disk or mutate assets.
		//  - export_text_to_file / export_to_file / export_asset write files on disk.
		//  - compile_blueprint deletes orphaned/invalid nodes in its pre-compile sweep.
		static const TSet<FString> WriteExact = {
			TEXT("export_text_to_file"), TEXT("export_to_file"), TEXT("export_asset"),
			TEXT("compile_blueprint"),
		};
		if (WriteExact.Contains(Name)) return EUECPToolSafety::Write;

		// Explicit Destructive: arbitrary shell + history-rewriting git actions.
		static const TSet<FString> DestructiveExact = {
			TEXT("run_command"), TEXT("git_revert"), TEXT("git_checkout"),
		};
		if (DestructiveExact.Contains(Name)) return EUECPToolSafety::Destructive;

		static const TSet<FString> PlanExact = {
			TEXT("project_plan"), TEXT("create_plan"), TEXT("update_step"),
			TEXT("advance_step"), TEXT("clear_plan"), TEXT("import_plan"),
			TEXT("memory"), TEXT("add_memory"), TEXT("suggest_memory"), TEXT("delete_memory"),
			TEXT("working_notes"), TEXT("set_working_notes"),
			TEXT("append_working_notes"), TEXT("clear_working_notes"),
			TEXT("task"), TEXT("set_tasks"), TEXT("add_task"), TEXT("update_task"),
			TEXT("edit_task"), TEXT("remove_task"), TEXT("reorder_task"), TEXT("clear_tasks"),
		};
		if (PlanExact.Contains(Name)) return EUECPToolSafety::PlanWrite;

		if (Name == TEXT("move_asset") || Name == TEXT("move_assets"))
			return EUECPToolSafety::Destructive;

		if (Name.StartsWith(TEXT("delete_"))   ||
			Name.StartsWith(TEXT("clear_"))    ||
			Name.StartsWith(TEXT("remove_"))   ||
			Name.StartsWith(TEXT("destroy_"))  ||
			Name.StartsWith(TEXT("purge_"))    ||
			Name.StartsWith(TEXT("reparent_")))  // structural, hard to undo (e.g. reparent_blueprint)
		{
			return EUECPToolSafety::Destructive;
		}

		if (Name.StartsWith(TEXT("get_"))      ||
			Name.StartsWith(TEXT("find_"))     ||
			Name.StartsWith(TEXT("list_"))     ||
			Name.StartsWith(TEXT("search_"))   ||
			Name.StartsWith(TEXT("scan_"))     ||
			Name.StartsWith(TEXT("check_"))    ||
			Name.StartsWith(TEXT("classify_")) ||
			Name.StartsWith(TEXT("inspect_"))  ||
			Name.StartsWith(TEXT("read_"))     ||
			Name.StartsWith(TEXT("verify_"))   ||
			Name.StartsWith(TEXT("validate_")) ||
			Name.StartsWith(TEXT("discover_")))
		{
			return EUECPToolSafety::Read;
		}

		return EUECPToolSafety::Write;
	}
}

UECPCORE_API void RegisterToolSafety(FName ToolName, EUECPToolSafety Safety)
{
	if (ToolName.IsNone()) return;
	FScopeLock _(&Lock());
	Map().Add(ToolName, Safety);
}

UECPCORE_API EUECPToolSafety GetToolSafety(FName ToolName)
{
	if (ToolName.IsNone()) return EUECPToolSafety::Write;
	{
		FScopeLock _(&Lock());
		if (const EUECPToolSafety* Found = Map().Find(ToolName))
			return *Found;
	}
	return HeuristicFor(ToolName.ToString());
}

UECPCORE_API bool IsReadOnly(FName ToolName)
{
	return GetToolSafety(ToolName) == EUECPToolSafety::Read;
}

UECPCORE_API bool IsDestructive(FName ToolName)
{
	return GetToolSafety(ToolName) == EUECPToolSafety::Destructive;
}

UECPCORE_API bool IsPlanModeAllowed(FName ToolName)
{
	const EUECPToolSafety S = GetToolSafety(ToolName);
	return S == EUECPToolSafety::Read || S == EUECPToolSafety::PlanWrite;
}

}
