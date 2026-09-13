// Copyright 2026, BlueprintsLab, All rights reserved

#include "Tools/CppAuthoringTools.h"

#include "CppCodegen.h"
#include "CppValidation.h"
#include "ModuleResolver.h"
#include "UECPCppExtModule.h"

#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace CppAuthoringTools
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

	bool LooksLikeUEPrefix(TCHAR C)
	{
		return C == TEXT('U') || C == TEXT('A') || C == TEXT('F') ||
		       C == TEXT('E') || C == TEXT('I') || C == TEXT('S');
	}

	FString StripTypePrefix(const FString& Name)
	{
		if (Name.Len() >= 2 && LooksLikeUEPrefix(Name[0]) && FChar::IsUpper(Name[1]))
		{
			return Name.Mid(1);
		}
		return Name;
	}

	bool HasTypePrefix(const FString& Name, TCHAR Expected)
	{
		return Name.Len() >= 2 && Name[0] == Expected && FChar::IsUpper(Name[1]);
	}

	bool HasAnyTypePrefix(const FString& Name)
	{
		return Name.Len() >= 2 && LooksLikeUEPrefix(Name[0]) && FChar::IsUpper(Name[1]);
	}

	bool NormalizeTypeName(const FString& In, TCHAR Expected, const TCHAR* Kind,
		FString& OutNormalized, bool& bOutWasFixed, FString& OutError)
	{
		bOutWasFixed = false;
		if (In.IsEmpty())
		{
			OutError = FString::Printf(TEXT("Name cannot be empty for %s."), Kind);
			return false;
		}

		if (HasTypePrefix(In, Expected))
		{
			OutNormalized = In;
			return true;
		}

		if (HasAnyTypePrefix(In))
		{
			OutError = FString::Printf(
				TEXT("Name '%s' has prefix '%c' but %s requires prefix '%c'. "
				     "Suggested name: '%c%s'. UnrealHeaderTool would reject this. "
				     "Either drop the wrong prefix (we'll add the right one) or "
				     "pass the corrected name explicitly."),
				*In, In[0], Kind, Expected, Expected, *In.Mid(1));
			return false;
		}

		OutNormalized = FString::Printf(TEXT("%c%s"), Expected, *In);
		bOutWasFixed = true;
		return true;
	}

	TCHAR GetExpectedUClassPrefix(const FString& ParentClass, bool bIsActorFlag)
	{
		if (bIsActorFlag) return TEXT('A');
		if (ParentClass.Len() >= 2 && ParentClass[0] == TEXT('A') && FChar::IsUpper(ParentClass[1]))
		{
			return TEXT('A');
		}
		return TEXT('U');
	}

	struct FResolvedPaths
	{
		const ModuleResolver::FModuleInfo* Module = nullptr;
		FString HeaderAbsPath;
		FString SourceAbsPath;
		FString FileBaseName;
	};

	bool ResolveTargetPaths(const TSharedPtr<FJsonObject>& Args,
		const FString& ClassName, bool bWantSource, const TArray<ModuleResolver::FModuleInfo>& Modules,
		FResolvedPaths& Out, FString& OutError)
	{
		FString ModuleName;
		Args->TryGetStringField(TEXT("module"), ModuleName);
		if (ModuleName.IsEmpty()) Args->TryGetStringField(TEXT("module_name"), ModuleName);

		if (!ModuleName.IsEmpty())
		{
			Out.Module = ModuleResolver::FindModuleByName(Modules, ModuleName);
			if (!Out.Module)
			{
				OutError = FString::Printf(TEXT("Module '%s' not found under Source/. Call list_project_modules to see what's available."), *ModuleName);
				return false;
			}
		}
		else
		{
			Out.Module = ModuleResolver::FindPrimaryGameModule(Modules);
			if (!Out.Module)
			{
				OutError = TEXT("Project has no primary C++ game module (no Source/<Name>/<Name>.Build.cs). Add a stub C++ class via the editor's Tools menu first.");
				return false;
			}
		}

		FString Subfolder;
		Args->TryGetStringField(TEXT("subfolder"), Subfolder);
		Subfolder.ReplaceInline(TEXT("\\"), TEXT("/"));
		Subfolder.RemoveFromStart(TEXT("/"));
		Subfolder.RemoveFromEnd  (TEXT("/"));

		Out.FileBaseName = StripTypePrefix(ClassName);

		FString Placement = TEXT("public_private");
		Args->TryGetStringField(TEXT("placement"), Placement);
		Placement = Placement.ToLower();

		const FString HeaderRel = Subfolder.IsEmpty() ? Out.FileBaseName + TEXT(".h")
		                                              : Subfolder + TEXT("/") + Out.FileBaseName + TEXT(".h");
		const FString SourceRel = Subfolder.IsEmpty() ? Out.FileBaseName + TEXT(".cpp")
		                                              : Subfolder + TEXT("/") + Out.FileBaseName + TEXT(".cpp");

		if (Placement == TEXT("module_root") || Placement == TEXT("flat"))
		{
			Out.HeaderAbsPath = FPaths::Combine(Out.Module->ModuleRoot, HeaderRel);
			if (bWantSource) Out.SourceAbsPath = FPaths::Combine(Out.Module->ModuleRoot, SourceRel);
		}
		else
		{
			Out.HeaderAbsPath = FPaths::Combine(Out.Module->ModuleRoot, TEXT("Public"),  HeaderRel);
			if (bWantSource) Out.SourceAbsPath = FPaths::Combine(Out.Module->ModuleRoot, TEXT("Private"), SourceRel);
		}
		return true;
	}

	bool WriteFileWithValidation(const FString& AbsPath, FString Content, bool bIsHeader, FString& OutError)
	{
		const FString FileName = FPaths::GetCleanFilename(AbsPath);
		CppValidation::FValidationResult Validation = bIsHeader
			? CppValidation::ValidateHeader(Content, FileName)
			: CppValidation::ValidateSource(Content, FileName);

		if (Validation.Errors.Num() > 0)
		{
			OutError = FString::Printf(TEXT("Generated content failed UE validation: %s"),
				*FString::Join(Validation.Errors, TEXT("; ")));
			return false;
		}
		if (!Validation.FixedContent.IsEmpty())
		{
			Content = Validation.FixedContent;
		}

		const FString ParentDir = FPaths::GetPath(AbsPath);
		if (!IFileManager::Get().DirectoryExists(*ParentDir))
		{
			IFileManager::Get().MakeDirectory(*ParentDir, true);
		}

		if (FPaths::FileExists(AbsPath))
		{
			OutError = FString::Printf(TEXT("File already exists: %s. Use edit_cpp_file to modify it, or delete it first."), *AbsPath);
			return false;
		}

		if (!FFileHelper::SaveStringToFile(Content, *AbsPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			OutError = FString::Printf(TEXT("Failed to write file: %s"), *AbsPath);
			return false;
		}
		return true;
	}

	void EmitSuccessJson(const FResolvedPaths& Paths, const FString& ClassName,
		const TArray<FString>& WrittenFiles, FString& OutJsonString,
		const FString& RequestedName = FString())
	{
		TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
		R->SetBoolField  (TEXT("success"),     true);
		R->SetStringField(TEXT("class_name"),  ClassName);
		R->SetStringField(TEXT("module"),      Paths.Module->Name);
		R->SetStringField(TEXT("module_root"), Paths.Module->ModuleRoot);
		R->SetStringField(TEXT("api_macro"),   ModuleResolver::GetApiMacro(Paths.Module->Name));

		if (!RequestedName.IsEmpty() && RequestedName != ClassName)
		{
			R->SetStringField(TEXT("requested_name"), RequestedName);
			R->SetStringField(TEXT("name_note"),
				FString::Printf(TEXT("Auto-prefixed '%s' -> '%s' to satisfy UnrealHeaderTool's UE naming rule."),
					*RequestedName, *ClassName));
		}

		TArray<TSharedPtr<FJsonValue>> FilesArr;
		for (const FString& F : WrittenFiles) FilesArr.Add(MakeShared<FJsonValueString>(F));
		R->SetArrayField(TEXT("files_written"), FilesArr);

		R->SetStringField(TEXT("next_step"),
			TEXT("Call compile_project to build the new files. After build succeeds, the class is available in the editor."));

		TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJsonString);
		FJsonSerializer::Serialize(R.ToSharedRef(), W);
	}
}

