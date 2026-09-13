// Copyright 2026, BlueprintsLab, All rights reserved

#include "Tools/WidgetTreeTools.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/NamedSlotInterface.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/PanelSlot.h"
#include "Components/PanelWidget.h"
#include "Components/Widget.h"
#include "Layout/Margin.h"
#include "Styling/SlateBrush.h"
#include "Widgets/Layout/Anchors.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EditorAssetLibrary.h"
#include "JsonObjectConverter.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Misc/PackageName.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/Class.h"
#include "UObject/UnrealType.h"
#include "WidgetBlueprint.h"

DEFINE_LOG_CATEGORY_STATIC(LogWidgetTreeTools, Log, All);

namespace WidgetTreeTools
{

static UWidgetBlueprint* LoadWidgetBlueprint(const FString& AssetPath)
{
	if (AssetPath.IsEmpty()) return nullptr;
	UObject* Loaded = UEditorAssetLibrary::LoadAsset(AssetPath);
	return Cast<UWidgetBlueprint>(Loaded);
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
		TEXT("Asset at '%s' is %s, not a WidgetBlueprint. Recreate it with blueprint(create_blueprint, parent_class='UserWidget', save_path=..., name=...)."),
		*AssetPath, *Loaded->GetClass()->GetName());
	return nullptr;
}

static UClass* ResolveWidgetClass(const FString& ClassSpec)
{
	if (ClassSpec.IsEmpty()) return nullptr;

	if (ClassSpec.StartsWith(TEXT("/")))
	{
		return LoadClass<UWidget>(nullptr, *ClassSpec);
	}

	for (TObjectIterator<UClass> It; It; ++It)
	{
		UClass* Candidate = *It;
		if (!Candidate || !Candidate->IsChildOf(UWidget::StaticClass())) continue;
		if (Candidate->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists)) continue;
		if (Candidate->GetName() == ClassSpec ||
			Candidate->GetName() == FString::Printf(TEXT("W%s"), *ClassSpec) ||
			Candidate->GetName() == FString::Printf(TEXT("U%s"), *ClassSpec))
		{
			return Candidate;
		}
	}
	return nullptr;
}

static bool IsPropertyExportable(FProperty* Property)
{
	if (!Property) return false;
	if (!Property->HasAnyPropertyFlags(CPF_Edit)) return false;
	if (Property->HasAnyPropertyFlags(CPF_Transient)) return false;
#if WITH_EDITOR
	if (Property->HasAnyPropertyFlags(CPF_EditorOnly))
	{
		const FString Cat = Property->GetMetaData(TEXT("Category"));
		if (Cat.IsEmpty()) return false;
	}
#endif
	return true;
}

static TSharedPtr<FJsonObject> ExportEditedProperties(UObject* Owner, UObject* DefaultOwner, const TSet<FName>& SkipNames)
{
	TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
	if (!Owner || !DefaultOwner) return Out;

	for (TFieldIterator<FProperty> It(Owner->GetClass()); It; ++It)
	{
		FProperty* Property = *It;
		if (!IsPropertyExportable(Property)) continue;
		if (SkipNames.Contains(Property->GetFName())) continue;

		const void* ValuePtr  = Property->ContainerPtrToValuePtr<void>(Owner);
		const void* DefaultPtr = Property->ContainerPtrToValuePtr<void>(DefaultOwner);
		if (Property->Identical(ValuePtr, DefaultPtr)) continue;

		TSharedPtr<FJsonValue> JsonVal = FJsonObjectConverter::UPropertyToJsonValue(Property, ValuePtr);
		if (JsonVal.IsValid())
		{
			Out->SetField(Property->GetName(), JsonVal);
		}
	}
	return Out;
}

static TSharedPtr<FJsonObject> ExportWidgetRecursive(UWidget* Widget)
{
	if (!Widget) return nullptr;

	TSharedPtr<FJsonObject> WidgetJson = MakeShared<FJsonObject>();
	WidgetJson->SetStringField(TEXT("widget_name"), Widget->GetName());
	WidgetJson->SetStringField(TEXT("widget_class"), Widget->GetClass()->GetPathName());

	UObject* DefaultWidget = Widget->GetClass()->GetDefaultObject();
	const TSet<FName> SkipWidget = { TEXT("Slot") };
	TSharedPtr<FJsonObject> Props = ExportEditedProperties(Widget, DefaultWidget, SkipWidget);
	if (Props->Values.Num() > 0)
	{
		WidgetJson->SetObjectField(TEXT("properties"), Props);
	}

	if (UPanelSlot* Slot = Widget->Slot)
	{
		UObject* DefaultSlot = Slot->GetClass()->GetDefaultObject();
		const TSet<FName> SkipSlot = { TEXT("Content"), TEXT("Parent") };
		TSharedPtr<FJsonObject> SlotProps = ExportEditedProperties(Slot, DefaultSlot, SkipSlot);
		if (SlotProps->Values.Num() > 0)
		{
			WidgetJson->SetStringField(TEXT("slot_class"), Slot->GetClass()->GetPathName());
			WidgetJson->SetObjectField(TEXT("slot_properties"), SlotProps);
		}
	}

	if (UPanelWidget* Panel = Cast<UPanelWidget>(Widget))
	{
		TArray<TSharedPtr<FJsonValue>> Children;
		Children.Reserve(Panel->GetChildrenCount());
		for (int32 i = 0; i < Panel->GetChildrenCount(); ++i)
		{
			if (UWidget* Child = Panel->GetChildAt(i))
			{
				if (TSharedPtr<FJsonObject> ChildJson = ExportWidgetRecursive(Child))
				{
					Children.Add(MakeShared<FJsonValueObject>(ChildJson));
				}
			}
		}
		if (Children.Num() > 0)
		{
			WidgetJson->SetArrayField(TEXT("children"), Children);
		}
	}

	return WidgetJson;
}

void HandleGetWidgetTreeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
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
	if (!WBP->WidgetTree)
	{
		OutError = FString::Printf(TEXT("WidgetBlueprint at '%s' has no WidgetTree (severely corrupt — recreate)."), *AssetPath);
		return;
	}

	FString RootName;
	Args->TryGetStringField(TEXT("widget_name"), RootName);
	if (RootName.IsEmpty()) Args->TryGetStringField(TEXT("root_widget_name"), RootName);

	UWidget* Root = nullptr;
	if (RootName.IsEmpty() || RootName.Equals(TEXT("Root"), ESearchCase::IgnoreCase))
	{
		Root = WBP->WidgetTree->RootWidget;
		if (!Root)
		{
			TSharedPtr<FJsonObject> Stub = MakeShared<FJsonObject>();
			Stub->SetBoolField(TEXT("empty"), true);
			Stub->SetStringField(TEXT("user_widget_path"), AssetPath);
			Stub->SetStringField(TEXT("message"), TEXT("WidgetTree has no root widget yet."));
			TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
			FJsonSerializer::Serialize(Stub.ToSharedRef(), Writer);
			return;
		}
	}
	else
	{
		Root = WBP->WidgetTree->FindWidget(FName(*RootName));
		if (!Root)
		{
			OutError = FString::Printf(TEXT("Widget '%s' not found in '%s'."), *RootName, *AssetPath);
			return;
		}
	}

	TSharedPtr<FJsonObject> TreeJson = ExportWidgetRecursive(Root);
	if (!TreeJson.IsValid())
	{
		OutError = TEXT("Failed to export widget tree.");
		return;
	}

	TSharedPtr<FJsonObject> Envelope = MakeShared<FJsonObject>();
	Envelope->SetBoolField(TEXT("success"), true);
	Envelope->SetStringField(TEXT("user_widget_path"), AssetPath);
	Envelope->SetObjectField(TEXT("tree"), TreeJson);

	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Envelope.ToSharedRef(), Writer);
}

static TSharedPtr<FJsonObject> DescribeProperty(FProperty* Property, bool bVerbose)
{
	TSharedPtr<FJsonObject> Desc = MakeShared<FJsonObject>();
	Desc->SetStringField(TEXT("name"), Property->GetName());
	Desc->SetStringField(TEXT("cpp_type"), Property->GetCPPType());
#if WITH_EDITOR
	const FString Category = Property->GetMetaData(TEXT("Category"));
	if (!Category.IsEmpty()) Desc->SetStringField(TEXT("category"), Category);
	if (bVerbose)
	{
		const FString Tooltip = Property->GetMetaData(TEXT("ToolTip"));
		if (!Tooltip.IsEmpty()) Desc->SetStringField(TEXT("tooltip"), Tooltip);
	}
#endif
	if (Property->ArrayDim > 1) Desc->SetNumberField(TEXT("array_dim"), Property->ArrayDim);
	return Desc;
}

static TArray<TSharedPtr<FJsonValue>> CollectSchema(UClass* Class, bool bVerbose)
{
	TArray<TSharedPtr<FJsonValue>> Out;
	if (!Class) return Out;
	static const TSet<FString> LowSignalCategories = {
		TEXT("Accessibility"), TEXT("Localization"), TEXT("Performance"),
		TEXT("Navigation"), TEXT("Render Transform"),
	};
	for (TFieldIterator<FProperty> It(Class); It; ++It)
	{
		FProperty* Property = *It;
		if (!IsPropertyExportable(Property)) continue;
		if (!bVerbose)
		{
			const FString Cat = Property->GetMetaData(TEXT("Category"));
			if (LowSignalCategories.Contains(Cat)) continue;
			if (Property->GetFName() == FName(TEXT("Slot"))) continue;
		}
		Out.Add(MakeShared<FJsonValueObject>(DescribeProperty(Property, bVerbose)));
	}
	return Out;
}

void HandleGetWidgetSchemaFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }

	FString ClassSpec;
	Args->TryGetStringField(TEXT("widget_class"), ClassSpec);
	if (ClassSpec.IsEmpty()) Args->TryGetStringField(TEXT("widget_type"), ClassSpec);
	if (ClassSpec.IsEmpty())
	{
		OutError = TEXT("Missing required parameter: widget_class (short name like 'Button' or full path).");
		return;
	}

	UClass* Class = ResolveWidgetClass(ClassSpec);
	if (!Class)
	{
		OutError = FString::Printf(TEXT("Widget class '%s' not found."), *ClassSpec);
		return;
	}

	bool bVerbose = false;
	Args->TryGetBoolField(TEXT("verbose"), bVerbose);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("widget_class"), Class->GetPathName());
	Result->SetStringField(TEXT("widget_class_short"), Class->GetName());

	const TArray<TSharedPtr<FJsonValue>> WidgetProps = CollectSchema(Class, bVerbose);
	Result->SetArrayField(TEXT("properties"), WidgetProps);

	FString SlotClassSpec;
	Args->TryGetStringField(TEXT("slot_class"), SlotClassSpec);
	if (!SlotClassSpec.IsEmpty())
	{
		UClass* SlotClass = nullptr;
		if (SlotClassSpec.StartsWith(TEXT("/")))
		{
			SlotClass = LoadClass<UPanelSlot>(nullptr, *SlotClassSpec);
		}
		else
		{
			for (TObjectIterator<UClass> It; It; ++It)
			{
				if (It->IsChildOf(UPanelSlot::StaticClass()) && It->GetName() == SlotClassSpec)
				{
					SlotClass = *It;
					break;
				}
			}
		}
		if (SlotClass)
		{
			Result->SetStringField(TEXT("slot_class"), SlotClass->GetPathName());
			Result->SetArrayField(TEXT("slot_properties"), CollectSchema(SlotClass, bVerbose));
		}
		else
		{
			Result->SetStringField(TEXT("slot_class_error"),
				FString::Printf(TEXT("Slot class '%s' not found."), *SlotClassSpec));
		}
	}

	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
}

static FString NormalizePropertyKey(const FString& Key)
{
	if (Key.IsEmpty()) return Key;

	if (Key.Contains(TEXT("_")))
	{
		TArray<FString> Parts;
		Key.ParseIntoArray(Parts, TEXT("_"), true);
		FString Result;
		Result.Reserve(Key.Len());
		for (FString& Part : Parts)
		{
			if (Part.Len() > 0)
			{
				Part[0] = FChar::ToUpper(Part[0]);
			}
			Result += Part;
		}
		return Result;
	}

	FString Result = Key;
	if (FChar::IsLower(Result[0]))
	{
		Result[0] = FChar::ToUpper(Result[0]);
	}
	return Result;
}

static const TMap<FString, FString>& GetPropertyAliasMap()
{
	static const TMap<FString, FString> Map = {
		{ TEXT("TextColor"),  TEXT("ColorAndOpacity") },
		{ TEXT("Anchor"),     TEXT("Anchors") },
		{ TEXT("HAlign"),     TEXT("HorizontalAlignment") },
		{ TEXT("VAlign"),     TEXT("VerticalAlignment") },
	};
	return Map;
}

