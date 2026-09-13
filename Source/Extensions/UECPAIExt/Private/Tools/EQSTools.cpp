// Copyright 2026, BlueprintsLab, All rights reserved.

#include "Tools/EQSTools.h"
#include "Managers/CapabilityProfile.h"
#include "Misc/EngineVersionComparison.h"
#include "Managers/SettingsManager.h"

#include "EnvironmentQuery/EnvQuery.h"
#include "EnvironmentQuery/EnvQueryOption.h"
#include "EnvironmentQuery/Generators/EnvQueryGenerator_SimpleGrid.h"
#include "EnvironmentQuery/Generators/EnvQueryGenerator_ActorsOfClass.h"
#include "EnvironmentQuery/Tests/EnvQueryTest_Distance.h"
#include "EnvironmentQuery/Tests/EnvQueryTest_Trace.h"
#include "EnvironmentQuery/Tests/EnvQueryTest_Dot.h"
#include "EnvironmentQuery/Contexts/EnvQueryContext_Querier.h"
#include "EnvironmentQuery/EnvQueryGenerator.h"

#include "EditorAssetLibrary.h"
#include "Serialization/JsonSerializer.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/Package.h"
#include "Misc/PackageName.h"
#include "UObject/UnrealType.h"

namespace EQSTools
{

static FArrayProperty* GetOptionsArrayProp()
{
	static FArrayProperty* Cached = nullptr;
	if (!Cached)
		Cached = FindFProperty<FArrayProperty>(UEnvQuery::StaticClass(), TEXT("Options"));
	return Cached;
}

static int32 QueryOptionsNum(UEnvQuery* Query)
{
	FArrayProperty* Prop = GetOptionsArrayProp();
	if (!Prop) return 0;
	FScriptArrayHelper Helper(Prop, Prop->ContainerPtrToValuePtr<void>(Query));
	return Helper.Num();
}

static UEnvQueryOption* QueryOptionsGet(UEnvQuery* Query, int32 Index)
{
	FArrayProperty* Prop = GetOptionsArrayProp();
	if (!Prop) return nullptr;
	FScriptArrayHelper Helper(Prop, Prop->ContainerPtrToValuePtr<void>(Query));
	if (!Helper.IsValidIndex(Index)) return nullptr;
	FObjectProperty* Inner = CastField<FObjectProperty>(Prop->Inner);
	if (!Inner) return nullptr;
	return Cast<UEnvQueryOption>(Inner->GetObjectPropertyValue(Helper.GetRawPtr(Index)));
}

static void QueryOptionsAdd(UEnvQuery* Query, UEnvQueryOption* Option)
{
	FArrayProperty* Prop = GetOptionsArrayProp();
	if (!Prop) return;
	FScriptArrayHelper Helper(Prop, Prop->ContainerPtrToValuePtr<void>(Query));
	int32 NewIdx = Helper.AddValue();
	FObjectProperty* Inner = CastField<FObjectProperty>(Prop->Inner);
	if (Inner) Inner->SetObjectPropertyValue(Helper.GetRawPtr(NewIdx), Option);
}

static FArrayProperty* GetTestsArrayProp()
{
	static FArrayProperty* Cached = nullptr;
	if (!Cached)
		Cached = FindFProperty<FArrayProperty>(UEnvQueryOption::StaticClass(), TEXT("Tests"));
	return Cached;
}

static void OptionTestsAdd(UEnvQueryOption* Option, UEnvQueryTest* Test)
{
	FArrayProperty* Prop = GetTestsArrayProp();
	if (!Prop) return;
	FScriptArrayHelper Helper(Prop, Prop->ContainerPtrToValuePtr<void>(Option));
	int32 NewIdx = Helper.AddValue();
	FObjectProperty* Inner = CastField<FObjectProperty>(Prop->Inner);
	if (Inner) Inner->SetObjectPropertyValue(Helper.GetRawPtr(NewIdx), Test);
}

static int32 OptionTestsNum(UEnvQueryOption* Option)
{
	FArrayProperty* Prop = GetTestsArrayProp();
	if (!Prop) return 0;
	FScriptArrayHelper Helper(Prop, Prop->ContainerPtrToValuePtr<void>(Option));
	return Helper.Num();
}

static UEnvQueryTest* OptionTestsGet(UEnvQueryOption* Option, int32 Index)
{
	FArrayProperty* Prop = GetTestsArrayProp();
	if (!Prop) return nullptr;
	FScriptArrayHelper Helper(Prop, Prop->ContainerPtrToValuePtr<void>(Option));
	if (!Helper.IsValidIndex(Index)) return nullptr;
	FObjectProperty* Inner = CastField<FObjectProperty>(Prop->Inner);
	if (!Inner) return nullptr;
	return Cast<UEnvQueryTest>(Inner->GetObjectPropertyValue(Helper.GetRawPtr(Index)));
}

static UEnvQuery* LoadOrCreateQuery(const FString& QueryPath, const FString& AssetName, const FString& SavePath, FString& OutError)
{
	UEnvQuery* Query = Cast<UEnvQuery>(UEditorAssetLibrary::LoadAsset(QueryPath));
	if (Query) return Query;

	if (AssetName.IsEmpty()) { OutError = TEXT("name is required"); return nullptr; }
	FString PackagePath = SavePath;
	while (PackagePath.EndsWith(TEXT("/"))) PackagePath = PackagePath.LeftChop(1);
	PackagePath += TEXT("/") + AssetName;

	if (FPackageName::DoesPackageExist(PackagePath))
	{
		OutError = FString::Printf(TEXT("Asset already exists at '%s'"), *PackagePath);
		return nullptr;
	}

	UPackage* Package = CreatePackage(*PackagePath);
	Query = NewObject<UEnvQuery>(Package, FName(*AssetName), RF_Public | RF_Standalone | RF_Transactional);
	if (!Query) { OutError = TEXT("Failed to create UEnvQuery"); return nullptr; }

	FAssetRegistryModule::AssetCreated(Query);
	Query->MarkPackageDirty();
	return Query;
}

void HandleCreateEQSQuery(const FString& AssetName, const FString& SavePath,
	FString& OutJsonString, FString& OutError)
{

	if (AssetName.IsEmpty()) { OutError = TEXT("name is required"); return; }
	FString PackagePath = SavePath;
	while (PackagePath.EndsWith(TEXT("/"))) PackagePath = PackagePath.LeftChop(1);
	PackagePath += TEXT("/") + AssetName;

	if (FPackageName::DoesPackageExist(PackagePath))
	{
		OutError = FString::Printf(TEXT("EQS Query already exists at '%s'"), *PackagePath);
		return;
	}

	UPackage* Package = CreatePackage(*PackagePath);
	UEnvQuery* Query = NewObject<UEnvQuery>(Package, FName(*AssetName), RF_Public | RF_Standalone | RF_Transactional);
	if (!Query) { OutError = TEXT("Failed to create UEnvQuery"); return; }

	FAssetRegistryModule::AssetCreated(Query);
	Query->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(Query->GetPathName(), false);

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"asset_path\":\"%s\",\"message\":\"EQS Query created. Use add_eqs_generator and add_eqs_test to configure it.\"}"),
		*Query->GetPathName());
}

