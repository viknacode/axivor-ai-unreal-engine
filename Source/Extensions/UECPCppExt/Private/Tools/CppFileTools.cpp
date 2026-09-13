// Copyright 2026, BlueprintsLab, All rights reserved

#include "Tools/CppFileTools.h"
#include "CppValidation.h"

#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
	void WriteJsonErrorPair(const FString& ErrorMessage, FString& OutJsonString, FString& OutError)
	{
		OutError = ErrorMessage;
		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error"), ErrorMessage);
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
		FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
	}

	bool IsCppExtension(const FString& Ext)
	{
		return Ext == TEXT("h") || Ext == TEXT("cpp") || Ext == TEXT("hpp") ||
		       Ext == TEXT("c") || Ext == TEXT("cc")  || Ext == TEXT("inl");
	}

	FString ResolveAbsolutePath(const FString& InPath)
	{
		return FPaths::IsRelative(InPath)
			? FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), InPath)
			: FPaths::ConvertRelativePathToFull(InPath);
	}
}

namespace CppFileTools
{

void HandleReadCppFileFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { WriteJsonErrorPair(TEXT("Invalid arguments"), OutJsonString, OutError); return; }

	FString FilePath;
	if (!Args->TryGetStringField(TEXT("file_path"), FilePath) || FilePath.IsEmpty())
	{
		WriteJsonErrorPair(TEXT("Missing required parameter: file_path"), OutJsonString, OutError);
		return;
	}

	const FString AbsolutePath = ResolveAbsolutePath(FilePath);

	if (!FPaths::FileExists(AbsolutePath))
	{
		WriteJsonErrorPair(FString::Printf(TEXT("File not found: %s"), *AbsolutePath), OutJsonString, OutError);
		return;
	}

	const FString Extension = FPaths::GetExtension(AbsolutePath).ToLower();
	if (!IsCppExtension(Extension))
	{
		WriteJsonErrorPair(FString::Printf(TEXT("File is not a C++ source file (.h, .cpp, .hpp, .c, .cc, .inl): %s"), *Extension), OutJsonString, OutError);
		return;
	}

	FString FileContent;
	if (!FFileHelper::LoadFileToString(FileContent, *AbsolutePath))
	{
		WriteJsonErrorPair(FString::Printf(TEXT("Failed to read file: %s"), *AbsolutePath), OutJsonString, OutError);
		return;
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("file_path"), FilePath);
	Result->SetStringField(TEXT("absolute_path"), AbsolutePath);
	Result->SetStringField(TEXT("content"), FileContent);
	Result->SetNumberField(TEXT("content_length"), FileContent.Len());

	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
}

void HandleWriteCppFileFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { WriteJsonErrorPair(TEXT("Invalid arguments"), OutJsonString, OutError); return; }

	FString FilePath, FileContent;
	if (!Args->TryGetStringField(TEXT("file_path"), FilePath) || !Args->TryGetStringField(TEXT("content"), FileContent))
	{
		WriteJsonErrorPair(TEXT("Missing required parameters: file_path and content"), OutJsonString, OutError);
		return;
	}

	const FString AbsolutePath = ResolveAbsolutePath(FilePath);

	const FString Extension = FPaths::GetExtension(AbsolutePath).ToLower();
	if (!IsCppExtension(Extension))
	{
		WriteJsonErrorPair(FString::Printf(TEXT("File is not a C++ source file (.h, .cpp, .hpp, .c, .cc, .inl): %s"), *Extension), OutJsonString, OutError);
		return;
	}

	const FString ProjectDir       = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
	const FString EnginePluginsDir = FPaths::ConvertRelativePathToFull(FPaths::EnginePluginsDir());
	if (!AbsolutePath.StartsWith(ProjectDir) && !AbsolutePath.StartsWith(EnginePluginsDir))
	{
		WriteJsonErrorPair(TEXT("Cannot write files outside the project directory or engine plugins directory for security reasons."), OutJsonString, OutError);
		return;
	}

	const FString ProjectSourceDir = FPaths::Combine(ProjectDir, TEXT("Source"));
	if (AbsolutePath.StartsWith(ProjectSourceDir) && !IFileManager::Get().DirectoryExists(*ProjectSourceDir))
	{
		OutError = TEXT("Project has no Source folder - create a C++ class first from Tools menu");
		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error"), TEXT("This project does not have a Source folder. It appears to be a Blueprint-only project."));
		Result->SetStringField(TEXT("instructions"), TEXT("To add C++ code to your project, go to Tools > New C++ Class and create any class (e.g., a generic UActorComponent subclass). This will generate the Source folder and necessary project files."));
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
		FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
		return;
	}

	const FString ParentPath = FPaths::GetPath(AbsolutePath);
	if (!IFileManager::Get().DirectoryExists(*ParentPath))
	{
		IFileManager::Get().MakeDirectory(*ParentPath, true);
	}

	const FString FileName = FPaths::GetCleanFilename(AbsolutePath);
	CppValidation::FValidationResult Validation = (Extension == TEXT("h") || Extension == TEXT("hpp"))
		? CppValidation::ValidateHeader(FileContent, FileName)
		: CppValidation::ValidateSource(FileContent, FileName);

	if (Validation.Errors.Num() > 0)
	{
		const FString ErrorList = FString::Join(Validation.Errors, TEXT("\n  - "));
		OutError = FString::Printf(TEXT("Validation failed: %s"), *ErrorList);
		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("UE C++ validation failed:\n  - %s"), *ErrorList));
		if (Validation.Warnings.Num() > 0)
		{
			TArray<TSharedPtr<FJsonValue>> WarnArr;
			for (const FString& W : Validation.Warnings) WarnArr.Add(MakeShared<FJsonValueString>(W));
			Result->SetArrayField(TEXT("warnings"), WarnArr);
		}
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
		FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
		return;
	}

	if (!Validation.FixedContent.IsEmpty())
	{
		FileContent = Validation.FixedContent;
	}

	if (!FFileHelper::SaveStringToFile(FileContent, *AbsolutePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		WriteJsonErrorPair(FString::Printf(TEXT("Failed to write file: %s"), *AbsolutePath), OutJsonString, OutError);
		return;
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("file_path"), AbsolutePath);
	Result->SetStringField(TEXT("message"), FString::Printf(TEXT("Successfully wrote %d characters to file."), FileContent.Len()));
	Result->SetNumberField(TEXT("bytes_written"), FileContent.Len());

	if (Validation.Warnings.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> WarnArr;
		for (const FString& W : Validation.Warnings) WarnArr.Add(MakeShared<FJsonValueString>(W));
		Result->SetArrayField(TEXT("warnings"), WarnArr);
		Result->SetBoolField(TEXT("auto_fixed"), !Validation.FixedContent.IsEmpty());
	}

	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
}

void HandleEditCppFileFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { WriteJsonErrorPair(TEXT("Invalid arguments"), OutJsonString, OutError); return; }

	FString FilePath, OldText, NewText;
	Args->TryGetStringField(TEXT("file_path"), FilePath);
	Args->TryGetStringField(TEXT("old_text"), OldText);
	Args->TryGetStringField(TEXT("new_text"), NewText);
	bool bReplaceAll = false;
	Args->TryGetBoolField(TEXT("replace_all"), bReplaceAll);

	if (OldText.IsEmpty())
	{
		WriteJsonErrorPair(TEXT("old_text cannot be empty"), OutJsonString, OutError);
		return;
	}
	if (OldText == NewText)
	{
		WriteJsonErrorPair(TEXT("old_text and new_text are identical - nothing to change"), OutJsonString, OutError);
		return;
	}

	const FString AbsPath = ResolveAbsolutePath(FilePath);

	const FString ProjectDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
	if (!AbsPath.StartsWith(ProjectDir))
	{
		WriteJsonErrorPair(TEXT("Cannot edit files outside the project directory"), OutJsonString, OutError);
		return;
	}

	const FString Extension = FPaths::GetExtension(AbsPath).ToLower();
	if (!IsCppExtension(Extension))
	{
		WriteJsonErrorPair(FString::Printf(TEXT("Not a C++ file: .%s"), *Extension), OutJsonString, OutError);
		return;
	}

	FString Content;
	if (!FFileHelper::LoadFileToString(Content, *AbsPath))
	{
		WriteJsonErrorPair(FString::Printf(TEXT("Could not read file: %s"), *AbsPath), OutJsonString, OutError);
		return;
	}

	int32 MatchCount = 0;
	{
		int32 SearchPos = 0;
		while (true)
		{
			const int32 Found = Content.Find(OldText, ESearchCase::CaseSensitive, ESearchDir::FromStart, SearchPos);
			if (Found == INDEX_NONE) break;
			MatchCount++;
			SearchPos = Found + OldText.Len();
		}
	}

	if (MatchCount == 0)
	{
		WriteJsonErrorPair(TEXT("old_text not found in file. Read the file first to get the exact text."), OutJsonString, OutError);
		return;
	}

	if (MatchCount > 1 && !bReplaceAll)
	{
		WriteJsonErrorPair(FString::Printf(
			TEXT("old_text found %d times - not unique. Provide more context or use replace_all=true."), MatchCount),
			OutJsonString, OutError);
		return;
	}

	FString NewContent;
	if (bReplaceAll)
	{
		NewContent = Content.Replace(*OldText, *NewText);
	}
	else
	{
		const int32 Pos = Content.Find(OldText, ESearchCase::CaseSensitive);
		NewContent = Content.Left(Pos) + NewText + Content.Mid(Pos + OldText.Len());
	}

	if (!FFileHelper::SaveStringToFile(NewContent, *AbsPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		WriteJsonErrorPair(FString::Printf(TEXT("Failed to write file: %s"), *AbsPath), OutJsonString, OutError);
		return;
	}

	const int32 ChangePos = NewContent.Find(NewText, ESearchCase::CaseSensitive);
	int32 LineNumber = 1;
	for (int32 i = 0; i < FMath::Min(ChangePos, NewContent.Len()); i++)
	{
		if (NewContent[i] == TEXT('\n')) LineNumber++;
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("file_path"), AbsPath);
	Result->SetNumberField(TEXT("replacements"), bReplaceAll ? MatchCount : 1);
	Result->SetNumberField(TEXT("change_at_line"), LineNumber);
	Result->SetStringField(TEXT("message"), FString::Printf(
		TEXT("Replaced %d occurrence(s) in %s (first at line %d)"),
		bReplaceAll ? MatchCount : 1, *FPaths::GetCleanFilename(AbsPath), LineNumber));

	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
}

}
