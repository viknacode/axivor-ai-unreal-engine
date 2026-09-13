// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "Services/UECPAssetGenTypes.h"

DECLARE_MULTICAST_DELEGATE_FourParams(FOnMeshyRetexturePhase,
	const FString& ,
	const FString& ,
	int32          ,
	const FString& );

class UECPASSETGEN_API FMeshyRetextureProvider
{
public:
	static FMeshyRetextureProvider& Get();

	void SubmitRetexture(
		const FRetextureRequest& Request,
		const FString& ApiKey,
		TFunction<void(const FRetextureResult&)> OnComplete);

	FOnMeshyRetexturePhase OnPhase;

private:
	FMeshyRetextureProvider() = default;

	bool   ExportSourceMeshToFBX(class UStaticMesh* Mesh, const FString& OutPath, FString& OutError);
	void   SubmitWithFbxPayload(const FRetextureRequest& Request, const FString& ApiKey, const FString& FbxDataUri, TFunction<void(const FRetextureResult&)> OnComplete);
	void   TrackAndImport       (const FString& TaskId, const FRetextureRequest& Request, const FString& ApiKey, TFunction<void(const FRetextureResult&)> OnComplete);
	void   ImportGlb            (const FString& LocalPath, const FRetextureRequest& Request, FRetextureResult Result, TFunction<void(const FRetextureResult&)> OnComplete);
	static void ClassifyTextureByName(const FString& TexName, const FString& TexPath, FRetextureResult& Out);
};
