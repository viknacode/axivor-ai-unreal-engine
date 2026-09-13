// Copyright 2026, BlueprintsLab, All rights reserved

#include "Meshy/MeshyTools.h"
#include "Meshy/Providers/MeshyGenerationProvider.h"
#include "Meshy/Providers/MeshyRemeshProvider.h"
#include "Meshy/Providers/MeshyRetextureProvider.h"
#include "Meshy/Providers/MeshyRiggingProvider.h"
#include "Meshy/Providers/MeshyAnimationProvider.h"
#include "Meshy/Providers/MeshyTextToImageProvider.h"
#include "Meshy/Providers/MeshyBalanceProvider.h"
#include "Meshy/MeshyTaskTracker.h"
#include "UECPAssetGenModule.h"
#include "UECPCoreModule.h"
#include "ApiKeyManager.h"
#include "Services/IUECPArchitectService.h"
#include "Misc/Guid.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
	FString GetMeshyApiKey()
	{
		const FString Key = FApiKeyManager::Get().GetActiveMeshGenApiKey();
		return Key;
	}

	FString GetActiveChatID()
	{
		return IUECPCoreModule::IsAvailable()
			? IUECPCoreModule::Get().GetArchitectService().GetActiveChatID()
			: FString();
	}

	FString SerializeJson(const TSharedRef<FJsonObject>& Obj)
	{
		FString Out;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
		FJsonSerializer::Serialize(Obj, Writer);
		return Out;
	}

	void EmitQueuedResponse(const FString& JobId, const FString& ToolNameForMsg, const FString& Detail, FString& OutJsonString)
	{
		TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("status"),  TEXT("queued"));
		Obj->SetStringField(TEXT("job_id"),  JobId);
		Obj->SetStringField(TEXT("tool"),    ToolNameForMsg);
		Obj->SetStringField(TEXT("message"), Detail.IsEmpty()
			? TEXT("Meshy job submitted. The result will arrive in the chat once Meshy finishes (usually 1-5 minutes).")
			: Detail);
		OutJsonString = SerializeJson(Obj);
	}

	void EmitError(const FString& Message, FString& OutError)
	{
		OutError = Message;
	}

	FString BeginAsyncJob(const FString& ToolName, const FString& Label)
	{
		const FString JobId  = FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower();
		const FString ChatID = GetActiveChatID();

		FUECPAsyncTaskInfo Info;
		Info.TaskId       = JobId;
		Info.ChatId       = ChatID;
		Info.ToolName     = ToolName;
		Info.Label        = Label;
		Info.RegisteredAt = FDateTime::UtcNow();
		IUECPCoreModule::Get().GetArchitectService().RegisterAsyncTask(Info);
		return JobId;
	}

	void ResolveJob(const FString& JobId, bool bSuccess, const TSharedRef<FJsonObject>& Body)
	{
		const FString JsonString = SerializeJson(Body);
		IUECPCoreModule::Get().GetArchitectService().ResolveAsyncTask(JobId, JsonString, bSuccess);
	}

	TSharedRef<FJsonObject> MakeErrorBody(const FString& Error)
	{
		TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
		Body->SetBoolField  (TEXT("success"), false);
		Body->SetStringField(TEXT("error"),   Error);
		return Body;
	}

	struct FMeshyAnimEntry
	{
		int32 ActionId;
		const TCHAR* Name;
		const TCHAR* Category;
	};

	const FMeshyAnimEntry GAnimCatalog[] = {
#include "MeshyAnimationCatalog.inl"
	};
}

