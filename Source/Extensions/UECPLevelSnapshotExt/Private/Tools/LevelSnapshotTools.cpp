// Copyright 2026, BlueprintsLab, All rights reserved.

#include "Tools/LevelSnapshotTools.h"
#include "Tools/BatchToolHelper.h"
#include "Utils/MountResolver.h"
#include "MCPToolsLog.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Engine/World.h"
#include "Editor.h"
#include "Modules/ModuleManager.h"
#include "ObjectTools.h"

#if defined(LEVELSNAPSHOTS_API)
#include "Data/LevelSnapshot.h"
#include "LevelSnapshotsFunctionLibrary.h"
#include "LevelSnapshotsFilteringLibrary.h"
#include "Filtering/PropertySelectionMap.h"
#endif

namespace LevelSnapshotTools
{

static bool IsModuleAvailable()
{
	return FModuleManager::Get().IsModuleLoaded(TEXT("LevelSnapshots"));
}

static UWorld* GetEditorWorld(FString& OutError)
{
	if (!GEditor)
	{
		OutError = TEXT("GEditor is null");
		return nullptr;
	}
	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		OutError = TEXT("No editor world available");
		return nullptr;
	}
	return World;
}

static FString SerializeSimpleSuccess(const FString& ExtraKey = TEXT(""), const FString& ExtraVal = TEXT(""))
{
	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("success"), true);
	if (!ExtraKey.IsEmpty())
		Root->SetStringField(ExtraKey, ExtraVal);
	FString Out;
	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Root.ToSharedRef(), W);
	return Out;
}

void HandleTakeLevelSnapshot(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!IsModuleAvailable())
	{
		OutError = TEXT("LevelSnapshots plugin module is not loaded. Enable the LevelSnapshots plugin in your .uproject.");
		return;
	}

	FString SnapshotName, Description, SavePath;
	Args->TryGetStringField(TEXT("name"), SnapshotName);
	Args->TryGetStringField(TEXT("description"), Description);
	Args->TryGetStringField(TEXT("save_path"), SavePath);

	if (SnapshotName.IsEmpty())
	{
		OutError = TEXT("'name' is required for take_level_snapshot");
		return;
	}

	if (SavePath.IsEmpty())
		SavePath = TEXT("/Game/LevelSnapshots");

	UWorld* World = GetEditorWorld(OutError);
	if (!World) return;

	UE_LOG(LogMCPTool, Log, TEXT("LevelSnapshotTools: taking snapshot '%s' in '%s'"), *SnapshotName, *SavePath);

#if defined(LEVELSNAPSHOTS_API)
	FString PackagePath = FString::Printf(TEXT("%s/%s"), *SavePath, *SnapshotName);

	FString PackageName = PackagePath;
	UPackage* Package = CreatePackage(*PackageName);
	if (!Package)
	{
		OutError = FString::Printf(TEXT("Failed to create package at: %s"), *PackageName);
		return;
	}
	Package->FullyLoad();

	ULevelSnapshot* Snapshot = NewObject<ULevelSnapshot>(Package, *SnapshotName, RF_Public | RF_Standalone);
	if (!Snapshot)
	{
		OutError = TEXT("Failed to create ULevelSnapshot object");
		return;
	}

	Snapshot->SetSnapshotName(FName(*SnapshotName));
	if (!Description.IsEmpty())
		Snapshot->SetSnapshotDescription(Description);

	bool bOk = Snapshot->SnapshotWorld(World);
	if (!bOk)
	{
		OutError = TEXT("SnapshotWorld() returned false — check that the world is valid and not in PIE");
		return;
	}

	Package->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(Snapshot);

	FString PackageFileName = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	UPackage::Save(Package, Snapshot, *PackageFileName, SaveArgs);

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("success"), true);
	Root->SetStringField(TEXT("snapshot_path"), PackagePath);
	Root->SetStringField(TEXT("name"), SnapshotName);
	Root->SetNumberField(TEXT("actor_count"), Snapshot->GetNumSavedActors());
	FString Out;
	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Root.ToSharedRef(), W);
	OutJsonString = Out;
#else
	OutError = TEXT("LevelSnapshots plugin API not available at compile time");
#endif
}

void HandleRestoreLevelSnapshot(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!IsModuleAvailable())
	{
		OutError = TEXT("LevelSnapshots plugin module is not loaded");
		return;
	}

	FString SnapshotPath;
	Args->TryGetStringField(TEXT("snapshot_path"), SnapshotPath);
	if (SnapshotPath.IsEmpty())
	{
		OutError = TEXT("'snapshot_path' is required for restore_level_snapshot");
		return;
	}

	UWorld* World = GetEditorWorld(OutError);
	if (!World) return;

