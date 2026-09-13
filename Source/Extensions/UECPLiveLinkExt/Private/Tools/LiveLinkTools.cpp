// Copyright 2026, BlueprintsLab, All rights reserved.

#include "Tools/LiveLinkTools.h"
#include "Tools/BatchToolHelper.h"
#include "Misc/EngineVersionComparison.h"
#include "MCPToolsLog.h"

#include "ILiveLinkClient.h"
#include "LiveLinkPreset.h"
#include "LiveLinkPresetTypes.h"
#include "Features/IModularFeatures.h"
#include "AssetToolsModule.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/SavePackage.h"
#include "PackageTools.h"
#include "FileHelpers.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/Paths.h"

#include "LiveLinkMessageBusSourceFactory.h"

namespace LiveLinkTools
{

static ILiveLinkClient* GetLiveLinkClient()
{
	if (!IModularFeatures::Get().IsModularFeatureAvailable(ILiveLinkClient::ModularFeatureName))
	{
		return nullptr;
	}
	return &IModularFeatures::Get().GetModularFeature<ILiveLinkClient>(ILiveLinkClient::ModularFeatureName);
}

void HandleListLiveLinkSources(FString& OutJson, FString& OutError)
{
	ILiveLinkClient* Client = GetLiveLinkClient();
	if (!Client)
	{
		OutError = TEXT("LiveLink client not available. Ensure the LiveLink plugin is enabled.");
		return;
	}

	TArray<FGuid> SourceGuids = Client->GetSources();
	TArray<TSharedPtr<FJsonValue>> SourceArray;

	for (const FGuid& Guid : SourceGuids)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("guid"), Guid.ToString());
		Obj->SetStringField(TEXT("type"), Client->GetSourceType(Guid).ToString());
		Obj->SetStringField(TEXT("status"), Client->GetSourceStatus(Guid).ToString());
		Obj->SetStringField(TEXT("machine_name"), Client->GetSourceMachineName(Guid).ToString());
		Obj->SetBoolField(TEXT("is_valid"), Client->IsSourceStillValid(Guid));
		SourceArray.Add(MakeShared<FJsonValueObject>(Obj));
	}

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetArrayField(TEXT("sources"), SourceArray);
	Root->SetNumberField(TEXT("count"), SourceArray.Num());

	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJson);
	FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);

	UE_LOG(LogMCPTool, Log, TEXT("list_live_link_sources: returned %d sources"), SourceArray.Num());
}

void HandleListLiveLinkSubjects(bool bIncludeDisabled, FString& OutJson, FString& OutError)
{
	ILiveLinkClient* Client = GetLiveLinkClient();
	if (!Client)
	{
		OutError = TEXT("LiveLink client not available.");
		return;
	}

	TArray<FLiveLinkSubjectKey> Subjects = Client->GetSubjects(bIncludeDisabled, false);
	TArray<TSharedPtr<FJsonValue>> SubjectArray;

	for (const FLiveLinkSubjectKey& Key : Subjects)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("name"), Key.SubjectName.Name.ToString());
		Obj->SetStringField(TEXT("source_guid"), Key.Source.ToString());

		TSubclassOf<ULiveLinkRole> Role = Client->GetSubjectRole_AnyThread(Key);
		if (Role)
		{
			Obj->SetStringField(TEXT("role"), Role->GetName());
		}
		else
		{
			Obj->SetStringField(TEXT("role"), TEXT("Unknown"));
		}

#if !UE_VERSION_OLDER_THAN(5, 5, 0)
		ELiveLinkSubjectState State = Client->GetSubjectState(Key.SubjectName);
		if      (State == ELiveLinkSubjectState::Connected)       Obj->SetStringField(TEXT("state"), TEXT("Connected"));
		else if (State == ELiveLinkSubjectState::Unresponsive)     Obj->SetStringField(TEXT("state"), TEXT("Unresponsive"));
		else if (State == ELiveLinkSubjectState::Disconnected)     Obj->SetStringField(TEXT("state"), TEXT("Disconnected"));
		else if (State == ELiveLinkSubjectState::InvalidOrDisabled)Obj->SetStringField(TEXT("state"), TEXT("InvalidOrDisabled"));
		else                                                        Obj->SetStringField(TEXT("state"), TEXT("Unknown"));
