// Copyright 2026, BlueprintsLab, All rights reserved

#include "Tools/BlueprintCDOTools.h"

#include "Engine/Blueprint.h"
#include "EditorAssetLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectHash.h"
#include "UObject/TextProperty.h"
#include "UObject/EnumProperty.h"
#include "UObject/SoftObjectPath.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "JsonObjectConverter.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "ScopedTransaction.h"
#include "MCPToolsLog.h"
#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <windows.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif

namespace BlueprintCDOTools
{

static bool ValidatePropertyShape(FProperty* Prop, FString& OutError, int32 Depth = 0)
{
	if (!Prop || Depth > 4) return true;
	if (FStructProperty* SP = CastField<FStructProperty>(Prop))
	{
		if (!SP->Struct)
		{
			OutError = FString::Printf(TEXT("Property '%s' is a struct field with null Struct pointer — the UDS/UScriptStruct it referenced is missing or unloaded. Cannot deserialise JSON into it."), *Prop->GetName());
			return false;
		}
		return true;
	}
	if (FArrayProperty* AP = CastField<FArrayProperty>(Prop)) return ValidatePropertyShape(AP->Inner, OutError, Depth + 1);
	if (FSetProperty*   STP = CastField<FSetProperty>(Prop))  return ValidatePropertyShape(STP->ElementProp, OutError, Depth + 1);
	if (FMapProperty*   MP = CastField<FMapProperty>(Prop))
	{
		return ValidatePropertyShape(MP->KeyProp, OutError, Depth + 1)
			&& ValidatePropertyShape(MP->ValueProp, OutError, Depth + 1);
	}
	return true;
}

#if PLATFORM_WINDOWS
static int32 SafeJsonValueToProperty(const TSharedPtr<FJsonValue>& Value, FProperty* Prop, void* ValuePtr)
{
	__try
	{
		return FJsonObjectConverter::JsonValueToUProperty(Value, Prop, ValuePtr, 0, 0) ? 0 : 2;
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		return 1;
	}
}
#else
static int32 SafeJsonValueToProperty(const TSharedPtr<FJsonValue>& Value, FProperty* Prop, void* ValuePtr)
{
	return FJsonObjectConverter::JsonValueToUProperty(Value, Prop, ValuePtr, 0, 0) ? 0 : 2;
}
#endif

static FString SafeEscapeJson(const FString& S)
{
	FString Out = S;
	Out.ReplaceInline(TEXT("\\"), TEXT("\\\\"), ESearchCase::CaseSensitive);
	Out.ReplaceInline(TEXT("\""), TEXT("\\\""), ESearchCase::CaseSensitive);
	Out.ReplaceInline(TEXT("\n"), TEXT("\\n"),  ESearchCase::CaseSensitive);
	Out.ReplaceInline(TEXT("\r"), TEXT("\\r"),  ESearchCase::CaseSensitive);
	Out.ReplaceInline(TEXT("\t"), TEXT("\\t"),  ESearchCase::CaseSensitive);
	return Out;
}

static FString SerializePropertyValue(FProperty* Prop, const void* Container)
{
	if (!Prop || !Container) return TEXT("null");

	const void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Container);

	if (const FBoolProperty* BoolProp = CastField<FBoolProperty>(Prop))
		return BoolProp->GetPropertyValue(ValuePtr) ? TEXT("true") : TEXT("false");

	if (const FByteProperty* ByteProp = CastField<FByteProperty>(Prop))
	{
		if (ByteProp->Enum)
		{
			const uint8 Val = ByteProp->GetPropertyValue(ValuePtr);
			return ByteProp->Enum->GetNameStringByValue((int64)Val);
		}
		return FString::FromInt((int32)ByteProp->GetPropertyValue(ValuePtr));
	}

	if (const FEnumProperty* EnumProp = CastField<FEnumProperty>(Prop))
	{
		const int64 Val = EnumProp->GetUnderlyingProperty()->GetSignedIntPropertyValue(ValuePtr);
		return EnumProp->GetEnum() ? EnumProp->GetEnum()->GetNameStringByValue(Val) : FString::FromInt((int32)Val);
	}

	if (const FNumericProperty* NumProp = CastField<FNumericProperty>(Prop))
		return NumProp->GetNumericPropertyValueToString(ValuePtr);

	if (const FStrProperty* StrProp = CastField<FStrProperty>(Prop))
		return StrProp->GetPropertyValue(ValuePtr);

	if (const FNameProperty* NameProp = CastField<FNameProperty>(Prop))
		return NameProp->GetPropertyValue(ValuePtr).ToString();

