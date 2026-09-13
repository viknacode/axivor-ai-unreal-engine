// Copyright 2026, BlueprintsLab, All rights reserved

#include "UECPBlueprintExtModule.h"

#include "Tools/BlueprintAssetTools.h"
#include "Tools/BlueprintCDOTools.h"
#include "Tools/BlueprintCompareTools.h"
#include "Tools/BlueprintDeletionTools.h"
#include "Tools/BlueprintGraphTools.h"
#include "Tools/ComponentTools.h"
#include "Tools/GameplayTagTools.h"
#include "Tools/VariableTools.h"

#include "UECPCoreModule.h"
#include "Services/IUECPToolDispatcher.h"

DEFINE_LOG_CATEGORY(LogUECPBlueprintExt);

namespace
{
	static const TArray<FName>& OwnedToolNames()
	{
		static const TArray<FName> Names = {
			TEXT("get_blueprint_skeleton"),
			TEXT("get_blueprint_graph"),
			TEXT("get_function_graph"),
			TEXT("get_graph"),
			TEXT("get_blueprint_subgraph"),
			TEXT("discover_nodes"),
			TEXT("find_node"),
			TEXT("compare_blueprints"),
			TEXT("snapshot_blueprint"),
			TEXT("diff_blueprint_since_snapshot"),
			TEXT("set_cdo_property"), TEXT("set_class_default"),
			TEXT("get_cdo_properties"), TEXT("get_class_defaults"),
			TEXT("delete_unused_variables"),

			TEXT("delete_component"),  TEXT("delete_components"),
			TEXT("delete_function"),   TEXT("delete_functions"),
			TEXT("delete_variable"),   TEXT("delete_variables"),
			TEXT("delete_nodes"),      TEXT("delete_node"),

			TEXT("build_blueprint_graph"),
			TEXT("place_node"), TEXT("set_pin_default"),
			TEXT("connect_pins"),
			TEXT("disconnect_pins"),
			TEXT("arrange_blueprint_nodes"),
			TEXT("add_function"), TEXT("override_function"),
			TEXT("add_timeline"),
			TEXT("clear_blueprint_graph"),
			TEXT("undo_last_clear"),

			TEXT("add_blueprint_comment"),
			TEXT("delete_blueprint_comment"),
			TEXT("update_blueprint_comment"),

			TEXT("set_timeline_properties"), TEXT("add_timeline_track"),

			TEXT("add_component"), TEXT("edit_component_property"), TEXT("set_component_property"),
			TEXT("set_character_anim_class"),
			TEXT("remove_component"), TEXT("rename_component"),
			TEXT("reparent_component"), TEXT("attach_component"), TEXT("set_root_component"),
			TEXT("set_component_collision_profile"), TEXT("set_component_collision_response"), TEXT("set_component_collision_enabled"),
			TEXT("set_component_mobility"),
			TEXT("set_skeletal_mesh_component"), TEXT("set_static_mesh_component"),
			TEXT("set_camera_component_properties"), TEXT("set_audio_component_properties"),
			TEXT("set_component_cast_shadows"), TEXT("set_component_active"),
			TEXT("set_component_replication"), TEXT("set_component_transform"),

			TEXT("add_gameplay_tag"), TEXT("remove_gameplay_tag"), TEXT("get_gameplay_tags"),
			TEXT("assign_gameplay_tag_to_blueprint"),
			TEXT("add_gameplay_tags_bulk"),
			TEXT("add_gameplay_tags"),
			TEXT("find_referencers_by_tag"),

			TEXT("add_dispatcher_param"),
			TEXT("add_event_dispatcher"),
			TEXT("add_function_param"),
			TEXT("add_variable"),
			TEXT("rename_variable"),
			TEXT("set_blueprint_variable_default"),
			TEXT("set_variable_flags"),
			TEXT("set_variable_metadata"),
			TEXT("add_local_variable"),
			TEXT("set_variable_expose_on_spawn"), TEXT("set_variable_replication_condition"),
			TEXT("categorize_variables"),
			TEXT("delete_event_dispatcher"),
			TEXT("remove_dispatcher_param"),
			TEXT("set_function_replication"), TEXT("remove_function_param"),
			TEXT("set_function_access"), TEXT("set_function_pure"),
			TEXT("list_overridable_functions"),
			TEXT("reparent_blueprint"),

			TEXT("add_macro"),
			TEXT("delete_macro"),
			TEXT("add_macro_parameter"),
			TEXT("get_macro_summary"),
			TEXT("implement_blueprint_interface"),
			TEXT("implement_interface"),
			TEXT("add_interface"),
			TEXT("unimplement_blueprint_interface"),
			TEXT("unimplement_interface"),
			TEXT("remove_interface"),
			TEXT("rename_function"),
		};
		return Names;
	}

	using FHandlerFn        = void(*)(const TSharedPtr<FJsonObject>&, FString&, FString&);
	using FHandlerSummaryFn = void(*)(const TSharedPtr<FJsonObject>&, FString&, FString&, FString&);

