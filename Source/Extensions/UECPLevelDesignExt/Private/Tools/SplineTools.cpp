// Copyright 2026, BlueprintsLab, All rights reserved.

#include "Tools/SplineTools.h"
#include "Tools/BatchToolHelper.h"

#include "Components/SplineComponent.h"
#include "Components/SplineMeshComponent.h"
#include "Engine/Blueprint.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "EditorAssetLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "ScopedTransaction.h"
#include "CollisionQueryParams.h"

namespace SplineTools
{

static AActor* FindLevelActorByLabel(UWorld* World, const FString& ActorLabel)
{
	if (!World || ActorLabel.IsEmpty()) return nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->GetActorLabel().Equals(ActorLabel, ESearchCase::IgnoreCase))
			return *It;
	}
	return nullptr;
}

// First level actor whose class was generated from the given Blueprint asset path ("/Game/X/BP_Road" or "/Game/X/BP_Road.BP_Road").
static AActor* FindLevelActorByBlueprint(UWorld* World, const FString& BlueprintPath)
{
	if (!World || BlueprintPath.IsEmpty()) return nullptr;
	FString PackagePath = BlueprintPath;
	int32 DotIdx = INDEX_NONE;
	if (PackagePath.FindChar(TEXT('.'), DotIdx)) PackagePath = PackagePath.Left(DotIdx);
	const FString ClassPrefix = PackagePath + TEXT(".");
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->GetClass() && It->GetClass()->GetPathName().StartsWith(ClassPrefix))
			return *It;
	}
	return nullptr;
}

// Accepts [pitch, yaw, roll], {pitch, yaw, roll} or editor-style {x: roll, y: pitch, z: yaw}. bOutArrayForm tells the caller
// whether the ambiguous array form was used (for the roll guard).
static bool ParseRotatorField(const TSharedPtr<FJsonObject>& Args, const TCHAR* FieldName, FRotator& OutRot, bool& bOutArrayForm)
{
	bOutArrayForm = false;
	const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
	if (Args->TryGetArrayField(FieldName, Arr) && Arr && Arr->Num() >= 3)
	{
		bOutArrayForm = true;
		OutRot = FRotator((*Arr)[0]->AsNumber(), (*Arr)[1]->AsNumber(), (*Arr)[2]->AsNumber());
		return true;
	}
	const TSharedPtr<FJsonObject>* Sub = nullptr;
	if (Args->TryGetObjectField(FieldName, Sub) && Sub)
	{
		double P = 0, Ya = 0, R = 0;
		const bool bNamedAxes = (*Sub)->HasField(TEXT("pitch")) || (*Sub)->HasField(TEXT("yaw")) || (*Sub)->HasField(TEXT("roll"));
		if (bNamedAxes)
		{
			(*Sub)->TryGetNumberField(TEXT("pitch"), P); (*Sub)->TryGetNumberField(TEXT("yaw"), Ya); (*Sub)->TryGetNumberField(TEXT("roll"), R);
		}
		else
		{
			(*Sub)->TryGetNumberField(TEXT("x"), R); (*Sub)->TryGetNumberField(TEXT("y"), P); (*Sub)->TryGetNumberField(TEXT("z"), Ya);
		}
		OutRot = FRotator(P, Ya, R);
		return true;
	}
	return false;
}

static USplineComponent* FindSplineComponent(UBlueprint* Blueprint, const FString& ComponentName)
{
	if (!Blueprint->SimpleConstructionScript) return nullptr;
	for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
	{
		if (!Node->ComponentTemplate) continue;
		USplineComponent* Spline = Cast<USplineComponent>(Node->ComponentTemplate);
		if (!Spline) continue;
		if (ComponentName.IsEmpty() || Node->GetVariableName().ToString() == ComponentName || Node->ComponentTemplate->GetName().Contains(ComponentName))
			return Spline;
	}
	return nullptr;
}

void HandleAddSplineComponent(const FString& BlueprintPath, const FString& ComponentName,
	bool bClosedLoop, FString& OutJsonString, FString& OutError)
{

	UBlueprint* Blueprint = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(BlueprintPath));
	if (!Blueprint) { OutError = TEXT("Blueprint not found: ") + BlueprintPath; return; }

	USimpleConstructionScript* SCS = Blueprint->SimpleConstructionScript;
	if (!SCS) { OutError = TEXT("Blueprint has no SCS (not an Actor-based Blueprint)"); return; }

	FString CompName = ComponentName.IsEmpty() ? TEXT("Spline") : ComponentName;

	if (FindSplineComponent(Blueprint, CompName))
	{
		OutJsonString = FString::Printf(TEXT("{\"success\":true,\"component\":\"%s\",\"message\":\"SplineComponent already exists\"}"), *CompName);
		return;
	}

	USCS_Node* Node = SCS->CreateNode(USplineComponent::StaticClass(), *CompName);
	USplineComponent* SplineComp = Cast<USplineComponent>(Node->ComponentTemplate);
	if (SplineComp)
	{
		SplineComp->SetClosedLoop(bClosedLoop, false);
	}
	SCS->AddNode(Node);

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	Blueprint->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(BlueprintPath, false);

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"component\":\"%s\",\"closed_loop\":%s,\"message\":\"SplineComponent added. Use set_spline_points to define the path.\"}"),
		*CompName, bClosedLoop ? TEXT("true") : TEXT("false"));
}

void HandleSetSplinePoints(const FString& BlueprintPath, const FString& ComponentName,
	const TArray<FVector>& Points, FString& OutJsonString, FString& OutError)
{

	if (Points.Num() < 2) { OutError = TEXT("At least 2 points required"); return; }

	UBlueprint* Blueprint = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(BlueprintPath));
	if (!Blueprint) { OutError = TEXT("Blueprint not found: ") + BlueprintPath; return; }

	USplineComponent* SplineComp = FindSplineComponent(Blueprint, ComponentName);
	if (!SplineComp) { OutError = TEXT("SplineComponent not found. Call add_spline_component first."); return; }

	SplineComp->SetSplinePoints(Points, ESplineCoordinateSpace::Local, true);

	FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
	Blueprint->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(BlueprintPath, false);

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"point_count\":%d,\"spline_length\":%.0f}"),
		Points.Num(), SplineComp->GetSplineLength());
}

