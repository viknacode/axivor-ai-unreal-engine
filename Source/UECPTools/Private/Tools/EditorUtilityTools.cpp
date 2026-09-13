// Copyright 2026, BlueprintsLab, All rights reserved.

#include "Tools/EditorUtilityTools.h"
#include "Services/IUECPToolDispatcher.h"
#include "UECPCoreModule.h"
#include "Managers/SettingsManager.h"

#include "EditorUtilityWidgetBlueprint.h"
#include "EditorUtilityWidgetBlueprintFactory.h"
#include "EditorUtilityBlueprintFactory.h"
#include "EditorUtilityObject.h"
#include "EditorUtilitySubsystem.h"
#include "EditorUtilityWidget.h"

#include "Subsystems/AssetEditorSubsystem.h"
#include "EditorAssetLibrary.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/ARFilter.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "UObject/Package.h"
#include "Misc/PackageName.h"
#include "Editor.h"
#include "Misc/MessageDialog.h"

#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "HighResScreenshot.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "UnrealClient.h"
#include "RenderingThread.h"
#include "EditorViewportClient.h"
#include "Misc/Paths.h"
#include "FileHelpers.h"
#include "HAL/IConsoleManager.h"
#include "AssetToolsModule.h"
#include "ObjectTools.h"
#include "Exporters/Exporter.h"
#include "Logging/MessageLog.h"
#include "UObject/ObjectRedirector.h"
#include "Engine/Blueprint.h"

namespace EditorUtilityTools
{

static UPackage* CreateAssetPackage(const FString& SavePath, const FString& Name, FString& OutFullPath)
{
	FString CleanPath = SavePath.EndsWith(TEXT("/")) ? SavePath : (SavePath + TEXT("/"));
	OutFullPath = CleanPath + Name;
	UPackage* Package = CreatePackage(*OutFullPath);
	Package->FullyLoad();
	return Package;
}

void HandleCreateEditorUtilityWidget(const FString& Name, const FString& SavePath,
	FString& OutJsonString, FString& OutError)
{
	FString FullPath;
	UPackage* Package = CreateAssetPackage(SavePath, Name, FullPath);

	UEditorUtilityWidgetBlueprintFactory* Factory = NewObject<UEditorUtilityWidgetBlueprintFactory>();
	Factory->ParentClass = UEditorUtilityWidget::StaticClass();

	UBlueprint* BP = Cast<UBlueprint>(Factory->FactoryCreateNew(
		UEditorUtilityWidgetBlueprint::StaticClass(), Package, *Name,
		RF_Public | RF_Standalone, nullptr, GWarn));

	if (!BP)
	{
		OutError = FString::Printf(TEXT("Failed to create EditorUtilityWidget '%s'"), *Name);
		return;
	}

	BP->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(BP);

	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"asset_path\":\"%s\"}"), *FullPath);
}

void HandleRunEditorUtilityWidget(const FString& AssetPath,
	FString& OutJsonString, FString& OutError)
{
	UEditorUtilityWidgetBlueprint* EUWBP = Cast<UEditorUtilityWidgetBlueprint>(
		UEditorAssetLibrary::LoadAsset(AssetPath));
	if (!EUWBP)
	{
		OutError = FString::Printf(TEXT("Could not load EditorUtilityWidgetBlueprint at '%s'"), *AssetPath);
		return;
	}

	UEditorUtilitySubsystem* EUS = GEditor->GetEditorSubsystem<UEditorUtilitySubsystem>();
	if (!EUS) { OutError = TEXT("EditorUtilitySubsystem unavailable"); return; }

	EUS->SpawnAndRegisterTab(EUWBP);

	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"asset_path\":\"%s\",\"message\":\"Widget tab opened\"}"),
		*AssetPath);
}

void HandleCreateEditorUtilityBlueprint(const FString& Name, const FString& SavePath,
	FString& OutJsonString, FString& OutError)
{
	FString FullPath;
	UPackage* Package = CreateAssetPackage(SavePath, Name, FullPath);

	UEditorUtilityBlueprintFactory* Factory = NewObject<UEditorUtilityBlueprintFactory>();
	Factory->ParentClass = UEditorUtilityObject::StaticClass();

	UBlueprint* BP = Cast<UBlueprint>(Factory->FactoryCreateNew(
		UBlueprint::StaticClass(), Package, *Name,
		RF_Public | RF_Standalone, nullptr, GWarn));

	if (!BP)
	{
		OutError = FString::Printf(TEXT("Failed to create EditorUtilityBlueprint '%s'"), *Name);
		return;
	}

	BP->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(BP);

	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"asset_path\":\"%s\"}"), *FullPath);
}

