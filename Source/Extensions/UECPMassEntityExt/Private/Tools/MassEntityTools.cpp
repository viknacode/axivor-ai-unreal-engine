// Copyright 2026, BlueprintsLab, All rights reserved.

#include "Tools/MassEntityTools.h"
#include "Tools/BatchToolHelper.h"
#include "Misc/EngineVersionComparison.h"
#include "MCPToolsLog.h"

#include "AssetToolsModule.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "UObject/UObjectIterator.h"
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"

#include "MassEntityConfigAsset.h"
#include "MassEntityTraitBase.h"
#include "MassSpawner.h"
#include "MassSpawnerTypes.h"
#include "MassAssortedFragmentsTrait.h"

#include "MassMovableVisualizationTrait.h"
#include "MassStationaryVisualizationTrait.h"
#include "MassVisualizationTrait.h"

#include "Movement/MassMovementTrait.h"

#include "MassLODTrait.h"

#include "MassAgentComponent.h"

#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"

namespace
{

static FString JsonObjToString(const TSharedPtr<FJsonObject>& Obj)
{
	FString S;
	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&S);
	FJsonSerializer::Serialize(Obj.ToSharedRef(), W);
	return S;
}

static UClass* FindMassTraitClass(const FString& TraitClassName)
{
	for (TObjectIterator<UClass> It; It; ++It)
	{
		UClass* C = *It;
		if (!C->IsChildOf(UMassEntityTraitBase::StaticClass())) continue;
		if (C->HasAnyClassFlags(CLASS_Abstract)) continue;
		if (C->GetName() == TraitClassName || C->GetName() == (TraitClassName + TEXT("_C")))
			return C;
	}
	FString Lower = TraitClassName.ToLower();
	FString WithoutU = Lower.StartsWith(TEXT("u")) ? Lower.RightChop(1) : Lower;
	for (TObjectIterator<UClass> It; It; ++It)
	{
		UClass* C = *It;
		if (!C->IsChildOf(UMassEntityTraitBase::StaticClass())) continue;
		if (C->HasAnyClassFlags(CLASS_Abstract)) continue;
		FString CN = C->GetName().ToLower();
		FString CNWithoutU = CN.StartsWith(TEXT("u")) ? CN.RightChop(1) : CN;
		if (CN == Lower || CNWithoutU == WithoutU || CNWithoutU == Lower)
			return C;
		FString DisplayName = C->GetMetaData(TEXT("DisplayName")).ToLower();
		if (!DisplayName.IsEmpty() && DisplayName == Lower)
			return C;
	}
	return nullptr;
}

static void SerializeObjectPropertiesFlat(UObject* Obj, TSharedPtr<FJsonObject>& Out, const FString& Prefix = TEXT(""))
{
	if (!Obj) return;
	UClass* Class = Obj->GetClass();
	for (TFieldIterator<FProperty> It(Class); It; ++It)
	{
		FProperty* Prop = *It;
		if (!Prop->HasAnyPropertyFlags(CPF_Edit)) continue;

		FString FullName = Prefix.IsEmpty() ? Prop->GetName() : (Prefix + TEXT(".") + Prop->GetName());
		const void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Obj);

		if (FFloatProperty* FP = CastField<FFloatProperty>(Prop))
			Out->SetNumberField(FullName, FP->GetPropertyValue(ValuePtr));
		else if (FDoubleProperty* DP = CastField<FDoubleProperty>(Prop))
			Out->SetNumberField(FullName, DP->GetPropertyValue(ValuePtr));
		else if (FIntProperty* IP = CastField<FIntProperty>(Prop))
			Out->SetNumberField(FullName, IP->GetPropertyValue(ValuePtr));
		else if (FBoolProperty* BP = CastField<FBoolProperty>(Prop))
			Out->SetBoolField(FullName, BP->GetPropertyValue(ValuePtr));
		else if (FStrProperty* SP = CastField<FStrProperty>(Prop))
			Out->SetStringField(FullName, SP->GetPropertyValue(ValuePtr));
		else if (FNameProperty* NP = CastField<FNameProperty>(Prop))
			Out->SetStringField(FullName, NP->GetPropertyValue(ValuePtr).ToString());
		else if (FObjectPropertyBase* OP = CastField<FObjectPropertyBase>(Prop))
		{
			UObject* RefObj = OP->GetObjectPropertyValue(ValuePtr);
			Out->SetStringField(FullName, RefObj ? RefObj->GetPathName() : TEXT("None"));
		}
		else if (FClassProperty* CP = CastField<FClassProperty>(Prop))
		{
			UClass* RefClass = Cast<UClass>(CP->GetObjectPropertyValue(ValuePtr));
			Out->SetStringField(FullName, RefClass ? RefClass->GetPathName() : TEXT("None"));
		}
		else if (FStructProperty* StP = CastField<FStructProperty>(Prop))
		{
			if (Prefix.IsEmpty())
			{
				const void* StructPtr = StP->ContainerPtrToValuePtr<void>(Obj);
				for (TFieldIterator<FProperty> SIt(StP->Struct); SIt; ++SIt)
				{
					FProperty* SP2 = *SIt;
					if (!SP2->HasAnyPropertyFlags(CPF_Edit)) continue;
					FString SubName = FullName + TEXT(".") + SP2->GetName();
					const void* SubPtr = SP2->ContainerPtrToValuePtr<void>(StructPtr);
					if (FFloatProperty* FP2 = CastField<FFloatProperty>(SP2))
						Out->SetNumberField(SubName, FP2->GetPropertyValue(SubPtr));
					else if (FDoubleProperty* DP2 = CastField<FDoubleProperty>(SP2))
						Out->SetNumberField(SubName, DP2->GetPropertyValue(SubPtr));
					else if (FIntProperty* IP2 = CastField<FIntProperty>(SP2))
						Out->SetNumberField(SubName, IP2->GetPropertyValue(SubPtr));
					else if (FBoolProperty* BP2 = CastField<FBoolProperty>(SP2))
						Out->SetBoolField(SubName, BP2->GetPropertyValue(SubPtr));
				}
			}
		}
	}
}

