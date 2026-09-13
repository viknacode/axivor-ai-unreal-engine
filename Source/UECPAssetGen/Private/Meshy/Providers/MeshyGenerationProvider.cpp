// Copyright 2026, BlueprintsLab, All rights reserved

#include "Meshy/Providers/MeshyGenerationProvider.h"
#include "Meshy/MeshyHttpClient.h"
#include "Meshy/MeshyTaskTracker.h"
#include "Meshy/MeshyAssetImporter.h"
#include "UECPAssetGenModule.h"
#include "ApiKeyManager.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"

namespace
{
	constexpr const TCHAR* MeshyTextTo3DEndpoint  = TEXT("https://api.meshy.ai/openapi/v2/text-to-3d");
	constexpr const TCHAR* MeshyImageTo3DEndpoint = TEXT("https://api.meshy.ai/openapi/v1/image-to-3d");

	FString CurrentAiModel()
	{
		return FApiKeyManager::Get().GetActiveMeshyAiModel();
	}

	FString BuildImageDataUri(const FMeshCreationRequest& Request)
	{
		if (!Request.ImageBase64Data.IsEmpty())
		{
			return FString::Printf(TEXT("data:image/png;base64,%s"), *Request.ImageBase64Data);
		}
		return Request.ImagePath;
	}

	FString BuildPollUrl(FMeshyGenerationProvider* , bool bIsImageJob, const FString& TaskId)
	{
		if (bIsImageJob)
		{
			return FString::Printf(TEXT("https://api.meshy.ai/openapi/v1/image-to-3d/%s"), *TaskId);
		}
		return FString::Printf(TEXT("https://api.meshy.ai/openapi/v2/text-to-3d/%s"), *TaskId);
	}
}

FMeshyGenerationProvider& FMeshyGenerationProvider::Get()
{
	static FMeshyGenerationProvider Instance;
	return Instance;
}

void FMeshyGenerationProvider::SubmitTextTo3D(
	const FMeshCreationRequest& Request,
	const FString& ApiKey,
	TFunction<void(const FMeshCreationResult&)> OnComplete)
{
	Submit(EJobKind::TextPreview, Request, ApiKey, MoveTemp(OnComplete));
}

void FMeshyGenerationProvider::SubmitImageTo3D(
	const FMeshCreationRequest& Request,
	const FString& ApiKey,
	TFunction<void(const FMeshCreationResult&)> OnComplete)
{
	Submit(EJobKind::ImagePreview, Request, ApiKey, MoveTemp(OnComplete));
}

void FMeshyGenerationProvider::SubmitRefine(
	const FMeshCreationRequest& Request,
	const FString& ApiKey,
	TFunction<void(const FMeshCreationResult&)> OnComplete)
{
	Submit(EJobKind::Refine, Request, ApiKey, MoveTemp(OnComplete));
}

