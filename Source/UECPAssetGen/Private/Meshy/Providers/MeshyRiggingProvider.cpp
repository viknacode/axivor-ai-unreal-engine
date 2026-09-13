// Copyright 2026, BlueprintsLab, All rights reserved

#include "Meshy/Providers/MeshyRiggingProvider.h"
#include "Meshy/MeshyHttpClient.h"
#include "Meshy/MeshyTaskTracker.h"
#include "Meshy/MeshyAssetImporter.h"
#include "UECPAssetGenModule.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"

FMeshyRiggingProvider& FMeshyRiggingProvider::Get()
{
	static FMeshyRiggingProvider Instance;
	return Instance;
}

void FMeshyRiggingProvider::Submit(
	const FMeshyRiggingRequest& Request,
	const FString& ApiKey,
	TFunction<void(const FMeshyRiggingResult&)> OnComplete)
{
	if (ApiKey.IsEmpty())
	{
		FMeshyRiggingResult Result; Result.ErrorMessage = TEXT("Meshy API key is empty");
		OnPhase.Broadcast(Request.AssetName, TEXT("failed"), -1, Result.ErrorMessage);
		OnComplete(Result);
		return;
	}
	if (Request.InputTaskId.IsEmpty() && Request.ModelUrl.IsEmpty() && Request.MeshAssetPath.IsEmpty())
	{
		FMeshyRiggingResult Result; Result.ErrorMessage = TEXT("Rigging requires input_task_id, model_url, or mesh_asset_path");
		OnPhase.Broadcast(Request.AssetName, TEXT("failed"), -1, Result.ErrorMessage);
		OnComplete(Result);
		return;
	}

	FString ResolvedModelUrl = Request.ModelUrl;
	if (Request.InputTaskId.IsEmpty() && Request.ModelUrl.IsEmpty() && !Request.MeshAssetPath.IsEmpty())
	{
		FString ExportError;
		const FString TempFbx = FMeshyAssetImporter::ExportStaticMeshToFbxTemp(Request.MeshAssetPath, ExportError);
		if (TempFbx.IsEmpty())
		{
			FMeshyRiggingResult Result; Result.ErrorMessage = ExportError;
			OnPhase.Broadcast(Request.AssetName, TEXT("failed"), -1, Result.ErrorMessage);
			OnComplete(Result);
			return;
		}
		FString DataUriError;
		ResolvedModelUrl = FMeshyAssetImporter::FileToBase64DataUri(TempFbx, DataUriError);
		if (ResolvedModelUrl.IsEmpty())
		{
			FMeshyRiggingResult Result; Result.ErrorMessage = DataUriError;
			OnPhase.Broadcast(Request.AssetName, TEXT("failed"), -1, Result.ErrorMessage);
			OnComplete(Result);
			return;
		}
		UE_LOG(LogUECPAssetGen, Log, TEXT("Meshy rig: uploaded %s as data URI (%d KB)"),
			*Request.MeshAssetPath, ResolvedModelUrl.Len() / 1024);
	}

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	if (!Request.InputTaskId.IsEmpty()) Payload->SetStringField(TEXT("input_task_id"), Request.InputTaskId);
	else                                Payload->SetStringField(TEXT("model_url"),     ResolvedModelUrl);
	Payload->SetNumberField(TEXT("height_meters"), Request.HeightMeters);

	if (!Request.InputTaskId.IsEmpty())
	{
		UE_LOG(LogUECPAssetGen, Log, TEXT("Meshy rig: submitting via TASK input_task_id=%s  height=%.2f"),
			*Request.InputTaskId, Request.HeightMeters);
	}
	else
	{
		const bool bIsDataUri = ResolvedModelUrl.StartsWith(TEXT("data:"));
		UE_LOG(LogUECPAssetGen, Log, TEXT("Meshy rig: submitting via %s model_url=%s%s  height=%.2f"),
			bIsDataUri ? TEXT("UPLOAD") : TEXT("URL"),
			*ResolvedModelUrl.Left(80),
			bIsDataUri ? TEXT("… (truncated data URI)") : TEXT(""),
			Request.HeightMeters);
	}

	FMeshyHttpClient::Get().Post(TEXT("https://api.meshy.ai/openapi/v1/rigging"), ApiKey, Payload,
		[this, Request, ApiKey, OnComplete = MoveTemp(OnComplete)](const FMeshyHttpResult& HttpResult) mutable
		{
			FMeshyRiggingResult Result;

			if (!HttpResult.bSuccess || !HttpResult.ResponseJson.IsValid())
			{
				Result.ErrorMessage = HttpResult.Error.IsEmpty()
					? FString::Printf(TEXT("Rigging submit failed (HTTP %d)"), HttpResult.HttpCode)
					: HttpResult.Error.ToDisplayString();
				OnPhase.Broadcast(Request.AssetName, TEXT("failed"), -1, Result.ErrorMessage);
				OnComplete(Result);
				return;
			}

			FString TaskId;
			HttpResult.ResponseJson->TryGetStringField(TEXT("result"), TaskId);
			if (TaskId.IsEmpty())
			{
				Result.ErrorMessage = FString::Printf(TEXT("No task id in rigging response: %s"), *HttpResult.ResponseBody.Left(200));
				OnPhase.Broadcast(Request.AssetName, TEXT("failed"), -1, Result.ErrorMessage);
				OnComplete(Result);
				return;
			}

			Result.TaskId = TaskId;
			OnPhase.Broadcast(Request.AssetName, TEXT("generating"), 0, FString());
			Track(TaskId, Request, ApiKey, MoveTemp(OnComplete));
		});
}

