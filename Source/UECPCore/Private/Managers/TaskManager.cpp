// Copyright 2026, BlueprintsLab, All rights reserved

#include "Managers/TaskManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonValue.h"

FTaskManager& FTaskManager::Get()
{
	static FTaskManager Instance;
	return Instance;
}

namespace
{
	FString NormalizeTaskStatus(const FString& In)
	{
		const FString S = In.TrimStartAndEnd().ToLower().Replace(TEXT("-"), TEXT("_")).Replace(TEXT(" "), TEXT("_"));
		if (S == TEXT("done") || S == TEXT("complete") || S == TEXT("completed") || S == TEXT("success")
			|| S == TEXT("succeeded") || S == TEXT("finished") || S == TEXT("pass") || S == TEXT("passed")
			|| S == TEXT("ok") || S == TEXT("resolved"))
			return TEXT("done");
		if (S == TEXT("in_progress") || S == TEXT("inprogress") || S == TEXT("started") || S == TEXT("active")
			|| S == TEXT("working") || S == TEXT("wip") || S == TEXT("doing") || S == TEXT("running"))
			return TEXT("in_progress");
		if (S == TEXT("failed") || S == TEXT("fail") || S == TEXT("error") || S == TEXT("blocked")
			|| S == TEXT("stuck"))
			return TEXT("failed");
		if (S == TEXT("pending") || S == TEXT("todo") || S == TEXT("not_started") || S == TEXT("notstarted")
			|| S == TEXT("queued") || S == TEXT("waiting"))
			return TEXT("pending");
		return S.IsEmpty() ? TEXT("pending") : S;
	}
}

FString FTaskManager::GetTaskFilePath(const FString& ConvID) const
{
	return FPaths::ProjectSavedDir() / TEXT("BpGeneratorUltimate") / TEXT("tasks") / ConvID + TEXT(".json");
}

void FTaskManager::EnsureTaskDirExists() const
{
	FString Dir = FPaths::ProjectSavedDir() / TEXT("BpGeneratorUltimate") / TEXT("tasks");
	IPlatformFile& PF = FPlatformFileManager::Get().GetPlatformFile();
	if (!PF.DirectoryExists(*Dir))
	{
		PF.CreateDirectoryTree(*Dir);
	}
}

void FTaskManager::ReindexTasks(TArray<TSharedPtr<FJsonValue>>& TasksArr)
{
	for (int32 i = 0; i < TasksArr.Num(); ++i)
	{
		TSharedPtr<FJsonObject> T = TasksArr[i].IsValid() ? TasksArr[i]->AsObject() : nullptr;
		if (T.IsValid()) T->SetNumberField(TEXT("index"), i);
	}
}

void FTaskManager::SetTasks(const FString& ConvID, const TArray<FTask>& Tasks)
{
	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetStringField(TEXT("conv_id"), ConvID);
	Obj->SetStringField(TEXT("created_at"), FDateTime::Now().ToString());

	TArray<TSharedPtr<FJsonValue>> Arr;
	for (int32 i = 0; i < Tasks.Num(); ++i)
	{
		TSharedPtr<FJsonObject> T = MakeShareable(new FJsonObject);
		T->SetNumberField(TEXT("index"), i);
		T->SetStringField(TEXT("content"), Tasks[i].Content);
		T->SetStringField(TEXT("status"), NormalizeTaskStatus(Tasks[i].Status));
		if (!Tasks[i].Verify.IsEmpty())       T->SetStringField(TEXT("verify"), Tasks[i].Verify);
		if (!Tasks[i].Verification.IsEmpty()) T->SetStringField(TEXT("verification"), Tasks[i].Verification);
		Arr.Add(MakeShareable(new FJsonValueObject(T)));
	}
	Obj->SetArrayField(TEXT("tasks"), Arr);

	TasksByConv.Add(ConvID, Obj);
	SaveTasks(ConvID);
}

void FTaskManager::UpdateTaskStatus(const FString& ConvID, int32 Index, const FString& Status)
{
	if (!TasksByConv.Contains(ConvID)) LoadTasks(ConvID);
	TSharedPtr<FJsonObject>* Ptr = TasksByConv.Find(ConvID);
	if (!Ptr || !Ptr->IsValid()) return;

	const TArray<TSharedPtr<FJsonValue>>* Arr;
	if (!(*Ptr)->TryGetArrayField(TEXT("tasks"), Arr)) return;

	const FString Canon = NormalizeTaskStatus(Status);

	// Only the addressed task changes. Earlier tasks are never auto-marked done:
	// the model must report each one explicitly so skipped/failed steps stay visible.
	for (const TSharedPtr<FJsonValue>& Val : *Arr)
	{
		TSharedPtr<FJsonObject> T = Val->AsObject();
		if (!T.IsValid()) continue;
		int32 Idx = 0;
		T->TryGetNumberField(TEXT("index"), Idx);
		if (Idx == Index)
		{
			T->SetStringField(TEXT("status"), Canon);
			break;
		}
	}
	SaveTasks(ConvID);
}