void MeshyTools::HandleCreate3DFromTextFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { EmitError(TEXT("Arguments missing"), OutError); return; }

	const FString ApiKey = GetMeshyApiKey();
	if (ApiKey.IsEmpty()) { EmitError(TEXT("Meshy API key not configured in the active slot"), OutError); return; }

	FString Prompt;
	if (!Args->TryGetStringField(TEXT("prompt"), Prompt) || Prompt.IsEmpty())
	{
		EmitError(TEXT("`prompt` is required"), OutError); return;
	}

	FMeshCreationRequest Req;
	Req.Prompt = Prompt;
	Args->TryGetStringField(TEXT("negative_prompt"), Req.NegativePrompt);
	Args->TryGetStringField(TEXT("asset_name"),       Req.CustomAssetName);
	if (Req.CustomAssetName.IsEmpty()) Req.CustomAssetName = TEXT("MeshyModel");
	Args->TryGetStringField(TEXT("save_path"),        Req.SavePath);
	int32 Seed = 0;
	if (Args->TryGetNumberField(TEXT("seed"), Seed)) Req.Seed = Seed;
	bool bAutoRefine = true;
	if (Args->TryGetBoolField(TEXT("auto_refine"), bAutoRefine)) {}
	Req.bAutoRefine = bAutoRefine;
	Args->TryGetStringField(TEXT("pose_mode"), Req.PoseMode);

	const FString JobId = BeginAsyncJob(TEXT("meshy.create_3d_from_text"),
		FString::Printf(TEXT("Text→3D: %s"), *Prompt.Left(40)));

	FMeshyGenerationProvider::Get().SubmitTextTo3D(Req, ApiKey,
		[JobId, AssetName = Req.CustomAssetName](const FMeshCreationResult& Result)
		{
			TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
			Body->SetBoolField  (TEXT("success"),     Result.bSuccess);
			Body->SetStringField(TEXT("asset_name"),  AssetName);
			if (Result.bSuccess)
			{
				Body->SetStringField(TEXT("asset_path"), Result.AssetPath);
				Body->SetStringField(TEXT("task_id"),    Result.TaskId);
			}
			else
			{
				Body->SetStringField(TEXT("error"), Result.ErrorMessage);
			}
			ResolveJob(JobId, Result.bSuccess, Body);
		});

	EmitQueuedResponse(JobId, TEXT("meshy.create_3d_from_text"), FString(), OutJsonString);
}

void MeshyTools::HandleCreate3DFromImageFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { EmitError(TEXT("Arguments missing"), OutError); return; }

	const FString ApiKey = GetMeshyApiKey();
	if (ApiKey.IsEmpty()) { EmitError(TEXT("Meshy API key not configured in the active slot"), OutError); return; }

	FMeshCreationRequest Req;
	Args->TryGetStringField(TEXT("image_url"),  Req.ImagePath);
	Args->TryGetStringField(TEXT("image_path"), Req.ImagePath);
	Args->TryGetStringField(TEXT("prompt"),     Req.Prompt);
	if (Req.ImagePath.IsEmpty())
	{
		EmitError(TEXT("`image_url` (or `image_path`) is required"), OutError); return;
	}

	Args->TryGetStringField(TEXT("asset_name"), Req.CustomAssetName);
	if (Req.CustomAssetName.IsEmpty()) Req.CustomAssetName = TEXT("MeshyModelFromImage");
	Args->TryGetStringField(TEXT("save_path"),  Req.SavePath);
	bool bAutoRefine = true;
	Args->TryGetBoolField(TEXT("auto_refine"),  bAutoRefine);
	Req.bAutoRefine = bAutoRefine;
	Args->TryGetStringField(TEXT("pose_mode"),  Req.PoseMode);

	const FString JobId = BeginAsyncJob(TEXT("meshy.create_3d_from_image"),
		FString::Printf(TEXT("Image→3D: %s"), *Req.CustomAssetName));

	FMeshyGenerationProvider::Get().SubmitImageTo3D(Req, ApiKey,
		[JobId, AssetName = Req.CustomAssetName](const FMeshCreationResult& Result)
		{
			TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
			Body->SetBoolField  (TEXT("success"),    Result.bSuccess);
			Body->SetStringField(TEXT("asset_name"), AssetName);
			if (Result.bSuccess)
			{
				Body->SetStringField(TEXT("asset_path"), Result.AssetPath);
				Body->SetStringField(TEXT("task_id"),    Result.TaskId);
			}
			else
			{
				Body->SetStringField(TEXT("error"), Result.ErrorMessage);
			}
			ResolveJob(JobId, Result.bSuccess, Body);
		});

	EmitQueuedResponse(JobId, TEXT("meshy.create_3d_from_image"), FString(), OutJsonString);
}