void FMeshyRiggingProvider::Track(
	const FString& TaskId,
	const FMeshyRiggingRequest& Request,
	const FString& ApiKey,
	TFunction<void(const FMeshyRiggingResult&)> OnComplete)
{
	FMeshyTaskInfo Info;
	Info.TaskId    = TaskId;
	Info.ApiKey    = ApiKey;
	Info.PollUrl   = FString::Printf(TEXT("https://api.meshy.ai/openapi/v1/rigging/%s"), *TaskId);
	Info.Kind      = EMeshyTaskKind::Rigging;
	Info.Label     = Request.AssetName.IsEmpty() ? TEXT("Meshy rigging") : FString::Printf(TEXT("Rig %s"), *Request.AssetName);
	Info.CreatedAt = FDateTime::UtcNow();

	FMeshyTaskTracker::Get().AddTask(Info,
		[this, Request, OnComplete = MoveTemp(OnComplete)](const FMeshyTaskResult& Terminal) mutable
		{
			FMeshyRiggingResult Result;
			Result.TaskId = Terminal.TaskId;

			if (!Terminal.bSuccess || !Terminal.ResponseJson.IsValid())
			{
				Result.ErrorMessage = Terminal.ErrorMessage.IsEmpty()
					? FString::Printf(TEXT("Rigging %s"), *Terminal.Status)
					: Terminal.ErrorMessage;
				if (Terminal.ResponseJson.IsValid())
				{
					FString RawJson;
					const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&RawJson);
					FJsonSerializer::Serialize(Terminal.ResponseJson.ToSharedRef(), Writer);
					UE_LOG(LogUECPAssetGen, Warning, TEXT("Meshy rig terminal-state response:\n%s"), *RawJson);
				}
				UE_LOG(LogUECPAssetGen, Warning, TEXT("Meshy rig failed for %s: %s"), *Request.AssetName, *Result.ErrorMessage);
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
			if (ResultObj.IsValid())
			{
				ResultObj->TryGetStringField(TEXT("rigged_character_fbx_url"), FbxUrl);
				if (FbxUrl.IsEmpty()) ResultObj->TryGetStringField(TEXT("rigged_character_glb_url"), FbxUrl);
			}
			if (FbxUrl.IsEmpty()) Terminal.ResponseJson->TryGetStringField(TEXT("rigged_character_fbx_url"), FbxUrl);
			if (FbxUrl.IsEmpty()) Terminal.ResponseJson->TryGetStringField(TEXT("rigged_character_glb_url"), FbxUrl);

			if (FbxUrl.IsEmpty())
			{
				FString RawJson;
				const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&RawJson);
				FJsonSerializer::Serialize(Terminal.ResponseJson.ToSharedRef(), Writer);
				UE_LOG(LogUECPAssetGen, Warning, TEXT("Meshy rig SUCCEEDED but no rigged-mesh URL found. Full response:\n%s"), *RawJson);
				Result.ErrorMessage = TEXT("Rigging succeeded but no rigged-mesh URL in response (logged full payload to LogUECPAssetGen)");
				OnPhase.Broadcast(Request.AssetName, TEXT("failed"), -1, Result.ErrorMessage);
				OnComplete(Result);
				return;
			}

			TArray<FString> AnimFbxUrls;
			if (Request.bImportBasicAnimations && ResultObj.IsValid())
			{
				const TSharedPtr<FJsonObject>* AnimsObjPtr = nullptr;
				if (ResultObj->TryGetObjectField(TEXT("basic_animations"), AnimsObjPtr) && AnimsObjPtr && AnimsObjPtr->IsValid())
				{
					static const TCHAR* AnimKeys[] = {
						TEXT("walking_fbx_url"),
						TEXT("running_fbx_url"),
					};
					for (const TCHAR* Key : AnimKeys)
					{
						FString Url;
						if ((*AnimsObjPtr)->TryGetStringField(FString(Key), Url) && !Url.IsEmpty())
						{
							AnimFbxUrls.Add(Url);
						}
					}
					UE_LOG(LogUECPAssetGen, Log, TEXT("Meshy rig: found %d bundled animations to import"), AnimFbxUrls.Num());
				}
			}

			OnPhase.Broadcast(Request.AssetName, TEXT("importing"), -1, FString());
			DownloadAndImportRig(FbxUrl, AnimFbxUrls, Request, MoveTemp(Result), MoveTemp(OnComplete));
		});
}

