// Copyright 2026, BlueprintsLab, All rights reserved

#include "Tools/WidgetLayoutTools.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/PanelSlot.h"
#include "Components/PanelWidget.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/Widget.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EditorAssetLibrary.h"
#include "Math/Box2D.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/Class.h"
#include "UObject/UnrealType.h"
#include "WidgetBlueprint.h"

DEFINE_LOG_CATEGORY_STATIC(LogWidgetLayoutTools, Log, All);

namespace WidgetLayoutTools
{

static UWidgetBlueprint* LoadWidgetBlueprint(const FString& AssetPath)
{
	if (AssetPath.IsEmpty()) return nullptr;
	return Cast<UWidgetBlueprint>(UEditorAssetLibrary::LoadAsset(AssetPath));
}

static UWidgetBlueprint* LoadWidgetBlueprintWithDiagnostic(const FString& AssetPath, FString& OutError)
{
	if (AssetPath.IsEmpty())
	{
		OutError = TEXT("Asset path is empty.");
		return nullptr;
	}
	UObject* Loaded = UEditorAssetLibrary::LoadAsset(AssetPath);
	if (!Loaded)
	{
		OutError = FString::Printf(
			TEXT("No asset found at '%s'. Verify the path with find_asset_by_name or asset_management(list_assets)."),
			*AssetPath);
		return nullptr;
	}
	if (UWidgetBlueprint* WBP = Cast<UWidgetBlueprint>(Loaded))
	{
		return WBP;
	}
	OutError = FString::Printf(
		TEXT("Asset at '%s' is %s, not a WidgetBlueprint. Recreate with blueprint(create_blueprint, parent_class='UserWidget', save_path=..., name=...)."),
		*AssetPath, *Loaded->GetClass()->GetName());
	return nullptr;
}

static FVector2D GetDesignTimeSize(UWidgetBlueprint* WBP)
{
	if (!WBP) return FVector2D(1920.0, 1080.0);
	if (UClass* GenClass = WBP->GeneratedClass)
	{
		if (UUserWidget* CDO = Cast<UUserWidget>(GenClass->GetDefaultObject()))
		{
			if (CDO->DesignTimeSize.X > 0.0f && CDO->DesignTimeSize.Y > 0.0f)
			{
				return FVector2D(CDO->DesignTimeSize);
			}
		}
	}
	return FVector2D(1920.0, 1080.0);
}

static FBox2D ResolveCanvasChildRect(UCanvasPanelSlot* Slot, const FVector2D& CanvasSize)
{
	const FAnchors Anch   = Slot->GetAnchors();
	const FMargin  Off    = Slot->GetOffsets();
	const FVector2D Align = FVector2D(Slot->GetAlignment());

	const FVector2D AnchorMin(Anch.Minimum);
	const FVector2D AnchorMax(Anch.Maximum);
	const FVector2D AnchorMinPx = AnchorMin * CanvasSize;
	const FVector2D AnchorMaxPx = AnchorMax * CanvasSize;

	const bool bStretchX = !FMath::IsNearlyEqual(AnchorMin.X, AnchorMax.X);
	const bool bStretchY = !FMath::IsNearlyEqual(AnchorMin.Y, AnchorMax.Y);

	FVector2D Pos, Size;

	if (bStretchX)
	{
		Pos.X  = AnchorMinPx.X + Off.Left;
		Size.X = (AnchorMaxPx.X - Off.Right) - Pos.X;
	}
	else
	{
		Size.X = Off.Right;
		Pos.X  = AnchorMinPx.X + Off.Left - Align.X * Size.X;
	}

	if (bStretchY)
	{
		Pos.Y  = AnchorMinPx.Y + Off.Top;
		Size.Y = (AnchorMaxPx.Y - Off.Bottom) - Pos.Y;
	}
	else
	{
		Size.Y = Off.Bottom;
		Pos.Y  = AnchorMinPx.Y + Off.Top - Align.Y * Size.Y;
	}

	return FBox2D(Pos, Pos + Size);
}

static TSharedPtr<FJsonObject> RectToJson(const FBox2D& Box)
{
	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetNumberField(TEXT("x"),      Box.Min.X);
	R->SetNumberField(TEXT("y"),      Box.Min.Y);
	R->SetNumberField(TEXT("width"),  Box.GetSize().X);
	R->SetNumberField(TEXT("height"), Box.GetSize().Y);
	return R;
}

static void WalkLayoutRecursive(
	UWidget* Widget,
	const FVector2D& ParentSize,
	TArray<TSharedPtr<FJsonValue>>& OutEntries)
{
	if (!Widget) return;

	TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
	Entry->SetStringField(TEXT("widget_name"),  Widget->GetName());
	Entry->SetStringField(TEXT("widget_class"), Widget->GetClass()->GetName());

	FBox2D ResolvedRect(FVector2D::ZeroVector, ParentSize);
	bool   bHasRect = false;

	if (UPanelSlot* Slot = Widget->Slot)
	{
		Entry->SetStringField(TEXT("slot_class"), Slot->GetClass()->GetName());

		if (UCanvasPanelSlot* CS = Cast<UCanvasPanelSlot>(Slot))
		{
			ResolvedRect = ResolveCanvasChildRect(CS, ParentSize);
			bHasRect     = true;

			Entry->SetObjectField(TEXT("rect"), RectToJson(ResolvedRect));

			const FAnchors A = CS->GetAnchors();
			TSharedPtr<FJsonObject> Anchors = MakeShared<FJsonObject>();
			Anchors->SetNumberField(TEXT("min_x"), A.Minimum.X);
			Anchors->SetNumberField(TEXT("min_y"), A.Minimum.Y);
			Anchors->SetNumberField(TEXT("max_x"), A.Maximum.X);
			Anchors->SetNumberField(TEXT("max_y"), A.Maximum.Y);
			Entry->SetObjectField(TEXT("anchors"), Anchors);

			const FMargin Off = CS->GetOffsets();
			TSharedPtr<FJsonObject> Offsets = MakeShared<FJsonObject>();
			Offsets->SetNumberField(TEXT("left"),   Off.Left);
			Offsets->SetNumberField(TEXT("top"),    Off.Top);
			Offsets->SetNumberField(TEXT("right"),  Off.Right);
			Offsets->SetNumberField(TEXT("bottom"), Off.Bottom);
			Entry->SetObjectField(TEXT("offsets"), Offsets);

			const FVector2D Align = FVector2D(CS->GetAlignment());
			TSharedPtr<FJsonObject> AlignJ = MakeShared<FJsonObject>();
			AlignJ->SetNumberField(TEXT("x"), Align.X);
			AlignJ->SetNumberField(TEXT("y"), Align.Y);
			Entry->SetObjectField(TEXT("alignment"), AlignJ);

			Entry->SetNumberField(TEXT("z_order"), CS->GetZOrder());
			Entry->SetBoolField(TEXT("auto_size"), CS->GetAutoSize());
		}
		else if (UHorizontalBoxSlot* HB = Cast<UHorizontalBoxSlot>(Slot))
		{
			const FMargin P = HB->GetPadding();
			TSharedPtr<FJsonObject> Pad = MakeShared<FJsonObject>();
			Pad->SetNumberField(TEXT("left"), P.Left); Pad->SetNumberField(TEXT("top"), P.Top);
			Pad->SetNumberField(TEXT("right"), P.Right); Pad->SetNumberField(TEXT("bottom"), P.Bottom);
			Entry->SetObjectField(TEXT("padding"), Pad);
			Entry->SetNumberField(TEXT("size_rule"), (int32)HB->GetSize().SizeRule);
			Entry->SetNumberField(TEXT("size_value"), HB->GetSize().Value);
		}
		else if (UVerticalBoxSlot* VB = Cast<UVerticalBoxSlot>(Slot))
		{
			const FMargin P = VB->GetPadding();
			TSharedPtr<FJsonObject> Pad = MakeShared<FJsonObject>();
			Pad->SetNumberField(TEXT("left"), P.Left); Pad->SetNumberField(TEXT("top"), P.Top);
			Pad->SetNumberField(TEXT("right"), P.Right); Pad->SetNumberField(TEXT("bottom"), P.Bottom);
			Entry->SetObjectField(TEXT("padding"), Pad);
			Entry->SetNumberField(TEXT("size_rule"), (int32)VB->GetSize().SizeRule);
			Entry->SetNumberField(TEXT("size_value"), VB->GetSize().Value);
		}
		else if (UOverlaySlot* OS = Cast<UOverlaySlot>(Slot))
		{
			const FMargin P = OS->GetPadding();
			TSharedPtr<FJsonObject> Pad = MakeShared<FJsonObject>();
			Pad->SetNumberField(TEXT("left"), P.Left); Pad->SetNumberField(TEXT("top"), P.Top);
			Pad->SetNumberField(TEXT("right"), P.Right); Pad->SetNumberField(TEXT("bottom"), P.Bottom);
			Entry->SetObjectField(TEXT("padding"), Pad);
		}
	}

	Entry->SetBoolField(TEXT("has_computed_rect"), bHasRect);
	OutEntries.Add(MakeShared<FJsonValueObject>(Entry));

	if (UPanelWidget* Panel = Cast<UPanelWidget>(Widget))
	{
		FVector2D ChildSpace = ParentSize;
		if (Cast<UCanvasPanel>(Panel))
		{
			ChildSpace = bHasRect ? ResolvedRect.GetSize() : ParentSize;
		}
		for (int32 i = 0; i < Panel->GetChildrenCount(); ++i)
		{
			WalkLayoutRecursive(Panel->GetChildAt(i), ChildSpace, OutEntries);
		}
	}
}

void HandleGetWidgetLayoutFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }

	FString AssetPath;
	if (!Args->TryGetStringField(TEXT("user_widget_path"), AssetPath) || AssetPath.IsEmpty())
	{
		if (!Args->TryGetStringField(TEXT("widget_path"), AssetPath) || AssetPath.IsEmpty())
		{
			OutError = TEXT("Missing required parameter: user_widget_path (or widget_path)");
			return;
		}
	}

	UWidgetBlueprint* WBP = LoadWidgetBlueprintWithDiagnostic(AssetPath, OutError);
	if (!WBP) return;
	if (!WBP->WidgetTree || !WBP->WidgetTree->RootWidget)
	{
		OutError = FString::Printf(TEXT("WidgetBlueprint at '%s' has no root widget."), *AssetPath);
		return;
	}

	const FVector2D DesignSize = GetDesignTimeSize(WBP);

	TArray<TSharedPtr<FJsonValue>> Entries;
	WalkLayoutRecursive(WBP->WidgetTree->RootWidget, DesignSize, Entries);

	TSharedPtr<FJsonObject> Envelope = MakeShared<FJsonObject>();
	Envelope->SetBoolField(TEXT("success"), true);
	Envelope->SetStringField(TEXT("user_widget_path"), AssetPath);
	TSharedPtr<FJsonObject> DesignJ = MakeShared<FJsonObject>();
	DesignJ->SetNumberField(TEXT("width"),  DesignSize.X);
	DesignJ->SetNumberField(TEXT("height"), DesignSize.Y);
	Envelope->SetObjectField(TEXT("design_time_size"), DesignJ);
	Envelope->SetArrayField(TEXT("widgets"), Entries);

	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Envelope.ToSharedRef(), Writer);
}

