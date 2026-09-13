// Copyright 2026, BlueprintsLab, All rights reserved

#include "Tools/LevelActorTools.h"
#include "Utils/EditorRuntime.h"
#include "Managers/CapabilityProfile.h"
#include "Managers/SettingsManager.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/EngineVersionComparison.h"
#include "Editor.h"
#include "EditorAssetLibrary.h"
#include "Subsystems/EditorActorSubsystem.h"
#include "Engine/StaticMeshActor.h"
#include "GameFramework/Actor.h"
#include "Components/PrimitiveComponent.h"
#include "Materials/MaterialInterface.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "EngineUtils.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "Engine/Blueprint.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"
#include "Components/MeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/LightComponent.h"
#include "Components/LocalLightComponent.h"
#include "Components/RectLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/GameModeBase.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "WorldPartition/DataLayer/DataLayerInstance.h"
#include "DataLayer/DataLayerEditorSubsystem.h"
#include "WorldPartition/DataLayer/DataLayerManager.h"
#include "Editor/EditorEngine.h"
#include "CineCameraActor.h"
#include "CineCameraComponent.h"
#include "Tools/BatchToolHelper.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/HLOD/HLODLayer.h"
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"
#include "Selection.h"
#include "LevelEditorSubsystem.h"
#include "FileHelpers.h"
#include "ScopedTransaction.h"
#include "Engine/StaticMesh.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "CollisionQueryParams.h"

namespace LevelActorTools
{
static constexpr int32 GActorListDefaultMax    = 200;
static constexpr int32 GActorListLocationCap   = 50;

// JSON transform parsers (defined further down, next to the *FromArgs handlers).
static FVector  ExtractVector(const TSharedPtr<FJsonObject>& Obj, const TCHAR* FieldName, const FVector& Default = FVector::ZeroVector);
static FRotator ExtractRotator(const TSharedPtr<FJsonObject>& Obj, const TCHAR* FieldName, const FRotator& Default = FRotator::ZeroRotator);

// One label -> actor index per call, so batch handlers do not rescan the world per item.
static void BuildActorLabelIndex(UWorld* World, TMap<FString, AActor*>& OutIndex)
{
	if (!World) return;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (AActor* Actor = *It)
		{
			OutIndex.FindOrAdd(Actor->GetActorLabel()) = Actor;
		}
	}
}

// Resolves a basic-shape name or a StaticMesh asset path to a UStaticMesh (used by spawn_mode=ism).
static UStaticMesh* ResolveStaticMeshForSpawn(const FString& ActorClassOrMesh)
{
	static const TMap<FString, FString> BasicShapeNames = {
		{ TEXT("cube"),     TEXT("Cube") },
		{ TEXT("sphere"),   TEXT("Sphere") },
		{ TEXT("cylinder"), TEXT("Cylinder") },
		{ TEXT("cone"),     TEXT("Cone") },
		{ TEXT("plane"),    TEXT("Plane") },
	};
	if (const FString* Canonical = BasicShapeNames.Find(ActorClassOrMesh.ToLower()))
	{
		const FString MeshPath = FString::Printf(TEXT("/Engine/BasicShapes/%s.%s"), **Canonical, **Canonical);
		return LoadObject<UStaticMesh>(nullptr, *MeshPath);
	}
	if (ActorClassOrMesh.StartsWith(TEXT("/")))
	{
		return Cast<UStaticMesh>(LoadObject<UObject>(nullptr, *ActorClassOrMesh));
	}
	return nullptr;
}

void HandleGetAllSceneActors(const FString& ClassFilter, int32 MaxResults, bool bIncludeLocation,
	FString& OutJsonString, FString& OutError)
{
	UEditorActorSubsystem* EditorActorSubsystem = GEditor->GetEditorSubsystem<UEditorActorSubsystem>();
	TArray<AActor*> AllActors;
	if (EditorActorSubsystem)
		AllActors = EditorActorSubsystem->GetAllLevelActors();

	const int32 EffectiveMax = (MaxResults <= 0) ? GActorListDefaultMax : MaxResults;

	int32 TotalMatched = 0;
	TArray<TSharedPtr<FJsonValue>> ActorJsonArray;
	for (AActor* Actor : AllActors)
	{
		if (!Actor) continue;
		if (!ClassFilter.IsEmpty() && !Actor->GetClass()->GetName().Contains(ClassFilter, ESearchCase::IgnoreCase))
			continue;

		++TotalMatched;
		if (ActorJsonArray.Num() >= EffectiveMax) continue;

		TSharedPtr<FJsonObject> ActorObject = MakeShareable(new FJsonObject());
		ActorObject->SetStringField(TEXT("label"), Actor->GetActorLabel());
		ActorObject->SetStringField(TEXT("class"), Actor->GetClass()->GetName());

		if (bIncludeLocation)
		{
			FVector Location = Actor->GetActorLocation();
			TArray<TSharedPtr<FJsonValue>> LocationArray;
			LocationArray.Add(MakeShareable(new FJsonValueNumber(Location.X)));
			LocationArray.Add(MakeShareable(new FJsonValueNumber(Location.Y)));
			LocationArray.Add(MakeShareable(new FJsonValueNumber(Location.Z)));
			ActorObject->SetArrayField(TEXT("location"), LocationArray);
		}

		ActorJsonArray.Add(MakeShareable(new FJsonValueObject(ActorObject)));
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetArrayField(TEXT("actors"), ActorJsonArray);
	Result->SetNumberField(TEXT("count"), ActorJsonArray.Num());
	Result->SetNumberField(TEXT("total"), TotalMatched);
	if (!ClassFilter.IsEmpty()) Result->SetStringField(TEXT("filter_class"), ClassFilter);
	if (TotalMatched > ActorJsonArray.Num())
	{
		Result->SetBoolField(TEXT("truncated"), true);
		Result->SetStringField(TEXT("hint"), ClassFilter.IsEmpty()
			? FString::Printf(TEXT("%d actors in level — only the first %d returned. Narrow with class_filter (substring) OR find_actors_by_bounds to limit by region. Pass max_results=N to raise the cap."), TotalMatched, ActorJsonArray.Num())
			: FString::Printf(TEXT("%d matches — only the first %d returned. Pass max_results=N to raise the cap."), TotalMatched, ActorJsonArray.Num()));
	}
	if (!bIncludeLocation && TotalMatched > GActorListLocationCap)
	{
		Result->SetStringField(TEXT("location_omitted"),
			TEXT("location stripped from rows (bulk listing). Pass include_location=true to keep coordinates."));
	}
	TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
}

void HandleGetAllSceneActors(const FString& ClassFilter, FString& OutJsonString, FString& OutError)
{
	HandleGetAllSceneActors(ClassFilter, 0, false, OutJsonString, OutError);
}
void HandleSetMultipleActorMaterials(const TArray<FString>& ActorLabels, const FString& MaterialPath, FString& OutError)

{

	if (ActorLabels.Num() == 0)

	{

		OutError = "No actor labels were provided for the bulk material operation.";

		return;

	}

	int32 FailCount = 0;

	for (const FString& Label : ActorLabels)

	{

		FString SingleError;

		HandleSetActorMaterial(Label, MaterialPath, SingleError);

		if (!SingleError.IsEmpty())

		{

			FailCount++;

		}

	}

	if (FailCount > 0)

	{

		OutError = FString::Printf(TEXT("Finished bulk operation, but failed to set material for %d out of %d actors. See Output Log for details."), FailCount, ActorLabels.Num());

	}

}
void HandleSetActorMaterial(const FString& ActorLabel, const FString& MaterialPath, FString& OutError, int32 SlotIndex)
{
	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		OutError = "Failed to get a valid editor world.";
		return;
	}
	AActor* TargetActor = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (*It && (*It)->GetActorLabel() == ActorLabel)
		{
			TargetActor = *It;
			break;
		}
	}
	if (!TargetActor)
	{
		OutError = FString::Printf(TEXT("Could not find an actor with the label '%s' in the level."), *ActorLabel);
		return;
	}
	UMaterialInterface* MaterialToApply = LoadObject<UMaterialInterface>(nullptr, *MaterialPath);
	if (!MaterialToApply)
	{
		OutError = FString::Printf(TEXT("Failed to load material at path '%s'."), *MaterialPath);
		return;
	}
	TArray<UPrimitiveComponent*> PrimitiveComponents;
	TargetActor->GetComponents<UPrimitiveComponent>(PrimitiveComponents);
	if (PrimitiveComponents.Num() == 0)
	{
		OutError = FString::Printf(TEXT("No primitive components (like Static Mesh or Skeletal Mesh) found on actor '%s' to apply a material to."), *ActorLabel);
		return;
	}
	for (UPrimitiveComponent* Comp : PrimitiveComponents)
	{
		if (SlotIndex >= 0)
		{
			if (SlotIndex < Comp->GetNumMaterials())
				Comp->SetMaterial(SlotIndex, MaterialToApply);
		}
		else
		{
			for (int32 i = 0; i < Comp->GetNumMaterials(); ++i)
				Comp->SetMaterial(i, MaterialToApply);
		}
	}
}
void HandleSetSelectedActors(const TArray<FString>& ActorLabels, FString& OutError)
{
	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		OutError = TEXT("Failed to get a valid editor world.");
		return;
	}
	TArray<AActor*> ActorsToSelect;
	for (const FString& Label : ActorLabels)
	{
		bool bFoundActor = false;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (*It && (*It)->GetActorLabel() == Label)
			{
				ActorsToSelect.Add(*It);
				bFoundActor = true;
				break;
			}
		}
	}
	if (ActorsToSelect.Num() > 0)
	{
		UEditorActorSubsystem* EditorActorSubsystem = GEditor->GetEditorSubsystem<UEditorActorSubsystem>();
		if (EditorActorSubsystem)
		{
			EditorActorSubsystem->SetSelectedLevelActors(ActorsToSelect);
		}
	}
	else if (ActorLabels.Num() > 0)
	{
		UEditorActorSubsystem* EditorActorSubsystem = GEditor->GetEditorSubsystem<UEditorActorSubsystem>();
		if (EditorActorSubsystem)
		{
			EditorActorSubsystem->SetSelectedLevelActors({});
		}
	}
}

static AActor* SpawnActorInLevelInternal(const FString& ActorClass, const FVector& Location, const FRotator& Rotation, const FVector& Scale, const FString& ActorLabel, FString& OutError)

{

	UWorld* World = GEditor->GetEditorWorldContext().World();

	if (!World)

	{

		OutError = TEXT("Failed to get a valid editor world to spawn actor in.");

		return nullptr;

	}

	UEditorActorSubsystem* EditorActorSubsystem = GEditor->GetEditorSubsystem<UEditorActorSubsystem>();

	if (!EditorActorSubsystem)

	{

		OutError = TEXT("Could not get the Editor Actor Subsystem.");

		return nullptr;

	}

	AActor* SpawnedActor = nullptr;

	static const TMap<FString, FString> BasicShapes = {
		{ TEXT("cube"),     TEXT("Cube") },
		{ TEXT("sphere"),   TEXT("Sphere") },
		{ TEXT("cylinder"), TEXT("Cylinder") },
		{ TEXT("cone"),     TEXT("Cone") },
		{ TEXT("plane"),    TEXT("Plane") },
	};

	if (const FString* CanonicalShape = BasicShapes.Find(ActorClass.ToLower()))

	{

		FString MeshPath = FString::Printf(TEXT("/Engine/BasicShapes/%s.%s"), **CanonicalShape, **CanonicalShape);

		UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *MeshPath);

		if (!Mesh)

		{

			OutError = FString::Printf(TEXT("Failed to load basic shape mesh: %s"), *MeshPath);

			return nullptr;

		}

		AStaticMeshActor* MeshActor = Cast<AStaticMeshActor>(EditorActorSubsystem->SpawnActorFromClass(AStaticMeshActor::StaticClass(), Location, Rotation));

		if (MeshActor)

		{

			MeshActor->GetStaticMeshComponent()->SetStaticMesh(Mesh);

			SpawnedActor = MeshActor;

		}

	}

	else if (ActorClass.StartsWith(TEXT("/")))

	{

		UObject* LoadedObject = LoadObject<UObject>(nullptr, *ActorClass);

		if (!LoadedObject)
		{
			FString FallbackName;
			int32 LastSlash;
			if (ActorClass.FindLastChar('/', LastSlash))
				FallbackName = ActorClass.RightChop(LastSlash + 1);
			else
				FallbackName = ActorClass;
			int32 DotIdx;
			if (FallbackName.FindChar('.', DotIdx)) FallbackName = FallbackName.Mid(DotIdx + 1);
			if (!FallbackName.IsEmpty())
			{
				const FString TargetA = FallbackName;
				const FString TargetB = FallbackName.StartsWith(TEXT("A")) ? FallbackName.Mid(1) : (TEXT("A") + FallbackName);
				for (TObjectIterator<UClass> It; It; ++It)
				{
					if (!It->IsChildOf(AActor::StaticClass()) || It->HasAnyClassFlags(CLASS_Abstract)) continue;
					const FString N = It->GetName();
					if (N.Equals(TargetA, ESearchCase::IgnoreCase) || N.Equals(TargetB, ESearchCase::IgnoreCase))
					{
						LoadedObject = *It;
						break;
					}
				}
			}
		}

		if (!LoadedObject)

		{

			OutError = FString::Printf(TEXT("Failed to load any asset at path: %s. For native classes use /Script/<Module>.<ClassName> form (e.g. /Script/CinematicCamera.CineCameraActor) or just the class name (CineCameraActor)."), *ActorClass);

			return nullptr;

		}

		if (UBlueprint* LoadedBP = Cast<UBlueprint>(LoadedObject))

		{

			if (UClass* GeneratedClass = LoadedBP->GeneratedClass)

			{

				if (GeneratedClass->IsChildOf(AActor::StaticClass()))

				{

					SpawnedActor = EditorActorSubsystem->SpawnActorFromClass(GeneratedClass, Location, Rotation);

				}

				else

				{

					OutError = FString::Printf(TEXT("Blueprint at path '%s' is not a spawnable Actor class."), *ActorClass);

					return nullptr;

				}

			}

			else

			{

				OutError = FString::Printf(TEXT("Blueprint at path '%s' has no valid GeneratedClass."), *ActorClass);

				return nullptr;

			}

		}

		else if (UStaticMesh* LoadedMesh = Cast<UStaticMesh>(LoadedObject))

		{

			AStaticMeshActor* MeshActor = Cast<AStaticMeshActor>(EditorActorSubsystem->SpawnActorFromClass(AStaticMeshActor::StaticClass(), Location, Rotation));

			if (MeshActor && MeshActor->GetStaticMeshComponent())

			{

				MeshActor->GetStaticMeshComponent()->SetStaticMesh(LoadedMesh);

				SpawnedActor = MeshActor;

			}

			else

			{

				OutError = FString::Printf(TEXT("Failed to spawn a StaticMeshActor for mesh '%s'."), *ActorClass);

				return nullptr;

			}

		}

		else if (UClass* LoadedClass = Cast<UClass>(LoadedObject))

		{

			if (LoadedClass->IsChildOf(AActor::StaticClass()) && !LoadedClass->HasAnyClassFlags(CLASS_Abstract))

			{

				SpawnedActor = EditorActorSubsystem->SpawnActorFromClass(LoadedClass, Location, Rotation);

			}

			else

			{

				OutError = FString::Printf(TEXT("Class at path '%s' is not a spawnable concrete AActor subclass."), *ActorClass);

				return nullptr;

			}

		}

		else

		{

			OutError = FString::Printf(TEXT("Asset at path '%s' is not a spawnable Blueprint or a Static Mesh."), *ActorClass);

			return nullptr;

		}

	}

	else

	{

		UClass* ClassToSpawn = FindFirstObjectSafe<UClass>(*ActorClass);

		if (!ClassToSpawn)

		{

			ClassToSpawn = FindFirstObjectSafe<UClass>(*("A" + ActorClass));

		}

		if (ClassToSpawn && ClassToSpawn->IsChildOf(AActor::StaticClass()))

		{

			SpawnedActor = EditorActorSubsystem->SpawnActorFromClass(ClassToSpawn, Location, Rotation);

		}

		else

		{

			OutError = FString::Printf(TEXT("Could not find a spawnable C++ class or Blueprint named '%s'."), *ActorClass);

			return nullptr;

		}

	}

	if (SpawnedActor)

	{

		SpawnedActor->SetActorScale3D(Scale);

		if (!ActorLabel.IsEmpty())

		{

			SpawnedActor->SetActorLabel(ActorLabel);

		}

		EditorActorSubsystem->SetSelectedLevelActors({ SpawnedActor });

	}

	else

	{

		OutError = FString::Printf(TEXT("Failed to spawn actor of class '%s'. The class may not be spawnable or an unknown error occurred."), *ActorClass);

	}

	return SpawnedActor;
}

void HandleSpawnActorInLevel(const FString& ActorClass, const FVector& Location, const FRotator& Rotation, const FVector& Scale, const FString& ActorLabel, FString& OutError)
{
	SpawnActorInLevelInternal(ActorClass, Location, Rotation, Scale, ActorLabel, OutError);
}

void HandleSpawnMultipleActorsInLevel(const TArray<FSpawnRequest>& SpawnRequests, FString& OutError)

{

	if (SpawnRequests.Num() == 0)

	{

		OutError = TEXT("No spawn requests provided in the batch.");

		return;

	}

	TArray<FString> PerActorErrors;

	for (const FSpawnRequest& Request : SpawnRequests)

	{

		FString SingleSpawnError;

		HandleSpawnActorInLevel(Request.ActorClass, Request.Location, Request.Rotation, Request.Scale, Request.ActorLabel, SingleSpawnError);

		if (!SingleSpawnError.IsEmpty())

		{

			FString ErrMsg = FString::Printf(TEXT("'%s' (%s): %s"), *Request.ActorLabel, *Request.ActorClass, *SingleSpawnError);
			PerActorErrors.Add(ErrMsg);

		}

	}

	if (PerActorErrors.Num() > 0)
	{
		OutError = FString::Printf(TEXT("%d/%d spawns failed: %s"), PerActorErrors.Num(), SpawnRequests.Num(), *FString::Join(PerActorErrors, TEXT("; ")));
	}

}

