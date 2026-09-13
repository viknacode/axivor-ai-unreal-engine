// Copyright 2026, BlueprintsLab, All rights reserved

#include "Tools/WidgetTools.h"
#include "Tools/BatchToolHelper.h"
#include "Tools/PropertyWriteReport.h"
#include "UECPCoreModule.h"
#include "Services/IUECPToolDispatcher.h"
#include "EditorAssetLibrary.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Animation/WidgetAnimation.h"
#include "MovieScene.h"
#include "Blueprint/UserWidget.h"
#pragma warning(disable: 4702)
#include "Tracks/MovieScenePropertyTrack.h"
#include "Tracks/MovieSceneFloatTrack.h"
#include "Sections/MovieSceneFloatSection.h"
#include "Channels/MovieSceneFloatChannel.h"
#include "Channels/MovieSceneChannelProxy.h"
#include "Serialization/JsonSerializer.h"
#include "WidgetBlueprint.h"
#include "UMG.h"
#include "Components/Image.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/GridSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/UniformGridSlot.h"
#include "Components/BorderSlot.h"
#include "Components/OverlaySlot.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/WrapBoxSlot.h"
#include "Components/Button.h"
#include "Components/EditableText.h"
#include "Components/EditableTextBox.h"
#include "Components/ProgressBar.h"
#include "Components/Border.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/WidgetSwitcher.h"
#include "Components/Slider.h"
#include "Components/CheckBox.h"
#include "Components/ComboBoxString.h"
#include "Components/SpinBox.h"
#include "UObject/UnrealType.h"
#include "Engine/Texture2D.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"

