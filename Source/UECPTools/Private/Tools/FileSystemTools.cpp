// Copyright 2026, BlueprintsLab, All rights reserved

#include "Tools/FileSystemTools.h"
#include "UECPCoreModule.h"
#include "Services/IUECPExtensionService.h"
#include "Services/IUECPToolDispatcher.h"
#include "Services/IUECPCreateAssetRegistry.h"
#include "Services/IUECPLearningService.h"
#include "Services/IUECPArchitectService.h"
#include "Managers/ProjectStateCache.h"
#include "Managers/UpdateManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/FileManager.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace FileSystemTools
{
	void HandleGetProjectRootPath(FString& OutPath, FString& OutError)
	{
		OutPath = FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath());

		if (OutPath.IsEmpty())
		{
			OutError = TEXT("Could not determine the project root path via FPaths::GetProjectFilePath().");
		}
		OutPath.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
	}

	void HandleScanDirectory(const FString& DirectoryPath, const TArray<FString>& Extensions, FString& OutJsonString, FString& OutError)
	{
		IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

		const bool bIsContentPath = DirectoryPath.StartsWith(TEXT("/Game")) || DirectoryPath.StartsWith(TEXT("/Engine"));
		const bool bHasSourceExts = Extensions.Num() > 0;
		if (bIsContentPath && !bHasSourceExts)
		{
			OutError = FString::Printf(
				TEXT("scan_directory scans source/disk files (.cpp/.h etc), not UE assets. "
				     "To list assets in the Content Browser use: "
				     "asset_management(action=\"list_assets_in_folder\", folder_path=\"%s\")"),
				*DirectoryPath);
			return;
		}

		FString ResolvedPath = DirectoryPath;
		if (DirectoryPath.StartsWith(TEXT("/Game/")))
		{
			ResolvedPath = FPaths::ProjectContentDir() / DirectoryPath.Mid(6);
		}
		else if (DirectoryPath.Equals(TEXT("/Game"), ESearchCase::IgnoreCase))
		{
			ResolvedPath = FPaths::ProjectContentDir();
		}
		else if (DirectoryPath.StartsWith(TEXT("/Engine/")))
		{
			ResolvedPath = FPaths::EngineContentDir() / DirectoryPath.Mid(8);
		}
		FPaths::NormalizeDirectoryName(ResolvedPath);

		if (!PlatformFile.DirectoryExists(*ResolvedPath))
		{
			OutError = FString::Printf(TEXT("Directory not found: %s (resolved to: %s)"), *DirectoryPath, *ResolvedPath);
			return;
		}

		TArray<FString> FoundFiles;
		TArray<FString> TargetExtensions = Extensions.Num() > 0 ? Extensions : TArray<FString>{TEXT(".cpp"), TEXT(".h"), TEXT(".cs")};

		class FFileVisitor : public IPlatformFile::FDirectoryVisitor
		{
		public:
			TArray<FString>& Files;
			const TArray<FString>& Exts;

			FFileVisitor(TArray<FString>& InFiles, const TArray<FString>& InExts) : Files(InFiles), Exts(InExts) {}

			virtual bool Visit(const TCHAR* FilenameOrDirectory, bool bIsDirectory) override
			{
				if (!bIsDirectory)
				{
					FString Ext = FPaths::GetExtension(FilenameOrDirectory);
					if (Exts.Contains(FString::Printf(TEXT(".%s"), *Ext.ToLower())) || Exts.Num() == 0)
					{
						Files.Add(FilenameOrDirectory);
					}
				}
				return true;
			}
		};

		FFileVisitor Visitor(FoundFiles, TargetExtensions);
		PlatformFile.IterateDirectoryRecursively(*ResolvedPath, Visitor);

		TSharedPtr<FJsonObject> ResultObject = MakeShareable(new FJsonObject);
		TArray<TSharedPtr<FJsonValue>> FilesArray;

		for (const FString& File : FoundFiles)
		{
			FilesArray.Add(MakeShareable(new FJsonValueString(File)));
		}

		ResultObject->SetArrayField(TEXT("files"), FilesArray);
		ResultObject->SetNumberField(TEXT("count"), FoundFiles.Num());
		ResultObject->SetBoolField(TEXT("success"), true);

		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
		FJsonSerializer::Serialize(ResultObject.ToSharedRef(), Writer);
	}

	void HandleExportToFile(const FString& FileName, const FString& FileContent, const FString& FileFormat, FString& OutError)
	{
		if (FileName.IsEmpty() || FileContent.IsEmpty())
		{
			OutError = TEXT("File name and content cannot be empty.");
			return;
		}

		const FString SafeFileName  = FPaths::MakeValidFileName(FileName);
		const FString FinalFileName = FString::Printf(TEXT("%s.%s"), *SafeFileName, *FileFormat);

		const FString SaveDirectory = FPaths::ProjectSavedDir() / TEXT("Exported Explanations");

		if (!IFileManager::Get().DirectoryExists(*SaveDirectory))
		{
			if (!IFileManager::Get().MakeDirectory(*SaveDirectory, true))
			{
				OutError = FString::Printf(TEXT("Failed to create directory: %s"), *SaveDirectory);
				return;
			}
		}

		const FString FullPath = SaveDirectory / FinalFileName;

		if (!FFileHelper::SaveStringToFile(FileContent, *FullPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			OutError = FString::Printf(TEXT("Failed to save file to path: %s"), *FullPath);
		}
	}

	static void WriteJsonErrorPair(const FString& ErrorMessage, FString& OutJsonString, FString& OutError)
	{
		OutError = ErrorMessage;
		TSharedPtr<FJsonObject> ResultObject = MakeShareable(new FJsonObject);
		ResultObject->SetBoolField(TEXT("success"), false);
		ResultObject->SetStringField(TEXT("error"), ErrorMessage);
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
		FJsonSerializer::Serialize(ResultObject.ToSharedRef(), Writer);
	}

	void HandleScanDirectoryFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
	{
		if (!Args.IsValid()) { WriteJsonErrorPair(TEXT("Invalid arguments"), OutJsonString, OutError); return; }

		FString DirectoryPath;
		if (!Args->TryGetStringField(TEXT("directory_path"), DirectoryPath) || DirectoryPath.IsEmpty())
			if (!Args->TryGetStringField(TEXT("path"), DirectoryPath) || DirectoryPath.IsEmpty())
				Args->TryGetStringField(TEXT("dir_path"), DirectoryPath);
		if (DirectoryPath.IsEmpty())
		{
			WriteJsonErrorPair(TEXT("Missing required parameter: directory_path"), OutJsonString, OutError);
			return;
		}

		if (DirectoryPath.StartsWith(TEXT("/Game")) || DirectoryPath.StartsWith(TEXT("/Engine")))
		{
			OutError = FString::Printf(
				TEXT("scan_directory scans source/disk files (.cpp/.h etc), not UE assets. "
				     "To list assets in the Content Browser use: "
				     "asset_management(action=\"list_assets_in_folder\", folder_path=\"%s\")"),
				*DirectoryPath);
			return;
		}

		if (!IFileManager::Get().DirectoryExists(*DirectoryPath))
		{
			WriteJsonErrorPair(FString::Printf(TEXT("Directory not found: %s"), *DirectoryPath), OutJsonString, OutError);
			return;
		}

		TArray<TSharedPtr<FJsonValue>> FilesArray;

		TArray<FString> HeaderFiles;
		IFileManager::Get().FindFilesRecursive(HeaderFiles, *DirectoryPath, TEXT("*.h"), true, false);
		for (const FString& File : HeaderFiles)
		{
			TSharedPtr<FJsonObject> FileObj = MakeShareable(new FJsonObject);
			FileObj->SetStringField(TEXT("file_path"), File);
			FileObj->SetStringField(TEXT("file_type"), TEXT("header"));
			FileObj->SetStringField(TEXT("relative_path"), File.Mid(DirectoryPath.Len() + 1));
			FilesArray.Add(MakeShareable(new FJsonValueObject(FileObj)));
		}

		TArray<FString> CppFiles;
		IFileManager::Get().FindFilesRecursive(CppFiles, *DirectoryPath, TEXT("*.cpp"), true, false);
		for (const FString& File : CppFiles)
		{
			TSharedPtr<FJsonObject> FileObj = MakeShareable(new FJsonObject);
			FileObj->SetStringField(TEXT("file_path"), File);
			FileObj->SetStringField(TEXT("file_type"), TEXT("source"));
			FileObj->SetStringField(TEXT("relative_path"), File.Mid(DirectoryPath.Len() + 1));
			FilesArray.Add(MakeShareable(new FJsonValueObject(FileObj)));
		}

		TArray<FString> HppFiles;
		IFileManager::Get().FindFilesRecursive(HppFiles, *DirectoryPath, TEXT("*.hpp"), true, false);
		for (const FString& File : HppFiles)
		{
			TSharedPtr<FJsonObject> FileObj = MakeShareable(new FJsonObject);
			FileObj->SetStringField(TEXT("file_path"), File);
			FileObj->SetStringField(TEXT("file_type"), TEXT("header"));
			FileObj->SetStringField(TEXT("relative_path"), File.Mid(DirectoryPath.Len() + 1));
			FilesArray.Add(MakeShareable(new FJsonValueObject(FileObj)));
		}

		TSharedPtr<FJsonObject> ResultObject = MakeShareable(new FJsonObject);
		ResultObject->SetStringField(TEXT("directory"),    DirectoryPath);
		ResultObject->SetArrayField (TEXT("files"),        FilesArray);
		ResultObject->SetNumberField(TEXT("file_count"),   FilesArray.Num());
		ResultObject->SetNumberField(TEXT("header_count"), HeaderFiles.Num());
		ResultObject->SetNumberField(TEXT("source_count"), CppFiles.Num() + HppFiles.Num());
		ResultObject->SetBoolField  (TEXT("success"),      true);

		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
		FJsonSerializer::Serialize(ResultObject.ToSharedRef(), Writer);
	}

	static FString FindExtensionUmbrellaDocs(const FString& Category)
	{
		if (Category.IsEmpty() || !IUECPCoreModule::IsAvailable()) return FString();
		const FName CatName(*Category);
		IUECPExtensionService& Ext = IUECPCoreModule::Get().GetExtensionService();
		for (const FUECPExtensionDescriptor& D : Ext.GetExtensions())
		{
			if (Ext.GetExtensionState(D.ExtensionId) != EUECPExtensionState::Loaded) continue;
			if (const FString* Docs = D.UmbrellaDocs.Find(CatName))
			{
				if (!Docs->IsEmpty()) return *Docs;
			}
		}
		return FString();
	}

	static FString BuildDisabledExtensionPrefix(const FString& Category)
	{
		if (Category.IsEmpty() || !IUECPCoreModule::IsAvailable()) return FString();
		IUECPExtensionService& Ext = IUECPCoreModule::Get().GetExtensionService();
		TOptional<FUECPExtensionDescriptor> Owner = Ext.FindExtensionByUmbrella(FName(*Category));
		if (!Owner.IsSet()) return FString();
		if (Ext.GetExtensionState(Owner->ExtensionId) == EUECPExtensionState::Loaded) return FString();
		return FString::Printf(
			TEXT("> ⚠️ The **%s** extension is currently disabled — these tools will fail with "
			     "\"Unknown tool\" until the user enables it in Settings → Extensions. "
			     "Stop calling tools from this umbrella until the user re-enables the extension.\n\n"),
			*Owner->DisplayName.ToString());
	}

	static FString BuildMissingCategoryError(const FString& Category)
	{
		if (!Category.IsEmpty() && IUECPCoreModule::IsAvailable())
		{
			IUECPExtensionService& Ext = IUECPCoreModule::Get().GetExtensionService();
			if (TOptional<FUECPExtensionDescriptor> Owner = Ext.FindExtensionByUmbrella(FName(*Category)); Owner.IsSet())
			{
				const EUECPExtensionState State = Ext.GetExtensionState(Owner->ExtensionId);
				if (State != EUECPExtensionState::Loaded)
				{
					return FString::Printf(
						TEXT("The '%s' umbrella is part of the '%s' extension, which is currently %s. "
						     "Open the editor's Settings → Extensions panel and enable '%s' (the editor "
						     "will offer to enable any required UE plugins and restart). Once loaded, "
						     "retry get_tool_docs(category='%s')."),
						*Category,
						*Owner->ExtensionId.ToString(),
						LexToString(State),
						*Owner->DisplayName.ToString(),
						*Category);
				}
			}
		}
		return FString::Printf(
			TEXT("Category '%s' not found in tool_docs.md or any registered extension"),
			*Category);
	}

	static bool FindCategoryHeading(const FString& FileContents, const FString& Category, int32& OutHeadingIdx, FString& OutHeading)
	{
		OutHeading = FString::Printf(TEXT("### %s"), *Category);
		OutHeadingIdx = INDEX_NONE;

		const FString FileLower = FileContents.ToLower();
		const FString HeadingLower = OutHeading.ToLower();

		int32 SearchPos = 0;
		while (SearchPos < FileLower.Len())
		{
			const int32 Found = FileLower.Find(HeadingLower, ESearchCase::CaseSensitive, ESearchDir::FromStart, SearchPos);
			if (Found == INDEX_NONE) break;

			const bool bAtLineStart = (Found == 0) || (FileLower[Found - 1] == '\n');
			const int32 After = Found + HeadingLower.Len();
			const bool bTerminated = (After >= FileLower.Len())
				|| FileLower[After] == '\n'
				|| FileLower[After] == '\r'
				|| FileLower[After] == ' ';

			if (bAtLineStart && bTerminated)
			{
				OutHeadingIdx = Found;
				return true;
			}
			SearchPos = Found + 1;
		}
		return false;
	}

	static FString BuildStaleCacheMarker(const FString& Cat)
	{
		return FString::Printf(
			TEXT("### %s\n_Documentation cache is being refreshed for this category — retry shortly. "
			     "If this persists, open Settings → Updates and confirm the asset sync is current._\n"),
			*Cat);
	}

	// get_tool_docs pagination: max_chars (default 12000, 0 = unlimited) and offset.
	static void ReadDocsWindowArgs(const TSharedPtr<FJsonObject>& Args, int32& OutMaxChars, int32& OutOffset)
	{
		OutMaxChars = 12000;
		OutOffset   = 0;
		double V = 0.0;
		if (Args.IsValid() && Args->TryGetNumberField(TEXT("max_chars"), V)) OutMaxChars = FMath::Max(0, (int32)V);
		if (Args.IsValid() && Args->TryGetNumberField(TEXT("offset"), V))    OutOffset   = FMath::Max(0, (int32)V);
	}

	// Slices Docs to [Offset, Offset+MaxChars) on a line boundary and annotates Obj with
	// total_chars / truncated / next_offset so the caller can page through a long umbrella section.
	static FString WindowDocs(const FString& Docs, int32 MaxChars, int32 Offset, const TSharedPtr<FJsonObject>& Obj)
	{
		const int32 Total = Docs.Len();
		Obj->SetNumberField(TEXT("total_chars"), Total);

		const int32 Start = FMath::Clamp(Offset, 0, Total);
		int32 End = Total;
		if (MaxChars > 0 && Start + MaxChars < Total)
		{
			End = Start + MaxChars;
			const int32 NewLine = Docs.Find(TEXT("\n"), ESearchCase::CaseSensitive, ESearchDir::FromEnd, End);
			if (NewLine != INDEX_NONE && NewLine > Start + MaxChars / 2) End = NewLine + 1;
		}

		const bool bTruncated = End < Total;
		Obj->SetBoolField(TEXT("truncated"), bTruncated);
		if (Start > 0) Obj->SetNumberField(TEXT("offset"), Start);
		if (bTruncated)
		{
			Obj->SetNumberField(TEXT("next_offset"), End);
			Obj->SetStringField(TEXT("hint"), FString::Printf(
				TEXT("Section is %d chars; returned %d..%d. Call again with offset=%d (same max_chars) for the rest, "
				     "or pass action='<name>' for a single action's docs."),
				Total, Start, End, End));
		}
		return Docs.Mid(Start, End - Start);
	}

	static bool TryGetActionMetadata(const FString& Action, FUECPToolMeta& Out)
	{
		if (Action.IsEmpty() || !IUECPCoreModule::IsAvailable()) return false;
		TArray<FUECPToolMeta> Metas;
		IUECPCoreModule::Get().GetToolDispatcher().GetAllToolMetadata(Metas);
		for (const FUECPToolMeta& M : Metas)
			if (M.Action.ToString().Equals(Action, ESearchCase::IgnoreCase)) { Out = M; return true; }
		return false;
	}

	static bool FindActionBullet(const FString& Action, const FString& Category, FString& OutBullet)
	{
		const FString Needle = FString::Printf(TEXT("- `%s`"), *Action);
		auto ScanText = [&](const FString& Text) -> bool
		{
			TArray<FString> Lines;
			Text.ParseIntoArrayLines(Lines,  false);
			for (const FString& Raw : Lines)
			{
				const FString L = Raw.TrimStartAndEnd();
				if (!L.StartsWith(Needle)) continue;
				const TCHAR After = L.Len() > Needle.Len() ? L[Needle.Len()] : TCHAR(' ');
				if (After == ' ' || After == TCHAR(0x2014) || After == '-' || After == ':')
				{ OutBullet = L; return true; }
			}
			return false;
		};

		TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("BpGeneratorUltimate"));
		FString CoreDocs;
		if (Plugin.IsValid())
			CoreDocs = FUpdateManager::Get().LoadCachedRevision(TEXT("tool_docs"));

		if (!Category.IsEmpty())
		{
			int32 HeadingIdx; FString Heading;
			if (!CoreDocs.IsEmpty() && FindCategoryHeading(CoreDocs, Category, HeadingIdx, Heading))
			{
				int32 SectionEnd = CoreDocs.Len();
				const int32 NextIdx = CoreDocs.Find(TEXT("\n### "), ESearchCase::CaseSensitive, ESearchDir::FromStart, HeadingIdx + Heading.Len() + 1);
				if (NextIdx != INDEX_NONE) SectionEnd = NextIdx + 1;
				if (ScanText(CoreDocs.Mid(HeadingIdx, SectionEnd - HeadingIdx))) return true;
			}
			const FString ExtDocs = FindExtensionUmbrellaDocs(Category);
			if (!ExtDocs.IsEmpty() && ScanText(ExtDocs)) return true;
		}

		if (!CoreDocs.IsEmpty() && ScanText(CoreDocs)) return true;
		if (IUECPCoreModule::IsAvailable())
		{
			IUECPExtensionService& Ext = IUECPCoreModule::Get().GetExtensionService();
			for (const FUECPExtensionDescriptor& D : Ext.GetExtensions())
			{
				if (Ext.GetExtensionState(D.ExtensionId) != EUECPExtensionState::Loaded) continue;
				for (const TPair<FName, FString>& Pair : D.UmbrellaDocs)
					if (!Pair.Value.IsEmpty() && ScanText(Pair.Value)) return true;
			}
		}
		return false;
	}

	void HandleGetToolDocsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
	{
		if (!Args.IsValid()) { OutError = TEXT("Invalid arguments"); return; }

		const bool _bDocCacheCurrent = FProjectStateCache::Get().IsDocCacheCurrent();

		{
			FString CategoriesStr;
			const TArray<TSharedPtr<FJsonValue>>* CheckArr = nullptr;
			if (!Args->TryGetArrayField(TEXT("categories"), CheckArr) &&
				Args->TryGetStringField(TEXT("categories"), CategoriesStr) && !CategoriesStr.IsEmpty())
			{
				TArray<TSharedPtr<FJsonValue>> Parsed;
				TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(CategoriesStr);
				if (FJsonSerializer::Deserialize(R, Parsed) && Parsed.Num() > 0)
					Args->SetArrayField(TEXT("categories"), Parsed);
				else
					Args->SetStringField(TEXT("category"), CategoriesStr);
			}
		}

		{
			FString ActionName;
			if (Args->TryGetStringField(TEXT("action"), ActionName) && !ActionName.TrimStartAndEnd().IsEmpty())
			{
				ActionName = ActionName.TrimStartAndEnd();
				FString CatHint;
				Args->TryGetStringField(TEXT("category"), CatHint);
				CatHint = CatHint.TrimStartAndEnd().ToLower();

				TSharedPtr<FJsonObject> R = MakeShareable(new FJsonObject());
				R->SetBoolField(TEXT("success"), true);
				R->SetStringField(TEXT("action"), ActionName);

				// Registry metadata (terse summary + params spec) and the tool_docs bullet (recipes,
				// gotchas, examples) are complementary — return both whenever both exist.
				FUECPToolMeta Meta;
				const bool bHasMeta = TryGetActionMetadata(ActionName, Meta);
				FString Category = CatHint;
				if (bHasMeta)
				{
					Category = Meta.Umbrella.ToString().ToLower();
					R->SetStringField(TEXT("category"), Category);
					if (!Meta.Summary.IsEmpty()) R->SetStringField(TEXT("summary"), Meta.Summary);
					if (!Meta.Params.IsEmpty())  R->SetStringField(TEXT("params"),  Meta.Params);
					R->SetStringField(TEXT("call"), FString::Printf(TEXT("%s(action='%s', ...)"), *Category, *ActionName));
				}

				FString Bullet;
				bool bHasBullet = false;
				if (_bDocCacheCurrent)
				{
					bHasBullet = FindActionBullet(ActionName, Category, Bullet);
				}
				else
				{
					R->SetStringField(TEXT("docs_status"), TEXT("cache_refreshing"));
				}

				if (bHasBullet)
				{
					R->SetStringField(TEXT("docs"), Bullet);
					if (!bHasMeta && !CatHint.IsEmpty()) R->SetStringField(TEXT("category"), CatHint);
				}

				if (!bHasMeta && !bHasBullet)
				{
					if (!_bDocCacheCurrent)
					{
						R->SetStringField(TEXT("docs"), BuildStaleCacheMarker(ActionName));
						TSharedRef<TJsonWriter<>> SW = TJsonWriterFactory<>::Create(&OutJsonString);
						FJsonSerializer::Serialize(R.ToSharedRef(), SW);
						return;
					}
					OutError = FString::Printf(
						TEXT("No action '%s' found in the tool registry or docs. Use search_tools(query='...') to find the right action, "
						     "or get_tool_docs(category='<umbrella>') for an umbrella's full reference."),
						*ActionName);
					return;
				}

				R->SetStringField(TEXT("source"),
					(bHasMeta && bHasBullet) ? TEXT("registry+doc") : (bHasMeta ? TEXT("registry") : TEXT("doc")));

				TSharedRef<TJsonWriter<>> AW = TJsonWriterFactory<>::Create(&OutJsonString);
				FJsonSerializer::Serialize(R.ToSharedRef(), AW);
				return;
			}
		}

		const TArray<TSharedPtr<FJsonValue>>* CategoriesArray = nullptr;
		if (Args->TryGetArrayField(TEXT("categories"), CategoriesArray) && CategoriesArray && CategoriesArray->Num() > 0)
		{
			TArray<FString> ToFetch;
			for (const TSharedPtr<FJsonValue>& V : *CategoriesArray)
			{
				FString Cat = V->AsString().TrimStartAndEnd().ToLower();
				if (!Cat.IsEmpty()) ToFetch.Add(Cat);
			}

			TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("BpGeneratorUltimate"));
			if (!Plugin.IsValid()) { OutError = TEXT("Plugin BpGeneratorUltimate not found"); return; }
			FString FileContents = FUpdateManager::Get().LoadCachedRevision(TEXT("tool_docs"));
			if (FileContents.IsEmpty()) { OutError = TEXT("Could not read tool_docs (cache entry unavailable)"); return; }

			// Batch form: max_chars applies per category; offset is only meaningful for one section.
			int32 MaxChars = 0, IgnoredOffset = 0;
			ReadDocsWindowArgs(Args, MaxChars, IgnoredOffset);

			TArray<TSharedPtr<FJsonValue>> ResultsArr;
			for (const FString& Cat : ToFetch)
			{
				TSharedPtr<FJsonObject> Entry = MakeShareable(new FJsonObject());
				Entry->SetStringField(TEXT("category"), Cat);

				if (!_bDocCacheCurrent)
				{
					Entry->SetStringField(TEXT("docs"), BuildStaleCacheMarker(Cat));
					ResultsArr.Add(MakeShareable(new FJsonValueObject(Entry)));
					continue;
				}

				int32 HeadingIdx = INDEX_NONE;
				FString Heading;
				const FString DisabledPrefix = BuildDisabledExtensionPrefix(Cat);

				if (!FindCategoryHeading(FileContents, Cat, HeadingIdx, Heading))
				{
					const FString ExtDocs = FindExtensionUmbrellaDocs(Cat);
					if (!ExtDocs.IsEmpty())
					{
						Entry->SetStringField(TEXT("docs"), WindowDocs(DisabledPrefix + ExtDocs, MaxChars, 0, Entry));
					}
					else
					{
						Entry->SetStringField(TEXT("error"), BuildMissingCategoryError(Cat));
					}
				}
				else
				{
					int32 SectionEnd = FileContents.Len();
					int32 NextIdx = FileContents.Find(TEXT("\n### "), ESearchCase::CaseSensitive, ESearchDir::FromStart, HeadingIdx + Heading.Len() + 1);
					if (NextIdx != INDEX_NONE) SectionEnd = NextIdx + 1;
					Entry->SetStringField(TEXT("docs"),
						WindowDocs(DisabledPrefix + FileContents.Mid(HeadingIdx, SectionEnd - HeadingIdx).TrimEnd(), MaxChars, 0, Entry));
				}
				ResultsArr.Add(MakeShareable(new FJsonValueObject(Entry)));
			}

			TSharedPtr<FJsonObject> BatchObj = MakeShareable(new FJsonObject());
			BatchObj->SetArrayField(TEXT("results"), ResultsArr);
			TSharedRef<TJsonWriter<>> BW = TJsonWriterFactory<>::Create(&OutJsonString);
			FJsonSerializer::Serialize(BatchObj.ToSharedRef(), BW);
			return;
		}

		FString Category;
		if (!Args->TryGetStringField(TEXT("category"), Category) || Category.IsEmpty())
		{
			OutError = TEXT("Missing required parameter: category (or categories=[...] for batch)");
			return;
		}
		Category = Category.TrimStartAndEnd().ToLower();

		if (!_bDocCacheCurrent)
		{
			TSharedPtr<FJsonObject> Stale = MakeShareable(new FJsonObject());
			Stale->SetStringField(TEXT("category"), Category);
			Stale->SetStringField(TEXT("docs"), BuildStaleCacheMarker(Category));
			Stale->SetBoolField(TEXT("success"), true);
			TSharedRef<TJsonWriter<>> SW = TJsonWriterFactory<>::Create(&OutJsonString);
			FJsonSerializer::Serialize(Stale.ToSharedRef(), SW);
			return;
		}

		TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("BpGeneratorUltimate"));
		if (!Plugin.IsValid()) { OutError = TEXT("Plugin BpGeneratorUltimate not found"); return; }

		FString FileContents = FUpdateManager::Get().LoadCachedRevision(TEXT("tool_docs"));
		if (FileContents.IsEmpty())
		{
			OutError = TEXT("Could not read tool_docs (cache entry unavailable)");
			return;
		}

		int32 MaxChars = 0, Offset = 0;
		ReadDocsWindowArgs(Args, MaxChars, Offset);

		int32 HeadingIdx = INDEX_NONE;
		FString Heading;
		const FString DisabledPrefix = BuildDisabledExtensionPrefix(Category);
		if (!FindCategoryHeading(FileContents, Category, HeadingIdx, Heading))
		{
			const FString ExtDocs = FindExtensionUmbrellaDocs(Category);
			if (!ExtDocs.IsEmpty())
			{
				TSharedPtr<FJsonObject> ResultObject = MakeShareable(new FJsonObject());
				ResultObject->SetStringField(TEXT("category"), Category);
				ResultObject->SetStringField(TEXT("docs"), WindowDocs(DisabledPrefix + ExtDocs, MaxChars, Offset, ResultObject));
				ResultObject->SetBoolField(TEXT("success"), true);
				TSharedRef<TJsonWriter<>> ExtWriter = TJsonWriterFactory<>::Create(&OutJsonString);
				FJsonSerializer::Serialize(ResultObject.ToSharedRef(), ExtWriter);
				return;
			}
			OutError = BuildMissingCategoryError(Category);
			return;
		}

		const int32 SectionStart = HeadingIdx;
		int32 SectionEnd = FileContents.Len();
		const int32 SearchFrom = SectionStart + Heading.Len() + 1;
		const int32 NextIdx = FileContents.Find(TEXT("\n### "), ESearchCase::CaseSensitive, ESearchDir::FromStart, SearchFrom);
		if (NextIdx != INDEX_NONE) SectionEnd = NextIdx + 1;

		const FString SectionText = FileContents.Mid(SectionStart, SectionEnd - SectionStart).TrimEnd();

		TSharedPtr<FJsonObject> ResultObject = MakeShareable(new FJsonObject());
		ResultObject->SetStringField(TEXT("category"), Category);
		ResultObject->SetStringField(TEXT("docs"), WindowDocs(DisabledPrefix + SectionText, MaxChars, Offset, ResultObject));
		ResultObject->SetBoolField  (TEXT("success"), true);

		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
		FJsonSerializer::Serialize(ResultObject.ToSharedRef(), Writer);
	}

	namespace SearchToolsInternal
	{
		static void Tokenize(const FString& In, TArray<FString>& Out)
		{
			FString Cur;
			auto Flush = [&]() { if (Cur.Len() >= 2) Out.Add(Cur); Cur.Reset(); };
			for (int32 i = 0; i < In.Len(); ++i)
			{
				const TCHAR C = In[i];
				if (FChar::IsAlnum(C))
				{
					if (Cur.Len() > 0 && FChar::IsUpper(C) && i > 0 && FChar::IsLower(In[i - 1])) Flush();
					Cur.AppendChar(FChar::ToLower(C));
				}
				else Flush();
			}
			Flush();
		}

		struct FEntry
		{
			FString Action;
			FString Umbrella;
			FString Description;   // tool_docs bullet text when documented, else the registry summary
			FString Summary;       // registry summary when it differs from Description
			FString Params;
			TArray<FString> Tokens;
			TArray<FString> NameTokens;
		};

		static void ParseDoc(const FString& DocText, const FString& DefaultUmbrella,
			const TSet<FName>& Registered, TMap<FString, FEntry>& OutByKey)
		{
			FString CurUmbrella = DefaultUmbrella;
			TArray<FString> Lines;
			DocText.ParseIntoArrayLines(Lines,  false);
			for (const FString& Raw : Lines)
			{
				const FString Line = Raw.TrimStartAndEnd();
				if (Line.StartsWith(TEXT("### ")))
				{
					FString Head = Line.RightChop(4).TrimStartAndEnd();
					int32 Sp; if (Head.FindChar(TEXT(' '), Sp)) Head = Head.Left(Sp);
					CurUmbrella = Head.ToLower();
					continue;
				}
				if (!Line.StartsWith(TEXT("- `"))) continue;
				const FString Rest = Line.RightChop(3);
				int32 CloseTick;
				if (!Rest.FindChar(TEXT('`'), CloseTick)) continue;
				const FString Action = Rest.Left(CloseTick).TrimStartAndEnd();
				if (Action.IsEmpty() || !Registered.Contains(FName(*Action))) continue;
				FString Desc = Rest.RightChop(CloseTick + 1).TrimStartAndEnd();
				while (Desc.Len() > 0 && (Desc[0] == TCHAR(0x2014) || Desc[0] == '-' || Desc[0] == ':'))
					Desc = Desc.RightChop(1).TrimStartAndEnd();
				const FString Key = Action;
				if (OutByKey.Contains(Key)) continue;
				FEntry E;
				E.Action = Action;
				E.Umbrella = CurUmbrella;
				E.Description = Desc;
				Tokenize(Action + TEXT(" ") + CurUmbrella + TEXT(" ") + Desc, E.Tokens);
				Tokenize(Action, E.NameTokens);
				OutByKey.Add(Key, MoveTemp(E));
			}
		}

		static void BuildIndex(TArray<FEntry>& Out)
		{
			TSet<FName> Registered;
			if (IUECPCoreModule::IsAvailable())
				for (const FName& N : IUECPCoreModule::Get().GetToolDispatcher().ListTools())
					Registered.Add(N);

			TMap<FString, FEntry> ByKey;

			if (TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("BpGeneratorUltimate")))
			{
				FString FileContents = FUpdateManager::Get().LoadCachedRevision(TEXT("tool_docs"));
				if (!FileContents.IsEmpty())
					ParseDoc(FileContents, FString(), Registered, ByKey);
			}

			if (IUECPCoreModule::IsAvailable())
			{
				IUECPExtensionService& Ext = IUECPCoreModule::Get().GetExtensionService();
				for (const FUECPExtensionDescriptor& D : Ext.GetExtensions())
				{
					if (Ext.GetExtensionState(D.ExtensionId) != EUECPExtensionState::Loaded) continue;
					for (const TPair<FName, FString>& Pair : D.UmbrellaDocs)
						if (!Pair.Value.IsEmpty())
							ParseDoc(Pair.Value, Pair.Key.ToString().ToLower(), Registered, ByKey);
				}
			}

			if (IUECPCoreModule::IsAvailable())
			{
				TArray<FUECPToolMeta> Metas;
				IUECPCoreModule::Get().GetToolDispatcher().GetAllToolMetadata(Metas);
				for (const FUECPToolMeta& M : Metas)
				{
					const FString Action = M.Action.ToString();
					if (Action.IsEmpty()) continue;
					const FString Umbrella = M.Umbrella.ToString().ToLower();

					if (FEntry* Existing = ByKey.Find(Action))
					{
						// Already indexed from a tool_docs bullet: keep that (richer) description and
						// merge in the registry's params spec + summary instead of overwriting it.
						if (Existing->Description.IsEmpty()) Existing->Description = M.Summary;
						else if (!M.Summary.IsEmpty() && Existing->Description != M.Summary) Existing->Summary = M.Summary;
						if (!M.Params.IsEmpty()) Existing->Params = M.Params;
						if (!Umbrella.IsEmpty()) Existing->Umbrella = Umbrella;
						Existing->Tokens.Reset();
						Tokenize(Action + TEXT(" ") + Existing->Umbrella + TEXT(" ") + Existing->Description
							+ TEXT(" ") + M.Summary + TEXT(" ") + M.Params, Existing->Tokens);
						continue;
					}

					FEntry E;
					E.Action = Action;
					E.Umbrella = Umbrella;
					E.Description = M.Summary;
					E.Params = M.Params;
					Tokenize(Action + TEXT(" ") + E.Umbrella + TEXT(" ") + M.Summary + TEXT(" ") + M.Params, E.Tokens);
					Tokenize(Action, E.NameTokens);
					ByKey.Add(Action, MoveTemp(E));
				}
			}

			if (IUECPCoreModule::IsAvailable())
			{
				for (const FName& TypeName : IUECPCoreModule::Get().GetCreateAssetRegistry().ListTypes())
				{
					const FString T = TypeName.ToString();
					if (T.IsEmpty()) continue;
					FEntry E;
					E.Action = TEXT("create_asset");
					E.Umbrella = TEXT("asset_management");
					E.Description = FString::Printf(TEXT("Create a %s asset (create_asset)."), *T);
					E.Params = FString::Printf(TEXT("asset_type='%s', name, save_path, options?"), *T);
					Tokenize(FString::Printf(TEXT("create asset %s asset_management"), *T), E.Tokens);
					Tokenize(T, E.NameTokens);
					ByKey.Add(TEXT("create_asset::") + T, MoveTemp(E));
				}
			}

			ByKey.GenerateValueArray(Out);
		}
	}

	void HandleSearchToolsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
	{
		using namespace SearchToolsInternal;
		if (!Args.IsValid()) { OutError = TEXT("Invalid arguments"); return; }

		FString Query;
		if (!Args->TryGetStringField(TEXT("query"), Query)) Args->TryGetStringField(TEXT("q"), Query);
		Query = Query.TrimStartAndEnd();

		FString Umbrella;
		if (!Args->TryGetStringField(TEXT("umbrella"), Umbrella)) Args->TryGetStringField(TEXT("category"), Umbrella);
		Umbrella = Umbrella.TrimStartAndEnd().ToLower();

		if (Query.IsEmpty() && Umbrella.IsEmpty())
		{
			OutError = TEXT("search_tools needs a 'query' (English intent keywords, e.g. \"set material two sided\") or an 'umbrella' to list all of a category's actions.");
			return;
		}

		const bool bListAll = Query.IsEmpty();
		int32 MaxResults = bListAll ? 200 : 15;
		double MR = 0.0;
		if (Args->TryGetNumberField(TEXT("max_results"), MR))
			MaxResults = FMath::Clamp((int32)MR, 1, bListAll ? 500 : 30);

		TArray<FString> QTerms;
		if (!bListAll)
		{
			Tokenize(Query, QTerms);
			if (QTerms.Num() == 0) { OutError = TEXT("Query had no searchable terms (need ≥2-char words)."); return; }
		}

		TArray<FEntry> Full;
		BuildIndex(Full);

		TArray<FEntry> Index;
		if (!Umbrella.IsEmpty())
			for (FEntry& E : Full) { if (E.Umbrella == Umbrella) Index.Add(MoveTemp(E)); }
		else
			Index = MoveTemp(Full);

		const int32 N = Index.Num();
		if (N == 0)
		{
			OutError = !Umbrella.IsEmpty()
				? FString::Printf(TEXT("No actions found for umbrella '%s'. Check the name (run search_tools with a query instead), or the owning extension may be disabled."), *Umbrella)
				: TEXT("Tool index is empty (tool docs unavailable).");
			return;
		}

		auto EmitEntry = [](const FEntry& E) -> TSharedPtr<FJsonValue>
		{
			TSharedPtr<FJsonObject> R = MakeShareable(new FJsonObject());
			R->SetStringField(TEXT("action"), E.Action);
			R->SetStringField(TEXT("category"), E.Umbrella);
			if (!E.Description.IsEmpty()) R->SetStringField(TEXT("description"), E.Description);
			if (!E.Summary.IsEmpty())     R->SetStringField(TEXT("summary"), E.Summary);
			if (!E.Params.IsEmpty()) R->SetStringField(TEXT("params"), E.Params);
			R->SetStringField(TEXT("call"), FString::Printf(TEXT("%s(action='%s', ...)"), *E.Umbrella, *E.Action));
			return MakeShareable(new FJsonValueObject(R));
		};

		TArray<TSharedPtr<FJsonValue>> ResultsArr;
		int32 TotalMatches = 0;

		if (bListAll)
		{
			Index.Sort([](const FEntry& A, const FEntry& B) { return A.Action < B.Action; });
			TotalMatches = Index.Num();
			const int32 Take = FMath::Min(MaxResults, Index.Num());
			for (int32 i = 0; i < Take; ++i) ResultsArr.Add(EmitEntry(Index[i]));
		}
		else
		{
			TMap<FString, int32> Df;
			double TotalLen = 0.0;
			for (const FEntry& E : Index)
			{
				TotalLen += E.Tokens.Num();
				TSet<FString> Seen(E.Tokens);
				for (const FString& T : Seen) Df.FindOrAdd(T)++;
			}
			const double AvgLen = N > 0 ? TotalLen / N : 1.0;
			const double K1 = 1.5, B = 0.75;

			struct FScored { int32 Idx; double Score; };
			TArray<FScored> Scored;
			for (int32 i = 0; i < N; ++i)
			{
				const FEntry& E = Index[i];
				TMap<FString, int32> Tf;
				for (const FString& T : E.Tokens) Tf.FindOrAdd(T)++;
				double Score = 0.0;
				for (const FString& QT : QTerms)
				{
					const int32 DfV = Df.FindRef(QT);
					const double Idf = FMath::Loge(((double)(N - DfV) + 0.5) / ((double)DfV + 0.5) + 1.0);

					if (const int32* TfPtr = Tf.Find(QT))
					{
						const double Norm = 1.0 - B + B * ((double)E.Tokens.Num() / AvgLen);
						Score += Idf * ((*TfPtr) * (K1 + 1.0)) / ((*TfPtr) + K1 * Norm);
					}

					if (E.NameTokens.Contains(QT)) Score += 2.5 * Idf;
				}
				if (Score <= 0.0) continue;
				Scored.Add({ i, Score });
			}
			Scored.Sort([](const FScored& A, const FScored& C) { return A.Score > C.Score; });
			TotalMatches = Scored.Num();
			const int32 Take = FMath::Min(MaxResults, Scored.Num());
			for (int32 i = 0; i < Take; ++i) ResultsArr.Add(EmitEntry(Index[Scored[i].Idx]));
		}

		TSharedPtr<FJsonObject> Root = MakeShareable(new FJsonObject());
		Root->SetBoolField(TEXT("success"), true);
		if (!Query.IsEmpty())    Root->SetStringField(TEXT("query"), Query);
		if (!Umbrella.IsEmpty()) Root->SetStringField(TEXT("umbrella"), Umbrella);
		Root->SetNumberField(TEXT("count"), ResultsArr.Num());
		Root->SetNumberField(TEXT("total_matches"), TotalMatches);
		Root->SetArrayField(TEXT("results"), ResultsArr);
		Root->SetStringField(TEXT("hint"), bListAll
			? FString::Printf(TEXT("All %d registered actions in the '%s' umbrella. Call %s(action='<action>', ...); get_tool_docs(category='%s', action='<action>') for fuller per-action detail."), ResultsArr.Num(), *Umbrella, *Umbrella, *Umbrella)
			: (ResultsArr.Num() > 0
				? TEXT("Call category(action='<action>', ...). The params above are usually enough — only call get_tool_docs(category='<category>', action='<action>') if you need fuller per-action detail, or get_tool_docs(category='<category>') for recipes/gotchas.")
				: TEXT("No matches — try simpler English keywords (verb + noun, e.g. \"add variable\", \"set collision\"), or get_tool_docs(category=...) if you know the umbrella.")));
		TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJsonString);
		FJsonSerializer::Serialize(Root.ToSharedRef(), W);
	}

	void HandleMarkLearningStepFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
	{
		if (!Args.IsValid()) { OutError = TEXT("Invalid arguments"); return; }

		FString StepSlug;
		Args->TryGetStringField(TEXT("step_id"), StepSlug);
		if (StepSlug.IsEmpty()) Args->TryGetStringField(TEXT("step_slug"), StepSlug);

		if (StepSlug.IsEmpty())
		{
			OutError = TEXT("step_id is required");
			return;
		}

		if (IUECPCoreModule::IsAvailable())
		{
			IUECPCoreModule::Get().GetLearningService().MarkNodeComplete(StepSlug);
		}
		OutJsonString = FString::Printf(TEXT("{\"success\":true,\"message\":\"Marked '%s' as completed\"}"), *StepSlug);
	}

	void HandleExportToFileFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
	{
		if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
		FString FileName, FileContent, FileFormat;
		Args->TryGetStringField(TEXT("file_name"), FileName);
		Args->TryGetStringField(TEXT("content"),   FileContent);
		Args->TryGetStringField(TEXT("format"),    FileFormat);
		HandleExportToFile(FileName, FileContent, FileFormat, OutError);
		if (OutError.IsEmpty())
		{
			FString Display = FPaths::ProjectSavedDir() / TEXT("Exported Explanations")
				/ FString::Printf(TEXT("%s.%s"), *FileName, *FileFormat);
			FPaths::MakeStandardFilename(Display);
			OutJsonString = FString::Printf(TEXT("{\"success\":true,\"message\":\"Successfully exported to %s\"}"), *Display);
		}
	}

	void HandleGetHandleReferenceFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
	{
		FString SectionsParam;
		if (Args.IsValid()) Args->TryGetStringField(TEXT("sections"), SectionsParam);
		if (SectionsParam.IsEmpty()) SectionsParam = TEXT("list");

		if (IUECPCoreModule::IsAvailable())
		{
			const FString FromService = IUECPCoreModule::Get().GetArchitectService().GetHandleReferenceSections(SectionsParam);
			if (!FromService.IsEmpty())
			{
				OutJsonString = FromService;
				return;
			}
		}

		TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("BpGeneratorUltimate"));
		if (!Plugin.IsValid()) { OutError = TEXT("Plugin not found"); return; }

		FString FileContents = FUpdateManager::Get().LoadCachedRevision(TEXT("node_handle_reference"));
		if (FileContents.IsEmpty())
		{
			OutError = TEXT("Could not read node_handle_reference (cache entry unavailable)");
			return;
		}

		TMap<FString, FString> Sections;
		TArray<FString> Lines;
		FileContents.ParseIntoArrayLines(Lines);
		FString CurrentSection;
		FString CurrentContent;
		FString PreambleContent;

		for (const FString& Line : Lines)
		{
			if (Line.StartsWith(TEXT("--- ")) && Line.EndsWith(TEXT(" ---")))
			{
				if (!CurrentSection.IsEmpty())
					Sections.Add(CurrentSection, CurrentContent);
				else if (!PreambleContent.IsEmpty())
					Sections.Add(TEXT("Critical Rules (Preamble)"), PreambleContent);
				CurrentSection = Line.Mid(4, Line.Len() - 8);
				CurrentContent = Line + TEXT("\n");
			}
			else if (CurrentSection.IsEmpty())
				PreambleContent += Line + TEXT("\n");
			else
				CurrentContent += Line + TEXT("\n");
		}
		if (!CurrentSection.IsEmpty())
			Sections.Add(CurrentSection, CurrentContent);

		SectionsParam = SectionsParam.TrimStartAndEnd();
		if (SectionsParam.StartsWith(TEXT("[")))
			SectionsParam = SectionsParam.Replace(TEXT("["), TEXT("")).Replace(TEXT("]"), TEXT(""))
				.Replace(TEXT("\""), TEXT("")).Replace(TEXT("'"), TEXT("")).TrimStartAndEnd();

		if (SectionsParam.IsEmpty() || SectionsParam.ToLower() == TEXT("list"))
		{
			TArray<FString> SortedNames;
			for (const auto& Pair : Sections) SortedNames.Add(Pair.Key);
			SortedNames.Sort();
			FString List = TEXT("Available handle reference sections:\n");
			for (const FString& Name : SortedNames)
				List += FString::Printf(TEXT("  - %s\n"), *Name);
			List += FString::Printf(TEXT("\nTotal: %d sections.\n"), Sections.Num());
			List += TEXT("\nExamples:\n");
			List += TEXT("  get_handle_reference(sections='Event Handles (for EventGraph nodes[]), Actor, Character, Flow Control')\n");
			List += TEXT("  get_handle_reference(sections='Variables, Casting, Flow Control')\n");
			OutJsonString = List;
			return;
		}

		TArray<FString> Requested;
		SectionsParam.ParseIntoArray(Requested, TEXT(","));

		FString Result;
		TArray<FString> NotFound;
		TSet<FString> Included;

		auto AppendSection = [&](const FString& Key, const FString& Content)
		{
			if (!Included.Contains(Key)) { Included.Add(Key); Result += Content + TEXT("\n"); }
		};

		for (FString& Req : Requested)
		{
			Req = Req.TrimStartAndEnd();
			if (Req.IsEmpty()) continue;

			const FString* Found = Sections.Find(Req);
			FString MatchedKey = Req;
			if (!Found)
			{
				const FString ReqLower = Req.ToLower();
				for (const auto& Pair : Sections)
				{
					if (Pair.Key.ToLower().Contains(ReqLower))
					{
						Found = &Pair.Value;
						MatchedKey = Pair.Key;
						break;
					}
				}
			}

			if (Found)
			{
				AppendSection(MatchedKey, *Found);
				const FString MKLower = MatchedKey.ToLower();
				if (MKLower.Contains(TEXT("event")) && !MKLower.Contains(TEXT("dispatcher")))
				{
					for (const auto& Pair : Sections)
					{
						if (Pair.Key.ToLower().Contains(TEXT("dispatcher")))
						{
							AppendSection(Pair.Key, Pair.Value);
							break;
						}
					}
				}
			}
			else
				NotFound.Add(Req);
		}

		if (NotFound.Num() > 0)
		{
			Result += FString::Printf(TEXT("\nNOT FOUND: %s\n"), *FString::Join(NotFound, TEXT(", ")));
			Result += TEXT("Call get_handle_reference(sections='list') to see available section names.\n");
		}

		if (Result.IsEmpty())
		{
			OutError = TEXT("No matching sections found. Call get_handle_reference(sections='list') to see available names.");
			return;
		}
		OutJsonString = Result;
	}
}
