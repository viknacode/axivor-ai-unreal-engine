// Copyright 2026, BlueprintsLab, All rights reserved.

#include "Tools/DataAssetTools.h"
#include "Managers/SettingsManager.h"
#include "EditorAssetLibrary.h"
#include "Engine/UserDefinedEnum.h"
#include "Misc/EngineVersionComparison.h"
#if UE_VERSION_OLDER_THAN(5, 5, 0)
#include "Engine/UserDefinedStruct.h"
#else
#include "StructUtils/UserDefinedStruct.h"
#endif
#include "Engine/DataTable.h"
#include "Engine/DataAsset.h"
#include "AssetToolsModule.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/StructureEditorUtils.h"
#include "UserDefinedStructure/UserDefinedStructEditorData.h"
#include "EdGraph/EdGraphPin.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/UnrealType.h"
#include "ScopedTransaction.h"

namespace DataAssetTools
{

void HandleGetDataAssetDetails(const FString& AssetPath, FString& OutDetailsJson, FString& OutError)
{
	UObject* Asset = StaticLoadObject(UObject::StaticClass(), nullptr, *AssetPath);
	if (!Asset)
	{
		OutError = FString::Printf(TEXT("Could not load asset at path: %s"), *AssetPath);
		return;
	}

	TSharedPtr<FJsonObject> RootObject = MakeShareable(new FJsonObject());

	if (UUserDefinedEnum* Enum = Cast<UUserDefinedEnum>(Asset))
	{
		RootObject->SetStringField("type", "Enum");
		TArray<TSharedPtr<FJsonValue>> EnumValues;
		for (int32 i = 0; i < Enum->NumEnums() - 1; ++i)
		{
			FString DisplayName = Enum->GetDisplayNameTextByIndex(i).ToString();
			EnumValues.Add(MakeShareable(new FJsonValueString(DisplayName)));
		}
		RootObject->SetArrayField("values", EnumValues);
	}
	else if (UUserDefinedStruct* Struct = Cast<UUserDefinedStruct>(Asset))
	{
		RootObject->SetStringField("type", "Struct");
		TArray<TSharedPtr<FJsonValue>> StructMembers;
		const TArray<FStructVariableDescription>& VarDescriptions = FStructureEditorUtils::GetVarDesc(Struct);

		for (const FStructVariableDescription& VarDesc : VarDescriptions)
		{
			TSharedPtr<FJsonObject> MemberObject = MakeShareable(new FJsonObject());
			MemberObject->SetStringField("name", VarDesc.VarName.ToString());

			FEdGraphPinType PinType = VarDesc.ToPinType();
			if (PinType.PinSubCategoryObject.IsValid())
				MemberObject->SetStringField("type", PinType.PinSubCategoryObject->GetName());
			else
				MemberObject->SetStringField("type", PinType.PinCategory.ToString());
			if (!VarDesc.DefaultValue.IsEmpty())
				MemberObject->SetStringField("default_value", VarDesc.DefaultValue);
			StructMembers.Add(MakeShareable(new FJsonValueObject(MemberObject)));
		}
		RootObject->SetArrayField("members", StructMembers);
	}
	else if (Asset->IsA<UBlueprint>())
	{
		UClass* BpClass = Cast<UBlueprint>(Asset)->GeneratedClass;
		if (BpClass)
		{
			RootObject->SetStringField("type", "Blueprint");
			TArray<TSharedPtr<FJsonValue>> BpMembers;
			for (TFieldIterator<FProperty> PropIt(BpClass); PropIt; ++PropIt)
			{
				FProperty* Property = *PropIt;
				if (Property && Property->HasAnyPropertyFlags(CPF_Edit) && Property->GetOwnerClass() == BpClass)
				{
					TSharedPtr<FJsonObject> MemberObject = MakeShareable(new FJsonObject());
					MemberObject->SetStringField("name", Property->GetName());
					MemberObject->SetStringField("type", Property->GetClass()->GetName());
					UObject* CDO = BpClass->GetDefaultObject();
					if (CDO)
					{
						FString ValueStr;
						Property->ExportTextItem_Direct(ValueStr, Property->ContainerPtrToValuePtr<void>(CDO), nullptr, CDO, PPF_None);
						if (!ValueStr.IsEmpty()) MemberObject->SetStringField("value", ValueStr);
					}
					BpMembers.Add(MakeShareable(new FJsonValueObject(MemberObject)));
				}
			}
			RootObject->SetArrayField("members", BpMembers);
		}
	}
	else
	{
		RootObject->SetStringField("type", "DataObject");
		TArray<TSharedPtr<FJsonValue>> Members;
		int32 BrokenRefCount = 0;
		for (TFieldIterator<FProperty> PropIt(Asset->GetClass()); PropIt; ++PropIt)
		{
			FProperty* Property = *PropIt;
			if (Property && Property->HasAnyPropertyFlags(CPF_Edit))
			{
				TSharedPtr<FJsonObject> MemberObject = MakeShareable(new FJsonObject());
				MemberObject->SetStringField("name", Property->GetName());
				MemberObject->SetStringField("type", Property->GetClass()->GetName());
				FString ValueStr;
				if (const FDoubleProperty* DP = CastField<FDoubleProperty>(Property))
					ValueStr = FString::SanitizeFloat(DP->GetPropertyValue_InContainer(Asset));
				else if (const FFloatProperty* FP = CastField<FFloatProperty>(Property))
					ValueStr = FString::SanitizeFloat(FP->GetPropertyValue_InContainer(Asset));
				else if (const FBoolProperty* BP2 = CastField<FBoolProperty>(Property))
					ValueStr = BP2->GetPropertyValue_InContainer(Asset) ? TEXT("true") : TEXT("false");
				else if (const FIntProperty* IP = CastField<FIntProperty>(Property))
					ValueStr = FString::FromInt(IP->GetPropertyValue_InContainer(Asset));
				else if (const FStrProperty* SP = CastField<FStrProperty>(Property))
					ValueStr = SP->GetPropertyValue_InContainer(Asset);
				else if (const FNameProperty* NP = CastField<FNameProperty>(Property))
					ValueStr = NP->GetPropertyValue_InContainer(Asset).ToString();
				else
					Property->ExportTextItem_Direct(ValueStr, Property->ContainerPtrToValuePtr<void>(Asset), nullptr, Asset, PPF_None);
				if (!ValueStr.IsEmpty()) MemberObject->SetStringField("value", ValueStr);

				if (const FObjectProperty* OP = CastField<FObjectProperty>(Property))
				{
					UObject* LiveRef = OP->GetObjectPropertyValue_InContainer(Asset);
					const bool bResolves = LiveRef != nullptr;
					MemberObject->SetBoolField(TEXT("ref_resolves"), bResolves);
					if (!bResolves && !ValueStr.IsEmpty() && !ValueStr.Equals(TEXT("None")))
					{
						MemberObject->SetBoolField(TEXT("ref_broken"), true);
						MemberObject->SetStringField(TEXT("ref_hint"),
							TEXT("Path is stored but resolves to null — the referenced asset was likely deleted. Re-set this property to rebind."));
						++BrokenRefCount;
					}
				}
				else if (const FSoftObjectProperty* SOP = CastField<FSoftObjectProperty>(Property))
				{
					const FSoftObjectPtr& SoftRef = SOP->GetPropertyValue_InContainer(Asset);
					const bool bHasPath = !SoftRef.ToSoftObjectPath().IsNull();
					if (bHasPath)
					{
						UObject* Resolved = SoftRef.Get();
						const bool bResolves = Resolved != nullptr;
						MemberObject->SetBoolField(TEXT("ref_resolves"), bResolves);
					}
				}
				Members.Add(MakeShareable(new FJsonValueObject(MemberObject)));
			}
		}
		RootObject->SetArrayField("members", Members);
		if (BrokenRefCount > 0)
		{
			RootObject->SetNumberField(TEXT("broken_ref_count"), BrokenRefCount);
			RootObject->SetStringField(TEXT("broken_ref_summary"),
				FString::Printf(TEXT("%d object reference(s) have stored paths but resolve to null — see `ref_broken` flags on members. Re-set each via edit_data_asset_defaults to rebind."), BrokenRefCount));
		}
	}

	FString OutputString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
	FJsonSerializer::Serialize(RootObject.ToSharedRef(), Writer);
	OutDetailsJson = OutputString;
}

void HandleEditDataAssetDefaults(const FString& AssetPath, const TArray<TSharedPtr<FJsonValue>>& Edits, FString& OutError)
{
	UObject* Asset = UEditorAssetLibrary::LoadAsset(AssetPath);
	if (!Asset)
	{
		OutError = FString::Printf(TEXT("Could not load a valid asset at path: %s"), *AssetPath);
		return;
	}

	if (Asset->IsA<UDataTable>())
	{
		OutError = TEXT("This asset is a DataTable, not a Data Asset. Use 'edit_data_table_rows' instead. ")
		           TEXT("Format: {\"table_path\": \"...\", \"edits\": [{\"row_name\": \"RowName\", \"FieldName\": value, ...}]}");
		return;
	}

	UObject* TargetObject = nullptr;
	if (UBlueprint* BlueprintAsset = Cast<UBlueprint>(Asset))
	{
		if (BlueprintAsset->GeneratedClass)
		{
			TargetObject = BlueprintAsset->GeneratedClass->GetDefaultObject();
		}
	}
	else
	{
		TargetObject = Asset;
	}

	if (!TargetObject)
	{
		OutError = FString::Printf(TEXT("Could not resolve a valid object to modify for asset: %s"), *AssetPath);
		return;
	}

	const FScopedTransaction Transaction(FText::FromString(TEXT("MCP: Edit Asset Defaults")));
	TargetObject->Modify();

	for (const TSharedPtr<FJsonValue>& EditValue : Edits)
	{
		const TSharedPtr<FJsonObject>& EditObject = EditValue->AsObject();
		if (!EditObject.IsValid()) continue;

		FString PropertyName, PropValStr;
		if (!EditObject->TryGetStringField(TEXT("property_name"), PropertyName))
			if (!EditObject->TryGetStringField(TEXT("property"), PropertyName))
				if (!EditObject->TryGetStringField(TEXT("field"), PropertyName))
					if (!EditObject->TryGetStringField(TEXT("name"), PropertyName))
						EditObject->TryGetStringField(TEXT("member_name"), PropertyName);
		const TSharedPtr<FJsonValue> ValField = EditObject->TryGetField(TEXT("value"));
		if (PropertyName.IsEmpty() || !ValField.IsValid())
		{
			OutError = "Invalid edit operation format. Each edit must have property_name (alias: field/property/name) and value.";
			return;
		}
		switch (ValField->Type)
		{
			case EJson::String:  PropValStr = ValField->AsString(); break;
			case EJson::Number:
			{
				const double N = ValField->AsNumber();
				PropValStr = (N == FMath::TruncToDouble(N)) ? FString::Printf(TEXT("%lld"), (int64)N) : FString::SanitizeFloat(N);
				break;
			}
			case EJson::Boolean: PropValStr = ValField->AsBool() ? TEXT("true") : TEXT("false"); break;
			default:             ValField->TryGetString(PropValStr); break;
		}

		FProperty* Property = TargetObject->GetClass()->FindPropertyByName(FName(*PropertyName));

		if (!Property)
		{
			OutError = FString::Printf(TEXT("Property '%s' not found on asset '%s'."), *PropertyName, *AssetPath);
			continue;
		}

		void* PropertyData = Property->ContainerPtrToValuePtr<void>(TargetObject);
		if (Property->ImportText_Direct(*PropValStr, PropertyData, nullptr, PPF_None) == nullptr)
		{
			OutError += FString::Printf(TEXT("\nFailed to import value '%s' for property '%s'."), *PropValStr, *PropertyName);
		}
	}

	Asset->MarkPackageDirty();
}

void HandleCreateDataAsset(const FString& AssetName, const FString& ClassPath, const FString& SavePath, const TSharedPtr<FJsonObject>& Defaults, FString& OutJsonString, FString& OutError)
{
	if (AssetName.IsEmpty()) { OutError = TEXT("asset_name is required"); return; }

	UClass* DataAssetClass = nullptr;

	DataAssetClass = FindObject<UClass>(nullptr, *ClassPath);

	if (!DataAssetClass)
	{
		if (UBlueprint* BP = LoadObject<UBlueprint>(nullptr, *ClassPath))
		{
			DataAssetClass = BP->GeneratedClass;
		}
	}

	if (!DataAssetClass)
	{
		FAssetRegistryModule& RegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		TArray<FAssetData> AssetDataList;
		RegistryModule.Get().GetAllAssets(AssetDataList);
		for (const FAssetData& Data : AssetDataList)
		{
			if (Data.AssetName.ToString() == ClassPath || Data.AssetName.ToString() == (ClassPath + TEXT("_C")))
			{
				if (UBlueprint* BP = Cast<UBlueprint>(Data.GetAsset()))
				{
					DataAssetClass = BP->GeneratedClass;
					break;
				}
			}
		}
	}

	if (!DataAssetClass)
	{
		OutError = FString::Printf(TEXT("Could not find a class for '%s'. Provide the full Blueprint asset path, e.g. /Game/DA_Item.DA_Item"), *ClassPath);
		TSharedPtr<FJsonObject> Fail = MakeShareable(new FJsonObject);
		Fail->SetBoolField(TEXT("success"), false);
		Fail->SetStringField(TEXT("error"), OutError);
		TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJsonString);
		FJsonSerializer::Serialize(Fail.ToSharedRef(), W);
		return;
	}