void HandleSpawnSplineActor(const FString& ActorLabel, const FString& SavePath,
	float LocationX, float LocationY, float LocationZ,
	const TArray<FVector>& Points, bool bClosedLoop,
	FString& OutJsonString, FString& OutError)
{

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World) { OutError = TEXT("No editor world available"); return; }

	FString BpName = ActorLabel.IsEmpty() ? TEXT("BP_SplineActor") : ActorLabel;
	if (BpName.IsEmpty()) { OutError = TEXT("name is required"); return; }
	FString PackagePath = SavePath;
	while (PackagePath.EndsWith(TEXT("/"))) PackagePath = PackagePath.LeftChop(1);
	PackagePath += TEXT("/") + BpName;

	UBlueprint* Blueprint = nullptr;
	if (!FPackageName::DoesPackageExist(PackagePath))
	{
		Blueprint = FKismetEditorUtilities::CreateBlueprint(
			AActor::StaticClass(),
			CreatePackage(*PackagePath),
			FName(*BpName),
			BPTYPE_Normal,
			UBlueprint::StaticClass(),
			UBlueprintGeneratedClass::StaticClass());
		if (!Blueprint) { OutError = TEXT("Failed to create Blueprint"); return; }
		FAssetRegistryModule::AssetCreated(Blueprint);
	}
	else
	{
		Blueprint = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(PackagePath));
	}
	if (!Blueprint) { OutError = TEXT("Blueprint not available"); return; }

	USimpleConstructionScript* SCS = Blueprint->SimpleConstructionScript;
	USplineComponent* SplineComp = FindSplineComponent(Blueprint, TEXT("Spline"));
	if (!SplineComp && SCS)
	{
		USCS_Node* Node = SCS->CreateNode(USplineComponent::StaticClass(), TEXT("Spline"));
		SplineComp = Cast<USplineComponent>(Node->ComponentTemplate);
		if (SplineComp) SplineComp->SetClosedLoop(bClosedLoop, false);
		SCS->AddNode(Node);
	}

	if (SplineComp && Points.Num() >= 2)
	{
		SplineComp->SetSplinePoints(Points, ESplineCoordinateSpace::Local, true);
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	Blueprint->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(Blueprint->GetPathName(), false);

	FTransform SpawnTransform(FRotator::ZeroRotator, FVector(LocationX, LocationY, LocationZ));
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AActor* Spawned = World->SpawnActor(Blueprint->GeneratedClass, &SpawnTransform, Params);
	if (Spawned)
	{
		Spawned->SetActorLabel(ActorLabel);
		GEditor->RedrawLevelEditingViewports();
	}

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"blueprint_path\":\"%s\",\"actor_label\":\"%s\",\"point_count\":%d}"),
		*Blueprint->GetPathName(), *ActorLabel, Points.Num());
}

void HandleGetSplineInfo(const FString& BlueprintPath, const FString& ComponentName,
	FString& OutJsonString, FString& OutError)
{

	UBlueprint* Blueprint = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(BlueprintPath));
	if (!Blueprint) { OutError = TEXT("Blueprint not found: ") + BlueprintPath; return; }

	USplineComponent* SplineComp = FindSplineComponent(Blueprint, ComponentName);
	if (!SplineComp) { OutError = TEXT("No SplineComponent found on Blueprint"); return; }

	int32 PointCount = SplineComp->GetNumberOfSplinePoints();
	float Length = SplineComp->GetSplineLength();
	bool bClosed = SplineComp->IsClosedLoop();

	FString PointsJson = TEXT("[");
	for (int32 i = 0; i < PointCount; ++i)
	{
		FVector Pt = SplineComp->GetLocationAtSplinePoint(i, ESplineCoordinateSpace::Local);
		if (i > 0) PointsJson += TEXT(",");
		PointsJson += FString::Printf(TEXT("[%.0f,%.0f,%.0f]"), Pt.X, Pt.Y, Pt.Z);
	}
	PointsJson += TEXT("]");

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"point_count\":%d,\"length\":%.0f,\"closed_loop\":%s,\"points\":%s}"),
		PointCount, Length, bClosed ? TEXT("true") : TEXT("false"), *PointsJson);
}

void HandleSetSplinePointTangent(const FString& BlueprintPath, const FString& ComponentName,
	int32 PointIndex, const FVector& ArriveTangent, const FVector& LeaveTangent,
	const FString& TangentType, FString& OutJsonString, FString& OutError)
{

	UBlueprint* Blueprint = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(BlueprintPath));
	if (!Blueprint) { OutError = TEXT("Blueprint not found: ") + BlueprintPath; return; }

	USplineComponent* SplineComp = FindSplineComponent(Blueprint, ComponentName);
	if (!SplineComp) { OutError = TEXT("No SplineComponent found. Call add_spline_component first."); return; }

	int32 PointCount = SplineComp->GetNumberOfSplinePoints();
	if (PointIndex < 0 || PointIndex >= PointCount)
	{
		OutError = FString::Printf(TEXT("Point index %d out of range (0-%d)"), PointIndex, PointCount - 1);
		return;
	}

	ESplinePointType::Type PointType = ESplinePointType::CurveCustomTangent;
	if (TangentType.Equals(TEXT("Linear"), ESearchCase::IgnoreCase))
		PointType = ESplinePointType::Linear;
	else if (TangentType.Equals(TEXT("Cubic"), ESearchCase::IgnoreCase) ||
	         TangentType.Equals(TEXT("Curve"), ESearchCase::IgnoreCase))
		PointType = ESplinePointType::Curve;
	else if (TangentType.Equals(TEXT("Constant"), ESearchCase::IgnoreCase))
		PointType = ESplinePointType::Constant;

	SplineComp->SetTangentsAtSplinePoint(PointIndex, ArriveTangent, LeaveTangent,
		ESplineCoordinateSpace::Local, false);
	SplineComp->SetSplinePointType(PointIndex, PointType, true);
	SplineComp->UpdateSpline();

	FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
	Blueprint->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(BlueprintPath, false);

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"point_index\":%d,\"tangent_type\":\"%s\",\"arrive\":[%.1f,%.1f,%.1f],\"leave\":[%.1f,%.1f,%.1f]}"),
		PointIndex, *TangentType,
		ArriveTangent.X, ArriveTangent.Y, ArriveTangent.Z,
		LeaveTangent.X, LeaveTangent.Y, LeaveTangent.Z);
}

