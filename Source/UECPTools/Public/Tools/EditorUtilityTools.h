// Copyright 2026, BlueprintsLab, All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace EditorUtilityTools
{

	UECPTOOLS_API void HandleCreateEditorUtilityWidget(const FString& Name, const FString& SavePath,
		FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleRunEditorUtilityWidget(const FString& AssetPath,
		FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleCreateEditorUtilityBlueprint(const FString& Name, const FString& SavePath,
		FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleExecConsoleCommand(const FString& Command, FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleTakeViewportScreenshot(const FString& FilePath, FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleSaveAsset(const FString& AssetPath, FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleSaveAllDirtyAssets(FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleGetConsoleVariable(const FString& VarName, FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleOpenAssetEditor(const FString& AssetPath, FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleExportAsset(const FString& AssetPath, const FString& ExportPath,
		FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleGetOutputLog(int32 LineCount, const FString& CategoryFilter, const FString& SeverityFilter,
		FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleGetOutputLogSince(int32 LineCount, const FString& CategoryFilter, const FString& SeverityFilter,
		double SinceTimestamp, FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleGetMapCheckErrors(FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleValidateAssets(const TArray<FString>& AssetPaths, FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleFixUpRedirectors(const FString& FolderPath, FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleGetProjectSetting(const FString& Section, const FString& Key, const FString& IniFile,
		FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleSetProjectSetting(const FString& Section, const FString& Key, const FString& Value, const FString& IniFile,
		FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleBatchRenameAssets(const FString& FolderPath, const FString& Find, const FString& Replace,
		const FString& Prefix, const FString& Suffix, FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleCreateEditorUtilityWidgetFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPTOOLS_API void HandleRunEditorUtilityWidgetFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPTOOLS_API void HandleCreateEditorUtilityBlueprintFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleExecConsoleCommandFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPTOOLS_API void HandleTakeViewportScreenshotFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPTOOLS_API void HandleSaveAssetFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPTOOLS_API void HandleSaveAllDirtyAssetsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPTOOLS_API void HandleGetConsoleVariableFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPTOOLS_API void HandleOpenAssetEditorFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPTOOLS_API void HandleExportAssetFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPTOOLS_API void HandleGetOutputLogFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPTOOLS_API void HandleGetMapCheckErrorsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPTOOLS_API void HandleValidateAssetsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPTOOLS_API void HandleFixUpRedirectorsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPTOOLS_API void HandleGetProjectSettingFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPTOOLS_API void HandleSetProjectSettingFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPTOOLS_API void HandleBatchRenameAssetsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
}
