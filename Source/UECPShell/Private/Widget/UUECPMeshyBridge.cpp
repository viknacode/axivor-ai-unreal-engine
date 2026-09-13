// Copyright 2026, BlueprintsLab, All rights reserved

#include "Widget/UUECPMeshyBridge.h"
#include "UECPCoreModule.h"
#include "Services/IUECPBugReportService.h"
#include "Services/IUECPGddService.h"
#include "Services/IUECPAiMemoryService.h"
#include "Services/IUECPVoiceService.h"
#include "Services/IUECPAnalystService.h"
#include "Services/IUECPScannerService.h"
#include "Services/IUECPArchitectService.h"
#include "Services/IUECPAgentRunnerService.h"
#include "Services/IUECPArchitectService.h"
#include "Services/IUECPCrewService.h"
#include "Services/IUECPACPRegistryService.h"
#include "Services/IUECPExtensionService.h"
#include "Services/IUECPToolDispatcher.h"
#include "Types/CrewTypes.h"
#include "ApiKeyManager.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Managers/HttpCommunicationManager.h"
#include "Managers/FreeTierConfigManager.h"
#include "Managers/ChatHistoryManager.h"
#include "AssetReferenceManager.h"
#include "SUECPMainWidget.h"
#include "Managers/PlanManager.h"
#include "Managers/TaskManager.h"
#include "SWebBrowser.h"
#include "Misc/FileHelper.h"
#include "Misc/Base64.h"
#include "Interfaces/IPluginManager.h"
#include "Utils/MountResolver.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "AssetReferenceTypes.h"
#include "Managers/ChatHistoryManager.h"
#include "Managers/SettingsManager.h"
#include "Serialization/JsonSerializer.h"
#include "SBlueprintDiff.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonWriter.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Tools/AssetPropertyTools.h"
#include "Tools/ProfilerTools.h"
#include "MeshAssetManager.h"
#include "Async/Async.h"
#include "DesktopPlatformModule.h"
#include "IDesktopPlatform.h"
#include "Engine/SkeletalMesh.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "GenericPlatform/GenericPlatformHttp.h"
#include "Utils/DiagramUtils.h"
#include "Widgets/SWindow.h"
#include "Framework/Application/SlateApplication.h"
#include "Containers/Ticker.h"

#define LOCTEXT_NAMESPACE "UUECPMeshyBridge"

#include "Meshy/Providers/MeshyGenerationProvider.h"
#include "Meshy/Providers/MeshyRemeshProvider.h"
#include "Meshy/Providers/MeshyRetextureProvider.h"
#include "Meshy/Providers/MeshyRiggingProvider.h"
#include "Meshy/Providers/MeshyAnimationProvider.h"
#include "Meshy/Providers/MeshyTextToImageProvider.h"
#include "Meshy/Providers/MeshyBalanceProvider.h"
#include "Meshy/MeshyTaskTracker.h"

namespace
{
	TSharedPtr<FJsonObject> ParseMeshyJson(const FString& Json)
	{
		TSharedPtr<FJsonObject> Obj;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
		FJsonSerializer::Deserialize(Reader, Obj);
		return Obj;
	}

	FString SerializeMeshyJson(const TSharedRef<FJsonObject>& Obj)
	{
		FString Out;
		const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
		FJsonSerializer::Serialize(Obj, Writer);
		return Out;
	}

	FString EscJsLocal(const FString& In)
	{
		FString Out = In;
		Out.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
		Out.ReplaceInline(TEXT("'"),  TEXT("\\'"));
		Out.ReplaceInline(TEXT("\n"), TEXT("\\n"));
		Out.ReplaceInline(TEXT("\r"), TEXT("\\r"));
		return Out;
	}

	FString MeshyKindToString(EMeshyTaskKind Kind)
	{
		switch (Kind)
		{
		case EMeshyTaskKind::TextTo3DPreview: return TEXT("text_to_3d");
		case EMeshyTaskKind::TextTo3DRefine:  return TEXT("text_to_3d_refine");
		case EMeshyTaskKind::ImageTo3D:        return TEXT("image_to_3d");
		case EMeshyTaskKind::MultiImageTo3D:   return TEXT("multi_image_to_3d");
		case EMeshyTaskKind::Remesh:           return TEXT("remesh");
		case EMeshyTaskKind::Retexture:        return TEXT("retexture");
		case EMeshyTaskKind::Rigging:          return TEXT("rigging");
		case EMeshyTaskKind::Animation:        return TEXT("animation");
		case EMeshyTaskKind::TextToImage:      return TEXT("text_to_image");
		}
		return TEXT("unknown");
	}
}

