// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace AssetManagementTools
{
	UECPTOOLS_API void HandleCreateBlueprint(const FString& BpName, const FString& ParentClass, const FString& SavePath, FString& OutAssetPath, FString& OutError);

	UECPTOOLS_API void HandleFindAssetByName(const FString& NamePattern, const FString& AssetType, const FString& ParentClass, FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleMoveAsset(const FString& AssetPath, const FString& DestinationFolder, FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleMoveAssets(const TArray<FString>& AssetPaths, const FString& DestinationFolder, FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleCreateBlueprintInterface(const FString& Name, const FString& SavePath, FString& OutAssetPath, FString& OutError);

	UECPTOOLS_API void HandleCreateBlueprintFunctionLibrary(const FString& Name, const FString& SavePath, FString& OutAssetPath, FString& OutError);

	UECPTOOLS_API void HandleCreateMacroLibrary(const FString& Name, const FString& SavePath, FString& OutAssetPath, FString& OutError);

	UECPTOOLS_API void HandleGetLevelBlueprint(FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleDuplicateAsset(const FString& SourcePath, const FString& DestPath, FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleRenameAsset(const FString& AssetPath, const FString& NewName, FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleCompileBlueprint(const FString& BpPath, FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleDeleteAsset(const TArray<FString>& AssetPaths, FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleSetClassDefault(const FString& BpPath, const FString& PropertyName, const FString& PropertyValue, FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleCreateBlueprintFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleRegisterPrimaryAssetType(const FString& AssetTypeName, const FString& AssetBaseClass,
		const TArray<FString>& DirectoriesToScan, bool bHasBlueprintClasses,
		FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleGetAssetManagerSummary(FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleReimportAsset(const FString& AssetPath, FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleMoveAssetFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPTOOLS_API void HandleDuplicateAssetFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPTOOLS_API void HandleDeleteAssetFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPTOOLS_API void HandleFindAssetByNameFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPTOOLS_API void HandleCheckAssetExistsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPTOOLS_API void HandleMoveAssetsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPTOOLS_API void HandleCreateProjectFolderFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPTOOLS_API void HandleGetProjectRootPathFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPTOOLS_API void HandleRenameAssetFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPTOOLS_API void HandleRegisterPrimaryAssetTypeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPTOOLS_API void HandleGetAssetManagerSummaryFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPTOOLS_API void HandleReimportAssetFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleCreateBlueprintInterfaceFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPTOOLS_API void HandleCreateBlueprintFunctionLibraryFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPTOOLS_API void HandleCreateMacroLibraryFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPTOOLS_API void HandleGetLevelBlueprintFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPTOOLS_API void HandleCompileBlueprintFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPTOOLS_API void HandleSetClassDefaultFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleCreateLevelFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleValidateBlueprintFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
}