static bool SetNestedProperty(UObject* Obj, const FString& PropPath, const FString& ValueStr, FString& OutError)
{
	if (!Obj) { OutError = TEXT("Null object"); return false; }

	TArray<FString> Parts;
	PropPath.ParseIntoArray(Parts, TEXT("."), true);
	if (Parts.Num() == 0) { OutError = TEXT("Empty property path"); return false; }

	void* Container = Obj;
	UStruct* ContainerStruct = Obj->GetClass();

	for (int32 i = 0; i < Parts.Num() - 1; i++)
	{
		FStructProperty* StP = FindFProperty<FStructProperty>(ContainerStruct, *Parts[i]);
		if (!StP) { OutError = FString::Printf(TEXT("Struct property '%s' not found on %s"), *Parts[i], *ContainerStruct->GetName()); return false; }
		Container = StP->ContainerPtrToValuePtr<void>(Container);
		ContainerStruct = StP->Struct;
	}

	FString LeafName = Parts.Last();
	FProperty* Leaf = FindFProperty<FProperty>(ContainerStruct, *LeafName);
	if (!Leaf) { OutError = FString::Printf(TEXT("Property '%s' not found"), *LeafName); return false; }

	void* LeafPtr = Leaf->ContainerPtrToValuePtr<void>(Container);

	if (FFloatProperty* FP = CastField<FFloatProperty>(Leaf))
	{
		FP->SetPropertyValue(LeafPtr, FCString::Atof(*ValueStr));
		return true;
	}
	if (FDoubleProperty* DP = CastField<FDoubleProperty>(Leaf))
	{
		DP->SetPropertyValue(LeafPtr, FCString::Atod(*ValueStr));
		return true;
	}
	if (FIntProperty* IP = CastField<FIntProperty>(Leaf))
	{
		IP->SetPropertyValue(LeafPtr, FCString::Atoi(*ValueStr));
		return true;
	}
	if (FBoolProperty* BP = CastField<FBoolProperty>(Leaf))
	{
		bool bVal = ValueStr.ToLower() == TEXT("true") || ValueStr == TEXT("1");
		BP->SetPropertyValue(LeafPtr, bVal);
		return true;
	}
	if (FStrProperty* SP = CastField<FStrProperty>(Leaf))
	{
		SP->SetPropertyValue(LeafPtr, ValueStr);
		return true;
	}
	if (FNameProperty* NP = CastField<FNameProperty>(Leaf))
	{
		NP->SetPropertyValue(LeafPtr, FName(*ValueStr));
		return true;
	}
	if (FObjectPropertyBase* OP = CastField<FObjectPropertyBase>(Leaf))
	{
		const bool bEmpty = (ValueStr == TEXT("None") || ValueStr.IsEmpty());
		if (OP->IsA<FClassProperty>() || OP->IsA<FSoftClassProperty>())
		{
			UClass* RefClass = nullptr;
			if (!bEmpty)
			{
				RefClass = LoadObject<UClass>(nullptr, *ValueStr);
				if (!RefClass) RefClass = LoadObject<UClass>(nullptr, *(ValueStr + TEXT("_C")));
				if (!RefClass)
					if (UBlueprint* BP = LoadObject<UBlueprint>(nullptr, *ValueStr))
						RefClass = BP->GeneratedClass;
			}
			OP->SetObjectPropertyValue(LeafPtr, RefClass);
		}
		else
		{
			UObject* RefObj = bEmpty ? nullptr : LoadObject<UObject>(nullptr, *ValueStr);
			OP->SetObjectPropertyValue(LeafPtr, RefObj);
		}
		return true;
	}
	if (FEnumProperty* EP = CastField<FEnumProperty>(Leaf))
	{
		UEnum* Enum = EP->GetEnum();
		int64 Val = Enum ? Enum->GetValueByNameString(ValueStr) : FCString::Atoi64(*ValueStr);
		if (Val == INDEX_NONE) Val = FCString::Atoi64(*ValueStr);
		EP->GetUnderlyingProperty()->SetIntPropertyValue(LeafPtr, Val);
		return true;
	}
	if (FByteProperty* ByP = CastField<FByteProperty>(Leaf))
	{
		if (ByP->Enum)
		{
			int64 Val = ByP->Enum->GetValueByNameString(ValueStr);
			if (Val == INDEX_NONE) Val = FCString::Atoi64(*ValueStr);
			ByP->SetPropertyValue(LeafPtr, (uint8)Val);
		}
		else
		{
			ByP->SetPropertyValue(LeafPtr, (uint8)FCString::Atoi(*ValueStr));
		}
		return true;
	}

	OutError = FString::Printf(TEXT("Unsupported property type '%s' for '%s'"), *Leaf->GetClass()->GetName(), *LeafName);
	return false;
}

static void CreateMassEntityConfigSingle(const FString& Name, const FString& SavePath,
	const FString& ParentConfigPath, FString& OutJsonString, FString& OutError)
{
	if (Name.IsEmpty()) { OutError = TEXT("name is required"); return; }
	if (SavePath.IsEmpty()) { OutError = TEXT("save_path is required"); return; }

	IAssetTools& AT = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
	UObject* NewAssetObj = AT.CreateAsset(Name, SavePath, UMassEntityConfigAsset::StaticClass(), nullptr);
	UMassEntityConfigAsset* Asset = Cast<UMassEntityConfigAsset>(NewAssetObj);
	if (!Asset) { OutError = FString::Printf(TEXT("Failed to create MassEntityConfigAsset '%s' at '%s'"), *Name, *SavePath); return; }

	if (!ParentConfigPath.IsEmpty())
	{
		UMassEntityConfigAsset* Parent = LoadObject<UMassEntityConfigAsset>(nullptr, *ParentConfigPath);
		if (Parent)
			Asset->GetMutableConfig().SetParentAsset(*Parent);
		else
			UE_LOG(LogMCPTool, Warning, TEXT("[MassEntity] Parent config not found: %s"), *ParentConfigPath);
	}

	Asset->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(Asset);

	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetBoolField(TEXT("success"), true);
	R->SetStringField(TEXT("name"), Name);
	R->SetStringField(TEXT("path"), FString::Printf(TEXT("%s/%s"), *SavePath, *Name));
	OutJsonString = JsonObjToString(R);
	UE_LOG(LogMCPTool, Log, TEXT("[MassEntity] Created entity config '%s' at '%s/%s'"), *Name, *SavePath, *Name);
}

