// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "Containers/UnrealString.h"
#include "Services/UECPAssetGenTypes.h"

DECLARE_MULTICAST_DELEGATE_FourParams(FOnMeshProgressUpdated,
	const FString& ,
	const FString& ,
	int32          ,
	const FString& );

class UECPASSETGEN_API FMeshAssetManager
{
public:
	static FMeshAssetManager& Get();

	void CreateMeshFromText (const FMeshCreationRequest& Request, const FString& ApiKey, TFunction<void(const FMeshCreationResult&)> OnComplete);
	void CreateMeshFromImage(const FMeshCreationRequest& Request, const FString& ApiKey, TFunction<void(const FMeshCreationResult&)> OnComplete);
	void RefineMeshPreview  (const FMeshCreationRequest& Request, const FString& ApiKey, TFunction<void(const FMeshCreationResult&)> OnComplete);
	void RetextureMesh      (const FRetextureRequest&    Request, const FString& ApiKey, TFunction<void(const FRetextureResult&)>    OnComplete);
	void CancelCreation();

	FOnMeshProgressUpdated OnProgressUpdated;

private:
	FMeshAssetManager();
	~FMeshAssetManager() = default;
};
