// Copyright 2026, BlueprintsLab, All rights reserved

#include "Widget/UUECPScannerBridge.h"
#include "UECPCoreModule.h"
#include "Services/IUECPBugReportService.h"
#include "Services/IUECPGddService.h"
#include "Services/IUECPAiMemoryService.h"
#include "Services/IUECPVoiceService.h"
#include "Services/IUECPAnalystService.h"
#include "Services/IUECPScannerService.h"
#include "Services/IUECPArchitectService.h"
#include "Services/IUECPAgentRunnerService.h"
#include "Services/IUECPArchitectService.h"
#include "Services/IUECPCrewService.h"
#include "Services/IUECPACPRegistryService.h"
#include "Services/IUECPExtensionService.h"
#include "Services/IUECPToolDispatcher.h"
#include "Types/CrewTypes.h"
#include "ApiKeyManager.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Managers/HttpCommunicationManager.h"
#include "Managers/FreeTierConfigManager.h"
#include "Managers/ChatHistoryManager.h"
#include "AssetReferenceManager.h"
#include "SUECPMainWidget.h"
#include "Managers/PlanManager.h"
#include "Managers/TaskManager.h"
#include "SWebBrowser.h"
#include "Misc/FileHelper.h"
#include "Misc/Base64.h"
#include "Interfaces/IPluginManager.h"
#include "Utils/MountResolver.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "AssetReferenceTypes.h"
#include "Managers/ChatHistoryManager.h"
#include "Managers/SettingsManager.h"
#include "Serialization/JsonSerializer.h"
#include "SBlueprintDiff.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonWriter.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Tools/AssetPropertyTools.h"
#include "Tools/ProfilerTools.h"
#include "MeshAssetManager.h"
#include "Async/Async.h"
#include "DesktopPlatformModule.h"
#include "IDesktopPlatform.h"
#include "Engine/SkeletalMesh.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "GenericPlatform/GenericPlatformHttp.h"
#include "Utils/DiagramUtils.h"
#include "Widgets/SWindow.h"
#include "Framework/Application/SlateApplication.h"
#include "Containers/Ticker.h"

#define LOCTEXT_NAMESPACE "UUECPScannerBridge"

void UUECPScannerBridge::ScanProject()
{
	IUECPCoreModule::Get().GetScannerService().ScanProject();
}

namespace
{
	struct FScanTypeDef { const TCHAR* Key; const TCHAR* Label; bool bDefaultOn; };
	static const FScanTypeDef GScanTypes[] = {
		{ TEXT("Blueprint"),       TEXT("Blueprints (Actor/Pawn/Char)"), true  },
		{ TEXT("Widget"),          TEXT("UMG Widgets"),                  true  },
		{ TEXT("AnimBlueprint"),   TEXT("Animation Blueprints"),         true  },
		{ TEXT("Interface"),       TEXT("Blueprint Interfaces"),         true  },
		{ TEXT("BehaviorTree"),    TEXT("Behavior Trees"),               true  },
		{ TEXT("Enum"),            TEXT("User-Defined Enums"),           true  },
		{ TEXT("Struct"),          TEXT("User-Defined Structs"),         true  },
		{ TEXT("DataAsset"),       TEXT("Data Assets"),                  true  },
		{ TEXT("DataTable"),       TEXT("Data Tables"),                  true  },
		{ TEXT("Material"),        TEXT("Materials (slow + crash risk)"),false },
		{ TEXT("Texture"),         TEXT("Textures"),                     false },
		{ TEXT("StaticMesh"),      TEXT("Static Meshes"),                false },
		{ TEXT("SkeletalMesh"),    TEXT("Skeletal Meshes"),              false },
		{ TEXT("AnimSequence"),    TEXT("Anim Sequences/Montages"),      false },
		{ TEXT("Sound"),           TEXT("Sound Cues / Waves"),           false },
		{ TEXT("Niagara"),         TEXT("Niagara Systems/Emitters"),     false },
		{ TEXT("LevelSequence"),   TEXT("Level Sequences"),              false },
		{ TEXT("ParticleSystem"),  TEXT("Cascade Particle Systems"),     false },
		{ TEXT("PhysicsAsset"),    TEXT("Physics Assets"),               false },
		{ TEXT("Curve"),           TEXT("Curve Assets"),                 false },
	};

	static FString GetScanTypeIniKey(const FString& TypeKey) { return FString::Printf(TEXT("ScanType_%s"), *TypeKey); }
}