void HandleExecConsoleCommand(const FString& Command, FString& OutJsonString, FString& OutError)
{
	if (Command.IsEmpty())
	{
		OutError = TEXT("command parameter is required");
		return;
	}

	if (!GEngine)
	{
		OutError = TEXT("GEngine is not available");
		return;
	}

	const FString FirstToken = Command.Left(Command.Find(TEXT(" "))).TrimStartAndEnd();
	const FString FirstTokenUpper = FirstToken.ToUpper();
	static const TMap<FString, FString> DeprecatedExecRedirects = {
		{ TEXT("NEW"),    TEXT("asset_management(action='create_asset', asset_type='Level') for a new level, or create_asset for any other asset") },
		{ TEXT("LOAD"),   TEXT("asset_management(action='open_asset')") },
		{ TEXT("SAVE"),   TEXT("asset_management(action='save_asset')") },
		{ TEXT("IMPORT"), TEXT("import_asset") },
		{ TEXT("EXPORT"), TEXT("asset_management(action='export_asset')") },
		{ TEXT("DELETE"), TEXT("asset_management(action='delete_asset')") },
	};
	if (const FString* Redirect = DeprecatedExecRedirects.Find(FirstTokenUpper))
	{
		OutError = FString::Printf(
			TEXT("exec_console_command refused: '%s' is a deprecated UnrealEd command that pops a modal dialog and blocks unattended runs. "
			     "Use the dedicated tool instead: %s."),
			*FirstToken, **Redirect);
		return;
	}

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;

	GEngine->Exec(World, *Command, *GLog);

	bool bKnownCommand = false;
	FString CommandName = Command.Left(Command.Find(TEXT(" ")));
	if (CommandName.IsEmpty()) CommandName = Command;
	IConsoleObject* CObj = IConsoleManager::Get().FindConsoleObject(*CommandName);
	if (CObj) bKnownCommand = true;

	FString Safe = Command.Replace(TEXT("\""), TEXT("\\\""));
	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"command\":\"%s\",\"known_cvar\":%s,\"note\":\"Exec dispatched. GEngine::Exec has no return value — check output log for errors.\"}"),
		*Safe, bKnownCommand ? TEXT("true") : TEXT("false"));
}

// Axivor: capture the viewport synchronously and verify the file landed.
// The old path only armed TakeHighResScreenShot() and reported success immediately, so
// callers were told a screenshot existed when the editor had not redrawn and nothing was
// ever written. An AI verifying its own work needs the pixels, not a promise.
void HandleTakeViewportScreenshot(const FString& FilePath, FString& OutJsonString, FString& OutError)
{
	FViewport* VP = GEditor ? GEditor->GetActiveViewport() : nullptr;
	if (!VP)
	{
		OutError = TEXT("No active editor viewport found. Open a level viewport (or focus the editor) and retry.");
		return;
	}

	// Resolve the destination: absolute path as given, relative under Saved/, empty = timestamped file.
	FString Resolved = FilePath.TrimStartAndEnd();
	if (Resolved.IsEmpty())
	{
		Resolved = FPaths::ProjectSavedDir() / TEXT("Screenshots") /
			FString::Printf(TEXT("viewport_%s.png"), *FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")));
	}
	else if (FPaths::IsRelative(Resolved))
	{
		Resolved = FPaths::ProjectSavedDir() / Resolved;
	}
	if (!Resolved.EndsWith(TEXT(".png"), ESearchCase::IgnoreCase)) Resolved += TEXT(".png");
	Resolved = FPaths::ConvertRelativePathToFull(Resolved);
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Resolved), true);

	// Force a fresh frame, then pull the pixels off the viewport.
	if (GEditor) GEditor->RedrawLevelEditingViewports(true);
	VP->Draw(false);
	FlushRenderingCommands();

	const FIntPoint Size = VP->GetSizeXY();
	if (Size.X <= 0 || Size.Y <= 0)
	{
		OutError = TEXT("The active viewport has no size (minimised or not yet drawn).");
		return;
	}

	TArray<FColor> Pixels;
	if (!VP->ReadPixels(Pixels, FReadSurfaceDataFlags(), FIntRect(0, 0, Size.X, Size.Y)) || Pixels.Num() == 0)
	{
		OutError = TEXT("Could not read pixels from the active viewport.");
		return;
	}
	for (FColor& C : Pixels) C.A = 255; // viewport alpha is meaningless in a PNG

	TArray64<uint8> Png;
	FImageUtils::PNGCompressImageArray(Size.X, Size.Y, TArrayView64<const FColor>(Pixels.GetData(), Pixels.Num()), Png);
	if (Png.Num() == 0 || !FFileHelper::SaveArrayToFile(Png, *Resolved))
	{
		OutError = FString::Printf(TEXT("Failed to write the screenshot to '%s' (check the path and disk space)."), *Resolved);
		return;
	}

	const int64 Bytes = IFileManager::Get().FileSize(*Resolved);
	if (Bytes <= 0)
	{
		OutError = FString::Printf(TEXT("Screenshot file '%s' is missing or empty after the write."), *Resolved);
		return;
	}

	TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
	Out->SetBoolField(TEXT("success"), true);
	Out->SetStringField(TEXT("file_path"), Resolved.Replace(TEXT("\\"), TEXT("/")));
	Out->SetNumberField(TEXT("width"), Size.X);
	Out->SetNumberField(TEXT("height"), Size.Y);
	Out->SetNumberField(TEXT("bytes"), (double)Bytes);
	Out->SetBoolField(TEXT("exists"), true);
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Out, Writer);
}

