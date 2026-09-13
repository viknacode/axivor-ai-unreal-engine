// Copyright 2026, BlueprintsLab, All rights reserved

#include "Meshy/Providers/MeshyRetextureProvider.h"
#include "Meshy/MeshyHttpClient.h"
#include "Meshy/MeshyTaskTracker.h"
#include "UECPAssetGenModule.h"
#include "ApiKeyManager.h"
#include "AssetImportTask.h"
#include "AssetToolsModule.h"
#include "EditorAssetLibrary.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Exporters/Exporter.h"
#include "HAL/FileManager.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstance.h"
#include "Misc/Base64.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "UObject/UObjectIterator.h"

FMeshyRetextureProvider& FMeshyRetextureProvider::Get()
{
	static FMeshyRetextureProvider Instance;
	return Instance;
}

bool FMeshyRetextureProvider::ExportSourceMeshToFBX(UStaticMesh* Mesh, const FString& OutPath, FString& OutError)
{
	if (!Mesh)
	{
		OutError = TEXT("Source StaticMesh is null");
		return false;
	}

	UExporter* Exporter = nullptr;
	const FString Ext = OutPath.Right(3).ToLower();
	for (TObjectIterator<UClass> It; It; ++It)
	{
		if (!It->IsChildOf(UExporter::StaticClass()) || It->HasAnyClassFlags(CLASS_Abstract)) continue;

		UExporter* Test = Cast<UExporter>(It->GetDefaultObject());
		if (!Test || Test->SupportedClass != UStaticMesh::StaticClass()) continue;

		for (const FString& Supported : Test->FormatExtension)
		{
			if (Supported.ToLower() == Ext)
			{
				Exporter = NewObject<UExporter>(GetTransientPackage(), *It);
				break;
			}
		}
		if (Exporter) break;
	}

	if (!Exporter)
	{
		OutError = TEXT("No FBX exporter registered for UStaticMesh");
		return false;
	}

	IFileManager::Get().MakeDirectory(*FPaths::GetPath(OutPath),  true);
	return UExporter::ExportToFile(Mesh, Exporter, *OutPath,  false) != 0;
}

void FMeshyRetextureProvider::SubmitRetexture(
	const FRetextureRequest& Request,
	const FString& ApiKey,
	TFunction<void(const FRetextureResult&)> OnComplete)
{
	if (ApiKey.IsEmpty())
	{
		FRetextureResult Result; Result.ErrorMessage = TEXT("Meshy API key is empty");
		OnPhase.Broadcast(Request.MaterialName, TEXT("failed"), -1, Result.ErrorMessage);
		OnComplete(Result);
		return;
	}

	UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *Request.MeshAssetPath);
	if (!Mesh)
	{
		FRetextureResult Result;
		Result.ErrorMessage = FString::Printf(TEXT("Failed to load StaticMesh at %s"), *Request.MeshAssetPath);
		OnPhase.Broadcast(Request.MaterialName, TEXT("failed"), -1, Result.ErrorMessage);
		OnComplete(Result);
		return;
	}

	const FString TempPath = FPaths::ProjectSavedDir() / TEXT("Temp") / FString::Printf(TEXT("retexture_%s.fbx"), *FGuid::NewGuid().ToString().Left(8));

	FString ExportError;
	if (!ExportSourceMeshToFBX(Mesh, TempPath, ExportError))
	{
		FRetextureResult Result;
		Result.ErrorMessage = ExportError.IsEmpty() ? TEXT("FBX export failed") : ExportError;
		OnPhase.Broadcast(Request.MaterialName, TEXT("failed"), -1, Result.ErrorMessage);
		OnComplete(Result);
		return;
	}

	TArray<uint8> FbxBytes;
	if (!FFileHelper::LoadFileToArray(FbxBytes, *TempPath))
	{
		FRetextureResult Result; Result.ErrorMessage = TEXT("Failed to read exported FBX bytes");
		OnPhase.Broadcast(Request.MaterialName, TEXT("failed"), -1, Result.ErrorMessage);
		OnComplete(Result);
		return;
	}

	UE_LOG(LogUECPAssetGen, Log, TEXT("Meshy retexture: exported mesh to FBX (%d bytes) → submitting"), FbxBytes.Num());

	const FString DataUri = TEXT("data:application/octet-stream;base64,") + FBase64::Encode(FbxBytes);
	SubmitWithFbxPayload(Request, ApiKey, DataUri, MoveTemp(OnComplete));
}

