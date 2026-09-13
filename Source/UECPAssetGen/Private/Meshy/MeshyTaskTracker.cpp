// Copyright 2026, BlueprintsLab, All rights reserved

#include "Meshy/MeshyTaskTracker.h"
#include "Meshy/MeshyHttpClient.h"
#include "UECPAssetGenModule.h"

FMeshyTaskTracker& FMeshyTaskTracker::Get()
{
	static FMeshyTaskTracker Instance;
	return Instance;
}

float FMeshyTaskTracker::GetPollDelay(int32 PollCount)
{
	if (PollCount < 3)  return 2.f;
	if (PollCount < 8)  return 5.f;
	if (PollCount < 20) return 10.f;
	return 30.f;
}

void FMeshyTaskTracker::AddTask(
	const FMeshyTaskInfo& Info,
	TFunction<void(const FMeshyTaskResult&)> OnComplete)
{
	if (Info.TaskId.IsEmpty() || Info.PollUrl.IsEmpty())
	{
		UE_LOG(LogUECPAssetGen, Warning, TEXT("MeshyTaskTracker: refusing to add task with empty id or poll url"));
		FMeshyTaskResult Failure;
		Failure.Status = TEXT("FAILED");
		Failure.ErrorMessage = TEXT("Tracker received empty task id or poll url");
		if (OnComplete) OnComplete(Failure);
		return;
	}

	if (ActiveTasks.Contains(Info.TaskId))
	{
		UE_LOG(LogUECPAssetGen, Warning, TEXT("MeshyTaskTracker: duplicate task id %s — replacing callback"), *Info.TaskId);
		ActiveTasks.Remove(Info.TaskId);
	}

	TSharedPtr<FPendingTask> Task = MakeShared<FPendingTask>();
	Task->Info = Info;
	if (Task->Info.CreatedAt.GetTicks() == 0)
	{
		Task->Info.CreatedAt = FDateTime::UtcNow();
	}
	Task->OnComplete = MoveTemp(OnComplete);
	ActiveTasks.Add(Info.TaskId, Task);

	OnTaskAdded.Broadcast(Task->Info, Task->Info.Label);
	SchedulePoll(Task);
}

void FMeshyTaskTracker::SchedulePoll(TSharedPtr<FPendingTask> Task)
{
	if (!Task.IsValid() || Task->bCanceled) return;

	const float Delay = GetPollDelay(Task->PollCount);

	Task->TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
		[this, WeakTask = TWeakPtr<FPendingTask>(Task)](float) mutable
		{
			TSharedPtr<FPendingTask> Pinned = WeakTask.Pin();
			if (!Pinned.IsValid() || Pinned->bCanceled) return false;
			DoPoll(Pinned);
			return false;
		}), Delay);
}

