// Copyright 2026, BlueprintsLab, All rights reserved.

#include "Tools/FoliageTools.h"
#include "Managers/SettingsManager.h"
#include "Tools/BatchToolHelper.h"

#include "EditorAssetLibrary.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/Package.h"
#include "Editor.h"
#include "EngineUtils.h"

#include "FoliageType_InstancedStaticMesh.h"
#include "UObject/UObjectHash.h"
#include "FoliageType.h"
#include "InstancedFoliageActor.h"
#include "InstancedFoliage.h"
#include "ProceduralFoliageSpawner.h"
#include "ProceduralFoliageVolume.h"
#include "ProceduralFoliageComponent.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonWriter.h"
#include "Engine/CollisionProfile.h"
#include "Misc/Paths.h"
#include "MCPToolsLog.h"
#include "ScopedTransaction.h"
#include "CollisionQueryParams.h"

namespace FoliageTools
{

void HandleAddFoliageType(const FString& StaticMeshPath, const FString& SavePath,
	FString& OutJsonString, FString& OutError)
{

	UStaticMesh* Mesh = Cast<UStaticMesh>(UEditorAssetLibrary::LoadAsset(StaticMeshPath));
	if (!Mesh) { OutError = FString::Printf(TEXT("StaticMesh not found: %s"), *StaticMeshPath); return; }

	FString MeshName = Mesh->GetName();
	MeshName.RemoveFromStart(TEXT("SM_"));
	FString AssetName = TEXT("FT_") + MeshName;

	if (AssetName.IsEmpty()) { OutError = TEXT("name is required"); return; }
	FString PackagePath = SavePath;
	while (PackagePath.EndsWith(TEXT("/"))) PackagePath = PackagePath.LeftChop(1);
	PackagePath += TEXT("/") + AssetName;

	if (FPackageName::DoesPackageExist(PackagePath))
	{
		OutJsonString = FString::Printf(TEXT("{\"success\":true,\"asset_path\":\"%s\",\"message\":\"FoliageType already exists.\"}"), *PackagePath);
		return;
	}

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	UObject* NewAsset = AssetTools.CreateAsset(AssetName, SavePath, UFoliageType_InstancedStaticMesh::StaticClass(), nullptr);
	if (!NewAsset) { OutError = TEXT("Failed to create FoliageType asset"); return; }

	UFoliageType_InstancedStaticMesh* FoliageType = Cast<UFoliageType_InstancedStaticMesh>(NewAsset);
	if (FoliageType)
	{
		FoliageType->SetStaticMesh(Mesh);
		FoliageType->MarkPackageDirty();
	}

	UEditorAssetLibrary::SaveAsset(NewAsset->GetPathName(), false);

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"asset_path\":\"%s\",\"mesh\":\"%s\",\"message\":\"FoliageType created. Use paint_foliage to scatter in level.\"}"),
		*NewAsset->GetPathName(), *StaticMeshPath);
}

