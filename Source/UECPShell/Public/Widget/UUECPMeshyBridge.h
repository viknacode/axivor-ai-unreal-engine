// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "Widget/UUECPBridgeBase.h"
#include "UUECPMeshyBridge.generated.h"

class SUECPMainWidget;
class SWebBrowser;

UCLASS()
class UUECPMeshyBridge : public UUECPBridgeBase
{
	GENERATED_BODY()

public:
	UFUNCTION() void MeshyCreateText     (const FString& Json);
	UFUNCTION() void MeshyCreateImage    (const FString& Json);
	UFUNCTION() void MeshyRemesh         (const FString& Json);
	UFUNCTION() void MeshyRetexture      (const FString& Json);
	UFUNCTION() void MeshyRig            (const FString& Json);
	UFUNCTION() void MeshyAnimate        (const FString& Json);
	UFUNCTION() void MeshyTextToImage    (const FString& Json);
	UFUNCTION() void MeshyGetBalance     ();
	UFUNCTION() void MeshyListAnimations ();
	UFUNCTION() void MeshyCancelJob      (const FString& JobId);
	UFUNCTION() void MeshyOpenAsset      (const FString& AssetPath);
	UFUNCTION() void MeshyBrowseAsset      (const FString& FieldId, const FString& ClassFilter);
	UFUNCTION() void MeshyUseSelectedAsset (const FString& FieldId, const FString& ClassFilter);
	UFUNCTION() void MeshySaveHistory(const FString& JsonArray);
	UFUNCTION() void MeshyLoadHistory();

	virtual void BeginDestroy() override;

private:
	void OpenAssetLink(const FString& AssetRef);

	void PushMeshyJobAdded   (const FString& InfoJson);
	void PushMeshyJobProgress(const FString& TaskId, int32 Progress, const FString& Status);
	void PushMeshyJobFinished(const FString& TaskId, const FString& Status, const FString& BodyJson);
	void PushMeshyBalance    (int32 Credits);
	void PushMeshyAnimCatalog(const FString& CatalogJson);
	void PushMeshyAssetPicked(const FString& FieldId, const FString& AssetPath);
	void PushMeshyHistory    (const FString& JsonArray);

	void EnsureMeshyTrackerWiring();
	bool             bMeshyTrackerWired = false;
	FDelegateHandle  MeshyTaskAddedHandle;
	FDelegateHandle  MeshyTaskProgressHandle;
	FDelegateHandle  MeshyTaskFinishedHandle;
};
