// Copyright 2026, BlueprintsLab, All rights reserved

#include "Tools/CppPluginTools.h"

#include "UECPCppExtModule.h"

#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/App.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace CppPluginTools
{

namespace
{

	void WriteJsonError(const FString& Msg, FString& OutJsonString, FString& OutError)
	{
		OutError = Msg;
		TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
		R->SetBoolField  (TEXT("success"), false);
		R->SetStringField(TEXT("error"),   Msg);
		TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJsonString);
		FJsonSerializer::Serialize(R.ToSharedRef(), W);
	}

	bool IsValidIdentifier(const FString& Name)
	{
		if (Name.IsEmpty()) return false;
		const TCHAR First = Name[0];
		if (!FChar::IsAlpha(First) && First != TEXT('_')) return false;
		for (int32 i = 1; i < Name.Len(); ++i)
		{
			const TCHAR C = Name[i];
			if (!FChar::IsAlnum(C) && C != TEXT('_')) return false;
		}
		return true;
	}

	bool IsKnownModuleType(const FString& T)
	{
		static const TSet<FString> Known = {
			TEXT("Runtime"), TEXT("RuntimeNoCommandlet"), TEXT("RuntimeAndProgram"),
			TEXT("CookedOnly"), TEXT("UncookedOnly"),
			TEXT("Developer"), TEXT("DeveloperTool"),
			TEXT("Editor"), TEXT("EditorNoCommandlet"), TEXT("EditorAndProgram"),
			TEXT("Program"), TEXT("ServerOnly"), TEXT("ClientOnly"), TEXT("ClientOnlyNoCommandlet")
		};
		return Known.Contains(T);
	}

	bool IsKnownLoadingPhase(const FString& P)
	{
		static const TSet<FString> Known = {
			TEXT("EarliestPossible"), TEXT("PostConfigInit"), TEXT("PostSplashScreen"),
			TEXT("PreEarlyLoadingScreen"), TEXT("PreLoadingScreen"),
			TEXT("PreDefault"), TEXT("Default"), TEXT("PostDefault"),
			TEXT("PostEngineInit"), TEXT("None")
		};
		return Known.Contains(P);
	}

	void GetStringArray(const TSharedPtr<FJsonObject>& Args, const TCHAR* Key, TArray<FString>& Out)
	{
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (!Args->TryGetArrayField(Key, Arr) || !Arr) return;
		for (const TSharedPtr<FJsonValue>& V : *Arr)
		{
			if (!V.IsValid()) continue;
			const FString S = V->AsString();
			if (!S.IsEmpty()) Out.Add(S);
		}
	}

	FString JoinQuoted(const TArray<FString>& Items)
	{
		FString Out;
		for (int32 i = 0; i < Items.Num(); ++i)
		{
			Out += FString::Printf(TEXT("\"%s\""), *Items[i]);
			if (i + 1 < Items.Num()) Out += TEXT(", ");
		}
		return Out;
	}

	bool WriteTextFile(const FString& Path, const FString& Contents, FString& OutError)
	{
		IFileManager& FM = IFileManager::Get();
		FM.MakeDirectory(*FPaths::GetPath(Path),  true);
		if (!FFileHelper::SaveStringToFile(Contents, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			OutError = FString::Printf(TEXT("Failed to write %s"), *Path);
			return false;
		}
		return true;
	}

	FString BuildUPluginJson(
		const FString& FriendlyName, const FString& Description, const FString& Category,
		const FString& Author, const FString& VersionName, bool bCanContainContent,
		const FString& ModuleName, const FString& ModuleType, const FString& LoadingPhase,
		const TArray<FString>& PluginDependencies)
	{
		TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetNumberField(TEXT("FileVersion"), 3);
		Root->SetNumberField(TEXT("Version"), 1);
		Root->SetStringField(TEXT("VersionName"), VersionName);
		Root->SetStringField(TEXT("FriendlyName"), FriendlyName);
		Root->SetStringField(TEXT("Description"), Description);
		Root->SetStringField(TEXT("Category"), Category);
		Root->SetStringField(TEXT("CreatedBy"), Author);
		Root->SetStringField(TEXT("CreatedByURL"), TEXT(""));
		Root->SetStringField(TEXT("DocsURL"), TEXT(""));
		Root->SetStringField(TEXT("MarketplaceURL"), TEXT(""));
		Root->SetStringField(TEXT("SupportURL"), TEXT(""));
		Root->SetBoolField(TEXT("CanContainContent"), bCanContainContent);
		Root->SetBoolField(TEXT("IsBetaVersion"), false);
		Root->SetBoolField(TEXT("IsExperimentalVersion"), false);
		Root->SetBoolField(TEXT("Installed"), false);

		TSharedRef<FJsonObject> Module = MakeShared<FJsonObject>();
		Module->SetStringField(TEXT("Name"), ModuleName);
		Module->SetStringField(TEXT("Type"), ModuleType);
		Module->SetStringField(TEXT("LoadingPhase"), LoadingPhase);

		TArray<TSharedPtr<FJsonValue>> Modules;
		Modules.Add(MakeShared<FJsonValueObject>(Module));
		Root->SetArrayField(TEXT("Modules"), Modules);

		if (PluginDependencies.Num() > 0)
		{
			TArray<TSharedPtr<FJsonValue>> Deps;
			for (const FString& Dep : PluginDependencies)
			{
				TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
				Entry->SetStringField(TEXT("Name"), Dep);
				Entry->SetBoolField  (TEXT("Enabled"), true);
				Deps.Add(MakeShared<FJsonValueObject>(Entry));
			}
			Root->SetArrayField(TEXT("Plugins"), Deps);
		}

		FString Out;
		TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> W = TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Out);
		FJsonSerializer::Serialize(Root, W);
		return Out;
	}

	FString BuildBuildCs(const FString& ModuleName,
		const TArray<FString>& PublicDeps, const TArray<FString>& PrivateDeps)
	{
		TArray<FString> Pub = PublicDeps;
		if (Pub.Num() == 0)
		{
			Pub = { TEXT("Core"), TEXT("CoreUObject"), TEXT("Engine") };
		}

		FString Out;
		Out += TEXT("// Copyright 2026, All Rights Reserved\n\n");
		Out += TEXT("using UnrealBuildTool;\n\n");
		Out += FString::Printf(TEXT("public class %s : ModuleRules\n{\n"), *ModuleName);
		Out += FString::Printf(TEXT("\tpublic %s(ReadOnlyTargetRules Target) : base(Target)\n\t{\n"), *ModuleName);
		Out += TEXT("\t\tPCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;\n\n");
		Out += FString::Printf(TEXT("\t\tPublicDependencyModuleNames.AddRange(new string[] { %s });\n"), *JoinQuoted(Pub));
		if (PrivateDeps.Num() > 0)
		{
			Out += FString::Printf(TEXT("\t\tPrivateDependencyModuleNames.AddRange(new string[] { %s });\n"), *JoinQuoted(PrivateDeps));
		}
		else
		{
			Out += TEXT("\t\tPrivateDependencyModuleNames.AddRange(new string[] { });\n");
		}
		Out += TEXT("\t}\n}\n");
		return Out;
	}

	FString BuildModuleHeader(const FString& ModuleName)
	{
		FString Out;
		Out += TEXT("// Copyright 2026, All Rights Reserved\n\n");
		Out += TEXT("#pragma once\n\n");
		Out += TEXT("#include \"CoreMinimal.h\"\n");
		Out += TEXT("#include \"Modules/ModuleManager.h\"\n\n");
		Out += FString::Printf(TEXT("class F%sModule : public IModuleInterface\n{\npublic:\n"), *ModuleName);
		Out += TEXT("\tvirtual void StartupModule() override;\n");
		Out += TEXT("\tvirtual void ShutdownModule() override;\n");
		Out += TEXT("};\n");
		return Out;
	}

	FString BuildModuleSource(const FString& ModuleName)
	{
		FString Out;
		Out += TEXT("// Copyright 2026, All Rights Reserved\n\n");
		Out += FString::Printf(TEXT("#include \"%sModule.h\"\n\n"), *ModuleName);
		Out += FString::Printf(TEXT("#define LOCTEXT_NAMESPACE \"F%sModule\"\n\n"), *ModuleName);
		Out += FString::Printf(TEXT("void F%sModule::StartupModule()\n{\n}\n\n"), *ModuleName);
		Out += FString::Printf(TEXT("void F%sModule::ShutdownModule()\n{\n}\n\n"), *ModuleName);
		Out += TEXT("#undef LOCTEXT_NAMESPACE\n\n");
		Out += FString::Printf(TEXT("IMPLEMENT_MODULE(F%sModule, %s)\n"), *ModuleName, *ModuleName);
		return Out;
	}

	bool EnsureUProjectEntry(const FString& PluginName, FString& OutError)
	{
		const FString UProjectPath = FPaths::GetProjectFilePath();
		FString Contents;
		if (!FFileHelper::LoadFileToString(Contents, *UProjectPath))
		{
			OutError = FString::Printf(TEXT("Could not read .uproject at %s"), *UProjectPath);
			return false;
		}

		TSharedPtr<FJsonObject> Root;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Contents);
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
		{
			OutError = TEXT(".uproject is not valid JSON");
			return false;
		}

		TArray<TSharedPtr<FJsonValue>> Plugins;
		const TArray<TSharedPtr<FJsonValue>>* ExistingArr = nullptr;
		if (Root->TryGetArrayField(TEXT("Plugins"), ExistingArr) && ExistingArr)
		{
			Plugins = *ExistingArr;
		}

		bool bFoundExisting = false;
		for (const TSharedPtr<FJsonValue>& V : Plugins)
		{
			const TSharedPtr<FJsonObject>* AsObj = nullptr;
			if (!V.IsValid() || !V->TryGetObject(AsObj) || !AsObj || !AsObj->IsValid()) continue;
			FString Existing;
			if ((*AsObj)->TryGetStringField(TEXT("Name"), Existing) && Existing == PluginName)
			{
				(*AsObj)->SetBoolField(TEXT("Enabled"), true);
				bFoundExisting = true;
				break;
			}
		}
		if (!bFoundExisting)
		{
			TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("Name"), PluginName);
			Entry->SetBoolField(TEXT("Enabled"), true);
			Plugins.Add(MakeShared<FJsonValueObject>(Entry));
		}
		Root->SetArrayField(TEXT("Plugins"), Plugins);

		FString NewContents;
		TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&NewContents);
		FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);

		if (!FFileHelper::SaveStringToFile(NewContents, *UProjectPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			OutError = TEXT("Failed to write updated .uproject");
			return false;
		}
		return true;
	}

	bool EmitModuleTree(const FString& PluginRoot, const FString& ModuleName,
		const TArray<FString>& PublicDeps, const TArray<FString>& PrivateDeps,
		TArray<FString>& OutCreated, FString& OutError)
	{
		const FString ModuleDir   = PluginRoot / TEXT("Source") / ModuleName;
		const FString PublicDir   = ModuleDir / TEXT("Public");
		const FString PrivateDir  = ModuleDir / TEXT("Private");
		const FString BuildCsPath = ModuleDir / (ModuleName + TEXT(".Build.cs"));
		const FString HeaderPath  = PublicDir / (ModuleName + TEXT("Module.h"));
		const FString SourcePath  = PrivateDir / (ModuleName + TEXT("Module.cpp"));

		if (!WriteTextFile(BuildCsPath, BuildBuildCs(ModuleName, PublicDeps, PrivateDeps), OutError)) return false;
		OutCreated.Add(BuildCsPath);
		if (!WriteTextFile(HeaderPath, BuildModuleHeader(ModuleName), OutError)) return false;
		OutCreated.Add(HeaderPath);
		if (!WriteTextFile(SourcePath, BuildModuleSource(ModuleName), OutError)) return false;
		OutCreated.Add(SourcePath);
		return true;
	}
}

void HandleCreatePluginFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { WriteJsonError(TEXT("Invalid args"), OutJsonString, OutError); return; }

	FString Name;
	if (!Args->TryGetStringField(TEXT("name"), Name) || Name.IsEmpty())
	{
		WriteJsonError(TEXT("Missing required arg: name"), OutJsonString, OutError);
		return;
	}
	if (!IsValidIdentifier(Name))
	{
		WriteJsonError(FString::Printf(TEXT("Invalid plugin name '%s' — must start with a letter/underscore and contain only letters, digits, and underscores"), *Name), OutJsonString, OutError);
		return;
	}

	FString ModuleType = TEXT("Editor");
	Args->TryGetStringField(TEXT("module_type"), ModuleType);
	if (!IsKnownModuleType(ModuleType))
	{
		WriteJsonError(FString::Printf(TEXT("Unknown module_type '%s'. Expected one of: Editor, Runtime, Developer, EditorNoCommandlet, …"), *ModuleType), OutJsonString, OutError);
		return;
	}

	FString LoadingPhase = TEXT("Default");
	Args->TryGetStringField(TEXT("loading_phase"), LoadingPhase);
	if (!IsKnownLoadingPhase(LoadingPhase))
	{
		WriteJsonError(FString::Printf(TEXT("Unknown loading_phase '%s'. Expected Default, PreDefault, PostDefault, PostEngineInit, …"), *LoadingPhase), OutJsonString, OutError);
		return;
	}

	FString FriendlyName = Name;       Args->TryGetStringField(TEXT("friendly_name"), FriendlyName);
	FString Description  = FString();  Args->TryGetStringField(TEXT("description"),   Description);
	FString Category     = TEXT("Other");  Args->TryGetStringField(TEXT("category"),  Category);
	FString Author       = FString();  Args->TryGetStringField(TEXT("author"),        Author);
	FString VersionName  = TEXT("1.0");Args->TryGetStringField(TEXT("version_name"),  VersionName);

	bool bCanContainContent = false;
	Args->TryGetBoolField(TEXT("can_contain_content"), bCanContainContent);
	bool bEnableInUProject = true;
	Args->TryGetBoolField(TEXT("enable_in_uproject"),  bEnableInUProject);

	TArray<FString> PublicDeps, PrivateDeps, PluginDeps;
	GetStringArray(Args, TEXT("public_dependencies"),  PublicDeps);
	GetStringArray(Args, TEXT("private_dependencies"), PrivateDeps);
	GetStringArray(Args, TEXT("plugin_dependencies"), PluginDeps);

	const FString PluginRoot = FPaths::ProjectPluginsDir() / Name;
	IFileManager& FM = IFileManager::Get();
	if (FM.DirectoryExists(*PluginRoot))
	{
		WriteJsonError(FString::Printf(TEXT("Plugin '%s' already exists at %s. Delete the directory first or pick a new name."), *Name, *PluginRoot), OutJsonString, OutError);
		return;
	}

	TArray<FString> Created;

	const FString UPluginPath = PluginRoot / (Name + TEXT(".uplugin"));
	const FString ManifestJson = BuildUPluginJson(FriendlyName, Description, Category, Author, VersionName,
		bCanContainContent, Name, ModuleType, LoadingPhase, PluginDeps);
	if (!WriteTextFile(UPluginPath, ManifestJson, OutError))
	{
		WriteJsonError(OutError, OutJsonString, OutError);
		return;
	}
	Created.Add(UPluginPath);

	if (!EmitModuleTree(PluginRoot, Name, PublicDeps, PrivateDeps, Created, OutError))
	{
		WriteJsonError(OutError, OutJsonString, OutError);
		return;
	}

	if (bCanContainContent)
	{
		const FString ContentDir = PluginRoot / TEXT("Content");
		FM.MakeDirectory(*ContentDir,  true);
	}

	bool bUProjectUpdated = false;
	if (bEnableInUProject)
	{
		FString UpErr;
		if (EnsureUProjectEntry(Name, UpErr))
		{
			bUProjectUpdated = true;
		}
		else
		{
			UE_LOG(LogUECPCppExt, Warning, TEXT("create_plugin: %s"), *UpErr);
		}
	}

	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetBoolField  (TEXT("success"),           true);
	R->SetStringField(TEXT("plugin_name"),       Name);
	R->SetStringField(TEXT("plugin_root"),       PluginRoot);
	R->SetStringField(TEXT("uplugin_path"),      UPluginPath);
	R->SetStringField(TEXT("module_name"),       Name);
	R->SetStringField(TEXT("module_type"),       ModuleType);
	R->SetStringField(TEXT("loading_phase"),     LoadingPhase);
	R->SetBoolField  (TEXT("can_contain_content"), bCanContainContent);
	R->SetBoolField  (TEXT("uproject_updated"),  bUProjectUpdated);

	TArray<TSharedPtr<FJsonValue>> FileArr;
	for (const FString& P : Created) FileArr.Add(MakeShared<FJsonValueString>(P));
	R->SetArrayField(TEXT("created_files"), FileArr);

	R->SetStringField(TEXT("next_step"),
		TEXT("Compile the project (compile_project mode='full' — the editor must be closed first). On next editor start the new module is loaded and reachable like any other (add_uproperty, create_actor, etc.)."));

	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(R.ToSharedRef(), W);
}

void HandleAddPluginModuleFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { WriteJsonError(TEXT("Invalid args"), OutJsonString, OutError); return; }

	FString PluginName;
	FString ModuleName;
	if (!Args->TryGetStringField(TEXT("plugin_name"), PluginName) || PluginName.IsEmpty()
	 || !Args->TryGetStringField(TEXT("module_name"), ModuleName) || ModuleName.IsEmpty())
	{
		WriteJsonError(TEXT("Missing required args: plugin_name and module_name"), OutJsonString, OutError);
		return;
	}
	if (!IsValidIdentifier(ModuleName))
	{
		WriteJsonError(FString::Printf(TEXT("Invalid module_name '%s' — must be a valid C++ identifier"), *ModuleName), OutJsonString, OutError);
		return;
	}

	FString ModuleType = TEXT("Editor");  Args->TryGetStringField(TEXT("module_type"),   ModuleType);
	FString LoadingPhase = TEXT("Default");Args->TryGetStringField(TEXT("loading_phase"), LoadingPhase);
	if (!IsKnownModuleType(ModuleType))
	{
		WriteJsonError(FString::Printf(TEXT("Unknown module_type '%s'"), *ModuleType), OutJsonString, OutError);
		return;
	}
	if (!IsKnownLoadingPhase(LoadingPhase))
	{
		WriteJsonError(FString::Printf(TEXT("Unknown loading_phase '%s'"), *LoadingPhase), OutJsonString, OutError);
		return;
	}

	TArray<FString> PublicDeps, PrivateDeps;
	GetStringArray(Args, TEXT("public_dependencies"),  PublicDeps);
	GetStringArray(Args, TEXT("private_dependencies"), PrivateDeps);

	const FString PluginRoot  = FPaths::ProjectPluginsDir() / PluginName;
	const FString UPluginPath = PluginRoot / (PluginName + TEXT(".uplugin"));
	IFileManager& FM = IFileManager::Get();
	if (!FM.FileExists(*UPluginPath))
	{
		WriteJsonError(FString::Printf(TEXT("Plugin '%s' not found at %s — create_plugin first."), *PluginName, *PluginRoot), OutJsonString, OutError);
		return;
	}
	const FString ModuleDir = PluginRoot / TEXT("Source") / ModuleName;
	if (FM.DirectoryExists(*ModuleDir))
	{
		WriteJsonError(FString::Printf(TEXT("Module '%s' already exists at %s"), *ModuleName, *ModuleDir), OutJsonString, OutError);
		return;
	}

	FString ManifestText;
	if (!FFileHelper::LoadFileToString(ManifestText, *UPluginPath))
	{
		WriteJsonError(TEXT("Could not read .uplugin"), OutJsonString, OutError);
		return;
	}
	TSharedPtr<FJsonObject> ManifestRoot;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ManifestText);
	if (!FJsonSerializer::Deserialize(Reader, ManifestRoot) || !ManifestRoot.IsValid())
	{
		WriteJsonError(TEXT(".uplugin is not valid JSON"), OutJsonString, OutError);
		return;
	}

	TArray<TSharedPtr<FJsonValue>> Modules;
	const TArray<TSharedPtr<FJsonValue>>* ExistingArr = nullptr;
	if (ManifestRoot->TryGetArrayField(TEXT("Modules"), ExistingArr) && ExistingArr)
	{
		Modules = *ExistingArr;
	}
	TSharedRef<FJsonObject> NewModule = MakeShared<FJsonObject>();
	NewModule->SetStringField(TEXT("Name"),         ModuleName);
	NewModule->SetStringField(TEXT("Type"),         ModuleType);
	NewModule->SetStringField(TEXT("LoadingPhase"), LoadingPhase);
	Modules.Add(MakeShared<FJsonValueObject>(NewModule));
	ManifestRoot->SetArrayField(TEXT("Modules"), Modules);

	FString NewManifest;
	TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&NewManifest);
	FJsonSerializer::Serialize(ManifestRoot.ToSharedRef(), Writer);
	if (!FFileHelper::SaveStringToFile(NewManifest, *UPluginPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		WriteJsonError(TEXT("Failed to write updated .uplugin"), OutJsonString, OutError);
		return;
	}

	TArray<FString> Created = { UPluginPath };
	if (!EmitModuleTree(PluginRoot, ModuleName, PublicDeps, PrivateDeps, Created, OutError))
	{
		WriteJsonError(OutError, OutJsonString, OutError);
		return;
	}

	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetBoolField  (TEXT("success"),       true);
	R->SetStringField(TEXT("plugin_name"),   PluginName);
	R->SetStringField(TEXT("module_name"),   ModuleName);
	R->SetStringField(TEXT("module_type"),   ModuleType);
	R->SetStringField(TEXT("loading_phase"), LoadingPhase);

	TArray<TSharedPtr<FJsonValue>> FileArr;
	for (const FString& P : Created) FileArr.Add(MakeShared<FJsonValueString>(P));
	R->SetArrayField(TEXT("created_files"), FileArr);
	R->SetStringField(TEXT("next_step"), TEXT("Compile the project (compile_project mode='full' — the editor must be closed first)."));

	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(R.ToSharedRef(), W);
}

