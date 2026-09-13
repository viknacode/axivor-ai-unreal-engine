// Copyright 2026, BlueprintsLab, All rights reserved

#include "Tools/GeometryScriptTools.h"
#include "Tools/BatchToolHelper.h"
#include "DynamicMeshActor.h"
#include "Components/DynamicMeshComponent.h"
#include "UDynamicMesh.h"
#include "GeometryScript/MeshPrimitiveFunctions.h"
#include "GeometryScript/MeshBooleanFunctions.h"
#include "GeometryScript/MeshAssetFunctions.h"
#include "GeometryScript/MeshQueryFunctions.h"
#include "Subsystems/EditorActorSubsystem.h"
#include "Engine/StaticMesh.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"

DEFINE_LOG_CATEGORY_STATIC(LogGeometryScriptTools, Log, All);

namespace GeometryScriptTools
{

static ADynamicMeshActor* FindDynamicMeshActor(UWorld* World, const FString& Label)
{
	if (!World) return nullptr;
	for (TActorIterator<ADynamicMeshActor> It(World); It; ++It)
	{
		if (It->GetActorLabel() == Label)
			return *It;
	}
	return nullptr;
}

static UWorld* GetEditorWorld()
{
	return GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
}

static void SpawnSingleDynamicMeshActor(const FString& Label, float X, float Y, float Z,
	FString& OutJsonString, FString& OutError)
{
	if (Label.IsEmpty()) { OutError = TEXT("actor_label is required"); return; }

	UWorld* World = GetEditorWorld();
	if (!World) { OutError = TEXT("No editor world"); return; }

	UEditorActorSubsystem* EAS = GEditor->GetEditorSubsystem<UEditorActorSubsystem>();
	if (!EAS) { OutError = TEXT("EditorActorSubsystem unavailable"); return; }

	AActor* SpawnedActor = EAS->SpawnActorFromClass(ADynamicMeshActor::StaticClass(), FVector(X, Y, Z));
	if (!SpawnedActor) { OutError = TEXT("Failed to spawn ADynamicMeshActor"); return; }

	SpawnedActor->SetActorLabel(Label);

	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"actor_label\":\"%s\",\"location_x\":%.1f,\"location_y\":%.1f,\"location_z\":%.1f}"),
		*Label, X, Y, Z);
	UE_LOG(LogGeometryScriptTools, Log, TEXT("spawn_dynamic_mesh_actor: %s at (%.1f,%.1f,%.1f)"), *Label, X, Y, Z);
}

void HandleSpawnDynamicMeshActorFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("actors"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString Label = BatchToolHelper::GetItemString(Item, TEXT("actor_label"));
			double X = 0, Y = 0, Z = 0;
			Item->TryGetNumberField(TEXT("location_x"), X);
			Item->TryGetNumberField(TEXT("location_y"), Y);
			Item->TryGetNumberField(TEXT("location_z"), Z);
			FString ItemOut, ItemErr;
			SpawnSingleDynamicMeshActor(Label, (float)X, (float)Y, (float)Z, ItemOut, ItemErr);
			if (ItemErr.IsEmpty()) { auto E = MakeShared<FJsonObject>(); E->SetStringField(TEXT("actor_label"), Label); Batch.AddSuccess(i, E); }
			else Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJsonString);
		return;
	}
	FString Label;
	double X = 0, Y = 0, Z = 0;
	Args->TryGetStringField(TEXT("actor_label"), Label);
	Args->TryGetNumberField(TEXT("location_x"), X);
	Args->TryGetNumberField(TEXT("location_y"), Y);
	Args->TryGetNumberField(TEXT("location_z"), Z);
	SpawnSingleDynamicMeshActor(Label, (float)X, (float)Y, (float)Z, OutJsonString, OutError);
}