void HandleAdvancedSpawnEx(
	int32 Count, const FString& ActorClass, const FString& BoundingActorLabel,
	const FVector& LocMin, const FVector& LocMax,
	const FVector& ScaleMin, const FVector& ScaleMax,
	const FRotator& RotMin, const FRotator& RotMax,
	const FAdvancedSpawnOptions& Options,
	FString& OutJsonString, FString& OutError)
{
	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		OutError = TEXT("Failed to get a valid editor world.");
		return;
	}

	UEditorActorSubsystem* EditorActorSubsystem = GEditor->GetEditorSubsystem<UEditorActorSubsystem>();
	if (!EditorActorSubsystem)
	{
		OutError = TEXT("Could not get the Editor Actor Subsystem.");
		return;
	}

	if (Count <= 0) { OutError = TEXT("count must be > 0"); return; }
	if (ActorClass.IsEmpty()) { OutError = TEXT("actor_class is required"); return; }

	FBox BoundingBox(ForceInit);
	const FString SelectedActorMagicString = TEXT("_SELECTED_");
	if (!BoundingActorLabel.IsEmpty())
	{
		AActor* BoundingActor = nullptr;
		if (BoundingActorLabel.Equals(SelectedActorMagicString))
		{
			TArray<AActor*> SelectedActors = EditorActorSubsystem->GetSelectedLevelActors();
			if (SelectedActors.Num() != 1)
			{
				OutError = FString::Printf(TEXT("To spawn within a selected volume, you must select exactly one actor in the level. You have %d selected."), SelectedActors.Num());
				return;
			}
			BoundingActor = SelectedActors[0];
		}
		else
		{
			for (TActorIterator<AActor> It(World); It; ++It)
			{
				if (*It && (*It)->GetActorLabel() == BoundingActorLabel)
				{
					BoundingActor = *It;
					break;
				}
			}
			if (!BoundingActor)
			{
				OutError = FString::Printf(TEXT("Could not find any actor in the level with the label '%s'."), *BoundingActorLabel);
				return;
			}
		}

		FVector Origin, Extent;
		BoundingActor->GetActorBounds(false, Origin, Extent);
		BoundingBox = FBox(Origin - Extent, Origin + Extent);
	}
	else
	{
		BoundingBox = FBox(LocMin, LocMax);
	}

	TArray<FString> Warnings;

	// Rotation policy: yaw-only unless the caller both supplied pitch/roll ranges AND set allow_tilt.
	const bool bRequestedTilt =
		!FMath::IsNearlyZero(RotMin.Pitch) || !FMath::IsNearlyZero(RotMax.Pitch) ||
		!FMath::IsNearlyZero(RotMin.Roll)  || !FMath::IsNearlyZero(RotMax.Roll);
	const bool bUseTilt = bRequestedTilt && Options.bAllowTilt;
	if (bRequestedTilt && !Options.bAllowTilt)
	{
		Warnings.Add(TEXT("random_rotation_min/max contain pitch/roll but allow_tilt is not true; pitch/roll forced to 0 (yaw-only). Pass allow_tilt:true to randomise tilt."));
	}
	if (Options.bAlignToNormal && !Options.bAlignToGround)
	{
		Warnings.Add(TEXT("align_to_normal requires align_to_ground:true; ignored."));
	}
	const bool bAlignToNormal = Options.bAlignToNormal && Options.bAlignToGround;

	// Sampling phase: all transforms are computed before anything is spawned so new actors never block later traces.
	const FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(AxivorSpawnAdvanced), false);
	const double TraceTop    = BoundingBox.Max.Z + 10000.0;
	const double TraceBottom = BoundingBox.Min.Z - 10000.0;

	TArray<FTransform> Transforms;
	Transforms.Reserve(Count);
	int32 SkippedNoGround = 0;
	for (int32 i = 0; i < Count; ++i)
	{
		FVector Location = FMath::RandPointInBox(BoundingBox);
		FVector GroundNormal = FVector::UpVector;

		if (Options.bAlignToGround)
		{
			FHitResult Hit;
			const FVector TraceStart(Location.X, Location.Y, TraceTop);
			const FVector TraceEnd(Location.X, Location.Y, TraceBottom);
			if (!World->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, ECC_WorldStatic, TraceParams))
			{
				++SkippedNoGround;
				continue;
			}
			Location.Z = Hit.ImpactPoint.Z;
			GroundNormal = Hit.ImpactNormal;
		}

		FVector Scale;
		if (Options.bUniformScale)
		{
			const double Uniform = FMath::RandRange(ScaleMin.X, ScaleMax.X);
			Scale = FVector(Uniform, Uniform, Uniform);
		}
		else
		{
			Scale = FVector(
				FMath::RandRange(ScaleMin.X, ScaleMax.X),
				FMath::RandRange(ScaleMin.Y, ScaleMax.Y),
				FMath::RandRange(ScaleMin.Z, ScaleMax.Z));
		}

		// FRotator(Pitch, Yaw, Roll)
		const double Pitch = bUseTilt ? FMath::RandRange(RotMin.Pitch, RotMax.Pitch) : 0.0;
		const double Yaw   = FMath::RandRange(RotMin.Yaw, RotMax.Yaw);
		const double Roll  = bUseTilt ? FMath::RandRange(RotMin.Roll, RotMax.Roll) : 0.0;
		FRotator Rotation(Pitch, Yaw, Roll);
		if (bAlignToNormal)
		{
			// Surface frame first (up = hit normal), then the random yaw/tilt applied in that local frame.
			const FQuat SurfaceQuat = FRotationMatrix::MakeFromZ(GroundNormal).ToQuat();
			Rotation = (SurfaceQuat * FQuat(Rotation)).Rotator();
		}

		Transforms.Emplace(Rotation, Location, Scale);
	}

	if (Transforms.Num() == 0)
	{
		OutError = FString::Printf(TEXT("No samples could be placed: %d/%d skipped because no ground (ECC_WorldStatic) was hit below the bounds. Pass align_to_ground:false to spawn floating."), SkippedNoGround, Count);
		return;
	}

	// Short display name for labels ("/Game/Meshes/SM_Rock.SM_Rock" -> "SM_Rock").
	FString BaseLabel = Options.Label;
	if (BaseLabel.IsEmpty())
	{
		BaseLabel = ActorClass;
		int32 SlashIdx = INDEX_NONE;
		if (BaseLabel.FindLastChar(TEXT('/'), SlashIdx)) BaseLabel = BaseLabel.RightChop(SlashIdx + 1);
		int32 DotIdx = INDEX_NONE;
		if (BaseLabel.FindChar(TEXT('.'), DotIdx)) BaseLabel = BaseLabel.Left(DotIdx);
		if (BaseLabel.IsEmpty()) BaseLabel = TEXT("Spawned");
	}

	const FString SpawnMode = Options.SpawnMode.IsEmpty() ? FString(TEXT("actors")) : Options.SpawnMode.ToLower();
	if (SpawnMode != TEXT("actors") && SpawnMode != TEXT("ism"))
	{
		OutError = FString::Printf(TEXT("Unknown spawn_mode '%s' (expected 'actors' or 'ism')."), *Options.SpawnMode);
		return;
	}

	const FScopedTransaction Transaction(NSLOCTEXT("AxivorAI", "SpawnAdvanced", "Spawn Advanced"));

	int32 Spawned = 0;
	int32 Instances = 0;
	FString HolderLabel;
	TArray<FString> SpawnErrors;

	if (SpawnMode == TEXT("ism"))
	{
		UStaticMesh* Mesh = ResolveStaticMeshForSpawn(ActorClass);
		if (!Mesh)
		{
			OutError = FString::Printf(TEXT("spawn_mode 'ism' requires actor_class to be a basic shape (Cube|Sphere|...) or a StaticMesh asset path; '%s' did not resolve to a StaticMesh."), *ActorClass);
			return;
		}

		FActorSpawnParameters HolderParams;
		AActor* Holder = World->SpawnActor<AActor>(AActor::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, HolderParams);
		if (!Holder) { OutError = TEXT("Failed to spawn the ISM holder actor."); return; }
		Holder->Modify();
		HolderLabel = Options.Label.IsEmpty() ? FString::Printf(TEXT("%s_ISM"), *Mesh->GetName()) : Options.Label;
		Holder->SetActorLabel(HolderLabel);

		UHierarchicalInstancedStaticMeshComponent* HISM = NewObject<UHierarchicalInstancedStaticMeshComponent>(Holder, TEXT("HISM"), RF_Transactional);
		HISM->SetMobility(EComponentMobility::Static);
		HISM->SetStaticMesh(Mesh);
		Holder->SetRootComponent(HISM);
		Holder->AddInstanceComponent(HISM);
		HISM->RegisterComponent();
		HISM->AddInstances(Transforms, /*bShouldReturnIndices*/ false, /*bWorldSpace*/ true);
		Instances = Transforms.Num();
		Spawned = 1;
		EditorActorSubsystem->SetSelectedLevelActors({ Holder });
	}
	else
	{
		for (int32 i = 0; i < Transforms.Num(); ++i)
		{
			FString SingleSpawnError;
			const FString Label = FString::Printf(TEXT("%s_%d"), *BaseLabel, i + 1);
			AActor* NewActor = SpawnActorInLevelInternal(ActorClass, Transforms[i].GetLocation(), Transforms[i].Rotator(), Transforms[i].GetScale3D(), Label, SingleSpawnError);
			if (NewActor)
			{
				NewActor->Modify();
				++Spawned;
			}
			else
			{
				SpawnErrors.Add(FString::Printf(TEXT("'%s': %s"), *Label, *SingleSpawnError));
			}
		}
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), Spawned > 0);
	Result->SetStringField(TEXT("spawn_mode"), SpawnMode);
	Result->SetNumberField(TEXT("requested"), Count);
	Result->SetNumberField(TEXT("spawned"), Spawned);
	Result->SetNumberField(TEXT("skipped_no_ground"), SkippedNoGround);
	if (SpawnMode == TEXT("ism"))
	{
		Result->SetNumberField(TEXT("instances"), Instances);
		Result->SetStringField(TEXT("actor_label"), HolderLabel);
	}
	if (Warnings.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> WarnArr;
		for (const FString& W : Warnings) WarnArr.Add(MakeShareable(new FJsonValueString(W)));
		Result->SetArrayField(TEXT("warnings"), WarnArr);
	}
	if (SpawnErrors.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> ErrArr;
		for (const FString& E : SpawnErrors) ErrArr.Add(MakeShareable(new FJsonValueString(E)));
		Result->SetArrayField(TEXT("errors"), ErrArr);
	}
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);

	if (Spawned == 0 && SpawnErrors.Num() > 0)
	{
		OutError = FString::Printf(TEXT("All %d spawns failed: %s"), SpawnErrors.Num(), *SpawnErrors[0]);
	}
}

void HandleAdvancedSpawn(
	int32 Count, const FString& ActorClass, const FString& BoundingActorLabel,
	const FVector& LocMin, const FVector& LocMax,
	const FVector& ScaleMin, const FVector& ScaleMax,
	const FRotator& RotMin, const FRotator& RotMax,
	FString& OutError)
{
	// Legacy entry point: original behaviour (no ground alignment, per-axis scale, full rotation ranges).
	FAdvancedSpawnOptions Options;
	Options.bAlignToGround = false;
	Options.bAlignToNormal = false;
	Options.bAllowTilt = true;
	Options.bUniformScale = false;
	FString IgnoredJson;
	HandleAdvancedSpawnEx(Count, ActorClass, BoundingActorLabel, LocMin, LocMax, ScaleMin, ScaleMax, RotMin, RotMax, Options, IgnoredJson, OutError);
}

void HandleSetActorTransform(const FString& ActorLabel,
	bool bSetLoc, const FVector& Loc,
	bool bSetRot, const FRotator& Rot,
	bool bSetScale, const FVector& Scale,
	FString& OutJsonString, FString& OutError)
{
	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World) { OutError = TEXT("No editor world"); return; }

	AActor* Actor = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
		if (*It && (*It)->GetActorLabel() == ActorLabel) { Actor = *It; break; }
	if (!Actor) { OutError = FString::Printf(TEXT("Actor '%s' not found in level"), *ActorLabel); return; }

	if (bSetLoc)   Actor->SetActorLocation(Loc);
	if (bSetRot)   Actor->SetActorRotation(Rot);
	if (bSetScale) Actor->SetActorScale3D(Scale);

	Actor->MarkPackageDirty();
	GEditor->NoteSelectionChange();

	FVector FinalLoc = Actor->GetActorLocation();
	FRotator FinalRot = Actor->GetActorRotation();
	FVector FinalScale = Actor->GetActorScale3D();
	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"label\":\"%s\",\"location\":[%.1f,%.1f,%.1f],\"rotation\":[%.1f,%.1f,%.1f],\"scale\":[%.2f,%.2f,%.2f]}"),
		*ActorLabel,
		FinalLoc.X, FinalLoc.Y, FinalLoc.Z,
		FinalRot.Pitch, FinalRot.Yaw, FinalRot.Roll,
		FinalScale.X, FinalScale.Y, FinalScale.Z);
}

void HandleGetActorDetails(const FString& ActorLabel, FString& OutJsonString, FString& OutError)
{
	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World) { OutError = TEXT("No editor world"); return; }

	AActor* Actor = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
		if (*It && (*It)->GetActorLabel() == ActorLabel) { Actor = *It; break; }
	if (!Actor) { OutError = FString::Printf(TEXT("Actor '%s' not found"), *ActorLabel); return; }

	FVector Loc = Actor->GetActorLocation();
	FRotator Rot = Actor->GetActorRotation();
	FVector Scale = Actor->GetActorScale3D();

	FString CompsJson = TEXT("[");
	bool bFirst = true;
	TArray<UActorComponent*> Comps;
	Actor->GetComponents(Comps);
	for (UActorComponent* Comp : Comps)
	{
		if (!Comp) continue;
		if (!bFirst) CompsJson += TEXT(",");
		bFirst = false;

		FString CompClass = Comp->GetClass()->GetName();
		FString CompName = Comp->GetName();
		FString Extra;

		if (UStaticMeshComponent* SMC = Cast<UStaticMeshComponent>(Comp))
		{
			FString MeshPath = SMC->GetStaticMesh() ? SMC->GetStaticMesh()->GetPathName() : TEXT("none");
			Extra = FString::Printf(TEXT(",\"mesh\":\"%s\""), *MeshPath);
		}
		else if (USkeletalMeshComponent* SKC = Cast<USkeletalMeshComponent>(Comp))
		{
			FString MeshPath = SKC->GetSkeletalMeshAsset() ? SKC->GetSkeletalMeshAsset()->GetPathName() : TEXT("none");
			Extra = FString::Printf(TEXT(",\"skeletal_mesh\":\"%s\""), *MeshPath);
		}
		CompsJson += FString::Printf(TEXT("{\"name\":\"%s\",\"class\":\"%s\"%s}"), *CompName, *CompClass, *Extra);
	}
	CompsJson += TEXT("]");

	FVector BoundsOrigin, BoundsExtent;
	Actor->GetActorBounds(false, BoundsOrigin, BoundsExtent);

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"label\":\"%s\",\"class\":\"%s\""
			 ",\"location\":[%.1f,%.1f,%.1f],\"rotation\":[%.1f,%.1f,%.1f],\"scale\":[%.2f,%.2f,%.2f]"
			 ",\"bounds_origin\":[%.1f,%.1f,%.1f],\"bounds_extent\":[%.1f,%.1f,%.1f]"
			 ",\"components\":%s}"),
		*ActorLabel, *Actor->GetClass()->GetName(),
		Loc.X, Loc.Y, Loc.Z,
		Rot.Pitch, Rot.Yaw, Rot.Roll,
		Scale.X, Scale.Y, Scale.Z,
		BoundsOrigin.X, BoundsOrigin.Y, BoundsOrigin.Z,
		BoundsExtent.X, BoundsExtent.Y, BoundsExtent.Z,
		*CompsJson);
}

void HandleDeleteActors(const TArray<FString>& ActorLabels, FString& OutJsonString, FString& OutError)
{
	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World) { OutError = TEXT("No editor world"); return; }

	TArray<AActor*> ToDelete;
	for (const FString& Label : ActorLabels)
	{
		for (TActorIterator<AActor> It(World); It; ++It)
			if (*It && (*It)->GetActorLabel() == Label) { ToDelete.Add(*It); break; }
	}

	if (GEditor)
	{
		if (USelection* SelActors = GEditor->GetSelectedActors())
		{
			for (AActor* Actor : ToDelete)
				if (Actor && SelActors->IsSelected(Actor))
					GEditor->SelectActor(Actor, false, false);
		}
		if (USelection* SelComps = GEditor->GetSelectedComponents())
			SelComps->DeselectAll();
	}

	int32 Deleted = 0;
	for (AActor* Actor : ToDelete)
	{
		if (IsValid(Actor))
		{
			World->DestroyActor(Actor);
			++Deleted;
		}
	}

	GEditor->NoteSelectionChange();
	World->MarkPackageDirty();

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"deleted\":%d,\"requested\":%d}"),
		Deleted, ActorLabels.Num());
}

void HandleDuplicateActor(const FString& ActorLabel, const FVector& Offset, const FString& NewLabel,
	FString& OutJsonString, FString& OutError)
{
	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World) { OutError = TEXT("No editor world"); return; }

	AActor* Source = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
		if (*It && (*It)->GetActorLabel() == ActorLabel) { Source = *It; break; }
	if (!Source) { OutError = FString::Printf(TEXT("Actor '%s' not found"), *ActorLabel); return; }

	UEditorActorSubsystem* EAS = GEditor->GetEditorSubsystem<UEditorActorSubsystem>();
	if (!EAS) { OutError = TEXT("EditorActorSubsystem unavailable"); return; }

	EAS->SetSelectedLevelActors({ Source });
	GEditor->edactDuplicateSelected(World->GetCurrentLevel(), false);

	TArray<AActor*> Selected = EAS->GetSelectedLevelActors();
	AActor* NewActor = (Selected.Num() > 0) ? Selected[0] : nullptr;

	if (!NewActor) { OutError = TEXT("Duplication failed — no new actor selected after duplicate"); return; }

	NewActor->SetActorLocation(Source->GetActorLocation() + Offset);
	if (!NewLabel.IsEmpty()) NewActor->SetActorLabel(NewLabel);

	World->MarkPackageDirty();

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"source\":\"%s\",\"new_label\":\"%s\",\"location\":[%.1f,%.1f,%.1f]}"),
		*ActorLabel, *NewActor->GetActorLabel(),
		NewActor->GetActorLocation().X, NewActor->GetActorLocation().Y, NewActor->GetActorLocation().Z);
}

static UClass* FindColorCorrectClass(const TCHAR* PathName, const TCHAR* ShortName)
{
	UClass* C = FindObject<UClass>(nullptr, PathName);
	if (!C) C = FindFirstObject<UClass>(ShortName, EFindFirstObjectOptions::None, ELogVerbosity::NoLogging);
	return C;
}

static const TCHAR* CCRPluginNotEnabledError =
	TEXT("ColorCorrectRegions plugin is not enabled. Enable it in the .uproject under Plugins → ColorCorrectRegions.");

void HandleSpawnColorCorrectionRegion(const FString& Shape, const FString& ActorLabel,
	float LocationX, float LocationY, float LocationZ,
	FString& OutJsonString, FString& OutError)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World) { OutError = TEXT("No editor world"); return; }

	UClass* CCRClass = FindColorCorrectClass(
		TEXT("/Script/ColorCorrectRegions.ColorCorrectionRegion"),
		TEXT("ColorCorrectionRegion"));
	if (!CCRClass) { OutError = CCRPluginNotEnabledError; return; }

	int32 ShapeIdx = INDEX_NONE;
	const FString S = Shape.ToLower();
	if (S.IsEmpty() || S == TEXT("sphere"))      ShapeIdx = 0;
	else if (S == TEXT("box") || S == TEXT("cube")) ShapeIdx = 1;
	else if (S == TEXT("cylinder"))                 ShapeIdx = 2;
	else if (S == TEXT("cone"))                     ShapeIdx = 3;
	else { OutError = FString::Printf(TEXT("Unknown shape '%s'. Use: sphere, box, cylinder, cone."), *Shape); return; }

	FVector Location(LocationX, LocationY, LocationZ);
	FRotator Rotation = FRotator::ZeroRotator;
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AActor* NewActor = World->SpawnActor(CCRClass, &Location, &Rotation, Params);
	if (!NewActor) { OutError = TEXT("Failed to spawn AColorCorrectionRegion"); return; }

	if (FProperty* TypeProp = FindFProperty<FProperty>(CCRClass, TEXT("Type")))
	{
		void* TypePtr = TypeProp->ContainerPtrToValuePtr<void>(NewActor);
		if (FByteProperty* Byte = CastField<FByteProperty>(TypeProp))
		{
			Byte->SetIntPropertyValue(TypePtr, (int64)ShapeIdx);
		}
		else if (FEnumProperty* Enum = CastField<FEnumProperty>(TypeProp))
		{
			Enum->GetUnderlyingProperty()->SetIntPropertyValue(TypePtr, (int64)ShapeIdx);
		}
		NewActor->PostEditChange();
	}

	if (!ActorLabel.IsEmpty()) NewActor->SetActorLabel(ActorLabel);
	NewActor->MarkPackageDirty();

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"actor_label\":\"%s\",\"actor_class\":\"%s\",\"shape\":\"%s\"}"),
		*NewActor->GetActorLabel(), *CCRClass->GetName(), *Shape);
}

void HandleSpawnColorCorrectionWindow(const FString& Shape, const FString& ActorLabel,
	float LocationX, float LocationY, float LocationZ,
	FString& OutJsonString, FString& OutError)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World) { OutError = TEXT("No editor world"); return; }

	UClass* CCWClass = FindColorCorrectClass(
		TEXT("/Script/ColorCorrectRegions.ColorCorrectionWindow"),
		TEXT("ColorCorrectionWindow"));
	if (!CCWClass) { OutError = CCRPluginNotEnabledError; return; }

	int32 ShapeIdx = INDEX_NONE;
	const FString S = Shape.ToLower();
	if (S.IsEmpty() || S == TEXT("square")) ShapeIdx = 0;
	else if (S == TEXT("circle"))            ShapeIdx = 1;
	else { OutError = FString::Printf(TEXT("Unknown shape '%s'. Use: square, circle."), *Shape); return; }

	FVector Location(LocationX, LocationY, LocationZ);
	FRotator Rotation = FRotator::ZeroRotator;
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AActor* NewActor = World->SpawnActor(CCWClass, &Location, &Rotation, Params);
	if (!NewActor) { OutError = TEXT("Failed to spawn AColorCorrectionWindow"); return; }

	if (FProperty* WTypeProp = FindFProperty<FProperty>(CCWClass, TEXT("WindowType")))
	{
		void* WTypePtr = WTypeProp->ContainerPtrToValuePtr<void>(NewActor);
		if (FByteProperty* Byte = CastField<FByteProperty>(WTypeProp))
		{
			Byte->SetIntPropertyValue(WTypePtr, (int64)ShapeIdx);
		}
		else if (FEnumProperty* Enum = CastField<FEnumProperty>(WTypeProp))
		{
			Enum->GetUnderlyingProperty()->SetIntPropertyValue(WTypePtr, (int64)ShapeIdx);
		}
		NewActor->PostEditChange();
	}

	if (!ActorLabel.IsEmpty()) NewActor->SetActorLabel(ActorLabel);
	NewActor->MarkPackageDirty();

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"actor_label\":\"%s\",\"actor_class\":\"%s\",\"shape\":\"%s\"}"),
		*NewActor->GetActorLabel(), *CCWClass->GetName(), *Shape);
}