void MeshyTools::HandleMeshyRemeshFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { EmitError(TEXT("Arguments missing"), OutError); return; }

	const FString ApiKey = GetMeshyApiKey();
	if (ApiKey.IsEmpty()) { EmitError(TEXT("Meshy API key not configured in the active slot"), OutError); return; }

	FMeshyRemeshRequest Req;
	Args->TryGetStringField(TEXT("input_task_id"), Req.InputTaskId);
	Args->TryGetStringField(TEXT("model_url"),     Req.ModelUrl);
	if (Req.InputTaskId.IsEmpty() && Req.ModelUrl.IsEmpty())
	{
		EmitError(TEXT("Either `input_task_id` or `model_url` is required"), OutError); return;
	}

	Args->TryGetStringField(TEXT("asset_name"),  Req.AssetName);
	if (Req.AssetName.IsEmpty()) Req.AssetName = TEXT("RemeshedModel");
	Args->TryGetStringField(TEXT("save_path"),   Req.PackagePath);
	Args->TryGetStringField(TEXT("topology"),    Req.Topology);
	if (Req.Topology.IsEmpty()) Req.Topology = TEXT("quad");
	int32 PolyCount = 0;
	if (Args->TryGetNumberField(TEXT("poly_count"),   PolyCount)) Req.PolyCount = PolyCount;
	double ResizeHeight = 0.0;
	if (Args->TryGetNumberField(TEXT("resize_height"), ResizeHeight)) Req.ResizeHeight = static_cast<float>(ResizeHeight);

	const FString JobId = BeginAsyncJob(TEXT("meshy.remesh"),
		FString::Printf(TEXT("Remesh: %s"), *Req.AssetName));

	FMeshyRemeshProvider::Get().Submit(Req, ApiKey,
		[JobId, AssetName = Req.AssetName](const FMeshyRemeshResult& Result)
		{
			TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
			Body->SetBoolField  (TEXT("success"),    Result.bSuccess);
			Body->SetStringField(TEXT("asset_name"), AssetName);
			if (Result.bSuccess)
			{
				Body->SetStringField(TEXT("asset_path"), Result.AssetPath);
				Body->SetStringField(TEXT("task_id"),    Result.TaskId);
			}
			else
			{
				Body->SetStringField(TEXT("error"), Result.ErrorMessage);
			}
			ResolveJob(JobId, Result.bSuccess, Body);
		});

	EmitQueuedResponse(JobId, TEXT("meshy.remesh"), FString(), OutJsonString);
}

void MeshyTools::HandleMeshyRetextureFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { EmitError(TEXT("Arguments missing"), OutError); return; }

	const FString ApiKey = GetMeshyApiKey();
	if (ApiKey.IsEmpty()) { EmitError(TEXT("Meshy API key not configured in the active slot"), OutError); return; }

	FRetextureRequest Req;
	if (!Args->TryGetStringField(TEXT("mesh_asset_path"), Req.MeshAssetPath) || Req.MeshAssetPath.IsEmpty())
	{
		EmitError(TEXT("`mesh_asset_path` is required (e.g. /Game/Meshes/SM_Foo)"), OutError); return;
	}
	if (!Args->TryGetStringField(TEXT("style_prompt"), Req.StylePrompt) || Req.StylePrompt.IsEmpty())
	{
		EmitError(TEXT("`style_prompt` is required"), OutError); return;
	}
	Args->TryGetStringField(TEXT("save_path"),     Req.SavePath);
	Args->TryGetStringField(TEXT("material_name"), Req.MaterialName);
	Args->TryGetBoolField  (TEXT("enable_pbr"),    Req.bEnablePBR);

	const FString JobId = BeginAsyncJob(TEXT("meshy.retexture"),
		FString::Printf(TEXT("Retexture: %s"), *Req.MaterialName));

	FMeshyRetextureProvider::Get().SubmitRetexture(Req, ApiKey,
		[JobId, MeshAssetPath = Req.MeshAssetPath](const FRetextureResult& Result)
		{
			TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
			Body->SetBoolField  (TEXT("success"),         Result.bSuccess);
			Body->SetStringField(TEXT("mesh_asset_path"), MeshAssetPath);
			if (Result.bSuccess)
			{
				if (!Result.MaterialPath.IsEmpty())   Body->SetStringField(TEXT("material_path"),    Result.MaterialPath);
				if (!Result.BaseColorPath.IsEmpty())  Body->SetStringField(TEXT("base_color_path"),  Result.BaseColorPath);
				if (!Result.NormalPath.IsEmpty())     Body->SetStringField(TEXT("normal_path"),      Result.NormalPath);
				if (!Result.MetallicPath.IsEmpty())   Body->SetStringField(TEXT("metallic_path"),    Result.MetallicPath);
				if (!Result.RoughnessPath.IsEmpty())  Body->SetStringField(TEXT("roughness_path"),   Result.RoughnessPath);
				Body->SetStringField(TEXT("task_id"), Result.TaskId);
			}
			else
			{
				Body->SetStringField(TEXT("error"), Result.ErrorMessage);
			}
			ResolveJob(JobId, Result.bSuccess, Body);
		});

	EmitQueuedResponse(JobId, TEXT("meshy.retexture"), FString(), OutJsonString);
}

