// Copyright 2026, BlueprintsLab, All rights reserved

#include "McpToolsCatalog.h"

#include "UECPCoreModule.h"
#include "Services/IUECPExtensionService.h"
#include "Services/IUECPToolDispatcher.h"
#include "Serialization/JsonSerializer.h"

namespace McpToolsCatalog
{

namespace
{

	const FUECPToolCatalogEntry CoreCatalog[] = {
		{ TEXT("asset_management"),TEXT("Find, list, move, duplicate, rename, delete, save, open, validate, fix-up, batch-rename, import, and inspect Content Browser assets."), true },
		{ TEXT("config"),          TEXT("Read, analyze, and write Unreal project settings (typed get_/set_project_setting) and inspect raw .ini config layers."), true },
		{ TEXT("crew"),            TEXT("Multi-agent crew runtime: dispatch_to a role, report_back to the orchestrator, checkpoint_pass/fail, complete the run, escalate to the user, ask_orchestrator. Use ONLY when you are a member of an active crew run — calls from outside a crew chat are denied."), true },
		{ TEXT("curve"),           TEXT("Create and edit Curve and Curve Table assets."), true },
		{ TEXT("editor_utility"),  TEXT("Editor Utility Blueprints + Widgets, plus editor-side automation: console, output log, viewport screenshot, filesystem walk, plugin discovery, project analysis."), true },
		{ TEXT("git_tools"),       TEXT("Git version control and OS terminal access for the UE project."), true },
		{ TEXT("groom"),           TEXT("Hair Groom assets: LOD settings, physics simulation, rendering, material, binding, animation."), true },
		{ TEXT("memory"),          TEXT("Read and write AI memories directly (bypasses approval queue)."), true },
		{ TEXT("mesh"),            TEXT("Static mesh properties, texture settings, physical materials, sockets, skeleton bones."), true },
		{ TEXT("meshy"),           TEXT("Meshy.ai 3D ecosystem: text→3D, image→3D, remesh, retexture, rig (humanoid), animate from action catalog, text→image, credit balance. Long-running actions return {status:'queued', job_id} immediately; the result lands later as an [TOOL_RESULT:meshy.*:...] message with the imported asset path."), true },
		{ TEXT("object_properties"),TEXT("Generic UPROPERTY reflection on any UObject (asset or live actor): list, get, set with PostEditChangeProperty. Fallback for properties without a dedicated tool."), true },
		{ TEXT("physics"),         TEXT("Create and configure Physics Assets, Physical Materials, Collision, and Chaos Destruction."), true },
		{ TEXT("play_test"),       TEXT("PIE play automation: control Play In Editor sessions, inspect runtime state, drive input."), true },
		{ TEXT("project_plan"),    TEXT("Per-conversation plan BRIEF (goal, assets/paths, guidelines, decisions): create_plan | get_plan | clear_plan | list_plans | import_plan. The checklist is the separate 'task' tool."), true },
		{ TEXT("task"),            TEXT("Standalone task checklist (TodoWrite-style), usable any time/any mode: set_tasks(items=[{content,status}]) to (re)write the list, update_task(index,status) as you go, plus add_task/edit_task/remove_task/reorder_task/clear_tasks/get_tasks."), true },
		{ TEXT("project_viz"),     TEXT("Project visualization: dependency graphs, inheritance trees, complexity heatmaps."), true },
		{ TEXT("render"),          TEXT("Engine-level rendering settings: Lumen, Nanite, ray tracing, shadows, AA, quality."), true },
		{ TEXT("string_table"),    TEXT("String tables for localization."), true },

		{ TEXT("search_tools"),          TEXT("Find the right tool ACTION by intent — pass an English keyword phrase (e.g. \"set material two sided\", \"add IK to control rig\") and get back the matching actions with their umbrella/category. Use this FIRST when you don't know which umbrella owns a capability, instead of guessing. Query in English intent keywords even when chatting in another language. Pass umbrella='<category>' with no query to list every action in that umbrella. Then call category(action='...') or get_tool_docs(category='...', action='...') for full params. Aliases: find_tool, discover_tools."), false },
		{ TEXT("find_tool"),             TEXT("Alias of search_tools — find a tool action by an English intent query."), false },
		{ TEXT("discover_tools"),        TEXT("Alias of search_tools — find a tool action by an English intent query."), false },
		{ TEXT("get_tool_docs"),         TEXT("Returns parameter documentation for a tool category — or pass action='<action>' for just one action's params (token-cheap). Use search_tools first to find the action."), false },
		{ TEXT("get_handle_reference"),  TEXT("Returns node handle cheat-sheet sections from node_handle_reference.txt."), false },
		{ TEXT("get_current_folder"),    TEXT("Returns the currently focused Content Browser folder path."), false },
		{ TEXT("get_selected_assets"),   TEXT("Returns the assets currently selected in the Content Browser."), false },
		{ TEXT("ask_user"),              TEXT("Ask the user structured questions in an inline card and BLOCK for their answer. questions=[{header, question, multiSelect?, options:[{label, description?}]}] — an 'Other' free-text + a feedback box are added automatically. Use for genuine decisions you can't infer; not for things you can decide yourself."), false },
		{ TEXT("proceed_with_plan"),     TEXT("Leave Plan Mode and start building — switches the chat into the user's execution mode (asks once the first time, then remembers). Call ONLY after recording a plan and the user confirms 'proceed'. No arguments."), false },
	};