static void AddMassTraitSingle(UMassEntityConfigAsset* Asset, const FString& TraitClassName,
	FString& OutJsonString, FString& OutError)
{
	UClass* TraitClass = FindMassTraitClass(TraitClassName);
	if (!TraitClass)
	{
		OutError = FString::Printf(TEXT("Trait class '%s' not found — call list_mass_trait_classes to see available"), *TraitClassName);
		return;
	}

	if (Asset->GetConfig().FindTrait(TraitClass))
	{
		UE_LOG(LogMCPTool, Log, TEXT("[MassEntity] Trait '%s' already present, skipping add"), *TraitClassName);
		TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
		R->SetBoolField(TEXT("success"), true);
		R->SetStringField(TEXT("trait_class"), TraitClass->GetName());
		R->SetStringField(TEXT("note"), TEXT("already present"));
		OutJsonString = JsonObjToString(R);
		return;
	}

#if !UE_VERSION_OLDER_THAN(5, 5, 0)
	UMassEntityTraitBase* Trait = Asset->AddTrait(TraitClass);
#else
	UMassEntityTraitBase* Trait = NewObject<UMassEntityTraitBase>(Asset, TraitClass);
	if (Trait)
	{
		Asset->GetMutableConfig().AddTrait(*Trait);
	}
#endif
	if (!Trait)
	{
		OutError = FString::Printf(TEXT("AddTrait returned null for class '%s'"), *TraitClass->GetName());
		return;
	}

	Asset->MarkPackageDirty();

	TSharedPtr<FJsonObject> Props = MakeShared<FJsonObject>();
	SerializeObjectPropertiesFlat(Trait, Props);

	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetBoolField(TEXT("success"), true);
	R->SetStringField(TEXT("trait_class"), TraitClass->GetName());
	R->SetStringField(TEXT("display_name"), TraitClass->GetMetaData(TEXT("DisplayName")));
	R->SetObjectField(TEXT("properties"), Props);
	OutJsonString = JsonObjToString(R);
	UE_LOG(LogMCPTool, Log, TEXT("[MassEntity] Added trait '%s' to config"), *TraitClass->GetName());
}

static void SetMassTraitPropertySingle(UMassEntityConfigAsset* Asset, const FString& TraitClassName,
	const FString& PropertyName, const FString& ValueStr, FString& OutJsonString, FString& OutError)
{
	UClass* TraitClass = FindMassTraitClass(TraitClassName);
	if (!TraitClass)
	{
		OutError = FString::Printf(TEXT("Trait class '%s' not found"), *TraitClassName);
		return;
	}

	const UMassEntityTraitBase* ConstTrait = Asset->GetConfig().FindTrait(TraitClass);
	if (!ConstTrait)
	{
		OutError = FString::Printf(TEXT("Trait '%s' not found on config — call add_mass_trait first"), *TraitClassName);
		return;
	}

	UMassEntityTraitBase* Trait = const_cast<UMassEntityTraitBase*>(ConstTrait);
	FString SetError;
	if (!SetNestedProperty(Trait, PropertyName, ValueStr, SetError))
	{
		OutError = FString::Printf(TEXT("Failed to set '%s' on trait '%s': %s"), *PropertyName, *TraitClassName, *SetError);
		return;
	}

	Asset->MarkPackageDirty();

	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetBoolField(TEXT("success"), true);
	R->SetStringField(TEXT("trait_class"), TraitClass->GetName());
	R->SetStringField(TEXT("property"), PropertyName);
	R->SetStringField(TEXT("value"), ValueStr);
	OutJsonString = JsonObjToString(R);
	UE_LOG(LogMCPTool, Log, TEXT("[MassEntity] Set '%s.%s' = '%s'"), *TraitClass->GetName(), *PropertyName, *ValueStr);
}

static void RemoveMassTraitSingle(UMassEntityConfigAsset* Asset, const FString& TraitClassName,
	FString& OutJsonString, FString& OutError)
{
	UClass* TraitClass = FindMassTraitClass(TraitClassName);
	if (!TraitClass) { OutError = FString::Printf(TEXT("Trait class not found: %s"), *TraitClassName); return; }

	TConstArrayView<UMassEntityTraitBase*> Traits = Asset->GetConfig().GetTraits();
	int32 RemoveIdx = INDEX_NONE;
	for (int32 i = 0; i < Traits.Num(); i++)
	{
		if (Traits[i] && Traits[i]->GetClass() == TraitClass)
		{
			RemoveIdx = i;
			break;
		}
	}
	if (RemoveIdx == INDEX_NONE)
	{
		OutError = FString::Printf(TEXT("Trait '%s' not found on config"), *TraitClassName);
		return;
	}

	FArrayProperty* TraitsProp = FindFProperty<FArrayProperty>(FMassEntityConfig::StaticStruct(), TEXT("Traits"));
	if (!TraitsProp) { OutError = TEXT("Cannot access Traits property via reflection"); return; }

	FScriptArrayHelper Helper(TraitsProp, TraitsProp->ContainerPtrToValuePtr<void>(&Asset->GetMutableConfig()));
	Helper.RemoveValues(RemoveIdx, 1);

	Asset->MarkPackageDirty();

	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetBoolField(TEXT("success"), true);
	R->SetStringField(TEXT("removed_trait"), TraitClassName);
	OutJsonString = JsonObjToString(R);
	UE_LOG(LogMCPTool, Log, TEXT("[MassEntity] Removed trait '%s' from '%s'"), *TraitClassName, *Asset->GetPathName());
}