void HandlePaintFoliage(const FString& FoliageTypePath, const FVector& Location,
	float Radius, float Density, int32 MaxInstances, FString& OutJsonString, FString& OutError)
{

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World) { OutError = TEXT("No editor world"); return; }

	if (Radius <= 0.f) { OutError = TEXT("radius must be > 0"); return; }
	if (MaxInstances <= 0) MaxInstances = 2000;

	UFoliageType* FoliageType = Cast<UFoliageType>(UEditorAssetLibrary::LoadAsset(FoliageTypePath));
	if (!FoliageType) { OutError = TEXT("FoliageType not found: ") + FoliageTypePath; return; }

	AInstancedFoliageActor* IFA = AInstancedFoliageActor::GetInstancedFoliageActorForCurrentLevel(World, true);
	if (!IFA) { OutError = TEXT("Could not get/create InstancedFoliageActor"); return; }

	const FScopedTransaction Transaction(NSLOCTEXT("AxivorAI", "PaintFoliage", "Paint Foliage"));
	IFA->Modify();

	FFoliageInfo* FoliageInfo = nullptr;
	UFoliageType* FoliageTypePtr = FoliageType;
	IFA->AddFoliageType(FoliageTypePtr, &FoliageInfo);

	if (!FoliageInfo) { OutError = TEXT("Could not get FoliageInfo"); return; }

	int32 CandidateCount = FMath::RoundToInt(Density * FMath::Square(Radius) * PI / 10000.f);
	CandidateCount = FMath::Clamp(CandidateCount, 1, MaxInstances);

	// Placement rules come from the FoliageType asset (same fields the Foliage Edit mode brush honours).
	const float MinDistance = FMath::Max(0.f, FoliageType->Radius);
	const float MinDistanceSq = MinDistance * MinDistance;
	const FFloatInterval SlopeRange = FoliageType->GroundSlopeAngle;
	const bool bAlignToNormal = FoliageType->AlignToNormal != 0;
	const bool bRandomYaw = FoliageType->RandomYaw != 0;
	const float RandomPitch = FMath::Max(0.f, FoliageType->RandomPitchAngle);

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(AxivorPaintFoliage), true);
	QueryParams.AddIgnoredActor(IFA);

	TArray<FFoliageInstance> FoliageInstances;
	FoliageInstances.Reserve(CandidateCount);
	TArray<FVector> PlacedLocations;
	PlacedLocations.Reserve(CandidateCount);
	int32 SkippedNoHit = 0;
	int32 SkippedSlope = 0;
	int32 SkippedTooClose = 0;

	for (int32 i = 0; i < CandidateCount; ++i)
	{
		const float Angle = FMath::FRandRange(0.f, 2.f * (float)PI);
		const float Dist = FMath::Sqrt(FMath::FRandRange(0.f, 1.f)) * Radius;
		const FVector Candidate = Location + FVector((double)(FMath::Cos(Angle) * Dist), (double)(FMath::Sin(Angle) * Dist), 0.0);

		FHitResult Hit;
		const FVector TraceStart = Candidate + FVector(0, 0, 5000);
		const FVector TraceEnd = Candidate - FVector(0, 0, 5000);
		if (!World->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, ECC_WorldStatic, QueryParams))
		{
			++SkippedNoHit;
			continue;
		}

		const FVector Normal = Hit.ImpactNormal.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
		const float SlopeDeg = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp((float)FVector::DotProduct(Normal, FVector::UpVector), -1.f, 1.f)));
		if (SlopeDeg < SlopeRange.Min || SlopeDeg > SlopeRange.Max)
		{
			++SkippedSlope;
			continue;
		}

		if (MinDistanceSq > 0.f)
		{
			bool bTooClose = false;
			for (const FVector& Placed : PlacedLocations)
			{
				if (FVector::DistSquared2D(Placed, Hit.ImpactPoint) < MinDistanceSq) { bTooClose = true; break; }
			}
			if (bTooClose)
			{
				++SkippedTooClose;
				continue;
			}
		}

		FFoliageInstance Inst;
		Inst.Location = Hit.ImpactPoint;
		Inst.DrawScale3D = FoliageType->GetRandomScale();
		Inst.ZOffset = FoliageType->ZOffset.Interpolate(FMath::FRand());

		const float Yaw = bRandomYaw ? FMath::FRandRange(0.f, 360.f) : 0.f;
		const float Pitch = RandomPitch > 0.f ? FMath::FRandRange(-RandomPitch, RandomPitch) : 0.f;
		Inst.Rotation = FRotator((double)Pitch, (double)Yaw, 0.0);
		Inst.PreAlignRotation = Inst.Rotation;
		if (bAlignToNormal)
		{
			Inst.AlignToNormal(Normal, FoliageType->AlignMaxAngle);
		}

		// ZOffset is applied along the instance's local up (matches the Foliage Edit mode brush).
		if (!FMath::IsNearlyZero(Inst.ZOffset))
		{
			Inst.Location = Inst.GetInstanceWorldTransform().TransformPosition(FVector(0.0, 0.0, (double)Inst.ZOffset));
		}

		Inst.BaseComponent = Hit.GetComponent();
		FoliageInstances.Add(Inst);
		PlacedLocations.Add(Hit.ImpactPoint);
	}

	if (FoliageInstances.Num() > 0)
	{
		TArray<const FFoliageInstance*> InstancePtrs;
		InstancePtrs.Reserve(FoliageInstances.Num());
		for (const FFoliageInstance& Inst : FoliageInstances)
			InstancePtrs.Add(&Inst);
		FoliageInfo->AddInstances(FoliageTypePtr, InstancePtrs);
	}
	IFA->MarkPackageDirty();

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"foliage_type\":\"%s\",\"placed\":%d,\"instances_added\":%d,\"candidates\":%d,\"max_instances\":%d,")
		TEXT("\"skipped_no_hit\":%d,\"skipped_slope\":%d,\"skipped_too_close\":%d,")
		TEXT("\"rules\":{\"min_distance\":%.1f,\"slope_min\":%.1f,\"slope_max\":%.1f,\"align_to_normal\":%s,\"random_yaw\":%s,\"random_pitch\":%.1f},")
		TEXT("\"location\":[%.0f,%.0f,%.0f],\"radius\":%.0f}"),
		*FoliageTypePath, FoliageInstances.Num(), FoliageInstances.Num(), CandidateCount, MaxInstances,
		SkippedNoHit, SkippedSlope, SkippedTooClose,
		MinDistance, SlopeRange.Min, SlopeRange.Max,
		bAlignToNormal ? TEXT("true") : TEXT("false"), bRandomYaw ? TEXT("true") : TEXT("false"), RandomPitch,
		Location.X, Location.Y, Location.Z, Radius);
}

