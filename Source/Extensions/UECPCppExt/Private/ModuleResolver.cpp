// Copyright 2026, BlueprintsLab, All rights reserved

#include "ModuleResolver.h"

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Internationalization/Regex.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace ModuleResolver
{

namespace
{
	void ExtractStringsFromArrayBlock(const FString& Block, TArray<FString>& Out)
	{
		const int32 Len = Block.Len();
		int32 i = 0;
		while (i < Len)
		{
			if (Block[i] == TEXT('"'))
			{
				const int32 Start = i + 1;
				int32 End = Start;
				while (End < Len && Block[End] != TEXT('"')) ++End;
				if (End > Start)
				{
					Out.Add(Block.Mid(Start, End - Start));
				}
				i = End + 1;
			}
			else
			{
				++i;
			}
		}
	}

	bool FindAddRangeBlock(const FString& BuildCs, const FString& Pattern, int32& OutBraceOpen, int32& OutBraceClose)
	{
		const int32 PatternIdx = BuildCs.Find(Pattern, ESearchCase::CaseSensitive);
		if (PatternIdx == INDEX_NONE) return false;

		int32 BraceOpen = BuildCs.Find(TEXT("{"), ESearchCase::CaseSensitive, ESearchDir::FromStart, PatternIdx);
		if (BraceOpen == INDEX_NONE) return false;

		int32 Depth = 1;
		int32 P = BraceOpen + 1;
		while (P < BuildCs.Len() && Depth > 0)
		{
			const TCHAR C = BuildCs[P];
			if      (C == TEXT('{')) ++Depth;
			else if (C == TEXT('}'))
			{
				--Depth;
				if (Depth == 0)
				{
					OutBraceOpen  = BraceOpen;
					OutBraceClose = P;
					return true;
				}
			}
			++P;
		}
		return false;
	}

	void ParseDeps(const FString& BuildCs, FModuleInfo& Mod)
	{
		auto Pull = [&](const TCHAR* Pattern, TArray<FString>& Dst)
		{
			int32 BO = INDEX_NONE, BC = INDEX_NONE;
			if (FindAddRangeBlock(BuildCs, Pattern, BO, BC) && BC > BO + 1)
			{
				const FString Inner = BuildCs.Mid(BO + 1, BC - BO - 1);
				ExtractStringsFromArrayBlock(Inner, Dst);
			}
		};

		Pull(TEXT("PublicDependencyModuleNames.AddRange"),  Mod.PublicDependencyModuleNames);
		Pull(TEXT("PrivateDependencyModuleNames.AddRange"), Mod.PrivateDependencyModuleNames);
		Pull(TEXT("PublicIncludePaths.AddRange"),           Mod.PublicIncludePaths);
		Pull(TEXT("PrivateIncludePaths.AddRange"),          Mod.PrivateIncludePaths);

		if      (BuildCs.Contains(TEXT(": ModuleRules")))           Mod.Type = TEXT("Runtime");
	}

	void ScanSourceTree(const FString& SourceRoot, bool bInPlugin, const FString& PluginName, TArray<FModuleInfo>& Out)
	{
		if (!IFileManager::Get().DirectoryExists(*SourceRoot)) return;

		TArray<FString> BuildCsFiles;
		IFileManager::Get().FindFilesRecursive(BuildCsFiles, *SourceRoot, TEXT("*.Build.cs"), true, false, false);

		for (const FString& CsPath : BuildCsFiles)
		{
			const FString FileBase    = FPaths::GetBaseFilename(CsPath);
			const FString ModuleName  = FileBase.LeftChop(6);
			const FString ModuleRoot  = FPaths::GetPath(CsPath);

			if (FPaths::GetCleanFilename(ModuleRoot) != ModuleName) continue;

			FString Contents;
			if (!FFileHelper::LoadFileToString(Contents, *CsPath)) continue;

			FModuleInfo M;
			M.Name        = ModuleName;
			M.BuildCsPath = CsPath;
			M.ModuleRoot  = ModuleRoot;
			M.bInPlugin   = bInPlugin;
			M.PluginName  = PluginName;
			ParseDeps(Contents, M);

			Out.Add(MoveTemp(M));
		}
	}

	FString ReadPrimaryModuleNameFromUProject()
	{
		const FString UProjectPath = FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath());
		FString Json;
		if (!FFileHelper::LoadFileToString(Json, *UProjectPath)) return FString();

		TSharedPtr<FJsonObject> Root;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid()) return FString();

		const TArray<TSharedPtr<FJsonValue>>* Modules = nullptr;
		if (!Root->TryGetArrayField(TEXT("Modules"), Modules) || !Modules || Modules->Num() == 0) return FString();

		const TSharedPtr<FJsonObject>& First = (*Modules)[0]->AsObject();
		if (!First.IsValid()) return FString();
		FString Name;
		First->TryGetStringField(TEXT("Name"), Name);
		return Name;
	}
}

