// Copyright 2026, BlueprintsLab, All rights reserved

#include "ScannerAutoRefresh.h"
#include "Describers/BpIssue.h"
#include "Describers/BpSummarizer.h"
#include "Services/ProjectIndexSchema.h"
#include "Managers/SettingsManager.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Async/Async.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/Blueprint.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
	constexpr float TickInterval        = 1.0f;
	constexpr double DebounceSeconds    = 5.0;

	int64 GetPackageMtime(const FName& PackageName)
	{
		const FString PkgFile = FPackageName::LongPackageNameToFilename(
			PackageName.ToString(), FPackageName::GetAssetPackageExtension());
		const FDateTime Stamp = IFileManager::Get().GetTimeStamp(*PkgFile);
		return Stamp == FDateTime::MinValue() ? 0 : Stamp.ToUnixTimestamp();
	}
}

FScannerAutoRefresh::FScannerAutoRefresh()
{
	if (!FModuleManager::Get().IsModuleLoaded(TEXT("AssetRegistry"))) return;

	IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	UpdateHandle = Registry.OnAssetUpdated().AddRaw(this, &FScannerAutoRefresh::OnAssetUpdated);
	RenameHandle = Registry.OnAssetRenamed().AddRaw(this, &FScannerAutoRefresh::OnAssetRenamed);
	RemoveHandle = Registry.OnAssetRemoved().AddRaw(this, &FScannerAutoRefresh::OnAssetRemoved);

	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateRaw(this, &FScannerAutoRefresh::TickDebounce),
		TickInterval);
}

FScannerAutoRefresh::~FScannerAutoRefresh()
{
	if (FModuleManager::Get().IsModuleLoaded(TEXT("AssetRegistry")))
	{
		IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
		if (UpdateHandle.IsValid()) Registry.OnAssetUpdated().Remove(UpdateHandle);
		if (RenameHandle.IsValid()) Registry.OnAssetRenamed().Remove(RenameHandle);
		if (RemoveHandle.IsValid()) Registry.OnAssetRemoved().Remove(RemoveHandle);
	}
	if (TickerHandle.IsValid()) FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
}

bool FScannerAutoRefresh::IsEnabled() const
{
	bool bEnabled = true;
	GConfig->GetBool(TEXT("BpGeneratorUltimate"), TEXT("Scanner_AutoRefresh"),
		bEnabled, FSettingsManager::GetGlobalConfigPath());
	return bEnabled;
}

bool FScannerAutoRefresh::IsBlueprintAsset(const FAssetData& Data) const
{
	const FString Class = Data.AssetClassPath.ToString();
	return Class.Contains(TEXT("Blueprint"));
}

void FScannerAutoRefresh::OnAssetUpdated(const FAssetData& Data)
{
	if (!IsEnabled()) return;
	if (!IsBlueprintAsset(Data)) return;
	PendingUpdates.Add(Data.GetSoftObjectPath().ToString());
	PendingRemovals.Remove(Data.GetSoftObjectPath().ToString());
	LastChangeTime = FPlatformTime::Seconds();
}

void FScannerAutoRefresh::OnAssetRenamed(const FAssetData& Data, const FString& OldPath)
{
	if (!IsEnabled()) return;
	if (!IsBlueprintAsset(Data)) return;
	PendingRemovals.Add(OldPath);
	PendingUpdates.Add(Data.GetSoftObjectPath().ToString());
	LastChangeTime = FPlatformTime::Seconds();
}

void FScannerAutoRefresh::OnAssetRemoved(const FAssetData& Data)
{
	if (!IsEnabled()) return;
	const FString Path = Data.GetSoftObjectPath().ToString();
	PendingUpdates.Remove(Path);
	PendingRemovals.Add(Path);
	LastChangeTime = FPlatformTime::Seconds();
}

bool FScannerAutoRefresh::TickDebounce(float )
{
	if (PendingUpdates.Num() == 0 && PendingRemovals.Num() == 0) return true;

	const double Now = FPlatformTime::Seconds();
	if ((Now - LastChangeTime) < DebounceSeconds) return true;

	ProcessPending();
	return true;
}