void UUECPScannerBridge::RequestScannerTypeFilters()
{
	const FString Section = TEXT("BpGeneratorUltimate");
	TArray<TSharedPtr<FJsonValue>> Arr;
	for (const FScanTypeDef& Def : GScanTypes)
	{
		bool bEnabled = Def.bDefaultOn;
		GConfig->GetBool(*Section, *GetScanTypeIniKey(Def.Key), bEnabled, GEditorPerProjectIni);

		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("key"), Def.Key);
		Obj->SetStringField(TEXT("label"), Def.Label);
		Obj->SetBoolField(TEXT("enabled"), bEnabled);
		Obj->SetBoolField(TEXT("default"), Def.bDefaultOn);
		Arr.Add(MakeShared<FJsonValueObject>(Obj));
	}
	FString Json;
	TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Json);
	FJsonSerializer::Serialize(Arr, Writer);

	ExecJs(FString::Printf(TEXT("window.onScannerTypeFilters && window.onScannerTypeFilters(%s);"), *Json));
}

void UUECPScannerBridge::SetScannerTypeFilter(const FString& TypeKey, bool bEnabled)
{
	const FString Section = TEXT("BpGeneratorUltimate");
	GConfig->SetBool(*Section, *GetScanTypeIniKey(TypeKey), bEnabled, GEditorPerProjectIni);
	GConfig->Flush(false, GEditorPerProjectIni);
}

namespace
{
	static FString SanitizeIniKeyPart(const FString& In)
	{
		FString Out = In;
		Out.ReplaceInline(TEXT("/"), TEXT("_"));
		Out.ReplaceInline(TEXT("\\"), TEXT("_"));
		Out.ReplaceInline(TEXT(" "), TEXT("_"));
		Out.ReplaceInline(TEXT("."), TEXT("_"));
		return Out;
	}
	static FString GetScanDirIniKey(const FString& DirPath) { return FString::Printf(TEXT("ScanDir_%s"), *SanitizeIniKeyPart(DirPath)); }
	static FString GetScanPluginIniKey(const FString& PluginName) { return FString::Printf(TEXT("ScanPlugin_%s"), *SanitizeIniKeyPart(PluginName)); }
}

void UUECPScannerBridge::RequestScannerDirectories()
{
	FAssetRegistryModule& Reg = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	TArray<FString> TopLevel;
	Reg.Get().GetSubPaths(TEXT("/Game"), TopLevel, false);
	TopLevel.Sort();

	const FString Section = TEXT("BpGeneratorUltimate");
	TArray<TSharedPtr<FJsonValue>> Arr;

	auto Emit = [&Arr, &Section](const FString& Path, const FString& Label)
	{
		bool bEnabled = true;
		GConfig->GetBool(*Section, *GetScanDirIniKey(Path), bEnabled, GEditorPerProjectIni);
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("path"), Path);
		Obj->SetStringField(TEXT("label"), Label);
		Obj->SetBoolField(TEXT("enabled"), bEnabled);
		Arr.Add(MakeShared<FJsonValueObject>(Obj));
	};

	Emit(TEXT("/Game"), TEXT("/Game (root)"));

	for (const FString& Path : TopLevel)
	{
		Emit(Path, Path);

		TArray<FString> Children;
		Reg.Get().GetSubPaths(Path, Children, false);
		Children.Sort();
		if (Children.Num() > 30) Children.SetNum(30);
		for (const FString& Child : Children)
		{
			Emit(Child, FString(TEXT("    ")) + Child);
		}
	}

	FString Json;
	TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Json);
	FJsonSerializer::Serialize(Arr, Writer);
	ExecJs(FString::Printf(TEXT("window.onScannerDirectories && window.onScannerDirectories(%s);"), *Json));
}

void UUECPScannerBridge::SetScannerDirectory(const FString& DirPath, bool bEnabled)
{
	const FString Section = TEXT("BpGeneratorUltimate");
	GConfig->SetBool(*Section, *GetScanDirIniKey(DirPath), bEnabled, GEditorPerProjectIni);
	GConfig->Flush(false, GEditorPerProjectIni);
}

