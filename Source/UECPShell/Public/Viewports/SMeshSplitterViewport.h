// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "SEditorViewport.h"
#include "PreviewScene.h"

class FMeshSplitterViewportClient;
class UDebugSkelMeshComponent;

DECLARE_DELEGATE_OneParam(FOnBoneClicked, const FName& );

class SMeshSplitterViewport : public SEditorViewport
{
public:
	SLATE_BEGIN_ARGS(SMeshSplitterViewport) {}
		SLATE_EVENT(FOnBoneClicked, OnBoneClicked)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	void SetPreviewMesh(USkeletalMesh* InMesh);

	void SetSelectedBones(const TSet<FName>& InSelectedBones);

	UDebugSkelMeshComponent* GetPreviewComponent() const;

protected:
	virtual TSharedRef<FEditorViewportClient> MakeEditorViewportClient() override;

private:
	TSharedPtr<FPreviewScene> PreviewScene;
	TSharedPtr<FMeshSplitterViewportClient> ViewportClient;
	FOnBoneClicked OnBoneClickedDelegate;
	UDebugSkelMeshComponent* PreviewMeshComponent = nullptr;
};