#if defined(LEVELSNAPSHOTS_API)
	ULevelSnapshot* Snapshot = Cast<ULevelSnapshot>(
		StaticLoadObject(ULevelSnapshot::StaticClass(), nullptr, *SnapshotPath));
	if (!Snapshot)
	{
		OutError = FString::Printf(TEXT("Could not load snapshot at: %s"), *SnapshotPath);
		return;
	}

	UE_LOG(LogMCPTool, Log, TEXT("LevelSnapshotTools: restoring snapshot '%s' to world"), *SnapshotPath);

	FPropertySelectionMap SelectionMap;
	Snapshot->ApplySnapshotToWorld(World, SelectionMap);

	OutJsonString = SerializeSimpleSuccess(TEXT("snapshot_path"), SnapshotPath);
#else
	OutError = TEXT("LevelSnapshots plugin API not available at compile time");
#endif
}

void HandleListLevelSnapshotsFromArgs(const TSharedPtr<FJsonObject>& , FString& OutJsonString, FString& OutError)
{
	HandleListLevelSnapshots(OutJsonString, OutError);
}

void HandleListLevelSnapshots(FString& OutJsonString, FString& OutError)
{
	if (!IsModuleAvailable())
	{
		OutError = TEXT("LevelSnapshots plugin module is not loaded");
		return;
	}

	IAssetRegistry& AR = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

#if defined(LEVELSNAPSHOTS_API)
	TArray<FAssetData> Assets;
	FARFilter Filter;
	Filter.ClassPaths.Add(ULevelSnapshot::StaticClass()->GetClassPathName());
	Filter.bRecursivePaths = true;
	for (const FString& Mount : UECPMountResolver::GetUserContentMounts())
	{
		Filter.PackagePaths.Add(FName(*Mount));
	}
	AR.GetAssets(Filter, Assets);

	TArray<TSharedPtr<FJsonValue>> Arr;
	for (const FAssetData& AD : Assets)
	{
		TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
		Item->SetStringField(TEXT("path"), AD.GetObjectPathString());
		Item->SetStringField(TEXT("name"), AD.AssetName.ToString());
		Item->SetStringField(TEXT("package_path"), AD.PackagePath.ToString());
		Arr.Add(MakeShared<FJsonValueObject>(Item));
	}

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("success"), true);
	Root->SetNumberField(TEXT("count"), Arr.Num());
	Root->SetArrayField(TEXT("snapshots"), Arr);
	FString Out;
	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Root.ToSharedRef(), W);
	OutJsonString = Out;
#else
	OutError = TEXT("LevelSnapshots plugin API not available at compile time");
#endif
}

void HandleDeleteLevelSnapshotFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!IsModuleAvailable())
	{
		OutError = TEXT("LevelSnapshots plugin module is not loaded");
		return;
	}

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("snapshot_paths"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			FString Path;
			if ((*ItemsArray)[i]->Type == EJson::String)
			{
				Path = (*ItemsArray)[i]->AsString();
			}
			else
			{
				TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
				if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
				Path = BatchToolHelper::GetItemString(Item, TEXT("snapshot_path"), TEXT("path"));
			}

			if (Path.IsEmpty()) { Batch.AddFailure(i, TEXT("snapshot_path is empty")); continue; }

			UObject* Obj = StaticLoadObject(UObject::StaticClass(), nullptr, *Path);
			if (!Obj) { Batch.AddFailure(i, FString::Printf(TEXT("Asset not found: %s"), *Path)); continue; }

			TArray<UObject*> ToDelete = { Obj };
			int32 Deleted = ObjectTools::DeleteObjects(ToDelete, false);
			if (Deleted > 0) { auto E = MakeShared<FJsonObject>(); E->SetStringField(TEXT("path"), Path); Batch.AddSuccess(i, E); }
			else { Batch.AddFailure(i, FString::Printf(TEXT("Failed to delete: %s"), *Path)); }
		}
		Batch.Finalize(OutJsonString);
		return;
	}

	FString SnapshotPath;
	Args->TryGetStringField(TEXT("snapshot_path"), SnapshotPath);
	if (SnapshotPath.IsEmpty())
	{
		OutError = TEXT("'snapshot_path' is required for delete_level_snapshot");
		return;
	}

	UObject* Obj = StaticLoadObject(UObject::StaticClass(), nullptr, *SnapshotPath);
	if (!Obj)
	{
		OutError = FString::Printf(TEXT("Asset not found: %s"), *SnapshotPath);
		return;
	}

	TArray<UObject*> ToDelete = { Obj };
	int32 Deleted = ObjectTools::DeleteObjects(ToDelete, false);
	if (Deleted > 0)
		OutJsonString = SerializeSimpleSuccess(TEXT("snapshot_path"), SnapshotPath);
	else
		OutError = FString::Printf(TEXT("Failed to delete snapshot: %s"), *SnapshotPath);
}