	TSharedRef<FJsonObject> BuildUmbrellaSchema()
	{
		TSharedRef<FJsonObject> Schema = MakeShared<FJsonObject>();
		Schema->SetStringField(TEXT("type"), TEXT("object"));

		TSharedRef<FJsonObject> Props = MakeShared<FJsonObject>();
		TSharedRef<FJsonObject> ActionProp = MakeShared<FJsonObject>();
		ActionProp->SetStringField(TEXT("type"), TEXT("string"));
		ActionProp->SetStringField(TEXT("description"),
			TEXT("The specific action to invoke. Call get_tool_docs(category=\"<this-tool-name>\") for the full action list and per-action parameters."));
		Props->SetObjectField(TEXT("action"), ActionProp);
		Schema->SetObjectField(TEXT("properties"), Props);

		TArray<TSharedPtr<FJsonValue>> Required;
		Required.Add(MakeShared<FJsonValueString>(TEXT("action")));
		Schema->SetArrayField(TEXT("required"), Required);

		Schema->SetBoolField(TEXT("additionalProperties"), true);
		return Schema;
	}

	TSharedRef<FJsonObject> BuildPermissiveSchema()
	{
		TSharedRef<FJsonObject> Schema = MakeShared<FJsonObject>();
		Schema->SetStringField(TEXT("type"), TEXT("object"));
		Schema->SetObjectField(TEXT("properties"), MakeShared<FJsonObject>());
		Schema->SetBoolField(TEXT("additionalProperties"), true);
		return Schema;
	}

	TSharedRef<FJsonObject> BuildHelperSchema(const FString& ToolName)
	{
		TSharedRef<FJsonObject> Schema = MakeShared<FJsonObject>();
		Schema->SetStringField(TEXT("type"), TEXT("object"));
		TSharedRef<FJsonObject> Props = MakeShared<FJsonObject>();
		TArray<TSharedPtr<FJsonValue>> Required;

		const auto AddProp = [&Props](const TCHAR* Name, const TCHAR* Type, const TCHAR* Desc)
		{
			TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
			P->SetStringField(TEXT("type"),        Type);
			P->SetStringField(TEXT("description"), Desc);
			Props->SetObjectField(Name, P);
		};

		if (ToolName == TEXT("search_tools") || ToolName == TEXT("find_tool") || ToolName == TEXT("discover_tools"))
		{
			AddProp(TEXT("query"),       TEXT("string"),  TEXT("English intent keywords for the action you want (e.g. \"set material two sided\", \"add IK to control rig\"). Query in English even when chatting in another language. Optional if 'umbrella' is given."));
			AddProp(TEXT("umbrella"),    TEXT("string"),  TEXT("Optional category filter. With no query, lists EVERY registered action in that umbrella (e.g. umbrella='material'). With a query, ranks matches within it."));
			AddProp(TEXT("max_results"), TEXT("integer"), TEXT("Max matches to return (default 15 for a query, 200 when listing an umbrella)."));
		}
		else if (ToolName == TEXT("get_tool_docs"))
		{
			AddProp(TEXT("category"),   TEXT("string"), TEXT("Tool category to fetch docs for (e.g. 'blueprint', 'niagara', 'asset_management')."));
			AddProp(TEXT("action"),     TEXT("string"), TEXT("Optional: fetch only this one action's params instead of the whole umbrella doc (token-cheap). Pair with search_tools."));
			AddProp(TEXT("tool_name"),  TEXT("string"), TEXT("Optional alias of category."));
			AddProp(TEXT("categories"), TEXT("array"),  TEXT("Batch form: array of category names to fetch in one call."));
			AddProp(TEXT("max_chars"),  TEXT("integer"),TEXT("Cap on the docs text per category (default 12000, 0 = unlimited). When cut, the result carries truncated=true, next_offset and total_chars."));
			AddProp(TEXT("offset"),     TEXT("integer"),TEXT("Resume a truncated category doc from this character offset (use the previous next_offset)."));
		}
		else if (ToolName == TEXT("get_handle_reference"))
		{
			AddProp(TEXT("section"), TEXT("string"), TEXT("Section name from node_handle_reference.txt to fetch."));
		}

		Schema->SetObjectField(TEXT("properties"), Props);
		if (Required.Num() > 0) Schema->SetArrayField(TEXT("required"), Required);
		Schema->SetBoolField(TEXT("additionalProperties"), true);
		return Schema;
	}

