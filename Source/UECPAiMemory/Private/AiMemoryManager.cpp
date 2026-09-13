// Copyright 2026, BlueprintsLab, All rights reserved

#include "AiMemoryManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"

FAiMemoryManager& FAiMemoryManager::Get()
{
	static FAiMemoryManager Instance;
	return Instance;
}

void FAiMemoryManager::SaveManifest()
{
	FString MemoryDir = FPaths::ProjectSavedDir() / TEXT("BpGeneratorUltimate");
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

	if (!PlatformFile.DirectoryExists(*MemoryDir))
	{
		PlatformFile.CreateDirectoryTree(*MemoryDir);
	}

	TSharedPtr<FJsonObject> JsonObj = MakeShareable(new FJsonObject);
	JsonObj->SetNumberField(TEXT("Version"), 1);
	JsonObj->SetStringField(TEXT("ProjectName"), FApp::GetProjectName());
	JsonObj->SetStringField(TEXT("LastUpdated"), FDateTime::Now().ToString());

	TArray<TSharedPtr<FJsonValue>> MemoriesArray;
	for (const TSharedPtr<FAiMemoryEntry>& Memory : AiMemories)
	{
		if (!Memory.IsValid()) continue;

		TSharedPtr<FJsonObject> MemObj = MakeShareable(new FJsonObject);
		MemObj->SetStringField(TEXT("MemoryId"), Memory->MemoryId.ToString());
		MemObj->SetNumberField(TEXT("Category"), (int32)Memory->Category);
		MemObj->SetStringField(TEXT("Content"), Memory->Content);
		MemObj->SetStringField(TEXT("Source"), Memory->Source);
		MemObj->SetStringField(TEXT("CreatedAt"), Memory->CreatedAt.ToString());
		MemObj->SetStringField(TEXT("LastAccessed"), Memory->LastAccessed.ToString());
		MemObj->SetNumberField(TEXT("AccessCount"), Memory->AccessCount);
		MemObj->SetBoolField(TEXT("bEnabled"), Memory->bEnabled);
		MemoriesArray.Add(MakeShareable(new FJsonValueObject(MemObj)));
	}
	JsonObj->SetArrayField(TEXT("Memories"), MemoriesArray);

	TArray<TSharedPtr<FJsonValue>> PendingArray;
	for (const TSharedPtr<FAiMemoryEntry>& Memory : PendingMemories)
	{
		if (!Memory.IsValid()) continue;
		TSharedPtr<FJsonObject> MemObj = MakeShareable(new FJsonObject);
		MemObj->SetStringField(TEXT("MemoryId"), Memory->MemoryId.ToString());
		MemObj->SetNumberField(TEXT("Category"), (int32)Memory->Category);
		MemObj->SetStringField(TEXT("Content"), Memory->Content);
		MemObj->SetStringField(TEXT("Source"), Memory->Source);
		PendingArray.Add(MakeShareable(new FJsonValueObject(MemObj)));
	}
	JsonObj->SetArrayField(TEXT("PendingMemories"), PendingArray);

	FString ManifestPath = MemoryDir / TEXT("ai_memory.json");
	FString JsonString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonString);
	FJsonSerializer::Serialize(JsonObj.ToSharedRef(), Writer);

	FFileHelper::SaveStringToFile(JsonString, *ManifestPath);
}