static void AppendBoxSingle(const TSharedPtr<FJsonObject>& Item, FString& OutJsonString, FString& OutError)
{
	FString ActorLabel = BatchToolHelper::GetItemString(Item, TEXT("actor_label"));
	if (ActorLabel.IsEmpty()) { OutError = TEXT("actor_label is required"); return; }

	ADynamicMeshActor* Actor = FindDynamicMeshActor(GetEditorWorld(), ActorLabel);
	if (!Actor) { OutError = FString::Printf(TEXT("DynamicMeshActor not found: %s"), *ActorLabel); return; }

	UDynamicMeshComponent* DMC = Actor->GetDynamicMeshComponent();
	if (!DMC) { OutError = TEXT("No DynamicMeshComponent"); return; }

	double SX = 100, SY = 100, SZ = 100, OX = 0, OY = 0, OZ = 0, RZ = 0;
	Item->TryGetNumberField(TEXT("size_x"), SX); Item->TryGetNumberField(TEXT("size_y"), SY); Item->TryGetNumberField(TEXT("size_z"), SZ);
	Item->TryGetNumberField(TEXT("offset_x"), OX); Item->TryGetNumberField(TEXT("offset_y"), OY); Item->TryGetNumberField(TEXT("offset_z"), OZ);
	Item->TryGetNumberField(TEXT("rotation_z"), RZ);

	FGeometryScriptPrimitiveOptions PrimOptions;
	FTransform OffsetTransform(FRotator(0.f, (float)RZ, 0.f), FVector((float)OX, (float)OY, (float)OZ));
	UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendBox(DMC->GetDynamicMesh(), PrimOptions, OffsetTransform,
		(float)SX, (float)SY, (float)SZ, 0, 0, 0, EGeometryScriptPrimitiveOriginMode::Base);
	DMC->NotifyMeshUpdated();

	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"actor_label\":\"%s\",\"size_x\":%.1f,\"size_y\":%.1f,\"size_z\":%.1f}"),
		*ActorLabel, (float)SX, (float)SY, (float)SZ);
	UE_LOG(LogGeometryScriptTools, Log, TEXT("append_box: %s (%.1f x %.1f x %.1f)"), *ActorLabel, (float)SX, (float)SY, (float)SZ);
}

void HandleAppendBoxFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("boxes"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString ItemOut, ItemErr;
			AppendBoxSingle(Item, ItemOut, ItemErr);
			if (ItemErr.IsEmpty()) { auto E = MakeShared<FJsonObject>(); E->SetStringField(TEXT("actor_label"), BatchToolHelper::GetItemString(Item, TEXT("actor_label"))); Batch.AddSuccess(i, E); }
			else Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJsonString);
		return;
	}
	AppendBoxSingle(Args, OutJsonString, OutError);
}

static void AppendSphereSingle(const TSharedPtr<FJsonObject>& Item, FString& OutJsonString, FString& OutError)
{
	FString ActorLabel = BatchToolHelper::GetItemString(Item, TEXT("actor_label"));
	if (ActorLabel.IsEmpty()) { OutError = TEXT("actor_label is required"); return; }

	ADynamicMeshActor* Actor = FindDynamicMeshActor(GetEditorWorld(), ActorLabel);
	if (!Actor) { OutError = FString::Printf(TEXT("DynamicMeshActor not found: %s"), *ActorLabel); return; }

	UDynamicMeshComponent* DMC = Actor->GetDynamicMeshComponent();
	if (!DMC) { OutError = TEXT("No DynamicMeshComponent"); return; }

	double R = 50, LD = 10, LoD = 16, OX = 0, OY = 0, OZ = 0, RZ = 0;
	Item->TryGetNumberField(TEXT("radius"), R);
	Item->TryGetNumberField(TEXT("latitude_steps"), LD);
	Item->TryGetNumberField(TEXT("longitude_steps"), LoD);
	Item->TryGetNumberField(TEXT("offset_x"), OX);
	Item->TryGetNumberField(TEXT("offset_y"), OY);
	Item->TryGetNumberField(TEXT("offset_z"), OZ);
	Item->TryGetNumberField(TEXT("rotation_z"), RZ);

	FGeometryScriptPrimitiveOptions PrimOptions;
	FTransform OffsetTransform(FRotator(0.f, (float)RZ, 0.f), FVector((float)OX, (float)OY, (float)OZ));
	UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendSphereLatLong(DMC->GetDynamicMesh(), PrimOptions, OffsetTransform,
		(float)R, (int32)LD, (int32)LoD, EGeometryScriptPrimitiveOriginMode::Center);
	DMC->NotifyMeshUpdated();

	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"actor_label\":\"%s\",\"radius\":%.1f}"), *ActorLabel, (float)R);
	UE_LOG(LogGeometryScriptTools, Log, TEXT("append_sphere: %s radius=%.1f"), *ActorLabel, (float)R);
}

void HandleAppendSphereFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("spheres"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString ItemOut, ItemErr;
			AppendSphereSingle(Item, ItemOut, ItemErr);
			if (ItemErr.IsEmpty()) { auto E = MakeShared<FJsonObject>(); E->SetStringField(TEXT("actor_label"), BatchToolHelper::GetItemString(Item, TEXT("actor_label"))); Batch.AddSuccess(i, E); }
			else Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJsonString);
		return;
	}
	AppendSphereSingle(Args, OutJsonString, OutError);
}

static void AppendCylinderSingle(const TSharedPtr<FJsonObject>& Item, FString& OutJsonString, FString& OutError)
{
	FString ActorLabel = BatchToolHelper::GetItemString(Item, TEXT("actor_label"));
	if (ActorLabel.IsEmpty()) { OutError = TEXT("actor_label is required"); return; }

	ADynamicMeshActor* Actor = FindDynamicMeshActor(GetEditorWorld(), ActorLabel);
	if (!Actor) { OutError = FString::Printf(TEXT("DynamicMeshActor not found: %s"), *ActorLabel); return; }

	UDynamicMeshComponent* DMC = Actor->GetDynamicMeshComponent();
	if (!DMC) { OutError = TEXT("No DynamicMeshComponent"); return; }

	double R = 50, H = 100, RSD = 12, OX = 0, OY = 0, OZ = 0, RZ = 0;
	Item->TryGetNumberField(TEXT("radius"), R);
	Item->TryGetNumberField(TEXT("height"), H);
	Item->TryGetNumberField(TEXT("radial_steps"), RSD);
	Item->TryGetNumberField(TEXT("offset_x"), OX);
	Item->TryGetNumberField(TEXT("offset_y"), OY);
	Item->TryGetNumberField(TEXT("offset_z"), OZ);
	Item->TryGetNumberField(TEXT("rotation_z"), RZ);

	FGeometryScriptPrimitiveOptions PrimOptions;
	FTransform OffsetTransform(FRotator(0.f, (float)RZ, 0.f), FVector((float)OX, (float)OY, (float)OZ));
	UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendCylinder(DMC->GetDynamicMesh(), PrimOptions, OffsetTransform,
		(float)R, (float)H, (int32)RSD, 0, true, EGeometryScriptPrimitiveOriginMode::Base);
	DMC->NotifyMeshUpdated();

	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"actor_label\":\"%s\",\"radius\":%.1f,\"height\":%.1f}"), *ActorLabel, (float)R, (float)H);
	UE_LOG(LogGeometryScriptTools, Log, TEXT("append_cylinder: %s radius=%.1f height=%.1f"), *ActorLabel, (float)R, (float)H);
}

void HandleAppendCylinderFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("cylinders"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString ItemOut, ItemErr;
			AppendCylinderSingle(Item, ItemOut, ItemErr);
			if (ItemErr.IsEmpty()) { auto E = MakeShared<FJsonObject>(); E->SetStringField(TEXT("actor_label"), BatchToolHelper::GetItemString(Item, TEXT("actor_label"))); Batch.AddSuccess(i, E); }
			else Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJsonString);
		return;
	}
	AppendCylinderSingle(Args, OutJsonString, OutError);
}