void MeshyTools::HandleMeshyRigModelFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { EmitError(TEXT("Arguments missing"), OutError); return; }

	const FString ApiKey = GetMeshyApiKey();
	if (ApiKey.IsEmpty()) { EmitError(TEXT("Meshy API key not configured in the active slot"), OutError); return; }

	FMeshyRiggingRequest Req;
	Args->TryGetStringField(TEXT("input_task_id"), Req.InputTaskId);
	Args->TryGetStringField(TEXT("model_url"),     Req.ModelUrl);
	if (Req.InputTaskId.IsEmpty() && Req.ModelUrl.IsEmpty())
	{
		EmitError(TEXT("Either `input_task_id` or `model_url` is required. Note: rigging is humanoid-only, ≤300k polys, +Z forward"), OutError); return;
	}
	Args->TryGetStringField(TEXT("asset_name"), Req.AssetName);
	if (Req.AssetName.IsEmpty()) Req.AssetName = TEXT("RiggedCharacter");
	Args->TryGetStringField(TEXT("save_path"),  Req.PackagePath);

	double HeightM = 1.7;
	if (Args->TryGetNumberField(TEXT("height_meters"), HeightM)) Req.HeightMeters = static_cast<float>(HeightM);
	Args->TryGetBoolField(TEXT("import_basic_animations"), Req.bImportBasicAnimations);

	const FString JobId = BeginAsyncJob(TEXT("meshy.rig_model"),
		FString::Printf(TEXT("Rig: %s"), *Req.AssetName));

	FMeshyRiggingProvider::Get().Submit(Req, ApiKey,
		[JobId, AssetName = Req.AssetName](const FMeshyRiggingResult& Result)
		{
			TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
			Body->SetBoolField  (TEXT("success"),    Result.bSuccess);
			Body->SetStringField(TEXT("asset_name"), AssetName);
			if (Result.bSuccess)
			{
				Body->SetStringField(TEXT("skeletal_mesh_path"), Result.SkeletalMeshPath);
				Body->SetStringField(TEXT("skeleton_path"),      Result.SkeletonPath);
				if (!Result.PhysicsAssetPath.IsEmpty()) Body->SetStringField(TEXT("physics_asset_path"), Result.PhysicsAssetPath);
				Body->SetStringField(TEXT("task_id"),            Result.TaskId);

				TArray<TSharedPtr<FJsonValue>> Anims;
				for (const FString& Path : Result.BasicAnimationPaths)
				{
					Anims.Add(MakeShared<FJsonValueString>(Path));
				}
				Body->SetArrayField(TEXT("basic_animation_paths"), Anims);
			}
			else
			{
				Body->SetStringField(TEXT("error"), Result.ErrorMessage);
			}
			ResolveJob(JobId, Result.bSuccess, Body);
		});

	EmitQueuedResponse(JobId, TEXT("meshy.rig_model"), FString(), OutJsonString);
}

void MeshyTools::HandleMeshyAnimateModelFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { EmitError(TEXT("Arguments missing"), OutError); return; }

	const FString ApiKey = GetMeshyApiKey();
	if (ApiKey.IsEmpty()) { EmitError(TEXT("Meshy API key not configured in the active slot"), OutError); return; }

	FMeshyAnimationRequest Req;
	if (!Args->TryGetStringField(TEXT("rig_task_id"), Req.RigTaskId) || Req.RigTaskId.IsEmpty())
	{
		EmitError(TEXT("`rig_task_id` is required (output of a prior meshy.rig_model call)"), OutError); return;
	}
	int32 ActionId = 0;
	if (!Args->TryGetNumberField(TEXT("action_id"), ActionId) || ActionId <= 0)
	{
		EmitError(TEXT("`action_id` is required (call meshy.list_animations to see the catalog)"), OutError); return;
	}
	Req.ActionId = ActionId;
	if (!Args->TryGetStringField(TEXT("skeleton_asset_path"), Req.SkeletonAssetPath) || Req.SkeletonAssetPath.IsEmpty())
	{
		EmitError(TEXT("`skeleton_asset_path` is required so the animation can target the skeleton from the rigging step"), OutError); return;
	}
	Args->TryGetStringField(TEXT("asset_name"), Req.AssetName);
	if (Req.AssetName.IsEmpty()) Req.AssetName = FString::Printf(TEXT("MeshyAnim_%d"), ActionId);
	Args->TryGetStringField(TEXT("save_path"),  Req.PackagePath);

	int32 Fps = 0;
	if (Args->TryGetNumberField(TEXT("change_fps"), Fps)) Req.ChangeFps = Fps;
	Args->TryGetBoolField(TEXT("extract_armature"), Req.bExtractArmature);

	const FString JobId = BeginAsyncJob(TEXT("meshy.animate_model"),
		FString::Printf(TEXT("Anim %d: %s"), ActionId, *Req.AssetName));

	FMeshyAnimationProvider::Get().Submit(Req, ApiKey,
		[JobId, AssetName = Req.AssetName, ActionId](const FMeshyAnimationResult& Result)
		{
			TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
			Body->SetBoolField  (TEXT("success"),    Result.bSuccess);
			Body->SetNumberField(TEXT("action_id"),  ActionId);
			Body->SetStringField(TEXT("asset_name"), AssetName);
			if (Result.bSuccess)
			{
				Body->SetStringField(TEXT("anim_sequence_path"), Result.AnimSequencePath);
				Body->SetStringField(TEXT("task_id"),            Result.TaskId);
			}
			else
			{
				Body->SetStringField(TEXT("error"), Result.ErrorMessage);
			}
			ResolveJob(JobId, Result.bSuccess, Body);
		});

	EmitQueuedResponse(JobId, TEXT("meshy.animate_model"), FString(), OutJsonString);
}

void MeshyTools::HandleMeshyTextToImageFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { EmitError(TEXT("Arguments missing"), OutError); return; }

	const FString ApiKey = GetMeshyApiKey();
	if (ApiKey.IsEmpty()) { EmitError(TEXT("Meshy API key not configured in the active slot"), OutError); return; }

	FMeshyTextToImageRequest Req;
	if (!Args->TryGetStringField(TEXT("prompt"), Req.Prompt) || Req.Prompt.IsEmpty())
	{
		EmitError(TEXT("`prompt` is required"), OutError); return;
	}
	Args->TryGetStringField(TEXT("asset_name"),      Req.AssetName);
	if (Req.AssetName.IsEmpty()) Req.AssetName = TEXT("GeneratedImage");
	Args->TryGetStringField(TEXT("save_path"),       Req.PackagePath);
	Args->TryGetStringField(TEXT("aspect_ratio"),    Req.AspectRatio);
	Args->TryGetStringField(TEXT("negative_prompt"), Req.NegativePrompt);
	Args->TryGetStringField(TEXT("ai_model"),        Req.AiModel);

	const FString JobId = BeginAsyncJob(TEXT("meshy.text_to_image"),
		FString::Printf(TEXT("Image: %s"), *Req.Prompt.Left(40)));

	FMeshyTextToImageProvider::Get().Submit(Req, ApiKey,
		[JobId, AssetName = Req.AssetName](const FMeshyTextToImageResult& Result)
		{
			TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
			Body->SetBoolField  (TEXT("success"),    Result.bSuccess);
			Body->SetStringField(TEXT("asset_name"), AssetName);
			if (Result.bSuccess)
			{
				Body->SetStringField(TEXT("texture_asset_path"), Result.TextureAssetPath);
				Body->SetStringField(TEXT("task_id"),            Result.TaskId);
			}
			else
			{
				Body->SetStringField(TEXT("error"), Result.ErrorMessage);
			}
			ResolveJob(JobId, Result.bSuccess, Body);
		});

	EmitQueuedResponse(JobId, TEXT("meshy.text_to_image"), FString(), OutJsonString);
}