void HandleSaveAsset(const FString& AssetPath, FString& OutJsonString, FString& OutError)
{
	if (AssetPath.IsEmpty())
	{
		OutError = TEXT("asset_path parameter is required");
		return;
	}

	const bool bOk = UEditorAssetLibrary::SaveAsset(AssetPath, false);
	FString SafePath = AssetPath.Replace(TEXT("\""), TEXT("\\\""));
	OutJsonString = FString::Printf(TEXT("{\"success\":%s,\"asset_path\":\"%s\"}"),
		bOk ? TEXT("true") : TEXT("false"), *SafePath);
}

void HandleSaveAllDirtyAssets(FString& OutJsonString, FString& OutError)
{
	const bool bOk = FEditorFileUtils::SaveDirtyPackages(
		false,
		true,
		true,
		false,
		false,
		false
	);
	OutJsonString = FString::Printf(TEXT("{\"success\":%s}"), bOk ? TEXT("true") : TEXT("false"));
}

void HandleGetConsoleVariable(const FString& VarName, FString& OutJsonString, FString& OutError)
{
	if (VarName.IsEmpty())
	{
		OutError = TEXT("variable_name parameter is required");
		return;
	}

	IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(*VarName);
	if (!CVar)
	{
		OutError = FString::Printf(TEXT("Console variable not found: %s"), *VarName);
		return;
	}

	FString Value = CVar->GetString();
	FString SafeName  = VarName.Replace(TEXT("\""), TEXT("\\\""));
	FString SafeValue = Value.Replace(TEXT("\""), TEXT("\\\""));
	OutJsonString = FString::Printf(TEXT("{\"variable\":\"%s\",\"value\":\"%s\"}"), *SafeName, *SafeValue);
}

void HandleOpenAssetEditor(const FString& AssetPath, FString& OutJsonString, FString& OutError)
{
	if (AssetPath.IsEmpty()) { OutError = TEXT("asset_path is required"); return; }

	UObject* Asset = LoadObject<UObject>(nullptr, *AssetPath);
	if (!Asset) { OutError = FString::Printf(TEXT("Failed to load asset: %s"), *AssetPath); return; }

	GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(Asset);
	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"asset_path\":\"%s\",\"message\":\"Asset editor opened\"}"), *AssetPath);
}

void HandleExportAsset(const FString& AssetPath, const FString& ExportPath, FString& OutJsonString, FString& OutError)
{

	if (AssetPath.IsEmpty()) { OutError = TEXT("asset_path is required"); return; }
	if (ExportPath.IsEmpty()) { OutError = TEXT("export_path is required"); return; }

	UObject* Asset = LoadObject<UObject>(nullptr, *AssetPath);
	if (!Asset) { OutError = FString::Printf(TEXT("Failed to load asset: %s"), *AssetPath); return; }

	UExporter* Exporter = UExporter::FindExporter(Asset, *FPaths::GetExtension(ExportPath));
	if (!Exporter)
	{
		TArray<UExporter*> Exporters;
		ObjectTools::AssembleListOfExporters(Exporters);
		for (UExporter* E : Exporters)
		{
			if (E->SupportsObject(Asset))
			{
				Exporter = E;
				break;
			}
		}
	}

	if (!Exporter)
	{
		OutJsonString = FString::Printf(TEXT("{\"success\":false,\"error\":\"No suitable exporter found for asset class '%s'. "
			"Try right-clicking the asset in Content Browser → Asset Actions → Export.\"}"),
			*Asset->GetClass()->GetName());
		return;
	}

	FString Dir = FPaths::GetPath(ExportPath);
	if (!Dir.IsEmpty()) IFileManager::Get().MakeDirectory(*Dir, true);

	bool bOk = UExporter::ExportToFile(Asset, Exporter, *ExportPath, false) != 0;
	if (bOk)
	{
		FString SafePath = ExportPath.Replace(TEXT("\\"), TEXT("/")).Replace(TEXT("\""), TEXT("\\\""));
		OutJsonString = FString::Printf(TEXT("{\"success\":true,\"export_path\":\"%s\",\"asset_class\":\"%s\"}"),
			*SafePath, *Asset->GetClass()->GetName());
	}
	else
	{
		OutError = FString::Printf(TEXT("Export failed for '%s' to '%s'"), *AssetPath, *ExportPath);
	}
}

