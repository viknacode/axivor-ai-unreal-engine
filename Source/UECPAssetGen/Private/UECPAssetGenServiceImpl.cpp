// Copyright 2026, BlueprintsLab, All rights reserved

#include "UECPAssetGenServiceImpl.h"
#include "TextureGenManager.h"
#include "MeshAssetManager.h"
#include "SoundGenManager.h"

void FUECPAssetGenServiceImpl::GenerateTexture(const FTextureGenRequest& Request, const FString& ApiKey,
	TFunction<void(const FTextureGenResult&)> Callback)
{
	FTextureGenManager::Get().GenerateTexture(Request, ApiKey, MoveTemp(Callback));
}

void FUECPAssetGenServiceImpl::CreateMeshFromText(const FMeshCreationRequest& Request, const FString& ApiKey,
	TFunction<void(const FMeshCreationResult&)> Callback)
{
	FMeshAssetManager::Get().CreateMeshFromText(Request, ApiKey, MoveTemp(Callback));
}

void FUECPAssetGenServiceImpl::CreateMeshFromImage(const FMeshCreationRequest& Request, const FString& ApiKey,
	TFunction<void(const FMeshCreationResult&)> Callback)
{
	FMeshAssetManager::Get().CreateMeshFromImage(Request, ApiKey, MoveTemp(Callback));
}

void FUECPAssetGenServiceImpl::RefineMeshPreview(const FMeshCreationRequest& Request, const FString& ApiKey,
	TFunction<void(const FMeshCreationResult&)> Callback)
{
	FMeshAssetManager::Get().RefineMeshPreview(Request, ApiKey, MoveTemp(Callback));
}

void FUECPAssetGenServiceImpl::GenerateSound(const FSoundGenRequest& Request, const FString& ApiKey,
	const FString& Endpoint, TFunction<void(const FSoundGenResult&)> Callback)
{
	FSoundGenManager::Get().GenerateSound(Request, ApiKey, Endpoint, MoveTemp(Callback));
}
