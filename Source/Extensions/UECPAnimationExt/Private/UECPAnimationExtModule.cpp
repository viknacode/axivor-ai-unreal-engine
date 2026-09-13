// Copyright 2026, BlueprintsLab, All rights reserved

#include "UECPAnimationExtModule.h"

#include "Tools/AnimationTools.h"
#include "Tools/PoseSearchTools.h"
#include "Tools/ChooserTools.h"

#include "UECPCoreModule.h"
#include "Services/IUECPToolDispatcher.h"
#include "Services/IUECPCreateAssetRegistry.h"

#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

DEFINE_LOG_CATEGORY(LogUECPAnimationExt);

namespace
{
	static const TArray<FName>& OwnedToolNames()
	{
		static const TArray<FName> Names = {
			TEXT("add_anim_curve"),
			TEXT("add_anim_notify"),
			TEXT("add_anim_state"),
			TEXT("add_blendspace_sample"),
			TEXT("add_montage_section"),
			TEXT("add_skeleton_socket"),
			TEXT("add_state_transition"),
			TEXT("add_virtual_bone"),
			TEXT("assign_compression_to_animation"),
			TEXT("get_compression_info"),
			TEXT("map_retarget_chain"),
			TEXT("remove_virtual_bone"),
			TEXT("rename_virtual_bone"),
			TEXT("set_transition_rule"),
			TEXT("link_montage_slot"),
			TEXT("set_montage_slot_name"),
			TEXT("add_anim_conduit"),
			TEXT("add_ik_solver"),
			TEXT("add_ik_goal"),
			TEXT("add_retarget_chain"),
			TEXT("set_anim_sequence_settings"),
			TEXT("set_anim_sequence_keys"),
			TEXT("set_anim_transform_curve_keys"),
			TEXT("get_anim_sequence_tracks"),
			TEXT("analyze_anim_motion"),
			TEXT("detect_anim_discontinuities"),
			TEXT("render_anim_sequence_thumbnail"),
			TEXT("create_anim_sequence"),
			TEXT("set_montage_blend_settings"),
			TEXT("add_anim_notify_state"),
			TEXT("add_play_niagara_notify"),
			TEXT("add_play_sound_notify"),
			TEXT("set_transition_blend_settings"),
			TEXT("set_anim_notify_property"),
			TEXT("get_anim_bp_summary"),
			TEXT("get_anim_graph_nodes"),
			TEXT("get_skeleton_bones"),
			TEXT("add_state_machine"),
			TEXT("remove_anim_state"),
			TEXT("set_state_machine_entry_state"),
			TEXT("wire_anim_node_to_output"),
			TEXT("remove_state_transition"),
			TEXT("rename_anim_state"),
			TEXT("create_anim_slot"),
			TEXT("get_blendspace_info"),
			TEXT("set_ik_retarget_root"),
			TEXT("set_retarget_pose"),
			TEXT("set_retarget_chain_settings"),
			TEXT("set_retarget_root_settings"),
			TEXT("edit_retarget_pose_bone"),
			TEXT("auto_align_retarget_pose"),
			TEXT("reset_retarget_pose"),
			TEXT("export_retarget_animation"),
			TEXT("create_anim_layer_interface"),
			TEXT("implement_anim_layer"),
			TEXT("add_anim_layer_node"),
			TEXT("get_anim_sequence_info"),
			TEXT("get_montage_summary"),
			TEXT("get_ik_rig_summary"),
			TEXT("list_anim_slots"),
			TEXT("add_layered_blend_per_bone"),
			TEXT("add_saved_pose"),
			TEXT("use_cached_pose"),
			TEXT("add_blend_by_bool"),
			TEXT("remove_anim_notify"),
			TEXT("add_blend_by_int"),
			TEXT("set_blendspace_axis"),
			TEXT("add_anim_notify_track"),
			TEXT("set_anim_curve_keys"),
			TEXT("get_retargeter_summary"),
			TEXT("create_anim_blueprint_from_parent"),
			TEXT("remove_anim_curve"),
			TEXT("add_sub_anim_instance"),
			TEXT("remove_blendspace_sample"),
			TEXT("remove_montage_section"),
			TEXT("rename_montage_section"),
			TEXT("move_montage_section"),
			TEXT("move_anim_notify"),
			TEXT("set_anim_notify_duration"),
			TEXT("crop_animation"),
			TEXT("move_anim_sync_marker"),
			TEXT("rename_anim_sync_marker"),
			TEXT("list_anim_curves"),
			TEXT("remove_anim_curve_key"),
			TEXT("remove_skeleton_socket"),
			TEXT("rename_skeleton_socket"),
			TEXT("set_skeleton_socket_transform"),
			TEXT("set_skeleton_socket_parent"),
			TEXT("move_blendspace_sample"),
			TEXT("set_blendspace_sample_animation"),
			TEXT("set_blendspace_sample_rate_scale"),
			TEXT("set_anim_state_animation"),
			TEXT("add_blendspace_player"),
			TEXT("set_blendspace_player_asset"),
			TEXT("compile_anim_blueprint"),
			TEXT("add_state_alias"),
			TEXT("set_transition_priority"),
			TEXT("set_anim_node_position"),
			TEXT("set_layered_blend_per_bone_filter"),
			TEXT("set_transition_options"),
			TEXT("set_state_options"),
			TEXT("get_skeleton_sockets"),
			TEXT("remove_ik_goal"),
			TEXT("remove_retarget_chain"),
			TEXT("add_modify_bone"),
			TEXT("add_copy_bone"),
			TEXT("add_look_at"),
			TEXT("add_two_bone_ik"),
			TEXT("add_apply_additive"),
			TEXT("add_make_dynamic_additive"),
			TEXT("add_inertialization"),
			TEXT("add_blend_by_enum"),
			TEXT("add_sequence_evaluator"),
			TEXT("add_random_player"),
			TEXT("list_virtual_bones"),
			TEXT("get_skeleton_hierarchy"),
			TEXT("add_slot_node"),
			TEXT("add_two_way_blend"),
			TEXT("add_apply_mesh_space_additive"),
			TEXT("add_copy_pose_from_mesh"),
			TEXT("add_rotate_root_bone"),
			TEXT("add_aim_offset_player"),
			TEXT("add_mirror"),
			TEXT("add_local_to_component_space"),
			TEXT("add_component_to_local_space"),
			TEXT("add_mesh_ref_pose"),
			TEXT("add_local_ref_pose"),
			TEXT("add_identity_pose"),
			TEXT("add_spring_bone"),
			TEXT("add_rigid_body"),
			TEXT("add_fabrik"),
			TEXT("add_ccdik"),
			TEXT("add_leg_ik"),
			TEXT("add_pose_by_name"),
			TEXT("add_bone_driven_controller"),
			TEXT("add_blend_bone_by_channel"),
			TEXT("add_motion_matching_node"),
			TEXT("connect_anim_nodes"),
			TEXT("build_anim_chain"),
			TEXT("add_control_rig_node"),
			TEXT("get_anim_montage_sections"),
			TEXT("set_montage_section_link"),
			TEXT("create_sync_group"),
			TEXT("add_blend_profile"),
			TEXT("add_anim_sync_marker"),
			TEXT("remove_anim_sync_marker"),
			TEXT("list_anim_sync_markers"),
			TEXT("get_anim_state_machines"),
			TEXT("set_anim_node_property"),
			TEXT("add_motion_warping_window"),
			TEXT("get_motion_warping_windows"),
			TEXT("remove_motion_warping_window"),

			// Composite template tools
			TEXT("create_locomotion_state_machine"),
			TEXT("create_montage_from_sequence"),
			TEXT("make_additive"),
			TEXT("retarget_setup"),
			TEXT("setup_motion_matching"),
			TEXT("setup_foot_ik"),

			TEXT("add_animation_to_database"),
			TEXT("add_pose_search_channel"),
			TEXT("build_pose_search_database"),
			TEXT("get_pose_search_summary"),
			TEXT("set_pose_search_channel_property"),
			TEXT("set_pose_search_schema_skeleton"),
			TEXT("remove_animation_from_database"),
			TEXT("set_pose_search_schema_property"),
			TEXT("remove_pose_search_channel"),
			TEXT("set_animation_database_entry_property"),
			TEXT("set_pose_search_database_schema"),
			TEXT("get_pose_search_channel_properties"),
			TEXT("add_database_to_normalization_set"),
			TEXT("set_schema_mirror_data_table"),
			TEXT("duplicate_pose_search_channel"),
			TEXT("reorder_pose_search_channels"),
			TEXT("list_pose_search_channel_types"),

			TEXT("add_chooser_column"),
			TEXT("add_chooser_row"),
			TEXT("get_chooser_summary"),
			TEXT("set_chooser_row_value"),
			TEXT("set_chooser_output_type"),
			TEXT("remove_chooser_row"),
			TEXT("remove_chooser_column"),
			TEXT("set_chooser_column_property"),
			TEXT("set_chooser_fallback"),
			TEXT("duplicate_chooser_row"),
			TEXT("reorder_chooser_rows"),
			TEXT("get_chooser_row_values"),
			TEXT("get_chooser_column_properties"),
			TEXT("bulk_set_chooser_rows"),
			TEXT("rename_chooser_column"),
			TEXT("reorder_chooser_columns"),
			TEXT("duplicate_chooser_column"),
			TEXT("set_chooser_context_data"),
		};
		return Names;
	}

	// Some handlers report failure only inside the result JSON ("success":false + "error")
	// without touching OutError; surface that as a real failure so the dispatcher never
	// reports bSuccess=true for a failed tool.
	static void PromoteJsonFailure(FUECPToolResult& R)
	{
		if (!R.ErrorMessage.IsEmpty() || R.ResultJson.IsEmpty()) return;
		if (!R.ResultJson.Contains(TEXT("\"success\""))) return;

		TSharedPtr<FJsonObject> Obj;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(R.ResultJson);
		if (!FJsonSerializer::Deserialize(Reader, Obj) || !Obj.IsValid()) return;

		bool bSuccess = true;
		if (Obj->TryGetBoolField(TEXT("success"), bSuccess) && !bSuccess)
		{
			FString Err;
			if (!Obj->TryGetStringField(TEXT("error"), Err) || Err.IsEmpty())
				Err = TEXT("Tool reported success=false without an error message; see result JSON.");
			R.ErrorMessage = Err;
		}
	}

	static auto MakeHandler(TFunction<void(const TSharedPtr<FJsonObject>&, FString&, FString&)> Fn)
	{
		return [Fn = MoveTemp(Fn)](const TSharedPtr<FJsonObject>& Args) -> FUECPToolResult
		{
			FUECPToolResult R;
			Fn(Args, R.ResultJson, R.ErrorMessage);
			PromoteJsonFailure(R);
			R.bSuccess = R.ErrorMessage.IsEmpty();
			return R;
		};
	}
}

