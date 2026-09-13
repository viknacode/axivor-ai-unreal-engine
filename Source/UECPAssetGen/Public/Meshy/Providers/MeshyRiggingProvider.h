// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"

struct FMeshyRiggingRequest
{
	FString AssetName;
	FString PackagePath;
	FString InputTaskId;
	FString ModelUrl;
	FString MeshAssetPath;
	float   HeightMeters = 1.7f;
	bool    bImportBasicAnimations = true;
};

struct FMeshyRiggingResult
{
	bool    bSuccess = false;
	FString ErrorMessage;
	FString TaskId;
	FString SkeletalMeshPath;
	FString SkeletonPath;
	FString PhysicsAssetPath;
	TArray<FString> BasicAnimationPaths;
};

DECLARE_MULTICAST_DELEGATE_FourParams(FOnMeshyRiggingPhase,
	const FString& , const FString& , int32 , const FString& );

class UECPASSETGEN_API FMeshyRiggingProvider
{
public:
	static FMeshyRiggingProvider& Get();

	void Submit(
		const FMeshyRiggingRequest& Request,
		const FString& ApiKey,
		TFunction<void(const FMeshyRiggingResult&)> OnComplete);

	FOnMeshyRiggingPhase OnPhase;

private:
	FMeshyRiggingProvider() = default;
	void Track(const FString& TaskId, const FMeshyRiggingRequest& Request, const FString& ApiKey, TFunction<void(const FMeshyRiggingResult&)> OnComplete);
	void DownloadAndImportRig(const FString& FbxUrl, const TArray<FString>& AnimFbxUrls, const FMeshyRiggingRequest& Request, FMeshyRiggingResult Result, TFunction<void(const FMeshyRiggingResult&)> OnComplete);
};