void UUECPMeshyBridge::BeginDestroy()
{
	if (bMeshyTrackerWired)
	{
		if (MeshyTaskAddedHandle.IsValid())    FMeshyTaskTracker::Get().OnTaskAdded.Remove(MeshyTaskAddedHandle);
		if (MeshyTaskProgressHandle.IsValid()) FMeshyTaskTracker::Get().OnTaskProgress.Remove(MeshyTaskProgressHandle);
		if (MeshyTaskFinishedHandle.IsValid()) FMeshyTaskTracker::Get().OnTaskFinished.Remove(MeshyTaskFinishedHandle);
		bMeshyTrackerWired = false;
	}
	Super::BeginDestroy();
}

void UUECPMeshyBridge::EnsureMeshyTrackerWiring()
{
	if (bMeshyTrackerWired) return;
	bMeshyTrackerWired = true;

	TWeakObjectPtr<UUECPMeshyBridge> WeakSelf(this);

	MeshyTaskAddedHandle = FMeshyTaskTracker::Get().OnTaskAdded.AddLambda(
		[WeakSelf](const FMeshyTaskInfo& Info, FString )
		{
			UUECPMeshyBridge* Self = WeakSelf.Get();
			if (!Self) return;
			TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
			Obj->SetStringField(TEXT("id"),           Info.TaskId);
			Obj->SetStringField(TEXT("tool"),         MeshyKindToString(Info.Kind));
			Obj->SetStringField(TEXT("label"),        Info.Label);
			Obj->SetStringField(TEXT("registered_at"), Info.CreatedAt.ToIso8601());
			Self->PushMeshyJobAdded(SerializeMeshyJson(Obj));
		});

	MeshyTaskProgressHandle = FMeshyTaskTracker::Get().OnTaskProgress.AddLambda(
		[WeakSelf](const FMeshyTaskInfo& Info, int32 Progress)
		{
			UUECPMeshyBridge* Self = WeakSelf.Get();
			if (!Self) return;
			Self->PushMeshyJobProgress(Info.TaskId, Progress, Info.Status);
		});

	MeshyTaskFinishedHandle = FMeshyTaskTracker::Get().OnTaskFinished.AddLambda(
		[WeakSelf](const FMeshyTaskResult& Result)
		{
			UUECPMeshyBridge* Self = WeakSelf.Get();
			if (!Self) return;
			const bool bSucceeded = Result.bSuccess;
			TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
			Body->SetBoolField(TEXT("success"), bSucceeded);
			if (!bSucceeded && !Result.ErrorMessage.IsEmpty())
			{
				Body->SetStringField(TEXT("error"), Result.ErrorMessage);
			}
			Self->PushMeshyJobFinished(Result.TaskId, bSucceeded ? TEXT("importing") : TEXT("failed"),
				SerializeMeshyJson(Body));
		});
}

void UUECPMeshyBridge::PushMeshyJobAdded(const FString& InfoJson)
{
	ExecJs(FString::Printf(TEXT("if(typeof onMeshyJobAdded==='function')onMeshyJobAdded('%s')"),
		*EscJsLocal(InfoJson)));
}

void UUECPMeshyBridge::PushMeshyJobProgress(const FString& TaskId, int32 Progress, const FString& Status)
{
	ExecJs(FString::Printf(TEXT("if(typeof onMeshyJobProgress==='function')onMeshyJobProgress('%s',%d,'%s')"),
		*EscJsLocal(TaskId), Progress, *EscJsLocal(Status)));
}

void UUECPMeshyBridge::PushMeshyJobFinished(const FString& TaskId, const FString& Status, const FString& BodyJson)
{
	if (TaskId.IsEmpty() && Status.Equals(TEXT("failed"), ESearchCase::IgnoreCase))
	{
		FString Err = TEXT("Meshy submit failed (no details)");
		TSharedPtr<FJsonObject> Body;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(BodyJson);
		if (FJsonSerializer::Deserialize(Reader, Body) && Body.IsValid())
		{
			FString ParsedErr;
			if (Body->TryGetStringField(TEXT("error"), ParsedErr) && !ParsedErr.IsEmpty()) Err = ParsedErr;
		}
		PushToast(Err, TEXT("error"));
		UE_LOG(LogTemp, Warning, TEXT("Meshy submit failed (no task id): %s"), *Err);
	}

	ExecJs(FString::Printf(TEXT("if(typeof onMeshyJobFinished==='function')onMeshyJobFinished('%s','%s','%s')"),
		*EscJsLocal(TaskId), *EscJsLocal(Status), *EscJsLocal(BodyJson)));
}

