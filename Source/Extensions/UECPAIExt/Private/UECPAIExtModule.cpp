// Copyright 2026, BlueprintsLab, All rights reserved

#include "UECPAIExtModule.h"

#include "Tools/BehaviorTreeTools.h"
#include "Tools/EQSTools.h"
#include "Tools/NavMeshTools.h"
#include "Tools/StateTreeTools.h"

#include "UECPCoreModule.h"
#include "Services/IUECPToolDispatcher.h"
#include "Services/IUECPCreateAssetRegistry.h"

DEFINE_LOG_CATEGORY(LogUECPAIExt);

namespace
{
	static const TArray<FName>& OwnedToolNames()
	{
		static const TArray<FName> Names = {
			TEXT("add_bt_composite"),
			TEXT("add_bt_decorator"),
			TEXT("add_bt_service"),
			TEXT("add_bt_task_node"),
			TEXT("add_blackboard_key"),
			TEXT("remove_bt_node"),
			TEXT("remove_blackboard_key"),
			TEXT("set_bt_node_property"),
			TEXT("get_bt_node_property"),
			TEXT("get_blackboard_keys"),
			TEXT("get_blackboard_summary"),
			TEXT("remove_bt_decorator"),
			TEXT("remove_bt_service"),
			TEXT("reorder_bt_children"),
			TEXT("rename_blackboard_key"),
			TEXT("get_bt_node_details"),
			TEXT("list_bt_native_classes"),
			TEXT("move_bt_node"),
			TEXT("set_blackboard_asset"),
			TEXT("get_bt_graph_nodes"),
			TEXT("reparent_bt_node"),
			TEXT("set_blackboard_parent"),
			TEXT("auto_layout_bt"),
			TEXT("build_bt_tree"),
			TEXT("get_behavior_tree_summary"),

			TEXT("add_eqs_generator"),
			TEXT("add_eqs_test"),
			TEXT("get_eqs_query_summary"),
			TEXT("set_eqs_param"),
			TEXT("remove_eqs_test"),
			TEXT("list_eqs_generator_types"),
			TEXT("list_eqs_test_types"),
			TEXT("remove_eqs_generator"),
			TEXT("add_eqs_option"),
			TEXT("set_eqs_test_score"),

			TEXT("spawn_nav_mesh_bounds_volume"),
			TEXT("set_navmesh_config"),
			TEXT("rebuild_navmesh"),
			TEXT("get_navmesh_info"),
			TEXT("add_ai_perception_component"),
			TEXT("configure_ai_sight"),
			TEXT("configure_ai_hearing"),
			TEXT("add_nav_modifier_volume"),
			TEXT("add_nav_link_proxy"),
			TEXT("set_ai_controller_class"),
			TEXT("configure_ai_damage"),
			TEXT("add_smart_object_slot"),
			TEXT("get_smart_object_info"),

			TEXT("add_state_tree_state"),
			TEXT("add_state_tree_task"),
			TEXT("add_state_tree_transition"),
			TEXT("delete_state_tree_state"),
			TEXT("get_state_tree_summary"),
			TEXT("get_state_tree_schema"),
			TEXT("set_state_tree_task_property"),
			TEXT("add_state_tree_evaluator"),
			TEXT("add_state_tree_condition"),
			TEXT("get_state_tree_state_details"),
			TEXT("list_state_tree_state_details"),
			TEXT("list_state_tree_states"),
			TEXT("set_state_tree_state_type"),
			TEXT("list_state_tree_task_classes"),
			TEXT("delete_state_tree_task"),
			TEXT("delete_state_tree_evaluator"),
			TEXT("delete_state_tree_condition"),
			TEXT("set_state_tree_evaluator_property"),
			TEXT("set_state_tree_condition_property"),
			TEXT("compile_state_tree"),
			TEXT("rename_state_tree_state"),
			TEXT("add_state_tree_transition_condition"),
			TEXT("set_state_tree_linked_state"),
			TEXT("reorder_state_tree_tasks"),
			TEXT("get_state_tree_evaluators"),
			TEXT("delete_state_tree_transition"),
			TEXT("set_state_tree_transition"),
			TEXT("delete_state_tree_transition_condition"),
			TEXT("add_state_tree_parameter"),
			TEXT("get_state_tree_parameters"),
			TEXT("remove_state_tree_parameter"),
			TEXT("set_state_tree_parameter_default"),
			TEXT("get_state_tree_parameter_default"),
			TEXT("set_state_tree_schema"),
			TEXT("reorder_state_tree_evaluators"),
			TEXT("set_state_tree_state_selection_behavior"),
			TEXT("set_condition_operand"),
			TEXT("bind_state_tree_property"),
			TEXT("get_state_tree_bindings"),
			TEXT("set_state_tree_task_class"),
			TEXT("set_state_tree_component_asset"),
			TEXT("remove_state_tree_condition"),
			TEXT("remove_state_tree_evaluator"),
		};
		return Names;
	}

