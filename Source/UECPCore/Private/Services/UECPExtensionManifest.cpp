// Copyright 2026, BlueprintsLab, All rights reserved

#include "Services/UECPExtensionManifest.h"

#include "ExtensionSDK.h"
#include "Managers/UpdateManager.h"
#include "UECPCoreModule.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Mcp/UECPMcpClient.h"
#include "Runtime/Launch/Resources/Version.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	FString ExpandPluginDir(const FString& In, const FString& PluginDir)
	{
		if (In.IsEmpty() || !In.Contains(TEXT("${plugin_dir}"))) return In;
		FString Out = In;
		Out.ReplaceInline(TEXT("${plugin_dir}"), *PluginDir, ESearchCase::CaseSensitive);
		return Out;
	}

	FString ReadString(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Key, const FString& PluginDir)
	{
		if (!Obj.IsValid()) return FString();
		FString Val;
		if (!Obj->TryGetStringField(Key, Val)) return FString();
		return ExpandPluginDir(Val, PluginDir);
	}

	bool ReadBool(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Key, bool DefaultValue)
	{
		if (!Obj.IsValid()) return DefaultValue;
		bool B = DefaultValue;
		Obj->TryGetBoolField(Key, B);
		return B;
	}

	int32 ReadInt(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Key, int32 DefaultValue)
	{
		if (!Obj.IsValid()) return DefaultValue;
		int32 I = DefaultValue;
		Obj->TryGetNumberField(Key, I);
		return I;
	}

	void ReadStringArray(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Key, TArray<FString>& Out)
	{
		Out.Reset();
		if (!Obj.IsValid()) return;
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (!Obj->TryGetArrayField(Key, Arr) || !Arr) return;
		for (const TSharedPtr<FJsonValue>& V : *Arr)
		{
			if (V.IsValid() && V->Type == EJson::String)
				Out.Add(V->AsString());
		}
	}

	void ReadFNameArray(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Key, TArray<FName>& Out)
	{
		Out.Reset();
		TArray<FString> Tmp;
		ReadStringArray(Obj, Key, Tmp);
		Out.Reserve(Tmp.Num());
		for (FString& S : Tmp) Out.Add(FName(*S));
	}

	bool ParseVersionString(const FString& InRaw, int32& OutPacked)
	{
		FString S = InRaw;
		S.TrimStartAndEndInline();
		if (S.IsEmpty()) return false;
		if (S.StartsWith(TEXT("v"), ESearchCase::IgnoreCase)) S.RightChopInline(1, EAllowShrinking::No);

		TArray<FString> Parts;
		S.ParseIntoArray(Parts, TEXT("."),  true);
		if (Parts.Num() == 0) return false;

		auto ToInt = [](const FString& Tok, int32& Out) -> bool
		{
			if (Tok.IsEmpty() || !Tok.IsNumeric()) return false;
			Out = FCString::Atoi(*Tok);
			return Out >= 0;
		};

		int32 Major = 0, Minor = 0, Patch = 0;
		if (!ToInt(Parts[0], Major)) return false;
		if (Parts.Num() >= 2 && !ToInt(Parts[1], Minor)) return false;
		if (Parts.Num() >= 3 && !ToInt(Parts[2], Patch)) return false;

		OutPacked = Major * 10000 + Minor * 100 + Patch;
		return true;
	}
}

namespace UECPExtensionManifest
{

bool TryParseDescriptor(
	const FString& JsonText,
	const FString& PluginDir,
	FUECPExtensionDescriptor& OutDescriptor,
	FString& OutError)
{
	return TryParseDescriptor(JsonText, PluginDir,  FString(), OutDescriptor, OutError);
}

bool TryParseDescriptor(
	const FString& JsonText,
	const FString& PluginDir,
	const FString& ManifestDir,
	FUECPExtensionDescriptor& OutDescriptor,
	FString& OutError)
{
	OutDescriptor = FUECPExtensionDescriptor{};

	TSharedPtr<FJsonObject> Root;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		OutError = TEXT("Manifest is not valid JSON");
		return false;
	}

	const int32 ManifestVersion = ReadInt(Root, TEXT("manifest_version"), 1);
	if (ManifestVersion > CurrentManifestVersion)
	{
		OutError = FString::Printf(
			TEXT("manifest_version %d is newer than this build supports (%d) — rebuild against a newer UECPCore"),
			ManifestVersion, CurrentManifestVersion);
		return false;
	}

	const FString ExtIdStr = ReadString(Root, TEXT("extension_id"), PluginDir);
	if (ExtIdStr.IsEmpty())
	{
		OutError = TEXT("Missing required field: extension_id");
		return false;
	}