void UUECPMeshyBridge::PushMeshyBalance(int32 Credits)
{
	ExecJs(FString::Printf(TEXT("if(typeof onMeshyBalance==='function')onMeshyBalance(%d)"), Credits));
}

void UUECPMeshyBridge::PushMeshyAnimCatalog(const FString& CatalogJson)
{
	ExecJs(FString::Printf(TEXT("if(typeof onMeshyAnimCatalog==='function')onMeshyAnimCatalog('%s')"),
		*EscJsLocal(CatalogJson)));
}

void UUECPMeshyBridge::MeshyCreateText(const FString& Json)
{
	EnsureMeshyTrackerWiring();
	const FString ApiKey = FApiKeyManager::Get().GetActiveMeshGenApiKey();
	if (ApiKey.IsEmpty()) { PushToast(TEXT("Meshy API key not configured"), TEXT("error")); return; }

	TSharedPtr<FJsonObject> Args = ParseMeshyJson(Json);
	if (!Args.IsValid()) { PushToast(TEXT("Invalid Meshy request"), TEXT("error")); return; }

	FMeshCreationRequest Req;
	Args->TryGetStringField(TEXT("prompt"),          Req.Prompt);
	Args->TryGetStringField(TEXT("negative_prompt"), Req.NegativePrompt);
	Args->TryGetStringField(TEXT("asset_name"),      Req.CustomAssetName);
	Args->TryGetStringField(TEXT("save_path"),       Req.SavePath);
	Args->TryGetBoolField  (TEXT("auto_refine"),     Req.bAutoRefine);
	Args->TryGetStringField(TEXT("pose_mode"),       Req.PoseMode);
	if (Req.Prompt.IsEmpty()) { PushToast(TEXT("Prompt is required"), TEXT("error")); return; }
	if (Req.CustomAssetName.IsEmpty()) Req.CustomAssetName = TEXT("GeneratedMesh");

	TWeakObjectPtr<UUECPMeshyBridge> WeakSelf(this);
	FMeshyGenerationProvider::Get().SubmitTextTo3D(Req, ApiKey,
		[WeakSelf](const FMeshCreationResult& Result)
		{
			AsyncTask(ENamedThreads::GameThread, [WeakSelf, Result]
			{
				UUECPMeshyBridge* Self = WeakSelf.Get();
				if (!Self) return;
				TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
				Body->SetBoolField(TEXT("success"), Result.bSuccess);
				if (!Result.TaskId.IsEmpty()) Body->SetStringField(TEXT("task_id"), Result.TaskId);
				if (Result.bSuccess && !Result.AssetPath.IsEmpty())  Body->SetStringField(TEXT("asset_path"), Result.AssetPath);
				if (!Result.bSuccess && !Result.ErrorMessage.IsEmpty()) Body->SetStringField(TEXT("error"),  Result.ErrorMessage);
				Self->PushMeshyJobFinished(Result.TaskId, Result.bSuccess ? TEXT("done") : TEXT("failed"),
					SerializeMeshyJson(Body));
			});
		});
}

