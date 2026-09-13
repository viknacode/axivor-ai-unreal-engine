// Copyright 2026, BlueprintsLab, All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace ChooserTools
{
	UECPANIMATIONEXT_API void HandleCreateChooserTable(const FString& AssetName, const FString& SavePath,
		const FString& OutputClass, FString& OutJson, FString& OutError);

	UECPANIMATIONEXT_API void HandleAddChooserColumn(const FString& ChooserPath, const FString& ColumnType,
		const FString& Label, FString& OutJson, FString& OutError);

	UECPANIMATIONEXT_API void HandleAddChooserRow(const FString& ChooserPath, const FString& AssetPath,
		FString& OutJson, FString& OutError);

	UECPANIMATIONEXT_API void HandleGetChooserSummary(const FString& ChooserPath, FString& OutJson, FString& OutError);

	UECPANIMATIONEXT_API void HandleSetChooserRowValue(const FString& ChooserPath, int32 ColumnIndex, int32 RowIndex,
		const FString& PropertyName, const FString& PropertyValue, FString& OutJson, FString& OutError);

	UECPANIMATIONEXT_API void HandleSetChooserOutputType(const FString& ChooserPath, const FString& OutputClass,
		FString& OutJson, FString& OutError);

	UECPANIMATIONEXT_API void HandleRemoveChooserRow(const FString& ChooserPath, int32 RowIndex,
		FString& OutJson, FString& OutError);

	UECPANIMATIONEXT_API void HandleRemoveChooserColumn(const FString& ChooserPath, int32 ColumnIndex,
		FString& OutJson, FString& OutError);

	UECPANIMATIONEXT_API void HandleSetChooserColumnProperty(const FString& ChooserPath, int32 ColumnIndex,
		const FString& PropertyName, const FString& PropertyValue, FString& OutJson, FString& OutError);

	UECPANIMATIONEXT_API void HandleSetChooserFallback(const FString& ChooserPath, const FString& AssetPath,
		FString& OutJson, FString& OutError);

	UECPANIMATIONEXT_API void HandleDuplicateChooserRow(const FString& ChooserPath, int32 RowIndex,
		FString& OutJson, FString& OutError);

	UECPANIMATIONEXT_API void HandleReorderChooserRows(const FString& ChooserPath, int32 FromIndex, int32 ToIndex,
		FString& OutJson, FString& OutError);

	UECPANIMATIONEXT_API void HandleGetChooserRowValues(const FString& ChooserPath, int32 RowIndex,
		FString& OutJson, FString& OutError);

	UECPANIMATIONEXT_API void HandleGetChooserColumnProperties(const FString& ChooserPath, int32 ColumnIndex,
		FString& OutJson, FString& OutError);

	UECPANIMATIONEXT_API void HandleBulkSetChooserRows(const FString& ChooserPath, const FString& RowsJson,
		FString& OutJson, FString& OutError);

	UECPANIMATIONEXT_API void HandleRenameChooserColumn(const FString& ChooserPath, int32 ColumnIndex,
		const FString& NewLabel, FString& OutJson, FString& OutError);

	UECPANIMATIONEXT_API void HandleReorderChooserColumns(const FString& ChooserPath, int32 FromIndex, int32 ToIndex,
		FString& OutJson, FString& OutError);

	UECPANIMATIONEXT_API void HandleDuplicateChooserColumn(const FString& ChooserPath, int32 ColumnIndex,
		FString& OutJson, FString& OutError);

	UECPANIMATIONEXT_API void HandleSetChooserContextData(const FString& ChooserPath, const FString& ContextClass,
		FString& OutJson, FString& OutError);

	UECPANIMATIONEXT_API void HandleAddChooserColumnFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);
	UECPANIMATIONEXT_API void HandleAddChooserRowFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);
	UECPANIMATIONEXT_API void HandleCreateChooserTableFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);
	UECPANIMATIONEXT_API void HandleGetChooserSummaryFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);
	UECPANIMATIONEXT_API void HandleSetChooserRowValueFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);
	UECPANIMATIONEXT_API void HandleSetChooserOutputTypeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);
	UECPANIMATIONEXT_API void HandleRemoveChooserRowFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);
	UECPANIMATIONEXT_API void HandleRemoveChooserColumnFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);
	UECPANIMATIONEXT_API void HandleSetChooserColumnPropertyFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);
	UECPANIMATIONEXT_API void HandleSetChooserFallbackFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);
	UECPANIMATIONEXT_API void HandleDuplicateChooserRowFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);
	UECPANIMATIONEXT_API void HandleReorderChooserRowsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);
	UECPANIMATIONEXT_API void HandleGetChooserRowValuesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);
	UECPANIMATIONEXT_API void HandleGetChooserColumnPropertiesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);
	UECPANIMATIONEXT_API void HandleBulkSetChooserRowsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);
	UECPANIMATIONEXT_API void HandleRenameChooserColumnFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);
	UECPANIMATIONEXT_API void HandleReorderChooserColumnsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);
	UECPANIMATIONEXT_API void HandleDuplicateChooserColumnFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);
	UECPANIMATIONEXT_API void HandleSetChooserContextDataFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);
}