namespace
{
	FString SnakeToPascal(const FString& In)
	{
		FString Out;
		bool bCapNext = true;
		for (int32 i = 0; i < In.Len(); ++i)
		{
			const TCHAR C = In[i];
			if (C == TEXT('_') || C == TEXT('-') || C == TEXT(' '))
			{
				bCapNext = true;
				continue;
			}
			Out.AppendChar(bCapNext ? FChar::ToUpper(C) : C);
			bCapNext = false;
		}
		return Out;
	}

	FString FindHostPluginRoot(const FString& HostPluginName)
	{
		const FString A = FPaths::ProjectPluginsDir() / HostPluginName;
		if (IFileManager::Get().DirectoryExists(*A) &&
			IFileManager::Get().FileExists(*(A / (HostPluginName + TEXT(".uplugin")))))
		{
			return A;
		}
		return FString();
	}

	FString BuildExtensionManifest(
		const FString& ExtensionId, const FString& DisplayName, const FString& Description,
		const FString& Category, const FString& CatalogBlurb, const FString& ModuleName,
		bool bDefaultEnabled, bool bIsThirdParty, bool bRequiresLicense, const FString& LicenseFeatureId,
		const TArray<FString>& RequiredPlugins, const FString& Umbrella, const FString& UmbrellaDocPath,
		const FString& SampleToolName)
	{
		TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetNumberField(TEXT("manifest_version"), 1);
		Root->SetNumberField(TEXT("api_version"),      1);
		Root->SetStringField(TEXT("extension_id"),     ExtensionId);
		Root->SetStringField(TEXT("display_name"),     DisplayName);
		Root->SetStringField(TEXT("description"),      Description);
		Root->SetStringField(TEXT("category"),         Category);
		Root->SetStringField(TEXT("catalog_blurb"),    CatalogBlurb);
		Root->SetStringField(TEXT("required_module_name"), ModuleName);
		Root->SetBoolField  (TEXT("default_enabled"),  bDefaultEnabled);
		Root->SetBoolField  (TEXT("is_third_party"),   bIsThirdParty);
		Root->SetBoolField  (TEXT("requires_license"), bRequiresLicense);
		if (bRequiresLicense && !LicenseFeatureId.IsEmpty())
		{
			Root->SetStringField(TEXT("license_feature_id"), LicenseFeatureId);
		}

		TArray<TSharedPtr<FJsonValue>> ReqPluginArr;
		for (const FString& P : RequiredPlugins) ReqPluginArr.Add(MakeShared<FJsonValueString>(P));
		Root->SetArrayField(TEXT("required_plugins"), ReqPluginArr);

		TArray<TSharedPtr<FJsonValue>> Umbrellas;
		Umbrellas.Add(MakeShared<FJsonValueString>(Umbrella));
		Root->SetArrayField(TEXT("owned_umbrellas"), Umbrellas);

		TArray<TSharedPtr<FJsonValue>> Tools;
		Tools.Add(MakeShared<FJsonValueString>(SampleToolName));
		Root->SetArrayField(TEXT("owned_tools"), Tools);

		TSharedRef<FJsonObject> Docs = MakeShared<FJsonObject>();
		Docs->SetStringField(Umbrella, UmbrellaDocPath);
		Root->SetObjectField(TEXT("umbrella_docs"), Docs);

		FString Out;
		TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> W = TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Out);
		FJsonSerializer::Serialize(Root, W);
		return Out;
	}

	FString BuildExtensionBuildCs(const FString& ModuleName,
		const TArray<FString>& PublicDeps, const TArray<FString>& PrivateDeps)
	{
		TArray<FString> Pub = { TEXT("Core"), TEXT("CoreUObject"), TEXT("Engine"),
		                        TEXT("Json"), TEXT("JsonUtilities"),
		                        TEXT("UECPCore"), TEXT("UECPTools") };
		for (const FString& P : PublicDeps) Pub.AddUnique(P);

		TArray<FString> Priv = { TEXT("EditorScriptingUtilities") };
		for (const FString& P : PrivateDeps) Priv.AddUnique(P);

		FString Out;
		Out += TEXT("// Copyright 2026, All Rights Reserved\n\n");
		Out += TEXT("using UnrealBuildTool;\n\n");
		Out += FString::Printf(TEXT("public class %s : ModuleRules\n{\n"), *ModuleName);
		Out += FString::Printf(TEXT("\tpublic %s(ReadOnlyTargetRules Target) : base(Target)\n\t{\n"), *ModuleName);
		Out += TEXT("\t\tif (Target.Type != TargetType.Editor) return;\n");
		Out += TEXT("\t\tPCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;\n");
		Out += TEXT("\t\tbUseUnity = false;\n\n");
		Out += FString::Printf(TEXT("\t\tPublicDependencyModuleNames.AddRange(new string[] { %s });\n"), *JoinQuoted(Pub));
		Out += FString::Printf(TEXT("\t\tPrivateDependencyModuleNames.AddRange(new string[] { %s });\n"), *JoinQuoted(Priv));
		Out += TEXT("\t}\n}\n");
		return Out;
	}

	FString BuildExtensionModuleHeader(const FString& ModuleName)
	{
		FString Out;
		Out += TEXT("// Copyright 2026, All Rights Reserved\n\n");
		Out += TEXT("#pragma once\n\n");
		Out += TEXT("#include \"CoreMinimal.h\"\n");
		Out += TEXT("#include \"Modules/ModuleManager.h\"\n\n");
		Out += FString::Printf(TEXT("DECLARE_LOG_CATEGORY_EXTERN(Log%s, Log, All);\n\n"), *ModuleName);
		Out += FString::Printf(TEXT("class F%sModule : public IModuleInterface\n{\npublic:\n"), *ModuleName);
		Out += TEXT("\tvirtual void StartupModule() override;\n");
		Out += TEXT("\tvirtual void ShutdownModule() override;\n");
		Out += TEXT("};\n");
		return Out;
	}

	FString BuildExtensionModuleSource(const FString& ModuleName, const FString& ToolsHeader,
		const FString& ToolsNamespace, const FString& SampleHandlerName, const FString& SampleToolName)
	{
		FString Out;
		Out += TEXT("// Copyright 2026, All Rights Reserved\n\n");
		Out += FString::Printf(TEXT("#include \"%sModule.h\"\n"), *ModuleName);
		Out += FString::Printf(TEXT("#include \"Tools/%s.h\"\n\n"), *ToolsHeader);
		Out += TEXT("#include \"UECPCoreModule.h\"\n");
		Out += TEXT("#include \"Services/IUECPToolDispatcher.h\"\n\n");
		Out += FString::Printf(TEXT("DEFINE_LOG_CATEGORY(Log%s);\n\n"), *ModuleName);
		Out += TEXT("namespace\n{\n");
		Out += TEXT("\tstatic const TArray<FName>& OwnedToolNames()\n\t{\n");
		Out += TEXT("\t\tstatic const TArray<FName> Names = {\n");
		Out += FString::Printf(TEXT("\t\t\tTEXT(\"%s\"),\n"), *SampleToolName);
		Out += TEXT("\t\t};\n");
		Out += TEXT("\t\treturn Names;\n");
		Out += TEXT("\t}\n\n");
		Out += TEXT("\tstatic auto MakeHandler(TFunction<void(const TSharedPtr<FJsonObject>&, FString&, FString&)> Fn)\n");
		Out += TEXT("\t{\n");
		Out += TEXT("\t\treturn [Fn = MoveTemp(Fn)](const TSharedPtr<FJsonObject>& Args) -> FUECPToolResult\n");
		Out += TEXT("\t\t{\n");
		Out += TEXT("\t\t\tFUECPToolResult R;\n");
		Out += TEXT("\t\t\tFn(Args, R.ResultJson, R.ErrorMessage);\n");
		Out += TEXT("\t\t\tR.bSuccess = R.ErrorMessage.IsEmpty();\n");
		Out += TEXT("\t\t\treturn R;\n");
		Out += TEXT("\t\t};\n");
		Out += TEXT("\t}\n");
		Out += TEXT("}\n\n");
		Out += FString::Printf(TEXT("void F%sModule::StartupModule()\n{\n"), *ModuleName);
		Out += TEXT("\tif (!IUECPCoreModule::IsAvailable()) return;\n");
		Out += TEXT("\tIUECPToolDispatcher& D = IUECPCoreModule::Get().GetToolDispatcher();\n\n");
		Out += FString::Printf(TEXT("\tD.RegisterHandler(TEXT(\"%s\"), MakeHandler(%s::%s));\n\n"),
			*SampleToolName, *ToolsNamespace, *SampleHandlerName);
		Out += FString::Printf(TEXT("\tUE_LOG(Log%s, Log, TEXT(\"Registered %%d tools\"), OwnedToolNames().Num());\n"), *ModuleName);
		Out += TEXT("}\n\n");
		Out += FString::Printf(TEXT("void F%sModule::ShutdownModule()\n{\n"), *ModuleName);
		Out += TEXT("\tif (!IUECPCoreModule::IsAvailable()) return;\n");
		Out += TEXT("\tIUECPToolDispatcher& D = IUECPCoreModule::Get().GetToolDispatcher();\n");
		Out += TEXT("\tfor (const FName& N : OwnedToolNames()) D.UnregisterHandler(N);\n");
		Out += TEXT("}\n\n");
		Out += FString::Printf(TEXT("IMPLEMENT_MODULE(F%sModule, %s)\n"), *ModuleName, *ModuleName);
		return Out;
	}

	FString BuildToolsHeader(const FString& ApiMacro, const FString& Namespace, const FString& HandlerName)
	{
		FString Out;
		Out += TEXT("// Copyright 2026, All Rights Reserved\n\n");
		Out += TEXT("#pragma once\n\n");
		Out += TEXT("#include \"CoreMinimal.h\"\n");
		Out += TEXT("#include \"Dom/JsonObject.h\"\n\n");
		Out += FString::Printf(TEXT("namespace %s\n{\n"), *Namespace);
		Out += FString::Printf(TEXT("\t%s_API void %s(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);\n"),
			*ApiMacro, *HandlerName);
		Out += TEXT("}\n");
		return Out;
	}

	FString BuildToolsSource(const FString& HeaderName, const FString& Namespace, const FString& HandlerName)
	{
		FString Out;
		Out += TEXT("// Copyright 2026, All Rights Reserved\n\n");
		Out += FString::Printf(TEXT("#include \"Tools/%s.h\"\n\n"), *HeaderName);
		Out += TEXT("#include \"Serialization/JsonSerializer.h\"\n");
		Out += TEXT("#include \"Serialization/JsonWriter.h\"\n\n");
		Out += FString::Printf(TEXT("namespace %s\n{\n"), *Namespace);
		Out += FString::Printf(TEXT("\tvoid %s(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& /*OutError*/)\n"), *HandlerName);
		Out += TEXT("\t{\n");
		Out += TEXT("\t\tTSharedRef<FJsonObject> R = MakeShared<FJsonObject>();\n");
		Out += TEXT("\t\tR->SetBoolField(TEXT(\"success\"), true);\n");
		Out += TEXT("\t\tR->SetStringField(TEXT(\"message\"), TEXT(\"Hello from the new UECP extension. Replace this stub with your tool logic.\"));\n\n");
		Out += TEXT("\t\tTSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJsonString);\n");
		Out += TEXT("\t\tFJsonSerializer::Serialize(R, W);\n");
		Out += TEXT("\t}\n");
		Out += TEXT("}\n");
		return Out;
	}

	FString BuildUmbrellaDoc(const FString& Umbrella, const FString& Description, const FString& SampleToolName)
	{
		FString Out;
		Out += FString::Printf(TEXT("### %s\n"), *Umbrella);
		Out += Description.IsEmpty() ? TEXT("Describe what this umbrella does for the AI here.\n\n") : (Description + TEXT("\n\n"));
		Out += TEXT("**Actions:**\n");
		Out += FString::Printf(TEXT("- `%s` — placeholder handler that returns `{success:true, message:\"Hello from the new UECP extension. Replace this stub with your tool logic.\"}`. Replace with your real tool docs once you have a handler in place.\n"), *SampleToolName);
		return Out;
	}

	bool PatchHostUPluginAddExtensionModule(const FString& HostPluginRoot, const FString& HostPluginName, const FString& ModuleName, FString& OutError)
	{
		const FString UPluginPath = HostPluginRoot / (HostPluginName + TEXT(".uplugin"));
		FString Contents;
		if (!FFileHelper::LoadFileToString(Contents, *UPluginPath))
		{
			OutError = FString::Printf(TEXT("Could not read %s"), *UPluginPath);
			return false;
		}
		TSharedPtr<FJsonObject> Root;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Contents);
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
		{
			OutError = FString::Printf(TEXT("%s is not valid JSON"), *UPluginPath);
			return false;
		}

		TArray<TSharedPtr<FJsonValue>> Modules;
		const TArray<TSharedPtr<FJsonValue>>* ExistingArr = nullptr;
		if (Root->TryGetArrayField(TEXT("Modules"), ExistingArr) && ExistingArr)
		{
			Modules = *ExistingArr;
		}
		for (const TSharedPtr<FJsonValue>& V : Modules)
		{
			const TSharedPtr<FJsonObject>* AsObj = nullptr;
			if (!V.IsValid() || !V->TryGetObject(AsObj) || !AsObj || !AsObj->IsValid()) continue;
			FString Existing;
			if ((*AsObj)->TryGetStringField(TEXT("Name"), Existing) && Existing == ModuleName)
			{
				return true;
			}
		}

		TArray<TSharedPtr<FJsonValue>> Platforms;
		Platforms.Add(MakeShared<FJsonValueString>(TEXT("Win64")));
		Platforms.Add(MakeShared<FJsonValueString>(TEXT("Mac")));
		Platforms.Add(MakeShared<FJsonValueString>(TEXT("Linux")));

		TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("Name"),         ModuleName);
		Entry->SetStringField(TEXT("Type"),         TEXT("Editor"));
		Entry->SetArrayField (TEXT("PlatformAllowList"), Platforms);
		Entry->SetStringField(TEXT("LoadingPhase"), TEXT("None"));
		Modules.Add(MakeShared<FJsonValueObject>(Entry));
		Root->SetArrayField(TEXT("Modules"), Modules);

		FString NewContents;
		TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&NewContents);
		FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);
		if (!FFileHelper::SaveStringToFile(NewContents, *UPluginPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			OutError = FString::Printf(TEXT("Failed to write updated %s"), *UPluginPath);
			return false;
		}
		return true;
	}
}

void HandleCreateUECPExtensionFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { WriteJsonError(TEXT("Invalid args"), OutJsonString, OutError); return; }

	FString ExtensionId;
	FString ModuleName;
	FString Umbrella;
	if (!Args->TryGetStringField(TEXT("extension_id"), ExtensionId) || ExtensionId.IsEmpty()
	 || !Args->TryGetStringField(TEXT("module_name"),  ModuleName)  || ModuleName.IsEmpty()
	 || !Args->TryGetStringField(TEXT("umbrella"),     Umbrella)    || Umbrella.IsEmpty())
	{
		WriteJsonError(TEXT("Missing required args: extension_id, module_name, umbrella"), OutJsonString, OutError);
		return;
	}
	if (!IsValidIdentifier(ModuleName))
	{
		WriteJsonError(FString::Printf(TEXT("Invalid module_name '%s' — must be a valid C++ identifier"), *ModuleName), OutJsonString, OutError);
		return;
	}

	FString DisplayName  = ExtensionId;          Args->TryGetStringField(TEXT("display_name"),  DisplayName);
	FString Description  = FString();            Args->TryGetStringField(TEXT("description"),   Description);
	FString Category     = TEXT("Other");        Args->TryGetStringField(TEXT("category"),      Category);
	FString CatalogBlurb = Description.IsEmpty() ? DisplayName : Description.Left(120);
	Args->TryGetStringField(TEXT("catalog_blurb"), CatalogBlurb);
	FString LicenseFeatureId; Args->TryGetStringField(TEXT("license_feature_id"), LicenseFeatureId);

	bool bDefaultEnabled  = false;  Args->TryGetBoolField(TEXT("default_enabled"),  bDefaultEnabled);
	bool bRequiresLicense = false;  Args->TryGetBoolField(TEXT("requires_license"), bRequiresLicense);

	FString HostPlugin = TEXT("BpGeneratorUltimate");
	Args->TryGetStringField(TEXT("host_plugin"), HostPlugin);

	bool bIsThirdParty = !HostPlugin.Equals(TEXT("BpGeneratorUltimate"), ESearchCase::IgnoreCase);
	Args->TryGetBoolField(TEXT("is_third_party"), bIsThirdParty);

	TArray<FString> RequiredPlugins, PublicDeps, PrivateDeps;
	GetStringArray(Args, TEXT("required_plugins"),     RequiredPlugins);
	GetStringArray(Args, TEXT("public_dependencies"),  PublicDeps);
	GetStringArray(Args, TEXT("private_dependencies"), PrivateDeps);

	const FString HostRoot = FindHostPluginRoot(HostPlugin);
	if (HostRoot.IsEmpty())
	{
		WriteJsonError(FString::Printf(TEXT("Could not find Plugins/%s/<host_plugin>.uplugin — extensions live inside an enabled plugin's source tree. Pass host_plugin='<YourPluginName>' to target a third-party plugin."), *HostPlugin), OutJsonString, OutError);
		return;
	}

	const FString ExtRoot = HostRoot / TEXT("Source") / TEXT("Extensions") / ModuleName;
	IFileManager& FM = IFileManager::Get();
	if (FM.DirectoryExists(*ExtRoot))
	{
		WriteJsonError(FString::Printf(TEXT("Extension already exists at %s. Delete the directory first or pick a different module_name."), *ExtRoot), OutJsonString, OutError);
		return;
	}

	const FString ApiMacro       = ModuleName.ToUpper();
	const FString UmbrellaPascal = SnakeToPascal(Umbrella);
	const FString ToolsHeader    = UmbrellaPascal + TEXT("Tools");
	const FString ToolsNamespace = ToolsHeader;
	const FString SampleHandler  = FString::Printf(TEXT("Handle%sHelloFromArgs"), *UmbrellaPascal);
	const FString SampleToolName = Umbrella + TEXT("_hello");
	const FString UmbrellaDocRel = FString::Printf(TEXT("Docs/%s.md"), *Umbrella);

	const FString ManifestPath = ExtRoot / (ModuleName + TEXT(".uecpext.json"));
	const FString BuildCsPath  = ExtRoot / (ModuleName + TEXT(".Build.cs"));
	const FString HeaderPath   = ExtRoot / TEXT("Public")  / (ModuleName + TEXT("Module.h"));
	const FString SourcePath   = ExtRoot / TEXT("Private") / (ModuleName + TEXT("Module.cpp"));
	const FString ToolsHPath   = ExtRoot / TEXT("Public")  / TEXT("Tools") / (ToolsHeader + TEXT(".h"));
	const FString ToolsCppPath = ExtRoot / TEXT("Private") / TEXT("Tools") / (ToolsHeader + TEXT(".cpp"));
	const FString DocPath      = ExtRoot / UmbrellaDocRel;

	TArray<FString> Created;

	const FString ManifestJson = BuildExtensionManifest(ExtensionId, DisplayName, Description, Category, CatalogBlurb,
		ModuleName, bDefaultEnabled, bIsThirdParty, bRequiresLicense, LicenseFeatureId, RequiredPlugins, Umbrella, UmbrellaDocRel, SampleToolName);
	if (!WriteTextFile(ManifestPath, ManifestJson, OutError)) { WriteJsonError(OutError, OutJsonString, OutError); return; }
	Created.Add(ManifestPath);

	if (!WriteTextFile(BuildCsPath, BuildExtensionBuildCs(ModuleName, PublicDeps, PrivateDeps), OutError)) { WriteJsonError(OutError, OutJsonString, OutError); return; }
	Created.Add(BuildCsPath);

	if (!WriteTextFile(HeaderPath, BuildExtensionModuleHeader(ModuleName), OutError)) { WriteJsonError(OutError, OutJsonString, OutError); return; }
	Created.Add(HeaderPath);

	if (!WriteTextFile(SourcePath, BuildExtensionModuleSource(ModuleName, ToolsHeader, ToolsNamespace, SampleHandler, SampleToolName), OutError))
	{ WriteJsonError(OutError, OutJsonString, OutError); return; }
	Created.Add(SourcePath);

	if (!WriteTextFile(ToolsHPath, BuildToolsHeader(ApiMacro, ToolsNamespace, SampleHandler), OutError)) { WriteJsonError(OutError, OutJsonString, OutError); return; }
	Created.Add(ToolsHPath);

	if (!WriteTextFile(ToolsCppPath, BuildToolsSource(ToolsHeader, ToolsNamespace, SampleHandler), OutError)) { WriteJsonError(OutError, OutJsonString, OutError); return; }
	Created.Add(ToolsCppPath);

	if (!WriteTextFile(DocPath, BuildUmbrellaDoc(Umbrella, Description, SampleToolName), OutError)) { WriteJsonError(OutError, OutJsonString, OutError); return; }
	Created.Add(DocPath);

	FString UPluginErr;
	const bool bHostPatched = PatchHostUPluginAddExtensionModule(HostRoot, HostPlugin, ModuleName, UPluginErr);
	if (!bHostPatched) UE_LOG(LogUECPCppExt, Warning, TEXT("create_uecp_extension: %s"), *UPluginErr);

	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetBoolField  (TEXT("success"),          true);
	R->SetStringField(TEXT("extension_id"),     ExtensionId);
	R->SetStringField(TEXT("module_name"),      ModuleName);
	R->SetStringField(TEXT("umbrella"),         Umbrella);
	R->SetStringField(TEXT("host_plugin"),      HostPlugin);
	R->SetBoolField  (TEXT("is_third_party"),   bIsThirdParty);
	R->SetStringField(TEXT("extension_root"),   ExtRoot);
	R->SetStringField(TEXT("manifest_path"),    ManifestPath);
	R->SetStringField(TEXT("sample_tool_name"), SampleToolName);
	R->SetBoolField  (TEXT("host_uplugin_patched"), bHostPatched);
	if (!bHostPatched) R->SetStringField(TEXT("host_uplugin_warning"), UPluginErr);

	TArray<TSharedPtr<FJsonValue>> FileArr;
	for (const FString& P : Created) FileArr.Add(MakeShared<FJsonValueString>(P));
	R->SetArrayField(TEXT("created_files"), FileArr);

	R->SetStringField(TEXT("next_step"),
		TEXT("1) Close the editor. 2) compile_project mode='full' — the new module needs a UBT relink. 3) Reopen the editor and enable the extension in Settings → Extensions. The sample handler will register the umbrella's first action."));

	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(R.ToSharedRef(), W);
}

void HandleAddPluginDependencyFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { WriteJsonError(TEXT("Invalid args"), OutJsonString, OutError); return; }

	FString PluginName, Dependency;
	if (!Args->TryGetStringField(TEXT("plugin_name"), PluginName) || PluginName.IsEmpty()
	 || !Args->TryGetStringField(TEXT("dependency"),  Dependency)  || Dependency.IsEmpty())
	{
		WriteJsonError(TEXT("Missing required args: plugin_name and dependency"), OutJsonString, OutError);
		return;
	}
	bool bEnabled = true;
	Args->TryGetBoolField(TEXT("enabled"), bEnabled);

	const FString PluginRoot = FindHostPluginRoot(PluginName);
	if (PluginRoot.IsEmpty())
	{
		WriteJsonError(FString::Printf(TEXT("Plugin '%s' not found under Plugins/. Pass plugin_name=<existing plugin>."), *PluginName), OutJsonString, OutError);
		return;
	}
	const FString UPluginPath = PluginRoot / (PluginName + TEXT(".uplugin"));

	FString Contents;
	if (!FFileHelper::LoadFileToString(Contents, *UPluginPath))
	{
		WriteJsonError(FString::Printf(TEXT("Could not read %s"), *UPluginPath), OutJsonString, OutError);
		return;
	}
	TSharedPtr<FJsonObject> Root;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Contents);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		WriteJsonError(FString::Printf(TEXT("%s is not valid JSON"), *UPluginPath), OutJsonString, OutError);
		return;
	}

	TArray<TSharedPtr<FJsonValue>> Plugins;
	const TArray<TSharedPtr<FJsonValue>>* ExistingArr = nullptr;
	if (Root->TryGetArrayField(TEXT("Plugins"), ExistingArr) && ExistingArr)
	{
		Plugins = *ExistingArr;
	}

	bool bFoundExisting = false;
	for (const TSharedPtr<FJsonValue>& V : Plugins)
	{
		const TSharedPtr<FJsonObject>* AsObj = nullptr;
		if (!V.IsValid() || !V->TryGetObject(AsObj) || !AsObj || !AsObj->IsValid()) continue;
		FString Existing;
		if ((*AsObj)->TryGetStringField(TEXT("Name"), Existing) && Existing == Dependency)
		{
			(*AsObj)->SetBoolField(TEXT("Enabled"), bEnabled);
			bFoundExisting = true;
			break;
		}
	}
	if (!bFoundExisting)
	{
		TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("Name"),    Dependency);
		Entry->SetBoolField  (TEXT("Enabled"), bEnabled);
		Plugins.Add(MakeShared<FJsonValueObject>(Entry));
	}
	Root->SetArrayField(TEXT("Plugins"), Plugins);

	FString NewContents;
	TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&NewContents);
	FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);
	if (!FFileHelper::SaveStringToFile(NewContents, *UPluginPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		WriteJsonError(FString::Printf(TEXT("Failed to write updated %s"), *UPluginPath), OutJsonString, OutError);
		return;
	}

	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetBoolField  (TEXT("success"),       true);
	R->SetStringField(TEXT("plugin_name"),   PluginName);
	R->SetStringField(TEXT("dependency"),    Dependency);
	R->SetBoolField  (TEXT("enabled"),       bEnabled);
	R->SetStringField(TEXT("action_taken"),  bFoundExisting ? TEXT("updated") : TEXT("added"));
	R->SetStringField(TEXT("uplugin_path"),  UPluginPath);

	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(R.ToSharedRef(), W);
}