class FLogCaptureDevice : public FOutputDevice
{
public:
	struct FLogEntry
	{
		FString Category;
		FString Message;
		ELogVerbosity::Type Verbosity;
		double Timestamp;
	};

	TArray<FLogEntry> Entries;
	FCriticalSection Lock;
	int32 MaxEntries = 500;

	static FLogCaptureDevice& Get()
	{
		static FLogCaptureDevice Instance;
		return Instance;
	}

	void StartCapture()
	{
		if (!bCapturing)
		{
			GLog->AddOutputDevice(this);
			bCapturing = true;
		}
	}

	void StopCapture()
	{
		if (bCapturing)
		{
			GLog->RemoveOutputDevice(this);
			bCapturing = false;
		}
	}

	bool IsCapturing() const { return bCapturing; }

	virtual void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& Category) override
	{
		FScopeLock ScopeLock(&Lock);
		if (Entries.Num() >= MaxEntries)
		{
			Entries.RemoveAt(0, Entries.Num() / 4);
		}
		FLogEntry Entry;
		Entry.Category = Category.ToString();
		Entry.Message = V;
		Entry.Verbosity = Verbosity;
		Entry.Timestamp = FPlatformTime::Seconds();
		Entries.Add(MoveTemp(Entry));
	}

private:
	bool bCapturing = false;
};

void HandleGetOutputLog(int32 LineCount, const FString& CategoryFilter, const FString& SeverityFilter,
	FString& OutJsonString, FString& OutError)
{
	HandleGetOutputLogSince(LineCount, CategoryFilter, SeverityFilter, 0.0, OutJsonString, OutError);
}

void HandleGetOutputLogSince(int32 LineCount, const FString& CategoryFilter, const FString& SeverityFilter,
	double SinceTimestamp, FString& OutJsonString, FString& OutError)
{

	FLogCaptureDevice& Capture = FLogCaptureDevice::Get();
	if (!Capture.IsCapturing())
	{
		Capture.StartCapture();
		const double NowTs = FPlatformTime::Seconds();
		OutJsonString = FString::Printf(
			TEXT("{\"success\":true,\"message\":\"Log capture started. Call again after some activity to retrieve entries.\",")
			TEXT("\"entries\":[],\"current_timestamp\":%.6f}"),
			NowTs);
		return;
	}

	FScopeLock ScopeLock(&Capture.Lock);

	if (LineCount <= 0) LineCount = 50;

	ELogVerbosity::Type MinVerbosity = ELogVerbosity::All;
	FString SevLower = SeverityFilter.ToLower();
	if (SevLower == TEXT("error") || SevLower == TEXT("fatal")) MinVerbosity = ELogVerbosity::Error;
	else if (SevLower == TEXT("warning")) MinVerbosity = ELogVerbosity::Warning;
	else if (SevLower == TEXT("log")) MinVerbosity = ELogVerbosity::Log;

	TArray<TSharedPtr<FJsonValue>> EntriesArray;
	int32 Start = FMath::Max(0, Capture.Entries.Num() - LineCount * 3);
	for (int32 i = Start; i < Capture.Entries.Num() && EntriesArray.Num() < LineCount; ++i)
	{
		const auto& E = Capture.Entries[i];
		if (SinceTimestamp > 0.0 && E.Timestamp <= SinceTimestamp) continue;
		if (!CategoryFilter.IsEmpty() && !E.Category.Contains(CategoryFilter, ESearchCase::IgnoreCase)) continue;
		if (MinVerbosity != ELogVerbosity::All && E.Verbosity > MinVerbosity) continue;

		TSharedPtr<FJsonObject> EntryObj = MakeShareable(new FJsonObject());
		EntryObj->SetStringField(TEXT("category"), E.Category);
		FString SafeMsg = E.Message.Replace(TEXT("\""), TEXT("'")).Left(500);
		EntryObj->SetStringField(TEXT("message"), SafeMsg);
		FString VerbStr = E.Verbosity == ELogVerbosity::Error ? TEXT("Error") :
			E.Verbosity == ELogVerbosity::Warning ? TEXT("Warning") :
			E.Verbosity == ELogVerbosity::Display ? TEXT("Display") : TEXT("Log");
		EntryObj->SetStringField(TEXT("severity"), VerbStr);
		EntryObj->SetNumberField(TEXT("timestamp"), E.Timestamp);
		EntriesArray.Add(MakeShareable(new FJsonValueObject(EntryObj)));
	}

	TSharedPtr<FJsonObject> Res = MakeShareable(new FJsonObject());
	Res->SetBoolField(TEXT("success"), true);
	Res->SetArrayField(TEXT("entries"), EntriesArray);
	Res->SetNumberField(TEXT("count"), EntriesArray.Num());
	Res->SetNumberField(TEXT("total_buffered"), Capture.Entries.Num());
	Res->SetNumberField(TEXT("current_timestamp"), FPlatformTime::Seconds());

	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Res.ToSharedRef(), W);
}

