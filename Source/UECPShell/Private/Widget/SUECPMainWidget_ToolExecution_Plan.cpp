// Copyright 2026, BlueprintsLab, All rights reserved.

#include "SUECPMainWidget.h"
#include "Widget/UUECPAppBridge.h"
#include "Managers/PlanManager.h"
#include "UECPCoreModule.h"
#include "Services/IUECPAiMemoryService.h"
#include "Types/CallerContext.h"
#include "AssetReferenceTypes.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

FToolExecutionResult SUECPMainWidget::ExecuteTool_ProjectPlan(const TSharedPtr<FJsonObject>& Args)
{
	FToolExecutionResult Result;

	FString Action;
	if (!Args->TryGetStringField(TEXT("action"), Action))
	{
		const bool bHasTitle    = Args->HasField(TEXT("title"));
		const bool bHasContext  = Args->HasField(TEXT("context"));
		const bool bHasSourceId = Args->HasField(TEXT("source_conv_id"));
		if (bHasSourceId)                  Action = TEXT("import_plan");
		else if (bHasTitle || bHasContext) Action = TEXT("create_plan");

		if (Action.IsEmpty())
		{
			Result.bSuccess = false;
			Result.ErrorMessage = TEXT("project_plan: missing 'action' field. Valid actions: create_plan | get_plan | clear_plan | list_plans | import_plan. (The step-by-step checklist is a separate tool: 'task'.)");
			return Result;
		}
	}

	if (Action.Equals(TEXT("set_plan"), ESearchCase::IgnoreCase)
	 || Action.Equals(TEXT("set_brief"), ESearchCase::IgnoreCase)
	 || Action.Equals(TEXT("update_context"), ESearchCase::IgnoreCase))
	{
		Action = TEXT("create_plan");
	}

	FString ConvID = UECPCallerContext::ReadCallerChatId(Args);
	if (ConvID.IsEmpty()) ConvID = ActiveArchitectChatID;
	FPlanManager& PM = FPlanManager::Get();

	if (Action == TEXT("create_plan"))
	{
		FString Title;
		Args->TryGetStringField(TEXT("title"), Title);
		if (Title.IsEmpty()) Title = TEXT("Plan");

		FString Context;
		Args->TryGetStringField(TEXT("context"), Context);
		if (Context.IsEmpty())
		{
			Result.bSuccess = false;
			Result.ErrorMessage = TEXT("create_plan: provide a 'context' brief (goal, assets + their paths, conventions/guidelines, key decisions). The step-by-step checklist is a separate tool — task(action='set_tasks', ...).");
			return Result;
		}

		const bool bIsActiveChat = (ConvID == ActiveArchitectChatID);
		if (bIsActiveChat && PlanProposalHistIdxByChat.Contains(ConvID))
		{
			int32 OldIdx = PlanProposalHistIdxByChat[ConvID];
			if (OldIdx >= 0 && OldIdx < ArchitectConversationHistory.Num())
			{
				ArchitectConversationHistory.RemoveAt(OldIdx);
				if (LastRenderedArchitectHistoryCount > OldIdx)
					LastRenderedArchitectHistoryCount--;
				LastRenderedArchitectRanges.Reset();
				ArchitectRangeHtmlCache.Reset();
				SaveArchitectChatHistory(ConvID);

				if (AppBridgeObject)
					AppBridgeObject->ExecJs(FString::Printf(
						TEXT("if(typeof deleteArchitectMsgAt==='function')deleteArchitectMsgAt(%d);"), OldIdx));
			}
			PlanProposalHistIdxByChat.Remove(ConvID);
		}

		PM.CreatePlan(ConvID, Title, Context);
		NotifyPlanUpdated(ConvID);

		if (bIsActiveChat && ArchitectConversationHistory.Num() > 0)
			PlanProposalHistIdxByChat.Add(ConvID, ArchitectConversationHistory.Num() - 1);

		TSharedPtr<FJsonObject> Resp = MakeShareable(new FJsonObject);
		Resp->SetBoolField(TEXT("success"), true);
		Resp->SetStringField(TEXT("message"), FString::Printf(TEXT("Plan brief '%s' recorded. Lay out the work with task(action='set_tasks', ...)."), *Title));
		Resp->SetStringField(TEXT("plan"), PM.GetPlanJson(ConvID));
		FString RespStr;
		TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&RespStr);
		FJsonSerializer::Serialize(Resp.ToSharedRef(), W);
		Result.bSuccess = true;
		Result.ResultJson = RespStr;
		return Result;
	}

	if (Action == TEXT("get_plan"))
	{
		FString PlanJson = PM.GetPlanJson(ConvID);
		Result.bSuccess = true;
		Result.ResultJson = PlanJson;
		return Result;
	}

	if (Action == TEXT("clear_plan"))
	{
		PM.ClearPlan(ConvID);
		PlanProposalHistIdxByChat.Remove(ConvID);
		NotifyPlanUpdated(ConvID);

		TSharedPtr<FJsonObject> Resp = MakeShareable(new FJsonObject);
		Resp->SetBoolField(TEXT("success"), true);
		Resp->SetStringField(TEXT("message"), TEXT("Plan cleared."));
		FString RespStr;
		TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&RespStr);
		FJsonSerializer::Serialize(Resp.ToSharedRef(), W);
		Result.bSuccess = true;
		Result.ResultJson = RespStr;
		return Result;
	}

	if (Action == TEXT("list_plans"))
	{
		TArray<FPlanManager::FPlanSummary> Summaries = PM.GetAllPlanSummaries();

		TSharedPtr<FJsonObject> Resp = MakeShareable(new FJsonObject);
		TArray<TSharedPtr<FJsonValue>> PlansArr;

		for (const FPlanManager::FPlanSummary& S : Summaries)
		{
			TSharedPtr<FJsonObject> P = MakeShareable(new FJsonObject);
			P->SetStringField(TEXT("conv_id"), S.ConvID);
			P->SetStringField(TEXT("title"), S.Title);
			P->SetStringField(TEXT("created_at"), S.CreatedAt);
			P->SetStringField(TEXT("context_snippet"), S.ContextSnippet);
			PlansArr.Add(MakeShareable(new FJsonValueObject(P)));
		}

		Resp->SetArrayField(TEXT("plans"), PlansArr);
		Resp->SetNumberField(TEXT("count"), Summaries.Num());

		FString RespStr;
		TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&RespStr);
		FJsonSerializer::Serialize(Resp.ToSharedRef(), W);
		Result.bSuccess = true;
		Result.ResultJson = RespStr;
		return Result;
	}

	if (Action == TEXT("import_plan"))
	{
		FString SourceConvID;
		Args->TryGetStringField(TEXT("source_conv_id"), SourceConvID);

		if (SourceConvID.IsEmpty())
		{
			Result.bSuccess = false;
			Result.ErrorMessage = TEXT("import_plan: 'source_conv_id' is required — use list_plans to get available conv_ids");
			return Result;
		}

		PM.ImportPlan(SourceConvID, ConvID);
		NotifyPlanUpdated(ConvID);

		TSharedPtr<FJsonObject> Resp = MakeShareable(new FJsonObject);
		Resp->SetBoolField(TEXT("success"), true);
		Resp->SetStringField(TEXT("message"), FString::Printf(TEXT("Plan brief imported from conversation '%s'."), *SourceConvID));
		Resp->SetStringField(TEXT("plan"), PM.GetPlanJson(ConvID));
		FString RespStr;
		TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&RespStr);
		FJsonSerializer::Serialize(Resp.ToSharedRef(), W);
		Result.bSuccess = true;
		Result.ResultJson = RespStr;
		return Result;
	}

	Result.bSuccess = false;
	Result.ErrorMessage = FString::Printf(TEXT("project_plan: unknown action '%s'. Valid: create_plan | get_plan | clear_plan | list_plans | import_plan. (Checklist is the separate 'task' tool.)"), *Action);
	return Result;
}