	if (!DataAssetClass->IsChildOf(UDataAsset::StaticClass()))
	{
		OutError = FString::Printf(TEXT("Class '%s' is not a UDataAsset subclass."), *DataAssetClass->GetName());
		TSharedPtr<FJsonObject> Fail = MakeShareable(new FJsonObject);
		Fail->SetBoolField(TEXT("success"), false);
		Fail->SetStringField(TEXT("error"), OutError);
		TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJsonString);
		FJsonSerializer::Serialize(Fail.ToSharedRef(), W);
		return;
	}

	UClass* PrimaryDataAssetClass = UClass::TryFindTypeSlow<UClass>(TEXT("PrimaryDataAsset"));

	if (UBlueprint* SourceBP = Cast<UBlueprint>(DataAssetClass->ClassGeneratedBy))
	{
		FKismetEditorUtilities::CompileBlueprint(SourceBP, EBlueprintCompileOptions::SkipGarbageCollection);
		DataAssetClass = SourceBP->GeneratedClass;
		if (!DataAssetClass)
		{
			OutError = FString::Printf(TEXT("Blueprint '%s' failed to compile."), *SourceBP->GetName());
			TSharedPtr<FJsonObject> Fail = MakeShareable(new FJsonObject);
			Fail->SetBoolField(TEXT("success"), false);
			Fail->SetStringField(TEXT("error"), OutError);
			TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJsonString);
			FJsonSerializer::Serialize(Fail.ToSharedRef(), W);
			return;
		}
	}

	FString TargetSavePath = SavePath;
	while (TargetSavePath.EndsWith(TEXT("/"))) TargetSavePath = TargetSavePath.LeftChop(1);
	if (TargetSavePath.IsEmpty())
		TargetSavePath = TEXT("/Game");

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
	UObject* NewAsset = AssetToolsModule.Get().CreateAsset(AssetName, TargetSavePath, DataAssetClass, nullptr);

	if (!NewAsset)
	{
		OutError = FString::Printf(TEXT("Failed to create Data Asset '%s' of class '%s'"), *AssetName, *DataAssetClass->GetName());
		TSharedPtr<FJsonObject> Fail = MakeShareable(new FJsonObject);
		Fail->SetBoolField(TEXT("success"), false);
		Fail->SetStringField(TEXT("error"), OutError);
		TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJsonString);
		FJsonSerializer::Serialize(Fail.ToSharedRef(), W);
		return;
	}

	if (Defaults.IsValid())
	{
		for (auto& Pair : Defaults->Values)
		{
			if (!Pair.Value.IsValid()) continue;

			FProperty* Property = DataAssetClass->FindPropertyByName(FName(*Pair.Key));
			if (!Property)
			{
				continue;
			}

			FString ValueStr;
			if (Pair.Value->Type == EJson::String)       ValueStr = Pair.Value->AsString();
			else if (Pair.Value->Type == EJson::Number)  ValueStr = FString::SanitizeFloat(Pair.Value->AsNumber());
			else if (Pair.Value->Type == EJson::Boolean) ValueStr = Pair.Value->AsBool() ? TEXT("True") : TEXT("False");

			void* PropertyData = Property->ContainerPtrToValuePtr<void>(NewAsset);
			Property->ImportText_Direct(*ValueStr, PropertyData, nullptr, PPF_None);
		}
	}

	NewAsset->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), NewAsset->GetPathName());
	Result->SetStringField(TEXT("class"), DataAssetClass->GetName());
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
}

