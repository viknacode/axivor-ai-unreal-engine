// Copyright 2026, BlueprintsLab, All rights reserved

#include "UECPToolsModule.h"
#include "UECPCoreModule.h"
#include "Services/IUECPToolDispatcher.h"
#include "Services/IUECPCreateAssetRegistry.h"
#include "Services/UECPToolSafety.h"

#include "Tools/AnalysisTools.h"
#include "Tools/ArchitectRunnerTools.h"
#include "Tools/AssetManagementTools.h"
#include "Tools/AssetPropertyTools.h"
#include "Tools/AssetTools.h"
#include "Tools/AssetVerificationTools.h"
#include "Tools/CreateAssetTools.h"
#include "Tools/CurveTools.h"
#include "Tools/DependencyGraphTools.h"
#include "Tools/EditorUtilityTools.h"
#include "Tools/ImportTools.h"
#include "Tools/FileSystemTools.h"
#include "Tools/FileTools.h"
#include "Tools/GitTools.h"
#include "Tools/GroomTools.h"
#include "Tools/PhysicsFoliageTools.h"
#include "Tools/PhysicsAssetTools.h"
#include "Tools/ObjectPropertyTools.h"
#include "Tools/PlayTestTools.h"
#include "Tools/PluginTools.h"
#include "Tools/ProjectScanTools.h"
#include "Tools/ProfilerTools.h"
#include "Tools/RenderingTools.h"
#include "Tools/ConfigTools.h"
#include "Tools/StringTableTools.h"
#include "Tools/UserInteractionTools.h"

// Tools/SelfTestTools.cpp (module-private; no public header).
namespace SelfTestTools
{
	void HandleSelfTestFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
}

DEFINE_LOG_CATEGORY(LogUECPTools);

namespace
{
	using FHandlerFn        = void(*)(const TSharedPtr<FJsonObject>&, FString&, FString&);
	using FHandlerSummaryFn = void(*)(const TSharedPtr<FJsonObject>&, FString&, FString&, FString&);

	IUECPToolDispatcher::FToolHandler MakeHandler(FHandlerFn Fn)
	{
		return [Fn](const TSharedPtr<FJsonObject>& Args) -> FUECPToolResult
		{
			FUECPToolResult Result;
			FString OutJson, OutError;
			Fn(Args, OutJson, OutError);
			Result.bSuccess = OutError.IsEmpty();
			Result.ResultJson = MoveTemp(OutJson);
			Result.ErrorMessage = MoveTemp(OutError);
			return Result;
		};
	}

	IUECPToolDispatcher::FToolHandler MakeHandler(FHandlerSummaryFn Fn)
	{
		return [Fn](const TSharedPtr<FJsonObject>& Args) -> FUECPToolResult
		{
			FUECPToolResult Result;
			FString OutJson, OutError, OutSummary;
			Fn(Args, OutJson, OutError, OutSummary);
			Result.bSuccess = OutError.IsEmpty();
			Result.ResultJson = MoveTemp(OutJson);
			Result.ErrorMessage = MoveTemp(OutError);
			Result.SummaryJson = MoveTemp(OutSummary);
			return Result;
		};
	}
}

#define REGISTER_TOOL(ToolName, Fn) Dispatcher.RegisterHandler(FName(TEXT(ToolName)), MakeHandler(&Fn))

#define REGISTER_TOOL_BG(ToolName, Fn) Dispatcher.RegisterHandler(FName(TEXT(ToolName)), MakeHandler(&Fn), EUECPToolThreadAffinity::AnyThread)

