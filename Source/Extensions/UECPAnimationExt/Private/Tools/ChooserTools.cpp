// Copyright 2026, BlueprintsLab, All rights reserved.

#include "Tools/ChooserTools.h"
#include "Tools/BatchToolHelper.h"
#include "Managers/SettingsManager.h"

#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"

#include "EditorAssetLibrary.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"
#include "UObject/UnrealType.h"
#include "Misc/EngineVersionComparison.h"
#if UE_VERSION_OLDER_THAN(5, 5, 0)
#include "InstancedStruct.h"
#else
#include "StructUtils/InstancedStruct.h"
#endif
#include "Modules/ModuleManager.h"

namespace ChooserTools
{

static FString GChooserStructCandidates;

static UScriptStruct* FindChooserStruct(const FString& StructName)
{
	GChooserStructCandidates.Empty();

	static bool bModulesLoaded = false;
	if (!bModulesLoaded)
	{
		bModulesLoaded = true;
		for (const TCHAR* Mod : { TEXT("Chooser"), TEXT("ChooserEditor") })
		{
			if (!FModuleManager::Get().IsModuleLoaded(Mod))
				FModuleManager::Get().LoadModule(Mod);
		}
	}

	FString NameNoF = StructName.StartsWith(TEXT("F")) ? StructName.Mid(1) : StructName;
	FString NameWithF = StructName.StartsWith(TEXT("F")) ? StructName : (FString(TEXT("F")) + StructName);

	for (const FString& Name : TArray<FString>{ NameNoF, NameWithF })
	{
		for (const TCHAR* Pkg : { TEXT("/Script/Chooser"), TEXT("/Script/ChooserEditor") })
		{
			if (UScriptStruct* S = FindObject<UScriptStruct>(nullptr, *FString::Printf(TEXT("%s.%s"), Pkg, *Name)))
				return S;
		}
	}

	for (TObjectIterator<UScriptStruct> It; It; ++It)
	{
		const FString ItName = It->GetName();
		if (ItName.Equals(NameNoF, ESearchCase::IgnoreCase) ||
			ItName.Equals(NameWithF, ESearchCase::IgnoreCase))
		{
			return *It;
		}
	}

	TArray<FString> Candidates;
	for (TObjectIterator<UScriptStruct> It; It; ++It)
	{
		const FString Path = It->GetPathName();
		if (Path.Contains(TEXT("Chooser"), ESearchCase::IgnoreCase) ||
			It->GetName().Contains(TEXT("Column"), ESearchCase::IgnoreCase))
		{
			Candidates.Add(Path);
		}
	}
	GChooserStructCandidates = Candidates.IsEmpty()
		? TEXT("none found")
		: FString::Join(Candidates, TEXT("; "));
	return nullptr;
}

static UClass* GetChooserTableClass()
{
	static UClass* Cached = nullptr;
	if (!Cached)
		Cached = FindObject<UClass>(nullptr, TEXT("/Script/Chooser.ChooserTable"));
	return Cached;
}

static FArrayProperty* GetResultsStructsProp()
{
	static FArrayProperty* Cached = nullptr;
	if (!Cached)
	{
		UClass* ChooserClass = GetChooserTableClass();
		if (ChooserClass)
			Cached = FindFProperty<FArrayProperty>(ChooserClass, TEXT("ResultsStructs"));
	}
	return Cached;
}

static FArrayProperty* GetColumnsStructsProp()
{
	static FArrayProperty* Cached = nullptr;
	if (!Cached)
	{
		UClass* ChooserClass = GetChooserTableClass();
		if (ChooserClass)
			Cached = FindFProperty<FArrayProperty>(ChooserClass, TEXT("ColumnsStructs"));
	}
	return Cached;
}

// Bulk tools call the single-cell handlers many times; suppress the per-call save and persist once.
static int32 GChooserSaveSuppressDepth = 0;
struct FScopedSuppressChooserSave
{
	FScopedSuppressChooserSave()  { ++GChooserSaveSuppressDepth; }
	~FScopedSuppressChooserSave() { --GChooserSaveSuppressDepth; }
};

// Persists a mutated chooser table to disk (PostEditChange so the editor/runtime caches refresh).
static void SaveChooserTable(UObject* Table, const FString& ChooserPath)
{
	if (!Table) return;
	Table->PostEditChange();
	if (GChooserSaveSuppressDepth > 0) return;
	const UPackage* Pkg = Table->GetPackage();
	const FString PathToSave = Pkg ? Pkg->GetName() : ChooserPath;
	UEditorAssetLibrary::SaveAsset(PathToSave, false);
}

void HandleCreateChooserTable(const FString& AssetName, const FString& SavePath,
	const FString& OutputClass, FString& OutJson, FString& OutError)
{

	UClass* ChooserClass = GetChooserTableClass();
	if (!ChooserClass)
	{
		OutError = TEXT("UChooserTable class not found — ensure the Chooser plugin is enabled");
		return;
	}

	if (AssetName.IsEmpty()) { OutError = TEXT("name is required"); return; }
	FString PackagePath = SavePath;
	while (PackagePath.EndsWith(TEXT("/"))) PackagePath = PackagePath.LeftChop(1);
	PackagePath += TEXT("/") + AssetName;

	if (FPackageName::DoesPackageExist(PackagePath))
	{
		OutError = FString::Printf(TEXT("Asset already exists at '%s'"), *PackagePath);
		return;
	}

	UPackage* Pkg = CreatePackage(*PackagePath);
	UObject* Table = NewObject<UObject>(Pkg, ChooserClass, FName(*AssetName),
		RF_Public | RF_Standalone | RF_Transactional);
	if (!Table) { OutError = TEXT("Failed to create UChooserTable object"); return; }

	if (!OutputClass.IsEmpty())
	{
		UClass* OutCls = FindObject<UClass>(nullptr, *OutputClass);
		if (!OutCls) OutCls = LoadObject<UClass>(nullptr, *OutputClass);
		if (OutCls)
		{
			FObjectProperty* OOTProp = FindFProperty<FObjectProperty>(ChooserClass, TEXT("OutputObjectType"));
			if (OOTProp)
				OOTProp->SetObjectPropertyValue(OOTProp->ContainerPtrToValuePtr<void>(Table), OutCls);
		}
	}

	FAssetRegistryModule::AssetCreated(Table);
	Table->PostEditChange();
	Table->MarkPackageDirty();

	// Write the package to disk now so the asset exists for subsequent tools / editor sessions.
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_NoError;
	const FString FileName = FPackageName::LongPackageNameToFilename(PackagePath, FPackageName::GetAssetPackageExtension());
	const bool bSaved = UPackage::SavePackage(Pkg, Table, *FileName, SaveArgs);
	if (!bSaved)
	{
		OutError = FString::Printf(TEXT("ChooserTable '%s' was created in memory but could not be saved to '%s'."), *PackagePath, *FileName);
		return;
	}

	OutJson = FString::Printf(TEXT("{\"success\":true,\"path\":\"%s\",\"saved\":true}"), *PackagePath);
}

void HandleAddChooserColumn(const FString& ChooserPath, const FString& ColumnType,
	const FString& Label, FString& OutJson, FString& OutError)
{

	UObject* Table = LoadObject<UObject>(nullptr, *ChooserPath);
	if (!Table || !Table->IsA(GetChooserTableClass()))
	{
		OutError = FString::Printf(TEXT("ChooserTable not found at '%s'"), *ChooserPath);
		return;
	}

	const FString TypeLower = ColumnType.ToLower();
	FString StructName;
	if      (TypeLower == TEXT("float_range"))    StructName = TEXT("FloatRangeColumn");
	else if (TypeLower == TEXT("float_distance")) StructName = TEXT("FloatDistanceColumn");
	else if (TypeLower == TEXT("output_object"))  StructName = TEXT("OutputObjectColumn");
	else if (TypeLower == TEXT("output_bool"))    StructName = TEXT("OutputBoolColumn");
	else if (TypeLower == TEXT("output_float"))   StructName = TEXT("OutputFloatColumn");
	else if (TypeLower == TEXT("output_enum"))    StructName = TEXT("OutputEnumColumn");
	else if (TypeLower == TEXT("output_struct"))  StructName = TEXT("OutputStructColumn");
	else if (TypeLower == TEXT("bool"))           StructName = TEXT("BoolColumn");
	else if (TypeLower == TEXT("enum"))           StructName = TEXT("EnumColumn");
	else if (TypeLower == TEXT("multi_enum"))     StructName = TEXT("MultiEnumColumn");
	else if (TypeLower == TEXT("gameplay_tag"))   StructName = TEXT("GameplayTagColumn");
	else if (TypeLower == TEXT("object"))         StructName = TEXT("ObjectColumn");
	else if (TypeLower == TEXT("object_class"))   StructName = TEXT("ObjectClassColumn");
	else if (TypeLower == TEXT("randomize"))      StructName = TEXT("RandomizeColumn");
	else
	{
		if (ColumnType.Contains(TEXT(".")))
			StructName = ColumnType.Right(ColumnType.Len() - ColumnType.Find(TEXT("."), ESearchCase::CaseSensitive, ESearchDir::FromEnd) - 1);
		else
			StructName = ColumnType;
	}

	UScriptStruct* ColStruct = FindChooserStruct(StructName);
	if (!ColStruct)
	{
		OutError = FString::Printf(
			TEXT("Column struct '%s' (%s) not found. Valid types: float_range, float_distance, output_object, output_bool, output_float, output_enum, output_struct, bool, enum, multi_enum, gameplay_tag, object, object_class, randomize. In-memory Chooser structs: %s"),
			*ColumnType, *StructName, *GChooserStructCandidates);
		return;
	}

	FArrayProperty* ColsProp = GetColumnsStructsProp();
	if (!ColsProp)
	{
		OutError = TEXT("Could not reflect ColumnsStructs on ChooserTable");
		return;
	}

	FScriptArrayHelper ColsHelper(ColsProp, ColsProp->ContainerPtrToValuePtr<void>(Table));
	const int32 NewIdx = ColsHelper.AddValue();

	void* ElemPtr = ColsHelper.GetRawPtr(NewIdx);
	FInstancedStruct* InstStruct = static_cast<FInstancedStruct*>(ElemPtr);
	InstStruct->InitializeAs(ColStruct, nullptr);

	Table->Modify();
	Table->MarkPackageDirty();
	SaveChooserTable(Table, ChooserPath);

	OutJson = FString::Printf(
		TEXT("{\"success\":true,\"column_type\":\"%s\",\"column_index\":%d,\"chooser_path\":\"%s\"}"),
		*ColumnType, NewIdx, *ChooserPath);
}

void HandleAddChooserRow(const FString& ChooserPath, const FString& AssetPath,
	FString& OutJson, FString& OutError)
{

	UObject* Table = LoadObject<UObject>(nullptr, *ChooserPath);
	if (!Table || !Table->IsA(GetChooserTableClass()))
	{
		OutError = FString::Printf(TEXT("ChooserTable not found at '%s'"), *ChooserPath);
		return;
	}

	FArrayProperty* ResultsProp = GetResultsStructsProp();
	if (!ResultsProp)
	{
		OutError = TEXT("Could not reflect ResultsStructs on ChooserTable (editor-only data — ensure this is an Editor build)");
		return;
	}

	FScriptArrayHelper ResultsHelper(ResultsProp,
		ResultsProp->ContainerPtrToValuePtr<void>(Table));
	const int32 NewRowIdx = ResultsHelper.AddValue();

	if (!AssetPath.IsEmpty())
	{
		UObject* Asset = LoadObject<UObject>(nullptr, *AssetPath);
		if (Asset)
		{
			UScriptStruct* AssetChooserStruct = FindChooserStruct(TEXT("AssetChooser"));
			if (AssetChooserStruct)
			{
				void* ElemPtr = ResultsHelper.GetRawPtr(NewRowIdx);
				FInstancedStruct* ResultStruct = static_cast<FInstancedStruct*>(ElemPtr);
				ResultStruct->InitializeAs(AssetChooserStruct, nullptr);
				void* StructMem = ResultStruct->GetMutableMemory();
				if (FObjectProperty* AssetProp =
					FindFProperty<FObjectProperty>(AssetChooserStruct, TEXT("Asset")))
				{
					AssetProp->SetObjectPropertyValue(
						AssetProp->ContainerPtrToValuePtr<void>(StructMem), Asset);
				}
			}
		}
	}

	FArrayProperty* ColsProp = GetColumnsStructsProp();
	if (ColsProp)
	{
		FScriptArrayHelper ColsHelper(ColsProp, ColsProp->ContainerPtrToValuePtr<void>(Table));
		for (int32 i = 0; i < ColsHelper.Num(); ++i)
		{
			void* ColElem = ColsHelper.GetRawPtr(i);
			FInstancedStruct* ColStruct = static_cast<FInstancedStruct*>(ColElem);
			if (!ColStruct || !ColStruct->IsValid()) continue;

			UScriptStruct* ColType = const_cast<UScriptStruct*>(ColStruct->GetScriptStruct());
			FArrayProperty* RowValuesProp =
				FindFProperty<FArrayProperty>(ColType, TEXT("RowValues"));
			if (RowValuesProp)
			{
				void* ColMem = ColStruct->GetMutableMemory();
				FScriptArrayHelper RVHelper(RowValuesProp,
					RowValuesProp->ContainerPtrToValuePtr<void>(ColMem));
				RVHelper.AddValue();
			}
		}
	}

	Table->Modify();
	Table->MarkPackageDirty();
	SaveChooserTable(Table, ChooserPath);

	OutJson = FString::Printf(
		TEXT("{\"success\":true,\"row_index\":%d,\"asset_path\":\"%s\",\"chooser_path\":\"%s\"}"),
		NewRowIdx, *AssetPath, *ChooserPath);
}

void HandleGetChooserSummary(const FString& ChooserPath, FString& OutJson, FString& OutError)
{

	UObject* Table = LoadObject<UObject>(nullptr, *ChooserPath);
	if (!Table || !Table->IsA(GetChooserTableClass()))
	{
		OutError = FString::Printf(TEXT("ChooserTable not found at '%s'"), *ChooserPath);
		return;
	}

	UClass* ChooserClass = GetChooserTableClass();

	int32 ColCount = 0;
	if (FArrayProperty* P = GetColumnsStructsProp())
	{
		FScriptArrayHelper H(P, P->ContainerPtrToValuePtr<void>(Table));
		ColCount = H.Num();
	}

	int32 RowCount = 0;
	if (FArrayProperty* P = GetResultsStructsProp())
	{
		FScriptArrayHelper H(P, P->ContainerPtrToValuePtr<void>(Table));
		RowCount = H.Num();
	}

	FString OutputTypeName = TEXT("None");
	if (FObjectProperty* OOTProp = FindFProperty<FObjectProperty>(ChooserClass, TEXT("OutputObjectType")))
	{
		UObject* OutType = OOTProp->GetObjectPropertyValue(
			OOTProp->ContainerPtrToValuePtr<void>(Table));
		if (OutType) OutputTypeName = OutType->GetName();
	}

	FString ColTypes = TEXT("[");
	if (FArrayProperty* ColsProp = GetColumnsStructsProp())
	{
		FScriptArrayHelper ColsHelper(ColsProp, ColsProp->ContainerPtrToValuePtr<void>(Table));
		for (int32 i = 0; i < ColsHelper.Num(); ++i)
		{
			void* ElemPtr = ColsHelper.GetRawPtr(i);
			const FInstancedStruct* ColStruct = static_cast<const FInstancedStruct*>(ElemPtr);
			if (ColStruct && ColStruct->IsValid())
			{
				if (i > 0) ColTypes += TEXT(",");
				ColTypes += TEXT("\"");
				ColTypes += ColStruct->GetScriptStruct()->GetName();
				ColTypes += TEXT("\"");
			}
		}
	}
	ColTypes += TEXT("]");

	OutJson = FString::Printf(
		TEXT("{\"success\":true,\"column_count\":%d,\"row_count\":%d,\"output_type\":\"%s\",\"columns\":%s}"),
		ColCount, RowCount, *OutputTypeName, *ColTypes);
}

void HandleSetChooserRowValue(const FString& ChooserPath, int32 ColumnIndex, int32 RowIndex,
	const FString& PropertyName, const FString& PropertyValue, FString& OutJson, FString& OutError)
{

	UObject* Table = LoadObject<UObject>(nullptr, *ChooserPath);
	if (!Table || !Table->IsA(GetChooserTableClass()))
	{
		OutError = FString::Printf(TEXT("ChooserTable not found at '%s'"), *ChooserPath);
		return;
	}

	FArrayProperty* ColsProp = GetColumnsStructsProp();
	if (!ColsProp) { OutError = TEXT("Could not reflect ColumnsStructs"); return; }

	FScriptArrayHelper ColsHelper(ColsProp, ColsProp->ContainerPtrToValuePtr<void>(Table));
	if (!ColsHelper.IsValidIndex(ColumnIndex))
	{
		OutError = FString::Printf(TEXT("Column index %d out of range (count: %d)"),
			ColumnIndex, ColsHelper.Num());
		return;
	}

	FInstancedStruct* ColStruct = reinterpret_cast<FInstancedStruct*>(ColsHelper.GetRawPtr(ColumnIndex));
	if (!ColStruct || !ColStruct->IsValid())
	{
		OutError = FString::Printf(TEXT("Column at index %d is invalid"), ColumnIndex);
		return;
	}

	UScriptStruct* ColType = const_cast<UScriptStruct*>(ColStruct->GetScriptStruct());
	FArrayProperty* RowValuesProp = FindFProperty<FArrayProperty>(ColType, TEXT("RowValues"));
	if (!RowValuesProp)
	{
		OutError = FString::Printf(TEXT("Column type '%s' has no RowValues array"), *ColType->GetName());
		return;
	}

	void* ColMem = ColStruct->GetMutableMemory();
	FScriptArrayHelper RVHelper(RowValuesProp, RowValuesProp->ContainerPtrToValuePtr<void>(ColMem));
	if (!RVHelper.IsValidIndex(RowIndex))
	{
		OutError = FString::Printf(TEXT("Row index %d out of range (column has %d rows)"),
			RowIndex, RVHelper.Num());
		return;
	}

	void* RowElemPtr = RVHelper.GetRawPtr(RowIndex);

	FStructProperty* InnerStruct = CastField<FStructProperty>(RowValuesProp->Inner);
	FProperty* Prop = nullptr;
	void* PropPtr = nullptr;
	if (InnerStruct)
	{
		Prop = FindFProperty<FProperty>(InnerStruct->Struct, *PropertyName);
		if (!Prop)
		{
			TArray<FString> Names;
			for (TFieldIterator<FProperty> It(InnerStruct->Struct); It; ++It)
				Names.Add(It->GetName());
			OutError = FString::Printf(
				TEXT("Property '%s' not found on row data type '%s'. Available: %s"),
				*PropertyName, *InnerStruct->Struct->GetName(), *FString::Join(Names, TEXT(", ")));
			return;
		}
		PropPtr = Prop->ContainerPtrToValuePtr<void>(RowElemPtr);
	}
	else
	{
		const bool bAcceptedName = PropertyName.IsEmpty()
			|| PropertyName.Equals(TEXT("Value"),     ESearchCase::IgnoreCase)
			|| PropertyName.Equals(TEXT("value"),     ESearchCase::IgnoreCase)
			|| PropertyName.Equals(TEXT("Object"),    ESearchCase::IgnoreCase)
			|| PropertyName.Equals(TEXT("OutputValue"), ESearchCase::IgnoreCase);
		if (!bAcceptedName)
		{
			OutError = FString::Printf(
				TEXT("Column '%s' stores bare values per row — pass property_name=\"Value\" with the value to set."),
				*ColType->GetName());
			return;
		}
		Prop    = RowValuesProp->Inner;
		PropPtr = RowElemPtr;
	}

	if (FObjectPropertyBase* ObjProp = CastField<FObjectPropertyBase>(Prop))
	{
		UObject* Asset = PropertyValue.IsEmpty() ? nullptr : LoadObject<UObject>(nullptr, *PropertyValue);
		if (!Asset && !PropertyValue.IsEmpty())
		{
			OutError = FString::Printf(TEXT("Could not load asset at '%s'"), *PropertyValue);
			return;
		}
		ObjProp->SetObjectPropertyValue(PropPtr, Asset);
	}
	else if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Prop))
	{
		BoolProp->SetPropertyValue(PropPtr,
			PropertyValue.Equals(TEXT("true"), ESearchCase::IgnoreCase) || PropertyValue == TEXT("1"));
	}
	else
	{
		const TCHAR* ImportResult = Prop->ImportText_Direct(*PropertyValue, PropPtr, nullptr, PPF_None, nullptr);
		if (!ImportResult)
		{
			OutError = FString::Printf(TEXT("Failed to set '%s': import failed for value '%s'"), *PropertyName, *PropertyValue);
			return;
		}
	}

	Table->Modify();
	Table->MarkPackageDirty();
	SaveChooserTable(Table, ChooserPath);

	OutJson = FString::Printf(
		TEXT("{\"success\":true,\"column_index\":%d,\"row_index\":%d,\"property\":\"%s\",\"chooser_path\":\"%s\"}"),
		ColumnIndex, RowIndex, *PropertyName, *ChooserPath);
}