void HandleAddSplinePoint(const FString& BlueprintPath, const FString& ComponentName,
	const FVector& Point, int32 InsertIndex, FString& OutJsonString, FString& OutError)
{

	UBlueprint* BP = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(BlueprintPath));
	if (!BP) { OutError = TEXT("Blueprint not found: ") + BlueprintPath; return; }

	USplineComponent* Spline = nullptr;
	if (BP->SimpleConstructionScript)
	{
		for (USCS_Node* Node : BP->SimpleConstructionScript->GetAllNodes())
		{
			if (Node && Node->ComponentTemplate && Node->ComponentTemplate->IsA<USplineComponent>())
			{
				if (ComponentName.IsEmpty() || Node->GetVariableName().ToString().Contains(ComponentName, ESearchCase::IgnoreCase))
				{
					Spline = Cast<USplineComponent>(Node->ComponentTemplate);
					break;
				}
			}
		}
	}
	if (!Spline) { OutError = TEXT("Spline component not found"); return; }

	int32 Idx = FMath::Clamp(InsertIndex, 0, Spline->GetNumberOfSplinePoints());
	Spline->AddSplinePointAtIndex(Point, Idx, ESplineCoordinateSpace::World, true);

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
	UEditorAssetLibrary::SaveAsset(BlueprintPath, false);

	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"point_index\":%d,\"total_points\":%d}"), Idx, Spline->GetNumberOfSplinePoints());
}

void HandleRemoveSplinePoint(const FString& BlueprintPath, const FString& ComponentName,
	int32 PointIndex, FString& OutJsonString, FString& OutError)
{

	UBlueprint* BP = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(BlueprintPath));
	if (!BP) { OutError = TEXT("Blueprint not found: ") + BlueprintPath; return; }

	USplineComponent* Spline = nullptr;
	if (BP->SimpleConstructionScript)
	{
		for (USCS_Node* Node : BP->SimpleConstructionScript->GetAllNodes())
		{
			if (Node && Node->ComponentTemplate && Node->ComponentTemplate->IsA<USplineComponent>())
			{
				if (ComponentName.IsEmpty() || Node->GetVariableName().ToString().Contains(ComponentName, ESearchCase::IgnoreCase))
				{
					Spline = Cast<USplineComponent>(Node->ComponentTemplate);
					break;
				}
			}
		}
	}
	if (!Spline) { OutError = TEXT("Spline component not found"); return; }

	if (PointIndex < 0 || PointIndex >= Spline->GetNumberOfSplinePoints())
	{
		OutError = FString::Printf(TEXT("Point index %d out of range (0-%d)"), PointIndex, Spline->GetNumberOfSplinePoints() - 1);
		return;
	}

	Spline->RemoveSplinePoint(PointIndex, true);
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
	UEditorAssetLibrary::SaveAsset(BlueprintPath, false);

	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"removed_index\":%d,\"remaining_points\":%d}"), PointIndex, Spline->GetNumberOfSplinePoints());
}

void HandleSetSplineProperties(const FString& BlueprintPath, const FString& ComponentName,
	bool bClosedLoop, const FString& SplineType, FString& OutJsonString, FString& OutError)
{

	UBlueprint* BP = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(BlueprintPath));
	if (!BP) { OutError = TEXT("Blueprint not found: ") + BlueprintPath; return; }

	USplineComponent* Spline = nullptr;
	if (BP->SimpleConstructionScript)
	{
		for (USCS_Node* Node : BP->SimpleConstructionScript->GetAllNodes())
		{
			if (Node && Node->ComponentTemplate && Node->ComponentTemplate->IsA<USplineComponent>())
			{
				if (ComponentName.IsEmpty() || Node->GetVariableName().ToString().Contains(ComponentName, ESearchCase::IgnoreCase))
				{
					Spline = Cast<USplineComponent>(Node->ComponentTemplate);
					break;
				}
			}
		}
	}
	if (!Spline) { OutError = TEXT("Spline component not found"); return; }

	Spline->SetClosedLoop(bClosedLoop, true);

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
	UEditorAssetLibrary::SaveAsset(BlueprintPath, false);

	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"closed_loop\":%s,\"point_count\":%d}"),
		bClosedLoop ? TEXT("true") : TEXT("false"), Spline->GetNumberOfSplinePoints());
}