#else
		Obj->SetStringField(TEXT("state"), TEXT("Unknown"));
#endif

		SubjectArray.Add(MakeShared<FJsonValueObject>(Obj));
	}

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetArrayField(TEXT("subjects"), SubjectArray);
	Root->SetNumberField(TEXT("count"), SubjectArray.Num());

	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJson);
	FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);

	UE_LOG(LogMCPTool, Log, TEXT("list_live_link_subjects: returned %d subjects"), SubjectArray.Num());
}

void HandleAddMessageBusSourceFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	ILiveLinkClient* Client = GetLiveLinkClient();
	if (!Client)
	{
		OutError = TEXT("LiveLink client not available.");
		return;
	}

	if (!FModuleManager::Get().IsModuleLoaded(TEXT("LiveLink")))
	{
		OutError = TEXT("LiveLink module not loaded.");
		return;
	}

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("items"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }

			FString ProviderAddress = BatchToolHelper::GetItemString(Item, TEXT("provider_address"), TEXT("address"));
			if (ProviderAddress.IsEmpty()) { Batch.AddFailure(i, TEXT("provider_address required")); continue; }

			ULiveLinkMessageBusSourceFactory* Factory = GetMutableDefault<ULiveLinkMessageBusSourceFactory>();
			if (!Factory)
			{
				Batch.AddFailure(i, TEXT("Could not get ULiveLinkMessageBusSourceFactory CDO"));
				continue;
			}

			TSharedPtr<ILiveLinkSource> Source = Factory->CreateSource(ProviderAddress);
			if (!Source.IsValid())
			{
				Batch.AddFailure(i, FString::Printf(TEXT("Failed to create source for address: %s"), *ProviderAddress));
				continue;
			}

			FGuid NewGuid = Client->AddSource(Source);

			TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
			ResultObj->SetStringField(TEXT("guid"), NewGuid.ToString());
			ResultObj->SetStringField(TEXT("address"), ProviderAddress);
			Batch.AddSuccess(i, ResultObj);
		}
		Batch.Finalize(OutJson);
		return;
	}

	FString ProviderAddress;
	Args->TryGetStringField(TEXT("provider_address"), ProviderAddress);
	if (ProviderAddress.IsEmpty()) Args->TryGetStringField(TEXT("address"), ProviderAddress);

	if (ProviderAddress.IsEmpty())
	{
		OutError = TEXT("provider_address is required");
		return;
	}

	ULiveLinkMessageBusSourceFactory* Factory = GetMutableDefault<ULiveLinkMessageBusSourceFactory>();
	if (!Factory)
	{
		OutError = TEXT("Could not get ULiveLinkMessageBusSourceFactory CDO");
		return;
	}

	TSharedPtr<ILiveLinkSource> Source = Factory->CreateSource(ProviderAddress);
	if (!Source.IsValid())
	{
		OutError = FString::Printf(TEXT("Failed to create source for address: %s"), *ProviderAddress);
		return;
	}

	FGuid NewGuid = Client->AddSource(Source);

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("guid"), NewGuid.ToString());
	Root->SetStringField(TEXT("address"), ProviderAddress);
	Root->SetStringField(TEXT("message"), TEXT("Source added"));

	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJson);
	FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);

	UE_LOG(LogMCPTool, Log, TEXT("add_message_bus_source: added source at %s, GUID=%s"), *ProviderAddress, *NewGuid.ToString());
}

void HandleRemoveLiveLinkSourceFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	ILiveLinkClient* Client = GetLiveLinkClient();
	if (!Client)
	{
		OutError = TEXT("LiveLink client not available.");
		return;
	}

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("items"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			FString GuidStr;
			if ((*ItemsArray)[i]->Type == EJson::String)
			{
				GuidStr = (*ItemsArray)[i]->AsString();
			}
			else
			{
				TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
				if (Item.IsValid()) Item->TryGetStringField(TEXT("source_guid"), GuidStr);
			}

			FGuid Guid;
			if (!FGuid::Parse(GuidStr, Guid))
			{
				Batch.AddFailure(i, FString::Printf(TEXT("Invalid GUID: %s"), *GuidStr));
				continue;
			}

			Client->RemoveSource(Guid);

			TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
			ResultObj->SetStringField(TEXT("removed_guid"), GuidStr);
			Batch.AddSuccess(i, ResultObj);
		}
		Batch.Finalize(OutJson);
		return;
	}

	FString GuidStr;
	Args->TryGetStringField(TEXT("source_guid"), GuidStr);

	FGuid Guid;
	if (!FGuid::Parse(GuidStr, Guid))
	{
		OutError = FString::Printf(TEXT("Invalid GUID: %s"), *GuidStr);
		return;
	}

	Client->RemoveSource(Guid);

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("removed_guid"), GuidStr);
	Root->SetStringField(TEXT("message"), TEXT("Source removed"));

	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJson);
	FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);

	UE_LOG(LogMCPTool, Log, TEXT("remove_live_link_source: removed source GUID=%s"), *GuidStr);
}

void HandleGetSubjectFrameDataFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	ILiveLinkClient* Client = GetLiveLinkClient();
	if (!Client)
	{
		OutError = TEXT("LiveLink client not available.");
		return;
	}

	auto GetSubjectInfo = [&](const FString& SubjectName, TSharedPtr<FJsonObject>& ResultObj) -> bool
	{
		FLiveLinkSubjectName SubjectNameVal(*SubjectName);
		ResultObj = MakeShared<FJsonObject>();
		ResultObj->SetStringField(TEXT("name"), SubjectName);

#if !UE_VERSION_OLDER_THAN(5, 5, 0)
		ELiveLinkSubjectState State = Client->GetSubjectState(SubjectNameVal);
		if      (State == ELiveLinkSubjectState::Connected)        ResultObj->SetStringField(TEXT("state"), TEXT("Connected"));
		else if (State == ELiveLinkSubjectState::Unresponsive)      ResultObj->SetStringField(TEXT("state"), TEXT("Unresponsive"));
		else if (State == ELiveLinkSubjectState::Disconnected)      ResultObj->SetStringField(TEXT("state"), TEXT("Disconnected"));
		else if (State == ELiveLinkSubjectState::InvalidOrDisabled) ResultObj->SetStringField(TEXT("state"), TEXT("InvalidOrDisabled"));
		else                                                         ResultObj->SetStringField(TEXT("state"), TEXT("Unknown"));
#else
		ResultObj->SetStringField(TEXT("state"), TEXT("Unknown"));
#endif

		TArray<FLiveLinkSubjectKey> AllSubjects = Client->GetSubjects(true, false);
		FLiveLinkSubjectKey FoundKey;
		bool bFound = false;
		for (const FLiveLinkSubjectKey& Key : AllSubjects)
		{
			if (Key.SubjectName.Name == FName(*SubjectName))
			{
				FoundKey = Key;
				bFound = true;
				break;
			}
		}

		if (bFound)
		{
			ResultObj->SetStringField(TEXT("source_guid"), FoundKey.Source.ToString());
#if !UE_VERSION_OLDER_THAN(5, 5, 0)
			const FLiveLinkStaticDataStruct* StaticData = Client->GetSubjectStaticData_AnyThread(FoundKey);
			if (StaticData)
			{
				const FLiveLinkBaseStaticData* BaseData = StaticData->Cast<FLiveLinkBaseStaticData>();
				if (BaseData)
				{
					ResultObj->SetNumberField(TEXT("property_count"), BaseData->PropertyNames.Num());
					TArray<TSharedPtr<FJsonValue>> PropNames;
					for (const FName& PropName : BaseData->PropertyNames)
					{
						PropNames.Add(MakeShared<FJsonValueString>(PropName.ToString()));
					}
					ResultObj->SetArrayField(TEXT("property_names"), PropNames);
				}
			}
#endif
		}
		return true;
	};

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("items"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			FString SubjName;
			if ((*ItemsArray)[i]->Type == EJson::String)
			{
				SubjName = (*ItemsArray)[i]->AsString();
			}
			else
			{
				TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
				if (Item.IsValid()) Item->TryGetStringField(TEXT("subject_name"), SubjName);
			}

			if (SubjName.IsEmpty()) { Batch.AddFailure(i, TEXT("subject_name required")); continue; }

			TSharedPtr<FJsonObject> ResultObj;
			GetSubjectInfo(SubjName, ResultObj);
			Batch.AddSuccess(i, ResultObj);
		}
		Batch.Finalize(OutJson);
		return;
	}

	FString SubjectName;
	Args->TryGetStringField(TEXT("subject_name"), SubjectName);
	if (SubjectName.IsEmpty()) Args->TryGetStringField(TEXT("name"), SubjectName);

	if (SubjectName.IsEmpty())
	{
		OutError = TEXT("subject_name is required");
		return;
	}

	TSharedPtr<FJsonObject> ResultObj;
	GetSubjectInfo(SubjectName, ResultObj);

	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJson);
	FJsonSerializer::Serialize(ResultObj.ToSharedRef(), Writer);
}