	const int32 DeclaredApiVersion = ReadInt(Root, TEXT("api_version"), 0);
	if (DeclaredApiVersion > UECP_EXTENSION_API_VERSION)
	{
		OutError = FString::Printf(
			TEXT("api_version %d is newer than this SDK supports (%d) — rebuild against the current ExtensionSDK.h"),
			DeclaredApiVersion, UECP_EXTENSION_API_VERSION);
		return false;
	}
	OutDescriptor.ApiVersion       = DeclaredApiVersion;
	OutDescriptor.ExtensionId      = FName(*ExtIdStr);
	OutDescriptor.DisplayName      = FText::FromString(ReadString(Root, TEXT("display_name"), PluginDir));
	OutDescriptor.Description      = FText::FromString(ReadString(Root, TEXT("description"), PluginDir));
	OutDescriptor.Category         = FText::FromString(ReadString(Root, TEXT("category"), PluginDir));
	OutDescriptor.CatalogBlurb     = ReadString(Root, TEXT("catalog_blurb"), PluginDir);
	OutDescriptor.RequiredModuleName = FName(*ReadString(Root, TEXT("required_module_name"), PluginDir));
	OutDescriptor.bDefaultEnabled  = ReadBool(Root, TEXT("default_enabled"),  false);
	OutDescriptor.bIsThirdParty    = ReadBool(Root, TEXT("is_third_party"),   false);
	OutDescriptor.bRequiresLicense = ReadBool(Root, TEXT("requires_license"), false);

	const FString LicenseId = ReadString(Root, TEXT("license_feature_id"), PluginDir);
	if (!LicenseId.IsEmpty()) OutDescriptor.LicenseFeatureId = FName(*LicenseId);

	ReadStringArray(Root, TEXT("required_plugins"), OutDescriptor.RequiredPlugins);
	ReadStringArray(Root, TEXT("required_extensions"), OutDescriptor.RequiredExtensions);
	ReadFNameArray (Root, TEXT("owned_umbrellas"),  OutDescriptor.OwnedUmbrellas);
	ReadFNameArray (Root, TEXT("owned_tools"),      OutDescriptor.OwnedTools);

	OutDescriptor.MinEngineVersion = ReadString(Root, TEXT("min_engine_version"), PluginDir);
	OutDescriptor.MaxEngineVersion = ReadString(Root, TEXT("max_engine_version"), PluginDir);

	OutDescriptor.AuthorName = ReadString(Root, TEXT("author_name"), PluginDir);
	OutDescriptor.WebsiteUrl = ReadString(Root, TEXT("website_url"), PluginDir);
	OutDescriptor.IconUrl    = ReadString(Root, TEXT("icon_url"),    PluginDir);

	const FString DocBaseDir = ManifestDir.IsEmpty() ? PluginDir : ManifestDir;
	if (const TSharedPtr<FJsonObject>* DocsObj = nullptr; Root->TryGetObjectField(TEXT("umbrella_docs"), DocsObj) && DocsObj && DocsObj->IsValid())
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& KV : (*DocsObj)->Values)
		{
			if (!KV.Value.IsValid() || KV.Value->Type != EJson::String) continue;
			FString Value = ExpandPluginDir(KV.Value->AsString(), PluginDir);

			const bool bIsPlainPointer = Value.EndsWith(TEXT(".md"), ESearchCase::IgnoreCase);
			const bool bIsEncPointer   = Value.EndsWith(TEXT(".uecpdat"), ESearchCase::IgnoreCase);
			if (bIsPlainPointer || bIsEncPointer)
			{
				const FString CandidatePath = FPaths::IsRelative(Value)
					? FPaths::ConvertRelativePathToFull(DocBaseDir / Value)
					: Value;

				FString DocBody;
				if (bIsEncPointer)
				{
					DocBody = FUpdateManager::ReadCachedPath(CandidatePath);
				}
				else
				{
					FFileHelper::LoadFileToString(DocBody, *CandidatePath);
				}
				if (!DocBody.IsEmpty())
				{
					OutDescriptor.UmbrellaDocs.Add(FName(*KV.Key), MoveTemp(DocBody));
				}
				else
				{
					UE_LOG(LogUECPCore, Warning,
						TEXT("Manifest umbrella_docs: %s -> revision unavailable at: %s"),
						*KV.Key, *CandidatePath);
				}
			}
			else
			{
				OutDescriptor.UmbrellaDocs.Add(FName(*KV.Key), MoveTemp(Value));
			}
		}
	}

	if (const TSharedPtr<FJsonObject>* McpObj = nullptr; Root->TryGetObjectField(TEXT("mcp_server"), McpObj) && McpObj && McpObj->IsValid())
	{
		FUECPMcpServerSpec Spec;
		Spec.Command = ReadString(*McpObj, TEXT("command"), PluginDir);
		ReadStringArray(*McpObj, TEXT("args"), Spec.Args);
		for (FString& Arg : Spec.Args) Arg = ExpandPluginDir(Arg, PluginDir);
		Spec.LogTag = ReadString(*McpObj, TEXT("log_tag"), PluginDir);
		if (!Spec.Command.IsEmpty())
			OutDescriptor.McpServer = MoveTemp(Spec);
	}

	return true;
}

