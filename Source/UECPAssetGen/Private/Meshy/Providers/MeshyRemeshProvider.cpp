// Copyright 2026, BlueprintsLab, All rights reserved

#include "Meshy/Providers/MeshyRemeshProvider.h"
#include "Meshy/MeshyHttpClient.h"
#include "Meshy/MeshyTaskTracker.h"
#include "Meshy/MeshyAssetImporter.h"
#include "UECPAssetGenModule.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"

FMeshyRemeshProvider& FMeshyRemeshProvider::Get()
{
	static FMeshyRemeshProvider Instance;
	return Instance;
}

void FMeshyRemeshProvider::Submit(
	const FMeshyRemeshRequest& Request,
	const FString& ApiKey,
	TFunction<void(const FMeshyRemeshResult&)> OnComplete)
{
	if (ApiKey.IsEmpty())
	{
		FMeshyRemeshResult Result; Result.ErrorMessage = TEXT("Meshy API key is empty");
		OnPhase.Broadcast(Request.AssetName, TEXT("failed"), -1, Result.ErrorMessage);
		OnComplete(Result);
		return;
	}
	if (Request.InputTaskId.IsEmpty() && Request.ModelUrl.IsEmpty() && Request.MeshAssetPath.IsEmpty())
	{
		FMeshyRemeshResult Result; Result.ErrorMessage = TEXT("Remesh requires input_task_id, model_url, or mesh_asset_path");
		OnPhase.Broadcast(Request.AssetName, TEXT("failed"), -1, Result.ErrorMessage);
		OnComplete(Result);
		return;
	}

	FString ResolvedModelUrl = Request.ModelUrl;
	if (Request.InputTaskId.IsEmpty() && Request.ModelUrl.IsEmpty() && !Request.MeshAssetPath.IsEmpty())
	{
		FString Err;
		const FString TempFbx = FMeshyAssetImporter::ExportStaticMeshToFbxTemp(Request.MeshAssetPath, Err);
		if (TempFbx.IsEmpty()) { FMeshyRemeshResult R; R.ErrorMessage = Err; OnPhase.Broadcast(Request.AssetName, TEXT("failed"), -1, Err); OnComplete(R); return; }
		ResolvedModelUrl = FMeshyAssetImporter::FileToBase64DataUri(TempFbx, Err);
		if (ResolvedModelUrl.IsEmpty()) { FMeshyRemeshResult R; R.ErrorMessage = Err; OnPhase.Broadcast(Request.AssetName, TEXT("failed"), -1, Err); OnComplete(R); return; }
	}

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	if (!Request.InputTaskId.IsEmpty()) Payload->SetStringField(TEXT("input_task_id"), Request.InputTaskId);
	else                                Payload->SetStringField(TEXT("model_url"),     ResolvedModelUrl);

	Payload->SetStringField(TEXT("topology"),         Request.Topology);
	Payload->SetNumberField(TEXT("target_polycount"), FMath::Clamp(Request.PolyCount, 100, 300000));
	if (Request.ResizeHeight > 0.f)
	{
		Payload->SetNumberField(TEXT("resize_height"), Request.ResizeHeight);
	}

	TArray<TSharedPtr<FJsonValue>> Formats;
	Formats.Add(MakeShared<FJsonValueString>(TEXT("glb")));
	Payload->SetArrayField(TEXT("target_formats"), Formats);

	FMeshyHttpClient::Get().Post(TEXT("https://api.meshy.ai/openapi/v1/remesh"), ApiKey, Payload,
		[this, Request, ApiKey, OnComplete = MoveTemp(OnComplete)](const FMeshyHttpResult& HttpResult) mutable
		{
			FMeshyRemeshResult Result;

			if (!HttpResult.bSuccess || !HttpResult.ResponseJson.IsValid())
			{
				Result.ErrorMessage = HttpResult.Error.IsEmpty()
					? FString::Printf(TEXT("Remesh submit failed (HTTP %d)"), HttpResult.HttpCode)
					: HttpResult.Error.ToDisplayString();
				OnPhase.Broadcast(Request.AssetName, TEXT("failed"), -1, Result.ErrorMessage);
				OnComplete(Result);
				return;
			}

			FString TaskId;
			HttpResult.ResponseJson->TryGetStringField(TEXT("result"), TaskId);
			if (TaskId.IsEmpty())
			{
				Result.ErrorMessage = FString::Printf(TEXT("No task id in remesh response: %s"), *HttpResult.ResponseBody.Left(200));
				OnPhase.Broadcast(Request.AssetName, TEXT("failed"), -1, Result.ErrorMessage);
				OnComplete(Result);
				return;
			}

			Result.TaskId = TaskId;
			OnPhase.Broadcast(Request.AssetName, TEXT("generating"), 0, FString());
			TrackAndImport(TaskId, Request, ApiKey, MoveTemp(OnComplete));
		});
}

