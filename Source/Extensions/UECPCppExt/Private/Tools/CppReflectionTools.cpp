// Copyright 2026, BlueprintsLab, All rights reserved

#include "Tools/CppReflectionTools.h"

#include "CppCodegen.h"
#include "CppValidation.h"
#include "HeaderScanner.h"
#include "UECPCppExtModule.h"

#include "Tools/BatchToolHelper.h"

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace CppReflectionTools
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

	FString ResolveAbsolutePath(const FString& In)
	{
		if (FPaths::IsRelative(In))
		{
			return FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), In);
		}
		return In;
	}

	bool LoadHeader(const FString& AbsPath, FString& OutContent, FString& OutError)
	{
		if (!FPaths::FileExists(AbsPath))
		{
			OutError = FString::Printf(TEXT("File not found: %s"), *AbsPath);
			return false;
		}
		const FString Ext = FPaths::GetExtension(AbsPath).ToLower();
		if (Ext != TEXT("h") && Ext != TEXT("hpp"))
		{
			OutError = FString::Printf(TEXT("Reflection edits target headers only (got .%s)"), *Ext);
			return false;
		}
		if (!FFileHelper::LoadFileToString(OutContent, *AbsPath))
		{
			OutError = FString::Printf(TEXT("Failed to read: %s"), *AbsPath);
			return false;
		}
		return true;
	}

	bool SaveHeaderValidated(const FString& AbsPath, FString Content, FString& OutError)
	{
		const FString FileName = FPaths::GetCleanFilename(AbsPath);
		CppValidation::FValidationResult V = CppValidation::ValidateHeader(Content, FileName);
		if (V.Errors.Num() > 0)
		{
			OutError = FString::Printf(TEXT("Edited content failed UE validation: %s"),
				*FString::Join(V.Errors, TEXT("; ")));
			return false;
		}
		if (!V.FixedContent.IsEmpty()) Content = V.FixedContent;

		if (!FFileHelper::SaveStringToFile(Content, *AbsPath))
		{
			OutError = FString::Printf(TEXT("Failed to write: %s"), *AbsPath);
			return false;
		}
		return true;
	}

	FString ResolvePairedSourcePath(const FString& HeaderAbsPath)
	{
		FString Dir  = FPaths::GetPath(HeaderAbsPath);
		FString Tail = FPaths::GetBaseFilename(HeaderAbsPath) + TEXT(".cpp");

		FString MaybePrivate = Dir;
		const int32 PubIdx = MaybePrivate.Find(TEXT("/Public/"), ESearchCase::IgnoreCase, ESearchDir::FromEnd);
		if (PubIdx != INDEX_NONE)
		{
			MaybePrivate = MaybePrivate.Left(PubIdx) + TEXT("/Private/") + MaybePrivate.Mid(PubIdx + 8);
		}
		return FPaths::Combine(MaybePrivate, Tail);
	}

	FString InsertBeforeClassClose(const FString& Original, const HeaderScanner::FClassBlock& Class, const FString& Snippet)
	{
		const int32 BraceClose = Class.BodyEndOffset;
		check(BraceClose != INDEX_NONE);

		int32 InsertAt = BraceClose;
		while (InsertAt > 0 && (Original[InsertAt - 1] == TEXT('\t') || Original[InsertAt - 1] == TEXT(' ')))
		{
			--InsertAt;
		}

		FString Result = Original.Left(InsertAt);
		if (Result.Len() > 0 && Result[Result.Len() - 1] != TEXT('\n'))
		{
			Result += TEXT("\n");
		}
		int32 ScanBack = Result.Len() - 2;
		while (ScanBack >= 0 && (Result[ScanBack] == TEXT('\t') || Result[ScanBack] == TEXT(' '))) --ScanBack;
		const bool bPrevLineEmpty = (ScanBack < 0) || (Result[ScanBack] == TEXT('\n'));
		if (!bPrevLineEmpty)
		{
			Result += TEXT("\n");
		}
		Result += Snippet;
		if (!Result.EndsWith(TEXT("\n"))) Result += TEXT("\n");
		Result += Original.Mid(InsertAt);
		return Result;
	}
}

void HandleAddUPropertyFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { WriteJsonError(TEXT("Invalid args"), OutJsonString, OutError); return; }

	FString FilePath, ClassName;
	Args->TryGetStringField(TEXT("file_path"),  FilePath);
	Args->TryGetStringField(TEXT("class_name"), ClassName);
	if (FilePath.IsEmpty()) { WriteJsonError(TEXT("Missing required parameter: file_path"), OutJsonString, OutError); return; }

	const TArray<TSharedPtr<FJsonValue>>* ItemsArr = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("properties"), ItemsArr))
	{
		const FString AbsPath = ResolveAbsolutePath(FilePath);
		FString Content;
		if (!LoadHeader(AbsPath, Content, OutError)) { WriteJsonError(OutError, OutJsonString, OutError); return; }

		HeaderScanner::FParseResult Parsed = HeaderScanner::ParseHeader(Content);
		const HeaderScanner::FClassBlock* Block = nullptr;
		if (!ClassName.IsEmpty())
		{
			Block = HeaderScanner::FindClass(Parsed, ClassName);
			if (!Block) { WriteJsonError(FString::Printf(TEXT("Class '%s' not found in %s"), *ClassName, *AbsPath), OutJsonString, OutError); return; }
		}
		else
		{
			for (const HeaderScanner::FClassBlock& B : Parsed.Classes)
			{
				if (B.Kind != TEXT("enum")) { Block = &B; break; }
			}
			if (!Block) { WriteJsonError(TEXT("Header has no UCLASS/USTRUCT/UINTERFACE; only an enum was found"), OutJsonString, OutError); return; }
		}
		if (Block->Kind == TEXT("enum"))
		{
			WriteJsonError(TEXT("UPROPERTY can't be added to a UENUM"), OutJsonString, OutError);
			return;
		}

		BatchToolHelper::FBatchResultBuilder Batch;
		FString CombinedSnippet;
		TSet<FString> ExistingNames;
		for (const HeaderScanner::FUPropertyInfo& P : Block->Properties) ExistingNames.Add(P.Name);
		TSet<FString> InBatchNames;

		for (int32 i = 0; i < ItemsArr->Num(); ++i)
		{
			const TSharedPtr<FJsonObject> Item = (*ItemsArr)[i].IsValid() ? (*ItemsArr)[i]->AsObject() : nullptr;
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }

			const FString IName = BatchToolHelper::GetItemString(Item, TEXT("name"));
			const FString IType = BatchToolHelper::GetItemString(Item, TEXT("type"));
			if (IName.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing name")); continue; }
			if (IType.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing type")); continue; }
			if (ExistingNames.Contains(IName))
			{
				Batch.AddFailure(i, FString::Printf(TEXT("UPROPERTY '%s' already exists in '%s'"), *IName, *Block->Name));
				continue;
			}
			if (InBatchNames.Contains(IName))
			{
				Batch.AddFailure(i, FString::Printf(TEXT("Duplicate name '%s' within this batch"), *IName));
				continue;
			}

			CppCodegen::FAddUPropertySpec PS;
			PS.Type         = IType;
			PS.Name         = IName;
			PS.Specifiers   = BatchToolHelper::GetItemString(Item, TEXT("specifiers"));
			PS.DefaultValue = BatchToolHelper::GetItemString(Item, TEXT("default_value"));
			PS.Comment      = BatchToolHelper::GetItemString(Item, TEXT("comment"));
			CombinedSnippet += CppCodegen::EmitUProperty(PS);

			InBatchNames.Add(IName);

			TSharedPtr<FJsonObject> Extra = MakeShared<FJsonObject>();
			Extra->SetStringField(TEXT("name"), IName);
			Extra->SetStringField(TEXT("type"), IType);
			Batch.AddSuccess(i, Extra);
		}

		if (CombinedSnippet.IsEmpty())
		{
			Batch.Finalize(OutJsonString);
			OutError = TEXT("No valid properties to add in batch");
			return;
		}

		const FString NewContent = InsertBeforeClassClose(Content, *Block, CombinedSnippet);
		if (!SaveHeaderValidated(AbsPath, NewContent, OutError)) { WriteJsonError(OutError, OutJsonString, OutError); return; }

		UE_LOG(LogUECPCppExt, Log, TEXT("add_uproperty[batch]: %s += %d (failed %d) -> %s"),
			*Block->Name, Batch.Completed, Batch.Failed, *AbsPath);

		Batch.Finalize(OutJsonString);
		return;
	}

	FString Name, Type;
	Args->TryGetStringField(TEXT("name"),       Name);
	Args->TryGetStringField(TEXT("type"),       Type);

	if (Name.IsEmpty()) { WriteJsonError(TEXT("Missing required parameter: name (or pass items=[{name,type},...] for batch)"), OutJsonString, OutError); return; }
	if (Type.IsEmpty()) { WriteJsonError(TEXT("Missing required parameter: type"), OutJsonString, OutError); return; }

	FString Specifiers, DefaultValue, Comment;
	Args->TryGetStringField(TEXT("specifiers"),    Specifiers);
	Args->TryGetStringField(TEXT("default_value"), DefaultValue);
	Args->TryGetStringField(TEXT("comment"),       Comment);

	const FString AbsPath = ResolveAbsolutePath(FilePath);
	FString Content;
	if (!LoadHeader(AbsPath, Content, OutError)) { WriteJsonError(OutError, OutJsonString, OutError); return; }

	HeaderScanner::FParseResult Parsed = HeaderScanner::ParseHeader(Content);
	if (Parsed.Classes.Num() == 0)
	{
		WriteJsonError(FString::Printf(TEXT("No UCLASS/USTRUCT/UENUM/UINTERFACE found in %s"), *AbsPath), OutJsonString, OutError);
		return;
	}

	const HeaderScanner::FClassBlock* Block = nullptr;
	if (!ClassName.IsEmpty())
	{
		Block = HeaderScanner::FindClass(Parsed, ClassName);
		if (!Block)
		{
			WriteJsonError(FString::Printf(TEXT("Class '%s' not found in %s"), *ClassName, *AbsPath), OutJsonString, OutError);
			return;
		}
	}
	else
	{
		for (const HeaderScanner::FClassBlock& B : Parsed.Classes)
		{
			if (B.Kind != TEXT("enum")) { Block = &B; break; }
		}
		if (!Block)
		{
			WriteJsonError(TEXT("Header has no UCLASS/USTRUCT/UINTERFACE; only an enum was found"), OutJsonString, OutError);
			return;
		}
	}

	if (Block->Kind == TEXT("enum"))
	{
		WriteJsonError(TEXT("UPROPERTY can't be added to a UENUM. Use add_uenum_value or edit the enum body directly."), OutJsonString, OutError);
		return;
	}

	for (const HeaderScanner::FUPropertyInfo& P : Block->Properties)
	{
		if (P.Name == Name)
		{
			WriteJsonError(FString::Printf(
				TEXT("UPROPERTY '%s' already exists in class '%s' (declared as %s). "
				     "Use edit_cpp_file if you need to change it."),
				*Name, *Block->Name, *P.Type),
				OutJsonString, OutError);
			return;
		}
	}

	CppCodegen::FAddUPropertySpec Spec;
	Spec.Type         = Type;
	Spec.Name         = Name;
	Spec.Specifiers   = Specifiers;
	Spec.DefaultValue = DefaultValue;
	Spec.Comment      = Comment;
	const FString Snippet = CppCodegen::EmitUProperty(Spec);

	const FString NewContent = InsertBeforeClassClose(Content, *Block, Snippet);
	if (!SaveHeaderValidated(AbsPath, NewContent, OutError)) { WriteJsonError(OutError, OutJsonString, OutError); return; }

	UE_LOG(LogUECPCppExt, Log, TEXT("add_uproperty: %s::%s (%s) -> %s"), *Block->Name, *Name, *Type, *AbsPath);

	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetBoolField  (TEXT("success"),    true);
	R->SetStringField(TEXT("file_path"),  AbsPath);
	R->SetStringField(TEXT("class_name"), Block->Name);
	R->SetStringField(TEXT("name"),       Name);
	R->SetStringField(TEXT("type"),       Type);
	R->SetStringField(TEXT("message"),    FString::Printf(TEXT("Added UPROPERTY %s %s to class %s"), *Type, *Name, *Block->Name));
	R->SetStringField(TEXT("next_step"),  TEXT("Call compile_project to pick up the new property."));
	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(R.ToSharedRef(), W);
}

void HandleAddUFunctionFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { WriteJsonError(TEXT("Invalid args"), OutJsonString, OutError); return; }

	FString FilePath, ClassName;
	Args->TryGetStringField(TEXT("file_path"),  FilePath);
	Args->TryGetStringField(TEXT("class_name"), ClassName);
	if (FilePath.IsEmpty()) { WriteJsonError(TEXT("Missing required parameter: file_path"), OutJsonString, OutError); return; }

	const TArray<TSharedPtr<FJsonValue>>* ItemsArr = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("functions"), ItemsArr))
	{
		const FString AbsPath = ResolveAbsolutePath(FilePath);
		FString Content;
		if (!LoadHeader(AbsPath, Content, OutError)) { WriteJsonError(OutError, OutJsonString, OutError); return; }

		HeaderScanner::FParseResult Parsed = HeaderScanner::ParseHeader(Content);
		const HeaderScanner::FClassBlock* Block = nullptr;
		if (!ClassName.IsEmpty())
		{
			Block = HeaderScanner::FindClass(Parsed, ClassName);
			if (!Block) { WriteJsonError(FString::Printf(TEXT("Class '%s' not found in %s"), *ClassName, *AbsPath), OutJsonString, OutError); return; }
		}
		else
		{
			for (const HeaderScanner::FClassBlock& B : Parsed.Classes)
			{
				if (B.Kind == TEXT("class") || B.Kind == TEXT("struct"))
				{
					if (Block == nullptr || (Block->Name.StartsWith(TEXT("U")) && B.Name.StartsWith(TEXT("I"))))
						Block = &B;
				}
			}
			if (!Block) { WriteJsonError(TEXT("Header has no UCLASS/USTRUCT/UINTERFACE"), OutJsonString, OutError); return; }
		}

		BatchToolHelper::FBatchResultBuilder Batch;
		FString CombinedDecls;
		FString CombinedDefs;
		TSet<FString> ExistingNames;
		for (const HeaderScanner::FUFunctionInfo& F : Block->Functions) ExistingNames.Add(F.Name);
		TSet<FString> InBatchNames;
		TArray<CppCodegen::FAddUFunctionSpec> AcceptedSpecs;

		for (int32 i = 0; i < ItemsArr->Num(); ++i)
		{
			const TSharedPtr<FJsonObject> Item = (*ItemsArr)[i].IsValid() ? (*ItemsArr)[i]->AsObject() : nullptr;
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }

			const FString IName = BatchToolHelper::GetItemString(Item, TEXT("name"));
			if (IName.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing name")); continue; }
			if (ExistingNames.Contains(IName))
			{
				Batch.AddFailure(i, FString::Printf(TEXT("UFUNCTION '%s' already exists on '%s'"), *IName, *Block->Name));
				continue;
			}
			if (InBatchNames.Contains(IName))
			{
				Batch.AddFailure(i, FString::Printf(TEXT("Duplicate name '%s' within this batch"), *IName));
				continue;
			}

			CppCodegen::FAddUFunctionSpec FS;
			FS.Name       = IName;
			FS.ReturnType = BatchToolHelper::GetItemString(Item, TEXT("return_type"));
			FS.Params     = BatchToolHelper::GetItemString(Item, TEXT("params"));
			FS.Specifiers = BatchToolHelper::GetItemString(Item, TEXT("specifiers"));
			FS.Comment    = BatchToolHelper::GetItemString(Item, TEXT("comment"));
			Item->TryGetBoolField(TEXT("virtual"), FS.bVirtual);
			Item->TryGetBoolField(TEXT("static"),  FS.bStatic);
			Item->TryGetBoolField(TEXT("const"),   FS.bConst);

			CombinedDecls += CppCodegen::EmitUFunctionDeclaration(FS);
			CombinedDefs  += FString(TEXT("\n")) + CppCodegen::EmitUFunctionDefinition(Block->Name, FS);
			InBatchNames.Add(IName);
			AcceptedSpecs.Add(FS);

			TSharedPtr<FJsonObject> Extra = MakeShared<FJsonObject>();
			Extra->SetStringField(TEXT("name"), IName);
			Batch.AddSuccess(i, Extra);
		}

		if (CombinedDecls.IsEmpty())
		{
			Batch.Finalize(OutJsonString);
			OutError = TEXT("No valid functions to add in batch");
			return;
		}

		const FString NewContent = InsertBeforeClassClose(Content, *Block, CombinedDecls);
		if (!SaveHeaderValidated(AbsPath, NewContent, OutError)) { WriteJsonError(OutError, OutJsonString, OutError); return; }

		bool bSourceUpdated = false;
		const FString SourceAbs = ResolvePairedSourcePath(AbsPath);
		if (FPaths::FileExists(SourceAbs))
		{
			FString SourceContent;
			if (FFileHelper::LoadFileToString(SourceContent, *SourceAbs))
			{
				SourceContent.RemoveFromEnd(TEXT("\n"));
				SourceContent += TEXT("\n") + CombinedDefs;
				if (FFileHelper::SaveStringToFile(SourceContent, *SourceAbs))
				{
					bSourceUpdated = true;
				}
			}
		}

		UE_LOG(LogUECPCppExt, Log, TEXT("add_ufunction[batch]: %s += %d (failed %d) -> %s%s"),
			*Block->Name, Batch.Completed, Batch.Failed, *AbsPath, bSourceUpdated ? TEXT(" + stubs in .cpp") : TEXT(""));

		Batch.Finalize(OutJsonString);
		TSharedPtr<FJsonObject> RBatch;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(OutJsonString);
		if (FJsonSerializer::Deserialize(Reader, RBatch) && RBatch.IsValid())
		{
			RBatch->SetStringField(TEXT("file_path"), AbsPath);
			RBatch->SetStringField(TEXT("class_name"), Block->Name);
			RBatch->SetBoolField  (TEXT("source_updated"), bSourceUpdated);
			if (bSourceUpdated) RBatch->SetStringField(TEXT("source_path"), SourceAbs);
			OutJsonString.Reset();
			TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
			FJsonSerializer::Serialize(RBatch.ToSharedRef(), Writer);
		}
		return;
	}

	FString Name, ReturnType, Params, Specifiers, Comment;
	Args->TryGetStringField(TEXT("name"),        Name);
	Args->TryGetStringField(TEXT("return_type"), ReturnType);
	Args->TryGetStringField(TEXT("params"),      Params);
	Args->TryGetStringField(TEXT("specifiers"),  Specifiers);
	Args->TryGetStringField(TEXT("comment"),     Comment);

	bool bVirtual = false; Args->TryGetBoolField(TEXT("virtual"), bVirtual);
	bool bStatic  = false; Args->TryGetBoolField(TEXT("static"),  bStatic);
	bool bConst   = false; Args->TryGetBoolField(TEXT("const"),   bConst);

	if (Name.IsEmpty()) { WriteJsonError(TEXT("Missing required parameter: name (or pass items=[{name,...},...] for batch)"), OutJsonString, OutError); return; }

	const FString AbsPath = ResolveAbsolutePath(FilePath);
	FString Content;
	if (!LoadHeader(AbsPath, Content, OutError)) { WriteJsonError(OutError, OutJsonString, OutError); return; }

	HeaderScanner::FParseResult Parsed = HeaderScanner::ParseHeader(Content);
	if (Parsed.Classes.Num() == 0)
	{
		WriteJsonError(FString::Printf(TEXT("No UCLASS/USTRUCT/UINTERFACE found in %s"), *AbsPath), OutJsonString, OutError);
		return;
	}

	const HeaderScanner::FClassBlock* Block = nullptr;
	if (!ClassName.IsEmpty())
	{
		Block = HeaderScanner::FindClass(Parsed, ClassName);
		if (!Block)
		{
			WriteJsonError(FString::Printf(TEXT("Class '%s' not found in %s"), *ClassName, *AbsPath), OutJsonString, OutError);
			return;
		}
	}
	else
	{
		for (const HeaderScanner::FClassBlock& B : Parsed.Classes)
		{
			if (B.Kind == TEXT("class") || B.Kind == TEXT("struct"))
			{
				if (Block == nullptr || (Block->Name.StartsWith(TEXT("U")) && B.Name.StartsWith(TEXT("I"))))
				{
					Block = &B;
				}
			}
		}
		if (!Block)
		{
			WriteJsonError(TEXT("Header has no UCLASS/USTRUCT/UINTERFACE"), OutJsonString, OutError);
			return;
		}
	}

	for (const HeaderScanner::FUFunctionInfo& F : Block->Functions)
	{
		if (F.Name == Name)
		{
			WriteJsonError(FString::Printf(
				TEXT("UFUNCTION '%s' already exists on class '%s'. Use edit_cpp_file to change its signature."),
				*Name, *Block->Name),
				OutJsonString, OutError);
			return;
		}
	}

	CppCodegen::FAddUFunctionSpec Spec;
	Spec.ReturnType = ReturnType;
	Spec.Name       = Name;
	Spec.Params     = Params;
	Spec.Specifiers = Specifiers;
	Spec.bVirtual   = bVirtual;
	Spec.bStatic    = bStatic;
	Spec.bConst     = bConst;
	Spec.Comment    = Comment;

	const FString Snippet = CppCodegen::EmitUFunctionDeclaration(Spec);
	const FString NewContent = InsertBeforeClassClose(Content, *Block, Snippet);
	if (!SaveHeaderValidated(AbsPath, NewContent, OutError)) { WriteJsonError(OutError, OutJsonString, OutError); return; }

	const FString SourceAbs = ResolvePairedSourcePath(AbsPath);
	bool bSourceUpdated = false;
	if (FPaths::FileExists(SourceAbs))
	{
		FString SourceContent;
		if (FFileHelper::LoadFileToString(SourceContent, *SourceAbs))
		{
			const FString StubDef = FString(TEXT("\n")) + CppCodegen::EmitUFunctionDefinition(Block->Name, Spec);
			SourceContent.RemoveFromEnd(TEXT("\n"));
			SourceContent += TEXT("\n");
			SourceContent += StubDef;
			if (FFileHelper::SaveStringToFile(SourceContent, *SourceAbs))
			{
				bSourceUpdated = true;
			}
		}
	}

	UE_LOG(LogUECPCppExt, Log, TEXT("add_ufunction: %s::%s -> %s%s"),
		*Block->Name, *Name, *AbsPath, bSourceUpdated ? TEXT(" + stub def in .cpp") : TEXT(""));

	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetBoolField  (TEXT("success"),         true);
	R->SetStringField(TEXT("file_path"),       AbsPath);
	R->SetStringField(TEXT("class_name"),      Block->Name);
	R->SetStringField(TEXT("name"),            Name);
	R->SetBoolField  (TEXT("source_updated"),  bSourceUpdated);
	if (bSourceUpdated) R->SetStringField(TEXT("source_path"), SourceAbs);
	R->SetStringField(TEXT("message"), FString::Printf(
		TEXT("Added UFUNCTION %s::%s%s"),
		*Block->Name, *Name, bSourceUpdated ? TEXT(" (stub definition appended to .cpp)") : TEXT("")));
	R->SetStringField(TEXT("next_step"), TEXT("Call compile_project to verify the new function compiles."));
	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(R.ToSharedRef(), W);
}

void HandleAddMemberFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { WriteJsonError(TEXT("Invalid args"), OutJsonString, OutError); return; }

	FString FilePath, ClassName;
	Args->TryGetStringField(TEXT("file_path"),  FilePath);
	Args->TryGetStringField(TEXT("class_name"), ClassName);
	if (FilePath.IsEmpty()) { WriteJsonError(TEXT("Missing required parameter: file_path"), OutJsonString, OutError); return; }

	auto BuildMemberSnippet = [](const FString& InAccess, const FString& InType, const FString& InName,
		const FString& InDefault, const FString& InComment) -> FString
	{
		FString S;
		if (!InComment.IsEmpty()) S += FString::Printf(TEXT("\t/** %s */\n"), *InComment);
		S += FString::Printf(TEXT("%s:\n"), *InAccess);
		if (InDefault.IsEmpty())
		{
			S += FString::Printf(TEXT("\t%s %s;\n"), *InType, *InName);
		}
		else
		{
			S += FString::Printf(TEXT("\t%s %s = %s;\n"), *InType, *InName, *InDefault);
		}
		return S;
	};

	const TArray<TSharedPtr<FJsonValue>>* ItemsArr = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("members"), ItemsArr))
	{
		const FString AbsPath = ResolveAbsolutePath(FilePath);
		FString Content;
		if (!LoadHeader(AbsPath, Content, OutError)) { WriteJsonError(OutError, OutJsonString, OutError); return; }

		HeaderScanner::FParseResult Parsed = HeaderScanner::ParseHeader(Content);
		const HeaderScanner::FClassBlock* Block = nullptr;
		if (!ClassName.IsEmpty())
		{
			Block = HeaderScanner::FindClass(Parsed, ClassName);
			if (!Block) { WriteJsonError(FString::Printf(TEXT("Class '%s' not found"), *ClassName), OutJsonString, OutError); return; }
		}
		else
		{
			for (const HeaderScanner::FClassBlock& B : Parsed.Classes)
			{
				if (B.Kind != TEXT("enum")) { Block = &B; break; }
			}
			if (!Block) { WriteJsonError(TEXT("No matching UCLASS/USTRUCT/UINTERFACE"), OutJsonString, OutError); return; }
		}

		BatchToolHelper::FBatchResultBuilder Batch;
		FString Combined;
		TSet<FString> InBatchNames;

		for (int32 i = 0; i < ItemsArr->Num(); ++i)
		{
			const TSharedPtr<FJsonObject> Item = (*ItemsArr)[i].IsValid() ? (*ItemsArr)[i]->AsObject() : nullptr;
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }

			const FString IName = BatchToolHelper::GetItemString(Item, TEXT("name"));
			const FString IType = BatchToolHelper::GetItemString(Item, TEXT("type"));
			if (IName.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing name")); continue; }
			if (IType.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing type")); continue; }
			if (InBatchNames.Contains(IName))
			{
				Batch.AddFailure(i, FString::Printf(TEXT("Duplicate name '%s' within this batch"), *IName));
				continue;
			}

			FString IAccess = BatchToolHelper::GetItemString(Item, TEXT("access"));
			if (IAccess.IsEmpty()) IAccess = TEXT("private");
			Combined += BuildMemberSnippet(IAccess, IType, IName,
				BatchToolHelper::GetItemString(Item, TEXT("default_value")),
				BatchToolHelper::GetItemString(Item, TEXT("comment")));
			InBatchNames.Add(IName);

			TSharedPtr<FJsonObject> Extra = MakeShared<FJsonObject>();
			Extra->SetStringField(TEXT("name"), IName);
			Batch.AddSuccess(i, Extra);
		}

		if (Combined.IsEmpty())
		{
			Batch.Finalize(OutJsonString);
			OutError = TEXT("No valid members to add in batch");
			return;
		}

		const FString NewContent = InsertBeforeClassClose(Content, *Block, Combined);
		if (!SaveHeaderValidated(AbsPath, NewContent, OutError)) { WriteJsonError(OutError, OutJsonString, OutError); return; }

		UE_LOG(LogUECPCppExt, Log, TEXT("add_member[batch]: %s += %d (failed %d)"),
			*Block->Name, Batch.Completed, Batch.Failed);
		Batch.Finalize(OutJsonString);
		return;
	}

	FString Name, Type, DefaultValue, Comment, Access;
	Args->TryGetStringField(TEXT("name"),          Name);
	Args->TryGetStringField(TEXT("type"),          Type);
	Args->TryGetStringField(TEXT("default_value"), DefaultValue);
	Args->TryGetStringField(TEXT("comment"),       Comment);
	Args->TryGetStringField(TEXT("access"),        Access);
	if (Access.IsEmpty()) Access = TEXT("private");

	if (Name.IsEmpty()) { WriteJsonError(TEXT("Missing required parameter: name (or pass items=[{name,type},...] for batch)"), OutJsonString, OutError); return; }
	if (Type.IsEmpty()) { WriteJsonError(TEXT("Missing required parameter: type"), OutJsonString, OutError); return; }

	const FString AbsPath = ResolveAbsolutePath(FilePath);
	FString Content;
	if (!LoadHeader(AbsPath, Content, OutError)) { WriteJsonError(OutError, OutJsonString, OutError); return; }

	HeaderScanner::FParseResult Parsed = HeaderScanner::ParseHeader(Content);
	const HeaderScanner::FClassBlock* Block = nullptr;
	if (!ClassName.IsEmpty())
	{
		Block = HeaderScanner::FindClass(Parsed, ClassName);
	}
	else if (Parsed.Classes.Num() > 0)
	{
		for (const HeaderScanner::FClassBlock& B : Parsed.Classes)
		{
			if (B.Kind != TEXT("enum")) { Block = &B; break; }
		}
	}
	if (!Block)
	{
		WriteJsonError(TEXT("No matching UCLASS/USTRUCT/UINTERFACE found"), OutJsonString, OutError);
		return;
	}

	const FString Snippet = BuildMemberSnippet(Access, Type, Name, DefaultValue, Comment);
	const FString NewContent = InsertBeforeClassClose(Content, *Block, Snippet);
	if (!SaveHeaderValidated(AbsPath, NewContent, OutError)) { WriteJsonError(OutError, OutJsonString, OutError); return; }

	UE_LOG(LogUECPCppExt, Log, TEXT("add_member: %s::%s (%s, %s)"), *Block->Name, *Name, *Type, *Access);

	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetBoolField  (TEXT("success"),    true);
	R->SetStringField(TEXT("file_path"),  AbsPath);
	R->SetStringField(TEXT("class_name"), Block->Name);
	R->SetStringField(TEXT("name"),       Name);
	R->SetStringField(TEXT("message"),    FString::Printf(TEXT("Added %s member %s %s to %s"), *Access, *Type, *Name, *Block->Name));
	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(R.ToSharedRef(), W);
}

void HandleAddIncludeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { WriteJsonError(TEXT("Invalid args"), OutJsonString, OutError); return; }

	FString FilePath;
	Args->TryGetStringField(TEXT("file_path"), FilePath);
	if (FilePath.IsEmpty())
	{
		WriteJsonError(TEXT("Missing required parameter: file_path"), OutJsonString, OutError);
		return;
	}

	auto NormalizeIncludeText = [](FString In) -> FString
	{
		In.RemoveFromStart(TEXT("\""));
		In.RemoveFromEnd  (TEXT("\""));
		In.RemoveFromStart(TEXT("<"));
		In.RemoveFromEnd  (TEXT(">"));
		return In;
	};

	const TArray<TSharedPtr<FJsonValue>>* ItemsArr = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("includes"), ItemsArr))
	{
		const FString AbsPath = ResolveAbsolutePath(FilePath);
		if (!FPaths::FileExists(AbsPath))
		{
			WriteJsonError(FString::Printf(TEXT("File not found: %s"), *AbsPath), OutJsonString, OutError);
			return;
		}
		FString Content;
		if (!FFileHelper::LoadFileToString(Content, *AbsPath))
		{
			WriteJsonError(FString::Printf(TEXT("Failed to read: %s"), *AbsPath), OutJsonString, OutError);
			return;
		}

		HeaderScanner::FParseResult Parsed = HeaderScanner::ParseHeader(Content);
		TSet<FString> AlreadyHave(Parsed.Includes);

		BatchToolHelper::FBatchResultBuilder Batch;
		TArray<FString> NewIncludes;
		for (int32 i = 0; i < ItemsArr->Num(); ++i)
		{
			FString Name;
			const TSharedPtr<FJsonValue>& V = (*ItemsArr)[i];
			if (V.IsValid() && V->Type == EJson::String)
			{
				Name = V->AsString();
			}
			else if (TSharedPtr<FJsonObject> O = V.IsValid() ? V->AsObject() : nullptr)
			{
				O->TryGetStringField(TEXT("include"), Name);
				if (Name.IsEmpty()) O->TryGetStringField(TEXT("path"), Name);
			}
			Name = NormalizeIncludeText(Name);
			if (Name.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing include path")); continue; }

			if (AlreadyHave.Contains(Name))
			{
				TSharedPtr<FJsonObject> Extra = MakeShared<FJsonObject>();
				Extra->SetStringField(TEXT("include"), Name);
				Extra->SetBoolField  (TEXT("already_present"), true);
				Batch.AddSuccess(i, Extra);
				continue;
			}

			NewIncludes.Add(Name);
			AlreadyHave.Add(Name);

			TSharedPtr<FJsonObject> Extra = MakeShared<FJsonObject>();
			Extra->SetStringField(TEXT("include"), Name);
			Batch.AddSuccess(i, Extra);
		}

		if (NewIncludes.Num() == 0)
		{
			Batch.Finalize(OutJsonString);
			return;
		}

		FString Block;
		for (const FString& Inc : NewIncludes)
		{
			Block += FString::Printf(TEXT("#include \"%s\"\n"), *Inc);
		}

		int32 InsertAt = INDEX_NONE;
		if (!Parsed.GeneratedHeaderInclude.IsEmpty())
		{
			const FString GenLine = FString::Printf(TEXT("#include \"%s\""), *Parsed.GeneratedHeaderInclude);
			InsertAt = Content.Find(GenLine, ESearchCase::CaseSensitive);
		}
		if (InsertAt == INDEX_NONE && Parsed.LastIncludeEndOffset != INDEX_NONE)
		{
			InsertAt = Parsed.LastIncludeEndOffset;
			if (InsertAt < Content.Len() && Content[InsertAt] == TEXT('\n')) ++InsertAt;
		}
		if (InsertAt == INDEX_NONE)
		{
			const int32 PragmaIdx = Content.Find(TEXT("#pragma once"), ESearchCase::CaseSensitive);
			if (PragmaIdx != INDEX_NONE)
			{
				InsertAt = PragmaIdx;
				while (InsertAt < Content.Len() && Content[InsertAt] != TEXT('\n')) ++InsertAt;
				if (InsertAt < Content.Len()) ++InsertAt;
				Block = TEXT("\n") + Block;
			}
			else
			{
				InsertAt = 0;
			}
		}

		const FString NewContent = Content.Left(InsertAt) + Block + Content.Mid(InsertAt);
		if (!SaveHeaderValidated(AbsPath, NewContent, OutError)) { WriteJsonError(OutError, OutJsonString, OutError); return; }

		UE_LOG(LogUECPCppExt, Log, TEXT("add_include[batch]: %s += %d -> %s"),
			*FPaths::GetCleanFilename(AbsPath), NewIncludes.Num(), *AbsPath);
		Batch.Finalize(OutJsonString);
		return;
	}

	FString Include;
	Args->TryGetStringField(TEXT("include"), Include);
	if (Include.IsEmpty())
	{
		WriteJsonError(TEXT("Missing required parameter: include (or pass items=[...] for batch)"), OutJsonString, OutError);
		return;
	}
	Include = NormalizeIncludeText(Include);

	const FString AbsPath = ResolveAbsolutePath(FilePath);
	if (!FPaths::FileExists(AbsPath))
	{
		WriteJsonError(FString::Printf(TEXT("File not found: %s"), *AbsPath), OutJsonString, OutError);
		return;
	}

	FString Content;
	if (!FFileHelper::LoadFileToString(Content, *AbsPath))
	{
		WriteJsonError(FString::Printf(TEXT("Failed to read: %s"), *AbsPath), OutJsonString, OutError);
		return;
	}

	const HeaderScanner::FParseResult Parsed = HeaderScanner::ParseHeader(Content);
	if (Parsed.Includes.Contains(Include))
	{
		TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
		R->SetBoolField  (TEXT("success"),    true);
		R->SetStringField(TEXT("file_path"),  AbsPath);
		R->SetStringField(TEXT("include"),    Include);
		R->SetBoolField  (TEXT("changed"),    false);
		R->SetStringField(TEXT("message"),    TEXT("Include already present; no change."));
		TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJsonString);
		FJsonSerializer::Serialize(R.ToSharedRef(), W);
		return;
	}

	const FString NewLine = FString::Printf(TEXT("#include \"%s\"\n"), *Include);

	int32 InsertAt = INDEX_NONE;
	if (!Parsed.GeneratedHeaderInclude.IsEmpty())
	{
		const FString GenLine = FString::Printf(TEXT("#include \"%s\""), *Parsed.GeneratedHeaderInclude);
		InsertAt = Content.Find(GenLine, ESearchCase::CaseSensitive);
	}
	if (InsertAt == INDEX_NONE && Parsed.LastIncludeEndOffset != INDEX_NONE)
	{
		InsertAt = Parsed.LastIncludeEndOffset;
		if (InsertAt < Content.Len() && Content[InsertAt] == TEXT('\n')) ++InsertAt;
	}
	if (InsertAt == INDEX_NONE)
	{
		const int32 PragmaIdx = Content.Find(TEXT("#pragma once"), ESearchCase::CaseSensitive);
		if (PragmaIdx != INDEX_NONE)
		{
			InsertAt = PragmaIdx;
			while (InsertAt < Content.Len() && Content[InsertAt] != TEXT('\n')) ++InsertAt;
			if (InsertAt < Content.Len()) ++InsertAt;
			if (InsertAt < Content.Len() && Content[InsertAt] != TEXT('\n'))
			{
				const FString WithBlank = FString(TEXT("\n")) + NewLine;
				const FString NewContent = Content.Left(InsertAt) + WithBlank + Content.Mid(InsertAt);
				if (!SaveHeaderValidated(AbsPath, NewContent, OutError)) { WriteJsonError(OutError, OutJsonString, OutError); return; }
			}
			else
			{
				const FString NewContent = Content.Left(InsertAt) + NewLine + Content.Mid(InsertAt);
				if (!SaveHeaderValidated(AbsPath, NewContent, OutError)) { WriteJsonError(OutError, OutJsonString, OutError); return; }
			}
		}
		else
		{
			const FString NewContent = NewLine + Content;
			if (!SaveHeaderValidated(AbsPath, NewContent, OutError)) { WriteJsonError(OutError, OutJsonString, OutError); return; }
		}
	}
	else
	{
		const FString NewContent = Content.Left(InsertAt) + NewLine + Content.Mid(InsertAt);
		if (!SaveHeaderValidated(AbsPath, NewContent, OutError)) { WriteJsonError(OutError, OutJsonString, OutError); return; }
	}

	UE_LOG(LogUECPCppExt, Log, TEXT("add_include: %s -> %s"), *Include, *AbsPath);

	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetBoolField  (TEXT("success"),    true);
	R->SetStringField(TEXT("file_path"),  AbsPath);
	R->SetStringField(TEXT("include"),    Include);
	R->SetBoolField  (TEXT("changed"),    true);
	R->SetStringField(TEXT("message"),    FString::Printf(TEXT("Inserted #include \"%s\""), *Include));
	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(R.ToSharedRef(), W);
}

}