FToolExecutionResult SUECPMainWidget::ExecuteTool_Memory(const TSharedPtr<FJsonObject>& Args)
{
	FToolExecutionResult Result;

	FString Action;
	if (!Args->TryGetStringField(TEXT("action"), Action))
	{
		Result.bSuccess = false;
		Result.ErrorMessage = TEXT("memory: missing 'action' field");
		return Result;
	}

	if (Action == TEXT("set") || Action == TEXT("save") || Action == TEXT("add")) Action = TEXT("add_memory");
	else if (Action == TEXT("get") || Action == TEXT("list") || Action == TEXT("read")) Action = TEXT("get_memories");
	else if (Action == TEXT("delete") || Action == TEXT("remove")) Action = TEXT("delete_memory");
	else if (Action == TEXT("suggest") || Action == TEXT("propose")) Action = TEXT("suggest_memory");
	else if (Action == TEXT("clear") || Action == TEXT("append") || Action == TEXT("reset"))
	{
		Result.bSuccess = false;
		Result.ErrorMessage = FString::Printf(
			TEXT("memory has no '%s' action — that's a working_notes verb. Use working_notes(action='%s', content=...) for per-chat scratchpad notes. memory is for cross-session AI memories — valid actions: add_memory | suggest_memory | get_memories | delete_memory."),
			*Action, *Action);
		return Result;
	}

	IUECPAiMemoryService& MM = IUECPCoreModule::Get().GetAiMemoryService();

	if (Action == TEXT("add_memory"))
	{
		FString Content, CategoryStr;
		Args->TryGetStringField(TEXT("content"), Content);
		Args->TryGetStringField(TEXT("category"), CategoryStr);

		if (Content.IsEmpty())
		{
			Result.bSuccess = false;
			Result.ErrorMessage = TEXT("add_memory: 'content' is required");
			return Result;
		}

		EAiMemoryCategory Category = EAiMemoryCategory::ProjectInfo;
		if      (CategoryStr == TEXT("recent_work"))    Category = EAiMemoryCategory::RecentWork;
		else if (CategoryStr == TEXT("preferences"))    Category = EAiMemoryCategory::Preferences;
		else if (CategoryStr == TEXT("patterns"))       Category = EAiMemoryCategory::Patterns;
		else if (CategoryStr == TEXT("asset_relations"))Category = EAiMemoryCategory::AssetRelations;
		else if (CategoryStr == TEXT("decisions"))      Category = EAiMemoryCategory::Decisions;

		TSharedPtr<FAiMemoryEntry> Entry = MakeShareable(new FAiMemoryEntry);
		Entry->Content  = Content;
		Entry->Category = Category;
		Entry->Source   = TEXT("ai_written");

		bool bDuplicate = false;
		for (const TSharedPtr<FAiMemoryEntry>& E : MM.GetAiMemories())
			if (E.IsValid() && E->Content == Content) { bDuplicate = true; break; }
		if (!bDuplicate)
			for (const TSharedPtr<FAiMemoryEntry>& E : MM.GetPendingMemories())
				if (E.IsValid() && E->Content == Content) { bDuplicate = true; break; }

		const bool bAutoApprove = FSettingsManager::Get().LoadAutoApproveMemories();
		if (!bDuplicate)
		{
			if (bAutoApprove)
				MM.GetAiMemories().Add(Entry);
			else
				MM.GetPendingMemories().Add(Entry);
			MM.SaveManifest();
			MM.RefreshOverlay();
			if (AppBridgeObject)
				AppBridgeObject->PushToast(
					bAutoApprove
						? TEXT("New memory auto-approved")
						: TEXT("New memory pending review — open AI Memory to approve"),
					TEXT("info"));
		}

		TSharedPtr<FJsonObject> Resp = MakeShareable(new FJsonObject);
		Resp->SetBoolField(TEXT("success"), true);
		Resp->SetStringField(TEXT("memory_id"), Entry->MemoryId.ToString());
		Resp->SetStringField(TEXT("message"), bDuplicate
			? TEXT("Memory already exists or is pending — no duplicate created.")
			: (bAutoApprove
				? TEXT("Memory auto-approved and added.")
				: TEXT("Memory queued for user review (pending list).")));
		Resp->SetStringField(TEXT("status"), bDuplicate
			? TEXT("duplicate")
			: (bAutoApprove ? TEXT("approved") : TEXT("pending")));
		FString RespStr;
		TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&RespStr);
		FJsonSerializer::Serialize(Resp.ToSharedRef(), W);
		Result.bSuccess = true;
		Result.ResultJson = RespStr;
		return Result;
	}

	if (Action == TEXT("get_memories"))
	{
		FString CategoryFilter;
		Args->TryGetStringField(TEXT("category"), CategoryFilter);

		TSharedPtr<FJsonObject> Resp = MakeShareable(new FJsonObject);
		TArray<TSharedPtr<FJsonValue>> MemArr;

		for (const TSharedPtr<FAiMemoryEntry>& M : MM.GetAiMemories())
		{
			if (!M.IsValid() || !M->bEnabled) continue;

			FString CatName = MM.GetCategoryDisplayName(M->Category).ToLower().Replace(TEXT(" "), TEXT("_"));
			if (!CategoryFilter.IsEmpty() && CatName != CategoryFilter) continue;

			TSharedPtr<FJsonObject> MemObj = MakeShareable(new FJsonObject);
			MemObj->SetStringField(TEXT("memory_id"), M->MemoryId.ToString());
			MemObj->SetStringField(TEXT("category"), CatName);
			MemObj->SetStringField(TEXT("content"), M->Content);
			MemObj->SetStringField(TEXT("source"), M->Source);
			MemArr.Add(MakeShareable(new FJsonValueObject(MemObj)));
		}

		Resp->SetArrayField(TEXT("memories"), MemArr);
		Resp->SetNumberField(TEXT("count"), MemArr.Num());

		FString RespStr;
		TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&RespStr);
		FJsonSerializer::Serialize(Resp.ToSharedRef(), W);
		Result.bSuccess = true;
		Result.ResultJson = RespStr;
		return Result;
	}

	if (Action == TEXT("delete_memory"))
	{
		FString MemoryIdStr;
		Args->TryGetStringField(TEXT("memory_id"), MemoryIdStr);
		if (MemoryIdStr.IsEmpty())
		{
			Result.bSuccess = false;
			Result.ErrorMessage = TEXT("delete_memory: 'memory_id' is required");
			return Result;
		}

		FGuid TargetId;
		if (!FGuid::Parse(MemoryIdStr, TargetId))
		{
			Result.bSuccess = false;
			Result.ErrorMessage = FString::Printf(TEXT("delete_memory: invalid memory_id '%s'"), *MemoryIdStr);
			return Result;
		}

		TArray<TSharedPtr<FAiMemoryEntry>>& Memories = MM.GetAiMemories();
		int32 Removed = Memories.RemoveAll([&TargetId](const TSharedPtr<FAiMemoryEntry>& M) {
			return M.IsValid() && M->MemoryId == TargetId;
		});

		if (Removed == 0)
		{
			Result.bSuccess = false;
			Result.ErrorMessage = FString::Printf(TEXT("delete_memory: no memory found with id '%s'"), *MemoryIdStr);
			return Result;
		}

		MM.SaveManifest();

		TSharedPtr<FJsonObject> Resp = MakeShareable(new FJsonObject);
		Resp->SetBoolField(TEXT("success"), true);
		Resp->SetStringField(TEXT("message"), TEXT("Memory deleted."));
		FString RespStr;
		TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&RespStr);
		FJsonSerializer::Serialize(Resp.ToSharedRef(), W);
		Result.bSuccess = true;
		Result.ResultJson = RespStr;
		return Result;
	}

	if (Action == TEXT("suggest_memory"))
	{
		FString Content, CategoryStr;
		Args->TryGetStringField(TEXT("content"), Content);
		Args->TryGetStringField(TEXT("category"), CategoryStr);

		if (Content.IsEmpty())
		{
			Result.bSuccess = false;
			Result.ErrorMessage = TEXT("suggest_memory: 'content' is required");
			return Result;
		}

		EAiMemoryCategory Category = EAiMemoryCategory::ProjectInfo;
		if      (CategoryStr == TEXT("recent_work"))     Category = EAiMemoryCategory::RecentWork;
		else if (CategoryStr == TEXT("preferences"))     Category = EAiMemoryCategory::Preferences;
		else if (CategoryStr == TEXT("patterns"))        Category = EAiMemoryCategory::Patterns;
		else if (CategoryStr == TEXT("asset_relations"))Category = EAiMemoryCategory::AssetRelations;
		else if (CategoryStr == TEXT("decisions"))       Category = EAiMemoryCategory::Decisions;

		auto& PendingList = MM.GetPendingMemories();
		for (const TSharedPtr<FAiMemoryEntry>& E : MM.GetAiMemories())
			if (E.IsValid() && E->Content == Content)
			{
				TSharedPtr<FJsonObject> R = MakeShareable(new FJsonObject);
				R->SetBoolField(TEXT("success"), true);
				R->SetStringField(TEXT("message"), TEXT("Memory already exists."));
				FString Rs; TSharedRef<TJsonWriter<>> Wr = TJsonWriterFactory<>::Create(&Rs);
				FJsonSerializer::Serialize(R.ToSharedRef(), Wr);
				Result.bSuccess = true; Result.ResultJson = Rs; return Result;
			}
		for (const TSharedPtr<FAiMemoryEntry>& E : PendingList)
			if (E.IsValid() && E->Content == Content)
			{
				TSharedPtr<FJsonObject> R = MakeShareable(new FJsonObject);
				R->SetBoolField(TEXT("success"), true);
				R->SetStringField(TEXT("message"), TEXT("Memory already suggested."));
				FString Rs; TSharedRef<TJsonWriter<>> Wr = TJsonWriterFactory<>::Create(&Rs);
				FJsonSerializer::Serialize(R.ToSharedRef(), Wr);
				Result.bSuccess = true; Result.ResultJson = Rs; return Result;
			}

		TSharedPtr<FAiMemoryEntry> Entry = MakeShareable(new FAiMemoryEntry);
		Entry->Content  = Content;
		Entry->Category = Category;
		Entry->Source   = TEXT("ai_suggested");
		PendingList.Add(Entry);
		MM.SaveManifest();
		MM.RefreshOverlay();

		if (AppBridgeObject)
			AppBridgeObject->PushToast(TEXT("New memory suggestion — review in AI Memory"), TEXT("info"));

		TSharedPtr<FJsonObject> Resp = MakeShareable(new FJsonObject);
		Resp->SetBoolField(TEXT("success"), true);
		Resp->SetStringField(TEXT("memory_id"), Entry->MemoryId.ToString());
		Resp->SetStringField(TEXT("message"), TEXT("Memory suggestion added to pending review."));
		FString RespStr;
		TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&RespStr);
		FJsonSerializer::Serialize(Resp.ToSharedRef(), W);
		Result.bSuccess = true;
		Result.ResultJson = RespStr;
		return Result;
	}

	Result.bSuccess = false;
	Result.ErrorMessage = FString::Printf(TEXT("memory: unknown action '%s'. Valid: add_memory | suggest_memory | get_memories | delete_memory"), *Action);
	return Result;
}