static bool TrySetProperty(UObject* Obj, const FString& PropName, const FString& Value, FString& OutMsg)
{
	if (!Obj) return false;
	FProperty* Prop = FindFProperty<FProperty>(Obj->GetClass(), *PropName);
	if (!Prop) return false;

	void* PropPtr = Prop->ContainerPtrToValuePtr<void>(Obj);

	if (FFloatProperty* FP = CastField<FFloatProperty>(Prop))
	{
		FP->SetPropertyValue(PropPtr, FCString::Atof(*Value));
		OutMsg = FString::Printf(TEXT("Set float %s = %s"), *PropName, *Value);
		return true;
	}
	if (FDoubleProperty* DP = CastField<FDoubleProperty>(Prop))
	{
		DP->SetPropertyValue(PropPtr, FCString::Atod(*Value));
		OutMsg = FString::Printf(TEXT("Set double %s = %s"), *PropName, *Value);
		return true;
	}
	if (FIntProperty* IP = CastField<FIntProperty>(Prop))
	{
		IP->SetPropertyValue(PropPtr, FCString::Atoi(*Value));
		OutMsg = FString::Printf(TEXT("Set int %s = %s"), *PropName, *Value);
		return true;
	}
	if (FBoolProperty* BP = CastField<FBoolProperty>(Prop))
	{
		bool bVal = Value.ToBool() || Value.Equals(TEXT("1")) || Value.Equals(TEXT("true"), ESearchCase::IgnoreCase);
		BP->SetPropertyValue(PropPtr, bVal);
		OutMsg = FString::Printf(TEXT("Set bool %s = %s"), *PropName, bVal ? TEXT("true") : TEXT("false"));
		return true;
	}
	if (FStrProperty* SP = CastField<FStrProperty>(Prop))
	{
		SP->SetPropertyValue(PropPtr, Value);
		OutMsg = FString::Printf(TEXT("Set string %s = %s"), *PropName, *Value);
		return true;
	}
	if (FStructProperty* StP = CastField<FStructProperty>(Prop))
	{
		auto ParseStructFloats = [](const FString& In, int32 NumExpected,
			float* OutA, const TCHAR* NameA,
			float* OutB, const TCHAR* NameB,
			float* OutC, const TCHAR* NameC,
			float* OutD, const TCHAR* NameD) -> bool
		{
			FString S = In;
			S.TrimStartAndEndInline();
			if (S.Len() >= 2 && (S[0] == TEXT('(') || S[0] == TEXT('{')) &&
			    (S[S.Len() - 1] == TEXT(')') || S[S.Len() - 1] == TEXT('}')))
			{
				S = S.Mid(1, S.Len() - 2);
				S.TrimStartAndEndInline();
			}
			TArray<FString> Parts;
			S.ParseIntoArray(Parts, TEXT(","));
			if (Parts.Num() < NumExpected) return false;

			auto TryName = [&Parts](const TCHAR* Name, float& Out) -> bool
			{
				if (!Name) return false;
				for (const FString& P : Parts)
				{
					FString L, R;
					if (P.Split(TEXT("="), &L, &R))
					{
						L.TrimStartAndEndInline(); R.TrimStartAndEndInline();
						if (L.Equals(Name, ESearchCase::IgnoreCase))
						{
							Out = FCString::Atof(*R);
							return true;
						}
					}
				}
				return false;
			};

			bool bAnyNamed = false;
			for (const FString& P : Parts) if (P.Contains(TEXT("="))) { bAnyNamed = true; break; }

			if (bAnyNamed)
			{
				bool bA = OutA ? TryName(NameA, *OutA) : true;
				bool bB = OutB ? TryName(NameB, *OutB) : true;
				bool bC = OutC ? TryName(NameC, *OutC) : true;
				bool bD = OutD ? TryName(NameD, *OutD) : true;
				return bA && bB && bC && bD;
			}

			if (OutA) *OutA = FCString::Atof(*Parts[0].TrimStartAndEnd());
			if (OutB) *OutB = FCString::Atof(*Parts[1].TrimStartAndEnd());
			if (OutC) *OutC = FCString::Atof(*Parts[2].TrimStartAndEnd());
			if (OutD && Parts.Num() >= 4) *OutD = FCString::Atof(*Parts[3].TrimStartAndEnd());
			return true;
		};

		if (StP->Struct == TBaseStructure<FVector>::Get())
		{
			float X = 0.f, Y = 0.f, Z = 0.f;
			if (ParseStructFloats(Value, 3,
				&X, TEXT("X"), &Y, TEXT("Y"), &Z, TEXT("Z"), nullptr, nullptr))
			{
				*StP->ContainerPtrToValuePtr<FVector>(Obj) = FVector(X, Y, Z);
				OutMsg = FString::Printf(TEXT("Set FVector %s = (X=%g, Y=%g, Z=%g)"), *PropName, X, Y, Z);
				return true;
			}
		}
		if (StP->Struct == TBaseStructure<FRotator>::Get())
		{
			float P = 0.f, Yaw = 0.f, Roll = 0.f;
			if (ParseStructFloats(Value, 3,
				&P, TEXT("Pitch"), &Yaw, TEXT("Yaw"), &Roll, TEXT("Roll"), nullptr, nullptr))
			{
				*StP->ContainerPtrToValuePtr<FRotator>(Obj) = FRotator(P, Yaw, Roll);
				OutMsg = FString::Printf(TEXT("Set FRotator %s = (P=%g, Y=%g, R=%g)"), *PropName, P, Yaw, Roll);
				return true;
			}
		}
		if (StP->Struct == TBaseStructure<FLinearColor>::Get())
		{
			float R = 0.f, G = 0.f, B = 0.f, A = 1.f;
			if (ParseStructFloats(Value, 3,
				&R, TEXT("R"), &G, TEXT("G"), &B, TEXT("B"), &A, TEXT("A")))
			{
				*StP->ContainerPtrToValuePtr<FLinearColor>(Obj) = FLinearColor(R, G, B, A);
				OutMsg = FString::Printf(TEXT("Set FLinearColor %s = (%g,%g,%g,%g)"), *PropName, R, G, B, A);
				return true;
			}
		}
	}
	if (FClassProperty* CP = CastField<FClassProperty>(Prop))
	{
		UClass* ResolvedClass = LoadObject<UClass>(nullptr, *Value);
		if (!ResolvedClass)
		{
			UBlueprint* BP = LoadObject<UBlueprint>(nullptr, *Value);
			if (BP && BP->GeneratedClass)
				ResolvedClass = BP->GeneratedClass;
		}
		if (!ResolvedClass)
		{
			FString ClassPath = Value + TEXT("_C");
			ResolvedClass = LoadObject<UClass>(nullptr, *ClassPath);
		}
		if (ResolvedClass)
		{
			CP->SetObjectPropertyValue(PropPtr, ResolvedClass);
			OutMsg = FString::Printf(TEXT("Set class %s = %s"), *PropName, *ResolvedClass->GetName());
			return true;
		}
	}
	if (FObjectProperty* OP = CastField<FObjectProperty>(Prop))
	{
		UObject* LoadedObj = LoadObject<UObject>(nullptr, *Value);
		if (LoadedObj)
		{
			OP->SetObjectPropertyValue(PropPtr, LoadedObj);
			OutMsg = FString::Printf(TEXT("Set object %s = %s"), *PropName, *Value);
			return true;
		}
	}
	if (FEnumProperty* EP = CastField<FEnumProperty>(Prop))
	{
		UEnum* Enum = EP->GetEnum();
		int64 EnumVal = -1;
		if (Value.IsNumeric())
		{
			EnumVal = FCString::Atoi64(*Value);
		}
		else if (Enum)
		{
			for (int32 i = 0; i < Enum->NumEnums(); i++)
			{
				FString EntryName = Enum->GetNameStringByIndex(i);
				int32 Sep; if (EntryName.FindLastChar(':', Sep)) EntryName = EntryName.Mid(Sep + 1);
				if (EntryName.Equals(Value, ESearchCase::IgnoreCase))
				{
					EnumVal = Enum->GetValueByIndex(i);
					break;
				}
			}
		}
		if (EnumVal >= 0)
		{
			void* ValuePtr = EP->ContainerPtrToValuePtr<void>(Obj);
			EP->GetUnderlyingProperty()->SetIntPropertyValue(ValuePtr, EnumVal);
			OutMsg = FString::Printf(TEXT("Set enum %s = %lld"), *PropName, EnumVal);
			return true;
		}
	}
	if (FByteProperty* ByteProp = CastField<FByteProperty>(Prop))
	{
		uint8 ByteVal = 0;
		if (Value.IsNumeric())
		{
			ByteVal = (uint8)FCString::Atoi(*Value);
		}
		else if (UEnum* Enum = ByteProp->Enum)
		{
			for (int32 i = 0; i < Enum->NumEnums(); i++)
			{
				FString EntryName = Enum->GetNameStringByIndex(i);
				int32 Sep; if (EntryName.FindLastChar(':', Sep)) EntryName = EntryName.Mid(Sep + 1);
				if (EntryName.Equals(Value, ESearchCase::IgnoreCase))
				{
					ByteVal = (uint8)Enum->GetValueByIndex(i);
					break;
				}
			}
		}
		ByteProp->SetPropertyValue(PropPtr, ByteVal);
		OutMsg = FString::Printf(TEXT("Set byte/enum %s = %d"), *PropName, (int32)ByteVal);
		return true;
	}
	if (FNameProperty* NP = CastField<FNameProperty>(Prop))
	{
		NP->SetPropertyValue(PropPtr, FName(*Value));
		OutMsg = FString::Printf(TEXT("Set name %s = %s"), *PropName, *Value);
		return true;
	}
	if (FTextProperty* TP = CastField<FTextProperty>(Prop))
	{
		TP->SetPropertyValue(PropPtr, FText::FromString(Value));
		OutMsg = FString::Printf(TEXT("Set text %s = %s"), *PropName, *Value);
		return true;
	}
	return false;
}

void HandleSetActorProperty(const FString& ActorLabel, const FString& PropertyName, const FString& PropertyValue,
	FString& OutJsonString, FString& OutError)
{
	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World) { OutError = TEXT("No editor world"); return; }

	FString ResolvedPropertyName = PropertyName;
	if (ResolvedPropertyName.Equals(TEXT("GameModeClass"), ESearchCase::IgnoreCase) ||
		ResolvedPropertyName.Equals(TEXT("GameMode"), ESearchCase::IgnoreCase))
		ResolvedPropertyName = TEXT("DefaultGameMode");

	AActor* Actor = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
		if (*It && (*It)->GetActorLabel() == ActorLabel) { Actor = *It; break; }
	if (!Actor && ActorLabel.Contains(TEXT("WorldSettings"), ESearchCase::IgnoreCase))
		Actor = World->GetWorldSettings();
	if (!Actor) { OutError = FString::Printf(TEXT("Actor '%s' not found"), *ActorLabel); return; }

	FString SetMsg;

	auto FinalizeComponentWrite = [Actor](UActorComponent* Comp, const FString& PropName)
	{
		if (!Comp) return;
		Comp->PostEditChange();
		Comp->MarkPackageDirty();
		Actor->PostEditChange();
		Actor->MarkPackageDirty();
		if (USceneComponent* SC = Cast<USceneComponent>(Comp))
		{
			if (PropName.Equals(TEXT("RelativeScale3D"), ESearchCase::IgnoreCase))
			{
				SC->SetRelativeScale3D(SC->GetRelativeScale3D());
			}
			else if (PropName.Equals(TEXT("RelativeLocation"), ESearchCase::IgnoreCase))
			{
				SC->SetRelativeLocation(SC->GetRelativeLocation());
			}
			else if (PropName.Equals(TEXT("RelativeRotation"), ESearchCase::IgnoreCase))
			{
				SC->SetRelativeRotation(SC->GetRelativeRotation());
			}
			SC->UpdateComponentToWorld();
		}
	};

	if (ResolvedPropertyName.Contains(TEXT(".")))
	{
		FString CompName, PropName;
		ResolvedPropertyName.Split(TEXT("."), &CompName, &PropName);
		TArray<UActorComponent*> Comps;
		Actor->GetComponents(Comps);
		for (UActorComponent* Comp : Comps)
		{
			if (!Comp) continue;
			if (Comp->GetName().Equals(CompName, ESearchCase::IgnoreCase))
			{
				if (TrySetProperty(Comp, PropName, PropertyValue, SetMsg))
				{
					FinalizeComponentWrite(Comp, PropName);
					OutJsonString = FString::Printf(
						TEXT("{\"success\":true,\"target\":\"component\",\"component\":\"%s\",\"detail\":\"%s\"}"),
						*Comp->GetName(), *SetMsg);
					return;
				}
				OutError = FString::Printf(TEXT("Property '%s' not found on component '%s'"), *PropName, *CompName);
				return;
			}
		}
		OutError = FString::Printf(TEXT("Component '%s' not found on actor '%s'"), *CompName, *ActorLabel);
		return;
	}

	if (TrySetProperty(Actor, ResolvedPropertyName, PropertyValue, SetMsg))
	{
		Actor->PostEditChange();
		Actor->MarkPackageDirty();
		OutJsonString = FString::Printf(TEXT("{\"success\":true,\"target\":\"actor\",\"detail\":\"%s\"}"), *SetMsg);
		return;
	}

	TArray<UActorComponent*> Comps;
	Actor->GetComponents(Comps);
	for (UActorComponent* Comp : Comps)
	{
		if (!Comp) continue;
		if (TrySetProperty(Comp, ResolvedPropertyName, PropertyValue, SetMsg))
		{
			FinalizeComponentWrite(Comp, ResolvedPropertyName);
			OutJsonString = FString::Printf(
				TEXT("{\"success\":true,\"target\":\"component\",\"component\":\"%s\",\"detail\":\"%s\"}"),
				*Comp->GetName(), *SetMsg);
			return;
		}
	}

	OutError = FString::Printf(TEXT("Property '%s' not found on actor '%s' or any of its components"), *ResolvedPropertyName, *ActorLabel);
}

void HandleRenameActor(const FString& OldLabel, const FString& NewLabel,
	FString& OutJsonString, FString& OutError)
{
	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World) { OutError = TEXT("No editor world"); return; }

	AActor* Actor = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
		if (*It && (*It)->GetActorLabel() == OldLabel) { Actor = *It; break; }
	if (!Actor) { OutError = FString::Printf(TEXT("Actor '%s' not found"), *OldLabel); return; }

	Actor->SetActorLabel(NewLabel);
	Actor->MarkPackageDirty();

	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"old_label\":\"%s\",\"new_label\":\"%s\"}"),
		*OldLabel, *Actor->GetActorLabel());
}

void HandleGetActorsByClass(const FString& ClassName, int32 MaxResults, bool bIncludeLocation,
	FString& OutJsonString, FString& OutError)
{
	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World) { OutError = TEXT("No editor world"); return; }

	const int32 EffectiveMax = (MaxResults <= 0) ? GActorListDefaultMax : MaxResults;

	int32 TotalMatched = 0;
	TArray<TSharedPtr<FJsonValue>> ActorArray;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor) continue;
		FString ActorClass = Actor->GetClass()->GetName();
		if (!ActorClass.Contains(ClassName, ESearchCase::IgnoreCase)) continue;

		++TotalMatched;
		if (ActorArray.Num() >= EffectiveMax) continue;

		TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
		Obj->SetStringField(TEXT("label"), Actor->GetActorLabel());
		Obj->SetStringField(TEXT("class"), ActorClass);
		if (bIncludeLocation)
		{
			FVector Loc = Actor->GetActorLocation();
			TArray<TSharedPtr<FJsonValue>> LocArr = {
				MakeShareable(new FJsonValueNumber(Loc.X)),
				MakeShareable(new FJsonValueNumber(Loc.Y)),
				MakeShareable(new FJsonValueNumber(Loc.Z))
			};
			Obj->SetArrayField(TEXT("location"), LocArr);
		}
		ActorArray.Add(MakeShareable(new FJsonValueObject(Obj)));
	}
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetArrayField(TEXT("actors"), ActorArray);
	Result->SetNumberField(TEXT("count"), ActorArray.Num());
	Result->SetNumberField(TEXT("total"), TotalMatched);
	Result->SetStringField(TEXT("filter_class"), ClassName);
	if (TotalMatched > ActorArray.Num())
	{
		Result->SetBoolField(TEXT("truncated"), true);
		if (ClassName.IsEmpty())
		{
			Result->SetStringField(TEXT("hint"),
				TEXT("class_name was empty so every actor in the level matched. Narrow with a class_name "
				     "substring (e.g. 'Lightswitch', 'PointLight', 'Character') OR use find_actors_by_bounds "
				     "to limit by region. Pass max_results=N to raise the cap explicitly."));
		}
		else
		{
			Result->SetStringField(TEXT("hint"),
				FString::Printf(TEXT("%d total matches — only the first %d returned. Narrow class_name or pass max_results=N."),
					TotalMatched, ActorArray.Num()));
		}
	}
	if (!bIncludeLocation && TotalMatched > GActorListLocationCap)
	{
		Result->SetStringField(TEXT("location_omitted"),
			TEXT("location stripped from rows (bulk listing). Pass include_location=true to keep coordinates."));
	}
	TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
}

void HandleGetActorsByClass(const FString& ClassName, FString& OutJsonString, FString& OutError)
{
	HandleGetActorsByClass(ClassName, 0, false, OutJsonString, OutError);
}

void HandleGetActorsInBox(const FVector& Center, const FVector& Extent, FString& OutJsonString, FString& OutError)
{
	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World) { OutError = TEXT("No editor world"); return; }

	FBox Box(Center - Extent, Center + Extent);
	TArray<TSharedPtr<FJsonValue>> ActorArray;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor) continue;
		if (Box.IsInsideOrOn(Actor->GetActorLocation()))
		{
			TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
			Obj->SetStringField(TEXT("label"), Actor->GetActorLabel());
			Obj->SetStringField(TEXT("class"), Actor->GetClass()->GetName());
			FVector Loc = Actor->GetActorLocation();
			TArray<TSharedPtr<FJsonValue>> LocArr = {
				MakeShareable(new FJsonValueNumber(Loc.X)),
				MakeShareable(new FJsonValueNumber(Loc.Y)),
				MakeShareable(new FJsonValueNumber(Loc.Z))
			};
			Obj->SetArrayField(TEXT("location"), LocArr);
			ActorArray.Add(MakeShareable(new FJsonValueObject(Obj)));
		}
	}
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetArrayField(TEXT("actors"), ActorArray);
	Result->SetNumberField(TEXT("count"), ActorArray.Num());
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
}

void HandleGetActorComponents(const FString& ActorLabel, FString& OutJsonString, FString& OutError)
{
	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World) { OutError = TEXT("No editor world"); return; }

	AActor* Actor = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
		if (*It && (*It)->GetActorLabel() == ActorLabel) { Actor = *It; break; }
	if (!Actor) { OutError = FString::Printf(TEXT("Actor '%s' not found"), *ActorLabel); return; }

	TArray<UActorComponent*> Comps;
	Actor->GetComponents(Comps);

	TArray<TSharedPtr<FJsonValue>> CompArray;
	for (UActorComponent* Comp : Comps)
	{
		if (!Comp) continue;
		TSharedPtr<FJsonObject> CompObj = MakeShareable(new FJsonObject);
		CompObj->SetStringField(TEXT("name"), Comp->GetName());
		CompObj->SetStringField(TEXT("class"), Comp->GetClass()->GetName());
		if (UStaticMeshComponent* SMC = Cast<UStaticMeshComponent>(Comp))
			if (SMC->GetStaticMesh())
				CompObj->SetStringField(TEXT("mesh"), SMC->GetStaticMesh()->GetPathName());
		if (USceneComponent* SC = Cast<USceneComponent>(Comp))
		{
			FVector RelLoc = SC->GetRelativeLocation();
			TArray<TSharedPtr<FJsonValue>> LocArr = {
				MakeShareable(new FJsonValueNumber(RelLoc.X)),
				MakeShareable(new FJsonValueNumber(RelLoc.Y)),
				MakeShareable(new FJsonValueNumber(RelLoc.Z))
			};
			CompObj->SetArrayField(TEXT("relative_location"), LocArr);
		}
		CompArray.Add(MakeShareable(new FJsonValueObject(CompObj)));
	}
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("label"), ActorLabel);
	Result->SetArrayField(TEXT("components"), CompArray);
	Result->SetNumberField(TEXT("component_count"), CompArray.Num());
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
}

void HandleGetLevelInfo(FString& OutJsonString, FString& OutError)
{
	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World) { OutError = TEXT("No editor world"); return; }

	int32 ActorCount = 0;
	for (TActorIterator<AActor> It(World); It; ++It) ++ActorCount;

	AWorldSettings* WS = World->GetWorldSettings();
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("world_name"), World->GetName());
	Result->SetNumberField(TEXT("actor_count"), ActorCount);
	if (WS)
	{
		Result->SetNumberField(TEXT("gravity_z"), WS->GetGravityZ());
		Result->SetNumberField(TEXT("kill_z"), WS->KillZ);
		if (WS->DefaultGameMode)
			Result->SetStringField(TEXT("default_game_mode"), WS->DefaultGameMode->GetName());
	}
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
}

void HandleSetActorVisibility(const TArray<FString>& ActorLabels, bool bVisible, FString& OutJsonString, FString& OutError)
{
	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World) { OutError = TEXT("No editor world"); return; }

	TArray<FString> Updated, NotFound;
	for (const FString& Label : ActorLabels)
	{
		bool bFound = false;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (*It && (*It)->GetActorLabel() == Label)
			{
				(*It)->SetIsTemporarilyHiddenInEditor(!bVisible);
				(*It)->MarkPackageDirty();
				Updated.Add(Label);
				bFound = true;
				break;
			}
		}
		if (!bFound) NotFound.Add(Label);
	}
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetNumberField(TEXT("updated_count"), Updated.Num());
	TArray<TSharedPtr<FJsonValue>> UpdatedArr;
	for (const FString& L : Updated) UpdatedArr.Add(MakeShareable(new FJsonValueString(L)));
	Result->SetArrayField(TEXT("updated"), UpdatedArr);
	if (NotFound.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> NFArr;
		for (const FString& L : NotFound) NFArr.Add(MakeShareable(new FJsonValueString(L)));
		Result->SetArrayField(TEXT("not_found"), NFArr);
	}
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
}

