// Copyright 2026, BlueprintsLab, All rights reserved

#include "Tools/DataTableTools.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "ContentBrowserItemPath.h"
#include "EditorAssetLibrary.h"
#include "AssetToolsModule.h"
#include "Engine/DataTable.h"
#include "Serialization/JsonSerializer.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace DataTableTools
{

static FString JsonValueToImportText(const TSharedPtr<FJsonValue>& Value)
{
	if (!Value.IsValid()) return TEXT("");

	switch (Value->Type)
	{
	case EJson::String:
		return FString::Printf(TEXT("\"%s\""), *Value->AsString());
	case EJson::Number:
		return FString::SanitizeFloat(Value->AsNumber());
	case EJson::Boolean:
		return Value->AsBool() ? TEXT("True") : TEXT("False");
	case EJson::Array:
	{
		const TArray<TSharedPtr<FJsonValue>>& Arr = Value->AsArray();
		FString Result = TEXT("(");
		for (int32 i = 0; i < Arr.Num(); i++)
		{
			if (i > 0) Result += TEXT(",");
			FString ElemStr = JsonValueToImportText(Arr[i]);
			if (Arr[i].IsValid() && Arr[i]->Type == EJson::String)
				ElemStr = Arr[i]->AsString();
			Result += ElemStr;
		}
		Result += TEXT(")");
		return Result;
	}
	case EJson::Object:
	{
		TSharedPtr<FJsonObject> Obj = Value->AsObject();
		if (!Obj.IsValid()) return TEXT("");
		FString Result = TEXT("(");
		bool bFirst = true;
		for (const auto& Pair : Obj->Values)
		{
			if (!bFirst) Result += TEXT(",");
			bFirst = false;
			FString ValStr;
			if (Pair.Value.IsValid())
			{
				if (Pair.Value->Type == EJson::String)
					ValStr = FString::Printf(TEXT("\"%s\""), *Pair.Value->AsString());
				else
					ValStr = JsonValueToImportText(Pair.Value);
			}
			Result += FString::Printf(TEXT("%s=%s"), *Pair.Key, *ValStr);
		}
		Result += TEXT(")");
		return Result;
	}
	default:
		return TEXT("");
	}
}

void HandleCreateDataTable(const FString& TableName, const FString& SavePath, const FString& RowStructPath, const TArray<TSharedPtr<FJsonValue>>& Rows, FString& OutJsonString, FString& OutError)
{
	if (TableName.IsEmpty()) { OutError = TEXT("table name is required (use 'name' or 'table_name' parameter)"); return; }

	if (RowStructPath.IsEmpty())
	{
		OutError = TEXT("DataTable requires a row struct. Create the UserStruct first (data create_asset asset_type='UserStruct'), then pass its full path as struct_type/row_struct (e.g. struct_type='/Game/Folder/FMyRow.FMyRow'). A DataTable with no row struct cannot hold rows.");
		return;
	}

	FString TargetSavePath = SavePath;
	while (TargetSavePath.EndsWith(TEXT("/"))) TargetSavePath = TargetSavePath.LeftChop(1);

	if (TargetSavePath.IsEmpty())
	{
		FContentBrowserModule& CB = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
		FContentBrowserItemPath CurrentPathItem = CB.Get().GetCurrentPath();
		if (CurrentPathItem.HasInternalPath())
		{
			TargetSavePath = CurrentPathItem.GetInternalPathString();
		}
	}
	if (TargetSavePath.IsEmpty()) TargetSavePath = TEXT("/Game/DataTables");

	UScriptStruct* RowStruct = nullptr;
	if (!RowStructPath.IsEmpty())
	{
		if (!RowStructPath.Contains(TEXT("/")))
		{
			FString StructName = RowStructPath;
			if (StructName.StartsWith(TEXT("F"))) StructName = StructName.Mid(1);

			static const TArray<FString> SearchPackages = {
				TEXT("/Script/Engine"),
				TEXT("/Script/CoreUObject"),
				TEXT("/Script/GameplayAbilities"),
				TEXT("/Script/Game"),
			};
			for (const FString& Pkg : SearchPackages)
			{
				FString TryPath = FString::Printf(TEXT("%s.%s"), *Pkg, *StructName);
				RowStruct = FindObject<UScriptStruct>(nullptr, *TryPath);
				if (RowStruct) break;
			}
			if (!RowStruct)
				RowStruct = LoadObject<UScriptStruct>(nullptr, *RowStructPath);
			if (!RowStruct)
			{
				FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
				TArray<FAssetData> FoundAssets;
				ARM.Get().GetAssetsByClass(FTopLevelAssetPath(TEXT("/Script/Engine"), TEXT("UserDefinedStruct")), FoundAssets);
				for (const FAssetData& AD : FoundAssets)
				{
					if (AD.AssetName.ToString().Equals(RowStructPath, ESearchCase::IgnoreCase))
					{
						RowStruct = Cast<UScriptStruct>(AD.GetAsset());
						break;
					}
				}
			}
			if (!RowStruct)
				RowStruct = FindFirstObject<UScriptStruct>(*RowStructPath, EFindFirstObjectOptions::None);
			if (!RowStruct && StructName != RowStructPath)
				RowStruct = FindFirstObject<UScriptStruct>(*StructName, EFindFirstObjectOptions::None);
		}
		else
		{
			RowStruct = LoadObject<UScriptStruct>(nullptr, *RowStructPath);
		}

		if (!RowStruct)
		{
			OutError = FString::Printf(TEXT("Could not load row struct '%s'. If this struct was just created, use its FULL asset path: /Game/Folder/S_Name.S_Name (short names are not indexed immediately after creation)."), *RowStructPath);
			TSharedPtr<FJsonObject> ResultObject = MakeShareable(new FJsonObject);
			ResultObject->SetBoolField(TEXT("success"), false);
			ResultObject->SetStringField(TEXT("error"), OutError);
			FString ResultString;
			TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResultString);
			FJsonSerializer::Serialize(ResultObject.ToSharedRef(), Writer);
			OutJsonString = ResultString;
			return;
		}
	}

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
	UObject* NewAsset = AssetToolsModule.Get().CreateAsset(TableName, TargetSavePath, UDataTable::StaticClass(), nullptr);

	UDataTable* DataTable = Cast<UDataTable>(NewAsset);
	if (!DataTable)
	{
		OutError = TEXT("Failed to create DataTable asset");
		TSharedPtr<FJsonObject> ResultObject = MakeShareable(new FJsonObject);
		ResultObject->SetBoolField(TEXT("success"), false);
		ResultObject->SetStringField(TEXT("error"), OutError);
		FString ResultString;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResultString);
		FJsonSerializer::Serialize(ResultObject.ToSharedRef(), Writer);
		OutJsonString = ResultString;
		return;
	}

	if (RowStruct)
	{
		DataTable->RowStruct = RowStruct;
	}

	int32 RowsAdded = 0;
	if (Rows.Num() > 0 && RowStruct)
	{
		for (const TSharedPtr<FJsonValue>& RowValue : Rows)
		{
			const TSharedPtr<FJsonObject>* RowObj = nullptr;
			if (!RowValue->TryGetObject(RowObj) || !RowObj) continue;

			FString RowName;
			if (!(*RowObj)->TryGetStringField(TEXT("row_name"), RowName))
				if (!(*RowObj)->TryGetStringField(TEXT("RowName"), RowName))
					(*RowObj)->TryGetStringField(TEXT("name"), RowName);
			if (RowName.IsEmpty()) continue;

			uint8* RowData = static_cast<uint8*>(FMemory::Malloc(RowStruct->GetStructureSize()));
			RowStruct->InitializeStruct(RowData);

			const TSharedPtr<FJsonObject>* FieldsSubObj = nullptr;
			const auto& FieldSource =
				((*RowObj)->TryGetObjectField(TEXT("fields"), FieldsSubObj) && FieldsSubObj)
				? (*FieldsSubObj)->Values : (*RowObj)->Values;
			for (auto& FieldPair : FieldSource)
			{
				const FString FieldName(*FieldPair.Key);
				TSharedPtr<FJsonValue> FieldValue = FieldPair.Value;

				if (FieldName == TEXT("row_name") || FieldName == TEXT("RowName") || FieldName == TEXT("name"))
				{
					continue;
				}

				FProperty* MatchedProperty = nullptr;
				for (TFieldIterator<FProperty> PropIt(RowStruct); PropIt; ++PropIt)
				{
					FProperty* Property = *PropIt;
					if (Property && !Property->IsNative())
					{
						FString PropName = Property->GetName();
						if (PropName.StartsWith(FieldName + TEXT("_")) || PropName.Equals(FieldName, ESearchCase::IgnoreCase))
						{
							MatchedProperty = Property;
							break;
						}
					}
				}
				if (!MatchedProperty)
				{
					for (TFieldIterator<FProperty> PropIt(RowStruct); PropIt; ++PropIt)
					{
						FProperty* Property = *PropIt;
						if (Property && Property->IsNative() && Property->GetName().Equals(FieldName, ESearchCase::IgnoreCase))
						{
							MatchedProperty = Property;
							break;
						}
					}
				}

				if (!MatchedProperty || !FieldValue.IsValid())
				{
					continue;
				}

				FString PropertyValueStr;
				bool bFoundValue = false;

				switch (FieldValue->Type)
				{
					case EJson::String:
						PropertyValueStr = FieldValue->AsString();
						bFoundValue = true;
						break;
					case EJson::Number:
						PropertyValueStr = FString::SanitizeFloat(FieldValue->AsNumber());
						bFoundValue = true;
						break;
					case EJson::Boolean:
						PropertyValueStr = FieldValue->AsBool() ? TEXT("True") : TEXT("False");
						bFoundValue = true;
						break;
					case EJson::Array:
					case EJson::Object:
						PropertyValueStr = JsonValueToImportText(FieldValue);
						bFoundValue = !PropertyValueStr.IsEmpty();
						break;
					default:
						break;
				}

				if (bFoundValue && !PropertyValueStr.IsEmpty())
				{
					MatchedProperty->ImportText_Direct(*PropertyValueStr, MatchedProperty->ContainerPtrToValuePtr<void>(RowData), nullptr, PPF_None);
				}
			}

			TMap<FName, uint8*>& MutableRowMap = const_cast<TMap<FName, uint8*>&>(DataTable->GetRowMap());
			MutableRowMap.Add(FName(*RowName), RowData);
			RowsAdded++;
		}
	}

	DataTable->MarkPackageDirty();

	TSharedPtr<FJsonObject> ResultObject = MakeShareable(new FJsonObject);
	ResultObject->SetBoolField(TEXT("success"), true);
	ResultObject->SetStringField(TEXT("asset_path"), DataTable->GetPathName());
	ResultObject->SetNumberField(TEXT("rows_added"), RowsAdded);
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(ResultObject.ToSharedRef(), Writer);
}