void HandleAddEQSGenerator(const FString& QueryPath, const FString& GeneratorType,
	const FString& ContextClass, float Radius, float GridSpacing, int32 GridSize,
	FString& OutJsonString, FString& OutError)
{

	UEnvQuery* Query = Cast<UEnvQuery>(UEditorAssetLibrary::LoadAsset(QueryPath));
	if (!Query) { OutError = TEXT("EQS Query not found: ") + QueryPath; return; }

	UEnvQueryOption* Option = NewObject<UEnvQueryOption>(Query, UEnvQueryOption::StaticClass(), NAME_None, RF_Transactional);

	UEnvQueryGenerator* Generator = nullptr;
	if (GeneratorType.Equals(TEXT("SimpleGrid"), ESearchCase::IgnoreCase) || GeneratorType.Equals(TEXT("Grid"), ESearchCase::IgnoreCase))
	{
		UEnvQueryGenerator_SimpleGrid* GridGen = NewObject<UEnvQueryGenerator_SimpleGrid>(Option, NAME_None, RF_Transactional);
		if (GridSpacing > 0.f)
		{
			FFloatProperty* SpacingProp = FindFProperty<FFloatProperty>(GridGen->GetClass(), TEXT("Space"));
			if (SpacingProp) SpacingProp->SetPropertyValue_InContainer(GridGen, GridSpacing);
		}
		if (Radius > 0.f)
		{
			FFloatProperty* RadiusProp = FindFProperty<FFloatProperty>(GridGen->GetClass(), TEXT("GridRadius"));
			if (RadiusProp) RadiusProp->SetPropertyValue_InContainer(GridGen, Radius);
		}
		Generator = GridGen;
	}
	else if (GeneratorType.Equals(TEXT("ActorsOfClass"), ESearchCase::IgnoreCase) || GeneratorType.Equals(TEXT("Actors"), ESearchCase::IgnoreCase))
	{
		UEnvQueryGenerator_ActorsOfClass* ActorGen = NewObject<UEnvQueryGenerator_ActorsOfClass>(Option, NAME_None, RF_Transactional);
		if (Radius > 0.f)
		{
			FFloatProperty* RadiusProp = FindFProperty<FFloatProperty>(ActorGen->GetClass(), TEXT("SearchRadius"));
			if (RadiusProp) RadiusProp->SetPropertyValue_InContainer(ActorGen, Radius);
		}
		Generator = ActorGen;
	}
	else
	{
		OutError = FString::Printf(TEXT("Unknown generator type '%s'. Use: SimpleGrid, ActorsOfClass"), *GeneratorType);
		return;
	}

	Option->Generator = Generator;
	QueryOptionsAdd(Query, Option);
	Query->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(QueryPath, false);

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"generator_type\":\"%s\",\"message\":\"Generator added. Use add_eqs_test to add scoring tests.\"}"),
		*GeneratorType);
}