static FString NormalizeHAlignString(const FString& In)
{
	if (In.StartsWith(TEXT("HAlign_"))) return In;
	if (In.Equals(TEXT("Fill"),   ESearchCase::IgnoreCase)) return TEXT("HAlign_Fill");
	if (In.Equals(TEXT("Left"),   ESearchCase::IgnoreCase)) return TEXT("HAlign_Left");
	if (In.Equals(TEXT("Center"), ESearchCase::IgnoreCase)) return TEXT("HAlign_Center");
	if (In.Equals(TEXT("Right"),  ESearchCase::IgnoreCase)) return TEXT("HAlign_Right");
	return In;
}
static FString NormalizeVAlignString(const FString& In)
{
	if (In.StartsWith(TEXT("VAlign_"))) return In;
	if (In.Equals(TEXT("Fill"),   ESearchCase::IgnoreCase)) return TEXT("VAlign_Fill");
	if (In.Equals(TEXT("Top"),    ESearchCase::IgnoreCase)) return TEXT("VAlign_Top");
	if (In.Equals(TEXT("Center"), ESearchCase::IgnoreCase)) return TEXT("VAlign_Center");
	if (In.Equals(TEXT("Bottom"), ESearchCase::IgnoreCase)) return TEXT("VAlign_Bottom");
	return In;
}

static FString NormalizeOrientationString(const FString& In)
{
	if (In.StartsWith(TEXT("Orient_"))) return In;
	if (In.Equals(TEXT("Horizontal"), ESearchCase::IgnoreCase)) return TEXT("Orient_Horizontal");
	if (In.Equals(TEXT("Vertical"),   ESearchCase::IgnoreCase)) return TEXT("Orient_Vertical");
	return In;
}

static void BakeBoxSlotShortcuts(const TSharedPtr<FJsonObject>& Slot)
{
	if (!Slot.IsValid()) return;

	if (TSharedPtr<FJsonValue> FillVal = Slot->TryGetField(TEXT("Fill")))
	{
		const double FillWeight = FillVal->AsNumber();
		TSharedPtr<FJsonObject> SizeObj = MakeShared<FJsonObject>();
		if (FillWeight > 0.0)
		{
			SizeObj->SetStringField(TEXT("SizeRule"), TEXT("Fill"));
			SizeObj->SetNumberField(TEXT("Value"),    FillWeight);
		}
		else
		{
			SizeObj->SetStringField(TEXT("SizeRule"), TEXT("Auto"));
			SizeObj->SetNumberField(TEXT("Value"),    1.0);
		}
		Slot->SetObjectField(TEXT("Size"), SizeObj);
		Slot->RemoveField(TEXT("Fill"));
	}

	if (TSharedPtr<FJsonValue> HV = Slot->TryGetField(TEXT("HorizontalAlignment")))
	{
		if (HV->Type == EJson::String)
		{
			Slot->SetStringField(TEXT("HorizontalAlignment"), NormalizeHAlignString(HV->AsString()));
		}
	}
	if (TSharedPtr<FJsonValue> VV = Slot->TryGetField(TEXT("VerticalAlignment")))
	{
		if (VV->Type == EJson::String)
		{
			Slot->SetStringField(TEXT("VerticalAlignment"), NormalizeVAlignString(VV->AsString()));
		}
	}
}

static void BakeOrientationEnum(const TSharedPtr<FJsonObject>& Props)
{
	if (!Props.IsValid()) return;
	if (TSharedPtr<FJsonValue> OV = Props->TryGetField(TEXT("Orientation")))
	{
		if (OV->Type == EJson::String)
		{
			Props->SetStringField(TEXT("Orientation"), NormalizeOrientationString(OV->AsString()));
		}
	}
}

static void BakeOverrideFlags(UObject* Target, const TSharedPtr<FJsonObject>& Props)
{
	if (!Target || !Props.IsValid()) return;
	UClass* Class = Target->GetClass();

	TArray<FString> Keys;
	Keys.Reserve(Props->Values.Num());
	for (const auto& P : Props->Values) Keys.Add(FString(*P.Key));
	for (const FString& Key : Keys)
	{
		if (Key.StartsWith(TEXT("bOverride_"))) continue;
		const FString FlagName = FString::Printf(TEXT("bOverride_%s"), *Key);
		if (Class->FindPropertyByName(FName(*FlagName)) && !Props->HasField(FlagName))
		{
			Props->SetBoolField(FlagName, true);
		}
	}
}

static void BakeTextBlockShortcuts(const TSharedPtr<FJsonObject>& Props)
{
	if (!Props.IsValid()) return;

	if (TSharedPtr<FJsonValue> FS = Props->TryGetField(TEXT("FontSize")))
	{
		const double Size = FS->AsNumber();
		TSharedPtr<FJsonObject> FontObj;
		const TSharedPtr<FJsonObject>* ExistingFontObj = nullptr;
		if (Props->TryGetObjectField(TEXT("Font"), ExistingFontObj) && ExistingFontObj && (*ExistingFontObj).IsValid())
		{
			FontObj = *ExistingFontObj;
		}
		else
		{
			FontObj = MakeShared<FJsonObject>();
		}
		FontObj->SetNumberField(TEXT("Size"), Size);
		Props->SetObjectField(TEXT("Font"), FontObj);
		Props->RemoveField(TEXT("FontSize"));
	}
}

static TSharedPtr<FJsonObject> NormalizeJsonKeysDeep(const TSharedPtr<FJsonObject>& Src)
{
	if (!Src.IsValid()) return Src;

	TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
	const TMap<FString, FString>& Aliases = GetPropertyAliasMap();

	for (const auto& Pair : Src->Values)
	{
		FString Key = NormalizePropertyKey(FString(*Pair.Key));
		if (const FString* Mapped = Aliases.Find(Key)) Key = *Mapped;

		TSharedPtr<FJsonValue> Value = Pair.Value;
		switch (Value->Type)
		{
			case EJson::Object:
			{
				Out->SetObjectField(Key, NormalizeJsonKeysDeep(Value->AsObject()));
				break;
			}
			case EJson::Array:
			{
				TArray<TSharedPtr<FJsonValue>> Arr = Value->AsArray();
				for (TSharedPtr<FJsonValue>& Item : Arr)
				{
					if (Item->Type == EJson::Object)
					{
						Item = MakeShared<FJsonValueObject>(NormalizeJsonKeysDeep(Item->AsObject()));
					}
				}
				Out->SetArrayField(Key, Arr);
				break;
			}
			default:
				Out->SetField(Key, Value);
				break;
		}
	}
	return Out;
}

