// Copyright 2026, BlueprintsLab, All rights reserved

#include "Managers/PlanManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonValue.h"

FPlanManager& FPlanManager::Get()
{
	static FPlanManager Instance;
	return Instance;
}

FString FPlanManager::GetPlanFilePath(const FString& ConvID) const
{
	return FPaths::ProjectSavedDir() / TEXT("BpGeneratorUltimate") / TEXT("plans") / ConvID + TEXT(".json");
}

void FPlanManager::EnsurePlanDirExists() const
{
	FString Dir = FPaths::ProjectSavedDir() / TEXT("BpGeneratorUltimate") / TEXT("plans");
	IPlatformFile& PF = FPlatformFileManager::Get().GetPlatformFile();
	if (!PF.DirectoryExists(*Dir))
	{
		PF.CreateDirectoryTree(*Dir);
	}
}

void FPlanManager::CreatePlan(const FString& ConvID, const FString& Title, const FString& Context)
{
	TSharedPtr<FJsonObject> PlanObj = MakeShareable(new FJsonObject);
	PlanObj->SetStringField(TEXT("title"), Title);
	PlanObj->SetStringField(TEXT("conv_id"), ConvID);
	PlanObj->SetStringField(TEXT("created_at"), FDateTime::Now().ToString());
	if (!Context.IsEmpty())
		PlanObj->SetStringField(TEXT("context"), Context);

	Plans.Add(ConvID, PlanObj);
	SavePlan(ConvID);
}

void FPlanManager::SetBrief(const FString& ConvID, const FString& Brief)
{
	if (!Plans.Contains(ConvID)) LoadPlan(ConvID);
	TSharedPtr<FJsonObject>* PlanPtr = Plans.Find(ConvID);
	if (!PlanPtr || !PlanPtr->IsValid())
	{
		CreatePlan(ConvID, TEXT("Plan"), Brief);
		return;
	}
	(*PlanPtr)->SetStringField(TEXT("context"), Brief);
	SavePlan(ConvID);
}

bool FPlanManager::HasActivePlan(const FString& ConvID) const
{
	if (Plans.Contains(ConvID))
	{
		return Plans[ConvID].IsValid();
	}
	return FPaths::FileExists(GetPlanFilePath(ConvID));
}

FString FPlanManager::GetPlanJson(const FString& ConvID) const
{
	if (!Plans.Contains(ConvID))
	{
		const_cast<FPlanManager*>(this)->LoadPlan(ConvID);
	}
	const TSharedPtr<FJsonObject>* PlanPtr = Plans.Find(ConvID);
	if (!PlanPtr || !PlanPtr->IsValid()) return TEXT("{}");

	FString Out;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize((*PlanPtr).ToSharedRef(), Writer);
	return Out;
}

FString FPlanManager::GetPlanForAI(const FString& ConvID) const
{
	if (!Plans.Contains(ConvID))
	{
		const_cast<FPlanManager*>(this)->LoadPlan(ConvID);
	}
	const TSharedPtr<FJsonObject>* PlanPtr = Plans.Find(ConvID);
	if (!PlanPtr || !PlanPtr->IsValid()) return FString();

	FString Title, Context;
	(*PlanPtr)->TryGetStringField(TEXT("title"), Title);
	(*PlanPtr)->TryGetStringField(TEXT("context"), Context);
	if (Title.IsEmpty() && Context.IsEmpty()) return FString();

	FString Out = FString::Printf(TEXT("=== PLAN: %s ===\n"), Title.IsEmpty() ? TEXT("(untitled)") : *Title);
	if (!Context.IsEmpty())
		Out += Context + TEXT("\n");
	Out += TEXT("=== END PLAN ===");
	return Out;
}

void FPlanManager::ClearPlan(const FString& ConvID)
{
	Plans.Remove(ConvID);

	FString FilePath = GetPlanFilePath(ConvID);
	if (FPaths::FileExists(FilePath))
	{
		IFileManager::Get().Delete(*FilePath);
	}
}

void FPlanManager::SavePlan(const FString& ConvID)
{
	const TSharedPtr<FJsonObject>* PlanPtr = Plans.Find(ConvID);
	if (!PlanPtr || !PlanPtr->IsValid()) return;

	EnsurePlanDirExists();

	FString JsonString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonString);
	FJsonSerializer::Serialize((*PlanPtr).ToSharedRef(), Writer);

	FFileHelper::SaveStringToFile(JsonString, *GetPlanFilePath(ConvID));
}

TArray<FPlanManager::FPlanSummary> FPlanManager::GetAllPlanSummaries() const
{
	TArray<FPlanSummary> Results;

	FString PlansDir = FPaths::ProjectSavedDir() / TEXT("BpGeneratorUltimate") / TEXT("plans");
	TArray<FString> Files;
	IFileManager::Get().FindFiles(Files, *(PlansDir / TEXT("*.json")), true, false);

	for (const FString& FileName : Files)
	{
		FString FilePath = PlansDir / FileName;
		FString JsonString;
		if (!FFileHelper::LoadFileToString(JsonString, *FilePath)) continue;

		TSharedPtr<FJsonObject> PlanObj;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
		if (!FJsonSerializer::Deserialize(Reader, PlanObj) || !PlanObj.IsValid()) continue;

		FPlanSummary Summary;
		Summary.ConvID = FPaths::GetBaseFilename(FileName);
		PlanObj->TryGetStringField(TEXT("title"), Summary.Title);
		if (Summary.Title.IsEmpty()) Summary.Title = TEXT("Untitled Plan");
		PlanObj->TryGetStringField(TEXT("created_at"), Summary.CreatedAt);

		FString Context;
		if (PlanObj->TryGetStringField(TEXT("context"), Context) && !Context.IsEmpty())
		{
			Context = Context.Replace(TEXT("\r"), TEXT(" ")).Replace(TEXT("\n"), TEXT(" ")).TrimStartAndEnd();
			Summary.ContextSnippet = Context.Len() > 160 ? (Context.Left(160) + TEXT("…")) : Context;
		}

		Results.Add(Summary);
	}

	return Results;
}

void FPlanManager::ImportPlan(const FString& SourceConvID, const FString& TargetConvID)
{
	if (!Plans.Contains(SourceConvID))
		LoadPlan(SourceConvID);

	const TSharedPtr<FJsonObject>* SrcPtr = Plans.Find(SourceConvID);
	if (!SrcPtr || !SrcPtr->IsValid()) return;

	FString JsonString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonString);
	FJsonSerializer::Serialize((*SrcPtr).ToSharedRef(), Writer);

	TSharedPtr<FJsonObject> Copy;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
	if (!FJsonSerializer::Deserialize(Reader, Copy) || !Copy.IsValid()) return;

	Copy->RemoveField(TEXT("steps"));
	Copy->SetStringField(TEXT("conv_id"), TargetConvID);
	Copy->SetStringField(TEXT("imported_from"), SourceConvID);
	Copy->SetStringField(TEXT("imported_at"), FDateTime::Now().ToString());

	Plans.Add(TargetConvID, Copy);
	SavePlan(TargetConvID);
}

void FPlanManager::LoadPlan(const FString& ConvID)
{
	FString FilePath = GetPlanFilePath(ConvID);
	if (!FPaths::FileExists(FilePath)) return;

	FString JsonString;
	FFileHelper::LoadFileToString(JsonString, *FilePath);

	TSharedPtr<FJsonObject> PlanObj;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
	if (FJsonSerializer::Deserialize(Reader, PlanObj) && PlanObj.IsValid())
	{
		Plans.Add(ConvID, PlanObj);
	}
}