void HandleAddEQSTest(const FString& QueryPath, const FString& TestType,
	const FString& FilterMode, float FloatFilterMin, float FloatFilterMax,
	FString& OutJsonString, FString& OutError)
{

	UEnvQuery* Query = Cast<UEnvQuery>(UEditorAssetLibrary::LoadAsset(QueryPath));
	if (!Query) { OutError = TEXT("EQS Query not found: ") + QueryPath; return; }

	if (QueryOptionsNum(Query) == 0)
	{
		OutError = TEXT("No generator options found on query. Call add_eqs_generator first.");
		return;
	}

	UEnvQueryOption* Option = QueryOptionsGet(Query, QueryOptionsNum(Query) - 1);
	if (!Option) { OutError = TEXT("Last query option is null."); return; }

	FString MatchType = TestType;
	if (TestType.Equals(TEXT("LineOfSight"), ESearchCase::IgnoreCase)) MatchType = TEXT("Trace");
	else if (TestType.Equals(TEXT("Direction"), ESearchCase::IgnoreCase)) MatchType = TEXT("Dot");

	const FString PreferredName = FString::Printf(TEXT("EnvQueryTest_%s"), *MatchType);
	UClass* TestClass = nullptr;
	UClass* ContainsFallback = nullptr;
	for (TObjectIterator<UClass> It; It; ++It)
	{
		if (It->IsChildOf(UEnvQueryTest::StaticClass()) && !It->HasAnyClassFlags(CLASS_Abstract) && It->ClassGeneratedBy == nullptr)
		{
			if (It->GetName().Equals(PreferredName, ESearchCase::IgnoreCase))
			{
				TestClass = *It;
				break;
			}
			if (!ContainsFallback && It->GetName().Contains(MatchType, ESearchCase::IgnoreCase))
			{
				ContainsFallback = *It;
			}
		}
	}
	if (!TestClass) TestClass = ContainsFallback;
	if (!TestClass)
	{
		OutError = FString::Printf(TEXT("Unknown test type '%s'. Use list_eqs_test_types to see available types."), *TestType);
		return;
	}

	UEnvQueryTest* Test = Cast<UEnvQueryTest>(NewObject<UObject>(Option, TestClass, NAME_None, RF_Transactional));
	if (!Test)
	{
		OutError = FString::Printf(TEXT("Failed to instantiate test class '%s'."), *TestClass->GetName());
		return;
	}

	bool bFilterTypeRequested = !FilterMode.IsEmpty();
	bool bFilterTypeApplied = false;
	bool bFilterMinRequested = FloatFilterMin >= 0.f;
	bool bFilterMaxRequested = FloatFilterMax >= 0.f;
	bool bFilterMinApplied = false;
	bool bFilterMaxApplied = false;

	if (bFilterTypeRequested)
	{
		FString FilterEnumName = FilterMode;
		if (FilterMode.Equals(TEXT("Min"), ESearchCase::IgnoreCase)) FilterEnumName = TEXT("Minimum");
		else if (FilterMode.Equals(TEXT("Max"), ESearchCase::IgnoreCase)) FilterEnumName = TEXT("Maximum");

		FByteProperty* FilterProp = FindFProperty<FByteProperty>(Test->GetClass(), TEXT("FilterType"));
		if (FilterProp && FilterProp->Enum)
		{
			int64 EnumVal = FilterProp->Enum->GetValueByNameString(FilterEnumName);
			if (EnumVal == INDEX_NONE)
				EnumVal = FilterProp->Enum->GetValueByNameString(FilterProp->Enum->GetName() + TEXT("::") + FilterEnumName);
			if (EnumVal != INDEX_NONE)
			{
				FilterProp->SetPropertyValue_InContainer(Test, (uint8)EnumVal);
				bFilterTypeApplied = true;
			}
		}
	}

	auto WriteFloatValueStruct = [&](const TCHAR* MemberName, float Value) -> bool
	{
		FStructProperty* SP = FindFProperty<FStructProperty>(Test->GetClass(), MemberName);
		if (!SP) return false;
		FFloatProperty* DefaultValProp = FindFProperty<FFloatProperty>(SP->Struct, TEXT("DefaultValue"));
		if (!DefaultValProp) return false;
		void* StructPtr = SP->ContainerPtrToValuePtr<void>(Test);
		DefaultValProp->SetPropertyValue_InContainer(StructPtr, Value);
		return true;
	};

	if (bFilterMinRequested)
		bFilterMinApplied = WriteFloatValueStruct(TEXT("FloatValueMin"), FloatFilterMin);
	if (bFilterMaxRequested)
		bFilterMaxApplied = WriteFloatValueStruct(TEXT("FloatValueMax"), FloatFilterMax);

	OptionTestsAdd(Option, Test);
	Query->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(QueryPath, false);

	auto BoolStr = [](bool b) { return b ? TEXT("true") : TEXT("false"); };
	FString FilterReport;
	if (bFilterTypeRequested)
		FilterReport += FString::Printf(TEXT(",\"filter_mode\":\"%s\",\"filter_mode_applied\":%s"), *FilterMode, BoolStr(bFilterTypeApplied));
	if (bFilterMinRequested)
		FilterReport += FString::Printf(TEXT(",\"filter_min\":%g,\"filter_min_applied\":%s"), FloatFilterMin, BoolStr(bFilterMinApplied));
	if (bFilterMaxRequested)
		FilterReport += FString::Printf(TEXT(",\"filter_max\":%g,\"filter_max_applied\":%s"), FloatFilterMax, BoolStr(bFilterMaxApplied));

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"test_type\":\"%s\",\"test_class\":\"%s\"%s,\"message\":\"Test added to EQS query.\"}"),
		*TestType, *TestClass->GetName(), *FilterReport);
}

