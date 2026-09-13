// Copyright 2026, BlueprintsLab, All rights reserved.

#include "SUECPMainWidget.h"
#include "Widget/UUECPAppBridge.h"
#include "Managers/TaskManager.h"
#include "Types/CallerContext.h"
#include "AssetReferenceTypes.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace
{
	FToolExecutionResult TaskOk(const FString& Message, const FString& TasksJson)
	{
		TSharedPtr<FJsonObject> Resp = MakeShareable(new FJsonObject);
		Resp->SetBoolField(TEXT("success"), true);
		Resp->SetStringField(TEXT("message"), Message);
		if (!TasksJson.IsEmpty()) Resp->SetStringField(TEXT("tasks"), TasksJson);
		FString Out;
		TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
		FJsonSerializer::Serialize(Resp.ToSharedRef(), W);
		FToolExecutionResult R; R.bSuccess = true; R.ResultJson = Out;
		return R;
	}
}

FToolExecutionResult SUECPMainWidget::ExecuteTool_Task(const TSharedPtr<FJsonObject>& Args)
{
	FToolExecutionResult Result;

	FString Action;
	if (!Args->TryGetStringField(TEXT("action"), Action))
	{
		if (Args->HasField(TEXT("items")) || Args->HasField(TEXT("tasks")))       Action = TEXT("set_tasks");
		else if (Args->HasField(TEXT("index")) && Args->HasField(TEXT("status"))) Action = TEXT("update_task");
		else if (Args->HasField(TEXT("content")))                                 Action = TEXT("add_task");
		if (Action.IsEmpty())
		{
			Result.bSuccess = false;
			Result.ErrorMessage = TEXT("task: missing 'action'. Valid: set_tasks | add_task | update_task | edit_task | remove_task | reorder_task | clear_tasks | get_tasks.");
			return Result;
		}
	}
	if (Action.Equals(TEXT("todo_write"), ESearchCase::IgnoreCase)
	 || Action.Equals(TEXT("write_tasks"), ESearchCase::IgnoreCase)
	 || Action.Equals(TEXT("update_tasks"), ESearchCase::IgnoreCase))
	{
		Action = TEXT("set_tasks");
	}

	FString ConvID = UECPCallerContext::ReadCallerChatId(Args);
	if (ConvID.IsEmpty()) ConvID = ActiveArchitectChatID;
	FTaskManager& TM = FTaskManager::Get();

	if (Action == TEXT("set_tasks"))
	{
		TArray<TSharedPtr<FJsonValue>> ParsedHolder;
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (!Args->TryGetArrayField(TEXT("items"), Arr) && !Args->TryGetArrayField(TEXT("tasks"), Arr))
		{
			FString Str;
			if (Args->TryGetStringField(TEXT("items"), Str) || Args->TryGetStringField(TEXT("tasks"), Str))
			{
				TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Str);
				if (FJsonSerializer::Deserialize(Reader, ParsedHolder)) Arr = &ParsedHolder;
			}
		}
		if (!Arr)
		{
			Result.bSuccess = false;
			Result.ErrorMessage = TEXT("set_tasks: provide items=[{content, status?, verify?}] (status defaults to pending; verify = read-back that must be reported before done).");
			return Result;
		}

		TArray<FTask> Tasks;
		for (const TSharedPtr<FJsonValue>& V : *Arr)
		{
			FTask T;
			const TSharedPtr<FJsonObject>* Obj = nullptr;
			if (V->TryGetObject(Obj) && Obj && Obj->IsValid())
			{
				(*Obj)->TryGetStringField(TEXT("content"), T.Content);
				if (T.Content.IsEmpty()) (*Obj)->TryGetStringField(TEXT("task"), T.Content);
				FString St; (*Obj)->TryGetStringField(TEXT("status"), St);
				if (!St.IsEmpty()) T.Status = St;
				(*Obj)->TryGetStringField(TEXT("verify"), T.Verify);
			}
			else
			{
				V->TryGetString(T.Content);
			}
			if (!T.Content.IsEmpty()) Tasks.Add(T);
		}
		TM.SetTasks(ConvID, Tasks);
		NotifyTasksUpdated(ConvID);
		return TaskOk(FString::Printf(TEXT("Task list set (%d tasks)."), Tasks.Num()), TM.GetTasksJson(ConvID));
	}

	if (Action == TEXT("add_task"))
	{
		FString Content; Args->TryGetStringField(TEXT("content"), Content);
		if (Content.IsEmpty())
		{
			Result.bSuccess = false;
			Result.ErrorMessage = TEXT("add_task: 'content' is required.");
			return Result;
		}
		int32 AtIndex = MAX_int32;
		Args->TryGetNumberField(TEXT("at_index"), AtIndex);
		FString Verify; Args->TryGetStringField(TEXT("verify"), Verify);
		TM.AddTask(ConvID, AtIndex, Content, Verify);
		NotifyTasksUpdated(ConvID);
		return TaskOk(TEXT("Task added."), TM.GetTasksJson(ConvID));
	}

	if (Action == TEXT("update_task"))
	{
		const TArray<TSharedPtr<FJsonValue>>* ItemsArr = nullptr;
		if (Args->TryGetArrayField(TEXT("items"), ItemsArr) && ItemsArr && ItemsArr->Num() > 0)
		{
			int32 Updated = 0;
			for (const TSharedPtr<FJsonValue>& V : *ItemsArr)
			{
				TSharedPtr<FJsonObject> Item = V.IsValid() ? V->AsObject() : nullptr;
				if (!Item.IsValid()) continue;
				int32 Idx = -1; FString St, Ver;
				Item->TryGetNumberField(TEXT("index"), Idx);
				Item->TryGetStringField(TEXT("status"), St);
				Item->TryGetStringField(TEXT("verification"), Ver);
				if (Idx < 0 || St.IsEmpty()) continue;
				FString Msg;
				if (!TM.UpdateTaskStatusVerified(ConvID, Idx, St, Ver, Msg))
				{
					NotifyTasksUpdated(ConvID);
					Result.bSuccess = false;
					Result.ErrorMessage = Msg;
					return Result;
				}
				Updated++;
			}
			NotifyTasksUpdated(ConvID);
			return TaskOk(FString::Printf(TEXT("%d task(s) updated."), Updated), TM.GetTasksJson(ConvID));
		}

		int32 Index = -1; Args->TryGetNumberField(TEXT("index"), Index);
		FString Status; Args->TryGetStringField(TEXT("status"), Status);
		if (Index < 0 || Status.IsEmpty())
		{
			Result.bSuccess = false;
			Result.ErrorMessage = TEXT("update_task: 'index' (>=0) and 'status' (pending/in_progress/done/failed) required — or items=[{index,status,verification?}]. Tasks with a 'verify' line need verification='<what you checked>' to become done.");
			return Result;
		}
		FString Verification; Args->TryGetStringField(TEXT("verification"), Verification);
		FString Msg;
		if (!TM.UpdateTaskStatusVerified(ConvID, Index, Status, Verification, Msg))
		{
			Result.bSuccess = false;
			Result.ErrorMessage = Msg;
			return Result;
		}
		NotifyTasksUpdated(ConvID);
		return TaskOk(Msg, TM.GetTasksJson(ConvID));
	}

	if (Action == TEXT("edit_task"))
	{
		int32 Index = -1; Args->TryGetNumberField(TEXT("index"), Index);
		FString Content; Args->TryGetStringField(TEXT("content"), Content);
		if (Index < 0 || Content.IsEmpty())
		{
			Result.bSuccess = false;
			Result.ErrorMessage = TEXT("edit_task: 'index' (>=0) and 'content' required.");
			return Result;
		}
		TM.EditTaskContent(ConvID, Index, Content);
		NotifyTasksUpdated(ConvID);
		return TaskOk(TEXT("Task edited."), TM.GetTasksJson(ConvID));
	}

	if (Action == TEXT("remove_task"))
	{
		int32 Index = -1; Args->TryGetNumberField(TEXT("index"), Index);
		if (Index < 0)
		{
			Result.bSuccess = false;
			Result.ErrorMessage = TEXT("remove_task: 'index' (>=0) required.");
			return Result;
		}
		TM.RemoveTask(ConvID, Index);
		NotifyTasksUpdated(ConvID);
		return TaskOk(TEXT("Task removed."), TM.GetTasksJson(ConvID));
	}

	if (Action == TEXT("reorder_task"))
	{
		int32 From = -1, To = -1;
		Args->TryGetNumberField(TEXT("from_index"), From);
		Args->TryGetNumberField(TEXT("to_index"), To);
		if (From < 0 || To < 0)
		{
			Result.bSuccess = false;
			Result.ErrorMessage = TEXT("reorder_task: 'from_index' and 'to_index' (>=0) required.");
			return Result;
		}
		TM.ReorderTask(ConvID, From, To);
		NotifyTasksUpdated(ConvID);
		return TaskOk(TEXT("Task reordered."), TM.GetTasksJson(ConvID));
	}

	if (Action == TEXT("clear_tasks"))
	{
		TM.ClearTasks(ConvID);
		NotifyTasksUpdated(ConvID);
		return TaskOk(TEXT("Task list cleared."), FString());
	}

	if (Action == TEXT("get_tasks"))
	{
		Result.bSuccess = true;
		Result.ResultJson = TM.GetTasksJson(ConvID);
		return Result;
	}

	Result.bSuccess = false;
	Result.ErrorMessage = FString::Printf(TEXT("task: unknown action '%s'. Valid: set_tasks | add_task | update_task | edit_task | remove_task | reorder_task | clear_tasks | get_tasks."), *Action);
	return Result;
}