bool FTaskManager::UpdateTaskStatusVerified(const FString& ConvID, int32 Index, const FString& Status, const FString& Verification, FString& OutMessage)
{
	if (!TasksByConv.Contains(ConvID)) LoadTasks(ConvID);
	TSharedPtr<FJsonObject>* Ptr = TasksByConv.Find(ConvID);
	if (!Ptr || !Ptr->IsValid()) { OutMessage = TEXT("No task list for this conversation."); return false; }
	const TArray<TSharedPtr<FJsonValue>>* Arr;
	if (!(*Ptr)->TryGetArrayField(TEXT("tasks"), Arr)) { OutMessage = TEXT("No tasks."); return false; }
	const FString Canon = NormalizeTaskStatus(Status);
	for (const TSharedPtr<FJsonValue>& Val : *Arr)
	{
		TSharedPtr<FJsonObject> T = Val->AsObject();
		if (!T.IsValid()) continue;
		int32 Idx = 0; T->TryGetNumberField(TEXT("index"), Idx);
		if (Idx != Index) continue;
		FString Verify; T->TryGetStringField(TEXT("verify"), Verify);
		if (Canon == TEXT("done") && !Verify.IsEmpty() && Verification.TrimStartAndEnd().IsEmpty())
		{
			FString Content; T->TryGetStringField(TEXT("content"), Content);
			OutMessage = FString::Printf(TEXT("Task %d ('%s') declares a verification step: \"%s\". Run that read-back first, then call update_task again with verification='<what you observed>'. Not marked done."), Index + 1, *Content, *Verify);
			return false;
		}
		T->SetStringField(TEXT("status"), Canon);
		if (!Verification.IsEmpty()) T->SetStringField(TEXT("verification"), Verification);
		SaveTasks(ConvID);
		OutMessage = FString::Printf(TEXT("Task %d -> '%s'."), Index, *Canon);
		return true;
	}
	OutMessage = FString::Printf(TEXT("Task index %d not found."), Index);
	return false;
}

void FTaskManager::EditTaskContent(const FString& ConvID, int32 Index, const FString& NewContent)
{
	if (!TasksByConv.Contains(ConvID)) LoadTasks(ConvID);
	TSharedPtr<FJsonObject>* Ptr = TasksByConv.Find(ConvID);
	if (!Ptr || !Ptr->IsValid()) return;

	const TArray<TSharedPtr<FJsonValue>>* Arr;
	if (!(*Ptr)->TryGetArrayField(TEXT("tasks"), Arr)) return;
	for (const TSharedPtr<FJsonValue>& Val : *Arr)
	{
		TSharedPtr<FJsonObject> T = Val->AsObject();
		if (!T.IsValid()) continue;
		int32 Idx = 0;
		T->TryGetNumberField(TEXT("index"), Idx);
		if (Idx == Index)
		{
			T->SetStringField(TEXT("content"), NewContent);
			break;
		}
	}
	SaveTasks(ConvID);
}

void FTaskManager::AddTask(const FString& ConvID, int32 AtIndex, const FString& Content, const FString& Verify)
{
	if (!TasksByConv.Contains(ConvID)) LoadTasks(ConvID);
	TSharedPtr<FJsonObject>* Ptr = TasksByConv.Find(ConvID);
	if (!Ptr || !Ptr->IsValid())
	{
		FTask T; T.Content = Content; T.Status = TEXT("pending"); T.Verify = Verify;
		SetTasks(ConvID, { T });
		return;
	}

	TArray<TSharedPtr<FJsonValue>> Arr;
	const TArray<TSharedPtr<FJsonValue>>* Existing;
	if ((*Ptr)->TryGetArrayField(TEXT("tasks"), Existing)) Arr = *Existing;

	TSharedPtr<FJsonObject> New = MakeShareable(new FJsonObject);
	New->SetNumberField(TEXT("index"), 0);
	New->SetStringField(TEXT("content"), Content);
	New->SetStringField(TEXT("status"), TEXT("pending"));
	if (!Verify.IsEmpty()) New->SetStringField(TEXT("verify"), Verify);

	const int32 InsertAt = FMath::Clamp(AtIndex, 0, Arr.Num());
	Arr.Insert(MakeShareable(new FJsonValueObject(New)), InsertAt);
	ReindexTasks(Arr);
	(*Ptr)->SetArrayField(TEXT("tasks"), Arr);
	SaveTasks(ConvID);
}