void HandleGetEQSQuerySummary(const FString& QueryPath,
	FString& OutJsonString, FString& OutError)
{

	UEnvQuery* Query = Cast<UEnvQuery>(UEditorAssetLibrary::LoadAsset(QueryPath));
	if (!Query) { OutError = TEXT("EQS Query not found: ") + QueryPath; return; }

	int32 OptionCount = QueryOptionsNum(Query);
	FString OptionsJson = TEXT("[");
	bool bFirstOpt = true;
	for (int32 OIdx = 0; OIdx < OptionCount; ++OIdx)
	{
		UEnvQueryOption* Option = QueryOptionsGet(Query, OIdx);
		if (!Option) continue;
		if (!bFirstOpt) OptionsJson += TEXT(",");
		FString GeneratorName = Option->Generator ? Option->Generator->GetClass()->GetName() : TEXT("None");
		FString TestsJson = TEXT("[");
		bool bFirstTest = true;
		int32 TestCount = OptionTestsNum(Option);
		for (int32 TIdx = 0; TIdx < TestCount; ++TIdx)
		{
			UEnvQueryTest* Test = OptionTestsGet(Option, TIdx);
			if (!Test) continue;
			if (!bFirstTest) TestsJson += TEXT(",");
			TestsJson += FString::Printf(TEXT("\"%s\""), *Test->GetClass()->GetName());
			bFirstTest = false;
		}
		TestsJson += TEXT("]");
		OptionsJson += FString::Printf(TEXT("{\"generator\":\"%s\",\"tests\":%s}"), *GeneratorName, *TestsJson);
		bFirstOpt = false;
	}
	OptionsJson += TEXT("]");

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"option_count\":%d,\"options\":%s}"),
		OptionCount, *OptionsJson);
}

