// Copyright 2026, BlueprintsLab, All rights reserved

#include "Meshy/Providers/MeshyTextToImageProvider.h"
#include "Meshy/MeshyHttpClient.h"
#include "Meshy/MeshyTaskTracker.h"
#include "Meshy/MeshyAssetImporter.h"
#include "UECPAssetGenModule.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"

FMeshyTextToImageProvider& FMeshyTextToImageProvider::Get()
{
	static FMeshyTextToImageProvider Instance;
	return Instance;
}

void FMeshyTextToImageProvider::Submit(
	const FMeshyTextToImageRequest& Request,
	const FString& ApiKey,
	TFunction<void(const FMeshyTextToImageResult&)> OnComplete)
{
	if (ApiKey.IsEmpty())
	{
		FMeshyTextToImageResult Result; Result.ErrorMessage = TEXT("Meshy API key is empty");
		OnPhase.Broadcast(Request.AssetName, TEXT("failed"), -1, Result.ErrorMessage);
		OnComplete(Result);
		return;
	}
	if (Request.Prompt.IsEmpty())
	{
		FMeshyTextToImageResult Result; Result.ErrorMessage = TEXT("Prompt is required for text-to-image");
		OnPhase.Broadcast(Request.AssetName, TEXT("failed"), -1, Result.ErrorMessage);
		OnComplete(Result);
		return;
	}

	const FString ResolvedModel = Request.AiModel.IsEmpty() ? TEXT("nano-banana-2") : Request.AiModel;

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("prompt"),   Request.Prompt);
	Payload->SetStringField(TEXT("ai_model"), ResolvedModel);
	if (!Request.AspectRatio.IsEmpty())    Payload->SetStringField(TEXT("aspect_ratio"),    Request.AspectRatio);
	if (!Request.NegativePrompt.IsEmpty()) Payload->SetStringField(TEXT("negative_prompt"), Request.NegativePrompt);

	FMeshyHttpClient::Get().Post(TEXT("https://api.meshy.ai/openapi/v1/text-to-image"), ApiKey, Payload,
		[this, Request, ApiKey, OnComplete = MoveTemp(OnComplete)](const FMeshyHttpResult& HttpResult) mutable
		{
			FMeshyTextToImageResult Result;

			if (!HttpResult.bSuccess || !HttpResult.ResponseJson.IsValid())
			{
				Result.ErrorMessage = HttpResult.Error.IsEmpty()
					? FString::Printf(TEXT("Text-to-image submit failed (HTTP %d)"), HttpResult.HttpCode)
					: HttpResult.Error.ToDisplayString();
				OnPhase.Broadcast(Request.AssetName, TEXT("failed"), -1, Result.ErrorMessage);
				OnComplete(Result);
				return;
			}

			FString TaskId;
			HttpResult.ResponseJson->TryGetStringField(TEXT("result"), TaskId);
			if (TaskId.IsEmpty())
			{
				Result.ErrorMessage = FString::Printf(TEXT("No task id in text-to-image response: %s"), *HttpResult.ResponseBody.Left(200));
				OnPhase.Broadcast(Request.AssetName, TEXT("failed"), -1, Result.ErrorMessage);
				OnComplete(Result);
				return;
			}

			Result.TaskId = TaskId;
			OnPhase.Broadcast(Request.AssetName, TEXT("generating"), 0, FString());
			Track(TaskId, Request, ApiKey, MoveTemp(OnComplete));
		});
}

void FMeshyTextToImageProvider::Track(
	const FString& TaskId,
	const FMeshyTextToImageRequest& Request,
	const FString& ApiKey,
	TFunction<void(const FMeshyTextToImageResult&)> OnComplete)
{
	FMeshyTaskInfo Info;
	Info.TaskId    = TaskId;
	Info.ApiKey    = ApiKey;
	Info.PollUrl   = FString::Printf(TEXT("https://api.meshy.ai/openapi/v1/text-to-image/%s"), *TaskId);
	Info.Kind      = EMeshyTaskKind::TextToImage;
	Info.Label     = Request.AssetName.IsEmpty() ? TEXT("Meshy text→image") : FString::Printf(TEXT("Image %s"), *Request.AssetName);
	Info.CreatedAt = FDateTime::UtcNow();

	FMeshyTaskTracker::Get().AddTask(Info,
		[this, Request, OnComplete = MoveTemp(OnComplete)](const FMeshyTaskResult& Terminal) mutable
		{
			FMeshyTextToImageResult Result;
			Result.TaskId = Terminal.TaskId;

			if (!Terminal.bSuccess || !Terminal.ResponseJson.IsValid())
			{
				Result.ErrorMessage = Terminal.ErrorMessage.IsEmpty()
					? FString::Printf(TEXT("Text-to-image %s"), *Terminal.Status)
					: Terminal.ErrorMessage;
				OnPhase.Broadcast(Request.AssetName, TEXT("failed"), -1, Result.ErrorMessage);
				OnComplete(Result);
				return;
			}

			FString ImageUrl;
			Terminal.ResponseJson->TryGetStringField(TEXT("image_url"), ImageUrl);
			if (ImageUrl.IsEmpty()) Terminal.ResponseJson->TryGetStringField(TEXT("url"), ImageUrl);

			if (ImageUrl.IsEmpty())
			{
				const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
				if (Terminal.ResponseJson->TryGetArrayField(TEXT("image_urls"), Arr) && Arr && Arr->Num() > 0)
				{
					ImageUrl = (*Arr)[0]->AsString();
				}
			}

			if (ImageUrl.IsEmpty())
			{
				Result.ErrorMessage = TEXT("Text-to-image succeeded but no image URL in response");
				OnPhase.Broadcast(Request.AssetName, TEXT("failed"), -1, Result.ErrorMessage);
				OnComplete(Result);
				return;
			}

			OnPhase.Broadcast(Request.AssetName, TEXT("importing"), -1, FString());

			const FString AssetName = Request.AssetName.IsEmpty() ? TEXT("GeneratedImage") : Request.AssetName;
			const FString DestPath  = FPaths::ProjectSavedDir() / TEXT("GeneratedContent") /
				FString::Printf(TEXT("Image_%s_%s.png"), *AssetName, *FGuid::NewGuid().ToString().Left(8));

			FMeshyHttpClient::Get().Download(ImageUrl, DestPath,
				[this, AssetName, Request, Result = MoveTemp(Result), OnComplete = MoveTemp(OnComplete)]
				(bool bOk, const FString& LocalPath) mutable
				{
					if (!bOk || LocalPath.IsEmpty())
					{
						Result.bSuccess = false;
						Result.ErrorMessage = TEXT("Failed to download generated image");
						OnPhase.Broadcast(Request.AssetName, TEXT("failed"), -1, Result.ErrorMessage);
						OnComplete(Result);
						return;
					}

					const FMeshyTextureImportResult Imported = FMeshyAssetImporter::ImportTextureFromFile(
						LocalPath, AssetName, Request.PackagePath,  true, TC_Default);
					Result.bSuccess         = Imported.bSuccess;
					Result.TextureAssetPath = Imported.AssetPath;
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
		});
}
