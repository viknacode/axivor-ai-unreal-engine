// Copyright 2026, BlueprintsLab, All rights reserved

#include "Tools/CppEngineSearchTools.h"
#include "UECPCppExtModule.h"

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Internationalization/Regex.h"
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

	struct FMatch
	{
		FString Kind;
		FString TypeName;
		FString File;
		FString RelFile;
		int32   Line = 0;
	};

	void ScanHeader(const FString& AbsPath, const FString& RelBase,
	                const FString& PatternLower, TArray<FMatch>& OutMatches, int32 RemainingBudget)
	{
		if (RemainingBudget <= 0) return;

		FString Content;
		if (!FFileHelper::LoadFileToString(Content, *AbsPath)) return;

		FString Line;
		const TCHAR* Cursor = *Content;
		int32 LineNum = 0;
		while (*Cursor)
		{
			Line.Reset();
			while (*Cursor && *Cursor != TEXT('\n'))
			{
				Line.AppendChar(*Cursor);
				++Cursor;
			}
			if (*Cursor == TEXT('\n')) ++Cursor;
			++LineNum;

			const int32 CommentIdx = Line.Find(TEXT("//"), ESearchCase::CaseSensitive);
			if (CommentIdx != INDEX_NONE)
			{
				Line = Line.Left(CommentIdx);
			}

			if (!Line.Contains(TEXT("class ")) && !Line.Contains(TEXT("struct ")) && !Line.Contains(TEXT("enum ")))
			{
				continue;
			}

			static const FRegexPattern ClassPat (TEXT("\\b(class|struct)\\s+(?:[A-Z][A-Z0-9_]*_API\\s+)?([A-Z][A-Za-z0-9_]+)\\b"));
			static const FRegexPattern EnumPat  (TEXT("\\benum\\s+class\\s+([A-Z][A-Za-z0-9_]+)\\b"));

			FRegexMatcher CM(ClassPat, Line);
			while (CM.FindNext())
			{
				FString Kind     = CM.GetCaptureGroup(1);
				FString TypeName = CM.GetCaptureGroup(2);
				if (TypeName.ToLower().Contains(PatternLower))
				{
					FMatch& M = OutMatches.AddDefaulted_GetRef();
					M.Kind     = Kind;
					M.TypeName = TypeName;
					M.File     = AbsPath;
					M.RelFile  = AbsPath.RightChop(RelBase.Len());
					M.Line     = LineNum;
					if (OutMatches.Num() >= RemainingBudget) return;
				}
			}

			FRegexMatcher EM(EnumPat, Line);
			while (EM.FindNext())
			{
				FString TypeName = EM.GetCaptureGroup(1);
				if (TypeName.ToLower().Contains(PatternLower))
				{
					FMatch& M = OutMatches.AddDefaulted_GetRef();
					M.Kind     = TEXT("enum");
					M.TypeName = TypeName;
					M.File     = AbsPath;
					M.RelFile  = AbsPath.RightChop(RelBase.Len());
					M.Line     = LineNum;
					if (OutMatches.Num() >= RemainingBudget) return;
				}
			}
		}
	}

	void ScanDirectory(const FString& Root, const FString& RelBase,
	                   const FString& PatternLower, int32 MaxResults, TArray<FMatch>& OutMatches)
	{
		if (OutMatches.Num() >= MaxResults) return;
		if (!IFileManager::Get().DirectoryExists(*Root)) return;

		TArray<FString> Headers;
		IFileManager::Get().FindFilesRecursive(Headers, *Root, TEXT("*.h"), true, false, false);

		for (const FString& H : Headers)
		{
			ScanHeader(H, RelBase, PatternLower, OutMatches, MaxResults);
			if (OutMatches.Num() >= MaxResults) return;
		}
	}
}

namespace CppEngineSearchTools
{

void HandleSearchEngineSourceFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { WriteJsonErrorPair(TEXT("Invalid arguments"), OutJsonString, OutError); return; }

	FString Pattern;
	if (!Args->TryGetStringField(TEXT("pattern"), Pattern) || Pattern.IsEmpty())
	{
		WriteJsonErrorPair(TEXT("Missing required parameter: pattern"), OutJsonString, OutError);
		return;
	}

	int32 MaxResults = 20;
	double MaxResultsDouble = 0;
	if (Args->TryGetNumberField(TEXT("max_results"), MaxResultsDouble))
	{
		MaxResults = static_cast<int32>(MaxResultsDouble);
	}
	MaxResults = FMath::Clamp(MaxResults, 1, 100);

	FString Scope = TEXT("engine");
	Args->TryGetStringField(TEXT("scope"), Scope);
	Scope = Scope.ToLower();