	static IUECPToolDispatcher::FToolHandler MakeHandler(FHandlerFn Fn)
	{
		return [Fn](const TSharedPtr<FJsonObject>& Args) -> FUECPToolResult
		{
			FUECPToolResult R;
			Fn(Args, R.ResultJson, R.ErrorMessage);
			R.bSuccess = R.ErrorMessage.IsEmpty();
			return R;
		};
	}

	static IUECPToolDispatcher::FToolHandler MakeHandler(FHandlerSummaryFn Fn)
	{
		return [Fn](const TSharedPtr<FJsonObject>& Args) -> FUECPToolResult
		{
			FUECPToolResult R;
			Fn(Args, R.ResultJson, R.ErrorMessage, R.SummaryJson);
			R.bSuccess = R.ErrorMessage.IsEmpty();
			return R;
		};
	}
}

void FUECPBlueprintExtModule::StartupModule()
{
	if (!IUECPCoreModule::IsAvailable()) return;
	IUECPToolDispatcher& D = IUECPCoreModule::Get().GetToolDispatcher();

	D.RegisterHandler(TEXT("get_blueprint_skeleton"),   MakeHandler(BlueprintGraphTools::HandleGetBlueprintSkeletonFromArgs));
	D.RegisterHandler(TEXT("get_blueprint_graph"),      MakeHandler(BlueprintGraphTools::HandleGetGraphNodes));
	D.RegisterHandler(TEXT("get_function_graph"),       MakeHandler(BlueprintGraphTools::HandleGetGraphNodes));
	D.RegisterHandler(TEXT("get_graph"),                MakeHandler(BlueprintGraphTools::HandleGetGraphNodes));
	D.RegisterHandler(TEXT("get_blueprint_subgraph"),   MakeHandler(BlueprintGraphTools::HandleGetBlueprintSubgraphFromArgs));
	D.RegisterHandler(TEXT("discover_nodes"),           MakeHandler(BlueprintGraphTools::HandleDiscoverNodes));
	D.RegisterHandler(TEXT("find_node"),                MakeHandler(BlueprintGraphTools::HandleDiscoverNodes));
	D.RegisterHandler(TEXT("compare_blueprints"),       MakeHandler(BlueprintCompareTools::HandleCompareBlueprintsFromArgs));
	D.RegisterHandler(TEXT("snapshot_blueprint"),       MakeHandler(BlueprintCompareTools::HandleSnapshotBlueprintFromArgs));
	D.RegisterHandler(TEXT("diff_blueprint_since_snapshot"), MakeHandler(BlueprintCompareTools::HandleDiffBlueprintSinceSnapshotFromArgs));
	D.RegisterHandler(TEXT("set_cdo_property"),         MakeHandler(BlueprintCDOTools::HandleSetCDOPropertyFromArgs));
	D.RegisterHandler(TEXT("set_class_default"),        MakeHandler(BlueprintCDOTools::HandleSetCDOPropertyFromArgs));
	D.RegisterHandler(TEXT("get_cdo_properties"),       MakeHandler(BlueprintCDOTools::HandleGetCDOPropertiesFromArgs));
	D.RegisterHandler(TEXT("get_class_defaults"),       MakeHandler(BlueprintCDOTools::HandleGetCDOPropertiesFromArgs));
	D.RegisterHandler(TEXT("delete_unused_variables"),  MakeHandler(BlueprintDeletionTools::HandleDeleteUnusedVariablesFromArgs));

	D.RegisterHandler(TEXT("delete_component"),         MakeHandler(BlueprintDeletionTools::HandleDeleteComponentFromArgs));
	D.RegisterHandler(TEXT("delete_components"),        MakeHandler(BlueprintDeletionTools::HandleDeleteComponentFromArgs));
	D.RegisterHandler(TEXT("delete_function"),          MakeHandler(BlueprintDeletionTools::HandleDeleteFunctionFromArgs));
	D.RegisterHandler(TEXT("delete_functions"),         MakeHandler(BlueprintDeletionTools::HandleDeleteFunctionFromArgs));
	D.RegisterHandler(TEXT("delete_variable"),          MakeHandler(BlueprintDeletionTools::HandleDeleteVariableFromArgs));
	D.RegisterHandler(TEXT("delete_variables"),         MakeHandler(BlueprintDeletionTools::HandleDeleteVariableFromArgs));
	D.RegisterHandler(TEXT("delete_nodes"),             MakeHandler(BlueprintDeletionTools::HandleDeleteNodes));
	D.RegisterHandler(TEXT("delete_node"),              MakeHandler(BlueprintDeletionTools::HandleDeleteNodes));

	D.RegisterHandler(TEXT("build_blueprint_graph"),    MakeHandler(BlueprintGraphTools::HandleBuildGraphFromArgs));
	D.RegisterHandler(TEXT("place_node"),               MakeHandler(BlueprintGraphTools::HandlePlaceNodeFromArgs));
	D.RegisterHandler(TEXT("set_pin_default"),          MakeHandler(BlueprintGraphTools::HandleSetPinDefaultFromArgs));
	D.RegisterHandler(TEXT("connect_pins"),             MakeHandler(BlueprintGraphTools::HandleConnectPins));
	D.RegisterHandler(TEXT("disconnect_pins"),          MakeHandler(BlueprintGraphTools::HandleDisconnectPins));
	D.RegisterHandler(TEXT("arrange_blueprint_nodes"),  MakeHandler(BlueprintGraphTools::HandleArrangeNodes));
	D.RegisterHandler(TEXT("add_function"),             MakeHandler(BlueprintGraphTools::HandleAddFunction));
	D.RegisterHandler(TEXT("override_function"),        MakeHandler(BlueprintGraphTools::HandleOverrideFunction));
	D.RegisterHandler(TEXT("add_timeline"),             MakeHandler(BlueprintGraphTools::HandleAddTimeline));
	D.RegisterHandler(TEXT("clear_blueprint_graph"),    MakeHandler(BlueprintGraphTools::HandleClearGraph));
	D.RegisterHandler(TEXT("undo_last_clear"),          MakeHandler(BlueprintGraphTools::HandleUndoLastClear));

	D.RegisterHandler(TEXT("add_blueprint_comment"),    MakeHandler(BlueprintGraphTools::HandleAddBlueprintComment));
	D.RegisterHandler(TEXT("delete_blueprint_comment"), MakeHandler(BlueprintGraphTools::HandleDeleteBlueprintComment));
	D.RegisterHandler(TEXT("update_blueprint_comment"), MakeHandler(BlueprintGraphTools::HandleUpdateBlueprintComment));

	D.RegisterHandler(TEXT("set_timeline_properties"),  MakeHandler(BlueprintGraphTools::HandleSetTimelineProperties));
	D.RegisterHandler(TEXT("add_timeline_track"),       MakeHandler(BlueprintGraphTools::HandleAddTimelineTrack));

	D.RegisterHandler(TEXT("add_component"),                  MakeHandler(ComponentTools::HandleAddComponentFromArgs));
	D.RegisterHandler(TEXT("edit_component_property"),        MakeHandler(ComponentTools::HandleEditComponentPropertyFromArgs));
	D.RegisterHandler(TEXT("set_component_property"),         MakeHandler(ComponentTools::HandleEditComponentPropertyFromArgs));
	D.RegisterHandler(TEXT("get_component_property"),        MakeHandler(ComponentTools::HandleGetComponentPropertyFromArgs));
	D.RegisterHandler(TEXT("read_component_property"),       MakeHandler(ComponentTools::HandleGetComponentPropertyFromArgs));
	D.RegisterHandler(TEXT("set_character_anim_class"),       MakeHandler(ComponentTools::HandleSetCharacterAnimClassFromArgs));
	D.RegisterHandler(TEXT("remove_component"),               MakeHandler(ComponentTools::HandleRemoveComponentFromArgs));
	D.RegisterHandler(TEXT("rename_component"),               MakeHandler(ComponentTools::HandleRenameComponentFromArgs));
	D.RegisterHandler(TEXT("reparent_component"),             MakeHandler(ComponentTools::HandleReparentComponentFromArgs));
	D.RegisterHandler(TEXT("attach_component"),               MakeHandler(ComponentTools::HandleReparentComponentFromArgs));
	D.RegisterHandler(TEXT("set_root_component"),             MakeHandler(ComponentTools::HandleSetRootComponentFromArgs));
	D.RegisterHandler(TEXT("set_component_collision_profile"),MakeHandler(ComponentTools::HandleSetComponentCollisionProfileFromArgs));
	D.RegisterHandler(TEXT("set_component_collision_response"),MakeHandler(ComponentTools::HandleSetComponentCollisionResponseFromArgs));
	D.RegisterHandler(TEXT("set_component_collision_enabled"),MakeHandler(ComponentTools::HandleSetComponentCollisionEnabledFromArgs));
	D.RegisterHandler(TEXT("set_component_mobility"),         MakeHandler(ComponentTools::HandleSetComponentMobilityFromArgs));
	D.RegisterHandler(TEXT("set_skeletal_mesh_component"),    MakeHandler(ComponentTools::HandleSetSkeletalMeshComponentFromArgs));
	D.RegisterHandler(TEXT("set_static_mesh_component"),      MakeHandler(ComponentTools::HandleSetStaticMeshComponentFromArgs));
	D.RegisterHandler(TEXT("set_camera_component_properties"),MakeHandler(ComponentTools::HandleSetCameraComponentPropertiesFromArgs));
	D.RegisterHandler(TEXT("set_audio_component_properties"), MakeHandler(ComponentTools::HandleSetAudioComponentPropertiesFromArgs));
	D.RegisterHandler(TEXT("set_component_cast_shadows"),     MakeHandler(ComponentTools::HandleSetComponentCastShadowsFromArgs));
	D.RegisterHandler(TEXT("set_component_active"),           MakeHandler(ComponentTools::HandleSetComponentActiveFromArgs));
	D.RegisterHandler(TEXT("set_component_replication"),      MakeHandler(ComponentTools::HandleSetComponentReplicationFromArgs));
	D.RegisterHandler(TEXT("set_component_transform"),        MakeHandler(ComponentTools::HandleSetComponentTransformFromArgs));

	D.RegisterHandler(TEXT("add_gameplay_tag"),                  MakeHandler(GameplayTagTools::HandleAddGameplayTagFromArgs));
	D.RegisterHandler(TEXT("remove_gameplay_tag"),               MakeHandler(GameplayTagTools::HandleRemoveGameplayTagFromArgs));
	D.RegisterHandler(TEXT("get_gameplay_tags"),                 MakeHandler(GameplayTagTools::HandleGetGameplayTagsFromArgs));
	D.RegisterHandler(TEXT("assign_gameplay_tag_to_blueprint"),  MakeHandler(GameplayTagTools::HandleAssignGameplayTagToBlueprintFromArgs));
	D.RegisterHandler(TEXT("add_gameplay_tags_bulk"),            MakeHandler(GameplayTagTools::HandleAddGameplayTagsBulkFromArgs));
	D.RegisterHandler(TEXT("add_gameplay_tags"),                 MakeHandler(GameplayTagTools::HandleAddGameplayTagsBulkFromArgs));
	D.RegisterHandler(TEXT("find_referencers_by_tag"),           MakeHandler(GameplayTagTools::HandleFindReferencersByTagFromArgs));

	D.RegisterHandler(TEXT("add_dispatcher_param"),              MakeHandler(VariableTools::HandleAddDispatcherParamFromArgs));
	D.RegisterHandler(TEXT("add_event_dispatcher"),              MakeHandler(VariableTools::HandleAddEventDispatcherFromArgs));
	D.RegisterHandler(TEXT("add_function_param"),                MakeHandler(VariableTools::HandleAddFunctionParamFromArgs));
	D.RegisterHandler(TEXT("add_variable"),                      MakeHandler(VariableTools::HandleAddVariableFromArgs));
	D.RegisterHandler(TEXT("rename_variable"),                   MakeHandler(VariableTools::HandleRenameVariableFromArgs));
	D.RegisterHandler(TEXT("set_blueprint_variable_default"),    MakeHandler(VariableTools::HandleSetBlueprintVariableDefaultFromArgs));
	D.RegisterHandler(TEXT("set_variable_flags"),                MakeHandler(VariableTools::HandleSetVariableFlagsFromArgs));
	D.RegisterHandler(TEXT("set_variable_metadata"),             MakeHandler(VariableTools::HandleSetVariableMetadataFromArgs));
	D.RegisterHandler(TEXT("add_local_variable"),                MakeHandler(VariableTools::HandleAddLocalVariableFromArgs));
	D.RegisterHandler(TEXT("set_variable_expose_on_spawn"),      MakeHandler(VariableTools::HandleSetVariableExposeOnSpawnFromArgs));
	D.RegisterHandler(TEXT("set_variable_replication_condition"),MakeHandler(VariableTools::HandleSetVariableReplicationConditionFromArgs));
	D.RegisterHandler(TEXT("categorize_variables"),              MakeHandler(VariableTools::HandleCategorizeVariablesFromArgs));
	D.RegisterHandler(TEXT("delete_event_dispatcher"),           MakeHandler(VariableTools::HandleDeleteEventDispatcherFromArgs));
	D.RegisterHandler(TEXT("remove_dispatcher_param"),           MakeHandler(VariableTools::HandleRemoveDispatcherParamFromArgs));
	D.RegisterHandler(TEXT("set_function_replication"),          MakeHandler(VariableTools::HandleSetFunctionReplicationFromArgs));
	D.RegisterHandler(TEXT("remove_function_param"),             MakeHandler(VariableTools::HandleRemoveFunctionParamFromArgs));
	D.RegisterHandler(TEXT("set_function_access"),               MakeHandler(VariableTools::HandleSetFunctionAccessFromArgs));
	D.RegisterHandler(TEXT("set_function_pure"),                 MakeHandler(VariableTools::HandleSetFunctionPureFromArgs));
	D.RegisterHandler(TEXT("list_overridable_functions"),        MakeHandler(VariableTools::HandleListOverridableFunctionsFromArgs));
	D.RegisterHandler(TEXT("reparent_blueprint"),                MakeHandler(VariableTools::HandleReparentBlueprintFromArgs));

	D.RegisterHandler(TEXT("add_macro"),                         MakeHandler(BlueprintAssetTools::HandleAddMacroFromArgs));
	D.RegisterHandler(TEXT("delete_macro"),                      MakeHandler(BlueprintAssetTools::HandleDeleteMacroFromArgs));
	D.RegisterHandler(TEXT("add_macro_parameter"),               MakeHandler(BlueprintAssetTools::HandleAddMacroParameterFromArgs));
	D.RegisterHandler(TEXT("get_macro_summary"),                 MakeHandler(BlueprintAssetTools::HandleGetMacroSummaryFromArgs));
	D.RegisterHandler(TEXT("implement_blueprint_interface"),     MakeHandler(BlueprintAssetTools::HandleImplementBlueprintInterfaceFromArgs));
	D.RegisterHandler(TEXT("implement_interface"),               MakeHandler(BlueprintAssetTools::HandleImplementBlueprintInterfaceFromArgs));
	D.RegisterHandler(TEXT("add_interface"),                     MakeHandler(BlueprintAssetTools::HandleImplementBlueprintInterfaceFromArgs));
	D.RegisterHandler(TEXT("unimplement_blueprint_interface"),   MakeHandler(BlueprintAssetTools::HandleUnimplementBlueprintInterfaceFromArgs));
	D.RegisterHandler(TEXT("unimplement_interface"),             MakeHandler(BlueprintAssetTools::HandleUnimplementBlueprintInterfaceFromArgs));
	D.RegisterHandler(TEXT("remove_interface"),                  MakeHandler(BlueprintAssetTools::HandleUnimplementBlueprintInterfaceFromArgs));
	D.RegisterHandler(TEXT("rename_function"),                   MakeHandler(BlueprintAssetTools::HandleRenameFunctionFromArgs));

	{
		const FName U(TEXT("blueprint"));
		const auto Meta = [&D, &U](const TCHAR* Action, const TCHAR* Summary, const TCHAR* Params)
		{ D.RegisterToolMetadata({ FName(Action), U, FString(Summary), FString(Params) }); };

		Meta(TEXT("get_blueprint_skeleton"),  TEXT("Compact skeleton of a Blueprint (variables, functions, components, graphs) — cheap first read."), TEXT("blueprint_path"));
		Meta(TEXT("get_blueprint_graph"),     TEXT("Get a graph's nodes + connections (alias get_function_graph / get_graph)."), TEXT("blueprint_path, graph_name"));
		Meta(TEXT("get_function_graph"),      TEXT("Alias of get_blueprint_graph."), TEXT("blueprint_path, graph_name"));
		Meta(TEXT("get_graph"),               TEXT("Alias of get_blueprint_graph."), TEXT("blueprint_path, graph_name"));
		Meta(TEXT("get_blueprint_subgraph"),  TEXT("Get a collapsed/macro subgraph's nodes."), TEXT("blueprint_path, graph_name"));
		Meta(TEXT("discover_nodes"),          TEXT("Find node spawners/handles by intent for build_blueprint_graph (alias find_node)."), TEXT("query, blueprint_path?, max_results?"));
		Meta(TEXT("find_node"),               TEXT("Alias of discover_nodes."), TEXT("query, blueprint_path?"));
		Meta(TEXT("compare_blueprints"),      TEXT("Structural diff of two Blueprints: variables, components, interfaces, event dispatchers, functions/macros/graphs (per-node + per-link diff keyed by node id), CDO property values."), TEXT("blueprint_path_a, blueprint_path_b, include_cdo?, include_unchanged_graphs?"));
		Meta(TEXT("snapshot_blueprint"),      TEXT("Take an in-memory snapshot of a Blueprint BEFORE editing so diff_blueprint_since_snapshot can report exactly what a build changed."), TEXT("blueprint_path, label?"));
		Meta(TEXT("diff_blueprint_since_snapshot"), TEXT("Diff the live Blueprint against an earlier snapshot_blueprint (added/removed/changed nodes, links, variables, functions, CDO). Use after build_blueprint_graph to verify the result."), TEXT("blueprint_path, snapshot_id?, release_snapshot?, include_cdo?"));
		Meta(TEXT("set_cdo_property"),        TEXT("Set a class-default (CDO) property (alias set_class_default; batch: items=[{property_name,value}])."), TEXT("blueprint_path, property_name, value | items=[{property_name,value}]"));
		Meta(TEXT("set_class_default"),       TEXT("Alias of set_cdo_property."), TEXT("blueprint_path, property_name, value | items=[{property_name,value}]"));
		Meta(TEXT("get_cdo_properties"),      TEXT("List a class-default (CDO) object's editable properties + current values (alias get_class_defaults). Read this before set_cdo_property to get exact property names."), TEXT("blueprint_path, name_filter?"));
		Meta(TEXT("get_class_defaults"),      TEXT("Alias of get_cdo_properties."), TEXT("blueprint_path, name_filter?"));
		Meta(TEXT("delete_unused_variables"), TEXT("Delete variables not referenced by any graph."), TEXT("blueprint_path"));
		Meta(TEXT("delete_component"),        TEXT("Delete a component (batch: delete_components)."), TEXT("blueprint_path, component_name"));
		Meta(TEXT("delete_components"),       TEXT("Batch delete components."), TEXT("blueprint_path, items=[component_name]"));
		Meta(TEXT("delete_function"),         TEXT("Delete a function (batch: delete_functions)."), TEXT("blueprint_path, function_name"));
		Meta(TEXT("delete_functions"),        TEXT("Batch delete functions."), TEXT("blueprint_path, items=[function_name]"));
		Meta(TEXT("delete_variable"),         TEXT("Delete a variable (batch: delete_variables)."), TEXT("blueprint_path, variable_name"));
		Meta(TEXT("delete_variables"),        TEXT("Batch delete variables."), TEXT("blueprint_path, items=[variable_name]"));
		Meta(TEXT("delete_nodes"),            TEXT("Delete graph nodes by id/handle (alias delete_node)."), TEXT("blueprint_path, graph_name, node_ids"));
		Meta(TEXT("delete_node"),             TEXT("Alias of delete_nodes."), TEXT("blueprint_path, graph_name, node_id"));
		Meta(TEXT("build_blueprint_graph"),   TEXT("Declarative whole-graph builder (nodes[] + connections[]) with auto-repair. Prefer over many place_node/connect_pins calls."), TEXT("blueprint_path, graph_name, nodes=[{id,handle}], connections=[{from,from_pin,to,to_pin}], clear_before_build?"));
		Meta(TEXT("place_node"),              TEXT("Place a single node by handle."), TEXT("blueprint_path, graph_name, handle, position?"));
		Meta(TEXT("set_pin_default"),         TEXT("Set a node pin's default value."), TEXT("blueprint_path, graph_name, node_id, pin_name, value"));
		Meta(TEXT("connect_pins"),            TEXT("Connect two node pins."), TEXT("blueprint_path, graph_name, from_node, from_pin, to_node, to_pin"));
		Meta(TEXT("disconnect_pins"),         TEXT("Disconnect a node pin."), TEXT("blueprint_path, graph_name, node_id, pin_name"));
		Meta(TEXT("arrange_blueprint_nodes"), TEXT("Auto-arrange a graph's nodes."), TEXT("blueprint_path, graph_name"));
		Meta(TEXT("add_function"),            TEXT("Add a function (params=[{name,type,direction}]; batch via items)."), TEXT("blueprint_path, function_name, params?"));
		Meta(TEXT("override_function"),       TEXT("Override an inherited/interface function (creates its graph)."), TEXT("blueprint_path, function_name"));
		Meta(TEXT("add_timeline"),            TEXT("Add a Timeline node to a graph."), TEXT("blueprint_path, graph_name, timeline_name"));
		Meta(TEXT("clear_blueprint_graph"),   TEXT("Clear a graph's nodes (undoable via undo_last_clear)."), TEXT("blueprint_path, graph_name"));
		Meta(TEXT("undo_last_clear"),         TEXT("Restore the last cleared graph."), TEXT("blueprint_path"));
		Meta(TEXT("add_blueprint_comment"),   TEXT("Add a comment box (optionally around node ids)."), TEXT("blueprint_path, graph_name, text, nodes?"));
		Meta(TEXT("delete_blueprint_comment"),TEXT("Delete a comment box."), TEXT("blueprint_path, graph_name, comment_id"));
		Meta(TEXT("update_blueprint_comment"),TEXT("Edit a comment box's text/bounds."), TEXT("blueprint_path, graph_name, comment_id, text?"));
		Meta(TEXT("set_timeline_properties"), TEXT("Set a Timeline's loop / autoplay / length."), TEXT("blueprint_path, timeline_name, loop?, autoplay?, length?"));
		Meta(TEXT("add_timeline_track"),      TEXT("Add a float/vector/color/event track (with keys) to a Timeline."), TEXT("blueprint_path, timeline_name, track_type, track_name, keys?"));
		Meta(TEXT("add_variable"),            TEXT("Add a variable (batch via items; instance_editable/replication/etc. inline)."), TEXT("blueprint_path, name, type, default_value?, instance_editable?, replication?"));
		Meta(TEXT("rename_variable"),         TEXT("Rename a variable (updates references)."), TEXT("blueprint_path, old_name, new_name"));
		Meta(TEXT("set_blueprint_variable_default"), TEXT("Set a variable's default value."), TEXT("blueprint_path, variable_name, value"));
		Meta(TEXT("set_variable_flags"),      TEXT("Set variable flags (instance_editable, expose_on_spawn, save_game, replication, advanced_display, tooltip)."), TEXT("blueprint_path, variable_name, <flags>"));
		Meta(TEXT("set_variable_metadata"),   TEXT("Set a variable's metadata (tooltip, clamp min/max, units, etc.)."), TEXT("blueprint_path, variable_name, metadata"));
		Meta(TEXT("add_local_variable"),      TEXT("Add a local variable to a function graph."), TEXT("blueprint_path, function_name, name, type"));
		Meta(TEXT("set_variable_expose_on_spawn"), TEXT("Toggle a variable's ExposeOnSpawn."), TEXT("blueprint_path, variable_name, expose"));
		Meta(TEXT("set_variable_replication_condition"), TEXT("Set a variable's replication condition (e.g. OwnerOnly, SkipOwner)."), TEXT("blueprint_path, variable_name, condition"));
		Meta(TEXT("categorize_variables"),    TEXT("Set the Category on one or more variables."), TEXT("blueprint_path, category, variables"));
		Meta(TEXT("add_event_dispatcher"),    TEXT("Add an event dispatcher (multicast delegate)."), TEXT("blueprint_path, name"));
		Meta(TEXT("add_dispatcher_param"),    TEXT("Add a parameter to an event dispatcher's signature."), TEXT("blueprint_path, dispatcher_name, param_name, param_type"));
		Meta(TEXT("remove_dispatcher_param"), TEXT("Remove a parameter from an event dispatcher."), TEXT("blueprint_path, dispatcher_name, param_name"));
		Meta(TEXT("delete_event_dispatcher"), TEXT("Delete an event dispatcher."), TEXT("blueprint_path, name"));
		Meta(TEXT("add_function_param"),      TEXT("Add an input/output parameter to a function."), TEXT("blueprint_path, function_name, param_name, param_type, direction=input|output"));
		Meta(TEXT("remove_function_param"),   TEXT("Remove a function parameter."), TEXT("blueprint_path, function_name, param_name"));
		Meta(TEXT("set_function_replication"),TEXT("Set a function's replication (NotReplicated/Multicast/Server/Client)."), TEXT("blueprint_path, function_name, replication"));
		Meta(TEXT("set_function_access"),     TEXT("Set a function's access specifier (public/protected/private)."), TEXT("blueprint_path, function_name, access"));
		Meta(TEXT("set_function_pure"),       TEXT("Toggle a function's pure (const, no-exec-pins) flag."), TEXT("blueprint_path, function_name, pure"));
		Meta(TEXT("list_overridable_functions"), TEXT("List functions the Blueprint can override from its parent/interfaces."), TEXT("blueprint_path"));
		Meta(TEXT("reparent_blueprint"),      TEXT("Reparent a Blueprint to a new parent class."), TEXT("blueprint_path, new_parent_class"));
		Meta(TEXT("add_macro"),               TEXT("Add a macro to a Blueprint or macro library."), TEXT("blueprint_path, macro_name"));
		Meta(TEXT("delete_macro"),            TEXT("Delete a macro."), TEXT("blueprint_path, macro_name"));
		Meta(TEXT("add_macro_parameter"),     TEXT("Add an input/output to a macro."), TEXT("blueprint_path, macro_name, param_name, param_type, direction"));
		Meta(TEXT("get_macro_summary"),       TEXT("Summarise a macro's parameters/nodes."), TEXT("blueprint_path, macro_name"));
		Meta(TEXT("implement_blueprint_interface"), TEXT("Implement a Blueprint Interface (aliases implement_interface / add_interface)."), TEXT("blueprint_path, interface_path"));
		Meta(TEXT("implement_interface"),     TEXT("Alias of implement_blueprint_interface."), TEXT("blueprint_path, interface_path"));
		Meta(TEXT("add_interface"),           TEXT("Alias of implement_blueprint_interface."), TEXT("blueprint_path, interface_path"));
		Meta(TEXT("unimplement_blueprint_interface"), TEXT("Remove an implemented interface (aliases unimplement_interface / remove_interface)."), TEXT("blueprint_path, interface_path"));
		Meta(TEXT("unimplement_interface"),   TEXT("Alias of unimplement_blueprint_interface."), TEXT("blueprint_path, interface_path"));
		Meta(TEXT("remove_interface"),        TEXT("Alias of unimplement_blueprint_interface."), TEXT("blueprint_path, interface_path"));
		Meta(TEXT("rename_function"),         TEXT("Rename a function (updates call sites)."), TEXT("blueprint_path, old_name, new_name"));
	}

	{
		const FName U(TEXT("component"));
		const auto Meta = [&D, &U](const TCHAR* Action, const TCHAR* Summary, const TCHAR* Params)
		{ D.RegisterToolMetadata({ FName(Action), U, FString(Summary), FString(Params) }); };

		Meta(TEXT("add_component"),            TEXT("Add a component to a Blueprint (attach_to accepts a UPROPERTY name or instance FName)."), TEXT("blueprint_path, component_class, component_name, attach_to?"));
		Meta(TEXT("edit_component_property"),  TEXT("Set a component property (alias set_component_property; batch; accepts JSON object/array/number values for structs)."), TEXT("blueprint_path, component_name, property_name, value"));
		Meta(TEXT("set_component_property"),   TEXT("Alias of edit_component_property."), TEXT("blueprint_path, component_name, property_name, value"));
		Meta(TEXT("get_component_property"),   TEXT("Read a component's current property value (alias read_component_property; supports nested dot paths)."), TEXT("blueprint_path, component_name, property_name"));
		Meta(TEXT("set_character_anim_class"), TEXT("Set a Character/SkeletalMesh component's AnimClass."), TEXT("blueprint_path, anim_class_path, component_name?"));
		Meta(TEXT("remove_component"),         TEXT("Remove a component from a Blueprint."), TEXT("blueprint_path, component_name"));
		Meta(TEXT("rename_component"),         TEXT("Rename a component."), TEXT("blueprint_path, old_name, new_name"));
		Meta(TEXT("reparent_component"),       TEXT("Re-attach a component under a new parent (alias attach_component)."), TEXT("blueprint_path, component_name, new_parent"));
		Meta(TEXT("attach_component"),         TEXT("Alias of reparent_component."), TEXT("blueprint_path, component_name, new_parent"));
		Meta(TEXT("set_root_component"),       TEXT("Promote a scene component to the Blueprint's root (re-homes the old root; discards an auto DefaultSceneRoot). Scene components only."), TEXT("blueprint_path, component_name"));
		Meta(TEXT("set_component_collision_profile"),  TEXT("Set a component's collision profile."), TEXT("blueprint_path, component_name, profile_name"));
		Meta(TEXT("set_component_collision_response"), TEXT("Set a component's per-channel collision response."), TEXT("blueprint_path, component_name, channel, response"));
		Meta(TEXT("set_component_collision_enabled"),  TEXT("Set a component's collision-enabled mode (NoCollision/QueryOnly/PhysicsOnly/QueryAndPhysics)."), TEXT("blueprint_path, component_name, enabled_mode"));
		Meta(TEXT("set_component_mobility"),   TEXT("Set a component's mobility (Static/Stationary/Movable)."), TEXT("blueprint_path, component_name, mobility"));
		Meta(TEXT("set_skeletal_mesh_component"), TEXT("Set a SkeletalMeshComponent's mesh asset."), TEXT("blueprint_path, component_name, mesh_path"));
		Meta(TEXT("set_static_mesh_component"),   TEXT("Set a StaticMeshComponent's mesh asset."), TEXT("blueprint_path, component_name, mesh_path"));
		Meta(TEXT("set_camera_component_properties"), TEXT("Set a CameraComponent's FOV / projection / etc."), TEXT("blueprint_path, component_name, <properties>"));
		Meta(TEXT("set_audio_component_properties"),  TEXT("Set an AudioComponent's sound / volume / pitch / auto-activate."), TEXT("blueprint_path, component_name, <properties>"));
		Meta(TEXT("set_component_cast_shadows"),  TEXT("Toggle a primitive component's shadow casting."), TEXT("blueprint_path, component_name, cast_shadows"));
		Meta(TEXT("set_component_active"),     TEXT("Set a component's initial active / auto-activate state."), TEXT("blueprint_path, component_name, active"));
		Meta(TEXT("set_component_replication"),TEXT("Toggle a component's replication."), TEXT("blueprint_path, component_name, replicates"));
		Meta(TEXT("set_component_transform"),  TEXT("Set a component's relative transform."), TEXT("blueprint_path, component_name, location?, rotation?, scale?"));
	}

	{
		const FName U(TEXT("gameplay_tags"));
		const auto Meta = [&D, &U](const TCHAR* Action, const TCHAR* Summary, const TCHAR* Params)
		{ D.RegisterToolMetadata({ FName(Action), U, FString(Summary), FString(Params) }); };

		Meta(TEXT("add_gameplay_tag"),         TEXT("Add a gameplay tag to the project tag table."), TEXT("tag, comment?"));
		Meta(TEXT("add_gameplay_tags_bulk"),   TEXT("Add many gameplay tags in one call (alias add_gameplay_tags)."), TEXT("tags=[...]"));
		Meta(TEXT("add_gameplay_tags"),        TEXT("Alias of add_gameplay_tags_bulk."), TEXT("tags=[...]"));
		Meta(TEXT("remove_gameplay_tag"),      TEXT("Remove a gameplay tag from the project table."), TEXT("tag"));
		Meta(TEXT("get_gameplay_tags"),        TEXT("List the project's gameplay tags (optional filter)."), TEXT("filter?"));
		Meta(TEXT("assign_gameplay_tag_to_blueprint"), TEXT("Assign a tag to a Blueprint's tag-container property."), TEXT("blueprint_path, tag, property_name?"));
		Meta(TEXT("find_referencers_by_tag"),  TEXT("Find assets that reference a gameplay tag."), TEXT("tag"));
	}

	UE_LOG(LogUECPBlueprintExt, Log, TEXT("Registered %d Blueprint tools (blueprint + component + gameplay_tags umbrellas)"),
		OwnedToolNames().Num());
}

void FUECPBlueprintExtModule::ShutdownModule()
{
	BlueprintCompareTools::ReleaseAllSnapshots();
	if (!IUECPCoreModule::IsAvailable()) return;
	IUECPToolDispatcher& D = IUECPCoreModule::Get().GetToolDispatcher();
	for (const FName& N : OwnedToolNames()) D.UnregisterHandler(N);
}

IMPLEMENT_MODULE(FUECPBlueprintExtModule, UECPBlueprintExt)