void HandleAddDataTableRows(const FString& TablePath, const TArray<TSharedPtr<FJsonValue>>& Rows, FString& OutJsonString, FString& OutError)
{

	UDataTable* DataTable = FindObject<UDataTable>(nullptr, *TablePath);
	if (!DataTable)
		DataTable = LoadObject<UDataTable>(nullptr, *TablePath);
	if (!DataTable)
		DataTable = Cast<UDataTable>(UEditorAssetLibrary::LoadAsset(TablePath));
	if (!DataTable)
	{
		OutError = FString::Printf(TEXT("Could not load DataTable: '%s'. Make sure the asset_path is the full package path (e.g. /Game/Folder/DT_Name or /Game/Folder/DT_Name.DT_Name)"), *TablePath);
		TSharedPtr<FJsonObject> ResultObject = MakeShareable(new FJsonObject);
		ResultObject->SetBoolField(TEXT("success"), false);
		ResultObject->SetStringField(TEXT("error"), OutError);
		FString ResultString;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResultString);
		FJsonSerializer::Serialize(ResultObject.ToSharedRef(), Writer);
		OutJsonString = ResultString;
		return;
	}

	UScriptStruct* RowStruct = DataTable->RowStruct;
	if (!RowStruct)
	{
		OutError = TEXT("DataTable has no row struct defined");
		TSharedPtr<FJsonObject> ResultObject = MakeShareable(new FJsonObject);
		ResultObject->SetBoolField(TEXT("success"), false);
		ResultObject->SetStringField(TEXT("error"), OutError);
		FString ResultString;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResultString);
		FJsonSerializer::Serialize(ResultObject.ToSharedRef(), Writer);
		OutJsonString = ResultString;
		return;
	}

	int32 RowsAdded = 0;
	for (const TSharedPtr<FJsonValue>& RowValue : Rows)
	{
		const TSharedPtr<FJsonObject>* RowObj = nullptr;
		if (!RowValue->TryGetObject(RowObj) || !RowObj) continue;

		FString RowName;
		if (!(*RowObj)->TryGetStringField(TEXT("row_name"), RowName))
			if (!(*RowObj)->TryGetStringField(TEXT("RowName"), RowName))
				(*RowObj)->TryGetStringField(TEXT("name"), RowName);
		if (RowName.IsEmpty())
		{
			continue;
		}

		uint8* RowData = static_cast<uint8*>(FMemory::Malloc(RowStruct->GetStructureSize()));
		RowStruct->InitializeStruct(RowData);

		const TSharedPtr<FJsonObject>* FieldsSubObj2 = nullptr;
		const auto& FieldSource2 =
			((*RowObj)->TryGetObjectField(TEXT("fields"), FieldsSubObj2) && FieldsSubObj2)
			? (*FieldsSubObj2)->Values : (*RowObj)->Values;

		for (auto& FieldPair : FieldSource2)
		{
			const FString FieldName(*FieldPair.Key);
			TSharedPtr<FJsonValue> FieldValue = FieldPair.Value;

			if (FieldName == TEXT("row_name") || FieldName == TEXT("RowName") || FieldName == TEXT("name")) continue;
			if (!FieldValue.IsValid()) continue;

			FProperty* MatchedProperty = nullptr;
			for (TFieldIterator<FProperty> PropIt(RowStruct); PropIt; ++PropIt)
			{
				FProperty* Property = *PropIt;
				if (Property && !Property->IsNative())
				{
					const FString PropName = Property->GetName();
					if (PropName.StartsWith(FieldName + TEXT("_")) || PropName.Equals(FieldName, ESearchCase::IgnoreCase))
					{
						MatchedProperty = Property;
						break;
					}
				}
			}
			if (!MatchedProperty)
			{
				for (TFieldIterator<FProperty> PropIt(RowStruct); PropIt; ++PropIt)
				{
					FProperty* Property = *PropIt;
					if (Property && Property->IsNative() && Property->GetName().Equals(FieldName, ESearchCase::IgnoreCase))
					{
						MatchedProperty = Property;
						break;
					}
				}
			}

			if (!MatchedProperty)
			{
				continue;
			}

			FString PropertyValueStr;
			bool bFoundValue = false;
			switch (FieldValue->Type)
			{
				case EJson::String:
					PropertyValueStr = FieldValue->AsString();
					bFoundValue = true;
					break;
				case EJson::Number:
					PropertyValueStr = FString::SanitizeFloat(FieldValue->AsNumber());
					bFoundValue = true;
					break;
				case EJson::Boolean:
					PropertyValueStr = FieldValue->AsBool() ? TEXT("True") : TEXT("False");
					bFoundValue = true;
					break;
				case EJson::Array:
				case EJson::Object:
					PropertyValueStr = JsonValueToImportText(FieldValue);
					bFoundValue = !PropertyValueStr.IsEmpty();
					break;
				default:
					break;
			}

			if (bFoundValue)
			{
				void* PropertyData = MatchedProperty->ContainerPtrToValuePtr<void>(RowData);

				bool bSet = false;
				if (FByteProperty* ByteProp = CastField<FByteProperty>(MatchedProperty))
				{
					if (UEnum* Enum = ByteProp->GetIntPropertyEnum())
					{
						FString ShortVal = PropertyValueStr;
						int32 ColonPos = INDEX_NONE;
						if (ShortVal.FindLastChar(TEXT(':'), ColonPos))
							ShortVal = ShortVal.Mid(ColonPos + 1).TrimStartAndEnd();
						for (int32 Idx = 0; Idx < Enum->NumEnums() - 1; Idx++)
						{
							FString DisplayName = Enum->GetDisplayNameTextByIndex(Idx).ToString();
							FString EntryName   = Enum->GetNameStringByIndex(Idx);
							int32 DC = INDEX_NONE;
							if (EntryName.FindLastChar(TEXT(':'), DC)) EntryName = EntryName.Mid(DC + 1).TrimStartAndEnd();
							FString EntryNoSuffix = EntryName;
							int32 US = INDEX_NONE;
							if (EntryNoSuffix.FindLastChar(TEXT('_'), US)) EntryNoSuffix = EntryNoSuffix.Left(US);
							if (DisplayName.Equals(ShortVal, ESearchCase::IgnoreCase) ||
								EntryName.Equals(ShortVal, ESearchCase::IgnoreCase) ||
								EntryNoSuffix.Equals(ShortVal, ESearchCase::IgnoreCase))
							{
								ByteProp->SetPropertyValue(PropertyData, (uint8)Enum->GetValueByIndex(Idx));
								bSet = true;
								break;
							}
						}
					}
				}
				if (!bSet && CastField<FEnumProperty>(MatchedProperty))
				{
					FEnumProperty* EnumProp = CastField<FEnumProperty>(MatchedProperty);
					if (UEnum* Enum = EnumProp->GetEnum())
					{
						FString ShortVal = PropertyValueStr;
						int32 ColonPos = INDEX_NONE;
						if (ShortVal.FindLastChar(TEXT(':'), ColonPos))
							ShortVal = ShortVal.Mid(ColonPos + 1).TrimStartAndEnd();
						for (int32 Idx = 0; Idx < Enum->NumEnums() - 1; Idx++)
						{
							FString DisplayName = Enum->GetDisplayNameTextByIndex(Idx).ToString();
							FString EntryName   = Enum->GetNameStringByIndex(Idx);
							int32 DC = INDEX_NONE;
							if (EntryName.FindLastChar(TEXT(':'), DC)) EntryName = EntryName.Mid(DC + 1).TrimStartAndEnd();
							FString EntryNoSuffix = EntryName;
							int32 US = INDEX_NONE;
							if (EntryNoSuffix.FindLastChar(TEXT('_'), US)) EntryNoSuffix = EntryNoSuffix.Left(US);
							if (DisplayName.Equals(ShortVal, ESearchCase::IgnoreCase) ||
								EntryName.Equals(ShortVal, ESearchCase::IgnoreCase) ||
								EntryNoSuffix.Equals(ShortVal, ESearchCase::IgnoreCase))
							{
								void* ValuePtr = EnumProp->ContainerPtrToValuePtr<void>(RowData);
								EnumProp->GetUnderlyingProperty()->SetIntPropertyValue(ValuePtr, Enum->GetValueByIndex(Idx));
								bSet = true;
								break;
							}
						}
					}
				}
				if (!bSet)
					MatchedProperty->ImportText_Direct(*PropertyValueStr, PropertyData, nullptr, PPF_None);
			}
		}

		TMap<FName, uint8*>& MutableRowMap = const_cast<TMap<FName, uint8*>&>(DataTable->GetRowMap());
		MutableRowMap.Add(FName(*RowName), RowData);
		RowsAdded++;
	}

	DataTable->MarkPackageDirty();

	TSharedPtr<FJsonObject> ResultObject = MakeShareable(new FJsonObject);
	ResultObject->SetBoolField(TEXT("success"), true);
	ResultObject->SetNumberField(TEXT("rows_added"), RowsAdded);
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(ResultObject.ToSharedRef(), Writer);
}