TArray<FModuleInfo> EnumerateProjectModules()
{
	TArray<FModuleInfo> Out;

	const FString ProjectDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());

	ScanSourceTree(FPaths::Combine(ProjectDir, TEXT("Source")),  false, FString(), Out);

	const FString PluginsRoot = FPaths::Combine(ProjectDir, TEXT("Plugins"));
	if (IFileManager::Get().DirectoryExists(*PluginsRoot))
	{
		TArray<FString> PluginDirs;
		IFileManager::Get().FindFiles(PluginDirs, *FPaths::Combine(PluginsRoot, TEXT("*")), false, true);
		for (const FString& PluginName : PluginDirs)
		{
			const FString PluginSource = FPaths::Combine(PluginsRoot, PluginName, TEXT("Source"));
			if (IFileManager::Get().DirectoryExists(*PluginSource))
			{
				ScanSourceTree(PluginSource,  true, PluginName, Out);
			}
		}
	}

	return Out;
}

const FModuleInfo* FindPrimaryGameModule(const TArray<FModuleInfo>& Modules)
{
	const FString PrimaryName = ReadPrimaryModuleNameFromUProject();
	if (!PrimaryName.IsEmpty())
	{
		if (const FModuleInfo* M = FindModuleByName(Modules, PrimaryName))
		{
			return M;
		}
	}
	for (const FModuleInfo& M : Modules)
	{
		if (!M.bInPlugin) return &M;
	}
	return Modules.Num() > 0 ? &Modules[0] : nullptr;
}

const FModuleInfo* FindModuleByName(const TArray<FModuleInfo>& Modules, const FString& Name)
{
	for (const FModuleInfo& M : Modules)
	{
		if (M.Name == Name) return &M;
	}
	return nullptr;
}

const FModuleInfo* FindOwningModule(const TArray<FModuleInfo>& Modules, const FString& AbsoluteFilePath)
{
	const FModuleInfo* Best = nullptr;
	int32 BestLen = 0;

	const FString Normalised = FPaths::ConvertRelativePathToFull(AbsoluteFilePath);

	for (const FModuleInfo& M : Modules)
	{
		const FString RootNormalised = FPaths::ConvertRelativePathToFull(M.ModuleRoot);
		if (Normalised.StartsWith(RootNormalised + TEXT("/")) ||
			Normalised.StartsWith(RootNormalised + TEXT("\\")) ||
			Normalised.Equals(RootNormalised))
		{
			if (RootNormalised.Len() > BestLen)
			{
				Best = &M;
				BestLen = RootNormalised.Len();
			}
		}
	}
	return Best;
}

FString GetApiMacro(const FString& ModuleName)
{
	if (ModuleName.IsEmpty()) return FString();
	return ModuleName.ToUpper() + TEXT("_API");
}

FString PatchBuildCs(const FString& OriginalBuildCs, const FBuildCsPatch& Patch, bool& bChanged)
{
	bChanged = false;
	FString Out = OriginalBuildCs;

	auto ApplyPatch = [&](const TCHAR* Pattern, const TArray<FString>& Add, const TArray<FString>& Remove)
	{
		if (Add.Num() == 0 && Remove.Num() == 0) return;

		int32 BO = INDEX_NONE, BC = INDEX_NONE;
		if (!FindAddRangeBlock(Out, Pattern, BO, BC) || BC <= BO + 1) return;

		FString Inner = Out.Mid(BO + 1, BC - BO - 1);
		TArray<FString> Existing;
		ExtractStringsFromArrayBlock(Inner, Existing);

		bool bLocalChanged = false;

		for (const FString& R : Remove)
		{
			if (Existing.Remove(R) > 0) bLocalChanged = true;
		}
		for (const FString& A : Add)
		{
			if (!Existing.Contains(A)) { Existing.Add(A); bLocalChanged = true; }
		}

		if (!bLocalChanged) return;

		FString Indent = TEXT("\t\t\t");
		{
			const int32 FirstQuote = Inner.Find(TEXT("\""), ESearchCase::CaseSensitive);
			if (FirstQuote != INDEX_NONE)
			{
				int32 LineStart = FirstQuote;
				while (LineStart > 0 && Inner[LineStart - 1] != TEXT('\n')) --LineStart;
				FString Indented;
				for (int32 i = LineStart; i < FirstQuote; ++i)
				{
					const TCHAR C = Inner[i];
					if (C == TEXT('\t') || C == TEXT(' ')) Indented.AppendChar(C);
					else break;
				}
				if (!Indented.IsEmpty()) Indent = Indented;
			}
		}

		FString NewInner = TEXT("\n");
		for (int32 i = 0; i < Existing.Num(); ++i)
		{
			NewInner += Indent + FString::Printf(TEXT("\"%s\""), *Existing[i]);
			if (i + 1 < Existing.Num()) NewInner += TEXT(",");
			NewInner += TEXT("\n");
		}
		FString CloseIndent = Indent;
		if (CloseIndent.Len() > 0) CloseIndent.LeftChopInline(1, EAllowShrinking::No);
		NewInner += CloseIndent;

		Out = Out.Left(BO + 1) + NewInner + Out.Mid(BC);
		bChanged = true;
	};

	ApplyPatch(TEXT("PublicDependencyModuleNames.AddRange"),  Patch.AddPublic,  Patch.RemovePublic);
	ApplyPatch(TEXT("PrivateDependencyModuleNames.AddRange"), Patch.AddPrivate, Patch.RemovePrivate);

	return Out;
}

}
