// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"

struct FMeshyTextToImageRequest
{
	FString Prompt;
	FString AssetName;
	FString PackagePath;
	FString AspectRatio;
	FString NegativePrompt;
	FString AiModel;
};

struct FMeshyTextToImageResult
{
	bool    bSuccess = false;
	FString ErrorMessage;
	FString TaskId;
	FString TextureAssetPath;
};

DECLARE_MULTICAST_DELEGATE_FourParams(FOnMeshyTextToImagePhase,
	const FString& , const FString& , int32 , const FString& );

class UECPASSETGEN_API FMeshyTextToImageProvider
{
public:
	static FMeshyTextToImageProvider& Get();

	void Submit(
		const FMeshyTextToImageRequest& Request,
		const FString& ApiKey,
		TFunction<void(const FMeshyTextToImageResult&)> OnComplete);

	FOnMeshyTextToImagePhase OnPhase;

private:
	FMeshyTextToImageProvider() = default;
	void Track(const FString& TaskId, const FMeshyTextToImageRequest& Request, const FString& ApiKey, TFunction<void(const FMeshyTextToImageResult&)> OnComplete);
};