static void RegisterCoreToolMetadata(IUECPToolDispatcher& Dispatcher)
{
	auto Block = [&Dispatcher](const TCHAR* Umbrella)
	{
		const FName U(Umbrella);
		return [&Dispatcher, U](const TCHAR* Action, const TCHAR* Summary, const TCHAR* Params, bool bAlwaysShow = false)
		{ Dispatcher.RegisterToolMetadata({ FName(Action), U, FString(Summary), FString(Params), bAlwaysShow }); };
	};

	{
		auto Meta = Block(TEXT("asset_management"));
		Meta(TEXT("create_asset"),            TEXT("Single entry point for every content-browser asset factory."), TEXT("asset_type, name, save_path, options?"), true);
		Meta(TEXT("list_create_asset_types"), TEXT("Enumerate every registered create_asset type + owning extension."), TEXT(""));
		Meta(TEXT("get_asset_summary"),       TEXT("Auto-summary of a non-Blueprint asset (BP → use get_blueprint_skeleton)."), TEXT("asset_path"), true);
		Meta(TEXT("get_selected_nodes"),      TEXT("Selected nodes in the focused asset editor graph (guids+titles+classes)."), TEXT(""));
		Meta(TEXT("find_asset_by_name"),      TEXT("Find assets by name pattern (alias find_assets/find_asset/search_assets)."), TEXT("name_pattern, asset_type?"), true);
		Meta(TEXT("find_assets"),             TEXT("Alias of find_asset_by_name."), TEXT("name_pattern, asset_type?"));
		Meta(TEXT("find_asset"),              TEXT("Alias of find_asset_by_name."), TEXT("name_pattern, asset_type?"));
		Meta(TEXT("search_assets"),           TEXT("Alias of find_asset_by_name."), TEXT("name_pattern, asset_type?"));
		Meta(TEXT("find"),                    TEXT("Alias of find_asset_by_name."), TEXT("name_pattern, asset_type?"));
		Meta(TEXT("find_by_class"),           TEXT("Alias of find_asset_by_name (by class)."), TEXT("name_pattern, asset_type?"));
		Meta(TEXT("find_engine_meshes"),      TEXT("Alias of find_asset_by_name."), TEXT("name_pattern"));
		Meta(TEXT("find_assets_by_path"),     TEXT("Alias of find_asset_by_name."), TEXT("name_pattern"));
		Meta(TEXT("browse_assets"),           TEXT("Alias of find_asset_by_name."), TEXT("name_pattern"));
		Meta(TEXT("check_asset_exists"),      TEXT("Check whether an asset exists (alias asset_exists)."), TEXT("asset_path"));
		Meta(TEXT("asset_exists"),            TEXT("Alias of check_asset_exists."), TEXT("asset_path"));
		Meta(TEXT("get_asset_info"),          TEXT("Read an asset's summary/details (alias get_asset_details; same as get_asset_summary)."), TEXT("asset_path"));
		Meta(TEXT("get_asset_details"),       TEXT("Alias of get_asset_summary."), TEXT("asset_path"));
		Meta(TEXT("find_blueprint_by_parent"),TEXT("Find Blueprints by parent class."), TEXT("parent_class, search_path?"));
		Meta(TEXT("delete_asset"),            TEXT("Delete asset(s) (alias delete_assets; accepts asset_paths)."), TEXT("asset_path|asset_paths"));
		Meta(TEXT("delete_assets"),           TEXT("Alias of delete_asset."), TEXT("asset_paths"));
		Meta(TEXT("duplicate_asset"),         TEXT("Duplicate an asset."), TEXT("source_path, dest_path"));
		Meta(TEXT("move_asset"),              TEXT("Move an asset to a folder."), TEXT("asset_path, destination_folder"));
		Meta(TEXT("move_assets"),             TEXT("Move multiple assets to a folder."), TEXT("asset_paths, destination_folder"));
		Meta(TEXT("rename_asset"),            TEXT("Rename an asset."), TEXT("asset_path, new_name"));
		Meta(TEXT("create_folder"),           TEXT("Create a /Game content folder (alias ensure_folder)."), TEXT("folder_path"));
		Meta(TEXT("ensure_folder"),           TEXT("Alias of create_folder (idempotent)."), TEXT("folder_path"));
		Meta(TEXT("verify_assets_in_folder"), TEXT("Count + presence-check assets in a folder after a batch create."), TEXT("folder, recursive?, expected?"));
		Meta(TEXT("get_project_root_path"),   TEXT("Get the .uproject file path + its directory."), TEXT(""));
		Meta(TEXT("register_primary_asset_type"), TEXT("Register a Primary Asset Type (writes DefaultGame.ini)."), TEXT("asset_type_name, asset_base_class, directories_to_scan, has_blueprint_classes?"));
		Meta(TEXT("get_asset_manager_summary"),TEXT("Summarise the Asset Manager config."), TEXT(""));
		Meta(TEXT("reimport_asset"),          TEXT("Reimport an asset from its source file."), TEXT("asset_path"));
		Meta(TEXT("compile_blueprint"),       TEXT("Compile a Blueprint (alias compile)."), TEXT("blueprint_path"));
		Meta(TEXT("compile"),                 TEXT("Alias of compile_blueprint."), TEXT("blueprint_path"));
		Meta(TEXT("validate_blueprint"),      TEXT("Validate a Blueprint, surfacing compile errors."), TEXT("blueprint_path"));
		Meta(TEXT("get_level_blueprint"),     TEXT("Get the persistent level's Level Blueprint path."), TEXT(""));
		Meta(TEXT("save_asset"),              TEXT("Save one asset (alias save/save_assets; accepts batch)."), TEXT("asset_path"));
		Meta(TEXT("save_assets"),             TEXT("Save multiple assets."), TEXT("asset_paths"));
		Meta(TEXT("save"),                    TEXT("Alias of save_asset."), TEXT("asset_path|asset_paths"));
		Meta(TEXT("save_all_dirty_assets"),   TEXT("Save every dirty asset (call before done on level edits)."), TEXT(""));
		Meta(TEXT("open_asset_editor"),       TEXT("Open an asset's editor window (alias open_asset)."), TEXT("asset_path"));
		Meta(TEXT("open_asset"),              TEXT("Alias of open_asset_editor."), TEXT("asset_path"));
		Meta(TEXT("export_asset"),            TEXT("Export an asset to an OS file via UE's exporter."), TEXT("asset_path"));
		Meta(TEXT("validate_assets"),         TEXT("Run data validation on assets."), TEXT("asset_path|asset_paths"));
		Meta(TEXT("fix_up_redirectors"),      TEXT("Fix up redirectors under a folder."), TEXT("folder_path?"));
		Meta(TEXT("get_map_check_errors"),    TEXT("Run Map Check on the open level."), TEXT(""));
		Meta(TEXT("batch_rename_assets"),     TEXT("Batch rename via find/replace/prefix/suffix."), TEXT("folder_path, find?, replace?, prefix?, suffix?"));
		Meta(TEXT("list_assets_in_folder"),   TEXT("List assets in a folder (alias list_assets; cap 500, use class_filter)."), TEXT("folder_path, recursive?, class_filter?, name_pattern?, limit?, offset?"), true);
		Meta(TEXT("list_assets"),             TEXT("Alias of list_assets_in_folder."), TEXT("folder_path, recursive?, class_filter?"));
		Meta(TEXT("list_folder_contents"),    TEXT("Alias of list_assets_in_folder."), TEXT("folder_path"));
		Meta(TEXT("list_folder"),             TEXT("Alias of list_assets_in_folder."), TEXT("folder_path"));
		Meta(TEXT("get_current_folder"),      TEXT("The folder the user has open/selected in the Content Browser."), TEXT(""), true);
		Meta(TEXT("get_focused_content_browser_path"), TEXT("The folder open in the Content Browser."), TEXT(""));
		Meta(TEXT("get_focused_folder_path"), TEXT("Alias of get_focused_content_browser_path."), TEXT(""));
		Meta(TEXT("get_selected_assets"),     TEXT("Asset paths selected in the Content Browser (errors if none)."), TEXT(""), true);
		Meta(TEXT("get_selected_content_browser_assets"), TEXT("Asset paths selected in the Content Browser."), TEXT(""));
		Meta(TEXT("get_selected_blueprint_path"), TEXT("First selected Blueprint asset's object path."), TEXT(""));
		Meta(TEXT("import_texture"),          TEXT("Import a texture (PNG/TGA/EXR/JPG)."), TEXT("file_path, destination_path?, srgb?, compression?, lod_group?"));
		Meta(TEXT("import_static_mesh"),      TEXT("Import a static mesh (FBX/OBJ)."), TEXT("file_path, destination_path?, import_scale?, combine_meshes?, generate_collision?"));
		Meta(TEXT("import_skeletal_mesh"),    TEXT("Import a skeletal mesh (FBX) — creates Skeleton alongside."), TEXT("file_path, destination_path?, skeleton_path?, import_scale?"));
		Meta(TEXT("import_sound_wave"),       TEXT("Import a sound wave (WAV/OGG/FLAC)."), TEXT("file_path, destination_path?"));
		Meta(TEXT("import_animation"),        TEXT("Import an animation (FBX) onto a skeleton."), TEXT("file_path, destination_path?, skeleton_path"));
		Meta(TEXT("import_asset"),            TEXT("Import any file, auto-detecting type."), TEXT("file_path, destination_path?"));
	}

	{
		auto Meta = Block(TEXT("editor_utility"));
		Meta(TEXT("run_editor_utility_widget"),TEXT("Run an Editor Utility Widget."), TEXT("asset_path"));
		Meta(TEXT("exec_console_command"),    TEXT("Run an editor console command (alias run_console_command; routes py)."), TEXT("command"));
		Meta(TEXT("run_console_command"),     TEXT("Alias of exec_console_command."), TEXT("command"));
		Meta(TEXT("get_console_variable"),    TEXT("Read a console variable."), TEXT("variable_name"));
		Meta(TEXT("get_output_log"),          TEXT("Read the editor output log (first call starts capture)."), TEXT("line_count?, category_filter?, severity_filter?"));
		Meta(TEXT("take_viewport_screenshot"),TEXT("Screenshot the active editor viewport to a PNG (synchronous; the result carries the real path, size and byte count)."), TEXT("file_path?"));
		Meta(TEXT("scan_directory"),          TEXT("List disk files (NOT asset discovery — use list_assets_in_folder)."), TEXT("directory_path, extensions?"));
		Meta(TEXT("select_folder"),           TEXT("Select a folder on disk."), TEXT("folder_path"));
		Meta(TEXT("export_text_to_file"),     TEXT("Write text to a file (alias export_to_file)."), TEXT("file_name, content, format?"));
		Meta(TEXT("export_to_file"),          TEXT("Alias of export_text_to_file."), TEXT("file_name, content, format?"));
		Meta(TEXT("list_plugins"),            TEXT("List project+external plugins (include_engine for all)."), TEXT("include_engine?, include_directory?"));
		Meta(TEXT("find_plugin"),             TEXT("Find a plugin by substring (reports enabled state)."), TEXT("search_pattern"));
		Meta(TEXT("list_plugin_files"),       TEXT("List files inside a plugin."), TEXT("plugin_name, relative_path?"));
		Meta(TEXT("scan_and_index_project_blueprints"), TEXT("Index project Blueprints to Saved/AI (alias scan_project)."), TEXT(""));
		Meta(TEXT("scan_project"),            TEXT("Alias of scan_and_index_project_blueprints."), TEXT(""));
		Meta(TEXT("query_project_index"),     TEXT("Query the project Blueprint index."), TEXT("query"));
		Meta(TEXT("list_extension_tools"),    TEXT("List tools provided by loaded extensions."), TEXT(""));
	}

	{
		auto Meta = Block(TEXT("config"));
		Meta(TEXT("get_project_setting"),     TEXT("Read a typed project setting."), TEXT("section, key, ini_file"));
		Meta(TEXT("set_project_setting"),     TEXT("Write a project setting (reloads GConfig)."), TEXT("section, key, value, ini_file"));
		Meta(TEXT("resolve_setting"),         TEXT("Effective value GConfig resolves for a key (alias resolve)."), TEXT("section, key, file?"));
		Meta(TEXT("resolve"),                 TEXT("Alias of resolve_setting."), TEXT("section, key, file?"));
		Meta(TEXT("explain_setting"),         TEXT("Show which config layers contain a value."), TEXT("section?, key|setting, file?"));
		Meta(TEXT("diff_config_from_default"),TEXT("Diff project .ini against its Base*.ini."), TEXT("file, section_filter?"));
		Meta(TEXT("search_config"),           TEXT("Full-text search across .ini files."), TEXT("query, file?"));
		Meta(TEXT("get_config_section"),      TEXT("All key/value pairs in a config section."), TEXT("section, file?"));
		Meta(TEXT("list_config_files"),       TEXT("List known .ini files + layer labels."), TEXT("category?"));
	}

	{
		auto Meta = Block(TEXT("mesh"));
		Meta(TEXT("set_static_mesh_properties"), TEXT("Set nanite/lod_bias/collision/shadow/lightmap on a static mesh."), TEXT("mesh_path, nanite_enabled?, lod_bias?, collision_complexity?, cast_shadow?, lightmap_resolution?"));
		Meta(TEXT("set_texture_properties"),  TEXT("Set compression/srgb/mip/lod_group on a texture."), TEXT("texture_path, compression?, srgb?, mip_gen?, lod_group?"));
		Meta(TEXT("assign_physical_material"),TEXT("Assign a physical material to a mesh."), TEXT("mesh_path, phys_mat_path"));
		Meta(TEXT("add_mesh_socket"),         TEXT("Add a socket to a mesh (bone_name required for skeletal)."), TEXT("mesh_path, socket_name, location?, rotation?, scale?, bone_name?"));
		Meta(TEXT("remove_mesh_socket"),      TEXT("Remove a mesh socket."), TEXT("mesh_path, socket_name"));
		Meta(TEXT("list_mesh_sockets"),       TEXT("List a mesh's sockets."), TEXT("mesh_path"));
		Meta(TEXT("list_skeleton_bones"),     TEXT("List bones of a USkeleton/USkeletalMesh."), TEXT("mesh_path"));
		Meta(TEXT("get_static_mesh_info"),    TEXT("Inspect a static mesh."), TEXT("path"));
		Meta(TEXT("get_texture_info"),        TEXT("Inspect a texture."), TEXT("path"));
		Meta(TEXT("list_morph_targets"),      TEXT("List a skeletal mesh's morph targets."), TEXT("mesh_path"));
		Meta(TEXT("get_skeletal_mesh_info"),  TEXT("Inspect a skeletal mesh."), TEXT("mesh_path"));
		Meta(TEXT("list_clothing_assets"),    TEXT("List a skeletal mesh's clothing assets."), TEXT("mesh_path"));
		Meta(TEXT("get_cloth_config"),        TEXT("Read a mesh's Chaos cloth config."), TEXT("mesh_path, cloth_index?"));
		Meta(TEXT("set_cloth_config"),        TEXT("Set a Chaos cloth config param."), TEXT("mesh_path, cloth_index?, param_name, value"));
		Meta(TEXT("assign_physics_asset_to_skeletal_mesh"), TEXT("Assign a PhysicsAsset to a skeletal mesh."), TEXT("mesh_path, physics_asset_path"));
		Meta(TEXT("split_skeletal_mesh"),     TEXT("Partition a skeletal mesh at bone split points."), TEXT("mesh_path, bone_names, output_path, weight_threshold?"));
		Meta(TEXT("preview_split_skeletal_mesh"), TEXT("Dry-run a skeletal mesh split (no assets created)."), TEXT("mesh_path, bone_names, weight_threshold?"));
		Meta(TEXT("auto_generate_lods"),      TEXT("Auto-generate LODs for a mesh."), TEXT("mesh_path, lod_count, reduction_percents?, screen_sizes?"));
		Meta(TEXT("set_lod_screen_size"),     TEXT("Set a LOD's screen size."), TEXT("mesh_path, lod_index, screen_size"));
		Meta(TEXT("set_texture_max_size"),    TEXT("Cap a texture's max in-game size."), TEXT("texture_path, max_size"));
	}

	{
		auto Meta = Block(TEXT("curve"));
		Meta(TEXT("add_curve_key"),           TEXT("Add a key to a curve asset."), TEXT("asset_path, time, value, channel?, interp_mode?"));
		Meta(TEXT("remove_curve_key"),        TEXT("Remove a curve key."), TEXT("asset_path, time, channel?"));
		Meta(TEXT("get_curve_keys"),          TEXT("List a curve channel's keys."), TEXT("asset_path, channel?"));
		Meta(TEXT("set_curve_key_interp"),    TEXT("Set a key's interpolation mode."), TEXT("asset_path, time, channel, interp_mode"));
		Meta(TEXT("set_curve_key_tangent"),   TEXT("Set a key's arrive/leave tangents."), TEXT("asset_path, time, channel, arrive_tangent, leave_tangent"));
		Meta(TEXT("add_curve_table_row"),     TEXT("Add a row of keys to a Curve Table."), TEXT("asset_path, row_name, keys"));
	}

	{
		auto Meta = Block(TEXT("physics"));
		Meta(TEXT("set_physical_material_properties"), TEXT("Set friction/restitution/density/surface on a PhysicalMaterial."), TEXT("phys_mat_path, friction?, restitution?, density?, surface_type?"));
		Meta(TEXT("get_physics_asset_summary"), TEXT("Summarise a PhysicsAsset."), TEXT("asset_path"));
		Meta(TEXT("add_physics_body"),        TEXT("Add a body to a PhysicsAsset."), TEXT("physics_asset_path, bone_name, shape_type?, mass?"));
		Meta(TEXT("set_physics_body_properties"), TEXT("Set a physics body's mass/damping."), TEXT("physics_asset_path, bone_name, mass?, linear_damping?, angular_damping?"));
		Meta(TEXT("add_physics_constraint"),  TEXT("Add a constraint between two bones."), TEXT("physics_asset_path, bone1, bone2"));
		Meta(TEXT("physics_asset_add_body"),         TEXT("Add a body to a PhysicsAsset."), TEXT("physics_asset_path, bone_name"));
		Meta(TEXT("physics_asset_remove_body"),      TEXT("Remove a body."), TEXT("physics_asset_path, bone_name"));
		Meta(TEXT("physics_asset_get_body_names"),   TEXT("List body names."), TEXT("physics_asset_path"));
		Meta(TEXT("physics_asset_get_body_shapes"),  TEXT("List a body's shapes."), TEXT("physics_asset_path, bone_name"));
		Meta(TEXT("physics_asset_set_sphere"),       TEXT("Add/set a sphere shape on a body."), TEXT("physics_asset_path, bone_name, radius, center?"));
		Meta(TEXT("physics_asset_set_capsule"),      TEXT("Add/set a capsule shape on a body."), TEXT("physics_asset_path, bone_name, radius, length"));
		Meta(TEXT("physics_asset_set_box"),          TEXT("Add/set a box shape on a body."), TEXT("physics_asset_path, bone_name, extents"));
		Meta(TEXT("physics_asset_remove_shape"),     TEXT("Remove a shape from a body."), TEXT("physics_asset_path, bone_name, shape_index"));
		Meta(TEXT("physics_asset_set_body_physics_mode"), TEXT("Set a body's physics mode."), TEXT("physics_asset_path, bone_name, mode"));
		Meta(TEXT("physics_asset_get_body_physics_mode"), TEXT("Get a body's physics mode."), TEXT("physics_asset_path, bone_name"));
		Meta(TEXT("physics_asset_set_body_mass_scale"),   TEXT("Set a body's mass scale."), TEXT("physics_asset_path, bone_name, mass_scale"));
		Meta(TEXT("physics_asset_get_body_mass_scale"),   TEXT("Get a body's mass scale."), TEXT("physics_asset_path, bone_name"));
		Meta(TEXT("physics_asset_get_constraints"),       TEXT("List a PhysicsAsset's constraints."), TEXT("physics_asset_path"));
		Meta(TEXT("physics_asset_set_constraint_limits"), TEXT("Set a constraint's limits."), TEXT("physics_asset_path, constraint_index, <limits>"));
		Meta(TEXT("physics_asset_remove_constraint"),     TEXT("Remove a constraint."), TEXT("physics_asset_path, constraint_index"));
		Meta(TEXT("get_gc_summary"),          TEXT("Summarise a GeometryCollection."), TEXT("asset_path"));
		Meta(TEXT("get_fracture_summary"),    TEXT("Summarise a GeometryCollection's fracture state."), TEXT("asset_path"));
		Meta(TEXT("set_geometry_collection_properties"), TEXT("Set damage threshold/clustering on a GC."), TEXT("asset_path, damage_threshold?, enable_clustering?, max_cluster_level?"));
		Meta(TEXT("place_gc_actor"),          TEXT("Place a GeometryCollection actor."), TEXT("actor_label, gc_path, location_x/y/z?"));
		Meta(TEXT("configure_gc_actor"),      TEXT("Configure a GC actor's object/collision type."), TEXT("actor_label, object_type?, collision_type?, enable_clustering?, simulating?"));
		Meta(TEXT("fracture_uniform"),        TEXT("Open a GC in Fracture Mode for manual uniform-grid fracture (programmatic fracture not yet implemented)."), TEXT("geometry_collection_path"));
		Meta(TEXT("fracture_voronoi"),        TEXT("Open a GC in Fracture Mode for manual Voronoi fracture (programmatic fracture not yet implemented)."), TEXT("geometry_collection_path"));
		Meta(TEXT("fracture_clustered"),      TEXT("Open a GC in Fracture Mode for manual clustered fracture (programmatic fracture not yet implemented)."), TEXT("geometry_collection_path"));
		Meta(TEXT("set_fracture_auto_cluster"), TEXT("Auto-cluster a GC's fracture levels (batch)."), TEXT("geometry_collection_path, max_cluster_level?, cluster_group_index?"));
		Meta(TEXT("set_collision_preset"),    TEXT("Set a component's collision preset."), TEXT("blueprint_path, component_name, preset_name"));
		Meta(TEXT("set_collision_response"),  TEXT("Set a component's per-channel collision response."), TEXT("blueprint_path, component_name, channel, response"));
		Meta(TEXT("get_collision_info"),      TEXT("Read a component's collision setup (alias get_component_collision_profile)."), TEXT("blueprint_path, component_name"));
		Meta(TEXT("get_component_collision_profile"), TEXT("Alias of get_collision_info."), TEXT("blueprint_path, component_name"));
		Meta(TEXT("set_collision_enabled"),   TEXT("Set a component's collision-enabled mode."), TEXT("blueprint_path, component_name, collision_mode, generate_overlap_events?"));
		Meta(TEXT("set_physics_constraint_properties"), TEXT("Set a physics constraint component's motions/limits."), TEXT("blueprint_path, component_name, <motions>, <limits>"));
		Meta(TEXT("create_collision_channel"),TEXT("Create a project collision channel (DefaultEngine.ini; restart)."), TEXT("channel_name, default_response?, is_trace_channel?"));
		Meta(TEXT("list_collision_channels"), TEXT("List project collision channels."), TEXT(""));
	}

	{
		auto Meta = Block(TEXT("object_properties"));
		Meta(TEXT("object_properties_list"),  TEXT("List reflected properties of any UObject (asset or live actor)."), TEXT("object_path"));
		Meta(TEXT("object_properties_get"),   TEXT("Get property values from any UObject."), TEXT("object_path, property_names?"));
		Meta(TEXT("object_properties_set"),   TEXT("Set property values on any UObject."), TEXT("object_path, properties"));
	}

	{
		auto Meta = Block(TEXT("play_test"));
		Meta(TEXT("get_pie_status"),          TEXT("PIE running/map/time/fps/player/game_mode snapshot."), TEXT(""));
		Meta(TEXT("get_pie_log"),             TEXT("Read the PIE log (since_timestamp to tail)."), TEXT("line_count?, category_filter?, severity_filter?, since_timestamp?"));
		Meta(TEXT("get_pie_actors"),          TEXT("List PIE actors."), TEXT("class_filter?, max_count?"));
		Meta(TEXT("get_pie_actor_state"),     TEXT("Read a PIE actor's properties."), TEXT("actor_label, property_names?"));
		Meta(TEXT("get_pie_player_state"),    TEXT("Read the PIE player state."), TEXT("property_names?"));
		Meta(TEXT("get_pie_performance"),     TEXT("PIE performance snapshot."), TEXT(""));
		Meta(TEXT("get_visible_widgets"),     TEXT("Walk visible widgets → text + button labels."), TEXT("max_widgets?"));
		Meta(TEXT("look_at_target"),          TEXT("Trace forward from camera → hit actor."), TEXT("max_distance?, channel?"));
		Meta(TEXT("actors_near_player"),      TEXT("Sphere overlap around the player, closest-first."), TEXT("radius?, class_filter?"));
		Meta(TEXT("get_player_runtime_state"),TEXT("Combined player snapshot (tags/montage/GAS/flags)."), TEXT(""));
		Meta(TEXT("get_gas_attributes"),      TEXT("Full GAS attribute set from an actor."), TEXT("actor_label?"));
		Meta(TEXT("is_player_stuck"),         TEXT("Detect whether the player hasn't moved (call repeatedly)."), TEXT("window_seconds?, move_threshold?"));
		Meta(TEXT("pause_pie"),               TEXT("Pause/unpause PIE."), TEXT("paused?"));
		Meta(TEXT("set_time_dilation"),       TEXT("Set PIE time dilation (slow-mo/fast-forward)."), TEXT("dilation"));
		Meta(TEXT("simulate_input"),          TEXT("Press/release a key (auto-release with duration)."), TEXT("key, event_type, duration?"));
		Meta(TEXT("simulate_input_burst"),    TEXT("Schedule a timed sequence of key events."), TEXT("steps"));
		Meta(TEXT("simulate_mouse_delta"),    TEXT("Drive AddYaw/AddPitch input."), TEXT("delta_x, delta_y"));
		Meta(TEXT("simulate_input_axis"),     TEXT("Inject an EnhancedInput axis value."), TEXT("input_action, value"));
		Meta(TEXT("execute_pie_console_command"), TEXT("Run a console command in PIE."), TEXT("command"));
		Meta(TEXT("take_pie_screenshot"),     TEXT("Screenshot the PIE viewport."), TEXT("file_path?"));
		Meta(TEXT("set_pie_actor_property"),  TEXT("Set a PIE actor property."), TEXT("actor_label, property_name, property_value"));
		Meta(TEXT("pie_line_trace"),          TEXT("Line trace in PIE."), TEXT("start, end, channel?"));
		Meta(TEXT("call_pie_blueprint_function"), TEXT("Call a Blueprint function on a PIE actor."), TEXT("actor_label, function_name, params?"));
		Meta(TEXT("teleport_player"),         TEXT("Teleport the PIE pawn."), TEXT("location, rotation?"));
		Meta(TEXT("spawn_test_actor"),        TEXT("Spawn an actor in PIE."), TEXT("class_path, location?, rotation?"));
		Meta(TEXT("generate_test_report"),    TEXT("Write a play-test report markdown file."), TEXT("title, output_path?, sections?"));
		Meta(TEXT("wait_for_pie_event"),      TEXT("Block until a PIE condition (MCP/external only)."), TEXT("condition_type, timeout_seconds?, ..."));
		Meta(TEXT("run_pie_test_sequence"),   TEXT("Run a scripted PIE step sequence (MCP/external only)."), TEXT("steps, stop_on_failure?"));
		Meta(TEXT("profile_project"),         TEXT("Profile PIE for a duration (MCP/external only)."), TEXT("duration?, channels?"));
		Meta(TEXT("analyze_trace"),           TEXT("Read an existing .utrace file."), TEXT("trace_path"));
	}

	{
		auto Meta = Block(TEXT("render"));
		Meta(TEXT("configure_lumen"),         TEXT("Configure Lumen GI/reflections."), TEXT("enable?, quality?, reflection_quality?"));
		Meta(TEXT("configure_nanite"),        TEXT("Configure Nanite detail/perf."), TEXT("max_pixels_per_edge?"));
		Meta(TEXT("set_ray_tracing"),         TEXT("Enable/disable ray tracing (restart required)."), TEXT("enable"));
		Meta(TEXT("configure_global_illumination"), TEXT("Set GI method + quality."), TEXT("method, quality?"));
		Meta(TEXT("set_shadow_quality"),      TEXT("Set shadow resolution/cascades/distance."), TEXT("max_resolution?, max_cascades?, distance_scale?"));
		Meta(TEXT("set_anti_aliasing"),       TEXT("Set AA method (TAA|TSR|FXAA|MSAA|None)."), TEXT("method"));
		Meta(TEXT("set_screen_percentage"),   TEXT("Set screen percentage (10-200)."), TEXT("value"));
		Meta(TEXT("set_rendering_quality"),   TEXT("Set overall quality (Low..Cinematic)."), TEXT("quality_level"));
		Meta(TEXT("get_render_settings"),     TEXT("Read current rendering settings."), TEXT(""));
		Meta(TEXT("spawn_hdri_backdrop"),     TEXT("Spawn an HDRI backdrop (needs HDRIBackdrop plugin; batch)."), TEXT("actor_label, cubemap_path, location_x/y/z?, intensity?, size?"));
	}

	{
		auto Meta = Block(TEXT("groom"));
		Meta(TEXT("get_groom_info"),          TEXT("Inspect a groom (hair/lod/physics/rendering groups)."), TEXT("groom_path"));
		Meta(TEXT("set_groom_lod_settings"),  TEXT("Set a groom group's LOD settings (batch)."), TEXT("groom_path, group_index?, lod_index?, screen_size?, curve_decimation?"));
		Meta(TEXT("set_groom_physics"),       TEXT("Set a groom group's physics sim (batch)."), TEXT("groom_path, group_index, enable_simulation?, sub_steps?, iteration_count?"));
		Meta(TEXT("set_groom_rendering"),     TEXT("Set a groom group's hair width (batch)."), TEXT("groom_path, group_index, hair_width"));
		Meta(TEXT("assign_groom_material"),   TEXT("Assign a material to a groom group (batch)."), TEXT("groom_path, group_index, material_path"));
		Meta(TEXT("spawn_groom_component"),   TEXT("Spawn a groom component actor (batch)."), TEXT("actor_label, groom_path, location_x/y/z?"));
	}

	{
		auto Meta = Block(TEXT("project_viz"));
		Meta(TEXT("get_dependency_graph"),    TEXT("Asset dependency graph JSON for the depgraph block."), TEXT("root_path, max_depth?, include_engine?"));
		Meta(TEXT("get_inheritance_tree"),    TEXT("Class inheritance tree (nomnoml) for the flow block."), TEXT("root_class, folder_filter?"));
	}

	{
		auto Meta = Block(TEXT("string_table"));
		Meta(TEXT("add_string_table_entry"),  TEXT("Add a localized entry (batch via items=[])."), TEXT("string_table_path, key, source_string"));
		Meta(TEXT("edit_string_table_entry"), TEXT("Edit an entry's source string."), TEXT("string_table_path, key, source_string"));
		Meta(TEXT("get_string_table_entries"),TEXT("List a string table's entries."), TEXT("string_table_path"));
		Meta(TEXT("remove_string_table_entry"),TEXT("Remove an entry."), TEXT("string_table_path, key"));
	}

	{
		auto Meta = Block(TEXT("git_tools"));
		Meta(TEXT("git_status"),              TEXT("Working-tree status."), TEXT(""));
		Meta(TEXT("git_log"),                 TEXT("Commit log."), TEXT("file_path?, max_count?"));
		Meta(TEXT("git_commit"),              TEXT("Create a commit."), TEXT("message, files?, stage_all?"));
		Meta(TEXT("git_diff"),                TEXT("Show a diff."), TEXT("file_path?, revision?"));
		Meta(TEXT("git_revert"),              TEXT("Revert files."), TEXT("files|file_path"));
		Meta(TEXT("git_init"),                TEXT("Initialise a git repo."), TEXT("path?"));
		Meta(TEXT("git_branch"),              TEXT("List/create/switch/delete branches."), TEXT("sub_action, branch_name?"));
		Meta(TEXT("git_stash"),               TEXT("Stash push/pop/list."), TEXT("sub_action"));
		Meta(TEXT("git"),                     TEXT("Generic git entrypoint."), TEXT("sub_action, ..."));
		Meta(TEXT("run_command"),             TEXT("Run any shell command (push/pull/gh/PR). NEVER for /Game asset ops."), TEXT("command, working_dir?"));
	}

	{
		auto Meta = Block(TEXT("discovery"));
		Meta(TEXT("search_tools"),  TEXT("Find actions by English intent (returns action+params, usually enough to call)."), TEXT("query, umbrella?, max_results?"), true);
		Meta(TEXT("get_tool_docs"), TEXT("Fetch one action's params (action=) or an umbrella's full recipes (categories=)."), TEXT("category, action? | categories=[...], max_chars?=12000, offset?"), true);
		Meta(TEXT("uecp_selftest"), TEXT("Tool-registry drift report: handlers vs metadata vs tool_docs vs extension manifests (read-only)."), TEXT("scope?=all, include_docs?=true"));
	}

	{
		auto Meta = Block(TEXT("ask_user"));
		Meta(TEXT("ask_user"), TEXT("Ask the user structured questions (single/multi-select + free-text) in an inline card; blocks until they answer. Prefer over plain-text questions when gathering requirements/preferences."), TEXT("questions:[{header, question, multiSelect?, options:[{label, description?}]}], timeout_secs?"), true);
	}

	{
		auto Meta = Block(TEXT("memory"));
		Meta(TEXT("suggest_memory"), TEXT("Propose a durable memory (adds to the review queue) — preferred for AI-proposed memories."), TEXT("content, category"));
		Meta(TEXT("add_memory"),     TEXT("Write a memory immediately (bypasses review) — use only on explicit request."), TEXT("content, category"));
		Meta(TEXT("get_memories"),   TEXT("List stored memories (optional category filter)."), TEXT("category?"));
		Meta(TEXT("delete_memory"),  TEXT("Delete a memory by id."), TEXT("memory_id"));
	}
	{
		auto Meta = Block(TEXT("project_plan"));
		Meta(TEXT("create_plan"),  TEXT("Record the plan brief (goal, assets/paths, guidelines, decisions). The checklist is the separate 'task' tool."), TEXT("title, context"));
		Meta(TEXT("get_plan"),     TEXT("Read the active plan brief."), TEXT(""));
		Meta(TEXT("clear_plan"),   TEXT("Clear the active plan."), TEXT(""));
		Meta(TEXT("list_plans"),   TEXT("List plan briefs from other conversations."), TEXT(""));
		Meta(TEXT("import_plan"),  TEXT("Import a plan brief from another conversation."), TEXT("source_conv_id"));
	}
	{
		auto Meta = Block(TEXT("task"));
		Meta(TEXT("set_tasks"),    TEXT("Replace the whole task checklist (TodoWrite-style) — primary path."), TEXT("items:[{content, status?, verify?}]"));
		Meta(TEXT("add_task"),     TEXT("Append a task (verify = read-back that proves it)."), TEXT("content, at_index?, verify?"));
		Meta(TEXT("update_task"),  TEXT("Set a task's status (batch via items=[{index,status,verification?}]). Tasks with a verify line need verification to become done."), TEXT("index, status, verification?"));
		Meta(TEXT("edit_task"),    TEXT("Edit a task's content."), TEXT("index, content"));
		Meta(TEXT("remove_task"),  TEXT("Remove a task."), TEXT("index"));
		Meta(TEXT("reorder_task"), TEXT("Move a task."), TEXT("from_index, to_index"));
		Meta(TEXT("clear_tasks"),  TEXT("Clear the task list."), TEXT(""));
		Meta(TEXT("get_tasks"),    TEXT("Read the task list."), TEXT(""));
	}
	{
		auto Meta = Block(TEXT("working_notes"));
		Meta(TEXT("set_working_notes"),    TEXT("Replace the chat's working notes (alias action='set')."), TEXT("content"));
		Meta(TEXT("append_working_notes"), TEXT("Append to the chat's working notes (alias action='append')."), TEXT("content"));
		Meta(TEXT("get_working_notes"),    TEXT("Read the chat's working notes (alias action='get')."), TEXT(""));
		Meta(TEXT("clear_working_notes"),  TEXT("Clear the chat's working notes (alias action='clear')."), TEXT(""));
	}
}