void HandleEditDataTableRows(const FString& TablePath, const TArray<TSharedPtr<FJsonValue>>& Edits, FString& OutJsonString, FString& OutError)
{

	UDataTable* DataTable = FindObject<UDataTable>(nullptr, *TablePath);
	if (!DataTable)
		DataTable = LoadObject<UDataTable>(nullptr, *TablePath);
	if (!DataTable)
		DataTable = Cast<UDataTable>(UEditorAssetLibrary::LoadAsset(TablePath));
	if (!DataTable)
	{
		OutError = FString::Printf(TEXT("Could not load DataTable: '%s'. Make sure the asset_path is the full package path (e.g. /Game/Folder/DT_Name or /Game/Folder/DT_Name.DT_Name)"), *TablePath);
		TSharedPtr<FJsonObject> ResultObject = MakeShareable(new FJsonObject);
		ResultObject->SetBoolField(TEXT("success"), false);
		ResultObject->SetStringField(TEXT("error"), OutError);
		FString ResultString;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResultString);
		FJsonSerializer::Serialize(ResultObject.ToSharedRef(), Writer);
		OutJsonString = ResultString;
		return;
	}

	UScriptStruct* RowStruct = DataTable->RowStruct;
	if (!RowStruct)
	{
		OutError = TEXT("DataTable has no row struct defined");
		TSharedPtr<FJsonObject> ResultObject = MakeShareable(new FJsonObject);
		ResultObject->SetBoolField(TEXT("success"), false);
		ResultObject->SetStringField(TEXT("error"), OutError);
		FString ResultString;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResultString);
		FJsonSerializer::Serialize(ResultObject.ToSharedRef(), Writer);
		OutJsonString = ResultString;
		return;
	}

	const TMap<FName, uint8*>& RowMap = DataTable->GetRowMap();
	int32 RowsEdited = 0;
	TArray<FString> FailedRows;

	for (const TSharedPtr<FJsonValue>& EditValue : Edits)
	{
		const TSharedPtr<FJsonObject>* EditObj = nullptr;
		if (!EditValue->TryGetObject(EditObj) || !EditObj) continue;

		FString RowName;
		if (!(*EditObj)->TryGetStringField(TEXT("row_name"), RowName))
		{
			(*EditObj)->TryGetStringField(TEXT("RowName"), RowName);
		}
		if (RowName.IsEmpty())
		{
			continue;
		}

		uint8* const* FoundRow = RowMap.Find(FName(*RowName));
		if (!FoundRow || !*FoundRow)
		{
			FString Msg = FString::Printf(TEXT("Row '%s' not found"), *RowName);
			FailedRows.Add(Msg);
			continue;
		}
		uint8* ExistingRowData = *FoundRow;

		const TSharedPtr<FJsonObject>* EditFieldsSubObj = nullptr;
		const auto& EditFieldSource =
			((*EditObj)->TryGetObjectField(TEXT("fields"), EditFieldsSubObj) && EditFieldsSubObj)
			? (*EditFieldsSubObj)->Values : (*EditObj)->Values;
		for (auto& FieldPair : EditFieldSource)
		{
			const FString FieldName(*FieldPair.Key);
			TSharedPtr<FJsonValue> FieldValue = FieldPair.Value;

			if (FieldName == TEXT("row_name") || FieldName == TEXT("RowName") || FieldName == TEXT("name")) continue;
			if (!FieldValue.IsValid()) continue;

			FProperty* MatchedProperty = nullptr;
			for (TFieldIterator<FProperty> PropIt(RowStruct); PropIt; ++PropIt)
			{
				FProperty* Property = *PropIt;
				if (Property && !Property->IsNative())
				{
					const FString PropName = Property->GetName();
					if (PropName.StartsWith(FieldName + TEXT("_")) || PropName.Equals(FieldName, ESearchCase::IgnoreCase))
					{
						MatchedProperty = Property;
						break;
					}
				}
			}
			if (!MatchedProperty)
			{
				for (TFieldIterator<FProperty> PropIt2(RowStruct); PropIt2; ++PropIt2)
				{
					FProperty* Property = *PropIt2;
					if (Property && Property->IsNative() && Property->GetName().Equals(FieldName, ESearchCase::IgnoreCase))
					{
						MatchedProperty = Property;
						break;
					}
				}
			}

			if (!MatchedProperty)
			{
				continue;
			}

			FString ValueStr;
			bool bFound = false;
			switch (FieldValue->Type)
			{
				case EJson::String:  ValueStr = FieldValue->AsString();                                      bFound = true; break;
				case EJson::Number:  ValueStr = FString::SanitizeFloat(FieldValue->AsNumber());              bFound = true; break;
				case EJson::Boolean: ValueStr = FieldValue->AsBool() ? TEXT("True") : TEXT("False");         bFound = true; break;
				case EJson::Array:
				case EJson::Object:
					ValueStr = JsonValueToImportText(FieldValue);
					bFound = !ValueStr.IsEmpty();
					break;
				default:
					break;
			}

			if (bFound)
			{
				void* PropertyData = MatchedProperty->ContainerPtrToValuePtr<void>(ExistingRowData);
				MatchedProperty->ImportText_Direct(*ValueStr, PropertyData, nullptr, PPF_None);
			}
		}

		RowsEdited++;
	}

	DataTable->MarkPackageDirty();

	TSharedPtr<FJsonObject> ResultObject = MakeShareable(new FJsonObject);
	ResultObject->SetBoolField(TEXT("success"), FailedRows.Num() == 0);
	ResultObject->SetNumberField(TEXT("rows_edited"), RowsEdited);
	if (FailedRows.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> FailArray;
		for (const FString& Msg : FailedRows) FailArray.Add(MakeShareable(new FJsonValueString(Msg)));
		ResultObject->SetArrayField(TEXT("not_found"), FailArray);
	}
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(ResultObject.ToSharedRef(), Writer);
}

