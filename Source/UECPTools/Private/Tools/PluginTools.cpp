// Copyright 2026, BlueprintsLab, All rights reserved

#include "Tools/PluginTools.h"
#include "Interfaces/IPluginManager.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"

namespace PluginTools
{
	void HandleListPlugins(bool bIncludeEngine, bool bIncludeDirectory, FString& OutJsonString, FString& OutError)
	{
		TSharedPtr<FJsonObject> ResultObject = MakeShareable(new FJsonObject);
		TArray<TSharedPtr<FJsonValue>> PluginsArray;

		IPluginManager& PluginManager = IPluginManager::Get();
		const TArray<TSharedRef<IPlugin>>& Plugins = PluginManager.GetEnabledPlugins();

		int32 EngineSkipped = 0;
		for (const TSharedRef<IPlugin>& Plugin : Plugins)
		{
			FString PluginType = TEXT("External");
			const FString BaseDir = Plugin->GetBaseDir();

			if (BaseDir.Contains(FPaths::EnginePluginsDir()))
			{
				PluginType = TEXT("Engine");
				if (!bIncludeEngine) { ++EngineSkipped; continue; }
			}
			else if (BaseDir.Contains(FPaths::ProjectPluginsDir()))
			{
				PluginType = TEXT("Project");
			}

			TSharedPtr<FJsonObject> PluginObj = MakeShareable(new FJsonObject);
			PluginObj->SetStringField(TEXT("name"), Plugin->GetName());
			PluginObj->SetStringField(TEXT("friendly_name"), Plugin->GetFriendlyName());
			PluginObj->SetStringField(TEXT("version"), Plugin->GetDescriptor().VersionName);
			PluginObj->SetStringField(TEXT("type"), PluginType);
			if (bIncludeDirectory)
			{
				PluginObj->SetStringField(TEXT("directory"), BaseDir);
			}

			PluginsArray.Add(MakeShareable(new FJsonValueObject(PluginObj)));
		}

		ResultObject->SetArrayField(TEXT("plugins"), PluginsArray);
		ResultObject->SetNumberField(TEXT("count"), PluginsArray.Num());
		if (EngineSkipped > 0)
		{
			ResultObject->SetNumberField(TEXT("engine_plugins_omitted"), EngineSkipped);
			ResultObject->SetStringField(TEXT("hint"),
				TEXT("Engine plugins omitted by default. Pass include_engine=true to include them, or use find_plugin with a search_pattern to query a specific engine plugin."));
		}
		ResultObject->SetBoolField(TEXT("success"), true);

		FString ResultString;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResultString);
		FJsonSerializer::Serialize(ResultObject.ToSharedRef(), Writer);
		OutJsonString = ResultString;
	}

	void HandleFindPlugin(const FString& SearchPattern, FString& OutJsonString, FString& OutError)
	{
		TSharedPtr<FJsonObject> ResultObject = MakeShareable(new FJsonObject);
		TArray<TSharedPtr<FJsonValue>> FoundPluginsArray;

		IPluginManager& PluginManager = IPluginManager::Get();
		const TArray<TSharedRef<IPlugin>> Plugins = PluginManager.GetDiscoveredPlugins();

		const FString LowerSearchPattern = SearchPattern.ToLower().Replace(TEXT(" "), TEXT(""));

		for (const TSharedRef<IPlugin>& Plugin : Plugins)
		{
			const FString PluginNameLower = Plugin->GetName().ToLower();
			const FString FriendlyNameLower = Plugin->GetFriendlyName().ToLower();

			if (PluginNameLower.Contains(LowerSearchPattern) || FriendlyNameLower.Contains(LowerSearchPattern))
			{
				FString BaseDir = Plugin->GetBaseDir();

				if (FPaths::IsRelative(BaseDir))
				{
					const FString EngineDirAbs = FPaths::ConvertRelativePathToFull(FPaths::EngineDir());
					const FString CombinedPath = FPaths::Combine(EngineDirAbs, BaseDir);
					BaseDir = FPaths::ConvertRelativePathToFull(CombinedPath);
				}

				if (IFileManager::Get().DirectoryExists(*BaseDir))
				{
					FString PluginType = TEXT("External");
					if (BaseDir.Contains(FPaths::EnginePluginsDir())) PluginType = TEXT("Engine");
					else if (BaseDir.Contains(FPaths::ProjectPluginsDir())) PluginType = TEXT("Project");

					TSharedPtr<FJsonObject> PluginObj = MakeShareable(new FJsonObject);
					PluginObj->SetStringField(TEXT("name"), Plugin->GetName());
					PluginObj->SetStringField(TEXT("friendly_name"), Plugin->GetFriendlyName());
					PluginObj->SetStringField(TEXT("version"), Plugin->GetDescriptor().VersionName);
					PluginObj->SetStringField(TEXT("type"), PluginType);
					PluginObj->SetBoolField  (TEXT("enabled"), Plugin->IsEnabled());
					PluginObj->SetStringField(TEXT("directory"), BaseDir);

					FoundPluginsArray.Add(MakeShareable(new FJsonValueObject(PluginObj)));
				}
			}
		}

		ResultObject->SetArrayField(TEXT("plugins"), FoundPluginsArray);
		ResultObject->SetNumberField(TEXT("count"), FoundPluginsArray.Num());
		ResultObject->SetBoolField(TEXT("success"), true);

		FString ResultString;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResultString);
		FJsonSerializer::Serialize(ResultObject.ToSharedRef(), Writer);
		OutJsonString = ResultString;
	}

	void HandleListPluginFiles(const FString& PluginName, const FString& RelativePath, FString& OutJsonString, FString& OutError)
	{
		TSharedPtr<FJsonObject> ResultObject = MakeShareable(new FJsonObject);
		IPluginManager& PluginManager = IPluginManager::Get();
		TSharedPtr<IPlugin> FoundPlugin = nullptr;

		const TArray<TSharedRef<IPlugin>>& Plugins = PluginManager.GetEnabledPlugins();
		for (const TSharedRef<IPlugin>& Plugin : Plugins)
		{
			if (Plugin->GetName().ToLower() == PluginName.ToLower())
			{
				FoundPlugin = Plugin;
				break;
			}
			if (Plugin->GetFriendlyName().ToLower() == PluginName.ToLower())
			{
				FoundPlugin = Plugin;
				break;
			}
		}

		if (!FoundPlugin.IsValid())
		{
			OutError = FString::Printf(TEXT("Plugin '%s' not found"), *PluginName);
			ResultObject->SetBoolField(TEXT("success"), false);
			ResultObject->SetStringField(TEXT("error"), OutError);

			FString ResultString;
			TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResultString);
			FJsonSerializer::Serialize(ResultObject.ToSharedRef(), Writer);
			OutJsonString = ResultString;
			return;
		}

		FString PluginBaseDir = FoundPlugin->GetBaseDir();
		if (FPaths::IsRelative(PluginBaseDir))
		{
			FString EngineDirAbs = FPaths::ConvertRelativePathToFull(FPaths::EngineDir());
			FString CombinedPath = FPaths::Combine(EngineDirAbs, PluginBaseDir);
			PluginBaseDir = FPaths::ConvertRelativePathToFull(CombinedPath);
		}

		FString SearchPath = RelativePath.IsEmpty() ? PluginBaseDir : FPaths::Combine(PluginBaseDir, RelativePath);

		if (!FPaths::DirectoryExists(*SearchPath))
		{
			OutError = FString::Printf(TEXT("Directory not found: %s"), *SearchPath);
			ResultObject->SetBoolField(TEXT("success"), false);
			ResultObject->SetStringField(TEXT("error"), OutError);

			FString ResultString;
			TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResultString);
			FJsonSerializer::Serialize(ResultObject.ToSharedRef(), Writer);
			OutJsonString = ResultString;
			return;
		}

		TArray<TSharedPtr<FJsonValue>> FilesArray;
		TArray<FString> AllItems;
		IFileManager::Get().FindFiles(AllItems, *SearchPath, TEXT("*"));

		for (const FString& Item : AllItems)
		{
			if (Item == TEXT(".") || Item == TEXT(".."))
				continue;

			FString ItemPath = FPaths::Combine(SearchPath, Item);
			bool bIsDirectory = IFileManager::Get().DirectoryExists(*ItemPath);

			TSharedPtr<FJsonObject> FileObj = MakeShareable(new FJsonObject);
			FileObj->SetStringField(TEXT("name"), Item);
			FileObj->SetStringField(TEXT("path"), ItemPath);
			FileObj->SetBoolField(TEXT("is_directory"), bIsDirectory);

			if (!bIsDirectory)
			{
				FileObj->SetStringField(TEXT("extension"), FPaths::GetExtension(Item).ToLower());
				int64 FileSize = IFileManager::Get().FileSize(*ItemPath);
				FileObj->SetNumberField(TEXT("size_bytes"), FileSize);
			}

			FilesArray.Add(MakeShareable(new FJsonValueObject(FileObj)));
		}

		ResultObject->SetArrayField(TEXT("files"), FilesArray);
		ResultObject->SetNumberField(TEXT("count"), FilesArray.Num());
		ResultObject->SetStringField(TEXT("directory"), SearchPath);
		ResultObject->SetStringField(TEXT("plugin_name"), PluginName);
		ResultObject->SetBoolField(TEXT("success"), true);

		FString ResultString;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResultString);
		FJsonSerializer::Serialize(ResultObject.ToSharedRef(), Writer);
		OutJsonString = ResultString;
	}

	void HandleListPluginsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
	{
		bool bIncludeEngine = false;
		bool bIncludeDirectory = false;
		if (Args.IsValid())
		{
			Args->TryGetBoolField(TEXT("include_engine"), bIncludeEngine);
			Args->TryGetBoolField(TEXT("include_directory"), bIncludeDirectory);
		}
		HandleListPlugins(bIncludeEngine, bIncludeDirectory, OutJsonString, OutError);
	}

	void HandleFindPluginFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
	{
		FString SearchPattern;
		if (!Args.IsValid() || !Args->TryGetStringField(TEXT("search_pattern"), SearchPattern) || SearchPattern.IsEmpty())
		{
			OutError = TEXT("Missing required parameter: search_pattern");
			OutJsonString = TEXT("{\"success\":false,\"error\":\"Missing required parameter: search_pattern\"}");
			return;
		}
		HandleFindPlugin(SearchPattern, OutJsonString, OutError);
	}

	void HandleListPluginFilesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
	{
		FString PluginName, RelativePath;
		if (Args.IsValid())
		{
			Args->TryGetStringField(TEXT("plugin_name"), PluginName);
			Args->TryGetStringField(TEXT("relative_path"), RelativePath);
		}
		if (PluginName.IsEmpty())
		{
			OutError = TEXT("Missing required parameter: plugin_name");
			OutJsonString = TEXT("{\"success\":false,\"error\":\"Missing required parameter: plugin_name\"}");
			return;
		}
		HandleListPluginFiles(PluginName, RelativePath, OutJsonString, OutError);
	}
}
