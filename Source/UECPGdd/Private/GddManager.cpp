// Copyright 2026, BlueprintsLab, All rights reserved

#include "GddManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"

FGddManager& FGddManager::Get()
{
	static FGddManager Instance;
	return Instance;
}

void FGddManager::SaveManifest()
{
	FString GddDir = FPaths::ProjectSavedDir() / TEXT("BpGeneratorUltimate") / TEXT("gdd");
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

	if (!PlatformFile.DirectoryExists(*GddDir))
	{
		PlatformFile.CreateDirectoryTree(*GddDir);
	}

	TSharedPtr<FJsonObject> JsonObj = MakeShareable(new FJsonObject);
	JsonObj->SetNumberField(TEXT("Version"), 1);

	TArray<TSharedPtr<FJsonValue>> FilesArray;
	for (const TSharedPtr<FGddFileEntry>& FilePtr : GddFiles)
	{
		if (!FilePtr.IsValid()) continue;

		TSharedPtr<FJsonObject> FileObj = MakeShareable(new FJsonObject);
		FileObj->SetStringField(TEXT("FileId"), FilePtr->FileId.ToString());
		FileObj->SetStringField(TEXT("FileName"), FilePtr->FileName);
		FileObj->SetStringField(TEXT("FilePath"), FilePtr->FilePath);
		FileObj->SetNumberField(TEXT("CharCount"), FilePtr->CharCount);
		FileObj->SetNumberField(TEXT("EstimatedTokens"), FilePtr->EstimatedTokens);
		FileObj->SetBoolField(TEXT("bEnabled"), FilePtr->bEnabled);
		FileObj->SetStringField(TEXT("LastModified"), FilePtr->LastModified.ToString());
		FilesArray.Add(MakeShareable(new FJsonValueObject(FileObj)));
	}
	JsonObj->SetArrayField(TEXT("Files"), FilesArray);

	FString ManifestPath = GddDir / TEXT("gdd_manifest.json");
	FString JsonString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonString);
	FJsonSerializer::Serialize(JsonObj.ToSharedRef(), Writer);

	FFileHelper::SaveStringToFile(JsonString, *ManifestPath);
}

void FGddManager::LoadManifest()
{
	FString ManifestPath = FPaths::ProjectSavedDir() / TEXT("BpGeneratorUltimate") / TEXT("gdd") / TEXT("gdd_manifest.json");

	if (!FPaths::FileExists(ManifestPath))
	{
		return;
	}

	FString JsonString;
	FFileHelper::LoadFileToString(JsonString, *ManifestPath);

	TSharedPtr<FJsonObject> JsonObj;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);

	if (FJsonSerializer::Deserialize(Reader, JsonObj) && JsonObj.IsValid())
	{
		GddFiles.Empty();

		const TArray<TSharedPtr<FJsonValue>>* FilesArray;
		if (JsonObj->TryGetArrayField(TEXT("Files"), FilesArray))
		{
			for (const TSharedPtr<FJsonValue>& FileValue : *FilesArray)
			{
				TSharedPtr<FJsonObject> FileObj = FileValue->AsObject();
				if (!FileObj.IsValid()) continue;

				TSharedPtr<FGddFileEntry> File = MakeShareable(new FGddFileEntry);

				FString FileIdStr;
				if (FileObj->TryGetStringField(TEXT("FileId"), FileIdStr))
				{
					File->FileId = FGuid(FileIdStr);
				}
				FileObj->TryGetStringField(TEXT("FileName"), File->FileName);
				FileObj->TryGetStringField(TEXT("FilePath"), File->FilePath);
				FileObj->TryGetNumberField(TEXT("CharCount"), File->CharCount);
				FileObj->TryGetNumberField(TEXT("EstimatedTokens"), File->EstimatedTokens);
				FileObj->TryGetBoolField(TEXT("bEnabled"), File->bEnabled);

				FString LastModifiedStr;
				if (FileObj->TryGetStringField(TEXT("LastModified"), LastModifiedStr))
				{
					FDateTime::Parse(LastModifiedStr, File->LastModified);
				}

				GddFiles.Add(File);
			}
		}

	}
}

FString FGddManager::GetContentForAI() const
{
	if (GddFiles.Num() == 0)
	{
		return FString();
	}

	bool bHasEnabledFiles = false;
	for (const TSharedPtr<FGddFileEntry>& File : GddFiles)
	{
		if (File.IsValid() && File->bEnabled)
		{
			bHasEnabledFiles = true;
			break;
		}
	}

	if (!bHasEnabledFiles)
	{
		return FString();
	}

	FString Content = TEXT("=== GAME DESIGN DOCUMENT CONTEXT ===\n\n");

	for (const TSharedPtr<FGddFileEntry>& File : GddFiles)
	{
		if (!File.IsValid() || !File->bEnabled) continue;

		FString FullPath = FPaths::ProjectSavedDir() / TEXT("BpGeneratorUltimate") / TEXT("gdd") / File->FilePath;

		FString FileContent;
		if (FFileHelper::LoadFileToString(FileContent, *FullPath))
		{
			Content += FString::Printf(TEXT("--- Section: %s ---\n%s\n\n"), *File->FileName, *FileContent);
		}
	}

	Content += TEXT("=== END GDD CONTEXT ===\n");
	return Content;
}

int32 FGddManager::GetTokenCount() const
{
	int32 TotalTokens = 0;
	for (const TSharedPtr<FGddFileEntry>& File : GddFiles)
	{
		if (File.IsValid() && File->bEnabled)
		{
			TotalTokens += File->EstimatedTokens;
		}
	}
	return TotalTokens;
}