void FMeshyGenerationProvider::Submit(
	EJobKind Kind,
	const FMeshCreationRequest& Request,
	const FString& ApiKey,
	TFunction<void(const FMeshCreationResult&)> OnComplete)
{
	if (ApiKey.IsEmpty())
	{
		FMeshCreationResult Result;
		Result.ErrorMessage = TEXT("Meshy API key is empty");
		OnPhase.Broadcast(Request.CustomAssetName, TEXT("failed"), -1, Result.ErrorMessage);
		OnComplete(Result);
		return;
	}

	FString Endpoint;
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();

	switch (Kind)
	{
	case EJobKind::TextPreview:
	{
		Endpoint = MeshyTextTo3DEndpoint;
		Payload->SetStringField(TEXT("mode"),     TEXT("preview"));
		Payload->SetStringField(TEXT("ai_model"), CurrentAiModel());
		Payload->SetStringField(TEXT("prompt"),   Request.Prompt);
		if (!Request.NegativePrompt.IsEmpty()) Payload->SetStringField(TEXT("negative_prompt"), Request.NegativePrompt);
		if (!Request.PoseMode.IsEmpty())       Payload->SetStringField(TEXT("pose_mode"),       Request.PoseMode);
		if (Request.Seed > 0)                  Payload->SetNumberField(TEXT("seed"), Request.Seed);
		break;
	}
	case EJobKind::ImagePreview:
	{
		Endpoint = MeshyImageTo3DEndpoint;
		Payload->SetStringField(TEXT("image_url"), BuildImageDataUri(Request));
		Payload->SetStringField(TEXT("ai_model"),  CurrentAiModel());
		if (!Request.Prompt.IsEmpty())   Payload->SetStringField(TEXT("prompt"),    Request.Prompt);
		if (!Request.PoseMode.IsEmpty()) Payload->SetStringField(TEXT("pose_mode"), Request.PoseMode);
		break;
	}
	case EJobKind::Refine:
	{
		Endpoint = MeshyTextTo3DEndpoint;
		Payload->SetStringField(TEXT("mode"),            TEXT("refine"));
		Payload->SetStringField(TEXT("preview_task_id"), Request.PreviewTaskId);
		Payload->SetStringField(TEXT("ai_model"),        CurrentAiModel());
		Payload->SetBoolField  (TEXT("enable_pbr"),      true);
		break;
	}
	}

	UE_LOG(LogUECPAssetGen, Log, TEXT("Meshy submit: %s [%s]"),
		Kind == EJobKind::TextPreview  ? TEXT("text-to-3d/preview") :
		Kind == EJobKind::ImagePreview ? TEXT("image-to-3d/preview") :
		                                 TEXT("text-to-3d/refine"),
		*Request.CustomAssetName);

	FMeshyHttpClient::Get().Post(Endpoint, ApiKey, Payload,
		[this, Kind, Request, ApiKey, OnComplete = MoveTemp(OnComplete)](const FMeshyHttpResult& HttpResult) mutable
		{
			FMeshCreationResult Result;
			if (!HttpResult.bSuccess || !HttpResult.ResponseJson.IsValid())
			{
				Result.ErrorMessage = HttpResult.Error.IsEmpty()
					? FString::Printf(TEXT("Submit failed (HTTP %d)"), HttpResult.HttpCode)
					: HttpResult.Error.ToDisplayString();
				OnPhase.Broadcast(Request.CustomAssetName, TEXT("failed"), -1, Result.ErrorMessage);
				OnComplete(Result);
				return;
			}

			FString TaskId;
			HttpResult.ResponseJson->TryGetStringField(TEXT("result"), TaskId);
			if (TaskId.IsEmpty())
			{
				Result.ErrorMessage = FString::Printf(TEXT("Meshy did not return a task id: %s"), *HttpResult.ResponseBody.Left(200));
				OnPhase.Broadcast(Request.CustomAssetName, TEXT("failed"), -1, Result.ErrorMessage);
				OnComplete(Result);
				return;
			}

			OnPhase.Broadcast(Request.CustomAssetName, Kind == EJobKind::Refine ? TEXT("refining") : TEXT("generating"), 0, FString());
			TrackTask(Kind, TaskId, Request, ApiKey, MoveTemp(OnComplete));
		});
}

void FMeshyGenerationProvider::TrackTask(
	EJobKind Kind,
	const FString& TaskId,
	const FMeshCreationRequest& Request,
	const FString& ApiKey,
	TFunction<void(const FMeshCreationResult&)> OnComplete)
{
	const bool bIsImage = (Kind == EJobKind::ImagePreview);

	FMeshyTaskInfo Info;
	Info.TaskId    = TaskId;
	Info.ApiKey    = ApiKey;
	Info.PollUrl   = BuildPollUrl(this, bIsImage, TaskId);
	Info.Kind      = bIsImage ? EMeshyTaskKind::ImageTo3D
	                : Kind == EJobKind::Refine ? EMeshyTaskKind::TextTo3DRefine
	                : EMeshyTaskKind::TextTo3DPreview;
	Info.Label     = Request.CustomAssetName.IsEmpty()
		? FString::Printf(TEXT("Meshy %s"), bIsImage ? TEXT("image→3D") : TEXT("text→3D"))
		: Request.CustomAssetName;
	Info.CreatedAt = FDateTime::UtcNow();

	FMeshyTaskTracker::Get().AddTask(Info,
		[this, Kind, Request, ApiKey, OnComplete = MoveTemp(OnComplete)](const FMeshyTaskResult& Terminal) mutable
		{
			HandleTerminal(Kind, Terminal, Request, ApiKey, MoveTemp(OnComplete));
		});
}