void HandleListDataAssetTypes(const FString& Filter, FString& OutJsonString, FString& OutError)
{

	TSharedPtr<FJsonObject> Res = MakeShareable(new FJsonObject);
	Res->SetBoolField(TEXT("success"), true);

	TArray<TSharedPtr<FJsonValue>> TypesArray;

	for (TObjectIterator<UClass> It; It; ++It)
	{
		UClass* Class = *It;
		if (!Class->IsChildOf(UDataAsset::StaticClass())) continue;
		if (Class == UDataAsset::StaticClass()) continue;
		if (Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated)) continue;

		FString ClassName = Class->GetName();
		if (!Filter.IsEmpty() && !ClassName.Contains(Filter, ESearchCase::IgnoreCase)) continue;

		TSharedPtr<FJsonObject> TypeObj = MakeShareable(new FJsonObject);
		TypeObj->SetStringField(TEXT("name"), ClassName);
		TypeObj->SetStringField(TEXT("path"), Class->GetPathName());
		TypeObj->SetBoolField(TEXT("is_native"), Class->ClassGeneratedBy == nullptr);

		int32 PropCount = 0;
		for (TFieldIterator<FProperty> PropIt(Class); PropIt; ++PropIt)
		{
			if (PropIt->HasAnyPropertyFlags(CPF_Edit)) PropCount++;
		}
		TypeObj->SetNumberField(TEXT("editable_property_count"), PropCount);

		TypesArray.Add(MakeShareable(new FJsonValueObject(TypeObj)));
	}

	Res->SetArrayField(TEXT("types"), TypesArray);
	Res->SetNumberField(TEXT("count"), TypesArray.Num());

	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Res.ToSharedRef(), W);
}