bool SUECPMainWidget::TryDispatchTaskTool(const FString& ToolName, const TSharedPtr<FJsonObject>& Arguments, FToolExecutionResult& OutResult)
{
	if (ToolName == TEXT("task") ||
		ToolName == TEXT("set_tasks")   || ToolName == TEXT("add_task")    ||
		ToolName == TEXT("update_task") || ToolName == TEXT("edit_task")   ||
		ToolName == TEXT("remove_task") || ToolName == TEXT("reorder_task")||
		ToolName == TEXT("clear_tasks") || ToolName == TEXT("get_tasks")   ||
		ToolName == TEXT("todo_write"))
	{
		if (ToolName != TEXT("task") && Arguments.IsValid() && !Arguments->HasField(TEXT("action")))
		{
			Arguments->SetStringField(TEXT("action"), ToolName);
		}
		OutResult = ExecuteTool_Task(Arguments);
		return true;
	}
	return false;
}

void SUECPMainWidget::NotifyTasksUpdated(const FString& ConvID)
{
	if (!AppBridgeObject || ConvID != ActiveArchitectChatID) return;

	FString TasksJson = FTaskManager::Get().GetTasksJson(ConvID);
	TasksJson.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
	TasksJson.ReplaceInline(TEXT("'"), TEXT("\\'"));
	TasksJson.ReplaceInline(TEXT("\n"), TEXT("\\n"));
	TasksJson.ReplaceInline(TEXT("\r"), TEXT(""));

	AppBridgeObject->ExecJs(FString::Printf(TEXT("if(typeof onTasksUpdate==='function')onTasksUpdate('%s');"), *TasksJson));
}