void HandleSetFoliageDensity(const FString& FoliageTypePath, float Density,
	float ScaleMin, float ScaleMax, FString& OutJsonString, FString& OutError)
{

	UFoliageType* FoliageType = Cast<UFoliageType>(UEditorAssetLibrary::LoadAsset(FoliageTypePath));
	if (!FoliageType) { OutError = TEXT("FoliageType not found: ") + FoliageTypePath; return; }

	if (FFloatProperty* DensityProp = FindFProperty<FFloatProperty>(FoliageType->GetClass(), TEXT("Density")))
	{
		DensityProp->SetPropertyValue_InContainer(FoliageType, Density);
	}

	FoliageType->Scaling = EFoliageScaling::Uniform;
	FoliageType->ScaleX = FFloatInterval(ScaleMin, ScaleMax);
	FoliageType->ScaleY = FFloatInterval(ScaleMin, ScaleMax);
	FoliageType->ScaleZ = FFloatInterval(ScaleMin, ScaleMax);

	FoliageType->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(FoliageTypePath, false);

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"foliage_type\":\"%s\",\"density\":%.2f,\"scale_min\":%.2f,\"scale_max\":%.2f}"),
		*FoliageTypePath, Density, ScaleMin, ScaleMax);
}

void HandleClearFoliage(const FString& FoliageTypePath, const FVector& Location,
	float Radius, FString& OutJsonString, FString& OutError)
{

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World) { OutError = TEXT("No editor world"); return; }

	UFoliageType* FoliageType = Cast<UFoliageType>(UEditorAssetLibrary::LoadAsset(FoliageTypePath));
	if (!FoliageType) { OutError = TEXT("FoliageType not found: ") + FoliageTypePath; return; }

	AInstancedFoliageActor* IFA = AInstancedFoliageActor::GetInstancedFoliageActorForCurrentLevel(World, false);
	if (!IFA) { OutJsonString = TEXT("{\"success\":true,\"removed\":0,\"message\":\"No InstancedFoliageActor in level.\"}"); return; }

	FFoliageInfo* FoliageInfo = IFA->FindInfo(FoliageType);
	if (!FoliageInfo) { OutJsonString = TEXT("{\"success\":true,\"removed\":0,\"message\":\"FoliageType not used in level.\"}"); return; }

	TArray<int32> ToRemove;
	double RadiusSq = (double)Radius * (double)Radius;
	for (int32 i = 0; i < FoliageInfo->Instances.Num(); ++i)
	{
		FVector Pos(FoliageInfo->Instances[i].Location);
		double DX = Pos.X - Location.X;
		double DY = Pos.Y - Location.Y;
		if ((DX*DX + DY*DY) <= RadiusSq)
		{
			ToRemove.Add(i);
		}
	}

	FoliageInfo->RemoveInstances(ToRemove, true);
	IFA->MarkPackageDirty();

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"foliage_type\":\"%s\",\"removed\":%d,\"radius\":%.0f}"),
		*FoliageTypePath, ToRemove.Num(), Radius);
}