void HandleSetChooserOutputType(const FString& ChooserPath, const FString& OutputClass,
	FString& OutJson, FString& OutError)
{

	UObject* Table = LoadObject<UObject>(nullptr, *ChooserPath);
	if (!Table || !Table->IsA(GetChooserTableClass()))
	{
		OutError = FString::Printf(TEXT("ChooserTable not found at '%s'"), *ChooserPath);
		return;
	}

	UClass* OutCls = FindObject<UClass>(nullptr, *OutputClass);
	if (!OutCls) OutCls = LoadObject<UClass>(nullptr, *OutputClass);
	if (!OutCls)
	{
		OutError = FString::Printf(TEXT("Output class not found: '%s'"), *OutputClass);
		return;
	}

	FObjectProperty* OOTProp =
		FindFProperty<FObjectProperty>(GetChooserTableClass(), TEXT("OutputObjectType"));
	if (!OOTProp) { OutError = TEXT("OutputObjectType property not found on ChooserTable"); return; }

	OOTProp->SetObjectPropertyValue(OOTProp->ContainerPtrToValuePtr<void>(Table), OutCls);
	Table->Modify();
	Table->MarkPackageDirty();
	SaveChooserTable(Table, ChooserPath);

	OutJson = FString::Printf(TEXT("{\"success\":true,\"output_class\":\"%s\",\"chooser_path\":\"%s\"}"),
		*OutputClass, *ChooserPath);
}