void HandleGetDataTableRows(const FString& TablePath, FString& OutJsonString, FString& OutError)
{

	UDataTable* DataTable = FindObject<UDataTable>(nullptr, *TablePath);
	if (!DataTable) DataTable = LoadObject<UDataTable>(nullptr, *TablePath);
	if (!DataTable) DataTable = Cast<UDataTable>(UEditorAssetLibrary::LoadAsset(TablePath));
	if (!DataTable)
	{
		OutError = FString::Printf(TEXT("Could not load DataTable: '%s'"), *TablePath);
		return;
	}

	const UScriptStruct* RowStruct = DataTable->GetRowStruct();
	if (!RowStruct)
	{
		OutError = TEXT("DataTable has no row struct defined");
		return;
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject());
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("table_path"), DataTable->GetPathName());
	Result->SetStringField(TEXT("row_struct"), RowStruct->GetName());

	TArray<TSharedPtr<FJsonValue>> RowsArray;
	const TMap<FName, uint8*>& RowMap = DataTable->GetRowMap();

	for (const auto& Pair : RowMap)
	{
		TSharedPtr<FJsonObject> RowObj = MakeShareable(new FJsonObject());
		RowObj->SetStringField(TEXT("row_name"), Pair.Key.ToString());

		TSharedPtr<FJsonObject> Fields = MakeShareable(new FJsonObject());
		for (TFieldIterator<FProperty> PropIt(RowStruct); PropIt; ++PropIt)
		{
			FProperty* Prop = *PropIt;
			if (!Prop) continue;

			FString FieldName = Prop->IsNative()
				? Prop->GetName()
				: Prop->GetAuthoredName().IsEmpty() ? Prop->GetName() : Prop->GetAuthoredName();

			FString ValueStr;
			const void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Pair.Value);
			Prop->ExportTextItem_Direct(ValueStr, ValuePtr, nullptr, nullptr, PPF_None);
			Fields->SetStringField(FieldName, ValueStr);
		}
		RowObj->SetObjectField(TEXT("fields"), Fields);
		RowsArray.Add(MakeShareable(new FJsonValueObject(RowObj)));
	}

	Result->SetArrayField(TEXT("rows"), RowsArray);
	Result->SetNumberField(TEXT("row_count"), RowMap.Num());

	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
}

