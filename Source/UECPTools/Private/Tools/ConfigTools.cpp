// Copyright 2026, BlueprintsLab, All rights reserved

#include "Tools/ConfigTools.h"

#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace ConfigTools
{

static FString ResolveConfigFilePath(const FString& FileHint)
{
	if (FileHint.IsEmpty()) return TEXT("");

	if (FPaths::FileExists(FileHint)) return FPaths::ConvertRelativePathToFull(FileHint);

	const FString Hint = FileHint.ToLower();
	const FString ProjectConfig = FPaths::ProjectConfigDir();

	if (Hint == TEXT("defaultengine") || Hint == TEXT("engine"))
		return ProjectConfig + TEXT("DefaultEngine.ini");
	if (Hint == TEXT("defaultgame") || Hint == TEXT("game"))
		return ProjectConfig + TEXT("DefaultGame.ini");
	if (Hint == TEXT("defaultinput") || Hint == TEXT("input"))
		return ProjectConfig + TEXT("DefaultInput.ini");
	if (Hint == TEXT("defaulteditor") || Hint == TEXT("editor"))
		return ProjectConfig + TEXT("DefaultEditor.ini");
	if (Hint == TEXT("defaultgameplaytags") || Hint == TEXT("gameplaytags"))
		return ProjectConfig + TEXT("DefaultGameplayTags.ini");
	if (Hint == TEXT("defaultscalability") || Hint == TEXT("scalability"))
		return ProjectConfig + TEXT("DefaultScalability.ini");

	if (Hint.Contains(TEXT("saved")) || Hint.Contains(TEXT("merged")))
	{
		FString Platform = FPlatformProperties::PlatformName();
		FString SavedDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Config"), Platform);
		if (FileHint.Contains(TEXT("Engine"), ESearchCase::IgnoreCase))
			return SavedDir / TEXT("Engine.ini");
		if (FileHint.Contains(TEXT("Game"), ESearchCase::IgnoreCase))
			return SavedDir / TEXT("Game.ini");
		if (FileHint.Contains(TEXT("Input"), ESearchCase::IgnoreCase))
			return SavedDir / TEXT("Input.ini");
		if (FileHint.Contains(TEXT("Editor"), ESearchCase::IgnoreCase))
			return SavedDir / TEXT("Editor.ini");
	}

	FString Candidate = ProjectConfig + FileHint;
	if (!Candidate.EndsWith(TEXT(".ini"))) Candidate += TEXT(".ini");
	if (FPaths::FileExists(Candidate)) return FPaths::ConvertRelativePathToFull(Candidate);

	return TEXT("");
}

static TArray<TPair<FString, FString>> GetKnownConfigPaths()
{
	TArray<TPair<FString, FString>> Files;
	const FString PC = FPaths::ProjectConfigDir();
	const FString EngDir = FPaths::Combine(FPaths::EngineDir(), TEXT("Config"));
	const FString Platform = FPlatformProperties::PlatformName();
	const FString Saved = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Config"), Platform);

	auto Add = [&](const FString& Label, const FString& Path)
	{
		if (FPaths::FileExists(Path)) Files.Add({ Label, FPaths::ConvertRelativePathToFull(Path) });
	};

	Add(TEXT("DefaultEngine"),        PC + TEXT("DefaultEngine.ini"));
	Add(TEXT("DefaultGame"),          PC + TEXT("DefaultGame.ini"));
	Add(TEXT("DefaultInput"),         PC + TEXT("DefaultInput.ini"));
	Add(TEXT("DefaultEditor"),        PC + TEXT("DefaultEditor.ini"));
	Add(TEXT("DefaultGameplayTags"),  PC + TEXT("DefaultGameplayTags.ini"));
	Add(TEXT("DefaultScalability"),   PC + TEXT("DefaultScalability.ini"));

	Add(TEXT("BaseEngine"),  EngDir / TEXT("BaseEngine.ini"));
	Add(TEXT("BaseGame"),    EngDir / TEXT("BaseGame.ini"));
	Add(TEXT("BaseInput"),   EngDir / TEXT("BaseInput.ini"));
	Add(TEXT("BaseEditor"),  EngDir / TEXT("BaseEditor.ini"));

	Add(TEXT("Saved_Engine"),  Saved / TEXT("Engine.ini"));
	Add(TEXT("Saved_Game"),    Saved / TEXT("Game.ini"));
	Add(TEXT("Saved_Input"),   Saved / TEXT("Input.ini"));
	Add(TEXT("Saved_Editor"),  Saved / TEXT("Editor.ini"));

	return Files;
}

void HandleResolveSettingFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }

	FString Section, Key, FileHint;
	Args->TryGetStringField(TEXT("section"), Section);
	Args->TryGetStringField(TEXT("key"),     Key);
	Args->TryGetStringField(TEXT("file"),    FileHint);

	if (Key.IsEmpty()) { OutError = TEXT("Missing required parameter: key"); return; }
	if (Section.IsEmpty()) { OutError = TEXT("Missing required parameter: section"); return; }

	FString Value;
	bool bFound = false;

	if (!FileHint.IsEmpty())
	{
		const FString FilePath = ResolveConfigFilePath(FileHint);
		if (!FilePath.IsEmpty())
			bFound = GConfig->GetString(*Section, *Key, Value, FilePath);
	}

	if (!bFound)
		bFound = GConfig->GetString(*Section, *Key, Value, TEXT(""));

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), bFound);
	Result->SetStringField(TEXT("section"), Section);
	Result->SetStringField(TEXT("key"), Key);
	if (bFound)
		Result->SetStringField(TEXT("value"), Value);
	else
		Result->SetStringField(TEXT("message"), FString::Printf(TEXT("Key '%s' not found in section [%s]"), *Key, *Section));

	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
}

void HandleExplainSettingFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }

	FString Section, Key;
	Args->TryGetStringField(TEXT("section"), Section);
	Args->TryGetStringField(TEXT("key"),     Key);
	if (Key.IsEmpty()) { OutError = TEXT("Missing required parameter: key"); return; }

	TArray<TPair<FString, FString>> KnownFiles = GetKnownConfigPaths();

	TArray<TSharedPtr<FJsonValue>> Layers;
	FString EffectiveValue;
	bool bAnyFound = false;

	for (const auto& FilePair : KnownFiles)
	{
		FString Val;
		bool bFound = false;

		if (!Section.IsEmpty())
		{
			bFound = GConfig->GetString(*Section, *Key, Val, FilePair.Value);
		}
		else
		{
			if (const FConfigFile* File = GConfig->Find(FilePair.Value))
			{
				for (const auto& SectionPair : AsConst(*File))
				{
					const FConfigValue* FoundVal = SectionPair.Value.Find(FName(*Key));
					if (FoundVal) { Val = FoundVal->GetValue(); bFound = true; Section = SectionPair.Key; break; }
				}
			}
		}

		if (bFound)
		{
			TSharedPtr<FJsonObject> Layer = MakeShared<FJsonObject>();
			Layer->SetStringField(TEXT("layer"), FilePair.Key);
			Layer->SetStringField(TEXT("file"), FilePair.Value);
			Layer->SetStringField(TEXT("value"), Val);
			Layers.Add(MakeShared<FJsonValueObject>(Layer));
			EffectiveValue = Val;
			bAnyFound = true;
		}
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), bAnyFound);
	Result->SetStringField(TEXT("key"), Key);
	Result->SetStringField(TEXT("section"), Section);
	Result->SetArrayField(TEXT("layers"), Layers);
	if (bAnyFound) Result->SetStringField(TEXT("effective_value"), EffectiveValue);

	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
}

void HandleDiffConfigFromDefaultFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }

	FString FileHint, SectionFilter;
	Args->TryGetStringField(TEXT("file"), FileHint);
	Args->TryGetStringField(TEXT("section"), SectionFilter);

	if (FileHint.IsEmpty()) FileHint = TEXT("DefaultEngine");

	const FString DefaultPath = ResolveConfigFilePath(FileHint);
	if (DefaultPath.IsEmpty() || !FPaths::FileExists(DefaultPath))
	{
		OutError = FString::Printf(TEXT("Could not resolve config file: '%s'"), *FileHint);
		return;
	}

	FString BaseName = FPaths::GetBaseFilename(DefaultPath);
	BaseName.ReplaceInline(TEXT("Default"), TEXT("Base"), ESearchCase::CaseSensitive);
	const FString BasePath = FPaths::Combine(FPaths::EngineDir(), TEXT("Config"), BaseName + TEXT(".ini"));

	const FConfigFile* DefaultFile = GConfig->Find(DefaultPath);
	const FConfigFile* BaseFile    = FPaths::FileExists(BasePath) ? GConfig->Find(BasePath) : nullptr;

	TArray<TSharedPtr<FJsonValue>> Diffs;

	if (DefaultFile)
	{
		for (const auto& SectionPair : AsConst(*DefaultFile))
		{
			const FString& SName = SectionPair.Key;
			if (!SectionFilter.IsEmpty() && !SName.Contains(SectionFilter, ESearchCase::IgnoreCase)) continue;

			for (const auto& KeyPair : SectionPair.Value)
			{
				const FString KeyStr = KeyPair.Key.ToString();
				const FString ValStr = KeyPair.Value.GetValue();

				FString BaseVal;
				bool bInBase = false;
				if (BaseFile)
				{
					const FConfigSection* BaseSection = BaseFile->FindSection(*SName);
					if (BaseSection)
					{
						const FConfigValue* BV = BaseSection->Find(FName(*KeyStr));
						if (BV) { BaseVal = BV->GetValue(); bInBase = true; }
					}
				}

				if (!bInBase || BaseVal != ValStr)
				{
					TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
					Entry->SetStringField(TEXT("section"), SName);
					Entry->SetStringField(TEXT("key"), KeyStr);
					Entry->SetStringField(TEXT("value"), ValStr);
					Entry->SetStringField(TEXT("change_type"), bInBase ? TEXT("modified") : TEXT("added"));
					if (bInBase) Entry->SetStringField(TEXT("base_value"), BaseVal);
					Diffs.Add(MakeShared<FJsonValueObject>(Entry));
				}
			}
		}
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("file"), DefaultPath);
	Result->SetStringField(TEXT("compared_against"), FPaths::FileExists(BasePath) ? BasePath : TEXT("(no base found)"));
	Result->SetNumberField(TEXT("diff_count"), Diffs.Num());
	Result->SetArrayField(TEXT("diffs"), Diffs);

	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
}

void HandleSearchConfigFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }

	FString Query, FileFilter;
	Args->TryGetStringField(TEXT("query"), Query);
	if (Query.IsEmpty()) Args->TryGetStringField(TEXT("search"), Query);
	Args->TryGetStringField(TEXT("file"), FileFilter);

	if (Query.IsEmpty()) { OutError = TEXT("Missing required parameter: query"); return; }

	const int32 MaxMatches = 100;
	TArray<TSharedPtr<FJsonValue>> Matches;

	auto SearchFile = [&](const FString& Label, const FString& FilePath)
	{
		if (Matches.Num() >= MaxMatches) return;
		const FConfigFile* File = GConfig->Find(FilePath);
		if (!File) return;

		for (const auto& SectionPair : AsConst(*File))
		{
			if (Matches.Num() >= MaxMatches) break;
			for (const auto& KeyPair : SectionPair.Value)
			{
				if (Matches.Num() >= MaxMatches) break;
				const FString KeyStr = KeyPair.Key.ToString();
				const FString ValStr = KeyPair.Value.GetValue();
				if (KeyStr.Contains(Query, ESearchCase::IgnoreCase) || ValStr.Contains(Query, ESearchCase::IgnoreCase))
				{
					TSharedPtr<FJsonObject> Match = MakeShared<FJsonObject>();
					Match->SetStringField(TEXT("file"), Label);
					Match->SetStringField(TEXT("section"), SectionPair.Key);
					Match->SetStringField(TEXT("key"), KeyStr);
					Match->SetStringField(TEXT("value"), ValStr);
					Matches.Add(MakeShared<FJsonValueObject>(Match));
				}
			}
		}
	};

	for (const auto& FilePair : GetKnownConfigPaths())
	{
		if (!FileFilter.IsEmpty() && !FilePair.Key.Contains(FileFilter, ESearchCase::IgnoreCase)) continue;
		SearchFile(FilePair.Key, FilePair.Value);
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("query"), Query);
	Result->SetNumberField(TEXT("match_count"), Matches.Num());
	Result->SetBoolField(TEXT("truncated"), Matches.Num() >= MaxMatches);
	Result->SetArrayField(TEXT("matches"), Matches);

	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
}

void HandleGetConfigSectionFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }

	FString Section, FileHint;
	Args->TryGetStringField(TEXT("section"), Section);
	Args->TryGetStringField(TEXT("file"), FileHint);

	if (Section.IsEmpty()) { OutError = TEXT("Missing required parameter: section"); return; }

	TArray<TSharedPtr<FJsonValue>> Entries;

	auto ReadFromFile = [&](const FString& FilePath)
	{
		const FConfigFile* File = GConfig->Find(FilePath);
		if (!File) return false;
		const FConfigSection* Sec = File->FindSection(*Section);
		if (!Sec) return false;
		for (const auto& KeyPair : *Sec)
		{
			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("key"), KeyPair.Key.ToString());
			Entry->SetStringField(TEXT("value"), KeyPair.Value.GetValue());
			Entries.Add(MakeShared<FJsonValueObject>(Entry));
		}
		return true;
	};

	bool bFound = false;
	if (!FileHint.IsEmpty())
	{
		const FString FilePath = ResolveConfigFilePath(FileHint);
		if (!FilePath.IsEmpty()) bFound = ReadFromFile(FilePath);
	}

	if (!bFound)
	{
		for (const auto& FilePair : GetKnownConfigPaths())
		{
			if (ReadFromFile(FilePair.Value)) { bFound = true; break; }
		}
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), bFound);
	Result->SetStringField(TEXT("section"), Section);
	Result->SetNumberField(TEXT("count"), Entries.Num());
	Result->SetArrayField(TEXT("entries"), Entries);

	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
}

void HandleListConfigFilesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString CategoryFilter;
	if (Args.IsValid()) Args->TryGetStringField(TEXT("category"), CategoryFilter);

	TArray<TSharedPtr<FJsonValue>> FilesArray;

	for (const auto& FilePair : GetKnownConfigPaths())
	{
		const FString& Label = FilePair.Key;
		const FString& Path  = FilePair.Value;

		if (!CategoryFilter.IsEmpty() && !Label.Contains(CategoryFilter, ESearchCase::IgnoreCase))
			continue;

		const int64 Size = IFileManager::Get().FileSize(*Path);

		FString Layer = TEXT("project");
		if (Label.StartsWith(TEXT("Base")))    Layer = TEXT("engine_base");
		if (Label.StartsWith(TEXT("Saved_"))) Layer = TEXT("saved_merged");

		TSharedPtr<FJsonObject> FileObj = MakeShared<FJsonObject>();
		FileObj->SetStringField(TEXT("label"), Label);
		FileObj->SetStringField(TEXT("path"), Path);
		FileObj->SetStringField(TEXT("layer"), Layer);
		FileObj->SetNumberField(TEXT("size_bytes"), (double)Size);
		FilesArray.Add(MakeShared<FJsonValueObject>(FileObj));
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetNumberField(TEXT("count"), FilesArray.Num());
	Result->SetArrayField(TEXT("files"), FilesArray);

	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
}

}
