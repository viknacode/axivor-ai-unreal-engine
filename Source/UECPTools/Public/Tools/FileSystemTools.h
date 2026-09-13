// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace FileSystemTools
{
	UECPTOOLS_API void HandleGetProjectRootPath(FString& OutPath, FString& OutError);

	UECPTOOLS_API void HandleScanDirectory(const FString& DirectoryPath, const TArray<FString>& Extensions, FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleExportToFile(const FString& FileName, const FString& FileContent, const FString& FileFormat, FString& OutError);

	UECPTOOLS_API void HandleScanDirectoryFromArgs    (const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPTOOLS_API void HandleGetToolDocsFromArgs      (const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPTOOLS_API void HandleSearchToolsFromArgs      (const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPTOOLS_API void HandleMarkLearningStepFromArgs (const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPTOOLS_API void HandleExportToFileFromArgs     (const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPTOOLS_API void HandleGetHandleReferenceFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
}