void HandleSetEQSParam(const FString& QueryPath, const FString& Target,
	const FString& PropertyName, const FString& PropertyValue,
	FString& OutJsonString, FString& OutError)
{
	UEnvQuery* Query = Cast<UEnvQuery>(UEditorAssetLibrary::LoadAsset(QueryPath));
	if (!Query) { OutError = TEXT("Could not load EQS Query: ") + QueryPath; return; }

	if (QueryOptionsNum(Query) == 0) { OutError = TEXT("EQS Query has no options/generators"); return; }

	UEnvQueryOption* Option = QueryOptionsGet(Query, 0);
	if (!Option) { OutError = TEXT("EQS Option 0 is null"); return; }

	UObject* TargetObj = nullptr;
	if (Target.Equals(TEXT("generator"), ESearchCase::IgnoreCase))
	{
		TargetObj = Option->Generator;
		if (!TargetObj) { OutError = TEXT("Generator is null on option 0"); return; }
	}
	else if (Target.StartsWith(TEXT("test_"), ESearchCase::IgnoreCase) || Target.StartsWith(TEXT("test"), ESearchCase::IgnoreCase))
	{
		FString IdxStr = Target.RightChop(Target.Find(TEXT("_")) + 1);
		int32 TestIdx = FCString::Atoi(*IdxStr);
		if (OptionTestsNum(Option) <= TestIdx) { OutError = FString::Printf(TEXT("Test index %d out of range (have %d tests)"), TestIdx, OptionTestsNum(Option)); return; }
		TargetObj = OptionTestsGet(Option, TestIdx);
	}
	else
	{
		OutError = FString::Printf(TEXT("Unknown target '%s'. Use 'generator' or 'test_0', 'test_1', etc."), *Target);
		return;
	}

	UClass* ObjClass = TargetObj->GetClass();
	FProperty* Prop = FindFProperty<FProperty>(ObjClass, *PropertyName);
	if (!Prop)
		for (TFieldIterator<FProperty> It(ObjClass); It; ++It)
			if (It->GetName().Equals(PropertyName, ESearchCase::IgnoreCase)) { Prop = *It; break; }
	if (!Prop) { OutError = FString::Printf(TEXT("Property '%s' not found on %s"), *PropertyName, *ObjClass->GetName()); return; }

	void* Container = TargetObj;
	if (FFloatProperty* FP = CastField<FFloatProperty>(Prop))
		FP->SetPropertyValue_InContainer(Container, FCString::Atof(*PropertyValue));
	else if (FDoubleProperty* DP = CastField<FDoubleProperty>(Prop))
		DP->SetPropertyValue_InContainer(Container, FCString::Atod(*PropertyValue));
	else if (FIntProperty* IP = CastField<FIntProperty>(Prop))
		IP->SetPropertyValue_InContainer(Container, FCString::Atoi(*PropertyValue));
	else if (FBoolProperty* BP = CastField<FBoolProperty>(Prop))
		BP->SetPropertyValue_InContainer(Container, PropertyValue.Equals(TEXT("true"), ESearchCase::IgnoreCase) || PropertyValue == TEXT("1"));
	else if (FStructProperty* SP = CastField<FStructProperty>(Prop))
	{
		FFloatProperty* DefaultValProp = FindFProperty<FFloatProperty>(SP->Struct, TEXT("DefaultValue"));
		if (DefaultValProp)
		{
			void* StructPtr = SP->ContainerPtrToValuePtr<void>(Container);
			DefaultValProp->SetPropertyValue_InContainer(StructPtr, FCString::Atof(*PropertyValue));
		}
		else
		{
			OutError = FString::Printf(TEXT("Unsupported struct type for property '%s' on %s"), *PropertyName, *ObjClass->GetName());
			return;
		}
	}
	else { OutError = FString::Printf(TEXT("Unsupported property type for '%s'"), *PropertyName); return; }

	Query->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(QueryPath, false);

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"query_path\":\"%s\",\"target\":\"%s\",\"property\":\"%s\",\"value\":\"%s\"}"),
		*QueryPath, *Target, *PropertyName, *PropertyValue);
}

void HandleRemoveEQSTest(const FString& QueryPath, int32 TestIndex,
	FString& OutJsonString, FString& OutError)
{

	UEnvQuery* Query = Cast<UEnvQuery>(UEditorAssetLibrary::LoadAsset(QueryPath));
	if (!Query) { OutError = FString::Printf(TEXT("Could not load EQS query at '%s'"), *QueryPath); return; }

	FArrayProperty* OptionsProp = FindFProperty<FArrayProperty>(UEnvQuery::StaticClass(), TEXT("Options"));
	if (!OptionsProp) { OutError = TEXT("Could not access EQS Options array"); return; }

	FScriptArrayHelper OptionsHelper(OptionsProp, OptionsProp->ContainerPtrToValuePtr<void>(Query));
	if (OptionsHelper.Num() == 0) { OutError = TEXT("Query has no options/generators"); return; }

	UEnvQueryOption* Option = Cast<UEnvQueryOption>(((UObject**)OptionsHelper.GetRawPtr(0))[0]);
	if (!Option) { OutError = TEXT("No query option found"); return; }

	if (TestIndex < 0 || TestIndex >= Option->Tests.Num())
	{
		OutError = FString::Printf(TEXT("Test index %d out of range (0-%d)"), TestIndex, Option->Tests.Num() - 1);
		return;
	}

	Option->Tests.RemoveAt(TestIndex);
	Query->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(QueryPath, false);

	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"query_path\":\"%s\",\"removed_test_index\":%d,\"remaining_tests\":%d}"),
		*QueryPath, TestIndex, Option->Tests.Num());
}

