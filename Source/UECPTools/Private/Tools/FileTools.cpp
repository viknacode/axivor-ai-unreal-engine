// Copyright 2026, BlueprintsLab, All rights reserved

#include "Tools/FileTools.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "Serialization/JsonSerializer.h"

namespace FileTools
{

void HandleSelectFolderFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString FolderPath;
	Args->TryGetStringField(TEXT("folder_path"), FolderPath);
	HandleSelectFolder(FolderPath, OutJsonString, OutError);
}

void HandleSelectFolder(const FString& FolderPath, FString& OutJsonString, FString& OutError)
{
	FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");

	ContentBrowserModule.Get().SyncBrowserToAssets(TArray<FAssetData>(), true);

	TSharedPtr<FJsonObject> ResultObject = MakeShareable(new FJsonObject);
	ResultObject->SetBoolField  (TEXT("success"),     true);
	ResultObject->SetStringField(TEXT("folder_path"), FolderPath);
	ResultObject->SetStringField(TEXT("message"),     FString::Printf(TEXT("Content Browser synced to: %s"), *FolderPath));

	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(ResultObject.ToSharedRef(), Writer);
}

}