void HandleGetMapCheckErrors(FString& OutJsonString, FString& OutError)
{

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World) { OutError = TEXT("No editor world available"); return; }

	GEditor->Exec(World, TEXT("MAP CHECK"), *GLog);

	FMessageLog MapCheckLog(TEXT("MapCheck"));

	OutJsonString = TEXT("{\"success\":true,\"message\":\"Map Check executed. "
		"Check the Message Log → Map Check tab in the editor for results. "
		"Use get_output_log(category_filter='MapCheck') to retrieve entries programmatically.\"}");
}

void HandleValidateAssets(const TArray<FString>& AssetPaths, FString& OutJsonString, FString& OutError)
{

	if (AssetPaths.IsEmpty()) { OutError = TEXT("asset_paths array is required"); return; }

	TSharedPtr<FJsonObject> Res = MakeShareable(new FJsonObject());
	Res->SetBoolField(TEXT("success"), true);

	TArray<TSharedPtr<FJsonValue>> Results;
	for (const FString& Path : AssetPaths)
	{
		TSharedPtr<FJsonObject> ItemObj = MakeShareable(new FJsonObject());
		ItemObj->SetStringField(TEXT("asset_path"), Path);

		UObject* Asset = LoadObject<UObject>(nullptr, *Path);
		if (!Asset)
		{
			ItemObj->SetStringField(TEXT("status"), TEXT("not_found"));
			ItemObj->SetStringField(TEXT("error"), TEXT("Asset could not be loaded"));
		}
		else
		{
			ItemObj->SetStringField(TEXT("status"), TEXT("valid"));
			ItemObj->SetStringField(TEXT("class"), Asset->GetClass()->GetName());

			UPackage* Pkg = Asset->GetPackage();
			if (Pkg && Pkg->IsDirty())
			{
				ItemObj->SetStringField(TEXT("note"), TEXT("unsaved changes"));
			}

			if (UBlueprint* BP = Cast<UBlueprint>(Asset))
			{
				FString StatusStr;
				switch (BP->Status)
				{
				case BS_UpToDate: StatusStr = TEXT("compiled"); break;
				case BS_Dirty: StatusStr = TEXT("needs_compile"); break;
				case BS_Error: StatusStr = TEXT("compile_error"); break;
				default: StatusStr = TEXT("unknown"); break;
				}
				ItemObj->SetStringField(TEXT("compile_status"), StatusStr);
			}
		}
		Results.Add(MakeShareable(new FJsonValueObject(ItemObj)));
	}

	Res->SetArrayField(TEXT("results"), Results);
	Res->SetNumberField(TEXT("count"), Results.Num());

	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Res.ToSharedRef(), W);
}

void HandleFixUpRedirectors(const FString& FolderPath, FString& OutJsonString, FString& OutError)
{

	FString SearchPath = FolderPath.IsEmpty() ? TEXT("/Game") : FolderPath;

	FAssetRegistryModule& RegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	IAssetRegistry& Registry = RegistryModule.Get();

	TArray<FAssetData> Redirectors;
	Registry.GetAssetsByClass(UObjectRedirector::StaticClass()->GetClassPathName(), Redirectors);

	TArray<UObjectRedirector*> LoadedRedirectors;
	int32 InFolder = 0;
	for (const FAssetData& AD : Redirectors)
	{
		if (!AD.PackageName.ToString().StartsWith(SearchPath)) continue;
		InFolder++;
		UObjectRedirector* Redir = Cast<UObjectRedirector>(AD.GetAsset());
		if (Redir) LoadedRedirectors.Add(Redir);
	}

	if (LoadedRedirectors.Num() > 0)
	{
		FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
		AssetToolsModule.Get().FixupReferencers(LoadedRedirectors);
	}

	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"folder\":\"%s\",\"redirectors_found\":%d,\"fixed\":%d}"),
		*SearchPath, InFolder, LoadedRedirectors.Num());
}

void HandleGetProjectSetting(const FString& Section, const FString& Key, const FString& IniFile,
	FString& OutJsonString, FString& OutError)
{

	if (Section.IsEmpty() || Key.IsEmpty()) { OutError = TEXT("section and key are required"); return; }

	FString IniPath;
	FString IniLower = IniFile.ToLower();
	if (IniLower.Contains(TEXT("engine"))) IniPath = GEngineIni;
	else if (IniLower.Contains(TEXT("game"))) IniPath = GGameIni;
	else if (IniLower.Contains(TEXT("input"))) IniPath = GInputIni;
	else if (IniLower.Contains(TEXT("editor"))) IniPath = GEditorIni;
	else IniPath = GEngineIni;

	FString Value;
	bool bFound = GConfig->GetString(*Section, *Key, Value, IniPath);

	TSharedPtr<FJsonObject> Res = MakeShareable(new FJsonObject());
	Res->SetBoolField(TEXT("success"), true);
	Res->SetBoolField(TEXT("found"), bFound);
	Res->SetStringField(TEXT("section"), Section);
	Res->SetStringField(TEXT("key"), Key);
	if (bFound) Res->SetStringField(TEXT("value"), Value);
	Res->SetStringField(TEXT("ini_file"), IniPath);

	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Res.ToSharedRef(), W);
}