void FUECPAnimationExtModule::StartupModule()
{
	if (!IUECPCoreModule::IsAvailable()) return;
	IUECPToolDispatcher& D = IUECPCoreModule::Get().GetToolDispatcher();

	D.RegisterHandler(TEXT("add_anim_curve"),                    MakeHandler(AnimationTools::HandleAddAnimCurveFromArgs));
	D.RegisterHandler(TEXT("add_anim_notify"),                   MakeHandler(AnimationTools::HandleAddAnimNotifyFromArgs));
	D.RegisterHandler(TEXT("add_anim_state"),                    MakeHandler(AnimationTools::HandleAddAnimStateFromArgs));
	D.RegisterHandler(TEXT("add_blendspace_sample"),             MakeHandler(AnimationTools::HandleAddBlendspaceSampleFromArgs));
	D.RegisterHandler(TEXT("add_montage_section"),               MakeHandler(AnimationTools::HandleAddMontageSectionFromArgs));
	D.RegisterHandler(TEXT("add_skeleton_socket"),               MakeHandler(AnimationTools::HandleAddSkeletonSocketFromArgs));
	D.RegisterHandler(TEXT("add_state_transition"),              MakeHandler(AnimationTools::HandleAddStateTransitionFromArgs));
	D.RegisterHandler(TEXT("add_virtual_bone"),                  MakeHandler(AnimationTools::HandleAddVirtualBoneFromArgs));
	D.RegisterHandler(TEXT("assign_compression_to_animation"),   MakeHandler(AnimationTools::HandleAssignCompressionToAnimationFromArgs));
	D.RegisterHandler(TEXT("get_compression_info"),              MakeHandler(AnimationTools::HandleGetCompressionInfoFromArgs));
	D.RegisterHandler(TEXT("map_retarget_chain"),                MakeHandler(AnimationTools::HandleMapRetargetChainFromArgs));
	D.RegisterHandler(TEXT("remove_virtual_bone"),               MakeHandler(AnimationTools::HandleRemoveVirtualBoneFromArgs));
	D.RegisterHandler(TEXT("rename_virtual_bone"),               MakeHandler(AnimationTools::HandleRenameVirtualBoneFromArgs));
	D.RegisterHandler(TEXT("set_transition_rule"),               MakeHandler(AnimationTools::HandleSetTransitionRuleFromArgs));
	D.RegisterHandler(TEXT("link_montage_slot"),                 MakeHandler(AnimationTools::HandleLinkMontageSlotFromArgs));
	D.RegisterHandler(TEXT("set_montage_slot_name"),             MakeHandler(AnimationTools::HandleSetMontageSlotNameFromArgs));
	D.RegisterHandler(TEXT("add_anim_conduit"),                  MakeHandler(AnimationTools::HandleAddAnimConduitFromArgs));
	D.RegisterHandler(TEXT("add_ik_solver"),                     MakeHandler(AnimationTools::HandleAddIKSolverFromArgs));
	D.RegisterHandler(TEXT("add_ik_goal"),                       MakeHandler(AnimationTools::HandleAddIKGoalFromArgs));
	D.RegisterHandler(TEXT("add_retarget_chain"),                MakeHandler(AnimationTools::HandleAddRetargetChainFromArgs));
	D.RegisterHandler(TEXT("set_anim_sequence_settings"),        MakeHandler(AnimationTools::HandleSetAnimSequenceSettingsFromArgs));
	D.RegisterHandler(TEXT("set_anim_sequence_keys"),            MakeHandler(AnimationTools::HandleSetAnimSequenceKeysFromArgs));
	D.RegisterHandler(TEXT("set_anim_transform_curve_keys"),     MakeHandler(AnimationTools::HandleSetAnimTransformCurveKeysFromArgs));
	D.RegisterHandler(TEXT("get_anim_sequence_tracks"),          MakeHandler(AnimationTools::HandleGetAnimSequenceTracksFromArgs));
	D.RegisterHandler(TEXT("analyze_anim_motion"),               MakeHandler(AnimationTools::HandleAnalyzeAnimMotionFromArgs));
	D.RegisterHandler(TEXT("detect_anim_discontinuities"),       MakeHandler(AnimationTools::HandleDetectAnimDiscontinuitiesFromArgs));
	D.RegisterHandler(TEXT("render_anim_sequence_thumbnail"),    MakeHandler(AnimationTools::HandleRenderAnimSequenceThumbnailFromArgs));
	D.RegisterHandler(TEXT("create_anim_sequence"),              MakeHandler(AnimationTools::HandleCreateAnimSequenceFromArgs));
	D.RegisterHandler(TEXT("set_montage_blend_settings"),        MakeHandler(AnimationTools::HandleSetMontageBlendSettingsFromArgs));
	D.RegisterHandler(TEXT("add_anim_notify_state"),             MakeHandler(AnimationTools::HandleAddAnimNotifyStateFromArgs));
	D.RegisterHandler(TEXT("add_play_niagara_notify"),           MakeHandler(AnimationTools::HandleAddPlayNiagaraNotifyFromArgs));
	D.RegisterHandler(TEXT("add_play_sound_notify"),             MakeHandler(AnimationTools::HandleAddPlaySoundNotifyFromArgs));
	D.RegisterHandler(TEXT("set_transition_blend_settings"),     MakeHandler(AnimationTools::HandleSetTransitionBlendSettingsFromArgs));
	D.RegisterHandler(TEXT("set_anim_notify_property"),          MakeHandler(AnimationTools::HandleSetAnimNotifyPropertyFromArgs));
	D.RegisterHandler(TEXT("get_anim_bp_summary"),               MakeHandler(AnimationTools::HandleGetAnimBpSummaryFromArgs));
	D.RegisterHandler(TEXT("get_anim_graph_nodes"),              MakeHandler(AnimationTools::HandleGetAnimGraphNodesFromArgs));
	D.RegisterHandler(TEXT("get_skeleton_bones"),                MakeHandler(AnimationTools::HandleGetSkeletonBonesFromArgs));
	D.RegisterHandler(TEXT("add_state_machine"),                 MakeHandler(AnimationTools::HandleAddStateMachineFromArgs));
	D.RegisterHandler(TEXT("remove_anim_state"),                 MakeHandler(AnimationTools::HandleRemoveAnimStateFromArgs));
	D.RegisterHandler(TEXT("set_state_machine_entry_state"),     MakeHandler(AnimationTools::HandleSetStateMachineEntryStateFromArgs));
	D.RegisterHandler(TEXT("wire_anim_node_to_output"),          MakeHandler(AnimationTools::HandleWireAnimNodeToOutputFromArgs));
	D.RegisterHandler(TEXT("remove_state_transition"),           MakeHandler(AnimationTools::HandleRemoveStateTransitionFromArgs));
	D.RegisterHandler(TEXT("rename_anim_state"),                 MakeHandler(AnimationTools::HandleRenameAnimStateFromArgs));
	D.RegisterHandler(TEXT("create_anim_slot"),                  MakeHandler(AnimationTools::HandleCreateAnimSlotFromArgs));
	D.RegisterHandler(TEXT("get_blendspace_info"),               MakeHandler(AnimationTools::HandleGetBlendspaceInfoFromArgs));
	D.RegisterHandler(TEXT("set_ik_retarget_root"),              MakeHandler(AnimationTools::HandleSetIKRetargetRootFromArgs));
	D.RegisterHandler(TEXT("set_retarget_pose"),                 MakeHandler(AnimationTools::HandleSetRetargetPoseFromArgs));
	D.RegisterHandler(TEXT("set_retarget_chain_settings"),       MakeHandler(AnimationTools::HandleSetRetargetChainSettingsFromArgs));
	D.RegisterHandler(TEXT("set_retarget_root_settings"),        MakeHandler(AnimationTools::HandleSetRetargetRootSettingsFromArgs));
	D.RegisterHandler(TEXT("edit_retarget_pose_bone"),           MakeHandler(AnimationTools::HandleEditRetargetPoseBoneFromArgs));
	D.RegisterHandler(TEXT("auto_align_retarget_pose"),          MakeHandler(AnimationTools::HandleAutoAlignRetargetPoseFromArgs));
	D.RegisterHandler(TEXT("reset_retarget_pose"),               MakeHandler(AnimationTools::HandleResetRetargetPoseFromArgs));
	D.RegisterHandler(TEXT("export_retarget_animation"),         MakeHandler(AnimationTools::HandleExportRetargetAnimationFromArgs));
	D.RegisterHandler(TEXT("create_anim_layer_interface"),       MakeHandler(AnimationTools::HandleCreateAnimLayerInterfaceFromArgs));
	D.RegisterHandler(TEXT("implement_anim_layer"),              MakeHandler(AnimationTools::HandleImplementAnimLayerFromArgs));
	D.RegisterHandler(TEXT("add_anim_layer_node"),               MakeHandler(AnimationTools::HandleAddAnimLayerNodeFromArgs));
	D.RegisterHandler(TEXT("get_anim_sequence_info"),            MakeHandler(AnimationTools::HandleGetAnimSequenceInfoFromArgs));
	D.RegisterHandler(TEXT("get_montage_summary"),               MakeHandler(AnimationTools::HandleGetMontageSummaryFromArgs));
	D.RegisterHandler(TEXT("get_ik_rig_summary"),                MakeHandler(AnimationTools::HandleGetIKRigSummaryFromArgs));
	D.RegisterHandler(TEXT("list_anim_slots"),                   MakeHandler(AnimationTools::HandleListAnimSlotsFromArgs));
	D.RegisterHandler(TEXT("add_layered_blend_per_bone"),        MakeHandler(AnimationTools::HandleAddLayeredBlendPerBoneFromArgs));
	D.RegisterHandler(TEXT("add_saved_pose"),                    MakeHandler(AnimationTools::HandleAddSavedPoseFromArgs));
	D.RegisterHandler(TEXT("use_cached_pose"),                   MakeHandler(AnimationTools::HandleUseCachedPoseFromArgs));
	D.RegisterHandler(TEXT("add_blend_by_bool"),                 MakeHandler(AnimationTools::HandleAddBlendByBoolFromArgs));
	D.RegisterHandler(TEXT("remove_anim_notify"),                MakeHandler(AnimationTools::HandleRemoveAnimNotifyFromArgs));
	D.RegisterHandler(TEXT("add_blend_by_int"),                  MakeHandler(AnimationTools::HandleAddBlendByIntFromArgs));
	D.RegisterHandler(TEXT("set_blendspace_axis"),               MakeHandler(AnimationTools::HandleSetBlendspaceAxisFromArgs));
	D.RegisterHandler(TEXT("add_anim_notify_track"),             MakeHandler(AnimationTools::HandleAddAnimNotifyTrackFromArgs));
	D.RegisterHandler(TEXT("set_anim_curve_keys"),               MakeHandler(AnimationTools::HandleSetAnimCurveKeyFromArgs));
	D.RegisterHandler(TEXT("get_retargeter_summary"),            MakeHandler(AnimationTools::HandleGetRetargeterSummaryFromArgs));
	D.RegisterHandler(TEXT("create_anim_blueprint_from_parent"), MakeHandler(AnimationTools::HandleCreateAnimBlueprintFromParentFromArgs));
	D.RegisterHandler(TEXT("remove_anim_curve"),                 MakeHandler(AnimationTools::HandleRemoveAnimCurveFromArgs));
	D.RegisterHandler(TEXT("add_sub_anim_instance"),             MakeHandler(AnimationTools::HandleAddSubAnimInstanceFromArgs));
	D.RegisterHandler(TEXT("remove_blendspace_sample"),          MakeHandler(AnimationTools::HandleRemoveBlendspaceSampleFromArgs));
	D.RegisterHandler(TEXT("remove_montage_section"),            MakeHandler(AnimationTools::HandleRemoveMontageSectionFromArgs));
	D.RegisterHandler(TEXT("rename_montage_section"),            MakeHandler(AnimationTools::HandleRenameMontageSectionFromArgs));
	D.RegisterHandler(TEXT("move_montage_section"),              MakeHandler(AnimationTools::HandleMoveMontageSectionFromArgs));
	D.RegisterHandler(TEXT("move_anim_notify"),                  MakeHandler(AnimationTools::HandleMoveAnimNotifyFromArgs));
	D.RegisterHandler(TEXT("set_anim_notify_duration"),          MakeHandler(AnimationTools::HandleSetAnimNotifyDurationFromArgs));
	D.RegisterHandler(TEXT("crop_animation"),                    MakeHandler(AnimationTools::HandleCropAnimationFromArgs));
	D.RegisterHandler(TEXT("move_anim_sync_marker"),             MakeHandler(AnimationTools::HandleMoveAnimSyncMarkerFromArgs));
	D.RegisterHandler(TEXT("rename_anim_sync_marker"),           MakeHandler(AnimationTools::HandleRenameAnimSyncMarkerFromArgs));
	D.RegisterHandler(TEXT("list_anim_curves"),                  MakeHandler(AnimationTools::HandleListAnimCurvesFromArgs));
	D.RegisterHandler(TEXT("remove_anim_curve_key"),             MakeHandler(AnimationTools::HandleRemoveAnimCurveKeyFromArgs));
	D.RegisterHandler(TEXT("remove_skeleton_socket"),            MakeHandler(AnimationTools::HandleRemoveSkeletonSocketFromArgs));
	D.RegisterHandler(TEXT("rename_skeleton_socket"),            MakeHandler(AnimationTools::HandleRenameSkeletonSocketFromArgs));
	D.RegisterHandler(TEXT("set_skeleton_socket_transform"),     MakeHandler(AnimationTools::HandleSetSkeletonSocketTransformFromArgs));
	D.RegisterHandler(TEXT("set_skeleton_socket_parent"),        MakeHandler(AnimationTools::HandleSetSkeletonSocketParentFromArgs));
	D.RegisterHandler(TEXT("move_blendspace_sample"),            MakeHandler(AnimationTools::HandleMoveBlendspaceSampleFromArgs));
	D.RegisterHandler(TEXT("set_blendspace_sample_animation"),   MakeHandler(AnimationTools::HandleSetBlendspaceSampleAnimationFromArgs));
	D.RegisterHandler(TEXT("set_blendspace_sample_rate_scale"),  MakeHandler(AnimationTools::HandleSetBlendspaceSampleRateScaleFromArgs));
	D.RegisterHandler(TEXT("set_anim_state_animation"),          MakeHandler(AnimationTools::HandleSetAnimStateAnimationFromArgs));
	D.RegisterHandler(TEXT("add_blendspace_player"),             MakeHandler(AnimationTools::HandleAddBlendSpacePlayerFromArgs));
	D.RegisterHandler(TEXT("set_blendspace_player_asset"),       MakeHandler(AnimationTools::HandleSetBlendSpacePlayerAssetFromArgs));
	D.RegisterHandler(TEXT("add_state_alias"),                   MakeHandler(AnimationTools::HandleAddStateAliasFromArgs));
	D.RegisterHandler(TEXT("set_transition_priority"),           MakeHandler(AnimationTools::HandleSetTransitionPriorityFromArgs));
	D.RegisterHandler(TEXT("set_anim_node_position"),            MakeHandler(AnimationTools::HandleSetAnimNodePositionFromArgs));
	D.RegisterHandler(TEXT("set_layered_blend_per_bone_filter"), MakeHandler(AnimationTools::HandleSetLayeredBlendPerBoneFilterFromArgs));
	D.RegisterHandler(TEXT("set_transition_options"),            MakeHandler(AnimationTools::HandleSetTransitionOptionsFromArgs));
	D.RegisterHandler(TEXT("set_state_options"),                 MakeHandler(AnimationTools::HandleSetStateOptionsFromArgs));
	D.RegisterHandler(TEXT("get_skeleton_sockets"),              MakeHandler(AnimationTools::HandleGetSkeletonSocketsFromArgs));
	D.RegisterHandler(TEXT("remove_ik_goal"),                    MakeHandler(AnimationTools::HandleRemoveIKGoalFromArgs));
	D.RegisterHandler(TEXT("remove_retarget_chain"),             MakeHandler(AnimationTools::HandleRemoveRetargetChainFromArgs));
	D.RegisterHandler(TEXT("add_modify_bone"),                   MakeHandler(AnimationTools::HandleAddModifyBoneFromArgs));
	D.RegisterHandler(TEXT("add_copy_bone"),                     MakeHandler(AnimationTools::HandleAddCopyBoneFromArgs));
	D.RegisterHandler(TEXT("add_look_at"),                       MakeHandler(AnimationTools::HandleAddLookAtFromArgs));
	D.RegisterHandler(TEXT("add_two_bone_ik"),                   MakeHandler(AnimationTools::HandleAddTwoBoneIKFromArgs));
	D.RegisterHandler(TEXT("add_apply_additive"),                MakeHandler(AnimationTools::HandleAddApplyAdditiveFromArgs));
	D.RegisterHandler(TEXT("add_make_dynamic_additive"),         MakeHandler(AnimationTools::HandleAddMakeDynamicAdditiveFromArgs));
	D.RegisterHandler(TEXT("add_inertialization"),               MakeHandler(AnimationTools::HandleAddInertializationFromArgs));
	D.RegisterHandler(TEXT("add_blend_by_enum"),                 MakeHandler(AnimationTools::HandleAddBlendByEnumFromArgs));
	D.RegisterHandler(TEXT("add_sequence_evaluator"),            MakeHandler(AnimationTools::HandleAddSequenceEvaluatorFromArgs));
	D.RegisterHandler(TEXT("add_random_player"),                 MakeHandler(AnimationTools::HandleAddRandomPlayerFromArgs));
	D.RegisterHandler(TEXT("list_virtual_bones"),                MakeHandler(AnimationTools::HandleListVirtualBonesFromArgs));
	D.RegisterHandler(TEXT("get_skeleton_hierarchy"),            MakeHandler(AnimationTools::HandleGetSkeletonHierarchyFromArgs));
	D.RegisterHandler(TEXT("add_slot_node"),                     MakeHandler(AnimationTools::HandleAddSlotNodeFromArgs));
	D.RegisterHandler(TEXT("add_two_way_blend"),                 MakeHandler(AnimationTools::HandleAddTwoWayBlendFromArgs));
	D.RegisterHandler(TEXT("add_apply_mesh_space_additive"),     MakeHandler(AnimationTools::HandleAddApplyMeshSpaceAdditiveFromArgs));
	D.RegisterHandler(TEXT("add_copy_pose_from_mesh"),           MakeHandler(AnimationTools::HandleAddCopyPoseFromMeshFromArgs));
	D.RegisterHandler(TEXT("add_rotate_root_bone"),              MakeHandler(AnimationTools::HandleAddRotateRootBoneFromArgs));
	D.RegisterHandler(TEXT("add_aim_offset_player"),             MakeHandler(AnimationTools::HandleAddAimOffsetPlayerFromArgs));
	D.RegisterHandler(TEXT("add_mirror"),                        MakeHandler(AnimationTools::HandleAddMirrorFromArgs));
	D.RegisterHandler(TEXT("add_local_to_component_space"),      MakeHandler(AnimationTools::HandleAddLocalToComponentSpaceFromArgs));
	D.RegisterHandler(TEXT("add_component_to_local_space"),      MakeHandler(AnimationTools::HandleAddComponentToLocalSpaceFromArgs));
	D.RegisterHandler(TEXT("add_mesh_ref_pose"),                 MakeHandler(AnimationTools::HandleAddMeshRefPoseFromArgs));
	D.RegisterHandler(TEXT("add_local_ref_pose"),                MakeHandler(AnimationTools::HandleAddLocalRefPoseFromArgs));
	D.RegisterHandler(TEXT("add_identity_pose"),                 MakeHandler(AnimationTools::HandleAddIdentityPoseFromArgs));
	D.RegisterHandler(TEXT("add_spring_bone"),                   MakeHandler(AnimationTools::HandleAddSpringBoneFromArgs));
	D.RegisterHandler(TEXT("add_rigid_body"),                    MakeHandler(AnimationTools::HandleAddRigidBodyFromArgs));
	D.RegisterHandler(TEXT("add_fabrik"),                        MakeHandler(AnimationTools::HandleAddFabrikFromArgs));
	D.RegisterHandler(TEXT("add_ccdik"),                         MakeHandler(AnimationTools::HandleAddCCDIKFromArgs));
	D.RegisterHandler(TEXT("add_leg_ik"),                        MakeHandler(AnimationTools::HandleAddLegIKFromArgs));
	D.RegisterHandler(TEXT("add_pose_by_name"),                  MakeHandler(AnimationTools::HandleAddPoseByNameFromArgs));
	D.RegisterHandler(TEXT("add_bone_driven_controller"),        MakeHandler(AnimationTools::HandleAddBoneDrivenControllerFromArgs));
	D.RegisterHandler(TEXT("add_blend_bone_by_channel"),         MakeHandler(AnimationTools::HandleAddBlendBoneByChannelFromArgs));
	D.RegisterHandler(TEXT("add_motion_matching_node"),          MakeHandler(AnimationTools::HandleAddMotionMatchingNodeFromArgs));
	D.RegisterHandler(TEXT("connect_anim_nodes"),                MakeHandler(AnimationTools::HandleConnectAnimNodesFromArgs));
	D.RegisterHandler(TEXT("build_anim_chain"),                  MakeHandler(AnimationTools::HandleBuildAnimChainFromArgs));
	D.RegisterHandler(TEXT("add_control_rig_node"),              MakeHandler(AnimationTools::HandleAddControlRigNodeFromArgs));
	D.RegisterHandler(TEXT("get_anim_montage_sections"),         MakeHandler(AnimationTools::HandleGetAnimMontageSectionsFromArgs));
	D.RegisterHandler(TEXT("set_montage_section_link"),          MakeHandler(AnimationTools::HandleSetMontageSectionLinkFromArgs));
	D.RegisterHandler(TEXT("create_sync_group"),                 MakeHandler(AnimationTools::HandleCreateSyncGroupFromArgs));
	D.RegisterHandler(TEXT("add_blend_profile"),                 MakeHandler(AnimationTools::HandleAddBlendProfileFromArgs));
	D.RegisterHandler(TEXT("add_anim_sync_marker"),              MakeHandler(AnimationTools::HandleAddAnimSyncMarkerFromArgs));
	D.RegisterHandler(TEXT("remove_anim_sync_marker"),           MakeHandler(AnimationTools::HandleRemoveAnimSyncMarkerFromArgs));
	D.RegisterHandler(TEXT("list_anim_sync_markers"),            MakeHandler(AnimationTools::HandleListAnimSyncMarkersFromArgs));
	D.RegisterHandler(TEXT("get_anim_state_machines"),           MakeHandler(AnimationTools::HandleGetAnimStateMachinesFromArgs));
	D.RegisterHandler(TEXT("set_anim_node_property"),            MakeHandler(AnimationTools::HandleSetAnimNodePropertyFromArgs));
	D.RegisterHandler(TEXT("compile_anim_blueprint"),            MakeHandler(AnimationTools::HandleCompileAnimBlueprintFromArgs));
	D.RegisterHandler(TEXT("add_motion_warping_window"),         MakeHandler(AnimationTools::HandleAddMotionWarpingWindowFromArgs));
	D.RegisterHandler(TEXT("get_motion_warping_windows"),        MakeHandler(AnimationTools::HandleGetMotionWarpingWindowsFromArgs));
	D.RegisterHandler(TEXT("remove_motion_warping_window"),      MakeHandler(AnimationTools::HandleRemoveMotionWarpingWindowFromArgs));

	// Composite template tools (compose the handlers above; every sub-step reported in steps[]).
	D.RegisterHandler(TEXT("create_locomotion_state_machine"),   MakeHandler(AnimationTools::HandleCreateLocomotionStateMachineFromArgs));
	D.RegisterHandler(TEXT("create_montage_from_sequence"),      MakeHandler(AnimationTools::HandleCreateMontageFromSequenceFromArgs));
	D.RegisterHandler(TEXT("make_additive"),                     MakeHandler(AnimationTools::HandleMakeAdditiveFromArgs));
	D.RegisterHandler(TEXT("retarget_setup"),                    MakeHandler(AnimationTools::HandleRetargetSetupFromArgs));
	D.RegisterHandler(TEXT("setup_motion_matching"),             MakeHandler(AnimationTools::HandleSetupMotionMatchingFromArgs));
	D.RegisterHandler(TEXT("setup_foot_ik"),                     MakeHandler(AnimationTools::HandleSetupFootIKFromArgs));

	D.RegisterHandler(TEXT("add_animation_to_database"),                  MakeHandler(PoseSearchTools::HandleAddAnimationToDatabaseFromArgs));
	D.RegisterHandler(TEXT("add_pose_search_channel"),                    MakeHandler(PoseSearchTools::HandleAddPoseSearchChannelFromArgs));
	D.RegisterHandler(TEXT("build_pose_search_database"),                 MakeHandler(PoseSearchTools::HandleBuildPoseSearchDatabaseFromArgs));
	D.RegisterHandler(TEXT("get_pose_search_summary"),                    MakeHandler(PoseSearchTools::HandleGetPoseSearchSummaryFromArgs));
	D.RegisterHandler(TEXT("set_pose_search_channel_property"),           MakeHandler(PoseSearchTools::HandleSetPoseSearchChannelPropertyFromArgs));
	D.RegisterHandler(TEXT("set_pose_search_schema_skeleton"),            MakeHandler(PoseSearchTools::HandleSetPoseSearchSchemaSkeletonFromArgs));
	D.RegisterHandler(TEXT("remove_animation_from_database"),             MakeHandler(PoseSearchTools::HandleRemoveAnimationFromDatabaseFromArgs));
	D.RegisterHandler(TEXT("set_pose_search_schema_property"),            MakeHandler(PoseSearchTools::HandleSetPoseSearchSchemaPropertyFromArgs));
	D.RegisterHandler(TEXT("remove_pose_search_channel"),                 MakeHandler(PoseSearchTools::HandleRemovePoseSearchChannelFromArgs));
	D.RegisterHandler(TEXT("set_animation_database_entry_property"),      MakeHandler(PoseSearchTools::HandleSetAnimationDatabaseEntryPropertyFromArgs));
	D.RegisterHandler(TEXT("set_pose_search_database_schema"),            MakeHandler(PoseSearchTools::HandleSetPoseSearchDatabaseSchemaFromArgs));
	D.RegisterHandler(TEXT("get_pose_search_channel_properties"),         MakeHandler(PoseSearchTools::HandleGetPoseSearchChannelPropertiesFromArgs));
	D.RegisterHandler(TEXT("add_database_to_normalization_set"),          MakeHandler(PoseSearchTools::HandleAddDatabaseToNormalizationSetFromArgs));
	D.RegisterHandler(TEXT("set_schema_mirror_data_table"),               MakeHandler(PoseSearchTools::HandleSetSchemaMirrorDataTableFromArgs));
	D.RegisterHandler(TEXT("duplicate_pose_search_channel"),              MakeHandler(PoseSearchTools::HandleDuplicatePoseSearchChannelFromArgs));
	D.RegisterHandler(TEXT("reorder_pose_search_channels"),               MakeHandler(PoseSearchTools::HandleReorderPoseSearchChannelsFromArgs));
	D.RegisterHandler(TEXT("list_pose_search_channel_types"),             MakeHandler(PoseSearchTools::HandleListPoseSearchChannelTypesFromArgs));

	D.RegisterHandler(TEXT("add_chooser_column"),            MakeHandler(ChooserTools::HandleAddChooserColumnFromArgs));
	D.RegisterHandler(TEXT("add_chooser_row"),               MakeHandler(ChooserTools::HandleAddChooserRowFromArgs));
	D.RegisterHandler(TEXT("get_chooser_summary"),           MakeHandler(ChooserTools::HandleGetChooserSummaryFromArgs));
	D.RegisterHandler(TEXT("set_chooser_row_value"),         MakeHandler(ChooserTools::HandleSetChooserRowValueFromArgs));
	D.RegisterHandler(TEXT("set_chooser_output_type"),       MakeHandler(ChooserTools::HandleSetChooserOutputTypeFromArgs));
	D.RegisterHandler(TEXT("remove_chooser_row"),            MakeHandler(ChooserTools::HandleRemoveChooserRowFromArgs));
	D.RegisterHandler(TEXT("remove_chooser_column"),         MakeHandler(ChooserTools::HandleRemoveChooserColumnFromArgs));
	D.RegisterHandler(TEXT("set_chooser_column_property"),   MakeHandler(ChooserTools::HandleSetChooserColumnPropertyFromArgs));
	D.RegisterHandler(TEXT("set_chooser_fallback"),          MakeHandler(ChooserTools::HandleSetChooserFallbackFromArgs));
	D.RegisterHandler(TEXT("duplicate_chooser_row"),         MakeHandler(ChooserTools::HandleDuplicateChooserRowFromArgs));
	D.RegisterHandler(TEXT("reorder_chooser_rows"),          MakeHandler(ChooserTools::HandleReorderChooserRowsFromArgs));
	D.RegisterHandler(TEXT("get_chooser_row_values"),        MakeHandler(ChooserTools::HandleGetChooserRowValuesFromArgs));
	D.RegisterHandler(TEXT("get_chooser_column_properties"), MakeHandler(ChooserTools::HandleGetChooserColumnPropertiesFromArgs));
	D.RegisterHandler(TEXT("bulk_set_chooser_rows"),         MakeHandler(ChooserTools::HandleBulkSetChooserRowsFromArgs));
	D.RegisterHandler(TEXT("rename_chooser_column"),         MakeHandler(ChooserTools::HandleRenameChooserColumnFromArgs));
	D.RegisterHandler(TEXT("reorder_chooser_columns"),       MakeHandler(ChooserTools::HandleReorderChooserColumnsFromArgs));
	D.RegisterHandler(TEXT("duplicate_chooser_column"),      MakeHandler(ChooserTools::HandleDuplicateChooserColumnFromArgs));
	D.RegisterHandler(TEXT("set_chooser_context_data"),      MakeHandler(ChooserTools::HandleSetChooserContextDataFromArgs));

	{
		IUECPCreateAssetRegistry& Reg = IUECPCoreModule::Get().GetCreateAssetRegistry();
		const FName ExtId(TEXT("Animation"));

		auto RejectorFor = [](const FString& AssetType, const FString& Use) -> IUECPCreateAssetRegistry::FFactoryFn
		{
			return [AssetType, Use](const FString& , const FString& ,
				const TSharedPtr<FJsonObject>& , FString& OutErr) -> FUECPCreateAssetResult
			{
				FUECPCreateAssetResult R;
				OutErr = FString::Printf(
					TEXT("create_asset: '%s' cannot be created from scratch — these come from source-asset imports. %s"),
					*AssetType, *Use);
				R.Error = OutErr;
				return R;
			};
		};
		Reg.RegisterType(TEXT("Skeleton"),                 RejectorFor(TEXT("Skeleton"),
			TEXT("Import a rigged FBX via import_skeletal_mesh(file_path=...); the importer creates the Skeleton asset alongside the SkeletalMesh.")), ExtId);
		Reg.RegisterType(TEXT("SkeletalMesh"),             RejectorFor(TEXT("SkeletalMesh"),
			TEXT("Use import_skeletal_mesh(file_path=...) on a rigged FBX, or import_asset on an FBX. To reuse an existing skeleton, pass its path as skeleton_path so a fresh one isn't created.")), ExtId);

		Reg.RegisterType(TEXT("AnimBlueprint"),            UECPCreateAsset::FactoryFromArgsFn(&AnimationTools::HandleCreateAnimBlueprintFromArgs,           TEXT("AnimBlueprint")),            ExtId);
		Reg.RegisterType(TEXT("AnimSequence"),             UECPCreateAsset::FactoryFromArgsFn(&AnimationTools::HandleCreateAnimSequenceFromArgs,           TEXT("AnimSequence")),             ExtId);
		Reg.RegisterType(TEXT("AnimMontage"),              UECPCreateAsset::FactoryFromArgsFn(&AnimationTools::HandleCreateAnimMontageFromArgs,             TEXT("AnimMontage")),              ExtId);
		Reg.RegisterType(TEXT("AnimComposite"),            UECPCreateAsset::FactoryFromArgsFn(&AnimationTools::HandleCreateAnimCompositeFromArgs,           TEXT("AnimComposite")),            ExtId);
		Reg.RegisterType(TEXT("AimOffset"),                UECPCreateAsset::FactoryFromArgsFn(&AnimationTools::HandleCreateAimOffsetFromArgs,               TEXT("AimOffset")),                ExtId);
		Reg.RegisterType(TEXT("BlendSpace"),               UECPCreateAsset::FactoryFromArgsFn(&AnimationTools::HandleCreateBlendspaceFromArgs,              TEXT("BlendSpace")),               ExtId);
		Reg.RegisterType(TEXT("PoseAsset"),                UECPCreateAsset::FactoryFromArgsFn(&AnimationTools::HandleCreatePoseAssetFromArgs,               TEXT("PoseAsset")),                ExtId);
		Reg.RegisterType(TEXT("BoneCompressionSettings"),  UECPCreateAsset::FactoryFromArgsFn(&AnimationTools::HandleCreateBoneCompressionSettingsFromArgs, TEXT("BoneCompressionSettings")),  ExtId);
		Reg.RegisterType(TEXT("CurveCompressionSettings"), UECPCreateAsset::FactoryFromArgsFn(&AnimationTools::HandleCreateCurveCompressionSettingsFromArgs,TEXT("CurveCompressionSettings")), ExtId);
		Reg.RegisterType(TEXT("IKRetargeter"),             UECPCreateAsset::FactoryFromArgsFn(&AnimationTools::HandleCreateIKRetargeterFromArgs,            TEXT("IKRetargeter")),             ExtId);
		Reg.RegisterType(TEXT("IKRig"),                    UECPCreateAsset::FactoryFromArgsFn(&AnimationTools::HandleCreateIKRigFromArgs,                   TEXT("IKRig")),                    ExtId);
		Reg.RegisterType(TEXT("PoseSearchDatabase"),       UECPCreateAsset::FactoryFromArgsFn(&PoseSearchTools::HandleCreatePoseSearchDatabaseFromArgs,     TEXT("PoseSearchDatabase")),       ExtId);
		Reg.RegisterType(TEXT("PoseSearchNormalizationSet"),UECPCreateAsset::FactoryFromArgsFn(&PoseSearchTools::HandleCreatePoseSearchNormalizationSetFromArgs, TEXT("PoseSearchNormalizationSet")), ExtId);
		Reg.RegisterType(TEXT("PoseSearchSchema"),         UECPCreateAsset::FactoryFromArgsFn(&PoseSearchTools::HandleCreatePoseSearchSchemaFromArgs,       TEXT("PoseSearchSchema")),         ExtId);
		Reg.RegisterType(TEXT("ChooserTable"),             UECPCreateAsset::FactoryFromArgsFn(&ChooserTools::HandleCreateChooserTableFromArgs,              TEXT("ChooserTable")),             ExtId);
	}

	{
		const FName U(TEXT("animation"));
		const auto Meta = [&D, &U](const TCHAR* Action, const TCHAR* Summary, const TCHAR* Params)
		{ D.RegisterToolMetadata({ FName(Action), U, FString(Summary), FString(Params) }); };

		Meta(TEXT("add_anim_curve"),                 TEXT("Add a named curve to an animation (batch)."), TEXT("animation_path, curve_name, curve_type?"));
		Meta(TEXT("add_anim_notify"),                TEXT("Add a notify to a track (track must exist). notify_class errors if unresolvable; time validated against clip length."), TEXT("animation_path, notify_name, time_position|time_normalized, notify_class?, track_name|track_index?"));
		Meta(TEXT("add_anim_notify_state"),          TEXT("Add a duration notify-state to a track (start validated against clip length)."), TEXT("animation_path, notify_state_name, start_time|start_time_normalized, duration, notify_state_class?, track_name|track_index?"));
		Meta(TEXT("add_play_niagara_notify"),        TEXT("Add a PlayNiagaraEffect notify (configured in one call)."), TEXT("animation_path, niagara_system_path, time_position, track_name|track_index?, location_offset?, socket_name?, attached?"));
		Meta(TEXT("add_play_sound_notify"),          TEXT("Add a PlaySound notify (configured in one call)."), TEXT("animation_path, sound_path, time_position, track_name|track_index?, volume_multiplier?, pitch_multiplier?"));
		Meta(TEXT("remove_anim_notify"),             TEXT("Remove a notify/notify-state at a time."), TEXT("animation_path, notify_name, time_position"));
		Meta(TEXT("add_anim_notify_track"),          TEXT("Add a notify track to an animation."), TEXT("animation_path, track_name"));
		Meta(TEXT("set_anim_notify_property"),       TEXT("Edit a notify's UObject property (not time/track)."), TEXT("asset_path, notify_name, time_position, property_name, property_value"));
		Meta(TEXT("move_anim_notify"),               TEXT("Move a notify to a new time/track."), TEXT("asset_path, notify_name, time_position, new_time?, new_track_index|new_track_name?"));
		Meta(TEXT("set_anim_notify_duration"),       TEXT("Set a notify-state's duration."), TEXT("asset_path, notify_name, time_position, new_duration"));
		Meta(TEXT("set_anim_curve_keys"),            TEXT("Set float curve keys (transform/vector unsupported)."), TEXT("animation_path, curve_name, key_time, key_value, curve_type?"));
		Meta(TEXT("remove_anim_curve"),              TEXT("Remove a named curve."), TEXT("animation_path, curve_name, curve_type?"));
		Meta(TEXT("list_anim_curves"),               TEXT("List an asset's curves."), TEXT("asset_path"));
		Meta(TEXT("remove_anim_curve_key"),          TEXT("Remove a single float curve key."), TEXT("asset_path, curve_name, key_time, curve_type?"));
		Meta(TEXT("add_skeleton_socket"),            TEXT("Add a skeleton socket (batch)."), TEXT("skeleton_path, socket_name, bone_name"));
		Meta(TEXT("get_skeleton_sockets"),           TEXT("List skeleton sockets."), TEXT("skeleton_path"));
		Meta(TEXT("remove_skeleton_socket"),         TEXT("Remove a skeleton socket."), TEXT("skeleton_path, socket_name"));
		Meta(TEXT("rename_skeleton_socket"),         TEXT("Rename a skeleton socket (collision-checked)."), TEXT("skeleton_path, old_name, new_name"));
		Meta(TEXT("set_skeleton_socket_transform"),  TEXT("Set a socket's relative transform."), TEXT("skeleton_path, socket_name, relative_location?, relative_rotation?, relative_scale?"));
		Meta(TEXT("set_skeleton_socket_parent"),     TEXT("Re-parent a socket to another bone."), TEXT("skeleton_path, socket_name, new_bone_name"));
		Meta(TEXT("add_virtual_bone"),               TEXT("Add a virtual bone (batch)."), TEXT("skeleton_path, virtual_bone_name, source_bone, target_bone"));
		Meta(TEXT("list_virtual_bones"),             TEXT("List virtual bones."), TEXT("skeleton_path"));
		Meta(TEXT("remove_virtual_bone"),            TEXT("Remove a virtual bone."), TEXT("skeleton_path, virtual_bone_name"));
		Meta(TEXT("rename_virtual_bone"),            TEXT("Rename a virtual bone (batch)."), TEXT("skeleton_path, old_name, new_name"));
		Meta(TEXT("get_skeleton_bones"),             TEXT("List skeleton bones."), TEXT("skeleton_path"));
		Meta(TEXT("get_skeleton_hierarchy"),         TEXT("Get bones with index/name/parent."), TEXT("skeleton_path"));
		Meta(TEXT("add_anim_sync_marker"),           TEXT("Add a sync marker within the clip length."), TEXT("animation_path, marker_name, time"));
		Meta(TEXT("remove_anim_sync_marker"),        TEXT("Remove a sync marker (optional exact time)."), TEXT("animation_path, marker_name, time?"));
		Meta(TEXT("list_anim_sync_markers"),         TEXT("List sync markers."), TEXT("animation_path"));
		Meta(TEXT("move_anim_sync_marker"),          TEXT("Reposition a sync marker."), TEXT("asset_path, marker_name, time_position, new_time"));
		Meta(TEXT("rename_anim_sync_marker"),        TEXT("Rename a sync marker in place."), TEXT("asset_path, old_name, new_name, time_position"));
		Meta(TEXT("add_blendspace_sample"),          TEXT("Add a sample to a BlendSpace (batch; 3D needs axis 2)."), TEXT("blendspace_path, animation_path, sample_x, sample_y?, sample_z?"));
		Meta(TEXT("move_blendspace_sample"),         TEXT("Move the nearest sample to a new coordinate."), TEXT("blendspace_path, sample_x, sample_y, new_sample_x, new_sample_y, sample_z?, new_sample_z?"));
		Meta(TEXT("set_blendspace_sample_animation"),TEXT("Swap a sample's bound animation."), TEXT("blendspace_path, sample_x, sample_y, new_animation_path, sample_z?"));
		Meta(TEXT("set_blendspace_sample_rate_scale"),TEXT("Set a sample's playback rate scale."), TEXT("blendspace_path, sample_x, sample_y, rate_scale"));
		Meta(TEXT("remove_blendspace_sample"),       TEXT("Remove the nearest sample."), TEXT("blendspace_path, sample_x, sample_y"));
		Meta(TEXT("set_blendspace_axis"),            TEXT("Configure a BlendSpace axis (name/range/grid/snap/wrap); errors on degenerate range or invalid axis for 1D."), TEXT("blendspace_path, axis_index, axis_name?, axis_min?, axis_max?, grid_divisions?, snap_to_grid?, wrap_input?"));
		Meta(TEXT("get_blendspace_info"),            TEXT("Inspect a BlendSpace."), TEXT("blendspace_path"));
		Meta(TEXT("add_montage_section"),            TEXT("Add a montage section (batch)."), TEXT("montage_path, section_name, time_position?"));
		Meta(TEXT("remove_montage_section"),         TEXT("Remove a montage section."), TEXT("montage_path, section_name"));
		Meta(TEXT("rename_montage_section"),         TEXT("Rename a montage section (rewires links)."), TEXT("montage_path, old_name, new_name"));
		Meta(TEXT("move_montage_section"),           TEXT("Move a section's start time (reorders)."), TEXT("montage_path, section_name, new_start_time"));
		Meta(TEXT("link_montage_slot"),              TEXT("Link an animation into a montage slot."), TEXT("montage_path, slot_name"));
		Meta(TEXT("set_montage_slot_name"),          TEXT("Set a montage's slot name."), TEXT("montage_path, slot_name"));
		Meta(TEXT("set_montage_blend_settings"),     TEXT("Set montage blend in/out + mode."), TEXT("montage_path, blend_in_time, blend_out_time, blend_mode?"));
		Meta(TEXT("get_anim_montage_sections"),      TEXT("List montage sections."), TEXT("montage_path"));
		Meta(TEXT("set_montage_section_link"),       TEXT("Link a section's next-section."), TEXT("montage_path, section_name, next_section"));
		Meta(TEXT("get_montage_summary"),            TEXT("Summarise a montage (sections + notifies)."), TEXT("montage_path"));
		Meta(TEXT("add_motion_warping_window"),      TEXT("Add a motion-warping window (needs MotionWarping)."), TEXT("montage_path, warp_target_name, start_time, end_time"));
		Meta(TEXT("get_motion_warping_windows"),     TEXT("List motion-warping windows."), TEXT("montage_path, warp_target_name?"));
		Meta(TEXT("remove_motion_warping_window"),   TEXT("Remove motion-warping window(s)."), TEXT("montage_path, warp_target_name"));
		Meta(TEXT("create_anim_sequence"),           TEXT("Create an empty AnimSequence bound to a skeleton."), TEXT("name, save_path, skeleton_path, fps?, duration?"));
		Meta(TEXT("set_anim_sequence_settings"),     TEXT("Set rate/root-motion/loop/additive settings."), TEXT("asset_path, rate_scale?, enable_root_motion?, loop?, additive_anim_type?, ref_pose_type?, ref_pose_seq?, ref_frame_index?"));
		Meta(TEXT("set_anim_sequence_keys"),         TEXT("Author bone tracks (rotation = additive deltas)."), TEXT("anim_sequence_path, tracks, fps?, duration?, mode?, base_pose_animation?, base_pose_frame?"));
		Meta(TEXT("set_anim_transform_curve_keys"),  TEXT("Author FTransform curve overlays."), TEXT("anim_sequence_path, curve_name, keys, mode?"));
		Meta(TEXT("get_anim_sequence_tracks"),       TEXT("Sample bone tracks (inspect before editing)."), TEXT("anim_sequence_path, bones?, sample_frames?, include_rotation_euler?"));
		Meta(TEXT("analyze_anim_motion"),            TEXT("Per-bone motion stats (find static/spiky bones)."), TEXT("anim_sequence_path, bones?, static_threshold_degrees?"));
		Meta(TEXT("detect_anim_discontinuities"),    TEXT("Find rotation jumps between frames."), TEXT("anim_sequence_path, angle_threshold_degrees?, bones?, max_hits?"));
		Meta(TEXT("render_anim_sequence_thumbnail"), TEXT("Render the clip at a time to a PNG."), TEXT("anim_sequence_path, time?, skeletal_mesh_path?, width?, height?, camera_yaw?, camera_pitch?, camera_distance?, lit?, output_path?"));
		Meta(TEXT("crop_animation"),                 TEXT("Trim an animation to a new time range."), TEXT("asset_path, new_start_time, new_end_time"));
		Meta(TEXT("get_anim_sequence_info"),         TEXT("Inspect an AnimSequence."), TEXT("asset_path"));
		Meta(TEXT("assign_compression_to_animation"),TEXT("Assign bone+curve compression settings (batch)."), TEXT("animation_path, bone_compression_path, curve_compression_path"));
		Meta(TEXT("get_compression_info"),           TEXT("Get an animation's compression settings."), TEXT("animation_path"));
		Meta(TEXT("create_anim_blueprint_from_parent"), TEXT("Subclass an existing AnimBP."), TEXT("name, save_path, parent_path, skeleton_path"));
		Meta(TEXT("get_anim_bp_summary"),            TEXT("Summarise an AnimBP incl. compile_status (CALL FIRST)."), TEXT("anim_blueprint_path"));
		Meta(TEXT("compile_anim_blueprint"),         TEXT("Compile an AnimBP and report compile_errors[]/compile_warnings[] (node title + GUID). Mutating AnimBP tools already compile and embed the same fields."), TEXT("anim_blueprint_path"));
		Meta(TEXT("get_anim_graph_nodes"),           TEXT("List AnimGraph nodes (GUID/class/title/pos/asset/dimensionality/pins). include_nested=true to also list in-state nodes."), TEXT("anim_blueprint_path, include_nested?"));
		Meta(TEXT("get_anim_state_machines"),        TEXT("Get state-machine tree (CALL FIRST)."), TEXT("anim_blueprint_path"));
		Meta(TEXT("add_state_machine"),              TEXT("Add a state machine to the AnimGraph."), TEXT("anim_blueprint_path, name, position_x?, position_y?"));
		Meta(TEXT("add_anim_state"),                 TEXT("Add a state (batch)."), TEXT("anim_blueprint_path, state_name, state_machine_name, animation_path?, blend_variable_x?, blend_variable_y?, position_x?, position_y?"));
		Meta(TEXT("remove_anim_state"),              TEXT("Remove a state."), TEXT("anim_blueprint_path, state_machine_name, state_name"));
		Meta(TEXT("rename_anim_state"),              TEXT("Rename a state."), TEXT("anim_blueprint_path, state_machine_name, old_name, new_name"));
		Meta(TEXT("set_anim_state_animation"),       TEXT("Swap a state's bound animation."), TEXT("anim_blueprint_path, state_machine_name, state_name, animation_path"));
		Meta(TEXT("add_blendspace_player"),          TEXT("Add a BlendSpacePlayer node at the top-level AnimGraph (1D/2D pins auto-derived)."), TEXT("anim_blueprint_path, blendspace_path, position_x?, position_y?"));
		Meta(TEXT("set_blendspace_player_asset"),    TEXT("Safely rebind a BlendSpacePlayer (by GUID, anywhere) to another BlendSpace; auto-reconstructs 1D<->2D pins, reports before/after. dry_run? + expect_current_asset? guard."), TEXT("anim_blueprint_path, node_guid, blendspace_path, dry_run?, expect_current_asset?"));
		Meta(TEXT("add_state_transition"),           TEXT("Add a directional transition (batch). bidirectional=true creates a second independent reverse transition node."), TEXT("anim_blueprint_path, from_state, to_state, state_machine_name, bidirectional?, crossfade_duration?"));
		Meta(TEXT("remove_state_transition"),        TEXT("Remove a transition."), TEXT("anim_blueprint_path, state_machine_name, from_state, to_state"));
		Meta(TEXT("set_transition_rule"),            TEXT("Set a transition rule (batch; always_true|bool_variable|not_bool_variable|float_compare|int_compare)."), TEXT("anim_blueprint_path, from_state, to_state, rule_type, variable_name?, compare_op?, compare_value?, state_machine_name"));
		Meta(TEXT("set_transition_blend_settings"),  TEXT("Set a transition's blend mode + times."), TEXT("anim_blueprint_path, from_state, to_state, blend_mode, blend_in_time, blend_out_time, state_machine_name"));
		Meta(TEXT("set_transition_priority"),        TEXT("Set transition priority (lower fires first)."), TEXT("anim_blueprint_path, state_machine_name, from_state, to_state, priority"));
		Meta(TEXT("set_transition_options"),         TEXT("Auto-rule / disable-notifies / blend-profile on a transition."), TEXT("anim_blueprint_path, state_machine_name, from_state, to_state, automatic_rule_based_on_sequence_player?, disable_notifications?, blend_profile?"));
		Meta(TEXT("set_state_options"),              TEXT("always_reset_on_enter / skip_first_update_transition on a state."), TEXT("anim_blueprint_path, state_machine_name, state_name, always_reset_on_enter?, skip_first_update_transition?"));
		Meta(TEXT("add_anim_conduit"),               TEXT("Add a conduit node to a state machine."), TEXT("anim_blueprint_path, conduit_name, state_machine_name, position_x?, position_y?"));
		Meta(TEXT("add_state_alias"),                TEXT("Add a state alias (wildcard via global_alias)."), TEXT("anim_blueprint_path, state_machine_name, alias_name, source_states?, global_alias?, position_x?, position_y?"));
		Meta(TEXT("set_state_machine_entry_state"),  TEXT("Set a state machine's entry state."), TEXT("anim_blueprint_path, state_machine_name, entry_state"));
		Meta(TEXT("wire_anim_node_to_output"),       TEXT("Wire a node to the AnimGraph Output Pose."), TEXT("anim_blueprint_path, node_name"));
		Meta(TEXT("set_anim_node_position"),         TEXT("Reposition any node by GUID."), TEXT("anim_blueprint_path, node_guid, pos_x, pos_y"));
		Meta(TEXT("set_anim_node_property"),         TEXT("Set an AnimGraph node property by GUID."), TEXT("anim_blueprint_path, node_guid, property_name, property_value"));
		Meta(TEXT("create_anim_slot"),               TEXT("Create a montage slot on a skeleton."), TEXT("skeleton_path, slot_name, group_name?"));
		Meta(TEXT("list_anim_slots"),                TEXT("List a skeleton's montage slots."), TEXT("skeleton_path"));
		Meta(TEXT("create_anim_layer_interface"),    TEXT("Create an anim layer interface asset."), TEXT("name, save_path"));
		Meta(TEXT("implement_anim_layer"),           TEXT("Implement an anim layer interface on an AnimBP."), TEXT("anim_blueprint_path, interface_path"));
		Meta(TEXT("add_anim_layer_node"),            TEXT("Add a linked anim-layer node."), TEXT("anim_blueprint_path, layer_name"));
		Meta(TEXT("add_sub_anim_instance"),          TEXT("Embed another AnimBP as a sub-instance node."), TEXT("anim_blueprint_path, linked_blueprint_path, position_x?, position_y?"));
		Meta(TEXT("add_layered_blend_per_bone"),     TEXT("Add a LayeredBoneBlend (set filter next!)."), TEXT("anim_blueprint_path, num_layers?, position_x?, position_y?"));
		Meta(TEXT("set_layered_blend_per_bone_filter"), TEXT("Set a LayeredBoneBlend layer's bone filter (mandatory)."), TEXT("anim_blueprint_path, node_guid|node_title, layer_index, bones"));
		Meta(TEXT("add_saved_pose"),                 TEXT("Add a SaveCachedPose node."), TEXT("anim_blueprint_path, pose_name, position_x?, position_y?"));
		Meta(TEXT("use_cached_pose"),                TEXT("Add a UseCachedPose node."), TEXT("anim_blueprint_path, pose_name, position_x?, position_y?"));
		Meta(TEXT("add_blend_by_bool"),              TEXT("Add a Blend Poses by bool node."), TEXT("anim_blueprint_path, position_x?, position_y?"));
		Meta(TEXT("add_blend_by_int"),               TEXT("Add a Blend Poses by int node."), TEXT("anim_blueprint_path, position_x?, position_y?"));
		Meta(TEXT("add_blend_by_enum"),              TEXT("Add a Blend Poses by enum node."), TEXT("anim_blueprint_path, position_x?, position_y?"));
		Meta(TEXT("add_two_way_blend"),              TEXT("Add a two-way blend node."), TEXT("anim_blueprint_path, position_x?, position_y?"));
		Meta(TEXT("add_blend_bone_by_channel"),      TEXT("Add a BlendBoneByChannel node."), TEXT("anim_blueprint_path, position_x?, position_y?"));
		Meta(TEXT("add_apply_additive"),             TEXT("Add an ApplyAdditive node."), TEXT("anim_blueprint_path, position_x?, position_y?"));
		Meta(TEXT("add_make_dynamic_additive"),      TEXT("Add a MakeDynamicAdditive node."), TEXT("anim_blueprint_path, position_x?, position_y?"));
		Meta(TEXT("add_apply_mesh_space_additive"),  TEXT("Add an ApplyMeshSpaceAdditive node."), TEXT("anim_blueprint_path, position_x?, position_y?"));
		Meta(TEXT("add_inertialization"),            TEXT("Add an Inertialization node."), TEXT("anim_blueprint_path, position_x?, position_y?"));
		Meta(TEXT("add_modify_bone"),                TEXT("Add a Transform (Modify) Bone node."), TEXT("anim_blueprint_path, position_x?, position_y?"));
		Meta(TEXT("add_copy_bone"),                  TEXT("Add a Copy Bone node."), TEXT("anim_blueprint_path, position_x?, position_y?"));
		Meta(TEXT("add_look_at"),                    TEXT("Add a LookAt node."), TEXT("anim_blueprint_path, position_x?, position_y?"));
		Meta(TEXT("add_two_bone_ik"),                TEXT("Add a Two Bone IK node."), TEXT("anim_blueprint_path, position_x?, position_y?"));
		Meta(TEXT("add_fabrik"),                     TEXT("Add a FABRIK node."), TEXT("anim_blueprint_path, position_x?, position_y?"));
		Meta(TEXT("add_ccdik"),                      TEXT("Add a CCDIK node."), TEXT("anim_blueprint_path, position_x?, position_y?"));
		Meta(TEXT("add_leg_ik"),                     TEXT("Add a Leg IK node."), TEXT("anim_blueprint_path, position_x?, position_y?"));
		Meta(TEXT("add_spring_bone"),                TEXT("Add a Spring controller node."), TEXT("anim_blueprint_path, position_x?, position_y?"));
		Meta(TEXT("add_rigid_body"),                 TEXT("Add a RigidBody (physics) node."), TEXT("anim_blueprint_path, position_x?, position_y?"));
		Meta(TEXT("add_bone_driven_controller"),     TEXT("Add a BoneDrivenController node."), TEXT("anim_blueprint_path, source_bone, target_bone, position_x?, position_y?"));
		Meta(TEXT("add_sequence_evaluator"),         TEXT("Add a SequenceEvaluator node."), TEXT("anim_blueprint_path, position_x?, position_y?"));
		Meta(TEXT("add_random_player"),              TEXT("Add a Random Sequence Player node."), TEXT("anim_blueprint_path, position_x?, position_y?"));
		Meta(TEXT("add_slot_node"),                  TEXT("Add a montage Slot node."), TEXT("anim_blueprint_path, slot_name, position_x?, position_y?"));
		Meta(TEXT("add_aim_offset_player"),          TEXT("Add an AimOffset player node."), TEXT("anim_blueprint_path, position_x?, position_y?"));
		Meta(TEXT("add_mirror"),                     TEXT("Add a Mirror node."), TEXT("anim_blueprint_path, position_x?, position_y?"));
		Meta(TEXT("add_pose_by_name"),               TEXT("Add a Pose By Name node."), TEXT("anim_blueprint_path, position_x?, position_y?"));
		Meta(TEXT("add_rotate_root_bone"),           TEXT("Add a RotateRootBone node."), TEXT("anim_blueprint_path, position_x?, position_y?"));
		Meta(TEXT("add_copy_pose_from_mesh"),        TEXT("Add a CopyPoseFromMesh node."), TEXT("anim_blueprint_path, position_x?, position_y?"));
		Meta(TEXT("add_local_to_component_space"),   TEXT("Add a Local->Component space node."), TEXT("anim_blueprint_path, position_x?, position_y?"));
		Meta(TEXT("add_component_to_local_space"),   TEXT("Add a Component->Local space node."), TEXT("anim_blueprint_path, position_x?, position_y?"));
		Meta(TEXT("add_mesh_ref_pose"),              TEXT("Add a mesh-space ref pose node."), TEXT("anim_blueprint_path, position_x?, position_y?"));
		Meta(TEXT("add_local_ref_pose"),             TEXT("Add a local-space ref pose node."), TEXT("anim_blueprint_path, position_x?, position_y?"));
		Meta(TEXT("add_identity_pose"),              TEXT("Add an identity pose node."), TEXT("anim_blueprint_path, position_x?, position_y?"));
		Meta(TEXT("add_motion_matching_node"),       TEXT("Add a Motion Matching node bound to a DB."), TEXT("anim_blueprint_path, pose_search_db_path"));
		Meta(TEXT("add_control_rig_node"),           TEXT("Add a Control Rig node to the AnimGraph."), TEXT("anim_blueprint_path, control_rig_path"));
		Meta(TEXT("connect_anim_nodes"),             TEXT("Connect two AnimGraph nodes by GUID."), TEXT("anim_blueprint_path, source_node_guid, target_node_guid"));
		Meta(TEXT("build_anim_chain"),               TEXT("Build a full AnimGraph chain in one call (types incl. sequence_player, sequence_evaluator, blendspace_player); every edge reported in connections[]."), TEXT("anim_blueprint_path, chain, auto_connect_to_output?"));
		Meta(TEXT("create_sync_group"),              TEXT("Guidance/redirect, not a create op: sync groups are bound per-node — returns an error pointing to set_anim_node_property (GroupName + Method=SyncGroup + GroupRole)."), TEXT("anim_blueprint_path, sync_group_name, group_role"));
		Meta(TEXT("add_blend_profile"),              TEXT("Add a per-bone blend profile to a skeleton."), TEXT("skeleton_path, profile_name"));

		// Composite templates — one call builds a whole recipe; result lists every sub-step in steps[] (ok/error) and ends with the compile report.
		Meta(TEXT("create_locomotion_state_machine"), TEXT("TEMPLATE: Idle/WalkRun(+JumpStart/JumpLoop/JumpLand) state machine with Speed/IsInAir rules (creates the variables if missing), entry=Idle, wired to Output Pose, one compile report. walk_anim+run_anim generate a 1D BlendSpace next to the AnimBP."), TEXT("anim_blueprint_path, idle_anim, walk_run_blendspace|walk_anim+run_anim, jump_start?, jump_loop?, jump_land?, speed_variable?=Speed, is_in_air_variable?=IsInAir, state_machine_name?=Locomotion, wire_to_output?=true, speed_threshold?=3, walk_speed?=150, run_speed?=500, crossfade_duration?=0.2"));
		Meta(TEXT("create_montage_from_sequence"),    TEXT("TEMPLATE: montage asset from a sequence — slot link, ordered sections (start_time|start_normalized, next?, loop?), loop_sections, blend in/out, notifies (class resolved), saved + summarised."), TEXT("sequence_path, montage_path (folder/name)|montage_name+save_path, slot?=DefaultSlot, sections?[{name, start_time|start_normalized, next?, loop?}], loop_sections?[], blend_in?, blend_out?, rate_scale?, notifies?[{name, time|time_normalized, class?, track_index?}]"));
		Meta(TEXT("make_additive"),                   TEXT("TEMPLATE: make a sequence additive with a consistent AdditiveAnimType/RefPoseType/RefPoseSeq/RefFrameIndex set (base defaults to the sequence itself), recompress (derived data), save."), TEXT("sequence_path, base_pose_sequence?, base_frame?=0, additive_type?=local|mesh_space|none, ref_pose_type?=anim_frame|ref_pose|anim_scaled|local_anim_frame, recompress?=true"));
		Meta(TEXT("setup_foot_ik"),                   TEXT("TEMPLATE: inserts Local->Component -> TwoBoneIK(left foot) -> TwoBoneIK(right foot) -> Component->Local before Output Pose (re-attaching the current pose source), creates/binds vector IK target + float alpha variables to the EffectorLocation/Alpha pins, compiles. Foot bones found by name heuristics when omitted."), TEXT("anim_blueprint_path, left_foot_bone?, right_foot_bone?, ik_target_left_variable?=IKLeftFoot, ik_target_right_variable?=IKRightFoot, alpha_variable?=IKAlpha, effector_space?=component|world|bone|parent"));
	}

	{
		const FName U(TEXT("ik_retarget"));
		const auto Meta = [&D, &U](const TCHAR* Action, const TCHAR* Summary, const TCHAR* Params)
		{ D.RegisterToolMetadata({ FName(Action), U, FString(Summary), FString(Params) }); };

		Meta(TEXT("add_ik_solver"),              TEXT("Add an IK solver (FBIK|Limb|BodyMover|SetTransform)."), TEXT("asset_path, solver_type"));
		Meta(TEXT("add_ik_goal"),                TEXT("Add an IK goal (auto-connects to solver if >=0)."), TEXT("asset_path, bone_name, goal_name?, solver_index?"));
		Meta(TEXT("add_retarget_chain"),         TEXT("Add a retarget chain (optionally link to a goal)."), TEXT("asset_path, chain_name, start_bone, end_bone, goal_name?"));
		Meta(TEXT("set_ik_retarget_root"),       TEXT("Set the IK Rig retarget root bone."), TEXT("asset_path, bone_name"));
		Meta(TEXT("get_ik_rig_summary"),         TEXT("Inspect an IK Rig (CALL FIRST)."), TEXT("asset_path"));
		Meta(TEXT("remove_ik_goal"),             TEXT("Remove an IK goal."), TEXT("asset_path, goal_name"));
		Meta(TEXT("remove_retarget_chain"),      TEXT("Remove a retarget chain."), TEXT("asset_path, chain_name"));
		Meta(TEXT("map_retarget_chain"),         TEXT("Map source->target chain (or auto_map=true)."), TEXT("retargeter_path, source_chain?, target_chain?, auto_map?"));
		Meta(TEXT("get_retargeter_summary"),     TEXT("Inspect an IK Retargeter."), TEXT("retargeter_path"));
		Meta(TEXT("set_retarget_chain_settings"),TEXT("Set per-chain FK/IK retarget settings."), TEXT("retargeter_path, chain_name, fk_translation_mode?, fk_rotation_mode?, ik_enable?, ik_blend_to_source?, ..."));
		Meta(TEXT("set_retarget_root_settings"), TEXT("Set retarget root settings (alphas/scale/affect_ik)."), TEXT("retargeter_path, rotation_alpha?, translation_alpha?, scale_vertical?, affect_ik_vertical?, ..."));
		Meta(TEXT("set_retarget_pose"),          TEXT("Create/switch a retarget pose for a side."), TEXT("retargeter_path, pose_name, side"));
		Meta(TEXT("auto_align_retarget_pose"),   TEXT("Auto-align a side's bones to source orientations (run first)."), TEXT("retargeter_path, side"));
		Meta(TEXT("edit_retarget_pose_bone"),    TEXT("Edit a bone in the current pose (batch via bones=[])."), TEXT("retargeter_path, bone_name, side, rotation"));
		Meta(TEXT("reset_retarget_pose"),        TEXT("Reset pose deltas back to ref pose."), TEXT("retargeter_path, side, pose_name?, bones?"));
		Meta(TEXT("export_retarget_animation"),  TEXT("Batch-retarget animations through the retargeter."), TEXT("retargeter_path, source_mesh, target_mesh, animation_paths, prefix?, suffix?"));

		// Composite template — full source->target retarget pipeline in one call; every sub-step in steps[] (unresolved chains are 'skipped', not fatal).
		Meta(TEXT("retarget_setup"),             TEXT("TEMPLATE: IK Rig per mesh (retarget root pelvis/hips + humanoid chains Root/Spine/Neck/Head/Clavicle/Arm/Leg[/fingers] by bone-name heuristics), IK Retargeter, auto chain mapping, auto-align target pose, then export each animation into output_folder (suffix defaults to _<TargetMesh>)."), TEXT("source_mesh_path, target_mesh_path, output_folder, animations?[], retargeter_name?, source_rig_name?, target_rig_name?, auto_map?=true, auto_align?=true, include_fingers?=false, prefix?, suffix?, animation_output_folder?"));
	}

	{
		const FName U(TEXT("pose_search"));
		const auto Meta = [&D, &U](const TCHAR* Action, const TCHAR* Summary, const TCHAR* Params)
		{ D.RegisterToolMetadata({ FName(Action), U, FString(Summary), FString(Params) }); };

		Meta(TEXT("add_pose_search_channel"),           TEXT("Add a schema channel (trajectory|pose|heading)."), TEXT("schema_path, channel_type"));
		Meta(TEXT("add_animation_to_database"),         TEXT("Add an animation entry to a database."), TEXT("database_path, animation_path, animation_type?"));
		Meta(TEXT("build_pose_search_database"),        TEXT("Index/build a pose search database."), TEXT("database_path"));
		Meta(TEXT("get_pose_search_summary"),           TEXT("Inspect a schema/database (CALL FIRST)."), TEXT("asset_path"));
		Meta(TEXT("set_pose_search_channel_property"),  TEXT("Set a channel property."), TEXT("schema_path, channel_index, property_name, property_value"));
		Meta(TEXT("set_pose_search_schema_skeleton"),   TEXT("Set a schema's skeleton/role."), TEXT("schema_path, skeleton_path, skeleton_index?, role?"));
		Meta(TEXT("set_pose_search_schema_property"),   TEXT("Set a schema property."), TEXT("schema_path, property_name, property_value"));
		Meta(TEXT("set_pose_search_database_schema"),   TEXT("Bind a schema to a database."), TEXT("database_path, schema_path"));
		Meta(TEXT("set_schema_mirror_data_table"),      TEXT("Set a schema's mirror data table."), TEXT("schema_path, mirror_data_table_path"));
		Meta(TEXT("set_animation_database_entry_property"), TEXT("Set a database entry property."), TEXT("database_path, animation_index, property_name, property_value"));
		Meta(TEXT("get_pose_search_channel_properties"),TEXT("Get a channel's properties."), TEXT("schema_path, channel_index"));
		Meta(TEXT("remove_animation_from_database"),    TEXT("Remove an animation entry."), TEXT("database_path, animation_index"));
		Meta(TEXT("remove_pose_search_channel"),        TEXT("Remove a schema channel."), TEXT("schema_path, channel_index"));
		Meta(TEXT("duplicate_pose_search_channel"),     TEXT("Duplicate a schema channel."), TEXT("schema_path, channel_index"));
		Meta(TEXT("reorder_pose_search_channels"),      TEXT("Reorder schema channels."), TEXT("schema_path, from_index, to_index"));
		Meta(TEXT("add_database_to_normalization_set"), TEXT("Add a database to a normalization set."), TEXT("normalization_set_path, database_path"));
		Meta(TEXT("list_pose_search_channel_types"),    TEXT("List available channel types."), TEXT(""));

		// Composite template — schema + database + index build (+ Chooser) in one call; every sub-step in steps[].
		Meta(TEXT("setup_motion_matching"),             TEXT("TEMPLATE: Pose Search schema (skeleton, trajectory channel with sample offsets, pose channel on foot_l/foot_r/pelvis or pose_bones), optional mirror table, database with animations[] entries, index build, optional Chooser table (output PoseSearchDatabase) with the database as a row."), TEXT("skeleton_path, animations[], output_folder, database_name?, schema_name?, trajectory_seconds?=[-0.3,0,0.3,0.6], pose_bones?[], create_chooser?=true, chooser_name?, mirror_data_table?, sample_rate?=30, trajectory_flags?"));
	}

	{
		const FName U(TEXT("chooser"));
		const auto Meta = [&D, &U](const TCHAR* Action, const TCHAR* Summary, const TCHAR* Params)
		{ D.RegisterToolMetadata({ FName(Action), U, FString(Summary), FString(Params) }); };

		Meta(TEXT("add_chooser_column"),           TEXT("Add a column (float_range|output_object|bool|enum|gameplay_tag|...)."), TEXT("chooser_path, column_type, label?"));
		Meta(TEXT("add_chooser_row"),              TEXT("Add a row bound to an asset."), TEXT("chooser_path, asset_path"));
		Meta(TEXT("get_chooser_summary"),          TEXT("Inspect a chooser table (CALL FIRST)."), TEXT("chooser_path"));
		Meta(TEXT("set_chooser_row_value"),        TEXT("Set a cell value (float_range: Min/Max; bool: Value)."), TEXT("chooser_path, column_index, row_index, property_name, property_value"));
		Meta(TEXT("set_chooser_output_type"),      TEXT("Set the chooser's output type."), TEXT("chooser_path, <output type>"));
		Meta(TEXT("set_chooser_fallback"),         TEXT("Set the fallback result."), TEXT("chooser_path, <fallback>"));
		Meta(TEXT("set_chooser_context_data"),     TEXT("Set the chooser's context data."), TEXT("chooser_path, <context>"));
		Meta(TEXT("set_chooser_column_property"),  TEXT("Set a column-level property (MinValue/MaxValue)."), TEXT("chooser_path, column_index, property_name, property_value"));
		Meta(TEXT("remove_chooser_row"),           TEXT("Remove a row."), TEXT("chooser_path, row_index"));
		Meta(TEXT("remove_chooser_column"),        TEXT("Remove a column."), TEXT("chooser_path, column_index"));
		Meta(TEXT("duplicate_chooser_row"),        TEXT("Duplicate a row."), TEXT("chooser_path, row_index"));
		Meta(TEXT("reorder_chooser_rows"),         TEXT("Reorder rows."), TEXT("chooser_path, from_index, to_index"));
		Meta(TEXT("duplicate_chooser_column"),     TEXT("Duplicate a column."), TEXT("chooser_path, column_index"));
		Meta(TEXT("reorder_chooser_columns"),      TEXT("Reorder columns."), TEXT("chooser_path, from_index, to_index"));
		Meta(TEXT("rename_chooser_column"),        TEXT("Rename a column."), TEXT("chooser_path, column_index, new_name"));
		Meta(TEXT("get_chooser_row_values"),       TEXT("Read a row's cell values."), TEXT("chooser_path, row_index"));
		Meta(TEXT("get_chooser_column_properties"),TEXT("Read a column's properties."), TEXT("chooser_path, column_index"));
		Meta(TEXT("bulk_set_chooser_rows"),        TEXT("Bulk-set rows from JSON (preferred for batch)."), TEXT("chooser_path, rows_json"));
	}

	UE_LOG(LogUECPAnimationExt, Log, TEXT("Registered %d Animation tools (animation + ik_retarget + pose_search + chooser umbrellas)"),
		OwnedToolNames().Num());
}