void HandleSetFoliageTypeProperties(const FString& FoliageTypePath,
	float CullDistanceMin, float CullDistanceMax,
	float ScaleMin, float ScaleMax,
	const FString& CollisionProfile,
	FString& OutJsonString, FString& OutError)
{

	UFoliageType* FT = Cast<UFoliageType>(UEditorAssetLibrary::LoadAsset(FoliageTypePath));
	if (!FT) { OutError = FString::Printf(TEXT("Could not load FoliageType at '%s'"), *FoliageTypePath); return; }

	FT->Modify();
	TArray<FString> Changed;

	if (CullDistanceMin >= 0.f || CullDistanceMax >= 0.f)
	{
		if (FStructProperty* SP = FindFProperty<FStructProperty>(FT->GetClass(), TEXT("CullDistance")))
		{
			void* Ptr = SP->ContainerPtrToValuePtr<void>(FT);
			if (CullDistanceMin >= 0.f)
				if (FIntProperty* MP = FindFProperty<FIntProperty>(SP->Struct, TEXT("Min")))
					MP->SetPropertyValue_InContainer(Ptr, (int32)CullDistanceMin);
			if (CullDistanceMax >= 0.f)
				if (FIntProperty* MP = FindFProperty<FIntProperty>(SP->Struct, TEXT("Max")))
					MP->SetPropertyValue_InContainer(Ptr, (int32)CullDistanceMax);
			Changed.Add(TEXT("cull_distance"));
		}
	}

	if (ScaleMin >= 0.f || ScaleMax >= 0.f)
	{
		auto SetInterval = [&](const TCHAR* PropName)
		{
			if (FStructProperty* SP = FindFProperty<FStructProperty>(FT->GetClass(), PropName))
			{
				void* Ptr = SP->ContainerPtrToValuePtr<void>(FT);
				if (ScaleMin >= 0.f)
					if (FFloatProperty* FP = FindFProperty<FFloatProperty>(SP->Struct, TEXT("Min")))
						FP->SetPropertyValue_InContainer(Ptr, ScaleMin);
				if (ScaleMax >= 0.f)
					if (FFloatProperty* FP = FindFProperty<FFloatProperty>(SP->Struct, TEXT("Max")))
						FP->SetPropertyValue_InContainer(Ptr, ScaleMax);
			}
		};
		SetInterval(TEXT("ScaleX")); SetInterval(TEXT("ScaleY")); SetInterval(TEXT("ScaleZ"));
		Changed.Add(TEXT("scale_range"));
	}

	if (!CollisionProfile.IsEmpty())
	{
		UFoliageType_InstancedStaticMesh* FTISM = Cast<UFoliageType_InstancedStaticMesh>(FT);
		if (FTISM)
		{
			FTISM->BodyInstance.SetCollisionProfileName(FName(*CollisionProfile));
			Changed.Add(TEXT("collision_profile"));
		}
		else
		{
			OutError = TEXT("collision_profile can only be set on UFoliageType_InstancedStaticMesh assets");
		}
	}

	FT->PostEditChange();
	FT->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(FoliageTypePath, false);

	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"asset_path\":\"%s\",\"changed\":\"%s\"}"),
		*FoliageTypePath, *FString::Join(Changed, TEXT(", ")));
}