void FAiMemoryManager::LoadManifest()
{
	FString ManifestPath = FPaths::ProjectSavedDir() / TEXT("BpGeneratorUltimate") / TEXT("ai_memory.json");

	if (!FPaths::FileExists(ManifestPath))
	{
		return;
	}

	FString JsonString;
	if (!FFileHelper::LoadFileToString(JsonString, *ManifestPath))
	{
		UE_LOG(LogTemp, Warning, TEXT("AiMemoryManager: Could not read memory file: %s"), *ManifestPath);
		return;
	}

	TSharedPtr<FJsonObject> JsonObj;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);

	if (FJsonSerializer::Deserialize(Reader, JsonObj) && JsonObj.IsValid())
	{
		AiMemories.Empty();

		const TArray<TSharedPtr<FJsonValue>>* MemoriesArray;
		if (JsonObj->TryGetArrayField(TEXT("Memories"), MemoriesArray))
		{
			for (const TSharedPtr<FJsonValue>& MemValue : *MemoriesArray)
			{
				TSharedPtr<FJsonObject> MemObj = MemValue->AsObject();
				if (!MemObj.IsValid()) continue;

				TSharedPtr<FAiMemoryEntry> Memory = MakeShareable(new FAiMemoryEntry);

				FString MemoryIdStr;
				if (MemObj->TryGetStringField(TEXT("MemoryId"), MemoryIdStr))
				{
					Memory->MemoryId = FGuid(MemoryIdStr);
				}

				int32 CategoryInt;
				if (MemObj->TryGetNumberField(TEXT("Category"), CategoryInt))
				{
					Memory->Category = (EAiMemoryCategory)CategoryInt;
				}

				MemObj->TryGetStringField(TEXT("Content"), Memory->Content);
				MemObj->TryGetStringField(TEXT("Source"), Memory->Source);

				FString CreatedAtStr, LastAccessedStr;
				if (MemObj->TryGetStringField(TEXT("CreatedAt"), CreatedAtStr))
				{
					FDateTime::Parse(CreatedAtStr, Memory->CreatedAt);
				}
				if (MemObj->TryGetStringField(TEXT("LastAccessed"), LastAccessedStr))
				{
					FDateTime::Parse(LastAccessedStr, Memory->LastAccessed);
				}

				MemObj->TryGetNumberField(TEXT("AccessCount"), Memory->AccessCount);
				MemObj->TryGetBoolField(TEXT("bEnabled"), Memory->bEnabled);

				AiMemories.Add(Memory);
			}
		}

		PendingMemories.Empty();
		const TArray<TSharedPtr<FJsonValue>>* PendingArray;
		if (JsonObj->TryGetArrayField(TEXT("PendingMemories"), PendingArray))
		{
			for (const TSharedPtr<FJsonValue>& V : *PendingArray)
			{
				TSharedPtr<FJsonObject> O = V->AsObject();
				if (!O.IsValid()) continue;
				TSharedPtr<FAiMemoryEntry> E = MakeShareable(new FAiMemoryEntry);
				FString IdStr; if (O->TryGetStringField(TEXT("MemoryId"), IdStr)) E->MemoryId = FGuid(IdStr);
				int32 CatInt = 0; if (O->TryGetNumberField(TEXT("Category"), CatInt)) E->Category = (EAiMemoryCategory)CatInt;
				O->TryGetStringField(TEXT("Content"), E->Content);
				O->TryGetStringField(TEXT("Source"), E->Source);
				PendingMemories.Add(E);
			}
		}
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("AiMemoryManager: Failed to parse JSON in memory file: %s"), *ManifestPath);
	}
}