namespace WidgetTools
{
void RecursivelyGetProperties(UStruct* InStruct, const FString& Prefix, TArray<TSharedPtr<FJsonValue>>& OutPropertiesArray, int32 Depth)
{
	if (!InStruct || Depth > 2)
	{
		return;
	}

	for (TFieldIterator<FProperty> PropIt(InStruct); PropIt; ++PropIt)
	{
		FProperty* Property = *PropIt;
		if (Property && Property->HasAnyPropertyFlags(CPF_Edit))
		{
			FString PropName = Prefix + Property->GetName();

			if (FStructProperty* StructProperty = CastField<FStructProperty>(Property))
			{
				TSharedPtr<FJsonObject> StructPropObject = MakeShareable(new FJsonObject());
				StructPropObject->SetStringField("name", PropName);
				StructPropObject->SetStringField("type", StructProperty->Struct->GetName());
				OutPropertiesArray.Add(MakeShareable(new FJsonValueObject(StructPropObject)));

				RecursivelyGetProperties(StructProperty->Struct, PropName + TEXT("."), OutPropertiesArray, Depth + 1);
			}
			else
			{
				TSharedPtr<FJsonObject> PropObject = MakeShareable(new FJsonObject());
				PropObject->SetStringField("name", PropName);
				PropObject->SetStringField("type", Property->GetCPPType());
				OutPropertiesArray.Add(MakeShareable(new FJsonValueObject(PropObject)));
			}
		}
	}
}

static class UWidget* FindWidgetForgiving(class UWidgetTree* Tree, const FString& Name, FString& OutNameList);

void HandleAddWidgetToUserWidget(const FString& WidgetPath, const FString& WidgetType, const FString& WidgetName, const FString& ParentName, FString& OutNewName, FString& OutError)

{

	UWidgetBlueprint* WidgetBP = LoadObject<UWidgetBlueprint>(nullptr, *WidgetPath);

	if (!WidgetBP)

	{

		OutError = FString::Printf(TEXT("Could not load User Widget Blueprint at path: %s"), *WidgetPath);

		return;

	}

	if (!WidgetBP->WidgetTree)

	{

		WidgetBP->WidgetTree = NewObject<UWidgetTree>(WidgetBP, TEXT("WidgetTree"), RF_Transactional);

	}

	UClass* WidgetClass = FindFirstObjectSafe<UClass>(*WidgetType);

	if (!WidgetClass || !WidgetClass->IsChildOf(UWidget::StaticClass()))

	{

		WidgetClass = FindFirstObjectSafe<UClass>(*("U" + WidgetType));

	}

	if (!WidgetClass || !WidgetClass->IsChildOf(UWidget::StaticClass()))

	{

		OutError = FString::Printf(TEXT("Could not find a valid UWidget class named '%s'."), *WidgetType);

		return;

	}

	FName NewWidgetFName = FBlueprintEditorUtils::FindUniqueKismetName(WidgetBP, WidgetName);

	UWidget* NewWidget = WidgetBP->WidgetTree->ConstructWidget<UWidget>(WidgetClass, NewWidgetFName);

	if (!NewWidget)

	{

		OutError = "Failed to construct new widget in the WidgetTree.";

		return;

	}

	OutNewName = NewWidgetFName.ToString();

	if (WidgetBP->WidgetTree->RootWidget == nullptr)

	{

		const TArray<FString> RootAliases = {
			TEXT("root"), TEXT("rootwidget"), TEXT("root_widget"),
			TEXT("canvas"), TEXT("canvaspanel"), TEXT("canvas_panel"),
			TEXT("canvas panel"), TEXT("rootcanvas"), TEXT("root canvas"),
		};
		bool bParentLooksLikeRoot = ParentName.IsEmpty();
		if (!bParentLooksLikeRoot)
		{
			for (const FString& Alias : RootAliases)
			{
				if (ParentName.Equals(Alias, ESearchCase::IgnoreCase))
				{ bParentLooksLikeRoot = true; break; }
			}
		}

		if (!bParentLooksLikeRoot)
		{
			WidgetBP->WidgetTree->RemoveWidget(NewWidget);
			OutError = FString::Printf(TEXT("The WidgetTree is empty, so a parent named '%s' cannot exist. Add a CanvasPanel root first, or omit parent_name to make this widget the root."), *ParentName);
			return;
		}

		const bool bNewIsCanvas = WidgetClass && WidgetClass->IsChildOf(UCanvasPanel::StaticClass());
		if (!ParentName.IsEmpty() && !bNewIsCanvas)
		{
			UCanvasPanel* RootCanvas = WidgetBP->WidgetTree->ConstructWidget<UCanvasPanel>(
				UCanvasPanel::StaticClass(),
				FBlueprintEditorUtils::FindUniqueKismetName(WidgetBP, TEXT("CanvasPanel")));
			if (!RootCanvas)
			{
				WidgetBP->WidgetTree->RemoveWidget(NewWidget);
				OutError = TEXT("Failed to auto-create CanvasPanel root.");
				return;
			}
			WidgetBP->WidgetTree->RootWidget = RootCanvas;
			RootCanvas->AddChild(NewWidget);
		}
		else
		{
			WidgetBP->WidgetTree->RootWidget = NewWidget;
		}

	}

	else

	{

		UWidget* ParentWidget = nullptr;

		if (!ParentName.IsEmpty())

		{

			FString AvailableNames;
			ParentWidget = FindWidgetForgiving(WidgetBP->WidgetTree, ParentName, AvailableNames);

			if (!ParentWidget)

			{

				WidgetBP->WidgetTree->RemoveWidget(NewWidget);

				OutError = FString::Printf(TEXT("Could not find a parent widget named '%s'. Available widgets: [%s]. Aliases 'Canvas' / 'CanvasPanel' / 'Root' resolve to the root widget."), *ParentName, *AvailableNames);

				return;

			}

		}

		else

		{

			ParentWidget = WidgetBP->WidgetTree->RootWidget;

		}

		UPanelWidget* ParentPanel = Cast<UPanelWidget>(ParentWidget);

		if (!ParentPanel)

		{

			WidgetBP->WidgetTree->RemoveWidget(NewWidget);

			OutError = FString::Printf(TEXT("The target parent '%s' is not a Panel Widget (e.g., CanvasPanel, VerticalBox) and cannot have children."), *ParentWidget->GetName());

			return;

		}

		ParentPanel->AddChild(NewWidget);

	}

	if (!WidgetName.IsEmpty() && NewWidget)
	{
		NewWidget->bIsVariable = true;
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WidgetBP);

	WidgetBP->GetPackage()->MarkPackageDirty();

}

static FVector2D ParseVec2_WT(const FString& S)
{
	FString C = S;
	C.RemoveFromStart(TEXT("("));
	C.RemoveFromEnd(TEXT(")"));
	TArray<FString> Parts;
	C.ParseIntoArray(Parts, TEXT(","));
	float X = Parts.IsValidIndex(0) ? FCString::Atof(*Parts[0].TrimStartAndEnd()) : 0.f;
	float Y = Parts.IsValidIndex(1) ? FCString::Atof(*Parts[1].TrimStartAndEnd()) : 0.f;
	return FVector2D(X, Y);
}

static FLinearColor ParseLinearColor_WT(const FString& S)
{
	FString L = S.TrimStartAndEnd();
	FString LL = L.ToLower();
	if (LL == TEXT("white"))       return FLinearColor::White;
	if (LL == TEXT("black"))       return FLinearColor::Black;
	if (LL == TEXT("red"))         return FLinearColor::Red;
	if (LL == TEXT("green"))       return FLinearColor::Green;
	if (LL == TEXT("blue"))        return FLinearColor::Blue;
	if (LL == TEXT("yellow"))      return FLinearColor::Yellow;
	if (LL == TEXT("transparent")) return FLinearColor::Transparent;
	if (LL.StartsWith(TEXT("#")))
	{
		FString Hex = LL.RightChop(1);
		auto H = [](const FString& H2, int32 i) { return FParse::HexDigit(H2[i*2])*16 + FParse::HexDigit(H2[i*2+1]); };
		if (Hex.Len() == 6) return FLinearColor(H(Hex,0)/255.f, H(Hex,1)/255.f, H(Hex,2)/255.f, 1.f);
		if (Hex.Len() == 8) return FLinearColor(H(Hex,0)/255.f, H(Hex,1)/255.f, H(Hex,2)/255.f, H(Hex,3)/255.f);
	}
	FString C2 = L; C2.RemoveFromStart(TEXT("(")); C2.RemoveFromEnd(TEXT(")"));
	TArray<FString> Parts; C2.ParseIntoArray(Parts, TEXT(","), true);
	FLinearColor Out = FLinearColor::White;
	bool bNamed = false;
	for (const FString& Part : Parts)
	{
		FString T = Part.TrimStartAndEnd().ToLower();
		if (T.StartsWith(TEXT("r="))) { Out.R = FCString::Atof(*T.RightChop(2)); bNamed = true; }
		else if (T.StartsWith(TEXT("g="))) { Out.G = FCString::Atof(*T.RightChop(2)); bNamed = true; }
		else if (T.StartsWith(TEXT("b="))) { Out.B = FCString::Atof(*T.RightChop(2)); bNamed = true; }
		else if (T.StartsWith(TEXT("a="))) { Out.A = FCString::Atof(*T.RightChop(2)); bNamed = true; }
	}
	if (bNamed) return Out;
	if (Parts.Num() >= 3)
		return FLinearColor(FCString::Atof(*Parts[0].TrimStartAndEnd()), FCString::Atof(*Parts[1].TrimStartAndEnd()),
		                    FCString::Atof(*Parts[2].TrimStartAndEnd()), Parts.Num() >= 4 ? FCString::Atof(*Parts[3].TrimStartAndEnd()) : 1.f);
	return FLinearColor::White;
}

static FMargin ParseFMargin_WT(const FString& S)
{
	FString C = S.TrimStartAndEnd(); C.RemoveFromStart(TEXT("(")); C.RemoveFromEnd(TEXT(")"));
	TArray<FString> P; C.ParseIntoArray(P, TEXT(","), true);
	if (P.Num() == 1) return FMargin(FCString::Atof(*P[0].TrimStartAndEnd()));
	if (P.Num() == 2) return FMargin(FCString::Atof(*P[0].TrimStartAndEnd()), FCString::Atof(*P[1].TrimStartAndEnd()));
	if (P.Num() >= 4) return FMargin(FCString::Atof(*P[0].TrimStartAndEnd()), FCString::Atof(*P[1].TrimStartAndEnd()),
	                                  FCString::Atof(*P[2].TrimStartAndEnd()), FCString::Atof(*P[3].TrimStartAndEnd()));
	return FMargin(0);
}

static EHorizontalAlignment ParseHAlign_WT(const FString& S)
{
	FString L = S.ToLower().TrimStartAndEnd();
	if (L == TEXT("left"))   return HAlign_Left;
	if (L == TEXT("center")) return HAlign_Center;
	if (L == TEXT("right"))  return HAlign_Right;
	return HAlign_Fill;
}

static EVerticalAlignment ParseVAlign_WT(const FString& S)
{
	FString L = S.ToLower().TrimStartAndEnd();
	if (L == TEXT("top"))    return VAlign_Top;
	if (L == TEXT("center")) return VAlign_Center;
	if (L == TEXT("bottom")) return VAlign_Bottom;
	return VAlign_Fill;
}

static FAnchors ParseAnchorPreset_WT(const FString& PresetStr)
{
	FString L = PresetStr.ToLower().TrimStartAndEnd();
	if (L == TEXT("fill") || L == TEXT("fill_screen"))        return FAnchors(0.f, 0.f, 1.f, 1.f);
	if (L == TEXT("fill_horizontal"))                          return FAnchors(0.f, 0.f, 1.f, 0.f);
	if (L == TEXT("fill_vertical"))                            return FAnchors(0.f, 0.f, 0.f, 1.f);
	if (L == TEXT("top_left") || L == TEXT("topleft"))         return FAnchors(0.f, 0.f, 0.f, 0.f);
	if (L == TEXT("top_center") || L == TEXT("topcenter"))     return FAnchors(0.5f,0.f, 0.5f,0.f);
	if (L == TEXT("top_right") || L == TEXT("topright"))       return FAnchors(1.f, 0.f, 1.f, 0.f);
	if (L == TEXT("center_left") || L == TEXT("centerleft") || L == TEXT("left")) return FAnchors(0.f, 0.5f,0.f, 0.5f);
	if (L == TEXT("center"))                                   return FAnchors(0.5f,0.5f,0.5f,0.5f);
	if (L == TEXT("center_right") || L == TEXT("centerright") || L == TEXT("right")) return FAnchors(1.f, 0.5f,1.f, 0.5f);
	if (L == TEXT("bottom_left") || L == TEXT("bottomleft"))   return FAnchors(0.f, 1.f, 0.f, 1.f);
	if (L == TEXT("bottom_center") || L == TEXT("bottomcenter") || L == TEXT("bottom")) return FAnchors(0.5f,1.f, 0.5f,1.f);
	if (L == TEXT("bottom_right") || L == TEXT("bottomright")) return FAnchors(1.f, 1.f, 1.f, 1.f);
	FString C = L; C.RemoveFromStart(TEXT("(")); C.RemoveFromEnd(TEXT(")"));
	TArray<FString> P; C.ParseIntoArray(P, TEXT(","));
	if (P.Num() >= 4)
		return FAnchors(FCString::Atof(*P[0]), FCString::Atof(*P[1]), FCString::Atof(*P[2]), FCString::Atof(*P[3]));
	if (P.Num() == 2)
	{
		float X = FCString::Atof(*P[0]), Y = FCString::Atof(*P[1]);
		return FAnchors(X, Y, X, Y);
	}
	return FAnchors(0.f, 0.f, 0.f, 0.f);
}

static FVector2D DefaultAlignmentForAnchor_WT(const FString& AnchorStr)
{
	FString L = AnchorStr.ToLower().TrimStartAndEnd();
	if (L == TEXT("top_right") || L == TEXT("topright"))               return FVector2D(1.f, 0.f);
	if (L == TEXT("top_center") || L == TEXT("topcenter"))             return FVector2D(0.5f, 0.f);
	if (L == TEXT("top_left") || L == TEXT("topleft"))                 return FVector2D(0.f, 0.f);
	if (L == TEXT("bottom_right") || L == TEXT("bottomright"))         return FVector2D(1.f, 1.f);
	if (L == TEXT("bottom_center") || L == TEXT("bottomcenter") || L == TEXT("bottom")) return FVector2D(0.5f, 1.f);
	if (L == TEXT("bottom_left") || L == TEXT("bottomleft"))           return FVector2D(0.f, 1.f);
	if (L == TEXT("center_right") || L == TEXT("centerright") || L == TEXT("right")) return FVector2D(1.f, 0.5f);
	if (L == TEXT("center_left") || L == TEXT("centerleft") || L == TEXT("left"))    return FVector2D(0.f, 0.5f);
	if (L == TEXT("center"))                                           return FVector2D(0.5f, 0.5f);
	if (L == TEXT("fill") || L == TEXT("fill_screen"))                 return FVector2D(0.5f, 0.5f);
	return FVector2D(0.f, 0.f);
}

FString JsonValueToPropertyString(const TSharedPtr<FJsonValue>& Val)
{
	if (!Val.IsValid()) return TEXT("");

	switch (Val->Type)
	{
	case EJson::String:
		return Val->AsString();
	case EJson::Number:
		{
			double D = Val->AsNumber();
			if (FMath::IsNearlyEqual(D, FMath::RoundToDouble(D)))
				return FString::Printf(TEXT("%d"), (int32)D);
			return FString::Printf(TEXT("%.4f"), D);
		}
	case EJson::Boolean:
		return Val->AsBool() ? TEXT("true") : TEXT("false");
	case EJson::Array:
		{
			const TArray<TSharedPtr<FJsonValue>>& Arr = Val->AsArray();
			TArray<FString> Parts;
			for (const auto& E : Arr)
			{
				if (E.IsValid())
				{
					if (E->Type == EJson::Number)
					{
						double D = E->AsNumber();
						if (FMath::IsNearlyEqual(D, FMath::RoundToDouble(D)))
							Parts.Add(FString::Printf(TEXT("%d"), (int32)D));
						else
							Parts.Add(FString::Printf(TEXT("%.4f"), D));
					}
					else
						Parts.Add(E->AsString());
				}
			}
			return FString::Join(Parts, TEXT(","));
		}
	case EJson::Object:
		{
			const TSharedPtr<FJsonObject>& Obj = Val->AsObject();
			if (Obj->HasField(TEXT("x")))
			{
				double Xd = 0, Yd = 0;
				Obj->TryGetNumberField(TEXT("x"), Xd);
				Obj->TryGetNumberField(TEXT("y"), Yd);
				return FString::Printf(TEXT("%g,%g"), Xd, Yd);
			}
			FString Out;
			TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> W =
				TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
			FJsonSerializer::Serialize(Obj.ToSharedRef(), W);
			return Out;
		}
	default:
		return TEXT("");
	}
}

static UWidget* FindWidgetForgiving(class UWidgetTree* Tree, const FString& Name, FString& OutNameList)
{
	if (!Tree) return nullptr;

	TArray<UWidget*> AllWidgets;
	Tree->GetAllWidgets(AllWidgets);
	TArray<FString> Names;
	Names.Reserve(AllWidgets.Num());
	for (UWidget* W : AllWidgets)
	{
		if (W) Names.Add(W->GetName());
	}
	OutNameList = FString::Join(Names, TEXT(", "));

	if (Name.IsEmpty()) return Tree->RootWidget;

	if (UWidget* Hit = Tree->FindWidget(FName(*Name)))
		return Hit;

	for (UWidget* W : AllWidgets)
	{
		if (W && W->GetName().Equals(Name, ESearchCase::IgnoreCase)) return W;
	}

	const TArray<FString> RootAliases = {
		TEXT("root"), TEXT("rootwidget"), TEXT("root_widget"),
		TEXT("canvas"), TEXT("canvaspanel"), TEXT("canvas_panel"),
		TEXT("canvas panel"), TEXT("rootcanvas"), TEXT("root canvas"),
	};
	if (Tree->RootWidget)
	{
		for (const FString& Alias : RootAliases)
		{
			if (Name.Equals(Alias, ESearchCase::IgnoreCase)) return Tree->RootWidget;
		}
	}

	{
		UWidget* Match = nullptr;
		int32 Count = 0;
		const FString NameLower = Name.ToLower();
		for (UWidget* W : AllWidgets)
		{
			if (!W) continue;
			if (W->GetName().ToLower().Contains(NameLower))
			{
				Match = W;
				if (++Count > 1) { Match = nullptr; break; }
			}
		}
		if (Match) return Match;
	}

	return nullptr;
}

FString NormalizeWidgetPropertyName(const FString& Key)
{
	if (Key.Len() > 0 && FChar::IsUpper(Key[0]) && !Key.Contains(TEXT("_")))
		return Key;

	static const TMap<FString, FString> Map = {
		{TEXT("text"), TEXT("Text")},
		{TEXT("brush_color"), TEXT("BrushColor")},
		{TEXT("font_size"), TEXT("Font.Size")},
		{TEXT("fontsize"),  TEXT("Font.Size")},
		{TEXT("FontSize"),  TEXT("Font.Size")},
		{TEXT("size"),      TEXT("Font.Size")},
		{TEXT("font_family"), TEXT("Font.FontObject")},
		{TEXT("color_and_opacity"), TEXT("ColorAndOpacity")},
		{TEXT("fill_color_and_opacity"), TEXT("FillColorAndOpacity")},
		{TEXT("percent"), TEXT("Percent")},
		{TEXT("background_color"), TEXT("BackgroundColor")},
		{TEXT("visibility"), TEXT("Visibility")},
		{TEXT("render_opacity"), TEXT("RenderOpacity")},
		{TEXT("auto_wrap_text"), TEXT("AutoWrapText")},
		{TEXT("wrap_text_at"), TEXT("WrapTextAt")},
		{TEXT("justification"), TEXT("Justification")},
		{TEXT("is_password"), TEXT("IsPassword")},
		{TEXT("hint_text"), TEXT("HintText")},
		{TEXT("content_color_and_opacity"), TEXT("ContentColorAndOpacity")},
		{TEXT("width_override"), TEXT("WidthOverride")},
		{TEXT("height_override"), TEXT("HeightOverride")},
		{TEXT("padding"), TEXT("Padding")},
		{TEXT("is_enabled"), TEXT("bIsEnabled")},
		{TEXT("is_variable"), TEXT("bIsVariable")},
		{TEXT("tool_tip_text"), TEXT("ToolTipText")},
		{TEXT("tooltip_text"), TEXT("ToolTipText")},
		{TEXT("clipping"), TEXT("Clipping")},
		{TEXT("scroll_bar_visibility"), TEXT("ScrollBarVisibility")},
		{TEXT("always_show_scrollbar"), TEXT("AlwaysShowScrollbar")},
		{TEXT("allow_overscroll"), TEXT("AllowOverscroll")},
		{TEXT("wheel_scroll_multiplier"), TEXT("WheelScrollMultiplier")},
		{TEXT("orientation"), TEXT("Orientation")},
		{TEXT("min_desired_width"), TEXT("MinDesiredWidth")},
		{TEXT("min_desired_height"), TEXT("MinDesiredHeight")},
		{TEXT("max_desired_width"), TEXT("MaxDesiredWidth")},
		{TEXT("max_desired_height"), TEXT("MaxDesiredHeight")},
		{TEXT("min_aspect_ratio"), TEXT("MinAspectRatio")},
		{TEXT("max_aspect_ratio"), TEXT("MaxAspectRatio")},
		{TEXT("active_widget_index"), TEXT("ActiveWidgetIndex")},
		{TEXT("value"),                TEXT("Value")},
		{TEXT("min_value"),            TEXT("MinValue")},
		{TEXT("max_value"),            TEXT("MaxValue")},
		{TEXT("step_size"),            TEXT("StepSize")},
		{TEXT("slider_bar_color"),     TEXT("SliderBarColor")},
		{TEXT("slider_handle_color"),  TEXT("SliderHandleColor")},
		{TEXT("is_checked"),    TEXT("IsChecked")},
		{TEXT("checked"),       TEXT("IsChecked")},
		{TEXT("checked_state"), TEXT("CheckedState")},
		{TEXT("options"),         TEXT("DefaultOptions")},
		{TEXT("default_options"), TEXT("DefaultOptions")},
		{TEXT("selected_option"), TEXT("SelectedOption")},
		{TEXT("default_option"),  TEXT("SelectedOption")},
		{TEXT("delta"),            TEXT("Delta")},
		{TEXT("min_slider_value"), TEXT("MinSliderValue")},
		{TEXT("max_slider_value"), TEXT("MaxSliderValue")},
		{TEXT("shadow_offset"), TEXT("ShadowOffset")},
		{TEXT("shadow_color_and_opacity"), TEXT("ShadowColorAndOpacity")},
		{TEXT("wrap_at"), TEXT("WrapTextAt")},
		{TEXT("auto_wrap"), TEXT("AutoWrapText")},
		{TEXT("position"), TEXT("Position")},
		{TEXT("size"), TEXT("Size")},
		{TEXT("anchors"), TEXT("Anchors")},
		{TEXT("alignment"), TEXT("Alignment")},
		{TEXT("z_order"), TEXT("ZOrder")},
		{TEXT("auto_size"), TEXT("AutoSize")},
		{TEXT("fill"), TEXT("Fill")},
	};

	FString Lower = Key.ToLower();
	if (const FString* Found = Map.Find(Lower))
		return *Found;

	FString Result;
	bool bCapNext = true;
	for (TCHAR C : Key)
	{
		if (C == '_')
		{
			bCapNext = true;
			continue;
		}
		Result += bCapNext ? FChar::ToUpper(C) : C;
		bCapNext = false;
	}
	return Result;
}

void ExtractInlineProperties(
	const TSharedPtr<FJsonObject>& WidgetDef,
	TArray<TPair<FString, FString>>& OutProperties)
{
	static const TSet<FString> ReservedKeys = {
		TEXT("widget_type"), TEXT("type"), TEXT("class"),
		TEXT("widget_name"), TEXT("name"),
		TEXT("parent_name"), TEXT("parent"),
		TEXT("children"), TEXT("properties"), TEXT("slot"),
		TEXT("layout_definition"), TEXT("layout"),
	};

	const TSharedPtr<FJsonObject>* SlotObj = nullptr;
	if (WidgetDef->TryGetObjectField(TEXT("slot"), SlotObj) && SlotObj && (*SlotObj).IsValid())
	{
		for (const auto& Pair : (*SlotObj)->Values)
		{
			FString InnerKey(*Pair.Key);
			if (InnerKey.StartsWith(TEXT("Slot."), ESearchCase::IgnoreCase))
				InnerKey = InnerKey.RightChop(5);
			FString PropName = TEXT("Slot.") + NormalizeWidgetPropertyName(InnerKey);
			FString PropVal = JsonValueToPropertyString(Pair.Value);
			OutProperties.Add({PropName, PropVal});
		}
	}

	for (const auto& Pair : WidgetDef->Values)
	{
		const FString PairKey(*Pair.Key);
		if (ReservedKeys.Contains(PairKey.ToLower()) || ReservedKeys.Contains(PairKey))
			continue;
		FString PropName = NormalizeWidgetPropertyName(PairKey);
		FString PropVal = JsonValueToPropertyString(Pair.Value);
		OutProperties.Add({PropName, PropVal});
	}
}

static FString StripUnderscores_WT(const FString& In)
{
	FString Out = In;
	Out.ReplaceInline(TEXT("_"), TEXT(""));
	return Out;
}

static bool ApplySlotTypedProperty(UPanelSlot* Slot, const FString& PropNameRaw, const FString& PropVal)
{
	if (!Slot) return false;
	const FString PropName = StripUnderscores_WT(PropNameRaw);

	if (UCanvasPanelSlot* CS = Cast<UCanvasPanelSlot>(Slot))
	{
		if (PropName.Equals(TEXT("Anchors"), ESearchCase::IgnoreCase))
		{
			CS->SetAnchors(ParseAnchorPreset_WT(PropVal));
			CS->SetAlignment(DefaultAlignmentForAnchor_WT(PropVal));
			return true;
		}
		if (PropName.Equals(TEXT("Position"), ESearchCase::IgnoreCase))       { CS->SetPosition(ParseVec2_WT(PropVal)); return true; }
		if (PropName.Equals(TEXT("Size"), ESearchCase::IgnoreCase))           { CS->SetSize(ParseVec2_WT(PropVal)); return true; }
		if (PropName.Equals(TEXT("Alignment"), ESearchCase::IgnoreCase))      { CS->SetAlignment(ParseVec2_WT(PropVal)); return true; }
		if (PropName.Equals(TEXT("ZOrder"), ESearchCase::IgnoreCase))         { CS->SetZOrder(FCString::Atoi(*PropVal)); return true; }
		if (PropName.Equals(TEXT("AutoSize"), ESearchCase::IgnoreCase))       { CS->SetAutoSize(PropVal.ToBool()); return true; }
	}
	else if (UUniformGridSlot* UGS = Cast<UUniformGridSlot>(Slot))
	{
		if (PropName.Equals(TEXT("Row"), ESearchCase::IgnoreCase))    { UGS->SetRow(FCString::Atoi(*PropVal)); return true; }
		if (PropName.Equals(TEXT("Column"), ESearchCase::IgnoreCase)) { UGS->SetColumn(FCString::Atoi(*PropVal)); return true; }
		if (PropName.Equals(TEXT("HorizontalAlignment"), ESearchCase::IgnoreCase) || PropName.Equals(TEXT("HAlign"), ESearchCase::IgnoreCase))
			{ UGS->SetHorizontalAlignment(ParseHAlign_WT(PropVal)); return true; }
		if (PropName.Equals(TEXT("VerticalAlignment"), ESearchCase::IgnoreCase) || PropName.Equals(TEXT("VAlign"), ESearchCase::IgnoreCase))
			{ UGS->SetVerticalAlignment(ParseVAlign_WT(PropVal)); return true; }
	}
	else if (UHorizontalBoxSlot* HBS = Cast<UHorizontalBoxSlot>(Slot))
	{
		if (PropName.Equals(TEXT("Fill"), ESearchCase::IgnoreCase))
		{
			FSlateChildSize SZ(ESlateSizeRule::Fill);
			SZ.Value = FCString::Atof(*PropVal);
			HBS->SetSize(SZ);
			return true;
		}
		if (PropName.Equals(TEXT("Size.SizeRule"), ESearchCase::IgnoreCase) || PropName.Equals(TEXT("SizeRule"), ESearchCase::IgnoreCase))
		{
			FSlateChildSize SZ = HBS->GetSize();
			const FString L = PropVal.ToLower().TrimStartAndEnd();
			SZ.SizeRule = (L == TEXT("fill") || L == TEXT("eslatesizerule::fill")) ? ESlateSizeRule::Fill : ESlateSizeRule::Automatic;
			HBS->SetSize(SZ);
			return true;
		}
		if (PropName.Equals(TEXT("Size.Value"), ESearchCase::IgnoreCase)
			|| PropName.Equals(TEXT("SizeValue"), ESearchCase::IgnoreCase)
			|| PropName.Equals(TEXT("FillValue"), ESearchCase::IgnoreCase))
		{
			FSlateChildSize SZ = HBS->GetSize();
			SZ.Value = FCString::Atof(*PropVal);
			HBS->SetSize(SZ);
			return true;
		}
		if (PropName.Equals(TEXT("Padding"), ESearchCase::IgnoreCase)) { HBS->SetPadding(ParseFMargin_WT(PropVal)); return true; }
		if (PropName.Equals(TEXT("HorizontalAlignment"), ESearchCase::IgnoreCase) || PropName.Equals(TEXT("HAlign"), ESearchCase::IgnoreCase))
			{ HBS->SetHorizontalAlignment(ParseHAlign_WT(PropVal)); return true; }
		if (PropName.Equals(TEXT("VerticalAlignment"), ESearchCase::IgnoreCase) || PropName.Equals(TEXT("VAlign"), ESearchCase::IgnoreCase))
			{ HBS->SetVerticalAlignment(ParseVAlign_WT(PropVal)); return true; }
	}
	else if (UVerticalBoxSlot* VBS = Cast<UVerticalBoxSlot>(Slot))
	{
		if (PropName.Equals(TEXT("Fill"), ESearchCase::IgnoreCase))
		{
			FSlateChildSize SZ(ESlateSizeRule::Fill);
			SZ.Value = FCString::Atof(*PropVal);
			VBS->SetSize(SZ);
			return true;
		}
		if (PropName.Equals(TEXT("Size.SizeRule"), ESearchCase::IgnoreCase) || PropName.Equals(TEXT("SizeRule"), ESearchCase::IgnoreCase))
		{
			FSlateChildSize SZ = VBS->GetSize();
			const FString L = PropVal.ToLower().TrimStartAndEnd();
			SZ.SizeRule = (L == TEXT("fill") || L == TEXT("eslatesizerule::fill")) ? ESlateSizeRule::Fill : ESlateSizeRule::Automatic;
			VBS->SetSize(SZ);
			return true;
		}
		if (PropName.Equals(TEXT("Size.Value"), ESearchCase::IgnoreCase)
			|| PropName.Equals(TEXT("SizeValue"), ESearchCase::IgnoreCase)
			|| PropName.Equals(TEXT("FillValue"), ESearchCase::IgnoreCase))
		{
			FSlateChildSize SZ = VBS->GetSize();
			SZ.Value = FCString::Atof(*PropVal);
			VBS->SetSize(SZ);
			return true;
		}
		if (PropName.Equals(TEXT("Padding"), ESearchCase::IgnoreCase)) { VBS->SetPadding(ParseFMargin_WT(PropVal)); return true; }
		if (PropName.Equals(TEXT("HorizontalAlignment"), ESearchCase::IgnoreCase) || PropName.Equals(TEXT("HAlign"), ESearchCase::IgnoreCase))
			{ VBS->SetHorizontalAlignment(ParseHAlign_WT(PropVal)); return true; }
		if (PropName.Equals(TEXT("VerticalAlignment"), ESearchCase::IgnoreCase) || PropName.Equals(TEXT("VAlign"), ESearchCase::IgnoreCase))
			{ VBS->SetVerticalAlignment(ParseVAlign_WT(PropVal)); return true; }
	}
	else if (UOverlaySlot* OS = Cast<UOverlaySlot>(Slot))
	{
		if (PropName.Equals(TEXT("HorizontalAlignment"), ESearchCase::IgnoreCase) || PropName.Equals(TEXT("HAlign"), ESearchCase::IgnoreCase))
			{ OS->SetHorizontalAlignment(ParseHAlign_WT(PropVal)); return true; }
		if (PropName.Equals(TEXT("VerticalAlignment"), ESearchCase::IgnoreCase) || PropName.Equals(TEXT("VAlign"), ESearchCase::IgnoreCase))
			{ OS->SetVerticalAlignment(ParseVAlign_WT(PropVal)); return true; }
		if (PropName.Equals(TEXT("Padding"), ESearchCase::IgnoreCase)) { OS->SetPadding(ParseFMargin_WT(PropVal)); return true; }
	}
	else if (UBorderSlot* BS = Cast<UBorderSlot>(Slot))
	{
		if (PropName.Equals(TEXT("HorizontalAlignment"), ESearchCase::IgnoreCase) || PropName.Equals(TEXT("HAlign"), ESearchCase::IgnoreCase))
			{ BS->SetHorizontalAlignment(ParseHAlign_WT(PropVal)); return true; }
		if (PropName.Equals(TEXT("VerticalAlignment"), ESearchCase::IgnoreCase) || PropName.Equals(TEXT("VAlign"), ESearchCase::IgnoreCase))
			{ BS->SetVerticalAlignment(ParseVAlign_WT(PropVal)); return true; }
		if (PropName.Equals(TEXT("Padding"), ESearchCase::IgnoreCase)) { BS->SetPadding(ParseFMargin_WT(PropVal)); return true; }
	}
	else if (UGridSlot* GS = Cast<UGridSlot>(Slot))
	{
		if (PropName.Equals(TEXT("Row"), ESearchCase::IgnoreCase))        { GS->SetRow(FCString::Atoi(*PropVal)); return true; }
		if (PropName.Equals(TEXT("Column"), ESearchCase::IgnoreCase))     { GS->SetColumn(FCString::Atoi(*PropVal)); return true; }
		if (PropName.Equals(TEXT("RowSpan"), ESearchCase::IgnoreCase))    { GS->SetRowSpan(FCString::Atoi(*PropVal)); return true; }
		if (PropName.Equals(TEXT("ColumnSpan"), ESearchCase::IgnoreCase)) { GS->SetColumnSpan(FCString::Atoi(*PropVal)); return true; }
		if (PropName.Equals(TEXT("Padding"), ESearchCase::IgnoreCase)) { GS->SetPadding(ParseFMargin_WT(PropVal)); return true; }
		if (PropName.Equals(TEXT("HorizontalAlignment"), ESearchCase::IgnoreCase) || PropName.Equals(TEXT("HAlign"), ESearchCase::IgnoreCase))
			{ GS->SetHorizontalAlignment(ParseHAlign_WT(PropVal)); return true; }
		if (PropName.Equals(TEXT("VerticalAlignment"), ESearchCase::IgnoreCase) || PropName.Equals(TEXT("VAlign"), ESearchCase::IgnoreCase))
			{ GS->SetVerticalAlignment(ParseVAlign_WT(PropVal)); return true; }
	}
	return false;
}

static bool ApplyWidgetTypedProperty(UWidget* Widget, const FString& PropNameRaw, const FString& PropVal)
{
	if (!Widget) return false;
	const FString PropName = StripUnderscores_WT(PropNameRaw);

	if (PropName.Equals(TEXT("Visibility"), ESearchCase::IgnoreCase))
	{
		FString L = PropVal.ToLower().TrimStartAndEnd();
		ESlateVisibility V = ESlateVisibility::Visible;
		if (L == TEXT("collapsed"))          V = ESlateVisibility::Collapsed;
		else if (L == TEXT("hidden"))        V = ESlateVisibility::Hidden;
		else if (L == TEXT("hittestinvisible")) V = ESlateVisibility::HitTestInvisible;
		else if (L == TEXT("selfhittestinvisible")) V = ESlateVisibility::SelfHitTestInvisible;
		Widget->SetVisibility(V);
		return true;
	}
	if (PropName.Equals(TEXT("RenderOpacity"), ESearchCase::IgnoreCase))
		{ Widget->SetRenderOpacity(FCString::Atof(*PropVal)); return true; }

	if (UTextBlock* TB = Cast<UTextBlock>(Widget))
	{
		if (PropName.Equals(TEXT("Text"), ESearchCase::IgnoreCase))
			{ TB->SetText(FText::FromString(PropVal)); return true; }
		if (PropName.Equals(TEXT("ColorAndOpacity"), ESearchCase::IgnoreCase))
			{ TB->SetColorAndOpacity(FSlateColor(ParseLinearColor_WT(PropVal))); return true; }
		if (PropName.Equals(TEXT("FontSize"), ESearchCase::IgnoreCase))
		{
			FSlateFontInfo F = TB->GetFont();
			F.Size = FCString::Atoi(*PropVal);
			TB->SetFont(F);
			return true;
		}
		if (PropName.Equals(TEXT("Justification"), ESearchCase::IgnoreCase))
		{
			FString L = PropVal.ToLower().TrimStartAndEnd();
			ETextJustify::Type J = ETextJustify::Left;
			if (L == TEXT("center")) J = ETextJustify::Center;
			else if (L == TEXT("right")) J = ETextJustify::Right;
			TB->SetJustification(J);
			return true;
		}
		if (PropName.Equals(TEXT("AutoWrapText"), ESearchCase::IgnoreCase))
			{ TB->SetAutoWrapText(PropVal.ToBool()); return true; }
		if (PropName.Equals(TEXT("WrapTextAt"), ESearchCase::IgnoreCase))
			{ TB->SetWrapTextAt(FCString::Atof(*PropVal)); return true; }
	}
	if (UImage* Img = Cast<UImage>(Widget))
	{
		if (PropName.Equals(TEXT("ColorAndOpacity"), ESearchCase::IgnoreCase))
			{ Img->SetColorAndOpacity(ParseLinearColor_WT(PropVal)); return true; }
	}
	if (UProgressBar* PB = Cast<UProgressBar>(Widget))
	{
		if (PropName.Equals(TEXT("Percent"), ESearchCase::IgnoreCase))
			{ PB->SetPercent(FCString::Atof(*PropVal)); return true; }
		if (PropName.Equals(TEXT("FillColorAndOpacity"), ESearchCase::IgnoreCase))
			{ PB->SetFillColorAndOpacity(ParseLinearColor_WT(PropVal)); return true; }
	}
	if (UBorder* Bdr = Cast<UBorder>(Widget))
	{
		if (PropName.Equals(TEXT("BrushColor"), ESearchCase::IgnoreCase))
		{
			FLinearColor Col = ParseLinearColor_WT(PropVal);
			FSlateBrush Brush;
			Brush.DrawAs = ESlateBrushDrawType::Box;
			Brush.Margin = FMargin(1.f / 3.f);
			Brush.TintColor = FSlateColor(Col);
			Bdr->SetBrush(Brush);
			Bdr->SetBrushColor(Col);
			return true;
		}
		if (PropName.Equals(TEXT("ContentColorAndOpacity"), ESearchCase::IgnoreCase))
			{ Bdr->SetContentColorAndOpacity(ParseLinearColor_WT(PropVal)); return true; }
		if (PropName.Equals(TEXT("Padding"), ESearchCase::IgnoreCase))
			{ Bdr->SetPadding(ParseFMargin_WT(PropVal)); return true; }
		if (PropName.Equals(TEXT("HorizontalAlignment"), ESearchCase::IgnoreCase) || PropName.Equals(TEXT("HAlign"), ESearchCase::IgnoreCase))
			{ Bdr->SetHorizontalAlignment(ParseHAlign_WT(PropVal)); return true; }
		if (PropName.Equals(TEXT("VerticalAlignment"), ESearchCase::IgnoreCase) || PropName.Equals(TEXT("VAlign"), ESearchCase::IgnoreCase))
			{ Bdr->SetVerticalAlignment(ParseVAlign_WT(PropVal)); return true; }
	}
	if (UButton* Btn = Cast<UButton>(Widget))
	{
		if (PropName.Equals(TEXT("BackgroundColor"), ESearchCase::IgnoreCase))
			{ Btn->SetBackgroundColor(ParseLinearColor_WT(PropVal)); return true; }
	}
	if (UEditableText* ET = Cast<UEditableText>(Widget))
	{
		if (PropName.Equals(TEXT("Text"), ESearchCase::IgnoreCase))
			{ ET->SetText(FText::FromString(PropVal)); return true; }
		if (PropName.Equals(TEXT("HintText"), ESearchCase::IgnoreCase))
			{ ET->SetHintText(FText::FromString(PropVal)); return true; }
		if (PropName.Equals(TEXT("IsPassword"), ESearchCase::IgnoreCase))
			{ ET->SetIsPassword(PropVal.ToBool()); return true; }
	}
	if (UEditableTextBox* ETB = Cast<UEditableTextBox>(Widget))
	{
		if (PropName.Equals(TEXT("Text"), ESearchCase::IgnoreCase))
			{ ETB->SetText(FText::FromString(PropVal)); return true; }
		if (PropName.Equals(TEXT("HintText"), ESearchCase::IgnoreCase))
			{ ETB->SetHintText(FText::FromString(PropVal)); return true; }
	}
	if (UScrollBox* SB = Cast<UScrollBox>(Widget))
	{
		FString Low = PropName.ToLower();
		if (Low == TEXT("scrollbarvisibility"))
		{
			FString V = PropVal.ToLower();
			ESlateVisibility SV = ESlateVisibility::Visible;
			if (V == TEXT("collapsed") || V == TEXT("auto")) SV = ESlateVisibility::Collapsed;
			else if (V == TEXT("hidden"))                    SV = ESlateVisibility::Hidden;
			SB->SetScrollBarVisibility(SV);
			return true;
		}
		if (Low == TEXT("orientation"))
		{
			SB->SetOrientation(PropVal.ToLower() == TEXT("horizontal") ? Orient_Horizontal : Orient_Vertical);
			return true;
		}
		if (Low == TEXT("alwaysshowscrollbar"))  { SB->SetAlwaysShowScrollbar(PropVal.ToBool()); return true; }
		if (Low == TEXT("allowoverscroll"))      { SB->SetAllowOverscroll(PropVal.ToBool()); return true; }
		if (Low == TEXT("wheelscrollmultiplier")) { SB->SetWheelScrollMultiplier(FCString::Atof(*PropVal)); return true; }
	}
	if (USizeBox* SzB = Cast<USizeBox>(Widget))
	{
		float F = FCString::Atof(*PropVal);
		FString Low = PropName.ToLower();
		if      (Low == TEXT("widthoverride"))     { SzB->SetWidthOverride(F); return true; }
		else if (Low == TEXT("heightoverride"))    { SzB->SetHeightOverride(F); return true; }
		else if (Low == TEXT("mindesiredwidth"))   { SzB->SetMinDesiredWidth(F); return true; }
		else if (Low == TEXT("mindesiredheight"))  { SzB->SetMinDesiredHeight(F); return true; }
		else if (Low == TEXT("maxdesiredwidth"))   { SzB->SetMaxDesiredWidth(F); return true; }
		else if (Low == TEXT("maxdesiredheight"))  { SzB->SetMaxDesiredHeight(F); return true; }
		else if (Low == TEXT("minaspectratio")) { SzB->SetMinAspectRatio(F); return true; }
		else if (Low == TEXT("maxaspectratio")) { SzB->SetMaxAspectRatio(F); return true; }
	}
	if (UWidgetSwitcher* WS = Cast<UWidgetSwitcher>(Widget))
	{
		if (PropName.ToLower() == TEXT("activewidgetindex"))
		{
			WS->SetActiveWidgetIndex(FCString::Atoi(*PropVal));
			return true;
		}
	}
	if (USlider* Slider = Cast<USlider>(Widget))
	{
		FString Low = PropName.ToLower();
		if (Low == TEXT("value") || Low == TEXT("percent"))
			{ Slider->SetValue(FCString::Atof(*PropVal)); return true; }
		if (Low == TEXT("minvalue"))
			{ Slider->SetMinValue(FCString::Atof(*PropVal)); return true; }
		if (Low == TEXT("maxvalue"))
			{ Slider->SetMaxValue(FCString::Atof(*PropVal)); return true; }
		if (Low == TEXT("stepsize"))
			{ Slider->SetStepSize(FCString::Atof(*PropVal)); return true; }
		if (Low == TEXT("sliderbarcolor"))
			{ Slider->SetSliderBarColor(ParseLinearColor_WT(PropVal)); return true; }
		if (Low == TEXT("sliderhandlecolor"))
			{ Slider->SetSliderHandleColor(ParseLinearColor_WT(PropVal)); return true; }
	}
	if (UCheckBox* CB = Cast<UCheckBox>(Widget))
	{
		FString Low = PropName.ToLower();
		if (Low == TEXT("ischecked") || Low == TEXT("checked"))
		{
			CB->SetCheckedState(PropVal.ToBool() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked);
			return true;
		}
		if (Low == TEXT("checkedstate"))
		{
			FString V = PropVal.ToLower();
			ECheckBoxState S = ECheckBoxState::Unchecked;
			if (V == TEXT("checked") || V == TEXT("true"))   S = ECheckBoxState::Checked;
			else if (V == TEXT("undetermined"))              S = ECheckBoxState::Undetermined;
			CB->SetCheckedState(S);
			return true;
		}
	}
	if (UComboBoxString* Combo = Cast<UComboBoxString>(Widget))
	{
		FString Low = PropName.ToLower();
		if (Low == TEXT("defaultoptions") || Low == TEXT("options"))
		{
			TArray<FString> Opts;
			PropVal.ParseIntoArray(Opts, TEXT(","), true);
			for (FString& O : Opts) O = O.TrimStartAndEnd();
			if (FArrayProperty* AP = FindFProperty<FArrayProperty>(UComboBoxString::StaticClass(), TEXT("DefaultOptions")))
			{
				FScriptArrayHelper Helper(AP, AP->ContainerPtrToValuePtr<void>(Combo));
				Helper.EmptyValues();
				if (FStrProperty* SP = CastField<FStrProperty>(AP->Inner))
				{
					for (const FString& Opt : Opts)
					{
						int32 Idx = Helper.AddValue();
						SP->SetPropertyValue(Helper.GetRawPtr(Idx), Opt);
					}
				}
			}
			return true;
		}
		if (Low == TEXT("selectedoption") || Low == TEXT("defaultoption"))
			{ Combo->SetSelectedOption(PropVal); return true; }
	}
	if (USpinBox* SB2 = Cast<USpinBox>(Widget))
	{
		FString Low = PropName.ToLower();
		float F = FCString::Atof(*PropVal);
		if (Low == TEXT("value"))          { SB2->SetValue(F); return true; }
		if (Low == TEXT("delta"))          { SB2->SetDelta(F); return true; }
		if (Low == TEXT("minvalue"))       { SB2->SetMinValue(F); return true; }
		if (Low == TEXT("maxvalue"))       { SB2->SetMaxValue(F); return true; }
		if (Low == TEXT("minslidervalue")) { SB2->SetMinSliderValue(F); return true; }
		if (Low == TEXT("maxslidervalue")) { SB2->SetMaxSliderValue(F); return true; }
	}
	return false;
}

static TSharedPtr<FJsonObject> NormalizeWidgetDef_WT(const TSharedPtr<FJsonObject>& WidgetDef)
{
	if (!WidgetDef.IsValid()) return nullptr;
	static const TSet<FString> KnownFields = {
		TEXT("type"), TEXT("widget_type"), TEXT("class"),
		TEXT("name"), TEXT("widget_name"),
		TEXT("parent"), TEXT("parent_name"),
		TEXT("children"), TEXT("properties"),
		TEXT("slot"), TEXT("widget_path")
	};
	for (const auto& Pair : WidgetDef->Values)
	{
		const FString PairKey(*Pair.Key);
		if (KnownFields.Contains(PairKey)) continue;
		const TSharedPtr<FJsonObject>* NestedObj = nullptr;
		if (Pair.Value->TryGetObject(NestedObj) &&
			((*NestedObj)->HasField(TEXT("type")) || (*NestedObj)->HasField(TEXT("widget_type")) || (*NestedObj)->HasField(TEXT("children"))))
		{
			TSharedPtr<FJsonObject> Normalized = MakeShared<FJsonObject>();
			for (const auto& NP : (*NestedObj)->Values) Normalized->SetField(FString(*NP.Key), NP.Value);
			if (!Normalized->HasField(TEXT("name"))) Normalized->SetStringField(TEXT("name"), PairKey);
			return Normalized;
		}
	}
	return WidgetDef;
}

static void FlattenLayout_WT(const TSharedPtr<FJsonObject>& WidgetDef, const FString& ParentName, TArray<TSharedPtr<FJsonObject>>& OutFlat)
{
	if (!WidgetDef.IsValid()) return;
	TSharedPtr<FJsonObject> Norm = NormalizeWidgetDef_WT(WidgetDef);
	TSharedPtr<FJsonObject> Flat = MakeShared<FJsonObject>();
	for (const auto& Pair : Norm->Values)
	{
		const FString PairKey(*Pair.Key);
		if (PairKey != TEXT("children")) Flat->SetField(PairKey, Pair.Value);
	}
	if (!ParentName.IsEmpty()) Flat->SetStringField(TEXT("parent_name"), ParentName);
	OutFlat.Add(Flat);

	FString ThisName;
	if (!Norm->TryGetStringField(TEXT("widget_name"), ThisName)) Norm->TryGetStringField(TEXT("name"), ThisName);

	const TArray<TSharedPtr<FJsonValue>>* ChildrenArr = nullptr;
	const TSharedPtr<FJsonObject>* ChildrenObj = nullptr;
	if (Norm->TryGetArrayField(TEXT("children"), ChildrenArr))
	{
		for (const TSharedPtr<FJsonValue>& CV : *ChildrenArr)
		{
			TSharedPtr<FJsonObject> CD = CV->AsObject();
			if (CD.IsValid()) FlattenLayout_WT(CD, ThisName, OutFlat);
		}
	}
	else if (Norm->TryGetObjectField(TEXT("children"), ChildrenObj))
	{
		for (const auto& CP : (*ChildrenObj)->Values)
		{
			const FString CPKey(*CP.Key);
			const TSharedPtr<FJsonObject>* CD = nullptr;
			if (CP.Value->TryGetObject(CD))
			{
				TSharedPtr<FJsonObject> NC = MakeShared<FJsonObject>();
				for (const auto& P : (*CD)->Values) NC->SetField(FString(*P.Key), P.Value);
				if (!NC->HasField(TEXT("name"))) NC->SetStringField(TEXT("name"), CPKey);
				FlattenLayout_WT(NC, ThisName, OutFlat);
			}
		}
	}
}

void HandleCreateWidgetFromLayout(const FString& WidgetPath, const TArray<TSharedPtr<FJsonValue>>& Layout, FString& OutError)

{

	UWidgetBlueprint* WidgetBP = Cast<UWidgetBlueprint>(UEditorAssetLibrary::LoadAsset(WidgetPath));
	if (!WidgetBP) WidgetBP = LoadObject<UWidgetBlueprint>(nullptr, *WidgetPath);

	if (!WidgetBP) { OutError = FString::Printf(TEXT("Could not load UWidgetBlueprint at path: %s"), *WidgetPath); return; }

	const FScopedTransaction Transaction(FText::FromString(TEXT("MCP: Create Widget Layout")));

	WidgetBP->Modify();

	if (!WidgetBP->WidgetTree) { WidgetBP->WidgetTree = NewObject<UWidgetTree>(WidgetBP, TEXT("WidgetTree"), RF_Transactional); }

	else if (WidgetBP->WidgetTree->RootWidget)
	{
		UWidget* OldRoot = WidgetBP->WidgetTree->RootWidget;
		WidgetBP->WidgetTree->RemoveWidget(OldRoot);
		OldRoot->Rename(nullptr, GetTransientPackage(),
			REN_DontCreateRedirectors | REN_NonTransactional | REN_DoNotDirty);
	}

	WidgetBP->WidgetTree->Modify();

	bool bHasChildren = false;
	for (const TSharedPtr<FJsonValue>& V : Layout)
	{
		TSharedPtr<FJsonObject> O = V->AsObject();
		if (O.IsValid() && O->HasField(TEXT("children"))) { bHasChildren = true; break; }
	}
	TArray<TSharedPtr<FJsonObject>> FlatList;
	if (bHasChildren)
	{
		for (const TSharedPtr<FJsonValue>& V : Layout)
		{
			TSharedPtr<FJsonObject> O = V->AsObject();
			if (O.IsValid()) FlattenLayout_WT(O, FString(), FlatList);
		}
	}
	else
	{
		for (const TSharedPtr<FJsonValue>& V : Layout)
		{
			TSharedPtr<FJsonObject> O = V->AsObject();
			if (O.IsValid()) FlatList.Add(O);
		}
	}

	TMap<FString, UWidget*> CreatedWidgetsMap;

	for (const TSharedPtr<FJsonObject>& WidgetDef : FlatList) {

		if (!WidgetDef.IsValid()) continue;

		FString WidgetTypeStr; if (!WidgetDef->TryGetStringField(TEXT("widget_type"), WidgetTypeStr)) { if (!WidgetDef->TryGetStringField(TEXT("type"), WidgetTypeStr)) { WidgetDef->TryGetStringField(TEXT("class"), WidgetTypeStr); } }

		FString WidgetName; if (!WidgetDef->TryGetStringField(TEXT("widget_name"), WidgetName)) { WidgetDef->TryGetStringField(TEXT("name"), WidgetName); }

		FString ParentName; if (!WidgetDef->TryGetStringField(TEXT("parent_name"), ParentName)) { WidgetDef->TryGetStringField(TEXT("parent"), ParentName); }

		if (WidgetName.IsEmpty() || WidgetTypeStr.IsEmpty()) { OutError = TEXT("Widget definition missing 'name' or 'type'."); return; }

		UClass* WidgetClass = FindFirstObjectSafe<UClass>(*WidgetTypeStr); if (!WidgetClass) { WidgetClass = FindFirstObjectSafe<UClass>(*("U" + WidgetTypeStr)); }

		if (!WidgetClass || !WidgetClass->IsChildOf(UWidget::StaticClass())) { OutError = FString::Printf(TEXT("Invalid widget type '%s'"), *WidgetTypeStr); return; }

		FName NewWidgetName = FBlueprintEditorUtils::FindUniqueKismetName(WidgetBP, WidgetName);

		if (UWidget* Existing = WidgetBP->WidgetTree->FindWidget(NewWidgetName))
		{
			if (Existing->GetClass() != WidgetClass)
			{
				NewWidgetName = FBlueprintEditorUtils::FindUniqueKismetName(WidgetBP, WidgetName + TEXT("_1"));
			}
		}

		if (UObject* Orphan = StaticFindObjectFast(UObject::StaticClass(), WidgetBP->WidgetTree, NewWidgetName))
		{
			if (Orphan->GetClass() != WidgetClass)
			{
				Orphan->Rename(nullptr, GetTransientPackage(),
					REN_DontCreateRedirectors | REN_NonTransactional | REN_DoNotDirty);
			}
		}

		UWidget* NewWidget = WidgetBP->WidgetTree->ConstructWidget<UWidget>(WidgetClass, NewWidgetName);

		if (!NewWidget) { OutError = FString::Printf(TEXT("Failed to construct widget '%s'."), *WidgetName); return; }

		NewWidget->bIsVariable = true;

		CreatedWidgetsMap.Add(WidgetName, NewWidget);

		if (ParentName.IsEmpty()) { WidgetBP->WidgetTree->RootWidget = NewWidget; }

		else {

			UWidget** FoundParentWidgetPtr = CreatedWidgetsMap.Find(ParentName);

			if (!FoundParentWidgetPtr || !*FoundParentWidgetPtr) { OutError = FString::Printf(TEXT("Could not find parent '%s'."), *ParentName); return; }

			UWidget* ParentWidget = *FoundParentWidgetPtr;

			if (UPanelWidget* ParentPanel = Cast<UPanelWidget>(ParentWidget)) { ParentPanel->AddChild(NewWidget); }

			else if (UContentWidget* ParentContent = Cast<UContentWidget>(ParentWidget)) { ParentContent->SetContent(NewWidget); }

			else { OutError = FString::Printf(TEXT("Parent '%s' is not a container."), *ParentName); return; }

		}

	}

	for (const TSharedPtr<FJsonObject>& WidgetDef : FlatList) {

		if (!WidgetDef.IsValid()) continue;

		FString WidgetName; if (!WidgetDef->TryGetStringField(TEXT("widget_name"), WidgetName)) { WidgetDef->TryGetStringField(TEXT("name"), WidgetName); }

		UWidget* TargetWidget = *CreatedWidgetsMap.Find(WidgetName);

		TArray<TPair<FString, FString>> ParsedProperties;

		const TArray<TSharedPtr<FJsonValue>>* PropertiesArray = nullptr; const TSharedPtr<FJsonObject>* PropertiesObject = nullptr;

		if (WidgetDef->TryGetArrayField(TEXT("properties"), PropertiesArray)) {

			for (const TSharedPtr<FJsonValue>& PropValue : *PropertiesArray) {

				const TSharedPtr<FJsonObject>& PropObject = PropValue->AsObject();

				if (PropObject.IsValid()) { FString Key, Value; if (!PropObject->TryGetStringField(TEXT("key"), Key)) { PropObject->TryGetStringField(TEXT("name"), Key); } PropObject->TryGetStringField(TEXT("value"), Value); if (!Key.IsEmpty()) { ParsedProperties.Add(TPair<FString, FString>(Key, Value)); } }

			}

		}

		else if (WidgetDef->TryGetObjectField(TEXT("properties"), PropertiesObject)) {

			for (const auto& Pair : (*PropertiesObject)->Values) { ParsedProperties.Add(TPair<FString, FString>(Pair.Key, Pair.Value->AsString())); }

		}

		{
			TArray<TPair<FString, FString>> InlineProps;
			ExtractInlineProperties(WidgetDef, InlineProps);
			ParsedProperties.Insert(InlineProps, 0);
		}

		for (const auto& PropPair : ParsedProperties) {

			FString PropertyPath = PropPair.Key;

			FString PropValStr = PropPair.Value;

			void* ContainerPtr = TargetWidget;

			UStruct* ContainerStruct = TargetWidget->GetClass();

			if (PropertyPath.StartsWith(TEXT("Slot."), ESearchCase::IgnoreCase))
		{
			if (!TargetWidget->Slot) continue;
			FString SlotPropName = PropertyPath.Mid(5);
			if (ApplySlotTypedProperty(TargetWidget->Slot, SlotPropName, PropValStr))
				continue;
			ContainerPtr = TargetWidget->Slot;
			ContainerStruct = TargetWidget->Slot->GetClass();
			PropertyPath = SlotPropName;
		}

		if (ContainerPtr == TargetWidget)
		{
			if (ApplyWidgetTypedProperty(TargetWidget, PropertyPath, PropValStr))
				continue;
		}

			TArray<FString> PathParts; PropertyPath.ParseIntoArray(PathParts, TEXT("."));

			FProperty* FinalProperty = nullptr;

			for (int32 i = 0; i < PathParts.Num(); ++i) {

				FProperty* CurrentProp = ContainerStruct->FindPropertyByName(FName(*PathParts[i]));

				if (!CurrentProp) { OutError = FString::Printf(TEXT("Property '%s' not found on '%s'"), *PathParts[i], *ContainerStruct->GetName()); return; }

				if (i == PathParts.Num() - 1) { FinalProperty = CurrentProp; }

				else {

					if (FStructProperty* StructProp = CastField<FStructProperty>(CurrentProp)) { ContainerStruct = StructProp->Struct; ContainerPtr = CurrentProp->ContainerPtrToValuePtr<void>(ContainerPtr); if (!ContainerPtr) { OutError = FString::Printf(TEXT("Container pointer became null while traversing path at '%s'."), *PathParts[i]); return; } }

					else { OutError = FString::Printf(TEXT("Property '%s' is not a struct and cannot be traversed."), *PathParts[i]); return; }

				}

			}

			if (FinalProperty && ContainerPtr) {

				FString FinalPropValStr = PropValStr;

				if (FinalProperty->IsA<FTextProperty>() && !PropValStr.StartsWith(TEXT("\"")) && !PropValStr.StartsWith(TEXT("'"))) { FinalPropValStr = FString::Printf(TEXT("\"%s\""), *PropValStr); }

				if (FinalProperty->ImportText_Direct(*FinalPropValStr, FinalProperty->ContainerPtrToValuePtr<void>(ContainerPtr), nullptr, PPF_None) == nullptr) { OutError = FString::Printf(TEXT("Failed to import value for '%s'."), *PropertyPath); return; }

			}

			else { OutError = FString::Printf(TEXT("Failed to resolve final property for path '%s'."), *PropertyPath); return; }

		}

	}

	if (WidgetBP->WidgetTree && WidgetBP->WidgetTree->RootWidget)
	{
		UCanvasPanel* RootCanvas = Cast<UCanvasPanel>(WidgetBP->WidgetTree->RootWidget);
		if (RootCanvas)
		{
			for (UWidget* Child : RootCanvas->GetAllChildren())
			{
				UCanvasPanelSlot* CSlot = Cast<UCanvasPanelSlot>(Child ? Child->Slot : nullptr);
				if (!CSlot) continue;
				const FAnchorData& LD = CSlot->GetLayout();
				FMargin Offsets = LD.Offsets;
				bool bFixed = false;
				if (LD.Anchors.Minimum.Y >= 0.9f && Offsets.Top > 0.f)
				{
					Offsets.Top = -Offsets.Top;
					bFixed = true;
				}
				if (LD.Anchors.Minimum.X >= 0.9f && Offsets.Left > 0.f)
				{
					Offsets.Left = -Offsets.Left;
					bFixed = true;
				}
				if (bFixed) CSlot->SetOffsets(Offsets);
			}
		}
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WidgetBP);

	WidgetBP->GetPackage()->MarkPackageDirty();

}
void HandleAddWidgetsToLayout(const FString& WidgetPath, const TArray<TSharedPtr<FJsonValue>>& Layout, FString& OutError)

{

	UWidgetBlueprint* WidgetBP = LoadObject<UWidgetBlueprint>(nullptr, *WidgetPath);

	if (!WidgetBP || !WidgetBP->WidgetTree) { OutError = TEXT("Could not load a valid widget blueprint to add to."); return; }

	const FScopedTransaction Transaction(FText::FromString(TEXT("MCP: Add Widgets to Layout")));

	WidgetBP->Modify(); WidgetBP->WidgetTree->Modify();

	TArray<UWidget*> AllCurrentWidgets; WidgetBP->WidgetTree->GetAllWidgets(AllCurrentWidgets);

	TMap<FString, UWidget*> AllWidgetsMap;

	for (UWidget* Widget : AllCurrentWidgets) { if (Widget) AllWidgetsMap.Add(Widget->GetFName().ToString(), Widget); }

	TArray<TSharedPtr<FJsonObject>> FlatList;
	{
		bool bHasChildren = false;
		for (const TSharedPtr<FJsonValue>& V : Layout)
		{
			TSharedPtr<FJsonObject> O = V.IsValid() ? V->AsObject() : nullptr;
			if (O.IsValid() && O->HasField(TEXT("children"))) { bHasChildren = true; break; }
		}
		if (bHasChildren)
		{
			for (const TSharedPtr<FJsonValue>& V : Layout)
			{
				TSharedPtr<FJsonObject> O = V.IsValid() ? V->AsObject() : nullptr;
				if (!O.IsValid()) continue;
				FString OuterType;
				if (!O->TryGetStringField(TEXT("widget_type"), OuterType))
				{
					if (!O->TryGetStringField(TEXT("type"), OuterType))
					{
						O->TryGetStringField(TEXT("class"), OuterType);
					}
				}
				FString OuterName;
				if (!O->TryGetStringField(TEXT("widget_name"), OuterName))
				{
					O->TryGetStringField(TEXT("name"), OuterName);
				}
				const TArray<TSharedPtr<FJsonValue>>* ChildArr = nullptr;
				const bool bIsParentRef = OuterType.IsEmpty() && !OuterName.IsEmpty()
					&& AllWidgetsMap.Contains(OuterName);
				if (bIsParentRef && O->TryGetArrayField(TEXT("children"), ChildArr) && ChildArr)
				{
					for (const TSharedPtr<FJsonValue>& CV : *ChildArr)
					{
						TSharedPtr<FJsonObject> CD = CV.IsValid() ? CV->AsObject() : nullptr;
						if (CD.IsValid()) FlattenLayout_WT(CD, OuterName, FlatList);
					}
				}
				else
				{
					FlattenLayout_WT(O, FString(), FlatList);
				}
			}
		}
		else
		{
			for (const TSharedPtr<FJsonValue>& V : Layout)
			{
				TSharedPtr<FJsonObject> O = V.IsValid() ? V->AsObject() : nullptr;
				if (O.IsValid()) FlatList.Add(O);
			}
		}
	}

	TMap<FString, UWidget*> NewWidgetsMap;

	for (const TSharedPtr<FJsonObject>& WidgetDef : FlatList) {

		if (!WidgetDef.IsValid()) continue;

		FString WidgetTypeStr; if (!WidgetDef->TryGetStringField(TEXT("widget_type"), WidgetTypeStr)) { if (!WidgetDef->TryGetStringField(TEXT("type"), WidgetTypeStr)) { WidgetDef->TryGetStringField(TEXT("class"), WidgetTypeStr); } }

		FString WidgetName; if (!WidgetDef->TryGetStringField(TEXT("widget_name"), WidgetName)) { WidgetDef->TryGetStringField(TEXT("name"), WidgetName); }

		FString ParentName; if (!WidgetDef->TryGetStringField(TEXT("parent_name"), ParentName)) { WidgetDef->TryGetStringField(TEXT("parent"), ParentName); }

		if (WidgetName.IsEmpty() || WidgetTypeStr.IsEmpty()) { OutError = TEXT("Widget definition missing 'name' or 'type'."); return; }

		UClass* WidgetClass = FindFirstObjectSafe<UClass>(*WidgetTypeStr); if (!WidgetClass) { WidgetClass = FindFirstObjectSafe<UClass>(*("U" + WidgetTypeStr)); }

		if (!WidgetClass || !WidgetClass->IsChildOf(UWidget::StaticClass())) { OutError = FString::Printf(TEXT("Invalid widget type '%s'."), *WidgetTypeStr); return; }

		FName NewWidgetName = FBlueprintEditorUtils::FindUniqueKismetName(WidgetBP, WidgetName);

		if (UWidget* Existing = WidgetBP->WidgetTree->FindWidget(NewWidgetName))
		{
			if (Existing->GetClass() != WidgetClass)
			{
				NewWidgetName = FBlueprintEditorUtils::FindUniqueKismetName(WidgetBP, WidgetName + TEXT("_1"));
			}
		}

		UWidget* NewWidget = WidgetBP->WidgetTree->ConstructWidget<UWidget>(WidgetClass, NewWidgetName);

		if (!NewWidget) { OutError = FString::Printf(TEXT("Failed to construct widget '%s'."), *WidgetName); return; }

		NewWidget->bIsVariable = true;

		AllWidgetsMap.Add(NewWidgetName.ToString(), NewWidget); NewWidgetsMap.Add(WidgetName, NewWidget);

		if (ParentName.IsEmpty())
		{
			WidgetBP->WidgetTree->RootWidget = NewWidget;
		}
		else
		{
			UWidget** FoundParentWidgetPtr = AllWidgetsMap.Find(ParentName);

			if (!FoundParentWidgetPtr || !*FoundParentWidgetPtr) { OutError = FString::Printf(TEXT("Could not find parent '%s'."), *ParentName); return; }

			UWidget* ParentWidget = *FoundParentWidgetPtr;

			if (UPanelWidget* ParentPanel = Cast<UPanelWidget>(ParentWidget)) { ParentPanel->AddChild(NewWidget); }

			else if (UContentWidget* ParentContent = Cast<UContentWidget>(ParentWidget)) { ParentContent->SetContent(NewWidget); }

			else { OutError = FString::Printf(TEXT("Parent '%s' is not a container."), *ParentName); return; }
		}

	}

	for (const TSharedPtr<FJsonObject>& WidgetDef : FlatList) {

		if (!WidgetDef.IsValid()) continue;

		FString WidgetName; if (!WidgetDef->TryGetStringField(TEXT("widget_name"), WidgetName)) { WidgetDef->TryGetStringField(TEXT("name"), WidgetName); }

		UWidget** TargetWidgetPtr = NewWidgetsMap.Find(WidgetName);
		if (!TargetWidgetPtr || !*TargetWidgetPtr) continue;
		UWidget* TargetWidget = *TargetWidgetPtr;

		TArray<TPair<FString, FString>> ParsedProperties;

		const TArray<TSharedPtr<FJsonValue>>* PropertiesArray = nullptr; const TSharedPtr<FJsonObject>* PropertiesObject = nullptr;

		if (WidgetDef->TryGetArrayField(TEXT("properties"), PropertiesArray)) {

			for (const TSharedPtr<FJsonValue>& PropValue : *PropertiesArray) {

				const TSharedPtr<FJsonObject>& PropObject = PropValue->AsObject();

				if (PropObject.IsValid()) { FString Key, Value; if (!PropObject->TryGetStringField(TEXT("key"), Key)) { PropObject->TryGetStringField(TEXT("name"), Key); } PropObject->TryGetStringField(TEXT("value"), Value); if (!Key.IsEmpty()) { ParsedProperties.Add(TPair<FString, FString>(Key, Value)); } }

			}

		}

		else if (WidgetDef->TryGetObjectField(TEXT("properties"), PropertiesObject)) {

			for (const auto& Pair : (*PropertiesObject)->Values) { ParsedProperties.Add(TPair<FString, FString>(Pair.Key, Pair.Value->AsString())); }

		}

		{
			TArray<TPair<FString, FString>> InlineProps;
			ExtractInlineProperties(WidgetDef, InlineProps);
			ParsedProperties.Insert(InlineProps, 0);
		}

		for (const auto& PropPair : ParsedProperties) {

			FString PropertyPath = PropPair.Key;

			FString PropValStr = PropPair.Value;

			void* ContainerPtr = TargetWidget;

			UStruct* ContainerStruct = TargetWidget->GetClass();

			if (PropertyPath.StartsWith(TEXT("Slot."), ESearchCase::IgnoreCase))
		{
			if (!TargetWidget->Slot) continue;
			FString SlotPropName = PropertyPath.Mid(5);
			if (ApplySlotTypedProperty(TargetWidget->Slot, SlotPropName, PropValStr))
				continue;
			ContainerPtr = TargetWidget->Slot;
			ContainerStruct = TargetWidget->Slot->GetClass();
			PropertyPath = SlotPropName;
		}

			TArray<FString> PathParts; PropertyPath.ParseIntoArray(PathParts, TEXT("."));

			FProperty* FinalProperty = nullptr;

			for (int32 i = 0; i < PathParts.Num(); ++i) {

				FProperty* CurrentProp = ContainerStruct->FindPropertyByName(FName(*PathParts[i]));

				if (!CurrentProp) { OutError = FString::Printf(TEXT("Property '%s' not found on '%s'"), *PathParts[i], *ContainerStruct->GetName()); return; }

				if (i == PathParts.Num() - 1) { FinalProperty = CurrentProp; }

				else {

					if (FStructProperty* StructProp = CastField<FStructProperty>(CurrentProp)) { ContainerStruct = StructProp->Struct; ContainerPtr = CurrentProp->ContainerPtrToValuePtr<void>(ContainerPtr); if (!ContainerPtr) { OutError = FString::Printf(TEXT("Container pointer became null while traversing path at '%s'."), *PathParts[i]); return; } }

					else { OutError = FString::Printf(TEXT("Property '%s' is not a struct."), *PathParts[i]); return; }

				}

			}

			if (FinalProperty && ContainerPtr) {

				FString FinalPropValStr = PropValStr;

				if (FinalProperty->IsA<FTextProperty>() && !PropValStr.StartsWith(TEXT("\"")) && !PropValStr.StartsWith(TEXT("'"))) { FinalPropValStr = FString::Printf(TEXT("\"%s\""), *PropValStr); }

				if (FinalProperty->ImportText_Direct(*FinalPropValStr, FinalProperty->ContainerPtrToValuePtr<void>(ContainerPtr), nullptr, PPF_None) == nullptr) { OutError = FString::Printf(TEXT("Failed to import value for '%s'."), *PropertyPath); return; }

			}

			else { OutError = FString::Printf(TEXT("Failed to resolve final property for path '%s'."), *PropertyPath); return; }

		}

	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WidgetBP);

	WidgetBP->GetPackage()->MarkPackageDirty();

}
void HandleEditWidgetProperties(const FString& WidgetPath, const TArray<TSharedPtr<FJsonValue>>& Edits, FString& OutError)

{

	UWidgetBlueprint* WidgetBP = LoadObject<UWidgetBlueprint>(nullptr, *WidgetPath);

	if (!WidgetBP || !WidgetBP->WidgetTree) { OutError = TEXT("Could not load a valid widget blueprint to edit."); return; }

	const FScopedTransaction Transaction(FText::FromString(TEXT("MCP: Edit Widget Properties")));

	WidgetBP->Modify();

	for (const TSharedPtr<FJsonValue>& EditValue : Edits)

	{

		const TSharedPtr<FJsonObject>& EditObject = EditValue->AsObject();

		if (!EditObject.IsValid()) continue;

		FString WidgetName, PropertyPath, PropValStr;

		if (!EditObject->TryGetStringField(TEXT("widget_name"), WidgetName) ||

			!EditObject->TryGetStringField(TEXT("property_name"), PropertyPath) ||

			!EditObject->TryGetStringField(TEXT("value"), PropValStr))

		{

			OutError = "Invalid edit operation format."; return;

		}

		UWidget* TargetWidget = WidgetBP->WidgetTree->FindWidget(FName(*WidgetName));

		if (!TargetWidget) { OutError = FString::Printf(TEXT("Could not find widget '%s' to edit."), *WidgetName); return; }

		TargetWidget->Modify();

		void* ContainerPtr = TargetWidget;

		UStruct* ContainerStruct = TargetWidget->GetClass();

		PropertyPath = NormalizeWidgetPropertyName(PropertyPath);

		if (PropertyPath.StartsWith(TEXT("Slot."), ESearchCase::IgnoreCase))
		{
			if (!TargetWidget->Slot) continue;
			TargetWidget->Slot->Modify();
			FString SlotPropName = PropertyPath.Mid(5);
			if (ApplySlotTypedProperty(TargetWidget->Slot, SlotPropName, PropValStr))
				continue;
			ContainerPtr = TargetWidget->Slot;
			ContainerStruct = TargetWidget->Slot->GetClass();
			PropertyPath = SlotPropName;
		}

		if (ContainerPtr == TargetWidget)
		{
			if (ApplyWidgetTypedProperty(TargetWidget, PropertyPath, PropValStr))
				continue;
		}

		TArray<FString> PathParts; PropertyPath.ParseIntoArray(PathParts, TEXT("."));

		FProperty* FinalProperty = nullptr;

		for (int32 i = 0; i < PathParts.Num(); ++i) {

			FProperty* CurrentProp = ContainerStruct->FindPropertyByName(FName(*PathParts[i]));

			if (!CurrentProp) { OutError = FString::Printf(TEXT("Property '%s' not found on '%s'"), *PathParts[i], *ContainerStruct->GetName()); return; }

			if (i == PathParts.Num() - 1) { FinalProperty = CurrentProp; }

			else {

				if (FStructProperty* StructProp = CastField<FStructProperty>(CurrentProp)) { ContainerStruct = StructProp->Struct; ContainerPtr = CurrentProp->ContainerPtrToValuePtr<void>(ContainerPtr); if (!ContainerPtr) { OutError = FString::Printf(TEXT("Container pointer became null at '%s'."), *PathParts[i]); return; } }

				else { OutError = FString::Printf(TEXT("Property '%s' is not a struct."), *PathParts[i]); return; }

			}

		}

		if (FinalProperty && ContainerPtr) {

			FString FinalPropValStr = PropValStr;

			if (FinalProperty->IsA<FTextProperty>() && !PropValStr.StartsWith(TEXT("\"")) && !PropValStr.StartsWith(TEXT("'"))) { FinalPropValStr = FString::Printf(TEXT("\"%s\""), *PropValStr); }

			if (FinalProperty->ImportText_Direct(*FinalPropValStr, FinalProperty->ContainerPtrToValuePtr<void>(ContainerPtr), nullptr, PPF_None) == nullptr) { OutError = FString::Printf(TEXT("Failed to import value for '%s'."), *PropertyPath); return; }

		}

		else { OutError = FString::Printf(TEXT("Failed to resolve final property for path '%s'."), *PropertyPath); return; }

	}

	FBlueprintEditorUtils::MarkBlueprintAsModified(WidgetBP);

	WidgetBP->GetPackage()->MarkPackageDirty();

}

void HandleGetBatchWidgetProperties(const TArray<FString>& WidgetClasses, FString& OutJsonString, FString& OutError)

{

	TSharedPtr<FJsonObject> ResultObject = MakeShareable(new FJsonObject());

	for (const FString& ClassName : WidgetClasses)

	{

		UClass* FoundClass = FindFirstObjectSafe<UClass>(*ClassName);

		if (!FoundClass) FoundClass = FindFirstObjectSafe<UClass>(*("U" + ClassName));

		if (!FoundClass) continue;

		TArray<TSharedPtr<FJsonValue>> PropertiesArray;

		RecursivelyGetProperties(FoundClass, TEXT(""), PropertiesArray, 0);

		ResultObject->SetArrayField(ClassName, PropertiesArray);

	}

	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);