bool IsEngineVersionInRange(const FString& MinVer, const FString& MaxVer)
{
	const int32 RunningPacked =
		ENGINE_MAJOR_VERSION * 10000 +
		ENGINE_MINOR_VERSION * 100 +
		ENGINE_PATCH_VERSION;

	if (!MinVer.IsEmpty())
	{
		int32 MinPacked = 0;
		if (ParseVersionString(MinVer, MinPacked) && RunningPacked < MinPacked)
		{
			return false;
		}
	}
	if (!MaxVer.IsEmpty())
	{
		int32 MaxPacked = 0;
		if (ParseVersionString(MaxVer, MaxPacked) && RunningPacked > MaxPacked)
		{
			return false;
		}
	}
	return true;
}

int32 ScanAndRegisterAll()
{
	if (!IUECPCoreModule::IsAvailable()) return 0;
	IUECPExtensionService& Ext = IUECPCoreModule::Get().GetExtensionService();

	int32 Registered = 0;
	int32 Skipped    = 0;

	const TArray<TSharedRef<IPlugin>> EnabledPlugins = IPluginManager::Get().GetEnabledPlugins();
	for (const TSharedRef<IPlugin>& Plugin : EnabledPlugins)
	{
		const FString PluginDir = Plugin->GetBaseDir();
		if (PluginDir.IsEmpty()) continue;

		TArray<FString> Manifests;
		IFileManager& FM = IFileManager::Get();

		FM.FindFiles(Manifests, *(PluginDir / TEXT("*.uecpext.json")), true, false);
		for (FString& Name : Manifests) Name = FPaths::ConvertRelativePathToFull(PluginDir / Name);

		TArray<FString> ExtFolderManifests;
		FM.FindFiles(ExtFolderManifests, *(PluginDir / TEXT("Extensions") / TEXT("*.uecpext.json")), true, false);
		for (FString& Name : ExtFolderManifests) Name = FPaths::ConvertRelativePathToFull(PluginDir / TEXT("Extensions") / Name);
		Manifests.Append(ExtFolderManifests);

		const FString SourceExtRoot = PluginDir / TEXT("Source") / TEXT("Extensions");
		if (FM.DirectoryExists(*SourceExtRoot))
		{
			TArray<FString> ModuleDirs;
			FM.FindFiles(ModuleDirs, *(SourceExtRoot / TEXT("*")), false, true);
			for (const FString& ModuleDir : ModuleDirs)
			{
				TArray<FString> ModuleManifests;
				const FString ModulePath = SourceExtRoot / ModuleDir;
				FM.FindFiles(ModuleManifests, *(ModulePath / TEXT("*.uecpext.json")), true, false);
				for (FString& Name : ModuleManifests)
					Manifests.Add(FPaths::ConvertRelativePathToFull(ModulePath / Name));
			}
		}

		for (const FString& ManifestPath : Manifests)
		{
			FString JsonText;
			if (!FFileHelper::LoadFileToString(JsonText, *ManifestPath))
			{
				UE_LOG(LogUECPCore, Warning, TEXT("Manifest scan: failed to read %s"), *ManifestPath);
				++Skipped;
				continue;
			}

			FUECPExtensionDescriptor Desc;
			FString ParseError;
			const FString ManifestDir = FPaths::GetPath(ManifestPath);
			if (!TryParseDescriptor(JsonText, PluginDir, ManifestDir, Desc, ParseError))
			{
				UE_LOG(LogUECPCore, Warning, TEXT("Manifest scan: %s — %s"), *ManifestPath, *ParseError);
				++Skipped;
				continue;
			}

			Ext.RegisterExtension(MoveTemp(Desc));
			++Registered;
		}
	}

	UE_LOG(LogUECPCore, Log,
		TEXT("Manifest scan: registered %d extension(s); skipped %d"),
		Registered, Skipped);
	return Registered;
}

}
