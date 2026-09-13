// Copyright 2026, BlueprintsLab, All rights reserved

#include "Viewports/SMeshSplitterViewport.h"
#include "EditorViewportClient.h"
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7)
#include "Settings/EditorViewportSettings.h"
#endif
#include "PreviewScene.h"
#include "Animation/DebugSkelMeshComponent.h"
#include "PersonaSelectionProxies.h"
#include "Engine/SkeletalMesh.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"

class FMeshSplitterViewportClient : public FEditorViewportClient
{
public:
	FMeshSplitterViewportClient(FPreviewScene* InPreviewScene, const TSharedRef<SMeshSplitterViewport>& InViewportWidget)
		: FEditorViewportClient(nullptr, InPreviewScene, StaticCastSharedRef<SEditorViewport>(InViewportWidget))
		, PreviewComponent(nullptr)
	{
		SetViewLocation(FVector(0, 150, 100));
		SetViewRotation(FRotator(-15, -90, 0));

		SetRealtime(true);
		DrawHelper.bDrawGrid = true;
		DrawHelper.bDrawPivot = false;
		EngineShowFlags.SetGrid(true);

		bUsingOrbitCamera = false;
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7)
		SetCameraSpeedSettings(FEditorViewportCameraSpeedSettings( 1.0f));
#else
		SetCameraSpeedSetting(3);
#endif
	}

	void SetPreviewComponent(UDebugSkelMeshComponent* InComp) { PreviewComponent = InComp; }
	void SetSelectedBones(const TSet<FName>& InBones) { SelectedBoneNames = InBones; }
	void SetBoneClickedCallback(FOnBoneClicked InCallback) { OnBoneClicked = InCallback; }

	virtual void Draw(const FSceneView* View, FPrimitiveDrawInterface* PDI) override
	{
		FEditorViewportClient::Draw(View, PDI);

		if (!PreviewComponent || !PreviewComponent->GetSkeletalMeshAsset()) return;

		const FReferenceSkeleton& RefSkel = PreviewComponent->GetSkeletalMeshAsset()->GetRefSkeleton();
		const int32 NumBones = RefSkel.GetNum();
		if (NumBones == 0) return;

		TArray<FTransform> BoneTransforms;
		BoneTransforms.SetNum(NumBones);
		for (int32 i = 0; i < NumBones; i++)
		{
			BoneTransforms[i] = PreviewComponent->GetBoneTransform(i);
		}

		for (int32 i = 0; i < NumBones; i++)
		{
			const FName BoneName = RefSkel.GetBoneName(i);
			const FVector BonePos = BoneTransforms[i].GetLocation();
			const bool bSelected = SelectedBoneNames.Contains(BoneName);

			PDI->SetHitProxy(new HPersonaBoneHitProxy(i, BoneName));

			const FLinearColor PointColor = bSelected ? FLinearColor(0.2f, 1.0f, 0.3f) : FLinearColor(0.9f, 0.9f, 0.9f);
			const float PointSize = bSelected ? 6.0f : 3.0f;
			PDI->DrawPoint(BonePos, PointColor, PointSize, SDPG_Foreground);

			const int32 ParentIdx = RefSkel.GetParentIndex(i);
			if (ParentIdx != INDEX_NONE)
			{
				const FVector ParentPos = BoneTransforms[ParentIdx].GetLocation();
				const FLinearColor LineColor = bSelected ? FLinearColor(0.1f, 0.8f, 0.2f) : FLinearColor(0.5f, 0.5f, 0.6f);
				const float LineThickness = bSelected ? 2.0f : 1.0f;
				PDI->DrawLine(ParentPos, BonePos, LineColor, SDPG_Foreground, LineThickness);
			}

			PDI->SetHitProxy(nullptr);
		}
	}

	virtual void ProcessClick(FSceneView& View, HHitProxy* HitProxy, FKey Key, EInputEvent Event, uint32 HitX, uint32 HitY) override
	{
		if (!HitProxy || Event != IE_Pressed) return;

		if (HPersonaBoneHitProxy* BoneProxy = HitProxyCast<HPersonaBoneHitProxy>(HitProxy))
		{
			OnBoneClicked.ExecuteIfBound(BoneProxy->BoneName);
			return;
		}

		FEditorViewportClient::ProcessClick(View, HitProxy, Key, Event, HitX, HitY);
	}

private:
	UDebugSkelMeshComponent* PreviewComponent;
	TSet<FName> SelectedBoneNames;
	FOnBoneClicked OnBoneClicked;
};

void SMeshSplitterViewport::Construct(const FArguments& InArgs)
{
	OnBoneClickedDelegate = InArgs._OnBoneClicked;

	PreviewScene = MakeShareable(new FPreviewScene(
		FPreviewScene::ConstructionValues()
			.SetLightRotation(FRotator(-40.0f, -67.5f, 0.0f))
			.SetSkyBrightness(0.5f)
			.SetCreatePhysicsScene(false)
	));

	UDirectionalLightComponent* DirLight = NewObject<UDirectionalLightComponent>();
	DirLight->Intensity = 2.0f;
	DirLight->LightColor = FColor(255, 255, 240);
	PreviewScene->AddComponent(DirLight, FTransform(FRotator(-40.0f, -67.5f, 0.0f)));

	SEditorViewport::Construct(SEditorViewport::FArguments());
}

TSharedRef<FEditorViewportClient> SMeshSplitterViewport::MakeEditorViewportClient()
{
	ViewportClient = MakeShareable(new FMeshSplitterViewportClient(PreviewScene.Get(), SharedThis(this)));
	ViewportClient->SetBoneClickedCallback(OnBoneClickedDelegate);
	return ViewportClient.ToSharedRef();
}

void SMeshSplitterViewport::SetPreviewMesh(USkeletalMesh* InMesh)
{
	if (!PreviewScene.IsValid()) return;

	if (PreviewMeshComponent)
	{
		PreviewScene->RemoveComponent(PreviewMeshComponent);
		PreviewMeshComponent = nullptr;
	}

	if (!InMesh) return;

	UDebugSkelMeshComponent* NewComp = NewObject<UDebugSkelMeshComponent>();
	NewComp->SetSkeletalMesh(InMesh);
	NewComp->EnablePreview(true, nullptr);
	PreviewScene->AddComponent(NewComp, FTransform::Identity);
	PreviewMeshComponent = NewComp;

	if (ViewportClient.IsValid())
	{
		ViewportClient->SetPreviewComponent(NewComp);

		const FBoxSphereBounds Bounds = InMesh->GetBounds();
		ViewportClient->SetViewLocation(Bounds.Origin + FVector(0, Bounds.SphereRadius * 2.0f, Bounds.SphereRadius * 0.5f));
		ViewportClient->SetLookAtLocation(Bounds.Origin);
	}

	if (ViewportClient.IsValid())
		ViewportClient->Invalidate();
}

void SMeshSplitterViewport::SetSelectedBones(const TSet<FName>& InSelectedBones)
{
	if (ViewportClient.IsValid())
	{
		ViewportClient->SetSelectedBones(InSelectedBones);
		ViewportClient->Invalidate();
	}
}

UDebugSkelMeshComponent* SMeshSplitterViewport::GetPreviewComponent() const
{
	if (!PreviewScene.IsValid()) return nullptr;
	return PreviewMeshComponent;
}