void UUECPMeshyBridge::MeshyCreateImage(const FString& Json)
{
	EnsureMeshyTrackerWiring();
	const FString ApiKey = FApiKeyManager::Get().GetActiveMeshGenApiKey();
	if (ApiKey.IsEmpty()) { PushToast(TEXT("Meshy API key not configured"), TEXT("error")); return; }

	TSharedPtr<FJsonObject> Args = ParseMeshyJson(Json);
	if (!Args.IsValid()) { PushToast(TEXT("Invalid Meshy request"), TEXT("error")); return; }

	auto W = OwnerWidget.Pin();
	FMeshCreationRequest Req;
	Args->TryGetStringField(TEXT("image_url"),  Req.ImagePath);
	Args->TryGetStringField(TEXT("prompt"),     Req.Prompt);
	Args->TryGetStringField(TEXT("asset_name"), Req.CustomAssetName);
	Args->TryGetStringField(TEXT("save_path"),  Req.SavePath);
	Args->TryGetBoolField  (TEXT("auto_refine"), Req.bAutoRefine);
	Args->TryGetStringField(TEXT("pose_mode"),  Req.PoseMode);
	if (Req.ImagePath.IsEmpty() && W.IsValid() && W->SourceImageAttachment.Base64Data.Len() > 0)
	{
		Req.ImageBase64Data = W->SourceImageAttachment.Base64Data;
	}
	if (Req.ImagePath.IsEmpty() && Req.ImageBase64Data.IsEmpty())
	{
		PushToast(TEXT("Pick an image first (Browse Image) or provide image_url"), TEXT("error")); return;
	}
	if (Req.CustomAssetName.IsEmpty()) Req.CustomAssetName = TEXT("MeshyModelFromImage");

	TWeakObjectPtr<UUECPMeshyBridge> WeakSelf(this);
	FMeshyGenerationProvider::Get().SubmitImageTo3D(Req, ApiKey,
		[WeakSelf](const FMeshCreationResult& Result)
		{
			AsyncTask(ENamedThreads::GameThread, [WeakSelf, Result]
			{
				UUECPMeshyBridge* Self = WeakSelf.Get();
				if (!Self) return;
				TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
				Body->SetBoolField(TEXT("success"), Result.bSuccess);
				if (!Result.TaskId.IsEmpty()) Body->SetStringField(TEXT("task_id"), Result.TaskId);
				if (Result.bSuccess && !Result.AssetPath.IsEmpty())  Body->SetStringField(TEXT("asset_path"), Result.AssetPath);
				if (!Result.bSuccess && !Result.ErrorMessage.IsEmpty()) Body->SetStringField(TEXT("error"),  Result.ErrorMessage);
				Self->PushMeshyJobFinished(Result.TaskId, Result.bSuccess ? TEXT("done") : TEXT("failed"),
					SerializeMeshyJson(Body));
			});
		});
}

void UUECPMeshyBridge::MeshyRemesh(const FString& Json)
{
	EnsureMeshyTrackerWiring();
	const FString ApiKey = FApiKeyManager::Get().GetActiveMeshGenApiKey();
	if (ApiKey.IsEmpty()) { PushToast(TEXT("Meshy API key not configured"), TEXT("error")); return; }

	TSharedPtr<FJsonObject> Args = ParseMeshyJson(Json);
	if (!Args.IsValid()) return;

	FMeshyRemeshRequest Req;
	Args->TryGetStringField(TEXT("input_task_id"),   Req.InputTaskId);
	Args->TryGetStringField(TEXT("model_url"),       Req.ModelUrl);
	Args->TryGetStringField(TEXT("mesh_asset_path"), Req.MeshAssetPath);
	Args->TryGetStringField(TEXT("topology"),        Req.Topology);
	int32 PolyCount = 0;
	if (Args->TryGetNumberField(TEXT("poly_count"), PolyCount)) Req.PolyCount = PolyCount;
	Args->TryGetStringField(TEXT("asset_name"), Req.AssetName);
	Args->TryGetStringField(TEXT("save_path"),  Req.PackagePath);

	if (Req.InputTaskId.IsEmpty() && Req.ModelUrl.IsEmpty() && Req.MeshAssetPath.IsEmpty())
	{
		PushToast(TEXT("Remesh needs a mesh_asset_path, input_task_id, or model_url"), TEXT("error")); return;
	}

	TWeakObjectPtr<UUECPMeshyBridge> WeakSelf(this);
	FMeshyRemeshProvider::Get().Submit(Req, ApiKey,
		[WeakSelf](const FMeshyRemeshResult& Result)
		{
			AsyncTask(ENamedThreads::GameThread, [WeakSelf, Result]
			{
				UUECPMeshyBridge* Self = WeakSelf.Get();
				if (!Self) return;
				TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
				Body->SetBoolField(TEXT("success"), Result.bSuccess);
				if (!Result.TaskId.IsEmpty()) Body->SetStringField(TEXT("task_id"), Result.TaskId);
				if (Result.bSuccess) Body->SetStringField(TEXT("asset_path"), Result.AssetPath);
				else if (!Result.ErrorMessage.IsEmpty()) Body->SetStringField(TEXT("error"), Result.ErrorMessage);
				Self->PushMeshyJobFinished(Result.TaskId, Result.bSuccess ? TEXT("done") : TEXT("failed"),
					SerializeMeshyJson(Body));
			});
		});
}