	FJsonSerializer::Serialize(ResultObject.ToSharedRef(), Writer);

}
void HandleEditWidgetProperty(const FString& WidgetPath, const FString& WidgetName, const FString& PropertyName, const FString& Value, FString& OutError, FString* OutApplied)

{

	UWidgetBlueprint* WidgetBP = LoadObject<UWidgetBlueprint>(nullptr, *WidgetPath);

	if (!WidgetBP || !WidgetBP->WidgetTree)

	{

		OutError = FString::Printf(TEXT("Could not load a valid User Widget Blueprint at path: %s"), *WidgetPath);

		return;

	}

	FString WidgetNameList;
	UWidget* TargetWidget = FindWidgetForgiving(WidgetBP->WidgetTree, WidgetName, WidgetNameList);

	if (!TargetWidget)

	{

		OutError = FString::Printf(TEXT("Could not find a widget named '%s'. Available widgets: [%s]"), *WidgetName, *WidgetNameList);

		return;

	}

	UObject* PropertyTargetObject = nullptr;

	FString ActualPropertyName = NormalizeWidgetPropertyName(PropertyName);

	if (PropertyName.StartsWith(TEXT("Slot.")))

	{

		UPanelSlot* Slot = TargetWidget->Slot;

		if (!Slot)

		{

			OutError = FString::Printf(TEXT("Widget '%s' does not have a valid 'Slot' property to edit."), *WidgetName);

			return;

		}

		PropertyTargetObject = Slot;

		ActualPropertyName = PropertyName.RightChop(5);

	}

	else

	{

		PropertyTargetObject = TargetWidget;

	}

	void* WriteContainer = PropertyTargetObject;
	FProperty* Property = nullptr;
	{
		TArray<FString> PathParts;
		ActualPropertyName.ParseIntoArray(PathParts, TEXT("."));
		UStruct* CurrentStruct = PropertyTargetObject->GetClass();
		for (int32 i = 0; i < PathParts.Num(); ++i)
		{
			FProperty* StepProp = CurrentStruct ? CurrentStruct->FindPropertyByName(FName(*PathParts[i])) : nullptr;
			if (!StepProp)
			{
				Property = nullptr;
				break;
			}
			if (i == PathParts.Num() - 1)
			{
				Property = StepProp;
				break;
			}
			FStructProperty* StructProp = CastField<FStructProperty>(StepProp);
			if (!StructProp)
			{
				Property = nullptr;
				break;
			}
			WriteContainer = StructProp->ContainerPtrToValuePtr<void>(WriteContainer);
			CurrentStruct = StructProp->Struct;
		}
	}

	if (Property)

	{

		UStruct* OwnerStruct = Property->GetOwnerStruct();

		if (OwnerStruct)

		{

			if (UClass* OwnerClass = Cast<UClass>(OwnerStruct))

			{

				if (!PropertyTargetObject->IsA(OwnerClass))

				{

					OutError += FString::Printf(TEXT("\n[Property Error] Widget '%s': Property '%s' owner mismatch."), *WidgetName, *ActualPropertyName);

					return;

				}

			}

		}

		void* DestPtr = Property->ContainerPtrToValuePtr<void>(WriteContainer);

		if (DestPtr == nullptr)

		{

			OutError += FString::Printf(TEXT("\n[Property Error] Widget '%s': Property '%s' has an invalid destination pointer."), *WidgetName, *ActualPropertyName);

			return;

		}

		PropertyTargetObject->Modify();

		FString ImportValue = Value;
		if (!ImportValue.StartsWith(TEXT("(")))
		{
			if (FStructProperty* SP = CastField<FStructProperty>(Property))
			{
				if (SP->Struct && SP->Struct->GetFName() == FName(TEXT("Margin")))
				{
					FMargin M = ParseFMargin_WT(ImportValue);
					ImportValue = FString::Printf(TEXT("(Left=%f,Top=%f,Right=%f,Bottom=%f)"), M.Left, M.Top, M.Right, M.Bottom);
				}
			}
		}

		if (Property->ImportText_Direct(*ImportValue, DestPtr, nullptr, PPF_None) == nullptr)

		{

			OutError += FString::Printf(TEXT("\n[Property Error] Widget '%s': Failed to import value '%s' into property '%s'."), *WidgetName, *Value, *ActualPropertyName);

		}
		else
		{
			if (ActualPropertyName.Equals(TEXT("bIsVariable"), ESearchCase::IgnoreCase))
				FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WidgetBP);
			else
				FBlueprintEditorUtils::MarkBlueprintAsModified(WidgetBP);
			WidgetBP->MarkPackageDirty();

			if (OutApplied)
				*OutApplied = UECPProps::ExportPropertyValueString(Property, DestPtr);
		}

	}

	else

	{

		TArray<FString> Available;
		const FString FirstPart = ActualPropertyName.Contains(TEXT("."))
			? ActualPropertyName.Left(ActualPropertyName.Find(TEXT("."))) : ActualPropertyName;
		const FString FirstLower = FirstPart.ToLower();
		for (TFieldIterator<FProperty> It(PropertyTargetObject->GetClass()); It; ++It)
		{
			if (Available.Num() >= 8) break;
			const FString N = It->GetName();
			if (N.ToLower().Contains(FirstLower))
			{
				Available.Add(N);
			}
		}
		const FString AvailHint = Available.Num() > 0
			? FString::Printf(TEXT(" Closest matches on %s: [%s]."),
				*PropertyTargetObject->GetClass()->GetName(), *FString::Join(Available, TEXT(", ")))
			: FString();
		OutError += FString::Printf(TEXT("\n[Property Error] Widget '%s': Property '%s' not found on target '%s'.%s"),
			*WidgetName, *ActualPropertyName, *PropertyTargetObject->GetClass()->GetName(), *AvailHint);

	}

}
void HandleDeleteWidget(const FString& WidgetPath, const FString& WidgetName, FString& OutError)

