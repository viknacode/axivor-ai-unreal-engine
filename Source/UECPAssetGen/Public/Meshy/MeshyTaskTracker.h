// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Interfaces/IHttpRequest.h"
#include "Meshy/MeshyTypes.h"

DECLARE_MULTICAST_DELEGATE_TwoParams(FOnMeshyTaskAdded,    const FMeshyTaskInfo& , FString );
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnMeshyTaskProgress, const FMeshyTaskInfo& , int32 );
DECLARE_MULTICAST_DELEGATE_OneParam (FOnMeshyTaskFinished, const FMeshyTaskResult& );

class UECPASSETGEN_API FMeshyTaskTracker
{
public:
	static FMeshyTaskTracker& Get();

	void AddTask(
		const FMeshyTaskInfo& Info,
		TFunction<void(const FMeshyTaskResult&)> OnComplete);

	bool CancelTask(const FString& TaskId);
	void CancelAll();

	TArray<FMeshyTaskInfo> GetActiveTasks() const;

	FOnMeshyTaskAdded    OnTaskAdded;
	FOnMeshyTaskProgress OnTaskProgress;
	FOnMeshyTaskFinished OnTaskFinished;

private:
	FMeshyTaskTracker() = default;

	struct FPendingTask
	{
		FMeshyTaskInfo Info;
		TFunction<void(const FMeshyTaskResult&)> OnComplete;
		int32 PollCount = 0;
		FTSTicker::FDelegateHandle TickerHandle;
		TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> ActivePoll;
		bool bCanceled = false;
	};

	void SchedulePoll(TSharedPtr<FPendingTask> Task);
	void DoPoll       (TSharedPtr<FPendingTask> Task);
	void DeliverResult(TSharedPtr<FPendingTask> Task, FMeshyTaskResult&& Result);

	static float GetPollDelay(int32 PollCount);

	TMap<FString , TSharedPtr<FPendingTask>> ActiveTasks;
};
