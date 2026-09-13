// Axivor AI — domain workflow recipes (umbrella `workflow`).
// Each recipe is an ordered list of steps: what to call, which arguments matter, and the
// read-back that verifies the step. The Architect is told to fetch a recipe before any
// multi-step build so decomposition is consistent and every step is verified.

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "UECPCoreModule.h"
#include "Services/IUECPToolDispatcher.h"
#include "Services/UECPToolSafety.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
	struct FStep { const TCHAR* Tool; const TCHAR* What; const TCHAR* Verify; };
	struct FRecipe { const TCHAR* Id; const TCHAR* Title; const TCHAR* Umbrellas; const TCHAR* Notes; TArray<FStep> Steps; };

	static const TArray<FRecipe>& Recipes()
	{
		static const TArray<FRecipe> R = {
			{ TEXT("gameplay_ability"), TEXT("New Gameplay Ability (GAS)"), TEXT("gas, blueprint, gameplay_tags"),
			  TEXT("Requires the GameplayAbilities plugin. Put the AbilitySystemComponent on PlayerState for multiplayer."),
			  { { TEXT("gas.list_gameplay_abilities / get_attribute_set_summary"), TEXT("Inspect existing abilities, attribute set and ASC owner"), TEXT("know the ASC class and attribute names") },
			    { TEXT("gameplay_tags.add_gameplay_tag"), TEXT("Create Ability.*, State.*, Cooldown.* tags"), TEXT("gameplay_tags list shows them") },
			    { TEXT("asset_management.create_asset(GameplayAbility)"), TEXT("Create GA_<Name> blueprint"), TEXT("get_ability_summary returns the asset") },
			    { TEXT("gas.set_ability_tags / set_ability_cost / set_ability_cooldown / add_ability_trigger"), TEXT("Configure tags, cost GE, cooldown GE, triggers"), TEXT("get_ability_summary shows tags/cost/cooldown") },
			    { TEXT("blueprint.build_blueprint_graph"), TEXT("Implement ActivateAbility → CommitAbility → effects → EndAbility"), TEXT("compile_blueprint: 0 errors, no unconnected exec") },
			    { TEXT("gas.grant_ability_to_blueprint"), TEXT("Grant the ability on the owner (startup abilities)"), TEXT("gas_get_granted_abilities lists it in PIE") },
			    { TEXT("play_test"), TEXT("PIE: activate and read gas_get_active_tags / attribute values"), TEXT("expected attribute change observed") } } },

			{ TEXT("open_world"), TEXT("Open world environment"), TEXT("landscape, world, pcg, spline, level_actor, environment"),
			  TEXT("Rotation arrays are [pitch,yaw,roll]; use {\"yaw\":N}. Trees: yaw-only, ground-snapped, uniform scale. Prefer PCG/ISM over thousands of actors. "
			       "One PCG actor per zone — never stack whole-map layers: call world_build_biome with a disjoint zone{center,extent} (or bounds_actor) per biome, and pass replace:true to rebuild a zone instead of layering another full-landscape pass on top. "
			       "Before adding more vegetation, call get_pcg_level_summary and keep total_instances under budget (lower density_scale or shrink zones otherwise). "
			       "Exclusions (set_pcg_exclusion, wired automatically by world_build_biome's exclude{} and world_build_road's clear_vegetation) keep vegetation out of buildings and roads — don't rely on manual cleanup. "
			       "Roads: world_build_road(clear_vegetation:true, flatten_terrain:true) tags the spline AxivorRoad, clears overlapping PCG vegetation and flattens the landscape under it in one call. "
			       "Landscape material layers: add_landscape_layer_info(layer_name) then world_landscape_paint_layer (which now self-heals a missing LayerInfo automatically when the material declares the layer). "
			       "Prefer generation_trigger=GenerateOnDemand on biome PCG components (world_build_biome sets this) so vegetation isn't rebuilt on every load/PIE."),
			  { { TEXT("environment.get_environment_summary + level_actor.get_level_info"), TEXT("Check world partition, existing landscape, lighting"), TEXT("know landscape label/bounds") },
			    { TEXT("landscape.create_landscape (or world_landscape_import_heightmap)"), TEXT("Terrain with preset/heightmap; material with layer infos"), TEXT("get_landscape_info: components/size correct") },
			    { TEXT("landscape.add_landscape_layer_info + world.world_landscape_paint_layer"), TEXT("Grass/rock/dirt layers by height & slope rules (paint self-heals a missing LayerInfo if the material declares the layer)"), TEXT("get_landscape_info layers present, none are None") },
			    { TEXT("world.world_build_road (clear_vegetation:true, flatten_terrain:true) / world_build_river"), TEXT("Roads and rivers along splines, tagged AxivorRoad, terrain flattened, vegetation cleared along the path"), TEXT("get_spline_info; segments_created > 0; vegetation_cleared / flatten_terrain reported") },
			    { TEXT("pcg.get_pcg_level_summary"), TEXT("Check current total_instances before adding a biome"), TEXT("total_instances is within budget for the target platform") },
			    { TEXT("world.world_build_biome (zone{center,extent} or bounds_actor per biome; replace:true to rebuild a zone)"), TEXT("Trees/bushes/grass/rocks layers via PCG, one disjoint zone per biome, excluded from buildings/roads, GenerateOnDemand"), TEXT("generate_pcg counts > 0; layers[].exclusion has no error; viewport check via screenshot") },
			    { TEXT("world.world_place_prefab"), TEXT("Villages/POIs from blueprints or level instances along grids/splines"), TEXT("list_actors shows labels; world_align_to_surface skipped=0") },
			    { TEXT("environment.set_sky_atmosphere/directional light/fog + post_process"), TEXT("Lighting and atmosphere"), TEXT("take_viewport_screenshot looks right") },
			    { TEXT("level_actor.save_current_level"), TEXT("Save"), TEXT("level not dirty") } } },

			{ TEXT("locomotion_anim"), TEXT("Locomotion Animation Blueprint"), TEXT("animation"),
			  TEXT("Use create_locomotion_state_machine for the standard Idle/Walk/Run/Jump template; always finish with compile_anim_blueprint."),
			  { { TEXT("animation.get_anim_bp_summary"), TEXT("Inspect skeleton, existing state machines"), TEXT("skeleton path known") },
			    { TEXT("asset_management.create_asset(BlendSpace)+animation.set_blendspace_axis/add_blendspace_sample"), TEXT("Walk/Run blendspace on Speed (0..600)"), TEXT("get_blendspace_info: samples valid") },
			    { TEXT("animation.create_locomotion_state_machine"), TEXT("States + transitions + variables + output wiring"), TEXT("compile_errors empty, connections all connected") },
			    { TEXT("blueprint.build_blueprint_graph on AnimBP EventGraph"), TEXT("Update Speed/IsInAir from the owning pawn"), TEXT("compile_blueprint 0 errors") },
			    { TEXT("play_test"), TEXT("PIE and watch state changes"), TEXT("no T-pose, transitions fire") } } },

			{ TEXT("montage"), TEXT("Montage with sections and notifies"), TEXT("animation"),
			  TEXT("create_montage_from_sequence does the whole thing; notify classes must exist (LoadClass)."),
			  { { TEXT("animation.get_anim_sequence_info"), TEXT("Length, frame rate, existing notifies"), TEXT("duration known") },
			    { TEXT("animation.create_montage_from_sequence"), TEXT("Slot, sections (normalized times), loops, blend, notifies"), TEXT("get_montage_summary lists sections/notifies") },
			    { TEXT("blueprint.build_blueprint_graph"), TEXT("PlayMontage + section jumps in the character"), TEXT("compile_blueprint 0 errors") } } },

			{ TEXT("retarget"), TEXT("Retarget animations to a new character"), TEXT("ik_retarget, animation"),
			  TEXT("retarget_setup composes IK Rigs + retargeter + export; check chain mapping before exporting many clips."),
			  { { TEXT("animation.retarget_setup"), TEXT("IK rigs, chains, retargeter, auto-map, auto-align, export"), TEXT("per-animation results all ok") },
			    { TEXT("ik_retarget.get_retargeter_summary"), TEXT("Verify chain mapping and root settings"), TEXT("no unmapped chains") },
			    { TEXT("animation.get_anim_sequence_info on an exported clip"), TEXT("Sanity check frames/length"), TEXT("length ≈ source") } } },

			{ TEXT("motion_matching"), TEXT("Motion Matching setup"), TEXT("pose_search, chooser, animation"),
			  TEXT("Needs PoseSearch + Chooser plugins; database must be indexed (build_pose_search_database)."),
			  { { TEXT("animation.setup_motion_matching"), TEXT("Schema (trajectory + pose bones), database, entries, index, chooser"), TEXT("build_result ok, num_poses > 0") },
			    { TEXT("animation.add_motion_matching_node + add_pose_history"), TEXT("Wire Pose History → Motion Matching → Output in the AnimBP"), TEXT("compile_anim_blueprint 0 errors") },
			    { TEXT("blueprint"), TEXT("Trajectory generation in the AnimBP/Character (see gasp_guide motion_matching)"), TEXT("PIE moves without popping") } } },

			{ TEXT("material"), TEXT("Material with parameters and instances"), TEXT("material"),
			  TEXT("Validate after edits; shader compilation is async — read compile errors after it finishes."),
			  { { TEXT("material.create_material / add_material_node / connect_material_nodes"), TEXT("Build the graph (BaseColor/Normal/Roughness...)"), TEXT("get_material_summary: outputs connected") },
			    { TEXT("material.set_material_property"), TEXT("Blend mode, shading model, two-sided, Substrate if enabled"), TEXT("summary reflects settings") },
			    { TEXT("material.validate_material"), TEXT("Compile check"), TEXT("is_valid true after compilation") },
			    { TEXT("material.create_material_instance + set_instance_parameter"), TEXT("Instances per variant"), TEXT("instance parameters listed") } } },

			{ TEXT("niagara"), TEXT("Niagara effect"), TEXT("niagara"),
			  TEXT("Start from a template system when possible; verify emitter modules exist before setting parameters."),
			  { { TEXT("niagara.create_niagara_system (template)"), TEXT("System + emitter"), TEXT("get_niagara_summary lists emitters") },
			    { TEXT("niagara.add_module / set_module_input"), TEXT("Spawn rate, lifetime, velocity, color, size"), TEXT("summary shows values") },
			    { TEXT("level_actor.spawn_actor(NiagaraActor)"), TEXT("Place and preview"), TEXT("screenshot shows the effect") } } },

			{ TEXT("ui_widget"), TEXT("UMG widget"), TEXT("widget, blueprint"),
			  TEXT("Design the tree first, then bindings; use CommonUI when the plugin is enabled."),
			  { { TEXT("asset_management.create_asset(WidgetBlueprint)"), TEXT("Create WBP_<Name>"), TEXT("asset exists") },
			    { TEXT("widget.add_widget / set_widget_property / set_anchors"), TEXT("Build the hierarchy and layout"), TEXT("get_widget_tree matches design") },
			    { TEXT("blueprint.build_blueprint_graph"), TEXT("Bindings / events"), TEXT("compile_blueprint 0 errors") },
			    { TEXT("blueprint (player controller / HUD)"), TEXT("CreateWidget + AddToViewport"), TEXT("PIE shows the widget") } } },

			{ TEXT("data_table"), TEXT("Data-driven content (struct + table + data assets)"), TEXT("data"),
			  TEXT("Create the struct first; every row field must match the struct — unmatched fields are errors."),
			  { { TEXT("data.create_struct"), TEXT("Row struct with typed fields"), TEXT("get_struct_info lists fields") },
			    { TEXT("asset_management.create_asset(DataTable)"), TEXT("Table with the struct"), TEXT("get_data_table_info") },
			    { TEXT("data.add_data_table_row"), TEXT("Rows"), TEXT("row count matches, no fields_unmatched") } } },

			{ TEXT("level_streaming"), TEXT("World Partition / streaming setup"), TEXT("level_actor, level_streaming"),
			  TEXT("Prefer World Partition (5.x) over legacy sublevels for open worlds."),
			  { { TEXT("level_actor.get_world_partition_info"), TEXT("Check state"), TEXT("enabled true") },
			    { TEXT("level_actor.enable_world_partition + set_hlod_layer_properties"), TEXT("Enable + HLOD"), TEXT("info reflects it") },
			    { TEXT("level_actor.create_data_layer / assign_actor_to_data_layer"), TEXT("Data layers for optional content"), TEXT("list_data_layers") } } },

			{ TEXT("packaging"), TEXT("Package a build"), TEXT("packaging, validation, git_tools"),
			  TEXT("Validate before cooking; poll the job log."),
			  { { TEXT("validation.validate_project (scoped)"), TEXT("Asset validation on changed folders"), TEXT("0 errors") },
			    { TEXT("packaging.validate_packaging_setup"), TEXT("Maps, platforms, settings"), TEXT("no blockers") },
			    { TEXT("packaging.package_project"), TEXT("BuildCookRun"), TEXT("get_packaging_status: success") } } },

			{ TEXT("cpp_class"), TEXT("New C++ class"), TEXT("cpp_tools"),
			  TEXT("Files are UTF-8; compile after writing; use Live Coding only for .cpp-only changes."),
			  { { TEXT("cpp_tools.get_class_summary / find_class_definition"), TEXT("Find the parent and module"), TEXT("module name known") },
			    { TEXT("cpp_tools.create_cpp_class"), TEXT("Header + source with UCLASS/UPROPERTY"), TEXT("files exist") },
			    { TEXT("cpp_tools.compile_project"), TEXT("Build"), TEXT("errors empty (check errors_truncated)") },
			    { TEXT("asset_management.create_asset(Blueprint, parent=new class)"), TEXT("Blueprint child for designers"), TEXT("compile_blueprint 0 errors") } } },

			{ TEXT("metahuman"), TEXT("MetaHuman character (5.8)"), TEXT("ue58, groom"),
			  TEXT("MetaHumanCharacter plugin required; Mesh to MetaHuman via metahuman_api; builds are async in the editor."),
			  { { TEXT("ue58.ue58_feature_status"), TEXT("Check MetaHuman plugins"), TEXT("MetaHumanCharacter enabled") },
			    { TEXT("ue58.metahuman_api(probe)"), TEXT("Discover the exact API in this engine"), TEXT("method names listed") },
			    { TEXT("ue58.metahuman_api(conform_body_from_mesh / python)"), TEXT("Mesh to MetaHuman"), TEXT("conformed true") },
			    { TEXT("ue58.metahuman_api(can_build → build)"), TEXT("Assemble"), TEXT("build_requested true; assets appear") } } },

			{ TEXT("pve_tree"), TEXT("Procedural Vegetation tree (5.8 PVE)"), TEXT("ue58, pcg"),
			  TEXT("PVE graph is a PCG graph; export needs the PVE editor Generate/Export; Nanite Foliage recommended."),
			  { { TEXT("ue58.pve_create_vegetation"), TEXT("Asset + graph"), TEXT("graph_path returned") },
			    { TEXT("ue58.pve_list_nodes"), TEXT("Pick node classes"), TEXT("classes known") },
			    { TEXT("pcg.add_pcg_node/set_pcg_node_property/connect_pcg_nodes"), TEXT("Seed → growers → mesher → export"), TEXT("get_pcg_graph_summary shows the chain") },
			    { TEXT("ue58.pve_open_editor"), TEXT("User generates + exports"), TEXT("exported mesh asset exists") } } },
		};
		return R;
	}

	static FString RecipeToJson(const FRecipe& R)
	{
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("id"), R.Id);
		O->SetStringField(TEXT("title"), R.Title);
		O->SetStringField(TEXT("umbrellas"), R.Umbrellas);
		O->SetStringField(TEXT("notes"), R.Notes);
		TArray<TSharedPtr<FJsonValue>> Steps;
		int32 i = 1;
		for (const FStep& S : R.Steps)
		{
			TSharedRef<FJsonObject> SO = MakeShared<FJsonObject>();
			SO->SetNumberField(TEXT("n"), i++);
			SO->SetStringField(TEXT("tool"), S.Tool);
			SO->SetStringField(TEXT("do"), S.What);
			SO->SetStringField(TEXT("verify"), S.Verify);
			Steps.Add(MakeShared<FJsonValueObject>(SO));
		}
		O->SetArrayField(TEXT("steps"), Steps);
		O->SetStringField(TEXT("rule"), TEXT("Turn each step into a task (project_plan/task tools), run its verify read-back before marking it done, and report any failed verify instead of continuing."));
		FString Out;
		const TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
		FJsonSerializer::Serialize(O, W); W->Close();
		return Out;
	}

	static FUECPToolResult HandleGetWorkflow(const TSharedPtr<FJsonObject>& Args)
	{
		FString Domain;
		if (Args.IsValid()) Args->TryGetStringField(TEXT("domain"), Domain);
		Domain = Domain.ToLower().Replace(TEXT(" "), TEXT("_"));
		FUECPToolResult Res;
		for (const FRecipe& R : Recipes())
		{
			if (Domain == R.Id) { Res.bSuccess = true; Res.ResultJson = RecipeToJson(R); return Res; }
		}
		// fuzzy: contains
		for (const FRecipe& R : Recipes())
		{
			if (!Domain.IsEmpty() && (FString(R.Id).Contains(Domain) || Domain.Contains(R.Id))) { Res.bSuccess = true; Res.ResultJson = RecipeToJson(R); return Res; }
		}
		FString Ids;
		for (const FRecipe& R : Recipes()) Ids += FString::Printf(TEXT("%s%s"), Ids.IsEmpty() ? TEXT("") : TEXT(", "), R.Id);
		Res.bSuccess = false;
		Res.ErrorMessage = FString::Printf(TEXT("Unknown workflow '%s'. Available: %s"), *Domain, *Ids);
		return Res;
	}

	static FUECPToolResult HandleListWorkflows(const TSharedPtr<FJsonObject>&)
	{
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		TArray<TSharedPtr<FJsonValue>> Arr;
		for (const FRecipe& R : Recipes())
		{
			TSharedRef<FJsonObject> RO = MakeShared<FJsonObject>();
			RO->SetStringField(TEXT("id"), R.Id); RO->SetStringField(TEXT("title"), R.Title); RO->SetStringField(TEXT("umbrellas"), R.Umbrellas);
			RO->SetNumberField(TEXT("steps"), R.Steps.Num());
			Arr.Add(MakeShared<FJsonValueObject>(RO));
		}
		O->SetArrayField(TEXT("workflows"), Arr);
		FUECPToolResult Res; Res.bSuccess = true;
		const TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Res.ResultJson);
		FJsonSerializer::Serialize(O, W); W->Close();
		return Res;
	}
}

class FUECPWorkflowExtModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		if (!IUECPCoreModule::IsAvailable()) return;
		IUECPToolDispatcher& D = IUECPCoreModule::Get().GetToolDispatcher();
		D.RegisterHandler(TEXT("get_workflow"),   HandleGetWorkflow);
		D.RegisterHandler(TEXT("list_workflows"), HandleListWorkflows);
		FUECPToolMeta M1; M1.Action = TEXT("get_workflow"); M1.Umbrella = TEXT("workflow"); M1.bAlwaysShow = true;
		M1.Summary = TEXT("BEFORE any multi-step build, fetch the ordered recipe for the domain (steps + tool per step + verify read-back). Domains: gameplay_ability, open_world, locomotion_anim, montage, retarget, motion_matching, material, niagara, ui_widget, data_table, level_streaming, packaging, cpp_class, metahuman, pve_tree.");
		M1.Params = TEXT("domain");
		D.RegisterToolMetadata(M1);
		FUECPToolMeta M2; M2.Action = TEXT("list_workflows"); M2.Umbrella = TEXT("workflow");
		M2.Summary = TEXT("List available workflow recipes."); M2.Params = TEXT("");
		D.RegisterToolMetadata(M2);
		UECPToolSafety::RegisterToolSafety(TEXT("get_workflow"), EUECPToolSafety::Read);
		UECPToolSafety::RegisterToolSafety(TEXT("list_workflows"), EUECPToolSafety::Read);
	}
	virtual void ShutdownModule() override
	{
		if (!IUECPCoreModule::IsAvailable()) return;
		IUECPToolDispatcher& D = IUECPCoreModule::Get().GetToolDispatcher();
		D.UnregisterHandler(TEXT("get_workflow"));
		D.UnregisterHandler(TEXT("list_workflows"));
	}
};

IMPLEMENT_MODULE(FUECPWorkflowExtModule, UECPWorkflowExt)
