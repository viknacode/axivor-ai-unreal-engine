// Copyright 2026, BlueprintsLab, All rights reserved

#include "MeshAssetManager.h"
#include "Meshy/Providers/MeshyGenerationProvider.h"
#include "Meshy/Providers/MeshyRetextureProvider.h"
#include "Meshy/MeshyTaskTracker.h"
#include "UECPAssetGenModule.h"

FMeshAssetManager& FMeshAssetManager::Get()
{
	static FMeshAssetManager Instance;
	return Instance;
}

FMeshAssetManager::FMeshAssetManager()
{
	FMeshyGenerationProvider::Get().OnPhase.AddLambda(
		[this](const FString& AssetName, const FString& Phase, int32 Progress, const FString& Err)
		{
			OnProgressUpdated.Broadcast(AssetName, Phase, Progress, Err);
		});

	FMeshyRetextureProvider::Get().OnPhase.AddLambda(
		[this](const FString& AssetName, const FString& Phase, int32 Progress, const FString& Err)
		{
			OnProgressUpdated.Broadcast(AssetName, Phase, Progress, Err);
		});
}

void FMeshAssetManager::CreateMeshFromText(
	const FMeshCreationRequest& Request,
	const FString& ApiKey,
	TFunction<void(const FMeshCreationResult&)> OnComplete)
{
	FMeshyGenerationProvider::Get().SubmitTextTo3D(Request, ApiKey, MoveTemp(OnComplete));
}

void FMeshAssetManager::CreateMeshFromImage(
	const FMeshCreationRequest& Request,
	const FString& ApiKey,
	TFunction<void(const FMeshCreationResult&)> OnComplete)
{
	FMeshyGenerationProvider::Get().SubmitImageTo3D(Request, ApiKey, MoveTemp(OnComplete));
}

void FMeshAssetManager::RefineMeshPreview(
	const FMeshCreationRequest& Request,
	const FString& ApiKey,
	TFunction<void(const FMeshCreationResult&)> OnComplete)
{
	FMeshyGenerationProvider::Get().SubmitRefine(Request, ApiKey, MoveTemp(OnComplete));
}

void FMeshAssetManager::RetextureMesh(
	const FRetextureRequest& Request,
	const FString& ApiKey,
	TFunction<void(const FRetextureResult&)> OnComplete)
{
	FMeshyRetextureProvider::Get().SubmitRetexture(Request, ApiKey, MoveTemp(OnComplete));
}

void FMeshAssetManager::CancelCreation()
{
	FMeshyTaskTracker::Get().CancelAll();
}