void FMeshyRemeshProvider::TrackAndImport(
	const FString& TaskId,
	const FMeshyRemeshRequest& Request,
	const FString& ApiKey,
	TFunction<void(const FMeshyRemeshResult&)> OnComplete)
{
	FMeshyTaskInfo Info;
	Info.TaskId    = TaskId;
	Info.ApiKey    = ApiKey;
	Info.PollUrl   = FString::Printf(TEXT("https://api.meshy.ai/openapi/v1/remesh/%s"), *TaskId);
	Info.Kind      = EMeshyTaskKind::Remesh;
	Info.Label     = Request.AssetName.IsEmpty() ? TEXT("Meshy remesh") : FString::Printf(TEXT("Remesh %s"), *Request.AssetName);
	Info.CreatedAt = FDateTime::UtcNow();

	FMeshyTaskTracker::Get().AddTask(Info,
		[this, Request, OnComplete = MoveTemp(OnComplete)](const FMeshyTaskResult& Terminal) mutable
		{
			FMeshyRemeshResult Result;
			Result.TaskId = Terminal.TaskId;

			if (!Terminal.bSuccess || !Terminal.ResponseJson.IsValid())
			{
				Result.ErrorMessage = Terminal.ErrorMessage.IsEmpty()
					? FString::Printf(TEXT("Remesh %s"), *Terminal.Status)
					: Terminal.ErrorMessage;
				OnPhase.Broadcast(Request.AssetName, TEXT("failed"), -1, Result.ErrorMessage);
				OnComplete(Result);
				return;
			}

			FString GlbUrl;
			const TSharedPtr<FJsonObject>* ModelUrlsObj = nullptr;
			if (Terminal.ResponseJson->TryGetObjectField(TEXT("model_urls"), ModelUrlsObj) && ModelUrlsObj && ModelUrlsObj->IsValid())
			{
				(*ModelUrlsObj)->TryGetStringField(TEXT("glb"), GlbUrl);
			}
			if (GlbUrl.IsEmpty())
			{
				Result.ErrorMessage = TEXT("Remesh succeeded but no GLB URL in response");
				OnPhase.Broadcast(Request.AssetName, TEXT("failed"), -1, Result.ErrorMessage);
				OnComplete(Result);
				return;
			}

			OnPhase.Broadcast(Request.AssetName, TEXT("importing"), -1, FString());
			DownloadAndImport(GlbUrl, Request, MoveTemp(Result), MoveTemp(OnComplete));
		});
}

void FMeshyRemeshProvider::DownloadAndImport(
	const FString& GlbUrl,
	const FMeshyRemeshRequest& Request,
	FMeshyRemeshResult Result,
	TFunction<void(const FMeshyRemeshResult&)> OnComplete)
{
	const FString AssetName = Request.AssetName.IsEmpty() ? TEXT("RemeshedModel") : Request.AssetName;
	const FString DestPath  = FPaths::ProjectSavedDir() / TEXT("GeneratedContent") /
		FString::Printf(TEXT("Remesh_%s_%s.glb"), *AssetName, *FGuid::NewGuid().ToString().Left(8));

	FMeshyHttpClient::Get().Download(GlbUrl, DestPath,
		[this, Request, Result = MoveTemp(Result), OnComplete = MoveTemp(OnComplete)]
		(bool bOk, const FString& LocalPath) mutable
		{
			if (!bOk || LocalPath.IsEmpty())
			{
				Result.bSuccess = false;
				Result.ErrorMessage = TEXT("Failed to download remeshed GLB");
				OnPhase.Broadcast(Request.AssetName, TEXT("failed"), -1, Result.ErrorMessage);
				OnComplete(Result);
				return;
			}

			const FString AssetName = Request.AssetName.IsEmpty() ? TEXT("RemeshedModel") : Request.AssetName;
			const FMeshyMeshImportResult Imported = FMeshyAssetImporter::ImportMeshFromFile(LocalPath, AssetName, Request.PackagePath);
			Result.bSuccess   = Imported.bSuccess;
			Result.AssetPath  = Imported.PrimaryAssetPath;
			if (!Imported.bSuccess)
			{
				Result.ErrorMessage = Imported.ErrorMessage;
				OnPhase.Broadcast(Request.AssetName, TEXT("failed"), -1, Result.ErrorMessage);
			}
			else
			{
				OnPhase.Broadcast(Request.AssetName, TEXT("ready"), 100, FString());
			}
			OnComplete(Result);
		});
}
