// Copyright 2026, BlueprintsLab, All rights reserved

#include "UECPCinematicsExtModule.h"

#include "Tools/SequencerTools.h"
#include "Tools/SequencerOutlinerTools.h"
#include "Tools/SequencerBindingTools.h"
#include "Tools/SequencerWorkflowTools.h"
#include "Tools/ControlRigTools.h"
#include "Tools/ControlRigAnimLayerTools.h"

#include "UECPCoreModule.h"
#include "Services/IUECPToolDispatcher.h"
#include "Services/IUECPCreateAssetRegistry.h"

DEFINE_LOG_CATEGORY(LogUECPCinematicsExt);

namespace
{
	static const TArray<FName>& OwnedToolNames()
	{
		static const TArray<FName> Names = {
			TEXT("get_sequence_summary"),
			TEXT("get_sequence_details"),
			TEXT("get_sequence_full_data"),
			TEXT("get_sequence_bindings"),
			TEXT("get_sequence_keyframes"),
			TEXT("list_sequences"),
			TEXT("add_sequence_actor_binding"),
			TEXT("add_sequence_spawnable"),
			TEXT("remove_sequence_binding"),
			TEXT("add_sequence_transform_track"),
			TEXT("add_sequence_camera_cut_track"),
			TEXT("add_sequence_camera_track"),
			TEXT("add_sequence_audio_track"),
			TEXT("add_sequence_event_track"),
			TEXT("add_sequence_visibility_track"),
			TEXT("add_sequence_skeletal_animation_track"),
			TEXT("add_sequence_material_track"),
			TEXT("add_sequence_fade_track"),
			TEXT("add_sequence_color_track"),
			TEXT("add_sequence_float_track"),
			TEXT("add_sequence_bool_track"),
			TEXT("add_sequence_integer_track"),
			TEXT("remove_sequence_track"),
			TEXT("add_sequence_keyframe"),
			TEXT("add_float_keyframe"),
			TEXT("add_color_keyframe"),
			TEXT("add_visibility_keyframe"),
			TEXT("add_bool_keyframe"),
			TEXT("add_integer_keyframe"),
			TEXT("remove_sequence_keyframe"),
			TEXT("set_keyframe_interpolation"),
			TEXT("set_sequence_section_range"),
			TEXT("set_sequence_playback_settings"),
			TEXT("set_sequence_display_rate"),
			TEXT("set_section_blend_type"),
			TEXT("set_section_completion_mode"),
			TEXT("move_sequencer_section"),
			TEXT("resize_sequencer_section"),
			TEXT("split_sequencer_section"),
			TEXT("remove_sequencer_section"),
			TEXT("move_sequencer_keyframe"),
			TEXT("set_sequencer_keyframe_value"),
			TEXT("rename_sequencer_track"),
			TEXT("set_sequencer_track_eval_disabled"),
			TEXT("set_sequencer_track_sort_order"),
			TEXT("rename_sequencer_binding"),
			TEXT("add_sequence_camera_shake"),
			TEXT("add_sequence_sub_sequence"),
			TEXT("list_subsequences"),
			TEXT("remove_subsequence"),
			TEXT("set_subsequence_params"),
			TEXT("add_level_visibility_section"),
			TEXT("spawn_camera_rig_crane"),
			TEXT("spawn_camera_rig_rail"),
			TEXT("attach_camera_to_rig"),
			TEXT("set_rig_rail_position"),
			TEXT("create_mrq_job"),
			TEXT("delete_mrq_job"),
			TEXT("set_mrq_output_settings"),
			TEXT("add_mrq_render_pass"),
			TEXT("list_mrq_render_passes"),
			TEXT("execute_mrq_render"),
			TEXT("get_mrq_render_status"),
			TEXT("cancel_mrq_render"),
			TEXT("get_mrq_queue_summary"),
			TEXT("clear_mrq_queue"),
			TEXT("get_rig_summary"),
			TEXT("add_control_rig_track"),
			TEXT("list_control_rig_controls"),
			TEXT("add_control_rig_section"),
			TEXT("set_control_rig_keyframe"),
			TEXT("add_marked_frame"),
			TEXT("delete_marked_frame"),
			TEXT("delete_all_marked_frames"),
			TEXT("find_marked_frame_by_label"),
			TEXT("get_marked_frames"),
			TEXT("set_marked_frames_locked"),
			TEXT("set_node_muted"),
			TEXT("set_node_solo"),
			TEXT("set_node_pinned"),
			TEXT("get_outliner_state"),
			TEXT("set_section_locked"),
			TEXT("set_playback_range_locked"),
			TEXT("focus_sub_sequence"),
			TEXT("focus_parent_sequence"),
			TEXT("get_sub_sequence_hierarchy"),
			TEXT("tag_binding"),
			TEXT("untag_binding"),
			TEXT("find_binding_by_tag"),
			TEXT("find_bindings_by_tag"),
			TEXT("get_all_binding_tags"),
			TEXT("get_binding_tags"),
			TEXT("remove_binding_tag"),
			TEXT("fix_actor_references"),
			TEXT("rebind_component"),
			TEXT("remove_invalid_bindings"),
			TEXT("replace_binding_with_actors"),
			TEXT("add_actors_to_binding"),
			TEXT("remove_actors_from_binding"),
			TEXT("remove_all_bindings"),
			TEXT("convert_to_spawnable"),
			TEXT("convert_to_possessable"),
			TEXT("change_actor_template_class"),
			TEXT("save_default_spawnable_state"),
			TEXT("get_custom_binding_type"),
			TEXT("copy_bindings"),
			TEXT("paste_bindings"),
			TEXT("copy_tracks"),
			TEXT("paste_tracks"),
			TEXT("copy_sections"),
			TEXT("paste_sections"),
			TEXT("add_event_trigger_section"),
			TEXT("add_event_repeater_section"),
			TEXT("set_section_condition"),
			TEXT("get_section_condition"),
			TEXT("set_track_condition"),
			TEXT("get_track_condition"),
			TEXT("set_track_row_condition"),
			TEXT("get_track_row_condition"),

			TEXT("add_rig_control"),
			TEXT("remove_rig_control"),
			TEXT("set_rig_control_properties"),
			TEXT("add_rig_variable"),
			TEXT("add_rig_vm_node"),
			TEXT("remove_rig_node"),
			TEXT("connect_rig_pins"),
			TEXT("disconnect_rig_pins"),
			TEXT("set_rig_pin_value"),
			TEXT("add_rig_two_bone_ik"),
			TEXT("add_rig_aim_constraint"),
			TEXT("add_rig_null"),
			TEXT("reparent_rig_element"),
			TEXT("add_rig_socket"),
			TEXT("add_rig_curve"),
			TEXT("add_rig_function"),
			TEXT("add_rig_function_pin"),
			TEXT("add_rig_function_node"),
			TEXT("add_rig_control_space"),
			TEXT("add_rig_control_chain"),
			TEXT("mirror_rig_control"),
			TEXT("set_rig_control_offset_transform"),
			TEXT("set_rig_control_shape_transform"),
			TEXT("build_rig_logic"),
			TEXT("compile_control_rig"),
			TEXT("get_control_rig_summary"),
			TEXT("get_rig_control"),
			TEXT("get_rig_graph_nodes"),
			TEXT("list_rig_vm_node_types"),
			TEXT("controlrig_get_anim_layers"),
			TEXT("controlrig_add_anim_layer_from_selection"),
			TEXT("controlrig_delete_anim_layer"),
			TEXT("controlrig_duplicate_anim_layer"),
			TEXT("controlrig_merge_anim_layers"),
			TEXT("controlrig_set_layered_mode"),
			TEXT("controlrig_is_layered_control_rig"),
			TEXT("controlrig_bake_to_control_rig"),
			TEXT("controlrig_collapse_anim_layers"),
			TEXT("controlrig_tween_control_rig"),
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

void FUECPCinematicsExtModule::StartupModule()
{
	if (!IUECPCoreModule::IsAvailable()) return;
	IUECPToolDispatcher& D = IUECPCoreModule::Get().GetToolDispatcher();

	D.RegisterHandler(TEXT("get_sequence_summary"),                   MakeHandler(SequencerTools::HandleGetSequenceSummaryFromArgs));
	D.RegisterHandler(TEXT("get_sequence_details"),                   MakeHandler(SequencerTools::HandleGetSequenceSummaryFromArgs));
	D.RegisterHandler(TEXT("get_sequence_full_data"),                 MakeHandler(SequencerTools::HandleGetSequenceFullDataFromArgs));
	D.RegisterHandler(TEXT("get_sequence_bindings"),                  MakeHandler(SequencerTools::HandleGetSequenceBindingsFromArgs));
	D.RegisterHandler(TEXT("get_sequence_keyframes"),                 MakeHandler(SequencerTools::HandleGetSequenceKeyframesFromArgs));
	D.RegisterHandler(TEXT("list_sequences"),                         MakeHandler(SequencerTools::HandleListSequencesFromArgs));
	D.RegisterHandler(TEXT("add_sequence_actor_binding"),             MakeHandler(SequencerTools::HandleAddSequenceActorBindingFromArgs));
	D.RegisterHandler(TEXT("add_sequence_spawnable"),                 MakeHandler(SequencerTools::HandleAddSequenceSpawnableFromArgs));
	D.RegisterHandler(TEXT("remove_sequence_binding"),                MakeHandler(SequencerTools::HandleRemoveSequenceBindingFromArgs));
	D.RegisterHandler(TEXT("add_sequence_transform_track"),           MakeHandler(SequencerTools::HandleAddSequenceTransformTrackFromArgs));
	D.RegisterHandler(TEXT("add_sequence_camera_cut_track"),          MakeHandler(SequencerTools::HandleAddSequenceCameraCutTrackFromArgs));
	D.RegisterHandler(TEXT("add_sequence_camera_track"),              MakeHandler(SequencerTools::HandleAddSequenceCameraTrackFromArgs));
	D.RegisterHandler(TEXT("add_sequence_audio_track"),               MakeHandler(SequencerTools::HandleAddSequenceAudioTrackFromArgs));
	D.RegisterHandler(TEXT("add_sequence_event_track"),               MakeHandler(SequencerTools::HandleAddSequenceEventTrackFromArgs));
	D.RegisterHandler(TEXT("add_sequence_visibility_track"),          MakeHandler(SequencerTools::HandleAddSequenceVisibilityTrackFromArgs));
	D.RegisterHandler(TEXT("add_sequence_skeletal_animation_track"),  MakeHandler(SequencerTools::HandleAddSequenceSkeletalAnimTrackFromArgs));
	D.RegisterHandler(TEXT("add_sequence_material_track"),            MakeHandler(SequencerTools::HandleAddSequenceMaterialTrackFromArgs));
	D.RegisterHandler(TEXT("add_sequence_fade_track"),                MakeHandler(SequencerTools::HandleAddSequenceFadeTrackFromArgs));
	D.RegisterHandler(TEXT("add_sequence_color_track"),               MakeHandler(SequencerTools::HandleAddSequenceColorTrackFromArgs));
	D.RegisterHandler(TEXT("add_sequence_float_track"),               MakeHandler(SequencerTools::HandleAddSequenceFloatTrackFromArgs));
	D.RegisterHandler(TEXT("add_sequence_bool_track"),                MakeHandler(SequencerTools::HandleAddSequenceBoolTrackFromArgs));
	D.RegisterHandler(TEXT("add_sequence_integer_track"),             MakeHandler(SequencerTools::HandleAddSequenceIntegerTrackFromArgs));
	D.RegisterHandler(TEXT("remove_sequence_track"),                  MakeHandler(SequencerTools::HandleRemoveSequenceTrackFromArgs));
	D.RegisterHandler(TEXT("add_sequence_keyframe"),                  MakeHandler(SequencerTools::HandleAddSequenceKeyframeFromArgs));
	D.RegisterHandler(TEXT("add_float_keyframe"),                     MakeHandler(SequencerTools::HandleAddFloatKeyframeFromArgs));
	D.RegisterHandler(TEXT("add_color_keyframe"),                     MakeHandler(SequencerTools::HandleAddColorKeyframeFromArgs));
	D.RegisterHandler(TEXT("add_visibility_keyframe"),                MakeHandler(SequencerTools::HandleAddVisibilityKeyframeFromArgs));
	D.RegisterHandler(TEXT("add_bool_keyframe"),                      MakeHandler(SequencerTools::HandleAddBoolKeyframeFromArgs));
	D.RegisterHandler(TEXT("add_integer_keyframe"),                   MakeHandler(SequencerTools::HandleAddIntegerKeyframeFromArgs));
	D.RegisterHandler(TEXT("remove_sequence_keyframe"),               MakeHandler(SequencerTools::HandleRemoveSequenceKeyframeFromArgs));
	D.RegisterHandler(TEXT("set_keyframe_interpolation"),             MakeHandler(SequencerTools::HandleSetKeyframeInterpolationFromArgs));
	D.RegisterHandler(TEXT("set_sequence_section_range"),             MakeHandler(SequencerTools::HandleSetSequenceSectionRangeFromArgs));
	D.RegisterHandler(TEXT("set_sequence_playback_settings"),         MakeHandler(SequencerTools::HandleSetSequencePlaybackSettingsFromArgs));
	D.RegisterHandler(TEXT("set_sequence_display_rate"),              MakeHandler(SequencerTools::HandleSetSequenceDisplayRateFromArgs));
	D.RegisterHandler(TEXT("set_section_blend_type"),                 MakeHandler(SequencerTools::HandleSetSectionBlendTypeFromArgs));
	D.RegisterHandler(TEXT("set_section_completion_mode"),            MakeHandler(SequencerTools::HandleSetSectionCompletionModeFromArgs));
	D.RegisterHandler(TEXT("move_sequencer_section"),                 MakeHandler(SequencerTools::HandleMoveSequencerSectionFromArgs));
	D.RegisterHandler(TEXT("resize_sequencer_section"),               MakeHandler(SequencerTools::HandleResizeSequencerSectionFromArgs));
	D.RegisterHandler(TEXT("split_sequencer_section"),                MakeHandler(SequencerTools::HandleSplitSequencerSectionFromArgs));
	D.RegisterHandler(TEXT("remove_sequencer_section"),               MakeHandler(SequencerTools::HandleRemoveSequencerSectionFromArgs));
	D.RegisterHandler(TEXT("move_sequencer_keyframe"),                MakeHandler(SequencerTools::HandleMoveSequencerKeyframeFromArgs));
	D.RegisterHandler(TEXT("set_sequencer_keyframe_value"),           MakeHandler(SequencerTools::HandleSetSequencerKeyframeValueFromArgs));
	D.RegisterHandler(TEXT("rename_sequencer_track"),                 MakeHandler(SequencerTools::HandleRenameSequencerTrackFromArgs));
	D.RegisterHandler(TEXT("set_sequencer_track_eval_disabled"),      MakeHandler(SequencerTools::HandleSetSequencerTrackEvalDisabledFromArgs));
	D.RegisterHandler(TEXT("set_sequencer_track_sort_order"),         MakeHandler(SequencerTools::HandleSetSequencerTrackSortOrderFromArgs));
	D.RegisterHandler(TEXT("rename_sequencer_binding"),               MakeHandler(SequencerTools::HandleRenameSequencerBindingFromArgs));
	D.RegisterHandler(TEXT("add_sequence_camera_shake"),              MakeHandler(SequencerTools::HandleAddSequenceCameraShakeFromArgs));
	D.RegisterHandler(TEXT("add_sequence_sub_sequence"),              MakeHandler(SequencerTools::HandleAddSequenceSubSequenceFromArgs));
	D.RegisterHandler(TEXT("list_subsequences"),                      MakeHandler(SequencerTools::HandleListSubsequencesFromArgs));
	D.RegisterHandler(TEXT("remove_subsequence"),                     MakeHandler(SequencerTools::HandleRemoveSubsequenceFromArgs));
	D.RegisterHandler(TEXT("set_subsequence_params"),                 MakeHandler(SequencerTools::HandleSetSubsequenceParamsFromArgs));
	D.RegisterHandler(TEXT("add_level_visibility_section"),           MakeHandler(SequencerTools::HandleAddLevelVisibilitySectionFromArgs));

	D.RegisterHandler(TEXT("spawn_camera_rig_crane"),                 MakeHandler(SequencerTools::HandleSpawnCameraRigCraneFromArgs));
	D.RegisterHandler(TEXT("spawn_camera_rig_rail"),                  MakeHandler(SequencerTools::HandleSpawnCameraRigRailFromArgs));
	D.RegisterHandler(TEXT("attach_camera_to_rig"),                   MakeHandler(SequencerTools::HandleAttachCameraToRigFromArgs));
	D.RegisterHandler(TEXT("set_rig_rail_position"),                  MakeHandler(SequencerTools::HandleSetRigRailPositionFromArgs));

	D.RegisterHandler(TEXT("create_mrq_job"),                         MakeHandler(SequencerTools::HandleCreateMRQJobFromArgs));
	D.RegisterHandler(TEXT("delete_mrq_job"),                         MakeHandler(SequencerTools::HandleDeleteMRQJobFromArgs));
	D.RegisterHandler(TEXT("set_mrq_output_settings"),                MakeHandler(SequencerTools::HandleSetMRQOutputSettingsFromArgs));
	D.RegisterHandler(TEXT("add_mrq_render_pass"),                    MakeHandler(SequencerTools::HandleAddMRQRenderPassFromArgs));
	D.RegisterHandler(TEXT("list_mrq_render_passes"),                 MakeHandler(SequencerTools::HandleListMRQRenderPassesFromArgs));
	D.RegisterHandler(TEXT("execute_mrq_render"),                     MakeHandler(SequencerTools::HandleExecuteMRQRenderFromArgs));
	D.RegisterHandler(TEXT("get_mrq_render_status"),                  MakeHandler(SequencerTools::HandleGetMRQRenderStatusFromArgs));
	D.RegisterHandler(TEXT("cancel_mrq_render"),                      MakeHandler(SequencerTools::HandleCancelMRQRenderFromArgs));
	D.RegisterHandler(TEXT("get_mrq_queue_summary"),                  MakeHandler(SequencerTools::HandleGetMRQQueueSummaryFromArgs));
	D.RegisterHandler(TEXT("clear_mrq_queue"),                        MakeHandler(SequencerTools::HandleClearMRQQueueFromArgs));

	D.RegisterHandler(TEXT("get_rig_summary"),                        MakeHandler(SequencerTools::HandleGetRigSummary));
	D.RegisterHandler(TEXT("add_control_rig_track"),                  MakeHandler(SequencerTools::HandleAddControlRigSequencerTrackFromArgs));
	D.RegisterHandler(TEXT("list_control_rig_controls"),              MakeHandler(SequencerTools::HandleListControlRigControlsFromArgs));
	D.RegisterHandler(TEXT("add_control_rig_section"),                MakeHandler(SequencerTools::HandleAddControlRigSectionFromArgs));
	D.RegisterHandler(TEXT("set_control_rig_keyframe"),               MakeHandler(SequencerTools::HandleSetControlRigKeyframeFromArgs));

	D.RegisterHandler(TEXT("add_marked_frame"),              MakeHandler(SequencerOutlinerTools::HandleAddMarkedFrameFromArgs));
	D.RegisterHandler(TEXT("delete_marked_frame"),           MakeHandler(SequencerOutlinerTools::HandleDeleteMarkedFrameFromArgs));
	D.RegisterHandler(TEXT("delete_all_marked_frames"),      MakeHandler(SequencerOutlinerTools::HandleDeleteAllMarkedFramesFromArgs));
	D.RegisterHandler(TEXT("find_marked_frame_by_label"),    MakeHandler(SequencerOutlinerTools::HandleFindMarkedFrameByLabelFromArgs));
	D.RegisterHandler(TEXT("get_marked_frames"),             MakeHandler(SequencerOutlinerTools::HandleGetMarkedFramesFromArgs));
	D.RegisterHandler(TEXT("set_marked_frames_locked"),      MakeHandler(SequencerOutlinerTools::HandleSetMarkedFramesLockedFromArgs));
	D.RegisterHandler(TEXT("set_node_muted"),                MakeHandler(SequencerOutlinerTools::HandleSetNodeMutedFromArgs));
	D.RegisterHandler(TEXT("set_node_solo"),                 MakeHandler(SequencerOutlinerTools::HandleSetNodeSoloFromArgs));
	D.RegisterHandler(TEXT("set_node_pinned"),               MakeHandler(SequencerOutlinerTools::HandleSetNodePinnedFromArgs));
	D.RegisterHandler(TEXT("get_outliner_state"),            MakeHandler(SequencerOutlinerTools::HandleGetOutlinerStateFromArgs));
	D.RegisterHandler(TEXT("set_section_locked"),            MakeHandler(SequencerOutlinerTools::HandleSetSectionLockedFromArgs));
	D.RegisterHandler(TEXT("set_playback_range_locked"),     MakeHandler(SequencerOutlinerTools::HandleSetPlaybackRangeLockedFromArgs));
	D.RegisterHandler(TEXT("focus_sub_sequence"),            MakeHandler(SequencerOutlinerTools::HandleFocusSubSequenceFromArgs));
	D.RegisterHandler(TEXT("focus_parent_sequence"),         MakeHandler(SequencerOutlinerTools::HandleFocusParentSequenceFromArgs));
	D.RegisterHandler(TEXT("get_sub_sequence_hierarchy"),    MakeHandler(SequencerOutlinerTools::HandleGetSubSequenceHierarchyFromArgs));

	D.RegisterHandler(TEXT("tag_binding"),                   MakeHandler(SequencerBindingTools::HandleTagBindingFromArgs));
	D.RegisterHandler(TEXT("untag_binding"),                 MakeHandler(SequencerBindingTools::HandleUntagBindingFromArgs));
	D.RegisterHandler(TEXT("find_binding_by_tag"),           MakeHandler(SequencerBindingTools::HandleFindBindingByTagFromArgs));
	D.RegisterHandler(TEXT("find_bindings_by_tag"),          MakeHandler(SequencerBindingTools::HandleFindBindingsByTagFromArgs));
	D.RegisterHandler(TEXT("get_all_binding_tags"),          MakeHandler(SequencerBindingTools::HandleGetAllBindingTagsFromArgs));
	D.RegisterHandler(TEXT("get_binding_tags"),              MakeHandler(SequencerBindingTools::HandleGetBindingTagsFromArgs));
	D.RegisterHandler(TEXT("remove_binding_tag"),            MakeHandler(SequencerBindingTools::HandleRemoveBindingTagFromArgs));
	D.RegisterHandler(TEXT("fix_actor_references"),          MakeHandler(SequencerBindingTools::HandleFixActorReferencesFromArgs));
	D.RegisterHandler(TEXT("rebind_component"),              MakeHandler(SequencerBindingTools::HandleRebindComponentFromArgs));
	D.RegisterHandler(TEXT("remove_invalid_bindings"),       MakeHandler(SequencerBindingTools::HandleRemoveInvalidBindingsFromArgs));
	D.RegisterHandler(TEXT("replace_binding_with_actors"),   MakeHandler(SequencerBindingTools::HandleReplaceBindingWithActorsFromArgs));
	D.RegisterHandler(TEXT("add_actors_to_binding"),         MakeHandler(SequencerBindingTools::HandleAddActorsToBindingFromArgs));
	D.RegisterHandler(TEXT("remove_actors_from_binding"),    MakeHandler(SequencerBindingTools::HandleRemoveActorsFromBindingFromArgs));
	D.RegisterHandler(TEXT("remove_all_bindings"),           MakeHandler(SequencerBindingTools::HandleRemoveAllBindingsFromArgs));
	D.RegisterHandler(TEXT("convert_to_spawnable"),          MakeHandler(SequencerBindingTools::HandleConvertToSpawnableFromArgs));
	D.RegisterHandler(TEXT("convert_to_possessable"),        MakeHandler(SequencerBindingTools::HandleConvertToPossessableFromArgs));
	D.RegisterHandler(TEXT("change_actor_template_class"),   MakeHandler(SequencerBindingTools::HandleChangeActorTemplateClassFromArgs));
	D.RegisterHandler(TEXT("save_default_spawnable_state"),  MakeHandler(SequencerBindingTools::HandleSaveDefaultSpawnableStateFromArgs));
	D.RegisterHandler(TEXT("get_custom_binding_type"),       MakeHandler(SequencerBindingTools::HandleGetCustomBindingTypeFromArgs));

	D.RegisterHandler(TEXT("copy_bindings"),                 MakeHandler(SequencerWorkflowTools::HandleCopyBindingsFromArgs));
	D.RegisterHandler(TEXT("paste_bindings"),                MakeHandler(SequencerWorkflowTools::HandlePasteBindingsFromArgs));
	D.RegisterHandler(TEXT("copy_tracks"),                   MakeHandler(SequencerWorkflowTools::HandleCopyTracksFromArgs));
	D.RegisterHandler(TEXT("paste_tracks"),                  MakeHandler(SequencerWorkflowTools::HandlePasteTracksFromArgs));
	D.RegisterHandler(TEXT("copy_sections"),                 MakeHandler(SequencerWorkflowTools::HandleCopySectionsFromArgs));
	D.RegisterHandler(TEXT("paste_sections"),                MakeHandler(SequencerWorkflowTools::HandlePasteSectionsFromArgs));
	D.RegisterHandler(TEXT("add_event_trigger_section"),     MakeHandler(SequencerWorkflowTools::HandleAddEventTriggerSectionFromArgs));
	D.RegisterHandler(TEXT("add_event_repeater_section"),    MakeHandler(SequencerWorkflowTools::HandleAddEventRepeaterSectionFromArgs));
	D.RegisterHandler(TEXT("set_section_condition"),         MakeHandler(SequencerWorkflowTools::HandleSetSectionConditionFromArgs));
	D.RegisterHandler(TEXT("get_section_condition"),         MakeHandler(SequencerWorkflowTools::HandleGetSectionConditionFromArgs));
	D.RegisterHandler(TEXT("set_track_condition"),           MakeHandler(SequencerWorkflowTools::HandleSetTrackConditionFromArgs));
	D.RegisterHandler(TEXT("get_track_condition"),           MakeHandler(SequencerWorkflowTools::HandleGetTrackConditionFromArgs));
	D.RegisterHandler(TEXT("set_track_row_condition"),       MakeHandler(SequencerWorkflowTools::HandleSetTrackRowConditionFromArgs));
	D.RegisterHandler(TEXT("get_track_row_condition"),       MakeHandler(SequencerWorkflowTools::HandleGetTrackRowConditionFromArgs));

	D.RegisterHandler(TEXT("add_rig_control"),            MakeHandler(ControlRigTools::HandleAddRigControlFromArgs));
	D.RegisterHandler(TEXT("remove_rig_control"),         MakeHandler(ControlRigTools::HandleRemoveRigControlFromArgs));
	D.RegisterHandler(TEXT("set_rig_control_properties"), MakeHandler(ControlRigTools::HandleSetRigControlPropertiesFromArgs));
	D.RegisterHandler(TEXT("add_rig_variable"),           MakeHandler(ControlRigTools::HandleAddRigVariableFromArgs));
	D.RegisterHandler(TEXT("add_rig_vm_node"),            MakeHandler(ControlRigTools::HandleAddRigVMNodeFromArgs));
	D.RegisterHandler(TEXT("remove_rig_node"),            MakeHandler(ControlRigTools::HandleRemoveRigNodeFromArgs));
	D.RegisterHandler(TEXT("connect_rig_pins"),           MakeHandler(ControlRigTools::HandleConnectRigPinsFromArgs));
	D.RegisterHandler(TEXT("disconnect_rig_pins"),        MakeHandler(ControlRigTools::HandleDisconnectRigPinsFromArgs));
	D.RegisterHandler(TEXT("set_rig_pin_value"),          MakeHandler(ControlRigTools::HandleSetRigPinValueFromArgs));
	D.RegisterHandler(TEXT("add_rig_two_bone_ik"),        MakeHandler(ControlRigTools::HandleAddRigTwoBoneIKFromArgs));
	D.RegisterHandler(TEXT("add_rig_aim_constraint"),     MakeHandler(ControlRigTools::HandleAddRigAimConstraintFromArgs));
	D.RegisterHandler(TEXT("add_rig_null"),               MakeHandler(ControlRigTools::HandleAddRigNullFromArgs));
	D.RegisterHandler(TEXT("reparent_rig_element"),       MakeHandler(ControlRigTools::HandleReparentRigElementFromArgs));
	D.RegisterHandler(TEXT("add_rig_socket"),             MakeHandler(ControlRigTools::HandleAddRigSocketFromArgs));
	D.RegisterHandler(TEXT("add_rig_curve"),              MakeHandler(ControlRigTools::HandleAddRigCurveFromArgs));
	D.RegisterHandler(TEXT("add_rig_function"),           MakeHandler(ControlRigTools::HandleAddRigFunctionFromArgs));
	D.RegisterHandler(TEXT("add_rig_function_pin"),       MakeHandler(ControlRigTools::HandleAddRigFunctionPinFromArgs));
	D.RegisterHandler(TEXT("add_rig_function_node"),      MakeHandler(ControlRigTools::HandleAddRigFunctionNodeFromArgs));
	D.RegisterHandler(TEXT("add_rig_control_space"),      MakeHandler(ControlRigTools::HandleAddRigControlSpaceFromArgs));
	D.RegisterHandler(TEXT("add_rig_control_chain"),      MakeHandler(ControlRigTools::HandleAddRigControlChainFromArgs));
	D.RegisterHandler(TEXT("mirror_rig_control"),         MakeHandler(ControlRigTools::HandleMirrorRigControlFromArgs));
	D.RegisterHandler(TEXT("set_rig_control_offset_transform"), MakeHandler(ControlRigTools::HandleSetRigControlOffsetTransformFromArgs));
	D.RegisterHandler(TEXT("set_rig_control_shape_transform"),  MakeHandler(ControlRigTools::HandleSetRigControlShapeTransformFromArgs));
	D.RegisterHandler(TEXT("build_rig_logic"),            MakeHandler(ControlRigTools::HandleBuildRigLogicFromArgs));
	D.RegisterHandler(TEXT("compile_control_rig"),        MakeHandler(ControlRigTools::HandleCompileControlRigFromArgs));
	D.RegisterHandler(TEXT("get_control_rig_summary"),    MakeHandler(ControlRigTools::HandleGetControlRigSummaryFromArgs));
	D.RegisterHandler(TEXT("get_rig_control"),            MakeHandler(ControlRigTools::HandleGetRigControlFromArgs));
	D.RegisterHandler(TEXT("get_rig_graph_nodes"),        MakeHandler(ControlRigTools::HandleGetRigGraphNodesFromArgs));
	D.RegisterHandler(TEXT("list_rig_vm_node_types"),     MakeHandler(ControlRigTools::HandleListRigVMNodeTypesFromArgs));

	D.RegisterHandler(TEXT("controlrig_get_anim_layers"),                MakeHandler(ControlRigAnimLayerTools::HandleGetAnimLayersFromArgs));
	D.RegisterHandler(TEXT("controlrig_add_anim_layer_from_selection"),  MakeHandler(ControlRigAnimLayerTools::HandleAddAnimLayerFromSelectionFromArgs));
	D.RegisterHandler(TEXT("controlrig_delete_anim_layer"),              MakeHandler(ControlRigAnimLayerTools::HandleDeleteAnimLayerFromArgs));
	D.RegisterHandler(TEXT("controlrig_duplicate_anim_layer"),           MakeHandler(ControlRigAnimLayerTools::HandleDuplicateAnimLayerFromArgs));
	D.RegisterHandler(TEXT("controlrig_merge_anim_layers"),              MakeHandler(ControlRigAnimLayerTools::HandleMergeAnimLayersFromArgs));
	D.RegisterHandler(TEXT("controlrig_set_layered_mode"),               MakeHandler(ControlRigAnimLayerTools::HandleSetLayeredModeFromArgs));
	D.RegisterHandler(TEXT("controlrig_is_layered_control_rig"),         MakeHandler(ControlRigAnimLayerTools::HandleIsLayeredControlRigFromArgs));
	D.RegisterHandler(TEXT("controlrig_bake_to_control_rig"),            MakeHandler(ControlRigAnimLayerTools::HandleBakeToControlRigFromArgs));
	D.RegisterHandler(TEXT("controlrig_collapse_anim_layers"),           MakeHandler(ControlRigAnimLayerTools::HandleCollapseAnimLayersFromArgs));
	D.RegisterHandler(TEXT("controlrig_tween_control_rig"),              MakeHandler(ControlRigAnimLayerTools::HandleTweenControlRigFromArgs));

	{
		IUECPCreateAssetRegistry& Reg = IUECPCoreModule::Get().GetCreateAssetRegistry();
		const FName ExtId(TEXT("Cinematics"));
		Reg.RegisterType(TEXT("ControlRig"),    UECPCreateAsset::FactoryFromArgsFn(&ControlRigTools::HandleCreateControlRigFromArgs, TEXT("ControlRig")),    ExtId);
		Reg.RegisterType(TEXT("LevelSequence"), UECPCreateAsset::FactoryFromArgsFn(&SequencerTools::HandleCreateLevelSequenceFromArgs, TEXT("LevelSequence")), ExtId);
	}

	{
		const FName U(TEXT("sequencer"));
		const auto Meta = [&D, &U](const TCHAR* Action, const TCHAR* Summary, const TCHAR* Params)
		{ D.RegisterToolMetadata({ FName(Action), U, FString(Summary), FString(Params) }); };

		Meta(TEXT("get_sequence_summary"),    TEXT("Compact summary of a Level Sequence (bindings, tracks, range; alias get_sequence_details)."), TEXT("sequence_path"));
		Meta(TEXT("get_sequence_details"),    TEXT("Alias of get_sequence_summary."), TEXT("sequence_path"));
		Meta(TEXT("get_sequence_full_data"),  TEXT("Full dump of a sequence's bindings/tracks/sections/keys."), TEXT("sequence_path"));
		Meta(TEXT("get_sequence_bindings"),   TEXT("List a sequence's bindings (possessables + spawnables)."), TEXT("sequence_path"));
		Meta(TEXT("get_sequence_keyframes"),  TEXT("List keyframes on a binding's property track."), TEXT("sequence_path, actor_label, property_name"));
		Meta(TEXT("list_sequences"),          TEXT("Find Level Sequence assets under a content path."), TEXT("search_path?"));
		Meta(TEXT("add_sequence_actor_binding"), TEXT("Bind a level actor as a possessable (batch supported)."), TEXT("sequence_path, actor_label"));
		Meta(TEXT("add_sequence_spawnable"),     TEXT("Add a spawnable from an actor class (spawn track auto-added)."), TEXT("sequence_path, actor_class, label"));
		Meta(TEXT("remove_sequence_binding"),    TEXT("Remove a binding and all its tracks."), TEXT("sequence_path, actor_label"));
		Meta(TEXT("add_sequence_transform_track"),  TEXT("Add a 3D transform track to a binding."), TEXT("sequence_path, actor_label"));
		Meta(TEXT("add_sequence_camera_cut_track"), TEXT("Add a camera-cut track for a camera binding."), TEXT("sequence_path, camera_actor_label, start_time_seconds, end_time_seconds"));
		Meta(TEXT("add_sequence_camera_track"),     TEXT("Add a camera (binding) track."), TEXT("sequence_path, camera_actor_label"));
		Meta(TEXT("add_sequence_audio_track"),      TEXT("Add an audio track with a sound section."), TEXT("sequence_path, sound_path, start_time_seconds, end_time_seconds"));
		Meta(TEXT("add_sequence_event_track"),      TEXT("Add an event track to a binding."), TEXT("sequence_path, actor_label"));
		Meta(TEXT("add_sequence_visibility_track"), TEXT("Add a visibility (spawned/active) track."), TEXT("sequence_path, actor_label"));
		Meta(TEXT("add_sequence_skeletal_animation_track"), TEXT("Add a skeletal animation track with an anim section."), TEXT("sequence_path, actor_label, animation_path, start_time_seconds"));
		Meta(TEXT("add_sequence_material_track"),   TEXT("Add a material-parameter track on a material slot."), TEXT("sequence_path, actor_label, material_index, parameter_name"));
		Meta(TEXT("add_sequence_fade_track"),       TEXT("Add a global fade track."), TEXT("sequence_path, fade_in_duration, fade_out_duration"));
		Meta(TEXT("add_sequence_color_track"),      TEXT("Add a color property track."), TEXT("sequence_path, actor_label, property_name"));
		Meta(TEXT("add_sequence_float_track"),      TEXT("Add a float property track."), TEXT("sequence_path, actor_label, property_name"));
		Meta(TEXT("add_sequence_bool_track"),       TEXT("Add a bool property track."), TEXT("sequence_path, actor_label, property_name"));
		Meta(TEXT("add_sequence_integer_track"),    TEXT("Add an integer property track."), TEXT("sequence_path, actor_label, property_name"));
		Meta(TEXT("remove_sequence_track"),         TEXT("Remove a track by name or type."), TEXT("sequence_path, track_name|track_type"));
		Meta(TEXT("add_sequence_keyframe"),     TEXT("Add a transform keyframe (location/rotation/scale)."), TEXT("sequence_path, actor_label, time_seconds, location_*, rotation_*, scale_*"));
		Meta(TEXT("add_float_keyframe"),        TEXT("Add a float keyframe (needs float track)."), TEXT("sequence_path, actor_label, property_name, time_seconds, value"));
		Meta(TEXT("add_color_keyframe"),        TEXT("Add a color keyframe (needs color track)."), TEXT("sequence_path, actor_label, property_name, time_seconds, r, g, b, a"));
		Meta(TEXT("add_visibility_keyframe"),   TEXT("Add a visibility keyframe."), TEXT("sequence_path, actor_label, time_seconds, value"));
		Meta(TEXT("add_bool_keyframe"),         TEXT("Add a bool keyframe (needs bool track)."), TEXT("sequence_path, actor_label, property_name, time_seconds, value"));
		Meta(TEXT("add_integer_keyframe"),      TEXT("Add an integer keyframe (needs integer track)."), TEXT("sequence_path, actor_label, property_name, time_seconds, value"));
		Meta(TEXT("remove_sequence_keyframe"),  TEXT("Remove a keyframe by index from a track."), TEXT("sequence_path, track_name, key_index"));
		Meta(TEXT("set_keyframe_interpolation"),TEXT("Set a key's interpolation (linear|cubic|constant)."), TEXT("sequence_path, track_name, key_index, interp_mode"));
		Meta(TEXT("set_sequence_section_range"),     TEXT("Set the sequence playback frame range."), TEXT("sequence_path, start_frame, end_frame, frames_per_second"));
		Meta(TEXT("set_sequence_playback_settings"), TEXT("Set frame rate + duration."), TEXT("sequence_path, frame_rate?, duration_seconds?"));
		Meta(TEXT("set_sequence_display_rate"),      TEXT("Set the display rate (24/30/60)."), TEXT("sequence_path, display_fps"));
		Meta(TEXT("set_section_blend_type"),         TEXT("Set a section's blend type (absolute|additive|relative|additive_from_base)."), TEXT("sequence_path, actor_label, track_type, section_index, blend_type"));
		Meta(TEXT("set_section_completion_mode"),    TEXT("Set a section's completion mode (keep_state|restore_state|project_default)."), TEXT("sequence_path, actor_label, track_type, section_index, completion_mode"));
		Meta(TEXT("move_sequencer_section"),    TEXT("Shift a section, preserving duration."), TEXT("sequence_path, actor_label?, track_name?, section_index?, new_start_seconds"));
		Meta(TEXT("resize_sequencer_section"),  TEXT("Change a section's end (start stays)."), TEXT("sequence_path, actor_label?, track_name?, section_index?, new_end_seconds"));
		Meta(TEXT("split_sequencer_section"),   TEXT("Split a section at a time inside its range."), TEXT("sequence_path, actor_label?, track_name?, section_index?, split_seconds"));
		Meta(TEXT("remove_sequencer_section"),  TEXT("Drop a single section without removing the track."), TEXT("sequence_path, actor_label?, track_name?, section_index?"));
		Meta(TEXT("move_sequencer_keyframe"),   TEXT("Move keys near a time to a new time (all channels)."), TEXT("sequence_path, actor_label, time_seconds, new_time_seconds, property_name?"));
		Meta(TEXT("set_sequencer_keyframe_value"), TEXT("Set a single-channel scalar key's value."), TEXT("sequence_path, actor_label, property_name, time_seconds, value, value_type"));
		Meta(TEXT("rename_sequencer_track"),    TEXT("Rename a nameable track's display name."), TEXT("sequence_path, actor_label?, track_name, new_display_name"));
		Meta(TEXT("set_sequencer_track_eval_disabled"), TEXT("Mute a track (data kept, eval skipped)."), TEXT("sequence_path, actor_label?, track_name, disabled"));
		Meta(TEXT("set_sequencer_track_sort_order"),    TEXT("Set a track's sort order (higher = lower in list)."), TEXT("sequence_path, actor_label?, track_name, sort_order"));
		Meta(TEXT("rename_sequencer_binding"),  TEXT("Rename a possessable/spawnable (collision-checked)."), TEXT("sequence_path, current_label, new_label"));
		Meta(TEXT("add_sequence_camera_shake"), TEXT("Add/extend a camera-shake track on a camera binding."), TEXT("sequence_path, actor_label, shake_class_path, start_seconds, duration_seconds?, play_scale?"));
		Meta(TEXT("add_sequence_sub_sequence"), TEXT("Add a sub-sequence section."), TEXT("sequence_path, sub_sequence_path, start_frame"));
		Meta(TEXT("list_subsequences"),         TEXT("List sub-sequence sections."), TEXT("sequence_path"));
		Meta(TEXT("remove_subsequence"),        TEXT("Remove a sub-sequence section by index."), TEXT("sequence_path, subsequence_index"));
		Meta(TEXT("set_subsequence_params"),    TEXT("Set a sub-sequence's offset + time scale."), TEXT("sequence_path, subsequence_index, start_offset_frames, time_scale"));
		Meta(TEXT("add_level_visibility_section"), TEXT("Add a level-visibility section (visible|hidden)."), TEXT("sequence_path, level_names, visibility, start_time, end_time"));
		Meta(TEXT("spawn_camera_rig_crane"), TEXT("Spawn a camera crane rig actor (batch)."), TEXT("actor_label, location_x/y/z, crane_arm_length?"));
		Meta(TEXT("spawn_camera_rig_rail"),  TEXT("Spawn a camera rail rig actor (batch)."), TEXT("actor_label, location_x/y/z"));
		Meta(TEXT("attach_camera_to_rig"),   TEXT("Attach a camera actor to a rig (batch)."), TEXT("camera_label, rig_label"));
		Meta(TEXT("set_rig_rail_position"),  TEXT("Set a rail rig's position 0-1 (batch)."), TEXT("actor_label, position"));
		Meta(TEXT("create_mrq_job"),         TEXT("Create an MRQ render job for a sequence + map."), TEXT("sequence_path, job_name, map_path"));
		Meta(TEXT("delete_mrq_job"),         TEXT("Delete an MRQ job."), TEXT("job_name"));
		Meta(TEXT("set_mrq_output_settings"),TEXT("Set an MRQ job's output dir/format/framerate."), TEXT("job_name, output_directory, filename_format, file_format, frame_rate"));
		Meta(TEXT("add_mrq_render_pass"),    TEXT("Add a render pass (deferred|png|exr|jpg|anti_alias)."), TEXT("job_name, pass_type"));
		Meta(TEXT("list_mrq_render_passes"), TEXT("List a job's render passes."), TEXT("job_name"));
		Meta(TEXT("execute_mrq_render"),     TEXT("Start rendering the MRQ queue."), TEXT(""));
		Meta(TEXT("get_mrq_render_status"),  TEXT("Get current MRQ render status."), TEXT(""));
		Meta(TEXT("cancel_mrq_render"),      TEXT("Cancel the active MRQ render."), TEXT(""));
		Meta(TEXT("get_mrq_queue_summary"),  TEXT("Summarise the MRQ job queue."), TEXT(""));
		Meta(TEXT("clear_mrq_queue"),        TEXT("Clear all MRQ jobs."), TEXT(""));
		Meta(TEXT("get_rig_summary"),           TEXT("Summarise Control Rig tracks/bindings in a sequence."), TEXT("sequence_path"));
		Meta(TEXT("add_control_rig_track"),     TEXT("Add a Control Rig track for a binding (auto-binds + instantiates)."), TEXT("sequence_path, binding_label, control_rig_path"));
		Meta(TEXT("list_control_rig_controls"), TEXT("List a bound rig's controls (CALL before keyframing)."), TEXT("sequence_path, binding_label"));
		Meta(TEXT("add_control_rig_section"),   TEXT("Add a Control Rig section over a frame range."), TEXT("sequence_path, binding_label, start_frame, end_frame"));
		Meta(TEXT("set_control_rig_keyframe"),  TEXT("Key a rig control (value_type float|int|bool|vector2d|rotator|transform)."), TEXT("sequence_path, binding_label, control_name, frame, value_type, value|value_list"));
		Meta(TEXT("add_marked_frame"),          TEXT("Add a marked frame (bookmark; batch via marks=[])."), TEXT("sequence_path, frame, label?"));
		Meta(TEXT("delete_marked_frame"),       TEXT("Delete a marked frame by index (batch via indices=[])."), TEXT("sequence_path, index"));
		Meta(TEXT("delete_all_marked_frames"),  TEXT("Delete all marked frames."), TEXT("sequence_path"));
		Meta(TEXT("find_marked_frame_by_label"),TEXT("Find a marked frame index by label (-1 if none)."), TEXT("sequence_path, label"));
		Meta(TEXT("get_marked_frames"),         TEXT("List marked frames."), TEXT("sequence_path"));
		Meta(TEXT("set_marked_frames_locked"),  TEXT("Lock/unlock dragging of marked frames."), TEXT("sequence_path, locked"));
		Meta(TEXT("set_node_muted"),            TEXT("Mute an outliner node by node path (batch via items=[])."), TEXT("sequence_path, node_path, muted"));
		Meta(TEXT("set_node_solo"),             TEXT("Solo an outliner node by node path (batch via items=[])."), TEXT("sequence_path, node_path, solo"));
		Meta(TEXT("set_node_pinned"),           TEXT("Pin an outliner node by node path (batch via items=[])."), TEXT("sequence_path, node_path, pinned"));
		Meta(TEXT("get_outliner_state"),        TEXT("Get muted/solo/pinned/lock state."), TEXT("sequence_path"));
		Meta(TEXT("set_section_locked"),        TEXT("Lock/unlock a section."), TEXT("sequence_path, binding_label?, track_index, section_index, locked"));
		Meta(TEXT("set_playback_range_locked"), TEXT("Lock/unlock the playback range markers."), TEXT("sequence_path, locked"));
		Meta(TEXT("focus_sub_sequence"),        TEXT("Open root, then step into a sub-sequence."), TEXT("sequence_path, sub_sequence_path"));
		Meta(TEXT("focus_parent_sequence"),     TEXT("Step up one sub-sequence level."), TEXT(""));
		Meta(TEXT("get_sub_sequence_hierarchy"),TEXT("Get the sub-section hierarchy from the focused sequence."), TEXT("sequence_path"));
		Meta(TEXT("tag_binding"),        TEXT("Attach a tag to a binding (batch via items=[])."), TEXT("sequence_path, binding_label, tag"));
		Meta(TEXT("untag_binding"),      TEXT("Remove a tag from a binding (batch via items=[])."), TEXT("sequence_path, binding_label, tag"));
		Meta(TEXT("find_binding_by_tag"),TEXT("Find the first binding label with a tag."), TEXT("sequence_path, tag"));
		Meta(TEXT("find_bindings_by_tag"),TEXT("Find all bindings with a tag ({label,guid})."), TEXT("sequence_path, tag"));
		Meta(TEXT("get_all_binding_tags"),TEXT("List all tags and their bindings."), TEXT("sequence_path"));
		Meta(TEXT("get_binding_tags"),   TEXT("List tags on a single binding."), TEXT("sequence_path, binding_label"));
		Meta(TEXT("remove_binding_tag"), TEXT("Drop a tag from every binding it's on."), TEXT("sequence_path, tag"));
		Meta(TEXT("fix_actor_references"),       TEXT("Re-resolve every possessable's bound actor."), TEXT("sequence_path"));
		Meta(TEXT("rebind_component"),           TEXT("Rebind a component possessable to another component."), TEXT("sequence_path, binding_label, component_name"));
		Meta(TEXT("remove_invalid_bindings"),    TEXT("Drop object references that won't resolve."), TEXT("sequence_path"));
		Meta(TEXT("replace_binding_with_actors"),TEXT("Replace a binding's bound actors entirely."), TEXT("sequence_path, binding_label, actor_labels"));
		Meta(TEXT("add_actors_to_binding"),      TEXT("Append actors to a binding's bound list."), TEXT("sequence_path, binding_label, actor_labels"));
		Meta(TEXT("remove_actors_from_binding"), TEXT("Remove listed actors from a binding."), TEXT("sequence_path, binding_label, actor_labels"));
		Meta(TEXT("remove_all_bindings"),        TEXT("Strip every binding from the sequence."), TEXT("sequence_path"));
		Meta(TEXT("convert_to_spawnable"),       TEXT("Convert a possessable to spawnable."), TEXT("sequence_path, binding_label"));
		Meta(TEXT("convert_to_possessable"),     TEXT("Convert a spawnable to possessable."), TEXT("sequence_path, binding_label"));
		Meta(TEXT("change_actor_template_class"),TEXT("Replace a spawnable's template actor class."), TEXT("sequence_path, binding_label, new_class"));
		Meta(TEXT("save_default_spawnable_state"),TEXT("Snapshot the preview actor as spawnable default."), TEXT("sequence_path, binding_label"));
		Meta(TEXT("get_custom_binding_type"),    TEXT("Get a binding's kind (possessable|spawnable)."), TEXT("sequence_path, binding_label"));
		Meta(TEXT("copy_bindings"),  TEXT("Copy bindings to clipboard text (returns exported_text)."), TEXT("sequence_path, binding_labels"));
		Meta(TEXT("paste_bindings"), TEXT("Paste bindings from clipboard text."), TEXT("sequence_path, exported_text?, duplicate_existing_actors?"));
		Meta(TEXT("copy_tracks"),    TEXT("Copy a binding's tracks (returns exported_text)."), TEXT("sequence_path, binding_label?, track_indices"));
		Meta(TEXT("paste_tracks"),   TEXT("Paste tracks onto a target binding."), TEXT("sequence_path, exported_text?, binding_label?"));
		Meta(TEXT("copy_sections"),  TEXT("Copy a track's sections (returns exported_text)."), TEXT("sequence_path, binding_label?, track_index, section_indices"));
		Meta(TEXT("paste_sections"), TEXT("Paste sections at a frame."), TEXT("sequence_path, exported_text?, binding_label?, track_index, time_frame?"));
		Meta(TEXT("add_event_trigger_section"), TEXT("Add an event-trigger section on an event track (batch)."), TEXT("sequence_path, binding_label?, track_index, frame"));
		Meta(TEXT("add_event_repeater_section"),TEXT("Add an event-repeater section across a range (batch)."), TEXT("sequence_path, binding_label?, track_index, start_frame, end_frame"));
		Meta(TEXT("set_section_condition"),  TEXT("Attach/clear a UMovieSceneCondition on a section."), TEXT("sequence_path, binding_label?, track_index, section_index, condition_class"));
		Meta(TEXT("get_section_condition"),  TEXT("Read a section's condition class."), TEXT("sequence_path, binding_label?, track_index, section_index"));
		Meta(TEXT("set_track_condition"),    TEXT("Attach/clear a condition on a track."), TEXT("sequence_path, binding_label?, track_index, condition_class"));
		Meta(TEXT("get_track_condition"),    TEXT("Read a track's condition class."), TEXT("sequence_path, binding_label?, track_index"));
		Meta(TEXT("set_track_row_condition"),TEXT("Attach/clear a condition on a track row."), TEXT("sequence_path, binding_label?, track_index, row_index, condition_class"));
		Meta(TEXT("get_track_row_condition"),TEXT("Read a track row's condition class."), TEXT("sequence_path, binding_label?, track_index, row_index"));
	}

	{
		const FName U(TEXT("control_rig"));
		const auto Meta = [&D, &U](const TCHAR* Action, const TCHAR* Summary, const TCHAR* Params)
		{ D.RegisterToolMetadata({ FName(Action), U, FString(Summary), FString(Params) }); };

		Meta(TEXT("add_rig_control"),          TEXT("Add a control (target_bone snaps offset; parent_control = anim channel; parent = hierarchy parent)."), TEXT("asset_path, control_name, control_type, parent_bone?, target_bone?, shape_name?, shape_color?, shape_scale?, parent?, parent_control?"));
		Meta(TEXT("remove_rig_control"),       TEXT("Remove a control."), TEXT("asset_path, control_name"));
		Meta(TEXT("set_rig_control_properties"),TEXT("Set a control's settings (shape/color/limits/animation_type/etc.)."), TEXT("asset_path, control_name, property_name, property_value"));
		Meta(TEXT("add_rig_null"),             TEXT("Add a null (group) element for control grouping/space switching."), TEXT("asset_path, null_name, parent?"));
		Meta(TEXT("reparent_rig_element"),     TEXT("Reparent a control/null under a new parent (maintains world transform)."), TEXT("asset_path, element_name, new_parent, element_type?, maintain_global?"));
		Meta(TEXT("add_rig_socket"),           TEXT("Add a named socket (attach point for props/weapons)."), TEXT("asset_path, socket_name, parent?, transform?, color?, description?"));
		Meta(TEXT("add_rig_curve"),            TEXT("Declare a named float curve channel (facial/morph driver)."), TEXT("asset_path, curve_name, default_value?"));
		Meta(TEXT("add_rig_control_space"),    TEXT("Register an extra switchable space on a control."), TEXT("asset_path, control_name, space, display_label?"));
		Meta(TEXT("add_rig_control_chain"),    TEXT("Create one FK control per bone (spine/finger/tail)."), TEXT("asset_path, bone_chain, name_template?, control_type?"));
		Meta(TEXT("mirror_rig_control"),       TEXT("Mirror a control L<->R across an axis."), TEXT("asset_path, source_control, mirror_axis?, mirror_name?"));
		Meta(TEXT("set_rig_control_offset_transform"), TEXT("Move a control relative to its parent."), TEXT("asset_path, control_name, transform"));
		Meta(TEXT("set_rig_control_shape_transform"),  TEXT("Move/scale a control's visible gizmo."), TEXT("asset_path, control_name, transform"));
		Meta(TEXT("add_rig_two_bone_ik"),      TEXT("Add a two-bone IK (auto pole vector + exec-chain wiring)."), TEXT("asset_path, root_bone, mid_bone, tip_bone, position_x?, position_y?, effector_control?, pole_control?, pole_vector?"));
		Meta(TEXT("add_rig_aim_constraint"),   TEXT("Add an aim/look-at constraint targeting a bone."), TEXT("asset_path, target_bone, aim_control, primary_axis?, position_x?, position_y?"));
		Meta(TEXT("add_rig_vm_node"),          TEXT("Spawn a RigVM unit node (or If/Select dispatch via unit_struct_path)."), TEXT("asset_path, unit_struct_path, method_name?, position_x?, position_y?, event?, cpp_type?"));
		Meta(TEXT("remove_rig_node"),          TEXT("Remove a RigVM node."), TEXT("asset_path, node_name, event?"));
		Meta(TEXT("connect_rig_pins"),         TEXT("Connect two RigVM pins (NodeName.PinName)."), TEXT("asset_path, source_pin_path, target_pin_path, event?"));
		Meta(TEXT("disconnect_rig_pins"),      TEXT("Break a RigVM pin link."), TEXT("asset_path, source_pin_path, target_pin_path, event?"));
		Meta(TEXT("set_rig_pin_value"),        TEXT("Set a RigVM pin's default value."), TEXT("asset_path, pin_path, pin_value, event?"));
		Meta(TEXT("add_rig_variable"),         TEXT("Add a rig member variable (getter/setter)."), TEXT("asset_path, variable_name, cpp_type, is_getter?, default_value?, position_x?, position_y?"));
		Meta(TEXT("build_rig_logic"),          TEXT("Place+wire a whole solver chain (nodes[]+links[], auto-exec-chain)."), TEXT("asset_path, nodes, links, event?"));
		Meta(TEXT("compile_control_rig"),      TEXT("Compile the RigVM; returns errors/warnings."), TEXT("asset_path"));
		Meta(TEXT("add_rig_function"),         TEXT("Add a reusable RigVM function (mutable = has exec)."), TEXT("asset_path, function_name, mutable?"));
		Meta(TEXT("add_rig_function_pin"),     TEXT("Add an exposed pin to a function interface."), TEXT("asset_path, function_name, pin_name, direction, cpp_type, default_value?"));
		Meta(TEXT("add_rig_function_node"),    TEXT("Instantiate (call) a function from an event graph."), TEXT("asset_path, function_name, event?, position_x?, position_y?"));
		Meta(TEXT("get_control_rig_summary"),  TEXT("Full rig summary (bones/controls/nulls/sockets/curves/nodes)."), TEXT("asset_path"));
		Meta(TEXT("get_rig_control"),          TEXT("Get one control's full details."), TEXT("asset_path, control_name"));
		Meta(TEXT("get_rig_graph_nodes"),      TEXT("Get a graph's nodes+links (CALL FIRST per event)."), TEXT("asset_path, event?"));
		Meta(TEXT("list_rig_vm_node_types"),   TEXT("List available RigVM node types (filterable)."), TEXT("filter?"));
		Meta(TEXT("controlrig_get_anim_layers"),               TEXT("List anim layers on the active Control Rig in Sequencer."), TEXT(""));
		Meta(TEXT("controlrig_add_anim_layer_from_selection"), TEXT("Add an anim layer from the selected controls."), TEXT(""));
		Meta(TEXT("controlrig_delete_anim_layer"),             TEXT("Delete an anim layer by index."), TEXT("index"));
		Meta(TEXT("controlrig_duplicate_anim_layer"),          TEXT("Duplicate an anim layer by index."), TEXT("index"));
		Meta(TEXT("controlrig_merge_anim_layers"),             TEXT("Merge two or more anim layers."), TEXT("indices"));
		Meta(TEXT("controlrig_set_layered_mode"),              TEXT("Toggle a Control Rig track's layered mode."), TEXT("sequence_path, track_index, layered"));
		Meta(TEXT("controlrig_is_layered_control_rig"),        TEXT("Query whether a track's rig is layered."), TEXT("sequence_path, track_index"));
		Meta(TEXT("controlrig_bake_to_control_rig"),           TEXT("Bake the sequence onto a Control Rig class."), TEXT("sequence_path, control_rig_class, reduce_keys?, tolerance?"));
		Meta(TEXT("controlrig_collapse_anim_layers"),          TEXT("Collapse a track's anim layers into one."), TEXT("sequence_path, track_index, reduce_keys?, tolerance?"));
		Meta(TEXT("controlrig_tween_control_rig"),             TEXT("Tween/blend the rig pose by a factor."), TEXT("sequence_path, track_index, tween_value"));
	}

	UE_LOG(LogUECPCinematicsExt, Log, TEXT("Registered %d Cinematics tools (sequencer + control_rig umbrellas)"),
		OwnedToolNames().Num());
}

void FUECPCinematicsExtModule::ShutdownModule()
{
	if (!IUECPCoreModule::IsAvailable()) return;
	IUECPToolDispatcher& D = IUECPCoreModule::Get().GetToolDispatcher();
	for (const FName& N : OwnedToolNames()) D.UnregisterHandler(N);
	IUECPCreateAssetRegistry& Reg = IUECPCoreModule::Get().GetCreateAssetRegistry();
	for (const TCHAR* T : { TEXT("ControlRig"), TEXT("LevelSequence") })
	{
		Reg.UnregisterType(T);
	}
}

IMPLEMENT_MODULE(FUECPCinematicsExtModule, UECPCinematicsExt)