void UUECPScannerBridge::RequestScannerPlugins()
{
	const FString Section = TEXT("BpGeneratorUltimate");
	TArray<TSharedPtr<FJsonValue>> Arr;
	IPluginManager& PluginMgr = IPluginManager::Get();
	TArray<TSharedRef<IPlugin>> EnabledWithContent = PluginMgr.GetEnabledPluginsWithContent();

	EnabledWithContent.Sort([](const TSharedRef<IPlugin>& A, const TSharedRef<IPlugin>& B) {
		return A->GetName() < B->GetName();
	});

	for (const TSharedRef<IPlugin>& P : EnabledWithContent)
	{
		const FString Name = P->GetName();
		const FString MountPath = P->GetMountedAssetPath();
		bool bEnabled = false;
		GConfig->GetBool(*Section, *GetScanPluginIniKey(Name), bEnabled, GEditorPerProjectIni);

		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("name"), Name);
		Obj->SetStringField(TEXT("mount_path"), MountPath);
		Obj->SetBoolField(TEXT("enabled"), bEnabled);
		Arr.Add(MakeShared<FJsonValueObject>(Obj));
	}

	FString Json;
	TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Json);
	FJsonSerializer::Serialize(Arr, Writer);
	ExecJs(FString::Printf(TEXT("window.onScannerPlugins && window.onScannerPlugins(%s);"), *Json));
}

void UUECPScannerBridge::SetScannerPlugin(const FString& PluginName, bool bEnabled)
{
	const FString Section = TEXT("BpGeneratorUltimate");
	GConfig->SetBool(*Section, *GetScanPluginIniKey(PluginName), bEnabled, GEditorPerProjectIni);
	GConfig->Flush(false, GEditorPerProjectIni);
}

void UUECPScannerBridge::RequestScannerPluginSources()
{
	const FString Section = TEXT("BpGeneratorUltimate");
	TArray<TSharedPtr<FJsonValue>> Arr;
	IPluginManager& PluginMgr = IPluginManager::Get();
	TArray<TSharedRef<IPlugin>> AllPlugins = PluginMgr.GetEnabledPlugins();
	AllPlugins.Sort([](const TSharedRef<IPlugin>& A, const TSharedRef<IPlugin>& B) { return A->GetName() < B->GetName(); });

	for (const TSharedRef<IPlugin>& P : AllPlugins)
	{
		const FString Name = P->GetName();
		const FString SourceDir = FPaths::Combine(P->GetBaseDir(), TEXT("Source"));
		if (!FPaths::DirectoryExists(SourceDir)) continue;

		bool bEnabled = false;
		GConfig->GetBool(*Section,
			*FString::Printf(TEXT("ScanPluginSource_%s"), *SanitizeIniKeyPart(Name)),
			bEnabled, GEditorPerProjectIni);

		TArray<FString> Headers;
		IFileManager::Get().FindFilesRecursive(Headers, *SourceDir, TEXT("*.h"), true, false);

		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("name"), Name);
		Obj->SetStringField(TEXT("source_dir"), SourceDir);
		Obj->SetNumberField(TEXT("header_count"), Headers.Num());
		Obj->SetBoolField(TEXT("enabled"), bEnabled);
		Arr.Add(MakeShared<FJsonValueObject>(Obj));
	}
	FString Json;
	TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Json);
	FJsonSerializer::Serialize(Arr, Writer);
	ExecJs(FString::Printf(TEXT("window.onScannerPluginSources && window.onScannerPluginSources(%s);"), *Json));
}

void UUECPScannerBridge::SetScannerPluginSource(const FString& PluginName, bool bEnabled)
{
	const FString Section = TEXT("BpGeneratorUltimate");
	GConfig->SetBool(*Section,
		*FString::Printf(TEXT("ScanPluginSource_%s"), *SanitizeIniKeyPart(PluginName)),
		bEnabled, GEditorPerProjectIni);
	GConfig->Flush(false, GEditorPerProjectIni);
}

namespace
{
	struct FScanOptionDef { const TCHAR* Key; const TCHAR* Label; const TCHAR* Description; bool bDefaultOn; };
	static const FScanOptionDef GScanOptions[] = {
		{ TEXT("Scan_ProjectSource"), TEXT("Scan project C++ source"),
		  TEXT("Index .h files in the project's Source/ folder so the AI understands your native classes."), true },
		{ TEXT("Scan_Verbose"), TEXT("Verbose scan logging"),
		  TEXT("Print per-asset progress to the Output Log. Useful for diagnosing which asset crashes the editor."), false },
		{ TEXT("Scan_SkipPlayInEditorLevels"), TEXT("Skip temp levels"),
		  TEXT("Exclude UEDPIE_*, UEDPIE_LOAD, Untitled, and /Temp worlds from the scan."), true },
	};
}

