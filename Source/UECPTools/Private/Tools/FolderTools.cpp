// Copyright 2026, BlueprintsLab, All rights reserved

#include "Tools/FolderTools.h"
#include "EditorAssetLibrary.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Utils/MountResolver.h"

namespace FolderTools
{

void HandleCreateProjectFolder(const FString& FolderPath, FString& OutJsonString, FString& OutError)
{
	TSharedPtr<FJsonObject> ResultObject = MakeShareable(new FJsonObject);

	if (FolderPath.IsEmpty() || !UECPMountResolver::IsValidMountedPath(FolderPath))
	{
		OutError = "Invalid folder path. Use /Game/... or /<PluginName>/....";
		ResultObject->SetBoolField(TEXT("success"), false);
		ResultObject->SetStringField(TEXT("error"), OutError);

		FString ResultString;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResultString);
		FJsonSerializer::Serialize(ResultObject.ToSharedRef(), Writer);
		OutJsonString = ResultString;
		return;
	}

	if (UEditorAssetLibrary::DoesDirectoryExist(FolderPath))
	{
		OutError = FString::Printf(TEXT("Folder '%s' already exists."), *FolderPath);
		ResultObject->SetBoolField(TEXT("success"), true);
		ResultObject->SetStringField(TEXT("message"), OutError);
		ResultObject->SetStringField(TEXT("folder_path"), FolderPath);

		FString ResultString;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResultString);
		FJsonSerializer::Serialize(ResultObject.ToSharedRef(), Writer);
		OutJsonString = ResultString;
		return;
	}

	if (!UEditorAssetLibrary::MakeDirectory(FolderPath))
	{
		OutError = FString::Printf(TEXT("Failed to create folder at '%s'."), *FolderPath);
		ResultObject->SetBoolField(TEXT("success"), false);
		ResultObject->SetStringField(TEXT("error"), OutError);

		FString ResultString;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResultString);
		FJsonSerializer::Serialize(ResultObject.ToSharedRef(), Writer);
		OutJsonString = ResultString;
	}
	else
	{
		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		AssetRegistryModule.Get().ScanPathsSynchronous({ FolderPath }, true);

		ResultObject->SetBoolField(TEXT("success"), true);
		ResultObject->SetStringField(TEXT("message"), FString::Printf(TEXT("Folder created at '%s'."), *FolderPath));
		ResultObject->SetStringField(TEXT("folder_path"), FolderPath);

		FString ResultString;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResultString);
		FJsonSerializer::Serialize(ResultObject.ToSharedRef(), Writer);
		OutJsonString = ResultString;
	}
}

}
