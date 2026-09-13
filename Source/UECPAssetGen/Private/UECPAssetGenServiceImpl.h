// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "Services/IUECPAssetGenService.h"

class FUECPAssetGenServiceImpl final : public IUECPAssetGenService
{
public:
	virtual void GenerateTexture(const FTextureGenRequest& Request, const FString& ApiKey,
		TFunction<void(const FTextureGenResult&)> Callback) override;

	virtual void CreateMeshFromText(const FMeshCreationRequest& Request, const FString& ApiKey,
		TFunction<void(const FMeshCreationResult&)> Callback) override;

	virtual void CreateMeshFromImage(const FMeshCreationRequest& Request, const FString& ApiKey,
		TFunction<void(const FMeshCreationResult&)> Callback) override;

	virtual void RefineMeshPreview(const FMeshCreationRequest& Request, const FString& ApiKey,
		TFunction<void(const FMeshCreationResult&)> Callback) override;

	virtual void GenerateSound(const FSoundGenRequest& Request, const FString& ApiKey,
		const FString& Endpoint, TFunction<void(const FSoundGenResult&)> Callback) override;
};