	static auto MakeHandler(TFunction<void(const TSharedPtr<FJsonObject>&, FString&, FString&)> Fn)
	{
		return [Fn = MoveTemp(Fn)](const TSharedPtr<FJsonObject>& Args) -> FUECPToolResult
		{
			FUECPToolResult R;
			Fn(Args, R.ResultJson, R.ErrorMessage);
			R.bSuccess = R.ErrorMessage.IsEmpty();
			return R;
		};
	}
}

void FUECPAIExtModule::StartupModule()
{
	if (!IUECPCoreModule::IsAvailable()) return;
	IUECPToolDispatcher& D = IUECPCoreModule::Get().GetToolDispatcher();

	D.RegisterHandler(TEXT("add_bt_composite"),       MakeHandler(BehaviorTreeTools::HandleAddBTCompositeFromArgs));
	D.RegisterHandler(TEXT("add_bt_decorator"),       MakeHandler(BehaviorTreeTools::HandleAddBTDecoratorFromArgs));
	D.RegisterHandler(TEXT("add_bt_service"),         MakeHandler(BehaviorTreeTools::HandleAddBTServiceFromArgs));
	D.RegisterHandler(TEXT("add_bt_task_node"),       MakeHandler(BehaviorTreeTools::HandleAddBTTaskNodeFromArgs));
	D.RegisterHandler(TEXT("add_blackboard_key"),     MakeHandler(BehaviorTreeTools::HandleAddBlackboardKeyFromArgs));
	D.RegisterHandler(TEXT("remove_bt_node"),         MakeHandler(BehaviorTreeTools::HandleRemoveBTNodeFromArgs));
	D.RegisterHandler(TEXT("remove_blackboard_key"),  MakeHandler(BehaviorTreeTools::HandleRemoveBlackboardKeyFromArgs));
	D.RegisterHandler(TEXT("set_bt_node_property"),   MakeHandler(BehaviorTreeTools::HandleSetBTNodePropertyFromArgs));
	D.RegisterHandler(TEXT("get_bt_node_property"),   MakeHandler(BehaviorTreeTools::HandleGetBTNodePropertyFromArgs));
	D.RegisterHandler(TEXT("get_blackboard_keys"),    MakeHandler(BehaviorTreeTools::HandleGetBlackboardKeysFromArgs));
	D.RegisterHandler(TEXT("get_blackboard_summary"), MakeHandler(BehaviorTreeTools::HandleGetBlackboardKeysFromArgs));
	D.RegisterHandler(TEXT("remove_bt_decorator"),    MakeHandler(BehaviorTreeTools::HandleRemoveBTDecoratorFromArgs));
	D.RegisterHandler(TEXT("remove_bt_service"),      MakeHandler(BehaviorTreeTools::HandleRemoveBTServiceFromArgs));
	D.RegisterHandler(TEXT("reorder_bt_children"),    MakeHandler(BehaviorTreeTools::HandleReorderBTChildrenFromArgs));
	D.RegisterHandler(TEXT("rename_blackboard_key"),  MakeHandler(BehaviorTreeTools::HandleRenameBlackboardKeyFromArgs));
	D.RegisterHandler(TEXT("get_bt_node_details"),    MakeHandler(BehaviorTreeTools::HandleGetBTNodeDetailsFromArgs));
	D.RegisterHandler(TEXT("list_bt_native_classes"), MakeHandler(BehaviorTreeTools::HandleListBTNativeClassesFromArgs));
	D.RegisterHandler(TEXT("move_bt_node"),           MakeHandler(BehaviorTreeTools::HandleMoveBTNodeFromArgs));
	D.RegisterHandler(TEXT("set_blackboard_asset"),   MakeHandler(BehaviorTreeTools::HandleSetBlackboardAssetFromArgs));
	D.RegisterHandler(TEXT("get_bt_graph_nodes"),     MakeHandler(BehaviorTreeTools::HandleGetBTGraphNodesFromArgs));
	D.RegisterHandler(TEXT("reparent_bt_node"),       MakeHandler(BehaviorTreeTools::HandleReparentBTNodeFromArgs));
	D.RegisterHandler(TEXT("set_blackboard_parent"),  MakeHandler(BehaviorTreeTools::HandleSetBlackboardParentFromArgs));
	D.RegisterHandler(TEXT("auto_layout_bt"),         MakeHandler(BehaviorTreeTools::HandleAutoLayoutBTFromArgs));
	D.RegisterHandler(TEXT("build_bt_tree"),          MakeHandler(BehaviorTreeTools::HandleBuildBTTreeFromArgs));
	D.RegisterHandler(TEXT("get_behavior_tree_summary"), MakeHandler(BehaviorTreeTools::HandleGetBehaviorTreeSummaryFromArgs));

	{
		const FName U(TEXT("behavior_tree"));
		const auto Meta = [&D, &U](const TCHAR* Action, const TCHAR* Summary, const TCHAR* Params)
		{ D.RegisterToolMetadata({ FName(Action), U, FString(Summary), FString(Params) }); };

		Meta(TEXT("add_blackboard_key"),      TEXT("Add a key to a Blackboard asset."), TEXT("blackboard_path, key_name, key_type, class_name? (for Object)"));
		Meta(TEXT("remove_blackboard_key"),   TEXT("Remove a key from a Blackboard."), TEXT("blackboard_path, key_name"));
		Meta(TEXT("rename_blackboard_key"),   TEXT("Rename a Blackboard key."), TEXT("blackboard_path, key_name, new_name"));
		Meta(TEXT("get_blackboard_keys"),     TEXT("List a Blackboard's keys (alias get_blackboard_summary)."), TEXT("blackboard_path (alias bb_path)"));
		Meta(TEXT("get_blackboard_summary"),  TEXT("Alias of get_blackboard_keys."), TEXT("blackboard_path"));
		Meta(TEXT("set_blackboard_asset"),    TEXT("Assign a Blackboard asset to a Behavior Tree."), TEXT("bt_path, blackboard_path"));
		Meta(TEXT("set_blackboard_parent"),   TEXT("Set a Blackboard's parent Blackboard (inheritance)."), TEXT("blackboard_path, parent_blackboard_path"));
		Meta(TEXT("add_bt_composite"),        TEXT("Add a composite node (Sequence/Selector/SimpleParallel) under a parent node."), TEXT("bt_path, composite_type, parent_node_name, position_x?, position_y?"));
		Meta(TEXT("add_bt_task_node"),        TEXT("Add a task node (native e.g. BTTask_MoveTo, or a Blueprint task) under a COMPOSITE parent."), TEXT("bt_path, task_class_path, parent_node_name, position_x?, position_y?"));
		Meta(TEXT("add_bt_service"),          TEXT("Attach a service to a composite/task node (added to SubNodes)."), TEXT("bt_path, node_name, service_class_path"));
		Meta(TEXT("add_bt_decorator"),        TEXT("Attach a decorator to a composite/task node (added to SubNodes). Set FlowAbortMode for reactive decorators."), TEXT("bt_path, node_name, decorator_class_path"));
		Meta(TEXT("set_bt_node_property"),    TEXT("Set a property on a node OR its attached decorator/service (plain name, no dot paths)."), TEXT("bt_path, node_name, property_name, property_value"));
		Meta(TEXT("get_bt_node_property"),    TEXT("Read a single node/decorator/service property."), TEXT("bt_path, node_name, property_name"));
		Meta(TEXT("get_bt_node_details"),     TEXT("Get a node's full details (property types + enum byte-index formats)."), TEXT("bt_path, node_name"));
		Meta(TEXT("get_bt_graph_nodes"),      TEXT("List every BT graph node (call FIRST on an existing tree to learn node_names)."), TEXT("bt_path"));
		Meta(TEXT("get_behavior_tree_summary"), TEXT("Summarise a Behavior Tree (structure + blackboard)."), TEXT("bt_path"));
		Meta(TEXT("list_bt_native_classes"),  TEXT("List native BT task/service/decorator classes."), TEXT("class_type=task|service|decorator|all"));
		Meta(TEXT("move_bt_node"),            TEXT("Reposition a BT node in the graph."), TEXT("bt_path, node_name, position_x, position_y"));
		Meta(TEXT("reparent_bt_node"),        TEXT("Move a node under a different composite parent."), TEXT("bt_path, node_name, new_parent_node_name"));
		Meta(TEXT("reorder_bt_children"),     TEXT("Reorder a composite's children (left-to-right execution priority)."), TEXT("bt_path, node_name, new_order"));
		Meta(TEXT("remove_bt_node"),          TEXT("Remove a composite/task node."), TEXT("bt_path, node_name"));
		Meta(TEXT("remove_bt_decorator"),     TEXT("Remove a decorator from a node (via SubNodes)."), TEXT("bt_path, node_name, decorator_index|class"));
		Meta(TEXT("remove_bt_service"),       TEXT("Remove a service from a node (via SubNodes)."), TEXT("bt_path, node_name, service_index|class"));
		Meta(TEXT("auto_layout_bt"),          TEXT("Clean top-down auto-layout of the whole tree."), TEXT("bt_path"));
		Meta(TEXT("build_bt_tree"),           TEXT("Declarative whole-tree builder — use instead of stringing together many add_* calls; returns name_map."), TEXT("bt_path, nodes=[{type,composite|task_class,name,parent,properties,decorators,services}], auto_layout?, clear_existing?"));
	}

	D.RegisterHandler(TEXT("add_eqs_generator"),       MakeHandler(EQSTools::HandleAddEQSGeneratorFromArgs));
	D.RegisterHandler(TEXT("add_eqs_test"),            MakeHandler(EQSTools::HandleAddEQSTestFromArgs));
	D.RegisterHandler(TEXT("get_eqs_query_summary"),   MakeHandler(EQSTools::HandleGetEQSQuerySummaryFromArgs));
	D.RegisterHandler(TEXT("set_eqs_param"),           MakeHandler(EQSTools::HandleSetEQSParamFromArgs));
	D.RegisterHandler(TEXT("remove_eqs_test"),         MakeHandler(EQSTools::HandleRemoveEQSTestFromArgs));
	D.RegisterHandler(TEXT("list_eqs_generator_types"),MakeHandler(EQSTools::HandleListEQSGeneratorTypesFromArgs));
	D.RegisterHandler(TEXT("list_eqs_test_types"),     MakeHandler(EQSTools::HandleListEQSTestTypesFromArgs));
	D.RegisterHandler(TEXT("remove_eqs_generator"),    MakeHandler(EQSTools::HandleRemoveEQSGeneratorFromArgs));
	D.RegisterHandler(TEXT("add_eqs_option"),          MakeHandler(EQSTools::HandleAddEQSOptionFromArgs));
	D.RegisterHandler(TEXT("set_eqs_test_score"),      MakeHandler(EQSTools::HandleSetEQSTestScoreFromArgs));

	{
		const FName U(TEXT("eqs"));
		const auto Meta = [&D, &U](const TCHAR* Action, const TCHAR* Summary, const TCHAR* Params)
		{ D.RegisterToolMetadata({ FName(Action), U, FString(Summary), FString(Params) }); };

		Meta(TEXT("add_eqs_generator"),        TEXT("Add a generator to an EQS query (defines the candidate item set)."), TEXT("query_path, generator_type, context_class?, radius?, grid_spacing?"));
		Meta(TEXT("add_eqs_test"),             TEXT("Add a test to an EQS query (filters/scores the generated items). test_type matches any EnvQueryTest subclass (Distance, Trace, Dot, ...). filter_mode sets the test FilterType (Minimum|Maximum|Range|Match); response reports filter_*_applied per param."), TEXT("query_path, test_type, filter_mode?(Minimum|Maximum|Range|Match), filter_min?, filter_max?"));
		Meta(TEXT("add_eqs_option"),           TEXT("Add an option (a generator + its own test set) to an EQS query."), TEXT("query_path, generator_type"));
		Meta(TEXT("set_eqs_param"),            TEXT("Set a property on an EQS generator / test / option."), TEXT("query_path, target, property_name, property_value"));
		Meta(TEXT("set_eqs_test_score"),       TEXT("Set a test's scoring equation, factor, and filter type."), TEXT("query_path, option_index?, test_index, scoring_equation, scoring_factor, filter_type"));
		Meta(TEXT("remove_eqs_test"),          TEXT("Remove a test from an EQS query."), TEXT("query_path, option_index?, test_index"));
		Meta(TEXT("remove_eqs_generator"),     TEXT("Remove a generator from an EQS query."), TEXT("query_path, generator_index"));
		Meta(TEXT("get_eqs_query_summary"),    TEXT("Summarise an EQS query (generators, tests, options)."), TEXT("query_path"));
		Meta(TEXT("list_eqs_generator_types"), TEXT("List available EQS generator types."), TEXT(""));
		Meta(TEXT("list_eqs_test_types"),      TEXT("List available EQS test types."), TEXT(""));
	}

	D.RegisterHandler(TEXT("spawn_nav_mesh_bounds_volume"), MakeHandler(NavMeshTools::HandleSpawnNavMeshBoundsVolumeFromArgs));
	D.RegisterHandler(TEXT("set_navmesh_config"),           MakeHandler(NavMeshTools::HandleSetNavMeshConfigFromArgs));
	D.RegisterHandler(TEXT("rebuild_navmesh"),              MakeHandler(NavMeshTools::HandleRebuildNavMeshFromArgs));
	D.RegisterHandler(TEXT("get_navmesh_info"),             MakeHandler(NavMeshTools::HandleGetNavMeshInfoFromArgs));
	D.RegisterHandler(TEXT("add_ai_perception_component"),  MakeHandler(NavMeshTools::HandleAddAIPerceptionComponentFromArgs));
	D.RegisterHandler(TEXT("configure_ai_sight"),           MakeHandler(NavMeshTools::HandleConfigureAISightFromArgs));
	D.RegisterHandler(TEXT("configure_ai_hearing"),         MakeHandler(NavMeshTools::HandleConfigureAIHearingFromArgs));
	D.RegisterHandler(TEXT("add_nav_modifier_volume"),      MakeHandler(NavMeshTools::HandleAddNavModifierVolumeFromArgs));
	D.RegisterHandler(TEXT("add_nav_link_proxy"),           MakeHandler(NavMeshTools::HandleAddNavLinkProxyFromArgs));
	D.RegisterHandler(TEXT("set_ai_controller_class"),      MakeHandler(NavMeshTools::HandleSetAIControllerClassFromArgs));
	D.RegisterHandler(TEXT("configure_ai_damage"),          MakeHandler(NavMeshTools::HandleConfigureAIDamageFromArgs));
	D.RegisterHandler(TEXT("add_smart_object_slot"),        MakeHandler(NavMeshTools::HandleAddSmartObjectSlotFromArgs));
	D.RegisterHandler(TEXT("get_smart_object_info"),        MakeHandler(NavMeshTools::HandleGetSmartObjectInfoFromArgs));

	{
		const FName U(TEXT("navmesh_ai"));
		const auto Meta = [&D, &U](const TCHAR* Action, const TCHAR* Summary, const TCHAR* Params)
		{ D.RegisterToolMetadata({ FName(Action), U, FString(Summary), FString(Params) }); };

		Meta(TEXT("spawn_nav_mesh_bounds_volume"), TEXT("Spawn a NavMeshBoundsVolume at a location/extent so the level has navigation."), TEXT("location_x/y/z, extent_x/y/z=1000"));
		Meta(TEXT("set_navmesh_config"),       TEXT("Set the RecastNavMesh agent/cell config (pass -1 to leave a field unchanged)."), TEXT("agent_radius, agent_height, max_step_height, cell_size"));
		Meta(TEXT("rebuild_navmesh"),          TEXT("Rebuild the level's navigation."), TEXT(""));
		Meta(TEXT("get_navmesh_info"),         TEXT("Report current navmesh/build state."), TEXT(""));
		Meta(TEXT("add_ai_perception_component"), TEXT("Add an AIPerception component to a Blueprint."), TEXT("blueprint_path, component_name=AIPerception"));
		Meta(TEXT("configure_ai_sight"),       TEXT("Configure an AIPerception sight sense."), TEXT("blueprint_path, component_name, sight_radius, lose_sight_radius, peripheral_angle, max_age, detection_by_affiliation_detect_enemies"));
		Meta(TEXT("configure_ai_hearing"),     TEXT("Configure an AIPerception hearing sense."), TEXT("blueprint_path, component_name, hearing_range, lo_s_hearing_range"));
		Meta(TEXT("configure_ai_damage"),      TEXT("Configure an AIPerception damage sense affiliation flags."), TEXT("blueprint_path, detect_enemies, detect_neutrals, detect_friendlies"));
		Meta(TEXT("add_nav_modifier_volume"),  TEXT("Spawn a NavModifierVolume (mark an area with a nav area class)."), TEXT("actor_label, location_x/y/z, extent_x/y/z, area_class"));
		Meta(TEXT("add_nav_link_proxy"),       TEXT("Spawn a NavLinkProxy (jump/teleport link between two points)."), TEXT("actor_label, location_x/y/z, end_offset_x/y/z"));
		Meta(TEXT("set_ai_controller_class"),  TEXT("Set a pawn Blueprint's AIControllerClass."), TEXT("blueprint_path, ai_controller_class"));
		Meta(TEXT("add_smart_object_slot"),    TEXT("Add a slot (with an activity tag) to a SmartObjectDefinition."), TEXT("asset_path, activity_tag"));
		Meta(TEXT("get_smart_object_info"),    TEXT("Summarise a SmartObjectDefinition's slots."), TEXT("asset_path"));
	}

	D.RegisterHandler(TEXT("add_state_tree_state"),                  MakeHandler(StateTreeTools::HandleAddStateTreeStateFromArgs));
	D.RegisterHandler(TEXT("add_state_tree_task"),                   MakeHandler(StateTreeTools::HandleAddStateTreeTaskFromArgs));
	D.RegisterHandler(TEXT("add_state_tree_transition"),             MakeHandler(StateTreeTools::HandleAddStateTreeTransitionFromArgs));
	D.RegisterHandler(TEXT("delete_state_tree_state"),               MakeHandler(StateTreeTools::HandleDeleteStateTreeStateFromArgs));
	D.RegisterHandler(TEXT("get_state_tree_summary"),                MakeHandler(StateTreeTools::HandleGetStateTreeSummaryFromArgs));
	D.RegisterHandler(TEXT("get_state_tree_schema"),                 MakeHandler(StateTreeTools::HandleGetStateTreeSchemaFromArgs));
	D.RegisterHandler(TEXT("set_state_tree_task_property"),          MakeHandler(StateTreeTools::HandleSetStateTreeTaskPropertyFromArgs));
	D.RegisterHandler(TEXT("add_state_tree_evaluator"),              MakeHandler(StateTreeTools::HandleAddStateTreeEvaluatorFromArgs));
	D.RegisterHandler(TEXT("add_state_tree_condition"),              MakeHandler(StateTreeTools::HandleAddStateTreeConditionFromArgs));
	D.RegisterHandler(TEXT("get_state_tree_state_details"),          MakeHandler(StateTreeTools::HandleGetStateTreeStateDetailsFromArgs));
	D.RegisterHandler(TEXT("list_state_tree_state_details"),         MakeHandler(StateTreeTools::HandleGetStateTreeStateDetailsFromArgs));
	D.RegisterHandler(TEXT("list_state_tree_states"),                MakeHandler(StateTreeTools::HandleGetStateTreeStateDetailsFromArgs));
	D.RegisterHandler(TEXT("set_state_tree_state_type"),             MakeHandler(StateTreeTools::HandleSetStateTreeStateTypeFromArgs));
	D.RegisterHandler(TEXT("list_state_tree_task_classes"),          MakeHandler(StateTreeTools::HandleListStateTreeTaskClassesFromArgs));
	D.RegisterHandler(TEXT("delete_state_tree_task"),                MakeHandler(StateTreeTools::HandleDeleteStateTreeTaskFromArgs));
	D.RegisterHandler(TEXT("delete_state_tree_evaluator"),           MakeHandler(StateTreeTools::HandleDeleteStateTreeEvaluatorFromArgs));
	D.RegisterHandler(TEXT("delete_state_tree_condition"),           MakeHandler(StateTreeTools::HandleDeleteStateTreeConditionFromArgs));
	D.RegisterHandler(TEXT("set_state_tree_evaluator_property"),     MakeHandler(StateTreeTools::HandleSetStateTreeEvaluatorPropertyFromArgs));
	D.RegisterHandler(TEXT("set_state_tree_condition_property"),     MakeHandler(StateTreeTools::HandleSetStateTreeConditionPropertyFromArgs));
	D.RegisterHandler(TEXT("compile_state_tree"),                    MakeHandler(StateTreeTools::HandleCompileStateTreeFromArgs));
	D.RegisterHandler(TEXT("rename_state_tree_state"),               MakeHandler(StateTreeTools::HandleRenameStateTreeStateFromArgs));
	D.RegisterHandler(TEXT("add_state_tree_transition_condition"),   MakeHandler(StateTreeTools::HandleAddStateTreeTransitionConditionFromArgs));
	D.RegisterHandler(TEXT("set_state_tree_linked_state"),           MakeHandler(StateTreeTools::HandleSetStateTreeLinkedStateFromArgs));
	D.RegisterHandler(TEXT("reorder_state_tree_tasks"),              MakeHandler(StateTreeTools::HandleReorderStateTreeTasksFromArgs));
	D.RegisterHandler(TEXT("get_state_tree_evaluators"),             MakeHandler(StateTreeTools::HandleGetStateTreeEvaluatorsFromArgs));
	D.RegisterHandler(TEXT("delete_state_tree_transition"),          MakeHandler(StateTreeTools::HandleDeleteStateTreeTransitionFromArgs));
	D.RegisterHandler(TEXT("set_state_tree_transition"),             MakeHandler(StateTreeTools::HandleSetStateTreeTransitionFromArgs));
	D.RegisterHandler(TEXT("delete_state_tree_transition_condition"),MakeHandler(StateTreeTools::HandleDeleteStateTreeTransitionConditionFromArgs));
	D.RegisterHandler(TEXT("add_state_tree_parameter"),              MakeHandler(StateTreeTools::HandleAddStateTreeParameterFromArgs));
	D.RegisterHandler(TEXT("get_state_tree_parameters"),             MakeHandler(StateTreeTools::HandleGetStateTreeParametersFromArgs));
	D.RegisterHandler(TEXT("remove_state_tree_parameter"),           MakeHandler(StateTreeTools::HandleRemoveStateTreeParameterFromArgs));
	D.RegisterHandler(TEXT("set_state_tree_parameter_default"),      MakeHandler(StateTreeTools::HandleSetStateTreeParameterDefaultFromArgs));
	D.RegisterHandler(TEXT("get_state_tree_parameter_default"),      MakeHandler(StateTreeTools::HandleGetStateTreeParameterDefaultFromArgs));
	D.RegisterHandler(TEXT("set_state_tree_schema"),                 MakeHandler(StateTreeTools::HandleSetStateTreeSchemaFromArgs));
	D.RegisterHandler(TEXT("reorder_state_tree_evaluators"),         MakeHandler(StateTreeTools::HandleReorderStateTreeEvaluatorsFromArgs));
	D.RegisterHandler(TEXT("set_state_tree_state_selection_behavior"),MakeHandler(StateTreeTools::HandleSetStateTreeStateSelectionBehaviorFromArgs));
	D.RegisterHandler(TEXT("set_condition_operand"),                 MakeHandler(StateTreeTools::HandleSetConditionOperandFromArgs));
	D.RegisterHandler(TEXT("bind_state_tree_property"),              MakeHandler(StateTreeTools::HandleBindStateTreePropertyFromArgs));
	D.RegisterHandler(TEXT("get_state_tree_bindings"),               MakeHandler(StateTreeTools::HandleGetStateTreeBindingsFromArgs));
	D.RegisterHandler(TEXT("set_state_tree_task_class"),             MakeHandler(StateTreeTools::HandleSetStateTreeTaskClassFromArgs));
	D.RegisterHandler(TEXT("set_state_tree_component_asset"),        MakeHandler(StateTreeTools::HandleSetStateTreeComponentAssetFromArgs));
	D.RegisterHandler(TEXT("remove_state_tree_condition"),           MakeHandler(StateTreeTools::HandleRemoveStateTreeConditionFromArgs));
	D.RegisterHandler(TEXT("remove_state_tree_evaluator"),           MakeHandler(StateTreeTools::HandleRemoveStateTreeEvaluatorFromArgs));

	{
		const FName U(TEXT("state_tree"));
		const auto Meta = [&D, &U](const TCHAR* Action, const TCHAR* Summary, const TCHAR* Params)
		{ D.RegisterToolMetadata({ FName(Action), U, FString(Summary), FString(Params) }); };

		Meta(TEXT("add_state_tree_state"),        TEXT("Add a state (empty parent = root)."), TEXT("asset_path, state_name, parent_state_name?"));
		Meta(TEXT("delete_state_tree_state"),     TEXT("Delete a state."), TEXT("asset_path, state_name"));
		Meta(TEXT("rename_state_tree_state"),     TEXT("Rename a state."), TEXT("asset_path, state_name, new_name"));
		Meta(TEXT("set_state_tree_state_type"),   TEXT("Set a state's type (e.g. State, Group, Linked, Subtree)."), TEXT("asset_path, state_name, state_type"));
		Meta(TEXT("set_state_tree_linked_state"), TEXT("Point a Linked-type state at the state it should run when entered."), TEXT("asset_path, state_name, to_state"));
		Meta(TEXT("set_state_tree_state_selection_behavior"), TEXT("Set how a state selects among children (TryEnterState / TrySelectChildrenInOrder / ...)."), TEXT("asset_path, state_name, behavior"));
		Meta(TEXT("add_state_tree_task"),         TEXT("Add a task to a state (task_class is a UScriptStruct name, NOT a UClass)."), TEXT("asset_path, state_name, task_class, task_label"));
		Meta(TEXT("set_state_tree_task_property"),TEXT("Set a property on a state's task."), TEXT("asset_path, state_name, task_label, property_name, property_value"));
		Meta(TEXT("set_state_tree_task_class"),   TEXT("Set/replace a state task's class (struct or Blueprint wrapper)."), TEXT("asset_path, state_name, task_class, blueprint_class"));
		Meta(TEXT("delete_state_tree_task"),      TEXT("Delete a task from a state."), TEXT("asset_path, state_name, task_label"));
		Meta(TEXT("reorder_state_tree_tasks"),    TEXT("Reorder a state's tasks (execution order)."), TEXT("asset_path, state_name, new_order"));
		Meta(TEXT("list_state_tree_task_classes"),TEXT("List available StateTree task classes (CALL FIRST)."), TEXT(""));
		Meta(TEXT("add_state_tree_evaluator"),    TEXT("Add a tree-level evaluator."), TEXT("asset_path, evaluator_class, evaluator_label"));
		Meta(TEXT("set_state_tree_evaluator_property"), TEXT("Set a property on an evaluator."), TEXT("asset_path, evaluator_class, property_name, property_value"));
		Meta(TEXT("delete_state_tree_evaluator"), TEXT("Delete an evaluator by class."), TEXT("asset_path, evaluator_class"));
		Meta(TEXT("remove_state_tree_evaluator"), TEXT("Delete an evaluator by 0-based index."), TEXT("asset_path, evaluator_index"));
		Meta(TEXT("reorder_state_tree_evaluators"), TEXT("Reorder the tree's evaluators."), TEXT("asset_path, new_order"));
		Meta(TEXT("get_state_tree_evaluators"),   TEXT("List the tree's evaluators."), TEXT("asset_path"));
		Meta(TEXT("add_state_tree_condition"),    TEXT("Add an ENTER condition to a state."), TEXT("asset_path, state_name, condition_class, condition_label"));
		Meta(TEXT("set_state_tree_condition_property"), TEXT("Set a condition property (pass trigger_type+to_state for a TRANSITION condition)."), TEXT("asset_path, state_name, condition_class, property_name, property_value"));
		Meta(TEXT("set_condition_operand"),       TEXT("Set the boolean operand (And/Or/Copy) chaining a state's conditions."), TEXT("asset_path, state_name, condition_class, operand, trigger_type?, to_state?"));
		Meta(TEXT("delete_state_tree_condition"), TEXT("Delete an ENTER condition by class."), TEXT("asset_path, state_name, condition_class"));
		Meta(TEXT("remove_state_tree_condition"), TEXT("Delete an ENTER condition by 0-based index."), TEXT("asset_path, state_name, condition_index"));
		Meta(TEXT("add_state_tree_transition"),   TEXT("Add a transition (trigger_type=OnTick|OnStateCompleted|OnStateSucceeded|OnStateFailed; NOT OnEvent)."), TEXT("asset_path, from_state, to_state, trigger_type"));
		Meta(TEXT("set_state_tree_transition"),   TEXT("Reassign an existing transition's trigger/target without rebuilding its conditions."), TEXT("asset_path, from_state, trigger_type, to_state, new_trigger?, new_to_state?"));
		Meta(TEXT("delete_state_tree_transition"),TEXT("Delete a transition."), TEXT("asset_path, from_state, trigger_type, to_state"));
		Meta(TEXT("add_state_tree_transition_condition"), TEXT("Add a condition to a transition's chain."), TEXT("asset_path, state_name, trigger_type, to_state, condition_class"));
		Meta(TEXT("delete_state_tree_transition_condition"), TEXT("Remove one condition from a transition's chain (keeps the transition)."), TEXT("asset_path, from_state, trigger_type, to_state, condition_class"));
		Meta(TEXT("add_state_tree_parameter"),    TEXT("Add a parameter slot (rich type catalogue; optional inline default via the matching *_value field)."), TEXT("asset_path, param_name, param_type, *_value?"));
		Meta(TEXT("set_state_tree_parameter_default"), TEXT("Set a parameter's default value (typed *_value field, or string_value ImportText fallback for structs)."), TEXT("asset_path, param_name, *_value"));
		Meta(TEXT("get_state_tree_parameter_default"), TEXT("Get a parameter's current value as an ExportText string."), TEXT("asset_path, param_name"));
		Meta(TEXT("get_state_tree_parameters"),   TEXT("List the tree's parameters (names + types)."), TEXT("asset_path"));
		Meta(TEXT("remove_state_tree_parameter"), TEXT("Remove a parameter slot (stale bindings need re-binding)."), TEXT("asset_path, param_name"));
		Meta(TEXT("bind_state_tree_property"),    TEXT("Bind a task/condition/evaluator property to a source (parameters.X / evaluators.0.Prop)."), TEXT("asset_path, state_name, node_class, node_type=task|condition|evaluator, property_name, source"));
		Meta(TEXT("get_state_tree_bindings"),     TEXT("List the tree's property bindings."), TEXT("asset_path"));
		Meta(TEXT("set_state_tree_schema"),       TEXT("Set the StateTree schema class."), TEXT("asset_path, schema_class"));
		Meta(TEXT("get_state_tree_schema"),       TEXT("Get the StateTree's schema."), TEXT("asset_path"));
		Meta(TEXT("set_state_tree_component_asset"), TEXT("Assign a StateTree asset to a StateTreeAIComponent on a Blueprint (REQUIRED after adding the component)."), TEXT("blueprint_path, component_name, state_tree_path"));
		Meta(TEXT("compile_state_tree"),          TEXT("Compile the StateTree asset."), TEXT("asset_path"));
		Meta(TEXT("get_state_tree_summary"),      TEXT("Summarise a StateTree (states, tasks, transitions)."), TEXT("asset_path"));
		Meta(TEXT("get_state_tree_state_details"),TEXT("Get a state's details (tasks, conditions, transitions)."), TEXT("asset_path, state_name?"));
		Meta(TEXT("list_state_tree_state_details"), TEXT("List every state's details."), TEXT("asset_path"));
		Meta(TEXT("list_state_tree_states"),      TEXT("List the tree's states."), TEXT("asset_path"));
	}

	{
		IUECPCreateAssetRegistry& Reg = IUECPCoreModule::Get().GetCreateAssetRegistry();
		const FName ExtId(TEXT("AI"));
		Reg.RegisterType(TEXT("BehaviorTree"),           UECPCreateAsset::FactoryFromArgsFn(&BehaviorTreeTools::HandleCreateBehaviorTreeFromArgs, TEXT("BehaviorTree")),           ExtId);
		Reg.RegisterType(TEXT("Blackboard"),             UECPCreateAsset::FactoryFromArgsFn(&BehaviorTreeTools::HandleCreateBlackboardFromArgs,   TEXT("Blackboard")),             ExtId);
		Reg.RegisterType(TEXT("EQSQuery"),               UECPCreateAsset::FactoryFromArgsFn(&EQSTools::HandleCreateEQSQueryFromArgs,              TEXT("EQSQuery")),               ExtId);
		Reg.RegisterType(TEXT("StateTree"),              UECPCreateAsset::FactoryFromArgsFn(&StateTreeTools::HandleCreateStateTreeFromArgs,       TEXT("StateTree")),              ExtId);
		Reg.RegisterType(TEXT("SmartObjectDefinition"),  UECPCreateAsset::FactoryFromArgsFn(&NavMeshTools::HandleCreateSmartObjectDefinitionFromArgs, TEXT("SmartObjectDefinition")), ExtId);
	}

	UE_LOG(LogUECPAIExt, Log, TEXT("Registered %d AI tools (behavior_tree + eqs + navmesh_ai + state_tree umbrellas)"),
		OwnedToolNames().Num());
}

void FUECPAIExtModule::ShutdownModule()
{
	if (!IUECPCoreModule::IsAvailable()) return;
	IUECPToolDispatcher& D = IUECPCoreModule::Get().GetToolDispatcher();
	for (const FName& N : OwnedToolNames()) D.UnregisterHandler(N);
	IUECPCreateAssetRegistry& Reg = IUECPCoreModule::Get().GetCreateAssetRegistry();
	for (const TCHAR* T : { TEXT("BehaviorTree"), TEXT("Blackboard"), TEXT("EQSQuery"),
	                        TEXT("StateTree"), TEXT("SmartObjectDefinition") })
	{
		Reg.UnregisterType(T);
	}
}

IMPLEMENT_MODULE(FUECPAIExtModule, UECPAIExt)
