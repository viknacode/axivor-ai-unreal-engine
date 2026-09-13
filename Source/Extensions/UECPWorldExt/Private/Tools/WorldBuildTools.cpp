// Axivor AI — World Builder: biome layers (populate_landscape composition), spline roads and rivers.

#include "WorldExtCommon.h"
#include "UECPWorldExtModule.h"

#include "ScopedTransaction.h"
#include "EditorAssetLibrary.h"
#include "Engine/StaticMesh.h"
#include "Components/SplineComponent.h"
#include "Components/SplineMeshComponent.h"
#include "Components/BoxComponent.h"
#include "Components/PrimitiveComponent.h"
#include "PCGComponent.h"
#include "PCGGraph.h"
#include "LandscapeProxy.h"
#include "Interfaces/IPluginManager.h"
#include "Math/InterpCurve.h"
#include "UObject/UnrealType.h"

namespace WorldExt
{
	// ═════════════════════════════════════════════════════════════════════════
	// Biome presets
	// ═════════════════════════════════════════════════════════════════════════
	struct FBiomeLayerParams
	{
		double Density;        // points per square meter (populate_landscape `density`)
		double MinSpacing;     // cm, 0 = off
		double MinScale;
		double MaxScale;
		double MaxSlopeDeg;
		double CullDistance;   // cm
	};
	struct FBiomePreset
	{
		const TCHAR* Id;
		const TCHAR* Prefix;
		FBiomeLayerParams Trees;
		FBiomeLayerParams Bushes;
		FBiomeLayerParams Grass;
		FBiomeLayerParams Rocks;
		const TCHAR* Notes;
	};
	static const FBiomePreset GBiomePresets[] =
	{
		//  id                  prefix               trees                                   bushes                                  grass                                 rocks
		{ TEXT("temperate_forest"), TEXT("TemperateForest"), { 0.020, 600.0, 0.80, 1.30, 35.0, 25000.0 }, { 0.050, 250.0, 0.70, 1.20, 40.0, 8000.0 }, { 1.50, 0.0, 0.80, 1.20, 45.0, 4000.0 }, { 0.004, 800.0, 0.60, 1.50, 60.0, 15000.0 },
		  TEXT("Mixed broadleaf canopy, dense understory, lush grass, occasional boulders.") },
		{ TEXT("pine_forest"),      TEXT("PineForest"),      { 0.030, 450.0, 0.90, 1.40, 40.0, 25000.0 }, { 0.020, 300.0, 0.60, 1.00, 40.0, 8000.0 }, { 0.80, 0.0, 0.70, 1.10, 45.0, 3500.0 }, { 0.006, 700.0, 0.70, 1.60, 65.0, 15000.0 },
		  TEXT("Tighter conifer spacing, sparse understory, thin grass, more exposed rock on slopes.") },
		{ TEXT("jungle"),           TEXT("Jungle"),          { 0.050, 300.0, 0.80, 1.50, 45.0, 20000.0 }, { 0.150, 150.0, 0.80, 1.40, 50.0, 7000.0 }, { 2.50, 0.0, 0.90, 1.40, 50.0, 4000.0 }, { 0.003, 900.0, 0.50, 1.20, 60.0, 12000.0 },
		  TEXT("Very dense multi-layer vegetation; expect heavy instance counts — lower density_scale on large landscapes.") },
		{ TEXT("meadow"),           TEXT("Meadow"),          { 0.002, 1500.0, 0.90, 1.30, 25.0, 30000.0 }, { 0.020, 400.0, 0.70, 1.10, 35.0, 8000.0 }, { 3.00, 0.0, 0.80, 1.30, 40.0, 5000.0 }, { 0.002, 1200.0, 0.40, 1.00, 50.0, 12000.0 },
		  TEXT("Open grassland with isolated trees and shrubs.") },
		{ TEXT("desert"),           TEXT("Desert"),          { 0.001, 2500.0, 0.70, 1.20, 20.0, 30000.0 }, { 0.010, 500.0, 0.60, 1.10, 30.0, 8000.0 }, { 0.20, 0.0, 0.60, 1.00, 35.0, 3000.0 }, { 0.010, 500.0, 0.50, 2.00, 70.0, 20000.0 },
		  TEXT("Sparse vegetation, scattered rocks with large scale variation.") },
		{ TEXT("rocky"),            TEXT("Rocky"),           { 0.003, 1200.0, 0.70, 1.10, 30.0, 25000.0 }, { 0.010, 400.0, 0.50, 0.90, 45.0, 7000.0 }, { 0.40, 0.0, 0.60, 1.00, 50.0, 3000.0 }, { 0.030, 300.0, 0.60, 2.50, 80.0, 25000.0 },
		  TEXT("Alpine / badlands: rocks dominate and are allowed on steep slopes; vegetation is sparse.") },
		{ TEXT("wetland"),          TEXT("Wetland"),         { 0.008, 900.0, 0.80, 1.30, 20.0, 25000.0 }, { 0.060, 200.0, 0.70, 1.30, 25.0, 8000.0 }, { 2.00, 0.0, 0.90, 1.50, 30.0, 4500.0 }, { 0.002, 1000.0, 0.40, 0.90, 40.0, 10000.0 },
		  TEXT("Flat marsh: reeds/grass everywhere, low slope limits keep vegetation off the banks.") },
	};

	static const FBiomePreset* FindPreset(const FString& Id)
	{
		const FString Norm = Id.ToLower().Replace(TEXT(" "), TEXT("_")).Replace(TEXT("-"), TEXT("_"));
		for (const FBiomePreset& P : GBiomePresets)
		{
			if (Norm == P.Id) return &P;
		}
		return nullptr;
	}
	static FString PresetList()
	{
		FString S;
		for (const FBiomePreset& P : GBiomePresets) { if (!S.IsEmpty()) S += TEXT(", "); S += P.Id; }
		return S;
	}
	static TArray<FString> LayerMeshes(const TSharedPtr<FJsonObject>& Args, const TSharedPtr<FJsonObject>& Meshes, const TCHAR* Layer)
	{
		TArray<FString> Out = ArgStrArray(Meshes, Layer);
		if (Out.Num() == 0) Out = ArgStrArray(Args, Layer); // also accept top-level trees[] / bushes[] / grass[] / rocks[]
		return Out;
	}

