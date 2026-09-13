// Copyright 2026, BlueprintsLab, All rights reserved

#include "Viewports/SBodyZoneSelector.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"

FBox2f SBodyZoneSelector::GetZoneRect(EBodyZone Zone)
{
	const float CX = 0.5f;
	const float BW = 0.13f;
	const float AG = 0.02f;
	const float AW = 0.14f;
	const float LG = 0.015f;

	switch (Zone)
	{
	case EBodyZone::Head:       return FBox2f(FVector2f(CX - 0.07f, 0.02f),  FVector2f(CX + 0.07f, 0.12f));

	case EBodyZone::Torso:      return FBox2f(FVector2f(CX - BW, 0.14f),     FVector2f(CX + BW, 0.30f));
	case EBodyZone::Abdomen:    return FBox2f(FVector2f(CX - BW, 0.30f),     FVector2f(CX + BW, 0.40f));

	case EBodyZone::UpperArmL:  return FBox2f(FVector2f(CX - BW - AG - AW, 0.14f), FVector2f(CX - BW - AG, 0.27f));
	case EBodyZone::UpperArmR:  return FBox2f(FVector2f(CX + BW + AG, 0.14f),      FVector2f(CX + BW + AG + AW, 0.27f));
	case EBodyZone::ForearmL:   return FBox2f(FVector2f(CX - BW - AG - AW, 0.27f), FVector2f(CX - BW - AG, 0.40f));
	case EBodyZone::ForearmR:   return FBox2f(FVector2f(CX + BW + AG, 0.27f),      FVector2f(CX + BW + AG + AW, 0.40f));
	case EBodyZone::HandL:      return FBox2f(FVector2f(CX - BW - AG - AW, 0.40f), FVector2f(CX - BW - AG, 0.48f));
	case EBodyZone::HandR:      return FBox2f(FVector2f(CX + BW + AG, 0.40f),      FVector2f(CX + BW + AG + AW, 0.48f));

	case EBodyZone::ThighL:     return FBox2f(FVector2f(CX - BW, 0.42f),           FVector2f(CX - LG, 0.60f));
	case EBodyZone::ThighR:     return FBox2f(FVector2f(CX + LG, 0.42f),           FVector2f(CX + BW, 0.60f));
	case EBodyZone::CalfL:      return FBox2f(FVector2f(CX - BW + 0.01f, 0.60f),   FVector2f(CX - LG, 0.78f));
	case EBodyZone::CalfR:      return FBox2f(FVector2f(CX + LG, 0.60f),           FVector2f(CX + BW - 0.01f, 0.78f));
	case EBodyZone::FootL:      return FBox2f(FVector2f(CX - BW, 0.78f),           FVector2f(CX - LG, 0.88f));
	case EBodyZone::FootR:      return FBox2f(FVector2f(CX + LG, 0.78f),           FVector2f(CX + BW, 0.88f));

	default: return FBox2f(FVector2f::ZeroVector, FVector2f::ZeroVector);
	}
}

FString SBodyZoneSelector::GetZoneName(EBodyZone Zone)
{
	switch (Zone)
	{
	case EBodyZone::Head:       return TEXT("Head");
	case EBodyZone::Torso:      return TEXT("Torso");
	case EBodyZone::Abdomen:    return TEXT("Abdomen");
	case EBodyZone::UpperArmL:  return TEXT("Arm L");
	case EBodyZone::UpperArmR:  return TEXT("Arm R");
	case EBodyZone::ForearmL:   return TEXT("Fore L");
	case EBodyZone::ForearmR:   return TEXT("Fore R");
	case EBodyZone::HandL:      return TEXT("Hand L");
	case EBodyZone::HandR:      return TEXT("Hand R");
	case EBodyZone::ThighL:     return TEXT("Thigh L");
	case EBodyZone::ThighR:     return TEXT("Thigh R");
	case EBodyZone::CalfL:      return TEXT("Calf L");
	case EBodyZone::CalfR:      return TEXT("Calf R");
	case EBodyZone::FootL:      return TEXT("Foot L");
	case EBodyZone::FootR:      return TEXT("Foot R");
	default: return TEXT("Unknown");
	}
}

