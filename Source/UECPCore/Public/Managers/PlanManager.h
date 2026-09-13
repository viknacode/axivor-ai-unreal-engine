// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "AssetReferenceTypes.h"
#include "Dom/JsonObject.h"

class UECPCORE_API FPlanManager
{
public:
	static FPlanManager& Get();

	void CreatePlan(const FString& ConvID, const FString& Title, const FString& Context = FString());

	void SetBrief(const FString& ConvID, const FString& Brief);

	bool HasActivePlan(const FString& ConvID) const;

	FString GetPlanForAI(const FString& ConvID) const;

	FString GetPlanJson(const FString& ConvID) const;

	void ClearPlan(const FString& ConvID);

	void SavePlan(const FString& ConvID);

	void LoadPlan(const FString& ConvID);

	struct FPlanSummary
	{
		FString ConvID;
		FString Title;
		FString CreatedAt;
		FString ContextSnippet;
	};

	TArray<FPlanSummary> GetAllPlanSummaries() const;

	void ImportPlan(const FString& SourceConvID, const FString& TargetConvID);

private:
	FPlanManager() = default;
	FPlanManager(const FPlanManager&) = delete;
	FPlanManager& operator=(const FPlanManager&) = delete;

	FString GetPlanFilePath(const FString& ConvID) const;
	void EnsurePlanDirExists() const;

	TMap<FString, TSharedPtr<FJsonObject>> Plans;
};