void UUECPMeshyBridge::MeshyRetexture(const FString& Json)
{
	EnsureMeshyTrackerWiring();
	const FString ApiKey = FApiKeyManager::Get().GetActiveMeshGenApiKey();
	if (ApiKey.IsEmpty()) { PushToast(TEXT("Meshy API key not configured"), TEXT("error")); return; }

	TSharedPtr<FJsonObject> Args = ParseMeshyJson(Json);
	if (!Args.IsValid()) return;

	FRetextureRequest Req;
	Args->TryGetStringField(TEXT("mesh_asset_path"), Req.MeshAssetPath);
	Args->TryGetStringField(TEXT("style_prompt"),    Req.StylePrompt);
	Args->TryGetStringField(TEXT("material_name"),   Req.MaterialName);
	Args->TryGetStringField(TEXT("save_path"),       Req.SavePath);
	Args->TryGetBoolField  (TEXT("enable_pbr"),      Req.bEnablePBR);
	if (Req.MeshAssetPath.IsEmpty() || Req.StylePrompt.IsEmpty())
	{
		PushToast(TEXT("Retexture needs mesh_asset_path and style_prompt"), TEXT("error")); return;
	}

	TWeakObjectPtr<UUECPMeshyBridge> WeakSelf(this);
	FMeshyRetextureProvider::Get().SubmitRetexture(Req, ApiKey,
		[WeakSelf](const FRetextureResult& Result)
		{
			AsyncTask(ENamedThreads::GameThread, [WeakSelf, Result]
			{
				UUECPMeshyBridge* Self = WeakSelf.Get();
				if (!Self) return;
				TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
				Body->SetBoolField(TEXT("success"), Result.bSuccess);
				if (!Result.TaskId.IsEmpty()) Body->SetStringField(TEXT("task_id"), Result.TaskId);
				if (Result.bSuccess)
				{
					if (!Result.MaterialPath.IsEmpty())  Body->SetStringField(TEXT("material_path"),  Result.MaterialPath);
					if (!Result.BaseColorPath.IsEmpty()) Body->SetStringField(TEXT("base_color_path"),Result.BaseColorPath);
					if (!Result.NormalPath.IsEmpty())    Body->SetStringField(TEXT("normal_path"),    Result.NormalPath);
					Body->SetStringField(TEXT("asset_path"), Result.MaterialPath.IsEmpty() ? Result.BaseColorPath : Result.MaterialPath);
				}
				else if (!Result.ErrorMessage.IsEmpty()) Body->SetStringField(TEXT("error"), Result.ErrorMessage);
				Self->PushMeshyJobFinished(Result.TaskId, Result.bSuccess ? TEXT("done") : TEXT("failed"),
					SerializeMeshyJson(Body));
			});
		});
}

void UUECPMeshyBridge::MeshyRig(const FString& Json)
{
	EnsureMeshyTrackerWiring();
	const FString ApiKey = FApiKeyManager::Get().GetActiveMeshGenApiKey();
	if (ApiKey.IsEmpty()) { PushToast(TEXT("Meshy API key not configured"), TEXT("error")); return; }

	TSharedPtr<FJsonObject> Args = ParseMeshyJson(Json);
	if (!Args.IsValid()) return;

	FMeshyRiggingRequest Req;
	Args->TryGetStringField(TEXT("input_task_id"),   Req.InputTaskId);
	Args->TryGetStringField(TEXT("model_url"),       Req.ModelUrl);
	Args->TryGetStringField(TEXT("mesh_asset_path"), Req.MeshAssetPath);
	double Height = 1.7;
	if (Args->TryGetNumberField(TEXT("height_meters"), Height)) Req.HeightMeters = static_cast<float>(Height);
	Args->TryGetBoolField  (TEXT("import_basic_animations"), Req.bImportBasicAnimations);
	Args->TryGetStringField(TEXT("asset_name"), Req.AssetName);
	Args->TryGetStringField(TEXT("save_path"),  Req.PackagePath);
	if (Req.InputTaskId.IsEmpty() && Req.ModelUrl.IsEmpty() && Req.MeshAssetPath.IsEmpty())
	{
		PushToast(TEXT("Rig needs a mesh_asset_path, input_task_id, or model_url"), TEXT("error")); return;
	}

	TWeakObjectPtr<UUECPMeshyBridge> WeakSelf(this);
	FMeshyRiggingProvider::Get().Submit(Req, ApiKey,
		[WeakSelf](const FMeshyRiggingResult& Result)
		{
			AsyncTask(ENamedThreads::GameThread, [WeakSelf, Result]
			{
				UUECPMeshyBridge* Self = WeakSelf.Get();
				if (!Self) return;
				TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
				Body->SetBoolField(TEXT("success"), Result.bSuccess);
				if (!Result.TaskId.IsEmpty()) Body->SetStringField(TEXT("task_id"), Result.TaskId);
				if (Result.bSuccess)
				{
					Body->SetStringField(TEXT("skeletal_mesh_path"), Result.SkeletalMeshPath);
					Body->SetStringField(TEXT("skeleton_path"),      Result.SkeletonPath);
					Body->SetStringField(TEXT("asset_path"),         Result.SkeletalMeshPath);
				}
				else if (!Result.ErrorMessage.IsEmpty()) Body->SetStringField(TEXT("error"), Result.ErrorMessage);
				Self->PushMeshyJobFinished(Result.TaskId, Result.bSuccess ? TEXT("done") : TEXT("failed"),
					SerializeMeshyJson(Body));
			});
		});
}