void HandleBulkTransformActors(const TArray<TSharedPtr<FJsonValue>>& Actors, FString& OutJsonString, FString& OutError)
{
	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World) { OutError = TEXT("No editor world"); return; }

	TMap<FString, AActor*> LabelIndex;
	BuildActorLabelIndex(World, LabelIndex);

	const FScopedTransaction Transaction(NSLOCTEXT("AxivorAI", "BulkTransformActors", "Bulk Transform Actors"));

	int32 UpdatedCount = 0;
	TArray<FString> Errors;
	for (const TSharedPtr<FJsonValue>& Val : Actors)
	{
		TSharedPtr<FJsonObject> Entry = Val->AsObject();
		if (!Entry.IsValid()) continue;
		FString Label;
		if (!Entry->TryGetStringField(TEXT("actor_label"), Label) && !Entry->TryGetStringField(TEXT("label"), Label)) continue;

		AActor* const* Found = LabelIndex.Find(Label);
		AActor* Actor = Found ? *Found : nullptr;
		if (!Actor) { Errors.Add(FString::Printf(TEXT("'%s' not found"), *Label)); continue; }

		Actor->Modify();

		// Accepts [x,y,z] / {x,y,z} for vectors and [pitch,yaw,roll] / {pitch,yaw,roll} / {x:roll,y:pitch,z:yaw} for rotation.
		if (Entry->HasField(TEXT("location")))
			Actor->SetActorLocation(ExtractVector(Entry, TEXT("location"), Actor->GetActorLocation()));

		if (Entry->HasField(TEXT("rotation")))
			Actor->SetActorRotation(ExtractRotator(Entry, TEXT("rotation"), Actor->GetActorRotation()));

		if (Entry->HasField(TEXT("scale")))
			Actor->SetActorScale3D(ExtractVector(Entry, TEXT("scale"), Actor->GetActorScale3D()));

		Actor->MarkPackageDirty();
		UpdatedCount++;
	}
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), Errors.Num() == 0);
	Result->SetNumberField(TEXT("updated_count"), UpdatedCount);
	if (Errors.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> ErrArr;
		for (const FString& E : Errors) ErrArr.Add(MakeShareable(new FJsonValueString(E)));
		Result->SetArrayField(TEXT("errors"), ErrArr);
	}
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
}

void HandleAlignActorsToFloor(const TArray<FString>& ActorLabels, float TraceOffset, FString& OutJsonString, FString& OutError)
{
	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World) { OutError = TEXT("No editor world"); return; }

	TMap<FString, AActor*> LabelIndex;
	BuildActorLabelIndex(World, LabelIndex);

	const FScopedTransaction Transaction(NSLOCTEXT("AxivorAI", "AlignActorsToFloor", "Align Actors To Floor"));

	TArray<TSharedPtr<FJsonValue>> ResultArr;
	for (const FString& Label : ActorLabels)
	{
		AActor* const* Found = LabelIndex.Find(Label);
		AActor* Actor = Found ? *Found : nullptr;

		TSharedPtr<FJsonObject> Entry = MakeShareable(new FJsonObject);
		Entry->SetStringField(TEXT("label"), Label);
		if (!Actor)
		{
			Entry->SetBoolField(TEXT("success"), false);
			Entry->SetStringField(TEXT("error"), TEXT("Not found"));
			ResultArr.Add(MakeShareable(new FJsonValueObject(Entry)));
			continue;
		}
		FVector Origin, BoxExtent;
		Actor->GetActorBounds(false, Origin, BoxExtent);
		float PivotToBtm = Origin.Z - BoxExtent.Z - Actor->GetActorLocation().Z;
		FVector TraceStart = Actor->GetActorLocation() + FVector(0, 0, TraceOffset);
		FVector TraceEnd = TraceStart - FVector(0, 0, 10000.0f);
		FHitResult Hit;
		FCollisionQueryParams Params;
		Params.AddIgnoredActor(Actor);
		if (World->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, ECC_WorldStatic, Params))
		{
			Actor->Modify();
			FVector NewLoc = Actor->GetActorLocation();
			NewLoc.Z = Hit.ImpactPoint.Z - PivotToBtm;
			Actor->SetActorLocation(NewLoc);
			Actor->MarkPackageDirty();
			Entry->SetBoolField(TEXT("success"), true);
			Entry->SetNumberField(TEXT("floor_z"), Hit.ImpactPoint.Z);
		}
		else
		{
			Entry->SetBoolField(TEXT("success"), false);
			Entry->SetStringField(TEXT("error"), TEXT("No floor found below actor"));
		}
		ResultArr.Add(MakeShareable(new FJsonValueObject(Entry)));
	}
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetArrayField(TEXT("results"), ResultArr);
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
}

void HandleAttachActor(const FString& ActorLabel, const FString& ParentLabel, bool bKeepWorldTransform,
	const FString& SocketName, FString& OutJsonString, FString& OutError)
{
	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World) { OutError = TEXT("No editor world"); return; }

	AActor* Child = nullptr;
	AActor* Parent = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (*It && (*It)->GetActorLabel() == ActorLabel) Child = *It;
		if (*It && (*It)->GetActorLabel() == ParentLabel) Parent = *It;
	}
	if (!Child)  { OutError = FString::Printf(TEXT("Actor '%s' not found"), *ActorLabel);  return; }
	if (!Parent) { OutError = FString::Printf(TEXT("Parent '%s' not found"), *ParentLabel); return; }

	EAttachmentRule Rule = bKeepWorldTransform ? EAttachmentRule::KeepWorld : EAttachmentRule::KeepRelative;
	FAttachmentTransformRules Rules(Rule, Rule, Rule, false);

	if (!SocketName.IsEmpty())
		Child->AttachToComponent(Parent->GetRootComponent(), Rules, FName(*SocketName));
	else
		Child->AttachToActor(Parent, Rules);

	Child->MarkPackageDirty();
	FString SocketInfo = SocketName.IsEmpty() ? TEXT("") : FString::Printf(TEXT(",\"socket\":\"%s\""), *SocketName);
	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"actor\":\"%s\",\"parent\":\"%s\"%s}"),
		*ActorLabel, *ParentLabel, *SocketInfo);
}

void HandleSetActorsPropertyByFilter(const FString& ClassFilter, const FString& PropertyName,
	const FString& PropertyValue, FString& OutJsonString, FString& OutError)
{
	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World) { OutError = TEXT("No editor world"); return; }
	if (ClassFilter.IsEmpty()) { OutError = TEXT("class_filter is required"); return; }
	if (PropertyName.IsEmpty()) { OutError = TEXT("property_name is required"); return; }

	int32 AffectedCount = 0;
	TArray<FString> Failed;

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor) continue;
		if (!Actor->GetClass()->GetName().Contains(ClassFilter, ESearchCase::IgnoreCase) &&
			!Actor->GetActorLabel().Contains(ClassFilter, ESearchCase::IgnoreCase)) continue;

		FString DummyJson, ActorError;
		HandleSetActorProperty(Actor->GetActorLabel(), PropertyName, PropertyValue, DummyJson, ActorError);
		if (ActorError.IsEmpty())
			++AffectedCount;
		else
			Failed.Add(Actor->GetActorLabel());
	}

	TSharedPtr<FJsonObject> J = MakeShareable(new FJsonObject);
	J->SetBoolField(TEXT("success"), true);
	J->SetNumberField(TEXT("affected_count"), AffectedCount);
	J->SetStringField(TEXT("class_filter"), ClassFilter);
	J->SetStringField(TEXT("property_name"), PropertyName);
	J->SetStringField(TEXT("property_value"), PropertyValue);
	if (Failed.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> FailArr;
		for (const FString& L : Failed) FailArr.Add(MakeShareable(new FJsonValueString(L)));
		J->SetArrayField(TEXT("failed"), FailArr);
	}
	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(J.ToSharedRef(), W);
}

void HandleDetachActor(const FString& ActorLabel, FString& OutJsonString, FString& OutError)
{
	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World) { OutError = TEXT("No editor world"); return; }

	AActor* Actor = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
		if (*It && (*It)->GetActorLabel() == ActorLabel) { Actor = *It; break; }
	if (!Actor) { OutError = FString::Printf(TEXT("Actor '%s' not found"), *ActorLabel); return; }

	FDetachmentTransformRules Rules(EDetachmentRule::KeepWorld, true);
	Actor->DetachFromActor(Rules);
	Actor->MarkPackageDirty();
	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"actor\":\"%s\",\"detached\":true}"), *ActorLabel);
}

void HandleMoveActorToFolder(const TArray<FString>& ActorLabels, const FString& FolderPath,
	FString& OutJsonString, FString& OutError)
{
	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World) { OutError = TEXT("No editor world"); return; }

	TArray<FString> Updated, NotFound;
	FName FolderName(*FolderPath);
	for (const FString& Label : ActorLabels)
	{
		bool bFound = false;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (*It && (*It)->GetActorLabel() == Label)
			{
				(*It)->SetFolderPath(FolderName);
				(*It)->MarkPackageDirty();
				Updated.Add(Label);
				bFound = true;
				break;
			}
		}
		if (!bFound) NotFound.Add(Label);
	}
	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("folder"), FolderPath);
	Result->SetNumberField(TEXT("updated_count"), Updated.Num());
	TArray<TSharedPtr<FJsonValue>> UpdatedArr;
	for (const FString& L : Updated) UpdatedArr.Add(MakeShareable(new FJsonValueString(L)));
	Result->SetArrayField(TEXT("updated"), UpdatedArr);
	if (NotFound.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> NFArr;
		for (const FString& L : NotFound) NFArr.Add(MakeShareable(new FJsonValueString(L)));
		Result->SetArrayField(TEXT("not_found"), NFArr);
	}
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
}

void HandleSetStaticMesh(const FString& ActorLabel, const FString& MeshPath, FString& OutJsonString, FString& OutError)
{
	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World) { OutError = TEXT("No editor world"); return; }

	AActor* Actor = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
		if (*It && (*It)->GetActorLabel() == ActorLabel) { Actor = *It; break; }
	if (!Actor) { OutError = FString::Printf(TEXT("Actor '%s' not found"), *ActorLabel); return; }

	UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *MeshPath);
	if (!Mesh) { OutError = FString::Printf(TEXT("Mesh not found: '%s'"), *MeshPath); return; }

	UStaticMeshComponent* SMC = Actor->FindComponentByClass<UStaticMeshComponent>();
	if (!SMC) { OutError = FString::Printf(TEXT("Actor '%s' has no StaticMeshComponent"), *ActorLabel); return; }

	SMC->SetStaticMesh(Mesh);
	Actor->MarkPackageDirty();
	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"actor\":\"%s\",\"mesh\":\"%s\"}"),
		*ActorLabel, *MeshPath);
}

void HandleSetActorPhysics(const FString& ActorLabel, bool bSimulate,
	bool bSetGravity, bool bEnableGravity,
	float Mass, float LinearDamping, float AngularDamping,
	FString& OutJsonString, FString& OutError)
{
	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World) { OutError = TEXT("No editor world"); return; }

	AActor* Actor = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
		if (*It && (*It)->GetActorLabel() == ActorLabel) { Actor = *It; break; }
	if (!Actor) { OutError = FString::Printf(TEXT("Actor '%s' not found"), *ActorLabel); return; }

	TArray<UPrimitiveComponent*> PrimComps;
	Actor->GetComponents<UPrimitiveComponent>(PrimComps);
	int32 Count = 0;
	for (UPrimitiveComponent* PC : PrimComps)
	{
		if (!PC) continue;
		PC->SetSimulatePhysics(bSimulate);
		if (bSetGravity) PC->SetEnableGravity(bEnableGravity);
		if (Mass >= 0.f)
		{
			PC->SetMassOverrideInKg(NAME_None, Mass, true);
		}
		if (LinearDamping >= 0.f)  PC->SetLinearDamping(LinearDamping);
		if (AngularDamping >= 0.f) PC->SetAngularDamping(AngularDamping);
		Count++;
	}
	Actor->MarkPackageDirty();
	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"actor\":\"%s\",\"simulate_physics\":%s,\"components_affected\":%d}"),
		*ActorLabel, bSimulate ? TEXT("true") : TEXT("false"), Count);
}

void HandleSetLightProperty(const FString& ActorLabel, const TSharedPtr<FJsonObject>& Params,
	FString& OutJsonString, FString& OutError)
{
	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World) { OutError = TEXT("No editor world"); return; }

	AActor* Actor = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
		if (*It && (*It)->GetActorLabel() == ActorLabel) { Actor = *It; break; }
	if (!Actor) { OutError = FString::Printf(TEXT("Actor '%s' not found"), *ActorLabel); return; }

	ULightComponent* LC = Actor->FindComponentByClass<ULightComponent>();
	if (!LC) { OutError = FString::Printf(TEXT("Actor '%s' has no LightComponent"), *ActorLabel); return; }

	double Intensity;
	if (Params->TryGetNumberField(TEXT("intensity"), Intensity))
		LC->SetIntensity((float)Intensity);

	const TArray<TSharedPtr<FJsonValue>>* ColorArr = nullptr;
	if (!Params->TryGetArrayField(TEXT("color"), ColorArr))
		Params->TryGetArrayField(TEXT("light_color"), ColorArr);
	if (ColorArr && ColorArr->Num() >= 3)
	{
		FLinearColor C((float)(*ColorArr)[0]->AsNumber(), (float)(*ColorArr)[1]->AsNumber(),
			(float)(*ColorArr)[2]->AsNumber());
		LC->SetLightColor(C);
	}

	bool bCastShadows;
	if (Params->TryGetBoolField(TEXT("cast_shadows"), bCastShadows))
		LC->SetCastShadows(bCastShadows);

	double AttenuationRadius;
	if (Params->TryGetNumberField(TEXT("attenuation_radius"), AttenuationRadius))
		if (ULocalLightComponent* LLC = Cast<ULocalLightComponent>(LC))
			LLC->SetAttenuationRadius((float)AttenuationRadius);

	double Temperature;
	if (Params->TryGetNumberField(TEXT("temperature"), Temperature))
	{
		LC->bUseTemperature = true;
		LC->SetTemperature((float)Temperature);
	}

	if (URectLightComponent* RL = Cast<URectLightComponent>(LC))
	{
		double Val;
		if (Params->TryGetNumberField(TEXT("source_width"), Val))    RL->SetSourceWidth((float)Val);
		if (Params->TryGetNumberField(TEXT("source_height"), Val))   RL->SetSourceHeight((float)Val);
		if (Params->TryGetNumberField(TEXT("barn_door_angle"), Val)) RL->SetBarnDoorAngle((float)Val);
		if (Params->TryGetNumberField(TEXT("barn_door_length"), Val)) RL->SetBarnDoorLength((float)Val);
	}

	if (USkyLightComponent* SkyL = Actor->FindComponentByClass<USkyLightComponent>())
	{
		FString SourceType;
		if (Params->TryGetStringField(TEXT("source_type"), SourceType))
		{
			if (SourceType.Equals(TEXT("CapturedScene"), ESearchCase::IgnoreCase))
				SkyL->SourceType = SLS_CapturedScene;
			else if (SourceType.Equals(TEXT("SpecifiedCubemap"), ESearchCase::IgnoreCase))
				SkyL->SourceType = SLS_SpecifiedCubemap;
		}
	}

	if (USpotLightComponent* SpotL = Cast<USpotLightComponent>(LC))
	{
		double Val;
		if (Params->TryGetNumberField(TEXT("inner_cone_angle"), Val)) SpotL->SetInnerConeAngle((float)Val);
		if (Params->TryGetNumberField(TEXT("outer_cone_angle"), Val)) SpotL->SetOuterConeAngle((float)Val);
	}

	Actor->MarkPackageDirty();
	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"actor\":\"%s\"}"), *ActorLabel);
}

void HandleAddActorTag(const FString& ActorLabel, const FString& Tag, FString& OutJsonString, FString& OutError)
{
	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World) { OutError = TEXT("No editor world"); return; }

	AActor* Actor = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
		if (*It && (*It)->GetActorLabel() == ActorLabel) { Actor = *It; break; }
	if (!Actor) { OutError = FString::Printf(TEXT("Actor '%s' not found"), *ActorLabel); return; }

	Actor->Tags.AddUnique(FName(*Tag));
	Actor->MarkPackageDirty();
	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"actor\":\"%s\",\"tag\":\"%s\",\"tag_count\":%d}"),
		*ActorLabel, *Tag, Actor->Tags.Num());
}

void HandleRemoveActorTag(const FString& ActorLabel, const FString& Tag, FString& OutJsonString, FString& OutError)
{
	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World) { OutError = TEXT("No editor world"); return; }

	AActor* Actor = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
		if (*It && (*It)->GetActorLabel() == ActorLabel) { Actor = *It; break; }
	if (!Actor) { OutError = FString::Printf(TEXT("Actor '%s' not found"), *ActorLabel); return; }

	int32 Removed = Actor->Tags.Remove(FName(*Tag));
	Actor->MarkPackageDirty();
	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"actor\":\"%s\",\"tag\":\"%s\",\"removed\":%d,\"remaining_tags\":%d}"),
		*ActorLabel, *Tag, Removed, Actor->Tags.Num());
}

void HandleGetActorTags(const FString& ActorLabel, FString& OutJsonString, FString& OutError)
{
	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World) { OutError = TEXT("No editor world"); return; }

	AActor* Actor = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
		if (*It && (*It)->GetActorLabel() == ActorLabel) { Actor = *It; break; }
	if (!Actor) { OutError = FString::Printf(TEXT("Actor '%s' not found"), *ActorLabel); return; }

	TArray<TSharedPtr<FJsonValue>> TagArr;
	for (const FName& T : Actor->Tags)
		TagArr.Add(MakeShareable(new FJsonValueString(T.ToString())));

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("label"), ActorLabel);
	Result->SetArrayField(TEXT("tags"), TagArr);
	Result->SetNumberField(TEXT("count"), TagArr.Num());
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
}

void HandleSetWorldSettings(const TSharedPtr<FJsonObject>& Params, FString& OutJsonString, FString& OutError)
{
	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World) { OutError = TEXT("No editor world"); return; }

	AWorldSettings* WS = World->GetWorldSettings();
	if (!WS) { OutError = TEXT("No WorldSettings found"); return; }

	double GravityZ;
	if (Params->TryGetNumberField(TEXT("gravity_z"), GravityZ))
	{
		WS->GlobalGravityZ = (float)GravityZ;
		WS->bGlobalGravitySet = true;
	}
	double KillZ;
	if (Params->TryGetNumberField(TEXT("kill_z"), KillZ))
		WS->KillZ = (float)KillZ;

	FString GameModeClass;
	if (Params->TryGetStringField(TEXT("default_game_mode"), GameModeClass))
	{
		UClass* GMClass = LoadObject<UClass>(nullptr, *GameModeClass);
		if (GMClass) WS->DefaultGameMode = GMClass;
	}
	WS->MarkPackageDirty();
	OutJsonString = TEXT("{\"success\":true,\"message\":\"World settings updated\"}");
}

void HandleFocusViewportOnActor(const FString& ActorLabel, FString& OutJsonString, FString& OutError)
{
	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World) { OutError = TEXT("No editor world"); return; }

	AActor* Actor = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
		if (*It && (*It)->GetActorLabel() == ActorLabel) { Actor = *It; break; }
	if (!Actor) { OutError = FString::Printf(TEXT("Actor '%s' not found"), *ActorLabel); return; }

	GEditor->MoveViewportCamerasToActor(*Actor, false);
	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"actor\":\"%s\"}"), *ActorLabel);
}

void HandleReplaceActor(const FString& ActorLabel, const FString& NewClassOrMesh,
	FString& OutJsonString, FString& OutError)
{
	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World) { OutError = TEXT("No editor world"); return; }

	AActor* OldActor = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
		if (*It && (*It)->GetActorLabel() == ActorLabel) { OldActor = *It; break; }
	if (!OldActor) { OutError = FString::Printf(TEXT("Actor '%s' not found"), *ActorLabel); return; }

	FTransform OldTransform = OldActor->GetActorTransform();
	FString OldLabel = OldActor->GetActorLabel();
	TArray<FName> OldTags = OldActor->Tags;
	FName OldFolder = OldActor->GetFolderPath();

	if (GEditor)
	{
		if (USelection* SelActors = GEditor->GetSelectedActors(); SelActors && SelActors->IsSelected(OldActor))
			GEditor->SelectActor(OldActor, false, false);
		if (USelection* SelComps = GEditor->GetSelectedComponents())
			SelComps->DeselectAll();
	}

	UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *NewClassOrMesh);
	if (Mesh)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AStaticMeshActor* NewActor = World->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), OldTransform, Params);
		if (NewActor)
		{
			NewActor->GetStaticMeshComponent()->SetStaticMesh(Mesh);
			NewActor->SetActorLabel(OldLabel);
			NewActor->Tags = OldTags;
			NewActor->SetFolderPath(OldFolder);
		}
		World->DestroyActor(OldActor);
		GEditor->NoteSelectionChange();
		OutJsonString = FString::Printf(
			TEXT("{\"success\":true,\"actor\":\"%s\",\"new_class\":\"StaticMeshActor\",\"mesh\":\"%s\"}"),
			*OldLabel, *NewClassOrMesh);
		return;
	}

	UClass* NewClass = LoadObject<UClass>(nullptr, *NewClassOrMesh);
	if (!NewClass)
	{
		for (TObjectIterator<UClass> It; It; ++It)
		{
			if (It->GetName().Equals(NewClassOrMesh, ESearchCase::IgnoreCase) ||
				It->GetName().Equals(NewClassOrMesh + TEXT("_C"), ESearchCase::IgnoreCase))
			{ NewClass = *It; break; }
		}
	}
	if (!NewClass) { OutError = FString::Printf(TEXT("Class or mesh '%s' not found"), *NewClassOrMesh); return; }

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AActor* NewActor = World->SpawnActor<AActor>(NewClass, OldTransform, SpawnParams);
	if (!NewActor) { OutError = TEXT("Failed to spawn replacement actor"); return; }

	NewActor->SetActorLabel(OldLabel);
	NewActor->Tags = OldTags;
	NewActor->SetFolderPath(OldFolder);
	World->DestroyActor(OldActor);
	GEditor->NoteSelectionChange();
	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"actor\":\"%s\",\"new_class\":\"%s\"}"),
		*OldLabel, *NewClassOrMesh);
}