void FUECPAnimationExtModule::ShutdownModule()
{
	if (!IUECPCoreModule::IsAvailable()) return;
	IUECPToolDispatcher& D = IUECPCoreModule::Get().GetToolDispatcher();
	for (const FName& N : OwnedToolNames()) D.UnregisterHandler(N);
	IUECPCreateAssetRegistry& Reg = IUECPCoreModule::Get().GetCreateAssetRegistry();
	for (const TCHAR* T : { TEXT("Skeleton"), TEXT("SkeletalMesh"),
	                        TEXT("AnimBlueprint"), TEXT("AnimSequence"), TEXT("AnimMontage"), TEXT("AnimComposite"),
	                        TEXT("AimOffset"), TEXT("BlendSpace"), TEXT("PoseAsset"),
	                        TEXT("BoneCompressionSettings"), TEXT("CurveCompressionSettings"),
	                        TEXT("IKRetargeter"), TEXT("IKRig"),
	                        TEXT("PoseSearchDatabase"), TEXT("PoseSearchNormalizationSet"), TEXT("PoseSearchSchema"),
	                        TEXT("ChooserTable") })
	{
		Reg.UnregisterType(T);
	}
}

IMPLEMENT_MODULE(FUECPAnimationExtModule, UECPAnimationExt)