static TSharedPtr<FJsonObject> SerializeMassSpawner(AMassSpawner* Spawner)
{
	TSharedPtr<FJsonObject> S = MakeShared<FJsonObject>();
	S->SetStringField(TEXT("actor_label"), Spawner->GetActorLabel());

	FVector Loc = Spawner->GetActorLocation();
	TSharedPtr<FJsonObject> LocObj = MakeShared<FJsonObject>();
	LocObj->SetNumberField(TEXT("x"), Loc.X); LocObj->SetNumberField(TEXT("y"), Loc.Y); LocObj->SetNumberField(TEXT("z"), Loc.Z);
	S->SetObjectField(TEXT("location"), LocObj);

	if (FIntProperty*   P = FindFProperty<FIntProperty>  (AMassSpawner::StaticClass(), TEXT("Count")))
		S->SetNumberField(TEXT("count"), P->GetPropertyValue_InContainer(Spawner));
	if (FBoolProperty*  P = FindFProperty<FBoolProperty> (AMassSpawner::StaticClass(), TEXT("bAutoSpawnOnBeginPlay")))
		S->SetBoolField(TEXT("auto_spawn"), P->GetPropertyValue_InContainer(Spawner));
	if (FFloatProperty* P = FindFProperty<FFloatProperty>(AMassSpawner::StaticClass(), TEXT("SpawningCountScale")))
		S->SetNumberField(TEXT("spawn_count_scale"), P->GetPropertyValue_InContainer(Spawner));

	FArrayProperty* ETProp = FindFProperty<FArrayProperty>(AMassSpawner::StaticClass(), TEXT("EntityTypes"));
	if (ETProp)
	{
		FStructProperty* InnerStruct = CastField<FStructProperty>(ETProp->Inner);
		FScriptArrayHelper Helper(ETProp, ETProp->ContainerPtrToValuePtr<void>(Spawner));
		TArray<TSharedPtr<FJsonValue>> TypesArr;
		for (int32 i = 0; i < Helper.Num(); i++)
		{
			void* ElemPtr = Helper.GetRawPtr(i);
			TSharedPtr<FJsonObject> TypeObj = MakeShared<FJsonObject>();
			if (InnerStruct)
			{
				if (FSoftObjectProperty* SOP = FindFProperty<FSoftObjectProperty>(InnerStruct->Struct, TEXT("EntityConfig")))
				{
					FString ConfigStr;
					SOP->ExportTextItem_InContainer(ConfigStr, ElemPtr, nullptr, nullptr, PPF_None);
					TypeObj->SetStringField(TEXT("config_path"), ConfigStr);
				}
				if (FFloatProperty* PropP = FindFProperty<FFloatProperty>(InnerStruct->Struct, TEXT("Proportion")))
					TypeObj->SetNumberField(TEXT("proportion"), PropP->GetPropertyValue_InContainer(ElemPtr));
			}
			TypesArr.Add(MakeShared<FJsonValueObject>(TypeObj));
		}
		S->SetArrayField(TEXT("entity_types"), TypesArr);
	}
	return S;
}

}

namespace MassEntityTools
{

void HandleCreateMassEntityConfigFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("configs"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString Name = BatchToolHelper::GetItemString(Item, TEXT("name"));
			FString SavePath = BatchToolHelper::GetItemString(Item, TEXT("save_path"));
			if (SavePath.IsEmpty()) SavePath = BatchToolHelper::GetItemString(Args, TEXT("save_path"));
			FString ParentPath = BatchToolHelper::GetItemString(Item, TEXT("parent_config_path"));
			FString ItemOut, ItemErr;
			CreateMassEntityConfigSingle(Name, SavePath, ParentPath, ItemOut, ItemErr);
			if (ItemErr.IsEmpty())
			{
				auto E = MakeShared<FJsonObject>(); E->SetStringField(TEXT("name"), Name);
				Batch.AddSuccess(i, E);
			}
			else Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJsonString);
		return;
	}

	FString Name, SavePath, ParentPath;
	Args->TryGetStringField(TEXT("name"), Name);
	Args->TryGetStringField(TEXT("save_path"), SavePath);
	Args->TryGetStringField(TEXT("parent_config_path"), ParentPath);
	CreateMassEntityConfigSingle(Name, SavePath, ParentPath, OutJsonString, OutError);
}

void HandleAddMassTraitFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString ConfigPath;
	Args->TryGetStringField(TEXT("config_path"), ConfigPath);
	if (ConfigPath.IsEmpty()) { OutError = TEXT("config_path is required"); return; }

	UMassEntityConfigAsset* Asset = LoadObject<UMassEntityConfigAsset>(nullptr, *ConfigPath);
	if (!Asset) { OutError = FString::Printf(TEXT("Config asset not found: %s"), *ConfigPath); return; }

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("traits"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			FString TraitClassName;
			if ((*ItemsArray)[i]->Type == EJson::String)
			{
				TraitClassName = (*ItemsArray)[i]->AsString();
			}
			else
			{
				TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
				if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
				TraitClassName = BatchToolHelper::GetItemString(Item, TEXT("trait_class"), TEXT("class"));
			}
			FString ItemOut, ItemErr;
			AddMassTraitSingle(Asset, TraitClassName, ItemOut, ItemErr);
			if (ItemErr.IsEmpty())
			{
				auto E = MakeShared<FJsonObject>(); E->SetStringField(TEXT("trait_class"), TraitClassName);
				Batch.AddSuccess(i, E);
			}
			else Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJsonString);
		return;
	}

	FString TraitClassName;
	Args->TryGetStringField(TEXT("trait_class"), TraitClassName);
	if (TraitClassName.IsEmpty()) Args->TryGetStringField(TEXT("class"), TraitClassName);
	if (TraitClassName.IsEmpty()) { OutError = TEXT("trait_class is required"); return; }
	AddMassTraitSingle(Asset, TraitClassName, OutJsonString, OutError);
}

void HandleSetMassTraitPropertyFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString ConfigPath;
	Args->TryGetStringField(TEXT("config_path"), ConfigPath);
	if (ConfigPath.IsEmpty()) { OutError = TEXT("config_path is required"); return; }

	UMassEntityConfigAsset* Asset = LoadObject<UMassEntityConfigAsset>(nullptr, *ConfigPath);
	if (!Asset) { OutError = FString::Printf(TEXT("Config asset not found: %s"), *ConfigPath); return; }

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("properties"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString TraitClass = BatchToolHelper::GetItemString(Item, TEXT("trait_class"), TEXT("class"));
			FString PropName = BatchToolHelper::GetItemString(Item, TEXT("property_name"), TEXT("property"));
			FString Value = BatchToolHelper::GetItemString(Item, TEXT("value"));
			if (Value.IsEmpty())
			{
				double NumVal = 0.0;
				if (Item->TryGetNumberField(TEXT("value"), NumVal))
					Value = FString::SanitizeFloat(NumVal);
			}
			FString ItemOut, ItemErr;
			SetMassTraitPropertySingle(Asset, TraitClass, PropName, Value, ItemOut, ItemErr);
			if (ItemErr.IsEmpty())
			{
				auto E = MakeShared<FJsonObject>(); E->SetStringField(TEXT("property"), PropName);
				Batch.AddSuccess(i, E);
			}
			else Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJsonString);
		return;
	}

	FString TraitClassName, PropName, ValueStr;
	Args->TryGetStringField(TEXT("trait_class"), TraitClassName);
	if (TraitClassName.IsEmpty()) Args->TryGetStringField(TEXT("class"), TraitClassName);
	Args->TryGetStringField(TEXT("property_name"), PropName);
	if (PropName.IsEmpty()) Args->TryGetStringField(TEXT("property"), PropName);
	Args->TryGetStringField(TEXT("value"), ValueStr);
	if (ValueStr.IsEmpty())
	{
		double NumVal = 0.0;
		if (Args->TryGetNumberField(TEXT("value"), NumVal))
			ValueStr = FString::SanitizeFloat(NumVal);
	}
	if (TraitClassName.IsEmpty()) { OutError = TEXT("trait_class is required"); return; }
	if (PropName.IsEmpty()) { OutError = TEXT("property_name is required"); return; }
	SetMassTraitPropertySingle(Asset, TraitClassName, PropName, ValueStr, OutJsonString, OutError);
}

void HandleGetMassEntityConfigSummaryFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString ConfigPath;
	Args->TryGetStringField(TEXT("config_path"), ConfigPath);
	if (ConfigPath.IsEmpty()) { OutError = TEXT("config_path is required"); return; }

	UMassEntityConfigAsset* Asset = LoadObject<UMassEntityConfigAsset>(nullptr, *ConfigPath);
	if (!Asset) { OutError = FString::Printf(TEXT("Config asset not found: %s"), *ConfigPath); return; }

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("config_path"), ConfigPath);

	const UMassEntityConfigAsset* Parent = Asset->GetConfig().GetParent();
	Root->SetStringField(TEXT("parent"), Parent ? Parent->GetPathName() : TEXT("None"));

	TArray<TSharedPtr<FJsonValue>> TraitsArr;
	TConstArrayView<UMassEntityTraitBase*> Traits = Asset->GetConfig().GetTraits();
	for (UMassEntityTraitBase* Trait : Traits)
	{
		if (!Trait) continue;
		TSharedPtr<FJsonObject> TraitObj = MakeShared<FJsonObject>();
		TraitObj->SetStringField(TEXT("class"), Trait->GetClass()->GetName());
		FString DisplayName = Trait->GetClass()->GetMetaData(TEXT("DisplayName"));
		TraitObj->SetStringField(TEXT("display_name"), DisplayName.IsEmpty() ? Trait->GetClass()->GetName() : DisplayName);

		TSharedPtr<FJsonObject> Props = MakeShared<FJsonObject>();
		SerializeObjectPropertiesFlat(Trait, Props);
		TraitObj->SetObjectField(TEXT("properties"), Props);
		TraitsArr.Add(MakeShared<FJsonValueObject>(TraitObj));
	}
	Root->SetArrayField(TEXT("traits"), TraitsArr);
	Root->SetNumberField(TEXT("trait_count"), Traits.Num());
	OutJsonString = JsonObjToString(Root);
}

void HandlePlaceMassSpawnerFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World) { OutError = TEXT("No editor world available"); return; }

	FString ActorLabel;
	Args->TryGetStringField(TEXT("actor_label"), ActorLabel);
	if (ActorLabel.IsEmpty()) ActorLabel = TEXT("MassSpawner");

	FVector Location = FVector::ZeroVector;
	const TSharedPtr<FJsonObject>* LocObj = nullptr;
	if (Args->TryGetObjectField(TEXT("location"), LocObj))
	{
		double X = 0, Y = 0, Z = 0;
		(*LocObj)->TryGetNumberField(TEXT("x"), X);
		(*LocObj)->TryGetNumberField(TEXT("y"), Y);
		(*LocObj)->TryGetNumberField(TEXT("z"), Z);
		Location = FVector(X, Y, Z);
	}

	int32 Count = 100;
	double CountD = 100;
	if (Args->TryGetNumberField(TEXT("count"), CountD)) Count = (int32)CountD;

	bool bAutoSpawn = true;
	Args->TryGetBoolField(TEXT("auto_spawn"), bAutoSpawn);

	double SpawnCountScale = 1.0;
	Args->TryGetNumberField(TEXT("spawn_count_scale"), SpawnCountScale);

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	FTransform SpawnTransform(FRotator::ZeroRotator, Location);
	AMassSpawner* Spawner = World->SpawnActor<AMassSpawner>(AMassSpawner::StaticClass(), SpawnTransform, SpawnParams);
	if (!Spawner) { OutError = TEXT("Failed to spawn AMassSpawner"); return; }
	Spawner->SetActorLabel(ActorLabel);

	if (FIntProperty* CP = FindFProperty<FIntProperty>(AMassSpawner::StaticClass(), TEXT("Count")))
		CP->SetPropertyValue_InContainer(Spawner, Count);

	if (FBoolProperty* BP = FindFProperty<FBoolProperty>(AMassSpawner::StaticClass(), TEXT("bAutoSpawnOnBeginPlay")))
		BP->SetPropertyValue_InContainer(Spawner, bAutoSpawn);

	if (FFloatProperty* ScP = FindFProperty<FFloatProperty>(AMassSpawner::StaticClass(), TEXT("SpawningCountScale")))
		ScP->SetPropertyValue_InContainer(Spawner, (float)SpawnCountScale);

	FArrayProperty* ETProp = FindFProperty<FArrayProperty>(AMassSpawner::StaticClass(), TEXT("EntityTypes"));
	if (ETProp)
	{
		FScriptArrayHelper ArrayHelper(ETProp, ETProp->ContainerPtrToValuePtr<void>(Spawner));

		auto AddEntityType = [&](const FString& ConfigPath, float Proportion)
		{
			UMassEntityConfigAsset* ConfigAsset = LoadObject<UMassEntityConfigAsset>(nullptr, *ConfigPath);
			if (!ConfigAsset)
			{
				UE_LOG(LogMCPTool, Warning, TEXT("[MassEntity] Entity config not found: %s"), *ConfigPath);
				return;
			}
			FStructProperty* InnerStruct = CastField<FStructProperty>(ETProp->Inner);
			if (!InnerStruct) return;

			int32 NewIdx = ArrayHelper.AddValue();
			void* NewEntry = ArrayHelper.GetRawPtr(NewIdx);

			if (FObjectProperty* ConfigProp = FindFProperty<FObjectProperty>(InnerStruct->Struct, TEXT("EntityConfig")))
				ConfigProp->SetObjectPropertyValue(ConfigProp->ContainerPtrToValuePtr<void>(NewEntry), ConfigAsset);
			if (FSoftObjectProperty* SOP = FindFProperty<FSoftObjectProperty>(InnerStruct->Struct, TEXT("EntityConfig")))
				SOP->SetPropertyValue(SOP->ContainerPtrToValuePtr<void>(NewEntry), FSoftObjectPtr(ConfigAsset));

			if (FFloatProperty* PropP = FindFProperty<FFloatProperty>(InnerStruct->Struct, TEXT("Proportion")))
				PropP->SetPropertyValue_InContainer(NewEntry, Proportion);
		};

		FString SingleConfigPath;
		Args->TryGetStringField(TEXT("entity_config_path"), SingleConfigPath);
		if (!SingleConfigPath.IsEmpty())
			AddEntityType(SingleConfigPath, 1.0f);

		const TArray<TSharedPtr<FJsonValue>>* EntityTypesArr = nullptr;
		if (Args->TryGetArrayField(TEXT("entity_types"), EntityTypesArr))
		{
			for (const TSharedPtr<FJsonValue>& ETV : *EntityTypesArr)
			{
				TSharedPtr<FJsonObject> ETObj = ETV->AsObject();
				if (!ETObj.IsValid()) continue;
				FString ETPath = BatchToolHelper::GetItemString(ETObj, TEXT("config_path"));
				double Prop = 1.0;
				ETObj->TryGetNumberField(TEXT("proportion"), Prop);
				if (!ETPath.IsEmpty()) AddEntityType(ETPath, (float)Prop);
			}
		}
	}

	GEditor->RedrawAllViewports();

	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetBoolField(TEXT("success"), true);
	R->SetStringField(TEXT("actor_label"), ActorLabel);
	R->SetNumberField(TEXT("count"), Count);
	R->SetBoolField(TEXT("auto_spawn"), bAutoSpawn);
	OutJsonString = JsonObjToString(R);
	UE_LOG(LogMCPTool, Log, TEXT("[MassEntity] Placed AMassSpawner '%s' count=%d"), *ActorLabel, Count);
}

void HandleConfigureMassSpawnerFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString ActorLabel;
	Args->TryGetStringField(TEXT("actor_label"), ActorLabel);
	if (ActorLabel.IsEmpty()) { OutError = TEXT("actor_label is required"); return; }

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World) { OutError = TEXT("No editor world available"); return; }

	AMassSpawner* Spawner = nullptr;
	for (TActorIterator<AMassSpawner> It(World); It; ++It)
	{
		if (It->GetActorLabel() == ActorLabel)
		{
			Spawner = *It;
			break;
		}
	}
	if (!Spawner) { OutError = FString::Printf(TEXT("AMassSpawner '%s' not found in level"), *ActorLabel); return; }

	double CountD = -1;
	if (Args->TryGetNumberField(TEXT("count"), CountD) && CountD >= 0)
		if (FIntProperty* CP = FindFProperty<FIntProperty>(AMassSpawner::StaticClass(), TEXT("Count")))
			CP->SetPropertyValue_InContainer(Spawner, (int32)CountD);

	bool bAutoSpawn = false;
	if (Args->TryGetBoolField(TEXT("auto_spawn"), bAutoSpawn))
		if (FBoolProperty* BP = FindFProperty<FBoolProperty>(AMassSpawner::StaticClass(), TEXT("bAutoSpawnOnBeginPlay")))
			BP->SetPropertyValue_InContainer(Spawner, bAutoSpawn);

	double Scale = -1.0;
	if (Args->TryGetNumberField(TEXT("spawn_count_scale"), Scale) && Scale >= 0)
		if (FFloatProperty* ScP = FindFProperty<FFloatProperty>(AMassSpawner::StaticClass(), TEXT("SpawningCountScale")))
			ScP->SetPropertyValue_InContainer(Spawner, (float)Scale);

	Spawner->MarkPackageDirty();

	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetBoolField(TEXT("success"), true);
	R->SetStringField(TEXT("actor_label"), ActorLabel);
	OutJsonString = JsonObjToString(R);
	UE_LOG(LogMCPTool, Log, TEXT("[MassEntity] Configured AMassSpawner '%s'"), *ActorLabel);
}

void HandleAddMassAgentComponentFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString BpPath;
	Args->TryGetStringField(TEXT("blueprint_path"), BpPath);
	if (BpPath.IsEmpty()) { OutError = TEXT("blueprint_path is required"); return; }

	UBlueprint* BP = LoadObject<UBlueprint>(nullptr, *BpPath);
	if (!BP) { OutError = FString::Printf(TEXT("Blueprint not found: %s"), *BpPath); return; }
	if (!BP->SimpleConstructionScript) { OutError = TEXT("Blueprint has no SimpleConstructionScript"); return; }

	for (USCS_Node* Node : BP->SimpleConstructionScript->GetAllNodes())
	{
		if (Node && Node->ComponentClass == UMassAgentComponent::StaticClass())
		{
			TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
			R->SetBoolField(TEXT("success"), true);
			R->SetStringField(TEXT("note"), TEXT("UMassAgentComponent already present"));
			OutJsonString = JsonObjToString(R);
			return;
		}
	}

	USCS_Node* NewNode = BP->SimpleConstructionScript->CreateNode(UMassAgentComponent::StaticClass(), TEXT("MassAgentComponent"));
	if (!NewNode) { OutError = TEXT("Failed to create SCS node for UMassAgentComponent"); return; }
	BP->SimpleConstructionScript->AddNode(NewNode);

	FKismetEditorUtilities::CompileBlueprint(BP);
	BP->MarkPackageDirty();

	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetBoolField(TEXT("success"), true);
	R->SetStringField(TEXT("blueprint"), BpPath);
	R->SetStringField(TEXT("component"), TEXT("MassAgentComponent"));
	OutJsonString = JsonObjToString(R);
	UE_LOG(LogMCPTool, Log, TEXT("[MassEntity] Added UMassAgentComponent to '%s'"), *BpPath);
}

void HandleListMassTraitClassesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	TArray<TSharedPtr<FJsonValue>> TraitsArr;
	for (TObjectIterator<UClass> It; It; ++It)
	{
		UClass* C = *It;
		if (!C->IsChildOf(UMassEntityTraitBase::StaticClass())) continue;
		if (C->HasAnyClassFlags(CLASS_Abstract)) continue;

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("class"), C->GetName());
		FString DisplayName = C->GetMetaData(TEXT("DisplayName"));
		Entry->SetStringField(TEXT("display_name"), DisplayName.IsEmpty() ? C->GetName() : DisplayName);
		Entry->SetStringField(TEXT("module"), C->GetOutermost()->GetName());
		TraitsArr.Add(MakeShared<FJsonValueObject>(Entry));
	}

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("success"), true);
	Root->SetNumberField(TEXT("count"), TraitsArr.Num());
	Root->SetArrayField(TEXT("traits"), TraitsArr);
	OutJsonString = JsonObjToString(Root);
}

void HandleRemoveMassTraitFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString ConfigPath;
	Args->TryGetStringField(TEXT("config_path"), ConfigPath);
	if (ConfigPath.IsEmpty()) { OutError = TEXT("config_path is required"); return; }

	UMassEntityConfigAsset* Asset = LoadObject<UMassEntityConfigAsset>(nullptr, *ConfigPath);
	if (!Asset) { OutError = FString::Printf(TEXT("Config asset not found: %s"), *ConfigPath); return; }

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("traits"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			FString TraitName;
			if ((*ItemsArray)[i]->Type == EJson::String)
				TraitName = (*ItemsArray)[i]->AsString();
			else if (TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject())
				TraitName = BatchToolHelper::GetItemString(Item, TEXT("trait_class"), TEXT("class"));
			if (TraitName.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing trait_class")); continue; }

			FString ItemOut, ItemErr;
			RemoveMassTraitSingle(Asset, TraitName, ItemOut, ItemErr);
			if (ItemErr.IsEmpty())
			{
				auto E = MakeShared<FJsonObject>(); E->SetStringField(TEXT("removed_trait"), TraitName);
				Batch.AddSuccess(i, E);
			}
			else Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJsonString);
		return;
	}

	FString TraitClassName;
	Args->TryGetStringField(TEXT("trait_class"), TraitClassName);
	if (TraitClassName.IsEmpty()) Args->TryGetStringField(TEXT("class"), TraitClassName);
	if (TraitClassName.IsEmpty()) { OutError = TEXT("trait_class is required"); return; }
	RemoveMassTraitSingle(Asset, TraitClassName, OutJsonString, OutError);
}

void HandleGetMassSpawnerSummaryFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World) { OutError = TEXT("No editor world available"); return; }

	FString ActorLabel;
	Args->TryGetStringField(TEXT("actor_label"), ActorLabel);

	if (ActorLabel.IsEmpty())
	{
		TArray<TSharedPtr<FJsonValue>> SpawnersArr;
		for (TActorIterator<AMassSpawner> It(World); It; ++It)
			SpawnersArr.Add(MakeShared<FJsonValueObject>(SerializeMassSpawner(*It)));

		TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetBoolField(TEXT("success"), true);
		Root->SetNumberField(TEXT("spawner_count"), SpawnersArr.Num());
		Root->SetArrayField(TEXT("spawners"), SpawnersArr);
		OutJsonString = JsonObjToString(Root);
		return;
	}

	AMassSpawner* Spawner = nullptr;
	for (TActorIterator<AMassSpawner> It(World); It; ++It)
	{
		if (It->GetActorLabel().Equals(ActorLabel, ESearchCase::IgnoreCase))
		{
			Spawner = *It;
			break;
		}
	}
	if (!Spawner) { OutError = FString::Printf(TEXT("AMassSpawner '%s' not found in level"), *ActorLabel); return; }

	TSharedPtr<FJsonObject> Root = SerializeMassSpawner(Spawner);
	Root->SetBoolField(TEXT("success"), true);
	OutJsonString = JsonObjToString(Root);
	UE_LOG(LogMCPTool, Log, TEXT("[MassEntity] Got summary for spawner '%s'"), *ActorLabel);
}

void HandleSetSpawnerEntityTypesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World) { OutError = TEXT("No editor world available"); return; }

	FString ActorLabel;
	Args->TryGetStringField(TEXT("actor_label"), ActorLabel);
	if (ActorLabel.IsEmpty()) { OutError = TEXT("actor_label is required"); return; }

	AMassSpawner* Spawner = nullptr;
	for (TActorIterator<AMassSpawner> It(World); It; ++It)
	{
		if (It->GetActorLabel().Equals(ActorLabel, ESearchCase::IgnoreCase))
		{
			Spawner = *It;
			break;
		}
	}
	if (!Spawner) { OutError = FString::Printf(TEXT("AMassSpawner '%s' not found"), *ActorLabel); return; }

	const TArray<TSharedPtr<FJsonValue>>* TypesArray = nullptr;
	if (!Args->TryGetArrayField(TEXT("entity_types"), TypesArray) || !TypesArray)
		if (!Args->TryGetArrayField(TEXT("items"), TypesArray) || !TypesArray)
			{ OutError = TEXT("entity_types=[{config_path, proportion},...] is required"); return; }

	FArrayProperty* ETProp = FindFProperty<FArrayProperty>(AMassSpawner::StaticClass(), TEXT("EntityTypes"));
	if (!ETProp) { OutError = TEXT("Could not find EntityTypes on AMassSpawner"); return; }
	FStructProperty* InnerStruct = CastField<FStructProperty>(ETProp->Inner);
	if (!InnerStruct) { OutError = TEXT("EntityTypes inner is not a struct"); return; }

	FScriptArrayHelper Helper(ETProp, ETProp->ContainerPtrToValuePtr<void>(Spawner));
	Helper.EmptyValues();

	int32 Added = 0;
	for (const TSharedPtr<FJsonValue>& Val : *TypesArray)
	{
		TSharedPtr<FJsonObject> Item = Val->AsObject();
		if (!Item.IsValid()) continue;
		FString ConfigPath;
		Item->TryGetStringField(TEXT("config_path"), ConfigPath);
		if (ConfigPath.IsEmpty()) continue;
		double Proportion = 1.0;
		Item->TryGetNumberField(TEXT("proportion"), Proportion);

		int32 NewIdx = Helper.AddValue();
		void* NewElem = Helper.GetRawPtr(NewIdx);

		UMassEntityConfigAsset* ConfigAsset = LoadObject<UMassEntityConfigAsset>(nullptr, *ConfigPath);
		if (FObjectProperty* OP = FindFProperty<FObjectProperty>(InnerStruct->Struct, TEXT("EntityConfig")))
			OP->SetObjectPropertyValue(OP->ContainerPtrToValuePtr<void>(NewElem), ConfigAsset);
		if (FSoftObjectProperty* SOP = FindFProperty<FSoftObjectProperty>(InnerStruct->Struct, TEXT("EntityConfig")))
		{
			FSoftObjectPtr SoftPtr = ConfigAsset ? FSoftObjectPtr(ConfigAsset) : FSoftObjectPtr(FSoftObjectPath(ConfigPath));
			SOP->SetPropertyValue(SOP->ContainerPtrToValuePtr<void>(NewElem), SoftPtr);
		}
		if (FFloatProperty* PropP = FindFProperty<FFloatProperty>(InnerStruct->Struct, TEXT("Proportion")))
			PropP->SetPropertyValue_InContainer(NewElem, (float)Proportion);

		Added++;
	}

	Spawner->MarkPackageDirty();

	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetBoolField(TEXT("success"), true);
	R->SetStringField(TEXT("actor_label"), ActorLabel);
	R->SetNumberField(TEXT("entity_types_set"), Added);
	OutJsonString = JsonObjToString(R);
	UE_LOG(LogMCPTool, Log, TEXT("[MassEntity] Set %d entity types on spawner '%s'"), Added, *ActorLabel);
}

void HandleValidateMassEntityConfigFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString ConfigPath;
	Args->TryGetStringField(TEXT("config_path"), ConfigPath);
	if (ConfigPath.IsEmpty()) { OutError = TEXT("config_path is required"); return; }

	UMassEntityConfigAsset* Asset = LoadObject<UMassEntityConfigAsset>(nullptr, *ConfigPath);
	if (!Asset) { OutError = FString::Printf(TEXT("Config asset not found: %s"), *ConfigPath); return; }

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World) { OutError = TEXT("No editor world available"); return; }

	class FMassValidationCapture : public FOutputDevice
	{
	public:
		TArray<FString> Errors;
		TArray<FString> Warnings;
		FMassValidationCapture() { bSuppressEventTag = true; }
		virtual void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& Category) override
		{
			if (Category != FName(TEXT("LogMass")) &&
				Category != FName(TEXT("MassEntity")) &&
				Category != FName(TEXT("LogMassRepresentation")))
				return;
			if (Verbosity <= ELogVerbosity::Error)
				Errors.Add(FString(V));
			else if (Verbosity == ELogVerbosity::Warning)
				Warnings.Add(FString(V));
		}
		virtual bool CanBeUsedOnMultipleThreads() const override { return true; }
	};

	Asset->DestroyEntityTemplate(*World);

	FMassValidationCapture Capture;
	GLog->AddOutputDevice(&Capture);
	Asset->GetOrCreateEntityTemplate(*World);
	GLog->RemoveOutputDevice(&Capture);

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("config_path"), ConfigPath);
	Root->SetBoolField(TEXT("valid"), Capture.Errors.Num() == 0);
	Root->SetNumberField(TEXT("error_count"), Capture.Errors.Num());
	Root->SetNumberField(TEXT("warning_count"), Capture.Warnings.Num());

	TArray<TSharedPtr<FJsonValue>> ErrorsArr, WarningsArr;
	for (const FString& E : Capture.Errors)
		ErrorsArr.Add(MakeShared<FJsonValueString>(E));
	for (const FString& W : Capture.Warnings)
		WarningsArr.Add(MakeShared<FJsonValueString>(W));
	Root->SetArrayField(TEXT("errors"), ErrorsArr);
	if (Capture.Warnings.Num() > 0)
		Root->SetArrayField(TEXT("warnings"), WarningsArr);

	OutJsonString = JsonObjToString(Root);
	UE_LOG(LogMCPTool, Log, TEXT("[MassEntity] Validated '%s': %d errors, %d warnings"),
		*ConfigPath, Capture.Errors.Num(), Capture.Warnings.Num());
}

}