void HandleAssignPhysicalMaterialToFoliage(const FString& FoliageTypePath,
	const FString& PhysMatPath,
	FString& OutJsonString, FString& OutError)
{

	UFoliageType_InstancedStaticMesh* FT = Cast<UFoliageType_InstancedStaticMesh>(UEditorAssetLibrary::LoadAsset(FoliageTypePath));
	if (!FT) { OutError = FString::Printf(TEXT("Could not load FoliageType_InstancedStaticMesh at '%s'"), *FoliageTypePath); return; }

	UPhysicalMaterial* PhysMat = Cast<UPhysicalMaterial>(UEditorAssetLibrary::LoadAsset(PhysMatPath));
	if (!PhysMat) { OutError = FString::Printf(TEXT("Could not load PhysicalMaterial at '%s'"), *PhysMatPath); return; }

	FT->Modify();
	FT->BodyInstance.SetPhysMaterialOverride(PhysMat);
	FT->PostEditChange();
	FT->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(FoliageTypePath, false);

	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"asset_path\":\"%s\",\"physical_material\":\"%s\"}"),
		*FoliageTypePath, *PhysMatPath);
}

void HandleGetFoliageSummary(FString& OutJsonString, FString& OutError)
{

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World) { OutError = TEXT("No editor world available"); return; }

	AInstancedFoliageActor* IFA = AInstancedFoliageActor::GetInstancedFoliageActorForCurrentLevel(World);
	if (!IFA) { OutError = TEXT("No InstancedFoliageActor found in current level (no foliage painted)"); return; }

	TSharedPtr<FJsonObject> Res = MakeShareable(new FJsonObject());
	Res->SetBoolField(TEXT("success"), true);

	TArray<TSharedPtr<FJsonValue>> TypesArray;
	int32 TotalInstances = 0;

	for (auto& Pair : IFA->GetFoliageInfos())
	{
		const UFoliageType* FoliageType = Pair.Key;
		const FFoliageInfo& Info = *Pair.Value;
		if (!FoliageType) continue;

		TSharedPtr<FJsonObject> TypeObj = MakeShareable(new FJsonObject());
		TypeObj->SetStringField(TEXT("foliage_type"), FoliageType->GetPathName());
		TypeObj->SetNumberField(TEXT("instance_count"), Info.Instances.Num());

		const UFoliageType_InstancedStaticMesh* ISMType = Cast<UFoliageType_InstancedStaticMesh>(FoliageType);
		if (ISMType && ISMType->Mesh)
		{
			TypeObj->SetStringField(TEXT("mesh_path"), ISMType->Mesh->GetPathName());
		}

		TypeObj->SetNumberField(TEXT("density_adjustment"), FoliageType->Density);
		TypeObj->SetNumberField(TEXT("scale_min"), FoliageType->ScaleX.Min);
		TypeObj->SetNumberField(TEXT("scale_max"), FoliageType->ScaleX.Max);
		TypeObj->SetNumberField(TEXT("cull_distance_min"), FoliageType->CullDistance.Min);
		TypeObj->SetNumberField(TEXT("cull_distance_max"), FoliageType->CullDistance.Max);

		TotalInstances += Info.Instances.Num();
		TypesArray.Add(MakeShareable(new FJsonValueObject(TypeObj)));
	}

	Res->SetArrayField(TEXT("foliage_types"), TypesArray);
	Res->SetNumberField(TEXT("type_count"), TypesArray.Num());
	Res->SetNumberField(TEXT("total_instances"), TotalInstances);

	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Res.ToSharedRef(), W);
}