	// ═════════════════════════════════════════════════════════════════════════
	// world_build_biome
	// ═════════════════════════════════════════════════════════════════════════
	// Exclusion params passed through to the pcg tool `set_pcg_exclusion` for every layer graph.
	struct FBiomeExclusionParams
	{
		bool bWorldCollision = true;
		TArray<FString> ActorTags;
		TArray<FString> ActorClasses;
		TArray<FString> SplineTags = { TEXT("AxivorRoad") };
		double SplineWidth = 600.0;
		double Margin = 0.15;
	};
	static FBiomeExclusionParams ParseBiomeExclusion(const TSharedPtr<FJsonObject>& ExcludeObj)
	{
		FBiomeExclusionParams P;
		if (!ExcludeObj.IsValid()) return P;
		if (ExcludeObj->HasTypedField<EJson::Boolean>(TEXT("world_collision"))) P.bWorldCollision = ExcludeObj->GetBoolField(TEXT("world_collision"));
		const TArray<FString> Tags = ArgStrArray(ExcludeObj, TEXT("actor_tags"));
		if (Tags.Num() > 0) P.ActorTags = Tags;
		const TArray<FString> Classes = ArgStrArray(ExcludeObj, TEXT("actor_classes"));
		if (Classes.Num() > 0) P.ActorClasses = Classes;
		if (ExcludeObj->HasField(TEXT("spline_tags")))
		{
			// Explicit spline_tags (even []) overrides the AxivorRoad default.
			P.SplineTags = ArgStrArray(ExcludeObj, TEXT("spline_tags"));
		}
		P.SplineWidth = ArgNum(ExcludeObj, TEXT("spline_width"), P.SplineWidth);
		P.Margin = ArgNum(ExcludeObj, TEXT("margin"), P.Margin);
		return P;
	}
	static double GetLandscapeZExtent(UWorld* World, const FString& LandscapeLabel)
	{
		for (TActorIterator<ALandscapeProxy> It(World); It; ++It)
		{
			if (LandscapeLabel.IsEmpty() || It->GetActorLabel().Equals(LandscapeLabel, ESearchCase::IgnoreCase))
			{
				FVector Origin(ForceInit), Extent(ForceInit);
				It->GetActorBounds(false, Origin, Extent);
				if (Extent.Z > 0.0) return Extent.Z;
			}
		}
		return 5000.0;
	}

	FUECPToolResult HandleBuildBiome(const TSharedPtr<FJsonObject>& Args)
	{
		UWorld* World = EditorWorld();
		if (!World) return Fail(TEXT("No editor world available."));
		const FString PresetId = ArgStr(Args, TEXT("preset"));
		const FBiomePreset* Preset = FindPreset(PresetId);
		if (!Preset) return Fail(FString::Printf(TEXT("Unknown preset '%s'. Use one of: %s."), *PresetId, *PresetList()));

		const FString LandscapeLabel = ArgStr(Args, TEXT("landscape_label"));
		const FString BoundsActorLabel = ArgStr(Args, TEXT("bounds_actor"));
		const double DensityScale = FMath::Max(0.0, ArgNum(Args, TEXT("density_scale"), 1.0));
		const bool bHasSeed = Args.IsValid() && Args->HasField(TEXT("seed"));
		const int32 Seed = (int32)ArgNum(Args, TEXT("seed"), 0.0);
		const FString Prefix = SanitizeLabel(ArgStr(Args, TEXT("name_prefix"), Preset->Prefix));
		const TSharedPtr<FJsonObject> Meshes = ArgObj(Args, TEXT("meshes"));
		const bool bReplace = ArgBool(Args, TEXT("replace"), false);
		const FBiomeExclusionParams Excl = ParseBiomeExclusion(ArgObj(Args, TEXT("exclude")));

		AActor* BoundsActor = nullptr;
		if (!BoundsActorLabel.IsEmpty())
		{
			BoundsActor = FindActorByLabel(World, BoundsActorLabel);
			if (!BoundsActor) return Fail(FString::Printf(TEXT("bounds_actor '%s' not found in the level."), *BoundsActorLabel));
		}

		// zone{center:{x,y}, extent:{x,y}} — world cm half-extents, alternative to bounds_actor.
		bool bHasZone = false;
		FVector2D ZoneCenter(ForceInit), ZoneExtent(ForceInit);
		if (const TSharedPtr<FJsonObject> ZoneObj = ArgObj(Args, TEXT("zone")))
		{
			const TSharedPtr<FJsonObject> C = ArgObj(ZoneObj, TEXT("center"));
			const TSharedPtr<FJsonObject> E = ArgObj(ZoneObj, TEXT("extent"));
			if (!C.IsValid() || !E.IsValid()) return Fail(TEXT("zone requires both center{x,y} and extent{x,y} (world cm half-extents)."));
			double CX = 0, CY = 0, EX = 0, EY = 0;
			C->TryGetNumberField(TEXT("x"), CX); C->TryGetNumberField(TEXT("y"), CY);
			E->TryGetNumberField(TEXT("x"), EX); E->TryGetNumberField(TEXT("y"), EY);
			if (EX <= 0.0 || EY <= 0.0) return Fail(TEXT("zone.extent.x and zone.extent.y must be > 0."));
			ZoneCenter = FVector2D(CX, CY); ZoneExtent = FVector2D(EX, EY);
			bHasZone = true;
		}
		if (BoundsActor && bHasZone) return Fail(TEXT("Pass either bounds_actor or zone, not both."));

		// Refuse to stack a new whole-landscape (or overlapping) layer set on top of an existing one
		// unless the caller explicitly asks to rebuild — this is what caused unbounded layer stacking.
		const FString LabelPrefix = FString::Printf(TEXT("PCG_%s_"), *Prefix);
		TArray<AActor*> ExistingLayerActors;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (It->GetActorLabel().StartsWith(LabelPrefix, ESearchCase::IgnoreCase)) ExistingLayerActors.Add(*It);
		}
		if (ExistingLayerActors.Num() > 0 && !bReplace)
		{
			TArray<FString> Labels;
			for (AActor* A : ExistingLayerActors) Labels.Add(A->GetActorLabel());
			return Fail(FString::Printf(TEXT("Biome layers already exist for prefix '%s': %s. Pass replace:true to delete and rebuild them, or use a different name_prefix / zone."), *Prefix, *FString::Join(Labels, TEXT(", "))));
		}