void FMeshyTaskTracker::DoPoll(TSharedPtr<FPendingTask> Task)
{
	if (!Task.IsValid() || Task->bCanceled) return;

	Task->PollCount++;
	const FString PollUrl = Task->Info.PollUrl;
	const FString ApiKey  = Task->Info.ApiKey;

	FMeshyHttpClient::Get().Get(PollUrl, ApiKey,
		[this, WeakTask = TWeakPtr<FPendingTask>(Task)](const FMeshyHttpResult& HttpResult)
		{
			TSharedPtr<FPendingTask> Pinned = WeakTask.Pin();
			if (!Pinned.IsValid() || Pinned->bCanceled) return;

			if (!HttpResult.bSuccess || !HttpResult.ResponseJson.IsValid())
			{
				if (Pinned->PollCount >= 60)
				{
					FMeshyTaskResult Failure;
					Failure.TaskId = Pinned->Info.TaskId;
					Failure.Status = TEXT("FAILED");
					Failure.ErrorMessage = HttpResult.Error.IsEmpty()
						? FString::Printf(TEXT("Poll failed (HTTP %d) after %d attempts"), HttpResult.HttpCode, Pinned->PollCount)
						: HttpResult.Error.ToDisplayString();
					DeliverResult(Pinned, MoveTemp(Failure));
					return;
				}
				SchedulePoll(Pinned);
				return;
			}

			const TSharedPtr<FJsonObject>& Json = HttpResult.ResponseJson;
			FString Status;
			Json->TryGetStringField(TEXT("status"), Status);

			int32 Progress = 0;
			if (Json->HasField(TEXT("progress")))
			{
				Progress = static_cast<int32>(Json->GetNumberField(TEXT("progress")));
			}

			Pinned->Info.Status   = Status;
			Pinned->Info.Progress = Progress;
			OnTaskProgress.Broadcast(Pinned->Info, Progress);

			if (Status.Equals(TEXT("SUCCEEDED"), ESearchCase::IgnoreCase))
			{
				FMeshyTaskResult Done;
				Done.bSuccess     = true;
				Done.TaskId       = Pinned->Info.TaskId;
				Done.Status       = Status;
				Done.Progress     = 100;
				Done.ResponseJson = Json;
				DeliverResult(Pinned, MoveTemp(Done));
				return;
			}

			if (Status.Equals(TEXT("FAILED"), ESearchCase::IgnoreCase) ||
				Status.Equals(TEXT("CANCELED"), ESearchCase::IgnoreCase) ||
				Status.Equals(TEXT("CANCELLED"), ESearchCase::IgnoreCase))
			{
				FMeshyError Err;
				const TSharedPtr<FJsonObject>* ErrObj = nullptr;
				if (Json->TryGetObjectField(TEXT("task_error"), ErrObj) && ErrObj && ErrObj->IsValid())
				{
					(*ErrObj)->TryGetStringField(TEXT("type"),    Err.Type);
					(*ErrObj)->TryGetStringField(TEXT("message"), Err.Message);
				}

				FMeshyTaskResult Failed;
				Failed.TaskId       = Pinned->Info.TaskId;
				Failed.Status       = Status;
				Failed.Progress     = Progress;
				Failed.ResponseJson = Json;
				Failed.ErrorMessage = Err.IsEmpty()
					? FString::Printf(TEXT("Task %s"), *Status)
					: Err.ToDisplayString();
				DeliverResult(Pinned, MoveTemp(Failed));
				return;
			}

			SchedulePoll(Pinned);
		});
}

void FMeshyTaskTracker::DeliverResult(TSharedPtr<FPendingTask> Task, FMeshyTaskResult&& Result)
{
	if (!Task.IsValid()) return;

	if (Task->TickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(Task->TickerHandle);
		Task->TickerHandle.Reset();
	}

	auto Cb = MoveTemp(Task->OnComplete);
	Task->OnComplete = nullptr;

	const FString TaskId = Task->Info.TaskId;
	ActiveTasks.Remove(TaskId);

	OnTaskFinished.Broadcast(Result);
	if (Cb)
	{
		Cb(Result);
	}
}

bool FMeshyTaskTracker::CancelTask(const FString& TaskId)
{
	TSharedPtr<FPendingTask>* Found = ActiveTasks.Find(TaskId);
	if (!Found || !Found->IsValid()) return false;

	TSharedPtr<FPendingTask> Task = *Found;
	Task->bCanceled = true;

	if (Task->ActivePoll.IsValid())
	{
		Task->ActivePoll->CancelRequest();
		Task->ActivePoll.Reset();
	}

	FMeshyTaskResult Canceled;
	Canceled.TaskId       = TaskId;
	Canceled.Status       = TEXT("CANCELED");
	Canceled.ErrorMessage = TEXT("Canceled by user");
	DeliverResult(Task, MoveTemp(Canceled));
	return true;
}

void FMeshyTaskTracker::CancelAll()
{
	TArray<FString> Ids;
	ActiveTasks.GetKeys(Ids);
	for (const FString& Id : Ids)
	{
		CancelTask(Id);
	}
}

TArray<FMeshyTaskInfo> FMeshyTaskTracker::GetActiveTasks() const
{
	TArray<FMeshyTaskInfo> Out;
	Out.Reserve(ActiveTasks.Num());
	for (const auto& Pair : ActiveTasks)
	{
		if (Pair.Value.IsValid())
		{
			Out.Add(Pair.Value->Info);
		}
	}
	return Out;
}