static void AppendConeSingle(const TSharedPtr<FJsonObject>& Item, FString& OutJsonString, FString& OutError)
{
	FString ActorLabel = BatchToolHelper::GetItemString(Item, TEXT("actor_label"));
	if (ActorLabel.IsEmpty()) { OutError = TEXT("actor_label is required"); return; }

	ADynamicMeshActor* Actor = FindDynamicMeshActor(GetEditorWorld(), ActorLabel);
	if (!Actor) { OutError = FString::Printf(TEXT("DynamicMeshActor not found: %s"), *ActorLabel); return; }

	UDynamicMeshComponent* DMC = Actor->GetDynamicMeshComponent();
	if (!DMC) { OutError = TEXT("No DynamicMeshComponent"); return; }

	double BR = 50, TR = 0, H = 100, RSD = 12, OX = 0, OY = 0, OZ = 0, RZ = 0;
	Item->TryGetNumberField(TEXT("base_radius"), BR);
	Item->TryGetNumberField(TEXT("apex_radius"), TR);
	Item->TryGetNumberField(TEXT("height"), H);
	Item->TryGetNumberField(TEXT("radial_steps"), RSD);
	Item->TryGetNumberField(TEXT("offset_x"), OX);
	Item->TryGetNumberField(TEXT("offset_y"), OY);
	Item->TryGetNumberField(TEXT("offset_z"), OZ);
	Item->TryGetNumberField(TEXT("rotation_z"), RZ);

	FGeometryScriptPrimitiveOptions PrimOptions;
	FTransform OffsetTransform(FRotator(0.f, (float)RZ, 0.f), FVector((float)OX, (float)OY, (float)OZ));
	UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendCone(DMC->GetDynamicMesh(), PrimOptions, OffsetTransform,
		(float)BR, (float)TR, (float)H, (int32)RSD, 0, true, EGeometryScriptPrimitiveOriginMode::Base);
	DMC->NotifyMeshUpdated();

	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"actor_label\":\"%s\",\"base_radius\":%.1f,\"top_radius\":%.1f,\"height\":%.1f}"),
		*ActorLabel, (float)BR, (float)TR, (float)H);
	UE_LOG(LogGeometryScriptTools, Log, TEXT("append_cone: %s base=%.1f top=%.1f h=%.1f"), *ActorLabel, (float)BR, (float)TR, (float)H);
}

void HandleAppendConeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("cones"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString ItemOut, ItemErr;
			AppendConeSingle(Item, ItemOut, ItemErr);
			if (ItemErr.IsEmpty()) { auto E = MakeShared<FJsonObject>(); E->SetStringField(TEXT("actor_label"), BatchToolHelper::GetItemString(Item, TEXT("actor_label"))); Batch.AddSuccess(i, E); }
			else Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJsonString);
		return;
	}
	AppendConeSingle(Args, OutJsonString, OutError);
}

static void CopyMeshFromStaticMeshSingle(const TSharedPtr<FJsonObject>& Item, FString& OutJsonString, FString& OutError)
{
	FString ActorLabel = BatchToolHelper::GetItemString(Item, TEXT("actor_label"));
	FString StaticMeshPath = BatchToolHelper::GetItemString(Item, TEXT("static_mesh_path"));
	if (ActorLabel.IsEmpty() || StaticMeshPath.IsEmpty()) { OutError = TEXT("actor_label and static_mesh_path are required"); return; }

	ADynamicMeshActor* Actor = FindDynamicMeshActor(GetEditorWorld(), ActorLabel);
	if (!Actor) { OutError = FString::Printf(TEXT("DynamicMeshActor not found: %s"), *ActorLabel); return; }

	UStaticMesh* SMesh = LoadObject<UStaticMesh>(nullptr, *StaticMeshPath);
	if (!SMesh) { OutError = FString::Printf(TEXT("UStaticMesh not found: %s"), *StaticMeshPath); return; }

	UDynamicMeshComponent* DMC = Actor->GetDynamicMeshComponent();
	if (!DMC) { OutError = TEXT("No DynamicMeshComponent"); return; }

	FGeometryScriptCopyMeshFromAssetOptions CopyOptions;
	EGeometryScriptOutcomePins Outcome;
	UGeometryScriptLibrary_StaticMeshFunctions::CopyMeshFromStaticMesh(SMesh, DMC->GetDynamicMesh(), CopyOptions,
		FGeometryScriptMeshReadLOD(), Outcome);

	if (Outcome == EGeometryScriptOutcomePins::Failure)
	{
		OutError = FString::Printf(TEXT("CopyMeshFromStaticMesh failed for %s"), *StaticMeshPath);
		return;
	}

	DMC->NotifyMeshUpdated();

	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"actor_label\":\"%s\",\"static_mesh_path\":\"%s\"}"),
		*ActorLabel, *StaticMeshPath);
	UE_LOG(LogGeometryScriptTools, Log, TEXT("copy_mesh_from_static_mesh: %s <- %s"), *ActorLabel, *StaticMeshPath);
}

void HandleCopyMeshFromStaticMeshFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("copies"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			if (!Item->HasField(TEXT("static_mesh_path")))
			{
				FString OuterPath; Args->TryGetStringField(TEXT("static_mesh_path"), OuterPath);
				if (!OuterPath.IsEmpty()) Item->SetStringField(TEXT("static_mesh_path"), OuterPath);
			}
			FString ItemOut, ItemErr;
			CopyMeshFromStaticMeshSingle(Item, ItemOut, ItemErr);
			if (ItemErr.IsEmpty()) { auto E = MakeShared<FJsonObject>(); E->SetStringField(TEXT("actor_label"), BatchToolHelper::GetItemString(Item, TEXT("actor_label"))); Batch.AddSuccess(i, E); }
			else Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJsonString);
		return;
	}
	CopyMeshFromStaticMeshSingle(Args, OutJsonString, OutError);
}

static void ApplyBooleanSingle(const TSharedPtr<FJsonObject>& Item, bool bDeleteToolActorDefault,
	FString& OutJsonString, FString& OutError)
{
	FString TargetLabel = BatchToolHelper::GetItemString(Item, TEXT("actor_label"));
	FString ToolLabel   = BatchToolHelper::GetItemString(Item, TEXT("tool_actor_label"));
	FString Operation   = BatchToolHelper::GetItemString(Item, TEXT("operation"));

	if (TargetLabel.IsEmpty() || ToolLabel.IsEmpty()) { OutError = TEXT("actor_label and tool_actor_label are required"); return; }

	bool bDeleteToolActor = bDeleteToolActorDefault;
	bool bDeleteOverride;
	if (Item->TryGetBoolField(TEXT("delete_tool_actor"), bDeleteOverride))
		bDeleteToolActor = bDeleteOverride;

	UWorld* World = GetEditorWorld();
	ADynamicMeshActor* TargetActor = FindDynamicMeshActor(World, TargetLabel);
	if (!TargetActor) { OutError = FString::Printf(TEXT("Target DynamicMeshActor not found: %s"), *TargetLabel); return; }

	ADynamicMeshActor* ToolActor = FindDynamicMeshActor(World, ToolLabel);
	if (!ToolActor) { OutError = FString::Printf(TEXT("Tool DynamicMeshActor not found: %s"), *ToolLabel); return; }

	UDynamicMeshComponent* TargetDMC = TargetActor->GetDynamicMeshComponent();
	UDynamicMeshComponent* ToolDMC   = ToolActor->GetDynamicMeshComponent();
	if (!TargetDMC || !ToolDMC) { OutError = TEXT("Missing DynamicMeshComponent"); return; }

	EGeometryScriptBooleanOperation BoolOp = EGeometryScriptBooleanOperation::Union;
	if (Operation == TEXT("Subtract"))
		BoolOp = EGeometryScriptBooleanOperation::Subtract;
	else if (Operation == TEXT("Intersect") || Operation == TEXT("Intersection"))
		BoolOp = EGeometryScriptBooleanOperation::Intersection;

	FGeometryScriptMeshBooleanOptions BoolOptions;
	UGeometryScriptLibrary_MeshBooleanFunctions::ApplyMeshBoolean(
		TargetDMC->GetDynamicMesh(), TargetActor->GetActorTransform(),
		ToolDMC->GetDynamicMesh(), ToolActor->GetActorTransform(),
		BoolOp, BoolOptions, nullptr);

	TargetDMC->NotifyMeshUpdated();

	if (bDeleteToolActor)
	{
		if (UEditorActorSubsystem* EAS = GEditor->GetEditorSubsystem<UEditorActorSubsystem>())
			EAS->DestroyActor(ToolActor);
	}

	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"actor_label\":\"%s\",\"tool_actor_label\":\"%s\",\"operation\":\"%s\",\"tool_deleted\":%s}"),
		*TargetLabel, *ToolLabel, *Operation, bDeleteToolActor ? TEXT("true") : TEXT("false"));
	UE_LOG(LogGeometryScriptTools, Log, TEXT("apply_boolean_operation: %s %s %s (tool_deleted=%d)"), *TargetLabel, *Operation, *ToolLabel, (int32)bDeleteToolActor);
}

void HandleApplyBooleanOperationFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	bool bDeleteDefault = true;
	Args->TryGetBoolField(TEXT("delete_tool_actor"), bDeleteDefault);

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("operations"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString ItemOut, ItemErr;
			ApplyBooleanSingle(Item, bDeleteDefault, ItemOut, ItemErr);
			if (ItemErr.IsEmpty()) { auto E = MakeShared<FJsonObject>(); E->SetStringField(TEXT("actor_label"), BatchToolHelper::GetItemString(Item, TEXT("actor_label"))); Batch.AddSuccess(i, E); }
			else Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJsonString);
		return;
	}
	ApplyBooleanSingle(Args, bDeleteDefault, OutJsonString, OutError);
}

static void BakeToStaticMeshSingle(const TSharedPtr<FJsonObject>& Item, const FString& OuterSavePath,
	FString& OutJsonString, FString& OutError)
{
	FString ActorLabel = BatchToolHelper::GetItemString(Item, TEXT("actor_label"));
	FString SavePath   = BatchToolHelper::GetItemString(Item, TEXT("save_path"));
	FString Name       = BatchToolHelper::GetItemString(Item, TEXT("name"));

	if (SavePath.IsEmpty()) SavePath = OuterSavePath;
	if (ActorLabel.IsEmpty() || SavePath.IsEmpty() || Name.IsEmpty())
	{
		OutError = TEXT("actor_label, save_path, and name are required");
		return;
	}

	ADynamicMeshActor* Actor = FindDynamicMeshActor(GetEditorWorld(), ActorLabel);
	if (!Actor) { OutError = FString::Printf(TEXT("DynamicMeshActor not found: %s"), *ActorLabel); return; }

	UDynamicMeshComponent* DMC = Actor->GetDynamicMeshComponent();
	if (!DMC) { OutError = TEXT("No DynamicMeshComponent"); return; }

	FString PathStr = SavePath;
	while (PathStr.EndsWith(TEXT("/"))) PathStr = PathStr.LeftChop(1);
	FString PackagePath = FString::Printf(TEXT("%s/%s"), *PathStr, *Name);

	UPackage* Package = CreatePackage(*PackagePath);
	if (!Package) { OutError = FString::Printf(TEXT("Failed to create package: %s"), *PackagePath); return; }

	UStaticMesh* NewMesh = NewObject<UStaticMesh>(Package, *Name, RF_Public | RF_Standalone | RF_Transactional);
	if (!NewMesh) { OutError = TEXT("Failed to create UStaticMesh"); return; }

	FGeometryScriptCopyMeshToAssetOptions CopyOptions;
	EGeometryScriptOutcomePins Outcome;
	UGeometryScriptLibrary_StaticMeshFunctions::CopyMeshToStaticMesh(
		DMC->GetDynamicMesh(), NewMesh, CopyOptions, FGeometryScriptMeshWriteLOD(), Outcome);

	if (Outcome == EGeometryScriptOutcomePins::Failure)
	{
		OutError = TEXT("CopyMeshToStaticMesh failed");
		return;
	}

	FAssetRegistryModule::AssetCreated(NewMesh);
	Package->MarkPackageDirty();

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	FString Filename = FPackageName::LongPackageNameToFilename(PackagePath, FPackageName::GetAssetPackageExtension());
	UPackage::SavePackage(Package, NewMesh, *Filename, SaveArgs);

	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"actor_label\":\"%s\",\"asset_path\":\"%s\"}"),
		*ActorLabel, *PackagePath);
	UE_LOG(LogGeometryScriptTools, Log, TEXT("bake_to_static_mesh: %s -> %s"), *ActorLabel, *PackagePath);
}

void HandleBakeToStaticMeshFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString OuterSavePath;
	Args->TryGetStringField(TEXT("save_path"), OuterSavePath);

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("meshes"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString ItemOut, ItemErr;
			BakeToStaticMeshSingle(Item, OuterSavePath, ItemOut, ItemErr);
			if (ItemErr.IsEmpty()) { auto E = MakeShared<FJsonObject>(); E->SetStringField(TEXT("actor_label"), BatchToolHelper::GetItemString(Item, TEXT("actor_label"))); Batch.AddSuccess(i, E); }
			else Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJsonString);
		return;
	}
	BakeToStaticMeshSingle(Args, OuterSavePath, OutJsonString, OutError);
}

static void ClearMeshSingle(const FString& ActorLabel, FString& OutJsonString, FString& OutError)
{
	if (ActorLabel.IsEmpty()) { OutError = TEXT("actor_label is required"); return; }

	ADynamicMeshActor* Actor = FindDynamicMeshActor(GetEditorWorld(), ActorLabel);
	if (!Actor) { OutError = FString::Printf(TEXT("DynamicMeshActor not found: %s"), *ActorLabel); return; }

	UDynamicMeshComponent* DMC = Actor->GetDynamicMeshComponent();
	if (!DMC) { OutError = TEXT("No DynamicMeshComponent"); return; }

	DMC->GetDynamicMesh()->Reset();
	DMC->NotifyMeshUpdated();

	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"actor_label\":\"%s\"}"), *ActorLabel);
	UE_LOG(LogGeometryScriptTools, Log, TEXT("clear_mesh: %s"), *ActorLabel);
}

void HandleClearMeshFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("actors"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			FString Label;
			if ((*ItemsArray)[i]->Type == EJson::String)
				Label = (*ItemsArray)[i]->AsString();
			else if (TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject())
				Label = BatchToolHelper::GetItemString(Item, TEXT("actor_label"));

			if (Label.IsEmpty()) { Batch.AddFailure(i, TEXT("actor_label required")); continue; }
			FString ItemOut, ItemErr;
			ClearMeshSingle(Label, ItemOut, ItemErr);
			if (ItemErr.IsEmpty()) { auto E = MakeShared<FJsonObject>(); E->SetStringField(TEXT("actor_label"), Label); Batch.AddSuccess(i, E); }
			else Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJsonString);
		return;
	}
	FString Label;
	Args->TryGetStringField(TEXT("actor_label"), Label);
	ClearMeshSingle(Label, OutJsonString, OutError);
}