		struct FLayerDef { const TCHAR* Name; const FBiomeLayerParams* Params; };
		const FLayerDef Layers[] = {
			{ TEXT("trees"),  &Preset->Trees },
			{ TEXT("bushes"), &Preset->Bushes },
			{ TEXT("grass"),  &Preset->Grass },
			{ TEXT("rocks"),  &Preset->Rocks },
		};

		const FScopedTransaction Transaction(NSLOCTEXT("AxivorAI", "WorldBuildBiome", "World: Build Biome"));

		int32 Replaced = 0;
		if (bReplace && ExistingLayerActors.Num() > 0)
		{
			for (AActor* A : ExistingLayerActors)
			{
				if (!A) continue;
				A->Modify();
				World->DestroyActor(A);
				++Replaced;
			}
		}

		TArray<TSharedPtr<FJsonValue>> LayerResults;
		TArray<TSharedPtr<FJsonValue>> Skipped;
		int32 Created = 0;
		int32 LayerIndex = 0;
		for (const FLayerDef& L : Layers)
		{
			const int32 ThisLayerIndex = LayerIndex++;
			const TArray<FString> MeshPaths = LayerMeshes(Args, Meshes, L.Name);
			TSharedRef<FJsonObject> R = MakeShared<FJsonObject>();
			R->SetStringField(TEXT("layer"), L.Name);
			if (MeshPaths.Num() == 0)
			{
				R->SetStringField(TEXT("status"), TEXT("skipped"));
				R->SetStringField(TEXT("reason"), FString::Printf(TEXT("no meshes provided for '%s' (pass meshes.%s[] to build this layer)"), L.Name, L.Name));
				Skipped.Add(MakeShared<FJsonValueObject>(R));
				continue;
			}
			const FBiomeLayerParams& P = *L.Params;
			const double Density = P.Density * DensityScale;
			const FString LayerName = FString::Printf(TEXT("%s_%s"), *Prefix, *SanitizeLabel(FString(L.Name)));

			TSharedRef<FJsonObject> A = MakeShared<FJsonObject>();
			{
				TArray<TSharedPtr<FJsonValue>> MeshArr;
				for (const FString& M : MeshPaths) MeshArr.Add(MakeShared<FJsonValueString>(M));
				A->SetArrayField(TEXT("mesh_paths"), MeshArr);
			}
			if (!LandscapeLabel.IsEmpty()) A->SetStringField(TEXT("landscape_label"), LandscapeLabel);
			A->SetStringField(TEXT("layer_name"), LayerName);
			A->SetNumberField(TEXT("density"), Density);
			A->SetNumberField(TEXT("min_scale"), P.MinScale);
			A->SetNumberField(TEXT("max_scale"), P.MaxScale);
			A->SetNumberField(TEXT("cull_distance"), P.CullDistance);
			A->SetNumberField(TEXT("max_slope_degrees"), P.MaxSlopeDeg);
			A->SetNumberField(TEXT("min_spacing"), P.MinSpacing);

			TSharedRef<FJsonObject> Used = MakeShared<FJsonObject>();
			Used->SetNumberField(TEXT("density_points_per_m2"), Density);
			Used->SetNumberField(TEXT("min_spacing_cm"), P.MinSpacing);
			Used->SetNumberField(TEXT("min_scale"), P.MinScale);
			Used->SetNumberField(TEXT("max_scale"), P.MaxScale);
			Used->SetNumberField(TEXT("max_slope_degrees"), P.MaxSlopeDeg);
			Used->SetNumberField(TEXT("cull_distance_cm"), P.CullDistance);
			R->SetObjectField(TEXT("params"), Used);
			R->SetNumberField(TEXT("mesh_count"), MeshPaths.Num());

			TSharedPtr<FJsonObject> PR; FString Err;
			if (!CallTool(TEXT("populate_landscape"), A, PR, Err))
			{
				R->SetStringField(TEXT("status"), TEXT("failed"));
				R->SetStringField(TEXT("error"), Err);
				LayerResults.Add(MakeShared<FJsonValueObject>(R));
				continue;
			}
			const FString ActorLabel = ArgStr(PR, TEXT("actor_label"));
			const FString GraphPath = ArgStr(PR, TEXT("graph_path"));
			R->SetStringField(TEXT("status"), TEXT("created"));
			R->SetStringField(TEXT("actor_label"), ActorLabel);
			R->SetStringField(TEXT("graph_path"), GraphPath);
			++Created;

			AActor* PCGActor = FindActorByLabel(World, ActorLabel);
			if (BoundsActor && PCGActor)
			{
				FVector Origin(ForceInit), Extent(ForceInit);
				BoundsActor->GetActorBounds(false, Origin, Extent);
				PCGActor->Modify();
				PCGActor->SetActorLocation(Origin);
				if (UBoxComponent* Box = PCGActor->FindComponentByClass<UBoxComponent>())
				{
					Box->Modify();
					Box->SetBoxExtent(FVector(Extent.X, Extent.Y, FMath::Max(Extent.Z, 1000.0) * 2.0));
				}
				R->SetObjectField(TEXT("bounds_center"), VecJson(Origin));
				R->SetObjectField(TEXT("bounds_extent"), VecJson(Extent));
			}
			else if (bHasZone && PCGActor)
			{
				const double ZExtent = GetLandscapeZExtent(World, LandscapeLabel);
				const FVector CurrentLoc = PCGActor->GetActorLocation();
				PCGActor->Modify();
				PCGActor->SetActorLocation(FVector(ZoneCenter.X, ZoneCenter.Y, CurrentLoc.Z));
				if (UBoxComponent* Box = PCGActor->FindComponentByClass<UBoxComponent>())
				{
					Box->Modify();
					Box->SetBoxExtent(FVector(ZoneExtent.X, ZoneExtent.Y, ZExtent));
				}
				TSharedRef<FJsonObject> ZoneOut = MakeShared<FJsonObject>();
				ZoneOut->SetObjectField(TEXT("center"), VecJson(FVector(ZoneCenter.X, ZoneCenter.Y, 0.0)));
				ZoneOut->SetObjectField(TEXT("extent"), VecJson(FVector(ZoneExtent.X, ZoneExtent.Y, ZExtent)));
				R->SetObjectField(TEXT("zone"), ZoneOut);
			}

			// Seed + generation trigger (always GenerateOnDemand so vegetation isn't rebuilt on every load/PIE).
			{
				TSharedRef<FJsonObject> SA = MakeShared<FJsonObject>();
				SA->SetStringField(TEXT("actor_label"), ActorLabel);
				SA->SetStringField(TEXT("generation_trigger"), TEXT("GenerateOnDemand"));
				if (bHasSeed) SA->SetNumberField(TEXT("seed"), Seed + ThisLayerIndex * 7919);
				TSharedPtr<FJsonObject> SR; FString SErr;
				if (CallTool(TEXT("set_pcg_component_properties"), SA, SR, SErr))
				{
					R->SetStringField(TEXT("generation_trigger"), TEXT("GenerateOnDemand"));
					if (bHasSeed) R->SetNumberField(TEXT("seed"), Seed + ThisLayerIndex * 7919);
				}
				else R->SetStringField(TEXT("component_properties_warning"), SErr);
			}

			// Exclusion: keep this layer out of buildings/roads. Never fails the biome — just recorded.
			if (!GraphPath.IsEmpty())
			{
				TSharedRef<FJsonObject> EA = MakeShared<FJsonObject>();
				EA->SetStringField(TEXT("graph_path"), GraphPath);
				EA->SetBoolField(TEXT("world_collision"), Excl.bWorldCollision);
				if (Excl.ActorTags.Num() > 0)
				{
					TArray<TSharedPtr<FJsonValue>> Arr; for (const FString& T : Excl.ActorTags) Arr.Add(MakeShared<FJsonValueString>(T));
					EA->SetArrayField(TEXT("actor_tags"), Arr);
				}
				if (Excl.ActorClasses.Num() > 0)
				{
					TArray<TSharedPtr<FJsonValue>> Arr; for (const FString& C : Excl.ActorClasses) Arr.Add(MakeShared<FJsonValueString>(C));
					EA->SetArrayField(TEXT("actor_classes"), Arr);
				}
				{
					TArray<TSharedPtr<FJsonValue>> Arr; for (const FString& T : Excl.SplineTags) Arr.Add(MakeShared<FJsonValueString>(T));
					EA->SetArrayField(TEXT("spline_tags"), Arr);
				}
				EA->SetNumberField(TEXT("spline_width"), Excl.SplineWidth);
				EA->SetNumberField(TEXT("margin"), Excl.Margin);
				TSharedPtr<FJsonObject> ER; FString EErr;
				if (CallTool(TEXT("set_pcg_exclusion"), EA, ER, EErr))
				{
					R->SetObjectField(TEXT("exclusion"), ER.IsValid() ? ER.ToSharedRef() : MakeShared<FJsonObject>());
				}
				else
				{
					TSharedRef<FJsonObject> ExErr = MakeShared<FJsonObject>();
					ExErr->SetStringField(TEXT("error"), EErr);
					R->SetObjectField(TEXT("exclusion"), ExErr);
				}
			}

			// Always regenerate after wiring bounds/zone/trigger/exclusion.
			if (!ActorLabel.IsEmpty())
			{
				TSharedRef<FJsonObject> GA = MakeShared<FJsonObject>();
				GA->SetStringField(TEXT("actor_label"), ActorLabel);
				GA->SetBoolField(TEXT("force"), true);
				TSharedPtr<FJsonObject> GR; FString GErr;
				R->SetBoolField(TEXT("regenerated"), CallTool(TEXT("generate_pcg"), GA, GR, GErr));
				if (!GErr.IsEmpty()) R->SetStringField(TEXT("regenerate_warning"), GErr);
			}
			LayerResults.Add(MakeShared<FJsonValueObject>(R));
		}

		TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
		Out->SetStringField(TEXT("preset"), Preset->Id);
		Out->SetStringField(TEXT("preset_notes"), Preset->Notes);
		if (!LandscapeLabel.IsEmpty()) Out->SetStringField(TEXT("landscape_label"), LandscapeLabel);
		if (BoundsActor) Out->SetStringField(TEXT("bounds_actor"), BoundsActor->GetActorLabel());
		Out->SetBoolField(TEXT("replace"), bReplace);
		if (Replaced > 0) Out->SetNumberField(TEXT("layers_replaced"), Replaced);
		Out->SetNumberField(TEXT("density_scale"), DensityScale);
		if (bHasSeed) Out->SetNumberField(TEXT("seed"), Seed);
		Out->SetNumberField(TEXT("layers_created"), Created);
		Out->SetArrayField(TEXT("layers"), LayerResults);
		Out->SetArrayField(TEXT("skipped"), Skipped);
		if (Created == 0 && Skipped.Num() == 4)
			return Fail(TEXT("No layer was built: pass meshes{trees[],bushes[],grass[],rocks[]} with at least one StaticMesh path."));
		FString Hint = TEXT("Each layer is a PCG actor 'PCG_<Prefix>_<layer>_Actor' with graph /Game/GeneratedPCG/PCG_<Prefix>_<layer>, generation_trigger=GenerateOnDemand. Tune with set_pcg_component_properties / set_pcg_node_property + generate_pcg, or rerun with a different density_scale. Call world_build_road / world_build_river next, then world_place_prefab.");
		if (!BoundsActor && !bHasZone) Hint += TEXT(" This call covered the whole landscape (no bounds_actor/zone) — when building multiple biomes, give each one a disjoint zone{center,extent} or bounds_actor instead of stacking whole-map layers.");
		Out->SetStringField(TEXT("hint"), Hint);
		return Ok(Out);
	}

	// ═════════════════════════════════════════════════════════════════════════
	// Spline helpers shared by roads and rivers
	// ═════════════════════════════════════════════════════════════════════════
	static TSharedRef<FJsonObject> PointsToJsonArgs(const TArray<FVector>& Points, TSharedRef<FJsonObject> Args)
	{
		TArray<TSharedPtr<FJsonValue>> Arr;
		for (const FVector& P : Points)
		{
			TArray<TSharedPtr<FJsonValue>> C;
			C.Add(MakeShared<FJsonValueNumber>(P.X)); C.Add(MakeShared<FJsonValueNumber>(P.Y)); C.Add(MakeShared<FJsonValueNumber>(P.Z));
			Arr.Add(MakeShared<FJsonValueArray>(C));
		}
		Args->SetArrayField(TEXT("points"), Arr);
		return Args;
	}

	// Existing spline actor by label, or a new one through spawn_spline_actor (+ points).
	static bool ResolveOrSpawnSplineActor(UWorld* World, const FString& ExistingLabel, const TArray<FVector>& Points, const FString& Name, bool bClosed,
		AActor*& OutActor, USplineComponent*& OutSpline, FString& OutBlueprintPath, bool& bOutSpawned, FString& OutErr)
	{
		OutActor = nullptr; OutSpline = nullptr; bOutSpawned = false;
		if (!ExistingLabel.IsEmpty())
		{
			OutActor = FindActorByLabel(World, ExistingLabel);
			if (!OutActor) { OutErr = FString::Printf(TEXT("spline_actor '%s' not found in the level."), *ExistingLabel); return false; }
			OutSpline = OutActor->FindComponentByClass<USplineComponent>();
			if (!OutSpline) { OutErr = FString::Printf(TEXT("Actor '%s' has no SplineComponent."), *ExistingLabel); return false; }
			if (Points.Num() >= 2)
			{
				OutSpline->Modify();
				OutSpline->SetSplinePoints(Points, ESplineCoordinateSpace::World, false);
			}
			return true;
		}
		if (Points.Num() < 2) { OutErr = TEXT("Provide points[] (at least 2, [x,y,z] world cm) or spline_actor (label of a level actor with a SplineComponent)."); return false; }

		TSet<AActor*> Before;
		for (TActorIterator<AActor> It(World); It; ++It) if (It->GetActorLabel().Equals(Name, ESearchCase::IgnoreCase)) Before.Add(*It);

		TSharedRef<FJsonObject> A = PointsToJsonArgs(Points, MakeShared<FJsonObject>());
		A->SetStringField(TEXT("actor_label"), Name);
		A->SetStringField(TEXT("save_path"), TEXT("/Game/World/Splines"));
		A->SetNumberField(TEXT("location_x"), 0.0); A->SetNumberField(TEXT("location_y"), 0.0); A->SetNumberField(TEXT("location_z"), 0.0);
		A->SetBoolField(TEXT("closed_loop"), bClosed);
		TSharedPtr<FJsonObject> R;
		if (!CallTool(TEXT("spawn_spline_actor"), A, R, OutErr)) return false;
		OutBlueprintPath = ArgStr(R, TEXT("blueprint_path"));

		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (It->GetActorLabel().Equals(Name, ESearchCase::IgnoreCase) && !Before.Contains(*It)) OutActor = *It;
		}
		if (!OutActor) OutActor = FindActorByLabel(World, Name);
		if (!OutActor) { OutErr = TEXT("spawn_spline_actor succeeded but the spawned actor could not be found by label."); return false; }
		OutSpline = OutActor->FindComponentByClass<USplineComponent>();
		if (!OutSpline) { OutErr = TEXT("Spawned spline actor has no SplineComponent instance."); return false; }
		bOutSpawned = true;
		// The template already holds the points (actor sits at the origin, so local == world); make sure the instance matches.
		OutSpline->Modify();
		OutSpline->SetSplinePoints(Points, ESplineCoordinateSpace::World, false);
		return true;
	}

	static void SnapSplineToGround(UWorld* World, AActor* Actor, USplineComponent* Spline, double Offset, int32& OutSnapped, int32& OutMissed)
	{
		OutSnapped = 0; OutMissed = 0;
		const int32 N = Spline->GetNumberOfSplinePoints();
		for (int32 i = 0; i < N; ++i)
		{
			const FVector P = Spline->GetLocationAtSplinePoint(i, ESplineCoordinateSpace::World);
			FHitResult Hit;
			if (TraceGround(World, P, 20000.0, 200000.0, Hit, Actor))
			{
				Spline->SetLocationAtSplinePoint(i, FVector(P.X, P.Y, Hit.ImpactPoint.Z + Offset), ESplineCoordinateSpace::World, false);
				++OutSnapped;
			}
			else ++OutMissed;
		}
	}

	static void SmoothSpline(USplineComponent* Spline, bool bClosed)
	{
		const int32 N = Spline->GetNumberOfSplinePoints();
		for (int32 i = 0; i < N; ++i) Spline->SetSplinePointType(i, ESplinePointType::Curve, false);
		Spline->SetClosedLoop(bClosed, false);
		Spline->bSplineHasBeenEdited = true;
		Spline->UpdateSpline();
	}

	// Scales the Axivor-generated spline meshes sideways so the road matches `Width` (forward axis X → width axis Y).
	static int32 ApplySplineMeshWidth(AActor* Actor, const FString& MeshPath, double Width, double& OutNativeWidth)
	{
		OutNativeWidth = 0.0;
		if (Width <= 0.0) return 0;
		UStaticMesh* Mesh = Cast<UStaticMesh>(UEditorAssetLibrary::LoadAsset(MeshPath));
		if (!Mesh) return 0;
		OutNativeWidth = Mesh->GetBounds().BoxExtent.Y * 2.0;
		if (OutNativeWidth <= 1.0) return 0;
		const double Scale = Width / OutNativeWidth;
		static const FName SplineMeshTag(TEXT("AxivorSplineMesh"));
		int32 Count = 0;
		TArray<USplineMeshComponent*> Comps;
		Actor->GetComponents<USplineMeshComponent>(Comps);
		for (USplineMeshComponent* SMC : Comps)
		{
			if (!SMC || !SMC->ComponentHasTag(SplineMeshTag)) continue;
			SMC->Modify();
			SMC->SetStartScale(FVector2D(Scale, 1.0), false);
			SMC->SetEndScale(FVector2D(Scale, 1.0), true);
			++Count;
		}
		return Count;
	}

	// Builds (or rebuilds) a spline + spline-mesh strip. Shared by world_build_road and the river fallback.
	static FUECPToolResult BuildSplineStrip(UWorld* World, const TSharedPtr<FJsonObject>& Args, const FString& DefaultName, double DefaultWidth, double DefaultGroundOffset, const TCHAR* Kind)
	{
		const FString MeshPath = ArgStr(Args, TEXT("mesh_path"));
		if (MeshPath.IsEmpty()) return Fail(FString::Printf(TEXT("mesh_path is required (StaticMesh used as the %s segment, modelled along +X)."), Kind));
		const FString MaterialPath = ArgStr(Args, TEXT("material_path"));
		const FString Name = SanitizeLabel(ArgStr(Args, TEXT("name"), DefaultName));
		const double Width = ArgNum(Args, TEXT("width"), DefaultWidth);
		const bool bClosed = ArgBool(Args, TEXT("closed"), false);
		const bool bAlign = ArgBool(Args, TEXT("align_to_ground"), true);
		const double GroundOffset = ArgNum(Args, TEXT("ground_offset"), DefaultGroundOffset);
		const bool bCollision = ArgBool(Args, TEXT("collision"), true);
		const TArray<FVector> Points = ArgVectorArray(Args, TEXT("points"));
		const FString ExistingLabel = ArgStr(Args, TEXT("spline_actor"));

		const FScopedTransaction Transaction(NSLOCTEXT("AxivorAI", "WorldBuildSplineStrip", "World: Build Spline Strip"));

		AActor* Actor = nullptr; USplineComponent* Spline = nullptr; FString BPPath; bool bSpawned = false; FString Err;
		if (!ResolveOrSpawnSplineActor(World, ExistingLabel, Points, Name, bClosed, Actor, Spline, BPPath, bSpawned, Err)) return Fail(Err);
		Actor->Modify();
		Spline->Modify();

		// Roads are tagged so PCG exclusion (world_build_biome, clear_vegetation) can find them by spline tag.
		const bool bIsRoad = FCString::Stricmp(Kind, TEXT("road")) == 0;
		if (bIsRoad)
		{
			Actor->Tags.AddUnique(FName(TEXT("AxivorRoad")));
			Actor->Tags.AddUnique(FName(TEXT("Road")));
		}

		int32 Snapped = 0, Missed = 0;
		if (bAlign) SnapSplineToGround(World, Actor, Spline, GroundOffset, Snapped, Missed);
		SmoothSpline(Spline, ExistingLabel.IsEmpty() ? bClosed : (bClosed || Spline->IsClosedLoop()));

		TSharedRef<FJsonObject> MA = MakeShared<FJsonObject>();
		MA->SetStringField(TEXT("actor_label"), Actor->GetActorLabel());
		MA->SetStringField(TEXT("mesh_path"), MeshPath);
		MA->SetStringField(TEXT("forward_axis"), TEXT("X"));
		if (!MaterialPath.IsEmpty()) MA->SetStringField(TEXT("material_path"), MaterialPath);
		MA->SetBoolField(TEXT("collision"), bCollision);
		TSharedPtr<FJsonObject> MR; FString MErr;
		if (!CallTool(TEXT("set_spline_mesh"), MA, MR, MErr)) return Fail(MErr);

		double NativeWidth = 0.0;
		const int32 Scaled = ApplySplineMeshWidth(Actor, MeshPath, Width, NativeWidth);
		Actor->PostEditMove(true);
		Actor->MarkPackageDirty();
		if (GEditor) GEditor->RedrawLevelEditingViewports();

		TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
		Out->SetStringField(TEXT("kind"), Kind);
		Out->SetStringField(TEXT("actor_label"), Actor->GetActorLabel());
		Out->SetBoolField(TEXT("spawned_new_actor"), bSpawned);
		if (!BPPath.IsEmpty()) Out->SetStringField(TEXT("blueprint_path"), BPPath);
		Out->SetStringField(TEXT("spline_component"), Spline->GetName());
		Out->SetNumberField(TEXT("points"), Spline->GetNumberOfSplinePoints());
		Out->SetNumberField(TEXT("segments"), ArgNum(MR, TEXT("segments_created"), 0.0));
		Out->SetNumberField(TEXT("spline_length_cm"), FMath::RoundToDouble((double)Spline->GetSplineLength()));
		Out->SetBoolField(TEXT("closed"), Spline->IsClosedLoop());
		Out->SetStringField(TEXT("mesh_path"), MeshPath);
		if (!MaterialPath.IsEmpty()) Out->SetStringField(TEXT("material_path"), MaterialPath);
		Out->SetNumberField(TEXT("width_cm"), Width);
		Out->SetNumberField(TEXT("mesh_native_width_cm"), FMath::RoundToDouble(NativeWidth));
		Out->SetNumberField(TEXT("segments_width_scaled"), Scaled);
		Out->SetBoolField(TEXT("aligned_to_ground"), bAlign);
		Out->SetNumberField(TEXT("points_snapped"), Snapped);
		Out->SetNumberField(TEXT("points_without_ground"), Missed);
		Out->SetStringField(TEXT("tangents"), TEXT("Curve (auto smooth)"));
		Out->SetStringField(TEXT("hint"), TEXT("Adjust the path with add_spline_point / set_spline_point_tangent on the actor's Blueprint or move the actor, then call set_spline_mesh again (it replaces the previous segments). Use world_align_to_surface on props placed along it."));
		return Ok(Out);
	}

	// ═════════════════════════════════════════════════════════════════════════
	// world_build_road
	// ═════════════════════════════════════════════════════════════════════════
	FUECPToolResult HandleBuildRoad(const TSharedPtr<FJsonObject>& Args)
	{
		UWorld* World = EditorWorld();
		if (!World) return Fail(TEXT("No editor world available."));
		const FUECPToolResult Result = BuildSplineStrip(World, Args, TEXT("Road"), 600.0, 5.0, TEXT("road"));
		if (!Result.bSuccess) return Result;
		TSharedPtr<FJsonObject> J = ParseJson(Result.ResultJson);
		if (!J.IsValid()) return Result;

		const FString ActorLabel = ArgStr(J, TEXT("actor_label"));
		AActor* RoadActor = FindActorByLabel(World, ActorLabel);
		const double Width = ArgNum(J, TEXT("width_cm"), 600.0);

		// clear_vegetation: keep freshly-scattered PCG vegetation off the new road by excluding
		// its bounding-box footprint (spline_tags=[AxivorRoad]) from every PCG component whose
		// generated bounds overlap the road.
		const bool bClearVegetation = ArgBool(Args, TEXT("clear_vegetation"), true);
		if (bClearVegetation && RoadActor)
		{
			FVector Origin(ForceInit), Extent(ForceInit);
			RoadActor->GetActorBounds(false, Origin, Extent);
			const FBox RoadBox(Origin - Extent, Origin + Extent);

			TArray<TSharedPtr<FJsonValue>> Cleared;
			TSet<UPCGComponent*> Seen;
			for (TActorIterator<AActor> It(World); It; ++It)
			{
				if (*It == RoadActor) continue;
				UPCGComponent* PCGComp = It->FindComponentByClass<UPCGComponent>();
				if (!PCGComp || Seen.Contains(PCGComp)) continue;
				const FBox CompBounds = PCGComp->GetGridBounds();
				if (!CompBounds.IsValid || !CompBounds.Intersect(RoadBox)) continue;
				Seen.Add(PCGComp);

				UPCGGraph* Graph = PCGComp->GetGraph();
				if (!Graph) continue;
				const FString GraphPath = Graph->GetPathName();
				const FString CompActorLabel = It->GetActorLabel();

				TSharedRef<FJsonObject> ClearEntry = MakeShared<FJsonObject>();
				ClearEntry->SetStringField(TEXT("actor_label"), CompActorLabel);
				ClearEntry->SetStringField(TEXT("graph_path"), GraphPath);

				TSharedRef<FJsonObject> EA = MakeShared<FJsonObject>();
				EA->SetStringField(TEXT("graph_path"), GraphPath);
				EA->SetBoolField(TEXT("world_collision"), false);
				{
					TArray<TSharedPtr<FJsonValue>> Arr;
					Arr.Add(MakeShared<FJsonValueString>(TEXT("AxivorRoad")));
					EA->SetArrayField(TEXT("spline_tags"), Arr);
				}
				EA->SetNumberField(TEXT("spline_width"), Width * 1.3);
				TSharedPtr<FJsonObject> ER; FString EErr;
				if (CallTool(TEXT("set_pcg_exclusion"), EA, ER, EErr))
				{
					ClearEntry->SetObjectField(TEXT("exclusion"), ER.IsValid() ? ER.ToSharedRef() : MakeShared<FJsonObject>());

					TSharedRef<FJsonObject> GA = MakeShared<FJsonObject>();
					GA->SetStringField(TEXT("actor_label"), CompActorLabel);
					GA->SetBoolField(TEXT("force"), true);
					TSharedPtr<FJsonObject> GR; FString GErr;
					ClearEntry->SetBoolField(TEXT("regenerated"), CallTool(TEXT("generate_pcg"), GA, GR, GErr));
					if (!GErr.IsEmpty()) ClearEntry->SetStringField(TEXT("regenerate_warning"), GErr);
				}
				else
				{
					ClearEntry->SetStringField(TEXT("error"), EErr);
				}
				Cleared.Add(MakeShared<FJsonValueObject>(ClearEntry));
			}
			J->SetArrayField(TEXT("vegetation_cleared"), Cleared);
		}

		// flatten_terrain: carve the landscape under the spline so the road sits flush.
		const bool bFlattenTerrain = ArgBool(Args, TEXT("flatten_terrain"), false);
		if (bFlattenTerrain && RoadActor)
		{
			TSharedRef<FJsonObject> FA = MakeShared<FJsonObject>();
			FA->SetStringField(TEXT("spline_actor"), ActorLabel);
			FA->SetNumberField(TEXT("width"), Width);
			TSharedPtr<FJsonObject> FR; FString FErr;
			if (CallTool(TEXT("world_landscape_flatten_spline"), FA, FR, FErr))
			{
				J->SetObjectField(TEXT("flatten_terrain"), FR.IsValid() ? FR.ToSharedRef() : MakeShared<FJsonObject>());
			}
			else
			{
				TSharedRef<FJsonObject> FErrObj = MakeShared<FJsonObject>();
				FErrObj->SetStringField(TEXT("error"), FErr);
				J->SetObjectField(TEXT("flatten_terrain"), FErrObj);
			}
		}

		return Ok(J.ToSharedRef());
	}

	// ═════════════════════════════════════════════════════════════════════════
	// world_build_river
	// ═════════════════════════════════════════════════════════════════════════
	static bool IsPluginEnabled(const TCHAR* Name)
	{
		const TSharedPtr<IPlugin> P = IPluginManager::Get().FindPlugin(Name);
		return P.IsValid() && P->IsEnabled();
	}

	// Writes a constant into an FInterpCurveFloat UPROPERTY (RiverWidth / Depth on UWaterSplineMetadata) by reflection.
	static bool SetMetadataCurve(UObject* Meta, const TCHAR* PropName, int32 NumPoints, float Value, FString& OutInfo)
	{
		if (!Meta) { OutInfo = TEXT("no metadata object"); return false; }
		FStructProperty* SP = CastField<FStructProperty>(Meta->GetClass()->FindPropertyByName(PropName));
		if (!SP || !SP->Struct || SP->Struct->GetFName() != FName(TEXT("InterpCurveFloat")))
		{
			OutInfo = FString::Printf(TEXT("property '%s' not found or not an InterpCurveFloat"), PropName);
			return false;
		}
		FInterpCurveFloat* Curve = SP->ContainerPtrToValuePtr<FInterpCurveFloat>(Meta);
		if (!Curve) { OutInfo = TEXT("null curve"); return false; }
		Meta->Modify();
		if (Curve->Points.Num() != NumPoints)
		{
			Curve->Points.Reset();
			for (int32 i = 0; i < NumPoints; ++i) Curve->Points.Add(FInterpCurvePoint<float>((float)i, Value));
		}
		else
		{
			for (FInterpCurvePoint<float>& Pt : Curve->Points) Pt.OutVal = Value;
		}
		return true;
	}

	FUECPToolResult HandleBuildRiver(const TSharedPtr<FJsonObject>& Args)
	{
		UWorld* World = EditorWorld();
		if (!World) return Fail(TEXT("No editor world available."));
		const TArray<FVector> Points = ArgVectorArray(Args, TEXT("points"));
		const bool bUseWater = ArgBool(Args, TEXT("use_water_plugin"), true);
		const double Width = ArgNum(Args, TEXT("width"), 800.0);
		const double Depth = ArgNum(Args, TEXT("depth"), 100.0);
		const FString Name = SanitizeLabel(ArgStr(Args, TEXT("name"), TEXT("River")));
		const bool bAlign = ArgBool(Args, TEXT("align_to_ground"), true);
		const double GroundOffset = ArgNum(Args, TEXT("ground_offset"), 0.0);

		FString FallbackReason;
		UClass* RiverClass = nullptr;
		if (!bUseWater) FallbackReason = TEXT("use_water_plugin=false");
		else if (!IsPluginEnabled(TEXT("Water"))) FallbackReason = TEXT("Water plugin is not enabled (enable it with ue58_enable_plugins(plugins=['Water']) and restart)");
		else
		{
			RiverClass = FindObject<UClass>(nullptr, TEXT("/Script/Water.WaterBodyRiver"));
			if (!RiverClass) FallbackReason = TEXT("class /Script/Water.WaterBodyRiver is not loaded");
		}

		if (RiverClass)
		{
			if (Points.Num() < 2) return Fail(TEXT("points[] with at least 2 [x,y,z] world positions is required."));
			const FScopedTransaction Transaction(NSLOCTEXT("AxivorAI", "WorldBuildRiver", "World: Build River"));
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			const FTransform SpawnTM(FRotator::ZeroRotator, Points[0]);
			AActor* Actor = World->SpawnActor(RiverClass, &SpawnTM, Params);
			if (!Actor) return Fail(TEXT("SpawnActor(WaterBodyRiver) failed."));
			Actor->SetActorLabel(Name);
			Actor->Modify();

			USplineComponent* Spline = Actor->FindComponentByClass<USplineComponent>();
			if (!Spline)
			{
				World->DestroyActor(Actor);
				return Fail(TEXT("WaterBodyRiver has no SplineComponent (unexpected Water plugin version)."));
			}
			Spline->Modify();
			Spline->SetSplinePoints(Points, ESplineCoordinateSpace::World, false);
			int32 Snapped = 0, Missed = 0;
			if (bAlign) SnapSplineToGround(World, Actor, Spline, GroundOffset, Snapped, Missed);
			SmoothSpline(Spline, false);

			// Width / depth live on the water spline metadata (UWaterSplineMetadata::RiverWidth / Depth curves).
			TArray<FString> Warnings;
			UObject* Meta = nullptr;
			if (FObjectProperty* MP = CastField<FObjectProperty>(Actor->GetClass()->FindPropertyByName(TEXT("WaterSplineMetadata"))))
				Meta = MP->GetObjectPropertyValue_InContainer(Actor);
			FString Info;
			const bool bWidthSet = SetMetadataCurve(Meta, TEXT("RiverWidth"), Spline->GetNumberOfSplinePoints(), (float)Width, Info);
			if (!bWidthSet) Warnings.Add(TEXT("width: ") + Info);
			const bool bDepthSet = SetMetadataCurve(Meta, TEXT("Depth"), Spline->GetNumberOfSplinePoints(), (float)Depth, Info);
			if (!bDepthSet) Warnings.Add(TEXT("depth: ") + Info);
			Spline->UpdateSpline();

			// Let the water body rebuild its mesh / spline meshes as the editor does after a gizmo move.
			Actor->PostEditMove(true);
			Actor->PostEditChange();
			Actor->MarkPackageDirty();
			if (GEditor) GEditor->RedrawLevelEditingViewports();

			TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
			Out->SetStringField(TEXT("path"), TEXT("water_plugin"));
			Out->SetStringField(TEXT("actor_label"), Actor->GetActorLabel());
			Out->SetStringField(TEXT("class"), Actor->GetClass()->GetPathName());
			Out->SetNumberField(TEXT("points"), Spline->GetNumberOfSplinePoints());
			Out->SetNumberField(TEXT("spline_length_cm"), FMath::RoundToDouble((double)Spline->GetSplineLength()));
			Out->SetNumberField(TEXT("width_cm"), Width);
			Out->SetBoolField(TEXT("width_applied"), bWidthSet);
			Out->SetNumberField(TEXT("depth_cm"), Depth);
			Out->SetBoolField(TEXT("depth_applied"), bDepthSet);
			Out->SetBoolField(TEXT("aligned_to_ground"), bAlign);
			Out->SetNumberField(TEXT("points_snapped"), Snapped);
			Out->SetNumberField(TEXT("points_without_ground"), Missed);
			if (Warnings.Num() > 0)
			{
				TArray<TSharedPtr<FJsonValue>> W;
				for (const FString& S : Warnings) W.Add(MakeShared<FJsonValueString>(S));
				Out->SetArrayField(TEXT("warnings"), W);
			}
			Out->SetStringField(TEXT("hint"), TEXT("The river carves the landscape only when a WaterBrushManager / Landscape water brush exists (Water plugin 'Water Brush' setup) — otherwise it is a water surface only. Set the water material with set_actor_property (WaterBodyComponent.WaterMaterial) or set_component_property; use the `water` umbrella for waves and zones."));
			return Ok(Out);
		}

		// Fallback: spline + spline-mesh strip (same as a road, slightly below the surface).
		if (ArgStr(Args, TEXT("mesh_path")).IsEmpty())
			return Fail(FString::Printf(TEXT("Water plugin path unavailable (%s) and no mesh_path given for the spline-mesh fallback. Pass mesh_path (a flat water plane StaticMesh along +X) or enable the Water plugin."), *FallbackReason));
		FUECPToolResult R = BuildSplineStrip(World, Args, TEXT("River"), Width, ArgNum(Args, TEXT("ground_offset"), -20.0), TEXT("river"));
		if (!R.bSuccess) return R;
		TSharedPtr<FJsonObject> J = ParseJson(R.ResultJson);
		if (!J.IsValid()) return R;
		J->SetStringField(TEXT("path"), TEXT("spline_mesh_fallback"));
		J->SetStringField(TEXT("fallback_reason"), FallbackReason);
		return Ok(J.ToSharedRef());
	}
}