void HandleCreateLiveLinkPreset(const FString& Name, const FString& SavePath, FString& OutJson, FString& OutError)
{
	ILiveLinkClient* Client = GetLiveLinkClient();
	if (!Client)
	{
		OutError = TEXT("LiveLink client not available.");
		return;
	}

	if (Name.IsEmpty() || SavePath.IsEmpty())
	{
		OutError = TEXT("name and save_path are required");
		return;
	}

	FString CleanPath = SavePath;
	if (!CleanPath.StartsWith(TEXT("/")))
	{
		OutError = TEXT("save_path must start with /Game/ or /Plugin/");
		return;
	}
	if (CleanPath.EndsWith(TEXT("/")))
	{
		CleanPath.RemoveFromEnd(TEXT("/"));
	}

	FString PackageName = CleanPath / Name;
	UPackage* Pkg = CreatePackage(*PackageName);
	if (!Pkg)
	{
		OutError = FString::Printf(TEXT("Failed to create package: %s"), *PackageName);
		return;
	}
	Pkg->FullyLoad();

	ULiveLinkPreset* Preset = NewObject<ULiveLinkPreset>(Pkg, FName(*Name), RF_Public | RF_Standalone | RF_Transactional);
	if (!Preset)
	{
		OutError = TEXT("Failed to create ULiveLinkPreset object");
		return;
	}

	Preset->BuildFromClient();

	FAssetRegistryModule::AssetCreated(Preset);
	Pkg->MarkPackageDirty();

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	FString PackageFilename = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
	UPackage::SavePackage(Pkg, Preset, *PackageFilename, SaveArgs);

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("asset_path"), PackageName);
	Root->SetNumberField(TEXT("source_count"), Preset->GetSourcePresets().Num());
	Root->SetNumberField(TEXT("subject_count"), Preset->GetSubjectPresets().Num());

	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJson);
	FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);

	UE_LOG(LogMCPTool, Log, TEXT("create_live_link_preset: created %s with %d sources, %d subjects"),
		*PackageName, Preset->GetSourcePresets().Num(), Preset->GetSubjectPresets().Num());
}