	if (const FTextProperty* TextProp = CastField<FTextProperty>(Prop))
		return TextProp->GetPropertyValue(ValuePtr).ToString();

	if (const FObjectPropertyBase* ObjProp = CastField<FObjectPropertyBase>(Prop))
	{
		UObject* Obj = ObjProp->GetObjectPropertyValue(ValuePtr);
		return Obj ? Obj->GetPathName() : TEXT("null");
	}

	if (const FSoftObjectProperty* SoftProp = CastField<FSoftObjectProperty>(Prop))
	{
		FSoftObjectPtr Soft = SoftProp->GetPropertyValue(ValuePtr);
		return Soft.IsNull() ? TEXT("null") : Soft.ToSoftObjectPath().ToString();
	}

	if (const FSoftClassProperty* SoftClassProp = CastField<FSoftClassProperty>(Prop))
	{
		FSoftObjectPtr Soft(SoftClassProp->GetPropertyValue(ValuePtr).ToSoftObjectPath());
		return Soft.IsNull() ? TEXT("null") : Soft.ToSoftObjectPath().ToString();
	}

	if (const FStructProperty* StructProp = CastField<FStructProperty>(Prop))
	{
		if (StructProp->Struct)
		{
			FString Out = TEXT("(");
			bool bFirst = true;
			for (TFieldIterator<FProperty> It(StructProp->Struct); It; ++It)
			{
				FProperty* SubProp = *It;
				if (!SubProp) continue;
				const void* SubValuePtr = SubProp->ContainerPtrToValuePtr<void>(ValuePtr);
				FString SubVal;
				if (const FBoolProperty* SubBool = CastField<FBoolProperty>(SubProp))
				{
					SubVal = SubBool->GetPropertyValue(SubValuePtr) ? TEXT("True") : TEXT("False");
				}
				else
				{
					SubProp->ExportTextItem_Direct(SubVal, SubValuePtr, nullptr, nullptr, PPF_None);
				}
				if (!bFirst) Out += TEXT(",");
				Out += SubProp->GetName() + TEXT("=") + SubVal;
				bFirst = false;
			}
			Out += TEXT(")");
			return Out;
		}
	}

	FString Exported;
	Prop->ExportTextItem_Direct(Exported, ValuePtr, nullptr, nullptr, PPF_None);
	return Exported;
}

static UClass* ResolveClassFromString(const FString& Input)
{
	FString Trimmed = Input;
	Trimmed.TrimStartAndEndInline();
	if (Trimmed.IsEmpty() || Trimmed.Equals(TEXT("None"), ESearchCase::IgnoreCase) || Trimmed.Equals(TEXT("null"), ESearchCase::IgnoreCase))
		return nullptr;

	if (UClass* Direct = LoadObject<UClass>(nullptr, *Trimmed))
		return Direct;

	if (Trimmed.StartsWith(TEXT("/")))
	{
		FString Normalized = Trimmed;
		int32 DotIdx;
		if (!Normalized.FindChar(TEXT('.'), DotIdx))
		{
			int32 LastSlash;
			if (Normalized.FindLastChar(TEXT('/'), LastSlash))
			{
				const FString Tail = Normalized.Mid(LastSlash + 1);
				Normalized = Normalized + TEXT(".") + Tail + TEXT("_C");
			}
		}
		else if (!Normalized.EndsWith(TEXT("_C")))
		{
			Normalized += TEXT("_C");
		}
		if (UClass* Loaded = LoadObject<UClass>(nullptr, *Normalized))
			return Loaded;
	}

	FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	TArray<FAssetData> Found;
	ARM.Get().GetAssetsByClass(UBlueprint::StaticClass()->GetClassPathName(), Found,  true);
	for (const FAssetData& A : Found)
	{
		if (A.AssetName.ToString().Equals(Trimmed, ESearchCase::IgnoreCase))
		{
			if (UBlueprint* AssetBP = Cast<UBlueprint>(A.GetAsset()))
				return AssetBP->GeneratedClass;
		}
	}
	return nullptr;
}

static UObject* LoadCDOTarget(const FString& AssetPath, UBlueprint*& OutBP, FString& OutError)
{
	OutBP = nullptr;
	UObject* Asset = UEditorAssetLibrary::LoadAsset(AssetPath);
	if (!Asset) { OutError = FString::Printf(TEXT("Could not load asset: %s"), *AssetPath); return nullptr; }

	OutBP = Cast<UBlueprint>(Asset);
	if (OutBP)
	{
		if (!OutBP->GeneratedClass) { OutError = TEXT("Blueprint has no GeneratedClass — compile it first."); return nullptr; }
		return OutBP->GeneratedClass->GetDefaultObject();
	}
	return Asset;
}

void HandleGetCDOPropertiesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }

	FString AssetPath;
	Args->TryGetStringField(TEXT("asset_path"), AssetPath);
	if (AssetPath.IsEmpty()) Args->TryGetStringField(TEXT("blueprint_path"), AssetPath);
	if (AssetPath.IsEmpty()) { OutError = TEXT("Missing required parameter: asset_path"); return; }

	FString Filter;
	Args->TryGetStringField(TEXT("name_filter"), Filter);
	if (Filter.IsEmpty()) Args->TryGetStringField(TEXT("filter"), Filter);

	UBlueprint* BP = nullptr;
	UObject* CDO = LoadCDOTarget(AssetPath, BP, OutError);
	if (!CDO) return;
	UClass* Class = BP ? BP->GeneratedClass.Get() : CDO->GetClass();
	if (!Class) { OutError = TEXT("Could not resolve class for CDO."); return; }

	TArray<TSharedPtr<FJsonValue>> Props;
	for (TFieldIterator<FProperty> It(Class, EFieldIteratorFlags::IncludeSuper); It; ++It)
	{
		FProperty* Prop = *It;
		if (!Prop || !Prop->HasAnyPropertyFlags(CPF_Edit)) continue;
		const FString Name = Prop->GetName();
		if (!Filter.IsEmpty() && !Name.Contains(Filter, ESearchCase::IgnoreCase)) continue;

		TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetStringField(TEXT("name"), Name);
		P->SetStringField(TEXT("type"), Prop->GetCPPType());
		P->SetStringField(TEXT("value"), SerializePropertyValue(Prop, CDO));
		P->SetBoolField(TEXT("writable"), !Prop->HasAnyPropertyFlags(CPF_EditConst));
		Props.Add(MakeShared<FJsonValueObject>(P));
	}

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("success"), true);
	Root->SetStringField(TEXT("asset_path"), AssetPath);
	Root->SetStringField(TEXT("class"), Class->GetName());
	Root->SetNumberField(TEXT("count"), Props.Num());
	Root->SetArrayField(TEXT("properties"), Props);
	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Root, W);
}

void HandleSetCDOPropertyFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }

	{
		const TArray<TSharedPtr<FJsonValue>>* BatchArr = nullptr;
		if ((Args->TryGetArrayField(TEXT("items"), BatchArr) || Args->TryGetArrayField(TEXT("defaults"), BatchArr)) && BatchArr)
		{
			FString CtxAsset;
			Args->TryGetStringField(TEXT("asset_path"), CtxAsset);
			if (CtxAsset.IsEmpty()) Args->TryGetStringField(TEXT("blueprint_path"), CtxAsset);

			int32 Completed = 0, Failed = 0;
			TArray<TSharedPtr<FJsonValue>> Results;
			for (int32 i = 0; i < BatchArr->Num(); ++i)
			{
				const TSharedPtr<FJsonObject>* ItObj = nullptr;
				if (!(*BatchArr)[i]->TryGetObject(ItObj) || !ItObj)
				{
					Failed++;
					continue;
				}
				TSharedRef<FJsonObject> Merged = MakeShared<FJsonObject>(**ItObj);
				if (!Merged->HasField(TEXT("asset_path")) && !Merged->HasField(TEXT("blueprint_path")) && !CtxAsset.IsEmpty())
					Merged->SetStringField(TEXT("asset_path"), CtxAsset);

				FString ItemJson, ItemErr;
				HandleSetCDOPropertyFromArgs(Merged, ItemJson, ItemErr);

				TSharedRef<FJsonObject> R = MakeShared<FJsonObject>();
				R->SetNumberField(TEXT("index"), i);
				if (ItemErr.IsEmpty()) { Completed++; R->SetBoolField(TEXT("success"), true); }
				else { Failed++; R->SetBoolField(TEXT("success"), false); R->SetStringField(TEXT("error"), ItemErr); }
				Results.Add(MakeShared<FJsonValueObject>(R));
			}

			TSharedRef<FJsonObject> Batch = MakeShared<FJsonObject>();
			Batch->SetBoolField(TEXT("success"), Failed == 0);
			Batch->SetNumberField(TEXT("completed"), Completed);
			Batch->SetNumberField(TEXT("failed"), Failed);
			if (Failed > 0) Batch->SetArrayField(TEXT("results"), Results);
			TSharedRef<TJsonWriter<>> BW = TJsonWriterFactory<>::Create(&OutJsonString);
			FJsonSerializer::Serialize(Batch, BW);
			return;
		}
	}

	FString AssetPath, PropertyName;
	Args->TryGetStringField(TEXT("asset_path"), AssetPath);
	if (AssetPath.IsEmpty()) Args->TryGetStringField(TEXT("blueprint_path"), AssetPath);
	Args->TryGetStringField(TEXT("property_name"), PropertyName);
	if (PropertyName.IsEmpty()) Args->TryGetStringField(TEXT("property"), PropertyName);
	if (PropertyName.IsEmpty()) Args->TryGetStringField(TEXT("name"), PropertyName);

	if (AssetPath.IsEmpty()) { OutError = TEXT("Missing required parameter: asset_path"); return; }
	if (PropertyName.IsEmpty()) { OutError = TEXT("Missing required parameter: property_name (or 'property')"); return; }

	TSharedPtr<FJsonValue> PropValue = Args->TryGetField(TEXT("property_value"));
	if (!PropValue.IsValid()) PropValue = Args->TryGetField(TEXT("value"));
	if (!PropValue.IsValid()) { OutError = TEXT("Missing required parameter: property_value (or 'value')"); return; }

	UBlueprint* BP = nullptr;
	UObject* CDO = LoadCDOTarget(AssetPath, BP, OutError);
	if (!CDO) return;

	UClass* Class = BP ? BP->GeneratedClass.Get() : CDO->GetClass();

	FProperty* Prop = Class->FindPropertyByName(FName(*PropertyName));
	if (!Prop)
	{
		for (TFieldIterator<FProperty> It(Class, EFieldIteratorFlags::IncludeSuper); It; ++It)
		{
			if (It->GetName().Equals(PropertyName, ESearchCase::IgnoreCase))
			{
				Prop = *It;
				break;
			}
		}
	}

	if (!Prop)
	{
		TArray<FString> Substring;
		TArray<FString> AllEditable;
		for (TFieldIterator<FProperty> It(Class, EFieldIteratorFlags::IncludeSuper); It; ++It)
		{
			if (!(It->PropertyFlags & CPF_Edit)) continue;
			AllEditable.AddUnique(It->GetName());
			if (It->GetName().Contains(PropertyName, ESearchCase::IgnoreCase))
				Substring.AddUnique(It->GetName());
		}
		if (BP)
		{
			for (const FBPVariableDescription& V : BP->NewVariables)
			{
				const FString N = V.VarName.ToString();
				AllEditable.AddUnique(N);
				if (N.Contains(PropertyName, ESearchCase::IgnoreCase))
					Substring.AddUnique(N);
			}
		}
		const FString Suggestions = Substring.Num() > 0 ? FString::Join(Substring, TEXT(", "))
			: (AllEditable.Num() > 0 ? FString::Join(AllEditable, TEXT(", ")) : TEXT("(no editable properties found — Blueprint may need compile_blueprint first)"));
		OutError = FString::Printf(TEXT("Property '%s' not found. Available editable properties: %s. (If this is a freshly-created Blueprint, run compile_blueprint first so the GeneratedClass picks up new variables.)"),
			*PropertyName, *Suggestions);
		return;
	}

	CDO->SetFlags(RF_Transactional);
	const FScopedTransaction Transaction(NSLOCTEXT("BlueprintCDOTools", "SetCDOProp", "Set CDO Property"));
	CDO->Modify();

	CDO->PreEditChange(Prop);

	void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(CDO);
	bool bWrote = false;

	if (FClassProperty* CP = CastField<FClassProperty>(Prop))
	{
		const FString StrValue = (PropValue->Type == EJson::String) ? PropValue->AsString()
			: (PropValue->Type == EJson::Null) ? FString() : PropValue->AsString();
		const bool bClearing = StrValue.IsEmpty()
			|| StrValue.Equals(TEXT("None"), ESearchCase::IgnoreCase)
			|| StrValue.Equals(TEXT("null"), ESearchCase::IgnoreCase);
		if (bClearing)
		{
			CP->SetObjectPropertyValue(ValuePtr, nullptr);
			bWrote = true;
		}
		else if (UClass* Resolved = ResolveClassFromString(StrValue))
		{
			if (CP->MetaClass && !Resolved->IsChildOf(CP->MetaClass))
			{
				OutError = FString::Printf(TEXT("Class '%s' does not inherit from required type '%s' for property '%s'."),
					*Resolved->GetName(), *CP->MetaClass->GetName(), *PropertyName);
				return;
			}
			CP->SetObjectPropertyValue(ValuePtr, Resolved);
			bWrote = true;
		}
		else
		{
			OutError = FString::Printf(TEXT("Could not resolve class '%s' for TSubclassOf<%s> property '%s'. Try the full path with '_C' (e.g. '/Game/Foo/BP_X.BP_X_C') or just the asset name."),
				*StrValue, CP->MetaClass ? *CP->MetaClass->GetName() : TEXT("UObject"), *PropertyName);
			return;
		}
	}
	else if (FSoftClassProperty* SCP = CastField<FSoftClassProperty>(Prop))
	{
		const FString StrValue = (PropValue->Type == EJson::String) ? PropValue->AsString()
			: (PropValue->Type == EJson::Null) ? FString() : PropValue->AsString();
		const bool bClearing = StrValue.IsEmpty()
			|| StrValue.Equals(TEXT("None"), ESearchCase::IgnoreCase)
			|| StrValue.Equals(TEXT("null"), ESearchCase::IgnoreCase);
		if (bClearing)
		{
			SCP->SetPropertyValue(ValuePtr, FSoftObjectPtr());
			bWrote = true;
		}
		else if (UClass* Resolved = ResolveClassFromString(StrValue))
		{
			if (SCP->MetaClass && !Resolved->IsChildOf(SCP->MetaClass))
			{
				OutError = FString::Printf(TEXT("Class '%s' does not inherit from required type '%s' for property '%s'."),
					*Resolved->GetName(), *SCP->MetaClass->GetName(), *PropertyName);
				return;
			}
			SCP->SetPropertyValue(ValuePtr, FSoftObjectPtr(FSoftObjectPath(Resolved)));
			bWrote = true;
		}
		else
		{
			OutError = FString::Printf(TEXT("Could not resolve soft class '%s' for TSoftClassPtr<%s> property '%s'."),
				*StrValue, SCP->MetaClass ? *SCP->MetaClass->GetName() : TEXT("UObject"), *PropertyName);
			return;
		}
	}
	else if (PropValue->Type == EJson::Object || PropValue->Type == EJson::Array)
	{
		FString ShapeErr;
		if (!ValidatePropertyShape(Prop, ShapeErr))
		{
			OutError = FString::Printf(TEXT("set_cdo_property: %s"), *ShapeErr);
			return;
		}
		const int32 ConvResult = SafeJsonValueToProperty(PropValue, Prop, ValuePtr);
		if (ConvResult == 1)
		{
			OutError = FString::Printf(
				TEXT("set_cdo_property: FJsonObjectConverter crashed converting JSON into property '%s'. "
				     "Most likely cause: a container/struct field references a UDS that isn't loaded, "
				     "OR the JSON shape doesn't match the property type. Inspect the property type via "
				     "get_blueprint_skeleton + simplify the value (try setting one field at a time)."),
				*PropertyName);
			return;
		}
		bWrote = (ConvResult == 0);
		if (!bWrote)
			OutError = FString::Printf(TEXT("FJsonObjectConverter could not convert JSON value to property '%s'"), *PropertyName);
	}
	else
	{
		FString StrValue;
		if (PropValue->Type == EJson::String)
			StrValue = PropValue->AsString();
		else if (PropValue->Type == EJson::Number)
			StrValue = FString::SanitizeFloat(PropValue->AsNumber());
		else if (PropValue->Type == EJson::Boolean)
			StrValue = PropValue->AsBool() ? TEXT("true") : TEXT("false");
		else if (PropValue->Type == EJson::Null)
			StrValue = TEXT("None");

		const TCHAR* WriteResult = Prop->ImportText_Direct(*StrValue, ValuePtr, CDO, PPF_None);
		bWrote = (WriteResult != nullptr);
		if (!bWrote)
			OutError = FString::Printf(TEXT("ImportText_Direct failed for property '%s' with value '%s'"), *PropertyName, *StrValue);
	}

	FPropertyChangedEvent Evt(Prop, EPropertyChangeType::ValueSet);
	CDO->PostEditChangeProperty(Evt);

	if (!bWrote) return;

	if (BP)
	{
		FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
		BP->MarkPackageDirty();
	}
	else
	{
		CDO->MarkPackageDirty();
	}

	FString VerifiedValue = SerializePropertyValue(Prop, CDO);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	Result->SetStringField(TEXT("property_name"), PropertyName);
	Result->SetStringField(TEXT("new_value"), VerifiedValue);

	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
}

}