void HandleRemoveChooserRow(const FString& ChooserPath, int32 RowIndex,
	FString& OutJson, FString& OutError)
{

	UObject* Table = LoadObject<UObject>(nullptr, *ChooserPath);
	if (!Table || !Table->IsA(GetChooserTableClass()))
	{
		OutError = FString::Printf(TEXT("ChooserTable not found at '%s'"), *ChooserPath);
		return;
	}

	FArrayProperty* ResultsProp = GetResultsStructsProp();
	if (!ResultsProp) { OutError = TEXT("Could not reflect ResultsStructs"); return; }

	FScriptArrayHelper ResultsHelper(ResultsProp, ResultsProp->ContainerPtrToValuePtr<void>(Table));
	if (!ResultsHelper.IsValidIndex(RowIndex))
	{
		OutError = FString::Printf(TEXT("Row index %d out of range (count: %d)"),
			RowIndex, ResultsHelper.Num());
		return;
	}

	ResultsHelper.RemoveValues(RowIndex, 1);

	if (FArrayProperty* ColsProp = GetColumnsStructsProp())
	{
		FScriptArrayHelper ColsHelper(ColsProp, ColsProp->ContainerPtrToValuePtr<void>(Table));
		for (int32 i = 0; i < ColsHelper.Num(); ++i)
		{
			FInstancedStruct* ColStruct = reinterpret_cast<FInstancedStruct*>(ColsHelper.GetRawPtr(i));
			if (!ColStruct || !ColStruct->IsValid()) continue;
			UScriptStruct* ColType = const_cast<UScriptStruct*>(ColStruct->GetScriptStruct());
			if (FArrayProperty* RVProp = FindFProperty<FArrayProperty>(ColType, TEXT("RowValues")))
			{
				void* ColMem = ColStruct->GetMutableMemory();
				FScriptArrayHelper RVHelper(RVProp, RVProp->ContainerPtrToValuePtr<void>(ColMem));
				if (RVHelper.IsValidIndex(RowIndex))
					RVHelper.RemoveValues(RowIndex, 1);
			}
		}
	}

	Table->Modify();
	Table->MarkPackageDirty();
	SaveChooserTable(Table, ChooserPath);

	OutJson = FString::Printf(
		TEXT("{\"success\":true,\"removed_row_index\":%d,\"remaining_rows\":%d,\"chooser_path\":\"%s\"}"),
		RowIndex, ResultsHelper.Num(), *ChooserPath);
}