void HandleListEQSGeneratorTypes(FString& OutJsonString, FString& OutError)
{

	TSharedPtr<FJsonObject> Res = MakeShareable(new FJsonObject());
	Res->SetBoolField(TEXT("success"), true);

	TArray<TSharedPtr<FJsonValue>> TypesArray;
	for (TObjectIterator<UClass> It; It; ++It)
	{
		if (It->IsChildOf(UEnvQueryGenerator::StaticClass()) && !It->HasAnyClassFlags(CLASS_Abstract) && It->ClassGeneratedBy == nullptr)
		{
			TypesArray.Add(MakeShareable(new FJsonValueString(It->GetName())));
		}
	}
	Res->SetArrayField(TEXT("generator_types"), TypesArray);
	Res->SetNumberField(TEXT("count"), TypesArray.Num());

	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Res.ToSharedRef(), W);
}

void HandleListEQSTestTypes(FString& OutJsonString, FString& OutError)
{

	TSharedPtr<FJsonObject> Res = MakeShareable(new FJsonObject());
	Res->SetBoolField(TEXT("success"), true);

	TArray<TSharedPtr<FJsonValue>> TypesArray;
	for (TObjectIterator<UClass> It; It; ++It)
	{
		if (It->IsChildOf(UEnvQueryTest::StaticClass()) && !It->HasAnyClassFlags(CLASS_Abstract) && It->ClassGeneratedBy == nullptr)
		{
			TypesArray.Add(MakeShareable(new FJsonValueString(It->GetName())));
		}
	}
	Res->SetArrayField(TEXT("test_types"), TypesArray);
	Res->SetNumberField(TEXT("count"), TypesArray.Num());

	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Res.ToSharedRef(), W);
}

void HandleRemoveEQSGenerator(const FString& QueryPath, int32 GeneratorIndex,
	FString& OutJsonString, FString& OutError)
{

	UEnvQuery* Query = Cast<UEnvQuery>(UEditorAssetLibrary::LoadAsset(QueryPath));
	if (!Query) { OutError = FString::Printf(TEXT("Could not load EQS query at '%s'"), *QueryPath); return; }

	FArrayProperty* OptionsProp = FindFProperty<FArrayProperty>(UEnvQuery::StaticClass(), TEXT("Options"));
	if (!OptionsProp) { OutError = TEXT("Could not access EQS Options array"); return; }

	FScriptArrayHelper OptionsHelper(OptionsProp, OptionsProp->ContainerPtrToValuePtr<void>(Query));
	if (GeneratorIndex < 0 || GeneratorIndex >= OptionsHelper.Num())
	{
		OutError = FString::Printf(TEXT("Generator index %d out of range (0-%d)"), GeneratorIndex, OptionsHelper.Num() - 1);
		return;
	}

	OptionsHelper.RemoveValues(GeneratorIndex, 1);
	Query->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(QueryPath, false);

	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"query_path\":\"%s\",\"removed_generator_index\":%d,\"remaining_generators\":%d}"),
		*QueryPath, GeneratorIndex, OptionsHelper.Num());
}

void HandleAddEQSOption(const FString& QueryPath, const FString& GeneratorType,
	FString& OutJsonString, FString& OutError)
{

	UEnvQuery* Query = Cast<UEnvQuery>(UEditorAssetLibrary::LoadAsset(QueryPath));
	if (!Query) { OutError = FString::Printf(TEXT("Could not load EQS query at '%s'"), *QueryPath); return; }

	UClass* GenClass = nullptr;
	for (TObjectIterator<UClass> It; It; ++It)
	{
		if (It->IsChildOf(UEnvQueryGenerator::StaticClass()) && !It->HasAnyClassFlags(CLASS_Abstract))
		{
			if (It->GetName().Contains(GeneratorType, ESearchCase::IgnoreCase))
			{
				GenClass = *It;
				break;
			}
		}
	}
	if (!GenClass)
	{
		OutError = FString::Printf(TEXT("Generator type '%s' not found. Use list_eqs_generator_types to see available types."), *GeneratorType);
		return;
	}

	UEnvQueryOption* NewOption = NewObject<UEnvQueryOption>(Query);
	UEnvQueryGenerator* Generator = NewObject<UEnvQueryGenerator>(NewOption, GenClass);
	NewOption->Generator = Generator;

	FArrayProperty* OptionsProp = GetOptionsArrayProp();
	if (!OptionsProp) { OutError = TEXT("Could not access EQS Options array"); return; }

	FScriptArrayHelper OptionsHelper(OptionsProp, OptionsProp->ContainerPtrToValuePtr<void>(Query));
	int32 NewIdx = OptionsHelper.AddValue();
	FObjectProperty* ElemProp = CastField<FObjectProperty>(OptionsProp->Inner);
	if (ElemProp)
	{
		ElemProp->SetObjectPropertyValue(OptionsHelper.GetRawPtr(NewIdx), NewOption);
	}

	Query->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(QueryPath, false);

	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"query_path\":\"%s\",\"generator\":\"%s\",\"option_index\":%d}"),
		*QueryPath, *GenClass->GetName(), NewIdx);
}

