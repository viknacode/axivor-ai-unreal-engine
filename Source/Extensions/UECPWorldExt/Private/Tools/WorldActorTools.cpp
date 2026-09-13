// Axivor AI — World Builder: grid snapping, surface alignment, prefab placement and filtered mass edits.

#include "WorldExtCommon.h"
#include "UECPWorldExtModule.h"

#include "ScopedTransaction.h"
#include "EditorAssetLibrary.h"
#include "Engine/Blueprint.h"
#include "Engine/Brush.h"
#include "GameFramework/WorldSettings.h"
#include "Components/SplineComponent.h"
#include "Components/MeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Misc/PackageName.h"
#include "Math/RotationMatrix.h"
#include "Math/RandomStream.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectIterator.h"

namespace WorldExt
{
	// ═════════════════════════════════════════════════════════════════════════
	// world_snap_to_grid
	// ═════════════════════════════════════════════════════════════════════════
	FUECPToolResult HandleSnapToGrid(const TSharedPtr<FJsonObject>& Args)
	{
		UWorld* World = EditorWorld();
		if (!World) return Fail(TEXT("No editor world available."));
		const double GridSize = ArgNum(Args, TEXT("grid_size"), 100.0);
		const double RotStep = ArgNum(Args, TEXT("rotation_step"), 15.0);
		const double ScaleStep = ArgNum(Args, TEXT("scale_step"), 0.25);
		const bool bSnapRot = ArgBool(Args, TEXT("snap_rotation"), false);
		const bool bSnapScale = ArgBool(Args, TEXT("snap_scale"), false);
		const bool bSnapZ = ArgBool(Args, TEXT("snap_z"), true);
		const bool bAllRotAxes = ArgBool(Args, TEXT("rotation_all_axes"), false);
		if (GridSize <= 0.0) return Fail(TEXT("grid_size must be > 0."));

		TArray<FString> Missing; bool bUsedSelection = false;
		const TArray<AActor*> Actors = ResolveActorsOrSelection(Args, World, Missing, bUsedSelection);
		if (Actors.Num() == 0)
			return Fail(bUsedSelection ? TEXT("Nothing selected in the editor. Select actors or pass actors[] labels.") : TEXT("None of the given actors[] labels were found."));

		const FScopedTransaction Transaction(NSLOCTEXT("AxivorAI", "WorldSnapToGrid", "World: Snap To Grid"));
		TArray<TSharedPtr<FJsonValue>> Results;
		int32 Changed = 0;
		for (AActor* Actor : Actors)
		{
			if (!Actor) continue;
			const FVector Loc = Actor->GetActorLocation();
			const FRotator Rot = Actor->GetActorRotation();
			const FVector Scale = Actor->GetActorScale3D();
			FVector NewLoc(FMath::GridSnap(Loc.X, GridSize), FMath::GridSnap(Loc.Y, GridSize), bSnapZ ? FMath::GridSnap(Loc.Z, GridSize) : Loc.Z);
			FRotator NewRot = Rot;
			if (bSnapRot && RotStep > 0.0)
			{
				NewRot.Yaw = FMath::GridSnap(Rot.Yaw, RotStep);
				if (bAllRotAxes) { NewRot.Pitch = FMath::GridSnap(Rot.Pitch, RotStep); NewRot.Roll = FMath::GridSnap(Rot.Roll, RotStep); }
			}
			FVector NewScale = Scale;
			if (bSnapScale && ScaleStep > 0.0)
			{
				NewScale = FVector(FMath::GridSnap(Scale.X, ScaleStep), FMath::GridSnap(Scale.Y, ScaleStep), FMath::GridSnap(Scale.Z, ScaleStep));
				if (FMath::IsNearlyZero(NewScale.X)) NewScale.X = ScaleStep;
				if (FMath::IsNearlyZero(NewScale.Y)) NewScale.Y = ScaleStep;
				if (FMath::IsNearlyZero(NewScale.Z)) NewScale.Z = ScaleStep;
			}
			const bool bMoved = !NewLoc.Equals(Loc, 0.01) || !NewRot.Equals(Rot, 0.01) || !NewScale.Equals(Scale, 0.0001);
			TSharedRef<FJsonObject> R = MakeShared<FJsonObject>();
			R->SetStringField(TEXT("label"), Actor->GetActorLabel());
			R->SetBoolField(TEXT("changed"), bMoved);
			if (bMoved)
			{
				Actor->Modify();
				Actor->SetActorLocationAndRotation(NewLoc, NewRot, false, nullptr, ETeleportType::TeleportPhysics);
				if (bSnapScale) Actor->SetActorScale3D(NewScale);
				Actor->PostEditMove(true);
				Actor->MarkPackageDirty();
				++Changed;
				R->SetObjectField(TEXT("location"), VecJson(NewLoc));
				if (bSnapRot) R->SetObjectField(TEXT("rotation"), RotJson(NewRot));
				if (bSnapScale) R->SetObjectField(TEXT("scale"), VecJson(NewScale));
			}
			Results.Add(MakeShared<FJsonValueObject>(R));
		}
		if (GEditor) GEditor->RedrawLevelEditingViewports();

		TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
		Out->SetBoolField(TEXT("used_selection"), bUsedSelection);
		Out->SetNumberField(TEXT("grid_size"), GridSize);
		if (bSnapRot) Out->SetNumberField(TEXT("rotation_step"), RotStep);
		if (bSnapScale) Out->SetNumberField(TEXT("scale_step"), ScaleStep);
		Out->SetNumberField(TEXT("actors"), Actors.Num());
		Out->SetNumberField(TEXT("changed"), Changed);
		Out->SetArrayField(TEXT("results"), Results);
		if (Missing.Num() > 0)
		{
			TArray<TSharedPtr<FJsonValue>> M; for (const FString& S : Missing) M.Add(MakeShared<FJsonValueString>(S));
			Out->SetArrayField(TEXT("not_found"), M);
		}
		return Ok(Out);
	}