void HandleRemoveChooserColumn(const FString& ChooserPath, int32 ColumnIndex,
	FString& OutJson, FString& OutError)
{

	UObject* Table = LoadObject<UObject>(nullptr, *ChooserPath);
	if (!Table || !Table->IsA(GetChooserTableClass()))
	{
		OutError = FString::Printf(TEXT("ChooserTable not found at '%s'"), *ChooserPath);
		return;
	}

	FArrayProperty* ColsProp = GetColumnsStructsProp();
	if (!ColsProp) { OutError = TEXT("Could not reflect ColumnsStructs"); return; }

	FScriptArrayHelper ColsHelper(ColsProp, ColsProp->ContainerPtrToValuePtr<void>(Table));
	if (!ColsHelper.IsValidIndex(ColumnIndex))
	{
		OutError = FString::Printf(TEXT("Column index %d out of range (count: %d)"),
			ColumnIndex, ColsHelper.Num());
		return;
	}

	ColsHelper.RemoveValues(ColumnIndex, 1);
	Table->Modify();
	Table->MarkPackageDirty();
	SaveChooserTable(Table, ChooserPath);

	OutJson = FString::Printf(
		TEXT("{\"success\":true,\"removed_column_index\":%d,\"remaining_columns\":%d,\"chooser_path\":\"%s\"}"),
		ColumnIndex, ColsHelper.Num(), *ChooserPath);
}

void HandleSetChooserColumnProperty(const FString& ChooserPath, int32 ColumnIndex,
	const FString& PropertyName, const FString& PropertyValue, FString& OutJson, FString& OutError)
{

	UObject* Table = LoadObject<UObject>(nullptr, *ChooserPath);
	if (!Table || !Table->IsA(GetChooserTableClass()))
		{ OutError = FString::Printf(TEXT("ChooserTable not found at '%s'"), *ChooserPath); return; }

	FArrayProperty* ColsProp = GetColumnsStructsProp();
	if (!ColsProp) { OutError = TEXT("Could not reflect ColumnsStructs"); return; }

	FScriptArrayHelper ColsHelper(ColsProp, ColsProp->ContainerPtrToValuePtr<void>(Table));
	if (!ColsHelper.IsValidIndex(ColumnIndex))
	{
		OutError = FString::Printf(TEXT("Column index %d out of range (count: %d)"),
			ColumnIndex, ColsHelper.Num());
		return;
	}

	FInstancedStruct* ColStruct = reinterpret_cast<FInstancedStruct*>(ColsHelper.GetRawPtr(ColumnIndex));
	if (!ColStruct || !ColStruct->IsValid())
		{ OutError = FString::Printf(TEXT("Column at index %d is invalid"), ColumnIndex); return; }

	UScriptStruct* ColType = const_cast<UScriptStruct*>(ColStruct->GetScriptStruct());
	FProperty* Prop = FindFProperty<FProperty>(ColType, *PropertyName);
	if (!Prop)
	{
		TArray<FString> Names;
		for (TFieldIterator<FProperty> It(ColType); It; ++It) Names.Add(It->GetName());
		OutError = FString::Printf(TEXT("Property '%s' not found on '%s'. Available: %s"),
			*PropertyName, *ColType->GetName(), *FString::Join(Names, TEXT(", ")));
		return;
	}

	void* PropPtr = Prop->ContainerPtrToValuePtr<void>(ColStruct->GetMutableMemory());
	if (FObjectPropertyBase* ObjProp = CastField<FObjectPropertyBase>(Prop))
	{
		UObject* Asset = LoadObject<UObject>(nullptr, *PropertyValue);
		if (!Asset && !PropertyValue.IsEmpty())
		{
			UClass* Cls = FindObject<UClass>(nullptr, *PropertyValue);
			if (!Cls) Cls = LoadObject<UClass>(nullptr, *PropertyValue);
			if (Cls) { ObjProp->SetObjectPropertyValue(PropPtr, Cls); }
			else { OutError = FString::Printf(TEXT("Asset/Class not found at '%s'"), *PropertyValue); return; }
		}
		else { ObjProp->SetObjectPropertyValue(PropPtr, Asset); }
	}
	else if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Prop))
	{
		BoolProp->SetPropertyValue(PropPtr,
			PropertyValue.Equals(TEXT("true"), ESearchCase::IgnoreCase) || PropertyValue == TEXT("1"));
	}
	else
	{
		const TCHAR* R = Prop->ImportText_Direct(*PropertyValue, PropPtr, nullptr, PPF_None, nullptr);
		if (!R)
			{ OutError = FString::Printf(TEXT("Failed to set '%s': import failed for '%s'"), *PropertyName, *PropertyValue); return; }
	}

	Table->Modify();
	Table->MarkPackageDirty();
	SaveChooserTable(Table, ChooserPath);
	OutJson = FString::Printf(
		TEXT("{\"success\":true,\"column_index\":%d,\"column_type\":\"%s\",\"property\":\"%s\",\"value\":\"%s\"}"),
		ColumnIndex, *ColType->GetName(), *PropertyName, *PropertyValue);
}

void HandleSetChooserFallback(const FString& ChooserPath, const FString& AssetPath,
	FString& OutJson, FString& OutError)
{

	UObject* Table = LoadObject<UObject>(nullptr, *ChooserPath);
	if (!Table || !Table->IsA(GetChooserTableClass()))
		{ OutError = FString::Printf(TEXT("ChooserTable not found at '%s'"), *ChooserPath); return; }

	UClass* TableClass = Table->GetClass();

	static const TCHAR* FallbackNames[] = {
		TEXT("FallbackResult"), TEXT("Fallback"), TEXT("DefaultResult"),
		TEXT("FallbackValue"), TEXT("FallbackObject"), nullptr
	};
	FProperty* FallbackProp = nullptr;
	for (int32 i = 0; FallbackNames[i]; ++i)
	{
		FallbackProp = FindFProperty<FProperty>(TableClass, FallbackNames[i]);
		if (FallbackProp) break;
	}

	if (!FallbackProp)
	{
		TArray<FString> TopLevel;
		for (TFieldIterator<FProperty> It(TableClass); It; ++It) TopLevel.Add(It->GetName());
		OutError = FString::Printf(TEXT("No fallback property found on UChooserTable. Properties: %s"),
			*FString::Join(TopLevel, TEXT(", ")));
		return;
	}

	void* PropPtr = FallbackProp->ContainerPtrToValuePtr<void>(Table);

	if (FStructProperty* StructProp = CastField<FStructProperty>(FallbackProp))
	{
		if (StructProp->Struct && StructProp->Struct->GetName().Contains(TEXT("InstancedStruct")))
		{
			FInstancedStruct* FallbackStruct = reinterpret_cast<FInstancedStruct*>(PropPtr);
			if (!AssetPath.IsEmpty())
			{
				UObject* Asset = LoadObject<UObject>(nullptr, *AssetPath);
				if (!Asset) { OutError = FString::Printf(TEXT("Asset not found at '%s'"), *AssetPath); return; }
				UScriptStruct* AssetChooserStruct = FindChooserStruct(TEXT("AssetChooser"));
				if (AssetChooserStruct)
				{
					FallbackStruct->InitializeAs(AssetChooserStruct, nullptr);
					FObjectProperty* AssetPropInner = FindFProperty<FObjectProperty>(AssetChooserStruct, TEXT("Asset"));
					if (AssetPropInner)
						AssetPropInner->SetObjectPropertyValue(
							AssetPropInner->ContainerPtrToValuePtr<void>(FallbackStruct->GetMutableMemory()), Asset);
				}
			}
			else { FallbackStruct->Reset(); }
			Table->Modify();
			Table->MarkPackageDirty();
			SaveChooserTable(Table, ChooserPath);
			OutJson = FString::Printf(TEXT("{\"success\":true,\"fallback_property\":\"%s\",\"asset_path\":\"%s\"}"),
				*FallbackProp->GetName(), *AssetPath);
			return;
		}
	}

	if (FObjectPropertyBase* ObjProp = CastField<FObjectPropertyBase>(FallbackProp))
	{
		UObject* Asset = AssetPath.IsEmpty() ? nullptr : LoadObject<UObject>(nullptr, *AssetPath);
		if (!Asset && !AssetPath.IsEmpty())
			{ OutError = FString::Printf(TEXT("Asset not found at '%s'"), *AssetPath); return; }
		ObjProp->SetObjectPropertyValue(PropPtr, Asset);
		Table->Modify();
		Table->MarkPackageDirty();
		SaveChooserTable(Table, ChooserPath);
		OutJson = FString::Printf(TEXT("{\"success\":true,\"fallback_property\":\"%s\",\"asset_path\":\"%s\"}"),
			*FallbackProp->GetName(), *AssetPath);
		return;
	}

	const TCHAR* R = FallbackProp->ImportText_Direct(*AssetPath, PropPtr, Table, PPF_None, nullptr);
	if (!R)
		{ OutError = FString::Printf(TEXT("Failed to set fallback property '%s'"), *FallbackProp->GetName()); return; }
	Table->Modify();
	Table->MarkPackageDirty();
	SaveChooserTable(Table, ChooserPath);
	OutJson = FString::Printf(TEXT("{\"success\":true,\"fallback_property\":\"%s\"}"), *FallbackProp->GetName());
}