{

	UObject* LoadedObj = UEditorAssetLibrary::LoadAsset(WidgetPath);

	UWidgetBlueprint* WidgetBP = Cast<UWidgetBlueprint>(LoadedObj);

	if (!WidgetBP)

	{

		OutError = FString::Printf(TEXT("Could not load Widget Blueprint at path: %s"), *WidgetPath);

		return;

	}

	if (!WidgetBP->WidgetTree)

	{

		OutError = TEXT("Widget Blueprint does not have a valid WidgetTree.");

		return;

	}

	UWidget* TargetWidget = WidgetBP->WidgetTree->FindWidget(FName(*WidgetName));

	if (!TargetWidget)

	{

		OutError = FString::Printf(TEXT("Widget '%s' not found in the Widget Blueprint."), *WidgetName);

		return;

	}

	if (WidgetBP->WidgetTree->RootWidget == TargetWidget)

	{

		OutError = TEXT("Cannot delete the root widget — UMG always needs exactly one root. To replace the root, call create_widget_from_layout with a new layout (it'll swap the root atomically), or use add_widget_to_user_widget to add a new widget then reparent_widget the existing children before delete.");

		return;

	}

	const FScopedTransaction Transaction(FText::FromString(TEXT("Delete Widget")));

	WidgetBP->Modify();
	WidgetBP->WidgetTree->Modify();

	WidgetBP->WidgetTree->RemoveWidget(TargetWidget);
	TargetWidget = nullptr;

	FBlueprintEditorUtils::MarkBlueprintAsModified(WidgetBP);

}

void HandleSetWidgetCanvasSize(const FString& WidgetPath, const FString& CanvasSize, const FString& DesignMode, FString& OutJsonString, FString& OutError)
{
	UWidgetBlueprint* WidgetBP = LoadObject<UWidgetBlueprint>(nullptr, *WidgetPath);
	if (!WidgetBP) { OutError = FString::Printf(TEXT("Could not load Widget Blueprint at: %s"), *WidgetPath); return; }

	WidgetBP->Modify();
	UUserWidget* CDO = WidgetBP->GeneratedClass ? Cast<UUserWidget>(WidgetBP->GeneratedClass->GetDefaultObject()) : nullptr;
	if (!CDO) { OutError = TEXT("Could not get widget CDO. Try compiling the blueprint first."); return; }
	CDO->Modify();

	if (!CanvasSize.IsEmpty())
	{
		TArray<FString> Parts; CanvasSize.ParseIntoArray(Parts, TEXT(","), true);
		if (Parts.Num() >= 2)
			CDO->DesignTimeSize = FVector2D(FCString::Atof(*Parts[0].TrimStartAndEnd()), FCString::Atof(*Parts[1].TrimStartAndEnd()));
		CDO->DesignSizeMode = EDesignPreviewSizeMode::Custom;
	}

	FString LowerMode = DesignMode.ToLower();
	if (LowerMode == TEXT("fillscreen") || LowerMode == TEXT("fill"))
		CDO->DesignSizeMode = EDesignPreviewSizeMode::FillScreen;
	else if (LowerMode == TEXT("custom"))
		CDO->DesignSizeMode = EDesignPreviewSizeMode::Custom;
	else if (LowerMode == TEXT("desired"))
		CDO->DesignSizeMode = EDesignPreviewSizeMode::Desired;
	else if (LowerMode == TEXT("desiredonscreen"))
		CDO->DesignSizeMode = EDesignPreviewSizeMode::DesiredOnScreen;

	FBlueprintEditorUtils::MarkBlueprintAsModified(WidgetBP);
	WidgetBP->GetPackage()->MarkPackageDirty();

	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"canvas_size\":\"%.0fx%.0f\",\"message\":\"Canvas size updated.\"}"),
		CDO->DesignTimeSize.X, CDO->DesignTimeSize.Y);
}