FToolExecutionResult SUECPMainWidget::ExecuteTool_WorkingNotes(const TSharedPtr<FJsonObject>& Args)
{
	FToolExecutionResult Result;

	FString Action;
	if (!Args->TryGetStringField(TEXT("action"), Action))
	{
		Result.bSuccess = false;
		Result.ErrorMessage = TEXT("working_notes: missing 'action' field");
		return Result;
	}

	FString ConvID = UECPCallerContext::ReadCallerChatId(Args);
	if (ConvID.IsEmpty()) ConvID = ActiveArchitectChatID;
	FString& Notes = ArchitectWorkingNotesByChat.FindOrAdd(ConvID);

	if (Action == TEXT("set"))
	{
		FString Content;
		Args->TryGetStringField(TEXT("content"), Content);
		if (Content.TrimStartAndEnd().IsEmpty())
		{
			Result.bSuccess = false;
			Result.ErrorMessage = TEXT("working_notes set: 'content' is required");
			return Result;
		}

		Notes = Content.TrimStartAndEnd();
		SaveArchitectWorkingNotes(ConvID);

		TSharedPtr<FJsonObject> Resp = MakeShareable(new FJsonObject);
		Resp->SetBoolField(TEXT("success"), true);
		Resp->SetStringField(TEXT("message"), TEXT("Working notes replaced."));
		Resp->SetStringField(TEXT("content"), Notes);
		FString RespStr;
		TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&RespStr);
		FJsonSerializer::Serialize(Resp.ToSharedRef(), W);
		Result.bSuccess = true;
		Result.ResultJson = RespStr;
		Result.SummaryJson = RespStr;
		return Result;
	}

	if (Action == TEXT("append"))
	{
		FString Content;
		Args->TryGetStringField(TEXT("content"), Content);
		Content = Content.TrimStartAndEnd();
		if (Content.IsEmpty())
		{
			Result.bSuccess = false;
			Result.ErrorMessage = TEXT("working_notes append: 'content' is required");
			return Result;
		}

		if (!Notes.IsEmpty())
		{
			Notes += TEXT("\n");
		}
		Notes += Content;
		SaveArchitectWorkingNotes(ConvID);

		TSharedPtr<FJsonObject> Resp = MakeShareable(new FJsonObject);
		Resp->SetBoolField(TEXT("success"), true);
		Resp->SetStringField(TEXT("message"), TEXT("Working notes updated."));
		Resp->SetStringField(TEXT("content"), Notes);
		FString RespStr;
		TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&RespStr);
		FJsonSerializer::Serialize(Resp.ToSharedRef(), W);
		Result.bSuccess = true;
		Result.ResultJson = RespStr;
		Result.SummaryJson = RespStr;
		return Result;
	}

	if (Action == TEXT("get"))
	{
		TSharedPtr<FJsonObject> Resp = MakeShareable(new FJsonObject);
		Resp->SetBoolField(TEXT("success"), true);
		Resp->SetStringField(TEXT("content"), Notes);
		FString RespStr;
		TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&RespStr);
		FJsonSerializer::Serialize(Resp.ToSharedRef(), W);
		Result.bSuccess = true;
		Result.ResultJson = RespStr;
		Result.SummaryJson = RespStr;
		return Result;
	}

	if (Action == TEXT("clear"))
	{
		Notes.Empty();
		ArchitectWorkingNotesByChat.Remove(ConvID);
		SaveArchitectWorkingNotes(ConvID);

		TSharedPtr<FJsonObject> Resp = MakeShareable(new FJsonObject);
		Resp->SetBoolField(TEXT("success"), true);
		Resp->SetStringField(TEXT("message"), TEXT("Working notes cleared."));
		FString RespStr;
		TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&RespStr);
		FJsonSerializer::Serialize(Resp.ToSharedRef(), W);
		Result.bSuccess = true;
		Result.ResultJson = RespStr;
		Result.SummaryJson = RespStr;
		return Result;
	}

	Result.bSuccess = false;
	Result.ErrorMessage = FString::Printf(TEXT("working_notes: unknown action '%s'. Valid: set | append | get | clear"), *Action);
	return Result;
}