void HandleSetSplineMesh(const FString& ActorLabel, const FString& BlueprintPath,
	const FString& SplineComponentName, const FString& MeshPath, const FString& ForwardAxis,
	const FString& MaterialPath, bool bCollision, FString& OutJsonString, FString& OutError)
{

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World) { OutError = TEXT("No editor world available"); return; }

	if (MeshPath.IsEmpty()) { OutError = TEXT("mesh_path is required"); return; }
	UStaticMesh* Mesh = Cast<UStaticMesh>(UEditorAssetLibrary::LoadAsset(MeshPath));
	if (!Mesh) { OutError = FString::Printf(TEXT("Could not load StaticMesh at '%s'"), *MeshPath); return; }

	UMaterialInterface* Material = nullptr;
	if (!MaterialPath.IsEmpty())
	{
		Material = Cast<UMaterialInterface>(UEditorAssetLibrary::LoadAsset(MaterialPath));
		if (!Material) { OutError = FString::Printf(TEXT("Could not load Material at '%s'"), *MaterialPath); return; }
	}

	ESplineMeshAxis::Type Axis = ESplineMeshAxis::X;
	if (!ForwardAxis.IsEmpty())
	{
		const FString AxisUpper = ForwardAxis.ToUpper();
		if      (AxisUpper == TEXT("X")) Axis = ESplineMeshAxis::X;
		else if (AxisUpper == TEXT("Y")) Axis = ESplineMeshAxis::Y;
		else if (AxisUpper == TEXT("Z")) Axis = ESplineMeshAxis::Z;
		else { OutError = FString::Printf(TEXT("forward_axis must be X, Y or Z (got '%s')"), *ForwardAxis); return; }
	}

	AActor* Actor = FindLevelActorByLabel(World, ActorLabel);
	if (!Actor && !BlueprintPath.IsEmpty()) Actor = FindLevelActorByBlueprint(World, BlueprintPath);
	if (!Actor)
	{
		OutError = ActorLabel.IsEmpty()
			? FString::Printf(TEXT("No level actor found for blueprint '%s'. Pass actor_label of a level actor that has a SplineComponent (spawn_spline_actor creates one)."), *BlueprintPath)
			: FString::Printf(TEXT("Actor '%s' not found in the level"), *ActorLabel);
		return;
	}

	USplineComponent* Spline = nullptr;
	{
		TArray<USplineComponent*> Splines;
		Actor->GetComponents<USplineComponent>(Splines);
		for (USplineComponent* Candidate : Splines)
		{
			if (!Candidate) continue;
			if (SplineComponentName.IsEmpty() || Candidate->GetName().Contains(SplineComponentName, ESearchCase::IgnoreCase))
			{
				Spline = Candidate;
				break;
			}
		}
	}
	if (!Spline) { OutError = FString::Printf(TEXT("Actor '%s' has no SplineComponent%s"), *Actor->GetActorLabel(), SplineComponentName.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" matching '%s'"), *SplineComponentName)); return; }

	const int32 NumPoints = Spline->GetNumberOfSplinePoints();
	if (NumPoints < 2) { OutError = TEXT("Spline needs at least 2 points"); return; }
	const int32 NumSegments = Spline->IsClosedLoop() ? NumPoints : NumPoints - 1;

	static const FName SplineMeshTag(TEXT("AxivorSplineMesh"));

	const FScopedTransaction Transaction(NSLOCTEXT("AxivorAI", "SetSplineMesh", "Set Spline Mesh"));
	Actor->Modify();

	// Replace spline meshes created by a previous call (identified by component tag).
	int32 Removed = 0;
	{
		TArray<USplineMeshComponent*> Existing;
		Actor->GetComponents<USplineMeshComponent>(Existing);
		for (USplineMeshComponent* Old : Existing)
		{
			if (!Old || !Old->ComponentHasTag(SplineMeshTag)) continue;
			Old->Modify();
			Old->DestroyComponent();
			++Removed;
		}
	}

	int32 Created = 0;
	for (int32 Seg = 0; Seg < NumSegments; ++Seg)
	{
		const int32 NextIdx = (Seg + 1) % NumPoints;
		// Local space of the spline component; the mesh component is attached directly to the spline so the frames match.
		const FVector StartPos = Spline->GetLocationAtSplinePoint(Seg, ESplineCoordinateSpace::Local);
		const FVector StartTan = Spline->GetLeaveTangentAtSplinePoint(Seg, ESplineCoordinateSpace::Local);
		const FVector EndPos   = Spline->GetLocationAtSplinePoint(NextIdx, ESplineCoordinateSpace::Local);
		const FVector EndTan   = Spline->GetArriveTangentAtSplinePoint(NextIdx, ESplineCoordinateSpace::Local);

		USplineMeshComponent* SMC = NewObject<USplineMeshComponent>(Actor, USplineMeshComponent::StaticClass(), NAME_None, RF_Transactional);
		if (!SMC) continue;
		SMC->ComponentTags.Add(SplineMeshTag);
		SMC->SetMobility(EComponentMobility::Static);
		SMC->SetForwardAxis(Axis, false);
		SMC->SetStaticMesh(Mesh);
		if (Material) SMC->SetMaterial(0, Material);
		SMC->SetCollisionEnabled(bCollision ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
		SMC->SetupAttachment(Spline);
		Actor->AddInstanceComponent(SMC);
		SMC->RegisterComponent();
		SMC->SetStartAndEnd(StartPos, StartTan, EndPos, EndTan, true);
		++Created;
	}

	Actor->MarkPackageDirty();
	if (GEditor) GEditor->RedrawLevelEditingViewports();

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"actor_label\":\"%s\",\"spline_component\":\"%s\",\"mesh_path\":\"%s\",\"forward_axis\":\"%s\",")
		TEXT("\"segments_created\":%d,\"removed_previous\":%d,\"closed_loop\":%s,\"collision\":%s}"),
		*Actor->GetActorLabel(), *Spline->GetName(), *MeshPath,
		Axis == ESplineMeshAxis::X ? TEXT("X") : (Axis == ESplineMeshAxis::Y ? TEXT("Y") : TEXT("Z")),
		Created, Removed, Spline->IsClosedLoop() ? TEXT("true") : TEXT("false"), bCollision ? TEXT("true") : TEXT("false"));
}

