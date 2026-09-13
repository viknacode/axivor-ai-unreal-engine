// Copyright 2026, BlueprintsLab, All rights reserved

#include "Meshy/Providers/MeshyAnimationProvider.h"
#include "Meshy/MeshyHttpClient.h"
#include "Meshy/MeshyTaskTracker.h"
#include "Meshy/MeshyAssetImporter.h"
#include "UECPAssetGenModule.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"

FMeshyAnimationProvider& FMeshyAnimationProvider::Get()
{
	static FMeshyAnimationProvider Instance;
	return Instance;
}

void FMeshyAnimationProvider::Submit(
	const FMeshyAnimationRequest& Request,
	const FString& ApiKey,
	TFunction<void(const FMeshyAnimationResult&)> OnComplete)
{
	auto Fail = [&](const FString& Msg)
	{
		FMeshyAnimationResult Result; Result.ErrorMessage = Msg;
		OnPhase.Broadcast(Request.AssetName, TEXT("failed"), -1, Msg);
		OnComplete(Result);
	};

	if (ApiKey.IsEmpty())                       { Fail(TEXT("Meshy API key is empty"));               return; }
	if (Request.RigTaskId.IsEmpty())            { Fail(TEXT("rig_task_id is required"));              return; }
	if (Request.ActionId <= 0)                  { Fail(TEXT("action_id is required (must be > 0)"));  return; }
	if (Request.SkeletonAssetPath.IsEmpty())    { Fail(TEXT("skeleton_asset_path is required so the anim can target a skeleton")); return; }

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("rig_task_id"), Request.RigTaskId);
	Payload->SetNumberField(TEXT("action_id"),   Request.ActionId);

	if (Request.ChangeFps > 0 || Request.bExtractArmature)
	{
		TSharedPtr<FJsonObject> PostProc = MakeShared<FJsonObject>();
		if (Request.ChangeFps > 0) PostProc->SetNumberField(TEXT("change_fps"),     Request.ChangeFps);
		if (Request.bExtractArmature) PostProc->SetBoolField(TEXT("extract_armature"), true);
		Payload->SetObjectField(TEXT("post_process"), PostProc);
	}

	FMeshyHttpClient::Get().Post(TEXT("https://api.meshy.ai/openapi/v1/animations"), ApiKey, Payload,
		[this, Request, ApiKey, OnComplete = MoveTemp(OnComplete)](const FMeshyHttpResult& HttpResult) mutable
		{
			FMeshyAnimationResult Result;

			if (!HttpResult.bSuccess || !HttpResult.ResponseJson.IsValid())
			{
				Result.ErrorMessage = HttpResult.Error.IsEmpty()
					? FString::Printf(TEXT("Animation submit failed (HTTP %d)"), HttpResult.HttpCode)
					: HttpResult.Error.ToDisplayString();
				OnPhase.Broadcast(Request.AssetName, TEXT("failed"), -1, Result.ErrorMessage);
				OnComplete(Result);
				return;
			}

			FString TaskId;
			HttpResult.ResponseJson->TryGetStringField(TEXT("result"), TaskId);
			if (TaskId.IsEmpty())
			{
				Result.ErrorMessage = FString::Printf(TEXT("No task id in animation response: %s"), *HttpResult.ResponseBody.Left(200));
				OnPhase.Broadcast(Request.AssetName, TEXT("failed"), -1, Result.ErrorMessage);
				OnComplete(Result);
				return;
			}

			Result.TaskId = TaskId;
			OnPhase.Broadcast(Request.AssetName, TEXT("generating"), 0, FString());
			Track(TaskId, Request, ApiKey, MoveTemp(OnComplete));
		});
}