void HandleDuplicateChooserRow(const FString& ChooserPath, int32 RowIndex,
	FString& OutJson, FString& OutError)
{

	UObject* Table = LoadObject<UObject>(nullptr, *ChooserPath);
	if (!Table || !Table->IsA(GetChooserTableClass()))
		{ OutError = FString::Printf(TEXT("ChooserTable not found at '%s'"), *ChooserPath); return; }

	FArrayProperty* ResultsProp = GetResultsStructsProp();
	FArrayProperty* ColsProp = GetColumnsStructsProp();
	if (!ResultsProp || !ColsProp)
		{ OutError = TEXT("Could not reflect ResultsStructs/ColumnsStructs"); return; }

	FScriptArrayHelper ResultsHelper(ResultsProp, ResultsProp->ContainerPtrToValuePtr<void>(Table));
	if (!ResultsHelper.IsValidIndex(RowIndex))
		{ OutError = FString::Printf(TEXT("Row index %d out of range (count: %d)"), RowIndex, ResultsHelper.Num()); return; }

	FInstancedStruct* SrcRow = reinterpret_cast<FInstancedStruct*>(ResultsHelper.GetRawPtr(RowIndex));
	UScriptStruct* RowStructType = SrcRow->IsValid() ? const_cast<UScriptStruct*>(SrcRow->GetScriptStruct()) : nullptr;
	const uint8* SrcMem = SrcRow->GetMemory();

	const int32 NewRowIdx = ResultsHelper.AddValue();
	FInstancedStruct* NewRow = reinterpret_cast<FInstancedStruct*>(ResultsHelper.GetRawPtr(NewRowIdx));
	if (RowStructType && SrcMem)
	{
		NewRow->InitializeAs(RowStructType, nullptr);
		if (NewRow->GetMutableMemory())
			RowStructType->CopyScriptStruct(NewRow->GetMutableMemory(), SrcMem);
	}

	FScriptArrayHelper ColsHelper(ColsProp, ColsProp->ContainerPtrToValuePtr<void>(Table));
	for (int32 i = 0; i < ColsHelper.Num(); ++i)
	{
		FInstancedStruct* ColStruct = reinterpret_cast<FInstancedStruct*>(ColsHelper.GetRawPtr(i));
		if (!ColStruct || !ColStruct->IsValid()) continue;
		UScriptStruct* ColType = const_cast<UScriptStruct*>(ColStruct->GetScriptStruct());
		FArrayProperty* RVProp = FindFProperty<FArrayProperty>(ColType, TEXT("RowValues"));
		if (!RVProp) continue;

		void* ColMem = ColStruct->GetMutableMemory();
		FScriptArrayHelper RVHelper(RVProp, RVProp->ContainerPtrToValuePtr<void>(ColMem));

		if (!RVHelper.IsValidIndex(RowIndex)) { RVHelper.AddValue(); continue; }

		FStructProperty* InnerProp = CastField<FStructProperty>(RVProp->Inner);
		if (!InnerProp) { RVHelper.AddValue(); continue; }

		UScriptStruct* CellType = InnerProp->Struct;
		int32 CellSize = CellType->GetStructureSize();

		TArray<uint8> CellSnap;
		CellSnap.SetNumUninitialized(CellSize);
		CellType->CopyScriptStruct(CellSnap.GetData(), RVHelper.GetRawPtr(RowIndex));

		const int32 NewCellIdx = RVHelper.AddValue();
		CellType->CopyScriptStruct(RVHelper.GetRawPtr(NewCellIdx), CellSnap.GetData());
	}

	Table->Modify();
	Table->MarkPackageDirty();
	SaveChooserTable(Table, ChooserPath);
	OutJson = FString::Printf(
		TEXT("{\"success\":true,\"source_row_index\":%d,\"new_row_index\":%d,\"total_rows\":%d}"),
		RowIndex, NewRowIdx, ResultsHelper.Num());
}

void HandleReorderChooserRows(const FString& ChooserPath, int32 FromIndex, int32 ToIndex,
	FString& OutJson, FString& OutError)
{

	if (FromIndex == ToIndex)
	{
		OutJson = TEXT("{\"success\":true,\"message\":\"No change needed\"}");
		return;
	}

	UObject* Table = LoadObject<UObject>(nullptr, *ChooserPath);
	if (!Table || !Table->IsA(GetChooserTableClass()))
		{ OutError = FString::Printf(TEXT("ChooserTable not found at '%s'"), *ChooserPath); return; }

	FArrayProperty* ResultsProp = GetResultsStructsProp();
	FArrayProperty* ColsProp = GetColumnsStructsProp();
	if (!ResultsProp || !ColsProp)
		{ OutError = TEXT("Could not reflect ResultsStructs/ColumnsStructs"); return; }

	FScriptArrayHelper ResultsHelper(ResultsProp, ResultsProp->ContainerPtrToValuePtr<void>(Table));
	if (!ResultsHelper.IsValidIndex(FromIndex) || !ResultsHelper.IsValidIndex(ToIndex))
	{
		OutError = FString::Printf(TEXT("Index out of range: from=%d to=%d (count=%d)"),
			FromIndex, ToIndex, ResultsHelper.Num());
		return;
	}

	auto BubbleMove = [](FScriptArrayHelper& Arr, int32 ElemSz, int32 From, int32 To)
	{
		if (From < To)
			for (int32 i = From; i < To; ++i) FMemory::Memswap(Arr.GetRawPtr(i), Arr.GetRawPtr(i + 1), ElemSz);
		else
			for (int32 i = From; i > To; --i) FMemory::Memswap(Arr.GetRawPtr(i), Arr.GetRawPtr(i - 1), ElemSz);
	};

	int32 RowElemSz = ResultsProp->Inner->GetSize();
	BubbleMove(ResultsHelper, RowElemSz, FromIndex, ToIndex);

	FScriptArrayHelper ColsHelper(ColsProp, ColsProp->ContainerPtrToValuePtr<void>(Table));
	for (int32 i = 0; i < ColsHelper.Num(); ++i)
	{
		FInstancedStruct* ColStruct = reinterpret_cast<FInstancedStruct*>(ColsHelper.GetRawPtr(i));
		if (!ColStruct || !ColStruct->IsValid()) continue;
		UScriptStruct* ColType = const_cast<UScriptStruct*>(ColStruct->GetScriptStruct());
		FArrayProperty* RVProp = FindFProperty<FArrayProperty>(ColType, TEXT("RowValues"));
		if (!RVProp) continue;
		void* ColMem = ColStruct->GetMutableMemory();
		FScriptArrayHelper RVHelper(RVProp, RVProp->ContainerPtrToValuePtr<void>(ColMem));
		if (!RVHelper.IsValidIndex(FromIndex) || !RVHelper.IsValidIndex(ToIndex)) continue;
		int32 CellSz = RVProp->Inner->GetSize();
		BubbleMove(RVHelper, CellSz, FromIndex, ToIndex);
	}

	Table->Modify();
	Table->MarkPackageDirty();
	SaveChooserTable(Table, ChooserPath);
	OutJson = FString::Printf(TEXT("{\"success\":true,\"moved_from\":%d,\"moved_to\":%d}"),
		FromIndex, ToIndex);
}