void FMeshyRiggingProvider::DownloadAndImportRig(
	const FString& FbxUrl,
	const TArray<FString>& AnimFbxUrls,
	const FMeshyRiggingRequest& Request,
	FMeshyRiggingResult Result,
	TFunction<void(const FMeshyRiggingResult&)> OnComplete)
{
	const FString AssetName = Request.AssetName.IsEmpty() ? TEXT("RiggedCharacter") : Request.AssetName;
	const FString RigDest = FPaths::ProjectSavedDir() / TEXT("GeneratedContent") /
		FString::Printf(TEXT("Rig_%s_%s.fbx"), *AssetName, *FGuid::NewGuid().ToString().Left(8));

	FMeshyHttpClient::Get().Download(FbxUrl, RigDest,
		[this, AssetName, AnimFbxUrls, Request, Result = MoveTemp(Result), OnComplete = MoveTemp(OnComplete)]
		(bool bOk, const FString& LocalPath) mutable
		{
			if (!bOk || LocalPath.IsEmpty())
			{
				Result.bSuccess = false;
				Result.ErrorMessage = TEXT("Failed to download rigged FBX");
				OnPhase.Broadcast(Request.AssetName, TEXT("failed"), -1, Result.ErrorMessage);
				OnComplete(Result);
				return;
			}

			const FMeshySkeletalImportResult RigImport = FMeshyAssetImporter::ImportSkeletalMeshFromFile(LocalPath, AssetName, Request.PackagePath);
			Result.bSuccess         = RigImport.bSuccess;
			Result.SkeletalMeshPath = RigImport.SkeletalMeshPath;
			Result.SkeletonPath     = RigImport.SkeletonPath;
			Result.PhysicsAssetPath = RigImport.PhysicsAssetPath;

			if (!RigImport.bSuccess)
			{
				Result.ErrorMessage = RigImport.ErrorMessage;
				OnPhase.Broadcast(Request.AssetName, TEXT("failed"), -1, Result.ErrorMessage);
				OnComplete(Result);
				return;
			}

			if (AnimFbxUrls.Num() == 0 || RigImport.SkeletonPath.IsEmpty())
			{
				OnPhase.Broadcast(Request.AssetName, TEXT("ready"), 100, FString());
				OnComplete(Result);
				return;
			}

			auto Walker = MakeShared<TFunction<void(int32)>>();
			*Walker = [this, AssetName, AnimFbxUrls, Request, Result, Walker, OnComplete, SkeletonPath = RigImport.SkeletonPath](int32 Index) mutable
			{
				if (Index >= AnimFbxUrls.Num())
				{
					OnPhase.Broadcast(Request.AssetName, TEXT("ready"), 100, FString());
					OnComplete(Result);
					return;
				}

				const FString AnimAssetName = FString::Printf(TEXT("%s_Anim_%d"), *AssetName, Index);
				const FString AnimDest      = FPaths::ProjectSavedDir() / TEXT("GeneratedContent") /
					FString::Printf(TEXT("RigAnim_%s_%s.fbx"), *AnimAssetName, *FGuid::NewGuid().ToString().Left(8));

				FMeshyHttpClient::Get().Download(AnimFbxUrls[Index], AnimDest,
					[this, AssetName, AnimAssetName, AnimFbxUrls, Request, Result, Walker, OnComplete, SkeletonPath, Index]
					(bool bOk2, const FString& AnimLocal) mutable
					{
						if (bOk2 && !AnimLocal.IsEmpty())
						{
							const FMeshyAnimationImportResult AnimImport = FMeshyAssetImporter::ImportAnimationFromFile(
								AnimLocal, AnimAssetName, Request.PackagePath, SkeletonPath);
							if (AnimImport.bSuccess)
							{
								Result.BasicAnimationPaths.Add(AnimImport.AnimSequencePath);
							}
							else
							{
								UE_LOG(LogUECPAssetGen, Warning, TEXT("Meshy rigging: basic animation %d failed to import: %s"), Index, *AnimImport.ErrorMessage);
							}
						}
						(*Walker)(Index + 1);
					});
			};
			(*Walker)(0);
		});
}
