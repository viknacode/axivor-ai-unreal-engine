// Copyright 2026, BlueprintsLab, All rights reserved

#include "UECPLevelDesignExtModule.h"

#include "Tools/LandscapeTools.h"
#include "Tools/LevelActorTools.h"
#include "Tools/LevelStreamingTools.h"
#include "Tools/PCGTools.h"
#include "Tools/EnvironmentTools.h"
#include "Tools/PostProcessTools.h"
#include "Tools/SplineTools.h"
#include "Tools/FoliageTools.h"

#include "UECPCoreModule.h"
#include "Services/IUECPToolDispatcher.h"
#include "Services/IUECPCreateAssetRegistry.h"
#include "Services/UECPToolSafety.h"

DEFINE_LOG_CATEGORY(LogUECPLevelDesignExt);

namespace
{
	static const TArray<FName>& OwnedToolNames()
	{
		static const TArray<FName> Names = {
			TEXT("create_landscape"),
			TEXT("set_landscape_material"),
			TEXT("get_landscape_info"),
			TEXT("set_landscape_properties"),
			TEXT("set_landscape_lod"),
			TEXT("export_landscape_heightmap"),
			TEXT("add_landscape_layer_info"),
			TEXT("populate_landscape"),

			TEXT("set_sky_light_properties"),
			TEXT("set_exponential_height_fog_properties"),
			TEXT("set_sky_atmosphere_properties"),
			TEXT("set_volumetric_cloud_properties"),
			TEXT("spawn_environment_actor"),
			TEXT("get_environment_summary"),
			TEXT("set_mpc_parameter_value"),
			TEXT("set_directional_light_properties"),
			TEXT("spawn_rvt_volume"),

			TEXT("add_actor_tag"),
			TEXT("attach_actor"),
			TEXT("delete_actor"),
			TEXT("delete_actors"),
			TEXT("delete_actor_from_level"),
			TEXT("duplicate_actor"),
			TEXT("duplicate_actor_in_level"),
			TEXT("remove_actor_tag"),
			TEXT("set_actor_property"),
			TEXT("set_actor_transform"),
			TEXT("set_light_property"),
			TEXT("set_actor_material"),
			TEXT("set_actor_materials"),
			TEXT("set_multiple_actor_materials"),
			TEXT("spawn_actor"),
			TEXT("spawn_actors"),
			TEXT("spawn_multiple_actors"),
			TEXT("spawn_color_correction_region"),
			TEXT("spawn_color_correction_window"),
			TEXT("get_all_scene_actors"),
			TEXT("get_all_level_actors"),
			TEXT("list_level_actors"),
			TEXT("list_actors"),
			TEXT("get_selected_level_actors"),
			TEXT("get_selected_actors"),
			TEXT("select_actors"),
			TEXT("get_actor_details"),
			TEXT("rename_actor"),
			TEXT("get_actors_by_class"),
			TEXT("find_actors_by_class"),
			TEXT("get_actors_in_box"),
			TEXT("get_actor_components"),
			TEXT("get_level_info"),
			TEXT("get_current_level"),
			TEXT("set_actor_visibility"),
			TEXT("bulk_transform_actors"),
			TEXT("align_actors_to_floor"),
			TEXT("detach_actor"),
			TEXT("move_actor_to_folder"),
			TEXT("move_actors_to_folder"),
			TEXT("set_static_mesh"),
			TEXT("set_actor_physics"),
			TEXT("get_actor_tags"),
			TEXT("set_world_settings"),
			TEXT("focus_viewport_on_actor"),
			TEXT("replace_actor"),
			TEXT("set_actor_replication"),
			TEXT("find_actors_by_bounds"),
			TEXT("begin_play_in_editor"),
			TEXT("stop_play_in_editor"),
			TEXT("open_level"),
			TEXT("save_current_level"),
			TEXT("save_current_level_as"),
			TEXT("get_asset_references"),
			TEXT("create_data_layer"),
			TEXT("assign_actor_to_data_layer"),
			TEXT("set_data_layer_state"),
			TEXT("list_data_layers"),
			TEXT("get_data_layer_actors"),
			TEXT("create_level_instance"),
			TEXT("get_level_instances"),
			TEXT("enable_world_partition"),
			TEXT("disable_world_partition"),
			TEXT("get_world_partition_info"),
			TEXT("set_hlod_layer_properties"),
			TEXT("list_hlod_layers"),
			TEXT("set_actors_property_by_filter"),
			TEXT("set_cine_camera_properties"),
			TEXT("select_actors_by_label"),
			TEXT("set_selected_actors"),
			TEXT("spawn_advanced"),

			TEXT("add_streaming_level"),
			TEXT("set_streaming_level_transform"),
			TEXT("create_level_streaming_volume"),
			TEXT("remove_streaming_level"),
			TEXT("get_streaming_levels"),
			TEXT("set_streaming_level_visibility"),
			TEXT("set_streaming_level_loaded"),

			TEXT("add_pcg_node"),
			TEXT("connect_pcg_nodes"),
			TEXT("set_pcg_mesh_spawner"),
			TEXT("set_pcg_node_property"),
			TEXT("get_pcg_graph_summary"),
			TEXT("remove_pcg_node"),
			TEXT("spawn_pcg_actor"),
			TEXT("set_pcg_actor_bounds"),
			TEXT("get_pcg_actor_info"),
			TEXT("set_pcg_transform_randomizer"),
			TEXT("generate_pcg"),
			TEXT("update_pcg_actor_graph"),
			TEXT("list_pcg_node_types"),
			TEXT("disconnect_pcg_nodes"),
			TEXT("get_pcg_node_properties"),
			TEXT("build_pcg_graph"),
			TEXT("set_pcg_slope_filter"),
			TEXT("set_pcg_noise_density"),
			TEXT("set_pcg_spline_sampler"),
			TEXT("clear_pcg_graph"),
			TEXT("set_pcg_component_properties"),
			TEXT("duplicate_pcg_node"),
			TEXT("set_pcg_node_comment"),
			TEXT("add_pcg_graph_parameter"),
			TEXT("get_pcg_graph_parameters"),
			TEXT("set_pcg_texture_sampler"),
			TEXT("set_pcg_data_from_actor"),
			TEXT("set_pcg_attribute_filter"),
			TEXT("duplicate_pcg_graph"),
			TEXT("create_pcg_subgraph_node"),
			TEXT("set_pcg_self_pruning"),
			TEXT("set_pcg_create_attribute"),
			TEXT("set_pcg_attribute_math"),
			TEXT("set_pcg_exclusion"),
			TEXT("get_pcg_level_summary"),

			TEXT("add_foliage_type"),
			TEXT("paint_foliage"),
			TEXT("set_foliage_density"),
			TEXT("clear_foliage"),
			TEXT("set_foliage_type_properties"),
			TEXT("assign_physical_material_to_foliage"),
			TEXT("get_foliage_summary"),
			TEXT("create_procedural_foliage_spawner"),
			TEXT("spawn_procedural_foliage_volume"),

			TEXT("create_post_process_volume"),
			TEXT("set_post_process_property"),
			TEXT("add_mpc_parameter"),
			TEXT("get_post_process_summary"),
			TEXT("add_post_process_blendable"),
			TEXT("set_mpc_default_value"),
			TEXT("get_mpc_summary"),
			TEXT("remove_post_process_blendable"),
			TEXT("set_post_process_priority"),

			TEXT("add_spline_point"),
			TEXT("set_spline_point_tangent"),
			TEXT("add_spline_component"),
			TEXT("set_spline_points"),
			TEXT("spawn_spline_actor"),
			TEXT("get_spline_info"),
			TEXT("remove_spline_point"),
			TEXT("set_spline_properties"),
			TEXT("set_spline_mesh"),
			TEXT("set_spline_mesh_component_properties"),
			TEXT("scatter_actors_along_spline"),
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

void FUECPLevelDesignExtModule::StartupModule()
{
	if (!IUECPCoreModule::IsAvailable()) return;
	IUECPToolDispatcher& D = IUECPCoreModule::Get().GetToolDispatcher();

	D.RegisterHandler(TEXT("create_landscape"),                  MakeHandler(LandscapeTools::HandleCreateLandscapeFromArgs));
	D.RegisterHandler(TEXT("set_landscape_material"),            MakeHandler(LandscapeTools::HandleSetLandscapeMaterialFromArgs));
	D.RegisterHandler(TEXT("get_landscape_info"),                MakeHandler(LandscapeTools::HandleGetLandscapeInfoFromArgs));
	D.RegisterHandler(TEXT("set_landscape_properties"),          MakeHandler(LandscapeTools::HandleSetLandscapePropertiesFromArgs));
	D.RegisterHandler(TEXT("set_landscape_lod"),                 MakeHandler(LandscapeTools::HandleSetLandscapeLODFromArgs));
	D.RegisterHandler(TEXT("export_landscape_heightmap"),        MakeHandler(LandscapeTools::HandleExportLandscapeHeightmapFromArgs));
	D.RegisterHandler(TEXT("add_landscape_layer_info"),          MakeHandler(LandscapeTools::HandleAddLandscapeLayerInfoFromArgs));
	D.RegisterHandler(TEXT("populate_landscape"),                MakeHandler(LandscapeTools::HandlePopulateLandscape));

	D.RegisterHandler(TEXT("set_sky_light_properties"),                MakeHandler(EnvironmentTools::HandleSetSkyLightPropertiesFromArgs));
	D.RegisterHandler(TEXT("set_exponential_height_fog_properties"),   MakeHandler(EnvironmentTools::HandleSetExponentialHeightFogPropertiesFromArgs));
	D.RegisterHandler(TEXT("set_sky_atmosphere_properties"),           MakeHandler(EnvironmentTools::HandleSetSkyAtmospherePropertiesFromArgs));
	D.RegisterHandler(TEXT("set_volumetric_cloud_properties"),         MakeHandler(EnvironmentTools::HandleSetVolumetricCloudPropertiesFromArgs));
	D.RegisterHandler(TEXT("spawn_environment_actor"),                 MakeHandler(EnvironmentTools::HandleSpawnEnvironmentActorFromArgs));
	D.RegisterHandler(TEXT("get_environment_summary"),                 MakeHandler(EnvironmentTools::HandleGetEnvironmentSummaryFromArgs));
	D.RegisterHandler(TEXT("set_mpc_parameter_value"),                 MakeHandler(EnvironmentTools::HandleSetMPCParameterValueFromArgs));
	D.RegisterHandler(TEXT("set_directional_light_properties"),        MakeHandler(EnvironmentTools::HandleSetDirectionalLightPropertiesFromArgs));
	D.RegisterHandler(TEXT("spawn_rvt_volume"),                        MakeHandler(EnvironmentTools::HandleSpawnRVTVolumeFromArgs));

	D.RegisterHandler(TEXT("add_actor_tag"),                MakeHandler(LevelActorTools::HandleAddActorTagFromArgs));
	D.RegisterHandler(TEXT("attach_actor"),                 MakeHandler(LevelActorTools::HandleAttachActorFromArgs));
	D.RegisterHandler(TEXT("delete_actor"),                 MakeHandler(LevelActorTools::HandleDeleteActorFromArgs));
	D.RegisterHandler(TEXT("delete_actors"),                MakeHandler(LevelActorTools::HandleDeleteActorFromArgs));
	D.RegisterHandler(TEXT("delete_actor_from_level"),      MakeHandler(LevelActorTools::HandleDeleteActorFromArgs));
	D.RegisterHandler(TEXT("duplicate_actor"),              MakeHandler(LevelActorTools::HandleDuplicateActorFromArgs));
	D.RegisterHandler(TEXT("duplicate_actor_in_level"),     MakeHandler(LevelActorTools::HandleDuplicateActorFromArgs));
	D.RegisterHandler(TEXT("remove_actor_tag"),             MakeHandler(LevelActorTools::HandleRemoveActorTagFromArgs));
	D.RegisterHandler(TEXT("set_actor_property"),           MakeHandler(LevelActorTools::HandleSetActorPropertyFromArgs));
	D.RegisterHandler(TEXT("set_actor_transform"),          MakeHandler(LevelActorTools::HandleSetActorTransformFromArgs));
	D.RegisterHandler(TEXT("set_light_property"),           MakeHandler(LevelActorTools::HandleSetLightPropertyFromArgs));
	D.RegisterHandler(TEXT("set_actor_material"),           MakeHandler(LevelActorTools::HandleSetActorMaterialsFromArgs));
	D.RegisterHandler(TEXT("set_actor_materials"),          MakeHandler(LevelActorTools::HandleSetActorMaterialsFromArgs));
	D.RegisterHandler(TEXT("set_multiple_actor_materials"), MakeHandler(LevelActorTools::HandleSetActorMaterialsFromArgs));
	D.RegisterHandler(TEXT("spawn_actor"),                  MakeHandler(LevelActorTools::HandleSpawnActorFromArgs));
	D.RegisterHandler(TEXT("spawn_actors"),                 MakeHandler(LevelActorTools::HandleSpawnActorFromArgs));
	D.RegisterHandler(TEXT("spawn_multiple_actors"),        MakeHandler(LevelActorTools::HandleSpawnActorFromArgs));
	D.RegisterHandler(TEXT("spawn_color_correction_region"), MakeHandler(LevelActorTools::HandleSpawnColorCorrectionRegionFromArgs));
	D.RegisterHandler(TEXT("spawn_color_correction_window"), MakeHandler(LevelActorTools::HandleSpawnColorCorrectionWindowFromArgs));
	D.RegisterHandler(TEXT("get_all_scene_actors"),         MakeHandler(LevelActorTools::HandleGetAllSceneActorsFromArgs));
	D.RegisterHandler(TEXT("get_all_level_actors"),         MakeHandler(LevelActorTools::HandleGetAllSceneActorsFromArgs));
	D.RegisterHandler(TEXT("list_level_actors"),            MakeHandler(LevelActorTools::HandleGetAllSceneActorsFromArgs));
	D.RegisterHandler(TEXT("list_actors"),                  MakeHandler(LevelActorTools::HandleGetAllSceneActorsFromArgs));
	D.RegisterHandler(TEXT("get_selected_level_actors"),    MakeHandler(LevelActorTools::HandleGetSelectedLevelActorsFromArgs));
	D.RegisterHandler(TEXT("get_selected_actors"),          MakeHandler(LevelActorTools::HandleGetSelectedLevelActorsFromArgs));
	D.RegisterHandler(TEXT("select_actors"),                MakeHandler(LevelActorTools::HandleSelectActorsFromArgs));
	D.RegisterHandler(TEXT("get_actor_details"),            MakeHandler(LevelActorTools::HandleGetActorDetailsFromArgs));
	D.RegisterHandler(TEXT("rename_actor"),                 MakeHandler(LevelActorTools::HandleRenameActorFromArgs));
	D.RegisterHandler(TEXT("get_actors_by_class"),          MakeHandler(LevelActorTools::HandleGetActorsByClassFromArgs));
	D.RegisterHandler(TEXT("find_actors_by_class"),         MakeHandler(LevelActorTools::HandleGetActorsByClassFromArgs));
	D.RegisterHandler(TEXT("get_actors_in_box"),            MakeHandler(LevelActorTools::HandleGetActorsInBoxFromArgs));
	D.RegisterHandler(TEXT("get_actor_components"),         MakeHandler(LevelActorTools::HandleGetActorComponentsFromArgs));
	D.RegisterHandler(TEXT("get_level_info"),               MakeHandler(LevelActorTools::HandleGetLevelInfoFromArgs));
	D.RegisterHandler(TEXT("get_current_level"),            MakeHandler(LevelActorTools::HandleGetLevelInfoFromArgs));
	D.RegisterHandler(TEXT("set_actor_visibility"),         MakeHandler(LevelActorTools::HandleSetActorVisibilityFromArgs));
	D.RegisterHandler(TEXT("bulk_transform_actors"),        MakeHandler(LevelActorTools::HandleBulkTransformActorsFromArgs));
	D.RegisterHandler(TEXT("align_actors_to_floor"),        MakeHandler(LevelActorTools::HandleAlignActorsToFloorFromArgs));
	D.RegisterHandler(TEXT("detach_actor"),                 MakeHandler(LevelActorTools::HandleDetachActorFromArgs));
	D.RegisterHandler(TEXT("move_actor_to_folder"),         MakeHandler(LevelActorTools::HandleMoveActorToFolderFromArgs));
	D.RegisterHandler(TEXT("move_actors_to_folder"),        MakeHandler(LevelActorTools::HandleMoveActorToFolderFromArgs));
	D.RegisterHandler(TEXT("set_static_mesh"),              MakeHandler(LevelActorTools::HandleSetStaticMeshFromArgs));
	D.RegisterHandler(TEXT("set_actor_physics"),            MakeHandler(LevelActorTools::HandleSetActorPhysicsFromArgs));
	D.RegisterHandler(TEXT("get_actor_tags"),               MakeHandler(LevelActorTools::HandleGetActorTagsFromArgs));
	D.RegisterHandler(TEXT("set_world_settings"),           MakeHandler(LevelActorTools::HandleSetWorldSettingsFromArgs));
	D.RegisterHandler(TEXT("focus_viewport_on_actor"),      MakeHandler(LevelActorTools::HandleFocusViewportOnActorFromArgs));
	D.RegisterHandler(TEXT("replace_actor"),                MakeHandler(LevelActorTools::HandleReplaceActorFromArgs));
	D.RegisterHandler(TEXT("set_actor_replication"),        MakeHandler(LevelActorTools::HandleSetActorReplicationFromArgs));
	D.RegisterHandler(TEXT("find_actors_by_bounds"),        MakeHandler(LevelActorTools::HandleFindActorsByBoundsFromArgs));
	D.RegisterHandler(TEXT("begin_play_in_editor"),         MakeHandler(LevelActorTools::HandleBeginPlayInEditorFromArgs));
	D.RegisterHandler(TEXT("stop_play_in_editor"),          MakeHandler(LevelActorTools::HandleStopPlayInEditorFromArgs));
	D.RegisterHandler(TEXT("open_level"),                   MakeHandler(LevelActorTools::HandleOpenLevelFromArgs));
	D.RegisterHandler(TEXT("save_current_level"),           MakeHandler(LevelActorTools::HandleSaveCurrentLevelFromArgs));
	D.RegisterHandler(TEXT("save_current_level_as"),        MakeHandler(LevelActorTools::HandleSaveCurrentLevelAsFromArgs));
	D.RegisterHandler(TEXT("get_asset_references"),         MakeHandler(LevelActorTools::HandleGetAssetReferencesFromArgs));
	D.RegisterHandler(TEXT("create_data_layer"),            MakeHandler(LevelActorTools::HandleCreateDataLayerFromArgs));
	D.RegisterHandler(TEXT("assign_actor_to_data_layer"),   MakeHandler(LevelActorTools::HandleAssignActorToDataLayerFromArgs));
	D.RegisterHandler(TEXT("set_data_layer_state"),         MakeHandler(LevelActorTools::HandleSetDataLayerStateFromArgs));
	D.RegisterHandler(TEXT("list_data_layers"),             MakeHandler(LevelActorTools::HandleListDataLayersFromArgs));
	D.RegisterHandler(TEXT("get_data_layer_actors"),        MakeHandler(LevelActorTools::HandleGetDataLayerActorsFromArgs));
	D.RegisterHandler(TEXT("create_level_instance"),        MakeHandler(LevelActorTools::HandleCreateLevelInstanceFromArgs));
	D.RegisterHandler(TEXT("get_level_instances"),          MakeHandler(LevelActorTools::HandleGetLevelInstancesFromArgs));
	D.RegisterHandler(TEXT("enable_world_partition"),       MakeHandler(LevelActorTools::HandleEnableWorldPartitionFromArgs));
	D.RegisterHandler(TEXT("disable_world_partition"),      MakeHandler(LevelActorTools::HandleDisableWorldPartitionFromArgs));
	D.RegisterHandler(TEXT("get_world_partition_info"),     MakeHandler(LevelActorTools::HandleGetWorldPartitionInfoFromArgs));
	D.RegisterHandler(TEXT("set_hlod_layer_properties"),    MakeHandler(LevelActorTools::HandleSetHLODLayerPropertiesFromArgs));
	D.RegisterHandler(TEXT("list_hlod_layers"),             MakeHandler(LevelActorTools::HandleListHLODLayersFromArgs));
	D.RegisterHandler(TEXT("set_actors_property_by_filter"), MakeHandler(LevelActorTools::HandleSetActorsPropertyByFilterFromArgs));
	D.RegisterHandler(TEXT("set_cine_camera_properties"),   MakeHandler(LevelActorTools::HandleSetCineCameraPropertiesFromArgs));
	D.RegisterHandler(TEXT("select_actors_by_label"),       MakeHandler(LevelActorTools::HandleSetSelectedActorsFromArgs));
	D.RegisterHandler(TEXT("set_selected_actors"),          MakeHandler(LevelActorTools::HandleSetSelectedActorsFromArgs));
	D.RegisterHandler(TEXT("spawn_advanced"),               MakeHandler(LevelActorTools::HandleAdvancedSpawnFromArgs));

	D.RegisterHandler(TEXT("add_streaming_level"),              MakeHandler(LevelStreamingTools::HandleAddStreamingLevelFromArgs));
	D.RegisterHandler(TEXT("set_streaming_level_transform"),    MakeHandler(LevelStreamingTools::HandleSetStreamingLevelTransformFromArgs));
	D.RegisterHandler(TEXT("create_level_streaming_volume"),    MakeHandler(LevelStreamingTools::HandleCreateLevelStreamingVolumeFromArgs));
	D.RegisterHandler(TEXT("remove_streaming_level"),           MakeHandler(LevelStreamingTools::HandleRemoveStreamingLevelFromArgs));
	D.RegisterHandler(TEXT("get_streaming_levels"),             MakeHandler(LevelStreamingTools::HandleGetStreamingLevelsFromArgs));
	D.RegisterHandler(TEXT("set_streaming_level_visibility"),   MakeHandler(LevelStreamingTools::HandleSetStreamingLevelVisibilityFromArgs));
	D.RegisterHandler(TEXT("set_streaming_level_loaded"),       MakeHandler(LevelStreamingTools::HandleSetStreamingLevelLoadedFromArgs));

	D.RegisterHandler(TEXT("add_pcg_node"),                  MakeHandler(PCGTools::HandleAddPCGNodeFromArgs));
	D.RegisterHandler(TEXT("connect_pcg_nodes"),             MakeHandler(PCGTools::HandleConnectPCGNodesFromArgs));
	D.RegisterHandler(TEXT("set_pcg_mesh_spawner"),          MakeHandler(PCGTools::HandleSetPCGMeshSpawnerFromArgs));
	D.RegisterHandler(TEXT("set_pcg_node_property"),         MakeHandler(PCGTools::HandleSetPCGNodePropertyFromArgs));
	D.RegisterHandler(TEXT("get_pcg_graph_summary"),         MakeHandler(PCGTools::HandleGetPCGGraphSummaryFromArgs));
	D.RegisterHandler(TEXT("remove_pcg_node"),               MakeHandler(PCGTools::HandleRemovePCGNodeFromArgs));
	D.RegisterHandler(TEXT("spawn_pcg_actor"),               MakeHandler(PCGTools::HandleSpawnPCGActorFromArgs));
	D.RegisterHandler(TEXT("set_pcg_actor_bounds"),          MakeHandler(PCGTools::HandleSetPCGActorBoundsFromArgs));
	D.RegisterHandler(TEXT("get_pcg_actor_info"),            MakeHandler(PCGTools::HandleGetPCGActorInfoFromArgs));
	D.RegisterHandler(TEXT("set_pcg_transform_randomizer"),  MakeHandler(PCGTools::HandleSetPCGTransformRandomizerFromArgs));
	D.RegisterHandler(TEXT("generate_pcg"),                  MakeHandler(PCGTools::HandleGeneratePCGFromArgs));
	D.RegisterHandler(TEXT("update_pcg_actor_graph"),        MakeHandler(PCGTools::HandleUpdatePCGActorGraphFromArgs));
	D.RegisterHandler(TEXT("list_pcg_node_types"),           MakeHandler(PCGTools::HandleListPCGNodeTypesFromArgs));
	D.RegisterHandler(TEXT("disconnect_pcg_nodes"),          MakeHandler(PCGTools::HandleDisconnectPCGNodesFromArgs));
	D.RegisterHandler(TEXT("get_pcg_node_properties"),       MakeHandler(PCGTools::HandleGetPCGNodePropertiesFromArgs));
	D.RegisterHandler(TEXT("build_pcg_graph"),               MakeHandler(PCGTools::HandleBuildPCGGraphFromArgs));
	D.RegisterHandler(TEXT("set_pcg_slope_filter"),          MakeHandler(PCGTools::HandleSetPCGSlopeFilterFromArgs));
	D.RegisterHandler(TEXT("set_pcg_noise_density"),         MakeHandler(PCGTools::HandleSetPCGNoiseDensityFromArgs));
	D.RegisterHandler(TEXT("set_pcg_spline_sampler"),        MakeHandler(PCGTools::HandleSetPCGSplineSamplerFromArgs));
	D.RegisterHandler(TEXT("clear_pcg_graph"),               MakeHandler(PCGTools::HandleClearPCGGraphFromArgs));
	D.RegisterHandler(TEXT("set_pcg_component_properties"),  MakeHandler(PCGTools::HandleSetPCGComponentPropertiesFromArgs));
	D.RegisterHandler(TEXT("duplicate_pcg_node"),            MakeHandler(PCGTools::HandleDuplicatePCGNodeFromArgs));
	D.RegisterHandler(TEXT("set_pcg_node_comment"),          MakeHandler(PCGTools::HandleSetPCGNodeCommentFromArgs));
	D.RegisterHandler(TEXT("add_pcg_graph_parameter"),       MakeHandler(PCGTools::HandleAddPCGGraphParameterFromArgs));
	D.RegisterHandler(TEXT("get_pcg_graph_parameters"),      MakeHandler(PCGTools::HandleGetPCGGraphParametersFromArgs));
	D.RegisterHandler(TEXT("set_pcg_texture_sampler"),       MakeHandler(PCGTools::HandleSetPCGTextureSamplerFromArgs));
	D.RegisterHandler(TEXT("set_pcg_data_from_actor"),       MakeHandler(PCGTools::HandleSetPCGDataFromActorFromArgs));
	D.RegisterHandler(TEXT("set_pcg_attribute_filter"),      MakeHandler(PCGTools::HandleSetPCGAttributeFilterFromArgs));
	D.RegisterHandler(TEXT("duplicate_pcg_graph"),           MakeHandler(PCGTools::HandleDuplicatePCGGraphFromArgs));
	D.RegisterHandler(TEXT("create_pcg_subgraph_node"),      MakeHandler(PCGTools::HandleCreatePCGSubgraphNodeFromArgs));
	D.RegisterHandler(TEXT("set_pcg_self_pruning"),          MakeHandler(PCGTools::HandleSetPCGSelfPruningFromArgs));
	D.RegisterHandler(TEXT("set_pcg_create_attribute"),      MakeHandler(PCGTools::HandleSetPCGCreateAttributeFromArgs));
	D.RegisterHandler(TEXT("set_pcg_attribute_math"),        MakeHandler(PCGTools::HandleSetPCGAttributeMathFromArgs));
	D.RegisterHandler(TEXT("set_pcg_exclusion"),             MakeHandler(PCGTools::HandleSetPCGExclusionFromArgs));
	D.RegisterHandler(TEXT("get_pcg_level_summary"),         MakeHandler(PCGTools::HandleGetPCGLevelSummaryFromArgs));

	D.RegisterHandler(TEXT("add_foliage_type"),                       MakeHandler(FoliageTools::HandleAddFoliageTypeFromArgs));
	D.RegisterHandler(TEXT("paint_foliage"),                          MakeHandler(FoliageTools::HandlePaintFoliageFromArgs));
	D.RegisterHandler(TEXT("set_foliage_density"),                    MakeHandler(FoliageTools::HandleSetFoliageDensityFromArgs));
	D.RegisterHandler(TEXT("clear_foliage"),                          MakeHandler(FoliageTools::HandleClearFoliageFromArgs));
	D.RegisterHandler(TEXT("set_foliage_type_properties"),            MakeHandler(FoliageTools::HandleSetFoliageTypePropertiesFromArgs));
	D.RegisterHandler(TEXT("assign_physical_material_to_foliage"),    MakeHandler(FoliageTools::HandleAssignPhysicalMaterialToFoliageFromArgs));
	D.RegisterHandler(TEXT("get_foliage_summary"),                    MakeHandler(FoliageTools::HandleGetFoliageSummaryFromArgs));
	D.RegisterHandler(TEXT("create_procedural_foliage_spawner"),      MakeHandler(FoliageTools::HandleCreateProceduralFoliageSpawnerFromArgs));
	D.RegisterHandler(TEXT("spawn_procedural_foliage_volume"),        MakeHandler(FoliageTools::HandleSpawnProceduralFoliageVolumeFromArgs));

	D.RegisterHandler(TEXT("create_post_process_volume"),            MakeHandler(PostProcessTools::HandleCreatePostProcessVolumeFromArgs));
	D.RegisterHandler(TEXT("set_post_process_property"),             MakeHandler(PostProcessTools::HandleSetPostProcessPropertyFromArgs));
	D.RegisterHandler(TEXT("add_mpc_parameter"),                     MakeHandler(PostProcessTools::HandleAddMPCParameterFromArgs));
	D.RegisterHandler(TEXT("get_post_process_summary"),              MakeHandler(PostProcessTools::HandleGetPostProcessSummaryFromArgs));
	D.RegisterHandler(TEXT("add_post_process_blendable"),            MakeHandler(PostProcessTools::HandleAddPostProcessBlendableFromArgs));
	D.RegisterHandler(TEXT("set_mpc_default_value"),                 MakeHandler(PostProcessTools::HandleSetMPCDefaultValueFromArgs));
	D.RegisterHandler(TEXT("remove_post_process_blendable"),         MakeHandler(PostProcessTools::HandleRemovePostProcessBlendableFromArgs));
	D.RegisterHandler(TEXT("set_post_process_priority"),             MakeHandler(PostProcessTools::HandleSetPostProcessPriorityFromArgs));
	D.RegisterHandler(TEXT("get_mpc_summary"),                       MakeHandler(PostProcessTools::HandleGetMPCSummaryFromArgs));

	D.RegisterHandler(TEXT("add_spline_point"),                      MakeHandler(SplineTools::HandleAddSplinePointFromArgs));
	D.RegisterHandler(TEXT("set_spline_point_tangent"),              MakeHandler(SplineTools::HandleSetSplinePointTangentFromArgs));
	D.RegisterHandler(TEXT("add_spline_component"),                  MakeHandler(SplineTools::HandleAddSplineComponentFromArgs));
	D.RegisterHandler(TEXT("set_spline_points"),                     MakeHandler(SplineTools::HandleSetSplinePointsFromArgs));
	D.RegisterHandler(TEXT("spawn_spline_actor"),                    MakeHandler(SplineTools::HandleSpawnSplineActorFromArgs));
	D.RegisterHandler(TEXT("get_spline_info"),                       MakeHandler(SplineTools::HandleGetSplineInfoFromArgs));
	D.RegisterHandler(TEXT("remove_spline_point"),                   MakeHandler(SplineTools::HandleRemoveSplinePointFromArgs));
	D.RegisterHandler(TEXT("set_spline_properties"),                 MakeHandler(SplineTools::HandleSetSplinePropertiesFromArgs));
	D.RegisterHandler(TEXT("set_spline_mesh"),                       MakeHandler(SplineTools::HandleSetSplineMeshFromArgs));
	D.RegisterHandler(TEXT("set_spline_mesh_component_properties"),  MakeHandler(SplineTools::HandleSetSplineMeshComponentPropertiesFromArgs));
	D.RegisterHandler(TEXT("scatter_actors_along_spline"),           MakeHandler(SplineTools::HandleScatterActorsAlongSplineFromArgs));

	{
		IUECPCreateAssetRegistry& Reg = IUECPCoreModule::Get().GetCreateAssetRegistry();
		const FName ExtId(TEXT("LevelDesign"));
		Reg.RegisterType(TEXT("HLODLayer"),            UECPCreateAsset::FactoryFromArgsFn(&LevelActorTools::HandleCreateHLODLayerFromArgs,             TEXT("HLODLayer")),            ExtId);
		Reg.RegisterType(TEXT("PCGGraph"),             UECPCreateAsset::FactoryFromArgsFn(&PCGTools::HandleCreatePCGGraphFromArgs,                     TEXT("PCGGraph")),             ExtId);
		Reg.RegisterType(TEXT("RenderTarget"),         UECPCreateAsset::FactoryFromArgsFn(&PostProcessTools::HandleCreateRenderTargetFromArgs,         TEXT("RenderTarget")),         ExtId);
		Reg.RegisterType(TEXT("RuntimeVirtualTexture"),UECPCreateAsset::FactoryFromArgsFn(&EnvironmentTools::HandleCreateRuntimeVirtualTextureFromArgs,TEXT("RuntimeVirtualTexture")),ExtId);
	}

	{
		const FName U(TEXT("landscape"));
		const auto Meta = [&D, &U](const TCHAR* Action, const TCHAR* Summary, const TCHAR* Params)
		{ D.RegisterToolMetadata({ FName(Action), U, FString(Summary), FString(Params) }); };

		Meta(TEXT("create_landscape"),         TEXT("Create a Landscape actor (preset terrain shapes)."), TEXT("preset?, size_x?, size_y?, scale_z?, mountain_height?, center_flat_radius?, location_x/y/z?"));
		Meta(TEXT("set_landscape_material"),    TEXT("Assign a material to a Landscape."), TEXT("actor_label, material_path"));
		Meta(TEXT("get_landscape_info"),        TEXT("Get the active Landscape's bounds/info."), TEXT(""));
		Meta(TEXT("set_landscape_properties"),  TEXT("Set a Landscape property."), TEXT("property_name, property_value"));
		Meta(TEXT("set_landscape_lod"),         TEXT("Set Landscape static-lighting LOD."), TEXT("static_lighting_lod"));
		Meta(TEXT("export_landscape_heightmap"),TEXT("Export the Landscape heightmap to a file on disk (.png 16-bit / .raw); returns the written file_path + width/height + file_size_bytes."), TEXT("file_path, actor_label?"));
		Meta(TEXT("add_landscape_layer_info"),  TEXT("Create a landscape layer info asset and register it as a target layer on the landscape (attaches to an existing material-declared target layer if one exists with a null LayerInfo). Returns known_layers[] so callers can confirm the layer is actually usable by world_landscape_paint_layer."), TEXT("actor_label?, layer_name, save_path?=/Game/Landscape"));
		Meta(TEXT("populate_landscape"),        TEXT("Scatter meshes onto a landscape via a generated PCG graph (surface sampler + slope filter + optional min spacing + yaw-only transform + mesh spawner)."), TEXT("mesh_paths, landscape_label?, layer_name?, density?, min_scale?, max_scale?, cull_distance?, max_slope_degrees?=35, min_spacing?=0"));
	}

	{
		const FName U(TEXT("environment"));
		const auto Meta = [&D, &U](const TCHAR* Action, const TCHAR* Summary, const TCHAR* Params)
		{ D.RegisterToolMetadata({ FName(Action), U, FString(Summary), FString(Params) }); };

		Meta(TEXT("set_sky_light_properties"),             TEXT("Set sky light intensity/color/source/mobility."), TEXT("actor_label, intensity?, light_color?, source_type?, mobility?"));
		Meta(TEXT("set_exponential_height_fog_properties"),TEXT("Set height-fog density/falloff/color/volumetric."), TEXT("actor_label, fog_density?, height_falloff?, inscattering_color?, start_distance?, fog_max_opacity?, volumetric_fog?"));
		Meta(TEXT("set_sky_atmosphere_properties"),        TEXT("Set a SkyAtmosphere property."), TEXT("actor_label, property_name, property_value"));
		Meta(TEXT("set_volumetric_cloud_properties"),      TEXT("Set a VolumetricCloud property."), TEXT("actor_label, property_name, property_value"));
		Meta(TEXT("spawn_environment_actor"),              TEXT("Spawn a sky/fog/atmosphere/cloud/light/reflection actor."), TEXT("actor_type, label, location_x/y/z?"));
		Meta(TEXT("get_environment_summary"),              TEXT("Summarise the level's environment actors."), TEXT(""));
		Meta(TEXT("set_mpc_parameter_value"),              TEXT("Set a Material Parameter Collection value."), TEXT("asset_path, parameter_name, scalar_value|vector_r/g/b/a, is_scalar?"));
		Meta(TEXT("set_directional_light_properties"),     TEXT("Set sun light intensity/color/angle/shadows."), TEXT("actor_label, intensity?, light_color?, rotation_pitch?, rotation_yaw?, mobility?, cast_shadows?"));
		Meta(TEXT("spawn_rvt_volume"),                     TEXT("Spawn a Runtime Virtual Texture volume."), TEXT("actor_label, rvt_asset_path, location_x/y/z?, extent_x/y/z?"));
	}

	{
		const FName U(TEXT("level_actor"));
		const auto Meta = [&D, &U](const TCHAR* Action, const TCHAR* Summary, const TCHAR* Params)
		{ D.RegisterToolMetadata({ FName(Action), U, FString(Summary), FString(Params) }); };

		Meta(TEXT("spawn_actor"),               TEXT("Place an actor / static mesh shape into the level by class. Basic shapes (Place Actors > Shapes): Cube, Sphere, Cylinder, Cone, Plane spawn as StaticMeshActors from /Engine/BasicShapes. Also accepts a /Game asset or /Script class path."), TEXT("actor_class (Cube|Sphere|Cylinder|Cone|Plane|class path), actor_label, location?, rotation? {yaw,pitch,roll} or [pitch,yaw,roll] degrees, scale?"));
		Meta(TEXT("spawn_actors"),              TEXT("Batch-place actors / basic shapes (Cube|Sphere|Cylinder|Cone|Plane) into the level."), TEXT("actors=[{label, actor_class, location?, rotation? {yaw,pitch,roll} or [pitch,yaw,roll] degrees, scale?}], allow_tilt? (array roll>45 is rejected unless true)"));
		Meta(TEXT("spawn_multiple_actors"),     TEXT("Alias of spawn_actors."), TEXT("actors=[...]"));
		Meta(TEXT("spawn_advanced"),            TEXT("Scatter N actors (or one HISM) inside a bounding actor / box, snapped to the ground. Yaw-only rotation unless allow_tilt."), TEXT("count, actor_class (class|StaticMesh path|Cube..), bounding_actor_label? | random_location_min/max?, random_scale_min/max?, random_rotation_min/max? {yaw,pitch,roll} or [pitch,yaw,roll] degrees (yaw-only unless allow_tilt), align_to_ground?=true, align_to_normal?=false, allow_tilt?=false, uniform_scale?=true, spawn_mode? actors|ism, label?"));
		Meta(TEXT("spawn_color_correction_region"), TEXT("Spawn a Color Correction Region (needs CCR plugin)."), TEXT("actor_label, shape?, location?"));
		Meta(TEXT("spawn_color_correction_window"), TEXT("Spawn a Color Correction Window."), TEXT("actor_label, shape?, location?"));
		Meta(TEXT("set_actor_transform"),       TEXT("Set an actor's transform (partial OK)."), TEXT("actor_label, location?, rotation? {yaw,pitch,roll} or [pitch,yaw,roll] degrees, scale?"));
		Meta(TEXT("set_actor_property"),        TEXT("Set an actor/component property."), TEXT("actor_label, property_name, property_value"));
		Meta(TEXT("set_actors_property_by_filter"), TEXT("Set a property on all actors matching a class filter."), TEXT("class_filter, property_name, property_value"));
		Meta(TEXT("bulk_transform_actors"),     TEXT("Batch-set transforms."), TEXT("actors=[{label, location?, rotation? {yaw,pitch,roll} or [pitch,yaw,roll] degrees, scale?}]"));
		Meta(TEXT("align_actors_to_floor"),     TEXT("Drop actors onto the floor via trace."), TEXT("actor_labels, trace_offset?"));
		Meta(TEXT("set_actor_visibility"),      TEXT("Show/hide actors."), TEXT("actor_labels, visible"));
		Meta(TEXT("set_actor_physics"),         TEXT("Set simulate-physics/gravity/mass/damping."), TEXT("actor_label, simulate_physics?, enable_gravity?, mass?, linear_damping?, angular_damping?"));
		Meta(TEXT("set_actor_replication"),     TEXT("Set replicate/movement/net settings."), TEXT("actor_label, replicate?, replicate_movement?, net_update_frequency?, net_cull_distance?"));
		Meta(TEXT("set_light_property"),        TEXT("Set light intensity/color/radius/shadows (color=[R,G,B], batch)."), TEXT("actor_label, intensity?, color?, attenuation_radius?, cast_shadows?, temperature?"));
		Meta(TEXT("set_cine_camera_properties"),TEXT("Set CineCamera focal/aperture/focus."), TEXT("actor_label, focal_length?, aperture?, focus_distance?"));
		Meta(TEXT("set_static_mesh"),           TEXT("Set a static-mesh actor's mesh."), TEXT("actor_label, mesh_path"));
		Meta(TEXT("replace_actor"),             TEXT("Replace an actor with another class/mesh."), TEXT("actor_label, new_class_or_mesh"));
		Meta(TEXT("add_actor_tag"),             TEXT("Add an actor tag (batch)."), TEXT("actor_label, tag"));
		Meta(TEXT("remove_actor_tag"),          TEXT("Remove an actor tag (batch)."), TEXT("actor_label, tag"));
		Meta(TEXT("get_actor_tags"),            TEXT("List an actor's tags."), TEXT("actor_label"));
		Meta(TEXT("attach_actor"),              TEXT("Attach an actor to a parent (optional socket)."), TEXT("actor_label, parent_label, keep_world_transform?, socket_name?"));
		Meta(TEXT("detach_actor"),              TEXT("Detach an actor from its parent."), TEXT("actor_label"));
		Meta(TEXT("move_actor_to_folder"),      TEXT("Move an actor to an outliner folder."), TEXT("actor_labels, folder_path"));
		Meta(TEXT("move_actors_to_folder"),     TEXT("Alias of move_actor_to_folder."), TEXT("actor_labels, folder_path"));
		Meta(TEXT("rename_actor"),              TEXT("Rename an actor."), TEXT("actor_label, new_name"));
		Meta(TEXT("duplicate_actor"),           TEXT("Duplicate an actor at an offset."), TEXT("actor_label, offset?, new_label?"));
		Meta(TEXT("duplicate_actor_in_level"),  TEXT("Alias of duplicate_actor."), TEXT("actor_label, offset?, new_label?"));
		Meta(TEXT("delete_actor"),              TEXT("Delete actor(s)."), TEXT("actor_labels"));
		Meta(TEXT("delete_actors"),             TEXT("Alias of delete_actor."), TEXT("actor_labels"));
		Meta(TEXT("delete_actor_from_level"),   TEXT("Alias of delete_actor."), TEXT("actor_labels"));
		Meta(TEXT("select_actors"),             TEXT("Set the editor selection to named actors."), TEXT("actor_labels"));
		Meta(TEXT("select_actors_by_label"),    TEXT("Select actors by label."), TEXT("actor_labels"));
		Meta(TEXT("set_selected_actors"),       TEXT("Set the editor selection."), TEXT("actor_labels"));
		Meta(TEXT("get_selected_level_actors"), TEXT("Get the current editor selection."), TEXT(""));
		Meta(TEXT("get_selected_actors"),       TEXT("Alias of get_selected_level_actors."), TEXT(""));
		Meta(TEXT("focus_viewport_on_actor"),   TEXT("Frame the viewport on an actor."), TEXT("actor_label"));
		Meta(TEXT("get_all_scene_actors"),      TEXT("List every level actor."), TEXT(""));
		Meta(TEXT("get_all_level_actors"),      TEXT("Alias of get_all_scene_actors."), TEXT(""));
		Meta(TEXT("list_level_actors"),         TEXT("Alias of get_all_scene_actors."), TEXT(""));
		Meta(TEXT("list_actors"),               TEXT("Alias of get_all_scene_actors."), TEXT(""));
		Meta(TEXT("get_actor_details"),         TEXT("Get a single actor's details."), TEXT("actor_label"));
		Meta(TEXT("get_actors_by_class"),       TEXT("Find actors by class (substring; empty=all)."), TEXT("class_name"));
		Meta(TEXT("find_actors_by_class"),      TEXT("Alias of get_actors_by_class."), TEXT("class_name"));
		Meta(TEXT("get_actors_in_box"),         TEXT("Find actors within an AABB."), TEXT("min_x/y/z, max_x/y/z"));
		Meta(TEXT("find_actors_by_bounds"),     TEXT("Find actors within center+extent."), TEXT("center, extent"));
		Meta(TEXT("get_actor_components"),      TEXT("List an actor's components."), TEXT("actor_label"));
		Meta(TEXT("get_asset_references"),      TEXT("List assets an actor references."), TEXT("actor_label"));
		Meta(TEXT("get_level_info"),            TEXT("Get current-level info."), TEXT(""));
		Meta(TEXT("get_current_level"),         TEXT("Alias of get_level_info."), TEXT(""));
		Meta(TEXT("set_world_settings"),        TEXT("Set gravity/kill-z/default game mode."), TEXT("gravity_z?, kill_z?, default_game_mode?"));
		Meta(TEXT("begin_play_in_editor"),      TEXT("Start Play In Editor."), TEXT(""));
		Meta(TEXT("stop_play_in_editor"),       TEXT("Stop Play In Editor."), TEXT(""));
		Meta(TEXT("open_level"),                TEXT("Open (load) an existing level/map asset in the editor."), TEXT("level_path"));
		Meta(TEXT("save_current_level"),        TEXT("Save the current level to its existing asset (use save_current_level_as for an unsaved 'Untitled' map)."), TEXT(""));
		Meta(TEXT("save_current_level_as"),     TEXT("Save the current (e.g. transient/Untitled) level as a NEW .umap asset and make it the active level."), TEXT("package_path, level_name"));
		Meta(TEXT("create_data_layer"),         TEXT("Create a data layer."), TEXT("name"));
		Meta(TEXT("assign_actor_to_data_layer"),TEXT("Assign an actor to a data layer."), TEXT("actor_label, data_layer"));
		Meta(TEXT("set_data_layer_state"),      TEXT("Set a data layer's runtime state."), TEXT("data_layer, state"));
		Meta(TEXT("list_data_layers"),          TEXT("List data layers."), TEXT(""));
		Meta(TEXT("get_data_layer_actors"),     TEXT("List actors in a data layer."), TEXT("data_layer"));
		Meta(TEXT("create_level_instance"),     TEXT("Create a level instance actor."), TEXT("actor_label, level_path, location_x/y/z?"));
		Meta(TEXT("get_level_instances"),       TEXT("List level instances."), TEXT(""));
		Meta(TEXT("enable_world_partition"),    TEXT("Enable World Partition."), TEXT(""));
		Meta(TEXT("disable_world_partition"),   TEXT("Disable World Partition."), TEXT(""));
		Meta(TEXT("get_world_partition_info"),  TEXT("Get World Partition info."), TEXT(""));
		Meta(TEXT("set_hlod_layer_properties"), TEXT("Set HLOD layer properties."), TEXT("hlod_layer_path, <properties>"));
		Meta(TEXT("list_hlod_layers"),          TEXT("List HLOD layers."), TEXT(""));
	}

	{
		const FName U(TEXT("material"));
		const auto Meta = [&D, &U](const TCHAR* Action, const TCHAR* Summary, const TCHAR* Params)
		{ D.RegisterToolMetadata({ FName(Action), U, FString(Summary), FString(Params) }); };

		Meta(TEXT("set_actor_material"),         TEXT("Assign a material to a level actor's slot(s)."), TEXT("actor_label|actor_labels, material_path, slot_index?"));
		Meta(TEXT("set_actor_materials"),        TEXT("Alias of set_actor_material."), TEXT("actor_label|actor_labels, material_path, slot_index?"));
		Meta(TEXT("set_multiple_actor_materials"),TEXT("Alias of set_actor_material."), TEXT("actor_label|actor_labels, material_path, slot_index?"));
	}

	{
		const FName U(TEXT("level_streaming"));
		const auto Meta = [&D, &U](const TCHAR* Action, const TCHAR* Summary, const TCHAR* Params)
		{ D.RegisterToolMetadata({ FName(Action), U, FString(Summary), FString(Params) }); };

		Meta(TEXT("add_streaming_level"),            TEXT("Bind a sublevel into the persistent world."), TEXT("level_path, streaming_type?, offset_x/y/z?"));
		Meta(TEXT("set_streaming_level_transform"),  TEXT("Set a streamed sublevel's transform offset."), TEXT("level_path, offset_x/y/z?, rot_pitch/yaw/roll?, scale_x/y/z?"));
		Meta(TEXT("create_level_streaming_volume"),  TEXT("Create a streaming volume (extent in cm)."), TEXT("level_path, location_x/y/z, extent_x/y/z, usage?"));
		Meta(TEXT("remove_streaming_level"),         TEXT("Unbind a sublevel from the persistent world."), TEXT("level_path"));
		Meta(TEXT("get_streaming_levels"),           TEXT("List bound streaming levels + state."), TEXT(""));
		Meta(TEXT("set_streaming_level_visibility"), TEXT("Toggle a sublevel's ShouldBeVisible."), TEXT("level_path, visible"));
		Meta(TEXT("set_streaming_level_loaded"),     TEXT("Toggle a sublevel's ShouldBeLoaded."), TEXT("level_path, loaded"));
	}

	{
		const FName U(TEXT("pcg"));
		const auto Meta = [&D, &U](const TCHAR* Action, const TCHAR* Summary, const TCHAR* Params)
		{ D.RegisterToolMetadata({ FName(Action), U, FString(Summary), FString(Params) }); };

		Meta(TEXT("build_pcg_graph"),             TEXT("Build a whole PCG graph (nodes+edges, auto-clear). Preferred."), TEXT("graph_path, nodes_json, edges_json, clear_existing?"));
		Meta(TEXT("get_pcg_graph_summary"),       TEXT("Summarise a PCG graph (CALL after every build)."), TEXT("graph_path"));
		Meta(TEXT("list_pcg_node_types"),         TEXT("List available PCG node settings classes."), TEXT(""));
		Meta(TEXT("add_pcg_node"),                TEXT("Add a PCG node by settings class."), TEXT("graph_path, settings_class, pos_x?, pos_y?"));
		Meta(TEXT("remove_pcg_node"),             TEXT("Remove a PCG node by index."), TEXT("graph_path, node_index"));
		Meta(TEXT("duplicate_pcg_node"),          TEXT("Duplicate a node + its property values."), TEXT("graph_path, node_index, pos_x?, pos_y?"));
		Meta(TEXT("connect_pcg_nodes"),           TEXT("Connect two PCG node pins."), TEXT("graph_path, from_node_index, to_node_index, from_pin?, to_pin?"));
		Meta(TEXT("disconnect_pcg_nodes"),        TEXT("Disconnect two PCG node pins."), TEXT("graph_path, from_node_index, to_node_index, from_pin?, to_pin?"));
		Meta(TEXT("set_pcg_node_property"),       TEXT("Set a PCG node property (batch via items=[])."), TEXT("graph_path, node_index, property_name, *_value"));
		Meta(TEXT("get_pcg_node_properties"),     TEXT("Get a PCG node's properties."), TEXT("graph_path, node_index"));
		Meta(TEXT("set_pcg_node_comment"),        TEXT("Set a node's comment bubble."), TEXT("graph_path, node_index, comment, pin_bubble?"));
		Meta(TEXT("clear_pcg_graph"),             TEXT("Remove all non-Input/Output nodes+edges."), TEXT("graph_path"));
		Meta(TEXT("duplicate_pcg_graph"),         TEXT("Copy a PCG graph asset to a new path."), TEXT("graph_path, dest_name, dest_path?"));
		Meta(TEXT("create_pcg_subgraph_node"),    TEXT("Add a Subgraph node referencing another graph."), TEXT("graph_path, subgraph_path, pos_x?, pos_y?"));
		Meta(TEXT("set_pcg_mesh_spawner"),        TEXT("Configure a StaticMeshSpawner (batch via items=[])."), TEXT("graph_path, node_index, mesh_paths, weights"));
		Meta(TEXT("set_pcg_transform_randomizer"),TEXT("Configure a TransformPoints randomizer (yaw-only + absolute rotation by default)."), TEXT("graph_path, node_index, scale_min?, scale_max?, uniform_scale?, rot_min_z/rot_max_z? (yaw), rot_min_x/rot_max_x? (pitch, default 0), rot_min_y/rot_max_y? (roll, default 0), absolute_rotation?=true, offset_min_z?, offset_max_z?"));
		Meta(TEXT("set_pcg_slope_filter"),        TEXT("Configure a NormalToDensity+DensityFilter slope pair."), TEXT("graph_path, normal_density_node_index, density_filter_node_index, max_slope_angle_degrees?, b_invert?"));
		Meta(TEXT("set_pcg_noise_density"),       TEXT("Configure a noise-density node."), TEXT("graph_path, node_index, scale?, brightness?, contrast?, mode?, iterations?"));
		Meta(TEXT("set_pcg_spline_sampler"),      TEXT("Configure a SplineSampler."), TEXT("graph_path, node_index, sampling_mode?, distance_increment?, num_samples?, subdivisions_per_segment?, dimension?"));
		Meta(TEXT("set_pcg_texture_sampler"),     TEXT("Configure a TextureSampler."), TEXT("graph_path, node_index, texture_path, color_channel?, texel_size?, use_advanced_tiling?, tiling_x?, tiling_y?"));
		Meta(TEXT("set_pcg_data_from_actor"),     TEXT("Configure a DataFromActor node."), TEXT("graph_path, node_index, actor_filter, actor_selection, actor_selection_tag?, actor_selection_class?, mode?"));
		Meta(TEXT("set_pcg_attribute_filter"),    TEXT("Configure an AttributeFilter node."), TEXT("graph_path, node_index, target_attribute, operator, use_constant_threshold?, threshold_constant?, threshold_attribute?"));
		Meta(TEXT("set_pcg_self_pruning"),        TEXT("Configure a SelfPruning node."), TEXT("graph_path, node_index, pruning_type, radius_similarity_factor?, randomized_pruning?"));
		Meta(TEXT("set_pcg_create_attribute"),    TEXT("Configure a CreateAttribute/AddAttribute node."), TEXT("graph_path, node_index, attribute_name, type, <type>_value"));
		Meta(TEXT("set_pcg_attribute_math"),      TEXT("Configure a MetadataMaths node (unary/binary/ternary op)."), TEXT("graph_path, node_index, operation, input_a, input_b?, input_c?, output_attribute, force_round_to_int?"));
		Meta(TEXT("set_pcg_exclusion"),           TEXT("Wire a Difference node in front of StaticMeshSpawner node(s) so vegetation/props skip houses/props/roads. Idempotent — reuses prior exclusion nodes on repeat calls."), TEXT("graph_path, world_collision?=true, actor_tags?[], actor_classes?[], spline_tags?[], spline_width?=600, margin?=0.15, target_nodes?[] (default: every StaticMeshSpawner)"));
		Meta(TEXT("add_pcg_graph_parameter"),     TEXT("Add a user parameter to a PCG graph."), TEXT("graph_path, param_name, param_type?"));
		Meta(TEXT("get_pcg_graph_parameters"),    TEXT("List a graph's user parameters + values."), TEXT("graph_path"));
		Meta(TEXT("spawn_pcg_actor"),             TEXT("Spawn a PCG actor bound to a graph. Caller-given bounds_x/y/z and a non-zero location win; the landscape is only auto-detected when bounds are omitted. Warns on >=50% overlap with a related PCG actor."), TEXT("graph_path, actor_label, location_x/y/z?, bounds_x/y/z?"));
		Meta(TEXT("set_pcg_actor_bounds"),        TEXT("Set a PCG actor's bounds (half-extents cm). Warns on >=50% overlap with a related PCG actor."), TEXT("actor_label, bounds_x/y/z"));
		Meta(TEXT("get_pcg_actor_info"),          TEXT("Get a PCG actor's info."), TEXT("actor_label"));
		Meta(TEXT("update_pcg_actor_graph"),      TEXT("Rebind a PCG actor to a graph."), TEXT("actor_label, graph_path"));
		Meta(TEXT("set_pcg_component_properties"),TEXT("Set a PCG component's seed/trigger/input_type. Notes when generation_trigger is set to GenerateOnLoad."), TEXT("actor_label, seed?, activated?, is_partitioned?, generation_trigger?, input_type?"));
		Meta(TEXT("generate_pcg"),                TEXT("Trigger PCG generation on an actor. Refuses (guard) if the level is already >2x over the instance budget before generating, unless force_over_budget=true. Returns level_total_instances/budget/over_budget/overlapping_actors[]."), TEXT("actor_label, force?, force_over_budget?"));
		Meta(TEXT("get_pcg_level_summary"),       TEXT("Read-only overview of every PCG actor in the level: bounds, generation state, instance counts, pairwise overlaps, budget, and plain-English warnings. Call before adding another layer."), TEXT(""));
	}

	{
		const FName U(TEXT("foliage"));
		const auto Meta = [&D, &U](const TCHAR* Action, const TCHAR* Summary, const TCHAR* Params)
		{ D.RegisterToolMetadata({ FName(Action), U, FString(Summary), FString(Params) }); };

		Meta(TEXT("add_foliage_type"),                    TEXT("Create a foliage type from a static mesh."), TEXT("static_mesh_path, save_path?"));
		Meta(TEXT("paint_foliage"),                       TEXT("Paint foliage instances in a radius, honouring the FoliageType's Radius/slope/align/yaw/pitch/ZOffset/scale rules."), TEXT("foliage_type_path, location_x/y/z, radius, density, max_instances?=2000"));
		Meta(TEXT("set_foliage_density"),                 TEXT("Set a foliage type's density + scale range."), TEXT("foliage_type_path, density?, scale_min?, scale_max?"));
		Meta(TEXT("clear_foliage"),                       TEXT("Clear instances of a foliage type."), TEXT("foliage_type_path"));
		Meta(TEXT("set_foliage_type_properties"),         TEXT("Set cull/scale/collision on a foliage type."), TEXT("foliage_type_path, cull_distance_min?, cull_distance_max?, scale_min?, scale_max?, collision_profile?"));
		Meta(TEXT("assign_physical_material_to_foliage"), TEXT("Assign a physical material to a foliage type."), TEXT("foliage_type_path, physical_material_path"));
		Meta(TEXT("get_foliage_summary"),                 TEXT("Summarise level foliage."), TEXT(""));
		Meta(TEXT("create_procedural_foliage_spawner"),   TEXT("Create a procedural foliage spawner asset."), TEXT("name, save_path, foliage_type_paths"));
		Meta(TEXT("spawn_procedural_foliage_volume"),     TEXT("Spawn a procedural foliage volume."), TEXT("actor_label, spawner_path, location_x/y/z, extent_x/y/z?"));
	}

	{
		const FName U(TEXT("post_process"));
		const auto Meta = [&D, &U](const TCHAR* Action, const TCHAR* Summary, const TCHAR* Params)
		{ D.RegisterToolMetadata({ FName(Action), U, FString(Summary), FString(Params) }); };

		Meta(TEXT("create_post_process_volume"),  TEXT("Create a post-process volume (label is the key)."), TEXT("label, location_x/y/z?, unbound?"));
		Meta(TEXT("set_post_process_property"),   TEXT("Set PP property/properties (auto bOverride; batch)."), TEXT("actor_label, property_name, property_value | properties=[...]"));
		Meta(TEXT("get_post_process_summary"),    TEXT("Summarise a PP volume (overrides[] + blendables[])."), TEXT("actor_label"));
		Meta(TEXT("set_post_process_priority"),   TEXT("Set PP volume priority/blend radius/weight."), TEXT("actor_label, priority, blend_radius?, blend_weight?"));
		Meta(TEXT("add_post_process_blendable"),  TEXT("Add a LUT/post-material blendable."), TEXT("actor_label, material_path, weight?"));
		Meta(TEXT("remove_post_process_blendable"),TEXT("Remove a blendable."), TEXT("actor_label, material_path"));
		Meta(TEXT("add_mpc_parameter"),           TEXT("Add a parameter to a Material Parameter Collection."), TEXT("asset_path, param_name, param_type, default_value?"));
		Meta(TEXT("set_mpc_default_value"),       TEXT("Update an MPC parameter's default."), TEXT("asset_path, parameter_name, scalar_value | vector_value, is_scalar?"));
		Meta(TEXT("get_mpc_summary"),             TEXT("List an MPC's scalar+vector parameters."), TEXT("asset_path"));
	}

	{
		const FName U(TEXT("spline"));
		const auto Meta = [&D, &U](const TCHAR* Action, const TCHAR* Summary, const TCHAR* Params)
		{ D.RegisterToolMetadata({ FName(Action), U, FString(Summary), FString(Params) }); };

		Meta(TEXT("spawn_spline_actor"),          TEXT("Spawn a spline actor."), TEXT("actor_label, location_x/y/z?"));
		Meta(TEXT("add_spline_component"),        TEXT("Add a spline component to a Blueprint."), TEXT("blueprint_path, component_name?"));
		Meta(TEXT("set_spline_points"),           TEXT("Set all spline points."), TEXT("blueprint_path, points, component_name?"));
		Meta(TEXT("add_spline_point"),            TEXT("Add/insert a spline point."), TEXT("blueprint_path, component_name, point_x/y/z, insert_index?"));
		Meta(TEXT("remove_spline_point"),         TEXT("Remove a spline point."), TEXT("blueprint_path, component_name, point_index"));
		Meta(TEXT("set_spline_point_tangent"),    TEXT("Set a point's arrive/leave tangents."), TEXT("blueprint_path, point_index, arrive_tangent, leave_tangent, component_name?, tangent_type?"));
		Meta(TEXT("get_spline_info"),             TEXT("Inspect a spline component."), TEXT("blueprint_path|actor_label, component_name?"));
		Meta(TEXT("set_spline_properties"),       TEXT("Set closed-loop / spline type."), TEXT("blueprint_path, component_name, closed_loop?, spline_type?"));
		Meta(TEXT("set_spline_mesh"),             TEXT("Build one SplineMeshComponent per segment on a level spline actor (replaces previous)."), TEXT("actor_label (level actor with a SplineComponent) | blueprint_path, spline_component_name?, mesh_path, forward_axis? X|Y|Z, material_path?, collision?=true"));
		Meta(TEXT("set_spline_mesh_component_properties"), TEXT("Set spline-mesh scale/roll/offset (-999=unchanged)."), TEXT("blueprint_path, component_name, forward_axis?, start_scale_x?, end_scale_x?, ..."));
		Meta(TEXT("scatter_actors_along_spline"), TEXT("Scatter meshes along a spline, re-traced to the ground."), TEXT("actor_label, mesh_path, spacing?, random_offset?, rotation_mode? align_to_spline|yaw_only|random|none|fixed, rotation? {yaw,pitch,roll} or [pitch,yaw,roll] degrees (for fixed), align_to_ground?=true, allow_tilt?"));
	}

	{
		using UECPToolSafety::RegisterToolSafety;
		// Heuristics already classify these correctly (set_* -> Write, get_* -> Read); registered
		// explicitly for clarity since set_pcg_exclusion mutates graph assets and
		// get_pcg_level_summary is purely read-only.
		RegisterToolSafety(TEXT("set_pcg_exclusion"),     EUECPToolSafety::Write);
		RegisterToolSafety(TEXT("get_pcg_level_summary"), EUECPToolSafety::Read);
	}

	UE_LOG(LogUECPLevelDesignExt, Log, TEXT("Registered %d Level Design tools (landscape + foliage + spline + level_streaming + level_actor + pcg + environment + post_process umbrellas)"),
		OwnedToolNames().Num());
}

void FUECPLevelDesignExtModule::ShutdownModule()
{
	if (!IUECPCoreModule::IsAvailable()) return;
	IUECPToolDispatcher& D = IUECPCoreModule::Get().GetToolDispatcher();
	for (const FName& N : OwnedToolNames()) D.UnregisterHandler(N);
	IUECPCreateAssetRegistry& Reg = IUECPCoreModule::Get().GetCreateAssetRegistry();
	for (const TCHAR* T : { TEXT("HLODLayer"), TEXT("PCGGraph"), TEXT("RenderTarget"), TEXT("RuntimeVirtualTexture") })
	{
		Reg.UnregisterType(T);
	}
}

IMPLEMENT_MODULE(FUECPLevelDesignExtModule, UECPLevelDesignExt)
