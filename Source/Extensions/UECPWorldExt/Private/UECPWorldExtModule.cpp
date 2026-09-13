// Axivor AI — World Builder extension (umbrella `world`).
//
// Composes the Level Design tools through the dispatcher where they exist (populate_landscape,
// spawn_spline_actor, set_spline_mesh, set_pcg_component_properties, generate_pcg,
// create_level_instance) and drives engine APIs directly for the rest (ground snapping /
// alignment, reflection-based mass edits, Water plugin rivers resolved by reflection, and
// FLandscapeEditDataInterface for heightmap import / sculpt / paint). No link-time dependency
// on the Water plugin: its classes are looked up at runtime and the tool falls back cleanly.

#include "UECPWorldExtModule.h"
#include "WorldExtCommon.h"
#include "Services/UECPToolSafety.h"

DEFINE_LOG_CATEGORY(LogUECPWorldExt);

namespace
{
	static const TCHAR* GWorldToolNames[] = {
		TEXT("world_build_biome"),
		TEXT("world_build_road"),
		TEXT("world_build_river"),
		TEXT("world_snap_to_grid"),
		TEXT("world_align_to_surface"),
		TEXT("world_place_prefab"),
		TEXT("world_mass_edit"),
		TEXT("world_landscape_import_heightmap"),
		TEXT("world_landscape_sculpt"),
		TEXT("world_landscape_paint_layer"),
		TEXT("world_landscape_flatten_spline"),
	};
}