static TSharedPtr<FJsonObject> MarginStringToJson(const FString& Spec)
{
	TArray<FString> Parts;
	Spec.ParseIntoArray(Parts, TEXT(","));
	float L = 0, T = 0, R = 0, B = 0;
	if (Parts.Num() == 1)
	{
		const float V = FCString::Atof(*Parts[0]);
		L = T = R = B = V;
	}
	else if (Parts.Num() == 2)
	{
		const float H = FCString::Atof(*Parts[0]);
		const float V = FCString::Atof(*Parts[1]);
		L = R = H; T = B = V;
	}
	else if (Parts.Num() >= 4)
	{
		L = FCString::Atof(*Parts[0]);
		T = FCString::Atof(*Parts[1]);
		R = FCString::Atof(*Parts[2]);
		B = FCString::Atof(*Parts[3]);
	}
	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetNumberField(TEXT("Left"),   L);
	Obj->SetNumberField(TEXT("Top"),    T);
	Obj->SetNumberField(TEXT("Right"),  R);
	Obj->SetNumberField(TEXT("Bottom"), B);
	return Obj;
}

static TSharedPtr<FJsonObject> LinearColorStringToJson(const FString& Spec)
{
	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	FString Cleaned = Spec;
	Cleaned.ReplaceInline(TEXT("("), TEXT(""));
	Cleaned.ReplaceInline(TEXT(")"), TEXT(""));
	TArray<FString> Pairs;
	Cleaned.ParseIntoArray(Pairs, TEXT(","));
	for (const FString& P : Pairs)
	{
		FString K, V;
		if (P.Split(TEXT("="), &K, &V))
		{
			Obj->SetNumberField(K.TrimStartAndEnd(), FCString::Atof(*V.TrimStartAndEnd()));
		}
	}
	return Obj;
}

static TSharedPtr<FJsonValue> CoerceStructShortcuts(FProperty* Property, const TSharedPtr<FJsonValue>& Value)
{
	if (!Property || !Value.IsValid()) return Value;

	if (FStructProperty* Struct = CastField<FStructProperty>(Property))
	{
		if (Value->Type == EJson::String)
		{
			const FString S = Value->AsString();
			if (Struct->Struct == TBaseStructure<FMargin>::Get())
			{
				return MakeShared<FJsonValueObject>(MarginStringToJson(S));
			}
			if (Struct->Struct == TBaseStructure<FLinearColor>::Get())
			{
				return MakeShared<FJsonValueObject>(LinearColorStringToJson(S));
			}
			if (Struct->Struct->GetFName() == FName(TEXT("SlateColor")))
			{
				TSharedPtr<FJsonObject> Wrapper = MakeShared<FJsonObject>();
				Wrapper->SetObjectField(TEXT("SpecifiedColor"), LinearColorStringToJson(S));
				return MakeShared<FJsonValueObject>(Wrapper);
			}
		}

		if (Struct->Struct == TBaseStructure<FVector2D>::Get())
		{
			if (Value->Type == EJson::Array)
			{
				const auto& Arr = Value->AsArray();
				if (Arr.Num() >= 2)
				{
					TSharedPtr<FJsonObject> V = MakeShared<FJsonObject>();
					V->SetNumberField(TEXT("X"), Arr[0]->AsNumber());
					V->SetNumberField(TEXT("Y"), Arr[1]->AsNumber());
					return MakeShared<FJsonValueObject>(V);
				}
			}
			if (Value->Type == EJson::String)
			{
				FString S = Value->AsString();
				S.ReplaceInline(TEXT("["), TEXT(""));
				S.ReplaceInline(TEXT("]"), TEXT(""));
				S.ReplaceInline(TEXT("("), TEXT(""));
				S.ReplaceInline(TEXT(")"), TEXT(""));
				TArray<FString> Parts;
				S.ParseIntoArray(Parts, TEXT(","));
				if (Parts.Num() >= 2)
				{
					TSharedPtr<FJsonObject> V = MakeShared<FJsonObject>();
					V->SetNumberField(TEXT("X"), FCString::Atof(*Parts[0].TrimStartAndEnd()));
					V->SetNumberField(TEXT("Y"), FCString::Atof(*Parts[1].TrimStartAndEnd()));
					return MakeShared<FJsonValueObject>(V);
				}
			}
		}
	}
	return Value;
}

