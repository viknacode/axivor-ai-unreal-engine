// Copyright 2026, BlueprintsLab, All rights reserved

#include "UECPAssetGenModule.h"
#include "UECPAssetGenServiceImpl.h"
#include "UECPCoreModule.h"
#include "ExtensionSDK.h"
#include "Services/IUECPToolDispatcher.h"
#include "Meshy/MeshyTools.h"

DEFINE_LOG_CATEGORY(LogUECPAssetGen);

void FUECPAssetGenModule::StartupModule()
{
	UE_LOG(LogUECPAssetGen, Log, TEXT("FUECPAssetGenModule: StartupModule"));

	if (!IUECPCoreModule::IsAvailable())
	{
		UE_LOG(LogUECPAssetGen, Warning, TEXT("FUECPAssetGenModule: UECPCore unavailable — service + meshy tools not registered"));
		return;
	}

	IUECPCoreModule::Get().SetAssetGenService(MakeShared<FUECPAssetGenServiceImpl>());

	IUECPToolDispatcher& Dispatcher = IUECPCoreModule::Get().GetToolDispatcher();
	UECP_REGISTER_TOOL_LEGACY("create_3d_from_text",  MeshyTools::HandleCreate3DFromTextFromArgs);
	UECP_REGISTER_TOOL_LEGACY("create_3d_from_image", MeshyTools::HandleCreate3DFromImageFromArgs);
	UECP_REGISTER_TOOL_LEGACY("meshy_remesh",         MeshyTools::HandleMeshyRemeshFromArgs);
	UECP_REGISTER_TOOL_LEGACY("meshy_retexture",      MeshyTools::HandleMeshyRetextureFromArgs);
	UECP_REGISTER_TOOL_LEGACY("rig_model",            MeshyTools::HandleMeshyRigModelFromArgs);
	UECP_REGISTER_TOOL_LEGACY("animate_model",        MeshyTools::HandleMeshyAnimateModelFromArgs);
	UECP_REGISTER_TOOL_LEGACY("meshy_text_to_image",  MeshyTools::HandleMeshyTextToImageFromArgs);
	UECP_REGISTER_TOOL_LEGACY("list_meshy_animations", MeshyTools::HandleMeshyListAnimationsFromArgs);
	UECP_REGISTER_TOOL_LEGACY("get_meshy_balance",    MeshyTools::HandleMeshyGetBalanceFromArgs);
	UECP_REGISTER_TOOL_LEGACY("get_meshy_task_status", MeshyTools::HandleMeshyGetTaskStatusFromArgs);
	UECP_REGISTER_TOOL_LEGACY("cancel_meshy_task",    MeshyTools::HandleMeshyCancelTaskFromArgs);

	{
		const FName U(TEXT("meshy"));
		const auto Meta = [&Dispatcher, &U](const TCHAR* Action, const TCHAR* Summary, const TCHAR* Params)
		{ Dispatcher.RegisterToolMetadata({ FName(Action), U, FString(Summary), FString(Params) }); };

		Meta(TEXT("create_3d_from_text"),   TEXT("Meshy text→3D (async: returns queued+job_id, result arrives later)."), TEXT("prompt, negative_prompt?, asset_name?, save_path?, auto_refine?, pose_mode?"));
		Meta(TEXT("create_3d_from_image"),  TEXT("Meshy image→3D (async)."), TEXT("image_url|image_path, prompt?, asset_name?, save_path?, auto_refine?, pose_mode?"));
		Meta(TEXT("meshy_remesh"),          TEXT("Re-topologise a Meshy mesh (async)."), TEXT("input_task_id|model_url, topology?, poly_count?, resize_height?, asset_name?, save_path?"));
		Meta(TEXT("meshy_retexture"),       TEXT("Re-texture a mesh from a style prompt (async)."), TEXT("mesh_asset_path, style_prompt, material_name?, save_path?, enable_pbr?"));
		Meta(TEXT("rig_model"),             TEXT("Rig a Meshy humanoid mesh →SkeletalMesh+Skeleton (async, humanoid-only)."), TEXT("input_task_id|model_url, asset_name?, save_path?, height_meters?, import_basic_animations?"));
		Meta(TEXT("animate_model"),         TEXT("Animate a Meshy-rigged character (async)."), TEXT("rig_task_id, action_id, skeleton_asset_path, asset_name?, save_path?, change_fps?, extract_armature?"));
		Meta(TEXT("meshy_text_to_image"),   TEXT("Meshy text→image via proxied Imagen/OpenAI (async)."), TEXT("prompt, aspect_ratio?, negative_prompt?, asset_name?, save_path?, ai_model?"));
		Meta(TEXT("list_meshy_animations"), TEXT("Synchronous: the 678-entry Meshy animation catalog."), TEXT(""));
		Meta(TEXT("get_meshy_balance"),     TEXT("Remaining Meshy credit balance (async)."), TEXT(""));
		Meta(TEXT("get_meshy_task_status"), TEXT("Synchronous: a queued job's current registry state."), TEXT("job_id"));
		Meta(TEXT("cancel_meshy_task"),     TEXT("Remove a job's chat-resume hook (Meshy upstream may still complete)."), TEXT("job_id"));
	}
}

void FUECPAssetGenModule::ShutdownModule()
{
	UE_LOG(LogUECPAssetGen, Log, TEXT("FUECPAssetGenModule: ShutdownModule"));

	if (IUECPCoreModule::IsAvailable())
	{
		IUECPCoreModule::Get().SetAssetGenService(nullptr);
	}
}

IMPLEMENT_MODULE(FUECPAssetGenModule, UECPAssetGen)