void FMeshyAnimationProvider::Track(
	const FString& TaskId,
	const FMeshyAnimationRequest& Request,
	const FString& ApiKey,
	TFunction<void(const FMeshyAnimationResult&)> OnComplete)
{
	FMeshyTaskInfo Info;
	Info.TaskId    = TaskId;
	Info.ApiKey    = ApiKey;
	Info.PollUrl   = FString::Printf(TEXT("https://api.meshy.ai/openapi/v1/animations/%s"), *TaskId);
	Info.Kind      = EMeshyTaskKind::Animation;
	Info.Label     = Request.AssetName.IsEmpty()
		? FString::Printf(TEXT("Meshy anim #%d"), Request.ActionId)
		: FString::Printf(TEXT("Anim %s"), *Request.AssetName);
	Info.CreatedAt = FDateTime::UtcNow();

	FMeshyTaskTracker::Get().AddTask(Info,
		[this, Request, OnComplete = MoveTemp(OnComplete)](const FMeshyTaskResult& Terminal) mutable
		{
			FMeshyAnimationResult Result;
			Result.TaskId = Terminal.TaskId;

			if (!Terminal.bSuccess || !Terminal.ResponseJson.IsValid())
			{
				Result.ErrorMessage = Terminal.ErrorMessage.IsEmpty()
					? FString::Printf(TEXT("Animation %s"), *Terminal.Status)
					: Terminal.ErrorMessage;
				OnPhase.Broadcast(Request.AssetName, TEXT("failed"), -1, Result.ErrorMessage);
				OnComplete(Result);
				return;
			}

			const TSharedPtr<FJsonObject>* ResultObjPtr = nullptr;
			TSharedPtr<FJsonObject> ResultObj;
			if (Terminal.ResponseJson->TryGetObjectField(TEXT("result"), ResultObjPtr) && ResultObjPtr && ResultObjPtr->IsValid())
			{
				ResultObj = *ResultObjPtr;
			}

			FString FbxUrl;
			auto TryKeysIn = [&FbxUrl](const TSharedPtr<FJsonObject>& Obj)
			{
				if (!Obj.IsValid()) return;
				static const TCHAR* Keys[] = {
					TEXT("rigged_fbx_url"),
					TEXT("animation_fbx_url"),
					TEXT("fbx_url"),
					TEXT("rigged_glb_url"),
					TEXT("animation_glb_url"),
					TEXT("glb_url"),
					TEXT("armature_fbx_url"),
					TEXT("armature_glb_url"),
				};
				for (const TCHAR* K : Keys)
				{
					if (FbxUrl.IsEmpty()) Obj->TryGetStringField(FString(K), FbxUrl);
				}
			};
			TryKeysIn(ResultObj);
			if (FbxUrl.IsEmpty()) TryKeysIn(Terminal.ResponseJson);

			if (FbxUrl.IsEmpty())
			{
				FString RawJson;
				const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&RawJson);
				FJsonSerializer::Serialize(Terminal.ResponseJson.ToSharedRef(), Writer);
				UE_LOG(LogUECPAssetGen, Warning, TEXT("Meshy animation SUCCEEDED but no FBX URL found. Full response:\n%s"), *RawJson);
				Result.ErrorMessage = TEXT("Animation succeeded but no FBX URL in response (logged full payload to LogUECPAssetGen)");
				OnPhase.Broadcast(Request.AssetName, TEXT("failed"), -1, Result.ErrorMessage);
				OnComplete(Result);
				return;
			}

			OnPhase.Broadcast(Request.AssetName, TEXT("importing"), -1, FString());

			const FString AssetName = Request.AssetName.IsEmpty()
				? FString::Printf(TEXT("MeshyAnim_%d"), Request.ActionId)
				: Request.AssetName;
			const FString DestPath = FPaths::ProjectSavedDir() / TEXT("GeneratedContent") /
				FString::Printf(TEXT("Anim_%s_%s.fbx"), *AssetName, *FGuid::NewGuid().ToString().Left(8));

			FMeshyHttpClient::Get().Download(FbxUrl, DestPath,
				[this, AssetName, Request, Result = MoveTemp(Result), OnComplete = MoveTemp(OnComplete)]
				(bool bOk, const FString& LocalPath) mutable
				{
					if (!bOk || LocalPath.IsEmpty())
					{
						Result.bSuccess = false;
						Result.ErrorMessage = TEXT("Failed to download animation FBX");
						OnPhase.Broadcast(Request.AssetName, TEXT("failed"), -1, Result.ErrorMessage);
						OnComplete(Result);
						return;
					}

					const FMeshyAnimationImportResult AnimImport =
						FMeshyAssetImporter::ImportAnimationFromFile(LocalPath, AssetName, Request.PackagePath, Request.SkeletonAssetPath);
					Result.bSuccess         = AnimImport.bSuccess;
					Result.AnimSequencePath = AnimImport.AnimSequencePath;
					if (!AnimImport.bSuccess)
					{
						Result.ErrorMessage = AnimImport.ErrorMessage;
						OnPhase.Broadcast(Request.AssetName, TEXT("failed"), -1, Result.ErrorMessage);
					}
					else
					{
						OnPhase.Broadcast(Request.AssetName, TEXT("ready"), 100, FString());
					}
					OnComplete(Result);
				});
		});
}