static void BakeCanvasSlotShortcuts(const TSharedPtr<FJsonObject>& Slot)
{
	if (!Slot.IsValid()) return;

	auto ReadVec2 = [](const TSharedPtr<FJsonValue>& V, FVector2D& Out) -> bool
	{
		if (!V.IsValid()) return false;
		if (V->Type == EJson::Array)
		{
			const auto& A = V->AsArray();
			if (A.Num() >= 2) { Out.X = A[0]->AsNumber(); Out.Y = A[1]->AsNumber(); return true; }
		}
		return false;
	};

	FVector2D Position(0, 0), Size(100, 100), Alignment(0, 0);
	FAnchors Anchors(0, 0, 0, 0);
	const bool bHasPosition  = ReadVec2(Slot->TryGetField(TEXT("Position")),  Position);
	const bool bHasSize      = ReadVec2(Slot->TryGetField(TEXT("Size")),      Size);
	const bool bHasAlignment = ReadVec2(Slot->TryGetField(TEXT("Alignment")), Alignment);
	bool bHasAnchors = false;

	if (TSharedPtr<FJsonValue> AV = Slot->TryGetField(TEXT("Anchors")))
	{
		if (AV->Type == EJson::String)
		{
			const FString Preset = AV->AsString().ToLower();
			if (Preset == TEXT("top_left"))         Anchors = FAnchors(0,0,0,0);
			else if (Preset == TEXT("top_right"))    Anchors = FAnchors(1,0,1,0);
			else if (Preset == TEXT("top_center"))   Anchors = FAnchors(0.5f,0,0.5f,0);
			else if (Preset == TEXT("bottom_left"))  Anchors = FAnchors(0,1,0,1);
			else if (Preset == TEXT("bottom_right")) Anchors = FAnchors(1,1,1,1);
			else if (Preset == TEXT("bottom_center"))Anchors = FAnchors(0.5f,1,0.5f,1);
			else if (Preset == TEXT("center"))       Anchors = FAnchors(0.5f,0.5f,0.5f,0.5f);
			else if (Preset == TEXT("fill"))         Anchors = FAnchors(0,0,1,1);
			else                                      Anchors = FAnchors(0,0,0,0);
			bHasAnchors = true;
		}
		else if (AV->Type == EJson::Array)
		{
			const auto& A = AV->AsArray();
			if (A.Num() == 4)
			{
				Anchors = FAnchors(A[0]->AsNumber(), A[1]->AsNumber(), A[2]->AsNumber(), A[3]->AsNumber());
				bHasAnchors = true;
			}
			else if (A.Num() == 2)
			{
				const float X = A[0]->AsNumber(), Y = A[1]->AsNumber();
				Anchors = FAnchors(X, Y, X, Y);
				bHasAnchors = true;
			}
		}
		else if (AV->Type == EJson::Object)
		{
			return;
		}
	}

	if (!bHasPosition && !bHasSize && !bHasAnchors && !bHasAlignment) return;

	TSharedPtr<FJsonObject> Layout = MakeShared<FJsonObject>();

	TSharedPtr<FJsonObject> AnchObj = MakeShared<FJsonObject>();
	{
		TSharedPtr<FJsonObject> Mn = MakeShared<FJsonObject>();
		Mn->SetNumberField(TEXT("X"), Anchors.Minimum.X);
		Mn->SetNumberField(TEXT("Y"), Anchors.Minimum.Y);
		TSharedPtr<FJsonObject> Mx = MakeShared<FJsonObject>();
		Mx->SetNumberField(TEXT("X"), Anchors.Maximum.X);
		Mx->SetNumberField(TEXT("Y"), Anchors.Maximum.Y);
		AnchObj->SetObjectField(TEXT("Minimum"), Mn);
		AnchObj->SetObjectField(TEXT("Maximum"), Mx);
	}
	Layout->SetObjectField(TEXT("Anchors"), AnchObj);

	TSharedPtr<FJsonObject> Offsets = MakeShared<FJsonObject>();
	const bool bStretchX = !FMath::IsNearlyEqual(Anchors.Minimum.X, Anchors.Maximum.X);
	const bool bStretchY = !FMath::IsNearlyEqual(Anchors.Minimum.Y, Anchors.Maximum.Y);
	Offsets->SetNumberField(TEXT("Left"),   Position.X);
	Offsets->SetNumberField(TEXT("Top"),    Position.Y);
	Offsets->SetNumberField(TEXT("Right"),  bStretchX ? 0 : Size.X);
	Offsets->SetNumberField(TEXT("Bottom"), bStretchY ? 0 : Size.Y);
	Layout->SetObjectField(TEXT("Offsets"), Offsets);

	TSharedPtr<FJsonObject> AlignObj = MakeShared<FJsonObject>();
	AlignObj->SetNumberField(TEXT("X"), Alignment.X);
	AlignObj->SetNumberField(TEXT("Y"), Alignment.Y);
	Layout->SetObjectField(TEXT("Alignment"), AlignObj);

	Slot->SetObjectField(TEXT("LayoutData"), Layout);
	Slot->RemoveField(TEXT("Position"));
	Slot->RemoveField(TEXT("Size"));
	Slot->RemoveField(TEXT("Anchors"));
	Slot->RemoveField(TEXT("Alignment"));
}

static void CoerceAllStructShortcuts(UStruct* OwnerStruct, const TSharedPtr<FJsonObject>& Obj)
{
	if (!OwnerStruct || !Obj.IsValid()) return;
	TArray<FString> Keys;
	Keys.Reserve(Obj->Values.Num());
	for (const auto& P : Obj->Values) Keys.Add(FString(*P.Key));
	for (const FString& Key : Keys)
	{
		FProperty* P = OwnerStruct->FindPropertyByName(FName(*Key));
		if (!P) continue;
		const TSharedPtr<FJsonValue> Val = Obj->TryGetField(Key);
		if (!Val.IsValid()) continue;
		const TSharedPtr<FJsonValue> Coerced = CoerceStructShortcuts(P, Val);
		if (Coerced != Val) Obj->SetField(Key, Coerced);
		if (FStructProperty* StructProp = CastField<FStructProperty>(P))
		{
			if (Coerced->Type == EJson::Object)
			{
				CoerceAllStructShortcuts(StructProp->Struct, Coerced->AsObject());
			}
		}
	}
}

static int32 ApplyPropertiesObject(
	UObject* Target,
	const TSharedPtr<FJsonObject>& InputProps,
	TArray<FString>& OutWarnings,
	const bool bIsCanvasPanelSlot = false)
{
	if (!Target || !InputProps.IsValid() || InputProps->Values.Num() == 0) return 0;

	TSharedPtr<FJsonObject> Props = NormalizeJsonKeysDeep(InputProps);

	if (bIsCanvasPanelSlot)
	{
		BakeCanvasSlotShortcuts(Props);
	}
	BakeBoxSlotShortcuts(Props);
	BakeTextBlockShortcuts(Props);
	BakeOrientationEnum(Props);
	BakeOverrideFlags(Target, Props);

	TSharedPtr<FJsonObject> Accepted = MakeShared<FJsonObject>();
	int32 Applied = 0;

	struct FPendingObjectAssign { FProperty* Prop; FString Path; bool bIsBrushResource; };
	TArray<FPendingObjectAssign> PendingObjects;

	for (const auto& Pair : Props->Values)
	{
		const FString PairKey(*Pair.Key);
		FProperty* P = Target->GetClass()->FindPropertyByName(FName(*PairKey));
		if (!P)
		{
			OutWarnings.Add(FString::Printf(TEXT("Property '%s' not found on '%s'"), *PairKey, *Target->GetClass()->GetName()));
			continue;
		}
		if (P->HasAnyPropertyFlags(CPF_Transient))
		{
			OutWarnings.Add(FString::Printf(TEXT("Property '%s' is transient and was skipped"), *PairKey));
			continue;
		}

		if (CastField<FObjectProperty>(P) && Pair.Value->Type == EJson::String)
		{
			PendingObjects.Add({ P, Pair.Value->AsString(), false });
			++Applied;
			continue;
		}

		if (FStructProperty* StructProp = CastField<FStructProperty>(P))
		{
			if (StructProp->Struct == TBaseStructure<FSlateBrush>::Get() && Pair.Value->Type == EJson::Object)
			{
				const TSharedPtr<FJsonObject> BrushObj = Pair.Value->AsObject();
				FString ResourcePath;
				if (BrushObj->TryGetStringField(TEXT("ResourceObject"), ResourcePath) && !ResourcePath.IsEmpty())
				{
					PendingObjects.Add({ P, ResourcePath, true });
					BrushObj->RemoveField(TEXT("ResourceObject"));
				}
			}
		}

		const TSharedPtr<FJsonValue> Coerced = CoerceStructShortcuts(P, Pair.Value);
		if (FStructProperty* StructProp = CastField<FStructProperty>(P))
		{
			if (Coerced->Type == EJson::Object)
			{
				CoerceAllStructShortcuts(StructProp->Struct, Coerced->AsObject());
			}
		}
		Accepted->SetField(PairKey, Coerced);
		++Applied;
	}

	if (Accepted->Values.Num() > 0)
	{
		Target->Modify();
		if (!FJsonObjectConverter::JsonObjectToUStruct(Accepted.ToSharedRef(), Target->GetClass(), Target, 0, 0))
		{
			OutWarnings.Add(FString::Printf(TEXT("JsonObjectToUStruct partial failure on '%s'"), *Target->GetClass()->GetName()));
		}
	}

	for (const FPendingObjectAssign& Pending : PendingObjects)
	{
		UObject* Resolved = LoadObject<UObject>(nullptr, *Pending.Path);
		if (!Resolved)
		{
			OutWarnings.Add(FString::Printf(TEXT("Could not load asset '%s' for property '%s'"),
				*Pending.Path, *Pending.Prop->GetName()));
			continue;
		}

		if (Pending.bIsBrushResource)
		{
			FStructProperty* BrushProp = CastField<FStructProperty>(Pending.Prop);
			void* BrushPtr = BrushProp->ContainerPtrToValuePtr<void>(Target);
			if (FProperty* ResProp = BrushProp->Struct->FindPropertyByName(TEXT("ResourceObject")))
			{
				if (FObjectProperty* ResObjProp = CastField<FObjectProperty>(ResProp))
				{
					ResObjProp->SetObjectPropertyValue(
						ResObjProp->ContainerPtrToValuePtr<void>(BrushPtr), Resolved);
				}
			}
		}
		else
		{
			FObjectProperty* ObjProp = CastField<FObjectProperty>(Pending.Prop);
			ObjProp->SetObjectPropertyValue(ObjProp->ContainerPtrToValuePtr<void>(Target), Resolved);
		}
	}

	return Applied;
}