void HandleCompareSnapshotToWorld(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!IsModuleAvailable())
	{
		OutError = TEXT("LevelSnapshots plugin module is not loaded");
		return;
	}

	FString SnapshotPath;
	Args->TryGetStringField(TEXT("snapshot_path"), SnapshotPath);
	if (SnapshotPath.IsEmpty())
	{
		OutError = TEXT("'snapshot_path' is required for compare_snapshot_to_world");
		return;
	}

	UWorld* World = GetEditorWorld(OutError);
	if (!World) return;

#if defined(LEVELSNAPSHOTS_API)
	ULevelSnapshot* Snapshot = Cast<ULevelSnapshot>(
		StaticLoadObject(ULevelSnapshot::StaticClass(), nullptr, *SnapshotPath));
	if (!Snapshot)
	{
		OutError = FString::Printf(TEXT("Could not load snapshot at: %s"), *SnapshotPath);
		return;
	}

	int32 AddedCount = 0, RemovedCount = 0, ModifiedCount = 0;
	TArray<FString> AddedActors, RemovedActors, ModifiedActors;

	Snapshot->DiffWorld(
		World,
		ULevelSnapshot::FActorConsumer::CreateLambda([&](AActor* WorldActor)
		{
			if (WorldActor && Snapshot->HasChangedSinceSnapshotWasTaken(WorldActor))
			{
				ModifiedCount++;
				ModifiedActors.Add(WorldActor->GetActorLabel());
			}
		}),
		ULevelSnapshot::FActorPathConsumer::CreateLambda([&](const FSoftObjectPath& Path)
		{
			RemovedCount++;
			RemovedActors.Add(Snapshot->GetActorLabel(Path));
		}),
		ULevelSnapshot::FActorConsumer::CreateLambda([&](AActor* WorldActor)
		{
			AddedCount++;
			if (WorldActor) AddedActors.Add(WorldActor->GetActorLabel());
		})
	);

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("success"), true);
	Root->SetStringField(TEXT("snapshot_path"), SnapshotPath);
	Root->SetNumberField(TEXT("added_count"), AddedCount);
	Root->SetNumberField(TEXT("removed_count"), RemovedCount);
	Root->SetNumberField(TEXT("modified_count"), ModifiedCount);

	auto ToJsonArr = [](const TArray<FString>& In, int32 Limit) {
		TArray<TSharedPtr<FJsonValue>> Out;
		for (int32 i = 0; i < In.Num() && i < Limit; i++)
			Out.Add(MakeShared<FJsonValueString>(In[i]));
		return Out;
	};
	Root->SetArrayField(TEXT("added_actors"), ToJsonArr(AddedActors, 50));
	Root->SetArrayField(TEXT("removed_actors"), ToJsonArr(RemovedActors, 50));
	Root->SetArrayField(TEXT("modified_actors"), ToJsonArr(ModifiedActors, 50));

	FString Out;
	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Root.ToSharedRef(), W);
	OutJsonString = Out;
#else
	OutError = TEXT("LevelSnapshots plugin API not available at compile time");
#endif
}

void HandleRestoreSpecificActors(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!IsModuleAvailable())
	{
		OutError = TEXT("LevelSnapshots plugin module is not loaded");
		return;
	}

	FString SnapshotPath;
	Args->TryGetStringField(TEXT("snapshot_path"), SnapshotPath);
	if (SnapshotPath.IsEmpty())
	{
		OutError = TEXT("'snapshot_path' is required for restore_specific_actors");
		return;
	}

	const TArray<TSharedPtr<FJsonValue>>* LabelsArray = nullptr;
	TArray<FString> ActorLabels;
	if (Args->TryGetArrayField(TEXT("actor_labels"), LabelsArray) && LabelsArray)
	{
		for (const auto& V : *LabelsArray)
			ActorLabels.Add(V->AsString());
	}
	if (ActorLabels.IsEmpty())
	{
		OutError = TEXT("'actor_labels' array is required for restore_specific_actors (e.g. [\"Cube\",\"Sphere\"])");
		return;
	}

	UWorld* World = GetEditorWorld(OutError);
	if (!World) return;