	const FString PatternLower = Pattern.ToLower();
	const FString EngineDir    = FPaths::ConvertRelativePathToFull(FPaths::EngineDir());
	const FString ProjectDir   = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());

	UE_LOG(LogUECPCppExt, Log, TEXT("search_engine_source: pattern='%s' scope=%s max=%d"),
		*Pattern, *Scope, MaxResults);

	TArray<FMatch> Matches;

	if (Scope == TEXT("project") || Scope == TEXT("all"))
	{
		ScanDirectory(FPaths::Combine(ProjectDir, TEXT("Source")),
		              ProjectDir, PatternLower, MaxResults, Matches);
		ScanDirectory(FPaths::Combine(ProjectDir, TEXT("Plugins")),
		              ProjectDir, PatternLower, MaxResults, Matches);
	}

	if (Scope == TEXT("engine") || Scope == TEXT("all"))
	{
		ScanDirectory(FPaths::Combine(EngineDir, TEXT("Source/Runtime")),
		              EngineDir, PatternLower, MaxResults, Matches);
		ScanDirectory(FPaths::Combine(EngineDir, TEXT("Source/Editor")),
		              EngineDir, PatternLower, MaxResults, Matches);
		ScanDirectory(FPaths::Combine(EngineDir, TEXT("Source/Developer")),
		              EngineDir, PatternLower, MaxResults, Matches);
		ScanDirectory(FPaths::Combine(EngineDir, TEXT("Plugins")),
		              EngineDir, PatternLower, MaxResults, Matches);
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("pattern"), Pattern);
	Result->SetStringField(TEXT("scope"), Scope);
	Result->SetNumberField(TEXT("match_count"), Matches.Num());

	TArray<TSharedPtr<FJsonValue>> MatchArr;
	MatchArr.Reserve(Matches.Num());
	for (const FMatch& M : Matches)
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("kind"), M.Kind);
		O->SetStringField(TEXT("name"), M.TypeName);
		O->SetStringField(TEXT("file"), M.File);
		O->SetStringField(TEXT("relative_file"), M.RelFile);
		O->SetNumberField(TEXT("line"), M.Line);
		MatchArr.Add(MakeShared<FJsonValueObject>(O));
	}
	Result->SetArrayField(TEXT("matches"), MatchArr);

	if (Matches.Num() == 0)
	{
		Result->SetStringField(TEXT("message"),
			FString::Printf(TEXT("No class/struct/enum names matching '%s' found in %s scope."),
				*Pattern, *Scope));
	}
	else
	{
		Result->SetStringField(TEXT("message"),
			FString::Printf(TEXT("Found %d type declaration(s) matching '%s'."),
				Matches.Num(), *Pattern));
	}

	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
}

namespace
{
	struct FApiMatch
	{
		FString ClassName;
		FString MethodName;
		FString Signature;
		FString File;
		FString RelFile;
		int32   Line = 0;
	};

	bool IsControlFlowOrStatement(const FString& Trimmed)
	{
		const TCHAR* Prefixes[] = {
			TEXT("if "), TEXT("if("), TEXT("else"), TEXT("while "), TEXT("while("),
			TEXT("for "), TEXT("for("), TEXT("switch "), TEXT("switch("),
			TEXT("return "), TEXT("return("), TEXT("case "), TEXT("default:"),
			TEXT("do "), TEXT("do{"), TEXT("throw "), TEXT("delete ")
		};
		for (const TCHAR* P : Prefixes)
		{
			if (Trimmed.StartsWith(P)) return true;
		}
		return false;
	}

	bool IsAllCapsMacroCall(const FString& Trimmed)
	{
		if (Trimmed.IsEmpty() || !FChar::IsUpper(Trimmed[0])) return false;
		for (int32 i = 0; i < Trimmed.Len(); ++i)
		{
			const TCHAR C = Trimmed[i];
			if (C == TEXT('(') || C == TEXT(' ') || C == TEXT('\t')) return i > 0;
			if (!FChar::IsUpper(C) && !FChar::IsDigit(C) && C != TEXT('_')) return false;
		}
		return false;
	}