static void DoCreateUClass(const TSharedPtr<FJsonObject>& Args, const FString& DefaultParent,
	bool bIsActorByDefault, bool bIsActorComponentByDefault,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { WriteJsonError(TEXT("Invalid args"), OutJsonString, OutError); return; }

	FString RequestedName;
	Args->TryGetStringField(TEXT("name"),       RequestedName);
	if (RequestedName.IsEmpty()) Args->TryGetStringField(TEXT("class_name"), RequestedName);
	if (RequestedName.IsEmpty()) { WriteJsonError(TEXT("Missing required parameter: name"), OutJsonString, OutError); return; }

	FString ParentClass;
	Args->TryGetStringField(TEXT("parent_class"), ParentClass);
	if (ParentClass.IsEmpty()) Args->TryGetStringField(TEXT("parent"), ParentClass);
	if (ParentClass.IsEmpty()) ParentClass = DefaultParent;

	const bool bIsActor =
		bIsActorByDefault ||
		ParentClass == TEXT("AActor")         || ParentClass == TEXT("APawn")        ||
		ParentClass == TEXT("ACharacter")     || ParentClass == TEXT("AController")  ||
		ParentClass == TEXT("APlayerController");
	const bool bIsActorComponent =
		bIsActorComponentByDefault ||
		ParentClass == TEXT("UActorComponent") ||
		ParentClass == TEXT("USceneComponent") ||
		ParentClass == TEXT("UPrimitiveComponent");

	const TCHAR ExpectedPrefix = GetExpectedUClassPrefix(ParentClass, bIsActor);
	FString ClassName;
	bool bNameNormalized = false;
	{
		FString NormalizeError;
		const TCHAR* KindLabel = bIsActor ? TEXT("AActor subclass") : TEXT("UObject subclass");
		if (!NormalizeTypeName(RequestedName, ExpectedPrefix, KindLabel,
			ClassName, bNameNormalized, NormalizeError))
		{
			WriteJsonError(NormalizeError, OutJsonString, OutError);
			return;
		}
	}

	bool bAbstract      = false; Args->TryGetBoolField(TEXT("abstract"),       bAbstract);
	bool bBlueprintable = true;  Args->TryGetBoolField(TEXT("blueprintable"),  bBlueprintable);
	bool bBlueprintType = true;  Args->TryGetBoolField(TEXT("blueprint_type"), bBlueprintType);
	bool bMinimalAPI    = false; Args->TryGetBoolField(TEXT("minimal_api"),    bMinimalAPI);

	FString Description, Category, DisplayName;
	Args->TryGetStringField(TEXT("description"),  Description);
	Args->TryGetStringField(TEXT("category"),     Category);
	Args->TryGetStringField(TEXT("display_name"), DisplayName);

	const TArray<ModuleResolver::FModuleInfo> Modules = ModuleResolver::EnumerateProjectModules();
	if (Modules.Num() == 0)
	{
		WriteJsonError(TEXT("Project has no C++ Source/. Add a C++ class via the editor's Tools menu first."), OutJsonString, OutError);
		return;
	}

	FResolvedPaths Paths;
	if (!ResolveTargetPaths(Args, ClassName,  !bAbstract, Modules, Paths, OutError))
	{
		WriteJsonError(OutError, OutJsonString, OutError);
		return;
	}

	CppCodegen::FUClassSpec Spec;
	Spec.ClassName        = ClassName;
	Spec.ParentClass      = ParentClass;
	Spec.ApiMacro         = ModuleResolver::GetApiMacro(Paths.Module->Name);
	Spec.bAbstract        = bAbstract;
	Spec.bBlueprintable   = bBlueprintable;
	Spec.bBlueprintType   = bBlueprintType;
	Spec.bMinimalAPI      = bMinimalAPI;
	Spec.Description      = Description;
	Spec.Category         = Category;
	Spec.DisplayName      = DisplayName;
	Spec.bIsActor         = bIsActor;
	Spec.bIsActorComponent= bIsActorComponent;

	FString Subfolder; Args->TryGetStringField(TEXT("subfolder"), Subfolder);
	Subfolder.ReplaceInline(TEXT("\\"), TEXT("/"));
	Subfolder.RemoveFromStart(TEXT("/")); Subfolder.RemoveFromEnd(TEXT("/"));
	Spec.ModuleSubfolder = Subfolder;

	{
		const TArray<TSharedPtr<FJsonValue>>* PropsArr = nullptr;
		if (Args->TryGetArrayField(TEXT("properties"), PropsArr) && PropsArr)
		{
			for (const TSharedPtr<FJsonValue>& V : *PropsArr)
			{
				const TSharedPtr<FJsonObject> O = V.IsValid() ? V->AsObject() : nullptr;
				if (!O.IsValid()) continue;
				CppCodegen::FAddUPropertySpec PS;
				O->TryGetStringField(TEXT("type"),          PS.Type);
				O->TryGetStringField(TEXT("name"),          PS.Name);
				O->TryGetStringField(TEXT("specifiers"),    PS.Specifiers);
				O->TryGetStringField(TEXT("default_value"), PS.DefaultValue);
				O->TryGetStringField(TEXT("comment"),       PS.Comment);
				if (!PS.Type.IsEmpty() && !PS.Name.IsEmpty())
				{
					Spec.Properties.Add(MoveTemp(PS));
				}
			}
		}

		const TArray<TSharedPtr<FJsonValue>>* FnsArr = nullptr;
		if (Args->TryGetArrayField(TEXT("functions"), FnsArr) && FnsArr)
		{
			for (const TSharedPtr<FJsonValue>& V : *FnsArr)
			{
				const TSharedPtr<FJsonObject> O = V.IsValid() ? V->AsObject() : nullptr;
				if (!O.IsValid()) continue;
				CppCodegen::FAddUFunctionSpec FS;
				O->TryGetStringField(TEXT("name"),        FS.Name);
				O->TryGetStringField(TEXT("return_type"), FS.ReturnType);
				O->TryGetStringField(TEXT("params"),      FS.Params);
				O->TryGetStringField(TEXT("specifiers"),  FS.Specifiers);
				O->TryGetStringField(TEXT("comment"),     FS.Comment);
				O->TryGetBoolField  (TEXT("virtual"),     FS.bVirtual);
				O->TryGetBoolField  (TEXT("static"),      FS.bStatic);
				O->TryGetBoolField  (TEXT("const"),       FS.bConst);
				if (!FS.Name.IsEmpty())
				{
					Spec.Functions.Add(MoveTemp(FS));
				}
			}
		}
	}

	const FString HeaderText = CppCodegen::EmitUClassHeader(Spec);
	if (!WriteFileWithValidation(Paths.HeaderAbsPath, HeaderText,  true, OutError))
	{
		WriteJsonError(OutError, OutJsonString, OutError);
		return;
	}

	TArray<FString> Written;
	Written.Add(Paths.HeaderAbsPath);

	if (!bAbstract)
	{
		const FString SourceText = CppCodegen::EmitUClassSource(Spec);
		if (!WriteFileWithValidation(Paths.SourceAbsPath, SourceText,  false, OutError))
		{
			IFileManager::Get().Delete(*Paths.HeaderAbsPath);
			WriteJsonError(OutError, OutJsonString, OutError);
			return;
		}
		Written.Add(Paths.SourceAbsPath);
	}

	UE_LOG(LogUECPCppExt, Log, TEXT("create_uclass: wrote %d file(s) for %s (inline: %d props / %d funcs)%s"),
		Written.Num(), *ClassName, Spec.Properties.Num(), Spec.Functions.Num(),
		bNameNormalized ? *FString::Printf(TEXT(" (normalized from '%s')"), *RequestedName) : TEXT(""));

	EmitSuccessJson(Paths, ClassName, Written, OutJsonString,
		bNameNormalized ? RequestedName : FString());

	if (Spec.Properties.Num() > 0 || Spec.Functions.Num() > 0)
	{
		TSharedPtr<FJsonObject> R;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(OutJsonString);
		if (FJsonSerializer::Deserialize(Reader, R) && R.IsValid())
		{
			R->SetNumberField(TEXT("inline_properties"), Spec.Properties.Num());
			R->SetNumberField(TEXT("inline_functions"),  Spec.Functions.Num());
			OutJsonString.Reset();
			TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
			FJsonSerializer::Serialize(R.ToSharedRef(), Writer);
		}
	}
}

void HandleCreateUClassFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	DoCreateUClass(Args,  TEXT("UObject"),
		 false,  false,
		OutJsonString, OutError);
}

void HandleCreateActorFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	DoCreateUClass(Args,  TEXT("AActor"),
		 true,  false,
		OutJsonString, OutError);
}

void HandleCreateActorComponentFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	DoCreateUClass(Args,  TEXT("UActorComponent"),
		 false,  true,
		OutJsonString, OutError);
}

void HandleCreateUStructFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { WriteJsonError(TEXT("Invalid args"), OutJsonString, OutError); return; }

	FString RequestedName;
	Args->TryGetStringField(TEXT("name"),        RequestedName);
	if (RequestedName.IsEmpty()) Args->TryGetStringField(TEXT("struct_name"), RequestedName);
	if (RequestedName.IsEmpty()) { WriteJsonError(TEXT("Missing required parameter: name"), OutJsonString, OutError); return; }

	FString StructName;
	bool bNameNormalized = false;
	{
		FString NormalizeError;
		if (!NormalizeTypeName(RequestedName, TEXT('F'), TEXT("USTRUCT"),
			StructName, bNameNormalized, NormalizeError))
		{
			WriteJsonError(NormalizeError, OutJsonString, OutError);
			return;
		}
	}

	bool bBlueprintType = true; Args->TryGetBoolField(TEXT("blueprint_type"), bBlueprintType);
	FString Description; Args->TryGetStringField(TEXT("description"), Description);

	CppCodegen::FUStructSpec Spec;
	Spec.StructName     = StructName;
	Spec.bBlueprintType = bBlueprintType;
	Spec.Description    = Description;

	const TArray<TSharedPtr<FJsonValue>>* MembersArr = nullptr;
	if (Args->TryGetArrayField(TEXT("members"), MembersArr) && MembersArr)
	{
		for (const TSharedPtr<FJsonValue>& V : *MembersArr)
		{
			TSharedPtr<FJsonObject> M = V->AsObject();
			if (!M.IsValid()) continue;
			CppCodegen::FUStructMemberSpec MS;
			M->TryGetStringField(TEXT("type"),          MS.Type);
			M->TryGetStringField(TEXT("name"),          MS.Name);
			M->TryGetStringField(TEXT("default_value"), MS.DefaultValue);
			M->TryGetStringField(TEXT("specifiers"),    MS.Specifiers);
			M->TryGetStringField(TEXT("comment"),       MS.Comment);
			if (!MS.Type.IsEmpty() && !MS.Name.IsEmpty())
			{
				Spec.Members.Add(MoveTemp(MS));
			}
		}
	}

	const TArray<ModuleResolver::FModuleInfo> Modules = ModuleResolver::EnumerateProjectModules();
	FResolvedPaths Paths;
	if (!ResolveTargetPaths(Args, StructName,  false, Modules, Paths, OutError))
	{
		WriteJsonError(OutError, OutJsonString, OutError);
		return;
	}
	Spec.ApiMacro = ModuleResolver::GetApiMacro(Paths.Module->Name);

	const FString HeaderText = CppCodegen::EmitUStructHeader(Spec);
	if (!WriteFileWithValidation(Paths.HeaderAbsPath, HeaderText,  true, OutError))
	{
		WriteJsonError(OutError, OutJsonString, OutError);
		return;
	}

	TArray<FString> Written; Written.Add(Paths.HeaderAbsPath);
	UE_LOG(LogUECPCppExt, Log, TEXT("create_ustruct: wrote %s"), *Paths.HeaderAbsPath);
	EmitSuccessJson(Paths, StructName, Written, OutJsonString,
		bNameNormalized ? RequestedName : FString());
}

void HandleCreateUEnumFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { WriteJsonError(TEXT("Invalid args"), OutJsonString, OutError); return; }

	FString RequestedName;
	Args->TryGetStringField(TEXT("name"),      RequestedName);
	if (RequestedName.IsEmpty()) Args->TryGetStringField(TEXT("enum_name"), RequestedName);
	if (RequestedName.IsEmpty()) { WriteJsonError(TEXT("Missing required parameter: name"), OutJsonString, OutError); return; }

	FString EnumName;
	bool bNameNormalized = false;
	{
		FString NormalizeError;
		if (!NormalizeTypeName(RequestedName, TEXT('E'), TEXT("UENUM"),
			EnumName, bNameNormalized, NormalizeError))
		{
			WriteJsonError(NormalizeError, OutJsonString, OutError);
			return;
		}
	}

	bool bBlueprintType = true; Args->TryGetBoolField(TEXT("blueprint_type"), bBlueprintType);
	FString UnderlyingType, Description;
	Args->TryGetStringField(TEXT("underlying_type"), UnderlyingType);
	Args->TryGetStringField(TEXT("description"),     Description);

	CppCodegen::FUEnumSpec Spec;
	Spec.EnumName       = EnumName;
	Spec.UnderlyingType = UnderlyingType;
	Spec.bBlueprintType = bBlueprintType;
	Spec.Description    = Description;

	const TArray<TSharedPtr<FJsonValue>>* ValuesArr = nullptr;
	if (Args->TryGetArrayField(TEXT("values"), ValuesArr) && ValuesArr)
	{
		for (const TSharedPtr<FJsonValue>& V : *ValuesArr)
		{
			CppCodegen::FUEnumValueSpec EVS;
			if (V->Type == EJson::String)
			{
				EVS.Name = V->AsString();
			}
			else if (TSharedPtr<FJsonObject> O = V->AsObject())
			{
				O->TryGetStringField(TEXT("name"),         EVS.Name);
				O->TryGetStringField(TEXT("display_name"), EVS.DisplayName);
				O->TryGetStringField(TEXT("comment"),      EVS.Comment);
			}
			if (!EVS.Name.IsEmpty()) Spec.Values.Add(MoveTemp(EVS));
		}
	}
	if (Spec.Values.Num() == 0)
	{
		WriteJsonError(TEXT("create_uenum requires at least one entry in values[]"), OutJsonString, OutError);
		return;
	}

	const TArray<ModuleResolver::FModuleInfo> Modules = ModuleResolver::EnumerateProjectModules();
	FResolvedPaths Paths;
	if (!ResolveTargetPaths(Args, EnumName,  false, Modules, Paths, OutError))
	{
		WriteJsonError(OutError, OutJsonString, OutError);
		return;
	}

	const FString HeaderText = CppCodegen::EmitUEnumHeader(Spec);
	if (!WriteFileWithValidation(Paths.HeaderAbsPath, HeaderText,  true, OutError))
	{
		WriteJsonError(OutError, OutJsonString, OutError);
		return;
	}

	TArray<FString> Written; Written.Add(Paths.HeaderAbsPath);
	UE_LOG(LogUECPCppExt, Log, TEXT("create_uenum: wrote %s"), *Paths.HeaderAbsPath);
	EmitSuccessJson(Paths, EnumName, Written, OutJsonString,
		bNameNormalized ? RequestedName : FString());
}

void HandleCreateUInterfaceFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { WriteJsonError(TEXT("Invalid args"), OutJsonString, OutError); return; }

	FString RequestedName;
	Args->TryGetStringField(TEXT("name"),           RequestedName);
	if (RequestedName.IsEmpty()) Args->TryGetStringField(TEXT("interface_name"), RequestedName);
	if (RequestedName.IsEmpty()) { WriteJsonError(TEXT("Missing required parameter: name"), OutJsonString, OutError); return; }

	FString InterfaceName;
	bool bNameNormalized = false;
	{
		FString NormalizeError;
		if (!NormalizeTypeName(RequestedName, TEXT('I'), TEXT("UINTERFACE"),
			InterfaceName, bNameNormalized, NormalizeError))
		{
			WriteJsonError(NormalizeError, OutJsonString, OutError);
			return;
		}
	}

	bool bMinimalAPI = false; Args->TryGetBoolField(TEXT("minimal_api"), bMinimalAPI);
	FString Description; Args->TryGetStringField(TEXT("description"), Description);

	CppCodegen::FUInterfaceSpec Spec;
	Spec.InterfaceName = InterfaceName;
	Spec.Description   = Description;
	Spec.bMinimalAPI   = bMinimalAPI;

	const TArray<TSharedPtr<FJsonValue>>* MethodsArr = nullptr;
	if (Args->TryGetArrayField(TEXT("methods"), MethodsArr) && MethodsArr)
	{
		for (const TSharedPtr<FJsonValue>& V : *MethodsArr)
		{
			TSharedPtr<FJsonObject> M = V->AsObject();
			if (!M.IsValid()) continue;
			CppCodegen::FUInterfaceMethodSpec MS;
			M->TryGetStringField(TEXT("name"),        MS.Name);
			M->TryGetStringField(TEXT("return_type"), MS.ReturnType);
			M->TryGetStringField(TEXT("params"),      MS.Params);
			M->TryGetStringField(TEXT("specifiers"),  MS.Specifiers);
			M->TryGetStringField(TEXT("comment"),     MS.Comment);
			if (!MS.Name.IsEmpty()) Spec.Methods.Add(MoveTemp(MS));
		}
	}

	const TArray<ModuleResolver::FModuleInfo> Modules = ModuleResolver::EnumerateProjectModules();
	FResolvedPaths Paths;
	if (!ResolveTargetPaths(Args, InterfaceName,  true, Modules, Paths, OutError))
	{
		WriteJsonError(OutError, OutJsonString, OutError);
		return;
	}
	Spec.ApiMacro = ModuleResolver::GetApiMacro(Paths.Module->Name);

	const FString HeaderText = CppCodegen::EmitUInterfaceHeader(Spec);
	if (!WriteFileWithValidation(Paths.HeaderAbsPath, HeaderText,  true, OutError))
	{
		WriteJsonError(OutError, OutJsonString, OutError);
		return;
	}

	const FString SourceText = CppCodegen::EmitUInterfaceSource(Spec);
	if (!WriteFileWithValidation(Paths.SourceAbsPath, SourceText,  false, OutError))
	{
		IFileManager::Get().Delete(*Paths.HeaderAbsPath);
		WriteJsonError(OutError, OutJsonString, OutError);
		return;
	}

	TArray<FString> Written; Written.Add(Paths.HeaderAbsPath); Written.Add(Paths.SourceAbsPath);
	UE_LOG(LogUECPCppExt, Log, TEXT("create_uinterface: wrote %d file(s) for %s"), Written.Num(), *InterfaceName);
	EmitSuccessJson(Paths, InterfaceName, Written, OutJsonString,
		bNameNormalized ? RequestedName : FString());
}

}