void FMeshyRetextureProvider::SubmitWithFbxPayload(
	const FRetextureRequest& Request,
	const FString& ApiKey,
	const FString& FbxDataUri,
	TFunction<void(const FRetextureResult&)> OnComplete)
{
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("model_url"),         FbxDataUri);
	Payload->SetStringField(TEXT("text_style_prompt"), Request.StylePrompt);
	Payload->SetBoolField  (TEXT("enable_original_uv"), true);
	Payload->SetBoolField  (TEXT("enable_pbr"),         Request.bEnablePBR);
	Payload->SetStringField(TEXT("ai_model"),           FApiKeyManager::Get().GetActiveMeshyAiModel());

	FMeshyHttpClient::Get().Post(TEXT("https://api.meshy.ai/openapi/v1/retexture"), ApiKey, Payload,
		[this, Request, ApiKey, OnComplete = MoveTemp(OnComplete)](const FMeshyHttpResult& HttpResult) mutable
		{
			FRetextureResult Result;
			if (!HttpResult.bSuccess || !HttpResult.ResponseJson.IsValid())
			{
				Result.ErrorMessage = HttpResult.Error.IsEmpty()
					? FString::Printf(TEXT("Retexture submit failed (HTTP %d)"), HttpResult.HttpCode)
					: HttpResult.Error.ToDisplayString();
				OnPhase.Broadcast(Request.MaterialName, TEXT("failed"), -1, Result.ErrorMessage);
				OnComplete(Result);
				return;
			}

			FString TaskId;
			HttpResult.ResponseJson->TryGetStringField(TEXT("result"), TaskId);
			if (TaskId.IsEmpty()) HttpResult.ResponseJson->TryGetStringField(TEXT("id"), TaskId);

			if (TaskId.IsEmpty())
			{
				Result.ErrorMessage = FString::Printf(TEXT("No task id in retexture response: %s"), *HttpResult.ResponseBody.Left(200));
				OnPhase.Broadcast(Request.MaterialName, TEXT("failed"), -1, Result.ErrorMessage);
				OnComplete(Result);
				return;
			}

			Result.TaskId = TaskId;
			OnPhase.Broadcast(Request.MaterialName, TEXT("generating"), 0, FString());
			TrackAndImport(TaskId, Request, ApiKey, MoveTemp(OnComplete));
		});
}

void FMeshyRetextureProvider::TrackAndImport(
	const FString& TaskId,
	const FRetextureRequest& Request,
	const FString& ApiKey,
	TFunction<void(const FRetextureResult&)> OnComplete)
{
	FMeshyTaskInfo Info;
	Info.TaskId    = TaskId;
	Info.ApiKey    = ApiKey;
	Info.PollUrl   = FString::Printf(TEXT("https://api.meshy.ai/openapi/v1/retexture/%s"), *TaskId);
	Info.Kind      = EMeshyTaskKind::Retexture;
	Info.Label     = Request.MaterialName.IsEmpty() ? TEXT("Meshy retexture") : Request.MaterialName;
	Info.CreatedAt = FDateTime::UtcNow();

	FMeshyTaskTracker::Get().AddTask(Info,
		[this, Request, OnComplete = MoveTemp(OnComplete)](const FMeshyTaskResult& Terminal) mutable
		{
			FRetextureResult Result;
			Result.TaskId   = Terminal.TaskId;
			Result.Status   = Terminal.Status;
			Result.Progress = Terminal.Progress;

			if (!Terminal.bSuccess || !Terminal.ResponseJson.IsValid())
			{
				Result.ErrorMessage = Terminal.ErrorMessage.IsEmpty()
					? FString::Printf(TEXT("Retexture %s"), *Terminal.Status)
					: Terminal.ErrorMessage;
				OnPhase.Broadcast(Request.MaterialName, TEXT("failed"), -1, Result.ErrorMessage);
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
				Terminal.ResponseJson->TryGetStringField(TEXT("model_url"), GlbUrl);
			}

			if (GlbUrl.IsEmpty())
			{
				Result.ErrorMessage = TEXT("Retexture succeeded but no GLB URL in response");
				OnPhase.Broadcast(Request.MaterialName, TEXT("failed"), -1, Result.ErrorMessage);
				OnComplete(Result);
				return;
			}

			OnPhase.Broadcast(Request.MaterialName, TEXT("importing"), -1, FString());
			const FString MatName = Request.MaterialName.IsEmpty() ? TEXT("RetexturedMesh") : Request.MaterialName;
			const FString DestPath = FPaths::ProjectSavedDir() / TEXT("GeneratedContent") /
				FString::Printf(TEXT("Retexture_%s_%s.glb"), *MatName, *FGuid::NewGuid().ToString().Left(8));

			FMeshyHttpClient::Get().Download(GlbUrl, DestPath,
				[this, Request, Result = MoveTemp(Result), OnComplete = MoveTemp(OnComplete)]
				(bool bOk, const FString& LocalPath) mutable
				{
					if (!bOk || LocalPath.IsEmpty())
					{
						Result.bSuccess     = false;
						Result.ErrorMessage = TEXT("Failed to download retextured GLB");
						OnPhase.Broadcast(Request.MaterialName, TEXT("failed"), -1, Result.ErrorMessage);
						OnComplete(Result);
						return;
					}
					ImportGlb(LocalPath, Request, MoveTemp(Result), MoveTemp(OnComplete));
				});
		});
}

