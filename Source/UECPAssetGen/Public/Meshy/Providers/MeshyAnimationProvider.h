// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"

struct FMeshyAnimationRequest
{
	FString AssetName;
	FString PackagePath;
	FString RigTaskId;
	int32   ActionId = 0;
	FString SkeletonAssetPath;
	int32   ChangeFps = 0;
	bool    bExtractArmature = false;
};

struct FMeshyAnimationResult
{
	bool    bSuccess = false;
	FString ErrorMessage;
	FString TaskId;
	FString AnimSequencePath;
};

DECLARE_MULTICAST_DELEGATE_FourParams(FOnMeshyAnimationPhase,
	const FString& , const FString& , int32 , const FString& );

class UECPASSETGEN_API FMeshyAnimationProvider
{
public:
	static FMeshyAnimationProvider& Get();

	void Submit(
		const FMeshyAnimationRequest& Request,
		const FString& ApiKey,
		TFunction<void(const FMeshyAnimationResult&)> OnComplete);

	FOnMeshyAnimationPhase OnPhase;

private:
	FMeshyAnimationProvider() = default;
	void Track(const FString& TaskId, const FMeshyAnimationRequest& Request, const FString& ApiKey, TFunction<void(const FMeshyAnimationResult&)> OnComplete);
};