static void GetMeshInfoSingle(const FString& ActorLabel, TSharedPtr<FJsonObject>& OutObj, FString& OutError)
{
	if (ActorLabel.IsEmpty()) { OutError = TEXT("actor_label is required"); return; }

	ADynamicMeshActor* Actor = FindDynamicMeshActor(GetEditorWorld(), ActorLabel);
	if (!Actor) { OutError = FString::Printf(TEXT("DynamicMeshActor not found: %s"), *ActorLabel); return; }

	UDynamicMeshComponent* DMC = Actor->GetDynamicMeshComponent();
	if (!DMC) { OutError = TEXT("No DynamicMeshComponent"); return; }

	UDynamicMesh* DynMesh = DMC->GetDynamicMesh();

	int32 TriIDCount = UGeometryScriptLibrary_MeshQueryFunctions::GetNumTriangleIDs(DynMesh);
	bool  bHasTriGaps = UGeometryScriptLibrary_MeshQueryFunctions::GetHasTriangleIDGaps(DynMesh);
	int32 VertCount  = UGeometryScriptLibrary_MeshQueryFunctions::GetVertexCount(DynMesh);
	FBox  Bounds     = UGeometryScriptLibrary_MeshQueryFunctions::GetMeshBoundingBox(DynMesh);

	OutObj = MakeShareable(new FJsonObject());
	OutObj->SetBoolField(TEXT("success"), true);
	OutObj->SetStringField(TEXT("actor_label"), ActorLabel);
	OutObj->SetNumberField(TEXT("triangle_id_count"), TriIDCount);
	OutObj->SetBoolField(TEXT("has_triangle_id_gaps"), bHasTriGaps);
	OutObj->SetNumberField(TEXT("vertex_count"), VertCount);

	TSharedPtr<FJsonObject> BoundsMin = MakeShareable(new FJsonObject());
	BoundsMin->SetNumberField(TEXT("x"), Bounds.Min.X);
	BoundsMin->SetNumberField(TEXT("y"), Bounds.Min.Y);
	BoundsMin->SetNumberField(TEXT("z"), Bounds.Min.Z);
	TSharedPtr<FJsonObject> BoundsMax = MakeShareable(new FJsonObject());
	BoundsMax->SetNumberField(TEXT("x"), Bounds.Max.X);
	BoundsMax->SetNumberField(TEXT("y"), Bounds.Max.Y);
	BoundsMax->SetNumberField(TEXT("z"), Bounds.Max.Z);
	OutObj->SetObjectField(TEXT("bounds_min"), BoundsMin);
	OutObj->SetObjectField(TEXT("bounds_max"), BoundsMax);

	UE_LOG(LogGeometryScriptTools, Log, TEXT("get_mesh_info: %s tri_ids=%d verts=%d gaps=%d"), *ActorLabel, TriIDCount, VertCount, (int32)bHasTriGaps);
}

void HandleGetMeshInfoFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("actors"), ItemsArray))
	{
		TArray<TSharedPtr<FJsonValue>> ResultArray;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			FString Label;
			if ((*ItemsArray)[i]->Type == EJson::String)
				Label = (*ItemsArray)[i]->AsString();
			else if (TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject())
				Label = BatchToolHelper::GetItemString(Item, TEXT("actor_label"));

			TSharedPtr<FJsonObject> InfoObj;
			FString ItemErr;
			GetMeshInfoSingle(Label, InfoObj, ItemErr);
			if (!InfoObj.IsValid())
			{
				TSharedPtr<FJsonObject> ErrObj = MakeShareable(new FJsonObject());
				ErrObj->SetBoolField(TEXT("success"), false);
				ErrObj->SetStringField(TEXT("actor_label"), Label);
				ErrObj->SetStringField(TEXT("error"), ItemErr);
				InfoObj = ErrObj;
			}
			ResultArray.Add(MakeShared<FJsonValueObject>(InfoObj));
		}
		TSharedPtr<FJsonObject> Root = MakeShareable(new FJsonObject());
		Root->SetBoolField(TEXT("success"), true);
		Root->SetArrayField(TEXT("results"), ResultArray);
		TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJsonString);
		FJsonSerializer::Serialize(Root.ToSharedRef(), W);
		return;
	}

	FString Label;
	Args->TryGetStringField(TEXT("actor_label"), Label);
	TSharedPtr<FJsonObject> InfoObj;
	GetMeshInfoSingle(Label, InfoObj, OutError);
	if (InfoObj.IsValid())
	{
		TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJsonString);
		FJsonSerializer::Serialize(InfoObj.ToSharedRef(), W);
	}
}

}