	void ScanHeaderForApi(const FString& AbsPath, const FString& RelBase,
		const FString& MethodPatternLower, const FString& ClassFilterLower,
		bool bExactMatch, int32 MaxResults, TArray<FApiMatch>& OutMatches)
	{
		if (OutMatches.Num() >= MaxResults) return;

		FString Content;
		if (!FFileHelper::LoadFileToString(Content, *AbsPath)) return;

		struct FScope { FString ClassName; int32 BraceDepth = 0; };
		TArray<FScope> Stack;
		int32 CurrentDepth = 0;

		static const FRegexPattern ClassBeginPat(
			TEXT("\\b(?:class|struct)\\s+(?:[A-Z][A-Z0-9_]*_API\\s+)?([A-Z][A-Za-z0-9_]+)\\b"));

		FString Line;
		const TCHAR* Cursor = *Content;
		int32 LineNum = 0;
		while (*Cursor)
		{
			Line.Reset();
			while (*Cursor && *Cursor != TEXT('\n'))
			{
				Line.AppendChar(*Cursor);
				++Cursor;
			}
			if (*Cursor == TEXT('\n')) ++Cursor;
			++LineNum;

			const int32 CommentIdx = Line.Find(TEXT("//"), ESearchCase::CaseSensitive);
			if (CommentIdx != INDEX_NONE) Line = Line.Left(CommentIdx);

			const FString Trimmed = Line.TrimStartAndEnd();
			if (Trimmed.IsEmpty()) continue;

			FRegexMatcher CBM(ClassBeginPat, Line);
			if (CBM.FindNext())
			{
				const bool bForwardDecl = !Line.Contains(TEXT("{")) && Line.Contains(TEXT(";"));
				if (!bForwardDecl)
				{
					FScope NewScope;
					NewScope.ClassName  = CBM.GetCaptureGroup(1);
					NewScope.BraceDepth = CurrentDepth;
					Stack.Push(NewScope);
				}
			}

			for (TCHAR C : Line)
			{
				if (C == TEXT('{')) ++CurrentDepth;
				else if (C == TEXT('}'))
				{
					--CurrentDepth;
					while (Stack.Num() > 0 && Stack.Top().BraceDepth >= CurrentDepth)
					{
						Stack.Pop();
					}
				}
			}

			if (IsControlFlowOrStatement(Trimmed)) continue;
			if (IsAllCapsMacroCall(Trimmed))       continue;
			if (Trimmed.StartsWith(TEXT("#")))     continue;

			const int32 ParenIdx = Line.Find(TEXT("("));
			if (ParenIdx == INDEX_NONE) continue;

			int32 NameEnd = ParenIdx - 1;
			while (NameEnd >= 0 && (Line[NameEnd] == TEXT(' ') || Line[NameEnd] == TEXT('\t'))) --NameEnd;
			int32 NameStart = NameEnd;
			while (NameStart >= 0 && (FChar::IsAlnum(Line[NameStart]) || Line[NameStart] == TEXT('_'))) --NameStart;
			++NameStart;
			if (NameStart > NameEnd) continue;

			const FString MethodName = Line.Mid(NameStart, NameEnd - NameStart + 1);
			if (MethodName.IsEmpty() || FChar::IsDigit(MethodName[0])) continue;

			const FString MethodLower = MethodName.ToLower();
			const bool bMatch = bExactMatch
				? MethodLower == MethodPatternLower
				: MethodLower.Contains(MethodPatternLower);
			if (!bMatch) continue;

			int32 BeforeName = NameStart - 1;
			while (BeforeName >= 0 && (Line[BeforeName] == TEXT(' ') || Line[BeforeName] == TEXT('\t'))) --BeforeName;
			if (BeforeName < 0) continue;
			const TCHAR Prev = Line[BeforeName];
			if (Prev == TEXT('.') || Prev == TEXT('>') || Prev == TEXT(':')) continue;

			const FString CurrentClass = Stack.Num() > 0 ? Stack.Top().ClassName : FString();
			if (!ClassFilterLower.IsEmpty())
			{
				if (CurrentClass.IsEmpty()) continue;
				if (!CurrentClass.ToLower().Contains(ClassFilterLower)) continue;
			}

			FApiMatch& M = OutMatches.AddDefaulted_GetRef();
			M.ClassName  = CurrentClass;
			M.MethodName = MethodName;
			M.Signature  = Trimmed;
			M.File       = AbsPath;
			M.RelFile    = AbsPath.RightChop(RelBase.Len());
			M.Line       = LineNum;
			if (OutMatches.Num() >= MaxResults) return;
		}
	}

	void ScanDirectoryForApi(const FString& Root, const FString& RelBase,
		const FString& MethodPatternLower, const FString& ClassFilterLower,
		bool bExactMatch, int32 MaxResults, TArray<FApiMatch>& OutMatches)
	{
		if (OutMatches.Num() >= MaxResults) return;
		if (!IFileManager::Get().DirectoryExists(*Root)) return;

		TArray<FString> Headers;
		IFileManager::Get().FindFilesRecursive(Headers, *Root, TEXT("*.h"), true, false, false);

		for (const FString& H : Headers)
		{
			ScanHeaderForApi(H, RelBase, MethodPatternLower, ClassFilterLower, bExactMatch, MaxResults, OutMatches);
			if (OutMatches.Num() >= MaxResults) return;
		}
	}
}

void HandleSearchEngineApiFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { WriteJsonErrorPair(TEXT("Invalid arguments"), OutJsonString, OutError); return; }

	FString Pattern;
	if (!Args->TryGetStringField(TEXT("pattern"), Pattern) || Pattern.IsEmpty())
	{
		WriteJsonErrorPair(TEXT("Missing required parameter: pattern"), OutJsonString, OutError);
		return;
	}

	FString ClassFilter;
	Args->TryGetStringField(TEXT("class_filter"), ClassFilter);

	int32 MaxResults = 30;
	double MaxResultsDouble = 0;
	if (Args->TryGetNumberField(TEXT("max_results"), MaxResultsDouble))
	{
		MaxResults = static_cast<int32>(MaxResultsDouble);
	}
	MaxResults = FMath::Clamp(MaxResults, 1, 200);

	bool bExactMatch = false;
	Args->TryGetBoolField(TEXT("exact_match"), bExactMatch);

	FString Scope = TEXT("engine");
	Args->TryGetStringField(TEXT("scope"), Scope);
	Scope = Scope.ToLower();

	const FString PatternLower     = Pattern.ToLower();
	const FString ClassFilterLower = ClassFilter.ToLower();
	const FString EngineDir        = FPaths::ConvertRelativePathToFull(FPaths::EngineDir());
	const FString ProjectDir       = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());

	UE_LOG(LogUECPCppExt, Log, TEXT("search_engine_api: pattern='%s' class_filter='%s' scope=%s exact=%d max=%d"),
		*Pattern, *ClassFilter, *Scope, bExactMatch ? 1 : 0, MaxResults);

	TArray<FApiMatch> Matches;

	if (Scope == TEXT("project") || Scope == TEXT("all"))
	{
		ScanDirectoryForApi(FPaths::Combine(ProjectDir, TEXT("Source")),
			ProjectDir, PatternLower, ClassFilterLower, bExactMatch, MaxResults, Matches);
		ScanDirectoryForApi(FPaths::Combine(ProjectDir, TEXT("Plugins")),
			ProjectDir, PatternLower, ClassFilterLower, bExactMatch, MaxResults, Matches);
	}

	if (Scope == TEXT("engine") || Scope == TEXT("all"))
	{
		ScanDirectoryForApi(FPaths::Combine(EngineDir, TEXT("Source/Runtime")),
			EngineDir, PatternLower, ClassFilterLower, bExactMatch, MaxResults, Matches);
		ScanDirectoryForApi(FPaths::Combine(EngineDir, TEXT("Source/Editor")),
			EngineDir, PatternLower, ClassFilterLower, bExactMatch, MaxResults, Matches);
		ScanDirectoryForApi(FPaths::Combine(EngineDir, TEXT("Source/Developer")),
			EngineDir, PatternLower, ClassFilterLower, bExactMatch, MaxResults, Matches);
		ScanDirectoryForApi(FPaths::Combine(EngineDir, TEXT("Plugins")),
			EngineDir, PatternLower, ClassFilterLower, bExactMatch, MaxResults, Matches);
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField  (TEXT("success"),     true);
	Result->SetStringField(TEXT("pattern"),     Pattern);
	if (!ClassFilter.IsEmpty()) Result->SetStringField(TEXT("class_filter"), ClassFilter);
	Result->SetStringField(TEXT("scope"),       Scope);
	Result->SetBoolField  (TEXT("exact_match"), bExactMatch);
	Result->SetNumberField(TEXT("match_count"), Matches.Num());

	TArray<TSharedPtr<FJsonValue>> MatchArr;
	MatchArr.Reserve(Matches.Num());
	for (const FApiMatch& M : Matches)
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("class_name"),    M.ClassName);
		O->SetStringField(TEXT("method_name"),   M.MethodName);
		O->SetStringField(TEXT("signature"),     M.Signature);
		O->SetStringField(TEXT("file"),          M.File);
		O->SetStringField(TEXT("relative_file"), M.RelFile);
		O->SetNumberField(TEXT("line"),          M.Line);
		MatchArr.Add(MakeShared<FJsonValueObject>(O));
	}
	Result->SetArrayField(TEXT("matches"), MatchArr);

	if (Matches.Num() == 0)
	{
		Result->SetStringField(TEXT("message"),
			FString::Printf(TEXT("No method/function declarations matching '%s' (%s scope%s)."),
				*Pattern, *Scope,
				ClassFilter.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(", class_filter='%s'"), *ClassFilter)));
	}

	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
}

}
