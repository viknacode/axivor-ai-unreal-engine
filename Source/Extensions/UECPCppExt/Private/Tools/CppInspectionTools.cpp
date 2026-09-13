// Copyright 2026, BlueprintsLab, All rights reserved

#include "Tools/CppInspectionTools.h"

#include "HeaderScanner.h"
#include "ModuleResolver.h"
#include "UECPCppExtModule.h"

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace CppInspectionTools
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

	TSharedRef<FJsonObject> SerializeProperty(const HeaderScanner::FUPropertyInfo& P)
	{
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("name"),       P.Name);
		O->SetStringField(TEXT("type"),       P.Type);
		O->SetStringField(TEXT("specifiers"), P.Specifiers);
		if (!P.DefaultValue.IsEmpty()) O->SetStringField(TEXT("default_value"), P.DefaultValue);
		return O;
	}

	TSharedRef<FJsonObject> SerializeFunction(const HeaderScanner::FUFunctionInfo& F)
	{
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("name"),        F.Name);
		O->SetStringField(TEXT("return_type"), F.ReturnType);
		O->SetStringField(TEXT("params"),      F.Params);
		O->SetStringField(TEXT("specifiers"),  F.Specifiers);
		O->SetBoolField  (TEXT("virtual"),     F.bVirtual);
		O->SetBoolField  (TEXT("static"),      F.bStatic);
		O->SetBoolField  (TEXT("const"),       F.bConst);
		O->SetBoolField  (TEXT("override"),    F.bOverride);
		return O;
	}

	TSharedRef<FJsonObject> SerializeClass(const HeaderScanner::FClassBlock& C)
	{
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("name"),         C.Name);
		O->SetStringField(TEXT("kind"),         C.Kind);
		O->SetStringField(TEXT("specifiers"),   C.Specifiers);
		if (!C.ParentName.IsEmpty()) O->SetStringField(TEXT("parent"),    C.ParentName);
		if (!C.ApiMacro.IsEmpty())   O->SetStringField(TEXT("api_macro"), C.ApiMacro);
		O->SetBoolField  (TEXT("has_generated_body"), C.GeneratedBodyOffset != INDEX_NONE);

		TArray<TSharedPtr<FJsonValue>> Props;
		for (const HeaderScanner::FUPropertyInfo& P : C.Properties)
		{
			Props.Add(MakeShared<FJsonValueObject>(SerializeProperty(P)));
		}
		O->SetArrayField(TEXT("properties"), Props);

		TArray<TSharedPtr<FJsonValue>> Funcs;
		for (const HeaderScanner::FUFunctionInfo& F : C.Functions)
		{
			Funcs.Add(MakeShared<FJsonValueObject>(SerializeFunction(F)));
		}
		O->SetArrayField(TEXT("functions"), Funcs);
		return O;
	}

	FString ResolveAbsolutePath(const FString& In)
	{
		if (FPaths::IsRelative(In))
		{
			return FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), In);
		}
		return In;
	}
}

void HandleGetClassSummaryFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { WriteJsonError(TEXT("Invalid args"), OutJsonString, OutError); return; }

	FString FilePath;
	Args->TryGetStringField(TEXT("file_path"), FilePath);
	if (FilePath.IsEmpty()) { WriteJsonError(TEXT("Missing required parameter: file_path"), OutJsonString, OutError); return; }

	const FString AbsPath = ResolveAbsolutePath(FilePath);
	if (!FPaths::FileExists(AbsPath))
	{
		WriteJsonError(FString::Printf(TEXT("File not found: %s"), *AbsPath), OutJsonString, OutError);
		return;
	}
	const FString Ext = FPaths::GetExtension(AbsPath).ToLower();
	if (Ext != TEXT("h") && Ext != TEXT("hpp"))
	{
		WriteJsonError(FString::Printf(TEXT("get_class_summary expects a header (.h/.hpp), got .%s"), *Ext), OutJsonString, OutError);
		return;
	}

	FString Content;
	if (!FFileHelper::LoadFileToString(Content, *AbsPath))
	{
		WriteJsonError(FString::Printf(TEXT("Failed to read: %s"), *AbsPath), OutJsonString, OutError);
		return;
	}

	const HeaderScanner::FParseResult Parsed = HeaderScanner::ParseHeader(Content);

	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetBoolField  (TEXT("success"),                true);
	R->SetStringField(TEXT("file_path"),              AbsPath);
	R->SetBoolField  (TEXT("has_pragma_once"),        Parsed.bHasPragmaOnce);
	R->SetStringField(TEXT("generated_header"),       Parsed.GeneratedHeaderInclude);

	TArray<TSharedPtr<FJsonValue>> IncArr;
	for (const FString& I : Parsed.Includes) IncArr.Add(MakeShared<FJsonValueString>(I));
	R->SetArrayField(TEXT("includes"), IncArr);

	TArray<TSharedPtr<FJsonValue>> FwdArr;
	for (const FString& F : Parsed.ForwardDeclarations) FwdArr.Add(MakeShared<FJsonValueString>(F));
	R->SetArrayField(TEXT("forward_declarations"), FwdArr);

	TArray<TSharedPtr<FJsonValue>> ClsArr;
	for (const HeaderScanner::FClassBlock& C : Parsed.Classes)
	{
		ClsArr.Add(MakeShared<FJsonValueObject>(SerializeClass(C)));
	}
	R->SetArrayField(TEXT("classes"), ClsArr);

	const TArray<ModuleResolver::FModuleInfo> Modules = ModuleResolver::EnumerateProjectModules();
	if (const ModuleResolver::FModuleInfo* Owner = ModuleResolver::FindOwningModule(Modules, AbsPath))
	{
		R->SetStringField(TEXT("module"),    Owner->Name);
		R->SetStringField(TEXT("api_macro"), ModuleResolver::GetApiMacro(Owner->Name));
	}

	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(R.ToSharedRef(), W);
}

void HandleFindClassDefinitionFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { WriteJsonError(TEXT("Invalid args"), OutJsonString, OutError); return; }

	FString ClassName;
	Args->TryGetStringField(TEXT("class_name"), ClassName);
	if (ClassName.IsEmpty()) Args->TryGetStringField(TEXT("name"), ClassName);
	if (ClassName.IsEmpty()) { WriteJsonError(TEXT("Missing required parameter: class_name"), OutJsonString, OutError); return; }

	const TArray<ModuleResolver::FModuleInfo> Modules = ModuleResolver::EnumerateProjectModules();

	struct FHit
	{
		FString File;
		FString Module;
		FString Kind;
		FString ParentName;
	};
	TArray<FHit> Hits;

	for (const ModuleResolver::FModuleInfo& Mod : Modules)
	{
		TArray<FString> Headers;
		IFileManager::Get().FindFilesRecursive(Headers, *Mod.ModuleRoot, TEXT("*.h"), true, false, false);
		for (const FString& H : Headers)
		{
			FString Content;
			if (!FFileHelper::LoadFileToString(Content, *H)) continue;
			if (Content.Find(ClassName, ESearchCase::CaseSensitive) == INDEX_NONE) continue;

			const HeaderScanner::FParseResult P = HeaderScanner::ParseHeader(Content);
			if (const HeaderScanner::FClassBlock* B = HeaderScanner::FindClass(P, ClassName))
			{
				Hits.Add({ H, Mod.Name, B->Kind, B->ParentName });
			}
		}
	}

	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetBoolField  (TEXT("success"),     true);
	R->SetStringField(TEXT("class_name"),  ClassName);
	R->SetNumberField(TEXT("hit_count"),   Hits.Num());

	TArray<TSharedPtr<FJsonValue>> Arr;
	for (const FHit& H : Hits)
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("file"),        H.File);
		O->SetStringField(TEXT("module"),      H.Module);
		O->SetStringField(TEXT("kind"),        H.Kind);
		if (!H.ParentName.IsEmpty()) O->SetStringField(TEXT("parent"), H.ParentName);
		Arr.Add(MakeShared<FJsonValueObject>(O));
	}
	R->SetArrayField(TEXT("results"), Arr);

	if (Hits.Num() == 0)
	{
		R->SetStringField(TEXT("message"),
			FString::Printf(TEXT("No definition of '%s' found in the project's Source/ tree. Try search_engine_source(scope=\"engine\") if it's an engine class."),
				*ClassName));
	}

	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(R.ToSharedRef(), W);
}

}