void UUECPMeshyBridge::MeshyAnimate(const FString& Json)
{
	EnsureMeshyTrackerWiring();
	const FString ApiKey = FApiKeyManager::Get().GetActiveMeshGenApiKey();
	if (ApiKey.IsEmpty()) { PushToast(TEXT("Meshy API key not configured"), TEXT("error")); return; }

	TSharedPtr<FJsonObject> Args = ParseMeshyJson(Json);
	if (!Args.IsValid()) return;

	FMeshyAnimationRequest Req;
	Args->TryGetStringField(TEXT("rig_task_id"),         Req.RigTaskId);
	int32 ActionId = 0;
	Args->TryGetNumberField(TEXT("action_id"),           ActionId);
	Req.ActionId = ActionId;
	Args->TryGetStringField(TEXT("skeleton_asset_path"), Req.SkeletonAssetPath);
	int32 Fps = 0;
	if (Args->TryGetNumberField(TEXT("change_fps"), Fps)) Req.ChangeFps = Fps;
	Args->TryGetStringField(TEXT("asset_name"), Req.AssetName);
	Args->TryGetStringField(TEXT("save_path"),  Req.PackagePath);

	if (Req.RigTaskId.IsEmpty() || Req.SkeletonAssetPath.IsEmpty() || Req.ActionId <= 0)
	{
		PushToast(TEXT("Animation needs rig_task_id, skeleton_asset_path, and a positive action_id"), TEXT("error"));
		return;
	}

	TWeakObjectPtr<UUECPMeshyBridge> WeakSelf(this);
	FMeshyAnimationProvider::Get().Submit(Req, ApiKey,
		[WeakSelf](const FMeshyAnimationResult& Result)
		{
			AsyncTask(ENamedThreads::GameThread, [WeakSelf, Result]
			{
				UUECPMeshyBridge* Self = WeakSelf.Get();
				if (!Self) return;
				TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
				Body->SetBoolField(TEXT("success"), Result.bSuccess);
				if (!Result.TaskId.IsEmpty()) Body->SetStringField(TEXT("task_id"), Result.TaskId);
				if (Result.bSuccess && !Result.AnimSequencePath.IsEmpty())
				{
					Body->SetStringField(TEXT("anim_sequence_path"), Result.AnimSequencePath);
					Body->SetStringField(TEXT("asset_path"),         Result.AnimSequencePath);
				}
				else if (!Result.ErrorMessage.IsEmpty()) Body->SetStringField(TEXT("error"), Result.ErrorMessage);
				Self->PushMeshyJobFinished(Result.TaskId, Result.bSuccess ? TEXT("done") : TEXT("failed"),
					SerializeMeshyJson(Body));
			});
		});
}