void FScannerAutoRefresh::ProcessPending()
{
	const TArray<FString> Updates = PendingUpdates.Array();
	const TArray<FString> Removals = PendingRemovals.Array();
	PendingUpdates.Empty();
	PendingRemovals.Empty();

	const FString IndexPath = FPaths::ProjectSavedDir() / TEXT("AI") / TEXT("ProjectIndex.json");
	if (!FPaths::FileExists(IndexPath)) return;

	FString Content;
	if (!FFileHelper::LoadFileToString(Content, *IndexPath)) return;

	TSharedPtr<FJsonObject> Index;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Content);
	if (!FJsonSerializer::Deserialize(Reader, Index) || !Index.IsValid()) return;

	double Version = 0.0;
	if (!Index->TryGetNumberField(UECPProjectIndex::MetaVersion, Version)
		|| (int32)Version != UECPProjectIndex::SchemaVersion)
		return;

	bool bChanged = false;

	for (const FString& Path : Removals)
	{
		if (Index->HasField(Path))
		{
			Index->RemoveField(Path);
			bChanged = true;
		}
	}

	if (Updates.Num() > 0)
	{
		FAssetRegistryModule& AR = FModuleManager::GetModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		for (const FString& Path : Updates)
		{
			const FAssetData AssetData = AR.Get().GetAssetByObjectPath(FSoftObjectPath(Path));
			if (!AssetData.IsValid()) continue;

			UObject* LoadedAsset = FindObject<UObject>(nullptr, *Path);
			if (!LoadedAsset)
			{
				LoadedAsset = LoadObject<UObject>(nullptr, *Path, nullptr, LOAD_Quiet | LOAD_NoWarn);
			}
			if (!IsValid(LoadedAsset)) continue;

			UBlueprint* Blueprint = Cast<UBlueprint>(LoadedAsset);
			if (!Blueprint) continue;

			FBpSummarizer Summarizer;
			TArray<FBpIssue> Issues;
			const FString Summary       = Summarizer.SummarizeWithIssues(Blueprint, Issues);
			const FString ShortSummary  = Summarizer.ComputeShortSummary(Blueprint);
			const int32   Complexity    = Summarizer.ComputeComplexityScore(Blueprint);

			TSharedPtr<FJsonObject> Entry = MakeShareable(new FJsonObject);
			const FString TypeTag = (Blueprint->BlueprintType == BPTYPE_Interface)
				? UECPProjectIndex::Type::Interface
				: UECPProjectIndex::Type::Blueprint;
			Entry->SetStringField(UECPProjectIndex::FieldType, TypeTag);
			Entry->SetStringField(UECPProjectIndex::FieldSummary, Summary);
			if (!ShortSummary.IsEmpty()) Entry->SetStringField(UECPProjectIndex::FieldShortSummary, ShortSummary);
			if (Complexity > 0)          Entry->SetNumberField(UECPProjectIndex::FieldComplexity, Complexity);
			Entry->SetNumberField(UECPProjectIndex::FieldMtime, (double)GetPackageMtime(AssetData.PackageName));

			if (Blueprint->ParentClass)
			{
				const FString ParentName = Blueprint->ParentClass->GetName();
				Entry->SetStringField(UECPProjectIndex::FieldParent, ParentName);
				FString Subtype;
				if      (ParentName.Contains(TEXT("UserWidget")) || ParentName.Contains(TEXT("Widget"))) Subtype = TEXT("Widget");
				else if (ParentName.Contains(TEXT("AnimInstance")))                                       Subtype = TEXT("AnimBP");
				else if (ParentName.Contains(TEXT("Actor")))                                              Subtype = TEXT("Actor");
				else                                                                                       Subtype = TEXT("Other");
				Entry->SetStringField(UECPProjectIndex::FieldSubtype, Subtype);
			}

			if (Issues.Num() > 0)
			{
				TArray<TSharedPtr<FJsonValue>> Arr;
				for (const FBpIssue& I : Issues)
				{
					TSharedPtr<FJsonObject> IO = MakeShareable(new FJsonObject);
					IO->SetStringField(UECPProjectIndex::IssueSeverity, FBpIssue::SeverityToString(I.Severity));
					IO->SetStringField(UECPProjectIndex::IssueCode,     I.Code);
					IO->SetStringField(UECPProjectIndex::IssueMessage,  I.Message);
					Arr.Add(MakeShareable(new FJsonValueObject(IO)));
				}
				Entry->SetArrayField(UECPProjectIndex::FieldIssues, Arr);
			}

			Index->SetObjectField(Path, Entry);
			bChanged = true;
		}
	}

	if (!bChanged) return;

	Index->SetStringField(UECPProjectIndex::MetaScanTimestamp, FDateTime::UtcNow().ToString());

	FString OutStr;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutStr);
	FJsonSerializer::Serialize(Index.ToSharedRef(), Writer);
	FFileHelper::SaveStringToFile(OutStr, *IndexPath);

	UE_LOG(LogTemp, Log, TEXT("Scanner auto-refresh patched %d update(s) + %d removal(s) into ProjectIndex.json"),
		Updates.Num(), Removals.Num());
}