static UWidget* ApplyNodeRecursive(
	UWidgetBlueprint* WBP,
	const TSharedPtr<FJsonObject>& Node,
	UPanelWidget* ParentPanel,
	TArray<FString>& OutCreated,
	TArray<FString>& OutUpdated,
	TArray<FString>& OutWarnings);

static UClass* ResolveWidgetClass_ForApply(const FString& ClassSpec)
{
	if (ClassSpec.IsEmpty()) return nullptr;
	if (ClassSpec.StartsWith(TEXT("/")))
	{
		if (UClass* Found = LoadClass<UWidget>(nullptr, *ClassSpec)) return Found;
	}
	for (TObjectIterator<UClass> It; It; ++It)
	{
		UClass* C = *It;
		if (!C || !C->IsChildOf(UWidget::StaticClass())) continue;
		if (C->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists)) continue;
		if (C->GetName() == ClassSpec ||
			C->GetName() == FString::Printf(TEXT("W%s"), *ClassSpec) ||
			C->GetName() == FString::Printf(TEXT("U%s"), *ClassSpec))
		{
			return C;
		}
	}
	return nullptr;
}

static UWidget* ApplyNodeRecursive(
	UWidgetBlueprint* WBP,
	const TSharedPtr<FJsonObject>& Node,
	UPanelWidget* ParentPanel,
	TArray<FString>& OutCreated,
	TArray<FString>& OutUpdated,
	TArray<FString>& OutWarnings)
{
	if (!WBP || !Node.IsValid()) return nullptr;

	FString WidgetName;
	Node->TryGetStringField(TEXT("widget_name"), WidgetName);
	FString ClassSpec;
	Node->TryGetStringField(TEXT("widget_class"), ClassSpec);

	UWidget* Widget = nullptr;
	bool bCreated = false;

	if (!WidgetName.IsEmpty())
	{
		Widget = WBP->WidgetTree->FindWidget(FName(*WidgetName));
	}

	if (!Widget)
	{
		UClass* Class = ResolveWidgetClass_ForApply(ClassSpec);
		if (!Class)
		{
			OutWarnings.Add(FString::Printf(TEXT("Unknown widget_class '%s' for '%s' — skipped"), *ClassSpec, *WidgetName));
			return nullptr;
		}
		const FName DesiredName = WidgetName.IsEmpty()
			? FBlueprintEditorUtils::FindUniqueKismetName(WBP, Class->GetName())
			: FName(*WidgetName);
		Widget = WBP->WidgetTree->ConstructWidget<UWidget>(Class, DesiredName);
		if (!Widget)
		{
			OutWarnings.Add(FString::Printf(TEXT("ConstructWidget failed for '%s' (%s)"), *WidgetName, *ClassSpec));
			return nullptr;
		}
		if (!WidgetName.IsEmpty())
		{
			Widget->bIsVariable = true;
		}
		bCreated = true;
	}
	else if (!ClassSpec.IsEmpty())
	{
		UClass* RequestedClass = ResolveWidgetClass_ForApply(ClassSpec);
		if (RequestedClass && !Widget->IsA(RequestedClass))
		{
			OutWarnings.Add(FString::Printf(
				TEXT("Existing widget '%s' is %s but tree requested %s — kept existing class"),
				*Widget->GetName(), *Widget->GetClass()->GetName(), *RequestedClass->GetName()));
		}
	}

	if (bCreated && ParentPanel)
	{
		ParentPanel->AddChild(Widget);
	}

	const TSharedPtr<FJsonObject>* PropsObj = nullptr;
	if (Node->TryGetObjectField(TEXT("properties"), PropsObj) && PropsObj && PropsObj->IsValid())
	{
		ApplyPropertiesObject(Widget, *PropsObj, OutWarnings);
	}

	const TSharedPtr<FJsonObject>* SlotObj = nullptr;
	if (Node->TryGetObjectField(TEXT("slot_properties"), SlotObj) && SlotObj && SlotObj->IsValid())
	{
		if (Widget->Slot)
		{
			const bool bCanvasSlot = Widget->Slot->IsA(UCanvasPanelSlot::StaticClass());
			ApplyPropertiesObject(Widget->Slot, *SlotObj, OutWarnings, bCanvasSlot);
		}
		else if ((*SlotObj)->Values.Num() > 0)
		{
			OutWarnings.Add(FString::Printf(TEXT("Widget '%s' has no Slot — slot_properties ignored"), *Widget->GetName()));
		}
	}

	(bCreated ? OutCreated : OutUpdated).Add(Widget->GetName());

	const TArray<TSharedPtr<FJsonValue>>* Children = nullptr;
	if (Node->TryGetArrayField(TEXT("children"), Children) && Children)
	{
		UPanelWidget* AsPanel = Cast<UPanelWidget>(Widget);
		if (!AsPanel && Children->Num() > 0)
		{
			OutWarnings.Add(FString::Printf(TEXT("Widget '%s' is not a panel but was given %d children — ignored"),
				*Widget->GetName(), Children->Num()));
		}
		else if (AsPanel)
		{
			for (const TSharedPtr<FJsonValue>& ChildVal : *Children)
			{
				const TSharedPtr<FJsonObject>* ChildObj = nullptr;
				if (ChildVal->TryGetObject(ChildObj) && ChildObj && ChildObj->IsValid())
				{
					ApplyNodeRecursive(WBP, *ChildObj, AsPanel, OutCreated, OutUpdated, OutWarnings);
				}
			}
		}
	}

	return Widget;
}

void HandleApplyWidgetTreeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
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

	const TSharedPtr<FJsonObject>* TreeObj = nullptr;
	if (!Args->TryGetObjectField(TEXT("tree"), TreeObj) || !TreeObj || !TreeObj->IsValid())
	{
		OutError = TEXT("Missing required parameter: tree (use the JSON shape produced by get_widget_tree)");
		return;
	}

	UWidgetBlueprint* WBP = LoadWidgetBlueprintWithDiagnostic(AssetPath, OutError);
	if (!WBP) return;
	if (!WBP->WidgetTree)
	{
		OutError = TEXT("WidgetBlueprint has no WidgetTree.");
		return;
	}

	TArray<FString> CreatedNames, UpdatedNames, Warnings;

	auto IsCanvasStyleSlot = [](const TSharedPtr<FJsonObject>& Node) -> bool
	{
		if (!Node.IsValid()) return false;
		const TSharedPtr<FJsonObject>* SlotPropsObj = nullptr;
		if (!Node->TryGetObjectField(TEXT("slot_properties"), SlotPropsObj) || !SlotPropsObj) return false;
		static const TCHAR* CanvasKeys[] = { TEXT("anchors"), TEXT("Anchors"), TEXT("position"), TEXT("Position"),
			TEXT("size"), TEXT("Size"), TEXT("alignment"), TEXT("Alignment"), TEXT("layout_data"), TEXT("LayoutData"),
			TEXT("z_order"), TEXT("ZOrder") };
		for (const TCHAR* K : CanvasKeys)
		{
			if ((*SlotPropsObj)->HasField(K)) return true;
		}
		FString SlotClass;
		Node->TryGetStringField(TEXT("slot_class"), SlotClass);
		return SlotClass.Equals(TEXT("CanvasPanelSlot"), ESearchCase::IgnoreCase);
	};

	UPanelWidget* RootParent = nullptr;
	if (!WBP->WidgetTree->RootWidget)
	{
		FString RootClass;
		(*TreeObj)->TryGetStringField(TEXT("widget_class"), RootClass);
		UClass* ResolvedRootClass = ResolveWidgetClass_ForApply(RootClass);

		const bool bRootIsCanvasPanel   = ResolvedRootClass && ResolvedRootClass->IsChildOf(UCanvasPanel::StaticClass());
		const bool bRootHasCanvasSlot   = IsCanvasStyleSlot(*TreeObj);
		const bool bRootIsContentWidget = ResolvedRootClass && !ResolvedRootClass->IsChildOf(UPanelWidget::StaticClass());
		const bool bNeedsCanvasWrap = !bRootIsCanvasPanel && (bRootHasCanvasSlot || bRootIsContentWidget);

		if (bNeedsCanvasWrap)
		{
			UCanvasPanel* AutoRoot = WBP->WidgetTree->ConstructWidget<UCanvasPanel>(
				UCanvasPanel::StaticClass(),
				FBlueprintEditorUtils::FindUniqueKismetName(WBP, TEXT("RootCanvas")));
			if (!AutoRoot)
			{
				OutError = TEXT("Failed to construct auto-injected CanvasPanel root.");
				return;
			}
			WBP->WidgetTree->RootWidget = AutoRoot;
			CreatedNames.Add(AutoRoot->GetName());
			RootParent = AutoRoot;
		}
	}
	else
	{
		FString ProvidedName;
		(*TreeObj)->TryGetStringField(TEXT("widget_name"), ProvidedName);
		if (!ProvidedName.IsEmpty() && ProvidedName != WBP->WidgetTree->RootWidget->GetName())
		{
			RootParent = Cast<UPanelWidget>(WBP->WidgetTree->RootWidget);
			if (!RootParent)
			{
				OutError = FString::Printf(TEXT("Existing root '%s' is not a panel, can't parent new root '%s' under it."),
					*WBP->WidgetTree->RootWidget->GetName(), *ProvidedName);
				return;
			}
		}
	}

	UWidget* AppliedRoot = nullptr;
	if (!WBP->WidgetTree->RootWidget)
	{
		AppliedRoot = ApplyNodeRecursive(WBP, *TreeObj, nullptr, CreatedNames, UpdatedNames, Warnings);
		if (AppliedRoot)
		{
			WBP->WidgetTree->RootWidget = AppliedRoot;
		}
	}
	else
	{
		AppliedRoot = ApplyNodeRecursive(WBP, *TreeObj, RootParent, CreatedNames, UpdatedNames, Warnings);
	}

	if (CreatedNames.Num() > 0 || UpdatedNames.Num() > 0)
	{
		if (WBP->WidgetTree && WBP->WidgetTree->RootWidget)
		{
			UCanvasPanel* RootCanvas = Cast<UCanvasPanel>(WBP->WidgetTree->RootWidget);
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
					if (bFixed)
					{
						CSlot->SetOffsets(Offsets);
						Warnings.Add(FString::Printf(TEXT("Auto-fixed out-of-bounds position on '%s'"), *Child->GetName()));
					}
				}
			}
		}

		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WBP);
		WBP->GetPackage()->MarkPackageDirty();
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("user_widget_path"), AssetPath);
	Result->SetNumberField(TEXT("created_count"), CreatedNames.Num());
	Result->SetNumberField(TEXT("updated_count"), UpdatedNames.Num());

	auto ToJsonArray = [](const TArray<FString>& In)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		Out.Reserve(In.Num());
		for (const FString& S : In) Out.Add(MakeShared<FJsonValueString>(S));
		return Out;
	};
	Result->SetArrayField(TEXT("created"), ToJsonArray(CreatedNames));
	Result->SetArrayField(TEXT("updated"), ToJsonArray(UpdatedNames));
	Result->SetArrayField(TEXT("warnings"), ToJsonArray(Warnings));

	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
}

void HandleGetNamedSlotsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString AssetPath;
	if (!Args.IsValid() || !Args->TryGetStringField(TEXT("widget_blueprint_path"), AssetPath) || AssetPath.IsEmpty())
	{
		OutError = TEXT("widget_blueprint_path required");
		return;
	}
	UWidgetBlueprint* WBP = LoadWidgetBlueprintWithDiagnostic(AssetPath, OutError);
	if (!WBP || !WBP->WidgetTree) return;

	TArray<TSharedPtr<FJsonValue>> Slots;
	WBP->WidgetTree->ForEachWidget([&](UWidget* Widget)
	{
		if (!Widget) return;
		INamedSlotInterface* Host = Cast<INamedSlotInterface>(Widget);
		if (!Host) return;
		TArray<FName> SlotNames;
		Host->GetSlotNames(SlotNames);
		for (const FName& SlotName : SlotNames)
		{
			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("host_widget"), Widget->GetName());
			Entry->SetStringField(TEXT("slot_name"), SlotName.ToString());
			if (UWidget* Content = Host->GetContentForSlot(SlotName))
			{
				Entry->SetStringField(TEXT("content_widget"), Content->GetName());
				Entry->SetStringField(TEXT("content_class"), Content->GetClass()->GetName());
			}
			Slots.Add(MakeShared<FJsonValueObject>(Entry));
		}
	});

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetNumberField(TEXT("count"), Slots.Num());
	Result->SetArrayField(TEXT("slots"), Slots);

	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
}

void HandleSetNamedSlotContentFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString AssetPath, HostName, SlotName, WidgetClassSpec, NewWidgetName;
	Args->TryGetStringField(TEXT("widget_blueprint_path"), AssetPath);
	Args->TryGetStringField(TEXT("host_widget_name"), HostName);
	Args->TryGetStringField(TEXT("slot_name"), SlotName);
	Args->TryGetStringField(TEXT("widget_class"), WidgetClassSpec);
	Args->TryGetStringField(TEXT("widget_name"), NewWidgetName);

	if (AssetPath.IsEmpty() || HostName.IsEmpty() || SlotName.IsEmpty() || WidgetClassSpec.IsEmpty())
	{
		OutError = TEXT("widget_blueprint_path, host_widget_name, slot_name, widget_class required");
		return;
	}

	UWidgetBlueprint* WBP = LoadWidgetBlueprintWithDiagnostic(AssetPath, OutError);
	if (!WBP || !WBP->WidgetTree) return;

	UWidget* Host = WBP->WidgetTree->FindWidget(FName(*HostName));
	if (!Host) { OutError = FString::Printf(TEXT("host widget '%s' not found"), *HostName); return; }
	INamedSlotInterface* HostInterface = Cast<INamedSlotInterface>(Host);
	if (!HostInterface) { OutError = FString::Printf(TEXT("widget '%s' (class %s) does not implement INamedSlotInterface"), *HostName, *Host->GetClass()->GetName()); return; }

	TArray<FName> Slots;
	HostInterface->GetSlotNames(Slots);
	if (!Slots.Contains(FName(*SlotName)))
	{
		FString Available;
		for (int32 i = 0; i < Slots.Num(); ++i)
		{
			if (i > 0) Available += TEXT(", ");
			Available += Slots[i].ToString();
		}
		OutError = FString::Printf(TEXT("slot '%s' not on host '%s'. Available: %s"), *SlotName, *HostName, *Available);
		return;
	}

	UClass* WidgetClass = ResolveWidgetClass(WidgetClassSpec);
	if (!WidgetClass || !WidgetClass->IsChildOf(UWidget::StaticClass()))
	{
		OutError = FString::Printf(TEXT("widget_class '%s' did not resolve to a UWidget subclass"), *WidgetClassSpec);
		return;
	}

	if (NewWidgetName.IsEmpty()) NewWidgetName = FString::Printf(TEXT("%s_%s"), *HostName, *SlotName);
	const FName NewWidgetFName(*NewWidgetName);
	if (WBP->WidgetTree->FindWidget(NewWidgetFName))
	{
		OutError = FString::Printf(TEXT("widget named '%s' already exists in this tree"), *NewWidgetName);
		return;
	}

	WBP->Modify();
	WBP->WidgetTree->Modify();
	UWidget* NewContent = WBP->WidgetTree->ConstructWidget<UWidget>(WidgetClass, NewWidgetFName);
	HostInterface->SetContentForSlot(FName(*SlotName), NewContent);

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WBP);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("host_widget"), HostName);
	Result->SetStringField(TEXT("slot_name"), SlotName);
	Result->SetStringField(TEXT("content_widget"), NewContent->GetName());
	Result->SetStringField(TEXT("content_class"), NewContent->GetClass()->GetName());

	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
}

void HandleClearNamedSlotContentFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString AssetPath, HostName, SlotName;
	Args->TryGetStringField(TEXT("widget_blueprint_path"), AssetPath);
	Args->TryGetStringField(TEXT("host_widget_name"), HostName);
	Args->TryGetStringField(TEXT("slot_name"), SlotName);
	if (AssetPath.IsEmpty() || HostName.IsEmpty() || SlotName.IsEmpty())
	{
		OutError = TEXT("widget_blueprint_path, host_widget_name, slot_name required");
		return;
	}

	UWidgetBlueprint* WBP = LoadWidgetBlueprintWithDiagnostic(AssetPath, OutError);
	if (!WBP || !WBP->WidgetTree) return;

	UWidget* Host = WBP->WidgetTree->FindWidget(FName(*HostName));
	if (!Host) { OutError = FString::Printf(TEXT("host widget '%s' not found"), *HostName); return; }
	INamedSlotInterface* HostInterface = Cast<INamedSlotInterface>(Host);
	if (!HostInterface) { OutError = FString::Printf(TEXT("widget '%s' does not implement INamedSlotInterface"), *HostName); return; }

	WBP->Modify();
	WBP->WidgetTree->Modify();
	HostInterface->SetContentForSlot(FName(*SlotName), nullptr);
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WBP);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("host_widget"), HostName);
	Result->SetStringField(TEXT("slot_name"), SlotName);

	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
}

}