void FTaskManager::RemoveTask(const FString& ConvID, int32 Index)
{
	if (!TasksByConv.Contains(ConvID)) LoadTasks(ConvID);
	TSharedPtr<FJsonObject>* Ptr = TasksByConv.Find(ConvID);
	if (!Ptr || !Ptr->IsValid()) return;

	TArray<TSharedPtr<FJsonValue>> Arr;
	const TArray<TSharedPtr<FJsonValue>>* Existing;
	if ((*Ptr)->TryGetArrayField(TEXT("tasks"), Existing)) Arr = *Existing;

	if (!Arr.IsValidIndex(Index)) return;
	Arr.RemoveAt(Index);
	ReindexTasks(Arr);
	(*Ptr)->SetArrayField(TEXT("tasks"), Arr);
	SaveTasks(ConvID);
}

void FTaskManager::ReorderTask(const FString& ConvID, int32 FromIndex, int32 ToIndex)
{
	if (!TasksByConv.Contains(ConvID)) LoadTasks(ConvID);
	TSharedPtr<FJsonObject>* Ptr = TasksByConv.Find(ConvID);
	if (!Ptr || !Ptr->IsValid()) return;

	TArray<TSharedPtr<FJsonValue>> Arr;
	const TArray<TSharedPtr<FJsonValue>>* Existing;
	if ((*Ptr)->TryGetArrayField(TEXT("tasks"), Existing)) Arr = *Existing;

	if (!Arr.IsValidIndex(FromIndex) || Arr.Num() == 0) return;
	const int32 Dest = FMath::Clamp(ToIndex, 0, Arr.Num() - 1);
	if (Dest == FromIndex) return;

	TSharedPtr<FJsonValue> Moved = Arr[FromIndex];
	Arr.RemoveAt(FromIndex);
	Arr.Insert(Moved, Dest);
	ReindexTasks(Arr);
	(*Ptr)->SetArrayField(TEXT("tasks"), Arr);
	SaveTasks(ConvID);
}

bool FTaskManager::HasTasks(const FString& ConvID) const
{
	if (!TasksByConv.Contains(ConvID))
		const_cast<FTaskManager*>(this)->LoadTasks(ConvID);
	const TSharedPtr<FJsonObject>* Ptr = TasksByConv.Find(ConvID);
	if (!Ptr || !Ptr->IsValid()) return false;
	const TArray<TSharedPtr<FJsonValue>>* Arr;
	return (*Ptr)->TryGetArrayField(TEXT("tasks"), Arr) && Arr->Num() > 0;
}

FString FTaskManager::GetTasksJson(const FString& ConvID) const
{
	if (!TasksByConv.Contains(ConvID))
		const_cast<FTaskManager*>(this)->LoadTasks(ConvID);
	const TSharedPtr<FJsonObject>* Ptr = TasksByConv.Find(ConvID);
	if (!Ptr || !Ptr->IsValid()) return TEXT("{}");

	FString Out;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize((*Ptr).ToSharedRef(), Writer);
	return Out;
}

FString FTaskManager::GetTasksForAI(const FString& ConvID) const
{
	if (!TasksByConv.Contains(ConvID))
		const_cast<FTaskManager*>(this)->LoadTasks(ConvID);
	const TSharedPtr<FJsonObject>* Ptr = TasksByConv.Find(ConvID);
	if (!Ptr || !Ptr->IsValid()) return FString();

	const TArray<TSharedPtr<FJsonValue>>* Arr;
	if (!(*Ptr)->TryGetArrayField(TEXT("tasks"), Arr) || Arr->Num() == 0) return FString();

	FString Out = TEXT("=== ACTIVE TASKS ===\n");
	Out += TEXT("REQUIRED workflow — keep this list current as you work:\n");
	Out += TEXT("  1. Before starting a task: task(action='update_task', index=<N>, status='in_progress')\n");
	Out += TEXT("  2. Do the work for that task (one logical unit).\n");
	Out += TEXT("  3. IMMEDIATELY after it succeeds: task(action='update_task', index=<N>, status='done', verification='<read-back you ran and what it showed>')\n");
	Out += TEXT("     Tasks that list a 'verify' line REQUIRE that verification note — the status change is refused without it.\n");
	Out += TEXT("  4. If it fails and you can't recover: status='failed' with a brief reason, then stop and ask.\n");
	Out += TEXT("  To (re)write the whole list at once: task(action='set_tasks', items=[{content, status}]).\n");
	Out += TEXT("Tasks left unchecked after the work has clearly happened make the run look broken.\n\n");
	Out += TEXT("Tasks:\n");

	for (const TSharedPtr<FJsonValue>& Val : *Arr)
	{
		TSharedPtr<FJsonObject> T = Val->AsObject();
		if (!T.IsValid()) continue;
		int32 Idx = 0;
		FString Content, Status;
		T->TryGetNumberField(TEXT("index"), Idx);
		T->TryGetStringField(TEXT("content"), Content);
		T->TryGetStringField(TEXT("status"), Status);

		FString Icon;
		if (Status == TEXT("done"))             Icon = TEXT("[done]");
		else if (Status == TEXT("in_progress")) Icon = TEXT("[in progress]");
		else if (Status == TEXT("failed"))      Icon = TEXT("[failed]");
		else                                    Icon = TEXT("[pending]");

		Out += FString::Printf(TEXT("%s %d. %s\n"), *Icon, Idx + 1, *Content);
		FString Verify, Verification;
		T->TryGetStringField(TEXT("verify"), Verify);
		T->TryGetStringField(TEXT("verification"), Verification);
		if (!Verify.IsEmpty())       Out += FString::Printf(TEXT("      verify: %s\n"), *Verify);
		if (!Verification.IsEmpty()) Out += FString::Printf(TEXT("      verified: %s\n"), *Verification);
	}

	Out += TEXT("=== END TASKS ===");
	return Out;
}