void MeshyTools::HandleMeshyListAnimationsFromArgs(const TSharedPtr<FJsonObject>& , FString& OutJsonString, FString& )
{
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField  (TEXT("success"), true);
	Root->SetNumberField(TEXT("count"),   UE_ARRAY_COUNT(GAnimCatalog));
	Root->SetStringField(TEXT("note"),    TEXT("Static catalog of well-known Meshy action_id values. Subject to Meshy library churn — call meshy.animate_model with the chosen action_id."));

	TArray<TSharedPtr<FJsonValue>> Entries;
	for (const FMeshyAnimEntry& E : GAnimCatalog)
	{
		TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetNumberField(TEXT("action_id"), E.ActionId);
		Obj->SetStringField(TEXT("name"),      E.Name);
		Obj->SetStringField(TEXT("category"),  E.Category);
		Entries.Add(MakeShared<FJsonValueObject>(Obj));
	}
	Root->SetArrayField(TEXT("animations"), Entries);

	OutJsonString = SerializeJson(Root);
}

void MeshyTools::HandleMeshyGetBalanceFromArgs(const TSharedPtr<FJsonObject>& , FString& OutJsonString, FString& OutError)
{
	const FString ApiKey = GetMeshyApiKey();
	if (ApiKey.IsEmpty()) { EmitError(TEXT("Meshy API key not configured in the active slot"), OutError); return; }

	const FString JobId = BeginAsyncJob(TEXT("meshy.get_balance"), TEXT("Balance check"));

	FMeshyBalanceProvider::Query(ApiKey,
		[JobId](const FMeshyBalanceResult& Result)
		{
			TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
			Body->SetBoolField(TEXT("success"), Result.bSuccess);
			if (Result.bSuccess)
			{
				Body->SetNumberField(TEXT("credits"), Result.Credits);
			}
			else
			{
				Body->SetStringField(TEXT("error"), Result.ErrorMessage);
			}
			ResolveJob(JobId, Result.bSuccess, Body);
		});

	EmitQueuedResponse(JobId, TEXT("meshy.get_balance"), TEXT("Balance request submitted; arrives in a few seconds."), OutJsonString);
}

void MeshyTools::HandleMeshyGetTaskStatusFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { EmitError(TEXT("Arguments missing"), OutError); return; }

	FString JobId;
	if (!Args->TryGetStringField(TEXT("job_id"), JobId) || JobId.IsEmpty())
	{
		EmitError(TEXT("`job_id` is required (returned in the queued response of a prior meshy.* call)"), OutError); return;
	}

	const FString ChatID = GetActiveChatID();
	const TArray<FUECPAsyncTaskInfo> Pending = IUECPCoreModule::Get().GetArchitectService().GetPendingAsyncTasksForChat(ChatID);
	const FUECPAsyncTaskInfo* Found = Pending.FindByPredicate([&](const FUECPAsyncTaskInfo& I){ return I.TaskId == JobId; });

	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("job_id"), JobId);
	if (Found)
	{
		Body->SetStringField(TEXT("status"),     TEXT("pending"));
		Body->SetStringField(TEXT("tool"),       Found->ToolName);
		Body->SetStringField(TEXT("label"),      Found->Label);
		Body->SetStringField(TEXT("registered_at"), Found->RegisteredAt.ToIso8601());
	}
	else
	{
		Body->SetStringField(TEXT("status"), TEXT("unknown_or_completed"));
		Body->SetStringField(TEXT("note"),
			TEXT("Job is not in the pending registry — either it completed (look for an earlier [TOOL_RESULT:meshy.*:...] message), was never started, or was registered against a different chat."));
	}
	OutJsonString = SerializeJson(Body);
}

void MeshyTools::HandleMeshyCancelTaskFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { EmitError(TEXT("Arguments missing"), OutError); return; }

	FString JobId;
	if (!Args->TryGetStringField(TEXT("job_id"), JobId) || JobId.IsEmpty())
	{
		EmitError(TEXT("`job_id` is required"), OutError); return;
	}

	const bool bCancelled = IUECPCoreModule::Get().GetArchitectService().CancelAsyncTask(JobId);

	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetBoolField  (TEXT("success"),   bCancelled);
	Body->SetStringField(TEXT("job_id"),    JobId);
	Body->SetStringField(TEXT("cancelled"), bCancelled ? TEXT("registry_only") : TEXT("not_found"));
	if (bCancelled)
	{
		Body->SetStringField(TEXT("note"),
			TEXT("Cancellation removes the chat-resume hook locally. The Meshy upstream job may still complete server-side and consume credits (Meshy has no documented cancel-task endpoint)."));
	}
	OutJsonString = SerializeJson(Body);
}
