// Copyright 2026, BlueprintsLab, All rights reserved

#include "Tools/CppModuleTools.h"

#include "ModuleResolver.h"
#include "UECPCppExtModule.h"

#include "Misc/FileHelper.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace CppModuleTools
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

	TSharedRef<FJsonObject> SerializeModule(const ModuleResolver::FModuleInfo& M)
	{
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("name"),         M.Name);
		O->SetStringField(TEXT("build_cs"),     M.BuildCsPath);
		O->SetStringField(TEXT("module_root"),  M.ModuleRoot);
		O->SetStringField(TEXT("api_macro"),    ModuleResolver::GetApiMacro(M.Name));
		O->SetBoolField  (TEXT("in_plugin"),    M.bInPlugin);
		if (M.bInPlugin) O->SetStringField(TEXT("plugin_name"), M.PluginName);

		auto AddArr = [&](const TCHAR* Key, const TArray<FString>& Src)
		{
			TArray<TSharedPtr<FJsonValue>> Arr;
			for (const FString& S : Src) Arr.Add(MakeShared<FJsonValueString>(S));
			O->SetArrayField(Key, Arr);
		};
		AddArr(TEXT("public_deps"),  M.PublicDependencyModuleNames);
		AddArr(TEXT("private_deps"), M.PrivateDependencyModuleNames);
		return O;
	}
}

void HandleListProjectModulesFromArgs(const TSharedPtr<FJsonObject>& , FString& OutJsonString, FString& OutError)
{
	const TArray<ModuleResolver::FModuleInfo> Modules = ModuleResolver::EnumerateProjectModules();

	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetBoolField  (TEXT("success"),      true);
	R->SetNumberField(TEXT("module_count"), Modules.Num());

	const ModuleResolver::FModuleInfo* Primary = ModuleResolver::FindPrimaryGameModule(Modules);
	if (Primary) R->SetStringField(TEXT("primary_module"), Primary->Name);

	TArray<TSharedPtr<FJsonValue>> Arr;
	for (const ModuleResolver::FModuleInfo& M : Modules)
	{
		Arr.Add(MakeShared<FJsonValueObject>(SerializeModule(M)));
	}
	R->SetArrayField(TEXT("modules"), Arr);

	if (Modules.Num() == 0)
	{
		R->SetStringField(TEXT("message"),
			TEXT("Project has no C++ Source/. Add a stub C++ class via the editor's Tools menu first to bootstrap the Source folder."));
	}

	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(R.ToSharedRef(), W);
}

void HandleEditModuleDepsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { WriteJsonError(TEXT("Invalid args"), OutJsonString, OutError); return; }

	FString ModuleName;
	Args->TryGetStringField(TEXT("module_name"), ModuleName);
	if (ModuleName.IsEmpty()) Args->TryGetStringField(TEXT("module"), ModuleName);
	if (ModuleName.IsEmpty()) { WriteJsonError(TEXT("Missing required parameter: module_name"), OutJsonString, OutError); return; }

	const TArray<ModuleResolver::FModuleInfo> Modules = ModuleResolver::EnumerateProjectModules();
	const ModuleResolver::FModuleInfo* Mod = ModuleResolver::FindModuleByName(Modules, ModuleName);
	if (!Mod)
	{
		WriteJsonError(FString::Printf(TEXT("Module '%s' not found. Call list_project_modules to see available modules."), *ModuleName), OutJsonString, OutError);
		return;
	}

	auto ReadStringArray = [&](const TCHAR* Field, TArray<FString>& Out)
	{
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (Args->TryGetArrayField(Field, Arr) && Arr)
		{
			for (const TSharedPtr<FJsonValue>& V : *Arr)
			{
				if (V.IsValid() && V->Type == EJson::String) Out.Add(V->AsString());
			}
		}
	};

	ModuleResolver::FBuildCsPatch Patch;
	ReadStringArray(TEXT("add_public"),     Patch.AddPublic);
	ReadStringArray(TEXT("add_private"),    Patch.AddPrivate);
	ReadStringArray(TEXT("remove_public"),  Patch.RemovePublic);
	ReadStringArray(TEXT("remove_private"), Patch.RemovePrivate);

	if (Patch.AddPublic.Num() == 0 && Patch.AddPrivate.Num() == 0 &&
	    Patch.RemovePublic.Num() == 0 && Patch.RemovePrivate.Num() == 0)
	{
		WriteJsonError(TEXT("No edits requested. Pass add_public[], add_private[], remove_public[], or remove_private[]."), OutJsonString, OutError);
		return;
	}

	FString Original;
	if (!FFileHelper::LoadFileToString(Original, *Mod->BuildCsPath))
	{
		WriteJsonError(FString::Printf(TEXT("Failed to read %s"), *Mod->BuildCsPath), OutJsonString, OutError);
		return;
	}

	bool bChanged = false;
	const FString Patched = ModuleResolver::PatchBuildCs(Original, Patch, bChanged);

	if (!bChanged)
	{
		TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
		R->SetBoolField  (TEXT("success"),     true);
		R->SetBoolField  (TEXT("changed"),     false);
		R->SetStringField(TEXT("module_name"), ModuleName);
		R->SetStringField(TEXT("message"),     TEXT("All requested entries were already in the desired state. No edit applied."));
		TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJsonString);
		FJsonSerializer::Serialize(R.ToSharedRef(), W);
		return;
	}

	if (!FFileHelper::SaveStringToFile(Patched, *Mod->BuildCsPath))
	{
		WriteJsonError(FString::Printf(TEXT("Failed to write %s"), *Mod->BuildCsPath), OutJsonString, OutError);
		return;
	}

	UE_LOG(LogUECPCppExt, Log, TEXT("edit_module_deps: %s (+pub %d / +pri %d / -pub %d / -pri %d)"),
		*ModuleName, Patch.AddPublic.Num(), Patch.AddPrivate.Num(),
		Patch.RemovePublic.Num(), Patch.RemovePrivate.Num());

	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetBoolField  (TEXT("success"),     true);
	R->SetBoolField  (TEXT("changed"),     true);
	R->SetStringField(TEXT("module_name"), ModuleName);
	R->SetStringField(TEXT("build_cs"),    Mod->BuildCsPath);
	R->SetStringField(TEXT("message"),     FString::Printf(TEXT("Patched %s.Build.cs"), *ModuleName));
	R->SetStringField(TEXT("next_step"),   TEXT("Call compile_project to relink with the new module dependencies."));
	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(R.ToSharedRef(), W);
}

}