	void EmitTool(const FUECPToolCatalogEntry& Entry, TArray<TSharedPtr<FJsonValue>>& OutTools)
	{
		TSharedRef<FJsonObject> Tool = MakeShared<FJsonObject>();
		Tool->SetStringField(TEXT("name"),        Entry.Name);
		Tool->SetStringField(TEXT("description"), Entry.Description);

		if (Entry.InputSchema.IsValid())
		{
			Tool->SetObjectField(TEXT("inputSchema"), Entry.InputSchema);
		}
		else if (Entry.bIsUmbrella)
		{
			// Prefer the schema derived from registered tool metadata (action enum + typed params);
			// the bare {action} shape remains the fallback for umbrellas without metadata.
			TSharedPtr<FJsonObject> Derived = IUECPCoreModule::IsAvailable()
				? IUECPCoreModule::Get().GetToolDispatcher().GetUmbrellaSchema(FName(*Entry.Name))
				: nullptr;
			if (Derived.IsValid())
			{
				Tool->SetObjectField(TEXT("inputSchema"), Derived);
			}
			else
			{
				Tool->SetObjectField(TEXT("inputSchema"), BuildUmbrellaSchema());
			}
		}
		else
		{
			Tool->SetObjectField(TEXT("inputSchema"), BuildHelperSchema(Entry.Name));
		}
		OutTools.Add(MakeShared<FJsonValueObject>(Tool));
	}
}

TSharedRef<FJsonObject> BuildToolsListResult()
{
	IUECPExtensionService* ExtSvc = IUECPCoreModule::IsAvailable()
		? &IUECPCoreModule::Get().GetExtensionService() : nullptr;

	TArray<TSharedPtr<FJsonValue>> ToolArray;
	ToolArray.Reserve(UE_ARRAY_COUNT(CoreCatalog) + 16);

	TSet<FName> EmittedNames;
	EmittedNames.Reserve(UE_ARRAY_COUNT(CoreCatalog));
	for (const FUECPToolCatalogEntry& E : CoreCatalog)
	{
		EmittedNames.Add(FName(E.Name));
		if (!ExtSvc || ExtSvc->ShouldShowUmbrella(FName(E.Name)))
		{
			EmitTool(E, ToolArray);
		}
	}

	if (ExtSvc)
	{
		for (const FUECPToolCatalogEntry& E : ExtSvc->GetExtensionCatalogEntries(EmittedNames))
		{
			EmitTool(E, ToolArray);
		}
	}

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetArrayField(TEXT("tools"), ToolArray);
	return Result;
}

TSharedRef<FJsonObject> HandleToolsCall(
	const TSharedPtr<FJsonObject>& CallParams,
	const FMcpInvocationContext& Context)
{
	const auto MakeError = [](const FString& Msg, bool bIsError = true) -> TSharedRef<FJsonObject>
	{
		TSharedRef<FJsonObject> R = MakeShared<FJsonObject>();
		TArray<TSharedPtr<FJsonValue>> Content;
		TSharedRef<FJsonObject> Block = MakeShared<FJsonObject>();
		Block->SetStringField(TEXT("type"), TEXT("text"));
		Block->SetStringField(TEXT("text"), Msg);
		Content.Add(MakeShared<FJsonValueObject>(Block));
		R->SetArrayField(TEXT("content"), Content);
		R->SetBoolField(TEXT("isError"), bIsError);
		return R;
	};

	if (!CallParams.IsValid())
	{
		return MakeError(TEXT("tools/call: missing params object"));
	}

	FString ToolName;
	if (!CallParams->TryGetStringField(TEXT("name"), ToolName) || ToolName.IsEmpty())
	{
		return MakeError(TEXT("tools/call: missing or empty params.name"));
	}

	const TSharedPtr<FJsonObject>* ArgsObj = nullptr;
	TSharedPtr<FJsonObject> Arguments;
	if (CallParams->TryGetObjectField(TEXT("arguments"), ArgsObj) && ArgsObj && ArgsObj->IsValid())
	{
		Arguments = *ArgsObj;
	}
	else
	{
		Arguments = MakeShared<FJsonObject>();
	}

	FString DispatchName = ToolName;
	{
		bool bIsUmbrella = false;
		for (const FUECPToolCatalogEntry& E : CoreCatalog)
		{
			if (E.bIsUmbrella && DispatchName == E.Name) { bIsUmbrella = true; break; }
		}
		if (!bIsUmbrella && IUECPCoreModule::IsAvailable())
		{
			IUECPExtensionService& Ext = IUECPCoreModule::Get().GetExtensionService();
			if (Ext.FindExtensionByUmbrella(FName(*DispatchName)).IsSet())
			{
				bIsUmbrella = true;
			}
		}
		if (bIsUmbrella)
		{
			FString Action;
			if (Arguments.IsValid() && Arguments->TryGetStringField(TEXT("action"), Action) && !Action.IsEmpty())
			{
				if (DispatchName != TEXT("crew"))
				{
					DispatchName = MoveTemp(Action);
				}
			}
		}
	}

	if (!Arguments->HasField(TEXT("type")))
	{
		Arguments->SetStringField(TEXT("type"), DispatchName);
	}

	FMcpPipelineRequest PipelineReq;
	PipelineReq.CommandType      = DispatchName;
	PipelineReq.JsonObject       = Arguments;
	PipelineReq.WriteUnlocked    = nullptr;
	PipelineReq.IsTransportAlive = Context.IsTransportAlive;
	PipelineReq.CallerChatId     = Context.CallerChatId;
	PipelineReq.CallerChatToken  = Context.CallerChatToken;

	const FMcpPipelineResponse PipelineResp = FRequestPipeline::Process(PipelineReq);

	// Cap the text block so one oversized tool result cannot blow the MCP client's context.
	// A JSON marker is appended after the cut so the client can detect the truncation.
	FString BodyText = PipelineResp.Body;
	{
		constexpr int32 MaxTextBlockBytes = 64 * 1024;
		const int32 BodyBytes = FTCHARToUTF8(*BodyText).Length();
		if (BodyBytes > MaxTextBlockBytes)
		{
			// Bodies are mostly ASCII JSON: start from a char-count cut and shrink until the
			// UTF-8 encoding fits (multi-byte chars only ever make the encoded size larger).
			int32 KeepChars = FMath::Min(BodyText.Len(), MaxTextBlockBytes);
			while (KeepChars > 0 && FTCHARToUTF8(*BodyText.Left(KeepChars)).Length() > MaxTextBlockBytes)
			{
				KeepChars = FMath::Max(0, KeepChars - FMath::Max(1, KeepChars / 16));
			}
			const int32 BytesOmitted = BodyBytes - FTCHARToUTF8(*BodyText.Left(KeepChars)).Length();
			BodyText = BodyText.Left(KeepChars);
			BodyText += FString::Printf(
				TEXT("\n{\"truncated\":true,\"bytes_omitted\":%d,\"hint\":\"re-call with a narrower filter\"}"),
				BytesOmitted);
		}
	}

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> Content;
	TSharedRef<FJsonObject> Block = MakeShared<FJsonObject>();
	Block->SetStringField(TEXT("type"), TEXT("text"));
	Block->SetStringField(TEXT("text"), BodyText);
	Content.Add(MakeShared<FJsonValueObject>(Block));
	Result->SetArrayField(TEXT("content"), Content);

	bool bIsError = PipelineResp.bDenied || PipelineResp.Body.IsEmpty();
	if (!bIsError)
	{
		// Prefer the response's own top-level "success" flag; substring-matching the whole
		// body misfires when a nested payload contains "success":false as data.
		TSharedPtr<FJsonObject> RespObj;
		TSharedRef<TJsonReader<>> RR = TJsonReaderFactory<>::Create(PipelineResp.Body);
		bool bSuccess = false;
		if (FJsonSerializer::Deserialize(RR, RespObj) && RespObj.IsValid()
			&& RespObj->TryGetBoolField(TEXT("success"), bSuccess))
		{
			bIsError = !bSuccess;
		}
		else
		{
			bIsError = PipelineResp.Body.Contains(TEXT("\"success\":false")) ||
			           PipelineResp.Body.Contains(TEXT("\"success\": false"));
		}
	}
	Result->SetBoolField(TEXT("isError"), bIsError);

	return Result;
}

}
