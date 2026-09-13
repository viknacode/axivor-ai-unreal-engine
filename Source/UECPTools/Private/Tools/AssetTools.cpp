// Copyright 2026, BlueprintsLab, All rights reserved

#include "Tools/AssetTools.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "Engine/Blueprint.h"
#include "UECPCoreModule.h"
#include "Services/IUECPExtensionService.h"
#include "Utils/ContentBrowserUtils.h"
#include "Utils/MountResolver.h"
#include "Managers/SettingsManager.h"

namespace AssetTools
{
	void HandleGetFocusedContentBrowserPath(FString& OutPath, FString& OutError)
	{
		UECPContentBrowserUtils::GetFocusedContentBrowserPath(OutPath, OutError);
	}

	void HandleListAssetsInFolder(const FString& FolderPath, FString& OutJsonString, FString& OutError, bool bRecursive)
	{
		FString ResolvedPath = FolderPath;
		if (ResolvedPath.IsEmpty())
		{
			FString PathError;
			HandleGetFocusedContentBrowserPath(ResolvedPath, PathError);
		}

		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));

		AssetRegistryModule.Get().ScanPathsSynchronous({ ResolvedPath }, true);

		TArray<FAssetData> AssetData;
		AssetRegistryModule.Get().GetAssetsByPath(FName(*ResolvedPath), AssetData, bRecursive);

		TArray<TSharedPtr<FJsonValue>> AssetJsonArray;
		for (const FAssetData& Data : AssetData)
		{
			TSharedPtr<FJsonObject> AssetObject = MakeShareable(new FJsonObject());
			AssetObject->SetStringField(TEXT("name"), Data.AssetName.ToString());
			AssetObject->SetStringField(TEXT("path"), Data.GetObjectPathString());
			AssetObject->SetStringField(TEXT("class"), Data.AssetClassPath.GetAssetName().ToString());
			AssetJsonArray.Add(MakeShareable(new FJsonValueObject(AssetObject)));
		}

		TSharedPtr<FJsonObject> ResultObject = MakeShareable(new FJsonObject());
		ResultObject->SetBoolField(TEXT("success"), true);
		ResultObject->SetStringField(TEXT("folder"), ResolvedPath);
		ResultObject->SetBoolField(TEXT("recursive"), bRecursive);
		ResultObject->SetNumberField(TEXT("count"), AssetData.Num());
		ResultObject->SetArrayField(TEXT("assets"), AssetJsonArray);
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
		FJsonSerializer::Serialize(ResultObject.ToSharedRef(), Writer);
	}

	void HandleGetSelectedContentBrowserAssets(FString& OutJsonString, FString& OutError)
	{
		FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
		IContentBrowserSingleton& ContentBrowserSingleton = ContentBrowserModule.Get();

		TArray<FAssetData> SelectedAssets;
		ContentBrowserSingleton.GetSelectedAssets(SelectedAssets);

		if (SelectedAssets.Num() == 0)
		{
			OutError = "No assets are currently selected in the Content Browser.";
			return;
		}

		TArray<TSharedPtr<FJsonValue>> AssetJsonArray;
		for (const FAssetData& Data : SelectedAssets)
		{
			TSharedPtr<FJsonObject> AssetObject = MakeShareable(new FJsonObject());
			AssetObject->SetStringField(TEXT("name"), Data.AssetName.ToString());
			AssetObject->SetStringField(TEXT("path"), Data.GetObjectPathString());
			AssetObject->SetStringField(TEXT("class"), Data.AssetClassPath.GetAssetName().ToString());
			AssetJsonArray.Add(MakeShareable(new FJsonValueObject(AssetObject)));
		}

		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
		FJsonSerializer::Serialize(AssetJsonArray, Writer);
	}

	void HandleFindBlueprintsByParent(const FString& ParentClassName, const FString& SearchPath, FString& OutJsonString, FString& OutError)
	{
		if (ParentClassName.IsEmpty())
		{
			OutError = TEXT("parent_class parameter is required");
			return;
		}

		FString Path = SearchPath.IsEmpty() ? TEXT("/Game") : SearchPath;

		FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		IAssetRegistry& AR = ARM.Get();

		FARFilter Filter;
		Filter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());
		Filter.PackagePaths.Add(FName(*Path));
		Filter.bRecursivePaths = true;
		Filter.bRecursiveClasses = true;

		TArray<FAssetData> Assets;
		AR.GetAssets(Filter, Assets);

		static const FName TagParentClass(TEXT("ParentClass"));
		static const FName TagNativeParentClass(TEXT("NativeParentClass"));

		TArray<TSharedPtr<FJsonValue>> Results;
		for (const FAssetData& D : Assets)
		{
			FString ParentPath;
			bool bMatchParent = D.GetTagValue(TagParentClass, ParentPath)
				&& ParentPath.Contains(ParentClassName, ESearchCase::IgnoreCase);

			if (!bMatchParent)
			{
				FString NativeParent;
				bMatchParent = D.GetTagValue(TagNativeParentClass, NativeParent)
					&& NativeParent.Contains(ParentClassName, ESearchCase::IgnoreCase);
				if (bMatchParent) ParentPath = NativeParent;
			}

			if (!bMatchParent) continue;

			TSharedPtr<FJsonObject> Entry = MakeShareable(new FJsonObject);
			Entry->SetStringField(TEXT("name"), D.AssetName.ToString());
			Entry->SetStringField(TEXT("path"), D.GetObjectPathString());
			Entry->SetStringField(TEXT("parent_class"), ParentPath);
			Results.Add(MakeShareable(new FJsonValueObject(Entry)));

			if (Results.Num() >= 50) break;
		}

		TSharedPtr<FJsonObject> Root = MakeShareable(new FJsonObject);
		Root->SetBoolField(TEXT("success"), true);
		Root->SetArrayField(TEXT("blueprints"), Results);
		Root->SetNumberField(TEXT("count"), Results.Num());
		Root->SetStringField(TEXT("parent_class_filter"), ParentClassName);
		Root->SetStringField(TEXT("search_path"), Path);

		TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJsonString);
		FJsonSerializer::Serialize(Root.ToSharedRef(), W);
	}

	void HandleGetCurrentFolderFromArgs(const TSharedPtr<FJsonObject>& , FString& OutJsonString, FString& OutError)
	{
		FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
		IContentBrowserSingleton& ContentBrowserSingleton = ContentBrowserModule.Get();

		FString CurrentPath;
		FContentBrowserItemPath CurrentPathItem = ContentBrowserSingleton.GetCurrentPath();

		if (CurrentPathItem.HasInternalPath())
		{
			CurrentPath = CurrentPathItem.GetInternalPathName().ToString();
		}

		if (CurrentPath.IsEmpty())
		{
			TArray<FString> SelectedFolders;
			ContentBrowserSingleton.GetSelectedPathViewFolders(SelectedFolders);
			if (SelectedFolders.Num() > 0)
			{
				CurrentPath = SelectedFolders[0];
			}
		}

		TSharedPtr<FJsonObject> ResultObject = MakeShareable(new FJsonObject);
		if (CurrentPath.IsEmpty())
		{
			CurrentPath = TEXT("/Game/");
			ResultObject->SetStringField(TEXT("folder_path"), CurrentPath);
			ResultObject->SetBoolField(TEXT("is_default"), true);
			ResultObject->SetStringField(TEXT("note"), TEXT("Could not determine current folder. Using /Game/ as default. Please click on a folder in the Content Browser."));
		}
		else
		{
			ResultObject->SetStringField(TEXT("folder_path"), CurrentPath);
			ResultObject->SetBoolField(TEXT("is_default"), false);
		}

		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
		FJsonSerializer::Serialize(ResultObject.ToSharedRef(), Writer);
		OutError.Reset();
	}

	void HandleListAssetsInFolderFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
	{
		if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }

		FString FolderPath;
		if (!Args->TryGetStringField(TEXT("folder_path"), FolderPath))
			if (!Args->TryGetStringField(TEXT("asset_path"), FolderPath))
				Args->TryGetStringField(TEXT("path"), FolderPath);

		if (FolderPath.IsEmpty())
		{
			FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
			IContentBrowserSingleton& ContentBrowserSingleton = ContentBrowserModule.Get();
			FContentBrowserItemPath CurrentPathItem = ContentBrowserSingleton.GetCurrentPath();
			if (CurrentPathItem.HasInternalPath())
			{
				FolderPath = CurrentPathItem.GetInternalPathString();
			}
			if (FolderPath.IsEmpty())
			{
				TArray<FString> SelectedFolders;
				ContentBrowserSingleton.GetSelectedPathViewFolders(SelectedFolders);
				if (SelectedFolders.Num() > 0) FolderPath = SelectedFolders[0];
			}
			if (FolderPath.IsEmpty())
			{
				OutError = TEXT("No folder_path provided and could not determine current folder. Please specify a folder_path or click on a folder in the Content Browser.");
				return;
			}
		}

		FString AssetTypeFilter;
		if (!Args->TryGetStringField(TEXT("asset_type"), AssetTypeFilter))
			Args->TryGetStringField(TEXT("class_filter"), AssetTypeFilter);
		FString NamePattern;
		Args->TryGetStringField(TEXT("name_pattern"), NamePattern);

		if (!FolderPath.StartsWith(TEXT("/")) && !FolderPath.Contains(TEXT(":")))
		{
			FolderPath = TEXT("/") + FolderPath;
		}

		FString FolderRerootNote;
		if (!UECPMountResolver::IsValidMountedPath(FolderPath))
		{
			FString Tail = FolderPath;
			while (Tail.StartsWith(TEXT("/"))) Tail = Tail.RightChop(1);
			const FString ReRooted = TEXT("/Game/") + Tail;
			FolderRerootNote = FString::Printf(
				TEXT("'%s' is not a mounted content root (valid roots: /Game, /Engine, plugin mounts) — resolved to '%s'. Use /Game/... paths."),
				*FolderPath, *ReRooted);
			FolderPath = ReRooted;
		}

		bool bRecursive = false;
		Args->TryGetBoolField(TEXT("recursive"), bRecursive);

		constexpr int32 MaxLimit = 5000;
		int32 Limit = 500;
		GConfig->GetInt(TEXT("BpGeneratorUltimate"), TEXT("ListAssetsLimit"),
			Limit, FSettingsManager::GetGlobalConfigPath());
		if (Limit < 50) Limit = 500;
		double LimitDouble = 0.0;
		if (Args->TryGetNumberField(TEXT("limit"), LimitDouble))
			Limit = FMath::Clamp((int32)LimitDouble, 1, MaxLimit);
		int32 Offset = 0;
		double OffsetDouble = 0.0;
		if (Args->TryGetNumberField(TEXT("offset"), OffsetDouble))
			Offset = FMath::Max(0, (int32)OffsetDouble);

		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		TArray<FAssetData> AssetData;
		AssetRegistryModule.Get().GetAssetsByPath(FName(*FolderPath), AssetData, bRecursive);

		TArray<const FAssetData*> Matched;
		Matched.Reserve(AssetData.Num());
		for (const FAssetData& Data : AssetData)
		{
			if (!AssetTypeFilter.IsEmpty())
			{
				FString AssetClass = Data.AssetClassPath.GetAssetName().ToString();
				if (!AssetClass.Contains(AssetTypeFilter, ESearchCase::IgnoreCase)) continue;
			}
			if (!NamePattern.IsEmpty() && !Data.AssetName.ToString().Contains(NamePattern, ESearchCase::IgnoreCase)) continue;
			Matched.Add(&Data);
		}

		const int32 TotalMatched = Matched.Num();
		const int32 SliceStart   = FMath::Min(Offset, TotalMatched);
		const int32 SliceEnd     = FMath::Min(SliceStart + Limit, TotalMatched);
		const bool bTruncated    = SliceEnd < TotalMatched;

		TArray<TSharedPtr<FJsonValue>> AssetJsonArray;
		AssetJsonArray.Reserve(SliceEnd - SliceStart);
		for (int32 i = SliceStart; i < SliceEnd; i++)
		{
			const FAssetData& Data = *Matched[i];
			TSharedPtr<FJsonObject> AssetObject = MakeShareable(new FJsonObject);
			AssetObject->SetStringField(TEXT("name"), Data.AssetName.ToString());
			AssetObject->SetStringField(TEXT("path"), Data.GetObjectPathString());
			AssetObject->SetStringField(TEXT("class"), Data.AssetClassPath.GetAssetName().ToString());
			AssetJsonArray.Add(MakeShareable(new FJsonValueObject(AssetObject)));
		}

		TSharedPtr<FJsonObject> ResultObject = MakeShareable(new FJsonObject);
		ResultObject->SetStringField(TEXT("folder_path"), FolderPath);
		if (!FolderRerootNote.IsEmpty()) ResultObject->SetStringField(TEXT("note"), FolderRerootNote);
		ResultObject->SetArrayField(TEXT("assets"), AssetJsonArray);
		ResultObject->SetNumberField(TEXT("count"), AssetJsonArray.Num());
		ResultObject->SetNumberField(TEXT("total"),  TotalMatched);
		ResultObject->SetNumberField(TEXT("offset"), SliceStart);
		ResultObject->SetNumberField(TEXT("limit"),  Limit);
		ResultObject->SetBoolField  (TEXT("truncated"), bTruncated);
		if (bTruncated)
		{
			ResultObject->SetStringField(TEXT("hint"),
				FString::Printf(TEXT("Showing %d/%d matches. Narrow with class_filter / name_pattern, or page with offset=%d."),
					AssetJsonArray.Num(), TotalMatched, SliceEnd));
		}

		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
		FJsonSerializer::Serialize(ResultObject.ToSharedRef(), Writer);
	}

	void HandleGetSelectedAssetsFromArgs(const TSharedPtr<FJsonObject>& , FString& OutJsonString, FString& OutError)
	{
		FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
		IContentBrowserSingleton& ContentBrowserSingleton = ContentBrowserModule.Get();

		TArray<FAssetData> SelectedAssets;
		ContentBrowserSingleton.GetSelectedAssets(SelectedAssets);

		TSharedPtr<FJsonObject> ResultObject = MakeShareable(new FJsonObject);
		if (SelectedAssets.Num() == 0)
		{
			OutError = TEXT("No assets are currently selected in the Content Browser.");
			ResultObject->SetBoolField(TEXT("success"), false);
			ResultObject->SetStringField(TEXT("error"), OutError);
			TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
			FJsonSerializer::Serialize(ResultObject.ToSharedRef(), Writer);
			return;
		}

		TArray<TSharedPtr<FJsonValue>> AssetsArray;
		for (const FAssetData& Data : SelectedAssets)
		{
			TSharedPtr<FJsonObject> AssetObj = MakeShareable(new FJsonObject);
			AssetObj->SetStringField(TEXT("name"), Data.AssetName.ToString());
			AssetObj->SetStringField(TEXT("path"), Data.GetObjectPathString());
			AssetObj->SetStringField(TEXT("class"), Data.AssetClassPath.GetAssetName().ToString());
			AssetsArray.Add(MakeShareable(new FJsonValueObject(AssetObj)));
		}

		ResultObject->SetBoolField(TEXT("success"), true);
		ResultObject->SetArrayField(TEXT("assets"), AssetsArray);
		ResultObject->SetNumberField(TEXT("count"), AssetsArray.Num());
		ResultObject->SetStringField(TEXT("message"), FString::Printf(TEXT("Found %d selected asset(s) in Content Browser"), AssetsArray.Num()));

		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
		FJsonSerializer::Serialize(ResultObject.ToSharedRef(), Writer);
	}

	void HandleFindBlueprintsByParentFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
	{
		if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
		FString ParentClass, SearchPath;
		if (!Args->TryGetStringField(TEXT("parent_class"), ParentClass) || ParentClass.IsEmpty())
		{
			OutError = TEXT("Missing required parameter: parent_class");
			return;
		}
		Args->TryGetStringField(TEXT("search_path"), SearchPath);
		HandleFindBlueprintsByParent(ParentClass, SearchPath, OutJsonString, OutError);
	}

	void HandleGetFocusedContentBrowserPathFromArgs(const TSharedPtr<FJsonObject>& , FString& OutJsonString, FString& OutError)
	{
		FString OutPath;
		HandleGetFocusedContentBrowserPath(OutPath, OutError);
		if (OutError.IsEmpty())
			OutJsonString = FString::Printf(TEXT("{\"success\":true,\"path\":\"%s\"}"), *OutPath);
	}

	void HandleGetSelectedContentBrowserAssetsFromArgs(const TSharedPtr<FJsonObject>& , FString& OutJsonString, FString& OutError)
	{
		FString AssetsJson;
		HandleGetSelectedContentBrowserAssets(AssetsJson, OutError);
		if (OutError.IsEmpty())
			OutJsonString = FString::Printf(TEXT("{\"success\":true,\"assets\":%s}"), *AssetsJson);
	}

	void HandleGetSelectedBlueprintPathFromArgs(const TSharedPtr<FJsonObject>& , FString& OutJsonString, FString& )
	{
		FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
		TArray<FAssetData> SelectedAssets;
		ContentBrowserModule.Get().GetSelectedAssets(SelectedAssets);
		const FString AssetPath = SelectedAssets.Num() > 0
			? SelectedAssets[0].GetObjectPathString() : FString();
		OutJsonString = FString::Printf(TEXT("{\"success\":true,\"path\":\"%s\"}"), *AssetPath);
	}

	void HandleListExtensionToolsFromArgs(const TSharedPtr<FJsonObject>& , FString& OutJsonString, FString& )
	{
		TArray<TSharedPtr<FJsonValue>> UmbrellaArr;
		if (IUECPCoreModule::IsAvailable())
		{
			IUECPExtensionService& ExtSvc = IUECPCoreModule::Get().GetExtensionService();
			for (const FUECPExtensionDescriptor& D : ExtSvc.GetExtensions())
			{
				if (ExtSvc.GetExtensionState(D.ExtensionId) != EUECPExtensionState::Loaded) continue;
				if (D.McpServer.IsSet()) continue;
				for (const FName& Umbrella : D.OwnedUmbrellas)
				{
					TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
					O->SetStringField(TEXT("name"),         Umbrella.ToString());
					O->SetStringField(TEXT("description"),  D.Description.ToString());
					O->SetStringField(TEXT("extension_id"), D.ExtensionId.ToString());
					TArray<TSharedPtr<FJsonValue>> ToolArr;
					for (const FName& Tool : D.OwnedTools)
					{
						ToolArr.Add(MakeShared<FJsonValueString>(Tool.ToString()));
					}
					O->SetArrayField(TEXT("tools"), ToolArr);
					UmbrellaArr.Add(MakeShared<FJsonValueObject>(O));
				}
			}
		}
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), true);
		Result->SetArrayField(TEXT("umbrellas"), UmbrellaArr);
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
		FJsonSerializer::Serialize(Result, Writer);
	}
}