void HandleSetActorReplication(
	const FString& ActorLabel,
	const FString& Replicate,
	const FString& ReplicateMovement,
	float NetUpdateFrequency,
	float NetCullDistance,
	FString& OutJsonString, FString& OutError)
{

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World) { OutError = TEXT("No editor world"); return; }

	AActor* Actor = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->GetActorLabel().Equals(ActorLabel, ESearchCase::IgnoreCase))
		{
			Actor = *It;
			break;
		}
	}
	if (!Actor) { OutError = FString::Printf(TEXT("Actor '%s' not found in level"), *ActorLabel); return; }

	if (!Replicate.IsEmpty())
	{
		bool bReplicate = Replicate.Equals(TEXT("true"), ESearchCase::IgnoreCase) ||
			Replicate.Equals(TEXT("1"), ESearchCase::IgnoreCase) ||
			Replicate.Equals(TEXT("yes"), ESearchCase::IgnoreCase);
		Actor->SetReplicates(bReplicate);
	}

	if (!ReplicateMovement.IsEmpty())
	{
		bool bRepMove = ReplicateMovement.Equals(TEXT("true"), ESearchCase::IgnoreCase) ||
			ReplicateMovement.Equals(TEXT("1"), ESearchCase::IgnoreCase) ||
			ReplicateMovement.Equals(TEXT("yes"), ESearchCase::IgnoreCase);
		Actor->SetReplicateMovement(bRepMove);
	}

#if UE_VERSION_OLDER_THAN(5, 5, 0)
	if (NetUpdateFrequency >= 0.f) Actor->NetUpdateFrequency = NetUpdateFrequency;
	if (NetCullDistance >= 0.f)
		Actor->NetCullDistanceSquared = NetCullDistance * NetCullDistance;
#else
	if (NetUpdateFrequency >= 0.f) Actor->SetNetUpdateFrequency(NetUpdateFrequency);
	if (NetCullDistance >= 0.f)
		Actor->SetNetCullDistanceSquared(NetCullDistance * NetCullDistance);
#endif

	Actor->MarkPackageDirty();

#if UE_VERSION_OLDER_THAN(5, 5, 0)
	const float ReportedNetUpdateFreq = Actor->NetUpdateFrequency;
#else
	const float ReportedNetUpdateFreq = Actor->GetNetUpdateFrequency();
#endif
	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"actor\":\"%s\",\"replicates\":%s,\"net_update_frequency\":%.1f}"),
		*ActorLabel,
		Actor->GetIsReplicated() ? TEXT("true") : TEXT("false"),
		ReportedNetUpdateFreq);
}

void HandleFindActorsByBounds(const FVector& Center, const FVector& Extent,
	FString& OutJsonString, FString& OutError)
{
	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World) { OutError = TEXT("No editor world"); return; }

	FBox QueryBox(Center - Extent, Center + Extent);
	TArray<TSharedPtr<FJsonValue>> ResultArray;

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor) continue;

		FVector Origin, BoxExtent;
		Actor->GetActorBounds(false, Origin, BoxExtent);
		FBox ActorBox(Origin - BoxExtent, Origin + BoxExtent);

		if (QueryBox.Intersect(ActorBox))
		{
			TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
			Obj->SetStringField(TEXT("label"), Actor->GetActorLabel());
			Obj->SetStringField(TEXT("class"), Actor->GetClass()->GetName());
			TArray<TSharedPtr<FJsonValue>> LocArr;
			FVector Loc = Actor->GetActorLocation();
			LocArr.Add(MakeShareable(new FJsonValueNumber(Loc.X)));
			LocArr.Add(MakeShareable(new FJsonValueNumber(Loc.Y)));
			LocArr.Add(MakeShareable(new FJsonValueNumber(Loc.Z)));
			Obj->SetArrayField(TEXT("location"), LocArr);
			ResultArray.Add(MakeShareable(new FJsonValueObject(Obj)));
		}
	}

	TSharedPtr<FJsonObject> Root = MakeShareable(new FJsonObject);
	Root->SetBoolField(TEXT("success"), true);
	Root->SetArrayField(TEXT("actors"), ResultArray);
	Root->SetNumberField(TEXT("count"), ResultArray.Num());
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);
}

void HandleBeginPlayInEditor(FString& OutJsonString, FString& OutError)
{
	{
		bool bSurgicalNoPIE = false;
		GConfig->GetBool(TEXT("BpGeneratorUltimate"), TEXT("SurgicalNoPIE"), bSurgicalNoPIE, FSettingsManager::GetGlobalConfigPath());
		if (bSurgicalNoPIE)
		{
			OutJsonString = TEXT("{\"success\":false,\"message\":\"Play In Editor is disabled by Surgical (no-PIE) mode. Make the graph/asset edit directly, or report findings — don't start a play session. Turn off Surgical mode in Settings to allow PIE.\"}");
			return;
		}
	}

	if (!GEditor) { OutError = TEXT("GEditor not available"); return; }
	if (GEditor->PlayWorld)
	{
		OutJsonString = TEXT("{\"success\":false,\"message\":\"PIE session already running\"}");
		return;
	}

	TArray<TSharedPtr<FJsonValue>> ErroredBPs;
	for (TObjectIterator<UBlueprint> It; It; ++It)
	{
		UBlueprint* BP = *It;
		if (!IsValid(BP) || !BP->GetPathName().StartsWith(TEXT("/Game"))) continue;
		if (BP->Status == BS_Dirty || BP->Status == BS_Unknown)
		{
			FKismetEditorUtilities::CompileBlueprint(BP);
		}
		if (BP->Status == BS_Error)
		{
			ErroredBPs.Add(MakeShareable(new FJsonValueString(BP->GetPathName())));
		}
	}
	if (ErroredBPs.Num() > 0)
	{
		TSharedPtr<FJsonObject> R = MakeShareable(new FJsonObject);
		R->SetBoolField(TEXT("success"), false);
		R->SetStringField(TEXT("error"), TEXT("PIE not started: Blueprint(s) have compile errors and would block on a 'Play anyway?' modal the caller can't answer. Fix them (validate_blueprint / compile_blueprint for details), then retry."));
		R->SetArrayField(TEXT("blueprints_with_errors"), ErroredBPs);
		TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJsonString);
		FJsonSerializer::Serialize(R.ToSharedRef(), W);
		return;
	}

	FRequestPlaySessionParams Params;
	Params.WorldType = EPlaySessionWorldType::PlayInEditor;
	GEditor->RequestPlaySession(Params);
	OutJsonString = TEXT("{\"success\":true,\"message\":\"PIE session starting\"}");
}

void HandleStopPlayInEditor(FString& OutJsonString, FString& OutError)
{
	if (!GEditor) { OutError = TEXT("GEditor not available"); return; }
	if (!GEditor->PlayWorld)
	{
		OutJsonString = TEXT("{\"success\":false,\"message\":\"No PIE session is running\"}");
		return;
	}
	GEditor->RequestEndPlayMap();
	OutJsonString = TEXT("{\"success\":true,\"message\":\"PIE session stopping\"}");
}

void HandleGetAssetReferences(const FString& AssetPath, FString& OutJsonString, FString& OutError)
{
	FAssetRegistryModule& RegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	IAssetRegistry& Registry = RegistryModule.Get();

	FString PkgPath = AssetPath;
	if (PkgPath.Contains(TEXT(".")))
		PkgPath = FPackageName::ObjectPathToPackageName(PkgPath);

	TArray<FName> Referencers;
	Registry.GetReferencers(FName(*PkgPath), Referencers);

	TArray<TSharedPtr<FJsonValue>> RefArray;
	for (const FName& Ref : Referencers)
	{
		const FString RefStr = Ref.ToString();
		if (!RefStr.StartsWith(TEXT("/Game/"))) continue;

		TArray<FAssetData> PkgAssets;
		Registry.GetAssetsByPackageName(Ref, PkgAssets);
		if (PkgAssets.Num() > 0)
		{
			const FAssetData& A = PkgAssets[0];
			TSharedPtr<FJsonObject> Entry = MakeShareable(new FJsonObject);
			Entry->SetStringField(TEXT("name"), A.AssetName.ToString());
			Entry->SetStringField(TEXT("path"), A.GetObjectPathString());
			Entry->SetStringField(TEXT("asset_type"), A.AssetClassPath.GetAssetName().ToString());
			RefArray.Add(MakeShareable(new FJsonValueObject(Entry)));
		}
		else
		{
			TSharedPtr<FJsonObject> Entry = MakeShareable(new FJsonObject);
			Entry->SetStringField(TEXT("name"), FPaths::GetBaseFilename(RefStr));
			Entry->SetStringField(TEXT("path"), RefStr);
			Entry->SetStringField(TEXT("asset_type"), TEXT("unknown"));
			RefArray.Add(MakeShareable(new FJsonValueObject(Entry)));
		}
	}

	TSharedPtr<FJsonObject> Root = MakeShareable(new FJsonObject);
	Root->SetBoolField(TEXT("success"), true);
	Root->SetStringField(TEXT("asset_path"), AssetPath);
	Root->SetArrayField(TEXT("referencers"), RefArray);
	Root->SetNumberField(TEXT("count"), RefArray.Num());
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);
}

void HandleCreateDataLayer(const FString& LayerName, FString& OutJsonString, FString& OutError)
{
	UDataLayerEditorSubsystem* DLES = GEditor ? GEditor->GetEditorSubsystem<UDataLayerEditorSubsystem>() : nullptr;
	if (!DLES) { OutError = TEXT("DataLayerEditorSubsystem not available (requires World Partition)"); return; }

	FDataLayerCreationParameters Params;
	UDataLayerInstance* Instance = DLES->CreateDataLayerInstance(Params);
	if (!Instance) { OutError = TEXT("Failed to create data layer — ensure World Partition is enabled for this level"); return; }
	DLES->SetDataLayerShortName(Instance, LayerName);

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"layer_name\":\"%s\",\"message\":\"Data layer created\"}"), *LayerName);
}

void HandleAssignActorToDataLayer(const FString& ActorLabel, const FString& LayerName, FString& OutJsonString, FString& OutError)
{
	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World) { OutError = TEXT("No editor world"); return; }

	AActor* Actor = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
		if (*It && (*It)->GetActorLabel().Equals(ActorLabel, ESearchCase::IgnoreCase)) { Actor = *It; break; }
	if (!Actor) { OutError = FString::Printf(TEXT("Actor '%s' not found"), *ActorLabel); return; }

	UDataLayerEditorSubsystem* DLES = GEditor->GetEditorSubsystem<UDataLayerEditorSubsystem>();
	if (!DLES) { OutError = TEXT("DataLayerEditorSubsystem not available"); return; }

	UDataLayerInstance* TargetLayer = DLES->GetDataLayerInstance(FName(*LayerName));
	if (!TargetLayer) { OutError = FString::Printf(TEXT("Data layer '%s' not found"), *LayerName); return; }

	bool bOk = DLES->AddActorToDataLayer(Actor, TargetLayer);
	if (!bOk) { OutError = TEXT("Failed to assign actor to data layer"); return; }

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"actor\":\"%s\",\"layer\":\"%s\"}"), *ActorLabel, *LayerName);
}

void HandleSetDataLayerState(const FString& LayerName, const FString& State, FString& OutJsonString, FString& OutError)
{
	UDataLayerEditorSubsystem* DLES = GEditor ? GEditor->GetEditorSubsystem<UDataLayerEditorSubsystem>() : nullptr;
	if (!DLES) { OutError = TEXT("DataLayerEditorSubsystem not available"); return; }

	UDataLayerInstance* TargetLayer = DLES->GetDataLayerInstance(FName(*LayerName));
	if (!TargetLayer) { OutError = FString::Printf(TEXT("Data layer '%s' not found"), *LayerName); return; }

	EDataLayerRuntimeState RuntimeState = EDataLayerRuntimeState::Unloaded;
	if (State.Equals(TEXT("loaded"), ESearchCase::IgnoreCase))
		RuntimeState = EDataLayerRuntimeState::Loaded;
	else if (State.Equals(TEXT("activated"), ESearchCase::IgnoreCase))
		RuntimeState = EDataLayerRuntimeState::Activated;

	FByteProperty* StateProp = CastField<FByteProperty>(
		FindFProperty<FProperty>(UDataLayerInstance::StaticClass(), TEXT("InitialRuntimeState")));
	if (StateProp)
	{
		TargetLayer->Modify();
		StateProp->SetPropertyValue_InContainer(TargetLayer, (uint8)RuntimeState);
		TargetLayer->MarkPackageDirty();
	}

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"layer\":\"%s\",\"state\":\"%s\"}"), *LayerName, *State);
}

void HandleListDataLayers(FString& OutJsonString, FString& OutError)
{
	UDataLayerEditorSubsystem* DLES = GEditor ? GEditor->GetEditorSubsystem<UDataLayerEditorSubsystem>() : nullptr;
	if (!DLES) { OutError = TEXT("DataLayerEditorSubsystem not available (requires World Partition)"); return; }

	TSharedPtr<FJsonObject> Res = MakeShareable(new FJsonObject());
	Res->SetBoolField(TEXT("success"), true);

	TArray<TSharedPtr<FJsonValue>> LayerArray;

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World) { OutError = TEXT("No editor world available"); return; }

	TArray<UDataLayerInstance*> AllLayers;
	UDataLayerManager* DLM = UDataLayerManager::GetDataLayerManager(World);
	if (DLM)
	{
		DLM->ForEachDataLayerInstance([&AllLayers](UDataLayerInstance* Instance)
		{
			if (Instance) AllLayers.Add(Instance);
			return true;
		});
	}
	if (AllLayers.IsEmpty())
	{
		OutJsonString = TEXT("{\"success\":true,\"data_layers\":[],\"count\":0,\"note\":\"No data layers found. Ensure World Partition is enabled.\"}");
		TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJsonString);
		return;
	}

	for (UDataLayerInstance* Layer : AllLayers)
	{
		TSharedPtr<FJsonObject> LayerObj = MakeShareable(new FJsonObject());
		LayerObj->SetStringField(TEXT("name"), Layer->GetDataLayerShortName());
		LayerObj->SetStringField(TEXT("full_name"), Layer->GetDataLayerFullName());

		FProperty* StateProp = FindFProperty<FProperty>(UDataLayerInstance::StaticClass(), TEXT("InitialRuntimeState"));
		if (StateProp)
		{
			FString StateStr;
			StateProp->ExportTextItem_Direct(StateStr, StateProp->ContainerPtrToValuePtr<void>(Layer), nullptr, Layer, PPF_None);
			LayerObj->SetStringField(TEXT("initial_runtime_state"), StateStr);
		}

		LayerArray.Add(MakeShareable(new FJsonValueObject(LayerObj)));
	}

	Res->SetArrayField(TEXT("data_layers"), LayerArray);
	Res->SetNumberField(TEXT("count"), LayerArray.Num());

	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Res.ToSharedRef(), W);
}

void HandleGetDataLayerActors(const FString& LayerName, FString& OutJsonString, FString& OutError)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World) { OutError = TEXT("No editor world"); return; }

	UDataLayerEditorSubsystem* DLES = GEditor->GetEditorSubsystem<UDataLayerEditorSubsystem>();
	if (!DLES) { OutError = TEXT("DataLayerEditorSubsystem not available"); return; }

	UDataLayerInstance* TargetLayer = DLES->GetDataLayerInstance(FName(*LayerName));
	if (!TargetLayer) { OutError = FString::Printf(TEXT("Data layer '%s' not found"), *LayerName); return; }

	TSharedPtr<FJsonObject> Res = MakeShareable(new FJsonObject());
	Res->SetBoolField(TEXT("success"), true);
	Res->SetStringField(TEXT("layer_name"), LayerName);

	TArray<TSharedPtr<FJsonValue>> ActorArray;
	TArray<AActor*> Actors = DLES->GetActorsFromDataLayer(TargetLayer);
	for (AActor* Actor : Actors)
	{
		if (!Actor) continue;
		TSharedPtr<FJsonObject> ActorObj = MakeShareable(new FJsonObject());
		ActorObj->SetStringField(TEXT("label"), Actor->GetActorLabel());
		ActorObj->SetStringField(TEXT("class"), Actor->GetClass()->GetName());
		FVector Loc = Actor->GetActorLocation();
		ActorObj->SetStringField(TEXT("location"), FString::Printf(TEXT("(%.1f, %.1f, %.1f)"), Loc.X, Loc.Y, Loc.Z));
		ActorArray.Add(MakeShareable(new FJsonValueObject(ActorObj)));
	}

	Res->SetArrayField(TEXT("actors"), ActorArray);
	Res->SetNumberField(TEXT("count"), ActorArray.Num());

	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Res.ToSharedRef(), W);
}

void HandleEnableWorldPartition(FString& OutJsonString, FString& OutError)
{
#if WITH_EDITOR
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World) { OutError = TEXT("No editor world"); return; }
	AWorldSettings* WS = World->GetWorldSettings();
	if (!WS) { OutError = TEXT("No WorldSettings found"); return; }

	UWorldPartition* WP = UWorldPartition::CreateOrRepairWorldPartition(WS);
	if (!WP) { OutError = TEXT("Failed to enable World Partition. Ensure the level is saved first."); return; }

	OutJsonString = TEXT("{\"success\":true,\"message\":\"World Partition enabled. Save the level to persist.\"}");
#else
	OutError = TEXT("World Partition APIs are editor-only");
#endif
}

void HandleDisableWorldPartition(FString& OutJsonString, FString& OutError)
{
#if WITH_EDITOR
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World) { OutError = TEXT("No editor world"); return; }
	AWorldSettings* WS = World->GetWorldSettings();
	if (!WS) { OutError = TEXT("No WorldSettings found"); return; }

	bool bOk = UWorldPartition::RemoveWorldPartition(WS);
	if (!bOk) { OutError = TEXT("Failed to remove World Partition (may not be enabled, or level not saved)."); return; }

	OutJsonString = TEXT("{\"success\":true,\"message\":\"World Partition removed. WARNING: This is destructive — all WP data (data layers, HLOD, streaming) has been removed.\"}");
#else
	OutError = TEXT("World Partition APIs are editor-only");
#endif
}

void HandleGetWorldPartitionInfo(FString& OutJsonString, FString& OutError)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World) { OutError = TEXT("No editor world"); return; }
	AWorldSettings* WS = World->GetWorldSettings();
	if (!WS) { OutError = TEXT("No WorldSettings found"); return; }

	UWorldPartition* WP = WS->GetWorldPartition();
	bool bEnabled = (WP != nullptr);

	int32 DataLayerCount = 0;
	UDataLayerManager* DLM = UDataLayerManager::GetDataLayerManager(World);
	if (DLM)
	{
		DLM->ForEachDataLayerInstance([&DataLayerCount](UDataLayerInstance* Instance)
		{
			if (Instance) DataLayerCount++;
			return true;
		});
	}

	FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	TArray<FAssetData> HLODAssets;
	ARM.Get().GetAssetsByClass(UHLODLayer::StaticClass()->GetClassPathName(), HLODAssets);

	TSharedPtr<FJsonObject> Res = MakeShareable(new FJsonObject());
	Res->SetBoolField(TEXT("success"), true);
	Res->SetBoolField(TEXT("world_partition_enabled"), bEnabled);
	Res->SetNumberField(TEXT("data_layer_count"), DataLayerCount);
	Res->SetNumberField(TEXT("hlod_layer_count"), HLODAssets.Num());
	if (bEnabled) Res->SetStringField(TEXT("world_name"), World->GetName());

	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Res.ToSharedRef(), W);
}

static EHLODLayerType ParseHLODLayerType(const FString& LayerType)
{
	if (LayerType.Equals(TEXT("MeshMerge"), ESearchCase::IgnoreCase))     return EHLODLayerType::MeshMerge;
	if (LayerType.Equals(TEXT("MeshSimplify"), ESearchCase::IgnoreCase))  return EHLODLayerType::MeshSimplify;
	if (LayerType.Equals(TEXT("MeshApproximate"), ESearchCase::IgnoreCase)) return EHLODLayerType::MeshApproximate;
	if (LayerType.Equals(TEXT("Custom"), ESearchCase::IgnoreCase))         return EHLODLayerType::Custom;
	return EHLODLayerType::Instancing;
}

