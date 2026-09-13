// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

enum class EBodyZone : uint8
{
	Head,
	Torso,
	Abdomen,
	UpperArmL,
	UpperArmR,
	ForearmL,
	ForearmR,
	HandL,
	HandR,
	ThighL,
	ThighR,
	CalfL,
	CalfR,
	FootL,
	FootR,
	COUNT
};

DECLARE_DELEGATE_TwoParams(FOnZoneSelectionChanged, EBodyZone , bool );

class SBodyZoneSelector : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SBodyZoneSelector) {}
		SLATE_EVENT(FOnZoneSelectionChanged, OnZoneSelectionChanged)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	void SetSelectedZones(const TSet<EBodyZone>& InSelected);

	const TSet<EBodyZone>& GetSelectedZones() const { return SelectedZones; }

	void ToggleZone(EBodyZone Zone);

	static FString GetZoneName(EBodyZone Zone);

	static TArray<FString> GetBonePatterns(EBodyZone Zone);

	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;

private:

	static FBox2f GetZoneRect(EBodyZone Zone);

	static EBodyZone HitTestZone(FVector2f NormalizedPos);

	TSet<EBodyZone> SelectedZones;
	EBodyZone HoveredZone = EBodyZone::COUNT;
	FOnZoneSelectionChanged OnZoneSelectionChanged;
};