void FMeshyGenerationProvider::HandleTerminal(
	EJobKind Kind,
	const FMeshyTaskResult& Terminal,
	const FMeshCreationRequest& Request,
	const FString& ApiKey,
	TFunction<void(const FMeshCreationResult&)> OnComplete)
{
	FMeshCreationResult Result;
	Result.TaskId   = Terminal.TaskId;
	Result.Status   = Terminal.Status;
	Result.Progress = Terminal.Progress;

	if (!Terminal.bSuccess || !Terminal.ResponseJson.IsValid())
	{
		Result.ErrorMessage = Terminal.ErrorMessage.IsEmpty()
			? FString::Printf(TEXT("Task %s"), *Terminal.Status)
			: Terminal.ErrorMessage;
		OnPhase.Broadcast(Request.CustomAssetName, TEXT("failed"), -1, Result.ErrorMessage);
		OnComplete(Result);
		return;
	}

	const TSharedPtr<FJsonObject>& Json = Terminal.ResponseJson;

	Json->TryGetStringField(TEXT("thumbnail_url"), Result.ThumbnailUrl);

	FString TaskType, Mode;
	Json->TryGetStringField(TEXT("type"), TaskType);
	Json->TryGetStringField(TEXT("mode"), Mode);
	const bool bIsPreview = TaskType.Contains(TEXT("preview")) || Mode == TEXT("preview");
	Result.bIsPreview = bIsPreview;

	FString GlbUrl;
	const TSharedPtr<FJsonObject>* ModelUrlsObj = nullptr;
	if (Json->TryGetObjectField(TEXT("model_urls"), ModelUrlsObj) && ModelUrlsObj && ModelUrlsObj->IsValid())
	{
		(*ModelUrlsObj)->TryGetStringField(TEXT("glb"), GlbUrl);
	}

	if (GlbUrl.IsEmpty())
	{
		Result.ErrorMessage = TEXT("Task succeeded but model_urls.glb was empty");
		OnPhase.Broadcast(Request.CustomAssetName, TEXT("failed"), -1, Result.ErrorMessage);
		OnComplete(Result);
		return;
	}

	if (Kind != EJobKind::Refine && bIsPreview && Request.bAutoRefine)
	{
		OnPhase.Broadcast(Request.CustomAssetName, TEXT("refining"), 0, FString());
		FMeshCreationRequest RefineRequest = Request;
		RefineRequest.PreviewTaskId = Terminal.TaskId;
		SubmitRefine(RefineRequest, ApiKey, MoveTemp(OnComplete));
		return;
	}

	OnPhase.Broadcast(Request.CustomAssetName, TEXT("importing"), -1, FString());
	DownloadAndImport(GlbUrl, Request, MoveTemp(Result), MoveTemp(OnComplete));
}

void FMeshyGenerationProvider::DownloadAndImport(
	const FString& GlbUrl,
	const FMeshCreationRequest& Request,
	FMeshCreationResult Result,
	TFunction<void(const FMeshCreationResult&)> OnComplete)
{
	const FString Filename = FString::Printf(TEXT("GeneratedContent/%s_%s.glb"),
		*Request.CustomAssetName, *FGuid::NewGuid().ToString().Left(8));
	const FString DestPath = FPaths::ProjectSavedDir() / Filename;

	FMeshyHttpClient::Get().Download(GlbUrl, DestPath,
		[this, Request, Result = MoveTemp(Result), OnComplete = MoveTemp(OnComplete)]
		(bool bDownloadOk, const FString& LocalPath) mutable
		{
			if (!bDownloadOk || LocalPath.IsEmpty())
			{
				Result.bSuccess     = false;
				Result.ErrorMessage = TEXT("Failed to download GLB from Meshy");
				OnPhase.Broadcast(Request.CustomAssetName, TEXT("failed"), -1, Result.ErrorMessage);
				OnComplete(Result);
				return;
			}

			const FString TargetName = Request.CustomAssetName.IsEmpty() ? TEXT("GeneratedMesh") : Request.CustomAssetName;
			const FMeshyMeshImportResult Imported = FMeshyAssetImporter::ImportMeshFromFile(LocalPath, TargetName, Request.SavePath);

			Result.bSuccess     = Imported.bSuccess;
			Result.DownloadPath = LocalPath;
			Result.AssetPath    = Imported.PrimaryAssetPath;
			if (!Imported.bSuccess)
			{
				Result.ErrorMessage = Imported.ErrorMessage;
				OnPhase.Broadcast(Request.CustomAssetName, TEXT("failed"), -1, Result.ErrorMessage);
			}
			else
			{
				Result.Progress = 100;
				OnPhase.Broadcast(Request.CustomAssetName, TEXT("ready"), 100, FString());
			}
			OnComplete(Result);
		});
}
