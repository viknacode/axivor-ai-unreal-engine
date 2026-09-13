// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "Services/UECPAssetGenTypes.h"

DECLARE_MULTICAST_DELEGATE_FourParams(FOnMeshyGenerationPhase,
	const FString& ,
	const FString& ,
	int32          ,
	const FString& );

class UECPASSETGEN_API FMeshyGenerationProvider
{
public:
	static FMeshyGenerationProvider& Get();

	void SubmitTextTo3D (const FMeshCreationRequest& Request, const FString& ApiKey, TFunction<void(const FMeshCreationResult&)> OnComplete);
	void SubmitImageTo3D(const FMeshCreationRequest& Request, const FString& ApiKey, TFunction<void(const FMeshCreationResult&)> OnComplete);
	void SubmitRefine   (const FMeshCreationRequest& Request, const FString& ApiKey, TFunction<void(const FMeshCreationResult&)> OnComplete);

	FOnMeshyGenerationPhase OnPhase;

private:
	enum class EJobKind : uint8 { TextPreview, ImagePreview, Refine };

	FMeshyGenerationProvider() = default;

	void Submit(EJobKind Kind, const FMeshCreationRequest& Request, const FString& ApiKey, TFunction<void(const FMeshCreationResult&)> OnComplete);
	void TrackTask(EJobKind Kind, const FString& TaskId, const FMeshCreationRequest& Request, const FString& ApiKey, TFunction<void(const FMeshCreationResult&)> OnComplete);
	void HandleTerminal(EJobKind Kind, const struct FMeshyTaskResult& Terminal, const FMeshCreationRequest& Request, const FString& ApiKey, TFunction<void(const FMeshCreationResult&)> OnComplete);
	void DownloadAndImport(const FString& GlbUrl, const FMeshCreationRequest& Request, FMeshCreationResult Result, TFunction<void(const FMeshCreationResult&)> OnComplete);
};