void HandleGetWidgetSummary(const FString& WidgetPath, FString& OutJsonString, FString& OutError)
{
	UWidgetBlueprint* WidgetBP = LoadObject<UWidgetBlueprint>(nullptr, *WidgetPath);
	if (!WidgetBP) { OutError = FString::Printf(TEXT("Could not load Widget Blueprint at: %s"), *WidgetPath); return; }

	UUserWidget* SumCDO = WidgetBP->GeneratedClass ? Cast<UUserWidget>(WidgetBP->GeneratedClass->GetDefaultObject()) : nullptr;
	FVector2D SumSize = SumCDO ? SumCDO->DesignTimeSize : FVector2D(100.0f, 100.0f);
	EDesignPreviewSizeMode SumMode = SumCDO ? SumCDO->DesignSizeMode : EDesignPreviewSizeMode::Desired;

	TSharedPtr<FJsonObject> Root = MakeShareable(new FJsonObject);
	Root->SetBoolField(TEXT("success"), true);
	Root->SetStringField(TEXT("canvas_size"), FString::Printf(TEXT("%.0fx%.0f"), SumSize.X, SumSize.Y));

	FString ModeStr;
	if (SumMode == EDesignPreviewSizeMode::FillScreen)           ModeStr = TEXT("FillScreen");
	else if (SumMode == EDesignPreviewSizeMode::Custom)          ModeStr = TEXT("Custom");
	else if (SumMode == EDesignPreviewSizeMode::Desired)         ModeStr = TEXT("Desired");
	else if (SumMode == EDesignPreviewSizeMode::DesiredOnScreen) ModeStr = TEXT("DesiredOnScreen");
	else                                                         ModeStr = TEXT("Unknown");
	Root->SetStringField(TEXT("design_mode"), ModeStr);

	TArray<TSharedPtr<FJsonValue>> WidgetList;

	TFunction<void(UWidget*, const FString&)> Traverse = [&](UWidget* W, const FString& ParentName)
	{
		if (!W) return;
		TSharedPtr<FJsonObject> Entry = MakeShareable(new FJsonObject);
		Entry->SetStringField(TEXT("name"), W->GetFName().ToString());
		Entry->SetStringField(TEXT("type"), W->GetClass()->GetName());
		Entry->SetStringField(TEXT("parent"), ParentName);
		if (W->Slot)
		{
			TSharedPtr<FJsonObject> SlotObj = MakeShareable(new FJsonObject);
			if (UCanvasPanelSlot* CS = Cast<UCanvasPanelSlot>(W->Slot))
			{
				FVector2D P = CS->GetPosition(); FVector2D S = CS->GetSize(); FAnchors A = CS->GetAnchors();
				SlotObj->SetStringField(TEXT("type"), TEXT("Canvas"));
				SlotObj->SetStringField(TEXT("position"), FString::Printf(TEXT("%.0f,%.0f"), P.X, P.Y));
				SlotObj->SetStringField(TEXT("size"), FString::Printf(TEXT("%.0f,%.0f"), S.X, S.Y));
				SlotObj->SetStringField(TEXT("anchors"), FString::Printf(TEXT("%.2f,%.2f,%.2f,%.2f"), A.Minimum.X, A.Minimum.Y, A.Maximum.X, A.Maximum.Y));
				SlotObj->SetNumberField(TEXT("z_order"), CS->GetZOrder());
			}
			else if (UHorizontalBoxSlot* HBS = Cast<UHorizontalBoxSlot>(W->Slot))
			{
				SlotObj->SetStringField(TEXT("type"), TEXT("HorizontalBox"));
				const FMargin& P = HBS->GetPadding();
				SlotObj->SetStringField(TEXT("padding"), FString::Printf(TEXT("%.1f,%.1f,%.1f,%.1f"), P.Left, P.Top, P.Right, P.Bottom));
				SlotObj->SetStringField(TEXT("h_align"), HBS->GetHorizontalAlignment() == HAlign_Fill ? TEXT("Fill") : HBS->GetHorizontalAlignment() == HAlign_Left ? TEXT("Left") : HBS->GetHorizontalAlignment() == HAlign_Center ? TEXT("Center") : TEXT("Right"));
				SlotObj->SetStringField(TEXT("v_align"), HBS->GetVerticalAlignment() == VAlign_Fill ? TEXT("Fill") : HBS->GetVerticalAlignment() == VAlign_Top ? TEXT("Top") : HBS->GetVerticalAlignment() == VAlign_Center ? TEXT("Center") : TEXT("Bottom"));
				const FSlateChildSize& Sz = HBS->GetSize();
				SlotObj->SetStringField(TEXT("size_rule"), Sz.SizeRule == ESlateSizeRule::Fill ? TEXT("Fill") : TEXT("Auto"));
				SlotObj->SetNumberField(TEXT("size_value"), Sz.Value);
			}
			else if (UVerticalBoxSlot* VBS = Cast<UVerticalBoxSlot>(W->Slot))
			{
				SlotObj->SetStringField(TEXT("type"), TEXT("VerticalBox"));
				const FMargin& P = VBS->GetPadding();
				SlotObj->SetStringField(TEXT("padding"), FString::Printf(TEXT("%.1f,%.1f,%.1f,%.1f"), P.Left, P.Top, P.Right, P.Bottom));
				SlotObj->SetStringField(TEXT("h_align"), VBS->GetHorizontalAlignment() == HAlign_Fill ? TEXT("Fill") : VBS->GetHorizontalAlignment() == HAlign_Left ? TEXT("Left") : VBS->GetHorizontalAlignment() == HAlign_Center ? TEXT("Center") : TEXT("Right"));
				SlotObj->SetStringField(TEXT("v_align"), VBS->GetVerticalAlignment() == VAlign_Fill ? TEXT("Fill") : VBS->GetVerticalAlignment() == VAlign_Top ? TEXT("Top") : VBS->GetVerticalAlignment() == VAlign_Center ? TEXT("Center") : TEXT("Bottom"));
				const FSlateChildSize& Sz = VBS->GetSize();
				SlotObj->SetStringField(TEXT("size_rule"), Sz.SizeRule == ESlateSizeRule::Fill ? TEXT("Fill") : TEXT("Auto"));
				SlotObj->SetNumberField(TEXT("size_value"), Sz.Value);
			}
			else if (UOverlaySlot* OS = Cast<UOverlaySlot>(W->Slot))
			{
				SlotObj->SetStringField(TEXT("type"), TEXT("Overlay"));
				const FMargin& P = OS->GetPadding();
				SlotObj->SetStringField(TEXT("padding"), FString::Printf(TEXT("%.1f,%.1f,%.1f,%.1f"), P.Left, P.Top, P.Right, P.Bottom));
				SlotObj->SetStringField(TEXT("h_align"), OS->GetHorizontalAlignment() == HAlign_Fill ? TEXT("Fill") : OS->GetHorizontalAlignment() == HAlign_Left ? TEXT("Left") : OS->GetHorizontalAlignment() == HAlign_Center ? TEXT("Center") : TEXT("Right"));
				SlotObj->SetStringField(TEXT("v_align"), OS->GetVerticalAlignment() == VAlign_Fill ? TEXT("Fill") : OS->GetVerticalAlignment() == VAlign_Top ? TEXT("Top") : OS->GetVerticalAlignment() == VAlign_Center ? TEXT("Center") : TEXT("Bottom"));
			}
			else if (UBorderSlot* BS = Cast<UBorderSlot>(W->Slot))
			{
				SlotObj->SetStringField(TEXT("type"), TEXT("Border"));
				const FMargin& P = BS->GetPadding();
				SlotObj->SetStringField(TEXT("padding"), FString::Printf(TEXT("%.1f,%.1f,%.1f,%.1f"), P.Left, P.Top, P.Right, P.Bottom));
				SlotObj->SetStringField(TEXT("h_align"), BS->GetHorizontalAlignment() == HAlign_Fill ? TEXT("Fill") : BS->GetHorizontalAlignment() == HAlign_Left ? TEXT("Left") : BS->GetHorizontalAlignment() == HAlign_Center ? TEXT("Center") : TEXT("Right"));
				SlotObj->SetStringField(TEXT("v_align"), BS->GetVerticalAlignment() == VAlign_Fill ? TEXT("Fill") : BS->GetVerticalAlignment() == VAlign_Top ? TEXT("Top") : BS->GetVerticalAlignment() == VAlign_Center ? TEXT("Center") : TEXT("Bottom"));
			}
			else if (UScrollBoxSlot* SBS = Cast<UScrollBoxSlot>(W->Slot))
			{
				SlotObj->SetStringField(TEXT("type"), TEXT("ScrollBox"));
				const FMargin& P = SBS->GetPadding();
				SlotObj->SetStringField(TEXT("padding"), FString::Printf(TEXT("%.1f,%.1f,%.1f,%.1f"), P.Left, P.Top, P.Right, P.Bottom));
			}
			else if (UWrapBoxSlot* WBS = Cast<UWrapBoxSlot>(W->Slot))
			{
				SlotObj->SetStringField(TEXT("type"), TEXT("WrapBox"));
				const FMargin& P = WBS->GetPadding();
				SlotObj->SetStringField(TEXT("padding"), FString::Printf(TEXT("%.1f,%.1f,%.1f,%.1f"), P.Left, P.Top, P.Right, P.Bottom));
			}
			else if (UUniformGridSlot* UGS = Cast<UUniformGridSlot>(W->Slot))
			{
				SlotObj->SetStringField(TEXT("type"), TEXT("UniformGrid"));
				SlotObj->SetNumberField(TEXT("row"), UGS->GetRow());
				SlotObj->SetNumberField(TEXT("column"), UGS->GetColumn());
			}
			else if (UGridSlot* GS = Cast<UGridSlot>(W->Slot))
			{
				SlotObj->SetStringField(TEXT("type"), TEXT("Grid"));
				SlotObj->SetNumberField(TEXT("row"), GS->GetRow());
				SlotObj->SetNumberField(TEXT("column"), GS->GetColumn());
				SlotObj->SetNumberField(TEXT("row_span"), GS->GetRowSpan());
				SlotObj->SetNumberField(TEXT("column_span"), GS->GetColumnSpan());
			}
			Entry->SetObjectField(TEXT("slot"), SlotObj);
		}
		WidgetList.Add(MakeShareable(new FJsonValueObject(Entry)));
		if (UPanelWidget* Panel = Cast<UPanelWidget>(W))
			for (int32 i = 0; i < Panel->GetChildrenCount(); ++i)
				Traverse(Panel->GetChildAt(i), W->GetFName().ToString());
	};

	if (WidgetBP->WidgetTree && WidgetBP->WidgetTree->RootWidget)
		Traverse(WidgetBP->WidgetTree->RootWidget, TEXT(""));

	Root->SetArrayField(TEXT("widgets"), WidgetList);
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);
}

void HandleSetWidgetSlot(const FString& WidgetPath, const FString& WidgetName, const TSharedRef<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	UWidgetBlueprint* WidgetBP = LoadObject<UWidgetBlueprint>(nullptr, *WidgetPath);
	if (!WidgetBP || !WidgetBP->WidgetTree) { OutError = FString::Printf(TEXT("Could not load Widget Blueprint at: %s"), *WidgetPath); return; }

	UWidget* W = WidgetBP->WidgetTree->FindWidget(FName(*WidgetName));
	if (!W || !W->Slot) { OutError = FString::Printf(TEXT("Widget '%s' not found or has no slot."), *WidgetName); return; }

	W->Slot->Modify();
	FString SlotType;

	auto ParseMarginArg = [&](const FString& Key) -> FMargin {
		const TArray<TSharedPtr<FJsonValue>>* JArr = nullptr;
		if (Args->TryGetArrayField(*Key, JArr) && JArr)
		{
			TArray<float> N; N.Reserve(JArr->Num());
			for (const TSharedPtr<FJsonValue>& V : *JArr) N.Add((float)V->AsNumber());
			if (N.Num() == 1) return FMargin(N[0]);
			if (N.Num() == 2) return FMargin(N[0], N[1]);
			if (N.Num() >= 4) return FMargin(N[0], N[1], N[2], N[3]);
			return FMargin(0);
		}
		FString Val; Args->TryGetStringField(*Key, Val);
		TArray<FString> P; Val.ParseIntoArray(P, TEXT(","), true);
		if (P.Num() == 1) return FMargin(FCString::Atof(*P[0]));
		if (P.Num() == 2) return FMargin(FCString::Atof(*P[0]), FCString::Atof(*P[1]));
		if (P.Num() >= 4) return FMargin(FCString::Atof(*P[0]), FCString::Atof(*P[1]), FCString::Atof(*P[2]), FCString::Atof(*P[3]));
		return FMargin(0);
	};
	auto ParseHAlign = [](const FString& S) -> EHorizontalAlignment {
		FString L = S.ToLower();
		if (L == TEXT("left")) return HAlign_Left; if (L == TEXT("center")) return HAlign_Center; if (L == TEXT("right")) return HAlign_Right; return HAlign_Fill;
	};
	auto ParseVAlign = [](const FString& S) -> EVerticalAlignment {
		FString L = S.ToLower();
		if (L == TEXT("top")) return VAlign_Top; if (L == TEXT("center")) return VAlign_Center; if (L == TEXT("bottom")) return VAlign_Bottom; return VAlign_Fill;
	};
	auto GetHAlignArg = [&](FString& Out) -> bool {
		return Args->TryGetStringField(TEXT("h_alignment"), Out) || Args->TryGetStringField(TEXT("h_align"), Out) || Args->TryGetStringField(TEXT("horizontal_alignment"), Out);
	};
	auto GetVAlignArg = [&](FString& Out) -> bool {
		return Args->TryGetStringField(TEXT("v_alignment"), Out) || Args->TryGetStringField(TEXT("v_align"), Out) || Args->TryGetStringField(TEXT("vertical_alignment"), Out);
	};
	auto ParseChildSize = [&]() -> TOptional<FSlateChildSize> {
		FSlateChildSize Size;
		bool bAny = false;
		FString RuleStr;
		if (Args->TryGetStringField(TEXT("size_rule"), RuleStr) || Args->TryGetStringField(TEXT("fill_rule"), RuleStr))
		{
			FString L = RuleStr.ToLower();
			Size.SizeRule = (L == TEXT("fill") || L == TEXT("1")) ? ESlateSizeRule::Fill : ESlateSizeRule::Automatic;
			bAny = true;
		}
		double V;
		if (Args->TryGetNumberField(TEXT("size_value"), V) || Args->TryGetNumberField(TEXT("fill_value"), V) || Args->TryGetNumberField(TEXT("fill"), V))
		{
			Size.Value = (float)V;
			if (!bAny) Size.SizeRule = ESlateSizeRule::Fill;
			bAny = true;
		}
		return bAny ? TOptional<FSlateChildSize>(Size) : TOptional<FSlateChildSize>();
	};
	auto ParseVec2D = [](const FString& S) -> FVector2D {
		TArray<FString> P; S.ParseIntoArray(P, TEXT(","), true);
		if (P.Num() >= 2) return FVector2D(FCString::Atof(*P[0].TrimStartAndEnd()), FCString::Atof(*P[1].TrimStartAndEnd()));
		float V = FCString::Atof(*S.TrimStartAndEnd()); return FVector2D(V, V);
	};
	auto ParseAnchor = [](const FString& S) -> FAnchors {
		FString L = S.ToLower();
		if (L == TEXT("fill")) return FAnchors(0,0,1,1);
		if (L == TEXT("center")) return FAnchors(0.5f,0.5f,0.5f,0.5f);
		if (L == TEXT("top_left") || L == TEXT("topleft")) return FAnchors(0,0,0,0);
		if (L == TEXT("top_center") || L == TEXT("topcenter")) return FAnchors(0.5f,0,0.5f,0);
		if (L == TEXT("top_right") || L == TEXT("topright")) return FAnchors(1,0,1,0);
		if (L == TEXT("bottom_left") || L == TEXT("bottomleft")) return FAnchors(0,1,0,1);
		if (L == TEXT("bottom_center") || L == TEXT("bottomcenter")) return FAnchors(0.5f,1,0.5f,1);
		if (L == TEXT("bottom_right") || L == TEXT("bottomright")) return FAnchors(1,1,1,1);
		if (L == TEXT("center_left") || L == TEXT("centerleft")) return FAnchors(0,0.5f,0,0.5f);
		if (L == TEXT("center_right") || L == TEXT("centerright")) return FAnchors(1,0.5f,1,0.5f);
		if (L == TEXT("horizontal_fill") || L == TEXT("hfill")) return FAnchors(0,0,1,0);
		if (L == TEXT("vertical_fill") || L == TEXT("vfill")) return FAnchors(0,0,0,1);
		TArray<FString> P; S.ParseIntoArray(P, TEXT(","), true);
		if (P.Num() >= 4) return FAnchors(FCString::Atof(*P[0]), FCString::Atof(*P[1]), FCString::Atof(*P[2]), FCString::Atof(*P[3]));
		return FAnchors(0,0,0,0);
	};

	auto GetVec2DArg = [&](const TCHAR* Key, FVector2D& Out) -> bool {
		const TArray<TSharedPtr<FJsonValue>>* JArr = nullptr;
		if (Args->TryGetArrayField(Key, JArr) && JArr && JArr->Num() >= 2)
		{
			Out = FVector2D((float)(*JArr)[0]->AsNumber(), (float)(*JArr)[1]->AsNumber());
			return true;
		}
		FString Val;
		if (Args->TryGetStringField(Key, Val)) { Out = ParseVec2D(Val); return true; }
		return false;
	};
	auto GetAnchorArg = [&](FAnchors& Out) -> bool {
		const TArray<TSharedPtr<FJsonValue>>* JArr = nullptr;
		if (Args->TryGetArrayField(TEXT("anchors"), JArr) && JArr)
		{
			if (JArr->Num() == 2)
			{
				const float X = (float)(*JArr)[0]->AsNumber();
				const float Y = (float)(*JArr)[1]->AsNumber();
				Out = FAnchors(X, Y, X, Y);
				return true;
			}
			if (JArr->Num() >= 4)
			{
				Out = FAnchors((float)(*JArr)[0]->AsNumber(), (float)(*JArr)[1]->AsNumber(),
				               (float)(*JArr)[2]->AsNumber(), (float)(*JArr)[3]->AsNumber());
				return true;
			}
		}
		FString Val;
		if (Args->TryGetStringField(TEXT("anchors"), Val)) { Out = ParseAnchor(Val); return true; }
		return false;
	};

	if (UCanvasPanelSlot* CS = Cast<UCanvasPanelSlot>(W->Slot))
	{
		SlotType = TEXT("Canvas");
		FVector2D V2;
		if (GetVec2DArg(TEXT("position"), V2)) CS->SetPosition(V2);
		if (GetVec2DArg(TEXT("size"), V2)) CS->SetSize(V2);
		FAnchors A;
		if (GetAnchorArg(A)) CS->SetAnchors(A);
		if (GetVec2DArg(TEXT("alignment"), V2)) CS->SetAlignment(V2);
		double ZOrderVal; if (Args->TryGetNumberField(TEXT("z_order"), ZOrderVal)) CS->SetZOrder((int32)ZOrderVal);
		bool bAutoSize; if (Args->TryGetBoolField(TEXT("auto_size"), bAutoSize)) CS->SetAutoSize(bAutoSize);
	}
	else if (UHorizontalBoxSlot* HBS = Cast<UHorizontalBoxSlot>(W->Slot))
	{
		SlotType = TEXT("HorizontalBox");
		FString Val;
		if (Args->HasField(TEXT("padding"))) HBS->SetPadding(ParseMarginArg(TEXT("padding")));
		if (GetHAlignArg(Val)) HBS->SetHorizontalAlignment(ParseHAlign(Val));
		if (GetVAlignArg(Val)) HBS->SetVerticalAlignment(ParseVAlign(Val));
		if (TOptional<FSlateChildSize> S = ParseChildSize()) HBS->SetSize(*S);
	}
	else if (UVerticalBoxSlot* VBS = Cast<UVerticalBoxSlot>(W->Slot))
	{
		SlotType = TEXT("VerticalBox");
		FString Val;
		if (Args->HasField(TEXT("padding"))) VBS->SetPadding(ParseMarginArg(TEXT("padding")));
		if (GetHAlignArg(Val)) VBS->SetHorizontalAlignment(ParseHAlign(Val));
		if (GetVAlignArg(Val)) VBS->SetVerticalAlignment(ParseVAlign(Val));
		if (TOptional<FSlateChildSize> S = ParseChildSize()) VBS->SetSize(*S);
	}
	else if (UOverlaySlot* OS = Cast<UOverlaySlot>(W->Slot))
	{
		SlotType = TEXT("Overlay");
		FString Val;
		if (Args->HasField(TEXT("padding"))) OS->SetPadding(ParseMarginArg(TEXT("padding")));
		if (GetHAlignArg(Val)) OS->SetHorizontalAlignment(ParseHAlign(Val));
		if (GetVAlignArg(Val)) OS->SetVerticalAlignment(ParseVAlign(Val));
	}
	else if (UBorderSlot* BS = Cast<UBorderSlot>(W->Slot))
	{
		SlotType = TEXT("Border");
		FString Val;
		if (Args->HasField(TEXT("padding"))) BS->SetPadding(ParseMarginArg(TEXT("padding")));
		if (GetHAlignArg(Val)) BS->SetHorizontalAlignment(ParseHAlign(Val));
		if (GetVAlignArg(Val)) BS->SetVerticalAlignment(ParseVAlign(Val));
	}
	else if (UScrollBoxSlot* SBS = Cast<UScrollBoxSlot>(W->Slot))
	{
		SlotType = TEXT("ScrollBox");
		FString Val;
		if (Args->HasField(TEXT("padding"))) SBS->SetPadding(ParseMarginArg(TEXT("padding")));
		if (GetHAlignArg(Val)) SBS->SetHorizontalAlignment(ParseHAlign(Val));
	}
	else if (UWrapBoxSlot* WBS = Cast<UWrapBoxSlot>(W->Slot))
	{
		SlotType = TEXT("WrapBox");
		FString Val;
		if (Args->HasField(TEXT("padding"))) WBS->SetPadding(ParseMarginArg(TEXT("padding")));
		if (GetHAlignArg(Val)) WBS->SetHorizontalAlignment(ParseHAlign(Val));
		if (GetVAlignArg(Val)) WBS->SetVerticalAlignment(ParseVAlign(Val));
		bool bFillEmpty; if (Args->TryGetBoolField(TEXT("fill_empty_space"), bFillEmpty)) WBS->SetFillEmptySpace(bFillEmpty);
	}
	else if (UUniformGridSlot* UGS = Cast<UUniformGridSlot>(W->Slot))
	{
		SlotType = TEXT("UniformGrid");
		FString Val; double IntVal;
		if (Args->TryGetNumberField(TEXT("row"), IntVal)) UGS->SetRow((int32)IntVal);
		if (Args->TryGetNumberField(TEXT("column"), IntVal)) UGS->SetColumn((int32)IntVal);
		if (GetHAlignArg(Val)) UGS->SetHorizontalAlignment(ParseHAlign(Val));
		if (GetVAlignArg(Val)) UGS->SetVerticalAlignment(ParseVAlign(Val));
	}
	else if (UGridSlot* GS = Cast<UGridSlot>(W->Slot))
	{
		SlotType = TEXT("Grid");
		FString Val; double IntVal;
		if (Args->TryGetNumberField(TEXT("row"), IntVal)) GS->SetRow((int32)IntVal);
		if (Args->TryGetNumberField(TEXT("column"), IntVal)) GS->SetColumn((int32)IntVal);
		if (Args->TryGetNumberField(TEXT("row_span"), IntVal)) GS->SetRowSpan((int32)IntVal);
		if (Args->TryGetNumberField(TEXT("column_span"), IntVal)) GS->SetColumnSpan((int32)IntVal);
		if (Args->HasField(TEXT("padding"))) GS->SetPadding(ParseMarginArg(TEXT("padding")));
		if (GetHAlignArg(Val)) GS->SetHorizontalAlignment(ParseHAlign(Val));
		if (GetVAlignArg(Val)) GS->SetVerticalAlignment(ParseVAlign(Val));
	}
	else
	{
		OutError = FString::Printf(TEXT("Unrecognized slot type on widget '%s' (class %s). Supported: Canvas, HorizontalBox, VerticalBox, Overlay, Border, ScrollBox, WrapBox, UniformGrid, Grid."), *WidgetName, *W->Slot->GetClass()->GetName());
		return;
	}

	FBlueprintEditorUtils::MarkBlueprintAsModified(WidgetBP);
	WidgetBP->GetPackage()->MarkPackageDirty();
	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"slot_type\":\"%s\",\"message\":\"Slot updated.\"}"), *SlotType);
}