void HandleImportDataTableCSV(const FString& TablePath, const FString& CSVContent, FString& OutJsonString, FString& OutError)
{

	if (CSVContent.IsEmpty())
	{
		OutError = TEXT("csv_content is empty");
		return;
	}

	UDataTable* DataTable = FindObject<UDataTable>(nullptr, *TablePath);
	if (!DataTable) DataTable = LoadObject<UDataTable>(nullptr, *TablePath);
	if (!DataTable) DataTable = Cast<UDataTable>(UEditorAssetLibrary::LoadAsset(TablePath));
	if (!DataTable)
	{
		OutError = FString::Printf(TEXT("Could not load DataTable: '%s'"), *TablePath);
		return;
	}

	TArray<FString> Problems = DataTable->CreateTableFromCSVString(CSVContent);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject());
	Result->SetBoolField(TEXT("success"), Problems.Num() == 0);
	Result->SetNumberField(TEXT("row_count"), DataTable->GetRowMap().Num());

	if (Problems.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> ProblemArray;
		for (const FString& P : Problems)
			ProblemArray.Add(MakeShareable(new FJsonValueString(P)));
		Result->SetArrayField(TEXT("problems"), ProblemArray);
	}

	DataTable->MarkPackageDirty();

	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
}

void HandleExportDataTableCSV(const FString& TablePath, FString& OutJsonString, FString& OutError)
{

	UDataTable* DataTable = FindObject<UDataTable>(nullptr, *TablePath);
	if (!DataTable) DataTable = LoadObject<UDataTable>(nullptr, *TablePath);
	if (!DataTable) DataTable = Cast<UDataTable>(UEditorAssetLibrary::LoadAsset(TablePath));
	if (!DataTable)
	{
		OutError = FString::Printf(TEXT("Could not load DataTable: '%s'"), *TablePath);
		return;
	}

	FString CSVOutput = DataTable->GetTableAsCSV();

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject());
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("csv"), CSVOutput);
	Result->SetNumberField(TEXT("row_count"), DataTable->GetRowMap().Num());

	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
}

void HandleCreateDataTableFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString TableName;
	if (!Args->TryGetStringField(TEXT("table_name"), TableName))
		Args->TryGetStringField(TEXT("name"), TableName);

	FString SavePath, RowStructPath;
	Args->TryGetStringField(TEXT("save_path"), SavePath);
	if (!Args->TryGetStringField(TEXT("row_struct"), RowStructPath))
		if (!Args->TryGetStringField(TEXT("struct_path"), RowStructPath))
			if (!Args->TryGetStringField(TEXT("row_struct_path"), RowStructPath))
				if (!Args->TryGetStringField(TEXT("struct_type"), RowStructPath))
					if (!Args->TryGetStringField(TEXT("row_struct_type"), RowStructPath))
						if (!Args->TryGetStringField(TEXT("struct_name"), RowStructPath))
							Args->TryGetStringField(TEXT("struct"), RowStructPath);

	TArray<TSharedPtr<FJsonValue>> Rows;
	const TArray<TSharedPtr<FJsonValue>>* RowsArray = nullptr;
	if (Args->TryGetArrayField(TEXT("rows"), RowsArray)) Rows = *RowsArray;

	HandleCreateDataTable(TableName, SavePath, RowStructPath, Rows, OutJsonString, OutError);
}

void HandleAddDataTableRowsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString TablePath;
	if (!Args->TryGetStringField(TEXT("table_path"), TablePath))
		if (!Args->TryGetStringField(TEXT("data_table_path"), TablePath))
			if (!Args->TryGetStringField(TEXT("asset_path"), TablePath))
				Args->TryGetStringField(TEXT("path"), TablePath);
	if (TablePath.IsEmpty())
	{
		OutError = TEXT("Missing required parameter: table_path (alias: asset_path / data_table_path)");
		TSharedPtr<FJsonObject> ResultObject = MakeShareable(new FJsonObject);
		ResultObject->SetBoolField(TEXT("success"), false);
		ResultObject->SetStringField(TEXT("error"), OutError);
		FString ResultString;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResultString);
		FJsonSerializer::Serialize(ResultObject.ToSharedRef(), Writer);
		OutJsonString = ResultString;
		return;
	}

	const TArray<TSharedPtr<FJsonValue>>* RowsArray = nullptr;
	if (!Args->TryGetArrayField(TEXT("rows"), RowsArray) || !RowsArray)
	{
		OutError = TEXT("Missing or invalid 'rows' array");
		TSharedPtr<FJsonObject> ResultObject = MakeShareable(new FJsonObject);
		ResultObject->SetBoolField(TEXT("success"), false);
		ResultObject->SetStringField(TEXT("error"), OutError);
		FString ResultString;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResultString);
		FJsonSerializer::Serialize(ResultObject.ToSharedRef(), Writer);
		OutJsonString = ResultString;
		return;
	}

	HandleAddDataTableRows(TablePath, *RowsArray, OutJsonString, OutError);
}

void HandleEditDataTableRowsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString TablePath;
	if (!Args->TryGetStringField(TEXT("table_path"), TablePath))
		if (!Args->TryGetStringField(TEXT("data_table_path"), TablePath))
			if (!Args->TryGetStringField(TEXT("asset_path"), TablePath))
				Args->TryGetStringField(TEXT("path"), TablePath);
	if (TablePath.IsEmpty())
	{
		OutError = TEXT("Missing required parameter: table_path (alias: asset_path / data_table_path)");
		TSharedPtr<FJsonObject> ResultObject = MakeShareable(new FJsonObject);
		ResultObject->SetBoolField(TEXT("success"), false);
		ResultObject->SetStringField(TEXT("error"), OutError);
		FString ResultString;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResultString);
		FJsonSerializer::Serialize(ResultObject.ToSharedRef(), Writer);
		OutJsonString = ResultString;
		return;
	}

	const TArray<TSharedPtr<FJsonValue>>* EditsArray = nullptr;
	if (!Args->TryGetArrayField(TEXT("edits"), EditsArray) || !EditsArray)
	{
		OutError = TEXT("Missing or invalid 'edits' array");
		TSharedPtr<FJsonObject> ResultObject = MakeShareable(new FJsonObject);
		ResultObject->SetBoolField(TEXT("success"), false);
		ResultObject->SetStringField(TEXT("error"), OutError);
		FString ResultString;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResultString);
		FJsonSerializer::Serialize(ResultObject.ToSharedRef(), Writer);
		OutJsonString = ResultString;
		return;
	}

	HandleEditDataTableRows(TablePath, *EditsArray, OutJsonString, OutError);
}

void HandleGetDataTableRowsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString TablePath;
	if (Args.IsValid())
	{
		if (!Args->TryGetStringField(TEXT("table_path"), TablePath))
			if (!Args->TryGetStringField(TEXT("data_table_path"), TablePath))
				if (!Args->TryGetStringField(TEXT("asset_path"), TablePath))
					Args->TryGetStringField(TEXT("path"), TablePath);
	}
	HandleGetDataTableRows(TablePath, OutJsonString, OutError);
}

void HandleImportDataTableCSVFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString TablePath, CSVContent;
	if (Args.IsValid())
	{
		Args->TryGetStringField(TEXT("table_path"), TablePath);
		Args->TryGetStringField(TEXT("csv_content"), CSVContent);

		if (CSVContent.IsEmpty())
		{
			FString CsvFilePath;
			if (!Args->TryGetStringField(TEXT("csv_file_path"), CsvFilePath))
				if (!Args->TryGetStringField(TEXT("file_path"), CsvFilePath))
					Args->TryGetStringField(TEXT("path"), CsvFilePath);

			if (!CsvFilePath.IsEmpty())
			{
				FString Resolved = CsvFilePath;
				if (FPaths::IsRelative(Resolved))
					Resolved = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), Resolved);

				if (!FFileHelper::LoadFileToString(CSVContent, *Resolved))
				{
					OutError = FString::Printf(
						TEXT("Could not read CSV file at '%s' (resolved: '%s'). Pass csv_content directly or use an absolute path."),
						*CsvFilePath, *Resolved);
					return;
				}
			}
		}
	}
	HandleImportDataTableCSV(TablePath, CSVContent, OutJsonString, OutError);
}

void HandleExportDataTableCSVFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString TablePath;
	if (Args.IsValid()) Args->TryGetStringField(TEXT("table_path"), TablePath);
	HandleExportDataTableCSV(TablePath, OutJsonString, OutError);
}

void HandleDeleteDataTableRowFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString DtPath, RowName;
	if (!Args->TryGetStringField(TEXT("data_table_path"), DtPath) || !Args->TryGetStringField(TEXT("row_name"), RowName))
	{
		OutError = TEXT("Missing required parameters: data_table_path, row_name");
		return;
	}

	UDataTable* DT = Cast<UDataTable>(UEditorAssetLibrary::LoadAsset(DtPath));
	if (!DT)
	{
		OutError = FString::Printf(TEXT("Could not load DataTable at: %s"), *DtPath);
		return;
	}
	if (!DT->GetRowMap().Contains(FName(*RowName)))
	{
		OutError = FString::Printf(TEXT("Row '%s' not found in DataTable '%s'"), *RowName, *DtPath);
		return;
	}

	DT->RemoveRow(FName(*RowName));
	DT->MarkPackageDirty();

	TSharedPtr<FJsonObject> Res = MakeShareable(new FJsonObject);
	Res->SetBoolField(TEXT("success"), true);
	Res->SetStringField(TEXT("data_table_path"), DtPath);
	Res->SetStringField(TEXT("deleted_row"), RowName);
	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Res.ToSharedRef(), W);
}

}
