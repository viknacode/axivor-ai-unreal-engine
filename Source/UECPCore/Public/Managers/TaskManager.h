// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "AssetReferenceTypes.h"
#include "Dom/JsonObject.h"

class UECPCORE_API FTaskManager
{
public:
	static FTaskManager& Get();

	void SetTasks(const FString& ConvID, const TArray<FTask>& Tasks);

	void AddTask(const FString& ConvID, int32 AtIndex, const FString& Content, const FString& Verify = FString());

	void UpdateTaskStatus(const FString& ConvID, int32 Index, const FString& Status);
	// Axivor: like UpdateTaskStatus, but a task that declares a `verify` read-back cannot be
	// marked done without a non-empty verification note. Returns false (with OutMessage) when refused.
	bool UpdateTaskStatusVerified(const FString& ConvID, int32 Index, const FString& Status, const FString& Verification, FString& OutMessage);

	void EditTaskContent(const FString& ConvID, int32 Index, const FString& NewContent);

	void RemoveTask(const FString& ConvID, int32 Index);

	void ReorderTask(const FString& ConvID, int32 FromIndex, int32 ToIndex);

	void ClearTasks(const FString& ConvID);

	bool HasTasks(const FString& ConvID) const;

	FString GetTasksForAI(const FString& ConvID) const;

	FString GetTasksJson(const FString& ConvID) const;

	void SaveTasks(const FString& ConvID);

	void LoadTasks(const FString& ConvID);

private:
	FTaskManager() = default;
	FTaskManager(const FTaskManager&) = delete;
	FTaskManager& operator=(const FTaskManager&) = delete;

	FString GetTaskFilePath(const FString& ConvID) const;
	void EnsureTaskDirExists() const;

	static void ReindexTasks(TArray<TSharedPtr<class FJsonValue>>& TasksArr);

	TSharedPtr<FJsonObject> MigrateLegacyPlanSteps(const FString& ConvID) const;

	TMap<FString, TSharedPtr<FJsonObject>> TasksByConv;
};