struct FCanvasGroup
{
	UCanvasPanel* Canvas = nullptr;
	FVector2D     CanvasSize = FVector2D::ZeroVector;
	TArray<TPair<UWidget*, FBox2D>> Children;
};

static void CollectCanvasGroups(
	UWidget* Widget,
	const FVector2D& ParentSize,
	TArray<FCanvasGroup>& OutGroups)
{
	if (!Widget) return;

	FVector2D MySize = ParentSize;
	if (UCanvasPanelSlot* CS = Cast<UCanvasPanelSlot>(Widget->Slot))
	{
		MySize = ResolveCanvasChildRect(CS, ParentSize).GetSize();
	}

	if (UCanvasPanel* Canvas = Cast<UCanvasPanel>(Widget))
	{
		FCanvasGroup Group;
		Group.Canvas     = Canvas;
		Group.CanvasSize = MySize;
		for (int32 i = 0; i < Canvas->GetChildrenCount(); ++i)
		{
			UWidget* Child = Canvas->GetChildAt(i);
			if (!Child) continue;
			if (UCanvasPanelSlot* ChildSlot = Cast<UCanvasPanelSlot>(Child->Slot))
			{
				Group.Children.Add({ Child, ResolveCanvasChildRect(ChildSlot, MySize) });
			}
		}
		if (Group.Children.Num() >= 1) OutGroups.Add(MoveTemp(Group));
	}

	if (UPanelWidget* Panel = Cast<UPanelWidget>(Widget))
	{
		for (int32 i = 0; i < Panel->GetChildrenCount(); ++i)
		{
			CollectCanvasGroups(Panel->GetChildAt(i), MySize, OutGroups);
		}
	}
}

void HandleCheckWidgetOverlapFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }

	FString AssetPath;
	if (!Args->TryGetStringField(TEXT("user_widget_path"), AssetPath) || AssetPath.IsEmpty())
	{
		if (!Args->TryGetStringField(TEXT("widget_path"), AssetPath) || AssetPath.IsEmpty())
		{
			OutError = TEXT("Missing required parameter: user_widget_path (or widget_path)");
			return;
		}
	}

	UWidgetBlueprint* WBP = LoadWidgetBlueprintWithDiagnostic(AssetPath, OutError);
	if (!WBP) return;
	if (!WBP->WidgetTree || !WBP->WidgetTree->RootWidget)
	{
		OutError = FString::Printf(TEXT("WidgetBlueprint at '%s' has no root widget."), *AssetPath);
		return;
	}

	const FVector2D DesignSize = GetDesignTimeSize(WBP);

	TArray<FCanvasGroup> Groups;
	CollectCanvasGroups(WBP->WidgetTree->RootWidget, DesignSize, Groups);

	TArray<TSharedPtr<FJsonValue>> OverlapEntries;
	TArray<TSharedPtr<FJsonValue>> OOBEntries;

	for (const FCanvasGroup& G : Groups)
	{
		const FBox2D CanvasBounds(FVector2D::ZeroVector, G.CanvasSize);

		for (int32 i = 0; i < G.Children.Num(); ++i)
		{
			const FBox2D& A = G.Children[i].Value;

			const bool bOOB =
				A.Min.X < -0.5 || A.Min.Y < -0.5 ||
				A.Max.X > G.CanvasSize.X + 0.5 || A.Max.Y > G.CanvasSize.Y + 0.5;
			if (bOOB)
			{
				TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
				Entry->SetStringField(TEXT("widget_name"), G.Children[i].Key->GetName());
				Entry->SetStringField(TEXT("parent"), G.Canvas->GetName());
				Entry->SetObjectField(TEXT("rect"), RectToJson(A));
				Entry->SetObjectField(TEXT("canvas_rect"), RectToJson(CanvasBounds));
				OOBEntries.Add(MakeShared<FJsonValueObject>(Entry));
			}

			for (int32 j = i + 1; j < G.Children.Num(); ++j)
			{
				const FBox2D& B = G.Children[j].Value;
				if (A.Intersect(B))
				{
					const FBox2D Inter = A.Overlap(B);
					const FVector2D InterSize = Inter.GetSize();
					const double Area = InterSize.X * InterSize.Y;
					if (Area < 0.5) continue;

					TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
					Entry->SetStringField(TEXT("a"), G.Children[i].Key->GetName());
					Entry->SetStringField(TEXT("b"), G.Children[j].Key->GetName());
					Entry->SetStringField(TEXT("parent"), G.Canvas->GetName());
					Entry->SetNumberField(TEXT("area"), Area);
					Entry->SetObjectField(TEXT("overlap_rect"), RectToJson(Inter));
					OverlapEntries.Add(MakeShared<FJsonValueObject>(Entry));
				}
			}
		}
	}

	TSharedPtr<FJsonObject> Envelope = MakeShared<FJsonObject>();
	Envelope->SetBoolField(TEXT("success"), true);
	Envelope->SetStringField(TEXT("user_widget_path"), AssetPath);
	Envelope->SetNumberField(TEXT("canvas_groups_checked"), Groups.Num());
	Envelope->SetArrayField(TEXT("overlaps"), OverlapEntries);
	Envelope->SetArrayField(TEXT("out_of_bounds"), OOBEntries);

	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Envelope.ToSharedRef(), Writer);
}