TArray<FString> SBodyZoneSelector::GetBonePatterns(EBodyZone Zone)
{
	switch (Zone)
	{
	case EBodyZone::Head:       return { TEXT("head") };
	case EBodyZone::Torso:      return { TEXT("spine_02"), TEXT("spine_03"), TEXT("spine2"), TEXT("chest"), TEXT("spine_01"), TEXT("spine1"), TEXT("spine") };
	case EBodyZone::Abdomen:    return { TEXT("pelvis"), TEXT("hips"), TEXT("spine_01"), TEXT("spine1") };
	case EBodyZone::UpperArmL:  return { TEXT("upperarm_l"), TEXT("upper_arm_l"), TEXT("leftshoulder"), TEXT("leftupperarm"), TEXT("leftarm") };
	case EBodyZone::UpperArmR:  return { TEXT("upperarm_r"), TEXT("upper_arm_r"), TEXT("rightshoulder"), TEXT("rightupperarm"), TEXT("rightarm") };
	case EBodyZone::ForearmL:   return { TEXT("lowerarm_l"), TEXT("lower_arm_l"), TEXT("leftforearm"), TEXT("leftlowerarm") };
	case EBodyZone::ForearmR:   return { TEXT("lowerarm_r"), TEXT("lower_arm_r"), TEXT("rightforearm"), TEXT("rightlowerarm") };
	case EBodyZone::HandL:      return { TEXT("hand_l"), TEXT("lefthand") };
	case EBodyZone::HandR:      return { TEXT("hand_r"), TEXT("righthand") };
	case EBodyZone::ThighL:     return { TEXT("thigh_l"), TEXT("upperleg_l"), TEXT("leftupleg"), TEXT("leftthigh") };
	case EBodyZone::ThighR:     return { TEXT("thigh_r"), TEXT("upperleg_r"), TEXT("rightupleg"), TEXT("rightthigh") };
	case EBodyZone::CalfL:      return { TEXT("calf_l"), TEXT("lowerleg_l"), TEXT("leftleg"), TEXT("leftcalf") };
	case EBodyZone::CalfR:      return { TEXT("calf_r"), TEXT("lowerleg_r"), TEXT("rightleg"), TEXT("rightcalf") };
	case EBodyZone::FootL:      return { TEXT("foot_l"), TEXT("leftfoot") };
	case EBodyZone::FootR:      return { TEXT("foot_r"), TEXT("rightfoot") };
	default: return {};
	}
}

void SBodyZoneSelector::Construct(const FArguments& InArgs)
{
	OnZoneSelectionChanged = InArgs._OnZoneSelectionChanged;
	SetCursor(EMouseCursor::Hand);
}

FVector2D SBodyZoneSelector::ComputeDesiredSize(float) const
{
	return FVector2D(300.0, 500.0);
}

EBodyZone SBodyZoneSelector::HitTestZone(FVector2f NormalizedPos)
{
	for (int32 i = 0; i < (int32)EBodyZone::COUNT; i++)
	{
		const EBodyZone Zone = (EBodyZone)i;
		const FBox2f Rect = GetZoneRect(Zone);
		if (NormalizedPos.X >= Rect.Min.X && NormalizedPos.X <= Rect.Max.X &&
			NormalizedPos.Y >= Rect.Min.Y && NormalizedPos.Y <= Rect.Max.Y)
		{
			return Zone;
		}
	}
	return EBodyZone::COUNT;
}

FReply SBodyZoneSelector::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		const FVector2D LocalPos = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
		const FVector2D Size = MyGeometry.GetLocalSize();
		if (Size.X <= 0 || Size.Y <= 0) return FReply::Unhandled();

		const FVector2f Normalized((float)(LocalPos.X / Size.X), (float)(LocalPos.Y / Size.Y));
		const EBodyZone Zone = HitTestZone(Normalized);

		if (Zone != EBodyZone::COUNT)
		{
			ToggleZone(Zone);
			return FReply::Handled();
		}
	}
	return FReply::Unhandled();
}

FReply SBodyZoneSelector::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	const FVector2D LocalPos = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
	const FVector2D Size = MyGeometry.GetLocalSize();
	if (Size.X <= 0 || Size.Y <= 0) return FReply::Unhandled();

	const FVector2f Normalized((float)(LocalPos.X / Size.X), (float)(LocalPos.Y / Size.Y));
	const EBodyZone NewHover = HitTestZone(Normalized);

	if (NewHover != HoveredZone)
	{
		HoveredZone = NewHover;
		Invalidate(EInvalidateWidgetReason::Paint);
	}
	return FReply::Unhandled();
}

void SBodyZoneSelector::ToggleZone(EBodyZone Zone)
{
	const bool bNowSelected = !SelectedZones.Contains(Zone);
	if (bNowSelected)
		SelectedZones.Add(Zone);
	else
		SelectedZones.Remove(Zone);

	OnZoneSelectionChanged.ExecuteIfBound(Zone, bNowSelected);
	Invalidate(EInvalidateWidgetReason::Paint);
}