void FUECPToolsModule::StartupModule()
{
	UE_LOG(LogUECPTools, Log, TEXT("FUECPToolsModule: Registering tool handlers"));

	if (!IUECPCoreModule::IsAvailable())
	{
		UE_LOG(LogUECPTools, Warning, TEXT("FUECPToolsModule: UECPCore not available — tool dispatcher unreachable"));
		return;
	}

	IUECPToolDispatcher& Dispatcher = IUECPCoreModule::Get().GetToolDispatcher();

	REGISTER_TOOL("get_selected_nodes", AnalysisTools::HandleGetSelectedNodesFromArgs);
	REGISTER_TOOL("get_asset_summary", AnalysisTools::HandleGetAssetSummaryFromArgs);

	REGISTER_TOOL("create_asset", CreateAssetTools::HandleCreateAssetFromArgs);
	REGISTER_TOOL("list_create_asset_types", CreateAssetTools::HandleListCreateAssetTypesFromArgs);
	REGISTER_TOOL("delete_asset", AssetManagementTools::HandleDeleteAssetFromArgs);
	REGISTER_TOOL("delete_assets", AssetManagementTools::HandleDeleteAssetFromArgs);
	REGISTER_TOOL("duplicate_asset", AssetManagementTools::HandleDuplicateAssetFromArgs);
	REGISTER_TOOL("move_asset", AssetManagementTools::HandleMoveAssetFromArgs);
	REGISTER_TOOL("move_assets", AssetManagementTools::HandleMoveAssetsFromArgs);
	REGISTER_TOOL("find_asset_by_name", AssetManagementTools::HandleFindAssetByNameFromArgs);
	REGISTER_TOOL("search_assets", AssetManagementTools::HandleFindAssetByNameFromArgs);
	REGISTER_TOOL("find_assets", AssetManagementTools::HandleFindAssetByNameFromArgs);
	REGISTER_TOOL("find_asset",  AssetManagementTools::HandleFindAssetByNameFromArgs);
	REGISTER_TOOL("check_asset_exists", AssetManagementTools::HandleCheckAssetExistsFromArgs);
	REGISTER_TOOL("asset_exists",       AssetManagementTools::HandleCheckAssetExistsFromArgs);
	REGISTER_TOOL("find_engine_meshes",  AssetManagementTools::HandleFindAssetByNameFromArgs);
	REGISTER_TOOL("find_assets_by_path", AssetManagementTools::HandleFindAssetByNameFromArgs);
	REGISTER_TOOL("browse_assets",       AssetManagementTools::HandleFindAssetByNameFromArgs);
	REGISTER_TOOL("get_asset_info",      AnalysisTools::HandleGetAssetSummaryFromArgs);
	REGISTER_TOOL("get_asset_details",   AnalysisTools::HandleGetAssetSummaryFromArgs);
	REGISTER_TOOL("find_asset", AssetManagementTools::HandleFindAssetByNameFromArgs);
	REGISTER_TOOL("find", AssetManagementTools::HandleFindAssetByNameFromArgs);
	REGISTER_TOOL("find_by_class", AssetManagementTools::HandleFindAssetByNameFromArgs);
	REGISTER_TOOL("rename_asset", AssetManagementTools::HandleRenameAssetFromArgs);
	REGISTER_TOOL("create_folder", AssetManagementTools::HandleCreateProjectFolderFromArgs);
	REGISTER_TOOL("ensure_folder", AssetManagementTools::HandleCreateProjectFolderFromArgs);
	REGISTER_TOOL("verify_assets_in_folder", AssetVerificationTools::HandleVerifyAssetsInFolderFromArgs);
	REGISTER_TOOL("get_project_root_path", AssetManagementTools::HandleGetProjectRootPathFromArgs);
	REGISTER_TOOL("register_primary_asset_type", AssetManagementTools::HandleRegisterPrimaryAssetTypeFromArgs);
	REGISTER_TOOL("get_asset_manager_summary", AssetManagementTools::HandleGetAssetManagerSummaryFromArgs);
	REGISTER_TOOL("reimport_asset", AssetManagementTools::HandleReimportAssetFromArgs);
	REGISTER_TOOL("compile_blueprint", AssetManagementTools::HandleCompileBlueprintFromArgs);
	REGISTER_TOOL("compile", AssetManagementTools::HandleCompileBlueprintFromArgs);
	REGISTER_TOOL("validate_blueprint", AssetManagementTools::HandleValidateBlueprintFromArgs);
	REGISTER_TOOL("get_level_blueprint", AssetManagementTools::HandleGetLevelBlueprintFromArgs);

	REGISTER_TOOL("get_current_folder", AssetTools::HandleGetCurrentFolderFromArgs);
	REGISTER_TOOL("list_assets_in_folder", AssetTools::HandleListAssetsInFolderFromArgs);
	REGISTER_TOOL("list_assets", AssetTools::HandleListAssetsInFolderFromArgs);
	REGISTER_TOOL("list_folder_contents", AssetTools::HandleListAssetsInFolderFromArgs);
	REGISTER_TOOL("list_folder", AssetTools::HandleListAssetsInFolderFromArgs);
	REGISTER_TOOL("get_selected_assets", AssetTools::HandleGetSelectedAssetsFromArgs);
	REGISTER_TOOL("find_blueprint_by_parent", AssetTools::HandleFindBlueprintsByParentFromArgs);
	REGISTER_TOOL("get_focused_content_browser_path", AssetTools::HandleGetFocusedContentBrowserPathFromArgs);
	REGISTER_TOOL("get_selected_content_browser_assets", AssetTools::HandleGetSelectedContentBrowserAssetsFromArgs);

	REGISTER_TOOL("add_mesh_socket", AssetPropertyTools::HandleAddMeshSocketFromArgs);
	REGISTER_TOOL("get_cloth_config", AssetPropertyTools::HandleGetClothConfigFromArgs);
	REGISTER_TOOL("remove_mesh_socket", AssetPropertyTools::HandleRemoveMeshSocketFromArgs);
	REGISTER_TOOL("set_cloth_config", AssetPropertyTools::HandleSetClothConfigFromArgs);
	REGISTER_TOOL("set_physical_material_properties", AssetPropertyTools::HandleSetPhysicalMaterialPropertiesFromArgs);
	REGISTER_TOOL("set_static_mesh_properties", AssetPropertyTools::HandleSetStaticMeshPropertiesFromArgs);
	REGISTER_TOOL("set_texture_properties", AssetPropertyTools::HandleSetTexturePropertiesFromArgs);
	REGISTER_TOOL("assign_physical_material", AssetPropertyTools::HandleAssignPhysicalMaterialFromArgs);
	REGISTER_TOOL("add_curve_table_row", AssetPropertyTools::HandleAddCurveTableRowFromArgs);
	REGISTER_TOOL("list_mesh_sockets", AssetPropertyTools::HandleListMeshSocketsFromArgs);
	REGISTER_TOOL("list_skeleton_bones", AssetPropertyTools::HandleListSkeletonBonesFromArgs);
	REGISTER_TOOL("get_static_mesh_info", AssetPropertyTools::HandleGetStaticMeshInfoFromArgs);
	REGISTER_TOOL("get_texture_info", AssetPropertyTools::HandleGetTextureInfoFromArgs);
	REGISTER_TOOL("list_morph_targets", AssetPropertyTools::HandleListMorphTargetsFromArgs);
	REGISTER_TOOL("get_skeletal_mesh_info", AssetPropertyTools::HandleGetSkeletalMeshInfoFromArgs);
	REGISTER_TOOL("assign_physics_asset_to_skeletal_mesh", AssetPropertyTools::HandleAssignPhysicsAssetToSkeletalMeshFromArgs);
	REGISTER_TOOL("list_clothing_assets", AssetPropertyTools::HandleListClothingAssetsFromArgs);
	REGISTER_TOOL("split_skeletal_mesh", AssetPropertyTools::HandleSplitSkeletalMeshFromArgs);
	REGISTER_TOOL("preview_split_skeletal_mesh", AssetPropertyTools::HandlePreviewSplitSkeletalMeshFromArgs);
	REGISTER_TOOL("auto_generate_lods", AssetPropertyTools::HandleAutoGenerateLODsFromArgs);
	REGISTER_TOOL("set_lod_screen_size", AssetPropertyTools::HandleSetLODScreenSizeFromArgs);
	REGISTER_TOOL("set_texture_max_size", AssetPropertyTools::HandleSetTextureMaxSizeFromArgs);

	REGISTER_TOOL("add_curve_key", CurveTools::HandleAddCurveKeyFromArgs);
	REGISTER_TOOL("remove_curve_key", CurveTools::HandleRemoveCurveKeyFromArgs);
	REGISTER_TOOL("get_curve_keys", CurveTools::HandleGetCurveKeysFromArgs);
	REGISTER_TOOL("set_curve_key_interp", CurveTools::HandleSetCurveKeyInterpFromArgs);
	REGISTER_TOOL("set_curve_key_tangent", CurveTools::HandleSetCurveKeyTangentFromArgs);

	REGISTER_TOOL("get_dependency_graph", DependencyGraphTools::HandleGetDependencyGraphFromArgs);
	REGISTER_TOOL("get_inheritance_tree", DependencyGraphTools::HandleGetInheritanceTreeFromArgs);

	REGISTER_TOOL("run_editor_utility_widget", EditorUtilityTools::HandleRunEditorUtilityWidgetFromArgs);
	REGISTER_TOOL("exec_console_command", EditorUtilityTools::HandleExecConsoleCommandFromArgs);
	REGISTER_TOOL("run_console_command",  EditorUtilityTools::HandleExecConsoleCommandFromArgs);
	REGISTER_TOOL("take_viewport_screenshot", EditorUtilityTools::HandleTakeViewportScreenshotFromArgs);
	REGISTER_TOOL("save_asset",  EditorUtilityTools::HandleSaveAssetFromArgs);
	REGISTER_TOOL("save_assets", EditorUtilityTools::HandleSaveAssetFromArgs);
	REGISTER_TOOL("save",        EditorUtilityTools::HandleSaveAssetFromArgs);
	REGISTER_TOOL("save_all_dirty_assets", EditorUtilityTools::HandleSaveAllDirtyAssetsFromArgs);
	REGISTER_TOOL("get_console_variable", EditorUtilityTools::HandleGetConsoleVariableFromArgs);
	REGISTER_TOOL("open_asset_editor", EditorUtilityTools::HandleOpenAssetEditorFromArgs);
	REGISTER_TOOL("open_asset", EditorUtilityTools::HandleOpenAssetEditorFromArgs);
	REGISTER_TOOL("export_asset", EditorUtilityTools::HandleExportAssetFromArgs);
	REGISTER_TOOL("get_output_log", EditorUtilityTools::HandleGetOutputLogFromArgs);
	REGISTER_TOOL("get_map_check_errors", EditorUtilityTools::HandleGetMapCheckErrorsFromArgs);
	REGISTER_TOOL("validate_assets", EditorUtilityTools::HandleValidateAssetsFromArgs);
	REGISTER_TOOL("fix_up_redirectors", EditorUtilityTools::HandleFixUpRedirectorsFromArgs);
	REGISTER_TOOL("get_project_setting", EditorUtilityTools::HandleGetProjectSettingFromArgs);
	REGISTER_TOOL("set_project_setting", EditorUtilityTools::HandleSetProjectSettingFromArgs);
	REGISTER_TOOL("batch_rename_assets", EditorUtilityTools::HandleBatchRenameAssetsFromArgs);

	REGISTER_TOOL("import_texture", ImportTools::HandleImportTextureFromArgs);
	REGISTER_TOOL("import_static_mesh", ImportTools::HandleImportStaticMeshFromArgs);
	REGISTER_TOOL("import_skeletal_mesh", ImportTools::HandleImportSkeletalMeshFromArgs);
	REGISTER_TOOL("import_sound_wave", ImportTools::HandleImportSoundWaveFromArgs);
	REGISTER_TOOL("import_animation", ImportTools::HandleImportAnimationFromArgs);
	REGISTER_TOOL("import_asset", ImportTools::HandleImportAssetFromArgs);

	REGISTER_TOOL("scan_directory", FileSystemTools::HandleScanDirectoryFromArgs);
	REGISTER_TOOL("get_tool_docs", FileSystemTools::HandleGetToolDocsFromArgs);
	REGISTER_TOOL("search_tools",   FileSystemTools::HandleSearchToolsFromArgs);
	REGISTER_TOOL("find_tool",      FileSystemTools::HandleSearchToolsFromArgs);
	REGISTER_TOOL("discover_tools", FileSystemTools::HandleSearchToolsFromArgs);
	REGISTER_TOOL("get_handle_reference", FileSystemTools::HandleGetHandleReferenceFromArgs);
	REGISTER_TOOL("uecp_selftest",  SelfTestTools::HandleSelfTestFromArgs);
	// No name-prefix heuristic classifies "uecp_selftest" as read-only, so declare it explicitly.
	UECPToolSafety::RegisterToolSafety(FName(TEXT("uecp_selftest")), EUECPToolSafety::Read);
	REGISTER_TOOL("mark_learning_step", FileSystemTools::HandleMarkLearningStepFromArgs);
	REGISTER_TOOL("export_text_to_file", FileSystemTools::HandleExportToFileFromArgs);

	REGISTER_TOOL_BG("git",         GitTools::HandleGitFromArgs);
	REGISTER_TOOL_BG("git_status",  GitTools::HandleGitFromArgs);
	REGISTER_TOOL_BG("git_log",     GitTools::HandleGitFromArgs);
	REGISTER_TOOL_BG("git_commit",  GitTools::HandleGitFromArgs);
	REGISTER_TOOL_BG("git_diff",    GitTools::HandleGitFromArgs);
	REGISTER_TOOL_BG("git_revert",  GitTools::HandleGitFromArgs);
	REGISTER_TOOL_BG("git_init",    GitTools::HandleGitFromArgs);
	REGISTER_TOOL_BG("git_branch",  GitTools::HandleGitFromArgs);
	REGISTER_TOOL_BG("git_stash",   GitTools::HandleGitFromArgs);
	REGISTER_TOOL_BG("run_command", GitTools::HandleGitFromArgs);

	REGISTER_TOOL_BG("ask_user", UserInteractionTools::HandleAskUserFromArgs);

	REGISTER_TOOL_BG("proceed_with_plan", UserInteractionTools::HandleProceedWithPlanFromArgs);

	REGISTER_TOOL("assign_groom_material", GroomTools::HandleAssignGroomMaterialFromArgs);
	REGISTER_TOOL("get_groom_info", GroomTools::HandleGetGroomInfoFromArgs);
	REGISTER_TOOL("set_groom_lod_settings", GroomTools::HandleSetGroomLODSettingsFromArgs);
	REGISTER_TOOL("set_groom_physics", GroomTools::HandleSetGroomPhysicsFromArgs);
	REGISTER_TOOL("set_groom_rendering", GroomTools::HandleSetGroomRenderingFromArgs);
	REGISTER_TOOL("spawn_groom_component", GroomTools::HandleSpawnGroomComponentFromArgs);

	REGISTER_TOOL("architect_start_chat", ArchitectRunnerTools::HandleArchitectStartChatFromArgs);
	REGISTER_TOOL("architect_stop_chat",  ArchitectRunnerTools::HandleArchitectStopChatFromArgs);
	REGISTER_TOOL("get_architect_chat_state", ArchitectRunnerTools::HandleGetArchitectChatStateFromArgs);

	REGISTER_TOOL("configure_gc_actor", PhysicsFoliageTools::HandleConfigureGCActorFromArgs);
	REGISTER_TOOL("fracture_clustered", PhysicsFoliageTools::HandleFractureClusteredFromArgs);
	REGISTER_TOOL("fracture_uniform", PhysicsFoliageTools::HandleFractureUniformFromArgs);
	REGISTER_TOOL("fracture_voronoi", PhysicsFoliageTools::HandleFractureVoronoiFromArgs);
	REGISTER_TOOL("get_fracture_summary", PhysicsFoliageTools::HandleGetFractureSummaryFromArgs);
	REGISTER_TOOL("get_gc_summary", PhysicsFoliageTools::HandleGetGCSummaryFromArgs);
	REGISTER_TOOL("place_gc_actor", PhysicsFoliageTools::HandlePlaceGCActorFromArgs);
	REGISTER_TOOL("set_fracture_auto_cluster", PhysicsFoliageTools::HandleSetFractureAutoClusterFromArgs);
	REGISTER_TOOL("get_physics_asset_summary", PhysicsFoliageTools::HandleGetPhysicsAssetSummaryFromArgs);
	REGISTER_TOOL("add_physics_body",            PhysicsAssetTools::HandleAddBodyFromArgs);
	REGISTER_TOOL("set_physics_body_properties", PhysicsFoliageTools::HandleSetPhysicsBodyPropertiesFromArgs);
	REGISTER_TOOL("add_physics_constraint",      PhysicsFoliageTools::HandleAddPhysicsConstraintFromArgs);
	REGISTER_TOOL("physics_asset_add_body",              PhysicsAssetTools::HandleAddBodyFromArgs);
	REGISTER_TOOL("physics_asset_remove_body",           PhysicsAssetTools::HandleRemoveBodyFromArgs);
	REGISTER_TOOL("physics_asset_get_body_names",        PhysicsAssetTools::HandleGetBodyNamesFromArgs);
	REGISTER_TOOL("physics_asset_get_body_shapes",       PhysicsAssetTools::HandleGetBodyShapesFromArgs);
	REGISTER_TOOL("physics_asset_set_sphere",            PhysicsAssetTools::HandleSetSphereFromArgs);
	REGISTER_TOOL("physics_asset_set_capsule",           PhysicsAssetTools::HandleSetCapsuleFromArgs);
	REGISTER_TOOL("physics_asset_set_box",               PhysicsAssetTools::HandleSetBoxFromArgs);
	REGISTER_TOOL("physics_asset_remove_shape",          PhysicsAssetTools::HandleRemoveShapeFromArgs);
	REGISTER_TOOL("physics_asset_set_body_physics_mode", PhysicsAssetTools::HandleSetBodyPhysicsModeFromArgs);
	REGISTER_TOOL("physics_asset_get_body_physics_mode", PhysicsAssetTools::HandleGetBodyPhysicsModeFromArgs);
	REGISTER_TOOL("physics_asset_set_body_mass_scale",   PhysicsAssetTools::HandleSetBodyMassScaleFromArgs);
	REGISTER_TOOL("physics_asset_get_body_mass_scale",   PhysicsAssetTools::HandleGetBodyMassScaleFromArgs);
	REGISTER_TOOL("physics_asset_get_constraints",       PhysicsAssetTools::HandleGetConstraintsFromArgs);
	REGISTER_TOOL("physics_asset_set_constraint_limits", PhysicsAssetTools::HandleSetConstraintLimitsFromArgs);
	REGISTER_TOOL("physics_asset_remove_constraint",     PhysicsAssetTools::HandleRemoveConstraintFromArgs);

	REGISTER_TOOL("object_properties_list", ObjectPropertyTools::HandleListPropertiesFromArgs);
	REGISTER_TOOL("object_properties_get",  ObjectPropertyTools::HandleGetPropertiesFromArgs);
	REGISTER_TOOL("object_properties_set",  ObjectPropertyTools::HandleSetPropertiesFromArgs);
	REGISTER_TOOL("set_collision_preset", PhysicsFoliageTools::HandleSetCollisionPresetFromArgs);
	REGISTER_TOOL("set_collision_response", PhysicsFoliageTools::HandleSetCollisionResponseFromArgs);
	REGISTER_TOOL("get_collision_info", PhysicsFoliageTools::HandleGetCollisionInfoFromArgs);
	REGISTER_TOOL("get_component_collision_profile", PhysicsFoliageTools::HandleGetCollisionInfoFromArgs);
	REGISTER_TOOL("set_collision_enabled", PhysicsFoliageTools::HandleSetCollisionEnabledFromArgs);
	REGISTER_TOOL("set_geometry_collection_properties", PhysicsFoliageTools::HandleSetGeometryCollectionPropertiesFromArgs);
	REGISTER_TOOL("set_physics_constraint_properties", PhysicsFoliageTools::HandleSetPhysicsConstraintPropertiesFromArgs);
	REGISTER_TOOL("create_collision_channel", PhysicsFoliageTools::HandleCreateCollisionChannelFromArgs);
	REGISTER_TOOL("list_collision_channels", PhysicsFoliageTools::HandleListCollisionChannelsFromArgs);

	REGISTER_TOOL("get_pie_status", PlayTestTools::HandleGetPieStatusFromArgs);
	REGISTER_TOOL("get_pie_log", PlayTestTools::HandleGetPieLogFromArgs);
	REGISTER_TOOL("get_pie_actors", PlayTestTools::HandleGetPieActorsFromArgs);
	REGISTER_TOOL("get_pie_actor_state", PlayTestTools::HandleGetPieActorStateFromArgs);
	REGISTER_TOOL("get_pie_player_state", PlayTestTools::HandleGetPiePlayerStateFromArgs);
	REGISTER_TOOL("simulate_input", PlayTestTools::HandleSimulateInputFromArgs);
	REGISTER_TOOL("execute_pie_console_command", PlayTestTools::HandleExecutePieConsoleCommandFromArgs);
	REGISTER_TOOL("take_pie_screenshot", PlayTestTools::HandleTakePieScreenshotFromArgs);
	REGISTER_TOOL("get_pie_performance", PlayTestTools::HandleGetPiePerformanceFromArgs);
	REGISTER_TOOL("set_pie_actor_property", PlayTestTools::HandleSetPieActorPropertyFromArgs);
	REGISTER_TOOL("pie_line_trace", PlayTestTools::HandlePieLineTraceFromArgs);
	REGISTER_TOOL("wait_for_pie_event", PlayTestTools::HandleWaitForPieEventFromArgs);
	REGISTER_TOOL("call_pie_blueprint_function", PlayTestTools::HandleCallPieBlueprintFunctionFromArgs);
	REGISTER_TOOL("run_pie_test_sequence", PlayTestTools::HandleRunPieTestSequenceFromArgs);
	REGISTER_TOOL("pause_pie", PlayTestTools::HandlePausePieFromArgs);
	REGISTER_TOOL("set_time_dilation", PlayTestTools::HandleSetTimeDilationFromArgs);
	REGISTER_TOOL("look_at_target", PlayTestTools::HandleLookAtTargetFromArgs);
	REGISTER_TOOL("is_player_stuck", PlayTestTools::HandleIsPlayerStuckFromArgs);
	REGISTER_TOOL("simulate_mouse_delta", PlayTestTools::HandleSimulateMouseDeltaFromArgs);
	REGISTER_TOOL("simulate_input_axis", PlayTestTools::HandleSimulateInputAxisFromArgs);
	REGISTER_TOOL("simulate_input_burst", PlayTestTools::HandleSimulateInputBurstFromArgs);
	REGISTER_TOOL("get_visible_widgets", PlayTestTools::HandleGetVisibleWidgetsFromArgs);
	REGISTER_TOOL("actors_near_player", PlayTestTools::HandleActorsNearPlayerFromArgs);
	REGISTER_TOOL("get_player_runtime_state", PlayTestTools::HandleGetPlayerRuntimeStateFromArgs);
	REGISTER_TOOL("teleport_player", PlayTestTools::HandleTeleportPlayerFromArgs);
	REGISTER_TOOL("spawn_test_actor", PlayTestTools::HandleSpawnTestActorFromArgs);
	REGISTER_TOOL("get_gas_attributes", PlayTestTools::HandleGetGasAttributesFromArgs);
	REGISTER_TOOL("generate_test_report", PlayTestTools::HandleGenerateTestReportFromArgs);

	REGISTER_TOOL("scan_and_index_project_blueprints", ProjectScanTools::HandleScanAndIndexProjectFromArgs);

	REGISTER_TOOL("spawn_hdri_backdrop", RenderingTools::HandleSpawnHDRIBackdropFromArgs);
	REGISTER_TOOL("configure_lumen", RenderingTools::HandleConfigureLumen);
	REGISTER_TOOL("configure_nanite", RenderingTools::HandleConfigureNanite);
	REGISTER_TOOL("set_ray_tracing", RenderingTools::HandleSetRayTracing);
	REGISTER_TOOL("configure_global_illumination", RenderingTools::HandleConfigureGlobalIllumination);
	REGISTER_TOOL("set_shadow_quality", RenderingTools::HandleSetShadowQuality);
	REGISTER_TOOL("set_anti_aliasing", RenderingTools::HandleSetAntiAliasing);
	REGISTER_TOOL("set_screen_percentage", RenderingTools::HandleSetScreenPercentage);
	REGISTER_TOOL("set_rendering_quality", RenderingTools::HandleSetRenderingQuality);
	REGISTER_TOOL("get_render_settings", RenderingTools::HandleGetRenderSettings);

	REGISTER_TOOL("list_plugins", PluginTools::HandleListPluginsFromArgs);
	REGISTER_TOOL("find_plugin", PluginTools::HandleFindPluginFromArgs);
	REGISTER_TOOL("list_plugin_files", PluginTools::HandleListPluginFilesFromArgs);

	REGISTER_TOOL("add_string_table_entry", StringTableTools::HandleAddStringTableEntryFromArgs);
	REGISTER_TOOL("edit_string_table_entry", StringTableTools::HandleEditStringTableEntryFromArgs);
	REGISTER_TOOL("get_string_table_entries", StringTableTools::HandleGetStringTableEntriesFromArgs);
	REGISTER_TOOL("remove_string_table_entry", StringTableTools::HandleRemoveStringTableEntryFromArgs);

	REGISTER_TOOL("resolve_setting",         ConfigTools::HandleResolveSettingFromArgs);
	REGISTER_TOOL("resolve",                 ConfigTools::HandleResolveSettingFromArgs);
	REGISTER_TOOL("explain_setting",         ConfigTools::HandleExplainSettingFromArgs);
	REGISTER_TOOL("diff_config_from_default",ConfigTools::HandleDiffConfigFromDefaultFromArgs);
	REGISTER_TOOL("search_config",           ConfigTools::HandleSearchConfigFromArgs);
	REGISTER_TOOL("get_config_section",      ConfigTools::HandleGetConfigSectionFromArgs);
	REGISTER_TOOL("list_config_files",       ConfigTools::HandleListConfigFilesFromArgs);

	REGISTER_TOOL   ("select_folder",               FileTools::HandleSelectFolderFromArgs);
	REGISTER_TOOL   ("get_selected_blueprint_path", AssetTools::HandleGetSelectedBlueprintPathFromArgs);
	REGISTER_TOOL   ("export_to_file",              FileSystemTools::HandleExportToFileFromArgs);
	REGISTER_TOOL   ("get_focused_folder_path",     AssetTools::HandleGetFocusedContentBrowserPathFromArgs);
	REGISTER_TOOL   ("scan_project",                ProjectScanTools::HandleScanAndIndexProjectFromArgs);
	REGISTER_TOOL   ("query_project_index",         ProjectScanTools::HandleQueryProjectIndexFromArgs);
	REGISTER_TOOL   ("list_extension_tools",        AssetTools::HandleListExtensionToolsFromArgs);

	REGISTER_TOOL_BG("analyze_trace",               ProfilerTools::HandleAnalyzeTraceFromArgs);
	REGISTER_TOOL_BG("profile_project",             ProfilerTools::HandleProfileProjectFromArgs);

	{
		IUECPCreateAssetRegistry& Reg = IUECPCoreModule::Get().GetCreateAssetRegistry();
		Reg.RegisterType(TEXT("Blueprint"),                  UECPCreateAsset::FactoryFromArgsFn(&AssetManagementTools::HandleCreateBlueprintFromArgs,                TEXT("Blueprint")),                  NAME_None);
		Reg.RegisterType(TEXT("BlueprintFunctionLibrary"),   UECPCreateAsset::FactoryFromArgsFn(&AssetManagementTools::HandleCreateBlueprintFunctionLibraryFromArgs, TEXT("BlueprintFunctionLibrary")),   NAME_None);
		Reg.RegisterType(TEXT("BlueprintInterface"),         UECPCreateAsset::FactoryFromArgsFn(&AssetManagementTools::HandleCreateBlueprintInterfaceFromArgs,       TEXT("BlueprintInterface")),         NAME_None);
		Reg.RegisterType(TEXT("MacroLibrary"),               UECPCreateAsset::FactoryFromArgsFn(&AssetManagementTools::HandleCreateMacroLibraryFromArgs,             TEXT("MacroLibrary")),               NAME_None);
		Reg.RegisterType(TEXT("EditorUtilityBlueprint"),     UECPCreateAsset::FactoryFromArgsFn(&EditorUtilityTools::HandleCreateEditorUtilityBlueprintFromArgs,     TEXT("EditorUtilityBlueprint")),     NAME_None);
		Reg.RegisterType(TEXT("EditorUtilityWidget"),        UECPCreateAsset::FactoryFromArgsFn(&EditorUtilityTools::HandleCreateEditorUtilityWidgetFromArgs,        TEXT("EditorUtilityWidget")),        NAME_None);
		Reg.RegisterType(TEXT("CurveFloat"),                 UECPCreateAsset::FactoryFromArgsFn(&CurveTools::HandleCreateCurveFloatFromArgs,                        TEXT("CurveFloat")),                 NAME_None);
		Reg.RegisterType(TEXT("CurveLinearColor"),           UECPCreateAsset::FactoryFromArgsFn(&CurveTools::HandleCreateCurveLinearColorFromArgs,                  TEXT("CurveLinearColor")),           NAME_None);
		Reg.RegisterType(TEXT("CurveVector"),                UECPCreateAsset::FactoryFromArgsFn(&CurveTools::HandleCreateCurveVectorFromArgs,                       TEXT("CurveVector")),                NAME_None);
		Reg.RegisterType(TEXT("CurveTable"),                 UECPCreateAsset::FactoryFromArgsFn(&AssetPropertyTools::HandleCreateCurveTableFromArgs,                TEXT("CurveTable")),                 NAME_None);
		Reg.RegisterType(TEXT("PhysicalMaterial"),           UECPCreateAsset::FactoryFromArgsFn(&AssetPropertyTools::HandleCreatePhysicalMaterialFromArgs,          TEXT("PhysicalMaterial")),           NAME_None);
		Reg.RegisterType(TEXT("PhysicsAsset"),               UECPCreateAsset::FactoryFromArgsFn(&PhysicsFoliageTools::HandleCreatePhysicsAssetFromArgs,             TEXT("PhysicsAsset")),               NAME_None);
		Reg.RegisterType(TEXT("GeometryCollection"),         UECPCreateAsset::FactoryFromArgsFn(&PhysicsFoliageTools::HandleCreateGeometryCollectionFromArgs,       TEXT("GeometryCollection")),         NAME_None);
		Reg.RegisterType(TEXT("GroomBinding"),               UECPCreateAsset::FactoryFromArgsFn(&GroomTools::HandleCreateGroomBindingFromArgs,                      TEXT("GroomBinding")),               NAME_None);
		Reg.RegisterType(TEXT("IESProfile"),                 UECPCreateAsset::FactoryFromArgsFn(&RenderingTools::HandleCreateIESProfileFromArgs,                    TEXT("IESProfile")),                 NAME_None);
		Reg.RegisterType(TEXT("StringTable"),                UECPCreateAsset::FactoryFromArgsFn(&StringTableTools::HandleCreateStringTableFromArgs,                 TEXT("StringTable")),                NAME_None);
		Reg.RegisterType(TEXT("Level"),                      UECPCreateAsset::FactoryFromArgsFn(&AssetManagementTools::HandleCreateLevelFromArgs,                    TEXT("World")),                      NAME_None);
		Reg.RegisterType(TEXT("World"),                      UECPCreateAsset::FactoryFromArgsFn(&AssetManagementTools::HandleCreateLevelFromArgs,                    TEXT("World")),                      NAME_None);
		Reg.RegisterType(TEXT("Map"),                        UECPCreateAsset::FactoryFromArgsFn(&AssetManagementTools::HandleCreateLevelFromArgs,                    TEXT("World")),                      NAME_None);
	}

	RegisterCoreToolMetadata(Dispatcher);

	UE_LOG(LogUECPTools, Log, TEXT("FUECPToolsModule: Registered %d tool handlers"), Dispatcher.ListTools().Num());
}

void FUECPToolsModule::ShutdownModule()
{
	if (IUECPCoreModule::IsAvailable())
	{
		IUECPCreateAssetRegistry& Reg = IUECPCoreModule::Get().GetCreateAssetRegistry();
		for (const TCHAR* T : { TEXT("Blueprint"), TEXT("BlueprintFunctionLibrary"), TEXT("BlueprintInterface"),
		                        TEXT("MacroLibrary"), TEXT("EditorUtilityBlueprint"), TEXT("EditorUtilityWidget"),
		                        TEXT("CurveFloat"), TEXT("CurveLinearColor"), TEXT("CurveVector"), TEXT("CurveTable"),
		                        TEXT("PhysicalMaterial"), TEXT("PhysicsAsset"), TEXT("GeometryCollection"),
		                        TEXT("GroomBinding"), TEXT("IESProfile"), TEXT("StringTable"),
		                        TEXT("Level"), TEXT("World"), TEXT("Map") })
		{
			Reg.UnregisterType(T);
		}
	}
	UE_LOG(LogUECPTools, Log, TEXT("FUECPToolsModule: ShutdownModule"));
}

#undef REGISTER_TOOL

IMPLEMENT_MODULE(FUECPToolsModule, UECPTools)