void HandleScatterActorsAlongSpline(const FString& ActorLabel, const FString& MeshPath,
	float Spacing, float RandomOffset, const FString& RotationMode,
	bool bAlignToGround, const FRotator& FixedRotation,
	FString& OutJsonString, FString& OutError)
{

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World) { OutError = TEXT("No editor world available"); return; }

	AActor* SplineActor = FindLevelActorByLabel(World, ActorLabel);
	if (!SplineActor) { OutError = FString::Printf(TEXT("Actor '%s' not found"), *ActorLabel); return; }

	USplineComponent* Spline = SplineActor->FindComponentByClass<USplineComponent>();
	if (!Spline) { OutError = FString::Printf(TEXT("Actor '%s' has no SplineComponent"), *ActorLabel); return; }

	if (Spacing <= 0.f) Spacing = 200.f;
	float SplineLength = Spline->GetSplineLength();
	int32 Count = FMath::FloorToInt(SplineLength / Spacing);
	if (Count <= 0) { OutError = TEXT("Spline too short for the given spacing"); return; }

	UStaticMesh* Mesh = nullptr;
	if (!MeshPath.IsEmpty())
	{
		Mesh = Cast<UStaticMesh>(UEditorAssetLibrary::LoadAsset(MeshPath));
		if (!Mesh) { OutError = FString::Printf(TEXT("Could not load StaticMesh at '%s'"), *MeshPath); return; }
	}

	// align_to_spline (default): full spline rotation. yaw_only: spline yaw, upright. random: upright + random yaw.
	// none/identity: zero rotation. fixed: FixedRotation as given.
	FString RotMode = RotationMode.ToLower();
	if (RotMode.IsEmpty() || RotMode == TEXT("spline")) RotMode = TEXT("align_to_spline");
	if (RotMode != TEXT("align_to_spline") && RotMode != TEXT("yaw_only") && RotMode != TEXT("random") &&
		RotMode != TEXT("none") && RotMode != TEXT("identity") && RotMode != TEXT("fixed"))
	{
		OutError = FString::Printf(TEXT("Unknown rotation_mode '%s' (expected align_to_spline | yaw_only | random | none | fixed)"), *RotationMode);
		return;
	}

	const FScopedTransaction Transaction(NSLOCTEXT("AxivorAI", "ScatterActorsAlongSpline", "Scatter Actors Along Spline"));

	FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(AxivorScatterSpline), false);

	int32 Spawned = 0;
	int32 SkippedNoGround = 0;
	for (int32 i = 0; i <= Count; ++i)
	{
		float Distance = i * Spacing;
		FVector Location = Spline->GetLocationAtDistanceAlongSpline(Distance, ESplineCoordinateSpace::World);
		FRotator Rotation = Spline->GetRotationAtDistanceAlongSpline(Distance, ESplineCoordinateSpace::World);

		if (RandomOffset > 0.f)
		{
			Location.X += FMath::RandRange(-RandomOffset, RandomOffset);
			Location.Y += FMath::RandRange(-RandomOffset, RandomOffset);
		}

		if (bAlignToGround)
		{
			// Re-trace after the offset so samples land on the actual ground; previously spawned actors are ignored.
			FHitResult Hit;
			const FVector TraceStart = Location + FVector(0, 0, 10000.0);
			const FVector TraceEnd = Location - FVector(0, 0, 10000.0);
			if (!World->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, ECC_WorldStatic, TraceParams))
			{
				++SkippedNoGround;
				continue;
			}
			Location.Z = Hit.ImpactPoint.Z;
		}

		if (RotMode == TEXT("yaw_only"))
		{
			Rotation.Pitch = 0.0;
			Rotation.Roll = 0.0;
		}
		else if (RotMode == TEXT("random"))
		{
			Rotation = FRotator(0.0, (double)FMath::RandRange(0.f, 360.f), 0.0);
		}
		else if (RotMode == TEXT("none") || RotMode == TEXT("identity"))
		{
			Rotation = FRotator::ZeroRotator;
		}
		else if (RotMode == TEXT("fixed"))
		{
			Rotation = FixedRotation;
		}

		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

		AActor* NewActor = World->SpawnActor<AActor>(AStaticMeshActor::StaticClass(), Location, Rotation, SpawnParams);
		if (NewActor)
		{
			NewActor->Modify();
			NewActor->SetActorLabel(FString::Printf(TEXT("%s_Scatter_%d"), *ActorLabel, i));
			if (Mesh)
			{
				UStaticMeshComponent* SMC = NewActor->FindComponentByClass<UStaticMeshComponent>();
				if (SMC) SMC->SetStaticMesh(Mesh);
			}
			TraceParams.AddIgnoredActor(NewActor);
			Spawned++;
		}
	}

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"spline_actor\":\"%s\",\"spawned\":%d,\"skipped_no_ground\":%d,\"spacing\":%.1f,\"spline_length\":%.1f,\"rotation_mode\":\"%s\",\"align_to_ground\":%s}"),
		*ActorLabel, Spawned, SkippedNoGround, Spacing, SplineLength, *RotMode, bAlignToGround ? TEXT("true") : TEXT("false"));
}