void SBodyZoneSelector::SetSelectedZones(const TSet<EBodyZone>& InSelected)
{
	SelectedZones = InSelected;
	Invalidate(EInvalidateWidgetReason::Paint);
}

int32 SBodyZoneSelector::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
	int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const FVector2D Size = AllottedGeometry.GetLocalSize();
	const FSlateBrush* WhiteBrush = FCoreStyle::Get().GetBrush("WhiteBrush");

	FSlateDrawElement::MakeBox(OutDrawElements, LayerId,
		AllottedGeometry.ToPaintGeometry(),
		WhiteBrush, ESlateDrawEffect::None, FLinearColor(0.03f, 0.03f, 0.06f));
	LayerId++;

	for (int32 i = 0; i < (int32)EBodyZone::COUNT; i++)
	{
		const EBodyZone Zone = (EBodyZone)i;
		const FBox2f Rect = GetZoneRect(Zone);
		const bool bSelected = SelectedZones.Contains(Zone);
		const bool bHovered = (Zone == HoveredZone);

		const FVector2D TopLeft(Rect.Min.X * Size.X, Rect.Min.Y * Size.Y);
		const FVector2D RectSize((Rect.Max.X - Rect.Min.X) * Size.X, (Rect.Max.Y - Rect.Min.Y) * Size.Y);

		FLinearColor FillColor;
		if (bSelected)
			FillColor = FLinearColor(0.15f, 0.7f, 0.3f, 0.85f);
		else if (bHovered)
			FillColor = FLinearColor(0.3f, 0.3f, 0.5f, 0.7f);
		else
			FillColor = FLinearColor(0.12f, 0.12f, 0.2f, 0.8f);

		FSlateDrawElement::MakeBox(OutDrawElements, LayerId,
			AllottedGeometry.ToPaintGeometry(RectSize, FSlateLayoutTransform(TopLeft)),
			WhiteBrush, ESlateDrawEffect::None, FillColor);

		const FLinearColor BorderColor = bSelected
			? FLinearColor(0.2f, 1.0f, 0.4f, 0.9f)
			: (bHovered ? FLinearColor(0.5f, 0.5f, 0.7f) : FLinearColor(0.2f, 0.2f, 0.3f));

		FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 1,
			AllottedGeometry.ToPaintGeometry(FVector2D(RectSize.X, 1), FSlateLayoutTransform(TopLeft)),
			WhiteBrush, ESlateDrawEffect::None, BorderColor);
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 1,
			AllottedGeometry.ToPaintGeometry(FVector2D(RectSize.X, 1), FSlateLayoutTransform(FVector2D(TopLeft.X, TopLeft.Y + RectSize.Y - 1))),
			WhiteBrush, ESlateDrawEffect::None, BorderColor);
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 1,
			AllottedGeometry.ToPaintGeometry(FVector2D(1, RectSize.Y), FSlateLayoutTransform(TopLeft)),
			WhiteBrush, ESlateDrawEffect::None, BorderColor);
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 1,
			AllottedGeometry.ToPaintGeometry(FVector2D(1, RectSize.Y), FSlateLayoutTransform(FVector2D(TopLeft.X + RectSize.X - 1, TopLeft.Y))),
			WhiteBrush, ESlateDrawEffect::None, BorderColor);

		const FString Label = GetZoneName(Zone);
		const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Regular", 7);
		const FLinearColor TextColor = bSelected ? FLinearColor::White : FLinearColor(0.6f, 0.6f, 0.7f);
		const FVector2D TextPos(TopLeft.X + 2, TopLeft.Y + RectSize.Y * 0.5 - 5);

		FSlateDrawElement::MakeText(OutDrawElements, LayerId + 2,
			AllottedGeometry.ToPaintGeometry(RectSize, FSlateLayoutTransform(TextPos)),
			Label, Font, ESlateDrawEffect::None, TextColor);
	}

	const FBox2f HeadRect = GetZoneRect(EBodyZone::Head);
	const FBox2f TorsoRect = GetZoneRect(EBodyZone::Torso);
	const FVector2D NeckTop(0.5 * Size.X, HeadRect.Max.Y * Size.Y);
	const FVector2D NeckBot(0.5 * Size.X, TorsoRect.Min.Y * Size.Y);
	TArray<FVector2D> NeckLine = { NeckTop, NeckBot };
	FSlateDrawElement::MakeLines(OutDrawElements, LayerId,
		AllottedGeometry.ToPaintGeometry(), NeckLine, ESlateDrawEffect::None, FLinearColor(0.3f, 0.3f, 0.4f), true, 2.0f);

	return LayerId + 3;
}
