// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"

struct FMeshyRemeshRequest
{
	FString AssetName;
	FString PackagePath;
	FString InputTaskId;
	FString ModelUrl;
	FString MeshAssetPath;
	FString Topology    = TEXT("quad");
	int32   PolyCount   = 30000;
	float   ResizeHeight = 0.f;
};

struct FMeshyRemeshResult
{
	bool    bSuccess = false;
	FString ErrorMessage;
	FString TaskId;
	FString AssetPath;
};

DECLARE_MULTICAST_DELEGATE_FourParams(FOnMeshyRemeshPhase,
	const FString& , const FString& , int32 , const FString& );

class UECPASSETGEN_API FMeshyRemeshProvider
{
public:
	static FMeshyRemeshProvider& Get();

	void Submit(
		const FMeshyRemeshRequest& Request,
		const FString& ApiKey,
		TFunction<void(const FMeshyRemeshResult&)> OnComplete);

	FOnMeshyRemeshPhase OnPhase;

private:
	FMeshyRemeshProvider() = default;
	void TrackAndImport(const FString& TaskId, const FMeshyRemeshRequest& Request, const FString& ApiKey, TFunction<void(const FMeshyRemeshResult&)> OnComplete);
	void DownloadAndImport(const FString& GlbUrl, const FMeshyRemeshRequest& Request, FMeshyRemeshResult Result, TFunction<void(const FMeshyRemeshResult&)> OnComplete);
};
