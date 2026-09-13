// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "Services/UECPAssetGenTypes.h"

class UECPCORE_API IUECPAssetGenService
{
public:
	virtual ~IUECPAssetGenService() = default;

	virtual void GenerateTexture(const FTextureGenRequest& Request,
		const FString& ApiKey,
		TFunction<void(const FTextureGenResult&)> Callback) = 0;

	virtual void CreateMeshFromText(const FMeshCreationRequest& Request,
		const FString& ApiKey,
		TFunction<void(const FMeshCreationResult&)> Callback) = 0;

	virtual void CreateMeshFromImage(const FMeshCreationRequest& Request,
		const FString& ApiKey,
		TFunction<void(const FMeshCreationResult&)> Callback) = 0;

	virtual void RefineMeshPreview(const FMeshCreationRequest& Request,
		const FString& ApiKey,
		TFunction<void(const FMeshCreationResult&)> Callback) = 0;

	virtual void GenerateSound(const FSoundGenRequest& Request,
		const FString& ApiKey,
		const FString& Endpoint,
		TFunction<void(const FSoundGenResult&)> Callback) = 0;
};