static void ApplyHLODLayerSettings(UHLODLayer* HLODLayer, const FString& LayerType, int32 CellSize)
{
	if (!LayerType.IsEmpty())
		HLODLayer->SetLayerType(ParseHLODLayerType(LayerType));

	if (CellSize > 0)
	{
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		HLODLayer->SetIsSpatiallyLoaded(true);
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
		if (FIntProperty* CSProp = CastField<FIntProperty>(
			FindFProperty<FProperty>(UHLODLayer::StaticClass(), TEXT("CellSize"))))
		{
			HLODLayer->Modify();
			CSProp->SetPropertyValue_InContainer(HLODLayer, CellSize);
		}
	}
}

void HandleCreateHLODLayer(const FString& Name, const FString& SavePath,
	const FString& LayerType, int32 CellSize,
	FString& OutJsonString, FString& OutError)
{
	if (Name.IsEmpty() || SavePath.IsEmpty()) { OutError = TEXT("name and save_path are required"); return; }

	FString PathStr = SavePath;
	while (PathStr.EndsWith(TEXT("/"))) PathStr = PathStr.LeftChop(1);
	FString PackagePath = FString::Printf(TEXT("%s/%s"), *PathStr, *Name);

	UPackage* Package = CreatePackage(*PackagePath);
	if (!Package) { OutError = FString::Printf(TEXT("Failed to create package at %s"), *PackagePath); return; }

	UHLODLayer* HLODLayer = NewObject<UHLODLayer>(Package, *Name, RF_Public | RF_Standalone | RF_Transactional);
	if (!HLODLayer) { OutError = TEXT("Failed to create UHLODLayer object"); return; }

	ApplyHLODLayerSettings(HLODLayer, LayerType.IsEmpty() ? TEXT("Instancing") : LayerType, CellSize);

	FAssetRegistryModule::AssetCreated(HLODLayer);
	Package->MarkPackageDirty();

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	FString PackageFilename = FPackageName::LongPackageNameToFilename(PackagePath, FPackageName::GetAssetPackageExtension());
	UPackage::SavePackage(Package, HLODLayer, *PackageFilename, SaveArgs);

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"asset_path\":\"%s\",\"layer_type\":\"%s\",\"cell_size\":%d}"),
		*PackagePath, *LayerType, CellSize);
}

void HandleSetHLODLayerProperties(const FString& AssetPath,
	const FString& LayerType, int32 CellSize,
	FString& OutJsonString, FString& OutError)
{
	if (AssetPath.IsEmpty()) { OutError = TEXT("asset_path is required"); return; }

	UHLODLayer* HLODLayer = LoadObject<UHLODLayer>(nullptr, *AssetPath);
	if (!HLODLayer) { OutError = FString::Printf(TEXT("UHLODLayer not found: %s"), *AssetPath); return; }

	HLODLayer->Modify();
	ApplyHLODLayerSettings(HLODLayer, LayerType, CellSize);
	HLODLayer->MarkPackageDirty();

	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"asset_path\":\"%s\"}"), *AssetPath);
}

void HandleListHLODLayers(FString& OutJsonString, FString& OutError)
{
	FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	TArray<FAssetData> HLODAssets;
	ARM.Get().GetAssetsByClass(UHLODLayer::StaticClass()->GetClassPathName(), HLODAssets);

	TSharedPtr<FJsonObject> Res = MakeShareable(new FJsonObject());
	Res->SetBoolField(TEXT("success"), true);

	TArray<TSharedPtr<FJsonValue>> LayerArray;
	for (const FAssetData& AD : HLODAssets)
	{
		TSharedPtr<FJsonObject> LayerObj = MakeShareable(new FJsonObject());
		LayerObj->SetStringField(TEXT("asset_path"), AD.GetObjectPathString());
		LayerObj->SetStringField(TEXT("name"), AD.AssetName.ToString());
		LayerArray.Add(MakeShareable(new FJsonValueObject(LayerObj)));
	}
	Res->SetArrayField(TEXT("hlod_layers"), LayerArray);
	Res->SetNumberField(TEXT("count"), LayerArray.Num());

	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Res.ToSharedRef(), W);
}

void HandleSetCineCameraProperties(
	const FString& ActorLabel,
	float FocalLength,
	float Aperture,
	float FocusDistance,
	float FilmbackWidth,
	float FilmbackHeight,
	FString& OutJsonString, FString& OutError)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World) { OutError = TEXT("No editor world"); return; }

	ACineCameraActor* CamActor = nullptr;
	for (TActorIterator<ACineCameraActor> It(World); It; ++It)
		if ((*It)->GetActorLabel().Equals(ActorLabel, ESearchCase::IgnoreCase)) { CamActor = *It; break; }
	if (!CamActor) { OutError = FString::Printf(TEXT("CineCameraActor '%s' not found"), *ActorLabel); return; }

	UCineCameraComponent* Cam = CamActor->GetCineCameraComponent();
	if (!Cam) { OutError = TEXT("No CineCameraComponent on actor"); return; }

	if (FocalLength > 0.f)    Cam->CurrentFocalLength = FocalLength;
	if (Aperture > 0.f)       Cam->CurrentAperture = Aperture;
	if (FocusDistance > 0.f)
	{
		Cam->FocusSettings.FocusMethod = ECameraFocusMethod::Manual;
		Cam->FocusSettings.ManualFocusDistance = FocusDistance;
	}
	if (FilmbackWidth > 0.f)  Cam->Filmback.SensorWidth = FilmbackWidth;
	if (FilmbackHeight > 0.f) Cam->Filmback.SensorHeight = FilmbackHeight;
	CamActor->MarkPackageDirty();

	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"actor_label\":\"%s\"}"), *ActorLabel);
}

void HandleCreateLevelInstance(const FString& ActorLabel, const FString& LevelPath,
	float LocationX, float LocationY, float LocationZ,
	FString& OutJsonString, FString& OutError)
{

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World) { OutError = TEXT("No editor world available"); return; }

	UClass* LIClass = FindObject<UClass>(nullptr, TEXT("/Script/Engine.LevelInstance"));
	if (!LIClass) LIClass = UClass::TryFindTypeSlow<UClass>(TEXT("ALevelInstance"));
	if (!LIClass)
	{
		LIClass = FindObject<UClass>(nullptr, TEXT("/Script/Engine.PackedLevelActor"));
		if (!LIClass) LIClass = UClass::TryFindTypeSlow<UClass>(TEXT("APackedLevelActor"));
	}
	if (!LIClass)
	{
		OutError = TEXT("LevelInstance/PackedLevelActor class not found. This requires UE5.1+ with World Partition.");
		return;
	}

	FVector Location(LocationX, LocationY, LocationZ);
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AActor* NewActor = World->SpawnActor(LIClass, &Location, nullptr, SpawnParams);
	if (!NewActor) { OutError = TEXT("Failed to spawn LevelInstance actor"); return; }

	if (!ActorLabel.IsEmpty())
		NewActor->SetActorLabel(ActorLabel);

	if (!LevelPath.IsEmpty())
	{
		FProperty* WorldAssetProp = NewActor->GetClass()->FindPropertyByName(TEXT("WorldAsset"));
		if (WorldAssetProp)
		{
			void* ValPtr = WorldAssetProp->ContainerPtrToValuePtr<void>(NewActor);
			WorldAssetProp->ImportText_Direct(*LevelPath, ValPtr, NewActor, PPF_None);
		}
	}

	NewActor->MarkPackageDirty();
	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"actor_label\":\"%s\",\"class\":\"%s\",\"level_path\":\"%s\"}"),
		*NewActor->GetActorLabel(), *NewActor->GetClass()->GetName(), *LevelPath);
}

void HandleGetLevelInstances(FString& OutJsonString, FString& OutError)
{

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World) { OutError = TEXT("No editor world available"); return; }

	TSharedPtr<FJsonObject> Res = MakeShareable(new FJsonObject());
	Res->SetBoolField(TEXT("success"), true);

	TArray<TSharedPtr<FJsonValue>> InstanceArray;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		FString ClassName = Actor->GetClass()->GetName();
		if (ClassName.Contains(TEXT("LevelInstance")) || ClassName.Contains(TEXT("PackedLevel")))
		{
			TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject());
			Obj->SetStringField(TEXT("label"), Actor->GetActorLabel());
			Obj->SetStringField(TEXT("class"), ClassName);
			FVector Loc = Actor->GetActorLocation();
			Obj->SetStringField(TEXT("location"), FString::Printf(TEXT("(%.1f, %.1f, %.1f)"), Loc.X, Loc.Y, Loc.Z));

			FProperty* WorldAssetProp = Actor->GetClass()->FindPropertyByName(TEXT("WorldAsset"));
			if (WorldAssetProp)
			{
				FString ValStr;
				WorldAssetProp->ExportTextItem_Direct(ValStr, WorldAssetProp->ContainerPtrToValuePtr<void>(Actor), nullptr, Actor, PPF_None);
				if (!ValStr.IsEmpty()) Obj->SetStringField(TEXT("world_asset"), ValStr);
			}

			InstanceArray.Add(MakeShareable(new FJsonValueObject(Obj)));
		}
	}

	Res->SetArrayField(TEXT("level_instances"), InstanceArray);
	Res->SetNumberField(TEXT("count"), InstanceArray.Num());

	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Res.ToSharedRef(), W);
}

static bool ParseTripleString(const FString& In, float& OutA, const TCHAR* NameA, float& OutB, const TCHAR* NameB, float& OutC, const TCHAR* NameC)
{
	FString S = In;
	S.TrimStartAndEndInline();
	if (S.IsEmpty()) return false;
	if (S.Len() >= 2 && (S[0] == TEXT('(') || S[0] == TEXT('{')) &&
	    (S[S.Len() - 1] == TEXT(')') || S[S.Len() - 1] == TEXT('}')))
	{
		S = S.Mid(1, S.Len() - 2);
		S.TrimStartAndEndInline();
	}
	TArray<FString> Parts;
	S.ParseIntoArray(Parts, TEXT(","));
	if (Parts.Num() < 3) return false;

	bool bAnyNamed = false;
	for (const FString& P : Parts) if (P.Contains(TEXT("="))) { bAnyNamed = true; break; }

	if (bAnyNamed)
	{
		auto Lookup = [&Parts](const TCHAR* Name, float& Out) -> bool
		{
			for (const FString& P : Parts)
			{
				FString L, R;
				if (P.Split(TEXT("="), &L, &R))
				{
					L.TrimStartAndEndInline(); R.TrimStartAndEndInline();
					if (L.Equals(Name, ESearchCase::IgnoreCase)) { Out = FCString::Atof(*R); return true; }
				}
			}
			return false;
		};
		bool A = Lookup(NameA, OutA);
		bool B = Lookup(NameB, OutB);
		bool C = Lookup(NameC, OutC);
		return A || B || C;
	}
	OutA = FCString::Atof(*Parts[0].TrimStartAndEnd());
	OutB = FCString::Atof(*Parts[1].TrimStartAndEnd());
	OutC = FCString::Atof(*Parts[2].TrimStartAndEnd());
	return true;
}

static FVector ExtractVector(const TSharedPtr<FJsonObject>& Obj, const TCHAR* FieldName, const FVector& Default)
{
	const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
	if (Obj->TryGetArrayField(FieldName, Arr) && Arr && Arr->Num() >= 3)
		return FVector((*Arr)[0]->AsNumber(), (*Arr)[1]->AsNumber(), (*Arr)[2]->AsNumber());
	const TSharedPtr<FJsonObject>* Sub = nullptr;
	if (Obj->TryGetObjectField(FieldName, Sub) && Sub)
	{
		double X = Default.X, Y = Default.Y, Z = Default.Z;
		(*Sub)->TryGetNumberField(TEXT("x"), X); (*Sub)->TryGetNumberField(TEXT("y"), Y); (*Sub)->TryGetNumberField(TEXT("z"), Z);
		return FVector(X, Y, Z);
	}
	FString StrVal;
	if (Obj->TryGetStringField(FieldName, StrVal))
	{
		float X = (float)Default.X, Y = (float)Default.Y, Z = (float)Default.Z;
		if (ParseTripleString(StrVal, X, TEXT("X"), Y, TEXT("Y"), Z, TEXT("Z")))
			return FVector(X, Y, Z);
	}
	return Default;
}

// Accepted forms: [pitch, yaw, roll] array, {pitch, yaw, roll} object, editor-style {x: roll, y: pitch, z: yaw} object, or "Pitch=..,Yaw=..,Roll=.." string.
static FRotator ExtractRotator(const TSharedPtr<FJsonObject>& Obj, const TCHAR* FieldName, const FRotator& Default)
{
	const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
	if (Obj->TryGetArrayField(FieldName, Arr) && Arr && Arr->Num() >= 3)
		return FRotator((*Arr)[0]->AsNumber(), (*Arr)[1]->AsNumber(), (*Arr)[2]->AsNumber());
	const TSharedPtr<FJsonObject>* Sub = nullptr;
	if (Obj->TryGetObjectField(FieldName, Sub) && Sub)
	{
		double P = Default.Pitch, Ya = Default.Yaw, R = Default.Roll;
		const bool bNamedAxes = (*Sub)->HasField(TEXT("pitch")) || (*Sub)->HasField(TEXT("yaw")) || (*Sub)->HasField(TEXT("roll"));
		if (bNamedAxes)
		{
			(*Sub)->TryGetNumberField(TEXT("pitch"), P); (*Sub)->TryGetNumberField(TEXT("yaw"), Ya); (*Sub)->TryGetNumberField(TEXT("roll"), R);
		}
		else
		{
			// Editor details-panel convention: X = roll, Y = pitch, Z = yaw.
			(*Sub)->TryGetNumberField(TEXT("x"), R); (*Sub)->TryGetNumberField(TEXT("y"), P); (*Sub)->TryGetNumberField(TEXT("z"), Ya);
		}
		return FRotator(P, Ya, R);
	}
	FString StrVal;
	if (Obj->TryGetStringField(FieldName, StrVal))
	{
		float P = (float)Default.Pitch, Ya = (float)Default.Yaw, R = (float)Default.Roll;
		if (ParseTripleString(StrVal, P, TEXT("Pitch"), Ya, TEXT("Yaw"), R, TEXT("Roll")))
			return FRotator(P, Ya, R);
	}
	return Default;
}

// Array-form rotations are [pitch, yaw, roll]; a large third element is almost always a yaw that landed in the roll slot.
// Returns false (and fills OutError) when |roll| > 45 and the caller did not opt in with allow_tilt:true.
static bool CheckRotationTiltGuard(const TSharedPtr<FJsonObject>& Obj, const TCHAR* FieldName, bool bAllowTilt, FString& OutError)
{
	if (bAllowTilt || !Obj.IsValid()) return true;
	const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
	if (!Obj->TryGetArrayField(FieldName, Arr) || !Arr || Arr->Num() < 3) return true;
	const double Roll = (*Arr)[2]->AsNumber();
	if (FMath::Abs(Roll) <= 45.0) return true;
	OutError = FString::Printf(
		TEXT("'%s' array is [pitch, yaw, roll] and roll=%.1f would tip the actor sideways. If you meant a yaw, pass {\"yaw\":%.1f} (or [0,%.1f,0]); to really roll it, pass allow_tilt:true."),
		FieldName, Roll, Roll, Roll);
	return false;
}

void HandleSpawnActorFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!EditorReadiness::IsSessionActive()) { OutError = TEXT("Session not ready — reload the editor and try again."); return; }
	if (!FCapabilityProfile::Get().IsEnabled(ECapability::SceneAuthoring)) { OutError = TEXT("Scene authoring isn't enabled for this install's profile yet — reconnect and try again."); return; }
	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("actors"), ItemsArray))
	{
		bool bBatchAllowTilt = false;
		Args->TryGetBoolField(TEXT("allow_tilt"), bBatchAllowTilt);

		const FScopedTransaction Transaction(NSLOCTEXT("AxivorAI", "SpawnActors", "Spawn Actors"));

		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString ActorClass = BatchToolHelper::GetItemString(Item, TEXT("actor_class"), TEXT("class"));
			if (ActorClass.IsEmpty()) Item->TryGetStringField(TEXT("class_path"), ActorClass);
			FString Label = BatchToolHelper::GetItemString(Item, TEXT("actor_label"), TEXT("label"));
			if (ActorClass.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing actor_class")); continue; }

			bool bItemAllowTilt = bBatchAllowTilt;
			Item->TryGetBoolField(TEXT("allow_tilt"), bItemAllowTilt);
			FString GuardErr;
			if (!CheckRotationTiltGuard(Item, TEXT("rotation"), bItemAllowTilt, GuardErr)) { Batch.AddFailure(i, GuardErr); continue; }

			FVector Loc = ExtractVector(Item, TEXT("location"));
			FRotator Rot = ExtractRotator(Item, TEXT("rotation"));
			FVector Scale = ExtractVector(Item, TEXT("scale"), FVector::OneVector);
			FString Err;
			AActor* Spawned = SpawnActorInLevelInternal(ActorClass, Loc, Rot, Scale, Label, Err);
			if (Spawned && Err.IsEmpty())
			{
				Spawned->Modify();
				auto Extra = MakeShared<FJsonObject>();
				Extra->SetStringField(TEXT("actor_class"), ActorClass);
				if (!Label.IsEmpty()) Extra->SetStringField(TEXT("actor_label"), Label);
				Batch.AddSuccess(i, Extra);
			}
			else Batch.AddFailure(i, Err.IsEmpty() ? FString(TEXT("Spawn failed")) : Err);
		}
		Batch.Finalize(OutJsonString);
		return;
	}
	FString ActorClass = BatchToolHelper::GetItemString(Args, TEXT("actor_class"), TEXT("class"));
	if (ActorClass.IsEmpty()) Args->TryGetStringField(TEXT("class_path"), ActorClass);
	FString Label = BatchToolHelper::GetItemString(Args, TEXT("actor_label"), TEXT("label"));
	FVector Loc = ExtractVector(Args, TEXT("location"));
	FRotator Rot = ExtractRotator(Args, TEXT("rotation"));
	FVector Scale = ExtractVector(Args, TEXT("scale"), FVector::OneVector);
	HandleSpawnActorInLevel(ActorClass, Loc, Rot, Scale, Label, OutError);
	if (OutError.IsEmpty())
		OutJsonString = FString::Printf(TEXT("{\"success\":true,\"actor_class\":\"%s\",\"actor_label\":\"%s\"}"), *ActorClass, *Label);
}

static void ExtractCCRSpawnArgs(const TSharedPtr<FJsonObject>& Args, FString& OutShape, FString& OutLabel, float& X, float& Y, float& Z)
{
	OutShape.Reset(); OutLabel.Reset(); X = Y = Z = 0.f;
	Args->TryGetStringField(TEXT("shape"), OutShape);
	Args->TryGetStringField(TEXT("actor_label"), OutLabel);
	if (OutLabel.IsEmpty()) Args->TryGetStringField(TEXT("label"), OutLabel);

	const TArray<TSharedPtr<FJsonValue>>* LocArr = nullptr;
	if (Args->TryGetArrayField(TEXT("location"), LocArr) && LocArr && LocArr->Num() >= 3)
	{
		X = (float)(*LocArr)[0]->AsNumber();
		Y = (float)(*LocArr)[1]->AsNumber();
		Z = (float)(*LocArr)[2]->AsNumber();
		return;
	}
	double DX = 0, DY = 0, DZ = 0;
	Args->TryGetNumberField(TEXT("location_x"), DX);
	Args->TryGetNumberField(TEXT("location_y"), DY);
	Args->TryGetNumberField(TEXT("location_z"), DZ);
	X = (float)DX; Y = (float)DY; Z = (float)DZ;
}

void HandleSpawnColorCorrectionRegionFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!EditorReadiness::IsSessionActive()) { OutError = TEXT("Session not ready — reload the editor and try again."); return; }
	if (!FCapabilityProfile::Get().IsEnabled(ECapability::SceneAuthoring)) { OutError = TEXT("Scene authoring isn't enabled for this install's profile yet — reconnect and try again."); return; }
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString Shape, Label; float X, Y, Z;
	ExtractCCRSpawnArgs(Args, Shape, Label, X, Y, Z);
	HandleSpawnColorCorrectionRegion(Shape, Label, X, Y, Z, OutJsonString, OutError);
}

void HandleSpawnColorCorrectionWindowFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!EditorReadiness::IsSessionActive()) { OutError = TEXT("Session not ready — reload the editor and try again."); return; }
	if (!FCapabilityProfile::Get().IsEnabled(ECapability::SceneAuthoring)) { OutError = TEXT("Scene authoring isn't enabled for this install's profile yet — reconnect and try again."); return; }
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString Shape, Label; float X, Y, Z;
	ExtractCCRSpawnArgs(Args, Shape, Label, X, Y, Z);
	HandleSpawnColorCorrectionWindow(Shape, Label, X, Y, Z, OutJsonString, OutError);
}

void HandleDeleteActorFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("actors"), ItemsArray))
	{
		TArray<FString> Labels;
		for (const auto& V : *ItemsArray)
		{
			if (V->Type == EJson::String) Labels.Add(V->AsString());
			else if (auto Obj = V->AsObject()) Labels.Add(BatchToolHelper::GetItemString(Obj, TEXT("actor_label"), TEXT("label")));
		}
		HandleDeleteActors(Labels, OutJsonString, OutError);
		return;
	}
	const TArray<TSharedPtr<FJsonValue>>* LabelsArr = nullptr;
	if (Args->TryGetArrayField(TEXT("actor_labels"), LabelsArr) && LabelsArr)
	{
		TArray<FString> Labels;
		for (const auto& V : *LabelsArr) Labels.Add(V->AsString());
		HandleDeleteActors(Labels, OutJsonString, OutError);
		return;
	}
	FString Label = BatchToolHelper::GetItemString(Args, TEXT("actor_label"), TEXT("label"));
	TArray<FString> Labels; Labels.Add(Label);
	HandleDeleteActors(Labels, OutJsonString, OutError);
}

void HandleDuplicateActorFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!EditorReadiness::IsSessionActive()) { OutError = TEXT("Session not ready — reload the editor and try again."); return; }
	if (!FCapabilityProfile::Get().IsEnabled(ECapability::SceneAuthoring)) { OutError = TEXT("Scene authoring isn't enabled for this install's profile yet — reconnect and try again."); return; }
	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("actors"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString Label = BatchToolHelper::GetItemString(Item, TEXT("actor_label"), TEXT("label"));
			FString NewLabel; Item->TryGetStringField(TEXT("new_label"), NewLabel);
			FVector Offset = ExtractVector(Item, TEXT("offset"));
			if (Label.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing actor_label")); continue; }
			FString ItemOut, ItemErr;
			HandleDuplicateActor(Label, Offset, NewLabel, ItemOut, ItemErr);
			if (ItemErr.IsEmpty())
			{
				auto Extra = MakeShared<FJsonObject>();
				Extra->SetStringField(TEXT("actor_label"), Label);
				Batch.AddSuccess(i, Extra);
			}
			else Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJsonString);
		return;
	}
	FString Label = BatchToolHelper::GetItemString(Args, TEXT("actor_label"), TEXT("label"));
	FString NewLabel; Args->TryGetStringField(TEXT("new_label"), NewLabel);
	FVector Offset = ExtractVector(Args, TEXT("offset"));
	HandleDuplicateActor(Label, Offset, NewLabel, OutJsonString, OutError);
}

void HandleSetActorTransformFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("actors"), ItemsArray))
	{
		HandleBulkTransformActors(*ItemsArray, OutJsonString, OutError);
		return;
	}
	FString Label = BatchToolHelper::GetItemString(Args, TEXT("actor_label"), TEXT("label"));
	bool bSetLoc = Args->HasField(TEXT("location"));
	bool bSetRot = Args->HasField(TEXT("rotation"));
	bool bSetScale = Args->HasField(TEXT("scale"));
	FVector Loc = ExtractVector(Args, TEXT("location"));
	FRotator Rot = ExtractRotator(Args, TEXT("rotation"));
	FVector Scale = ExtractVector(Args, TEXT("scale"), FVector::OneVector);
	HandleSetActorTransform(Label, bSetLoc, Loc, bSetRot, Rot, bSetScale, Scale, OutJsonString, OutError);
}

void HandleSetActorPropertyFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("properties"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString Label = BatchToolHelper::GetItemString(Item, TEXT("actor_label"), TEXT("label"));
			FString PropName = BatchToolHelper::GetItemString(Item, TEXT("property_name"));
			FString PropValue = BatchToolHelper::GetItemString(Item, TEXT("property_value"), TEXT("value"));
			if (Label.IsEmpty()) Label = BatchToolHelper::GetItemString(Args, TEXT("actor_label"), TEXT("label"));
			if (PropName.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing property_name")); continue; }
			FString ItemOut, ItemErr;
			HandleSetActorProperty(Label, PropName, PropValue, ItemOut, ItemErr);
			if (ItemErr.IsEmpty())
			{
				auto Extra = MakeShared<FJsonObject>();
				Extra->SetStringField(TEXT("property_name"), PropName);
				Batch.AddSuccess(i, Extra);
			}
			else Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJsonString);
		return;
	}
	FString Label = BatchToolHelper::GetItemString(Args, TEXT("actor_label"), TEXT("label"));
	FString PropName = BatchToolHelper::GetItemString(Args, TEXT("property_name"));
	FString PropValue = BatchToolHelper::GetItemString(Args, TEXT("property_value"), TEXT("value"));
	HandleSetActorProperty(Label, PropName, PropValue, OutJsonString, OutError);
}

void HandleAttachActorFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("attachments"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString Label = BatchToolHelper::GetItemString(Item, TEXT("actor_label"), TEXT("label"));
			FString Parent = BatchToolHelper::GetItemString(Item, TEXT("parent_label"), TEXT("parent"));
			FString Socket; Item->TryGetStringField(TEXT("socket_name"), Socket);
			bool bKeepWorld = true; Item->TryGetBoolField(TEXT("keep_world_transform"), bKeepWorld);
			if (Label.IsEmpty() || Parent.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing actor_label or parent_label")); continue; }
			FString ItemOut, ItemErr;
			HandleAttachActor(Label, Parent, bKeepWorld, Socket, ItemOut, ItemErr);
			if (ItemErr.IsEmpty())
			{
				auto Extra = MakeShared<FJsonObject>();
				Extra->SetStringField(TEXT("actor_label"), Label);
				Extra->SetStringField(TEXT("parent_label"), Parent);
				Batch.AddSuccess(i, Extra);
			}
			else Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJsonString);
		return;
	}
	FString Label = BatchToolHelper::GetItemString(Args, TEXT("actor_label"), TEXT("label"));
	FString Parent = BatchToolHelper::GetItemString(Args, TEXT("parent_label"), TEXT("parent"));
	FString Socket; Args->TryGetStringField(TEXT("socket_name"), Socket);
	bool bKeepWorld = true; Args->TryGetBoolField(TEXT("keep_world_transform"), bKeepWorld);
	HandleAttachActor(Label, Parent, bKeepWorld, Socket, OutJsonString, OutError);
}

void HandleAddActorTagFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("tags"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			FString Label, TagStr;
			if ((*ItemsArray)[i]->Type == EJson::String)
			{
				TagStr = (*ItemsArray)[i]->AsString();
				Label = BatchToolHelper::GetItemString(Args, TEXT("actor_label"), TEXT("label"));
			}
			else if (auto Item = (*ItemsArray)[i]->AsObject())
			{
				Label = BatchToolHelper::GetItemString(Item, TEXT("actor_label"), TEXT("label"));
				if (Label.IsEmpty()) Label = BatchToolHelper::GetItemString(Args, TEXT("actor_label"), TEXT("label"));
				TagStr = BatchToolHelper::GetItemString(Item, TEXT("tag"), TEXT("tag_name"));
			}
			if (TagStr.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing tag")); continue; }
			FString ItemOut, ItemErr;
			HandleAddActorTag(Label, TagStr, ItemOut, ItemErr);
			if (ItemErr.IsEmpty())
			{
				auto Extra = MakeShared<FJsonObject>();
				Extra->SetStringField(TEXT("tag"), TagStr);
				Batch.AddSuccess(i, Extra);
			}
			else Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJsonString);
		return;
	}
	FString Label = BatchToolHelper::GetItemString(Args, TEXT("actor_label"), TEXT("label"));
	FString TagStr = BatchToolHelper::GetItemString(Args, TEXT("tag"), TEXT("tag_name"));
	HandleAddActorTag(Label, TagStr, OutJsonString, OutError);
}

void HandleRemoveActorTagFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("tags"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			FString Label, TagStr;
			if ((*ItemsArray)[i]->Type == EJson::String)
			{
				TagStr = (*ItemsArray)[i]->AsString();
				Label = BatchToolHelper::GetItemString(Args, TEXT("actor_label"), TEXT("label"));
			}
			else if (auto Item = (*ItemsArray)[i]->AsObject())
			{
				Label = BatchToolHelper::GetItemString(Item, TEXT("actor_label"), TEXT("label"));
				if (Label.IsEmpty()) Label = BatchToolHelper::GetItemString(Args, TEXT("actor_label"), TEXT("label"));
				TagStr = BatchToolHelper::GetItemString(Item, TEXT("tag"), TEXT("tag_name"));
			}
			if (TagStr.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing tag")); continue; }
			FString ItemOut, ItemErr;
			HandleRemoveActorTag(Label, TagStr, ItemOut, ItemErr);
			if (ItemErr.IsEmpty())
			{
				auto Extra = MakeShared<FJsonObject>();
				Extra->SetStringField(TEXT("tag"), TagStr);
				Batch.AddSuccess(i, Extra);
			}
			else Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJsonString);
		return;
	}
	FString Label = BatchToolHelper::GetItemString(Args, TEXT("actor_label"), TEXT("label"));
	FString TagStr = BatchToolHelper::GetItemString(Args, TEXT("tag"), TEXT("tag_name"));
	HandleRemoveActorTag(Label, TagStr, OutJsonString, OutError);
}

void HandleSetLightPropertyFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("lights"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString Label = BatchToolHelper::GetItemString(Item, TEXT("actor_label"), TEXT("label"));
			if (Label.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing actor_label")); continue; }
			FString ItemOut, ItemErr;
			HandleSetLightProperty(Label, Item, ItemOut, ItemErr);
			if (ItemErr.IsEmpty())
			{
				auto Extra = MakeShared<FJsonObject>();
				Extra->SetStringField(TEXT("actor_label"), Label);
				Batch.AddSuccess(i, Extra);
			}
			else Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJsonString);
		return;
	}
	FString Label = BatchToolHelper::GetItemString(Args, TEXT("actor_label"), TEXT("label"));
	HandleSetLightProperty(Label, Args, OutJsonString, OutError);
}

void HandleSetActorMaterialsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }

	FString MaterialPath;
	if (!Args->TryGetStringField(TEXT("material_path"), MaterialPath) || MaterialPath.IsEmpty())
	{
		OutError = TEXT("Missing required parameter: material_path");
		return;
	}

	const TArray<TSharedPtr<FJsonValue>>* ActorLabelsArray = nullptr;
	TArray<TSharedPtr<FJsonValue>> SingleLabelArray;
	if (!Args->TryGetArrayField(TEXT("actor_labels"), ActorLabelsArray) || !ActorLabelsArray || ActorLabelsArray->Num() == 0)
	{
		FString SingleLabel;
		if (Args->TryGetStringField(TEXT("actor_label"), SingleLabel) || Args->TryGetStringField(TEXT("actor_name"), SingleLabel))
		{
			SingleLabelArray.Add(MakeShareable(new FJsonValueString(SingleLabel)));
			ActorLabelsArray = &SingleLabelArray;
		}
		else
		{
			OutError = TEXT("Missing required parameter: actor_label or actor_labels");
			return;
		}
	}

	UMaterialInterface* Material = Cast<UMaterialInterface>(UEditorAssetLibrary::LoadAsset(MaterialPath));
	if (!Material)
	{
		OutError = FString::Printf(TEXT("Could not load material at path: %s"), *MaterialPath);
		return;
	}

	UWorld* EditorWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!EditorWorld)
	{
		OutError = TEXT("Could not get editor world.");
		return;
	}

	TMap<FString, AActor*> ActorsByLabel;
	for (TActorIterator<AActor> It(EditorWorld); It; ++It)
	{
		if (AActor* Actor = *It)
		{
			ActorsByLabel.Add(Actor->GetActorLabel(), Actor);
		}
	}

	int32 SlotIndex = -1;
	{
		double SlotIdxD = -1.0;
		if (Args->TryGetNumberField(TEXT("slot_index"), SlotIdxD))
		{
			SlotIndex = (int32)SlotIdxD;
		}
	}

	int32 SuccessCount = 0;
	int32 FailCount = 0;
	int32 TotalMaterialsApplied = 0;

	for (const TSharedPtr<FJsonValue>& LabelValue : *ActorLabelsArray)
	{
		FString ActorLabel = LabelValue->AsString();
		if (ActorLabel.IsEmpty()) { FailCount++; continue; }

		AActor** FoundActorPtr = ActorsByLabel.Find(ActorLabel);
		if (!FoundActorPtr || !(*FoundActorPtr)) { FailCount++; continue; }

		AActor* FoundActor = *FoundActorPtr;
		TArray<UMeshComponent*> MeshComponents;
		FoundActor->GetComponents<UMeshComponent>(MeshComponents);

		int32 MaterialsApplied = 0;
		for (UMeshComponent* MeshComp : MeshComponents)
		{
			if (SlotIndex >= 0)
			{
				if (SlotIndex < MeshComp->GetNumMaterials())
				{
					MeshComp->SetMaterial(SlotIndex, Material);
					MaterialsApplied++;
				}
			}
			else
			{
				const int32 NumMaterials = MeshComp->GetNumMaterials();
				for (int32 i = 0; i < NumMaterials; i++)
				{
					MeshComp->SetMaterial(i, Material);
					MaterialsApplied++;
				}
			}
		}

		if (MaterialsApplied > 0) { SuccessCount++; TotalMaterialsApplied += MaterialsApplied; }
		else { FailCount++; }
	}

	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField(TEXT("success"), SuccessCount > 0);
	Obj->SetStringField(TEXT("material_path"), MaterialPath);
	Obj->SetNumberField(TEXT("actors_updated"), SuccessCount);
	Obj->SetNumberField(TEXT("actors_failed"), FailCount);
	Obj->SetNumberField(TEXT("total_material_slots_updated"), TotalMaterialsApplied);
	Obj->SetStringField(TEXT("message"), FString::Printf(
		TEXT("Applied material '%s' to %d actors (%d failed, %d total material slots)"),
		*MaterialPath, SuccessCount, FailCount, TotalMaterialsApplied));
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Obj.ToSharedRef(), Writer);

	if (SuccessCount == 0)
	{
		OutError = FString::Printf(TEXT("Failed to apply material '%s' to any of %d requested actors."),
			*MaterialPath, ActorLabelsArray->Num());
	}
}

namespace
{
	static bool ParseVec3FromArray(const TArray<TSharedPtr<FJsonValue>>& Arr, FVector& Out)
	{
		if (Arr.Num() < 3) return false;
		Out.X = Arr[0]->AsNumber();
		Out.Y = Arr[1]->AsNumber();
		Out.Z = Arr[2]->AsNumber();
		return true;
	}

	static void CollectStringArray(const TArray<TSharedPtr<FJsonValue>>& Arr, TArray<FString>& Out)
	{
		for (const TSharedPtr<FJsonValue>& V : Arr)
		{
			FString S;
			if (V.IsValid() && V->TryGetString(S)) Out.Add(S);
		}
	}
}

void HandleGetAllSceneActorsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString ClassFilter;
	int32 MaxResults = 0;
	bool bIncludeLocation = false;
	if (Args.IsValid())
	{
		Args->TryGetStringField(TEXT("class_filter"), ClassFilter);
		if (Args->HasField(TEXT("max_results")))
		{
			MaxResults = (int32)Args->GetNumberField(TEXT("max_results"));
		}
		Args->TryGetBoolField(TEXT("include_location"), bIncludeLocation);
	}
	HandleGetAllSceneActors(ClassFilter, MaxResults, bIncludeLocation, OutJsonString, OutError);
}

void HandleGetSelectedLevelActorsFromArgs(const TSharedPtr<FJsonObject>& , FString& OutJsonString, FString& OutError)
{
	TSharedPtr<FJsonObject> ResultObject = MakeShareable(new FJsonObject);
	if (!GEditor)
	{
		OutError = TEXT("GEditor not available.");
		return;
	}

	TArray<TSharedPtr<FJsonValue>> ActorsArray;
	USelection* Selection = GEditor->GetSelectedActors();
	for (FSelectionIterator It(*Selection); It; ++It)
	{
		AActor* Actor = Cast<AActor>(*It);
		if (!Actor) continue;

		TSharedPtr<FJsonObject> ActorObj = MakeShareable(new FJsonObject);
		ActorObj->SetStringField(TEXT("label"), Actor->GetActorLabel());
		ActorObj->SetStringField(TEXT("class"), Actor->GetClass()->GetName());

		FVector Loc = Actor->GetActorLocation();
		TSharedPtr<FJsonObject> LocationObj = MakeShareable(new FJsonObject);
		LocationObj->SetNumberField(TEXT("x"), Loc.X);
		LocationObj->SetNumberField(TEXT("y"), Loc.Y);
		LocationObj->SetNumberField(TEXT("z"), Loc.Z);
		ActorObj->SetObjectField(TEXT("location"), LocationObj);

		FRotator Rot = Actor->GetActorRotation();
		TSharedPtr<FJsonObject> RotObj = MakeShareable(new FJsonObject);
		RotObj->SetNumberField(TEXT("pitch"), Rot.Pitch);
		RotObj->SetNumberField(TEXT("yaw"), Rot.Yaw);
		RotObj->SetNumberField(TEXT("roll"), Rot.Roll);
		ActorObj->SetObjectField(TEXT("rotation"), RotObj);

		FVector Scale = Actor->GetActorScale3D();
		TSharedPtr<FJsonObject> ScaleObj = MakeShareable(new FJsonObject);
		ScaleObj->SetNumberField(TEXT("x"), Scale.X);
		ScaleObj->SetNumberField(TEXT("y"), Scale.Y);
		ScaleObj->SetNumberField(TEXT("z"), Scale.Z);
		ActorObj->SetObjectField(TEXT("scale"), ScaleObj);

		ActorsArray.Add(MakeShareable(new FJsonValueObject(ActorObj)));
	}

	ResultObject->SetBoolField(TEXT("success"), true);
	ResultObject->SetNumberField(TEXT("count"), ActorsArray.Num());
	ResultObject->SetBoolField(TEXT("selection_is_empty"), ActorsArray.Num() == 0);
	ResultObject->SetArrayField(TEXT("actors"), ActorsArray);
	if (ActorsArray.Num() == 0)
		ResultObject->SetStringField(TEXT("hint"), TEXT("No actors are selected in the level viewport. Ask the user to select one or more actors first."));

	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(ResultObject.ToSharedRef(), Writer);
}

void HandleSelectActorsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }

	const TArray<TSharedPtr<FJsonValue>>* LabelsArr = nullptr;
	if (!Args->TryGetArrayField(TEXT("actor_labels"), LabelsArr) || !LabelsArr)
	{
		OutError = TEXT("Missing required parameter: actor_labels (array)");
		return;
	}

	TArray<FString> Labels;
	CollectStringArray(*LabelsArr, Labels);

	HandleSetSelectedActors(Labels, OutError);
	TSharedPtr<FJsonObject> ResultObject = MakeShareable(new FJsonObject);
	if (OutError.IsEmpty())
	{
		ResultObject->SetBoolField(TEXT("success"), true);
		ResultObject->SetNumberField(TEXT("selected_count"), Labels.Num());
		ResultObject->SetStringField(TEXT("message"), Labels.Num() > 0
			? FString::Printf(TEXT("Selected %d actor(s) in the level."), Labels.Num())
			: TEXT("Cleared actor selection."));
	}
	else
	{
		ResultObject->SetBoolField(TEXT("success"), false);
		ResultObject->SetStringField(TEXT("error"), OutError);
	}
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(ResultObject.ToSharedRef(), Writer);
}

void HandleGetActorDetailsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString ActorLabel;
	if (!Args->TryGetStringField(TEXT("actor_label"), ActorLabel))
	{
		OutError = TEXT("Missing required parameter: actor_label");
		return;
	}
	HandleGetActorDetails(ActorLabel, OutJsonString, OutError);
}

void HandleRenameActorFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString OldLabel, NewLabel;
	if (!Args->TryGetStringField(TEXT("actor_label"), OldLabel) ||
		!Args->TryGetStringField(TEXT("new_label"), NewLabel))
	{
		OutError = TEXT("Missing required parameters: actor_label, new_label");
		return;
	}
	HandleRenameActor(OldLabel, NewLabel, OutJsonString, OutError);
}

void HandleGetActorsByClassFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString ClassName;
	if (!Args->TryGetStringField(TEXT("class_name"), ClassName))
	{
		OutError = TEXT("Missing class_name");
		return;
	}
	int32 MaxResults = 0;
	if (Args->HasField(TEXT("max_results")))
	{
		MaxResults = (int32)Args->GetNumberField(TEXT("max_results"));
	}
	bool bIncludeLocation = false;
	Args->TryGetBoolField(TEXT("include_location"), bIncludeLocation);
	HandleGetActorsByClass(ClassName, MaxResults, bIncludeLocation, OutJsonString, OutError);
}

void HandleGetActorsInBoxFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	const TArray<TSharedPtr<FJsonValue>>* CenterArr = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* ExtentArr = nullptr;
	if (!Args->TryGetArrayField(TEXT("center"), CenterArr) || !CenterArr || CenterArr->Num() < 3 ||
		!Args->TryGetArrayField(TEXT("extent"), ExtentArr) || !ExtentArr || ExtentArr->Num() < 3)
	{
		OutError = TEXT("Missing center or extent [x,y,z]");
		return;
	}
	FVector Center, Extent;
	ParseVec3FromArray(*CenterArr, Center);
	ParseVec3FromArray(*ExtentArr, Extent);
	HandleGetActorsInBox(Center, Extent, OutJsonString, OutError);
}

void HandleGetActorComponentsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString Label;
	if (!Args->TryGetStringField(TEXT("actor_label"), Label))
	{
		OutError = TEXT("Missing actor_label");
		return;
	}
	HandleGetActorComponents(Label, OutJsonString, OutError);
}

void HandleGetLevelInfoFromArgs(const TSharedPtr<FJsonObject>& , FString& OutJsonString, FString& OutError)
{
	HandleGetLevelInfo(OutJsonString, OutError);
}

void HandleSetActorVisibilityFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	const TArray<TSharedPtr<FJsonValue>>* LabelsArr = nullptr;
	if (!Args->TryGetArrayField(TEXT("actor_labels"), LabelsArr) || !LabelsArr)
	{
		OutError = TEXT("Missing actor_labels");
		return;
	}
	bool bVisible = true;
	Args->TryGetBoolField(TEXT("visible"), bVisible);
	TArray<FString> Labels;
	CollectStringArray(*LabelsArr, Labels);
	HandleSetActorVisibility(Labels, bVisible, OutJsonString, OutError);
}

void HandleBulkTransformActorsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	const TArray<TSharedPtr<FJsonValue>>* ActorsArr = nullptr;
	if (!Args->TryGetArrayField(TEXT("actors"), ActorsArr) || !ActorsArr)
	{
		OutError = TEXT("Missing actors array");
		return;
	}
	HandleBulkTransformActors(*ActorsArr, OutJsonString, OutError);
}

void HandleAlignActorsToFloorFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	const TArray<TSharedPtr<FJsonValue>>* LabelsArr = nullptr;
	if (!Args->TryGetArrayField(TEXT("actor_labels"), LabelsArr) || !LabelsArr)
	{
		OutError = TEXT("Missing actor_labels");
		return;
	}
	double TraceOffset = 10.0;
	Args->TryGetNumberField(TEXT("trace_offset"), TraceOffset);
	TArray<FString> Labels;
	CollectStringArray(*LabelsArr, Labels);
	HandleAlignActorsToFloor(Labels, (float)TraceOffset, OutJsonString, OutError);
}

void HandleDetachActorFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString Label;
	if (!Args->TryGetStringField(TEXT("actor_label"), Label))
	{
		OutError = TEXT("Missing actor_label");
		return;
	}
	HandleDetachActor(Label, OutJsonString, OutError);
}

void HandleMoveActorToFolderFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	const TArray<TSharedPtr<FJsonValue>>* LabelsArr = nullptr;
	FString FolderPath;
	if (!Args->TryGetArrayField(TEXT("actor_labels"), LabelsArr) || !LabelsArr ||
		!Args->TryGetStringField(TEXT("folder_path"), FolderPath))
	{
		OutError = TEXT("Missing actor_labels or folder_path");
		return;
	}
	TArray<FString> Labels;
	CollectStringArray(*LabelsArr, Labels);
	HandleMoveActorToFolder(Labels, FolderPath, OutJsonString, OutError);
}

void HandleSetStaticMeshFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString Label, MeshPath;
	if (!Args->TryGetStringField(TEXT("actor_label"), Label) ||
		!Args->TryGetStringField(TEXT("mesh_path"), MeshPath))
	{
		OutError = TEXT("Missing actor_label or mesh_path");
		return;
	}
	HandleSetStaticMesh(Label, MeshPath, OutJsonString, OutError);
}

void HandleSetActorPhysicsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString Label;
	bool bSimulate = true;
	if (!Args->TryGetStringField(TEXT("actor_label"), Label))
	{
		OutError = TEXT("Missing actor_label");
		return;
	}
	Args->TryGetBoolField(TEXT("simulate_physics"), bSimulate);
	bool bSetGravity = false, bEnableGravity = true;
	if (Args->HasField(TEXT("enable_gravity")))
	{
		bSetGravity = true;
		Args->TryGetBoolField(TEXT("enable_gravity"), bEnableGravity);
	}
	double Mass = -1.0, LinearDamping = -1.0, AngularDamping = -1.0;
	Args->TryGetNumberField(TEXT("mass"), Mass);
	Args->TryGetNumberField(TEXT("linear_damping"), LinearDamping);
	Args->TryGetNumberField(TEXT("angular_damping"), AngularDamping);
	HandleSetActorPhysics(Label, bSimulate, bSetGravity, bEnableGravity,
		(float)Mass, (float)LinearDamping, (float)AngularDamping, OutJsonString, OutError);
}

void HandleGetActorTagsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString Label;
	if (!Args->TryGetStringField(TEXT("actor_label"), Label))
	{
		OutError = TEXT("Missing actor_label");
		return;
	}
	HandleGetActorTags(Label, OutJsonString, OutError);
}

void HandleSetWorldSettingsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	HandleSetWorldSettings(Args, OutJsonString, OutError);
}

void HandleFocusViewportOnActorFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString Label;
	if (!Args->TryGetStringField(TEXT("actor_label"), Label))
	{
		OutError = TEXT("Missing actor_label");
		return;
	}
	HandleFocusViewportOnActor(Label, OutJsonString, OutError);
}

void HandleReplaceActorFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString Label, NewClassOrMesh;
	if (!Args->TryGetStringField(TEXT("actor_label"), Label) ||
		!Args->TryGetStringField(TEXT("new_class_or_mesh"), NewClassOrMesh))
	{
		OutError = TEXT("Missing actor_label or new_class_or_mesh");
		return;
	}
	HandleReplaceActor(Label, NewClassOrMesh, OutJsonString, OutError);
}

void HandleSetActorReplicationFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString ActorLabel, ReplicateStr, ReplicateMovementStr;
	double NetUpdateFreq = -1.0, NetCullDist = -1.0;
	Args->TryGetStringField(TEXT("actor_label"), ActorLabel);
	Args->TryGetStringField(TEXT("replicate"), ReplicateStr);
	Args->TryGetStringField(TEXT("replicate_movement"), ReplicateMovementStr);
	Args->TryGetNumberField(TEXT("net_update_frequency"), NetUpdateFreq);
	Args->TryGetNumberField(TEXT("net_cull_distance"), NetCullDist);
	HandleSetActorReplication(ActorLabel, ReplicateStr, ReplicateMovementStr,
		(float)NetUpdateFreq, (float)NetCullDist, OutJsonString, OutError);
}

void HandleFindActorsByBoundsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FVector Center(0, 0, 0), Extent(500, 500, 500);
	if (Args.IsValid())
	{
		const TArray<TSharedPtr<FJsonValue>>* CenterArr = nullptr;
		const TArray<TSharedPtr<FJsonValue>>* ExtentArr = nullptr;
		if (Args->TryGetArrayField(TEXT("center"), CenterArr) && CenterArr && CenterArr->Num() >= 3)
		{
			Center.X = (float)(*CenterArr)[0]->AsNumber();
			Center.Y = (float)(*CenterArr)[1]->AsNumber();
			Center.Z = (float)(*CenterArr)[2]->AsNumber();
		}
		if (Args->TryGetArrayField(TEXT("extent"), ExtentArr) && ExtentArr && ExtentArr->Num() >= 3)
		{
			Extent.X = (float)(*ExtentArr)[0]->AsNumber();
			Extent.Y = (float)(*ExtentArr)[1]->AsNumber();
			Extent.Z = (float)(*ExtentArr)[2]->AsNumber();
		}
	}
	HandleFindActorsByBounds(Center, Extent, OutJsonString, OutError);
}

void HandleBeginPlayInEditorFromArgs(const TSharedPtr<FJsonObject>& , FString& OutJsonString, FString& OutError)
{
	HandleBeginPlayInEditor(OutJsonString, OutError);
}

void HandleStopPlayInEditorFromArgs(const TSharedPtr<FJsonObject>& , FString& OutJsonString, FString& OutError)
{
	HandleStopPlayInEditor(OutJsonString, OutError);
}

void HandleOpenLevelFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString LevelPath;
	if (Args.IsValid())
	{
		if (!Args->TryGetStringField(TEXT("level_path"), LevelPath))
			Args->TryGetStringField(TEXT("asset_path"), LevelPath);
	}
	if (LevelPath.IsEmpty()) { OutError = TEXT("open_level: 'level_path' is required (e.g. '/Game/Maps/L_Arena')."); return; }

	int32 DotIdx;
	if (LevelPath.FindChar(TEXT('.'), DotIdx) && DotIdx > 0) LevelPath = LevelPath.Left(DotIdx);

	if (!FPackageName::DoesPackageExist(LevelPath))
	{
		OutError = FString::Printf(TEXT("open_level: no level asset at '%s'. Create one first with create_asset(asset_type='Level')."), *LevelPath);
		return;
	}

	ULevelEditorSubsystem* LES = GEditor ? GEditor->GetEditorSubsystem<ULevelEditorSubsystem>() : nullptr;
	if (!LES) { OutError = TEXT("open_level: LevelEditorSubsystem unavailable."); return; }

	if (!LES->LoadLevel(LevelPath))
	{
		OutError = FString::Printf(TEXT("open_level: LoadLevel('%s') failed (the editor may have blocked on an unsaved-changes prompt — save_current_level or discard first)."), *LevelPath);
		return;
	}
	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"opened\":\"%s\"}"), *LevelPath);
}

void HandleSaveCurrentLevelFromArgs(const TSharedPtr<FJsonObject>& , FString& OutJsonString, FString& OutError)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	const FString WorldName = World ? World->GetMapName() : FString();
	if (WorldName.Equals(TEXT("Untitled"), ESearchCase::IgnoreCase) || WorldName.IsEmpty())
	{
		OutError = TEXT("save_current_level: the current level is an unsaved 'Untitled' map — use save_current_level_as(package_path, level_name) to write it to a new asset.");
		return;
	}

	ULevelEditorSubsystem* LES = GEditor ? GEditor->GetEditorSubsystem<ULevelEditorSubsystem>() : nullptr;
	if (!LES) { OutError = TEXT("save_current_level: LevelEditorSubsystem unavailable."); return; }

	if (!LES->SaveCurrentLevel()) { OutError = TEXT("save_current_level: SaveCurrentLevel() failed."); return; }
	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"saved\":\"%s\"}"), *WorldName);
}

void HandleSaveCurrentLevelAsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }

	FString PackagePath, LevelName;
	if (!Args->TryGetStringField(TEXT("package_path"), PackagePath)) Args->TryGetStringField(TEXT("save_path"), PackagePath);
	if (!Args->TryGetStringField(TEXT("level_name"), LevelName))     Args->TryGetStringField(TEXT("name"), LevelName);
	if (PackagePath.IsEmpty() || LevelName.IsEmpty())
	{
		OutError = TEXT("save_current_level_as requires 'package_path' (e.g. '/Game/Maps') and 'level_name' (e.g. 'L_Arena').");
		return;
	}
	while (PackagePath.EndsWith(TEXT("/"))) PackagePath = PackagePath.LeftChop(1);
	if (PackagePath.EndsWith(TEXT("/") + LevelName, ESearchCase::IgnoreCase))
		PackagePath = PackagePath.LeftChop(LevelName.Len() + 1);

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World) { OutError = TEXT("save_current_level_as: no editor world to save."); return; }

	const FString AssetPath = PackagePath + TEXT("/") + LevelName;
	if (!FPackageName::IsValidLongPackageName(AssetPath))
	{
		OutError = FString::Printf(TEXT("save_current_level_as: '%s' is not a valid /Game package path."), *AssetPath);
		return;
	}
	if (FPackageName::DoesPackageExist(AssetPath))
	{
		OutError = FString::Printf(TEXT("save_current_level_as: an asset already exists at '%s' — pick another name or delete it first (avoids an overwrite modal)."), *AssetPath);
		return;
	}

	if (!UEditorLoadingAndSavingUtils::SaveMap(World, AssetPath))
	{
		OutError = FString::Printf(TEXT("save_current_level_as: SaveMap('%s') failed."), *AssetPath);
		return;
	}
	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"saved_as\":\"%s\"}"), *AssetPath);
}

void HandleGetAssetReferencesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString AssetPath;
	if (Args.IsValid()) Args->TryGetStringField(TEXT("asset_path"), AssetPath);
	HandleGetAssetReferences(AssetPath, OutJsonString, OutError);
}

void HandleCreateDataLayerFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString LayerName;
	if (Args.IsValid()) Args->TryGetStringField(TEXT("layer_name"), LayerName);
	HandleCreateDataLayer(LayerName, OutJsonString, OutError);
}

void HandleAssignActorToDataLayerFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString ActorLabel, LayerName;
	if (Args.IsValid())
	{
		Args->TryGetStringField(TEXT("actor_label"), ActorLabel);
		Args->TryGetStringField(TEXT("layer_name"), LayerName);
	}
	HandleAssignActorToDataLayer(ActorLabel, LayerName, OutJsonString, OutError);
}

void HandleSetDataLayerStateFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString LayerName, State;
	if (Args.IsValid())
	{
		Args->TryGetStringField(TEXT("layer_name"), LayerName);
		Args->TryGetStringField(TEXT("state"), State);
	}
	HandleSetDataLayerState(LayerName, State, OutJsonString, OutError);
}

void HandleListDataLayersFromArgs(const TSharedPtr<FJsonObject>& , FString& OutJsonString, FString& OutError)
{
	HandleListDataLayers(OutJsonString, OutError);
}

void HandleGetDataLayerActorsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString LayerName;
	if (Args.IsValid()) Args->TryGetStringField(TEXT("layer_name"), LayerName);
	HandleGetDataLayerActors(LayerName, OutJsonString, OutError);
}

void HandleCreateLevelInstanceFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString ActorLabel, LevelPath;
	double LX = 0, LY = 0, LZ = 0;
	if (Args.IsValid())
	{
		Args->TryGetStringField(TEXT("actor_label"), ActorLabel);
		Args->TryGetStringField(TEXT("level_path"), LevelPath);
		Args->TryGetNumberField(TEXT("location_x"), LX);
		Args->TryGetNumberField(TEXT("location_y"), LY);
		Args->TryGetNumberField(TEXT("location_z"), LZ);
	}
	HandleCreateLevelInstance(ActorLabel, LevelPath, (float)LX, (float)LY, (float)LZ, OutJsonString, OutError);
}

void HandleGetLevelInstancesFromArgs(const TSharedPtr<FJsonObject>& , FString& OutJsonString, FString& OutError)
{
	HandleGetLevelInstances(OutJsonString, OutError);
}

void HandleEnableWorldPartitionFromArgs(const TSharedPtr<FJsonObject>& , FString& OutJsonString, FString& OutError)
{
	HandleEnableWorldPartition(OutJsonString, OutError);
}

void HandleDisableWorldPartitionFromArgs(const TSharedPtr<FJsonObject>& , FString& OutJsonString, FString& OutError)
{
	HandleDisableWorldPartition(OutJsonString, OutError);
}

void HandleGetWorldPartitionInfoFromArgs(const TSharedPtr<FJsonObject>& , FString& OutJsonString, FString& OutError)
{
	HandleGetWorldPartitionInfo(OutJsonString, OutError);
}

void HandleCreateHLODLayerFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString Name, SavePath, LayerType;
	double CellSize = 0.0;
	if (Args.IsValid())
	{
		Args->TryGetStringField(TEXT("name"), Name);
		Args->TryGetStringField(TEXT("save_path"), SavePath);
		Args->TryGetStringField(TEXT("layer_type"), LayerType);
		Args->TryGetNumberField(TEXT("cell_size"), CellSize);
	}
	if (LayerType.IsEmpty()) LayerType = TEXT("Instancing");
	HandleCreateHLODLayer(Name, SavePath, LayerType, (int32)CellSize, OutJsonString, OutError);
}

void HandleSetHLODLayerPropertiesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString AssetPath, LayerType;
	double CellSize = 0.0;
	if (Args.IsValid())
	{
		Args->TryGetStringField(TEXT("asset_path"), AssetPath);
		Args->TryGetStringField(TEXT("layer_type"), LayerType);
		Args->TryGetNumberField(TEXT("cell_size"), CellSize);
	}
	HandleSetHLODLayerProperties(AssetPath, LayerType, (int32)CellSize, OutJsonString, OutError);
}

void HandleListHLODLayersFromArgs(const TSharedPtr<FJsonObject>& , FString& OutJsonString, FString& OutError)
{
	HandleListHLODLayers(OutJsonString, OutError);
}

void HandleSetActorsPropertyByFilterFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString ClassFilter, PropertyName, PropertyValue;
	if (!Args->TryGetStringField(TEXT("class_filter"), ClassFilter) || ClassFilter.IsEmpty())
	{
		OutError = TEXT("Missing required parameter: class_filter");
		return;
	}
	if (!Args->TryGetStringField(TEXT("property_name"), PropertyName) || PropertyName.IsEmpty())
	{
		OutError = TEXT("Missing required parameter: property_name");
		return;
	}
	Args->TryGetStringField(TEXT("property_value"), PropertyValue);
	HandleSetActorsPropertyByFilter(ClassFilter, PropertyName, PropertyValue, OutJsonString, OutError);
}

void HandleSetCineCameraPropertiesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString ActorLabel;
	double FocalLength = 0, Aperture = 0, FocusDistance = 0, FilmbackWidth = 0, FilmbackHeight = 0;
	if (Args.IsValid())
	{
		Args->TryGetStringField(TEXT("actor_label"), ActorLabel);
		Args->TryGetNumberField(TEXT("focal_length"), FocalLength);
		Args->TryGetNumberField(TEXT("aperture"), Aperture);
		Args->TryGetNumberField(TEXT("focus_distance"), FocusDistance);
		Args->TryGetNumberField(TEXT("filmback_width"), FilmbackWidth);
		Args->TryGetNumberField(TEXT("filmback_height"), FilmbackHeight);
	}
	HandleSetCineCameraProperties(ActorLabel, (float)FocalLength, (float)Aperture,
		(float)FocusDistance, (float)FilmbackWidth, (float)FilmbackHeight,
		OutJsonString, OutError);
}

void HandleSetSelectedActorsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	TArray<FString> ActorLabels;
	const TArray<TSharedPtr<FJsonValue>>* LabelsArray = nullptr;
	if (Args->TryGetArrayField(TEXT("actor_labels"), LabelsArray))
	{
		for (const TSharedPtr<FJsonValue>& Val : *LabelsArray)
			ActorLabels.Add(Val->AsString());
	}
	HandleSetSelectedActors(ActorLabels, OutError);
	if (OutError.IsEmpty())
		OutJsonString = FString::Printf(TEXT("{\"success\":true,\"message\":\"Updated actor selection. %d actors selected.\"}"), ActorLabels.Num());
}

void HandleAdvancedSpawnFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!EditorReadiness::IsSessionActive()) { OutError = TEXT("Session not ready — reload the editor and try again."); return; }
	if (!FCapabilityProfile::Get().IsEnabled(ECapability::SceneAuthoring)) { OutError = TEXT("Scene authoring isn't enabled for this install's profile yet — reconnect and try again."); return; }
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }

	int32 Count = 0;
	double CountD = 0;
	if (Args->TryGetNumberField(TEXT("count"), CountD)) Count = (int32)CountD;

	FString ActorClass, BoundingActorLabel;
	Args->TryGetStringField(TEXT("actor_class"), ActorClass);
	if (ActorClass.IsEmpty()) Args->TryGetStringField(TEXT("mesh_path"), ActorClass);
	Args->TryGetStringField(TEXT("bounding_actor_label"), BoundingActorLabel);

	FAdvancedSpawnOptions Options;
	Args->TryGetBoolField(TEXT("align_to_ground"), Options.bAlignToGround);
	Args->TryGetBoolField(TEXT("align_to_normal"), Options.bAlignToNormal);
	Args->TryGetBoolField(TEXT("allow_tilt"), Options.bAllowTilt);
	Args->TryGetBoolField(TEXT("uniform_scale"), Options.bUniformScale);
	Args->TryGetStringField(TEXT("spawn_mode"), Options.SpawnMode);
	Args->TryGetStringField(TEXT("label"), Options.Label);
	if (Options.Label.IsEmpty()) Args->TryGetStringField(TEXT("actor_label"), Options.Label);

	// Array-form rotations are [pitch, yaw, roll]; reject an accidental roll unless tilt was explicitly allowed.
	if (!CheckRotationTiltGuard(Args, TEXT("random_rotation_min"), Options.bAllowTilt, OutError)) return;
	if (!CheckRotationTiltGuard(Args, TEXT("random_rotation_max"), Options.bAllowTilt, OutError)) return;

	const FVector LocMin   = ExtractVector(Args, TEXT("random_location_min"), FVector(-1000, -1000, 0));
	const FVector LocMax   = ExtractVector(Args, TEXT("random_location_max"), FVector(1000, 1000, 0));
	const FVector ScaleMin = ExtractVector(Args, TEXT("random_scale_min"),    FVector(1, 1, 1));
	const FVector ScaleMax = ExtractVector(Args, TEXT("random_scale_max"),    FVector(1, 1, 1));
	const FRotator RotMin  = ExtractRotator(Args, TEXT("random_rotation_min"), FRotator::ZeroRotator);
	const FRotator RotMax  = ExtractRotator(Args, TEXT("random_rotation_max"), FRotator::ZeroRotator);

	HandleAdvancedSpawnEx(Count, ActorClass, BoundingActorLabel, LocMin, LocMax, ScaleMin, ScaleMax, RotMin, RotMax, Options, OutJsonString, OutError);
}

}