static void WriteFailureJson(FString& OutJsonString, const FString& Err)
{
	TSharedPtr<FJsonObject> ResultObject = MakeShareable(new FJsonObject);
	ResultObject->SetBoolField(TEXT("success"), false);
	ResultObject->SetStringField(TEXT("error"), Err);
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(ResultObject.ToSharedRef(), Writer);
}

void HandleGetDataAssetDetailsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString AssetPath;
	if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath))
	{
		OutError = TEXT("Missing required parameter: asset_path");
		WriteFailureJson(OutJsonString, OutError);
		return;
	}

	FString DetailsJson, Err;
	HandleGetDataAssetDetails(AssetPath, DetailsJson, Err);

	TSharedPtr<FJsonObject> ResultObject = MakeShareable(new FJsonObject);
	if (!Err.IsEmpty())
	{
		OutError = Err;
		ResultObject->SetBoolField(TEXT("success"), false);
		ResultObject->SetStringField(TEXT("error"), Err);
	}
	else
	{
		ResultObject->SetBoolField(TEXT("success"), true);
		ResultObject->SetStringField(TEXT("asset_path"), AssetPath);
		ResultObject->SetStringField(TEXT("details"), DetailsJson);
	}

	FString ResultString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResultString);
	FJsonSerializer::Serialize(ResultObject.ToSharedRef(), Writer);
	OutJsonString = ResultString;
}

void HandleEditDataAssetDefaultsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	TSharedPtr<FJsonObject> ResultObject = MakeShareable(new FJsonObject);

	FString AssetPath;
	const bool bHasTopAsset = Args->TryGetStringField(TEXT("asset_path"), AssetPath) && !AssetPath.IsEmpty();

	const TArray<TSharedPtr<FJsonValue>>* EditsArray = nullptr;
	TArray<TSharedPtr<FJsonValue>> SynthEdits;
	if (!Args->TryGetArrayField(TEXT("edits"), EditsArray))
		if (!Args->TryGetArrayField(TEXT("items"), EditsArray))
			if (!Args->TryGetArrayField(TEXT("properties"), EditsArray))
				Args->TryGetArrayField(TEXT("defaults"), EditsArray);

	if (!bHasTopAsset && EditsArray && EditsArray->Num() > 0)
	{
		TMap<FString, TArray<TSharedPtr<FJsonValue>>> ByAsset;
		TArray<FString> AssetOrder;
		bool bEveryItemHasAsset = true;
		for (const TSharedPtr<FJsonValue>& V : *EditsArray)
		{
			TSharedPtr<FJsonObject> O = (V.IsValid() && V->Type == EJson::Object) ? V->AsObject() : nullptr;
			FString AP;
			if (O.IsValid() && (O->TryGetStringField(TEXT("asset_path"), AP) || O->TryGetStringField(TEXT("path"), AP)) && !AP.IsEmpty())
			{
				if (!ByAsset.Contains(AP)) AssetOrder.Add(AP);
				ByAsset.FindOrAdd(AP).Add(V);
			}
			else { bEveryItemHasAsset = false; break; }
		}
		if (bEveryItemHasAsset && ByAsset.Num() > 0)
		{
			int32 OkAssets = 0, FailAssets = 0;
			FString BatchErrors;
			for (const FString& AP : AssetOrder)
			{
				FString ItemErr;
				HandleEditDataAssetDefaults(AP, ByAsset[AP], ItemErr);
				if (ItemErr.IsEmpty()) ++OkAssets;
				else { ++FailAssets; BatchErrors += FString::Printf(TEXT("[%s] %s\n"), *AP, *ItemErr); }
			}
			ResultObject->SetBoolField(TEXT("success"), FailAssets == 0);
			ResultObject->SetNumberField(TEXT("assets_edited"), OkAssets);
			ResultObject->SetNumberField(TEXT("assets_failed"), FailAssets);
			if (!BatchErrors.IsEmpty()) ResultObject->SetStringField(TEXT("errors"), BatchErrors);
			TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJsonString);
			FJsonSerializer::Serialize(ResultObject.ToSharedRef(), W);
			return;
		}
	}

	if (!bHasTopAsset)
	{
		OutError = TEXT("Missing required parameter: asset_path (or give each item its own asset_path for a cross-asset batch).");
		WriteFailureJson(OutJsonString, OutError);
		return;
	}

	if (!EditsArray)
	{
		FString PropName, Value;
		if (!Args->TryGetStringField(TEXT("property_name"), PropName))
			if (!Args->TryGetStringField(TEXT("property"), PropName))
				if (!Args->TryGetStringField(TEXT("field"), PropName))
					Args->TryGetStringField(TEXT("member_name"), PropName);
		const bool bHasValue = Args->TryGetStringField(TEXT("value"), Value);

		if (!PropName.IsEmpty() && bHasValue)
		{
			TSharedPtr<FJsonObject> One = MakeShared<FJsonObject>();
			One->SetStringField(TEXT("property_name"), PropName);
			One->SetStringField(TEXT("value"), Value);
			SynthEdits.Add(MakeShareable(new FJsonValueObject(One)));
			EditsArray = &SynthEdits;
		}
		else
		{
			OutError = TEXT("Missing required parameter: edits. Pass edits=[{property_name, value}] "
				"(alias: items=[...]; e.g. edits=[{\"property_name\":\"EffectData\",\"value\":\"(EffectType=Bleed,BuildupThreshold=100.0)\"}]). "
				"For one property you may also pass property_name + value directly.");
			WriteFailureJson(OutJsonString, OutError);
			return;
		}
	}

	UObject* Asset = UEditorAssetLibrary::LoadAsset(AssetPath);
	if (!Asset)
	{
		OutError = FString::Printf(TEXT("Could not load asset at path: %s"), *AssetPath);
		WriteFailureJson(OutJsonString, OutError);
		return;
	}

	UObject* TargetObject = nullptr;
	if (UBlueprint* BlueprintAsset = Cast<UBlueprint>(Asset))
	{
		if (BlueprintAsset->GeneratedClass)
			TargetObject = BlueprintAsset->GeneratedClass->GetDefaultObject();
	}
	else
	{
		TargetObject = Asset;
	}

	if (!TargetObject)
	{
		OutError = FString::Printf(TEXT("Could not resolve a valid object to modify for asset: %s"), *AssetPath);
		WriteFailureJson(OutJsonString, OutError);
		return;
	}

	int32 SuccessfulEdits = 0;
	int32 FailedEdits = 0;
	FString AllErrors;

	const FScopedTransaction Transaction(FText::FromString(TEXT("MCP: Edit Asset Defaults")));
	TargetObject->Modify();

	for (const TSharedPtr<FJsonValue>& EditValue : *EditsArray)
	{
		const TSharedPtr<FJsonObject>& EditObject = EditValue->AsObject();
		if (!EditObject.IsValid()) continue;

		FString PropertyName, PropValStr;
		auto TryGetString = [&EditObject](const TCHAR* Key, FString& Out) -> bool
		{
			return EditObject->TryGetStringField(Key, Out);
		};
		auto TryGetAnyAsString = [&EditObject](const TCHAR* Key, FString& Out) -> bool
		{
			TSharedPtr<FJsonValue> V = EditObject->TryGetField(Key);
			if (!V.IsValid()) return false;
			if (V->Type == EJson::String) { Out = V->AsString(); return true; }
			if (V->Type == EJson::Number) { Out = FString::SanitizeFloat(V->AsNumber()); return true; }
			if (V->Type == EJson::Boolean){ Out = V->AsBool() ? TEXT("true") : TEXT("false"); return true; }
			if (V->Type == EJson::Null)   { Out = TEXT(""); return true; }
			TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> W =
				TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
			FJsonSerializer::Serialize(V, FString(), W);
			return true;
		};
		bool bHaveName = TryGetString(TEXT("property_name"), PropertyName)
			|| TryGetString(TEXT("name"),     PropertyName)
			|| TryGetString(TEXT("field"),    PropertyName)
			|| TryGetString(TEXT("key"),      PropertyName)
			|| TryGetString(TEXT("property"), PropertyName);
		bool bHaveValue = TryGetAnyAsString(TEXT("value"),         PropValStr)
			|| TryGetAnyAsString(TEXT("property_value"), PropValStr)
			|| TryGetAnyAsString(TEXT("default_value"),  PropValStr);

		if (!bHaveName && !bHaveValue && EditObject->Values.Num() == 1)
		{
			static const TSet<FString> MetaKeys = {
				TEXT("property_name"), TEXT("name"), TEXT("field"), TEXT("key"), TEXT("property"),
				TEXT("value"), TEXT("property_value"), TEXT("default_value")
			};
			for (const auto& Pair : EditObject->Values)
			{
				const FString PairKey(*Pair.Key);
				if (!MetaKeys.Contains(PairKey))
				{
					PropertyName = PairKey;
					if (Pair.Value.IsValid()) TryGetAnyAsString(*PairKey, PropValStr);
					bHaveName = bHaveValue = true;
				}
				break;
			}
		}

		if (!bHaveName || !bHaveValue)
		{
			TArray<FString> KeysSeen;
			KeysSeen.Reserve(EditObject->Values.Num());
			for (const auto& P : EditObject->Values) KeysSeen.Add(FString(*P.Key));
			AllErrors += FString::Printf(TEXT("Invalid edit operation format — got keys [%s], "
				"expected {\"property_name\":\"X\",\"value\":\"Y\"} (aliases: name/field/key/property + value/property_value/default_value, OR a bare {\"X\":\"Y\"} pair). "),
				*FString::Join(KeysSeen, TEXT(", ")));
			FailedEdits++;
			continue;
		}

		FProperty* FinalProperty = nullptr;
		void* FinalContainerPtr = TargetObject;
		UStruct* CurrentStruct = TargetObject->GetClass();

		TArray<FString> PropertyPathParts;
		if (PropertyName.Contains(TEXT(".")))
		{
			PropertyName.ParseIntoArray(PropertyPathParts, TEXT("."));
			for (int32 i = 0; i < PropertyPathParts.Num(); ++i)
			{
				FProperty* CurrentProp = CurrentStruct->FindPropertyByName(FName(*PropertyPathParts[i]));
				if (!CurrentProp)
				{
					AllErrors += FString::Printf(TEXT("Property '%s' not found on '%s'. "), *PropertyPathParts[i], *CurrentStruct->GetName());
					FailedEdits++;
					break;
				}

				if (i == PropertyPathParts.Num() - 1)
				{
					FinalProperty = CurrentProp;
				}
				else
				{
					if (FStructProperty* StructProp = CastField<FStructProperty>(CurrentProp))
					{
						CurrentStruct = StructProp->Struct;
						FinalContainerPtr = StructProp->ContainerPtrToValuePtr<void>(FinalContainerPtr);
						if (!FinalContainerPtr)
						{
							AllErrors += FString::Printf(TEXT("Container pointer became null while traversing path at '%s'. "), *PropertyPathParts[i]);
							FailedEdits++;
							break;
						}
					}
					else
					{
						AllErrors += FString::Printf(TEXT("Property '%s' is not a struct and cannot be traversed. "), *PropertyPathParts[i]);
						FailedEdits++;
						break;
					}
				}
			}

			if (!FinalProperty || FailedEdits > 0)
			{
				continue;
			}
		}
		else
		{
			FinalProperty = TargetObject->GetClass()->FindPropertyByName(FName(*PropertyName));
			FinalContainerPtr = TargetObject;
		}

		if (!FinalProperty)
		{
			AllErrors += FString::Printf(TEXT("Property '%s' not found. "), *PropertyName);
			FailedEdits++;
			continue;
		}

		void* PropertyData = FinalProperty->ContainerPtrToValuePtr<void>(FinalContainerPtr);

		FString FinalPropValStr = PropValStr;
		if (FinalProperty->IsA<FTextProperty>() && !PropValStr.StartsWith(TEXT("\"")) && !PropValStr.StartsWith(TEXT("'")))
			FinalPropValStr = FString::Printf(TEXT("\"%s\""), *PropValStr);

		if (FinalProperty->ImportText_Direct(*FinalPropValStr, PropertyData, nullptr, PPF_None) != nullptr)
			SuccessfulEdits++;
		else
		{
			AllErrors += FString::Printf(TEXT("Failed to set '%s'. "), *PropertyName);
			FailedEdits++;
		}
	}

	Asset->MarkPackageDirty();

	ResultObject->SetBoolField(TEXT("success"), FailedEdits == 0);
	ResultObject->SetNumberField(TEXT("successful_edits"), SuccessfulEdits);
	ResultObject->SetNumberField(TEXT("failed_edits"), FailedEdits);
	if (!AllErrors.IsEmpty())
		ResultObject->SetStringField(TEXT("errors"), AllErrors);

	if (FailedEdits > 0) OutError = AllErrors;

	FString ResultString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResultString);
	FJsonSerializer::Serialize(ResultObject.ToSharedRef(), Writer);
	OutJsonString = ResultString;
}

void HandleCreateDataAssetFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString AssetName;
	Args->TryGetStringField(TEXT("name"), AssetName);
	if (AssetName.IsEmpty()) Args->TryGetStringField(TEXT("asset_name"), AssetName);
	if (AssetName.IsEmpty())
	{
		OutError = TEXT("Missing required parameter: name");
		WriteFailureJson(OutJsonString, OutError);
		return;
	}

	FString ClassPath;
	Args->TryGetStringField(TEXT("class_path"), ClassPath);
	if (ClassPath.IsEmpty()) Args->TryGetStringField(TEXT("parent_class"), ClassPath);
	if (ClassPath.IsEmpty())
	{
		OutError = TEXT("Missing required parameter: class_path (or parent_class) — the fully-qualified class path of your UDataAsset subclass, e.g. \"/Game/MyProject/DA_MyData.DA_MyData_C\" or \"/Script/Engine.DataAsset\"");
		WriteFailureJson(OutJsonString, OutError);
		return;
	}

	FString SavePath;
	Args->TryGetStringField(TEXT("save_path"), SavePath);
	if (SavePath.IsEmpty()) SavePath = FSettingsManager::GetDefaultSavePath();
	while (SavePath.EndsWith(TEXT("/"))) SavePath = SavePath.LeftChop(1);

	TSharedPtr<FJsonObject> NormalizedDefaults;
	const TSharedPtr<FJsonObject>* DefaultsObj = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* DefaultsArray = nullptr;
	if (Args->TryGetObjectField(TEXT("defaults"), DefaultsObj) && DefaultsObj)
	{
		NormalizedDefaults = *DefaultsObj;
	}
	else if (Args->TryGetArrayField(TEXT("defaults"), DefaultsArray) && DefaultsArray)
	{
		NormalizedDefaults = MakeShareable(new FJsonObject);
		for (const TSharedPtr<FJsonValue>& Item : *DefaultsArray)
		{
			const TSharedPtr<FJsonObject>* ItemObj = nullptr;
			if (!Item->TryGetObject(ItemObj) || !ItemObj) continue;
			FString PropName;
			if (!(*ItemObj)->TryGetStringField(TEXT("name"), PropName)) continue;
			TSharedPtr<FJsonValue> Val = (*ItemObj)->TryGetField(TEXT("value"));
			if (Val.IsValid()) NormalizedDefaults->SetField(PropName, Val);
		}
	}

	HandleCreateDataAsset(AssetName, ClassPath, SavePath, NormalizedDefaults, OutJsonString, OutError);
}

void HandleListDataAssetTypesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString Filter;
	Args->TryGetStringField(TEXT("filter"), Filter);
	HandleListDataAssetTypes(Filter, OutJsonString, OutError);
}

}