void UUECPScannerBridge::RequestScannerOptions()
{
	const FString Section = TEXT("BpGeneratorUltimate");
	TArray<TSharedPtr<FJsonValue>> Arr;
	for (const FScanOptionDef& Def : GScanOptions)
	{
		bool bEnabled = Def.bDefaultOn;
		GConfig->GetBool(*Section, Def.Key, bEnabled, GEditorPerProjectIni);

		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("key"), Def.Key);
		Obj->SetStringField(TEXT("label"), Def.Label);
		Obj->SetStringField(TEXT("description"), Def.Description);
		Obj->SetBoolField(TEXT("enabled"), bEnabled);
		Obj->SetBoolField(TEXT("default"), Def.bDefaultOn);
		Arr.Add(MakeShared<FJsonValueObject>(Obj));
	}
	FString Json;
	TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Json);
	FJsonSerializer::Serialize(Arr, Writer);
	ExecJs(FString::Printf(TEXT("window.onScannerOptions && window.onScannerOptions(%s);"), *Json));
}

void UUECPScannerBridge::SetScannerOption(const FString& Key, bool bEnabled)
{
	const FString Section = TEXT("BpGeneratorUltimate");
	GConfig->SetBool(*Section, *Key, bEnabled, GEditorPerProjectIni);
	GConfig->Flush(false, GEditorPerProjectIni);
}

void UUECPScannerBridge::RequestScannerResponseMode()
{
	FString Mode;
	GConfig->GetString(TEXT("BpGeneratorUltimate"), TEXT("Scanner_ResponseMode"),
		Mode, FSettingsManager::GetGlobalConfigPath());
	Mode = Mode.ToLower();
	if (Mode != TEXT("ai") && Mode != TEXT("index")) Mode = TEXT("auto");
	ExecJs(FString::Printf(TEXT("window.onScannerResponseMode && window.onScannerResponseMode(\"%s\");"), *Mode));
}

void UUECPScannerBridge::SetScannerResponseMode(const FString& Mode)
{
	FString Normalised = Mode.ToLower();
	if (Normalised != TEXT("ai") && Normalised != TEXT("index")) Normalised = TEXT("auto");
	GConfig->SetString(TEXT("BpGeneratorUltimate"), TEXT("Scanner_ResponseMode"),
		*Normalised, FSettingsManager::GetGlobalConfigPath());
	GConfig->Flush(false, FSettingsManager::GetGlobalConfigPath());
}

void UUECPScannerBridge::ResetScannerSettings()
{
	const FString Section = TEXT("BpGeneratorUltimate");

	auto RemoveKey = [&Section](const FString& Key)
	{
		GConfig->RemoveKey(*Section, *Key, GEditorPerProjectIni);
	};

	for (const FScanTypeDef& Def : GScanTypes)
	{
		RemoveKey(GetScanTypeIniKey(Def.Key));
	}

	for (const FScanOptionDef& Def : GScanOptions)
	{
		RemoveKey(Def.Key);
	}

	{
		FAssetRegistryModule& Reg = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		TArray<FString> SubPaths;
		Reg.Get().GetSubPaths(TEXT("/Game"), SubPaths, false);
		RemoveKey(GetScanDirIniKey(TEXT("/Game")));
		for (const FString& P : SubPaths) RemoveKey(GetScanDirIniKey(P));
	}

	{
		IPluginManager& PluginMgr = IPluginManager::Get();
		TArray<TSharedRef<IPlugin>> AllPlugins = PluginMgr.GetEnabledPlugins();
		for (const TSharedRef<IPlugin>& P : AllPlugins)
		{
			const FString Name = P->GetName();
			RemoveKey(GetScanPluginIniKey(Name));
			RemoveKey(FString::Printf(TEXT("ScanPluginSource_%s"), *SanitizeIniKeyPart(Name)));
		}
	}

	GConfig->Flush(false, GEditorPerProjectIni);
}

void UUECPScannerBridge::ClearScannerCrashSkipList()
{
	const FString Path = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("BpGeneratorUltimate"), TEXT("scan_skip_list.txt"));
	IFileManager::Get().Delete(*Path, false, true, true);
}

void UUECPScannerBridge::GetPerformanceReport()
{
	IUECPCoreModule::Get().GetScannerService().GetPerformanceReport();
}

void UUECPScannerBridge::GetProjectOverview()
{
	IUECPCoreModule::Get().GetScannerService().GetProjectOverview();
}

void UUECPScannerBridge::OpenDashboard()
{
	auto W = OwnerWidget.Pin();
	if (!W.IsValid()) return;
	TSharedPtr<FJsonObject> EmptyArgs = MakeShareable(new FJsonObject);
	W->ExecuteTool_OpenProjectDashboard(EmptyArgs);
}

#undef LOCTEXT_NAMESPACE