void HandleSetSplineMeshComponentProperties(
	const FString& BlueprintPath, const FString& ComponentName,
	const FString& ForwardAxis,
	float StartOffsetX, float StartOffsetY, float EndOffsetX, float EndOffsetY,
	float StartScaleX, float StartScaleY, float EndScaleX, float EndScaleY,
	float StartRoll, float EndRoll,
	FString& OutJsonString, FString& OutError)
{

	UBlueprint* BP = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(BlueprintPath));
	if (!BP) { OutError = TEXT("Blueprint not found: ") + BlueprintPath; return; }
	if (!BP->SimpleConstructionScript) { OutError = TEXT("Blueprint has no SCS"); return; }

	USCS_Node* TargetNode = nullptr;
	for (USCS_Node* Node : BP->SimpleConstructionScript->GetAllNodes())
	{
		if (!Node || !Node->ComponentTemplate) continue;
		if (Node->GetVariableName().ToString().Equals(ComponentName, ESearchCase::IgnoreCase) ||
			Node->ComponentTemplate->GetName().Equals(ComponentName, ESearchCase::IgnoreCase))
		{
			TargetNode = Node;
			break;
		}
	}
	if (!TargetNode) { OutError = FString::Printf(TEXT("Component '%s' not found in Blueprint SCS"), *ComponentName); return; }

	USplineMeshComponent* SMC = Cast<USplineMeshComponent>(TargetNode->ComponentTemplate);
	if (!SMC) { OutError = FString::Printf(TEXT("Component '%s' is not a SplineMeshComponent"), *ComponentName); return; }

	constexpr float NoChange = -999.f;
	TArray<FString> Changed;

	if (!ForwardAxis.IsEmpty())
	{
		FString AxisUpper = ForwardAxis.ToUpper();
		if      (AxisUpper == TEXT("X")) { SMC->ForwardAxis = ESplineMeshAxis::X; Changed.Add(TEXT("forward_axis=X")); }
		else if (AxisUpper == TEXT("Y")) { SMC->ForwardAxis = ESplineMeshAxis::Y; Changed.Add(TEXT("forward_axis=Y")); }
		else if (AxisUpper == TEXT("Z")) { SMC->ForwardAxis = ESplineMeshAxis::Z; Changed.Add(TEXT("forward_axis=Z")); }
	}

	if (StartOffsetX != NoChange) { SMC->SplineParams.StartOffset.X = StartOffsetX; Changed.Add(TEXT("start_offset_x")); }
	if (StartOffsetY != NoChange) { SMC->SplineParams.StartOffset.Y = StartOffsetY; Changed.Add(TEXT("start_offset_y")); }
	if (EndOffsetX   != NoChange) { SMC->SplineParams.EndOffset.X   = EndOffsetX;   Changed.Add(TEXT("end_offset_x")); }
	if (EndOffsetY   != NoChange) { SMC->SplineParams.EndOffset.Y   = EndOffsetY;   Changed.Add(TEXT("end_offset_y")); }

	if (StartScaleX != NoChange) { SMC->SplineParams.StartScale.X = StartScaleX; Changed.Add(TEXT("start_scale_x")); }
	if (StartScaleY != NoChange) { SMC->SplineParams.StartScale.Y = StartScaleY; Changed.Add(TEXT("start_scale_y")); }
	if (EndScaleX   != NoChange) { SMC->SplineParams.EndScale.X   = EndScaleX;   Changed.Add(TEXT("end_scale_x")); }
	if (EndScaleY   != NoChange) { SMC->SplineParams.EndScale.Y   = EndScaleY;   Changed.Add(TEXT("end_scale_y")); }

	if (StartRoll != NoChange) { SMC->SplineParams.StartRoll = StartRoll; Changed.Add(TEXT("start_roll")); }
	if (EndRoll   != NoChange) { SMC->SplineParams.EndRoll   = EndRoll;   Changed.Add(TEXT("end_roll")); }

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
	BP->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(BlueprintPath, false);

	FString ChangedStr;
	for (int32 i = 0; i < Changed.Num(); ++i) { if (i > 0) ChangedStr += TEXT(", "); ChangedStr += Changed[i]; }
	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"component\":\"%s\",\"changed\":\"%s\"}"), *ComponentName, *ChangedStr);
}

void HandleAddSplinePointFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString BlueprintPath, ComponentName;
	if (!Args->TryGetStringField(TEXT("blueprint_path"), BlueprintPath))
		Args->TryGetStringField(TEXT("spline_blueprint_path"), BlueprintPath);
	Args->TryGetStringField(TEXT("component_name"), ComponentName);

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("points"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			double PX = 0, PY = 0, PZ = 0;
			const TArray<TSharedPtr<FJsonValue>>* LocArr = nullptr;
			if (Item->TryGetArrayField(TEXT("location"), LocArr) && LocArr && LocArr->Num() >= 3)
			{ (*LocArr)[0]->TryGetNumber(PX); (*LocArr)[1]->TryGetNumber(PY); (*LocArr)[2]->TryGetNumber(PZ); }
			else { Item->TryGetNumberField(TEXT("x"), PX); Item->TryGetNumberField(TEXT("y"), PY); Item->TryGetNumberField(TEXT("z"), PZ); }
			FVector ArriveTan(0,0,0), LeaveTan(0,0,0);
			const TArray<TSharedPtr<FJsonValue>>* TanArr = nullptr;
			if (Item->TryGetArrayField(TEXT("arrive_tangent"), TanArr) && TanArr && TanArr->Num() >= 3)
			{ double X=0,Y=0,Z=0; (*TanArr)[0]->TryGetNumber(X); (*TanArr)[1]->TryGetNumber(Y); (*TanArr)[2]->TryGetNumber(Z); ArriveTan = FVector(X,Y,Z); }
			if (Item->TryGetArrayField(TEXT("leave_tangent"), TanArr) && TanArr && TanArr->Num() >= 3)
			{ double X=0,Y=0,Z=0; (*TanArr)[0]->TryGetNumber(X); (*TanArr)[1]->TryGetNumber(Y); (*TanArr)[2]->TryGetNumber(Z); LeaveTan = FVector(X,Y,Z); }
			FString ItemOut, ItemErr;
			HandleAddSplinePoint(BlueprintPath, ComponentName, FVector(PX, PY, PZ), -1, ItemOut, ItemErr);
			if (ItemErr.IsEmpty()) Batch.AddSuccess(i);
			else Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJsonString);
		return;
	}
	double PX = 0, PY = 0, PZ = 0, II = -1;
	Args->TryGetNumberField(TEXT("point_x"), PX); Args->TryGetNumberField(TEXT("point_y"), PY); Args->TryGetNumberField(TEXT("point_z"), PZ);
	Args->TryGetNumberField(TEXT("insert_index"), II);
	HandleAddSplinePoint(BlueprintPath, ComponentName, FVector(PX, PY, PZ), (int32)II, OutJsonString, OutError);
}

void HandleSetSplinePointTangentFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString BlueprintPath, ComponentName;
	Args->TryGetStringField(TEXT("blueprint_path"), BlueprintPath);
	Args->TryGetStringField(TEXT("component_name"), ComponentName);

	auto ParseVecFromObj = [](const TSharedPtr<FJsonObject>& Obj, const FString& Key) -> FVector
	{
		FVector V(0, 0, 0);
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (Obj->TryGetArrayField(Key, Arr) && Arr && Arr->Num() >= 3)
		{ double X=0,Y=0,Z=0; (*Arr)[0]->TryGetNumber(X); (*Arr)[1]->TryGetNumber(Y); (*Arr)[2]->TryGetNumber(Z); V = FVector(X,Y,Z); }
		return V;
	};

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("tangents"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			double PtIdx = 0; Item->TryGetNumberField(TEXT("point_index"), PtIdx);
			FVector Arrive = ParseVecFromObj(Item, TEXT("arrive"));
			FVector Leave = Item->HasField(TEXT("leave")) ? ParseVecFromObj(Item, TEXT("leave")) : Arrive;
			FString TangentType; Item->TryGetStringField(TEXT("tangent_type"), TangentType);
			if (TangentType.IsEmpty()) TangentType = TEXT("CurveCustomTangent");
			FString ItemOut, ItemErr;
			HandleSetSplinePointTangent(BlueprintPath, ComponentName, (int32)PtIdx, Arrive, Leave, TangentType, ItemOut, ItemErr);
			if (ItemErr.IsEmpty()) Batch.AddSuccess(i);
			else Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJsonString);
		return;
	}
	double PtIdx = 0; Args->TryGetNumberField(TEXT("point_index"), PtIdx);
	FVector Arrive = ParseVecFromObj(Args, TEXT("arrive_tangent"));
	FVector Leave = Args->HasField(TEXT("leave_tangent")) ? ParseVecFromObj(Args, TEXT("leave_tangent")) : Arrive;
	FString TangentType; Args->TryGetStringField(TEXT("tangent_type"), TangentType);
	if (TangentType.IsEmpty()) TangentType = TEXT("CurveCustomTangent");
	HandleSetSplinePointTangent(BlueprintPath, ComponentName, (int32)PtIdx, Arrive, Leave, TangentType, OutJsonString, OutError);
}

static TArray<FVector> ExtractVectorArray(const TSharedPtr<FJsonObject>& Args, const FString& Key)
{
	TArray<FVector> Out;
	const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
	if (!Args->TryGetArrayField(Key, Arr) || !Arr) return Out;
	for (const TSharedPtr<FJsonValue>& Val : *Arr)
	{
		const TArray<TSharedPtr<FJsonValue>>* Coords = nullptr;
		if (Val->TryGetArray(Coords) && Coords && Coords->Num() >= 3)
		{
			double X = 0, Y = 0, Z = 0;
			(*Coords)[0]->TryGetNumber(X);
			(*Coords)[1]->TryGetNumber(Y);
			(*Coords)[2]->TryGetNumber(Z);
			Out.Add(FVector((float)X, (float)Y, (float)Z));
		}
	}
	return Out;
}

void HandleAddSplineComponentFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString BlueprintPath, ComponentName;
	bool bClosedLoop = false;
	Args->TryGetStringField(TEXT("blueprint_path"), BlueprintPath);
	Args->TryGetStringField(TEXT("component_name"), ComponentName);
	Args->TryGetBoolField(TEXT("closed_loop"), bClosedLoop);
	HandleAddSplineComponent(BlueprintPath, ComponentName, bClosedLoop, OutJsonString, OutError);
}

void HandleSetSplinePointsFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString BlueprintPath, ComponentName;
	Args->TryGetStringField(TEXT("blueprint_path"), BlueprintPath);
	Args->TryGetStringField(TEXT("component_name"), ComponentName);
	TArray<FVector> Points = ExtractVectorArray(Args, TEXT("points"));
	HandleSetSplinePoints(BlueprintPath, ComponentName, Points, OutJsonString, OutError);
}

void HandleSpawnSplineActorFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString ActorLabel = TEXT("SplineActor"), SavePath = TEXT("/Game/Splines");
	double LocationX = 0, LocationY = 0, LocationZ = 0;
	bool bClosedLoop = false;
	Args->TryGetStringField(TEXT("actor_label"), ActorLabel);
	Args->TryGetStringField(TEXT("save_path"), SavePath);
	Args->TryGetNumberField(TEXT("location_x"), LocationX);
	Args->TryGetNumberField(TEXT("location_y"), LocationY);
	Args->TryGetNumberField(TEXT("location_z"), LocationZ);
	Args->TryGetBoolField(TEXT("closed_loop"), bClosedLoop);
	TArray<FVector> Points = ExtractVectorArray(Args, TEXT("points"));
	HandleSpawnSplineActor(ActorLabel, SavePath,
		(float)LocationX, (float)LocationY, (float)LocationZ,
		Points, bClosedLoop, OutJsonString, OutError);
}

void HandleGetSplineInfoFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString BlueprintPath, ComponentName;
	Args->TryGetStringField(TEXT("blueprint_path"), BlueprintPath);
	Args->TryGetStringField(TEXT("component_name"), ComponentName);
	HandleGetSplineInfo(BlueprintPath, ComponentName, OutJsonString, OutError);
}

void HandleRemoveSplinePointFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString BlueprintPath, ComponentName;
	double PointIndex = 0;
	Args->TryGetStringField(TEXT("blueprint_path"), BlueprintPath);
	Args->TryGetStringField(TEXT("component_name"), ComponentName);
	Args->TryGetNumberField(TEXT("point_index"), PointIndex);
	HandleRemoveSplinePoint(BlueprintPath, ComponentName, (int32)PointIndex, OutJsonString, OutError);
}

void HandleSetSplinePropertiesFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString BlueprintPath, ComponentName, SplineType;
	bool bClosedLoop = false;
	Args->TryGetStringField(TEXT("blueprint_path"), BlueprintPath);
	Args->TryGetStringField(TEXT("component_name"), ComponentName);
	Args->TryGetBoolField(TEXT("closed_loop"), bClosedLoop);
	Args->TryGetStringField(TEXT("spline_type"), SplineType);
	HandleSetSplineProperties(BlueprintPath, ComponentName, bClosedLoop, SplineType, OutJsonString, OutError);
}

void HandleSetSplineMeshFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString ActorLabel, BlueprintPath, SplineComponentName, MeshPath, ForwardAxis, MaterialPath;
	bool bCollision = true;
	if (!Args->TryGetStringField(TEXT("actor_label"), ActorLabel)) Args->TryGetStringField(TEXT("actor"), ActorLabel);
	Args->TryGetStringField(TEXT("blueprint_path"), BlueprintPath);
	if (!Args->TryGetStringField(TEXT("spline_component_name"), SplineComponentName)) Args->TryGetStringField(TEXT("component_name"), SplineComponentName);
	Args->TryGetStringField(TEXT("mesh_path"), MeshPath);
	Args->TryGetStringField(TEXT("forward_axis"), ForwardAxis);
	Args->TryGetStringField(TEXT("material_path"), MaterialPath);
	Args->TryGetBoolField(TEXT("collision"), bCollision);
	HandleSetSplineMesh(ActorLabel, BlueprintPath, SplineComponentName, MeshPath, ForwardAxis, MaterialPath, bCollision, OutJsonString, OutError);
}

void HandleSetSplineMeshComponentPropertiesFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString BlueprintPath, ComponentName, ForwardAxis;
	double StartOffX = -999, StartOffY = -999, EndOffX = -999, EndOffY = -999;
	double StartScaleX = -999, StartScaleY = -999, EndScaleX = -999, EndScaleY = -999;
	double StartRoll = -999, EndRoll = -999;
	Args->TryGetStringField(TEXT("blueprint_path"), BlueprintPath);
	Args->TryGetStringField(TEXT("component_name"), ComponentName);
	Args->TryGetStringField(TEXT("forward_axis"), ForwardAxis);
	Args->TryGetNumberField(TEXT("start_offset_x"), StartOffX);
	Args->TryGetNumberField(TEXT("start_offset_y"), StartOffY);
	Args->TryGetNumberField(TEXT("end_offset_x"), EndOffX);
	Args->TryGetNumberField(TEXT("end_offset_y"), EndOffY);
	Args->TryGetNumberField(TEXT("start_scale_x"), StartScaleX);
	Args->TryGetNumberField(TEXT("start_scale_y"), StartScaleY);
	Args->TryGetNumberField(TEXT("end_scale_x"), EndScaleX);
	Args->TryGetNumberField(TEXT("end_scale_y"), EndScaleY);
	Args->TryGetNumberField(TEXT("start_roll"), StartRoll);
	Args->TryGetNumberField(TEXT("end_roll"), EndRoll);
	HandleSetSplineMeshComponentProperties(BlueprintPath, ComponentName, ForwardAxis,
		(float)StartOffX, (float)StartOffY, (float)EndOffX, (float)EndOffY,
		(float)StartScaleX, (float)StartScaleY, (float)EndScaleX, (float)EndScaleY,
		(float)StartRoll, (float)EndRoll,
		OutJsonString, OutError);
}

void HandleScatterActorsAlongSplineFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString ActorLabel, MeshPath, RotationMode;
	double Spacing = 200, RandomOffset = 0;
	bool bAlignToGround = true;
	bool bAllowTilt = false;
	Args->TryGetStringField(TEXT("actor_label"), ActorLabel);
	Args->TryGetStringField(TEXT("mesh_path"), MeshPath);
	Args->TryGetNumberField(TEXT("spacing"), Spacing);
	Args->TryGetNumberField(TEXT("random_offset"), RandomOffset);
	Args->TryGetStringField(TEXT("rotation_mode"), RotationMode);
	Args->TryGetBoolField(TEXT("align_to_ground"), bAlignToGround);
	Args->TryGetBoolField(TEXT("allow_tilt"), bAllowTilt);

	// Optional fixed rotation (used by rotation_mode "fixed"). Array form is [pitch, yaw, roll]: reject an accidental roll.
	FRotator FixedRotation = FRotator::ZeroRotator;
	bool bRotArrayForm = false;
	if (ParseRotatorField(Args, TEXT("rotation"), FixedRotation, bRotArrayForm))
	{
		if (bRotArrayForm && !bAllowTilt && FMath::Abs(FixedRotation.Roll) > 45.0)
		{
			OutError = FString::Printf(
				TEXT("'rotation' array is [pitch, yaw, roll] and roll=%.1f would tip the actors sideways. If you meant a yaw, pass {\"yaw\":%.1f} (or [0,%.1f,0]); to really roll them, pass allow_tilt:true."),
				FixedRotation.Roll, FixedRotation.Roll, FixedRotation.Roll);
			return;
		}
		if (RotationMode.IsEmpty()) RotationMode = TEXT("fixed");
	}

	HandleScatterActorsAlongSpline(ActorLabel, MeshPath, (float)Spacing, (float)RandomOffset,
		RotationMode, bAlignToGround, FixedRotation, OutJsonString, OutError);
}

}