FString FAiMemoryManager::GetContentForAI() const
{
	if (AiMemories.Num() == 0)
	{
		return FString();
	}

	bool bHasEnabledMemories = false;
	for (const TSharedPtr<FAiMemoryEntry>& Memory : AiMemories)
	{
		if (Memory.IsValid() && Memory->bEnabled)
		{
			bHasEnabledMemories = true;
			break;
		}
	}

	if (!bHasEnabledMemories)
	{
		return FString();
	}

	static constexpr int32 MaxMemoryTotalChars = 16000;

	FString Content = TEXT("=== AI MEMORY ===\n\n");
	int32 TotalCharsUsed = Content.Len();

	TMap<EAiMemoryCategory, TArray<FString>> CategorizedMemories;
	for (const TSharedPtr<FAiMemoryEntry>& Memory : AiMemories)
	{
		if (!Memory.IsValid() || !Memory->bEnabled) continue;
		CategorizedMemories.FindOrAdd(Memory->Category).Add(Memory->Content);
	}

	bool bTruncated = false;

	auto OutputCategory = [&](EAiMemoryCategory Category, const FString& CategoryName) {
		if (bTruncated) return;
		if (TArray<FString>* Memos = CategorizedMemories.Find(Category))
		{
			if (Memos->Num() > 0)
			{
				FString CategoryHeader = FString::Printf(TEXT("## %s\n"), *CategoryName);
				if (TotalCharsUsed + CategoryHeader.Len() > MaxMemoryTotalChars)
				{
					Content += TEXT("... [additional memory entries omitted due to size]\n");
					bTruncated = true;
					return;
				}
				Content += CategoryHeader;
				TotalCharsUsed += CategoryHeader.Len();

				for (const FString& Memo : *Memos)
				{
					FString Entry = FString::Printf(TEXT("- %s\n"), *Memo);
					if (TotalCharsUsed + Entry.Len() > MaxMemoryTotalChars)
					{
						Content += TEXT("... [additional memory entries omitted due to size]\n");
						bTruncated = true;
						return;
					}
					Content += Entry;
					TotalCharsUsed += Entry.Len();
				}
				Content += TEXT("\n");
				TotalCharsUsed += 1;
			}
		}
	};

	OutputCategory(EAiMemoryCategory::ProjectInfo, TEXT("Project Info"));
	OutputCategory(EAiMemoryCategory::RecentWork, TEXT("Recent Work"));
	OutputCategory(EAiMemoryCategory::Preferences, TEXT("Preferences"));
	OutputCategory(EAiMemoryCategory::Patterns, TEXT("Patterns"));
	OutputCategory(EAiMemoryCategory::AssetRelations, TEXT("Asset Relations"));
	OutputCategory(EAiMemoryCategory::Decisions, TEXT("Decisions"));

	Content += TEXT("=== END AI MEMORY ===\n");
	return Content;
}

int32 FAiMemoryManager::GetTokenCount() const
{
	int32 TotalTokens = 0;
	for (const TSharedPtr<FAiMemoryEntry>& Memory : AiMemories)
	{
		if (Memory.IsValid() && Memory->bEnabled)
		{
			TotalTokens += Memory->Content.Len() / 4;
		}
	}
	return TotalTokens;
}

FString FAiMemoryManager::GetCategoryDisplayName(EAiMemoryCategory Category)
{
	switch (Category)
	{
	case EAiMemoryCategory::ProjectInfo: return TEXT("Project Info");
	case EAiMemoryCategory::RecentWork: return TEXT("Recent Work");
	case EAiMemoryCategory::Preferences: return TEXT("Preferences");
	case EAiMemoryCategory::Patterns: return TEXT("Patterns");
	case EAiMemoryCategory::AssetRelations: return TEXT("Asset Relations");
	case EAiMemoryCategory::Decisions: return TEXT("Decisions");
	default: return TEXT("Unknown");
	}
}

FLinearColor FAiMemoryManager::GetCategoryColor(EAiMemoryCategory Category)
{
	switch (Category)
	{
	case EAiMemoryCategory::ProjectInfo: return FLinearColor(0.3f, 0.5f, 0.9f);
	case EAiMemoryCategory::RecentWork: return FLinearColor(0.3f, 0.8f, 0.4f);
	case EAiMemoryCategory::Preferences: return FLinearColor(0.7f, 0.4f, 0.9f);
	case EAiMemoryCategory::Patterns: return FLinearColor(0.9f, 0.6f, 0.2f);
	case EAiMemoryCategory::AssetRelations: return FLinearColor(0.2f, 0.7f, 0.8f);
	case EAiMemoryCategory::Decisions: return FLinearColor(0.9f, 0.8f, 0.2f);
	default: return FLinearColor::White;
	}
}