static TArray<TSharedPtr<FJsonValue>> CollectBindableEvents(UClass* Class)
{
	TArray<TSharedPtr<FJsonValue>> Out;
	if (!Class) return Out;

	for (TFieldIterator<FMulticastDelegateProperty> It(Class); It; ++It)
	{
		FMulticastDelegateProperty* Delegate = *It;
		if (!Delegate) continue;
		if (!Delegate->HasAnyPropertyFlags(CPF_BlueprintAssignable)) continue;

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), Delegate->GetName());

		TArray<TSharedPtr<FJsonValue>> Params;
		if (UFunction* Signature = Delegate->SignatureFunction)
		{
			for (TFieldIterator<FProperty> PIt(Signature); PIt; ++PIt)
			{
				FProperty* P = *PIt;
				if (!P->HasAnyPropertyFlags(CPF_Parm)) continue;
				if (P->HasAnyPropertyFlags(CPF_ReturnParm)) continue;
				TSharedPtr<FJsonObject> Param = MakeShared<FJsonObject>();
				Param->SetStringField(TEXT("name"), P->GetName());
				Param->SetStringField(TEXT("cpp_type"), P->GetCPPType());
				Params.Add(MakeShared<FJsonValueObject>(Param));
			}
		}
		Entry->SetArrayField(TEXT("parameters"), Params);
		Out.Add(MakeShared<FJsonValueObject>(Entry));
	}
	return Out;
}

void HandleGetWidgetBindableEventsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }

	UClass* TargetClass = nullptr;
	FString ResolvedSource;

	FString ClassSpec;
	Args->TryGetStringField(TEXT("widget_class"), ClassSpec);
	if (ClassSpec.IsEmpty()) Args->TryGetStringField(TEXT("widget_type"), ClassSpec);

	if (!ClassSpec.IsEmpty())
	{
		if (ClassSpec.StartsWith(TEXT("/")))
		{
			TargetClass = LoadClass<UWidget>(nullptr, *ClassSpec);
		}
		if (!TargetClass)
		{
			for (TObjectIterator<UClass> It; It; ++It)
			{
				UClass* C = *It;
				if (!C || !C->IsChildOf(UWidget::StaticClass())) continue;
				if (C->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists)) continue;
				if (C->GetName() == ClassSpec ||
					C->GetName() == FString::Printf(TEXT("W%s"), *ClassSpec) ||
					C->GetName() == FString::Printf(TEXT("U%s"), *ClassSpec))
				{
					TargetClass = C;
					break;
				}
			}
		}
		ResolvedSource = FString::Printf(TEXT("class:%s"), *ClassSpec);
	}
	else
	{
		FString AssetPath;
		Args->TryGetStringField(TEXT("user_widget_path"), AssetPath);
		if (AssetPath.IsEmpty()) Args->TryGetStringField(TEXT("widget_path"), AssetPath);

		FString WidgetName;
		Args->TryGetStringField(TEXT("widget_name"), WidgetName);

		if (AssetPath.IsEmpty() || WidgetName.IsEmpty())
		{
			OutError = TEXT("Specify widget_class, or both user_widget_path and widget_name.");
			return;
		}

		UWidgetBlueprint* WBP = LoadWidgetBlueprintWithDiagnostic(AssetPath, OutError);
		if (!WBP) return;
		if (!WBP->WidgetTree)
		{
			OutError = FString::Printf(TEXT("WidgetBlueprint at '%s' has no WidgetTree."), *AssetPath);
			return;
		}
		UWidget* Widget = WBP->WidgetTree->FindWidget(FName(*WidgetName));
		if (!Widget)
		{
			OutError = FString::Printf(TEXT("Widget '%s' not found in '%s'."), *WidgetName, *AssetPath);
			return;
		}
		TargetClass    = Widget->GetClass();
		ResolvedSource = FString::Printf(TEXT("instance:%s.%s"), *AssetPath, *WidgetName);
	}

	if (!TargetClass)
	{
		OutError = FString::Printf(TEXT("Could not resolve widget class from spec '%s'."), *ClassSpec);
		return;
	}

	const TArray<TSharedPtr<FJsonValue>> Events = CollectBindableEvents(TargetClass);

	TSharedPtr<FJsonObject> Envelope = MakeShared<FJsonObject>();
	Envelope->SetBoolField(TEXT("success"), true);
	Envelope->SetStringField(TEXT("widget_class"), TargetClass->GetPathName());
	Envelope->SetStringField(TEXT("source"), ResolvedSource);
	Envelope->SetArrayField(TEXT("events"), Events);

	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Envelope.ToSharedRef(), Writer);
}

}