void HandleSetProjectSetting(const FString& Section, const FString& Key, const FString& Value, const FString& IniFile,
	FString& OutJsonString, FString& OutError)
{

	if (Section.IsEmpty() || Key.IsEmpty()) { OutError = TEXT("section and key are required"); return; }

	FString IniPath;
	FString IniLower = IniFile.ToLower();
	if (IniLower.Contains(TEXT("engine"))) IniPath = GEngineIni;
	else if (IniLower.Contains(TEXT("game"))) IniPath = GGameIni;
	else if (IniLower.Contains(TEXT("input"))) IniPath = GInputIni;
	else if (IniLower.Contains(TEXT("editor"))) IniPath = GEditorIni;
	else IniPath = GEngineIni;

	GConfig->SetString(*Section, *Key, *Value, IniPath);
	GConfig->Flush(false, IniPath);

	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"section\":\"%s\",\"key\":\"%s\",\"value\":\"%s\"}"),
		*Section, *Key, *Value);
}

void HandleBatchRenameAssets(const FString& FolderPath, const FString& Find, const FString& Replace,
	const FString& Prefix, const FString& Suffix, FString& OutJsonString, FString& OutError)
{

	if (FolderPath.IsEmpty()) { OutError = TEXT("folder_path is required"); return; }
	if (Find.IsEmpty() && Prefix.IsEmpty() && Suffix.IsEmpty()) { OutError = TEXT("At least one of find, prefix, or suffix is required"); return; }

	FAssetRegistryModule& RegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	IAssetRegistry& Registry = RegistryModule.Get();

	TArray<FAssetData> Assets;
	Registry.GetAssetsByPath(FName(*FolderPath), Assets, true);

	int32 Renamed = 0;
	TArray<TSharedPtr<FJsonValue>> Results;
	for (const FAssetData& AD : Assets)
	{
		if (AD.AssetClassPath == UObjectRedirector::StaticClass()->GetClassPathName()) continue;

		FString OldName = AD.AssetName.ToString();
		FString NewName = OldName;

		if (!Find.IsEmpty())
			NewName = NewName.Replace(*Find, *Replace);
		if (!Prefix.IsEmpty())
			NewName = Prefix + NewName;
		if (!Suffix.IsEmpty())
			NewName = NewName + Suffix;

		if (NewName == OldName) continue;

		FString OldPath = AD.PackageName.ToString();
		FString Dir = FPaths::GetPath(OldPath);
		FString NewPath = Dir / NewName;

		UObject* Asset = AD.GetAsset();
		if (!Asset) continue;

		TArray<FAssetRenameData> RenameData;
		RenameData.Add(FAssetRenameData(Asset, Dir, NewName));

		FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
		bool bOk = AssetToolsModule.Get().RenameAssets(RenameData);

		if (bOk)
		{
			Renamed++;
			TSharedPtr<FJsonObject> R = MakeShareable(new FJsonObject());
			R->SetStringField(TEXT("old"), OldName);
			R->SetStringField(TEXT("new"), NewName);
			Results.Add(MakeShareable(new FJsonValueObject(R)));
		}
	}

	TSharedPtr<FJsonObject> Res = MakeShareable(new FJsonObject());
	Res->SetBoolField(TEXT("success"), true);
	Res->SetNumberField(TEXT("renamed"), Renamed);
	Res->SetNumberField(TEXT("total_scanned"), Assets.Num());
	Res->SetArrayField(TEXT("renames"), Results);

	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Res.ToSharedRef(), W);
}

void HandleCreateEditorUtilityWidgetFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString Name, SavePath;
	Args->TryGetStringField(TEXT("name"), Name);
	Args->TryGetStringField(TEXT("save_path"), SavePath);
	if (SavePath.IsEmpty()) SavePath = FSettingsManager::GetDefaultSavePath();
	HandleCreateEditorUtilityWidget(Name, SavePath, OutJsonString, OutError);
}

void HandleRunEditorUtilityWidgetFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString AssetPath;
	Args->TryGetStringField(TEXT("asset_path"), AssetPath);
	HandleRunEditorUtilityWidget(AssetPath, OutJsonString, OutError);
}

void HandleCreateEditorUtilityBlueprintFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString Name, SavePath;
	Args->TryGetStringField(TEXT("name"), Name);
	Args->TryGetStringField(TEXT("save_path"), SavePath);
	if (SavePath.IsEmpty()) SavePath = FSettingsManager::GetDefaultSavePath();
	HandleCreateEditorUtilityBlueprint(Name, SavePath, OutJsonString, OutError);
}

void HandleExecConsoleCommandFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString Command;
	Args->TryGetStringField(TEXT("command"), Command);

	FString Trimmed = Command.TrimStart();
	if (Trimmed.StartsWith(TEXT("py ")) || Trimmed.StartsWith(TEXT("py\t")) || Trimmed == TEXT("py"))
	{
		FString PythonCode = Trimmed.Mid(3).TrimStart();
		if (PythonCode.Len() >= 2 &&
			((PythonCode.StartsWith(TEXT("\"")) && PythonCode.EndsWith(TEXT("\""))) ||
			 (PythonCode.StartsWith(TEXT("'")) && PythonCode.EndsWith(TEXT("'")))))
		{
			PythonCode = PythonCode.Mid(1, PythonCode.Len() - 2);
		}
		if (IUECPCoreModule::IsAvailable())
		{
			TSharedPtr<FJsonObject> PyArgs = MakeShared<FJsonObject>();
			PyArgs->SetStringField(TEXT("code"), PythonCode);
			PyArgs->SetStringField(TEXT("description"), TEXT("Redirected from exec_console_command"));
			const FUECPToolResult R = IUECPCoreModule::Get().GetToolDispatcher()
				.ExecuteFromArgs(TEXT("execute_python"), PyArgs);
			OutJsonString = R.ResultJson;
			OutError      = R.ErrorMessage;
		}
		else
		{
			OutError = TEXT("Python execution unavailable - UECPCore module not loaded");
		}
		return;
	}

	HandleExecConsoleCommand(Command, OutJsonString, OutError);
}

void HandleTakeViewportScreenshotFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString FilePath;
	Args->TryGetStringField(TEXT("file_path"), FilePath);
	HandleTakeViewportScreenshot(FilePath, OutJsonString, OutError);
}

void HandleSaveAssetFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }

	const TArray<TSharedPtr<FJsonValue>>* PathsArr = nullptr;
	if (!Args->TryGetArrayField(TEXT("asset_paths"), PathsArr))
		if (!Args->TryGetArrayField(TEXT("assets"),  PathsArr))
			Args->TryGetArrayField(TEXT("items"),    PathsArr);

	if (PathsArr && PathsArr->Num() > 0)
	{
		int32 Saved = 0, Failed = 0;
		TArray<TSharedPtr<FJsonValue>> Failures;
		for (const TSharedPtr<FJsonValue>& V : *PathsArr)
		{
			if (!V.IsValid()) continue;
			FString Path;
			if (V->Type == EJson::String) Path = V->AsString();
			else if (auto Obj = V->AsObject()) Obj->TryGetStringField(TEXT("asset_path"), Path);
			if (Path.IsEmpty()) continue;
			FString ItemOut, ItemErr;
			HandleSaveAsset(Path, ItemOut, ItemErr);
			if (ItemErr.IsEmpty()) { ++Saved; }
			else
			{
				++Failed;
				TSharedPtr<FJsonObject> F = MakeShared<FJsonObject>();
				F->SetStringField(TEXT("asset_path"), Path);
				F->SetStringField(TEXT("error"), ItemErr);
				Failures.Add(MakeShared<FJsonValueObject>(F));
			}
		}
		TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetBoolField(TEXT("success"), Failed == 0);
		Root->SetNumberField(TEXT("saved"), Saved);
		Root->SetNumberField(TEXT("failed"), Failed);
		if (Failed > 0) Root->SetArrayField(TEXT("failures"), Failures);
		TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJsonString);
		FJsonSerializer::Serialize(Root.ToSharedRef(), W);
		return;
	}

	FString AssetPath;
	Args->TryGetStringField(TEXT("asset_path"), AssetPath);
	HandleSaveAsset(AssetPath, OutJsonString, OutError);
}

void HandleSaveAllDirtyAssetsFromArgs(const TSharedPtr<FJsonObject>& ,
	FString& OutJsonString, FString& OutError)
{
	HandleSaveAllDirtyAssets(OutJsonString, OutError);
}

void HandleGetConsoleVariableFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString VarName;
	Args->TryGetStringField(TEXT("variable_name"), VarName);
	HandleGetConsoleVariable(VarName, OutJsonString, OutError);
}

void HandleOpenAssetEditorFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString AssetPath;
	Args->TryGetStringField(TEXT("asset_path"), AssetPath);
	HandleOpenAssetEditor(AssetPath, OutJsonString, OutError);
}

void HandleExportAssetFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString AssetPath, ExportPath;
	Args->TryGetStringField(TEXT("asset_path"), AssetPath);
	Args->TryGetStringField(TEXT("export_path"), ExportPath);
	HandleExportAsset(AssetPath, ExportPath, OutJsonString, OutError);
}

void HandleGetOutputLogFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	double Since = 0.0;
	if (Args.IsValid()) Args->TryGetNumberField(TEXT("since_timestamp"), Since);
	FString CategoryFilter, SeverityFilter;
	double LineCount = 50;
	if (Args.IsValid())
	{
		Args->TryGetNumberField(TEXT("line_count"), LineCount);
		Args->TryGetStringField(TEXT("category_filter"), CategoryFilter);
		Args->TryGetStringField(TEXT("severity_filter"), SeverityFilter);
	}
	HandleGetOutputLogSince((int32)LineCount, CategoryFilter, SeverityFilter, Since, OutJsonString, OutError);
}

void HandleGetMapCheckErrorsFromArgs(const TSharedPtr<FJsonObject>& ,
	FString& OutJsonString, FString& OutError)
{
	HandleGetMapCheckErrors(OutJsonString, OutError);
}

void HandleValidateAssetsFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	TArray<FString> Paths;
	const TArray<TSharedPtr<FJsonValue>>* PathsArr = nullptr;
	if (Args->TryGetArrayField(TEXT("asset_paths"), PathsArr))
	{
		for (const auto& V : *PathsArr) { FString S; if (V->TryGetString(S)) Paths.Add(S); }
	}
	else
	{
		FString Single;
		Args->TryGetStringField(TEXT("asset_path"), Single);
		if (!Single.IsEmpty()) Paths.Add(Single);
	}

	{
		FAssetRegistryModule& AR = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		const int32 ExpandCap = 500;
		bool bTruncated = false;
		TArray<FString> Expanded;
		for (const FString& P : Paths)
		{
			FARFilter Filter;
			Filter.PackagePaths.Add(FName(*P));
			Filter.bRecursivePaths = true;
			TArray<FAssetData> Hits;
			AR.Get().GetAssets(Filter, Hits);
			if (Hits.Num() > 0)
			{
				for (const FAssetData& AD : Hits)
				{
					if (Expanded.Num() >= ExpandCap) { bTruncated = true; break; }
					Expanded.Add(AD.GetObjectPathString());
				}
			}
			else
			{
				Expanded.Add(P);
			}
		}
		Paths = MoveTemp(Expanded);
		if (bTruncated)
		{
			HandleValidateAssets(Paths, OutJsonString, OutError);
			if (OutError.IsEmpty() && !OutJsonString.IsEmpty())
			{
				TSharedPtr<FJsonObject> RootJson;
				TSharedRef<TJsonReader<>> Rd = TJsonReaderFactory<>::Create(OutJsonString);
				if (FJsonSerializer::Deserialize(Rd, RootJson) && RootJson.IsValid())
				{
					RootJson->SetBoolField(TEXT("truncated"), true);
					RootJson->SetStringField(TEXT("truncated_note"), FString::Printf(TEXT("Folder expansion capped at %d assets. Validate sub-folders individually for the rest."), ExpandCap));
					TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJsonString);
					FJsonSerializer::Serialize(RootJson.ToSharedRef(), W);
				}
			}
			return;
		}
	}

	HandleValidateAssets(Paths, OutJsonString, OutError);
}

void HandleFixUpRedirectorsFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString FolderPath;
	Args->TryGetStringField(TEXT("folder_path"), FolderPath);
	HandleFixUpRedirectors(FolderPath, OutJsonString, OutError);
}

void HandleGetProjectSettingFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString Section, Key, IniFile;
	Args->TryGetStringField(TEXT("section"), Section);
	Args->TryGetStringField(TEXT("key"), Key);
	Args->TryGetStringField(TEXT("ini_file"), IniFile);
	if (IniFile.IsEmpty()) IniFile = TEXT("engine");
	HandleGetProjectSetting(Section, Key, IniFile, OutJsonString, OutError);
}

void HandleSetProjectSettingFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString Section, Key, Value, IniFile;
	Args->TryGetStringField(TEXT("section"), Section);
	Args->TryGetStringField(TEXT("key"), Key);
	Args->TryGetStringField(TEXT("value"), Value);
	Args->TryGetStringField(TEXT("ini_file"), IniFile);
	if (IniFile.IsEmpty()) IniFile = TEXT("engine");
	HandleSetProjectSetting(Section, Key, Value, IniFile, OutJsonString, OutError);
}

void HandleBatchRenameAssetsFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString FolderPath, Find, Replace, Prefix, Suffix;
	Args->TryGetStringField(TEXT("folder_path"), FolderPath);
	Args->TryGetStringField(TEXT("find"), Find);
	Args->TryGetStringField(TEXT("replace"), Replace);
	Args->TryGetStringField(TEXT("prefix"), Prefix);
	Args->TryGetStringField(TEXT("suffix"), Suffix);
	HandleBatchRenameAssets(FolderPath, Find, Replace, Prefix, Suffix, OutJsonString, OutError);
}

}