void HandleGetChooserRowValues(const FString& ChooserPath, int32 RowIndex,
	FString& OutJson, FString& OutError)
{

	UObject* Table = LoadObject<UObject>(nullptr, *ChooserPath);
	if (!Table || !Table->IsA(GetChooserTableClass()))
		{ OutError = FString::Printf(TEXT("ChooserTable not found at '%s'"), *ChooserPath); return; }

	FArrayProperty* ColsProp = GetColumnsStructsProp();
	if (!ColsProp) { OutError = TEXT("Could not reflect ColumnsStructs"); return; }

	FScriptArrayHelper ColsHelper(ColsProp, ColsProp->ContainerPtrToValuePtr<void>(Table));

	FString ColsJson = TEXT("[");
	bool bFirstCol = true;
	for (int32 ColIdx = 0; ColIdx < ColsHelper.Num(); ++ColIdx)
	{
		FInstancedStruct* ColStruct = reinterpret_cast<FInstancedStruct*>(ColsHelper.GetRawPtr(ColIdx));
		if (!ColStruct || !ColStruct->IsValid()) continue;

		UScriptStruct* ColType = const_cast<UScriptStruct*>(ColStruct->GetScriptStruct());
		FArrayProperty* RVProp = FindFProperty<FArrayProperty>(ColType, TEXT("RowValues"));
		if (!RVProp) continue;

		void* ColMem = ColStruct->GetMutableMemory();
		FScriptArrayHelper RVHelper(RVProp, RVProp->ContainerPtrToValuePtr<void>(ColMem));
		if (!RVHelper.IsValidIndex(RowIndex)) continue;

		FStructProperty* CellProp = CastField<FStructProperty>(RVProp->Inner);
		if (!CellProp) continue;

		void* CellPtr = RVHelper.GetRawPtr(RowIndex);
		FString PropsJson = TEXT("[");
		bool bFirstProp = true;
		for (TFieldIterator<FProperty> It(CellProp->Struct); It; ++It)
		{
			FProperty* P = *It;
			FString Val;
			P->ExportTextItem_Direct(Val, P->ContainerPtrToValuePtr<void>(CellPtr), nullptr, nullptr, PPF_None);
			FString SafeVal = Val.Replace(TEXT("\""), TEXT("\\\""));
			if (!bFirstProp) PropsJson += TEXT(",");
			PropsJson += FString::Printf(TEXT("{\"name\":\"%s\",\"value\":\"%s\"}"), *P->GetName(), *SafeVal);
			bFirstProp = false;
		}
		PropsJson += TEXT("]");

		if (!bFirstCol) ColsJson += TEXT(",");
		ColsJson += FString::Printf(TEXT("{\"column_index\":%d,\"column_type\":\"%s\",\"properties\":%s}"),
			ColIdx, *ColType->GetName(), *PropsJson);
		bFirstCol = false;
	}
	ColsJson += TEXT("]");

	OutJson = FString::Printf(TEXT("{\"success\":true,\"row_index\":%d,\"columns\":%s}"), RowIndex, *ColsJson);
}

void HandleGetChooserColumnProperties(const FString& ChooserPath, int32 ColumnIndex,
	FString& OutJson, FString& OutError)
{

	UObject* Table = LoadObject<UObject>(nullptr, *ChooserPath);
	if (!Table || !Table->IsA(GetChooserTableClass()))
		{ OutError = FString::Printf(TEXT("ChooserTable not found at '%s'"), *ChooserPath); return; }

	FArrayProperty* ColsProp = GetColumnsStructsProp();
	if (!ColsProp) { OutError = TEXT("Could not reflect ColumnsStructs"); return; }

	FScriptArrayHelper ColsHelper(ColsProp, ColsProp->ContainerPtrToValuePtr<void>(Table));
	if (!ColsHelper.IsValidIndex(ColumnIndex))
	{
		OutError = FString::Printf(TEXT("Column index %d out of range (count: %d)"), ColumnIndex, ColsHelper.Num());
		return;
	}

	FInstancedStruct* ColStruct = reinterpret_cast<FInstancedStruct*>(ColsHelper.GetRawPtr(ColumnIndex));
	if (!ColStruct || !ColStruct->IsValid()) { OutError = TEXT("Column struct is invalid"); return; }

	const UScriptStruct* ColType = ColStruct->GetScriptStruct();
	const void* ColMem = ColStruct->GetMemory();

	FString PropsJson = TEXT("[");
	bool bFirst = true;
	for (TFieldIterator<FProperty> It(ColType); It; ++It)
	{
		FProperty* P = *It;
		FString Val;
		P->ExportTextItem_Direct(Val, P->ContainerPtrToValuePtr<void>(ColMem), nullptr, nullptr, PPF_None);
		FString SafeVal = Val.Replace(TEXT("\""), TEXT("\\\""));
		if (!bFirst) PropsJson += TEXT(",");
		PropsJson += FString::Printf(TEXT("{\"name\":\"%s\",\"type\":\"%s\",\"value\":\"%s\"}"),
			*P->GetName(), *P->GetClass()->GetName(), *SafeVal);
		bFirst = false;
	}
	PropsJson += TEXT("]");

	OutJson = FString::Printf(TEXT("{\"success\":true,\"column_index\":%d,\"column_type\":\"%s\",\"properties\":%s}"),
		ColumnIndex, *ColType->GetName(), *PropsJson);
}

void HandleBulkSetChooserRows(const FString& ChooserPath, const FString& RowsJson,
	FString& OutJson, FString& OutError)
{

	TSharedPtr<FJsonValue> ParsedVal;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(RowsJson);
	if (!FJsonSerializer::Deserialize(Reader, ParsedVal) || ParsedVal->Type != EJson::Array)
	{
		OutError = TEXT("rows_json must be a JSON array of {column_index, row_index, property_name, property_value}");
		return;
	}

	const TArray<TSharedPtr<FJsonValue>>& Entries = ParsedVal->AsArray();
	int32 SuccessCount = 0;
	FString ErrorsJson = TEXT("[");
	bool bFirstErr = true;

	{
		FScopedSuppressChooserSave SuppressPerCellSaves;
		for (int32 i = 0; i < Entries.Num(); ++i)
		{
			const TSharedPtr<FJsonObject>* EntryObj;
			if (!Entries[i]->TryGetObject(EntryObj)) continue;

			int32 ColIdx = (int32)(*EntryObj)->GetNumberField(TEXT("column_index"));
			int32 RowIdx = (int32)(*EntryObj)->GetNumberField(TEXT("row_index"));
			FString PropName = (*EntryObj)->GetStringField(TEXT("property_name"));
			FString PropVal  = (*EntryObj)->GetStringField(TEXT("property_value"));

			FString CellJson, CellErr;
			HandleSetChooserRowValue(ChooserPath, ColIdx, RowIdx, PropName, PropVal, CellJson, CellErr);
			if (CellErr.IsEmpty())
			{
				++SuccessCount;
			}
			else
			{
				if (!bFirstErr) ErrorsJson += TEXT(",");
				FString SafeErr = CellErr.Replace(TEXT("\""), TEXT("\\\""));
				ErrorsJson += FString::Printf(TEXT("{\"entry_index\":%d,\"error\":\"%s\"}"), i, *SafeErr);
				bFirstErr = false;
			}
		}
	}
	ErrorsJson += TEXT("]");

	// Persist once for the whole batch.
	bool bSaved = false;
	if (SuccessCount > 0)
	{
		if (UObject* Table = LoadObject<UObject>(nullptr, *ChooserPath))
		{
			SaveChooserTable(Table, ChooserPath);
			bSaved = true;
		}
	}

	OutJson = FString::Printf(
		TEXT("{\"success\":true,\"success_count\":%d,\"error_count\":%d,\"saved\":%s,\"errors\":%s}"),
		SuccessCount, Entries.Num() - SuccessCount, bSaved ? TEXT("true") : TEXT("false"), *ErrorsJson);
}

void HandleRenameChooserColumn(const FString& ChooserPath, int32 ColumnIndex,
	const FString& NewLabel, FString& OutJson, FString& OutError)
{

	static const TArray<FString> LabelPropNames = {
		TEXT("Tag"), TEXT("Label"), TEXT("ColumnName"), TEXT("Name"), TEXT("DisplayName")
	};

	for (const FString& PropName : LabelPropNames)
	{
		FString TryJson, TryErr;
		HandleSetChooserColumnProperty(ChooserPath, ColumnIndex, PropName, NewLabel, TryJson, TryErr);
		if (TryErr.IsEmpty())
		{
			OutJson = TryJson;
			return;
		}
	}

	OutError = FString::Printf(
		TEXT("No label property found on column %d — tried Tag, Label, ColumnName, Name, DisplayName"),
		ColumnIndex);
}

void HandleReorderChooserColumns(const FString& ChooserPath, int32 FromIndex, int32 ToIndex,
	FString& OutJson, FString& OutError)
{

	if (FromIndex == ToIndex)
	{
		OutJson = TEXT("{\"success\":true,\"message\":\"No change needed\"}");
		return;
	}

	UObject* Table = LoadObject<UObject>(nullptr, *ChooserPath);
	if (!Table || !Table->IsA(GetChooserTableClass()))
		{ OutError = FString::Printf(TEXT("ChooserTable not found at '%s'"), *ChooserPath); return; }

	FArrayProperty* ColsProp = GetColumnsStructsProp();
	if (!ColsProp) { OutError = TEXT("Could not reflect ColumnsStructs"); return; }

	FScriptArrayHelper ColsHelper(ColsProp, ColsProp->ContainerPtrToValuePtr<void>(Table));
	if (!ColsHelper.IsValidIndex(FromIndex) || !ColsHelper.IsValidIndex(ToIndex))
	{
		OutError = FString::Printf(TEXT("Column index out of range: from=%d to=%d (count=%d)"),
			FromIndex, ToIndex, ColsHelper.Num());
		return;
	}

	const int32 ElemSz = static_cast<int32>(ColsProp->Inner->GetSize());
	if (FromIndex < ToIndex)
		for (int32 i = FromIndex; i < ToIndex; ++i) FMemory::Memswap(ColsHelper.GetRawPtr(i), ColsHelper.GetRawPtr(i + 1), ElemSz);
	else
		for (int32 i = FromIndex; i > ToIndex; --i) FMemory::Memswap(ColsHelper.GetRawPtr(i), ColsHelper.GetRawPtr(i - 1), ElemSz);

	Table->Modify();
	Table->MarkPackageDirty();
	SaveChooserTable(Table, ChooserPath);
	OutJson = FString::Printf(TEXT("{\"success\":true,\"moved_from\":%d,\"moved_to\":%d}"), FromIndex, ToIndex);
}