void HandleApplyLiveLinkPreset(const FString& PresetPath, FString& OutJson, FString& OutError)
{
	if (PresetPath.IsEmpty())
	{
		OutError = TEXT("preset_path is required");
		return;
	}

	ULiveLinkPreset* Preset = LoadObject<ULiveLinkPreset>(nullptr, *PresetPath);
	if (!Preset)
	{
		OutError = FString::Printf(TEXT("Failed to load ULiveLinkPreset at: %s"), *PresetPath);
		return;
	}

	bool bOK = Preset->AddToClient(true);

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("success"), bOK);
	Root->SetStringField(TEXT("preset_path"), PresetPath);
	Root->SetNumberField(TEXT("source_count"), Preset->GetSourcePresets().Num());
	Root->SetNumberField(TEXT("subject_count"), Preset->GetSubjectPresets().Num());
	if (!bOK)
	{
		Root->SetStringField(TEXT("warning"), TEXT("AddToClient returned false — some sources may not have been recreated."));
	}

	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJson);
	FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);

	UE_LOG(LogMCPTool, Log, TEXT("apply_live_link_preset: applied %s, success=%d"), *PresetPath, bOK ? 1 : 0);
}

void HandleGetLiveLinkSummary(FString& OutJson, FString& OutError)
{
	ILiveLinkClient* Client = GetLiveLinkClient();

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();

	if (!Client)
	{
		Root->SetBoolField(TEXT("live_link_available"), false);
		Root->SetStringField(TEXT("message"), TEXT("LiveLink client not available. Enable the LiveLink plugin."));
		TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJson);
		FJsonSerializer::Serialize(Root.ToSharedRef(), W);
		return;
	}

	TArray<FGuid> Sources = Client->GetSources();
	TArray<FLiveLinkSubjectKey> Subjects = Client->GetSubjects(false, false);

	Root->SetBoolField(TEXT("live_link_available"), true);
	Root->SetNumberField(TEXT("source_count"), Sources.Num());
	Root->SetNumberField(TEXT("subject_count"), Subjects.Num());

	TArray<TSharedPtr<FJsonValue>> SubjectNames;
	for (const FLiveLinkSubjectKey& Key : Subjects)
	{
		SubjectNames.Add(MakeShared<FJsonValueString>(Key.SubjectName.Name.ToString()));
	}
	Root->SetArrayField(TEXT("subject_names"), SubjectNames);

	TArray<TSharedPtr<FJsonValue>> SourceInfos;
	for (const FGuid& Guid : Sources)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("guid"), Guid.ToString());
		Obj->SetStringField(TEXT("type"), Client->GetSourceType(Guid).ToString());
		Obj->SetStringField(TEXT("status"), Client->GetSourceStatus(Guid).ToString());
		Obj->SetBoolField(TEXT("is_valid"), Client->IsSourceStillValid(Guid));
		SourceInfos.Add(MakeShared<FJsonValueObject>(Obj));
	}
	Root->SetArrayField(TEXT("sources"), SourceInfos);

	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJson);
	FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);

	UE_LOG(LogMCPTool, Log, TEXT("get_live_link_summary: %d sources, %d subjects"), Sources.Num(), Subjects.Num());
}

void HandleListLiveLinkSourcesFromArgs(const TSharedPtr<FJsonObject>& ,
	FString& OutJson, FString& OutError)
{
	HandleListLiveLinkSources(OutJson, OutError);
}

void HandleListLiveLinkSubjectsFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJson, FString& OutError)
{
	bool bIncludeDisabled = false;
	if (Args.IsValid()) Args->TryGetBoolField(TEXT("include_disabled"), bIncludeDisabled);
	HandleListLiveLinkSubjects(bIncludeDisabled, OutJson, OutError);
}

void HandleCreateLiveLinkPresetFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString Name, SavePath;
	Args->TryGetStringField(TEXT("name"), Name);
	Args->TryGetStringField(TEXT("save_path"), SavePath);
	HandleCreateLiveLinkPreset(Name, SavePath, OutJson, OutError);
}

void HandleApplyLiveLinkPresetFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString PresetPath;
	Args->TryGetStringField(TEXT("preset_path"), PresetPath);
	if (PresetPath.IsEmpty()) Args->TryGetStringField(TEXT("asset_path"), PresetPath);
	HandleApplyLiveLinkPreset(PresetPath, OutJson, OutError);
}

void HandleGetLiveLinkSummaryFromArgs(const TSharedPtr<FJsonObject>& ,
	FString& OutJson, FString& OutError)
{
	HandleGetLiveLinkSummary(OutJson, OutError);
}

}