void HandleSetEQSTestScore(const FString& QueryPath, int32 OptionIndex, int32 TestIndex,
	const FString& ScoringEquation, float ScoringFactor, const FString& FilterType,
	FString& OutJsonString, FString& OutError)
{

	UEnvQuery* Query = Cast<UEnvQuery>(UEditorAssetLibrary::LoadAsset(QueryPath));
	if (!Query) { OutError = FString::Printf(TEXT("Could not load EQS query at '%s'"), *QueryPath); return; }

	FArrayProperty* OptionsProp = GetOptionsArrayProp();
	if (!OptionsProp) { OutError = TEXT("Could not access EQS Options array"); return; }

	FScriptArrayHelper OptionsHelper(OptionsProp, OptionsProp->ContainerPtrToValuePtr<void>(Query));
	if (OptionIndex < 0 || OptionIndex >= OptionsHelper.Num())
	{
		OutError = FString::Printf(TEXT("Option index %d out of range (0-%d)"), OptionIndex, OptionsHelper.Num() - 1);
		return;
	}

	FObjectProperty* ElemProp = CastField<FObjectProperty>(OptionsProp->Inner);
	UEnvQueryOption* Option = Cast<UEnvQueryOption>(ElemProp->GetObjectPropertyValue(OptionsHelper.GetRawPtr(OptionIndex)));
	if (!Option) { OutError = TEXT("Invalid option at index"); return; }

	if (TestIndex < 0 || TestIndex >= Option->Tests.Num())
	{
		OutError = FString::Printf(TEXT("Test index %d out of range (0-%d)"), TestIndex, Option->Tests.Num() - 1);
		return;
	}

	UEnvQueryTest* Test = Option->Tests[TestIndex];
	if (!Test) { OutError = TEXT("Null test at index"); return; }

	if (!ScoringEquation.IsEmpty())
	{
		FProperty* EqProp = FindFProperty<FProperty>(Test->GetClass(), TEXT("ScoringEquation"));
		if (EqProp)
		{
			void* ValPtr = EqProp->ContainerPtrToValuePtr<void>(Test);
			EqProp->ImportText_Direct(*ScoringEquation, ValPtr, Test, PPF_None);
		}
	}

	if (ScoringFactor != 0.f)
	{
		FProperty* FactorProp = FindFProperty<FProperty>(Test->GetClass(), TEXT("ScoringFactor"));
		if (FactorProp)
		{
			void* ValPtr = FactorProp->ContainerPtrToValuePtr<void>(Test);
			FString FactorStr = FString::Printf(TEXT("%g"), ScoringFactor);
			FactorProp->ImportText_Direct(*FactorStr, ValPtr, Test, PPF_None);
		}
	}

	if (!FilterType.IsEmpty())
	{
		FProperty* FilterProp = FindFProperty<FProperty>(Test->GetClass(), TEXT("FilterType"));
		if (FilterProp)
		{
			void* ValPtr = FilterProp->ContainerPtrToValuePtr<void>(Test);
			FilterProp->ImportText_Direct(*FilterType, ValPtr, Test, PPF_None);
		}
	}

	Query->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(QueryPath, false);

	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"query_path\":\"%s\",\"option\":%d,\"test\":%d}"),
		*QueryPath, OptionIndex, TestIndex);
}

void HandleCreateEQSQueryFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!FCapabilityProfile::Get().IsEnabled(ECapability::AssetProvisioning)) { OutError = TEXT("EQS module not initialised"); return; }
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString AssetName = TEXT("EQS_NewQuery"), SavePath;
	if (!Args->TryGetStringField(TEXT("asset_name"), AssetName) || AssetName.IsEmpty())
		Args->TryGetStringField(TEXT("name"), AssetName);
	Args->TryGetStringField(TEXT("save_path"), SavePath);
	if (SavePath.IsEmpty()) SavePath = FSettingsManager::GetDefaultSavePath();
	HandleCreateEQSQuery(AssetName, SavePath, OutJsonString, OutError);
}

void HandleAddEQSGeneratorFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString QueryPath, GeneratorType, ContextClass;
	double Radius = 1000.0, GridSpacing = 100.0;
	int32 GridSize = 5;
	Args->TryGetStringField(TEXT("query_path"), QueryPath);
	Args->TryGetStringField(TEXT("generator_type"), GeneratorType);
	Args->TryGetStringField(TEXT("context_class"), ContextClass);
	Args->TryGetNumberField(TEXT("radius"), Radius);
	Args->TryGetNumberField(TEXT("grid_spacing"), GridSpacing);
	HandleAddEQSGenerator(QueryPath, GeneratorType, ContextClass, (float)Radius, (float)GridSpacing, GridSize, OutJsonString, OutError);
}

void HandleAddEQSTestFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString QueryPath, TestType, FilterMode;
	double FilterMin = -1.0, FilterMax = -1.0;
	Args->TryGetStringField(TEXT("query_path"), QueryPath);
	Args->TryGetStringField(TEXT("test_type"), TestType);
	Args->TryGetStringField(TEXT("filter_mode"), FilterMode);
	Args->TryGetNumberField(TEXT("filter_min"), FilterMin);
	Args->TryGetNumberField(TEXT("filter_max"), FilterMax);
	HandleAddEQSTest(QueryPath, TestType, FilterMode, (float)FilterMin, (float)FilterMax, OutJsonString, OutError);
}

void HandleGetEQSQuerySummaryFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString QueryPath;
	Args->TryGetStringField(TEXT("query_path"), QueryPath);
	HandleGetEQSQuerySummary(QueryPath, OutJsonString, OutError);
}

void HandleSetEQSParamFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString QueryPath, Target, PropertyName, PropertyValue;
	Args->TryGetStringField(TEXT("query_path"), QueryPath);
	Args->TryGetStringField(TEXT("target"), Target);
	Args->TryGetStringField(TEXT("property_name"), PropertyName);
	if (!Args->TryGetStringField(TEXT("property_value"), PropertyValue))
	{
		double NumVal = 0.0;
		bool BoolVal = false;
		if (Args->TryGetNumberField(TEXT("property_value"), NumVal))
			PropertyValue = FString::Printf(TEXT("%g"), NumVal);
		else if (Args->TryGetBoolField(TEXT("property_value"), BoolVal))
			PropertyValue = BoolVal ? TEXT("true") : TEXT("false");
	}
	HandleSetEQSParam(QueryPath, Target, PropertyName, PropertyValue, OutJsonString, OutError);
}

void HandleRemoveEQSTestFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString QueryPath;
	int32 TestIndex = 0;
	Args->TryGetStringField(TEXT("query_path"), QueryPath);
	Args->TryGetNumberField(TEXT("test_index"), TestIndex);
	HandleRemoveEQSTest(QueryPath, TestIndex, OutJsonString, OutError);
}

void HandleListEQSGeneratorTypesFromArgs(const TSharedPtr<FJsonObject>& ,
	FString& OutJsonString, FString& OutError)
{
	HandleListEQSGeneratorTypes(OutJsonString, OutError);
}

void HandleListEQSTestTypesFromArgs(const TSharedPtr<FJsonObject>& ,
	FString& OutJsonString, FString& OutError)
{
	HandleListEQSTestTypes(OutJsonString, OutError);
}

void HandleRemoveEQSGeneratorFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString QueryPath;
	int32 GeneratorIndex = 0;
	Args->TryGetStringField(TEXT("query_path"), QueryPath);
	Args->TryGetNumberField(TEXT("generator_index"), GeneratorIndex);
	if (QueryPath.IsEmpty()) Args->TryGetStringField(TEXT("asset_path"), QueryPath);
	HandleRemoveEQSGenerator(QueryPath, GeneratorIndex, OutJsonString, OutError);
}

void HandleAddEQSOptionFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString QueryPath, GeneratorType;
	Args->TryGetStringField(TEXT("query_path"), QueryPath);
	Args->TryGetStringField(TEXT("generator_type"), GeneratorType);
	if (QueryPath.IsEmpty()) Args->TryGetStringField(TEXT("asset_path"), QueryPath);
	HandleAddEQSOption(QueryPath, GeneratorType, OutJsonString, OutError);
}

void HandleSetEQSTestScoreFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString QueryPath, ScoringEquation, FilterType;
	double OptionIndex = 0, TestIndex = 0, ScoringFactor = 1;
	Args->TryGetStringField(TEXT("query_path"), QueryPath);
	if (QueryPath.IsEmpty()) Args->TryGetStringField(TEXT("asset_path"), QueryPath);
	Args->TryGetNumberField(TEXT("option_index"), OptionIndex);
	Args->TryGetNumberField(TEXT("test_index"), TestIndex);
	Args->TryGetStringField(TEXT("scoring_equation"), ScoringEquation);
	Args->TryGetNumberField(TEXT("scoring_factor"), ScoringFactor);
	Args->TryGetStringField(TEXT("filter_type"), FilterType);
	HandleSetEQSTestScore(QueryPath, (int32)OptionIndex, (int32)TestIndex,
		ScoringEquation, (float)ScoringFactor, FilterType, OutJsonString, OutError);
}

}