void HandleSetImageBrush(const FString& WidgetPath, const FString& WidgetName, const FString& TexturePath, const FString& ImageSize, FString& OutJsonString, FString& OutError)
{
	UWidgetBlueprint* WidgetBP = LoadObject<UWidgetBlueprint>(nullptr, *WidgetPath);
	if (!WidgetBP || !WidgetBP->WidgetTree) { OutError = FString::Printf(TEXT("Could not load Widget Blueprint at: %s"), *WidgetPath); return; }

	UWidget* W = WidgetBP->WidgetTree->FindWidget(FName(*WidgetName));
	UImage* ImageWidget = Cast<UImage>(W);
	if (!ImageWidget) { OutError = FString::Printf(TEXT("Widget '%s' is not a UImage."), *WidgetName); return; }

	UTexture2D* Texture = LoadObject<UTexture2D>(nullptr, *TexturePath);
	if (!Texture) { OutError = FString::Printf(TEXT("Could not load Texture2D at: %s"), *TexturePath); return; }

	ImageWidget->Modify();
	FSlateBrush NewBrush = ImageWidget->GetBrush();
	NewBrush.SetResourceObject(Texture);
	NewBrush.DrawAs = ESlateBrushDrawType::Image;

	if (!ImageSize.IsEmpty())
	{
		TArray<FString> P; ImageSize.ParseIntoArray(P, TEXT(","), true);
		if (P.Num() >= 2) NewBrush.ImageSize = FVector2f(FCString::Atof(*P[0].TrimStartAndEnd()), FCString::Atof(*P[1].TrimStartAndEnd()));
	}
	else
	{
		NewBrush.ImageSize = FVector2f((float)Texture->GetSizeX(), (float)Texture->GetSizeY());
	}
	ImageWidget->SetBrush(NewBrush);

	FBlueprintEditorUtils::MarkBlueprintAsModified(WidgetBP);
	WidgetBP->GetPackage()->MarkPackageDirty();
	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"message\":\"Image brush set on '%s'.\"}"), *WidgetName);
}

void HandleReparentWidget(const FString& WidgetPath, const FString& WidgetName, const FString& NewParentName, FString& OutJsonString, FString& OutError)
{
	UWidgetBlueprint* WidgetBP = Cast<UWidgetBlueprint>(UEditorAssetLibrary::LoadAsset(WidgetPath));
	if (!WidgetBP || !WidgetBP->WidgetTree)
	{ OutError = TEXT("Could not load widget blueprint"); return; }

	UWidget* Widget = WidgetBP->WidgetTree->FindWidget(FName(*WidgetName));
	if (!Widget)
	{ OutError = FString::Printf(TEXT("Widget '%s' not found"), *WidgetName); return; }

	if (Widget == WidgetBP->WidgetTree->RootWidget)
	{ OutError = TEXT("Cannot reparent the root widget"); return; }

	UWidget* NewParentWidget = WidgetBP->WidgetTree->FindWidget(FName(*NewParentName));
	if (!NewParentWidget)
	{ OutError = FString::Printf(TEXT("New parent '%s' not found"), *NewParentName); return; }

	UPanelWidget* NewPanel = Cast<UPanelWidget>(NewParentWidget);
	if (!NewPanel)
	{ OutError = FString::Printf(TEXT("'%s' is not a panel widget — it cannot accept children"), *NewParentName); return; }

	UPanelWidget* OldPanel = Widget->GetParent();
	FString OldParentName = OldPanel ? OldPanel->GetName() : TEXT("none");

	if (OldPanel) { OldPanel->RemoveChild(Widget); }

	NewPanel->AddChild(Widget);

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WidgetBP);
	WidgetBP->GetPackage()->MarkPackageDirty();

	TSharedPtr<FJsonObject> Out = MakeShareable(new FJsonObject());
	Out->SetBoolField(TEXT("success"), true);
	Out->SetStringField(TEXT("widget"), WidgetName);
	Out->SetStringField(TEXT("new_parent"), NewParentName);
	Out->SetStringField(TEXT("old_parent"), OldParentName);
	FJsonSerializer::Serialize(Out.ToSharedRef(), TJsonWriterFactory<>::Create(&OutJsonString));
}

void HandleListWidgetTypes(const FString& Filter, FString& OutJsonString, FString& OutError)
{

	TSharedPtr<FJsonObject> Res = MakeShareable(new FJsonObject());
	Res->SetBoolField(TEXT("success"), true);

	TArray<TSharedPtr<FJsonValue>> TypesArray;
	for (TObjectIterator<UClass> It; It; ++It)
	{
		if (It->IsChildOf(UWidget::StaticClass()) && !It->HasAnyClassFlags(CLASS_Abstract) && It->ClassGeneratedBy == nullptr)
		{
			FString Name = It->GetName();
			if (Filter.IsEmpty() || Name.Contains(Filter, ESearchCase::IgnoreCase))
			{
				TypesArray.Add(MakeShareable(new FJsonValueString(Name)));
			}
		}
	}
	Res->SetArrayField(TEXT("widget_types"), TypesArray);
	Res->SetNumberField(TEXT("count"), TypesArray.Num());

	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Res.ToSharedRef(), W);
}

void HandleAddWidgetAnimation(const FString& WidgetPath, const FString& AnimationName, float Duration, FString& OutJsonString, FString& OutError)
{

	UWidgetBlueprint* WidgetBP = Cast<UWidgetBlueprint>(UEditorAssetLibrary::LoadAsset(WidgetPath));
	if (!WidgetBP) { OutError = FString::Printf(TEXT("Could not load WidgetBlueprint at '%s'"), *WidgetPath); return; }

	UWidgetAnimation* NewAnim = NewObject<UWidgetAnimation>(WidgetBP, FName(*AnimationName), RF_Transactional);
	if (!NewAnim) { OutError = TEXT("Failed to create widget animation"); return; }

	UMovieScene* MovieScene = NewAnim->GetMovieScene();
	if (!MovieScene)
	{
		MovieScene = NewObject<UMovieScene>(NewAnim, FName(*AnimationName), RF_Transactional);
		NewAnim->MovieScene = MovieScene;
	}
	if (MovieScene)
	{
		MovieScene->SetDisplayRate(FFrameRate(30, 1));
		if (Duration > 0.f)
		{
			FFrameRate FrameRate = MovieScene->GetTickResolution();
			FFrameNumber EndFrame = FrameRate.AsFrameNumber(Duration);
			MovieScene->SetPlaybackRange(TRange<FFrameNumber>(FFrameNumber(0), EndFrame));
		}
	}

	WidgetBP->Animations.Add(NewAnim);
	FBlueprintEditorUtils::MarkBlueprintAsModified(WidgetBP);
	WidgetBP->GetPackage()->MarkPackageDirty();

	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"widget_path\":\"%s\",\"animation\":\"%s\",\"duration\":%.2f}"),
		*WidgetPath, *AnimationName, Duration);
}

void HandleRemoveWidgetAnimation(const FString& WidgetPath, const FString& AnimationName, FString& OutJsonString, FString& OutError)
{

	UWidgetBlueprint* WidgetBP = Cast<UWidgetBlueprint>(UEditorAssetLibrary::LoadAsset(WidgetPath));
	if (!WidgetBP) { OutError = FString::Printf(TEXT("Could not load WidgetBlueprint at '%s'"), *WidgetPath); return; }

	int32 FoundIndex = INDEX_NONE;
	for (int32 i = 0; i < WidgetBP->Animations.Num(); ++i)
	{
		if (WidgetBP->Animations[i] && WidgetBP->Animations[i]->GetName().Equals(AnimationName, ESearchCase::IgnoreCase))
		{
			FoundIndex = i;
			break;
		}
	}

	if (FoundIndex == INDEX_NONE)
	{
		OutError = FString::Printf(TEXT("Animation '%s' not found in WidgetBlueprint '%s'"), *AnimationName, *WidgetPath);
		return;
	}

	WidgetBP->Animations.RemoveAt(FoundIndex);
	FBlueprintEditorUtils::MarkBlueprintAsModified(WidgetBP);
	WidgetBP->GetPackage()->MarkPackageDirty();

	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"widget_path\":\"%s\",\"removed_animation\":\"%s\",\"remaining_animations\":%d}"),
		*WidgetPath, *AnimationName, WidgetBP->Animations.Num());
}

void HandleGetWidgetAnimationSummary(const FString& WidgetPath, FString& OutJsonString, FString& OutError)
{

	UWidgetBlueprint* WidgetBP = Cast<UWidgetBlueprint>(UEditorAssetLibrary::LoadAsset(WidgetPath));
	if (!WidgetBP) { OutError = FString::Printf(TEXT("Could not load WidgetBlueprint at '%s'"), *WidgetPath); return; }

	TSharedPtr<FJsonObject> Res = MakeShareable(new FJsonObject());
	Res->SetBoolField(TEXT("success"), true);
	Res->SetStringField(TEXT("widget_path"), WidgetPath);

	TArray<TSharedPtr<FJsonValue>> AnimArray;
	for (UWidgetAnimation* Anim : WidgetBP->Animations)
	{
		if (!Anim) continue;
		TSharedPtr<FJsonObject> AnimObj = MakeShareable(new FJsonObject());
		AnimObj->SetStringField(TEXT("name"), Anim->GetName());

		UMovieScene* MS = Anim->GetMovieScene();
		if (MS)
		{
			FFrameRate TickRes = MS->GetTickResolution();
			TRange<FFrameNumber> Range = MS->GetPlaybackRange();
			double Duration = 0.0;
			if (Range.HasUpperBound() && Range.HasLowerBound())
			{
				Duration = TickRes.AsSeconds(FFrameTime(Range.GetUpperBoundValue() - Range.GetLowerBoundValue()));
			}
			AnimObj->SetNumberField(TEXT("duration"), Duration);

			int32 TotalTracks = 0;
			TArray<TSharedPtr<FJsonValue>> TrackArray;
			for (const FMovieSceneBinding& Binding : const_cast<const UMovieScene*>(MS)->GetBindings())
			{
				for (UMovieSceneTrack* Track : Binding.GetTracks())
				{
					if (!Track) continue;
					TotalTracks++;
					TSharedPtr<FJsonObject> TrackObj = MakeShareable(new FJsonObject());
					TrackObj->SetStringField(TEXT("name"), Track->GetTrackName().ToString());
					TrackObj->SetStringField(TEXT("type"), Track->GetClass()->GetName());
					TrackObj->SetNumberField(TEXT("section_count"), Track->GetAllSections().Num());
					TrackArray.Add(MakeShareable(new FJsonValueObject(TrackObj)));
				}
			}
			AnimObj->SetNumberField(TEXT("track_count"), TotalTracks);
			AnimObj->SetArrayField(TEXT("tracks"), TrackArray);
		}
		AnimArray.Add(MakeShareable(new FJsonValueObject(AnimObj)));
	}
	Res->SetArrayField(TEXT("animations"), AnimArray);
	Res->SetNumberField(TEXT("animation_count"), AnimArray.Num());

	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Res.ToSharedRef(), W);
}

void HandleDuplicateWidget(const FString& WidgetPath, const FString& WidgetName, const FString& NewName, FString& OutJsonString, FString& OutError)
{

	UWidgetBlueprint* WidgetBP = Cast<UWidgetBlueprint>(UEditorAssetLibrary::LoadAsset(WidgetPath));
	if (!WidgetBP) { OutError = FString::Printf(TEXT("Could not load WidgetBlueprint at '%s'"), *WidgetPath); return; }

	UWidgetTree* Tree = WidgetBP->WidgetTree;
	if (!Tree) { OutError = TEXT("WidgetTree is null"); return; }

	UWidget* Source = Tree->FindWidget(FName(*WidgetName));
	if (!Source) { OutError = FString::Printf(TEXT("Widget '%s' not found"), *WidgetName); return; }

	UWidget* Clone = DuplicateObject<UWidget>(Source, Tree, FName(*NewName));
	if (!Clone) { OutError = TEXT("Failed to duplicate widget"); return; }
	Clone->Rename(*NewName, Tree);

	UPanelWidget* Parent = Source->GetParent();
	if (Parent)
	{
		UPanelSlot* Slot = Parent->AddChild(Clone);
		if (!Slot) { OutError = TEXT("Failed to add duplicated widget to parent"); return; }
	}
	else
	{
		OutError = TEXT("Source widget has no parent; cannot place clone");
		return;
	}

	FBlueprintEditorUtils::MarkBlueprintAsModified(WidgetBP);
	WidgetBP->GetPackage()->MarkPackageDirty();

	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"widget_path\":\"%s\",\"source\":\"%s\",\"duplicate\":\"%s\"}"),
		*WidgetPath, *WidgetName, *Clone->GetName());
}

static void RecursiveWidgetHierarchy(UWidget* W, TSharedPtr<FJsonObject>& OutObj)
{
	if (!W) return;
	OutObj->SetStringField(TEXT("name"), W->GetName());
	OutObj->SetStringField(TEXT("type"), W->GetClass()->GetName());
	OutObj->SetStringField(TEXT("visibility"), UEnum::GetValueAsString(W->GetVisibility()));

	UPanelSlot* Slot = W->Slot;
	if (Slot)
	{
		TSharedPtr<FJsonObject> SlotObj = MakeShareable(new FJsonObject());
		SlotObj->SetStringField(TEXT("slot_type"), Slot->GetClass()->GetName());
		if (UCanvasPanelSlot* CS = Cast<UCanvasPanelSlot>(Slot))
		{
			SlotObj->SetStringField(TEXT("position"), FString::Printf(TEXT("(%g,%g)"), CS->GetPosition().X, CS->GetPosition().Y));
			SlotObj->SetStringField(TEXT("size"), FString::Printf(TEXT("(%g,%g)"), CS->GetSize().X, CS->GetSize().Y));
		}
		OutObj->SetObjectField(TEXT("slot"), SlotObj);
	}

	UPanelWidget* Panel = Cast<UPanelWidget>(W);
	if (Panel)
	{
		TArray<TSharedPtr<FJsonValue>> ChildArray;
		for (int32 i = 0; i < Panel->GetChildrenCount(); ++i)
		{
			UWidget* Child = Panel->GetChildAt(i);
			if (!Child) continue;
			TSharedPtr<FJsonObject> ChildObj = MakeShareable(new FJsonObject());
			RecursiveWidgetHierarchy(Child, ChildObj);
			ChildArray.Add(MakeShareable(new FJsonValueObject(ChildObj)));
		}
		OutObj->SetArrayField(TEXT("children"), ChildArray);
	}
}

void HandleGetWidgetHierarchy(const FString& WidgetPath, FString& OutJsonString, FString& OutError)
{

	UWidgetBlueprint* WidgetBP = Cast<UWidgetBlueprint>(UEditorAssetLibrary::LoadAsset(WidgetPath));
	if (!WidgetBP) { OutError = FString::Printf(TEXT("Could not load WidgetBlueprint at '%s'"), *WidgetPath); return; }

	UWidgetTree* Tree = WidgetBP->WidgetTree;
	if (!Tree) { OutError = TEXT("WidgetTree is null"); return; }

	TSharedPtr<FJsonObject> Res = MakeShareable(new FJsonObject());
	Res->SetBoolField(TEXT("success"), true);
	Res->SetStringField(TEXT("widget_path"), WidgetPath);

	UWidget* Root = Tree->RootWidget;
	if (Root)
	{
		TSharedPtr<FJsonObject> RootObj = MakeShareable(new FJsonObject());
		RecursiveWidgetHierarchy(Root, RootObj);
		Res->SetObjectField(TEXT("hierarchy"), RootObj);
	}

	int32 Total = 0;
	Tree->ForEachWidget([&Total](UWidget*) { Total++; });
	Res->SetNumberField(TEXT("total_widgets"), Total);

	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Res.ToSharedRef(), W);
}

void HandleSetWidgetNavigation(const FString& WidgetPath, const FString& WidgetName,
	const FString& Direction, const FString& TargetWidgetName, FString& OutJsonString, FString& OutError)
{

	UWidgetBlueprint* WidgetBP = Cast<UWidgetBlueprint>(UEditorAssetLibrary::LoadAsset(WidgetPath));
	if (!WidgetBP) { OutError = FString::Printf(TEXT("Could not load WidgetBlueprint at '%s'"), *WidgetPath); return; }

	UWidgetTree* Tree = WidgetBP->WidgetTree;
	if (!Tree) { OutError = TEXT("WidgetTree is null"); return; }

	UWidget* Widget = Tree->FindWidget(FName(*WidgetName));
	if (!Widget) { OutError = FString::Printf(TEXT("Widget '%s' not found"), *WidgetName); return; }

	UWidget* Target = Tree->FindWidget(FName(*TargetWidgetName));
	if (!Target) { OutError = FString::Printf(TEXT("Target widget '%s' not found"), *TargetWidgetName); return; }

	FString DirLower = Direction.ToLower();
	FString PropName;
	if (DirLower == TEXT("up")) PropName = TEXT("Up");
	else if (DirLower == TEXT("down")) PropName = TEXT("Down");
	else if (DirLower == TEXT("left")) PropName = TEXT("Left");
	else if (DirLower == TEXT("right")) PropName = TEXT("Right");
	else if (DirLower == TEXT("next")) PropName = TEXT("Next");
	else if (DirLower == TEXT("previous")) PropName = TEXT("Previous");
	else { OutError = FString::Printf(TEXT("Invalid direction '%s'. Use: up/down/left/right/next/previous"), *Direction); return; }

	Widget->SetNavigationRuleExplicit(
		DirLower == TEXT("up") ? EUINavigation::Up :
		DirLower == TEXT("down") ? EUINavigation::Down :
		DirLower == TEXT("left") ? EUINavigation::Left :
		DirLower == TEXT("right") ? EUINavigation::Right :
		DirLower == TEXT("next") ? EUINavigation::Next :
		EUINavigation::Previous,
		Target);

	FBlueprintEditorUtils::MarkBlueprintAsModified(WidgetBP);
	WidgetBP->GetPackage()->MarkPackageDirty();

	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"widget\":\"%s\",\"direction\":\"%s\",\"target\":\"%s\"}"),
		*WidgetName, *Direction, *TargetWidgetName);
}

void HandleGetWidgetProperties(const FString& WidgetPath, const FString& WidgetName, FString& OutJsonString, FString& OutError)
{

	UWidgetBlueprint* WidgetBP = Cast<UWidgetBlueprint>(UEditorAssetLibrary::LoadAsset(WidgetPath));
	if (!WidgetBP) { OutError = FString::Printf(TEXT("Could not load WidgetBlueprint at '%s'"), *WidgetPath); return; }

	UWidgetTree* Tree = WidgetBP->WidgetTree;
	if (!Tree) { OutError = TEXT("WidgetTree is null"); return; }

	UWidget* Widget = Tree->FindWidget(FName(*WidgetName));
	if (!Widget) { OutError = FString::Printf(TEXT("Widget '%s' not found"), *WidgetName); return; }

	TSharedPtr<FJsonObject> Res = MakeShareable(new FJsonObject());
	Res->SetBoolField(TEXT("success"), true);
	Res->SetStringField(TEXT("widget_name"), WidgetName);
	Res->SetStringField(TEXT("widget_type"), Widget->GetClass()->GetName());
	Res->SetStringField(TEXT("visibility"), UEnum::GetValueAsString(Widget->GetVisibility()));

	TSharedPtr<FJsonObject> PropsObj = MakeShareable(new FJsonObject());
	for (TFieldIterator<FProperty> It(Widget->GetClass()); It; ++It)
	{
		FProperty* Prop = *It;
		if (!Prop || !Prop->HasAnyPropertyFlags(CPF_Edit)) continue;

		FString ValueStr;
		const void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Widget);
		Prop->ExportTextItem_Direct(ValueStr, ValuePtr, nullptr, Widget, PPF_None);
		if (!ValueStr.IsEmpty())
		{
			PropsObj->SetStringField(Prop->GetName(), ValueStr);
		}
	}
	Res->SetObjectField(TEXT("properties"), PropsObj);

	if (UPanelSlot* Slot = Widget->Slot)
	{
		TSharedPtr<FJsonObject> SlotObj = MakeShareable(new FJsonObject());
		SlotObj->SetStringField(TEXT("slot_type"), Slot->GetClass()->GetName());
		for (TFieldIterator<FProperty> It(Slot->GetClass()); It; ++It)
		{
			FProperty* Prop = *It;
			if (!Prop || !Prop->HasAnyPropertyFlags(CPF_Edit)) continue;
			FString ValueStr;
			const void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Slot);
			Prop->ExportTextItem_Direct(ValueStr, ValuePtr, nullptr, Slot, PPF_None);
			if (!ValueStr.IsEmpty())
			{
				SlotObj->SetStringField(Prop->GetName(), ValueStr);
			}
		}
		Res->SetObjectField(TEXT("slot"), SlotObj);
	}

	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Res.ToSharedRef(), W);
}