void FMeshyRetextureProvider::ClassifyTextureByName(const FString& TexName, const FString& TexPath, FRetextureResult& Out)
{
	const FString Lower = TexName.ToLower();
	if (Lower.Contains(TEXT("base")) || Lower.Contains(TEXT("color")) ||
		Lower.Contains(TEXT("diffuse")) || Lower.Contains(TEXT("albedo")))
	{
		Out.BaseColorPath = TexPath;
	}
	else if (Lower.Contains(TEXT("normal")))
	{
		Out.NormalPath = TexPath;
	}
	else if (Lower.Contains(TEXT("metallic")) || Lower.Contains(TEXT("metal")))
	{
		Out.MetallicPath = TexPath;
	}
	else if (Lower.Contains(TEXT("rough")))
	{
		Out.RoughnessPath = TexPath;
	}
	else if (Out.BaseColorPath.IsEmpty())
	{
		Out.BaseColorPath = TexPath;
	}
}

void FMeshyRetextureProvider::ImportGlb(
	const FString& LocalPath,
	const FRetextureRequest& Request,
	FRetextureResult Result,
	TFunction<void(const FRetextureResult&)> OnComplete)
{
	const FString SaveDir = Request.SavePath.IsEmpty() ? TEXT("/Game/GeneratedTextures") : Request.SavePath;
	const FString MatName = Request.MaterialName.IsEmpty() ? TEXT("RetexturedMesh") : Request.MaterialName;
	const FString ImportDir = SaveDir / (TEXT("Retexture_") + MatName);

	if (!UEditorAssetLibrary::DoesDirectoryExist(ImportDir))
	{
		UEditorAssetLibrary::MakeDirectory(ImportDir);
	}

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
	UAssetImportTask* ImportTask = NewObject<UAssetImportTask>();
	ImportTask->Filename         = LocalPath;
	ImportTask->DestinationPath  = ImportDir;
	ImportTask->DestinationName  = FPaths::GetBaseFilename(LocalPath);
	ImportTask->bReplaceExisting = true;
	ImportTask->bAutomated       = true;
	ImportTask->bSave            = true;
	TArray<UAssetImportTask*> ImportTasks; ImportTasks.Add(ImportTask);
	AssetToolsModule.Get().ImportAssetTasks(ImportTasks);
	TArray<UObject*> Imported = ImportTask->GetObjects();

	for (UObject* Obj : Imported)
	{
		if (!Obj) continue;
		if (UTexture2D* Tex = Cast<UTexture2D>(Obj))
		{
			ClassifyTextureByName(Tex->GetName(), Tex->GetPathName(), Result);
		}
		else if (UMaterial* Mat = Cast<UMaterial>(Obj))
		{
			Result.MaterialPath = Mat->GetPathName();
		}
		else if (UMaterialInstance* MatInst = Cast<UMaterialInstance>(Obj))
		{
			if (Result.MaterialPath.IsEmpty())
			{
				Result.MaterialPath = MatInst->GetPathName();
			}
		}
	}

	Result.bSuccess = true;
	Result.Progress = 100;
	UE_LOG(LogUECPAssetGen, Log, TEXT("Meshy retexture: imported. BC=%s N=%s M=%s R=%s Mat=%s"),
		*Result.BaseColorPath, *Result.NormalPath, *Result.MetallicPath, *Result.RoughnessPath, *Result.MaterialPath);
	OnPhase.Broadcast(Request.MaterialName, TEXT("ready"), 100, FString());
	OnComplete(Result);
}