#if defined(LEVELSNAPSHOTS_API)
	ULevelSnapshot* Snapshot = Cast<ULevelSnapshot>(
		StaticLoadObject(ULevelSnapshot::StaticClass(), nullptr, *SnapshotPath));
	if (!Snapshot)
	{
		OutError = FString::Printf(TEXT("Could not load snapshot at: %s"), *SnapshotPath);
		return;
	}

	TSet<FString> TargetLabels;
	for (const FString& L : ActorLabels)
		TargetLabels.Add(L.ToLower());

	FPropertySelectionMap Map = ULevelSnapshotsFilteringLibrary::DiffAndFilterSnapshot(World, Snapshot, nullptr);

	TSet<FString> MatchedLabels;

	auto ActorLabelForModifiedPath = [](const FSoftObjectPath& Path) -> FString
	{
		UObject* Resolved = Path.ResolveObject();
		if (!Resolved) return FString();
		AActor* OwningActor = Cast<AActor>(Resolved);
		if (!OwningActor) OwningActor = Resolved->GetTypedOuter<AActor>();
		return OwningActor ? OwningActor->GetActorLabel() : FString();
	};

	{
		TArray<UObject*> ObjectsToDrop;
		TSet<AActor*> ActorsToDropComponents;
		Map.ForEachModifiedObject([&](const FSoftObjectPath& Path)
		{
			UObject* Resolved = Path.ResolveObject();
			const FString Label = ActorLabelForModifiedPath(Path);
			if (!Label.IsEmpty() && TargetLabels.Contains(Label.ToLower()))
			{
				MatchedLabels.Add(Label.ToLower());
				return;
			}
			if (Resolved)
			{
				ObjectsToDrop.Add(Resolved);
				AActor* OwningActor = Cast<AActor>(Resolved);
				if (!OwningActor) OwningActor = Resolved->GetTypedOuter<AActor>();
				if (OwningActor) ActorsToDropComponents.Add(OwningActor);
			}
		});
		for (UObject* Obj : ObjectsToDrop)
			Map.RemoveObjectPropertiesFromMap(Obj);
		for (AActor* Act : ActorsToDropComponents)
			Map.RemoveComponentSelection(Act);
	}

	{
		TArray<FSoftObjectPath> RespawnsToDrop;
		for (const FSoftObjectPath& Path : Map.GetDeletedActorsToRespawn())
		{
			const FString Label = Snapshot->GetActorLabel(Path);
			if (!Label.IsEmpty() && TargetLabels.Contains(Label.ToLower()))
				MatchedLabels.Add(Label.ToLower());
			else
				RespawnsToDrop.Add(Path);
		}
		for (const FSoftObjectPath& Path : RespawnsToDrop)
			Map.RemoveDeletedActorToRespawn(Path);
	}

	{
		TArray<AActor*> DespawnsToDrop;
		for (const TWeakObjectPtr<AActor>& WeakActor : Map.GetNewActorsToDespawn())
		{
			AActor* Act = WeakActor.Get();
			if (!Act) continue;
			const FString Label = Act->GetActorLabel();
			if (!Label.IsEmpty() && TargetLabels.Contains(Label.ToLower()))
				MatchedLabels.Add(Label.ToLower());
			else
				DespawnsToDrop.Add(Act);
		}
		for (AActor* Act : DespawnsToDrop)
			Map.RemoveNewActorToDespawn(Act);
	}

	TArray<FString> UnmatchedLabels;
	for (const FString& Requested : ActorLabels)
	{
		if (!MatchedLabels.Contains(Requested.ToLower()))
			UnmatchedLabels.Add(Requested);
	}

	if (MatchedLabels.Num() == 0)
	{
		OutError = FString::Printf(
			TEXT("None of the requested actors (%s) have changes versus the snapshot, so nothing was restored. ")
			TEXT("Verify the actor labels, or use restore_level_snapshot for a full restore."),
			*FString::Join(ActorLabels, TEXT(", ")));
		return;
	}

	Snapshot->ApplySnapshotToWorld(World, Map);

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("success"), true);
	Root->SetStringField(TEXT("snapshot_path"), SnapshotPath);
	Root->SetNumberField(TEXT("requested_actor_count"), ActorLabels.Num());
	Root->SetNumberField(TEXT("matched_count"), MatchedLabels.Num());

	TArray<TSharedPtr<FJsonValue>> ReqArr;
	for (const FString& L : ActorLabels) ReqArr.Add(MakeShared<FJsonValueString>(L));
	Root->SetArrayField(TEXT("requested_labels"), ReqArr);

	TArray<TSharedPtr<FJsonValue>> UnmatchedArr;
	for (const FString& L : UnmatchedLabels) UnmatchedArr.Add(MakeShared<FJsonValueString>(L));
	Root->SetArrayField(TEXT("unmatched_labels"), UnmatchedArr);

	FString Out;
	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Root.ToSharedRef(), W);
	OutJsonString = Out;
#else
	OutError = TEXT("LevelSnapshots plugin API not available at compile time");
#endif
}

}