void UUECPMeshyBridge::MeshyTextToImage(const FString& Json)
{
	EnsureMeshyTrackerWiring();
	const FString ApiKey = FApiKeyManager::Get().GetActiveMeshGenApiKey();
	if (ApiKey.IsEmpty()) { PushToast(TEXT("Meshy API key not configured"), TEXT("error")); return; }

	TSharedPtr<FJsonObject> Args = ParseMeshyJson(Json);
	if (!Args.IsValid()) return;

	FMeshyTextToImageRequest Req;
	Args->TryGetStringField(TEXT("prompt"),          Req.Prompt);
	Args->TryGetStringField(TEXT("aspect_ratio"),    Req.AspectRatio);
	Args->TryGetStringField(TEXT("negative_prompt"), Req.NegativePrompt);
	Args->TryGetStringField(TEXT("asset_name"),      Req.AssetName);
	Args->TryGetStringField(TEXT("save_path"),       Req.PackagePath);
	Args->TryGetStringField(TEXT("ai_model"),        Req.AiModel);
	if (Req.Prompt.IsEmpty()) { PushToast(TEXT("Prompt is required"), TEXT("error")); return; }

	TWeakObjectPtr<UUECPMeshyBridge> WeakSelf(this);
	FMeshyTextToImageProvider::Get().Submit(Req, ApiKey,
		[WeakSelf](const FMeshyTextToImageResult& Result)
		{
			AsyncTask(ENamedThreads::GameThread, [WeakSelf, Result]
			{
				UUECPMeshyBridge* Self = WeakSelf.Get();
				if (!Self) return;
				TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
				Body->SetBoolField(TEXT("success"), Result.bSuccess);
				if (!Result.TaskId.IsEmpty()) Body->SetStringField(TEXT("task_id"), Result.TaskId);
				if (Result.bSuccess) Body->SetStringField(TEXT("asset_path"), Result.TextureAssetPath);
				else if (!Result.ErrorMessage.IsEmpty()) Body->SetStringField(TEXT("error"), Result.ErrorMessage);
				Self->PushMeshyJobFinished(Result.TaskId, Result.bSuccess ? TEXT("done") : TEXT("failed"),
					SerializeMeshyJson(Body));
			});
		});
}

void UUECPMeshyBridge::MeshyGetBalance()
{
	EnsureMeshyTrackerWiring();
	const FString ApiKey = FApiKeyManager::Get().GetActiveMeshGenApiKey();
	if (ApiKey.IsEmpty()) { PushMeshyBalance(0); return; }

	TWeakObjectPtr<UUECPMeshyBridge> WeakSelf(this);
	FMeshyBalanceProvider::Query(ApiKey,
		[WeakSelf](const FMeshyBalanceResult& Result)
		{
			AsyncTask(ENamedThreads::GameThread, [WeakSelf, Result]
			{
				UUECPMeshyBridge* Self = WeakSelf.Get();
				if (!Self) return;
				Self->PushMeshyBalance(Result.bSuccess ? Result.Credits : 0);
			});
		});
}

void UUECPMeshyBridge::MeshyListAnimations()
{
	if (!IUECPCoreModule::IsAvailable()) return;
	FUECPToolResult R = IUECPCoreModule::Get().GetToolDispatcher().ExecuteFromArgs(
		FName(TEXT("list_meshy_animations")), MakeShared<FJsonObject>());
	if (R.bSuccess) PushMeshyAnimCatalog(R.ResultJson);
}

void UUECPMeshyBridge::MeshyCancelJob(const FString& JobId)
{
	if (JobId.IsEmpty()) return;
	FMeshyTaskTracker::Get().CancelTask(JobId);
}

void UUECPMeshyBridge::MeshyOpenAsset(const FString& AssetPath)
{
	if (AssetPath.IsEmpty()) return;
	OpenAssetLink(AssetPath);
}

#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/Skeleton.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
	UClass* ResolveMeshyPickerClass(const FString& Filter)
	{
		if (Filter.Equals(TEXT("StaticMesh"),   ESearchCase::IgnoreCase)) return UStaticMesh::StaticClass();
		if (Filter.Equals(TEXT("SkeletalMesh"), ESearchCase::IgnoreCase)) return USkeletalMesh::StaticClass();
		if (Filter.Equals(TEXT("Skeleton"),     ESearchCase::IgnoreCase)) return USkeleton::StaticClass();
		if (Filter.Equals(TEXT("Mesh"),         ESearchCase::IgnoreCase)) return UObject::StaticClass();
		return nullptr;
	}

	bool MeshyAssetMatchesFilter(const FAssetData& Data, const FString& Filter)
	{
		UClass* Cls = Data.GetClass();
		if (!Cls) return false;
		if (Filter.Equals(TEXT("Mesh"), ESearchCase::IgnoreCase))
		{
			return Cls->IsChildOf(UStaticMesh::StaticClass())
				|| Cls->IsChildOf(USkeletalMesh::StaticClass())
				|| Cls->IsChildOf(USkeleton::StaticClass());
		}
		UClass* FilterClass = ResolveMeshyPickerClass(Filter);
		return FilterClass && Cls->IsChildOf(FilterClass);
	}

	FString GetMeshyHistoryFilePath()
	{
		return FPaths::ProjectSavedDir() / TEXT("MeshyGen") / TEXT("job_history.json");
	}
}