void HandleCreateProceduralFoliageSpawner(const FString& Name, const FString& SavePath,
	const TArray<FString>& FoliageTypePaths, FString& OutJsonString, FString& OutError)
{

	if (Name.IsEmpty()) { OutError = TEXT("name is required"); return; }

	FString PackagePath = SavePath;
	while (PackagePath.EndsWith(TEXT("/"))) PackagePath = PackagePath.LeftChop(1);
	if (PackagePath.IsEmpty()) PackagePath = TEXT("/Game/Foliage");

	IAssetTools& AssetTools = FAssetToolsModule::GetModule().Get();
	UObject* NewAsset = AssetTools.CreateAsset(Name, PackagePath, UProceduralFoliageSpawner::StaticClass(), nullptr);
	UProceduralFoliageSpawner* Spawner = Cast<UProceduralFoliageSpawner>(NewAsset);
	if (!Spawner) { OutError = TEXT("Failed to create ProceduralFoliageSpawner asset"); return; }

	int32 TypesAdded = 0;
	for (const FString& TypePath : FoliageTypePaths)
	{
		if (TypePath.IsEmpty()) continue;
		UFoliageType* FoliageType = Cast<UFoliageType>(UEditorAssetLibrary::LoadAsset(TypePath));
		if (FoliageType)
		{
			FFoliageTypeObject FTO;
			if (FObjectProperty* ObjProp = FindFProperty<FObjectProperty>(FFoliageTypeObject::StaticStruct(), TEXT("FoliageTypeObject")))
				ObjProp->SetObjectPropertyValue(ObjProp->ContainerPtrToValuePtr<void>(&FTO), FoliageType);
			const_cast<TArray<FFoliageTypeObject>&>(Spawner->GetFoliageTypes()).Add(FTO);
			TypesAdded++;
		}
	}

	Spawner->MarkPackageDirty();
	FString FullPath = PackagePath + TEXT("/") + Name;
	UEditorAssetLibrary::SaveAsset(FullPath, false);

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"asset_path\":\"%s\",\"foliage_types_added\":%d}"),
		*FullPath, TypesAdded);
}

void HandleSpawnProceduralFoliageVolume(const FString& ActorLabel, const FString& SpawnerPath,
	float LocationX, float LocationY, float LocationZ,
	float ExtentX, float ExtentY, float ExtentZ,
	FString& OutJsonString, FString& OutError)
{

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World) { OutError = TEXT("No editor world available"); return; }

	UProceduralFoliageSpawner* Spawner = nullptr;
	if (!SpawnerPath.IsEmpty())
	{
		Spawner = Cast<UProceduralFoliageSpawner>(UEditorAssetLibrary::LoadAsset(SpawnerPath));
		if (!Spawner) { OutError = FString::Printf(TEXT("Could not load ProceduralFoliageSpawner at '%s'"), *SpawnerPath); return; }
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	FVector Location(LocationX, LocationY, LocationZ);

	AProceduralFoliageVolume* Volume = World->SpawnActor<AProceduralFoliageVolume>(Location, FRotator::ZeroRotator, SpawnParams);
	if (!Volume) { OutError = TEXT("Failed to spawn AProceduralFoliageVolume"); return; }

	FString Label = ActorLabel.IsEmpty() ? TEXT("ProceduralFoliageVolume") : ActorLabel;
	Volume->SetActorLabel(Label);

	constexpr float DefaultExtent = 200.f;
	float SX = ExtentX > 0.f ? ExtentX / DefaultExtent : 1.f;
	float SY = ExtentY > 0.f ? ExtentY / DefaultExtent : 1.f;
	float SZ = ExtentZ > 0.f ? ExtentZ / DefaultExtent : 1.f;
	if (ExtentX > 0.f || ExtentY > 0.f || ExtentZ > 0.f)
	{
		Volume->SetActorScale3D(FVector(SX, SY, SZ));
	}

	if (Spawner && Volume->ProceduralComponent)
	{
		Volume->ProceduralComponent->FoliageSpawner = Spawner;
	}

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"actor_label\":\"%s\",\"spawner_path\":\"%s\",\"location\":[%.1f,%.1f,%.1f],\"scale\":[%.3f,%.3f,%.3f]}"),
		*Volume->GetActorLabel(), *SpawnerPath, LocationX, LocationY, LocationZ, SX, SY, SZ);
}

void HandleAddFoliageTypeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("types"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			FString MeshPath, SavePath = TEXT("/Game/Foliage");
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (Item.IsValid())
			{
				MeshPath = BatchToolHelper::GetItemString(Item, TEXT("static_mesh_path"), TEXT("mesh_path"));
				FString SP = BatchToolHelper::GetItemString(Item, TEXT("save_path"));
				if (!SP.IsEmpty()) SavePath = SP;
			}
			else
			{
				(*ItemsArray)[i]->TryGetString(MeshPath);
			}
			if (MeshPath.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing static_mesh_path")); continue; }
			FString ItemOut, ItemErr;
			HandleAddFoliageType(MeshPath, SavePath, ItemOut, ItemErr);
			if (ItemErr.IsEmpty())
			{
				auto Extra = MakeShared<FJsonObject>(); Extra->SetStringField(TEXT("mesh_path"), MeshPath);
				Batch.AddSuccess(i, Extra);
			}
			else Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJsonString);
		return;
	}
	FString MeshPath, SavePath = TEXT("/Game/Foliage");
	Args->TryGetStringField(TEXT("static_mesh_path"), MeshPath);
	Args->TryGetStringField(TEXT("save_path"), SavePath);
	HandleAddFoliageType(MeshPath, SavePath, OutJsonString, OutError);
}

void HandlePaintFoliageFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString FoliageTypePath;
	if (!Args->TryGetStringField(TEXT("foliage_type_path"), FoliageTypePath))
	{
		OutError = TEXT("foliage_type_path required");
		return;
	}
	double LocX = 0, LocY = 0, LocZ = 0, Radius = 1000.0, Density = 0.01, MaxInstances = 2000.0;
	Args->TryGetNumberField(TEXT("location_x"), LocX);
	Args->TryGetNumberField(TEXT("location_y"), LocY);
	Args->TryGetNumberField(TEXT("location_z"), LocZ);
	Args->TryGetNumberField(TEXT("radius"), Radius);
	Args->TryGetNumberField(TEXT("density"), Density);
	Args->TryGetNumberField(TEXT("max_instances"), MaxInstances);
	HandlePaintFoliage(FoliageTypePath, FVector((float)LocX, (float)LocY, (float)LocZ),
		(float)Radius, (float)Density, (int32)MaxInstances, OutJsonString, OutError);
}

void HandleSetFoliageDensityFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString FoliageTypePath;
	if (!Args->TryGetStringField(TEXT("foliage_type_path"), FoliageTypePath))
	{
		OutError = TEXT("foliage_type_path required");
		return;
	}
	double Density = 0.01, ScaleMin = 0.8, ScaleMax = 1.2;
	Args->TryGetNumberField(TEXT("density"), Density);
	Args->TryGetNumberField(TEXT("scale_min"), ScaleMin);
	Args->TryGetNumberField(TEXT("scale_max"), ScaleMax);
	HandleSetFoliageDensity(FoliageTypePath, (float)Density, (float)ScaleMin, (float)ScaleMax, OutJsonString, OutError);
}

void HandleClearFoliageFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString FoliageTypePath;
	if (!Args->TryGetStringField(TEXT("foliage_type_path"), FoliageTypePath))
	{
		OutError = TEXT("foliage_type_path required");
		return;
	}
	double LocX = 0, LocY = 0, LocZ = 0, Radius = 1000.0;
	Args->TryGetNumberField(TEXT("location_x"), LocX);
	Args->TryGetNumberField(TEXT("location_y"), LocY);
	Args->TryGetNumberField(TEXT("location_z"), LocZ);
	Args->TryGetNumberField(TEXT("radius"), Radius);
	HandleClearFoliage(FoliageTypePath, FVector((float)LocX, (float)LocY, (float)LocZ),
		(float)Radius, OutJsonString, OutError);
}

void HandleSetFoliageTypePropertiesFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString Path, Profile;
	float CullMin = -1.f, CullMax = -1.f, ScaleMin = -1.f, ScaleMax = -1.f;
	Args->TryGetStringField(TEXT("foliage_type_path"), Path);
	Args->TryGetStringField(TEXT("collision_profile"), Profile);
	double V = 0;
	if (Args->TryGetNumberField(TEXT("cull_distance_min"), V)) CullMin = (float)V;
	if (Args->TryGetNumberField(TEXT("cull_distance_max"), V)) CullMax = (float)V;
	if (Args->TryGetNumberField(TEXT("scale_min"), V)) ScaleMin = (float)V;
	if (Args->TryGetNumberField(TEXT("scale_max"), V)) ScaleMax = (float)V;
	HandleSetFoliageTypeProperties(Path, CullMin, CullMax, ScaleMin, ScaleMax, Profile, OutJsonString, OutError);
}

void HandleAssignPhysicalMaterialToFoliageFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString Path, PhysMatPath;
	Args->TryGetStringField(TEXT("foliage_type_path"), Path);
	Args->TryGetStringField(TEXT("physical_material_path"), PhysMatPath);
	HandleAssignPhysicalMaterialToFoliage(Path, PhysMatPath, OutJsonString, OutError);
}

void HandleGetFoliageSummaryFromArgs(const TSharedPtr<FJsonObject>& ,
	FString& OutJsonString, FString& OutError)
{
	HandleGetFoliageSummary(OutJsonString, OutError);
}

void HandleCreateProceduralFoliageSpawnerFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString Name, SavePath;
	Args->TryGetStringField(TEXT("name"), Name);
	Args->TryGetStringField(TEXT("save_path"), SavePath);
	if (SavePath.IsEmpty()) SavePath = FSettingsManager::GetDefaultSavePath();
	TArray<FString> TypePaths;
	const TArray<TSharedPtr<FJsonValue>>* TypeArr = nullptr;
	if (Args->TryGetArrayField(TEXT("foliage_type_paths"), TypeArr))
	{
		for (const TSharedPtr<FJsonValue>& V : *TypeArr)
		{ FString S; if (V->TryGetString(S)) TypePaths.Add(S); }
	}
	HandleCreateProceduralFoliageSpawner(Name, SavePath, TypePaths, OutJsonString, OutError);
}

void HandleSpawnProceduralFoliageVolumeFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString Label, SpawnerPath;
	double LX = 0, LY = 0, LZ = 0, EX = 0, EY = 0, EZ = 0;
	Args->TryGetStringField(TEXT("actor_label"), Label);
	Args->TryGetStringField(TEXT("spawner_path"), SpawnerPath);
	Args->TryGetNumberField(TEXT("location_x"), LX);
	Args->TryGetNumberField(TEXT("location_y"), LY);
	Args->TryGetNumberField(TEXT("location_z"), LZ);
	Args->TryGetNumberField(TEXT("extent_x"), EX);
	Args->TryGetNumberField(TEXT("extent_y"), EY);
	Args->TryGetNumberField(TEXT("extent_z"), EZ);
	HandleSpawnProceduralFoliageVolume(Label, SpawnerPath, (float)LX, (float)LY, (float)LZ,
		(float)EX, (float)EY, (float)EZ, OutJsonString, OutError);
}

}