	// ═════════════════════════════════════════════════════════════════════════
	// world_align_to_surface
	// ═════════════════════════════════════════════════════════════════════════
	FUECPToolResult HandleAlignToSurface(const TSharedPtr<FJsonObject>& Args)
	{
		UWorld* World = EditorWorld();
		if (!World) return Fail(TEXT("No editor world available."));
		const bool bAlignNormal = ArgBool(Args, TEXT("align_to_normal"), false);
		const double MaxSlope = ArgNum(Args, TEXT("max_slope"), 90.0);
		const bool bKeepYaw = ArgBool(Args, TEXT("keep_yaw"), true);
		const double Offset = ArgNum(Args, TEXT("offset"), 0.0);
		const double TraceHeight = ArgNum(Args, TEXT("trace_height"), 10000.0);
		const bool bUseBoundsBottom = ArgBool(Args, TEXT("use_bounds_bottom"), true);

		TArray<FString> Missing; bool bUsedSelection = false;
		TArray<AActor*> Actors = ResolveActorsOrSelection(Args, World, Missing, bUsedSelection);
		if (Actors.Num() == 0)
			return Fail(bUsedSelection ? TEXT("Nothing selected in the editor. Select actors or pass actors[] labels.") : TEXT("None of the given actors[] labels were found."));

		const FScopedTransaction Transaction(NSLOCTEXT("AxivorAI", "WorldAlignToSurface", "World: Align To Surface"));
		TArray<TSharedPtr<FJsonValue>> Aligned;
		TArray<TSharedPtr<FJsonValue>> Skipped;
		for (AActor* Actor : Actors)
		{
			if (!Actor) continue;
			TSharedRef<FJsonObject> R = MakeShared<FJsonObject>();
			R->SetStringField(TEXT("label"), Actor->GetActorLabel());
			const FVector Loc = Actor->GetActorLocation();
			FHitResult Hit;
			if (!TraceGround(World, Loc, TraceHeight, 200000.0, Hit, Actor, &Actors))
			{
				R->SetStringField(TEXT("reason"), TEXT("no surface hit below the actor (WorldStatic trace)"));
				Skipped.Add(MakeShared<FJsonValueObject>(R));
				continue;
			}
			const FVector Normal = Hit.ImpactNormal.GetSafeNormal();
			const double SlopeDeg = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Normal.Z, -1.0, 1.0)));
			if (SlopeDeg > MaxSlope)
			{
				R->SetNumberField(TEXT("slope_degrees"), FMath::RoundToDouble(SlopeDeg * 10.0) / 10.0);
				R->SetStringField(TEXT("reason"), FString::Printf(TEXT("surface slope %.1f deg exceeds max_slope %.1f"), SlopeDeg, MaxSlope));
				Skipped.Add(MakeShared<FJsonValueObject>(R));
				continue;
			}
			double PivotToBottom = 0.0; // negative when the pivot sits above the bounds bottom
			if (bUseBoundsBottom)
			{
				FVector Origin(ForceInit), Extent(ForceInit);
				Actor->GetActorBounds(false, Origin, Extent);
				PivotToBottom = Origin.Z - Extent.Z - Loc.Z;
			}
			FRotator NewRot = Actor->GetActorRotation();
			FVector NewLoc = Loc;
			if (bAlignNormal)
			{
				const FVector Forward = FRotator(0.0, NewRot.Yaw, 0.0).Vector();
				NewRot = bKeepYaw ? FRotationMatrix::MakeFromZX(Normal, Forward).Rotator() : FRotationMatrix::MakeFromZ(Normal).Rotator();
				NewLoc = Hit.ImpactPoint + Normal * (Offset - PivotToBottom);
			}
			else
			{
				NewLoc = FVector(Loc.X, Loc.Y, Hit.ImpactPoint.Z + Offset - PivotToBottom);
			}
			Actor->Modify();
			Actor->SetActorLocationAndRotation(NewLoc, NewRot, false, nullptr, ETeleportType::TeleportPhysics);
			Actor->PostEditMove(true);
			Actor->MarkPackageDirty();
			R->SetObjectField(TEXT("location"), VecJson(NewLoc));
			R->SetObjectField(TEXT("rotation"), RotJson(NewRot));
			R->SetNumberField(TEXT("slope_degrees"), FMath::RoundToDouble(SlopeDeg * 10.0) / 10.0);
			R->SetStringField(TEXT("hit_actor"), Hit.GetActor() ? Hit.GetActor()->GetActorLabel() : TEXT(""));
			Aligned.Add(MakeShared<FJsonValueObject>(R));
		}
		if (GEditor) GEditor->RedrawLevelEditingViewports();

		TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
		Out->SetBoolField(TEXT("used_selection"), bUsedSelection);
		Out->SetBoolField(TEXT("align_to_normal"), bAlignNormal);
		Out->SetBoolField(TEXT("keep_yaw"), bKeepYaw);
		Out->SetNumberField(TEXT("aligned_count"), Aligned.Num());
		Out->SetNumberField(TEXT("skipped_count"), Skipped.Num());
		Out->SetArrayField(TEXT("aligned"), Aligned);
		Out->SetArrayField(TEXT("skipped"), Skipped);
		if (Missing.Num() > 0)
		{
			TArray<TSharedPtr<FJsonValue>> M; for (const FString& S : Missing) M.Add(MakeShared<FJsonValueString>(S));
			Out->SetArrayField(TEXT("not_found"), M);
		}
		return Ok(Out);
	}

	// ═════════════════════════════════════════════════════════════════════════
	// world_place_prefab
	// ═════════════════════════════════════════════════════════════════════════
	struct FPlacement
	{
		FVector Location = FVector::ZeroVector;
		FVector PathDirection = FVector::ForwardVector;
	};

	static FString ObjectPathFromAssetPath(const FString& In)
	{
		if (In.Contains(TEXT("."))) return In;
		return In + TEXT(".") + FPackageName::GetShortName(In);
	}

	FUECPToolResult HandlePlacePrefab(const TSharedPtr<FJsonObject>& Args)
	{
		UWorld* World = EditorWorld();
		if (!World) return Fail(TEXT("No editor world available."));
		const FString Prefab = ArgStr(Args, TEXT("prefab"));
		if (Prefab.IsEmpty()) return Fail(TEXT("prefab is required: a Blueprint asset path (/Game/.../BP_House) or a level asset path for a Level Instance (/Game/.../L_Hut)."));
		const FString RotMode = ArgStr(Args, TEXT("rotation_mode"), TEXT("yaw_random")).ToLower();
		if (RotMode != TEXT("yaw_random") && RotMode != TEXT("fixed") && RotMode != TEXT("face_path"))
			return Fail(TEXT("rotation_mode must be yaw_random | fixed | face_path."));
		const bool bSnap = ArgBool(Args, TEXT("snap_to_ground"), true);
		const double GroundOffset = ArgNum(Args, TEXT("ground_offset"), 0.0);
		const int32 Seed = (int32)ArgNum(Args, TEXT("seed"), 1337.0);
		const double ScaleMin = ArgNum(Args, TEXT("scale_min"), 1.0);
		const double ScaleMax = ArgNum(Args, TEXT("scale_max"), ScaleMin);
		const int32 MaxCount = FMath::Max(1, (int32)ArgNum(Args, TEXT("max_count"), 500.0));
		FRotator FixedRot = FRotator::ZeroRotator;
		ArgRotator(Args, TEXT("rotation"), FixedRot);
		FRandomStream Rand(Seed);

		// ── Resolve the prefab: Blueprint class, native class, or level (Level Instance) ──
		UClass* SpawnClass = nullptr;
		FString LevelPath;
		FString Mode;
		{
			IAssetRegistry& AR = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
			const FAssetData AD = AR.GetAssetByObjectPath(FSoftObjectPath(ObjectPathFromAssetPath(Prefab)));
			if (AD.IsValid() && AD.AssetClassPath == UWorld::StaticClass()->GetClassPathName())
			{
				LevelPath = AD.GetSoftObjectPath().ToString();
				Mode = TEXT("level_instance");
			}
			else if (Prefab.StartsWith(TEXT("/Script/")))
			{
				SpawnClass = FindObject<UClass>(nullptr, *Prefab);
				Mode = TEXT("native_class");
			}
			else
			{
				UObject* Asset = UEditorAssetLibrary::LoadAsset(Prefab);
				if (UBlueprint* BP = Cast<UBlueprint>(Asset)) SpawnClass = BP->GeneratedClass;
				else if (UWorld* LW = Cast<UWorld>(Asset)) { LevelPath = LW->GetPathName(); Mode = TEXT("level_instance"); }
				if (SpawnClass) Mode = TEXT("blueprint");
			}
			if (!SpawnClass && LevelPath.IsEmpty())
				return Fail(FString::Printf(TEXT("Could not resolve prefab '%s' as a Blueprint, native actor class or level asset."), *Prefab));
			if (SpawnClass && !SpawnClass->IsChildOf(AActor::StaticClass()))
				return Fail(FString::Printf(TEXT("'%s' is not an Actor class (%s)."), *Prefab, *SpawnClass->GetName()));
		}

		// ── Compute placements ──
		TArray<FPlacement> Placements;
		FString Source;
		const TArray<FVector> Points = ArgVectorArray(Args, TEXT("points"));
		const TSharedPtr<FJsonObject> Grid = ArgObj(Args, TEXT("grid"));
		const TSharedPtr<FJsonObject> Along = ArgObj(Args, TEXT("along_spline"));
		if (Points.Num() > 0)
		{
			Source = TEXT("points");
			for (int32 i = 0; i < Points.Num(); ++i)
			{
				FPlacement P; P.Location = Points[i];
				const int32 Next = (i + 1 < Points.Num()) ? i + 1 : i;
				const int32 Prev = (i > 0) ? i - 1 : i;
				const FVector Dir = (Points[Next] - Points[Prev]).GetSafeNormal2D();
				if (!Dir.IsNearlyZero()) P.PathDirection = Dir;
				Placements.Add(P);
			}
		}
		else if (Grid.IsValid())
		{
			Source = TEXT("grid");
			const int32 Rows = FMath::Max(1, (int32)ArgNum(Grid, TEXT("rows"), 1.0));
			const int32 Cols = FMath::Max(1, (int32)ArgNum(Grid, TEXT("cols"), 1.0));
			const double Spacing = ArgNum(Grid, TEXT("spacing"), 1000.0);
			const double Jitter = ArgNum(Grid, TEXT("jitter"), 0.0);
			FVector Origin = FVector::ZeroVector;
			ArgVector(Grid, TEXT("origin"), Origin);
			const FVector Right = FRotator(0.0, ArgNum(Grid, TEXT("yaw"), 0.0), 0.0).Vector();
			const FVector Fwd = FVector::CrossProduct(FVector::UpVector, Right).GetSafeNormal();
			for (int32 r = 0; r < Rows; ++r)
			{
				for (int32 c = 0; c < Cols; ++c)
				{
					FPlacement P;
					P.Location = Origin + Right * (c * Spacing) + Fwd * (r * Spacing);
					if (Jitter > 0.0) P.Location += FVector(Rand.FRandRange(-Jitter, Jitter), Rand.FRandRange(-Jitter, Jitter), 0.0);
					P.PathDirection = Right;
					Placements.Add(P);
				}
			}
		}
		else if (Along.IsValid())
		{
			Source = TEXT("along_spline");
			const FString SplineLabel = ArgStr(Along, TEXT("actor"));
			AActor* SplineActor = FindActorByLabel(World, SplineLabel);
			if (!SplineActor) return Fail(FString::Printf(TEXT("along_spline.actor '%s' not found."), *SplineLabel));
			USplineComponent* Spline = SplineActor->FindComponentByClass<USplineComponent>();
			if (!Spline) return Fail(FString::Printf(TEXT("Actor '%s' has no SplineComponent."), *SplineLabel));
			const double Spacing = FMath::Max(1.0, ArgNum(Along, TEXT("spacing"), 1000.0));
			const double Lateral = ArgNum(Along, TEXT("offset"), 0.0);
			const double StartOffset = FMath::Max(0.0, ArgNum(Along, TEXT("start_offset"), 0.0));
			const double Length = Spline->GetSplineLength();
			for (double D = StartOffset; D <= Length + KINDA_SMALL_NUMBER; D += Spacing)
			{
				FPlacement P;
				const FVector Dir = Spline->GetDirectionAtDistanceAlongSpline((float)D, ESplineCoordinateSpace::World);
				const FVector RightV = Spline->GetRightVectorAtDistanceAlongSpline((float)D, ESplineCoordinateSpace::World);
				P.Location = Spline->GetLocationAtDistanceAlongSpline((float)D, ESplineCoordinateSpace::World) + RightV * Lateral;
				if (!Dir.IsNearlyZero()) P.PathDirection = Dir;
				Placements.Add(P);
				if (Placements.Num() >= MaxCount) break;
			}
		}
		else return Fail(TEXT("Provide points[] ([x,y,z]...), grid{rows,cols,spacing,origin?,yaw?,jitter?} or along_spline{actor,spacing,offset?,start_offset?}."));
		if (Placements.Num() == 0) return Fail(TEXT("No placements computed."));
		if (Placements.Num() > MaxCount) Placements.SetNum(MaxCount);

		const FString Prefix = SanitizeLabel(ArgStr(Args, TEXT("name_prefix"), FPackageName::GetShortName(Prefab)));

		// ── Spawn ──
		const FScopedTransaction Transaction(NSLOCTEXT("AxivorAI", "WorldPlacePrefab", "World: Place Prefab"));
		TArray<TSharedPtr<FJsonValue>> Labels;
		TArray<TSharedPtr<FJsonValue>> Placed;
		TArray<TSharedPtr<FJsonValue>> Failed;
		int32 NoGround = 0;
		for (int32 i = 0; i < Placements.Num(); ++i)
		{
			FPlacement& P = Placements[i];
			if (bSnap)
			{
				FHitResult Hit;
				if (TraceGround(World, P.Location, 10000.0, 200000.0, Hit)) P.Location.Z = Hit.ImpactPoint.Z + GroundOffset;
				else ++NoGround;
			}
			FRotator Rot = FRotator::ZeroRotator;
			if (RotMode == TEXT("yaw_random")) Rot = FRotator(0.0, Rand.FRandRange(0.0, 360.0), 0.0);
			else if (RotMode == TEXT("fixed")) Rot = FixedRot;
			else Rot = FRotator(0.0, P.PathDirection.Rotation().Yaw, 0.0) + FRotator(0.0, FixedRot.Yaw, 0.0);
			const double S = (ScaleMax > ScaleMin) ? Rand.FRandRange(ScaleMin, ScaleMax) : ScaleMin;
			const FString Label = FString::Printf(TEXT("%s_%03d"), *Prefix, i + 1);

			AActor* Spawned = nullptr;
			FString Err;
			if (SpawnClass)
			{
				FActorSpawnParameters SP;
				SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
				const FTransform TM(Rot, P.Location, FVector(S));
				Spawned = World->SpawnActor(SpawnClass, &TM, SP);
				if (!Spawned) Err = TEXT("SpawnActor returned null");
			}
			else
			{
				TSharedRef<FJsonObject> LA = MakeShared<FJsonObject>();
				LA->SetStringField(TEXT("actor_label"), Label);
				LA->SetStringField(TEXT("level_path"), LevelPath);
				LA->SetNumberField(TEXT("location_x"), P.Location.X);
				LA->SetNumberField(TEXT("location_y"), P.Location.Y);
				LA->SetNumberField(TEXT("location_z"), P.Location.Z);
				TSharedPtr<FJsonObject> LR;
				if (CallTool(TEXT("create_level_instance"), LA, LR, Err))
				{
					Spawned = FindActorByLabel(World, Label);
					if (!Spawned) Err = TEXT("create_level_instance succeeded but the actor was not found by label");
				}
			}
			if (!Spawned)
			{
				TSharedRef<FJsonObject> F = MakeShared<FJsonObject>();
				F->SetNumberField(TEXT("index"), i);
				F->SetStringField(TEXT("error"), Err);
				Failed.Add(MakeShared<FJsonValueObject>(F));
				continue;
			}
			Spawned->Modify();
			Spawned->SetActorLabel(Label);
			if (!SpawnClass)
			{
				Spawned->SetActorRotation(Rot, ETeleportType::TeleportPhysics);
				if (!FMath::IsNearlyEqual(S, 1.0)) Spawned->SetActorScale3D(FVector(S));
				Spawned->PostEditMove(true);
			}
			Spawned->MarkPackageDirty();
			Labels.Add(MakeShared<FJsonValueString>(Spawned->GetActorLabel()));
			if (Placed.Num() < 200)
			{
				TSharedRef<FJsonObject> E = MakeShared<FJsonObject>();
				E->SetStringField(TEXT("label"), Spawned->GetActorLabel());
				E->SetObjectField(TEXT("location"), VecJson(P.Location));
				E->SetNumberField(TEXT("yaw"), FMath::RoundToDouble(Rot.Yaw * 10.0) / 10.0);
				if (!FMath::IsNearlyEqual(S, 1.0)) E->SetNumberField(TEXT("scale"), FMath::RoundToDouble(S * 100.0) / 100.0);
				Placed.Add(MakeShared<FJsonValueObject>(E));
			}
		}
		if (GEditor) GEditor->RedrawLevelEditingViewports();

		TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
		Out->SetStringField(TEXT("prefab"), Prefab);
		Out->SetStringField(TEXT("mode"), Mode);
		if (SpawnClass) Out->SetStringField(TEXT("class"), SpawnClass->GetPathName());
		if (!LevelPath.IsEmpty()) Out->SetStringField(TEXT("level_path"), LevelPath);
		Out->SetStringField(TEXT("source"), Source);
		Out->SetStringField(TEXT("rotation_mode"), RotMode);
		Out->SetBoolField(TEXT("snap_to_ground"), bSnap);
		Out->SetNumberField(TEXT("seed"), Seed);
		Out->SetNumberField(TEXT("placed_count"), Labels.Num());
		Out->SetNumberField(TEXT("failed_count"), Failed.Num());
		Out->SetNumberField(TEXT("points_without_ground"), NoGround);
		Out->SetArrayField(TEXT("labels"), Labels);
		Out->SetArrayField(TEXT("placed"), Placed);
		if (Placed.Num() < Labels.Num()) Out->SetBoolField(TEXT("placed_truncated"), true);
		if (Failed.Num() > 0) Out->SetArrayField(TEXT("failed"), Failed);
		Out->SetStringField(TEXT("hint"), TEXT("Refine with world_align_to_surface (align_to_normal for rocks/props), world_snap_to_grid for modular pieces, and world_mass_edit(filter.label_contains=name_prefix) for bulk tweaks."));
		return Ok(Out);
	}

	// ═════════════════════════════════════════════════════════════════════════
	// world_mass_edit
	// ═════════════════════════════════════════════════════════════════════════
	static UClass* ResolveActorClass(const FString& In, FString& OutErr)
	{
		if (In.IsEmpty()) return nullptr;
		UClass* C = nullptr;
		if (In.StartsWith(TEXT("/Script/"))) C = FindObject<UClass>(nullptr, *In);
		else if (In.StartsWith(TEXT("/Game/")) || In.StartsWith(TEXT("/")))
		{
			if (UBlueprint* BP = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(In))) C = BP->GeneratedClass;
			else C = FindObject<UClass>(nullptr, *In);
		}
		else
		{
			C = UClass::TryFindTypeSlow<UClass>(In);
			if (!C && In.StartsWith(TEXT("A"))) C = UClass::TryFindTypeSlow<UClass>(In.Mid(1));
			if (!C) C = UClass::TryFindTypeSlow<UClass>(In + TEXT("_C"));
		}
		if (!C) OutErr = FString::Printf(TEXT("filter.class '%s' could not be resolved (use a short native name like StaticMeshActor, a /Script path or a Blueprint asset path)."), *In);
		return C;
	}

	// JSON value → UE ImportText string: strings as-is, numbers, bools, objects as (K=V,...), arrays as (a,b).
	static FString JsonToImportText(const TSharedPtr<FJsonValue>& V)
	{
		if (!V.IsValid()) return FString();
		switch (V->Type)
		{
		case EJson::String: return V->AsString();
		case EJson::Boolean: return V->AsBool() ? TEXT("True") : TEXT("False");
		case EJson::Number:
		{
			const double N = V->AsNumber();
			if (FMath::IsNearlyEqual(N, FMath::RoundToDouble(N)) && FMath::Abs(N) < 1e15) return FString::Printf(TEXT("%lld"), (long long)FMath::RoundToDouble(N));
			return FString::SanitizeFloat(N);
		}
		case EJson::Object:
		{
			FString S = TEXT("(");
			bool bFirst = true;
			for (const TPair<FString, TSharedPtr<FJsonValue>>& KV : V->AsObject()->Values)
			{
				if (!bFirst) S += TEXT(",");
				bFirst = false;
				S += KV.Key + TEXT("=") + JsonToImportText(KV.Value);
			}
			return S + TEXT(")");
		}
		case EJson::Array:
		{
			FString S = TEXT("(");
			bool bFirst = true;
			for (const TSharedPtr<FJsonValue>& E : V->AsArray())
			{
				if (!bFirst) S += TEXT(",");
				bFirst = false;
				S += JsonToImportText(E);
			}
			return S + TEXT(")");
		}
		default: return FString();
		}
	}

	static FProperty* FindPropertyCI(UClass* Cls, const FString& Name)
	{
		if (!Cls) return nullptr;
		if (FProperty* P = Cls->FindPropertyByName(FName(*Name))) return P;
		for (TFieldIterator<FProperty> It(Cls); It; ++It)
		{
			if (It->GetName().Equals(Name, ESearchCase::IgnoreCase) || It->GetDisplayNameText().ToString().Equals(Name, ESearchCase::IgnoreCase)) return *It;
		}
		return nullptr;
	}

	static UActorComponent* FindComponentBySpec(AActor* Actor, const FString& Spec)
	{
		TArray<UActorComponent*> Comps;
		Actor->GetComponents(Comps);
		UClass* SpecClass = UClass::TryFindTypeSlow<UClass>(Spec);
		if (!SpecClass && Spec.StartsWith(TEXT("U"))) SpecClass = UClass::TryFindTypeSlow<UClass>(Spec.Mid(1));
		for (UActorComponent* C : Comps)
		{
			if (!C) continue;
			if (C->GetName().Equals(Spec, ESearchCase::IgnoreCase)) return C;
		}
		for (UActorComponent* C : Comps)
		{
			if (!C) continue;
			const FString CN = C->GetClass()->GetName();
			if (CN.Equals(Spec, ESearchCase::IgnoreCase) || (TEXT("U") + CN).Equals(Spec, ESearchCase::IgnoreCase)) return C;
			if (SpecClass && C->GetClass()->IsChildOf(SpecClass)) return C;
		}
		return nullptr;
	}

	static bool SetPropertyByText(UObject* Target, const FString& PropName, const FString& Text, bool bDryRun, FString& OutErr)
	{
		FProperty* Prop = FindPropertyCI(Target->GetClass(), PropName);
		if (!Prop) { OutErr = FString::Printf(TEXT("property '%s' not found on %s"), *PropName, *Target->GetClass()->GetName()); return false; }
		if (bDryRun) return true;
		Target->Modify();
		Target->PreEditChange(Prop);
		void* Ptr = Prop->ContainerPtrToValuePtr<void>(Target);
		const TCHAR* Result = Prop->ImportText_Direct(*Text, Ptr, Target, PPF_None);
		if (!Result) { OutErr = FString::Printf(TEXT("ImportText failed for '%s' with value '%s' (type %s)"), *PropName, *Text.Left(120), *Prop->GetCPPType()); return false; }
		FPropertyChangedEvent Ev(Prop);
		Target->PostEditChangeProperty(Ev);
		return true;
	}

	FUECPToolResult HandleMassEdit(const TSharedPtr<FJsonObject>& Args)
	{
		UWorld* World = EditorWorld();
		if (!World) return Fail(TEXT("No editor world available."));
		const TSharedPtr<FJsonObject> Filter = ArgObj(Args, TEXT("filter"));
		if (!Filter.IsValid()) return Fail(TEXT("filter{} is required (class?, label_contains?, label_prefix?, labels[]?, tag?, folder?, in_box{min,max}?, selection?) — at least one criterion."));
		const FString ClassSpec = ArgStr(Filter, TEXT("class"));
		const FString LabelContains = ArgStr(Filter, TEXT("label_contains"));
		const FString LabelPrefix = ArgStr(Filter, TEXT("label_prefix"));
		const TArray<FString> Labels = ArgStrArray(Filter, TEXT("labels"));
		const FString Tag = ArgStr(Filter, TEXT("tag"));
		const FString Folder = ArgStr(Filter, TEXT("folder"));
		const bool bSelection = ArgBool(Filter, TEXT("selection"), false);
		const TSharedPtr<FJsonObject> InBox = ArgObj(Filter, TEXT("in_box"));
		FVector BoxMin = FVector::ZeroVector, BoxMax = FVector::ZeroVector;
		bool bHasBox = false;
		if (InBox.IsValid())
		{
			if (!ArgVector(InBox, TEXT("min"), BoxMin) || !ArgVector(InBox, TEXT("max"), BoxMax)) return Fail(TEXT("filter.in_box needs min and max ([x,y,z])."));
			const FVector Lo(FMath::Min(BoxMin.X, BoxMax.X), FMath::Min(BoxMin.Y, BoxMax.Y), FMath::Min(BoxMin.Z, BoxMax.Z));
			const FVector Hi(FMath::Max(BoxMin.X, BoxMax.X), FMath::Max(BoxMin.Y, BoxMax.Y), FMath::Max(BoxMin.Z, BoxMax.Z));
			BoxMin = Lo; BoxMax = Hi; bHasBox = true;
		}
		if (ClassSpec.IsEmpty() && LabelContains.IsEmpty() && LabelPrefix.IsEmpty() && Labels.Num() == 0 && Tag.IsEmpty() && Folder.IsEmpty() && !bSelection && !bHasBox)
			return Fail(TEXT("filter{} has no criteria; refusing to edit every actor in the level. Add class / label_contains / label_prefix / labels / tag / folder / in_box / selection."));

		FString ClassErr;
		UClass* FilterClass = ResolveActorClass(ClassSpec, ClassErr);
		if (!ClassSpec.IsEmpty() && !FilterClass) return Fail(ClassErr);

		TSet<AActor*> Selected;
		if (bSelection && GEditor)
		{
			if (USelection* Sel = GEditor->GetSelectedActors())
			{
				for (FSelectionIterator It(*Sel); It; ++It) { if (AActor* A = Cast<AActor>(*It)) Selected.Add(A); }
			}
		}

		const bool bDryRun = ArgBool(Args, TEXT("dry_run"), false);
		const int32 MaxActors = FMath::Max(1, (int32)ArgNum(Args, TEXT("max_actors"), 2000.0));
		const TSharedPtr<FJsonObject> SetObj = ArgObj(Args, TEXT("set"));
		const TSharedPtr<FJsonObject> Xf = ArgObj(Args, TEXT("transform"));
		const FString MaterialPath = ArgStr(Args, TEXT("material_path"));
		const FString NewFolder = ArgStr(Args, TEXT("folder"));
		const TArray<FString> TagsAdd = ArgStrArray(Args, TEXT("tags_add"));
		const TArray<FString> TagsRemove = ArgStrArray(Args, TEXT("tags_remove"));
		FVector OffLoc = FVector::ZeroVector; FRotator OffRot = FRotator::ZeroRotator; FVector ScaleMul = FVector::OneVector;
		bool bHasXf = false;
		if (Xf.IsValid())
		{
			if (ArgVector(Xf, TEXT("offset_location"), OffLoc)) bHasXf = true;
			if (ArgRotator(Xf, TEXT("offset_rotation"), OffRot)) bHasXf = true;
			if (Xf->HasField(TEXT("scale_multiply")))
			{
				const TSharedPtr<FJsonValue> SV = Xf->TryGetField(TEXT("scale_multiply"));
				double Uniform = 1.0;
				if (SV.IsValid() && SV->TryGetNumber(Uniform)) { ScaleMul = FVector(Uniform); bHasXf = true; }
				else if (ParseVector(SV, ScaleMul)) bHasXf = true;
			}
		}
		UMaterialInterface* Material = nullptr;
		if (!MaterialPath.IsEmpty())
		{
			Material = Cast<UMaterialInterface>(UEditorAssetLibrary::LoadAsset(MaterialPath));
			if (!Material) return Fail(FString::Printf(TEXT("Could not load material '%s'."), *MaterialPath));
		}
		const bool bHasSet = SetObj.IsValid() && SetObj->Values.Num() > 0;
		if (!bHasSet && !bHasXf && !Material && NewFolder.IsEmpty() && TagsAdd.Num() == 0 && TagsRemove.Num() == 0)
			return Fail(TEXT("Nothing to apply: pass set{}, transform{}, material_path, folder, tags_add[] or tags_remove[]. Use dry_run=true to only list the matching actors."));

		// ── Resolve actors ──
		TArray<AActor*> Matched;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* A = *It;
			if (!A || A->IsTemplate() || A->HasAnyFlags(RF_Transient) || A->IsA<AWorldSettings>()) continue;
			if (A->IsA<ABrush>() && !FilterClass) continue;
			if (FilterClass && !A->GetClass()->IsChildOf(FilterClass)) continue;
			const FString L = A->GetActorLabel();
			if (!LabelContains.IsEmpty() && !L.Contains(LabelContains, ESearchCase::IgnoreCase)) continue;
			if (!LabelPrefix.IsEmpty() && !L.StartsWith(LabelPrefix, ESearchCase::IgnoreCase)) continue;
			if (Labels.Num() > 0)
			{
				bool bIn = false;
				for (const FString& X : Labels) { if (L.Equals(X, ESearchCase::IgnoreCase)) { bIn = true; break; } }
				if (!bIn) continue;
			}
			if (!Tag.IsEmpty() && !A->Tags.Contains(FName(*Tag))) continue;
			if (!Folder.IsEmpty())
			{
				const FString FP = A->GetFolderPath().ToString();
				if (!FP.Equals(Folder, ESearchCase::IgnoreCase) && !FP.StartsWith(Folder + TEXT("/"), ESearchCase::IgnoreCase)) continue;
			}
			if (bHasBox)
			{
				const FVector P = A->GetActorLocation();
				if (P.X < BoxMin.X || P.Y < BoxMin.Y || P.Z < BoxMin.Z || P.X > BoxMax.X || P.Y > BoxMax.Y || P.Z > BoxMax.Z) continue;
			}
			if (bSelection && !Selected.Contains(A)) continue;
			Matched.Add(A);
			if (Matched.Num() >= MaxActors) break;
		}
		if (Matched.Num() == 0) return Fail(TEXT("No actors matched the filter."));

		// ── Apply ──
		const FScopedTransaction Transaction(NSLOCTEXT("AxivorAI", "WorldMassEdit", "World: Mass Edit"));
		TArray<TSharedPtr<FJsonValue>> Results;
		int32 Edited = 0, ErrorCount = 0;
		for (AActor* Actor : Matched)
		{
			TSharedRef<FJsonObject> R = MakeShared<FJsonObject>();
			R->SetStringField(TEXT("label"), Actor->GetActorLabel());
			R->SetStringField(TEXT("class"), Actor->GetClass()->GetName());
			TArray<TSharedPtr<FJsonValue>> Applied;
			TArray<TSharedPtr<FJsonValue>> Errors;
			if (!bDryRun) Actor->Modify();

			if (bHasSet)
			{
				for (const TPair<FString, TSharedPtr<FJsonValue>>& KV : SetObj->Values)
				{
					FString CompSpec, PropName = KV.Key;
					UObject* Target = Actor;
					if (KV.Key.Contains(TEXT(".")))
					{
						KV.Key.Split(TEXT("."), &CompSpec, &PropName, ESearchCase::IgnoreCase, ESearchDir::FromEnd);
						UActorComponent* Comp = FindComponentBySpec(Actor, CompSpec);
						if (!Comp) { Errors.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("component '%s' not found"), *CompSpec))); continue; }
						Target = Comp;
					}
					const FString Text = JsonToImportText(KV.Value);
					FString Err;
					if (SetPropertyByText(Target, PropName, Text, bDryRun, Err))
						Applied.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("%s%s = %s"), bDryRun ? TEXT("(dry) ") : TEXT(""), *KV.Key, *Text.Left(80))));
					else Errors.Add(MakeShared<FJsonValueString>(Err));
				}
			}
			if (bHasXf)
			{
				if (!bDryRun)
				{
					const FVector NewLoc = Actor->GetActorLocation() + OffLoc;
					const FRotator NewRot = (Actor->GetActorRotation() + OffRot).GetNormalized();
					Actor->SetActorLocationAndRotation(NewLoc, NewRot, false, nullptr, ETeleportType::TeleportPhysics);
					if (!ScaleMul.Equals(FVector::OneVector)) Actor->SetActorScale3D(Actor->GetActorScale3D() * ScaleMul);
					Actor->PostEditMove(true);
				}
				Applied.Add(MakeShared<FJsonValueString>(TEXT("transform")));
			}
			if (Material)
			{
				int32 Slots = 0;
				TArray<UMeshComponent*> Meshes;
				Actor->GetComponents<UMeshComponent>(Meshes);
				for (UMeshComponent* MC : Meshes)
				{
					if (!MC) continue;
					const int32 N = MC->GetNumMaterials();
					if (!bDryRun) MC->Modify();
					for (int32 s = 0; s < N; ++s) { if (!bDryRun) MC->SetMaterial(s, Material); ++Slots; }
				}
				if (Slots > 0) Applied.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("material x%d slots"), Slots)));
				else Errors.Add(MakeShared<FJsonValueString>(TEXT("no mesh component / material slots")));
			}
			if (!NewFolder.IsEmpty())
			{
				if (!bDryRun) Actor->SetFolderPath(FName(*NewFolder));
				Applied.Add(MakeShared<FJsonValueString>(TEXT("folder ") + NewFolder));
			}
			if (TagsAdd.Num() > 0 || TagsRemove.Num() > 0)
			{
				if (!bDryRun)
				{
					for (const FString& T : TagsAdd) Actor->Tags.AddUnique(FName(*T));
					for (const FString& T : TagsRemove) Actor->Tags.Remove(FName(*T));
				}
				Applied.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("tags +%d -%d"), TagsAdd.Num(), TagsRemove.Num())));
			}
			if (!bDryRun && Applied.Num() > 0) { Actor->MarkPackageDirty(); ++Edited; }
			if (Errors.Num() > 0) ++ErrorCount;
			R->SetArrayField(TEXT("applied"), Applied);
			if (Errors.Num() > 0) R->SetArrayField(TEXT("errors"), Errors);
			if (Results.Num() < 300) Results.Add(MakeShared<FJsonValueObject>(R));
		}
		if (!bDryRun && GEditor) GEditor->RedrawLevelEditingViewports();

		TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
		Out->SetBoolField(TEXT("dry_run"), bDryRun);
		Out->SetNumberField(TEXT("matched"), Matched.Num());
		Out->SetNumberField(TEXT("edited"), Edited);
		Out->SetNumberField(TEXT("actors_with_errors"), ErrorCount);
		Out->SetBoolField(TEXT("truncated_by_max_actors"), Matched.Num() >= MaxActors);
		Out->SetArrayField(TEXT("results"), Results);
		if (Results.Num() < Matched.Num()) Out->SetBoolField(TEXT("results_truncated"), true);
		return Ok(Out);
	}
}
