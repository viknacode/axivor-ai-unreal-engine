// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace DataTableTools
{
	UECPDATAEXT_API void HandleCreateDataTable(const FString& TableName, const FString& SavePath, const FString& RowStructPath, const TArray<TSharedPtr<FJsonValue>>& Rows, FString& OutJsonString, FString& OutError);

	UECPDATAEXT_API void HandleAddDataTableRows(const FString& TablePath, const TArray<TSharedPtr<FJsonValue>>& Rows, FString& OutJsonString, FString& OutError);

	UECPDATAEXT_API void HandleEditDataTableRows(const FString& TablePath, const TArray<TSharedPtr<FJsonValue>>& Edits, FString& OutJsonString, FString& OutError);

	UECPDATAEXT_API void HandleGetDataTableRows(const FString& TablePath, FString& OutJsonString, FString& OutError);

	UECPDATAEXT_API void HandleImportDataTableCSV(const FString& TablePath, const FString& CSVContent, FString& OutJsonString, FString& OutError);

	UECPDATAEXT_API void HandleExportDataTableCSV(const FString& TablePath, FString& OutJsonString, FString& OutError);

	UECPDATAEXT_API void HandleCreateDataTableFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPDATAEXT_API void HandleAddDataTableRowsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPDATAEXT_API void HandleEditDataTableRowsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPDATAEXT_API void HandleGetDataTableRowsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPDATAEXT_API void HandleImportDataTableCSVFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPDATAEXT_API void HandleExportDataTableCSVFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPDATAEXT_API void HandleDeleteDataTableRowFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
}