bool SUECPMainWidget::TryDispatchPlanTool(const FString& ToolName, const TSharedPtr<FJsonObject>& Arguments, FToolExecutionResult& OutResult)
{
	if (ToolName == TEXT("project_plan") ||
		ToolName == TEXT("create_plan")  ||
		ToolName == TEXT("get_plan")     || ToolName == TEXT("clear_plan")  ||
		ToolName == TEXT("list_plans")   || ToolName == TEXT("import_plan"))
	{
		if (ToolName != TEXT("project_plan") && Arguments.IsValid() && !Arguments->HasField(TEXT("action")))
		{
			Arguments->SetStringField(TEXT("action"), ToolName);
		}
		OutResult = ExecuteTool_ProjectPlan(Arguments);
		return true;
	}

	if (ToolName == TEXT("memory") ||
		ToolName == TEXT("add_memory") || ToolName == TEXT("suggest_memory") || ToolName == TEXT("get_memories") || ToolName == TEXT("delete_memory"))
	{
		if (ToolName != TEXT("memory") && Arguments.IsValid() && !Arguments->HasField(TEXT("action")))
		{
			Arguments->SetStringField(TEXT("action"), ToolName);
		}
		OutResult = ExecuteTool_Memory(Arguments);
		return true;
	}

	if (ToolName == TEXT("working_notes") ||
		ToolName == TEXT("get_working_notes") || ToolName == TEXT("set_working_notes") ||
		ToolName == TEXT("append_working_notes") || ToolName == TEXT("clear_working_notes"))
	{
		if (ToolName != TEXT("working_notes") && Arguments.IsValid() && !Arguments->HasField(TEXT("action")))
		{
			if (ToolName == TEXT("get_working_notes")) Arguments->SetStringField(TEXT("action"), TEXT("get"));
			else if (ToolName == TEXT("set_working_notes")) Arguments->SetStringField(TEXT("action"), TEXT("set"));
			else if (ToolName == TEXT("append_working_notes")) Arguments->SetStringField(TEXT("action"), TEXT("append"));
			else if (ToolName == TEXT("clear_working_notes")) Arguments->SetStringField(TEXT("action"), TEXT("clear"));
		}
		OutResult = ExecuteTool_WorkingNotes(Arguments);
		return true;
	}

	return false;
}

void SUECPMainWidget::NotifyPlanUpdated(const FString& ConvID)
{
	if (!AppBridgeObject || ConvID != ActiveArchitectChatID) return;

	FString PlanJson = FPlanManager::Get().GetPlanJson(ConvID);
	PlanJson.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
	PlanJson.ReplaceInline(TEXT("'"), TEXT("\\'"));
	PlanJson.ReplaceInline(TEXT("\n"), TEXT("\\n"));
	PlanJson.ReplaceInline(TEXT("\r"), TEXT(""));

	FString JS = FString::Printf(TEXT("if(typeof onPlanUpdate==='function')onPlanUpdate('%s');"), *PlanJson);
	AppBridgeObject->ExecJs(JS);
}