void UUECPMeshyBridge::PushMeshyAssetPicked(const FString& FieldId, const FString& AssetPath)
{
	ExecJs(FString::Printf(TEXT("if(typeof onMeshyAssetPicked==='function')onMeshyAssetPicked('%s','%s')"),
		*EscJsLocal(FieldId), *EscJsLocal(AssetPath)));
}

void UUECPMeshyBridge::PushMeshyHistory(const FString& JsonArray)
{
	ExecJs(FString::Printf(TEXT("if(typeof onMeshyHistoryLoaded==='function')onMeshyHistoryLoaded('%s')"),
		*EscJsLocal(JsonArray)));
}

void UUECPMeshyBridge::MeshyBrowseAsset(const FString& FieldId, const FString& ClassFilter)
{
	UClass* FilterClass = ResolveMeshyPickerClass(ClassFilter);
	if (!FilterClass)
	{
		PushToast(FString::Printf(TEXT("Unknown picker class filter: %s"), *ClassFilter), TEXT("error"));
		return;
	}

	FAssetRegistryModule& AR = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	TArray<FAssetData> Candidates;
	AR.Get().GetAssetsByClass(FilterClass->GetClassPathName(), Candidates);
	if (Candidates.Num() == 0)
	{
		PushToast(FString::Printf(TEXT("No %s assets in this project"), *ClassFilter), TEXT("info"));
		return;
	}

	PushToast(TEXT("Tip: select the asset in the Content Browser then click 'Use Selected'."), TEXT("info"));
}

void UUECPMeshyBridge::MeshyUseSelectedAsset(const FString& FieldId, const FString& ClassFilter)
{
	if (!ResolveMeshyPickerClass(ClassFilter))
	{
		PushToast(FString::Printf(TEXT("Unknown picker class filter: %s"), *ClassFilter), TEXT("error"));
		return;
	}

	FContentBrowserModule& CB = FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
	TArray<FAssetData> Selected;
	CB.Get().GetSelectedAssets(Selected);

	FString PickedPath;
	for (const FAssetData& Data : Selected)
	{
		if (MeshyAssetMatchesFilter(Data, ClassFilter))
		{
			PickedPath = Data.GetObjectPathString();
			break;
		}
	}

	if (PickedPath.IsEmpty())
	{
		const FString ExpectedKind = ClassFilter.Equals(TEXT("Mesh"), ESearchCase::IgnoreCase)
			? TEXT("StaticMesh / SkeletalMesh / Skeleton")
			: ClassFilter;
		PushToast(FString::Printf(TEXT("No %s selected in Content Browser"), *ExpectedKind), TEXT("error"));
		return;
	}
	PushMeshyAssetPicked(FieldId, PickedPath);
}

void UUECPMeshyBridge::MeshySaveHistory(const FString& JsonArray)
{
	const FString Path = GetMeshyHistoryFilePath();
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path),  true);
	FFileHelper::SaveStringToFile(JsonArray, *Path);
}

void UUECPMeshyBridge::MeshyLoadHistory()
{
	const FString Path = GetMeshyHistoryFilePath();
	FString Contents;
	if (FFileHelper::LoadFileToString(Contents, *Path))
	{
		PushMeshyHistory(Contents);
	}
	else
	{
		PushMeshyHistory(TEXT("[]"));
	}
}

void UUECPMeshyBridge::OpenAssetLink(const FString& AssetRef)
{
	FString Decoded = FGenericPlatformHttp::UrlDecode(AssetRef);
	while (Decoded.EndsWith(TEXT("/")))
		Decoded = Decoded.LeftChop(1);

	IAssetRegistry& AR = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
	TArray<FAssetData> Found;
	if (Decoded.StartsWith(TEXT("/Game/")))
		AR.GetAssetsByPackageName(FName(*Decoded), Found);

	if (Found.IsEmpty())
	{
		auto W = OwnerWidget.Pin();
		if (W.IsValid())
		{
			FString Path = W->FindAssetPathByName(Decoded.StartsWith(TEXT("/Game/")) ? FPaths::GetBaseFilename(Decoded) : Decoded);
			if (!Path.IsEmpty())
				AR.GetAssetsByPackageName(FName(*Path), Found);
		}
	}

	if (!Found.IsEmpty())
	{
		FContentBrowserModule& CB = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
		CB.Get().SyncBrowserToAssets(Found, true);
	}
}

#undef LOCTEXT_NAMESPACE