void FTaskManager::ClearTasks(const FString& ConvID)
{
	TasksByConv.Remove(ConvID);
	FString FilePath = GetTaskFilePath(ConvID);
	if (FPaths::FileExists(FilePath))
		IFileManager::Get().Delete(*FilePath);
}

void FTaskManager::SaveTasks(const FString& ConvID)
{
	const TSharedPtr<FJsonObject>* Ptr = TasksByConv.Find(ConvID);
	if (!Ptr || !Ptr->IsValid()) return;

	EnsureTaskDirExists();
	FString JsonString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonString);
	FJsonSerializer::Serialize((*Ptr).ToSharedRef(), Writer);
	FFileHelper::SaveStringToFile(JsonString, *GetTaskFilePath(ConvID));
}

TSharedPtr<FJsonObject> FTaskManager::MigrateLegacyPlanSteps(const FString& ConvID) const
{
	const FString PlanPath = FPaths::ProjectSavedDir() / TEXT("BpGeneratorUltimate") / TEXT("plans") / ConvID + TEXT(".json");
	if (!FPaths::FileExists(PlanPath)) return nullptr;

	FString JsonString;
	if (!FFileHelper::LoadFileToString(JsonString, *PlanPath)) return nullptr;

	TSharedPtr<FJsonObject> PlanObj;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
	if (!FJsonSerializer::Deserialize(Reader, PlanObj) || !PlanObj.IsValid()) return nullptr;

	const TArray<TSharedPtr<FJsonValue>>* StepsArr;
	if (!PlanObj->TryGetArrayField(TEXT("steps"), StepsArr) || StepsArr->Num() == 0) return nullptr;

	TArray<TSharedPtr<FJsonValue>> TaskArr;
	for (int32 i = 0; i < StepsArr->Num(); ++i)
	{
		TSharedPtr<FJsonObject> S = (*StepsArr)[i]->AsObject();
		if (!S.IsValid()) continue;
		FString Desc, Status;
		S->TryGetStringField(TEXT("description"), Desc);
		S->TryGetStringField(TEXT("status"), Status);
		TSharedPtr<FJsonObject> T = MakeShareable(new FJsonObject);
		T->SetNumberField(TEXT("index"), TaskArr.Num());
		T->SetStringField(TEXT("content"), Desc);
		T->SetStringField(TEXT("status"), NormalizeTaskStatus(Status));
		TaskArr.Add(MakeShareable(new FJsonValueObject(T)));
	}
	if (TaskArr.Num() == 0) return nullptr;

	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetStringField(TEXT("conv_id"), ConvID);
	Obj->SetStringField(TEXT("created_at"), FDateTime::Now().ToString());
	Obj->SetArrayField(TEXT("tasks"), TaskArr);
	return Obj;
}

void FTaskManager::LoadTasks(const FString& ConvID)
{
	FString FilePath = GetTaskFilePath(ConvID);
	if (FPaths::FileExists(FilePath))
	{
		FString JsonString;
		FFileHelper::LoadFileToString(JsonString, *FilePath);
		TSharedPtr<FJsonObject> Obj;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
		if (FJsonSerializer::Deserialize(Reader, Obj) && Obj.IsValid())
			TasksByConv.Add(ConvID, Obj);
		return;
	}

	if (TSharedPtr<FJsonObject> Migrated = MigrateLegacyPlanSteps(ConvID))
	{
		TasksByConv.Add(ConvID, Migrated);
		SaveTasks(ConvID);
	}
}