void FUECPWorldExtModule::StartupModule()
{
	if (!IUECPCoreModule::IsAvailable()) return;
	IUECPToolDispatcher& D = IUECPCoreModule::Get().GetToolDispatcher();

	D.RegisterHandler(TEXT("world_build_biome"),               WorldExt::HandleBuildBiome);
	D.RegisterHandler(TEXT("world_build_road"),                WorldExt::HandleBuildRoad);
	D.RegisterHandler(TEXT("world_build_river"),               WorldExt::HandleBuildRiver);
	D.RegisterHandler(TEXT("world_snap_to_grid"),              WorldExt::HandleSnapToGrid);
	D.RegisterHandler(TEXT("world_align_to_surface"),          WorldExt::HandleAlignToSurface);
	D.RegisterHandler(TEXT("world_place_prefab"),              WorldExt::HandlePlacePrefab);
	D.RegisterHandler(TEXT("world_mass_edit"),                 WorldExt::HandleMassEdit);
	D.RegisterHandler(TEXT("world_landscape_import_heightmap"), WorldExt::HandleLandscapeImportHeightmap);
	D.RegisterHandler(TEXT("world_landscape_sculpt"),          WorldExt::HandleLandscapeSculpt);
	D.RegisterHandler(TEXT("world_landscape_paint_layer"),     WorldExt::HandleLandscapePaintLayer);
	D.RegisterHandler(TEXT("world_landscape_flatten_spline"),  WorldExt::HandleLandscapeFlattenSpline);

	{
		const FName U(TEXT("world"));
		const auto Meta = [&D, &U](const TCHAR* Action, const TCHAR* Summary, const TCHAR* Params)
		{ D.RegisterToolMetadata({ FName(Action), U, FString(Summary), FString(Params) }); };
		Meta(TEXT("world_build_biome"),
			TEXT("Build a biome on the landscape: one PCG scatter layer per mesh group (trees / bushes / grass / rocks) with per-preset density, spacing, scale range, slope limit and cull distance (composes populate_landscape). Layers without meshes are skipped and reported. Presets: temperate_forest | pine_forest | jungle | meadow | desert | rocky | wetland. Give each biome a disjoint zone (or bounds_actor) instead of covering the whole landscape every call; refuses to stack onto an existing PCG_<prefix>_* layer set unless replace:true. Excludes buildings/roads from every layer via set_pcg_exclusion and sets GenerateOnDemand."),
			TEXT("preset, meshes{trees[],bushes[],grass[],rocks[]}, landscape_label?, bounds_actor? (actor whose bounds limit the scatter) | zone?{center:{x,y}, extent:{x,y}} (world cm half-extents), replace?=false (delete + rebuild existing PCG_<name_prefix>_* layers), exclude?{world_collision?=true, actor_tags?[], actor_classes?[], spline_tags?=[\"AxivorRoad\"], spline_width?=600, margin?=0.15}, density_scale?=1, seed?, name_prefix?"));
		Meta(TEXT("world_build_road"),
			TEXT("Build a road: spawn a spline actor from points (or reuse spline_actor), ground-snap each point, smooth tangents, then lay one SplineMeshComponent per segment (forward axis X) scaled sideways to `width`. Tags the spline actor AxivorRoad + Road. clear_vegetation excludes the road footprint from overlapping PCG components; flatten_terrain carves the landscape under it (world_landscape_flatten_spline)."),
			TEXT("points[] ([x,y,z] world cm) | spline_actor, mesh_path, width?=600, material_path?, name?=Road, closed?=false, align_to_ground?=true, ground_offset?=5, collision?=true, clear_vegetation?=true, flatten_terrain?=false"));
		Meta(TEXT("world_build_river"),
			TEXT("Build a river: with the Water plugin enabled spawns a WaterBodyRiver (spline points, ground snap, RiverWidth / Depth curves); otherwise falls back to a spline + spline-mesh strip using mesh_path. Returns which path was taken."),
			TEXT("points[], width?=800, depth?=100, mesh_path? (required for the fallback), material_path?, use_water_plugin?=true, name?=River, align_to_ground?=true, ground_offset?"));
		Meta(TEXT("world_snap_to_grid"),
			TEXT("Snap actor locations (and optionally yaw / scale) to a grid. Uses the editor selection when actors[] is omitted."),
			TEXT("actors[]? | selection?=true, grid_size?=100, snap_z?=true, snap_rotation?=false, rotation_step?=15, rotation_all_axes?=false, snap_scale?=false, scale_step?=0.25"));
		Meta(TEXT("world_align_to_surface"),
			TEXT("Drop actors onto the surface below (line trace from above), optionally tilting them to the surface normal while keeping yaw; skips actors with no ground or slope > max_slope and reports them."),
			TEXT("actors[]? | selection?=true, align_to_normal?=false, keep_yaw?=true, max_slope?=90, offset?=0, trace_height?=10000, use_bounds_bottom?=true"));
		Meta(TEXT("world_place_prefab"),
			TEXT("Place a Blueprint actor or Level Instance at points, on a grid or along a spline, with yaw_random | fixed | face_path rotation and optional ground snapping / scale jitter. Returns the labels created."),
			TEXT("prefab (Blueprint path | level path | /Script class), points[] | grid{rows,cols,spacing,origin?,yaw?,jitter?} | along_spline{actor,spacing,offset?,start_offset?}, rotation_mode?=yaw_random|fixed|face_path, rotation? {pitch,yaw,roll}, snap_to_ground?=true, ground_offset?=0, seed?, name_prefix?, scale_min?=1, scale_max?, max_count?=500"));
		Meta(TEXT("world_mass_edit"),
			TEXT("Filter level actors (class, label, tag, folder, box, selection) and apply in one transaction: property sets by reflection ('Prop' or 'Component.Prop'), transform offsets / scale multiply, a material on every mesh slot, outliner folder, tags. dry_run lists matches only."),
			TEXT("filter{class?, label_contains?, label_prefix?, labels[]?, tag?, folder?, in_box{min,max}?, selection?}, set{Prop: value | Component.Prop: value}?, transform{offset_location?, offset_rotation?, scale_multiply?}?, material_path?, folder?, tags_add[]?, tags_remove[]?, dry_run?=false, max_actors?=2000"));
		Meta(TEXT("world_landscape_import_heightmap"),
			TEXT("Replace the landscape heightmap from a 16-bit PNG or .r16/.raw file (resampled to the landscape size when it differs) through FLandscapeEditDataInterface, inside the current / given edit layer."),
			TEXT("file_path, landscape_label?, scale_z? (sets the landscape Z scale), edit_layer?, flip_y?=false"));
		Meta(TEXT("world_landscape_sculpt"),
			TEXT("Sculpt a circular region of the landscape: raise | lower (strength = cm) | flatten (to target_height or the centre height) | smooth (box blur), with a smoothstep falloff."),
			TEXT("center{x,y} (world cm), radius (cm), mode=raise|lower|flatten|smooth, strength? (cm for raise/lower, 0..1 blend for flatten/smooth), falloff?=0.5, target_height?, smooth_kernel?=2, landscape_label?, edit_layer?"));
		Meta(TEXT("world_landscape_paint_layer"),
			TEXT("Paint a landscape material layer by rule: fill everything, height_rule (world Z range), slope_rule (degrees), or circle brush — rules can be combined. Self-heals: if the layer has no LayerInfo yet but the landscape material declares it, calls add_landscape_layer_info automatically and retries."),
			TEXT("layer_name, mode=fill|height_rule|slope_rule|circle, min_height?, max_height?, min_slope?, max_slope?, height_blend?, slope_blend?, center{x,y}?, radius?, falloff?=0.5, strength?=1, invert?=false, erase?=false, landscape_label?, edit_layer?"));
		Meta(TEXT("world_landscape_flatten_spline"),
			TEXT("Flatten a strip of landscape under a spline (e.g. a road): samples the spline every sample_step cm and carves a disc at each sample's height into the heightmap, keeping the max weight per vertex so overlapping discs blend cleanly."),
			TEXT("spline_actor, width?=600, falloff?=0.5, sample_step?, landscape_label?, edit_layer?, height_offset?=0"));
	}

	using UECPToolSafety::RegisterToolSafety;
	RegisterToolSafety(TEXT("world_build_biome"),                EUECPToolSafety::Write);
	RegisterToolSafety(TEXT("world_build_road"),                 EUECPToolSafety::Write);
	RegisterToolSafety(TEXT("world_build_river"),                EUECPToolSafety::Write);
	RegisterToolSafety(TEXT("world_snap_to_grid"),               EUECPToolSafety::Write);
	RegisterToolSafety(TEXT("world_align_to_surface"),           EUECPToolSafety::Write);
	RegisterToolSafety(TEXT("world_place_prefab"),               EUECPToolSafety::Write);
	RegisterToolSafety(TEXT("world_mass_edit"),                  EUECPToolSafety::Destructive);
	RegisterToolSafety(TEXT("world_landscape_import_heightmap"), EUECPToolSafety::Destructive);
	RegisterToolSafety(TEXT("world_landscape_sculpt"),           EUECPToolSafety::Destructive);
	// Runs inside a transaction and is trivially undoable; Destructive triggered confirmation
	// dialogs that stalled the AI mid-workflow (e.g. self-healing add_landscape_layer_info calls).
	RegisterToolSafety(TEXT("world_landscape_paint_layer"),      EUECPToolSafety::Write);
	RegisterToolSafety(TEXT("world_landscape_flatten_spline"),   EUECPToolSafety::Destructive);

	UE_LOG(LogUECPWorldExt, Log, TEXT("Axivor World Builder registered (umbrella 'world', %d tools)."), (int32)UE_ARRAY_COUNT(GWorldToolNames));
}

void FUECPWorldExtModule::ShutdownModule()
{
	if (!IUECPCoreModule::IsAvailable()) return;
	IUECPToolDispatcher& D = IUECPCoreModule::Get().GetToolDispatcher();
	for (const TCHAR* N : GWorldToolNames)
	{
		D.UnregisterHandler(FName(N));
	}
}

IMPLEMENT_MODULE(FUECPWorldExtModule, UECPWorldExt)