static UWidgetAnimation* FindWidgetAnimation(UWidgetBlueprint* WidgetBP, const FString& AnimName)
{
	for (UWidgetAnimation* Anim : WidgetBP->Animations)
	{
		if (Anim && Anim->GetName().Equals(AnimName, ESearchCase::IgnoreCase))
			return Anim;
	}
	return nullptr;
}

static FGuid FindOrCreateWidgetBinding(UWidgetAnimation* Anim, UWidgetBlueprint* WidgetBP, const FString& WidgetName)
{
	UMovieScene* MS = Anim->GetMovieScene();
	if (!MS) return FGuid();

	for (const FWidgetAnimationBinding& WAB : Anim->AnimationBindings)
	{
		if (WAB.WidgetName.ToString().Equals(WidgetName, ESearchCase::IgnoreCase))
			return WAB.AnimationGuid;
	}

	UWidget* Widget = WidgetBP->WidgetTree ? WidgetBP->WidgetTree->FindWidget(FName(*WidgetName)) : nullptr;
	if (!Widget) return FGuid();

	Anim->Modify();
	MS->Modify();
	FGuid NewGuid = MS->AddPossessable(WidgetName, Widget->GetClass());

	FWidgetAnimationBinding WAB;
	WAB.WidgetName = FName(*WidgetName);
	WAB.SlotWidgetName = NAME_None;
	WAB.AnimationGuid = NewGuid;
	Anim->AnimationBindings.Add(WAB);

	return NewGuid;
}

void HandleAddWidgetAnimationTrack(const FString& WidgetPath, const FString& AnimationName,
	const FString& WidgetName, const FString& PropertyName,
	FString& OutJsonString, FString& OutError)
{

	UWidgetBlueprint* WidgetBP = Cast<UWidgetBlueprint>(UEditorAssetLibrary::LoadAsset(WidgetPath));
	if (!WidgetBP) { OutError = FString::Printf(TEXT("Could not load WidgetBlueprint at '%s'"), *WidgetPath); return; }

	UWidgetAnimation* Anim = FindWidgetAnimation(WidgetBP, AnimationName);
	if (!Anim) { OutError = FString::Printf(TEXT("Animation '%s' not found"), *AnimationName); return; }

	UMovieScene* MS = Anim->GetMovieScene();
	if (!MS) { OutError = TEXT("Animation has no MovieScene"); return; }

	FGuid BindingGuid = FindOrCreateWidgetBinding(Anim, WidgetBP, WidgetName);
	if (!BindingGuid.IsValid()) { OutError = FString::Printf(TEXT("Widget '%s' not found in WidgetTree"), *WidgetName); return; }

	UMovieSceneFloatTrack* Track = MS->AddTrack<UMovieSceneFloatTrack>(BindingGuid);
	if (!Track) { OutError = TEXT("Failed to create property track"); return; }

	FString PropPath = PropertyName;
	FString PropLower = PropertyName.ToLower();
	if (PropLower == TEXT("opacity") || PropLower == TEXT("renderopacity"))
		PropPath = TEXT("RenderOpacity");
	else if (PropLower == TEXT("translation_x") || PropLower == TEXT("translatex"))
		PropPath = TEXT("RenderTransform.Translation.X");
	else if (PropLower == TEXT("translation_y") || PropLower == TEXT("translatey"))
		PropPath = TEXT("RenderTransform.Translation.Y");
	else if (PropLower == TEXT("scale_x") || PropLower == TEXT("scalex"))
		PropPath = TEXT("RenderTransform.Scale.X");
	else if (PropLower == TEXT("scale_y") || PropLower == TEXT("scaley"))
		PropPath = TEXT("RenderTransform.Scale.Y");
	else if (PropLower == TEXT("angle") || PropLower == TEXT("rotation"))
		PropPath = TEXT("RenderTransform.Angle");
	else if (PropLower == TEXT("color_r"))
		PropPath = TEXT("ColorAndOpacity.R");
	else if (PropLower == TEXT("color_g"))
		PropPath = TEXT("ColorAndOpacity.G");
	else if (PropLower == TEXT("color_b"))
		PropPath = TEXT("ColorAndOpacity.B");
	else if (PropLower == TEXT("color_a"))
		PropPath = TEXT("ColorAndOpacity.A");

	Track->SetPropertyNameAndPath(FName(*PropPath), PropPath);

	UMovieSceneSection* Section = Track->CreateNewSection();
	if (Section)
	{
		Section->SetRange(MS->GetPlaybackRange());
		Track->AddSection(*Section);
	}

	FBlueprintEditorUtils::MarkBlueprintAsModified(WidgetBP);
	WidgetBP->GetPackage()->MarkPackageDirty();

	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"animation\":\"%s\",\"widget\":\"%s\",\"property\":\"%s\"}"),
		*AnimationName, *WidgetName, *PropPath);
}

void HandleAddWidgetAnimationKeyframe(const FString& WidgetPath, const FString& AnimationName,
	const FString& WidgetName, const FString& PropertyName,
	float Time, float Value, const FString& InterpMode,
	FString& OutJsonString, FString& OutError)
{

	UWidgetBlueprint* WidgetBP = Cast<UWidgetBlueprint>(UEditorAssetLibrary::LoadAsset(WidgetPath));
	if (!WidgetBP) { OutError = FString::Printf(TEXT("Could not load WidgetBlueprint at '%s'"), *WidgetPath); return; }

	UWidgetAnimation* Anim = FindWidgetAnimation(WidgetBP, AnimationName);
	if (!Anim) { OutError = FString::Printf(TEXT("Animation '%s' not found"), *AnimationName); return; }

	UMovieScene* MS = Anim->GetMovieScene();
	if (!MS) { OutError = TEXT("Animation has no MovieScene"); return; }

	FGuid BindingGuid;
	for (const FWidgetAnimationBinding& WAB : Anim->AnimationBindings)
	{
		if (WAB.WidgetName.ToString().Equals(WidgetName, ESearchCase::IgnoreCase))
		{
			BindingGuid = WAB.AnimationGuid;
			break;
		}
	}
	if (!BindingGuid.IsValid())
	{
		BindingGuid = FindOrCreateWidgetBinding(Anim, WidgetBP, WidgetName);
		if (!BindingGuid.IsValid()) { OutError = FString::Printf(TEXT("Widget '%s' not bound in animation"), *WidgetName); return; }
	}

	UMovieSceneFloatTrack* TargetTrack = nullptr;
	FString PropLower = PropertyName.ToLower();
	for (UMovieSceneTrack* Track : MS->FindTracks(UMovieSceneFloatTrack::StaticClass(), BindingGuid))
	{
		UMovieSceneFloatTrack* FTrack = Cast<UMovieSceneFloatTrack>(Track);
		if (FTrack)
		{
			FString TrackProp = FTrack->GetPropertyName().ToString().ToLower();
			if (TrackProp.Contains(PropLower) || PropLower.Contains(TrackProp))
			{
				TargetTrack = FTrack;
				break;
			}
		}
	}

	if (!TargetTrack)
	{
		OutError = FString::Printf(TEXT("No track found for property '%s' on widget '%s'. Call add_widget_animation_track first."), *PropertyName, *WidgetName);
		return;
	}

	if (TargetTrack->GetAllSections().IsEmpty())
	{
		UMovieSceneSection* Section = TargetTrack->CreateNewSection();
		if (Section)
		{
			Section->SetRange(MS->GetPlaybackRange());
			TargetTrack->AddSection(*Section);
		}
	}

	FFrameRate TickRes = MS->GetTickResolution();
	FFrameNumber Frame = TickRes.AsFrameNumber(Time);

	for (UMovieSceneSection* Section : TargetTrack->GetAllSections())
	{
		TArrayView<FMovieSceneFloatChannel*> Channels = Section->GetChannelProxy().GetChannels<FMovieSceneFloatChannel>();
		if (Channels.Num() > 0)
		{
			ERichCurveInterpMode Interp = RCIM_Cubic;
			FString InterpLower = InterpMode.ToLower();
			if (InterpLower == TEXT("linear")) Interp = RCIM_Linear;
			else if (InterpLower == TEXT("constant") || InterpLower == TEXT("step")) Interp = RCIM_Constant;

			Channels[0]->AddCubicKey(Frame, Value);

			{
				TMovieSceneChannelData<FMovieSceneFloatValue> ChData = Channels[0]->GetData();
				int32 KeyIdx = INDEX_NONE;
				for (int32 k = 0; k < ChData.GetTimes().Num(); ++k)
				{
					if (FMath::Abs(ChData.GetTimes()[k].Value - Frame.Value) <= 1)
					{ KeyIdx = k; break; }
				}
				if (KeyIdx != INDEX_NONE)
				{
					ChData.GetValues()[KeyIdx].InterpMode = Interp;
					ChData.GetValues()[KeyIdx].TangentMode = RCTM_Auto;
				}
			}

			FBlueprintEditorUtils::MarkBlueprintAsModified(WidgetBP);
			WidgetBP->GetPackage()->MarkPackageDirty();

			OutJsonString = FString::Printf(TEXT("{\"success\":true,\"animation\":\"%s\",\"widget\":\"%s\",\"property\":\"%s\",\"time\":%.3f,\"value\":%.3f}"),
				*AnimationName, *WidgetName, *PropertyName, Time, Value);
			return;
		}
	}

	OutError = TEXT("No float channels found on the track section");
}

void HandleEditWidgetPropertyFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString WidgetPath;
	Args->TryGetStringField(TEXT("widget_path"), WidgetPath);
	if (WidgetPath.IsEmpty()) Args->TryGetStringField(TEXT("user_widget_path"), WidgetPath);

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	bool bHasBatch = BatchToolHelper::TryGetBatchItems(Args, TEXT("properties"), ItemsArray);
	if (!bHasBatch && Args->TryGetArrayField(TEXT("edits"), ItemsArray) && ItemsArray && ItemsArray->Num() > 0)
		bHasBatch = true;
	if (bHasBatch)
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString WN = BatchToolHelper::GetItemString(Item, TEXT("widget_name"), TEXT("name"));
			FString PN = BatchToolHelper::GetItemString(Item, TEXT("property_name"), TEXT("property"));
			FString Val = BatchToolHelper::GetItemString(Item, TEXT("value"));
			if (WN.IsEmpty() || PN.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing widget_name or property_name")); continue; }
			FString ItemErr, ItemApplied;
			HandleEditWidgetProperty(WidgetPath, WN, PN, Val, ItemErr, &ItemApplied);
			if (ItemErr.IsEmpty())
			{
				auto Extra = MakeShared<FJsonObject>(); Extra->SetStringField(TEXT("widget_name"), WN); Extra->SetStringField(TEXT("property"), PN);
				Extra->SetStringField(TEXT("applied_value"), ItemApplied);
				Batch.AddSuccess(i, Extra);
			}
			else Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJsonString);
		return;
	}
	FString WN, PN, Val;
	Args->TryGetStringField(TEXT("widget_name"), WN);
	Args->TryGetStringField(TEXT("property_name"), PN);
	Args->TryGetStringField(TEXT("value"), Val);
	FString Applied;
	HandleEditWidgetProperty(WidgetPath, WN, PN, Val, OutError, &Applied);
	if (OutError.IsEmpty())
	{
		Applied.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
		Applied.ReplaceInline(TEXT("\""), TEXT("\\\""));
		OutJsonString = FString::Printf(TEXT("{\"success\":true,\"widget_name\":\"%s\",\"property\":\"%s\",\"applied_value\":\"%s\"}"), *WN, *PN, *Applied);
	}
}

static FString GetWidgetPathFromArgs(const TSharedPtr<FJsonObject>& Args)
{
	FString Path;
	Args->TryGetStringField(TEXT("widget_path"), Path);
	if (Path.IsEmpty()) Args->TryGetStringField(TEXT("user_widget_path"), Path);
	if (Path.IsEmpty()) Args->TryGetStringField(TEXT("blueprint_path"), Path);
	return Path;
}

static FString BuildSuccessJson(const TSharedPtr<FJsonObject>& Obj)
{
	FString Out;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Obj.ToSharedRef(), Writer);
	return Out;
}

void HandleCreateWidgetBlueprintFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	TSharedPtr<FJsonObject> Redirected = MakeShareable(new FJsonObject);
	FString Name, SavePath;
	Args->TryGetStringField(TEXT("name"), Name);
	Args->TryGetStringField(TEXT("save_path"), SavePath);
	if (SavePath.IsEmpty()) Args->TryGetStringField(TEXT("path"), SavePath);
	Redirected->SetStringField(TEXT("asset_type"),   TEXT("Blueprint"));
	Redirected->SetStringField(TEXT("name"),         Name);
	Redirected->SetStringField(TEXT("parent_class"), TEXT("UserWidget"));
	if (!SavePath.IsEmpty()) Redirected->SetStringField(TEXT("save_path"), SavePath);

	if (!IUECPCoreModule::IsAvailable())
	{
		OutError = TEXT("UECPCore unavailable — cannot create widget blueprint.");
		return;
	}
	const FUECPToolResult Result = IUECPCoreModule::Get().GetToolDispatcher()
		.ExecuteFromArgs(TEXT("create_asset"), Redirected);
	OutJsonString = Result.ResultJson;
	OutError = Result.ErrorMessage;
}

void HandleAddWidgetToUserWidgetFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	const FString WidgetPath = GetWidgetPathFromArgs(Args);

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (Args->TryGetArrayField(TEXT("items"), ItemsArray) && ItemsArray && ItemsArray->Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> Results;
		int32 SuccessCount = 0, FailCount = 0;
		for (int32 i = 0; i < ItemsArray->Num(); ++i)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i].IsValid() ? (*ItemsArray)[i]->AsObject() : nullptr;
			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetNumberField(TEXT("index"), i);
			if (!Item.IsValid())
			{
				Entry->SetBoolField(TEXT("success"), false);
				Entry->SetStringField(TEXT("error"), TEXT("Invalid item (expected object)"));
				++FailCount;
				Results.Add(MakeShareable(new FJsonValueObject(Entry)));
				continue;
			}
			FString WT, WN, PN;
			Item->TryGetStringField(TEXT("widget_type"), WT);
			Item->TryGetStringField(TEXT("widget_name"), WN);
			if (!Item->TryGetStringField(TEXT("parent_name"), PN) || PN.IsEmpty())
				Item->TryGetStringField(TEXT("parent_widget_name"), PN);

			FString NewName, ItemErr;
			HandleAddWidgetToUserWidget(WidgetPath, WT, WN, PN, NewName, ItemErr);
			if (ItemErr.IsEmpty())
			{
				Entry->SetBoolField(TEXT("success"), true);
				Entry->SetStringField(TEXT("widget_name"), NewName);
				++SuccessCount;
			}
			else
			{
				Entry->SetBoolField(TEXT("success"), false);
				Entry->SetStringField(TEXT("error"), ItemErr);
				++FailCount;
			}
			Results.Add(MakeShareable(new FJsonValueObject(Entry)));
		}
		TSharedPtr<FJsonObject> Root = MakeShareable(new FJsonObject);
		Root->SetBoolField(TEXT("success"), FailCount == 0);
		Root->SetNumberField(TEXT("added"), SuccessCount);
		Root->SetNumberField(TEXT("failed"), FailCount);
		Root->SetArrayField(TEXT("results"), Results);
		OutJsonString = BuildSuccessJson(Root);
		return;
	}

	FString WidgetType, WidgetName, ParentName;
	Args->TryGetStringField(TEXT("widget_type"), WidgetType);
	Args->TryGetStringField(TEXT("widget_name"), WidgetName);
	if (!Args->TryGetStringField(TEXT("parent_name"), ParentName) || ParentName.IsEmpty())
		Args->TryGetStringField(TEXT("parent_widget_name"), ParentName);

	FString NewName;
	HandleAddWidgetToUserWidget(WidgetPath, WidgetType, WidgetName, ParentName, NewName, OutError);
	if (!OutError.IsEmpty()) return;

	TSharedPtr<FJsonObject> Root = MakeShareable(new FJsonObject);
	Root->SetBoolField(TEXT("success"), true);
	Root->SetStringField(TEXT("widget_name"), NewName);
	Root->SetStringField(TEXT("message"), FString::Printf(TEXT("Added widget '%s' to '%s'."), *NewName, *WidgetPath));
	OutJsonString = BuildSuccessJson(Root);
}

void HandleGetBatchWidgetPropertiesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	const TArray<TSharedPtr<FJsonValue>>* ClassesArray = nullptr;
	if (!Args->TryGetArrayField(TEXT("widget_classes"), ClassesArray))
	{
		OutError = TEXT("Missing required parameter: widget_classes (array of class names)");
		return;
	}
	TArray<FString> ClassNames;
	for (const TSharedPtr<FJsonValue>& V : *ClassesArray)
	{
		if (V.IsValid()) ClassNames.Add(V->AsString());
	}
	HandleGetBatchWidgetProperties(ClassNames, OutJsonString, OutError);
}

void HandleAddWidgetsToLayoutFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	const FString WidgetPath = GetWidgetPathFromArgs(Args);
	if (WidgetPath.IsEmpty()) { OutError = TEXT("Missing required parameter: widget_path"); return; }

	const TArray<TSharedPtr<FJsonValue>>* LayoutArray = nullptr;
	if (!Args->TryGetArrayField(TEXT("layout_definition"), LayoutArray)
		&& !Args->TryGetArrayField(TEXT("layout"), LayoutArray))
	{
		OutError = TEXT("Missing required parameter: layout_definition (array of widget definitions)");
		return;
	}
	HandleAddWidgetsToLayout(WidgetPath, *LayoutArray, OutError);
	if (!OutError.IsEmpty()) return;

	TSharedPtr<FJsonObject> Root = MakeShareable(new FJsonObject);
	Root->SetBoolField(TEXT("success"), true);
	Root->SetNumberField(TEXT("widgets_added"), LayoutArray->Num());
	Root->SetStringField(TEXT("message"), FString::Printf(TEXT("Added %d widgets to '%s'."), LayoutArray->Num(), *WidgetPath));
	OutJsonString = BuildSuccessJson(Root);
}

void HandleCreateWidgetFromLayoutFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	const FString WidgetPath = GetWidgetPathFromArgs(Args);
	if (WidgetPath.IsEmpty()) { OutError = TEXT("Missing required parameter: widget_path"); return; }

	const TArray<TSharedPtr<FJsonValue>>* LayoutArrayPtr = nullptr;
	const TSharedPtr<FJsonObject>* LayoutObject = nullptr;
	TArray<TSharedPtr<FJsonValue>> LocalLayout;

	if (Args->TryGetArrayField(TEXT("layout"), LayoutArrayPtr)
		|| Args->TryGetArrayField(TEXT("layout_definition"), LayoutArrayPtr))
	{
		LocalLayout = *LayoutArrayPtr;
	}
	else if (Args->TryGetObjectField(TEXT("layout"), LayoutObject)
		|| Args->TryGetObjectField(TEXT("layout_definition"), LayoutObject))
	{
		LocalLayout.Add(MakeShareable(new FJsonValueObject(*LayoutObject)));
	}
	else
	{
		FString LayoutString;
		if (Args->TryGetStringField(TEXT("layout"), LayoutString) ||
			Args->TryGetStringField(TEXT("layout_definition"), LayoutString))
		{
			LayoutString.TrimStartAndEndInline();
			if (LayoutString.StartsWith(TEXT("[")))
			{
				TArray<TSharedPtr<FJsonValue>> Parsed;
				TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(LayoutString);
				if (FJsonSerializer::Deserialize(Reader, Parsed))
				{
					LocalLayout = MoveTemp(Parsed);
				}
			}
			else if (LayoutString.StartsWith(TEXT("{")))
			{
				TSharedPtr<FJsonObject> ParsedObj;
				TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(LayoutString);
				if (FJsonSerializer::Deserialize(Reader, ParsedObj) && ParsedObj.IsValid())
				{
					LocalLayout.Add(MakeShareable(new FJsonValueObject(ParsedObj)));
				}
			}
		}
		if (LocalLayout.Num() == 0)
		{
			OutError = TEXT("Missing required parameter: layout (array or object of widget definitions). Layout may also be passed as a JSON-stringified array — ensure the string parses to a JSON array or object.");
			return;
		}
	}

	{
		int32 RootCount = 0;
		bool bHasCanvasRoot = false;
		for (const TSharedPtr<FJsonValue>& V : LocalLayout)
		{
			TSharedPtr<FJsonObject> WDef = V.IsValid() ? V->AsObject() : nullptr;
			if (!WDef.IsValid()) continue;
			FString PN;
			if (!WDef->TryGetStringField(TEXT("parent_name"), PN))
				WDef->TryGetStringField(TEXT("parent"), PN);
			if (PN.IsEmpty())
			{
				++RootCount;
				FString WT;
				if (!WDef->TryGetStringField(TEXT("type"), WT))
					WDef->TryGetStringField(TEXT("widget_type"), WT);
				if (WT.ToLower().Contains(TEXT("canvaspanel"))) bHasCanvasRoot = true;
			}
		}
		if (RootCount > 1 && !bHasCanvasRoot)
		{
			TSharedPtr<FJsonObject> AutoRoot = MakeShared<FJsonObject>();
			AutoRoot->SetStringField(TEXT("widget_type"), TEXT("CanvasPanel"));
			AutoRoot->SetStringField(TEXT("widget_name"), TEXT("AutoRoot"));
			for (const TSharedPtr<FJsonValue>& V : LocalLayout)
			{
				TSharedPtr<FJsonObject> WDef = V.IsValid() ? V->AsObject() : nullptr;
				if (!WDef.IsValid()) continue;
				FString PN;
				if (!WDef->TryGetStringField(TEXT("parent_name"), PN))
					WDef->TryGetStringField(TEXT("parent"), PN);
				if (PN.IsEmpty()) WDef->SetStringField(TEXT("parent_name"), TEXT("AutoRoot"));
			}
			LocalLayout.Insert(MakeShareable(new FJsonValueObject(AutoRoot)), 0);
		}
	}

	UObject* Existing = UEditorAssetLibrary::LoadAsset(WidgetPath);
	if (!Existing)
	{
		FString CleanPath = WidgetPath;
		int32 ObjDot;
		if (CleanPath.FindChar('.', ObjDot)) CleanPath = CleanPath.Left(ObjDot);

		FString WName;
		int32 LastSlash;
		if (CleanPath.FindLastChar('/', LastSlash))
			WName = CleanPath.RightChop(LastSlash + 1);
		else
			WName = CleanPath;

		UPackage* Pkg = CreatePackage(*CleanPath);
		if (FindObject<UBlueprint>(Pkg, *WName))
		{
			Existing = UEditorAssetLibrary::LoadAsset(WidgetPath);
		}
		else
		{
			UBlueprint* NewBP = FKismetEditorUtilities::CreateBlueprint(
				UUserWidget::StaticClass(),
				Pkg,
				FName(*WName),
				BPTYPE_Normal,
				UWidgetBlueprint::StaticClass(),
				UWidgetBlueprintGeneratedClass::StaticClass());
			if (NewBP)
			{
				FAssetRegistryModule::AssetCreated(NewBP);
			}
		}
	}

	HandleCreateWidgetFromLayout(WidgetPath, LocalLayout, OutError);
	if (!OutError.IsEmpty()) return;

	UWidgetBlueprint* WidgetBP = Cast<UWidgetBlueprint>(UEditorAssetLibrary::LoadAsset(WidgetPath));
	if (WidgetBP && !WidgetBP->GeneratedClass)
	{
		FKismetEditorUtilities::CompileBlueprint(WidgetBP);
	}
	UUserWidget* CDO = (WidgetBP && WidgetBP->GeneratedClass)
		? Cast<UUserWidget>(WidgetBP->GeneratedClass->GetDefaultObject())
		: nullptr;
	if (CDO)
	{
		FString CanvasSizeStr;
		if (Args->TryGetStringField(TEXT("canvas_size"), CanvasSizeStr) && !CanvasSizeStr.IsEmpty())
		{
			CDO->Modify();
			CDO->DesignTimeSize = ParseVec2_WT(CanvasSizeStr);
			CDO->DesignSizeMode = EDesignPreviewSizeMode::Custom;
		}
		FString DesignModeStr;
		if (Args->TryGetStringField(TEXT("design_mode"), DesignModeStr) && !DesignModeStr.IsEmpty())
		{
			CDO->Modify();
			const FString L = DesignModeStr.ToLower();
			if (L == TEXT("fillscreen") || L == TEXT("fill"))      CDO->DesignSizeMode = EDesignPreviewSizeMode::FillScreen;
			else if (L == TEXT("custom"))                          CDO->DesignSizeMode = EDesignPreviewSizeMode::Custom;
			else if (L == TEXT("desired"))                         CDO->DesignSizeMode = EDesignPreviewSizeMode::Desired;
			else if (L == TEXT("desiredonscreen"))                 CDO->DesignSizeMode = EDesignPreviewSizeMode::DesiredOnScreen;
		}
		if (WidgetBP)
		{
			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WidgetBP);
			WidgetBP->GetPackage()->MarkPackageDirty();
		}
	}

	TSharedPtr<FJsonObject> Root = MakeShareable(new FJsonObject);
	Root->SetBoolField(TEXT("success"), true);
	Root->SetNumberField(TEXT("widgets_created"), LocalLayout.Num());
	Root->SetStringField(TEXT("message"), FString::Printf(TEXT("Created widget layout with %d widgets."), LocalLayout.Num()));
	OutJsonString = BuildSuccessJson(Root);
}

void HandleSetWidgetCanvasSizeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	const FString WidgetPath = GetWidgetPathFromArgs(Args);
	FString CanvasSize, DesignMode;
	Args->TryGetStringField(TEXT("canvas_size"), CanvasSize);
	Args->TryGetStringField(TEXT("design_mode"), DesignMode);
	HandleSetWidgetCanvasSize(WidgetPath, CanvasSize, DesignMode, OutJsonString, OutError);
}

void HandleGetWidgetSummaryFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	const FString WidgetPath = GetWidgetPathFromArgs(Args);
	HandleGetWidgetSummary(WidgetPath, OutJsonString, OutError);
}

void HandleSetWidgetSlotFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	const FString WidgetPath = GetWidgetPathFromArgs(Args);
	FString WidgetName;
	Args->TryGetStringField(TEXT("widget_name"), WidgetName);
	if (WidgetPath.IsEmpty() || WidgetName.IsEmpty())
	{
		OutError = TEXT("Missing required parameters: widget_path, widget_name");
		return;
	}
	HandleSetWidgetSlot(WidgetPath, WidgetName, Args.ToSharedRef(), OutJsonString, OutError);
}

void HandleSetImageBrushFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	const FString WidgetPath = GetWidgetPathFromArgs(Args);
	FString WidgetName, TexturePath, ImageSize;
	Args->TryGetStringField(TEXT("widget_name"), WidgetName);
	Args->TryGetStringField(TEXT("texture_path"), TexturePath);
	Args->TryGetStringField(TEXT("image_size"), ImageSize);
	HandleSetImageBrush(WidgetPath, WidgetName, TexturePath, ImageSize, OutJsonString, OutError);
}

void HandleDeleteWidgetFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	const FString WidgetPath = GetWidgetPathFromArgs(Args);
	FString WidgetName;
	Args->TryGetStringField(TEXT("widget_name"), WidgetName);
	if (WidgetPath.IsEmpty() || WidgetName.IsEmpty())
	{
		OutError = TEXT("Missing required parameters: widget_path, widget_name");
		return;
	}
	HandleDeleteWidget(WidgetPath, WidgetName, OutError);
	if (!OutError.IsEmpty()) return;
	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"message\":\"Deleted widget '%s' from '%s'.\"}"),
		*WidgetName, *WidgetPath);
}

void HandleReparentWidgetFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	const FString WidgetPath = GetWidgetPathFromArgs(Args);
	FString WidgetName, NewParent;
	Args->TryGetStringField(TEXT("widget_name"), WidgetName);
	Args->TryGetStringField(TEXT("new_parent_name"), NewParent);
	HandleReparentWidget(WidgetPath, WidgetName, NewParent, OutJsonString, OutError);
}

void HandleListWidgetTypesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString Filter;
	Args->TryGetStringField(TEXT("filter"), Filter);
	HandleListWidgetTypes(Filter, OutJsonString, OutError);
}

void HandleAddWidgetAnimationFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	const FString WidgetPath = GetWidgetPathFromArgs(Args);
	FString AnimName;
	double Duration = 1.0;
	Args->TryGetStringField(TEXT("animation_name"), AnimName);
	Args->TryGetNumberField(TEXT("duration"), Duration);
	HandleAddWidgetAnimation(WidgetPath, AnimName, (float)Duration, OutJsonString, OutError);
}

void HandleRemoveWidgetAnimationFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	const FString WidgetPath = GetWidgetPathFromArgs(Args);
	FString AnimName;
	Args->TryGetStringField(TEXT("animation_name"), AnimName);
	HandleRemoveWidgetAnimation(WidgetPath, AnimName, OutJsonString, OutError);
}

void HandleGetWidgetAnimationSummaryFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	const FString WidgetPath = GetWidgetPathFromArgs(Args);
	HandleGetWidgetAnimationSummary(WidgetPath, OutJsonString, OutError);
}

void HandleDuplicateWidgetFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	const FString WidgetPath = GetWidgetPathFromArgs(Args);
	FString WidgetName, NewName;
	Args->TryGetStringField(TEXT("widget_name"), WidgetName);
	Args->TryGetStringField(TEXT("new_name"), NewName);
	HandleDuplicateWidget(WidgetPath, WidgetName, NewName, OutJsonString, OutError);
}

void HandleRenameWidgetFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	const FString WidgetPath = GetWidgetPathFromArgs(Args);
	FString WidgetName, NewName;
	Args->TryGetStringField(TEXT("widget_name"), WidgetName);
	if (!Args->TryGetStringField(TEXT("new_name"), NewName))
		Args->TryGetStringField(TEXT("new_widget_name"), NewName);

	if (WidgetPath.IsEmpty()) { OutError = TEXT("widget_path required"); return; }
	if (WidgetName.IsEmpty()) { OutError = TEXT("widget_name required"); return; }
	if (NewName.IsEmpty())    { OutError = TEXT("new_name required");    return; }
	if (WidgetName.Equals(NewName, ESearchCase::CaseSensitive))
	{
		OutJsonString = FString::Printf(TEXT("{\"success\":true,\"noop\":true,\"reason\":\"name unchanged\"}"));
		return;
	}

	UWidgetBlueprint* WidgetBP = Cast<UWidgetBlueprint>(UEditorAssetLibrary::LoadAsset(WidgetPath));
	if (!WidgetBP) { OutError = FString::Printf(TEXT("Could not load WidgetBlueprint at '%s'"), *WidgetPath); return; }

	UWidgetTree* Tree = WidgetBP->WidgetTree;
	if (!Tree) { OutError = TEXT("WidgetTree is null"); return; }

	UWidget* Target = Tree->FindWidget(FName(*WidgetName));
	if (!Target) { OutError = FString::Printf(TEXT("Widget '%s' not found"), *WidgetName); return; }

	if (Tree->FindWidget(FName(*NewName)) != nullptr)
	{
		OutError = FString::Printf(TEXT("A widget named '%s' already exists in this tree — pick a different name."), *NewName);
		return;
	}

	if (!Target->Rename(*NewName, Tree, REN_DontCreateRedirectors))
	{
		OutError = FString::Printf(TEXT("Rename failed (UE rejected '%s' → '%s')"), *WidgetName, *NewName);
		return;
	}

	FBlueprintEditorUtils::MarkBlueprintAsModified(WidgetBP);
	WidgetBP->GetPackage()->MarkPackageDirty();

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"widget_path\":\"%s\",\"old_name\":\"%s\",\"new_name\":\"%s\"}"),
		*WidgetPath, *WidgetName, *Target->GetName());
}

void HandleGetWidgetHierarchyFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	const FString WidgetPath = GetWidgetPathFromArgs(Args);
	HandleGetWidgetHierarchy(WidgetPath, OutJsonString, OutError);
}

void HandleSetWidgetNavigationFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	const FString WidgetPath = GetWidgetPathFromArgs(Args);
	FString WidgetName, Direction, Target;
	Args->TryGetStringField(TEXT("widget_name"), WidgetName);
	Args->TryGetStringField(TEXT("direction"), Direction);
	Args->TryGetStringField(TEXT("target_widget"), Target);
	HandleSetWidgetNavigation(WidgetPath, WidgetName, Direction, Target, OutJsonString, OutError);
}

void HandleGetWidgetPropertiesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	const FString WidgetPath = GetWidgetPathFromArgs(Args);
	FString WidgetName;
	Args->TryGetStringField(TEXT("widget_name"), WidgetName);
	HandleGetWidgetProperties(WidgetPath, WidgetName, OutJsonString, OutError);
}

void HandleAddWidgetAnimationTrackFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	const FString WidgetPath = GetWidgetPathFromArgs(Args);
	FString AnimName, WidgetName, PropName;
	Args->TryGetStringField(TEXT("animation_name"), AnimName);
	Args->TryGetStringField(TEXT("widget_name"), WidgetName);
	Args->TryGetStringField(TEXT("property_name"), PropName);
	HandleAddWidgetAnimationTrack(WidgetPath, AnimName, WidgetName, PropName, OutJsonString, OutError);
}

void HandleAddWidgetAnimationKeyframeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	const FString WidgetPath = GetWidgetPathFromArgs(Args);
	FString AnimName, WidgetName, PropName, InterpMode;
	double Time = 0.0, Value = 0.0;
	Args->TryGetStringField(TEXT("animation_name"), AnimName);
	Args->TryGetStringField(TEXT("widget_name"), WidgetName);
	Args->TryGetStringField(TEXT("property_name"), PropName);
	Args->TryGetNumberField(TEXT("time"), Time);
	Args->TryGetNumberField(TEXT("value"), Value);
	Args->TryGetStringField(TEXT("interp_mode"), InterpMode);
	HandleAddWidgetAnimationKeyframe(WidgetPath, AnimName, WidgetName, PropName,
		(float)Time, (float)Value, InterpMode, OutJsonString, OutError);
}

void HandleSetWidgetIsVariableFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }

	FString WidgetPath;
	if (!Args->TryGetStringField(TEXT("widget_path"), WidgetPath))
		Args->TryGetStringField(TEXT("user_widget_path"), WidgetPath);

	UWidgetBlueprint* WidgetBP = LoadObject<UWidgetBlueprint>(nullptr, *WidgetPath);
	if (!WidgetBP) { OutError = FString::Printf(TEXT("Could not load WidgetBlueprint: %s"), *WidgetPath); return; }
	if (!WidgetBP->WidgetTree) { OutError = TEXT("WidgetBlueprint has no WidgetTree"); return; }

	bool bOuterIsVar = true;
	Args->TryGetBoolField(TEXT("is_variable"), bOuterIsVar);

	auto ApplyOne = [&](const FString& WidgetName, bool bIsVar, FString& ItemErr) -> bool
	{
		if (WidgetName.IsEmpty()) { ItemErr = TEXT("widget_name is required"); return false; }
		FString NameList;
		UWidget* Target = FindWidgetForgiving(WidgetBP->WidgetTree, WidgetName, NameList);
		if (!Target)
		{
			ItemErr = FString::Printf(TEXT("Widget '%s' not found. Available: %s"), *WidgetName, *NameList);
			return false;
		}
		Target->Modify();
		Target->bIsVariable = bIsVar;
		return true;
	};

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("widgets"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		bool bAnyChanged = false;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			FString ItemName;
			bool bItemIsVar = bOuterIsVar;
			const TSharedPtr<FJsonValue>& Val = (*ItemsArray)[i];
			if (Val->Type == EJson::String) { ItemName = Val->AsString(); }
			else if (Val->Type == EJson::Object)
			{
				TSharedPtr<FJsonObject> Item = Val->AsObject();
				if (Item.IsValid())
				{
					Item->TryGetStringField(TEXT("widget_name"), ItemName);
					Item->TryGetBoolField(TEXT("is_variable"), bItemIsVar);
				}
			}
			FString ItemErr;
			if (ApplyOne(ItemName, bItemIsVar, ItemErr))
			{
				bAnyChanged = true;
				auto Extra = MakeShared<FJsonObject>();
				Extra->SetStringField(TEXT("widget_name"), ItemName);
				Extra->SetBoolField(TEXT("is_variable"), bItemIsVar);
				Batch.AddSuccess(i, Extra);
			}
			else
			{
				Batch.AddFailure(i, ItemErr);
			}
		}
		if (bAnyChanged)
		{
			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WidgetBP);
			UEditorAssetLibrary::SaveAsset(WidgetPath, false);
		}
		Batch.Finalize(OutJsonString);
		return;
	}

	FString WidgetName;
	Args->TryGetStringField(TEXT("widget_name"), WidgetName);
	FString ItemErr;
	if (!ApplyOne(WidgetName, bOuterIsVar, ItemErr)) { OutError = ItemErr; return; }
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WidgetBP);
	UEditorAssetLibrary::SaveAsset(WidgetPath, false);

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"widget_name\":\"%s\",\"is_variable\":%s}"),
		*WidgetName, bOuterIsVar ? TEXT("true") : TEXT("false"));
}

void HandleBindWidgetPropertyFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }

	FString WidgetPath;
	if (!Args->TryGetStringField(TEXT("widget_path"), WidgetPath))
		Args->TryGetStringField(TEXT("user_widget_path"), WidgetPath);
	if (WidgetPath.IsEmpty()) { OutError = TEXT("Missing required parameter: widget_path"); return; }

	UWidgetBlueprint* WidgetBP = LoadObject<UWidgetBlueprint>(nullptr, *WidgetPath);
	if (!WidgetBP) { OutError = FString::Printf(TEXT("Could not load WidgetBlueprint: %s"), *WidgetPath); return; }

	UClass* SkelClass = WidgetBP->SkeletonGeneratedClass ? WidgetBP->SkeletonGeneratedClass : WidgetBP->GeneratedClass;

	auto ApplyOne = [&](const FString& WidgetName, const FString& PropertyName, const FString& FunctionName, FString& ErrOut) -> bool
	{
		if (WidgetName.IsEmpty() || PropertyName.IsEmpty() || FunctionName.IsEmpty())
		{
			ErrOut = TEXT("widget_name, property_name, and function_name are all required");
			return false;
		}
		if (WidgetBP->WidgetTree)
		{
			FString NameList;
			if (UWidget* Target = FindWidgetForgiving(WidgetBP->WidgetTree, WidgetName, NameList))
			{
				if (!Target->bIsVariable)
				{
					Target->Modify();
					Target->bIsVariable = true;
				}
			}
			else
			{
				ErrOut = FString::Printf(TEXT("Widget '%s' not found in WidgetTree. Available: %s"), *WidgetName, *NameList);
				return false;
			}
		}
		FGuid FuncGuid;
		bool bFunctionExists = false;
		if (SkelClass)
		{
			if (UFunction* Fn = SkelClass->FindFunctionByName(FName(*FunctionName)))
			{
				bFunctionExists = true;
				FBlueprintEditorUtils::GetFunctionGuidFromClassByFieldName(SkelClass, FName(*FunctionName), FuncGuid);
				(void)Fn;
			}
		}
		if (!bFunctionExists)
		{
			ErrOut = FString::Printf(TEXT("Function '%s' not found on WidgetBlueprint. Create it first via the blueprint umbrella (add_function)."), *FunctionName);
			return false;
		}

		FDelegateEditorBinding NewBinding;
		NewBinding.ObjectName   = WidgetName;
		NewBinding.PropertyName = FName(*PropertyName);
		NewBinding.FunctionName = FName(*FunctionName);
		NewBinding.MemberGuid   = FuncGuid;
		NewBinding.Kind         = EBindingKind::Function;

		WidgetBP->Modify();
		WidgetBP->Bindings.Remove(NewBinding);
		WidgetBP->Bindings.Add(NewBinding);
		return true;
	};

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("bindings"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		bool bAnyChanged = false;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			const TSharedPtr<FJsonValue>& V = (*ItemsArray)[i];
			if (V->Type != EJson::Object) { Batch.AddFailure(i, TEXT("each binding must be an object")); continue; }
			TSharedPtr<FJsonObject> It = V->AsObject();
			FString WN, PN, FN;
			It->TryGetStringField(TEXT("widget_name"),   WN);
			It->TryGetStringField(TEXT("property_name"), PN);
			It->TryGetStringField(TEXT("function_name"), FN);
			FString ItemErr;
			if (ApplyOne(WN, PN, FN, ItemErr))
			{
				bAnyChanged = true;
				auto Extra = MakeShared<FJsonObject>();
				Extra->SetStringField(TEXT("widget_name"), WN);
				Extra->SetStringField(TEXT("property"),    PN);
				Extra->SetStringField(TEXT("function"),    FN);
				Batch.AddSuccess(i, Extra);
			}
			else
			{
				Batch.AddFailure(i, ItemErr);
			}
		}
		if (bAnyChanged)
		{
			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WidgetBP);
			UEditorAssetLibrary::SaveAsset(WidgetPath, false);
		}
		Batch.Finalize(OutJsonString);
		return;
	}

	FString WidgetName, PropertyName, FunctionName;
	Args->TryGetStringField(TEXT("widget_name"),    WidgetName);
	Args->TryGetStringField(TEXT("property_name"),  PropertyName);
	Args->TryGetStringField(TEXT("function_name"),  FunctionName);
	FString ItemErr;
	if (!ApplyOne(WidgetName, PropertyName, FunctionName, ItemErr)) { OutError = ItemErr; return; }

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WidgetBP);
	UEditorAssetLibrary::SaveAsset(WidgetPath, false);

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"widget_name\":\"%s\",\"property\":\"%s\",\"function\":\"%s\"}"),
		*WidgetName, *PropertyName, *FunctionName);
}

void HandleUnbindWidgetPropertyFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString WidgetPath, WidgetName, PropertyName;
	if (!Args->TryGetStringField(TEXT("widget_path"), WidgetPath))
		Args->TryGetStringField(TEXT("user_widget_path"), WidgetPath);
	Args->TryGetStringField(TEXT("widget_name"),   WidgetName);
	Args->TryGetStringField(TEXT("property_name"), PropertyName);
	if (WidgetName.IsEmpty() || PropertyName.IsEmpty())
	{ OutError = TEXT("widget_name and property_name are required"); return; }

	UWidgetBlueprint* WidgetBP = LoadObject<UWidgetBlueprint>(nullptr, *WidgetPath);
	if (!WidgetBP) { OutError = FString::Printf(TEXT("Could not load WidgetBlueprint: %s"), *WidgetPath); return; }

	WidgetBP->Modify();
	const int32 RemovedCount = WidgetBP->Bindings.RemoveAll(
		[&](const FDelegateEditorBinding& B)
		{
			return B.ObjectName == WidgetName && B.PropertyName == FName(*PropertyName);
		});

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WidgetBP);
	UEditorAssetLibrary::SaveAsset(WidgetPath, false);

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"widget_name\":\"%s\",\"property\":\"%s\",\"removed\":%d}"),
		*WidgetName, *PropertyName, RemovedCount);
}

void HandleGetWidgetBindingsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString WidgetPath;
	if (!Args->TryGetStringField(TEXT("widget_path"), WidgetPath))
		Args->TryGetStringField(TEXT("user_widget_path"), WidgetPath);
	UWidgetBlueprint* WidgetBP = LoadObject<UWidgetBlueprint>(nullptr, *WidgetPath);
	if (!WidgetBP) { OutError = FString::Printf(TEXT("Could not load WidgetBlueprint: %s"), *WidgetPath); return; }

	TArray<TSharedPtr<FJsonValue>> Arr;
	for (const FDelegateEditorBinding& B : WidgetBP->Bindings)
	{
		TSharedPtr<FJsonObject> O = MakeShareable(new FJsonObject);
		O->SetStringField(TEXT("widget_name"),   B.ObjectName);
		O->SetStringField(TEXT("property_name"), B.PropertyName.ToString());
		O->SetStringField(TEXT("function_name"), B.FunctionName.ToString());
		const TCHAR* KindStr = (B.Kind == EBindingKind::Function) ? TEXT("Function") : TEXT("Property");
		O->SetStringField(TEXT("kind"), KindStr);
		Arr.Add(MakeShareable(new FJsonValueObject(O)));
	}

	TSharedPtr<FJsonObject> Root = MakeShareable(new FJsonObject);
	Root->SetBoolField(TEXT("success"), true);
	Root->SetNumberField(TEXT("count"), Arr.Num());
	Root->SetArrayField(TEXT("bindings"), Arr);

	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Root.ToSharedRef(), W);
}

}
