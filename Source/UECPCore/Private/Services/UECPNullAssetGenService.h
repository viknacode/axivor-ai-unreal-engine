// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "Services/IUECPAssetGenService.h"

class FUECPNullAssetGenService final : public IUECPAssetGenService
{
public:
	virtual void GenerateTexture(const FTextureGenRequest&, const FString&,
		TFunction<void(const FTextureGenResult&)> Callback) override
	{
		if (Callback) { FTextureGenResult R; R.ErrorMessage = TEXT("AssetGen module not loaded."); Callback(R); }
	}

	virtual void CreateMeshFromText(const FMeshCreationRequest&, const FString&,
		TFunction<void(const FMeshCreationResult&)> Callback) override
	{
		if (Callback) { FMeshCreationResult R; R.ErrorMessage = TEXT("AssetGen module not loaded."); Callback(R); }
	}

	virtual void CreateMeshFromImage(const FMeshCreationRequest&, const FString&,
		TFunction<void(const FMeshCreationResult&)> Callback) override
	{
		if (Callback) { FMeshCreationResult R; R.ErrorMessage = TEXT("AssetGen module not loaded."); Callback(R); }
	}

	virtual void RefineMeshPreview(const FMeshCreationRequest&, const FString&,
		TFunction<void(const FMeshCreationResult&)> Callback) override
	{
		if (Callback) { FMeshCreationResult R; R.ErrorMessage = TEXT("AssetGen module not loaded."); Callback(R); }
	}

	virtual void GenerateSound(const FSoundGenRequest&, const FString&, const FString&,
		TFunction<void(const FSoundGenResult&)> Callback) override
	{
		if (Callback) { FSoundGenResult R; R.ErrorMessage = TEXT("AssetGen module not loaded."); Callback(R); }
	}
};