void HandleDuplicateChooserColumn(const FString& ChooserPath, int32 ColumnIndex,
	FString& OutJson, FString& OutError)
{

	UObject* Table = LoadObject<UObject>(nullptr, *ChooserPath);
	if (!Table || !Table->IsA(GetChooserTableClass()))
		{ OutError = FString::Printf(TEXT("ChooserTable not found at '%s'"), *ChooserPath); return; }

	FArrayProperty* ColsProp = GetColumnsStructsProp();
	if (!ColsProp) { OutError = TEXT("Could not reflect ColumnsStructs"); return; }

	FScriptArrayHelper ColsHelper(ColsProp, ColsProp->ContainerPtrToValuePtr<void>(Table));
	if (!ColsHelper.IsValidIndex(ColumnIndex))
	{
		OutError = FString::Printf(TEXT("Column index %d out of range (count: %d)"), ColumnIndex, ColsHelper.Num());
		return;
	}

	FInstancedStruct* SrcCol = reinterpret_cast<FInstancedStruct*>(ColsHelper.GetRawPtr(ColumnIndex));
	if (!SrcCol || !SrcCol->IsValid()) { OutError = TEXT("Source column struct is invalid"); return; }

	const UScriptStruct* ColType = SrcCol->GetScriptStruct();
	TArray<uint8> Snap;
	Snap.SetNumZeroed(ColType->GetStructureSize());
	ColType->InitializeStruct(Snap.GetData());
	ColType->CopyScriptStruct(Snap.GetData(), SrcCol->GetMemory());

	ColsHelper.AddValue();
	FInstancedStruct* NewCol = reinterpret_cast<FInstancedStruct*>(ColsHelper.GetRawPtr(ColsHelper.Num() - 1));
	NewCol->InitializeAs(ColType, nullptr);
	ColType->CopyScriptStruct(NewCol->GetMutableMemory(), Snap.GetData());
	ColType->DestroyStruct(Snap.GetData());

	Table->Modify();
	Table->MarkPackageDirty();
	SaveChooserTable(Table, ChooserPath);
	OutJson = FString::Printf(
		TEXT("{\"success\":true,\"source_index\":%d,\"new_index\":%d,\"column_count\":%d}"),
		ColumnIndex, ColsHelper.Num() - 1, ColsHelper.Num());
}

void HandleSetChooserContextData(const FString& ChooserPath, const FString& ContextClass,
	FString& OutJson, FString& OutError)
{

	UObject* Table = LoadObject<UObject>(nullptr, *ChooserPath);
	if (!Table || !Table->IsA(GetChooserTableClass()))
		{ OutError = FString::Printf(TEXT("ChooserTable not found at '%s'"), *ChooserPath); return; }

	UClass* CtxClass = FindObject<UClass>(nullptr, *ContextClass);
	if (!CtxClass)
	{
		for (TObjectIterator<UClass> It; It; ++It)
		{
			if (It->GetName().Equals(ContextClass, ESearchCase::IgnoreCase))
				{ CtxClass = *It; break; }
		}
	}
	if (!CtxClass) { OutError = FString::Printf(TEXT("Class not found: '%s'"), *ContextClass); return; }

	static const TArray<FName> CtxPropNames = { TEXT("ContextData"), TEXT("ContextObjectTypes"), TEXT("Context") };
	FArrayProperty* CtxProp = nullptr;
	for (const FName& N : CtxPropNames)
	{
		CtxProp = FindFProperty<FArrayProperty>(Table->GetClass(), N);
		if (CtxProp) break;
	}

	if (!CtxProp)
	{
		for (const FName& N : CtxPropNames)
		{
			FObjectPropertyBase* ObjProp = FindFProperty<FObjectPropertyBase>(Table->GetClass(), N);
			if (ObjProp)
			{
				ObjProp->SetObjectPropertyValue(ObjProp->ContainerPtrToValuePtr<void>(Table), CtxClass);
				Table->Modify();
				Table->MarkPackageDirty();
				SaveChooserTable(Table, ChooserPath);
				OutJson = FString::Printf(TEXT("{\"success\":true,\"context_class\":\"%s\"}"), *CtxClass->GetPathName());
				return;
			}
		}
		OutError = TEXT("No context data property found on UChooserTable in this UE version");
		return;
	}

	FScriptArrayHelper CtxHelper(CtxProp, CtxProp->ContainerPtrToValuePtr<void>(Table));

	FObjectPropertyBase* InnerObj = CastField<FObjectPropertyBase>(CtxProp->Inner);
	if (InnerObj)
	{
		for (int32 i = 0; i < CtxHelper.Num(); ++i)
		{
			if (InnerObj->GetObjectPropertyValue(CtxHelper.GetRawPtr(i)) == CtxClass)
			{
				OutJson = FString::Printf(TEXT("{\"success\":true,\"message\":\"Already present\",\"context_count\":%d}"), CtxHelper.Num());
				return;
			}
		}
		CtxHelper.AddValue();
		InnerObj->SetObjectPropertyValue(CtxHelper.GetRawPtr(CtxHelper.Num() - 1), CtxClass);
		Table->Modify();
		Table->MarkPackageDirty();
		SaveChooserTable(Table, ChooserPath);
		OutJson = FString::Printf(TEXT("{\"success\":true,\"context_class\":\"%s\",\"context_count\":%d}"),
			*CtxClass->GetPathName(), CtxHelper.Num());
		return;
	}

	FStructProperty* InnerStruct = CastField<FStructProperty>(CtxProp->Inner);
	if (InnerStruct && InnerStruct->Struct && InnerStruct->Struct->GetName() == TEXT("InstancedStruct"))
	{
		static const TArray<FString> DescStructNames = {
			TEXT("ObjectChooserBase_ContextProperty"), TEXT("ChooserObjectContextData"),
			TEXT("ContextObjectTypeFilter"), TEXT("ChooserParameterObjectBase")
		};
		UScriptStruct* DescStruct = nullptr;
		for (const FString& SN : DescStructNames)
		{
			DescStruct = FindChooserStruct(SN);
			if (DescStruct) break;
		}

		if (!DescStruct)
		{
			for (TObjectIterator<UScriptStruct> It; It; ++It)
			{
				const FString Path = It->GetPathName();
				if (!Path.Contains(TEXT("Chooser"), ESearchCase::IgnoreCase)) continue;
				for (TFieldIterator<FObjectPropertyBase> PIt(*It); PIt; ++PIt)
				{
					if (PIt->PropertyClass && PIt->PropertyClass->IsChildOf(UClass::StaticClass()))
						{ DescStruct = *It; break; }
				}
				if (DescStruct) break;
			}
		}

		if (!DescStruct)
		{
			OutError = TEXT("Could not find a suitable context descriptor struct in the Chooser plugin");
			return;
		}

		CtxHelper.AddValue();
		FInstancedStruct* Entry = reinterpret_cast<FInstancedStruct*>(CtxHelper.GetRawPtr(CtxHelper.Num() - 1));
		Entry->InitializeAs(DescStruct, nullptr);

		for (TFieldIterator<FObjectPropertyBase> PIt(DescStruct); PIt; ++PIt)
		{
			if (PIt->PropertyClass && PIt->PropertyClass->IsChildOf(UClass::StaticClass()))
			{
				PIt->SetObjectPropertyValue(PIt->ContainerPtrToValuePtr<void>(Entry->GetMutableMemory()), CtxClass);
				break;
			}
		}

		Table->Modify();
		Table->MarkPackageDirty();
		SaveChooserTable(Table, ChooserPath);
		OutJson = FString::Printf(TEXT("{\"success\":true,\"context_class\":\"%s\",\"descriptor_struct\":\"%s\",\"context_count\":%d}"),
			*CtxClass->GetPathName(), *DescStruct->GetName(), CtxHelper.Num());
		return;
	}

	OutError = TEXT("ContextData inner property type is not supported (expected object or instanced struct)");
}

void HandleAddChooserColumnFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	FString ChooserPath; Args->TryGetStringField(TEXT("chooser_path"), ChooserPath);

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("columns"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString CT = BatchToolHelper::GetItemString(Item, TEXT("column_type"), TEXT("type"));
			FString Label = BatchToolHelper::GetItemString(Item, TEXT("label"), TEXT("name"));
			if (CT.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing column_type")); continue; }
			FString ItemOut, ItemErr;
			HandleAddChooserColumn(ChooserPath, CT, Label, ItemOut, ItemErr);
			if (ItemErr.IsEmpty())
			{
				auto Extra = MakeShared<FJsonObject>(); Extra->SetStringField(TEXT("column_type"), CT);
				Batch.AddSuccess(i, Extra);
			}
			else Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJson);
		return;
	}
	FString CT, Label;
	Args->TryGetStringField(TEXT("column_type"), CT); Args->TryGetStringField(TEXT("label"), Label);
	HandleAddChooserColumn(ChooserPath, CT, Label, OutJson, OutError);
}

void HandleAddChooserRowFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	FString ChooserPath; Args->TryGetStringField(TEXT("chooser_path"), ChooserPath);

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("rows"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			FString AP;
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (Item.IsValid())
				AP = BatchToolHelper::GetItemString(Item, TEXT("asset_path"), TEXT("path"));
			else
				(*ItemsArray)[i]->TryGetString(AP);
			FString ItemOut, ItemErr;
			HandleAddChooserRow(ChooserPath, AP, ItemOut, ItemErr);
			if (ItemErr.IsEmpty()) Batch.AddSuccess(i);
			else Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJson);
		return;
	}
	FString AP; Args->TryGetStringField(TEXT("asset_path"), AP);
	HandleAddChooserRow(ChooserPath, AP, OutJson, OutError);
}

void HandleCreateChooserTableFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString AssetName = TEXT("CT_New"), SavePath, OutputClass;
	Args->TryGetStringField(TEXT("asset_name"), AssetName);
	Args->TryGetStringField(TEXT("save_path"), SavePath);
	Args->TryGetStringField(TEXT("output_class"), OutputClass);
	if (SavePath.IsEmpty()) SavePath = FSettingsManager::GetDefaultSavePath();
	HandleCreateChooserTable(AssetName, SavePath, OutputClass, OutJson, OutError);
}

void HandleGetChooserSummaryFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString ChooserPath;
	Args->TryGetStringField(TEXT("chooser_path"), ChooserPath);
	HandleGetChooserSummary(ChooserPath, OutJson, OutError);
}

void HandleSetChooserRowValueFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString ChooserPath, PropertyName, PropertyValue;
	int32 ColumnIndex = 0, RowIndex = 0;
	Args->TryGetStringField(TEXT("chooser_path"), ChooserPath);
	Args->TryGetNumberField(TEXT("column_index"), ColumnIndex);
	Args->TryGetNumberField(TEXT("row_index"), RowIndex);
	Args->TryGetStringField(TEXT("property_name"), PropertyName);
	Args->TryGetStringField(TEXT("property_value"), PropertyValue);
	HandleSetChooserRowValue(ChooserPath, ColumnIndex, RowIndex, PropertyName, PropertyValue, OutJson, OutError);
}

void HandleSetChooserOutputTypeFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString ChooserPath, OutputClass;
	Args->TryGetStringField(TEXT("chooser_path"), ChooserPath);
	Args->TryGetStringField(TEXT("output_class"), OutputClass);
	HandleSetChooserOutputType(ChooserPath, OutputClass, OutJson, OutError);
}

void HandleRemoveChooserRowFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString ChooserPath;
	int32 RowIndex = 0;
	Args->TryGetStringField(TEXT("chooser_path"), ChooserPath);
	Args->TryGetNumberField(TEXT("row_index"), RowIndex);
	HandleRemoveChooserRow(ChooserPath, RowIndex, OutJson, OutError);
}

void HandleRemoveChooserColumnFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString ChooserPath;
	int32 ColumnIndex = 0;
	Args->TryGetStringField(TEXT("chooser_path"), ChooserPath);
	Args->TryGetNumberField(TEXT("column_index"), ColumnIndex);
	HandleRemoveChooserColumn(ChooserPath, ColumnIndex, OutJson, OutError);
}

void HandleSetChooserColumnPropertyFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString ChooserPath, PropertyName, PropertyValue;
	int32 ColumnIndex = 0;
	Args->TryGetStringField(TEXT("chooser_path"), ChooserPath);
	Args->TryGetNumberField(TEXT("column_index"), ColumnIndex);
	Args->TryGetStringField(TEXT("property_name"), PropertyName);
	Args->TryGetStringField(TEXT("property_value"), PropertyValue);
	HandleSetChooserColumnProperty(ChooserPath, ColumnIndex, PropertyName, PropertyValue, OutJson, OutError);
}

void HandleSetChooserFallbackFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString ChooserPath, AssetPath;
	Args->TryGetStringField(TEXT("chooser_path"), ChooserPath);
	Args->TryGetStringField(TEXT("asset_path"), AssetPath);
	HandleSetChooserFallback(ChooserPath, AssetPath, OutJson, OutError);
}

void HandleDuplicateChooserRowFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString ChooserPath;
	int32 RowIndex = 0;
	Args->TryGetStringField(TEXT("chooser_path"), ChooserPath);
	Args->TryGetNumberField(TEXT("row_index"), RowIndex);
	HandleDuplicateChooserRow(ChooserPath, RowIndex, OutJson, OutError);
}

void HandleReorderChooserRowsFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString ChooserPath;
	int32 FromIndex = 0, ToIndex = 0;
	Args->TryGetStringField(TEXT("chooser_path"), ChooserPath);
	Args->TryGetNumberField(TEXT("from_index"), FromIndex);
	Args->TryGetNumberField(TEXT("to_index"), ToIndex);
	HandleReorderChooserRows(ChooserPath, FromIndex, ToIndex, OutJson, OutError);
}

void HandleGetChooserRowValuesFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString ChooserPath;
	int32 RowIndex = 0;
	Args->TryGetStringField(TEXT("chooser_path"), ChooserPath);
	Args->TryGetNumberField(TEXT("row_index"), RowIndex);
	HandleGetChooserRowValues(ChooserPath, RowIndex, OutJson, OutError);
}

void HandleGetChooserColumnPropertiesFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString ChooserPath;
	int32 ColumnIndex = 0;
	Args->TryGetStringField(TEXT("chooser_path"), ChooserPath);
	Args->TryGetNumberField(TEXT("column_index"), ColumnIndex);
	HandleGetChooserColumnProperties(ChooserPath, ColumnIndex, OutJson, OutError);
}

void HandleBulkSetChooserRowsFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString ChooserPath, RowsJson;
	Args->TryGetStringField(TEXT("chooser_path"), ChooserPath);
	Args->TryGetStringField(TEXT("rows_json"), RowsJson);
	HandleBulkSetChooserRows(ChooserPath, RowsJson, OutJson, OutError);
}

void HandleRenameChooserColumnFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString ChooserPath, NewLabel;
	int32 ColumnIndex = 0;
	Args->TryGetStringField(TEXT("chooser_path"), ChooserPath);
	Args->TryGetNumberField(TEXT("column_index"), ColumnIndex);
	Args->TryGetStringField(TEXT("new_label"), NewLabel);
	HandleRenameChooserColumn(ChooserPath, ColumnIndex, NewLabel, OutJson, OutError);
}

void HandleReorderChooserColumnsFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString ChooserPath;
	int32 FromIndex = 0, ToIndex = 0;
	Args->TryGetStringField(TEXT("chooser_path"), ChooserPath);
	Args->TryGetNumberField(TEXT("from_index"), FromIndex);
	Args->TryGetNumberField(TEXT("to_index"), ToIndex);
	HandleReorderChooserColumns(ChooserPath, FromIndex, ToIndex, OutJson, OutError);
}

void HandleDuplicateChooserColumnFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString ChooserPath;
	int32 ColumnIndex = 0;
	Args->TryGetStringField(TEXT("chooser_path"), ChooserPath);
	Args->TryGetNumberField(TEXT("column_index"), ColumnIndex);
	HandleDuplicateChooserColumn(ChooserPath, ColumnIndex, OutJson, OutError);
}

void HandleSetChooserContextDataFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString ChooserPath, ContextClass;
	Args->TryGetStringField(TEXT("chooser_path"), ChooserPath);
	Args->TryGetStringField(TEXT("context_class"), ContextClass);
	HandleSetChooserContextData(ChooserPath, ContextClass, OutJson, OutError);
}

}
