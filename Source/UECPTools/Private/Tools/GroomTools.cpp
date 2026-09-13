// Copyright 2026, BlueprintsLab, All rights reserved.

#include "Tools/GroomTools.h"
#include "Tools/BatchToolHelper.h"

#include "EditorAssetLibrary.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/Package.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonWriter.h"
#include "MCPToolsLog.h"

#include "Modules/ModuleManager.h"

#include "GroomAsset.h"
#include "GroomBindingAsset.h"
#include "GroomComponent.h"
#include "GroomActor.h"
#include "GroomAssetPhysics.h"
#include "GroomAssetRendering.h"
#include "GroomAssetInterpolation.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "LevelEditorSubsystem.h"
#include "Subsystems/UnrealEditorSubsystem.h"

namespace GroomTools
{

static bool IsHairLoaded()
{
	return FModuleManager::Get().IsModuleLoaded(TEXT("HairStrandsCore"));
}

void HandleGetGroomInfoFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	if (!IsHairLoaded()) { OutError = TEXT("HairStrandsCore module not loaded. Enable HairStrands plugin."); return; }

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("paths"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			FString Path;
			if ((*ItemsArray)[i]->Type == EJson::String)
				Path = (*ItemsArray)[i]->AsString();
			else if (TSharedPtr<FJsonObject> O = (*ItemsArray)[i]->AsObject())
				O->TryGetStringField(TEXT("groom_path"), Path);

			if (Path.IsEmpty()) { Batch.AddFailure(i, TEXT("groom_path required")); continue; }
			UGroomAsset* Groom = Cast<UGroomAsset>(UEditorAssetLibrary::LoadAsset(Path));
			if (!Groom) { Batch.AddFailure(i, FString::Printf(TEXT("GroomAsset not found: %s"), *Path)); continue; }
			TSharedPtr<FJsonObject> E = MakeShared<FJsonObject>();
			E->SetStringField(TEXT("groom_path"), Path);
			E->SetNumberField(TEXT("num_hair_groups"), Groom->GetNumHairGroups());
			E->SetNumberField(TEXT("num_lod_groups"), Groom->GetHairGroupsLOD().Num());
			E->SetNumberField(TEXT("num_physics_groups"), Groom->GetHairGroupsPhysics().Num());
			E->SetNumberField(TEXT("num_rendering_groups"), Groom->GetHairGroupsRendering().Num());
			Batch.AddSuccess(i, E);
		}
		Batch.Finalize(OutJson);
		return;
	}
	FString Path; Args->TryGetStringField(TEXT("groom_path"), Path);
	if (Path.IsEmpty()) Args->TryGetStringField(TEXT("asset_path"), Path);
	if (Path.IsEmpty()) { OutError = TEXT("groom_path required"); return; }
	UGroomAsset* Groom = Cast<UGroomAsset>(UEditorAssetLibrary::LoadAsset(Path));
	if (!Groom) { OutError = FString::Printf(TEXT("GroomAsset not found: %s"), *Path); return; }
	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("success"), true);
	Root->SetStringField(TEXT("groom_path"), Path);
	Root->SetNumberField(TEXT("num_hair_groups"), Groom->GetNumHairGroups());
	Root->SetNumberField(TEXT("num_lod_groups"), Groom->GetHairGroupsLOD().Num());
	Root->SetNumberField(TEXT("num_physics_groups"), Groom->GetHairGroupsPhysics().Num());
	Root->SetNumberField(TEXT("num_rendering_groups"), Groom->GetHairGroupsRendering().Num());
	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJson);
	FJsonSerializer::Serialize(Root.ToSharedRef(), W);
}

void HandleSetGroomLODSettingsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	if (!IsHairLoaded()) { OutError = TEXT("HairStrandsCore module not loaded"); return; }

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("settings"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString GPath = BatchToolHelper::GetItemString(Item, TEXT("groom_path"), TEXT("asset_path"));
			double GI = 0, LI = 0, SS = -1, CD = -1;
			Item->TryGetNumberField(TEXT("group_index"), GI);
			Item->TryGetNumberField(TEXT("lod_index"), LI);
			Item->TryGetNumberField(TEXT("screen_size"), SS);
			Item->TryGetNumberField(TEXT("curve_decimation"), CD);
			UGroomAsset* Groom = Cast<UGroomAsset>(UEditorAssetLibrary::LoadAsset(GPath));
			if (!Groom) { Batch.AddFailure(i, FString::Printf(TEXT("GroomAsset not found: %s"), *GPath)); continue; }
			int32 GroupIdx = (int32)GI, LodIdx = (int32)LI;
			if (GroupIdx >= Groom->GetHairGroupsLOD().Num()) { Batch.AddFailure(i, TEXT("group_index out of range")); continue; }
			if (LodIdx >= Groom->GetHairGroupsLOD()[GroupIdx].LODs.Num()) { Batch.AddFailure(i, TEXT("lod_index out of range")); continue; }
			if (SS >= 0) Groom->GetHairGroupsLOD()[GroupIdx].LODs[LodIdx].ScreenSize = (float)SS;
			if (CD >= 0) Groom->GetHairGroupsLOD()[GroupIdx].LODs[LodIdx].CurveDecimation = (float)CD;
			Groom->MarkPackageDirty();
			TSharedPtr<FJsonObject> E = MakeShared<FJsonObject>(); E->SetStringField(TEXT("groom_path"), GPath); E->SetNumberField(TEXT("group_index"), GroupIdx); E->SetNumberField(TEXT("lod_index"), LodIdx);
			Batch.AddSuccess(i, E);
		}
		Batch.Finalize(OutJson); return;
	}
	FString GPath; double GI = 0, LI = 0, SS = -1, CD = -1;
	Args->TryGetStringField(TEXT("groom_path"), GPath);
	Args->TryGetNumberField(TEXT("group_index"), GI); Args->TryGetNumberField(TEXT("lod_index"), LI);
	Args->TryGetNumberField(TEXT("screen_size"), SS); Args->TryGetNumberField(TEXT("curve_decimation"), CD);
	if (GPath.IsEmpty()) { OutError = TEXT("groom_path required"); return; }
	UGroomAsset* Groom = Cast<UGroomAsset>(UEditorAssetLibrary::LoadAsset(GPath));
	if (!Groom) { OutError = FString::Printf(TEXT("GroomAsset not found: %s"), *GPath); return; }
	int32 GroupIdx = (int32)GI, LodIdx = (int32)LI;
	if (GroupIdx >= Groom->GetHairGroupsLOD().Num()) { OutError = TEXT("group_index out of range"); return; }
	if (LodIdx >= Groom->GetHairGroupsLOD()[GroupIdx].LODs.Num()) { OutError = TEXT("lod_index out of range"); return; }
	if (SS >= 0) Groom->GetHairGroupsLOD()[GroupIdx].LODs[LodIdx].ScreenSize = (float)SS;
	if (CD >= 0) Groom->GetHairGroupsLOD()[GroupIdx].LODs[LodIdx].CurveDecimation = (float)CD;
	Groom->MarkPackageDirty();
	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("success"), true); Root->SetStringField(TEXT("groom_path"), GPath);
	Root->SetNumberField(TEXT("group_index"), GroupIdx); Root->SetNumberField(TEXT("lod_index"), LodIdx);
	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJson);
	FJsonSerializer::Serialize(Root.ToSharedRef(), W);
}

void HandleCreateGroomBindingFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	if (!IsHairLoaded()) { OutError = TEXT("HairStrandsCore module not loaded"); return; }

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("bindings"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString Name = BatchToolHelper::GetItemString(Item, TEXT("name"));
			FString SP = BatchToolHelper::GetItemString(Item, TEXT("save_path"));
			FString GroomPath = BatchToolHelper::GetItemString(Item, TEXT("groom_path"));
			FString MeshPath = BatchToolHelper::GetItemString(Item, TEXT("target_mesh_path"), TEXT("skeletal_mesh_path"));
			if (Name.IsEmpty() || SP.IsEmpty() || GroomPath.IsEmpty()) { Batch.AddFailure(i, TEXT("name, save_path, groom_path required")); continue; }
			FString ItemOut, ItemErr;
			FString PackagePath = SP / Name;
			UPackage* Pkg = CreatePackage(*PackagePath);
			if (!Pkg) { Batch.AddFailure(i, TEXT("Failed to create package")); continue; }
			UGroomBindingAsset* Binding = NewObject<UGroomBindingAsset>(Pkg, FName(*Name), RF_Public | RF_Standalone | RF_Transactional);
			if (!Binding) { Batch.AddFailure(i, TEXT("Failed to create GroomBindingAsset")); continue; }
			if (!GroomPath.IsEmpty()) if (UGroomAsset* G = Cast<UGroomAsset>(UEditorAssetLibrary::LoadAsset(GroomPath))) Binding->SetGroom(G);
			if (!MeshPath.IsEmpty()) if (USkeletalMesh* SM = Cast<USkeletalMesh>(UEditorAssetLibrary::LoadAsset(MeshPath))) Binding->SetTargetSkeletalMesh(SM);
			FAssetRegistryModule::AssetCreated(Binding);
			Binding->MarkPackageDirty();
			TSharedPtr<FJsonObject> E = MakeShared<FJsonObject>(); E->SetStringField(TEXT("asset_path"), PackagePath); E->SetStringField(TEXT("name"), Name);
			Batch.AddSuccess(i, E);
		}
		Batch.Finalize(OutJson); return;
	}
	FString Name, SP, GroomPath, MeshPath;
	Args->TryGetStringField(TEXT("name"), Name); Args->TryGetStringField(TEXT("save_path"), SP);
	Args->TryGetStringField(TEXT("groom_path"), GroomPath); Args->TryGetStringField(TEXT("target_mesh_path"), MeshPath);
	if (MeshPath.IsEmpty()) Args->TryGetStringField(TEXT("skeletal_mesh_path"), MeshPath);
	if (Name.IsEmpty() || SP.IsEmpty() || GroomPath.IsEmpty()) { OutError = TEXT("name, save_path, groom_path required"); return; }
	FString PackagePath = SP / Name;
	UPackage* Pkg = CreatePackage(*PackagePath);
	if (!Pkg) { OutError = TEXT("Failed to create package"); return; }
	UGroomBindingAsset* Binding = NewObject<UGroomBindingAsset>(Pkg, FName(*Name), RF_Public | RF_Standalone | RF_Transactional);
	if (!Binding) { OutError = TEXT("Failed to create GroomBindingAsset"); return; }
	if (UGroomAsset* G = Cast<UGroomAsset>(UEditorAssetLibrary::LoadAsset(GroomPath))) Binding->SetGroom(G);
	if (!MeshPath.IsEmpty()) if (USkeletalMesh* SM = Cast<USkeletalMesh>(UEditorAssetLibrary::LoadAsset(MeshPath))) Binding->SetTargetSkeletalMesh(SM);
	FAssetRegistryModule::AssetCreated(Binding);
	Binding->MarkPackageDirty();
	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("success"), true); Root->SetStringField(TEXT("asset_path"), PackagePath); Root->SetStringField(TEXT("name"), Name);
	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJson);
	FJsonSerializer::Serialize(Root.ToSharedRef(), W);
}

void HandleSetGroomPhysicsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	if (!IsHairLoaded()) { OutError = TEXT("HairStrandsCore module not loaded"); return; }

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("settings"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString GPath = BatchToolHelper::GetItemString(Item, TEXT("groom_path"), TEXT("asset_path"));
			double GI = 0, SubSteps = -1, IterCount = -1; bool bEnableSim = false;
			Item->TryGetNumberField(TEXT("group_index"), GI);
			Item->TryGetBoolField(TEXT("enable_simulation"), bEnableSim);
			Item->TryGetNumberField(TEXT("sub_steps"), SubSteps);
			Item->TryGetNumberField(TEXT("iteration_count"), IterCount);
			UGroomAsset* Groom = Cast<UGroomAsset>(UEditorAssetLibrary::LoadAsset(GPath));
			if (!Groom) { Batch.AddFailure(i, FString::Printf(TEXT("GroomAsset not found: %s"), *GPath)); continue; }
			int32 GroupIdx = (int32)GI;
			if (GroupIdx >= Groom->GetHairGroupsPhysics().Num()) { Batch.AddFailure(i, TEXT("group_index out of range")); continue; }
			Groom->GetHairGroupsPhysics()[GroupIdx].SolverSettings.EnableSimulation = bEnableSim;
			if (SubSteps >= 0) Groom->GetHairGroupsPhysics()[GroupIdx].SolverSettings.SubSteps = (int32)SubSteps;
			if (IterCount >= 0) Groom->GetHairGroupsPhysics()[GroupIdx].SolverSettings.IterationCount = (int32)IterCount;
			Groom->MarkPackageDirty();
			TSharedPtr<FJsonObject> E = MakeShared<FJsonObject>(); E->SetStringField(TEXT("groom_path"), GPath); E->SetNumberField(TEXT("group_index"), GroupIdx); E->SetBoolField(TEXT("enable_simulation"), bEnableSim);
			Batch.AddSuccess(i, E);
		}
		Batch.Finalize(OutJson); return;
	}
	FString GPath; double GI = 0, SubSteps = -1, IterCount = -1; bool bEnableSim = false;
	Args->TryGetStringField(TEXT("groom_path"), GPath);
	Args->TryGetNumberField(TEXT("group_index"), GI);
	Args->TryGetBoolField(TEXT("enable_simulation"), bEnableSim);
	Args->TryGetNumberField(TEXT("sub_steps"), SubSteps);
	Args->TryGetNumberField(TEXT("iteration_count"), IterCount);
	if (GPath.IsEmpty()) { OutError = TEXT("groom_path required"); return; }
	UGroomAsset* Groom = Cast<UGroomAsset>(UEditorAssetLibrary::LoadAsset(GPath));
	if (!Groom) { OutError = FString::Printf(TEXT("GroomAsset not found: %s"), *GPath); return; }
	int32 GroupIdx = (int32)GI;
	if (GroupIdx >= Groom->GetHairGroupsPhysics().Num()) { OutError = TEXT("group_index out of range"); return; }
	Groom->GetHairGroupsPhysics()[GroupIdx].SolverSettings.EnableSimulation = bEnableSim;
	if (SubSteps >= 0) Groom->GetHairGroupsPhysics()[GroupIdx].SolverSettings.SubSteps = (int32)SubSteps;
	if (IterCount >= 0) Groom->GetHairGroupsPhysics()[GroupIdx].SolverSettings.IterationCount = (int32)IterCount;
	Groom->MarkPackageDirty();
	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("success"), true); Root->SetStringField(TEXT("groom_path"), GPath);
	Root->SetNumberField(TEXT("group_index"), GroupIdx); Root->SetBoolField(TEXT("enable_simulation"), bEnableSim);
	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJson);
	FJsonSerializer::Serialize(Root.ToSharedRef(), W);
}

void HandleSetGroomRenderingFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	if (!IsHairLoaded()) { OutError = TEXT("HairStrandsCore module not loaded"); return; }

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("settings"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString GPath = BatchToolHelper::GetItemString(Item, TEXT("groom_path"), TEXT("asset_path"));
			double GI = 0, HW = -1;
			Item->TryGetNumberField(TEXT("group_index"), GI);
			Item->TryGetNumberField(TEXT("hair_width"), HW);
			UGroomAsset* Groom = Cast<UGroomAsset>(UEditorAssetLibrary::LoadAsset(GPath));
			if (!Groom) { Batch.AddFailure(i, FString::Printf(TEXT("GroomAsset not found: %s"), *GPath)); continue; }
			int32 GroupIdx = (int32)GI;
			if (GroupIdx >= Groom->GetHairGroupsRendering().Num()) { Batch.AddFailure(i, TEXT("group_index out of range")); continue; }
			if (HW > 0) { Groom->GetHairGroupsRendering()[GroupIdx].GeometrySettings.HairWidth = (float)HW; Groom->GetHairGroupsRendering()[GroupIdx].GeometrySettings.HairWidth_Override = true; }
			Groom->MarkPackageDirty();
			TSharedPtr<FJsonObject> E = MakeShared<FJsonObject>(); E->SetStringField(TEXT("groom_path"), GPath); E->SetNumberField(TEXT("group_index"), GroupIdx);
			Batch.AddSuccess(i, E);
		}
		Batch.Finalize(OutJson); return;
	}
	FString GPath; double GI = 0, HW = -1;
	Args->TryGetStringField(TEXT("groom_path"), GPath);
	Args->TryGetNumberField(TEXT("group_index"), GI);
	Args->TryGetNumberField(TEXT("hair_width"), HW);
	if (GPath.IsEmpty()) { OutError = TEXT("groom_path required"); return; }
	UGroomAsset* Groom = Cast<UGroomAsset>(UEditorAssetLibrary::LoadAsset(GPath));
	if (!Groom) { OutError = FString::Printf(TEXT("GroomAsset not found: %s"), *GPath); return; }
	int32 GroupIdx = (int32)GI;
	if (GroupIdx >= Groom->GetHairGroupsRendering().Num()) { OutError = TEXT("group_index out of range"); return; }
	if (HW > 0) { Groom->GetHairGroupsRendering()[GroupIdx].GeometrySettings.HairWidth = (float)HW; Groom->GetHairGroupsRendering()[GroupIdx].GeometrySettings.HairWidth_Override = true; }
	Groom->MarkPackageDirty();
	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("success"), true); Root->SetStringField(TEXT("groom_path"), GPath); Root->SetNumberField(TEXT("group_index"), GroupIdx);
	if (HW > 0) Root->SetNumberField(TEXT("hair_width"), HW);
	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJson);
	FJsonSerializer::Serialize(Root.ToSharedRef(), W);
}

void HandleAssignGroomMaterialFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	if (!IsHairLoaded()) { OutError = TEXT("HairStrandsCore module not loaded"); return; }

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("assignments"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString GPath = BatchToolHelper::GetItemString(Item, TEXT("groom_path"), TEXT("asset_path"));
			FString MatPath = BatchToolHelper::GetItemString(Item, TEXT("material_path"));
			double GI = 0; Item->TryGetNumberField(TEXT("group_index"), GI);
			UGroomAsset* Groom = Cast<UGroomAsset>(UEditorAssetLibrary::LoadAsset(GPath));
			if (!Groom) { Batch.AddFailure(i, FString::Printf(TEXT("GroomAsset not found: %s"), *GPath)); continue; }
			int32 GroupIdx = (int32)GI;
			if (GroupIdx >= Groom->GetHairGroupsRendering().Num()) { Batch.AddFailure(i, TEXT("group_index out of range")); continue; }
			FString MatName = FPackageName::GetShortName(MatPath);
			Groom->GetHairGroupsRendering()[GroupIdx].MaterialSlotName = FName(*MatName);
			if (!MatPath.IsEmpty())
			{
				if (UMaterialInterface* Mat = Cast<UMaterialInterface>(UEditorAssetLibrary::LoadAsset(MatPath)))
					Groom->GetHairGroupsRendering()[GroupIdx].Material = Mat;
			}
			Groom->MarkPackageDirty();
			TSharedPtr<FJsonObject> E = MakeShared<FJsonObject>(); E->SetStringField(TEXT("groom_path"), GPath); E->SetNumberField(TEXT("group_index"), GroupIdx); E->SetStringField(TEXT("material_slot_name"), MatName);
			Batch.AddSuccess(i, E);
		}
		Batch.Finalize(OutJson); return;
	}
	FString GPath, MatPath; double GI = 0;
	Args->TryGetStringField(TEXT("groom_path"), GPath); Args->TryGetStringField(TEXT("material_path"), MatPath);
	Args->TryGetNumberField(TEXT("group_index"), GI);
	if (GPath.IsEmpty() || MatPath.IsEmpty()) { OutError = TEXT("groom_path and material_path required"); return; }
	UGroomAsset* Groom = Cast<UGroomAsset>(UEditorAssetLibrary::LoadAsset(GPath));
	if (!Groom) { OutError = FString::Printf(TEXT("GroomAsset not found: %s"), *GPath); return; }
	int32 GroupIdx = (int32)GI;
	if (GroupIdx >= Groom->GetHairGroupsRendering().Num()) { OutError = TEXT("group_index out of range"); return; }
	FString MatName = FPackageName::GetShortName(MatPath);
	Groom->GetHairGroupsRendering()[GroupIdx].MaterialSlotName = FName(*MatName);
	if (UMaterialInterface* Mat = Cast<UMaterialInterface>(UEditorAssetLibrary::LoadAsset(MatPath)))
		Groom->GetHairGroupsRendering()[GroupIdx].Material = Mat;
	Groom->MarkPackageDirty();
	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("success"), true); Root->SetStringField(TEXT("groom_path"), GPath);
	Root->SetNumberField(TEXT("group_index"), GroupIdx); Root->SetStringField(TEXT("material_slot_name"), MatName);
	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJson);
	FJsonSerializer::Serialize(Root.ToSharedRef(), W);
}

void HandleSpawnGroomComponentFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	if (!IsHairLoaded()) { OutError = TEXT("HairStrandsCore module not loaded"); return; }

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World) { OutError = TEXT("No editor world"); return; }

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("actors"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString Label = BatchToolHelper::GetItemString(Item, TEXT("actor_label"));
			FString GPath = BatchToolHelper::GetItemString(Item, TEXT("groom_path"));
			double LX = 0, LY = 0, LZ = 0;
			Item->TryGetNumberField(TEXT("location_x"), LX); Item->TryGetNumberField(TEXT("location_y"), LY); Item->TryGetNumberField(TEXT("location_z"), LZ);
			FActorSpawnParameters SP; SP.Name = Label.IsEmpty() ? NAME_None : FName(*Label);
			AGroomActor* Actor = World->SpawnActor<AGroomActor>(AGroomActor::StaticClass(), FTransform(FVector((float)LX, (float)LY, (float)LZ)), SP);
			if (!Actor) { Batch.AddFailure(i, TEXT("Failed to spawn AGroomActor")); continue; }
			if (!Label.IsEmpty()) Actor->SetActorLabel(Label);
			if (!GPath.IsEmpty() && Actor->GetGroomComponent())
				if (UGroomAsset* G = Cast<UGroomAsset>(UEditorAssetLibrary::LoadAsset(GPath)))
					Actor->GetGroomComponent()->SetGroomAsset(G);
			TSharedPtr<FJsonObject> E = MakeShared<FJsonObject>(); E->SetStringField(TEXT("actor_label"), Actor->GetActorLabel()); E->SetStringField(TEXT("groom_path"), GPath);
			Batch.AddSuccess(i, E);
		}
		Batch.Finalize(OutJson); return;
	}
	FString Label, GPath; double LX = 0, LY = 0, LZ = 0;
	Args->TryGetStringField(TEXT("actor_label"), Label); Args->TryGetStringField(TEXT("groom_path"), GPath);
	Args->TryGetNumberField(TEXT("location_x"), LX); Args->TryGetNumberField(TEXT("location_y"), LY); Args->TryGetNumberField(TEXT("location_z"), LZ);
	FActorSpawnParameters SP; SP.Name = Label.IsEmpty() ? NAME_None : FName(*Label);
	AGroomActor* Actor = World->SpawnActor<AGroomActor>(AGroomActor::StaticClass(), FTransform(FVector((float)LX, (float)LY, (float)LZ)), SP);
	if (!Actor) { OutError = TEXT("Failed to spawn AGroomActor"); return; }
	if (!Label.IsEmpty()) Actor->SetActorLabel(Label);
	if (!GPath.IsEmpty() && Actor->GetGroomComponent())
		if (UGroomAsset* G = Cast<UGroomAsset>(UEditorAssetLibrary::LoadAsset(GPath)))
			Actor->GetGroomComponent()->SetGroomAsset(G);
	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("success"), true); Root->SetStringField(TEXT("actor_label"), Actor->GetActorLabel()); Root->SetStringField(TEXT("groom_path"), GPath);
	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJson);
	FJsonSerializer::Serialize(Root.ToSharedRef(), W);
}

}