namespace
{
	FString FindOwnedUmbrellaInManifest(const TSharedPtr<FJsonObject>& Manifest)
	{
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (!Manifest->TryGetArrayField(TEXT("owned_umbrellas"), Arr) || !Arr || Arr->Num() == 0) return FString();
		return (*Arr)[0]->AsString();
	}
}

void HandleRegisterExtensionToolFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { WriteJsonError(TEXT("Invalid args"), OutJsonString, OutError); return; }

	FString ExtRoot, ToolName, HandlerNamespace, HandlerFunction;
	if (!Args->TryGetStringField(TEXT("extension_root"),    ExtRoot)          || ExtRoot.IsEmpty()
	 || !Args->TryGetStringField(TEXT("tool_name"),         ToolName)         || ToolName.IsEmpty()
	 || !Args->TryGetStringField(TEXT("handler_namespace"), HandlerNamespace) || HandlerNamespace.IsEmpty()
	 || !Args->TryGetStringField(TEXT("handler_function"),  HandlerFunction)  || HandlerFunction.IsEmpty())
	{
		WriteJsonError(TEXT("Missing required args: extension_root, tool_name, handler_namespace, handler_function"), OutJsonString, OutError);
		return;
	}

	FString DocsSection;
	Args->TryGetStringField(TEXT("docs_section"), DocsSection);

	IFileManager& FM = IFileManager::Get();
	if (!FM.DirectoryExists(*ExtRoot))
	{
		WriteJsonError(FString::Printf(TEXT("extension_root does not exist: %s"), *ExtRoot), OutJsonString, OutError);
		return;
	}

	FString ManifestPath;
	{
		TArray<FString> Found;
		FM.FindFiles(Found, *(ExtRoot / TEXT("*.uecpext.json")),  true,  false);
		if (Found.Num() != 1)
		{
			WriteJsonError(FString::Printf(TEXT("Expected exactly one .uecpext.json in %s, found %d"), *ExtRoot, Found.Num()), OutJsonString, OutError);
			return;
		}
		ManifestPath = ExtRoot / Found[0];
	}

	FString ModuleCppPath;
	{
		TArray<FString> Found;
		FM.FindFiles(Found, *(ExtRoot / TEXT("Private") / TEXT("*Module.cpp")), true, false);
		if (Found.Num() != 1)
		{
			WriteJsonError(FString::Printf(TEXT("Expected exactly one *Module.cpp in %s/Private, found %d"), *ExtRoot, Found.Num()), OutJsonString, OutError);
			return;
		}
		ModuleCppPath = ExtRoot / TEXT("Private") / Found[0];
	}

	FString ManifestText;
	if (!FFileHelper::LoadFileToString(ManifestText, *ManifestPath))
	{
		WriteJsonError(FString::Printf(TEXT("Could not read %s"), *ManifestPath), OutJsonString, OutError);
		return;
	}
	TSharedPtr<FJsonObject> Manifest;
	TSharedRef<TJsonReader<>> ManifestReader = TJsonReaderFactory<>::Create(ManifestText);
	if (!FJsonSerializer::Deserialize(ManifestReader, Manifest) || !Manifest.IsValid())
	{
		WriteJsonError(TEXT("Manifest is not valid JSON"), OutJsonString, OutError);
		return;
	}
	TArray<TSharedPtr<FJsonValue>> Owned;
	const TArray<TSharedPtr<FJsonValue>>* OwnedExisting = nullptr;
	if (Manifest->TryGetArrayField(TEXT("owned_tools"), OwnedExisting) && OwnedExisting)
	{
		Owned = *OwnedExisting;
	}
	bool bManifestAlreadyHasTool = false;
	for (const TSharedPtr<FJsonValue>& V : Owned)
	{
		if (V.IsValid() && V->AsString() == ToolName) { bManifestAlreadyHasTool = true; break; }
	}
	if (!bManifestAlreadyHasTool)
	{
		Owned.Add(MakeShared<FJsonValueString>(ToolName));
		Manifest->SetArrayField(TEXT("owned_tools"), Owned);

		FString NewManifestText;
		TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> ManifestWriter = TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&NewManifestText);
		FJsonSerializer::Serialize(Manifest.ToSharedRef(), ManifestWriter);
		if (!FFileHelper::SaveStringToFile(NewManifestText, *ManifestPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			WriteJsonError(FString::Printf(TEXT("Failed to write %s"), *ManifestPath), OutJsonString, OutError);
			return;
		}
	}

	FString CppText;
	if (!FFileHelper::LoadFileToString(CppText, *ModuleCppPath))
	{
		WriteJsonError(FString::Printf(TEXT("Could not read %s"), *ModuleCppPath), OutJsonString, OutError);
		return;
	}

	const FString CppOwnedLine    = FString::Printf(TEXT("TEXT(\"%s\")"), *ToolName);
	const FString CppRegisterLine = FString::Printf(TEXT("D.RegisterHandler(TEXT(\"%s\"), MakeHandler(%s::%s));"),
		*ToolName, *HandlerNamespace, *HandlerFunction);

	const bool bCppAlreadyHasTool = CppText.Contains(CppOwnedLine) && CppText.Contains(CppRegisterLine);
	if (!bCppAlreadyHasTool)
	{
		const FString OwnedAnchor = TEXT("static const TArray<FName> Names = {");
		const int32 OwnedStart = CppText.Find(OwnedAnchor);
		if (OwnedStart == INDEX_NONE)
		{
			WriteJsonError(FString::Printf(TEXT("Could not find OwnedToolNames initializer in %s"), *ModuleCppPath), OutJsonString, OutError);
			return;
		}
		const int32 OwnedClose = CppText.Find(TEXT("};"), ESearchCase::CaseSensitive, ESearchDir::FromStart, OwnedStart);
		if (OwnedClose == INDEX_NONE)
		{
			WriteJsonError(TEXT("Could not find OwnedToolNames closing brace"), OutJsonString, OutError);
			return;
		}
		int32 OwnedLineStart = OwnedClose;
		while (OwnedLineStart > 0 && CppText[OwnedLineStart - 1] != TEXT('\n')) --OwnedLineStart;
		const FString InsertOwned = FString::Printf(TEXT("\t\t\t%s,\n"), *CppOwnedLine);
		CppText.InsertAt(OwnedLineStart, InsertOwned);

		const FString StartupAnchor = TEXT("::StartupModule()");
		const int32 StartupStart = CppText.Find(StartupAnchor);
		if (StartupStart == INDEX_NONE)
		{
			WriteJsonError(TEXT("Could not find StartupModule body"), OutJsonString, OutError);
			return;
		}
		const int32 BodyOpen = CppText.Find(TEXT("{"), ESearchCase::CaseSensitive, ESearchDir::FromStart, StartupStart);
		if (BodyOpen == INDEX_NONE)
		{
			WriteJsonError(TEXT("Could not find StartupModule opening brace"), OutJsonString, OutError);
			return;
		}
		const int32 BodyClose = CppText.Find(TEXT("\n}"), ESearchCase::CaseSensitive, ESearchDir::FromStart, BodyOpen);
		if (BodyClose == INDEX_NONE)
		{
			WriteJsonError(TEXT("Could not find StartupModule closing brace"), OutJsonString, OutError);
			return;
		}
		int32 InsertAt = BodyClose;
		const FString LogAnchor = TEXT("UE_LOG(");
		const int32 LogIdx = CppText.Find(LogAnchor, ESearchCase::CaseSensitive, ESearchDir::FromStart, BodyOpen);
		if (LogIdx != INDEX_NONE && LogIdx < BodyClose)
		{
			InsertAt = LogIdx;
		}
		int32 LineStart = InsertAt;
		while (LineStart > 0 && CppText[LineStart - 1] != TEXT('\n')) --LineStart;
		const FString InsertReg = FString::Printf(TEXT("\t%s\n\n"), *CppRegisterLine);
		CppText.InsertAt(LineStart, InsertReg);

		if (!FFileHelper::SaveStringToFile(CppText, *ModuleCppPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			WriteJsonError(FString::Printf(TEXT("Failed to write %s"), *ModuleCppPath), OutJsonString, OutError);
			return;
		}
	}

	bool bDocAppended = false;
	FString DocPath;
	if (!DocsSection.IsEmpty())
	{
		const FString Umbrella = FindOwnedUmbrellaInManifest(Manifest);
		if (!Umbrella.IsEmpty())
		{
			DocPath = ExtRoot / TEXT("Docs") / (Umbrella + TEXT(".md"));
			FString DocText;
			FFileHelper::LoadFileToString(DocText, *DocPath);

			FString Section = DocsSection;
			Section.TrimStartInline();
			if (Section.StartsWith(TEXT("**Actions:**")))
			{
				int32 NewlineIdx = INDEX_NONE;
				Section.FindChar(TEXT('\n'), NewlineIdx);
				Section = (NewlineIdx == INDEX_NONE)
					? FString()
					: Section.Mid(NewlineIdx + 1).TrimStart();
			}

			if (!DocText.EndsWith(TEXT("\n"))) DocText += TEXT("\n");
			DocText += Section;
			if (!DocText.EndsWith(TEXT("\n"))) DocText += TEXT("\n");
			if (FFileHelper::SaveStringToFile(DocText, *DocPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
			{
				bDocAppended = true;
			}
		}
	}

	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetBoolField  (TEXT("success"),           true);
	R->SetStringField(TEXT("tool_name"),         ToolName);
	R->SetBoolField  (TEXT("manifest_updated"),  !bManifestAlreadyHasTool);
	R->SetBoolField  (TEXT("module_cpp_updated"), !bCppAlreadyHasTool);
	R->SetBoolField  (TEXT("docs_appended"),     bDocAppended);
	if (bDocAppended) R->SetStringField(TEXT("docs_path"), DocPath);
	R->SetStringField(TEXT("manifest_path"),     ManifestPath);
	R->SetStringField(TEXT("module_cpp_path"),   ModuleCppPath);

	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(R.ToSharedRef(), W);
}

}
