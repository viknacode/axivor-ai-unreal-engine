// Copyright 2026, BlueprintsLab, All rights reserved.

#include "Tools/GASTools.h"
#include "Managers/ProjectStateCache.h"
#include "Managers/SettingsManager.h"
#include "Tools/BatchToolHelper.h"

#include "AbilitySystemComponent.h"
#include "Abilities/GameplayAbility.h"
#include "GameplayEffect.h"
#include "GameplayEffectComponents/TargetTagsGameplayEffectComponent.h"
#include "GameplayEffectComponents/AssetTagsGameplayEffectComponent.h"
#include "GameplayEffectComponents/TargetTagRequirementsGameplayEffectComponent.h"
#include "GameplayEffectComponents/ChanceToApplyGameplayEffectComponent.h"
#include "GameplayEffectComponents/ImmunityGameplayEffectComponent.h"
#include "GameplayEffectComponents/RemoveOtherGameplayEffectComponent.h"
#include "GameplayEffectComponents/CustomCanApplyGameplayEffectComponent.h"
#include "GameplayEffectComponents/AbilitiesGameplayEffectComponent.h"
#include "GameplayEffectComponents/AdditionalEffectsGameplayEffectComponent.h"
#include "GameplayEffectComponents/BlockAbilityTagsGameplayEffectComponent.h"
#include "Misc/EngineVersionComparison.h"
#if !UE_VERSION_OLDER_THAN(5, 7, 0)
#include "GameplayEffectComponents/CancelAbilityTagsGameplayEffectComponent.h"
#endif
#include "GameplayEffectExecutionCalculation.h"
#include "GameplayEffectCustomApplicationRequirement.h"
#include "AttributeSet.h"
#include "GameplayCueNotify_Static.h"
#include "GameplayTagsManager.h"
#include "GameplayTagsEditorModule.h"

#include "Engine/Blueprint.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "EditorAssetLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "EdGraphSchema_K2.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Dom/JsonValue.h"
#include "GameplayEffectTypes.h"
#include "GameplayEffectAttributeCaptureDefinition.h"
#include "Abilities/GameplayAbilityTypes.h"
#include "UObject/UObjectIterator.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "ActiveGameplayEffectHandle.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

namespace GASTools
{

static UBlueprint* CreateBlueprintAsset(const FString& AssetName, const FString& SavePath, UClass* ParentClass, FString& OutError)
{
	if (AssetName.IsEmpty()) { OutError = TEXT("name is required"); return nullptr; }
	FString PackagePath = SavePath;
	while (PackagePath.EndsWith(TEXT("/"))) PackagePath = PackagePath.LeftChop(1);
	PackagePath += TEXT("/") + AssetName;

	if (FPackageName::DoesPackageExist(PackagePath))
	{
		OutError = FString::Printf(TEXT("Asset already exists at '%s'"), *PackagePath);
		return nullptr;
	}

	UBlueprint* NewBP = FKismetEditorUtilities::CreateBlueprint(
		ParentClass,
		CreatePackage(*PackagePath),
		FName(*AssetName),
		BPTYPE_Normal,
		UBlueprint::StaticClass(),
		UBlueprintGeneratedClass::StaticClass());

	if (!NewBP) { OutError = TEXT("FKismetEditorUtilities::CreateBlueprint failed"); return nullptr; }

	FAssetRegistryModule::AssetCreated(NewBP);
	NewBP->MarkPackageDirty();
	return NewBP;
}

static bool AddAttributeVar(UBlueprint* Blueprint, const FString& AttrName, FString& OutError)
{
	for (const FBPVariableDescription& Var : Blueprint->NewVariables)
	{
		if (Var.VarName == FName(*AttrName)) return true;
	}

	UScriptStruct* AttrStruct = FindObject<UScriptStruct>(nullptr, TEXT("/Script/GameplayAbilities.GameplayAttributeData"));
	if (!AttrStruct)
	{
		OutError = TEXT("FGameplayAttributeData struct not found. Ensure GameplayAbilities module is loaded.");
		return false;
	}

	FEdGraphPinType PinType;
	PinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
	PinType.PinSubCategoryObject = AttrStruct;

	FBlueprintEditorUtils::AddMemberVariable(Blueprint, FName(*AttrName), PinType);
	return true;
}

void HandleCreateAttributeSet(const FString& AssetName, const FString& SavePath,
	const TArray<FString>& AttributeNames,
	FString& OutJsonString, FString& OutError)
{

	UBlueprint* Blueprint = CreateBlueprintAsset(AssetName, SavePath, UAttributeSet::StaticClass(), OutError);
	if (!Blueprint) return;

	TArray<FString> Added;
	for (const FString& AttrName : AttributeNames)
	{
		if (!AttrName.IsEmpty())
		{
			FString AddErr;
			if (AddAttributeVar(Blueprint, AttrName, AddErr)) Added.Add(AttrName);
		}
	}
	if (AttributeNames.IsEmpty())
	{
		FString AddErr;
		AddAttributeVar(Blueprint, TEXT("Health"), AddErr);
		AddAttributeVar(Blueprint, TEXT("MaxHealth"), AddErr);
		Added.Add(TEXT("Health")); Added.Add(TEXT("MaxHealth"));
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	Blueprint->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(Blueprint->GetPathName(), false);

	FString AttrList = TEXT("[");
	bool bFirst = true;
	for (const FString& A : Added) { if (!bFirst) AttrList += TEXT(","); AttrList += FString::Printf(TEXT("\"%s\""), *A); bFirst = false; }
	AttrList += TEXT("]");

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"asset_path\":\"%s\",\"attributes\":%s,\"message\":\"AttributeSet created. Add ASC to your Character and register this AttributeSet on it.\"}"),
		*Blueprint->GetPathName(), *AttrList);
}

void HandleAddGameplayAttribute(const FString& BlueprintPath, const FString& AttributeName,
	float DefaultValue,
	FString& OutJsonString, FString& OutError)
{

	if (AttributeName.IsEmpty()) { OutError = TEXT("attribute_name is required"); return; }

	UBlueprint* Blueprint = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(BlueprintPath));
	if (!Blueprint) { OutError = TEXT("Blueprint not found: ") + BlueprintPath; return; }

	FString AddErr;
	if (!AddAttributeVar(Blueprint, AttributeName, AddErr))
	{
		OutError = AddErr; return;
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);

	FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipSave);

	float AppliedDefault = 0.f;
	bool bDefaultApplied = false;
	if (Blueprint->GeneratedClass)
	{
		if (UObject* CDO = Blueprint->GeneratedClass->GetDefaultObject())
		{
			if (FStructProperty* AttrProp = FindFProperty<FStructProperty>(Blueprint->GeneratedClass, FName(*AttributeName)))
			{
				if (AttrProp->Struct == FGameplayAttributeData::StaticStruct())
				{
					if (FGameplayAttributeData* AttrData = AttrProp->ContainerPtrToValuePtr<FGameplayAttributeData>(CDO))
					{
						CDO->Modify();
						AttrData->SetBaseValue(DefaultValue);
						AttrData->SetCurrentValue(DefaultValue);
						AppliedDefault = AttrData->GetBaseValue();
						bDefaultApplied = true;
					}
				}
			}
		}
	}

	Blueprint->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(BlueprintPath, false);

	if (!bDefaultApplied)
	{
		OutError = FString::Printf(TEXT("Attribute '%s' was added but its default BaseValue could not be applied (CDO struct property not found after compile)."), *AttributeName);
		return;
	}

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"attribute\":\"%s\",\"type\":\"FGameplayAttributeData\",\"default_value\":%g,\"message\":\"Attribute added with default BaseValue applied.\"}"),
		*AttributeName, AppliedDefault);
}

void HandleCreateGameplayAbility(const FString& AssetName, const FString& SavePath,
	const FString& AbilityTagName,
	FString& OutJsonString, FString& OutError)
{

	UBlueprint* Blueprint = CreateBlueprintAsset(AssetName, SavePath, UGameplayAbility::StaticClass(), OutError);
	if (!Blueprint) return;

	if (!AbilityTagName.IsEmpty())
	{
		UGameplayAbility* AbilityCDO = Cast<UGameplayAbility>(Blueprint->GeneratedClass ? Blueprint->GeneratedClass->GetDefaultObject() : nullptr);
		if (AbilityCDO)
		{
			UGameplayTagsManager& TagManager = UGameplayTagsManager::Get();
			FGameplayTag Tag = TagManager.RequestGameplayTag(FName(*AbilityTagName), false);
			if (Tag.IsValid())
			{
				AbilityCDO->Modify();
PRAGMA_DISABLE_DEPRECATION_WARNINGS
				AbilityCDO->AbilityTags.AddTag(Tag);
PRAGMA_ENABLE_DEPRECATION_WARNINGS
			}
		}
	}

	FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
	Blueprint->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(Blueprint->GetPathName(), false);

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"asset_path\":\"%s\",\"ability_tag\":\"%s\",\"message\":\"GameplayAbility Blueprint created. Implement ActivateAbility event in the Blueprint.\"}"),
		*Blueprint->GetPathName(), *AbilityTagName);
}

void HandleCreateGameplayEffect(const FString& AssetName, const FString& SavePath,
	const FString& DurationPolicy,
	FString& OutJsonString, FString& OutError)
{

	UBlueprint* Blueprint = CreateBlueprintAsset(AssetName, SavePath, UGameplayEffect::StaticClass(), OutError);
	if (!Blueprint) return;

	if (!DurationPolicy.IsEmpty() && Blueprint->GeneratedClass)
	{
		UGameplayEffect* EffectCDO = Cast<UGameplayEffect>(Blueprint->GeneratedClass->GetDefaultObject());
		if (EffectCDO)
		{
			EGameplayEffectDurationType Policy = EGameplayEffectDurationType::Instant;
			if (DurationPolicy.Equals(TEXT("infinite"), ESearchCase::IgnoreCase))
				Policy = EGameplayEffectDurationType::Infinite;
			else if (DurationPolicy.Equals(TEXT("has_duration"), ESearchCase::IgnoreCase) || DurationPolicy.Equals(TEXT("duration"), ESearchCase::IgnoreCase))
				Policy = EGameplayEffectDurationType::HasDuration;

			EffectCDO->Modify();
			EffectCDO->DurationPolicy = Policy;
		}
	}

	FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
	Blueprint->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(Blueprint->GetPathName(), false);

	FString DurStr = DurationPolicy.IsEmpty() ? TEXT("instant") : DurationPolicy;
	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"asset_path\":\"%s\",\"duration_policy\":\"%s\",\"message\":\"GameplayEffect created. Add Modifiers in Blueprint editor to change attributes.\"}"),
		*Blueprint->GetPathName(), *DurStr);
}

void HandleAddAbilitySystemComponent(const FString& BlueprintPath, const FString& ComponentName,
	FString& OutJsonString, FString& OutError)
{

	UBlueprint* Blueprint = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(BlueprintPath));
	if (!Blueprint) { OutError = TEXT("Blueprint not found: ") + BlueprintPath; return; }

	USimpleConstructionScript* SCS = Blueprint->SimpleConstructionScript;
	if (!SCS) { OutError = TEXT("Blueprint has no SCS (not an Actor-based Blueprint)"); return; }

	FString CompName = ComponentName.IsEmpty() ? TEXT("AbilitySystemComponent") : ComponentName;

	for (USCS_Node* Node : SCS->GetAllNodes())
	{
		if (Node->ComponentTemplate && Node->ComponentTemplate->IsA(UAbilitySystemComponent::StaticClass()))
		{
			OutJsonString = FString::Printf(TEXT("{\"success\":true,\"component\":\"%s\",\"message\":\"AbilitySystemComponent already exists\"}"), *CompName);
			return;
		}
	}

	USCS_Node* Node = SCS->CreateNode(UAbilitySystemComponent::StaticClass(), *CompName);
	SCS->AddNode(Node);

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	Blueprint->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(BlueprintPath, false);

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"component\":\"%s\",\"message\":\"AbilitySystemComponent added. Also implement IAbilitySystemInterface on the Blueprint to return this component.\"}"),
		*CompName);
}

void HandleSetAbilityTags(const FString& BlueprintPath,
	const TArray<FString>& AbilityTags,
	const TArray<FString>& BlockTags,
	const TArray<FString>& CancelTags,
	FString& OutJsonString, FString& OutError)
{

	UBlueprint* Blueprint = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(BlueprintPath));
	if (!Blueprint) { OutError = TEXT("Blueprint not found: ") + BlueprintPath; return; }

	if (!Blueprint->GeneratedClass)
	{
		OutError = TEXT("Blueprint has no generated class. Compile it first.");
		return;
	}

	UGameplayAbility* AbilityCDO = Cast<UGameplayAbility>(Blueprint->GeneratedClass->GetDefaultObject());
	if (!AbilityCDO) { OutError = TEXT("Blueprint is not a UGameplayAbility subclass"); return; }

	UGameplayTagsManager& TagManager = UGameplayTagsManager::Get();
	AbilityCDO->Modify();

	for (const FString& TagStr : AbilityTags)
	{
		FGameplayTag Tag = TagManager.RequestGameplayTag(FName(*TagStr), false);
		if (Tag.IsValid())
		{
PRAGMA_DISABLE_DEPRECATION_WARNINGS
			AbilityCDO->AbilityTags.AddTag(Tag);
PRAGMA_ENABLE_DEPRECATION_WARNINGS
		}
	}
	if (FStructProperty* BlockProp = FindFProperty<FStructProperty>(UGameplayAbility::StaticClass(), TEXT("BlockAbilitiesWithTag")))
	{
		FGameplayTagContainer* BlockContainer = BlockProp->ContainerPtrToValuePtr<FGameplayTagContainer>(AbilityCDO);
		for (const FString& TagStr : BlockTags)
		{
			FGameplayTag Tag = TagManager.RequestGameplayTag(FName(*TagStr), false);
			if (Tag.IsValid() && BlockContainer) BlockContainer->AddTag(Tag);
		}
	}
	if (FStructProperty* CancelProp = FindFProperty<FStructProperty>(UGameplayAbility::StaticClass(), TEXT("CancelAbilitiesWithTag")))
	{
		FGameplayTagContainer* CancelContainer = CancelProp->ContainerPtrToValuePtr<FGameplayTagContainer>(AbilityCDO);
		for (const FString& TagStr : CancelTags)
		{
			FGameplayTag Tag = TagManager.RequestGameplayTag(FName(*TagStr), false);
			if (Tag.IsValid() && CancelContainer) CancelContainer->AddTag(Tag);
		}
	}

	FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
	Blueprint->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(BlueprintPath, false);

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"ability_tags\":%d,\"block_tags\":%d,\"cancel_tags\":%d}"),
		AbilityTags.Num(), BlockTags.Num(), CancelTags.Num());
}

void HandleCreateGameplayCue(const FString& AssetName, const FString& SavePath,
	const FString& CueTag,
	FString& OutJsonString, FString& OutError)
{

	UBlueprint* Blueprint = CreateBlueprintAsset(AssetName, SavePath, UGameplayCueNotify_Static::StaticClass(), OutError);
	if (!Blueprint) return;

	if (!CueTag.IsEmpty() && Blueprint->GeneratedClass)
	{
		UGameplayCueNotify_Static* CueCDO = Cast<UGameplayCueNotify_Static>(Blueprint->GeneratedClass->GetDefaultObject());
		if (CueCDO)
		{
			UGameplayTagsManager& TagManager = UGameplayTagsManager::Get();
			FGameplayTag Tag = TagManager.RequestGameplayTag(FName(*CueTag), false);
			if (Tag.IsValid())
			{
				CueCDO->Modify();
				CueCDO->GameplayCueTag = Tag;
			}
		}
	}

	FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
	Blueprint->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(Blueprint->GetPathName(), false);

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"asset_path\":\"%s\",\"cue_tag\":\"%s\",\"message\":\"GameplayCue created. Implement OnExecute or OnActive events for visual/audio feedback.\"}"),
		*Blueprint->GetPathName(), *CueTag);
}

void HandleAddGameplayEffectModifier(const FString& EffectPath, const FString& AttributeName,
	const FString& ModifierOp, float Magnitude,
	FString& OutJsonString, FString& OutError)
{
	UBlueprint* BP = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(EffectPath));
	if (!BP || !BP->GeneratedClass) { OutError = TEXT("Could not load GameplayEffect Blueprint: ") + EffectPath; return; }

	UGameplayEffect* GE = Cast<UGameplayEffect>(BP->GeneratedClass->GetDefaultObject());
	if (!GE) { OutError = TEXT("Asset is not a GameplayEffect: ") + EffectPath; return; }

	FString TargetClassName, BareAttrName = AttributeName;
	int32 DotIdx;
	if (AttributeName.FindChar(TEXT('.'), DotIdx))
	{
		TargetClassName = AttributeName.Left(DotIdx);
		BareAttrName    = AttributeName.Mid(DotIdx + 1);
	}

	if (!TargetClassName.IsEmpty())
	{
		TArray<FAssetData> Found;
		const FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		FARFilter Filter;
		Filter.bRecursivePaths = true;
		Filter.PackagePaths.Add(FName(TEXT("/Game")));
		Filter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());
		Filter.PackageNames.Reset();
		ARM.Get().GetAssets(Filter, Found);
		for (const FAssetData& AD : Found)
		{
			if (AD.AssetName.ToString().Equals(TargetClassName, ESearchCase::IgnoreCase))
			{
				if (UBlueprint* ASBp = Cast<UBlueprint>(AD.GetAsset()))
				{
					if (ASBp->Status == BS_Dirty || ASBp->Status == BS_Unknown || !ASBp->GeneratedClass)
						FKismetEditorUtilities::CompileBlueprint(ASBp);
				}
				break;
			}
		}
	}

	FProperty* AttrProp = nullptr;
	for (TObjectIterator<UClass> It; It; ++It)
	{
		if (!It->IsChildOf(UAttributeSet::StaticClass()) || It->HasAnyClassFlags(CLASS_Abstract)) continue;
		if (!TargetClassName.IsEmpty())
		{
			FString ClsName = It->GetName();
			if (ClsName.EndsWith(TEXT("_C"))) ClsName = ClsName.LeftChop(2);
			if (!ClsName.Equals(TargetClassName, ESearchCase::IgnoreCase)) continue;
		}
		FProperty* P = FindFProperty<FProperty>(*It, *BareAttrName);
		if (!P)
			for (TFieldIterator<FProperty> FIt(*It); FIt; ++FIt)
				if (FIt->GetName().Equals(BareAttrName, ESearchCase::IgnoreCase)) { P = *FIt; break; }
		if (P) { AttrProp = P; break; }
	}
	if (!AttrProp)
	{
		TArray<FString> LoadedAS;
		for (TObjectIterator<UClass> It; It; ++It)
		{
			if (!It->IsChildOf(UAttributeSet::StaticClass()) || It->HasAnyClassFlags(CLASS_Abstract)) continue;
			FString ClsName = It->GetName();
			if (ClsName.EndsWith(TEXT("_C"))) ClsName = ClsName.LeftChop(2);
			if (!ClsName.Equals(TEXT("AttributeSet"), ESearchCase::IgnoreCase)) LoadedAS.AddUnique(ClsName);
		}
		OutError = FString::Printf(TEXT("Attribute '%s' not found. Loaded AttributeSet classes: %s. If your AS Blueprint isn't in the list, compile it first via compile_blueprint."),
			*AttributeName, *FString::Join(LoadedAS, TEXT(", ")));
		return;
	}

	EGameplayModOp::Type Op;
	FString CanonicalOp;
	if (ModifierOp.Contains(TEXT("multiply"), ESearchCase::IgnoreCase) || ModifierOp.Contains(TEXT("mult"), ESearchCase::IgnoreCase))
	{
		Op = EGameplayModOp::Multiplicitive; CanonicalOp = TEXT("Multiplicitive");
	}
	else if (ModifierOp.Contains(TEXT("divi"), ESearchCase::IgnoreCase))
	{
		Op = EGameplayModOp::Division; CanonicalOp = TEXT("Division");
	}
	else if (ModifierOp.Contains(TEXT("override"), ESearchCase::IgnoreCase))
	{
		Op = EGameplayModOp::Override; CanonicalOp = TEXT("Override");
	}
	else if (ModifierOp.Contains(TEXT("add"), ESearchCase::IgnoreCase) || ModifierOp.IsEmpty())
	{
		Op = EGameplayModOp::Additive; CanonicalOp = TEXT("Additive");
	}
	else
	{
		OutError = FString::Printf(TEXT("Invalid modifier_op '%s'. Use: Additive | Multiplicitive (multiply) | Division (divide) | Override."), *ModifierOp);
		return;
	}

	FGameplayModifierInfo Mod;
	Mod.Attribute = FGameplayAttribute(AttrProp);
	Mod.ModifierOp = Op;
	Mod.ModifierMagnitude = FGameplayEffectModifierMagnitude(Magnitude);
	GE->Modifiers.Add(Mod);

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
	UEditorAssetLibrary::SaveAsset(EffectPath, false);

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"effect_path\":\"%s\",\"attribute\":\"%s\",\"op\":\"%s\",\"magnitude\":%g}"),
		*EffectPath, *AttributeName, *CanonicalOp, Magnitude);
}

void HandleSetGameplayEffectDuration(const FString& EffectPath, const FString& DurationPolicy,
	float DurationSeconds, FString& OutJsonString, FString& OutError)
{
	UBlueprint* BP = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(EffectPath));
	if (!BP || !BP->GeneratedClass) { OutError = TEXT("Could not load GameplayEffect Blueprint: ") + EffectPath; return; }

	UGameplayEffect* GE = Cast<UGameplayEffect>(BP->GeneratedClass->GetDefaultObject());
	if (!GE) { OutError = TEXT("Asset is not a GameplayEffect: ") + EffectPath; return; }

	FString P = DurationPolicy.ToLower();
	P.ReplaceInline(TEXT("_"), TEXT("")); P.ReplaceInline(TEXT(" "), TEXT(""));
	FString Applied;
	if (P == TEXT("infinite"))
	{
		GE->DurationPolicy = EGameplayEffectDurationType::Infinite;
		Applied = TEXT("Infinite");
	}
	else if (P == TEXT("hasduration") || P == TEXT("duration") || P == TEXT("timed"))
	{
		GE->DurationPolicy = EGameplayEffectDurationType::HasDuration;
		if (DurationSeconds > 0.f)
			GE->DurationMagnitude = FGameplayEffectModifierMagnitude(DurationSeconds);
		Applied = TEXT("HasDuration");
	}
	else if (P == TEXT("instant"))
	{
		GE->DurationPolicy = EGameplayEffectDurationType::Instant;
		Applied = TEXT("Instant");
	}
	else
	{
		OutError = FString::Printf(TEXT("Invalid duration_policy '%s'. Use: Instant | HasDuration (needs duration_seconds) | Infinite."), *DurationPolicy);
		return;
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
	UEditorAssetLibrary::SaveAsset(EffectPath, false);

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"effect_path\":\"%s\",\"duration_policy\":\"%s\",\"duration_seconds\":%g}"),
		*EffectPath, *Applied, DurationSeconds);
}

void HandleGrantAbilityToBlueprint(const FString& BlueprintPath, const FString& AbilityPath,
	FString& OutJsonString, FString& OutError)
{
	UBlueprint* BP = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(BlueprintPath));
	if (!BP) { OutError = TEXT("Blueprint not found: ") + BlueprintPath; return; }

	UBlueprint* AbilityBP = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(AbilityPath));
	if (!AbilityBP || !AbilityBP->GeneratedClass) { OutError = TEXT("Ability Blueprint not found: ") + AbilityPath; return; }

	UClass* AbilityClass = AbilityBP->GeneratedClass;
	if (!AbilityClass->IsChildOf(UGameplayAbility::StaticClass())) { OutError = TEXT("Asset is not a GameplayAbility: ") + AbilityPath; return; }

	FArrayProperty* AbilityArrayProp = nullptr;
	for (TFieldIterator<FArrayProperty> It(BP->GeneratedClass); It; ++It)
	{
		FArrayProperty* AP = *It;
		FClassProperty* Inner = CastField<FClassProperty>(AP->Inner);
		if (Inner && Inner->MetaClass && Inner->MetaClass->IsChildOf(UGameplayAbility::StaticClass()))
		{
			AbilityArrayProp = AP; break;
		}
	}
	if (!AbilityArrayProp)
	{
		FEdGraphPinType PinType;
		PinType.PinCategory = UEdGraphSchema_K2::PC_Class;
		PinType.PinSubCategoryObject = UGameplayAbility::StaticClass();
		PinType.ContainerType = EPinContainerType::Array;
		FBlueprintEditorUtils::AddMemberVariable(BP, FName(TEXT("DefaultAbilities")), PinType);
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
		FKismetEditorUtilities::CompileBlueprint(BP, EBlueprintCompileOptions::SkipSave);

		for (TFieldIterator<FArrayProperty> It(BP->GeneratedClass); It; ++It)
		{
			FArrayProperty* AP = *It;
			if (!AP->GetName().StartsWith(TEXT("DefaultAbilities"))) continue;
			if (CastField<FClassProperty>(AP->Inner))
			{
				AbilityArrayProp = AP; break;
			}
		}
		if (!AbilityArrayProp)
		{
			UEditorAssetLibrary::SaveAsset(BlueprintPath, false);
			OutJsonString = FString::Printf(
				TEXT("{\"success\":true,\"blueprint_path\":\"%s\",\"ability_path\":\"%s\",\"message\":\"DefaultAbilities variable created. Call grant_ability_to_blueprint again, or add the ability manually in the Blueprint Details panel.\"}"),
				*BlueprintPath, *AbilityPath);
			return;
		}
	}

	UObject* CDO = BP->GeneratedClass->GetDefaultObject();
	FScriptArrayHelper Helper(AbilityArrayProp, AbilityArrayProp->ContainerPtrToValuePtr<void>(CDO));
	FObjectProperty* InnerObjProp = CastField<FObjectProperty>(AbilityArrayProp->Inner);

	for (int32 i = 0; i < Helper.Num(); ++i)
	{
		UObject* Existing = InnerObjProp->GetObjectPropertyValue(Helper.GetRawPtr(i));
		if (Existing == AbilityClass) { OutJsonString = FString::Printf(TEXT("{\"success\":true,\"message\":\"Ability already granted\",\"property\":\"%s\"}"), *AbilityArrayProp->GetName()); return; }
	}

	Helper.AddValue();
	InnerObjProp->SetObjectPropertyValue(Helper.GetRawPtr(Helper.Num() - 1), AbilityClass);

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
	UEditorAssetLibrary::SaveAsset(BlueprintPath, false);

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"blueprint_path\":\"%s\",\"ability_path\":\"%s\",\"property\":\"%s\"}"),
		*BlueprintPath, *AbilityPath, *AbilityArrayProp->GetName());
}

void HandleSetGameplayEffectStacking(const FString& EffectPath, const FString& StackingType,
	int32 StackLimitCount, const FString& StackDurationRefreshPolicy, const FString& StackPeriodResetPolicy,
	const FString& StackExpirationPolicy, FString& OutJsonString, FString& OutError)
{

	UBlueprint* BP = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(EffectPath));
	if (!BP || !BP->GeneratedClass) { OutError = TEXT("Could not load GameplayEffect Blueprint: ") + EffectPath; return; }
	UGameplayEffect* GE = Cast<UGameplayEffect>(BP->GeneratedClass->GetDefaultObject());
	if (!GE) { OutError = TEXT("Asset is not a GameplayEffect: ") + EffectPath; return; }

	EGameplayEffectStackingType NewStackingType = EGameplayEffectStackingType::None;
	if (StackingType.Contains(TEXT("source"), ESearchCase::IgnoreCase))
		NewStackingType = EGameplayEffectStackingType::AggregateBySource;
	else if (StackingType.Contains(TEXT("target"), ESearchCase::IgnoreCase))
		NewStackingType = EGameplayEffectStackingType::AggregateByTarget;
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	GE->StackingType = NewStackingType;
	PRAGMA_ENABLE_DEPRECATION_WARNINGS

	if (StackLimitCount > 0) GE->StackLimitCount = StackLimitCount;

	if (!StackDurationRefreshPolicy.IsEmpty())
	{
		if (StackDurationRefreshPolicy.Contains(TEXT("refresh"), ESearchCase::IgnoreCase))
			GE->StackDurationRefreshPolicy = EGameplayEffectStackingDurationPolicy::RefreshOnSuccessfulApplication;
		else
			GE->StackDurationRefreshPolicy = EGameplayEffectStackingDurationPolicy::NeverRefresh;
	}

	if (!StackPeriodResetPolicy.IsEmpty())
	{
		if (StackPeriodResetPolicy.Contains(TEXT("reset"), ESearchCase::IgnoreCase))
			GE->StackPeriodResetPolicy = EGameplayEffectStackingPeriodPolicy::ResetOnSuccessfulApplication;
		else
			GE->StackPeriodResetPolicy = EGameplayEffectStackingPeriodPolicy::NeverReset;
	}

	if (!StackExpirationPolicy.IsEmpty())
	{
		if (StackExpirationPolicy.Contains(TEXT("clear"), ESearchCase::IgnoreCase))
			GE->StackExpirationPolicy = EGameplayEffectStackingExpirationPolicy::ClearEntireStack;
		else if (StackExpirationPolicy.Contains(TEXT("single"), ESearchCase::IgnoreCase) || StackExpirationPolicy.Contains(TEXT("remove"), ESearchCase::IgnoreCase))
			GE->StackExpirationPolicy = EGameplayEffectStackingExpirationPolicy::RemoveSingleStackAndRefreshDuration;
		else
			GE->StackExpirationPolicy = EGameplayEffectStackingExpirationPolicy::RefreshDuration;
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
	UEditorAssetLibrary::SaveAsset(EffectPath, false);

	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"effect_path\":\"%s\",\"stacking_type\":\"%s\",\"stack_limit\":%d}"),
		*EffectPath, *StackingType, StackLimitCount);
}

void HandleSetGameplayEffectPeriod(const FString& EffectPath, float Period,
	bool bExecuteOnApplication, FString& OutJsonString, FString& OutError)
{

	UBlueprint* BP = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(EffectPath));
	if (!BP || !BP->GeneratedClass) { OutError = TEXT("Could not load GameplayEffect Blueprint: ") + EffectPath; return; }
	UGameplayEffect* GE = Cast<UGameplayEffect>(BP->GeneratedClass->GetDefaultObject());
	if (!GE) { OutError = TEXT("Asset is not a GameplayEffect: ") + EffectPath; return; }

	GE->Period = FScalableFloat(Period);
	GE->bExecutePeriodicEffectOnApplication = bExecuteOnApplication;

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
	UEditorAssetLibrary::SaveAsset(EffectPath, false);

	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"effect_path\":\"%s\",\"period\":%g,\"execute_on_application\":%s}"),
		*EffectPath, Period, bExecuteOnApplication ? TEXT("true") : TEXT("false"));
}

void HandleSetAbilityCost(const FString& AbilityPath, const FString& CostEffectPath,
	FString& OutJsonString, FString& OutError)
{

	UBlueprint* AbilityBP = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(AbilityPath));
	if (!AbilityBP || !AbilityBP->GeneratedClass) { OutError = TEXT("Could not load Ability Blueprint: ") + AbilityPath; return; }
	UGameplayAbility* AbilityCDO = Cast<UGameplayAbility>(AbilityBP->GeneratedClass->GetDefaultObject());
	if (!AbilityCDO) { OutError = TEXT("Asset is not a GameplayAbility: ") + AbilityPath; return; }

	UBlueprint* CostBP = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(CostEffectPath));
	if (!CostBP || !CostBP->GeneratedClass) { OutError = TEXT("Could not load Cost Effect Blueprint: ") + CostEffectPath; return; }
	if (!CostBP->GeneratedClass->IsChildOf(UGameplayEffect::StaticClass())) { OutError = TEXT("Cost asset is not a GameplayEffect"); return; }

	FClassProperty* CostProp = FindFProperty<FClassProperty>(UGameplayAbility::StaticClass(), TEXT("CostGameplayEffectClass"));
	if (CostProp)
	{
		CostProp->SetPropertyValue_InContainer(AbilityCDO, CostBP->GeneratedClass);
	}
	else
	{
		OutError = TEXT("Could not find CostGameplayEffectClass property");
		return;
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AbilityBP);
	UEditorAssetLibrary::SaveAsset(AbilityPath, false);

	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"ability_path\":\"%s\",\"cost_effect\":\"%s\"}"),
		*AbilityPath, *CostEffectPath);
}

void HandleSetAbilityCooldown(const FString& AbilityPath, const FString& CooldownEffectPath,
	FString& OutJsonString, FString& OutError)
{

	UBlueprint* AbilityBP = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(AbilityPath));
	if (!AbilityBP || !AbilityBP->GeneratedClass) { OutError = TEXT("Could not load Ability Blueprint: ") + AbilityPath; return; }
	UGameplayAbility* AbilityCDO = Cast<UGameplayAbility>(AbilityBP->GeneratedClass->GetDefaultObject());
	if (!AbilityCDO) { OutError = TEXT("Asset is not a GameplayAbility: ") + AbilityPath; return; }

	UBlueprint* CooldownBP = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(CooldownEffectPath));
	if (!CooldownBP || !CooldownBP->GeneratedClass) { OutError = TEXT("Could not load Cooldown Effect Blueprint: ") + CooldownEffectPath; return; }
	if (!CooldownBP->GeneratedClass->IsChildOf(UGameplayEffect::StaticClass())) { OutError = TEXT("Cooldown asset is not a GameplayEffect"); return; }

	FClassProperty* CooldownProp = FindFProperty<FClassProperty>(UGameplayAbility::StaticClass(), TEXT("CooldownGameplayEffectClass"));
	if (CooldownProp)
	{
		CooldownProp->SetPropertyValue_InContainer(AbilityCDO, CooldownBP->GeneratedClass);
	}
	else
	{
		OutError = TEXT("Could not find CooldownGameplayEffectClass property");
		return;
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AbilityBP);
	UEditorAssetLibrary::SaveAsset(AbilityPath, false);

	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"ability_path\":\"%s\",\"cooldown_effect\":\"%s\"}"),
		*AbilityPath, *CooldownEffectPath);
}

void HandleGetGameplayEffectSummary(const FString& EffectPath,
	FString& OutJsonString, FString& OutError)
{

	UBlueprint* BP = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(EffectPath));
	if (!BP || !BP->GeneratedClass) { OutError = TEXT("Could not load GameplayEffect Blueprint: ") + EffectPath; return; }
	UGameplayEffect* GE = Cast<UGameplayEffect>(BP->GeneratedClass->GetDefaultObject());
	if (!GE) { OutError = TEXT("Asset is not a GameplayEffect: ") + EffectPath; return; }

	TSharedPtr<FJsonObject> Res = MakeShareable(new FJsonObject());
	Res->SetBoolField(TEXT("success"), true);
	Res->SetStringField(TEXT("effect_path"), EffectPath);

	FString DurStr;
	switch (GE->DurationPolicy)
	{
		case EGameplayEffectDurationType::Instant: DurStr = TEXT("Instant"); break;
		case EGameplayEffectDurationType::Infinite: DurStr = TEXT("Infinite"); break;
		case EGameplayEffectDurationType::HasDuration: DurStr = TEXT("HasDuration"); break;
	}
	Res->SetStringField(TEXT("duration_policy"), DurStr);
	if (GE->DurationPolicy == EGameplayEffectDurationType::HasDuration)
	{
		float DurVal = 0.f;
		GE->DurationMagnitude.GetStaticMagnitudeIfPossible(1.f, DurVal);
		Res->SetNumberField(TEXT("duration"), DurVal);
	}

	FString StackStr;
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	const EGameplayEffectStackingType ReadStackingType = GE->StackingType;
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
	switch (ReadStackingType)
	{
		case EGameplayEffectStackingType::None: StackStr = TEXT("None"); break;
		case EGameplayEffectStackingType::AggregateBySource: StackStr = TEXT("AggregateBySource"); break;
		case EGameplayEffectStackingType::AggregateByTarget: StackStr = TEXT("AggregateByTarget"); break;
	}
	Res->SetStringField(TEXT("stacking_type"), StackStr);
	Res->SetNumberField(TEXT("stack_limit"), GE->StackLimitCount);

	Res->SetNumberField(TEXT("period"), GE->Period.GetValue());
	Res->SetBoolField(TEXT("execute_periodic_on_application"), GE->bExecutePeriodicEffectOnApplication);

	TArray<TSharedPtr<FJsonValue>> ModsArray;
	for (const FGameplayModifierInfo& Mod : GE->Modifiers)
	{
		TSharedPtr<FJsonObject> ModObj = MakeShareable(new FJsonObject());
		ModObj->SetStringField(TEXT("attribute"), Mod.Attribute.GetName());
		FString OpStr;
		switch (Mod.ModifierOp)
		{
			case EGameplayModOp::Additive: OpStr = TEXT("Additive"); break;
			case EGameplayModOp::Multiplicitive: OpStr = TEXT("Multiplicitive"); break;
			case EGameplayModOp::Override: OpStr = TEXT("Override"); break;
			default: OpStr = TEXT("Other"); break;
		}
		ModObj->SetStringField(TEXT("operation"), OpStr);

		const EGameplayEffectMagnitudeCalculation CalcType = Mod.ModifierMagnitude.GetMagnitudeCalculationType();
		FString CalcStr;
		switch (CalcType)
		{
			case EGameplayEffectMagnitudeCalculation::ScalableFloat:           CalcStr = TEXT("ScalableFloat"); break;
			case EGameplayEffectMagnitudeCalculation::AttributeBased:          CalcStr = TEXT("AttributeBased"); break;
			case EGameplayEffectMagnitudeCalculation::CustomCalculationClass:  CalcStr = TEXT("CustomCalculationClass"); break;
			case EGameplayEffectMagnitudeCalculation::SetByCaller:             CalcStr = TEXT("SetByCaller"); break;
		}
		ModObj->SetStringField(TEXT("magnitude_type"), CalcStr);

		float Mag = 0.f;
		const bool bStaticMagOk = Mod.ModifierMagnitude.GetStaticMagnitudeIfPossible(1.f, Mag);
		if (bStaticMagOk)
		{
			ModObj->SetNumberField(TEXT("magnitude"), Mag);
		}
		else
		{
			UScriptStruct* MagStruct = FGameplayEffectModifierMagnitude::StaticStruct();
			void* MagPtr = (void*)&Mod.ModifierMagnitude;
			if (CalcType == EGameplayEffectMagnitudeCalculation::SetByCaller)
			{
				if (FStructProperty* SF = FindFProperty<FStructProperty>(MagStruct, TEXT("SetByCallerMagnitude")))
				{
					void* SFPtr = SF->ContainerPtrToValuePtr<void>(MagPtr);
					if (FStructProperty* TagSP = FindFProperty<FStructProperty>(SF->Struct, TEXT("DataTag")))
					{
						const FGameplayTag& Tag = *(FGameplayTag*)TagSP->ContainerPtrToValuePtr<void>(SFPtr);
						if (Tag.IsValid()) ModObj->SetStringField(TEXT("data_tag"), Tag.ToString());
					}
					if (FNameProperty* NameProp = FindFProperty<FNameProperty>(SF->Struct, TEXT("DataName")))
					{
						const FName Name = NameProp->GetPropertyValue(NameProp->ContainerPtrToValuePtr<void>(SFPtr));
						if (!Name.IsNone()) ModObj->SetStringField(TEXT("data_name"), Name.ToString());
					}
				}
			}
			else if (CalcType == EGameplayEffectMagnitudeCalculation::AttributeBased)
			{
				if (FStructProperty* AB = FindFProperty<FStructProperty>(MagStruct, TEXT("AttributeBasedMagnitude")))
				{
					void* ABPtr = AB->ContainerPtrToValuePtr<void>(MagPtr);
					if (FStructProperty* BackingSP = FindFProperty<FStructProperty>(AB->Struct, TEXT("BackingAttribute")))
					{
						void* BackingPtr = BackingSP->ContainerPtrToValuePtr<void>(ABPtr);
						if (FStructProperty* AttrSP = FindFProperty<FStructProperty>(BackingSP->Struct, TEXT("AttributeToCapture")))
						{
							const FGameplayAttribute& Attr = *(FGameplayAttribute*)AttrSP->ContainerPtrToValuePtr<void>(BackingPtr);
							if (Attr.IsValid()) ModObj->SetStringField(TEXT("backing_attribute"), Attr.GetName());
						}
					}
					if (FStructProperty* CoeffSP = FindFProperty<FStructProperty>(AB->Struct, TEXT("Coefficient")))
					{
						void* CoeffPtr = CoeffSP->ContainerPtrToValuePtr<void>(ABPtr);
						if (FFloatProperty* ValProp = FindFProperty<FFloatProperty>(CoeffSP->Struct, TEXT("Value")))
						{
							ModObj->SetNumberField(TEXT("coefficient"),
								ValProp->GetPropertyValue(ValProp->ContainerPtrToValuePtr<void>(CoeffPtr)));
						}
					}
				}
			}
			else if (CalcType == EGameplayEffectMagnitudeCalculation::CustomCalculationClass)
			{
				if (FStructProperty* CustomSP = FindFProperty<FStructProperty>(MagStruct, TEXT("CustomMagnitude")))
				{
					void* CustomPtr = CustomSP->ContainerPtrToValuePtr<void>(MagPtr);
					if (FClassProperty* CCP = FindFProperty<FClassProperty>(CustomSP->Struct, TEXT("CalculationClassMagnitude")))
					{
						UObject* Cls = CCP->GetObjectPropertyValue(CCP->ContainerPtrToValuePtr<void>(CustomPtr));
						if (Cls) ModObj->SetStringField(TEXT("calculation_class"), Cls->GetPathName());
					}
				}
			}
		}
		{
			UScriptStruct* MIS = FGameplayModifierInfo::StaticStruct();
			auto AddReq = [&](const TCHAR* Field, const TCHAR* ReqKey, const TCHAR* IgnKey)
			{
				FStructProperty* SP = FindFProperty<FStructProperty>(MIS, Field);
				if (!SP) return;
				const FGameplayTagRequirements* R = SP->ContainerPtrToValuePtr<FGameplayTagRequirements>(&Mod);
				if (!R) return;
				auto Emit = [&](const FGameplayTagContainer& C, const TCHAR* K)
				{
					TArray<FGameplayTag> L; C.GetGameplayTagArray(L);
					if (L.Num() == 0) return;
					TArray<TSharedPtr<FJsonValue>> A; for (const FGameplayTag& T : L) A.Add(MakeShareable(new FJsonValueString(T.ToString())));
					ModObj->SetArrayField(K, A);
				};
				Emit(R->RequireTags, ReqKey); Emit(R->IgnoreTags, IgnKey);
			};
			AddReq(TEXT("SourceTags"), TEXT("source_required_tags"), TEXT("source_ignored_tags"));
			AddReq(TEXT("TargetTags"), TEXT("target_required_tags"), TEXT("target_ignored_tags"));
		}
		ModsArray.Add(MakeShareable(new FJsonValueObject(ModObj)));
	}
	Res->SetArrayField(TEXT("modifiers"), ModsArray);
	Res->SetNumberField(TEXT("modifier_count"), GE->Modifiers.Num());

	auto WriteTags = [&Res](const FString& Field, const FGameplayTagContainer& Container)
	{
		TArray<FGameplayTag> TagList;
		Container.GetGameplayTagArray(TagList);
		if (TagList.Num() == 0) return;
		TArray<TSharedPtr<FJsonValue>> Arr;
		for (const FGameplayTag& T : TagList) Arr.Add(MakeShareable(new FJsonValueString(T.ToString())));
		Res->SetArrayField(Field, Arr);
	};
	auto Resolve = [](const FInheritedTagContainer& C) -> const FGameplayTagContainer&
	{
		return C.CombinedTags.IsEmpty() ? C.Added : C.CombinedTags;
	};
	if (const UTargetTagsGameplayEffectComponent* TT = GE->FindComponent<UTargetTagsGameplayEffectComponent>())
		WriteTags(TEXT("granted_tags"), Resolve(TT->GetConfiguredTargetTagChanges()));
	if (const UAssetTagsGameplayEffectComponent* AT = GE->FindComponent<UAssetTagsGameplayEffectComponent>())
		WriteTags(TEXT("asset_tags"), Resolve(AT->GetConfiguredAssetTagChanges()));
	if (const UTargetTagRequirementsGameplayEffectComponent* TR = GE->FindComponent<UTargetTagRequirementsGameplayEffectComponent>())
	{
		WriteTags(TEXT("application_required_tags"), TR->ApplicationTagRequirements.RequireTags);
		WriteTags(TEXT("ongoing_required_tags"), TR->OngoingTagRequirements.RequireTags);
		WriteTags(TEXT("removal_required_tags"), TR->RemovalTagRequirements.RequireTags);
	}

	if (const UChanceToApplyGameplayEffectComponent* CC = GE->FindComponent<UChanceToApplyGameplayEffectComponent>())
		Res->SetNumberField(TEXT("chance_to_apply"), CC->GetChanceToApplyToTarget().GetValueAtLevel(0.f));
	if (const UImmunityGameplayEffectComponent* IM = GE->FindComponent<UImmunityGameplayEffectComponent>())
		Res->SetNumberField(TEXT("immunity_query_count"), IM->ImmunityQueries.Num());
	if (const URemoveOtherGameplayEffectComponent* RO = GE->FindComponent<URemoveOtherGameplayEffectComponent>())
		Res->SetNumberField(TEXT("remove_other_query_count"), RO->RemoveGameplayEffectQueries.Num());
	if (const UCustomCanApplyGameplayEffectComponent* CA = GE->FindComponent<UCustomCanApplyGameplayEffectComponent>())
	{
		TArray<TSharedPtr<FJsonValue>> Arr;
		for (const TSubclassOf<UGameplayEffectCustomApplicationRequirement>& R : CA->ApplicationRequirements)
			if (UClass* Cls = R.Get()) Arr.Add(MakeShareable(new FJsonValueString(Cls->GetName())));
		if (Arr.Num()) Res->SetArrayField(TEXT("custom_application_requirements"), Arr);
	}
	if (GE->FindComponent<UAbilitiesGameplayEffectComponent>())
		Res->SetBoolField(TEXT("grants_abilities"), true);
	if (const UAdditionalEffectsGameplayEffectComponent* AE = GE->FindComponent<UAdditionalEffectsGameplayEffectComponent>())
	{
		Res->SetNumberField(TEXT("on_application_effects"), AE->OnApplicationGameplayEffects.Num());
		Res->SetNumberField(TEXT("on_complete_effects"), AE->OnCompleteAlways.Num() + AE->OnCompleteNormal.Num() + AE->OnCompletePrematurely.Num());
	}
	if (const UBlockAbilityTagsGameplayEffectComponent* BA = GE->FindComponent<UBlockAbilityTagsGameplayEffectComponent>())
		WriteTags(TEXT("block_ability_tags"), Resolve(BA->GetConfiguredBlockedAbilityTagChanges()));
#if !UE_VERSION_OLDER_THAN(5, 7, 0)
	if (GE->FindComponent<UCancelAbilityTagsGameplayEffectComponent>())
		Res->SetBoolField(TEXT("cancels_abilities"), true);
#endif
	{
		TArray<TSharedPtr<FJsonValue>> Cues;
		for (const FGameplayEffectCue& Cue : GE->GameplayCues)
		{
			TArray<FGameplayTag> Tg; Cue.GameplayCueTags.GetGameplayTagArray(Tg);
			for (const FGameplayTag& T : Tg) Cues.Add(MakeShareable(new FJsonValueString(T.ToString())));
		}
		if (Cues.Num()) Res->SetArrayField(TEXT("gameplay_cues"), Cues);
	}
	if (GE->Executions.Num() > 0)
	{
		FArrayProperty* CondAP = FindFProperty<FArrayProperty>(FGameplayEffectExecutionDefinition::StaticStruct(), TEXT("ConditionalGameplayEffects"));
		TArray<TSharedPtr<FJsonValue>> Ex;
		for (FGameplayEffectExecutionDefinition& E : GE->Executions)
		{
			TSharedRef<FJsonObject> EO = MakeShared<FJsonObject>();
			UClass* C = E.CalculationClass.Get();
			EO->SetStringField(TEXT("calculation_class"), C ? C->GetName() : TEXT("None"));
			if (CondAP) { FScriptArrayHelper H(CondAP, CondAP->ContainerPtrToValuePtr<void>(&E)); EO->SetNumberField(TEXT("conditional_effects"), H.Num()); }
			Ex.Add(MakeShareable(new FJsonValueObject(EO)));
		}
		Res->SetArrayField(TEXT("executions"), Ex);
	}
	if (GE->OverflowEffects.Num() > 0) Res->SetNumberField(TEXT("overflow_effects"), GE->OverflowEffects.Num());
	if (GE->bDenyOverflowApplication) Res->SetBoolField(TEXT("deny_overflow_application"), true);

	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Res.ToSharedRef(), W);
}

void HandleGetAttributeSetSummary(const FString& AttributeSetPath,
	FString& OutJsonString, FString& OutError)
{

	UBlueprint* BP = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(AttributeSetPath));
	if (!BP || !BP->GeneratedClass) { OutError = TEXT("Could not load AttributeSet Blueprint: ") + AttributeSetPath; return; }
	if (!BP->GeneratedClass->IsChildOf(UAttributeSet::StaticClass())) { OutError = TEXT("Asset is not an AttributeSet: ") + AttributeSetPath; return; }

	TSharedPtr<FJsonObject> Res = MakeShareable(new FJsonObject());
	Res->SetBoolField(TEXT("success"), true);
	Res->SetStringField(TEXT("attribute_set_path"), AttributeSetPath);
	Res->SetStringField(TEXT("class_name"), BP->GeneratedClass->GetName());

	TArray<TSharedPtr<FJsonValue>> AttrsArray;
	UObject* CDO = BP->GeneratedClass->GetDefaultObject();
	for (TFieldIterator<FStructProperty> It(BP->GeneratedClass); It; ++It)
	{
		FStructProperty* Prop = *It;
		if (!Prop || Prop->GetOwnerClass() == UAttributeSet::StaticClass()) continue;
		if (Prop->Struct != FGameplayAttributeData::StaticStruct()) continue;

		TSharedPtr<FJsonObject> AttrObj = MakeShareable(new FJsonObject());
		AttrObj->SetStringField(TEXT("name"), Prop->GetName());

		float DefaultVal = 0.f;
		if (const FGameplayAttributeData* AttrData = Prop->ContainerPtrToValuePtr<FGameplayAttributeData>(CDO))
			DefaultVal = AttrData->GetBaseValue();

		AttrObj->SetNumberField(TEXT("default_value"), DefaultVal);
		AttrsArray.Add(MakeShareable(new FJsonValueObject(AttrObj)));
	}

	Res->SetArrayField(TEXT("attributes"), AttrsArray);
	Res->SetNumberField(TEXT("attribute_count"), AttrsArray.Num());

	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Res.ToSharedRef(), W);
}

void HandleRemoveGameplayEffectModifier(const FString& EffectPath, const FString& AttributeName,
	int32 ModifierIndex, FString& OutJsonString, FString& OutError)
{

	UBlueprint* BP = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(EffectPath));
	if (!BP || !BP->GeneratedClass) { OutError = TEXT("Could not load GameplayEffect Blueprint: ") + EffectPath; return; }
	UGameplayEffect* GE = Cast<UGameplayEffect>(BP->GeneratedClass->GetDefaultObject());
	if (!GE) { OutError = TEXT("Asset is not a GameplayEffect: ") + EffectPath; return; }

	int32 RemovedCount = 0;
	FString RemovedBy;
	if (ModifierIndex >= 0)
	{
		if (!GE->Modifiers.IsValidIndex(ModifierIndex))
		{
			OutError = FString::Printf(TEXT("modifier_index %d out of range -- '%s' has %d modifier(s)."),
				ModifierIndex, *EffectPath, GE->Modifiers.Num());
			return;
		}
		GE->Modifiers.RemoveAt(ModifierIndex);
		RemovedCount = 1;
		RemovedBy = FString::Printf(TEXT("index %d"), ModifierIndex);
	}
	else if (!AttributeName.IsEmpty())
	{
		for (int32 i = GE->Modifiers.Num() - 1; i >= 0; --i)
		{
			if (GE->Modifiers[i].Attribute.GetName().Equals(AttributeName, ESearchCase::IgnoreCase))
			{
				GE->Modifiers.RemoveAt(i);
				RemovedCount++;
			}
		}
		if (RemovedCount == 0)
		{
			OutError = FString::Printf(TEXT("No modifier found for attribute '%s' in '%s'"), *AttributeName, *EffectPath);
			return;
		}
		RemovedBy = FString::Printf(TEXT("attribute '%s'"), *AttributeName);
	}
	else
	{
		OutError = TEXT("Provide modifier_index (preferred) or attribute_name to identify the modifier to remove.");
		return;
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
	UEditorAssetLibrary::SaveAsset(EffectPath, false);

	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"effect_path\":\"%s\",\"removed_by\":\"%s\",\"removed_count\":%d}"),
		*EffectPath, *RemovedBy, RemovedCount);
}

void HandleListGameplayAbilities(const FString& BlueprintPath, FString& OutJsonString, FString& OutError)
{

	UBlueprint* BP = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(BlueprintPath));
	if (!BP) { OutError = FString::Printf(TEXT("Could not load Blueprint at '%s'"), *BlueprintPath); return; }

	UClass* BPClass = BP->GeneratedClass;
	if (!BPClass) { OutError = TEXT("Blueprint has no generated class"); return; }

	UObject* CDO = BPClass->GetDefaultObject();

	TSharedPtr<FJsonObject> Res = MakeShareable(new FJsonObject());
	Res->SetBoolField(TEXT("success"), true);
	Res->SetStringField(TEXT("blueprint_path"), BlueprintPath);

	TArray<TSharedPtr<FJsonValue>> AbilityArray;
	if (CDO)
	{
		for (TFieldIterator<FArrayProperty> It(BPClass); It; ++It)
		{
			FArrayProperty* ArrProp = *It;
			FClassProperty* ElemProp = CastField<FClassProperty>(ArrProp->Inner);
			if (!ElemProp || !ElemProp->MetaClass
				|| !ElemProp->MetaClass->IsChildOf(UGameplayAbility::StaticClass()))
				continue;

			FScriptArrayHelper Helper(ArrProp, ArrProp->ContainerPtrToValuePtr<void>(CDO));
			for (int32 e = 0; e < Helper.Num(); ++e)
			{
				UClass* AbilityClass = Cast<UClass>(ElemProp->GetObjectPropertyValue(Helper.GetRawPtr(e)));
				if (!AbilityClass) continue;
				TSharedPtr<FJsonObject> AbObj = MakeShareable(new FJsonObject());
				AbObj->SetStringField(TEXT("name"), AbilityClass->GetName());
				AbObj->SetStringField(TEXT("path"), AbilityClass->GetPathName());
				AbObj->SetStringField(TEXT("source_property"), ArrProp->GetName());
				AbilityArray.Add(MakeShareable(new FJsonValueObject(AbObj)));
			}
		}
	}

	Res->SetArrayField(TEXT("abilities"), AbilityArray);
	Res->SetNumberField(TEXT("count"), AbilityArray.Num());
	if (AbilityArray.Num() == 0)
		Res->SetStringField(TEXT("note"),
			TEXT("No granted-ability array (TArray<TSubclassOf<UGameplayAbility>>) found on this Blueprint's CDO. "
			     "Use grant_ability_to_blueprint to add abilities to it."));

	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Res.ToSharedRef(), W);
}

static UGameplayEffect* GASLoadEffectCDO(const FString& EffectPath, UBlueprint*& OutBP, FString& OutError)
{
	OutBP = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(EffectPath));
	if (!OutBP || !OutBP->GeneratedClass) { OutError = TEXT("Could not load GameplayEffect Blueprint: ") + EffectPath; return nullptr; }
	UGameplayEffect* GE = Cast<UGameplayEffect>(OutBP->GeneratedClass->GetDefaultObject());
	if (!GE) { OutError = TEXT("Asset is not a GameplayEffect: ") + EffectPath; return nullptr; }
	return GE;
}

static FGameplayTagContainer GASBuildTags(const TArray<FString>& Tags, TArray<FString>& OutUnresolved)
{
	FGameplayTagContainer Out;
	for (const FString& TagStr : Tags)
	{
		FGameplayTag Tag = UGameplayTagsManager::Get().RequestGameplayTag(FName(*TagStr), false);
		if (!Tag.IsValid())
		{
			IGameplayTagsEditorModule::Get().AddNewGameplayTagToINI(TagStr);
			Tag = UGameplayTagsManager::Get().RequestGameplayTag(FName(*TagStr), false);
		}
		if (Tag.IsValid()) Out.AddTag(Tag); else OutUnresolved.Add(TagStr);
	}
	return Out;
}

static UClass* GASResolveClass(const FString& Path)
{
	FString P = Path; P.TrimStartAndEndInline();
	if (P.IsEmpty() || P.Equals(TEXT("None"), ESearchCase::IgnoreCase)) return nullptr;
	if (UClass* C = LoadObject<UClass>(nullptr, *P)) return C;
	if (UClass* C = LoadObject<UClass>(nullptr, *(P + TEXT("_C")))) return C;
	if (UBlueprint* BP = LoadObject<UBlueprint>(nullptr, *P)) return BP->GeneratedClass.Get();
	return nullptr;
}

static void GASFinalizeEffect(UBlueprint* BP, const FString& EffectPath)
{
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
	UEditorAssetLibrary::SaveAsset(EffectPath, false);
}

void HandleSetGameplayEffectTags(const FString& EffectPath, const FString& TagCategory,
	const TArray<FString>& Tags, FString& OutJsonString, FString& OutError)
{

	UBlueprint* BP = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(EffectPath));
	if (!BP || !BP->GeneratedClass) { OutError = TEXT("Could not load GameplayEffect Blueprint: ") + EffectPath; return; }
	UGameplayEffect* GE = Cast<UGameplayEffect>(BP->GeneratedClass->GetDefaultObject());
	if (!GE) { OutError = TEXT("Asset is not a GameplayEffect: ") + EffectPath; return; }

	FGameplayTagContainer TagContainer;
	TArray<FString> Unresolved;
	for (const FString& TagStr : Tags)
	{
		FGameplayTag Tag = UGameplayTagsManager::Get().RequestGameplayTag(FName(*TagStr), false);
		if (!Tag.IsValid())
		{
			IGameplayTagsEditorModule::Get().AddNewGameplayTagToINI(TagStr);
			Tag = UGameplayTagsManager::Get().RequestGameplayTag(FName(*TagStr), false);
		}
		if (Tag.IsValid())
			TagContainer.AddTag(Tag);
		else
			Unresolved.Add(TagStr);
	}
	if (TagContainer.IsEmpty() && Tags.Num() > 0)
	{
		OutError = FString::Printf(TEXT("Could not resolve or register any of the requested tags: %s"), *FString::Join(Unresolved, TEXT(", ")));
		return;
	}

	FString CatLower = TagCategory.ToLower();
	FString Warning;
	if (CatLower == TEXT("granted") || CatLower == TEXT("grantedtags") || CatLower == TEXT("owned"))
	{
		UTargetTagsGameplayEffectComponent& Comp = GE->FindOrAddComponent<UTargetTagsGameplayEffectComponent>();
		FInheritedTagContainer ITC;
		ITC.Added = TagContainer;
		Comp.SetAndApplyTargetTagChanges(ITC);

		if (GE->DurationPolicy == EGameplayEffectDurationType::Instant)
		{
			Warning = TEXT("This GameplayEffect is Instant, so granted tags will not apply. Set duration_policy to HasDuration or Infinite (e.g. set_gameplay_effect_duration) for the granted tags to take effect.");
		}
	}
	else if (CatLower == TEXT("asset") || CatLower == TEXT("assettags"))
	{
		UAssetTagsGameplayEffectComponent& Comp = GE->FindOrAddComponent<UAssetTagsGameplayEffectComponent>();
		FInheritedTagContainer ITC;
		ITC.Added = TagContainer;
		Comp.SetAndApplyAssetTagChanges(ITC);
	}
	else if (CatLower == TEXT("application") || CatLower == TEXT("applicationrequired"))
	{
		UTargetTagRequirementsGameplayEffectComponent& Comp = GE->FindOrAddComponent<UTargetTagRequirementsGameplayEffectComponent>();
		Comp.ApplicationTagRequirements.RequireTags = TagContainer;
	}
	else if (CatLower == TEXT("ongoing") || CatLower == TEXT("ongoingrequired"))
	{
		UTargetTagRequirementsGameplayEffectComponent& Comp = GE->FindOrAddComponent<UTargetTagRequirementsGameplayEffectComponent>();
		Comp.OngoingTagRequirements.RequireTags = TagContainer;
	}
	else if (CatLower == TEXT("removal") || CatLower == TEXT("removaltags"))
	{
		UTargetTagRequirementsGameplayEffectComponent& Comp = GE->FindOrAddComponent<UTargetTagRequirementsGameplayEffectComponent>();
		Comp.RemovalTagRequirements.RequireTags = TagContainer;
	}
	else
	{
		OutError = FString::Printf(TEXT("Invalid tag_category '%s'. Use: granted (tags the effect grants while active — e.g. cooldown tags), asset (the effect's own identity tags), application, ongoing, removal"), *TagCategory);
		return;
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
	UEditorAssetLibrary::SaveAsset(EffectPath, false);

	const FString UnresolvedJson = Unresolved.Num() > 0
		? FString::Printf(TEXT(",\"unresolved_tags\":\"%s\""), *FString::Join(Unresolved, TEXT(", ")))
		: TEXT("");
	const FString WarningJson = Warning.IsEmpty() ? TEXT("") : FString::Printf(TEXT(",\"warning\":\"%s\""), *Warning);
	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"effect_path\":\"%s\",\"tag_category\":\"%s\",\"tag_count\":%d%s%s}"),
		*EffectPath, *TagCategory, TagContainer.Num(), *UnresolvedJson, *WarningJson);
}

static TArray<FString> GASStringArray(const TSharedPtr<FJsonObject>& Args, const TCHAR* Field)
{
	TArray<FString> Out;
	const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
	if (Args->TryGetArrayField(Field, Arr) && Arr)
		for (const TSharedPtr<FJsonValue>& V : *Arr) { FString S; if (V.IsValid() && V->TryGetString(S)) Out.Add(S); }
	return Out;
}
static FString GASEffectPath(const TSharedPtr<FJsonObject>& Args)
{
	FString P; Args->TryGetStringField(TEXT("effect_path"), P);
	if (P.IsEmpty()) Args->TryGetStringField(TEXT("path"), P);
	return P;
}

void HandleSetGameplayEffectChanceFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	const FString EffectPath = GASEffectPath(Args);
	double Chance = 1.0; Args->TryGetNumberField(TEXT("chance"), Chance);
	UBlueprint* BP = nullptr; UGameplayEffect* GE = GASLoadEffectCDO(EffectPath, BP, OutError); if (!GE) return;
	GE->FindOrAddComponent<UChanceToApplyGameplayEffectComponent>().SetChanceToApplyToTarget(FScalableFloat((float)Chance));
	GASFinalizeEffect(BP, EffectPath);
	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"effect_path\":\"%s\",\"chance\":%.3f}"), *EffectPath, Chance);
}

void HandleSetGameplayEffectImmunityFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	const FString EffectPath = GASEffectPath(Args);
	TArray<FString> Tags = GASStringArray(Args, TEXT("immunity_tags")); if (Tags.Num() == 0) Tags = GASStringArray(Args, TEXT("tags"));
	UBlueprint* BP = nullptr; UGameplayEffect* GE = GASLoadEffectCDO(EffectPath, BP, OutError); if (!GE) return;
	TArray<FString> Unresolved; FGameplayTagContainer C = GASBuildTags(Tags, Unresolved);
	UImmunityGameplayEffectComponent& Comp = GE->FindOrAddComponent<UImmunityGameplayEffectComponent>();
	Comp.ImmunityQueries.Empty();
	if (!C.IsEmpty()) Comp.ImmunityQueries.Add(FGameplayEffectQuery::MakeQuery_MatchAnyEffectTags(C));
	GASFinalizeEffect(BP, EffectPath);
	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"effect_path\":\"%s\",\"immunity_tag_count\":%d}"), *EffectPath, C.Num());
}

void HandleSetGameplayEffectRemoveOtherFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	const FString EffectPath = GASEffectPath(Args);
	TArray<FString> Tags = GASStringArray(Args, TEXT("remove_tags")); if (Tags.Num() == 0) Tags = GASStringArray(Args, TEXT("tags"));
	UBlueprint* BP = nullptr; UGameplayEffect* GE = GASLoadEffectCDO(EffectPath, BP, OutError); if (!GE) return;
	TArray<FString> Unresolved; FGameplayTagContainer C = GASBuildTags(Tags, Unresolved);
	URemoveOtherGameplayEffectComponent& Comp = GE->FindOrAddComponent<URemoveOtherGameplayEffectComponent>();
	Comp.RemoveGameplayEffectQueries.Empty();
	if (!C.IsEmpty()) Comp.RemoveGameplayEffectQueries.Add(FGameplayEffectQuery::MakeQuery_MatchAnyEffectTags(C));
	GASFinalizeEffect(BP, EffectPath);
	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"effect_path\":\"%s\",\"remove_tag_count\":%d}"), *EffectPath, C.Num());
}

void HandleSetGameplayEffectCustomApplicationFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	const FString EffectPath = GASEffectPath(Args);
	TArray<FString> Paths = GASStringArray(Args, TEXT("requirement_classes")); if (Paths.Num() == 0) Paths = GASStringArray(Args, TEXT("classes"));
	UBlueprint* BP = nullptr; UGameplayEffect* GE = GASLoadEffectCDO(EffectPath, BP, OutError); if (!GE) return;
	UCustomCanApplyGameplayEffectComponent& Comp = GE->FindOrAddComponent<UCustomCanApplyGameplayEffectComponent>();
	Comp.ApplicationRequirements.Empty();
	TArray<FString> Unresolved; int32 Added = 0;
	for (const FString& P : Paths)
	{
		UClass* Cls = GASResolveClass(P);
		if (Cls && Cls->IsChildOf(UGameplayEffectCustomApplicationRequirement::StaticClass())) { Comp.ApplicationRequirements.Add(Cls); ++Added; }
		else Unresolved.Add(P);
	}
	GASFinalizeEffect(BP, EffectPath);
	const FString U = Unresolved.Num() ? FString::Printf(TEXT(",\"unresolved\":\"%s\""), *FString::Join(Unresolved, TEXT(", "))) : TEXT("");
	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"effect_path\":\"%s\",\"requirement_count\":%d%s}"), *EffectPath, Added, *U);
}

void HandleAddGameplayEffectGrantedAbilityFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	const FString EffectPath = GASEffectPath(Args);
	FString AbilityPath; Args->TryGetStringField(TEXT("ability_path"), AbilityPath);
	double Level = 1.0; Args->TryGetNumberField(TEXT("level"), Level);
	FString Removal; Args->TryGetStringField(TEXT("removal_policy"), Removal);
	UBlueprint* BP = nullptr; UGameplayEffect* GE = GASLoadEffectCDO(EffectPath, BP, OutError); if (!GE) return;
	UClass* AbilCls = GASResolveClass(AbilityPath);
	if (!AbilCls || !AbilCls->IsChildOf(UGameplayAbility::StaticClass())) { OutError = FString::Printf(TEXT("Could not resolve GameplayAbility class '%s'."), *AbilityPath); return; }
	FGameplayAbilitySpecConfig Config;
	Config.Ability = AbilCls;
	Config.LevelScalableFloat = FScalableFloat((float)Level);
	if (Removal.Equals(TEXT("RemoveAbilityOnEnd"), ESearchCase::IgnoreCase)) Config.RemovalPolicy = EGameplayEffectGrantedAbilityRemovePolicy::RemoveAbilityOnEnd;
	else if (Removal.Equals(TEXT("DoNothing"), ESearchCase::IgnoreCase)) Config.RemovalPolicy = EGameplayEffectGrantedAbilityRemovePolicy::DoNothing;
	else Config.RemovalPolicy = EGameplayEffectGrantedAbilityRemovePolicy::CancelAbilityImmediately;
	GE->FindOrAddComponent<UAbilitiesGameplayEffectComponent>().AddGrantedAbilityConfig(Config);
	GASFinalizeEffect(BP, EffectPath);
	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"effect_path\":\"%s\",\"granted_ability\":\"%s\"}"), *EffectPath, *AbilCls->GetName());
}

void HandleAddGameplayEffectConditionalEffectFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	const FString EffectPath = GASEffectPath(Args);
	FString CondPath; Args->TryGetStringField(TEXT("conditional_effect_path"), CondPath);
	if (CondPath.IsEmpty()) Args->TryGetStringField(TEXT("additional_effect_path"), CondPath);
	FString Trigger; Args->TryGetStringField(TEXT("trigger"), Trigger); Trigger = Trigger.ToLower();
	UBlueprint* BP = nullptr; UGameplayEffect* GE = GASLoadEffectCDO(EffectPath, BP, OutError); if (!GE) return;
	UClass* EffCls = GASResolveClass(CondPath);
	if (!EffCls || !EffCls->IsChildOf(UGameplayEffect::StaticClass())) { OutError = FString::Printf(TEXT("Could not resolve GameplayEffect class '%s'."), *CondPath); return; }
	UAdditionalEffectsGameplayEffectComponent& Comp = GE->FindOrAddComponent<UAdditionalEffectsGameplayEffectComponent>();
	if (Trigger == TEXT("on_complete_always")) Comp.OnCompleteAlways.Add(EffCls);
	else if (Trigger == TEXT("on_complete_normal")) Comp.OnCompleteNormal.Add(EffCls);
	else if (Trigger == TEXT("on_complete_prematurely")) Comp.OnCompletePrematurely.Add(EffCls);
	else
	{
		FConditionalGameplayEffect CE;
		CE.EffectClass = EffCls;
		TArray<FString> SrcTags = GASStringArray(Args, TEXT("required_source_tags"));
		TArray<FString> U; CE.RequiredSourceTags = GASBuildTags(SrcTags, U);
		Comp.OnApplicationGameplayEffects.Add(CE);
		Trigger = TEXT("on_application");
	}
	GASFinalizeEffect(BP, EffectPath);
	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"effect_path\":\"%s\",\"added_effect\":\"%s\",\"trigger\":\"%s\"}"), *EffectPath, *EffCls->GetName(), *Trigger);
}

void HandleSetGameplayEffectBlockAbilityTagsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	const FString EffectPath = GASEffectPath(Args);
	TArray<FString> Tags = GASStringArray(Args, TEXT("tags"));
	UBlueprint* BP = nullptr; UGameplayEffect* GE = GASLoadEffectCDO(EffectPath, BP, OutError); if (!GE) return;
	TArray<FString> U; FInheritedTagContainer ITC; ITC.Added = GASBuildTags(Tags, U);
	GE->FindOrAddComponent<UBlockAbilityTagsGameplayEffectComponent>().SetAndApplyBlockedAbilityTagChanges(ITC);
	GASFinalizeEffect(BP, EffectPath);
	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"effect_path\":\"%s\",\"block_tag_count\":%d}"), *EffectPath, ITC.Added.Num());
}

void HandleSetGameplayEffectCancelAbilityTagsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	const FString EffectPath = GASEffectPath(Args);
	TArray<FString> WithTags = GASStringArray(Args, TEXT("tags")); if (WithTags.Num() == 0) WithTags = GASStringArray(Args, TEXT("with_tags"));
	TArray<FString> WithoutTags = GASStringArray(Args, TEXT("without_tags"));
	UBlueprint* BP = nullptr; UGameplayEffect* GE = GASLoadEffectCDO(EffectPath, BP, OutError); if (!GE) return;
#if !UE_VERSION_OLDER_THAN(5, 7, 0)
	TArray<FString> U1, U2;
	FInheritedTagContainer With; With.Added = GASBuildTags(WithTags, U1);
	FInheritedTagContainer Without; Without.Added = GASBuildTags(WithoutTags, U2);
	GE->FindOrAddComponent<UCancelAbilityTagsGameplayEffectComponent>().SetAndApplyCanceledAbilityTagChanges(With, Without);
	GASFinalizeEffect(BP, EffectPath);
	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"effect_path\":\"%s\",\"cancel_with_count\":%d,\"cancel_without_count\":%d}"), *EffectPath, With.Added.Num(), Without.Added.Num());
#else
	(void)WithTags; (void)WithoutTags;
	OutError = TEXT("set_gameplay_effect_cancel_ability_tags requires UE 5.7+ (CancelAbilityTagsGameplayEffectComponent was added in 5.7). On 5.4-5.6 use set_gameplay_effect_block_ability_tags.");
#endif
}

void HandleAddGameplayEffectCueFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	const FString EffectPath = GASEffectPath(Args);
	FString CueTag; Args->TryGetStringField(TEXT("cue_tag"), CueTag);
	double MinLevel = 0.0, MaxLevel = 0.0; Args->TryGetNumberField(TEXT("min_level"), MinLevel); Args->TryGetNumberField(TEXT("max_level"), MaxLevel);
	UBlueprint* BP = nullptr; UGameplayEffect* GE = GASLoadEffectCDO(EffectPath, BP, OutError); if (!GE) return;
	TArray<FString> U; FGameplayTagContainer CueTags = GASBuildTags({ CueTag }, U);
	if (CueTags.IsEmpty()) { OutError = FString::Printf(TEXT("Could not resolve or register cue tag '%s'."), *CueTag); return; }
	FGameplayEffectCue Cue;
	Cue.GameplayCueTags = CueTags;
	Cue.MinLevel = (float)MinLevel;
	Cue.MaxLevel = (float)MaxLevel;
	GE->GameplayCues.Add(Cue);
	GASFinalizeEffect(BP, EffectPath);
	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"effect_path\":\"%s\",\"cue_tag\":\"%s\",\"cue_count\":%d}"), *EffectPath, *CueTag, GE->GameplayCues.Num());
}

void HandleAddGameplayEffectExecutionFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	const FString EffectPath = GASEffectPath(Args);
	FString CalcPath; Args->TryGetStringField(TEXT("calculation_class"), CalcPath);
	if (CalcPath.IsEmpty()) Args->TryGetStringField(TEXT("execution_class"), CalcPath);
	UBlueprint* BP = nullptr; UGameplayEffect* GE = GASLoadEffectCDO(EffectPath, BP, OutError); if (!GE) return;
	UClass* CalcCls = GASResolveClass(CalcPath);
	if (!CalcCls || !CalcCls->IsChildOf(UGameplayEffectExecutionCalculation::StaticClass())) { OutError = FString::Printf(TEXT("Could not resolve GameplayEffectExecutionCalculation class '%s'."), *CalcPath); return; }
	FGameplayEffectExecutionDefinition Exec;
	Exec.CalculationClass = CalcCls;
	GE->Executions.Add(Exec);
	GASFinalizeEffect(BP, EffectPath);
	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"effect_path\":\"%s\",\"execution_class\":\"%s\",\"execution_count\":%d}"), *EffectPath, *CalcCls->GetName(), GE->Executions.Num());
}

void HandleSetGameplayEffectOverflowFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	const FString EffectPath = GASEffectPath(Args);
	UBlueprint* BP = nullptr; UGameplayEffect* GE = GASLoadEffectCDO(EffectPath, BP, OutError); if (!GE) return;
	bool bDeny = false; if (Args->TryGetBoolField(TEXT("deny_overflow_application"), bDeny)) GE->bDenyOverflowApplication = bDeny;
	TArray<FString> U;
	const TArray<FString> Overflow = GASStringArray(Args, TEXT("overflow_effects"));
	if (Overflow.Num() > 0) { GE->OverflowEffects.Empty(); for (const FString& P : Overflow) { if (UClass* C = GASResolveClass(P)) GE->OverflowEffects.Add(C); else U.Add(P); } }
	GASFinalizeEffect(BP, EffectPath);
	const FString UJ = U.Num() ? FString::Printf(TEXT(",\"unresolved\":\"%s\""), *FString::Join(U, TEXT(", "))) : TEXT("");
	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"effect_path\":\"%s\",\"overflow_effects\":%d,\"deny_overflow_application\":%s%s}"),
		*EffectPath, GE->OverflowEffects.Num(), GE->bDenyOverflowApplication ? TEXT("true") : TEXT("false"), *UJ);
}

void HandleSetModifierTagRequirementsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	const FString EffectPath = GASEffectPath(Args);
	UBlueprint* BP = nullptr; UGameplayEffect* GE = GASLoadEffectCDO(EffectPath, BP, OutError); if (!GE) return;

	int32 Idx = -1; double IdxD = -1.0;
	if (Args->TryGetNumberField(TEXT("modifier_index"), IdxD)) Idx = (int32)IdxD;
	FString AttrName; Args->TryGetStringField(TEXT("attribute_name"), AttrName);
	if (Idx < 0 && !AttrName.IsEmpty())
		for (int32 i = 0; i < GE->Modifiers.Num(); ++i)
			if (GE->Modifiers[i].Attribute.GetName().Equals(AttrName, ESearchCase::IgnoreCase) || GE->Modifiers[i].Attribute.GetName().Contains(AttrName)) { Idx = i; break; }
	if (Idx < 0 || Idx >= GE->Modifiers.Num()) { OutError = FString::Printf(TEXT("Modifier not found (modifier_index or attribute_name). The effect has %d modifier(s)."), GE->Modifiers.Num()); return; }

	UScriptStruct* MS = FGameplayModifierInfo::StaticStruct();
	auto SetReq = [&](const TCHAR* FieldName, const TCHAR* ReqArg, const TCHAR* IgnArg)
	{
		FStructProperty* SP = FindFProperty<FStructProperty>(MS, FieldName);
		if (!SP) return;
		FGameplayTagRequirements* R = SP->ContainerPtrToValuePtr<FGameplayTagRequirements>(&GE->Modifiers[Idx]);
		if (!R) return;
		TArray<FString> U;
		if (Args->HasField(ReqArg)) R->RequireTags = GASBuildTags(GASStringArray(Args, ReqArg), U);
		if (Args->HasField(IgnArg)) R->IgnoreTags = GASBuildTags(GASStringArray(Args, IgnArg), U);
	};
	SetReq(TEXT("SourceTags"), TEXT("source_required_tags"), TEXT("source_ignored_tags"));
	SetReq(TEXT("TargetTags"), TEXT("target_required_tags"), TEXT("target_ignored_tags"));

	GASFinalizeEffect(BP, EffectPath);
	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"effect_path\":\"%s\",\"modifier_index\":%d}"), *EffectPath, Idx);
}

void HandleAddExecutionConditionalEffectFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	const FString EffectPath = GASEffectPath(Args);
	double ExecIdxD = 0.0; Args->TryGetNumberField(TEXT("execution_index"), ExecIdxD);
	const int32 ExecIdx = (int32)ExecIdxD;
	FString CondPath; Args->TryGetStringField(TEXT("conditional_effect_path"), CondPath);
	UBlueprint* BP = nullptr; UGameplayEffect* GE = GASLoadEffectCDO(EffectPath, BP, OutError); if (!GE) return;
	if (ExecIdx < 0 || ExecIdx >= GE->Executions.Num()) { OutError = FString::Printf(TEXT("execution_index %d out of range (the effect has %d execution(s); add one with add_gameplay_effect_execution)."), ExecIdx, GE->Executions.Num()); return; }
	UClass* EffCls = GASResolveClass(CondPath);
	if (!EffCls || !EffCls->IsChildOf(UGameplayEffect::StaticClass())) { OutError = FString::Printf(TEXT("Could not resolve GameplayEffect class '%s'."), *CondPath); return; }

	FArrayProperty* AP = FindFProperty<FArrayProperty>(FGameplayEffectExecutionDefinition::StaticStruct(), TEXT("ConditionalGameplayEffects"));
	if (!AP) { OutError = TEXT("ConditionalGameplayEffects property not found on this engine version."); return; }
	FScriptArrayHelper H(AP, AP->ContainerPtrToValuePtr<void>(&GE->Executions[ExecIdx]));
	const int32 NewIdx = H.AddValue();
	FConditionalGameplayEffect* CE = reinterpret_cast<FConditionalGameplayEffect*>(H.GetRawPtr(NewIdx));
	CE->EffectClass = EffCls;
	TArray<FString> U; CE->RequiredSourceTags = GASBuildTags(GASStringArray(Args, TEXT("required_source_tags")), U);

	GASFinalizeEffect(BP, EffectPath);
	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"effect_path\":\"%s\",\"execution_index\":%d,\"conditional_effect\":\"%s\"}"), *EffectPath, ExecIdx, *EffCls->GetName());
}

void HandleAddGameplayAttributeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString BlueprintPath;
	Args->TryGetStringField(TEXT("blueprint_path"), BlueprintPath);

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("attributes"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString AttrName = BatchToolHelper::GetItemString(Item, TEXT("attribute_name"), TEXT("name"));
			double DefaultValue = 100.0; Item->TryGetNumberField(TEXT("default_value"), DefaultValue);
			if (AttrName.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing attribute_name")); continue; }
			FString ItemOut, ItemErr;
			HandleAddGameplayAttribute(BlueprintPath, AttrName, (float)DefaultValue, ItemOut, ItemErr);
			if (ItemErr.IsEmpty()) { auto E = MakeShared<FJsonObject>(); E->SetStringField(TEXT("attribute_name"), AttrName); Batch.AddSuccess(i, E); }
			else Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJsonString);
		return;
	}
	FString AttributeName; double DefaultValue = 100.0;
	Args->TryGetStringField(TEXT("attribute_name"), AttributeName);
	Args->TryGetNumberField(TEXT("default_value"), DefaultValue);
	HandleAddGameplayAttribute(BlueprintPath, AttributeName, (float)DefaultValue, OutJsonString, OutError);
}

void HandleAddGameplayEffectModifierFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString EffectPath;
	Args->TryGetStringField(TEXT("effect_path"), EffectPath);

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("modifiers"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString Attr = BatchToolHelper::GetItemString(Item, TEXT("attribute_name"), TEXT("attribute"));
			FString ModOp = BatchToolHelper::GetItemString(Item, TEXT("modifier_op"), TEXT("op"));
			double Magnitude = 0.0; Item->TryGetNumberField(TEXT("magnitude"), Magnitude);
			if (Attr.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing attribute_name")); continue; }
			FString ItemOut, ItemErr;
			HandleAddGameplayEffectModifier(EffectPath, Attr, ModOp, (float)Magnitude, ItemOut, ItemErr);
			if (ItemErr.IsEmpty()) { auto E = MakeShared<FJsonObject>(); E->SetStringField(TEXT("attribute"), Attr); Batch.AddSuccess(i, E); }
			else Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJsonString);
		return;
	}
	FString AttributeName, ModifierOp; double Magnitude = 0.0;
	Args->TryGetStringField(TEXT("attribute_name"), AttributeName);
	Args->TryGetStringField(TEXT("modifier_op"), ModifierOp);
	Args->TryGetNumberField(TEXT("magnitude"), Magnitude);
	HandleAddGameplayEffectModifier(EffectPath, AttributeName, ModifierOp, (float)Magnitude, OutJsonString, OutError);
}

void HandleCreateGameplayEffectFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!FProjectStateCache::Get().IsScanContextReady()) { OutError = TEXT("Ability system not ready — reload the editor and try again."); return; }
	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("effects"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString Name = BatchToolHelper::GetItemString(Item, TEXT("asset_name"), TEXT("name"));
			FString SavePath = BatchToolHelper::GetItemString(Item, TEXT("save_path"));
			FString DurPolicy = BatchToolHelper::GetItemString(Item, TEXT("duration_policy"));
			if (SavePath.IsEmpty()) Args->TryGetStringField(TEXT("save_path"), SavePath);
			if (DurPolicy.IsEmpty()) DurPolicy = TEXT("instant");
			if (Name.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing asset_name")); continue; }
			FString ItemOut, ItemErr;
			HandleCreateGameplayEffect(Name, SavePath, DurPolicy, ItemOut, ItemErr);
			if (ItemErr.IsEmpty()) { auto E = MakeShared<FJsonObject>(); E->SetStringField(TEXT("asset_name"), Name); Batch.AddSuccess(i, E); }
			else Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJsonString);
		return;
	}
	FString AssetName = TEXT("GE_NewEffect"), SavePath = TEXT("/Game/GAS"), DurationPolicy = TEXT("instant");
	if (!Args->TryGetStringField(TEXT("asset_name"), AssetName) || AssetName.IsEmpty())
		Args->TryGetStringField(TEXT("name"), AssetName);
	Args->TryGetStringField(TEXT("save_path"), SavePath);
	Args->TryGetStringField(TEXT("duration_policy"), DurationPolicy);
	HandleCreateGameplayEffect(AssetName, SavePath, DurationPolicy, OutJsonString, OutError);
}

static TArray<FString> ExtractStringArrayField(const TSharedPtr<FJsonObject>& Args, const FString& Key)
{
	TArray<FString> Out;
	const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
	if (Args->TryGetArrayField(Key, Arr) && Arr)
	{
		for (const TSharedPtr<FJsonValue>& V : *Arr)
		{ FString S; if (V->TryGetString(S)) Out.Add(S); }
	}
	return Out;
}

void HandleCreateAttributeSetFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString AssetName = TEXT("AS_MyAttributeSet"), SavePath;
	if (!Args->TryGetStringField(TEXT("asset_name"), AssetName) || AssetName.IsEmpty())
		Args->TryGetStringField(TEXT("name"), AssetName);
	Args->TryGetStringField(TEXT("save_path"), SavePath);
	if (SavePath.IsEmpty()) SavePath = FSettingsManager::GetDefaultSavePath();
	const TArray<FString> AttrNames = ExtractStringArrayField(Args, TEXT("attribute_names"));
	HandleCreateAttributeSet(AssetName, SavePath, AttrNames, OutJsonString, OutError);
}

void HandleCreateGameplayAbilityFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString AssetName = TEXT("GA_NewAbility"), SavePath, AbilityTag;
	if (!Args->TryGetStringField(TEXT("asset_name"), AssetName) || AssetName.IsEmpty())
		Args->TryGetStringField(TEXT("name"), AssetName);
	Args->TryGetStringField(TEXT("save_path"), SavePath);
	if (SavePath.IsEmpty()) SavePath = FSettingsManager::GetDefaultSavePath();
	Args->TryGetStringField(TEXT("ability_tag"), AbilityTag);
	HandleCreateGameplayAbility(AssetName, SavePath, AbilityTag, OutJsonString, OutError);
}

void HandleAddAbilitySystemComponentFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString BlueprintPath, ComponentName;
	Args->TryGetStringField(TEXT("blueprint_path"), BlueprintPath);
	Args->TryGetStringField(TEXT("component_name"), ComponentName);
	HandleAddAbilitySystemComponent(BlueprintPath, ComponentName, OutJsonString, OutError);
}

static int32 GASSetAbilityTagProp(UGameplayAbility* CDO, const TCHAR* PropName, const TArray<FString>& Tags)
{
	FStructProperty* Prop = FindFProperty<FStructProperty>(UGameplayAbility::StaticClass(), PropName);
	if (!Prop) return -1;
	FGameplayTagContainer* C = Prop->ContainerPtrToValuePtr<FGameplayTagContainer>(CDO);
	if (!C) return -1;
	TArray<FString> Unresolved;
	*C = GASBuildTags(Tags, Unresolved);
	return C->Num();
}

void HandleSetAbilityTagsFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString BlueprintPath;
	if (!Args->TryGetStringField(TEXT("blueprint_path"), BlueprintPath) || BlueprintPath.IsEmpty())
		if (!Args->TryGetStringField(TEXT("ability_path"), BlueprintPath) || BlueprintPath.IsEmpty())
			Args->TryGetStringField(TEXT("asset_path"), BlueprintPath);

	UBlueprint* BP = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(BlueprintPath));
	if (!BP || !BP->GeneratedClass) { OutError = TEXT("Could not load GameplayAbility Blueprint: ") + BlueprintPath; return; }
	UGameplayAbility* CDO = Cast<UGameplayAbility>(BP->GeneratedClass->GetDefaultObject());
	if (!CDO) { OutError = TEXT("Asset is not a GameplayAbility: ") + BlueprintPath; return; }
	CDO->Modify();

	TSharedRef<FJsonObject> Res = MakeShared<FJsonObject>();
	Res->SetBoolField(TEXT("success"), true);
	Res->SetStringField(TEXT("blueprint_path"), BlueprintPath);
	bool bAny = false;
	const TPair<const TCHAR*, const TCHAR*> Map[] = {
		{ TEXT("ability_tags"),            TEXT("AbilityTags") },
		{ TEXT("block_tags"),              TEXT("BlockAbilitiesWithTag") },
		{ TEXT("cancel_tags"),             TEXT("CancelAbilitiesWithTag") },
		{ TEXT("activation_owned_tags"),   TEXT("ActivationOwnedTags") },
		{ TEXT("activation_required_tags"),TEXT("ActivationRequiredTags") },
		{ TEXT("activation_blocked_tags"), TEXT("ActivationBlockedTags") },
		{ TEXT("source_required_tags"),    TEXT("SourceRequiredTags") },
		{ TEXT("source_blocked_tags"),     TEXT("SourceBlockedTags") },
		{ TEXT("target_required_tags"),    TEXT("TargetRequiredTags") },
		{ TEXT("target_blocked_tags"),     TEXT("TargetBlockedTags") },
	};
	for (const TPair<const TCHAR*, const TCHAR*>& E : Map)
	{
		if (!Args->HasField(E.Key)) continue;
		const int32 N = GASSetAbilityTagProp(CDO, E.Value, ExtractStringArrayField(Args, E.Key));
		if (N >= 0) { Res->SetNumberField(E.Key, N); bAny = true; }
	}
	if (!bAny)
	{
		OutError = TEXT("No recognised tag arrays provided. Use: ability_tags, block_tags, cancel_tags, activation_owned_tags, activation_required_tags, activation_blocked_tags, source_required_tags, source_blocked_tags, target_required_tags, target_blocked_tags.");
		return;
	}

	FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
	BP->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(BlueprintPath, false);

	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Res, W);
}

void HandleGetAbilitySummaryFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString BlueprintPath;
	if (!Args->TryGetStringField(TEXT("blueprint_path"), BlueprintPath) || BlueprintPath.IsEmpty())
		if (!Args->TryGetStringField(TEXT("ability_path"), BlueprintPath) || BlueprintPath.IsEmpty())
			Args->TryGetStringField(TEXT("path"), BlueprintPath);

	UBlueprint* BP = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(BlueprintPath));
	if (!BP || !BP->GeneratedClass) { OutError = TEXT("Could not load GameplayAbility Blueprint: ") + BlueprintPath; return; }
	UGameplayAbility* CDO = Cast<UGameplayAbility>(BP->GeneratedClass->GetDefaultObject());
	if (!CDO) { OutError = TEXT("Asset is not a GameplayAbility: ") + BlueprintPath; return; }

	UClass* Cls = UGameplayAbility::StaticClass();
	TSharedRef<FJsonObject> Res = MakeShared<FJsonObject>();
	Res->SetBoolField(TEXT("success"), true);
	Res->SetStringField(TEXT("blueprint_path"), BlueprintPath);
	Res->SetStringField(TEXT("class_name"), BP->GeneratedClass->GetName());

	auto ReadTags = [&](const TCHAR* PropName, const TCHAR* Key)
	{
		if (FStructProperty* P = FindFProperty<FStructProperty>(Cls, PropName))
		{
			const FGameplayTagContainer* C = P->ContainerPtrToValuePtr<FGameplayTagContainer>(CDO);
			TArray<FGameplayTag> L; if (C) C->GetGameplayTagArray(L);
			if (L.Num()) { TArray<TSharedPtr<FJsonValue>> A; for (const FGameplayTag& T : L) A.Add(MakeShareable(new FJsonValueString(T.ToString()))); Res->SetArrayField(Key, A); }
		}
	};
	ReadTags(TEXT("AbilityTags"), TEXT("ability_tags"));
	ReadTags(TEXT("BlockAbilitiesWithTag"), TEXT("block_tags"));
	ReadTags(TEXT("CancelAbilitiesWithTag"), TEXT("cancel_tags"));
	ReadTags(TEXT("ActivationOwnedTags"), TEXT("activation_owned_tags"));
	ReadTags(TEXT("ActivationRequiredTags"), TEXT("activation_required_tags"));
	ReadTags(TEXT("ActivationBlockedTags"), TEXT("activation_blocked_tags"));
	ReadTags(TEXT("SourceRequiredTags"), TEXT("source_required_tags"));
	ReadTags(TEXT("SourceBlockedTags"), TEXT("source_blocked_tags"));
	ReadTags(TEXT("TargetRequiredTags"), TEXT("target_required_tags"));
	ReadTags(TEXT("TargetBlockedTags"), TEXT("target_blocked_tags"));

	auto ReadClass = [&](const TCHAR* PropName, const TCHAR* Key)
	{
		if (FClassProperty* CP = FindFProperty<FClassProperty>(Cls, PropName))
			if (UObject* O = CP->GetObjectPropertyValue(CP->ContainerPtrToValuePtr<void>(CDO)))
				Res->SetStringField(Key, O->GetName());
	};
	ReadClass(TEXT("CooldownGameplayEffectClass"), TEXT("cooldown_effect"));
	ReadClass(TEXT("CostGameplayEffectClass"), TEXT("cost_effect"));

	auto ReadEnum = [&](const TCHAR* PropName, const TCHAR* Key)
	{
		if (FByteProperty* BPr = FindFProperty<FByteProperty>(Cls, PropName))
		{
			const uint8 V = BPr->GetPropertyValue(BPr->ContainerPtrToValuePtr<void>(CDO));
			Res->SetStringField(Key, BPr->Enum ? BPr->Enum->GetNameStringByValue((int64)V) : FString::FromInt(V));
		}
		else if (FEnumProperty* EP = FindFProperty<FEnumProperty>(Cls, PropName))
		{
			void* Ptr = EP->ContainerPtrToValuePtr<void>(CDO);
			const int64 V = EP->GetUnderlyingProperty()->GetSignedIntPropertyValue(Ptr);
			Res->SetStringField(Key, EP->GetEnum() ? EP->GetEnum()->GetNameStringByValue(V) : FString::FromInt((int32)V));
		}
	};
	ReadEnum(TEXT("InstancingPolicy"), TEXT("instancing_policy"));
	ReadEnum(TEXT("NetExecutionPolicy"), TEXT("net_execution_policy"));
	ReadEnum(TEXT("NetSecurityPolicy"), TEXT("net_security_policy"));
	ReadEnum(TEXT("ReplicationPolicy"), TEXT("replication_policy"));

	if (FArrayProperty* TP = FindFProperty<FArrayProperty>(Cls, TEXT("AbilityTriggers")))
	{
		FScriptArrayHelper H(TP, TP->ContainerPtrToValuePtr<void>(CDO));
		Res->SetNumberField(TEXT("trigger_count"), H.Num());
	}

	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Res, W);
}

void HandleCreateGameplayCueFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString AssetName = TEXT("GC_NewCue"), SavePath, CueTag;
	if (!Args->TryGetStringField(TEXT("asset_name"), AssetName) || AssetName.IsEmpty())
		Args->TryGetStringField(TEXT("name"), AssetName);
	Args->TryGetStringField(TEXT("save_path"), SavePath);
	if (SavePath.IsEmpty()) SavePath = FSettingsManager::GetDefaultSavePath();
	Args->TryGetStringField(TEXT("cue_tag"), CueTag);
	HandleCreateGameplayCue(AssetName, SavePath, CueTag, OutJsonString, OutError);
}

void HandleSetGameplayEffectDurationFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString EffectPath, DurationPolicy;
	double DurationSeconds = 0.0;
	Args->TryGetStringField(TEXT("effect_path"), EffectPath);
	Args->TryGetStringField(TEXT("duration_policy"), DurationPolicy);
	Args->TryGetNumberField(TEXT("duration_seconds"), DurationSeconds);
	HandleSetGameplayEffectDuration(EffectPath, DurationPolicy, (float)DurationSeconds, OutJsonString, OutError);
}

void HandleGrantAbilityToBlueprintFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString BlueprintPath, AbilityPath;
	Args->TryGetStringField(TEXT("blueprint_path"), BlueprintPath);
	Args->TryGetStringField(TEXT("ability_path"), AbilityPath);
	HandleGrantAbilityToBlueprint(BlueprintPath, AbilityPath, OutJsonString, OutError);
}

void HandleSetGameplayEffectStackingFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString EP, ST, DRP, PRP, SEP;
	int32 SLC = 0;
	Args->TryGetStringField(TEXT("effect_path"), EP);
	Args->TryGetStringField(TEXT("stacking_type"), ST);
	Args->TryGetNumberField(TEXT("stack_limit_count"), SLC);
	Args->TryGetStringField(TEXT("duration_refresh_policy"), DRP);
	Args->TryGetStringField(TEXT("period_reset_policy"), PRP);
	Args->TryGetStringField(TEXT("expiration_policy"), SEP);
	HandleSetGameplayEffectStacking(EP, ST, SLC, DRP, PRP, SEP, OutJsonString, OutError);
}

void HandleSetGameplayEffectPeriodFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString EP;
	double Period = 0;
	bool bExec = false;
	Args->TryGetStringField(TEXT("effect_path"), EP);
	Args->TryGetNumberField(TEXT("period_seconds"), Period);
	if (Period == 0) Args->TryGetNumberField(TEXT("period"), Period);
	Args->TryGetBoolField(TEXT("execute_on_application"), bExec);
	HandleSetGameplayEffectPeriod(EP, (float)Period, bExec, OutJsonString, OutError);
}

void HandleSetAbilityCostFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString AP, CP;
	Args->TryGetStringField(TEXT("ability_path"), AP);
	if (AP.IsEmpty()) Args->TryGetStringField(TEXT("blueprint_path"), AP);
	Args->TryGetStringField(TEXT("cost_effect_path"), CP);
	HandleSetAbilityCost(AP, CP, OutJsonString, OutError);
}

void HandleSetAbilityCooldownFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString AP, CP;
	Args->TryGetStringField(TEXT("ability_path"), AP);
	if (AP.IsEmpty()) Args->TryGetStringField(TEXT("blueprint_path"), AP);
	Args->TryGetStringField(TEXT("cooldown_effect_path"), CP);
	HandleSetAbilityCooldown(AP, CP, OutJsonString, OutError);
}

void HandleGetGameplayEffectSummaryFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString EP;
	Args->TryGetStringField(TEXT("effect_path"), EP);
	if (EP.IsEmpty()) Args->TryGetStringField(TEXT("blueprint_path"), EP);
	if (EP.IsEmpty()) Args->TryGetStringField(TEXT("asset_path"), EP);
	if (EP.IsEmpty()) Args->TryGetStringField(TEXT("path"), EP);
	if (EP.IsEmpty()) { OutError = TEXT("Missing effect_path (or blueprint_path / asset_path / path)"); return; }
	HandleGetGameplayEffectSummary(EP, OutJsonString, OutError);
}

void HandleGetAttributeSetSummaryFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString AP;
	Args->TryGetStringField(TEXT("attribute_set_path"), AP);
	if (AP.IsEmpty()) Args->TryGetStringField(TEXT("blueprint_path"), AP);
	if (AP.IsEmpty()) Args->TryGetStringField(TEXT("asset_path"), AP);
	if (AP.IsEmpty()) Args->TryGetStringField(TEXT("path"), AP);
	if (AP.IsEmpty()) { OutError = TEXT("Missing attribute_set_path (or blueprint_path / asset_path / path)"); return; }
	HandleGetAttributeSetSummary(AP, OutJsonString, OutError);
}

void HandleRemoveGameplayEffectModifierFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString EP, AN;
	Args->TryGetStringField(TEXT("effect_path"), EP);
	Args->TryGetStringField(TEXT("attribute_name"), AN);
	int32 MI = INDEX_NONE;
	double MId = 0;
	if (Args->TryGetNumberField(TEXT("modifier_index"), MId)) MI = (int32)MId;
	HandleRemoveGameplayEffectModifier(EP, AN, MI, OutJsonString, OutError);
}

void HandleListGameplayAbilitiesFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString BP;
	Args->TryGetStringField(TEXT("blueprint_path"), BP);
	if (BP.IsEmpty()) Args->TryGetStringField(TEXT("asset_path"), BP);
	HandleListGameplayAbilities(BP, OutJsonString, OutError);
}

void HandleSetGameplayEffectTagsFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString EP, TC;
	Args->TryGetStringField(TEXT("effect_path"), EP);
	Args->TryGetStringField(TEXT("tag_category"), TC);
	if (EP.IsEmpty()) Args->TryGetStringField(TEXT("asset_path"), EP);
	const TArray<FString> Tags = ExtractStringArrayField(Args, TEXT("tags"));
	HandleSetGameplayEffectTags(EP, TC, Tags, OutJsonString, OutError);
}

namespace
{
	FProperty* FindGameplayAttributeProperty(const FString& AttributeName)
	{
		FString TargetClassName, BareName = AttributeName;
		int32 DotIdx;
		if (AttributeName.FindChar(TEXT('.'), DotIdx))
		{
			TargetClassName = AttributeName.Left(DotIdx);
			BareName        = AttributeName.Mid(DotIdx + 1);
		}

		if (!TargetClassName.IsEmpty())
		{
			TArray<FAssetData> Found;
			const FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
			FARFilter F; F.bRecursivePaths = true;
			F.PackagePaths.Add(FName(TEXT("/Game")));
			F.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());
			ARM.Get().GetAssets(F, Found);
			for (const FAssetData& AD : Found)
			{
				if (AD.AssetName.ToString().Equals(TargetClassName, ESearchCase::IgnoreCase))
				{
					if (UBlueprint* ASBp = Cast<UBlueprint>(AD.GetAsset()))
					{
						if (ASBp->Status == BS_Dirty || ASBp->Status == BS_Unknown || !ASBp->GeneratedClass)
							FKismetEditorUtilities::CompileBlueprint(ASBp);
					}
					break;
				}
			}
		}

		for (TObjectIterator<UClass> It; It; ++It)
		{
			if (!It->IsChildOf(UAttributeSet::StaticClass()) || It->HasAnyClassFlags(CLASS_Abstract)) continue;
			if (!TargetClassName.IsEmpty())
			{
				FString ClsName = It->GetName();
				if (ClsName.EndsWith(TEXT("_C"))) ClsName = ClsName.LeftChop(2);
				if (!ClsName.Equals(TargetClassName, ESearchCase::IgnoreCase)) continue;
			}
			FProperty* P = FindFProperty<FProperty>(*It, *BareName);
			if (!P)
				for (TFieldIterator<FProperty> FIt(*It); FIt; ++FIt)
					if (FIt->GetName().Equals(BareName, ESearchCase::IgnoreCase)) { P = *FIt; break; }
			if (P) return P;
		}
		return nullptr;
	}

	FGameplayModifierInfo* LocateModifier(UGameplayEffect* GE, int32 Index, const FString& AttributeName, FString& OutError)
	{
		if (!GE) { OutError = TEXT("Effect CDO is null"); return nullptr; }
		if (Index >= 0 && Index < GE->Modifiers.Num()) return &GE->Modifiers[Index];

		if (!AttributeName.IsEmpty())
		{
			FProperty* AttrProp = FindGameplayAttributeProperty(AttributeName);
			if (!AttrProp) { OutError = FString::Printf(TEXT("Attribute '%s' not found."), *AttributeName); return nullptr; }
			for (FGameplayModifierInfo& M : GE->Modifiers)
			{
				if (M.Attribute.GetUProperty() == AttrProp) return &M;
			}
			OutError = FString::Printf(TEXT("No modifier on this effect targets attribute '%s'."), *AttributeName);
			return nullptr;
		}

		OutError = FString::Printf(TEXT("modifier_index %d out of range (effect has %d modifier(s)) and no attribute_name fallback provided."),
			Index, GE->Modifiers.Num());
		return nullptr;
	}

	template <typename PropClass, typename ValueT>
	bool SetStructField(UScriptStruct* S, void* StructPtr, const TCHAR* FieldName, const ValueT& Value)
	{
		if (PropClass* P = FindFProperty<PropClass>(S, FieldName))
		{
			P->SetPropertyValue(P->template ContainerPtrToValuePtr<void>(StructPtr), Value);
			return true;
		}
		return false;
	}
}

void HandleSetModifierMagnitudeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }

	FString EffectPath, MagType, AttributeName;
	double ModifierIndex = -1;
	Args->TryGetStringField(TEXT("effect_path"),     EffectPath);
	Args->TryGetNumberField(TEXT("modifier_index"),  ModifierIndex);
	Args->TryGetStringField(TEXT("attribute_name"),  AttributeName);
	Args->TryGetStringField(TEXT("magnitude_type"),  MagType);

	if (MagType.IsEmpty()) { OutError = TEXT("`magnitude_type` is required (\"scalable_float\"|\"set_by_caller\"|\"attribute_based\"|\"custom\")."); return; }

	UBlueprint* BP = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(EffectPath));
	if (!BP || !BP->GeneratedClass) { OutError = TEXT("Could not load GameplayEffect Blueprint: ") + EffectPath; return; }
	UGameplayEffect* GE = Cast<UGameplayEffect>(BP->GeneratedClass->GetDefaultObject());
	if (!GE) { OutError = TEXT("Asset is not a GameplayEffect: ") + EffectPath; return; }

	FGameplayModifierInfo* Mod = LocateModifier(GE, (int32)ModifierIndex, AttributeName, OutError);
	if (!Mod) return;

	UScriptStruct* MagStruct = FGameplayEffectModifierMagnitude::StaticStruct();
	void* MagPtr = &Mod->ModifierMagnitude;

	const FString MType = MagType.ToLower();
	int64 CalcEnumValue = 0;
	FString CanonicalType;

	if (MType == TEXT("scalable_float") || MType == TEXT("scalablefloat") || MType == TEXT("constant"))
	{
		CalcEnumValue = (int64)EGameplayEffectMagnitudeCalculation::ScalableFloat;
		CanonicalType = TEXT("ScalableFloat");

		double Value = 0.0;
		Args->TryGetNumberField(TEXT("value"), Value);

		if (FStructProperty* SF = FindFProperty<FStructProperty>(MagStruct, TEXT("ScalableFloatMagnitude")))
		{
			void* SFPtr = SF->ContainerPtrToValuePtr<void>(MagPtr);
			SetStructField<FFloatProperty>(SF->Struct, SFPtr, TEXT("Value"), (float)Value);

			FString CurvePath, RowName;
			if (Args->TryGetStringField(TEXT("curve_table"), CurvePath) && !CurvePath.IsEmpty())
			{
				Args->TryGetStringField(TEXT("curve_row_name"), RowName);
				if (FStructProperty* CurveSP = FindFProperty<FStructProperty>(SF->Struct, TEXT("Curve")))
				{
					void* CurvePtr = CurveSP->ContainerPtrToValuePtr<void>(SFPtr);
					if (FObjectProperty* CTP = FindFProperty<FObjectProperty>(CurveSP->Struct, TEXT("CurveTable")))
					{
						UObject* CT = UEditorAssetLibrary::LoadAsset(CurvePath);
						CTP->SetObjectPropertyValue(CTP->ContainerPtrToValuePtr<void>(CurvePtr), CT);
					}
					if (!RowName.IsEmpty())
					{
						SetStructField<FNameProperty>(CurveSP->Struct, CurvePtr, TEXT("RowName"), FName(*RowName));
					}
				}
			}
		}
	}
	else if (MType == TEXT("set_by_caller") || MType == TEXT("setbycaller"))
	{
		CalcEnumValue = (int64)EGameplayEffectMagnitudeCalculation::SetByCaller;
		CanonicalType = TEXT("SetByCaller");

		FString DataTagStr, DataNameStr;
		Args->TryGetStringField(TEXT("data_tag"),  DataTagStr);
		Args->TryGetStringField(TEXT("data_name"), DataNameStr);

		if (FStructProperty* SF = FindFProperty<FStructProperty>(MagStruct, TEXT("SetByCallerMagnitude")))
		{
			void* SFPtr = SF->ContainerPtrToValuePtr<void>(MagPtr);
			if (!DataTagStr.IsEmpty())
			{
				FGameplayTag Tag = UGameplayTagsManager::Get().RequestGameplayTag(FName(*DataTagStr), false);
				if (FStructProperty* TagSP = FindFProperty<FStructProperty>(SF->Struct, TEXT("DataTag")))
				{
					void* TagPtr = TagSP->ContainerPtrToValuePtr<void>(SFPtr);
					*(FGameplayTag*)TagPtr = Tag;
				}
			}
			if (!DataNameStr.IsEmpty())
			{
				SetStructField<FNameProperty>(SF->Struct, SFPtr, TEXT("DataName"), FName(*DataNameStr));
			}
			if (DataTagStr.IsEmpty() && DataNameStr.IsEmpty())
			{
				OutError = TEXT("set_by_caller requires either data_tag (preferred) or data_name."); return;
			}
		}
	}
	else if (MType == TEXT("attribute_based") || MType == TEXT("attributebased"))
	{
		CalcEnumValue = (int64)EGameplayEffectMagnitudeCalculation::AttributeBased;
		CanonicalType = TEXT("AttributeBased");

		FString BackingAttr, SourceStr, CalcType;
		double Coefficient = 1.0, PreAdd = 0.0, PostAdd = 0.0;
		bool bSnapshot = false;
		Args->TryGetStringField(TEXT("backing_attribute"), BackingAttr);
		Args->TryGetStringField(TEXT("source"),            SourceStr);
		Args->TryGetStringField(TEXT("calculation_type"),  CalcType);
		Args->TryGetNumberField(TEXT("coefficient"),       Coefficient);
		Args->TryGetNumberField(TEXT("pre_add"),           PreAdd);
		Args->TryGetNumberField(TEXT("post_add"),          PostAdd);
		Args->TryGetBoolField  (TEXT("snapshot"),          bSnapshot);

		if (BackingAttr.IsEmpty()) { OutError = TEXT("attribute_based requires backing_attribute (e.g. \"AS_Stats.AttackPower\")."); return; }
		FProperty* AttrProp = FindGameplayAttributeProperty(BackingAttr);
		if (!AttrProp) { OutError = FString::Printf(TEXT("backing_attribute '%s' not found."), *BackingAttr); return; }

		if (FStructProperty* AB = FindFProperty<FStructProperty>(MagStruct, TEXT("AttributeBasedMagnitude")))
		{
			void* ABPtr = AB->ContainerPtrToValuePtr<void>(MagPtr);
			auto WriteScalableFloat = [&](const TCHAR* FieldName, float Value)
			{
				FStructProperty* SF = FindFProperty<FStructProperty>(AB->Struct, FieldName);
				if (!SF) return;
				void* SFPtr = SF->ContainerPtrToValuePtr<void>(ABPtr);
				if (FFloatProperty* ValProp = FindFProperty<FFloatProperty>(SF->Struct, TEXT("Value")))
				{
					ValProp->SetPropertyValue(ValProp->ContainerPtrToValuePtr<void>(SFPtr), Value);
				}
			};
			WriteScalableFloat(TEXT("Coefficient"),               (float)Coefficient);
			WriteScalableFloat(TEXT("PreMultiplyAdditiveValue"),  (float)PreAdd);
			WriteScalableFloat(TEXT("PostMultiplyAdditiveValue"), (float)PostAdd);

			int64 CalcVal = (int64)EAttributeBasedFloatCalculationType::AttributeMagnitude;
			const FString CL = CalcType.ToLower();
			if      (CL == TEXT("basevalue") || CL == TEXT("base_value") || CL == TEXT("base"))      CalcVal = (int64)EAttributeBasedFloatCalculationType::AttributeBaseValue;
			else if (CL == TEXT("bonus") || CL == TEXT("bonusmagnitude") || CL == TEXT("bonus_magnitude")) CalcVal = (int64)EAttributeBasedFloatCalculationType::AttributeBonusMagnitude;
			else if (CL == TEXT("uptochannel") || CL == TEXT("up_to_channel") || CL == TEXT("channel"))    CalcVal = (int64)EAttributeBasedFloatCalculationType::AttributeMagnitudeEvaluatedUpToChannel;
			if (FByteProperty* CP = FindFProperty<FByteProperty>(AB->Struct, TEXT("AttributeCalculationType")))
			{
				CP->SetIntPropertyValue(CP->ContainerPtrToValuePtr<void>(ABPtr), CalcVal);
			}
			else if (FEnumProperty* EP = FindFProperty<FEnumProperty>(AB->Struct, TEXT("AttributeCalculationType")))
			{
				EP->GetUnderlyingProperty()->SetIntPropertyValue(EP->ContainerPtrToValuePtr<void>(ABPtr), CalcVal);
			}

			if (FStructProperty* BackingSP = FindFProperty<FStructProperty>(AB->Struct, TEXT("BackingAttribute")))
			{
				void* BackingPtr = BackingSP->ContainerPtrToValuePtr<void>(ABPtr);
				if (FStructProperty* AttrSP = FindFProperty<FStructProperty>(BackingSP->Struct, TEXT("AttributeToCapture")))
				{
					FGameplayAttribute NewAttr(AttrProp);
					*(FGameplayAttribute*)AttrSP->ContainerPtrToValuePtr<void>(BackingPtr) = NewAttr;
				}
				int64 SrcVal = (int64)EGameplayEffectAttributeCaptureSource::Source;
				if (SourceStr.Equals(TEXT("Target"), ESearchCase::IgnoreCase)) SrcVal = (int64)EGameplayEffectAttributeCaptureSource::Target;
				if (FByteProperty* SrcP = FindFProperty<FByteProperty>(BackingSP->Struct, TEXT("AttributeSource")))
				{
					SrcP->SetIntPropertyValue(SrcP->ContainerPtrToValuePtr<void>(BackingPtr), SrcVal);
				}
				else if (FEnumProperty* SrcEP = FindFProperty<FEnumProperty>(BackingSP->Struct, TEXT("AttributeSource")))
				{
					SrcEP->GetUnderlyingProperty()->SetIntPropertyValue(SrcEP->ContainerPtrToValuePtr<void>(BackingPtr), SrcVal);
				}
				SetStructField<FBoolProperty>(BackingSP->Struct, BackingPtr, TEXT("bSnapshot"), bSnapshot);
			}
		}
	}
	else if (MType == TEXT("custom") || MType == TEXT("customcalculation") || MType == TEXT("custom_calculation"))
	{
		CalcEnumValue = (int64)EGameplayEffectMagnitudeCalculation::CustomCalculationClass;
		CanonicalType = TEXT("CustomCalculationClass");

		FString CalcClassPath;
		Args->TryGetStringField(TEXT("calculation_class_path"), CalcClassPath);
		if (CalcClassPath.IsEmpty()) { OutError = TEXT("custom requires calculation_class_path (UMMC subclass — e.g. \"/Game/GAS/MMC_Damage.MMC_Damage_C\")."); return; }

		if (FStructProperty* CustomSP = FindFProperty<FStructProperty>(MagStruct, TEXT("CustomMagnitude")))
		{
			void* CustomPtr = CustomSP->ContainerPtrToValuePtr<void>(MagPtr);
			UClass* CalcClass = LoadObject<UClass>(nullptr, *CalcClassPath);
			if (!CalcClass)
			{
				if (UBlueprint* CalcBp = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(CalcClassPath)))
					CalcClass = CalcBp->GeneratedClass;
			}
			if (!CalcClass) { OutError = FString::Printf(TEXT("Could not resolve calculation_class_path '%s'."), *CalcClassPath); return; }

			if (FClassProperty* CCP = FindFProperty<FClassProperty>(CustomSP->Struct, TEXT("CalculationClassMagnitude")))
			{
				CCP->SetObjectPropertyValue(CCP->ContainerPtrToValuePtr<void>(CustomPtr), CalcClass);
			}
		}
	}
	else
	{
		OutError = FString::Printf(TEXT("magnitude_type '%s' not recognised. Valid: scalable_float, set_by_caller, attribute_based, custom."), *MagType);
		return;
	}

	if (FByteProperty* TBP = FindFProperty<FByteProperty>(MagStruct, TEXT("MagnitudeCalculationType")))
	{
		TBP->SetIntPropertyValue(TBP->ContainerPtrToValuePtr<void>(MagPtr), CalcEnumValue);
	}
	else if (FEnumProperty* TEP = FindFProperty<FEnumProperty>(MagStruct, TEXT("MagnitudeCalculationType")))
	{
		TEP->GetUnderlyingProperty()->SetIntPropertyValue(TEP->ContainerPtrToValuePtr<void>(MagPtr), CalcEnumValue);
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
	UEditorAssetLibrary::SaveAsset(EffectPath, false);

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"effect_path\":\"%s\",\"magnitude_type\":\"%s\"}"),
		*EffectPath, *CanonicalType);
}

void HandleSetAbilityNetConfigFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }

	FString AbilityPath, NetExec, NetSec, RepPolicy, InstPolicy;
	Args->TryGetStringField(TEXT("ability_path"),         AbilityPath);
	Args->TryGetStringField(TEXT("net_execution_policy"), NetExec);
	Args->TryGetStringField(TEXT("net_security_policy"),  NetSec);
	Args->TryGetStringField(TEXT("replication_policy"),   RepPolicy);
	Args->TryGetStringField(TEXT("instancing_policy"),    InstPolicy);

	UBlueprint* BP = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(AbilityPath));
	if (!BP || !BP->GeneratedClass) { OutError = TEXT("Could not load Ability Blueprint: ") + AbilityPath; return; }
	UGameplayAbility* GA = Cast<UGameplayAbility>(BP->GeneratedClass->GetDefaultObject());
	if (!GA) { OutError = TEXT("Asset is not a GameplayAbility: ") + AbilityPath; return; }

	GA->Modify();
	TArray<FString> Applied;

	auto WriteByteEnum = [GA](const TCHAR* PropName, int64 Value)
	{
		if (FByteProperty* BP = FindFProperty<FByteProperty>(GA->GetClass(), PropName))
		{
			BP->SetIntPropertyValue(BP->ContainerPtrToValuePtr<void>(GA), Value);
		}
	};

	if (!NetExec.IsEmpty())
	{
		const FString L = NetExec.ToLower();
		int64 V = -1;
		if      (L == TEXT("localpredicted")  || L == TEXT("local_predicted"))  V = (int64)EGameplayAbilityNetExecutionPolicy::LocalPredicted;
		else if (L == TEXT("localonly")       || L == TEXT("local_only"))       V = (int64)EGameplayAbilityNetExecutionPolicy::LocalOnly;
		else if (L == TEXT("serverinitiated") || L == TEXT("server_initiated")) V = (int64)EGameplayAbilityNetExecutionPolicy::ServerInitiated;
		else if (L == TEXT("serveronly")      || L == TEXT("server_only"))      V = (int64)EGameplayAbilityNetExecutionPolicy::ServerOnly;
		else { OutError = FString::Printf(TEXT("Unknown net_execution_policy '%s'. Valid: LocalPredicted, LocalOnly, ServerInitiated, ServerOnly."), *NetExec); return; }
		WriteByteEnum(TEXT("NetExecutionPolicy"), V);
		Applied.Add(FString::Printf(TEXT("net_execution_policy=%s"), *NetExec));
	}
	if (!NetSec.IsEmpty())
	{
		const FString L = NetSec.ToLower();
		int64 V = -1;
		if      (L == TEXT("clientorserver")        || L == TEXT("client_or_server"))         V = (int64)EGameplayAbilityNetSecurityPolicy::ClientOrServer;
		else if (L == TEXT("serveronlyexecution")   || L == TEXT("server_only_execution"))    V = (int64)EGameplayAbilityNetSecurityPolicy::ServerOnlyExecution;
		else if (L == TEXT("serveronlytermination") || L == TEXT("server_only_termination"))  V = (int64)EGameplayAbilityNetSecurityPolicy::ServerOnlyTermination;
		else if (L == TEXT("serveronly")            || L == TEXT("server_only"))              V = (int64)EGameplayAbilityNetSecurityPolicy::ServerOnly;
		else { OutError = FString::Printf(TEXT("Unknown net_security_policy '%s'."), *NetSec); return; }
		WriteByteEnum(TEXT("NetSecurityPolicy"), V);
		Applied.Add(FString::Printf(TEXT("net_security_policy=%s"), *NetSec));
	}
	if (!RepPolicy.IsEmpty())
	{
		const FString L = RepPolicy.ToLower();
		int64 V = -1;
		if      (L == TEXT("replicateno")  || L == TEXT("replicate_no")  || L == TEXT("no")  || L == TEXT("false")) V = (int64)EGameplayAbilityReplicationPolicy::ReplicateNo;
		else if (L == TEXT("replicateyes") || L == TEXT("replicate_yes") || L == TEXT("yes") || L == TEXT("true"))  V = (int64)EGameplayAbilityReplicationPolicy::ReplicateYes;
		else { OutError = FString::Printf(TEXT("Unknown replication_policy '%s'."), *RepPolicy); return; }
		WriteByteEnum(TEXT("ReplicationPolicy"), V);
		Applied.Add(FString::Printf(TEXT("replication_policy=%s"), *RepPolicy));
	}
	if (!InstPolicy.IsEmpty())
	{
		const FString L = InstPolicy.ToLower();
		int64 V = -1;
		if      (L == TEXT("instancedperactor")     || L == TEXT("instanced_per_actor"))     V = (int64)EGameplayAbilityInstancingPolicy::InstancedPerActor;
		else if (L == TEXT("instancedperexecution") || L == TEXT("instanced_per_execution")) V = (int64)EGameplayAbilityInstancingPolicy::InstancedPerExecution;
		else { OutError = FString::Printf(TEXT("Unknown instancing_policy '%s'. Valid: InstancedPerActor, InstancedPerExecution. (NonInstanced is deprecated in 5.5.)"), *InstPolicy); return; }
		WriteByteEnum(TEXT("InstancingPolicy"), V);
		Applied.Add(FString::Printf(TEXT("instancing_policy=%s"), *InstPolicy));
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
	UEditorAssetLibrary::SaveAsset(AbilityPath, false);

	TSharedPtr<FJsonObject> Out = MakeShareable(new FJsonObject);
	Out->SetBoolField(TEXT("success"), true);
	Out->SetStringField(TEXT("ability_path"), AbilityPath);
	Out->SetStringField(TEXT("applied"), FString::Join(Applied, TEXT(", ")));
	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Out.ToSharedRef(), W);
}

void HandleAddAbilityTriggerFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }

	FString AbilityPath;
	Args->TryGetStringField(TEXT("ability_path"), AbilityPath);

	UBlueprint* BP = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(AbilityPath));
	if (!BP || !BP->GeneratedClass) { OutError = TEXT("Could not load Ability Blueprint: ") + AbilityPath; return; }
	UGameplayAbility* GA = Cast<UGameplayAbility>(BP->GeneratedClass->GetDefaultObject());
	if (!GA) { OutError = TEXT("Asset is not a GameplayAbility: ") + AbilityPath; return; }

	GA->Modify();

	FArrayProperty* TriggersProp = FindFProperty<FArrayProperty>(GA->GetClass(), TEXT("AbilityTriggers"));
	if (!TriggersProp)
	{
		OutError = TEXT("AbilityTriggers property not found on UGameplayAbility — engine version mismatch?");
		return;
	}
	FScriptArrayHelper Helper(TriggersProp, TriggersProp->ContainerPtrToValuePtr<void>(GA));

	auto AppendOne = [&](const FString& TagStr, const FString& SourceStr) -> bool
	{
		FGameplayTag Tag = UGameplayTagsManager::Get().RequestGameplayTag(FName(*TagStr), false);
		if (!Tag.IsValid())
		{
			OutError = FString::Printf(TEXT("trigger_tag '%s' is not a valid registered gameplay tag — use add_gameplay_tag first."), *TagStr);
			return false;
		}

		const int32 NewIdx = Helper.AddValue();
		FAbilityTriggerData* Trigger = (FAbilityTriggerData*)Helper.GetRawPtr(NewIdx);
		Trigger->TriggerTag = Tag;
		Trigger->TriggerSource = EGameplayAbilityTriggerSource::GameplayEvent;
		const FString S = SourceStr.ToLower();
		if      (S == TEXT("ownedtagadded")   || S == TEXT("owned_tag_added"))   Trigger->TriggerSource = EGameplayAbilityTriggerSource::OwnedTagAdded;
		else if (S == TEXT("ownedtagpresent") || S == TEXT("owned_tag_present")) Trigger->TriggerSource = EGameplayAbilityTriggerSource::OwnedTagPresent;
		return true;
	};

	int32 AddedCount = 0;
	const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
	if (Args->TryGetArrayField(TEXT("items"), Items) || Args->TryGetArrayField(TEXT("triggers"), Items))
	{
		for (int32 i = 0; i < Items->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*Items)[i]->AsObject();
			if (!Item.IsValid()) continue;
			FString TagStr, SrcStr;
			Item->TryGetStringField(TEXT("trigger_tag"),    TagStr);
			Item->TryGetStringField(TEXT("trigger_source"), SrcStr);
			if (TagStr.IsEmpty()) continue;
			if (AppendOne(TagStr, SrcStr)) ++AddedCount;
			else return;
		}
	}
	else
	{
		FString TagStr, SrcStr;
		Args->TryGetStringField(TEXT("trigger_tag"),    TagStr);
		Args->TryGetStringField(TEXT("trigger_source"), SrcStr);
		if (TagStr.IsEmpty()) { OutError = TEXT("trigger_tag is required."); return; }
		if (AppendOne(TagStr, SrcStr)) ++AddedCount;
		else return;
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
	UEditorAssetLibrary::SaveAsset(AbilityPath, false);

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"ability_path\":\"%s\",\"added\":%d,\"total_triggers\":%d}"),
		*AbilityPath, AddedCount, Helper.Num());
}

namespace
{
	AActor* FindActorByLabelInWorld(UWorld* World, const FString& Label)
	{
		if (!World) return nullptr;
		for (FActorIterator It(World); It; ++It)
		{
			if (*It && It->GetActorLabel() == Label) return *It;
		}
		return nullptr;
	}

	AActor* ResolveActor(const TSharedPtr<FJsonObject>& Args, FString& OutError)
	{
		FString ActorPath; Args->TryGetStringField(TEXT("actor_path"), ActorPath);
		if (!ActorPath.IsEmpty())
		{
			if (UObject* Obj = StaticLoadObject(AActor::StaticClass(), nullptr, *ActorPath))
			{
				if (AActor* A = Cast<AActor>(Obj)) return A;
			}
			OutError = FString::Printf(TEXT("actor_path '%s' did not load to an AActor"), *ActorPath);
			return nullptr;
		}

		FString ActorLabel; Args->TryGetStringField(TEXT("actor_label"), ActorLabel);
		if (ActorLabel.IsEmpty())
		{
			OutError = TEXT("actor_path or actor_label required");
			return nullptr;
		}

		if (GEditor)
		{
			for (const FWorldContext& Ctx : GEditor->GetWorldContexts())
			{
				if (Ctx.WorldType != EWorldType::PIE && Ctx.WorldType != EWorldType::Game) continue;
				if (AActor* A = FindActorByLabelInWorld(Ctx.World(), ActorLabel)) return A;
			}
			if (AActor* A = FindActorByLabelInWorld(GEditor->GetEditorWorldContext().World(), ActorLabel)) return A;
		}
		OutError = FString::Printf(TEXT("No actor with label '%s' found in any open world"), *ActorLabel);
		return nullptr;
	}

	UAbilitySystemComponent* GetASC(AActor* Actor, FString& OutError)
	{
		if (!Actor) { OutError = TEXT("Actor is null"); return nullptr; }
		UAbilitySystemComponent* ASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Actor);
		if (!ASC)
		{
			OutError = FString::Printf(TEXT("Actor '%s' has no AbilitySystemComponent"), *Actor->GetActorLabel());
		}
		return ASC;
	}

	FString SerializeJson(const TSharedPtr<FJsonObject>& Obj)
	{
		FString Out;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
		FJsonSerializer::Serialize(Obj.ToSharedRef(), Writer);
		return Out;
	}
}

void HandleGetAttributeValuesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	AActor* Actor = ResolveActor(Args, OutError); if (!Actor) return;
	UAbilitySystemComponent* ASC = GetASC(Actor, OutError); if (!ASC) return;

	TArray<TSharedPtr<FJsonValue>> Arr;
	for (const UAttributeSet* Set : ASC->GetSpawnedAttributes())
	{
		if (!Set) continue;
		const UClass* SetClass = Set->GetClass();
		const FString SetClassName = SetClass->GetName();
		for (TFieldIterator<FStructProperty> PropIt(SetClass); PropIt; ++PropIt)
		{
			FStructProperty* Prop = *PropIt;
			if (!Prop || Prop->Struct != FGameplayAttributeData::StaticStruct()) continue;

			const FGameplayAttribute Attribute(Prop);
			const float CurrentValue = ASC->GetNumericAttribute(Attribute);
			const float BaseValue    = ASC->GetNumericAttributeBase(Attribute);

			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("attribute_name"), Prop->GetName());
			Entry->SetStringField(TEXT("full_name"), FString::Printf(TEXT("%s.%s"), *SetClassName, *Prop->GetName()));
			Entry->SetStringField(TEXT("set_class"), SetClassName);
			Entry->SetNumberField(TEXT("base_value"), BaseValue);
			Entry->SetNumberField(TEXT("current_value"), CurrentValue);
			Arr.Add(MakeShared<FJsonValueObject>(Entry));
		}
	}

	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetBoolField(TEXT("success"), true);
	R->SetStringField(TEXT("actor_label"), Actor->GetActorLabel());
	R->SetNumberField(TEXT("count"), Arr.Num());
	R->SetArrayField(TEXT("attributes"), Arr);
	OutJsonString = SerializeJson(R);
}

void HandleGetActiveEffectsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	AActor* Actor = ResolveActor(Args, OutError); if (!Actor) return;
	UAbilitySystemComponent* ASC = GetASC(Actor, OutError); if (!ASC) return;

	const float Now = Actor->GetWorld() ? Actor->GetWorld()->GetTimeSeconds() : 0.f;

	TArray<FActiveGameplayEffectHandle> Handles = ASC->GetActiveEffects(FGameplayEffectQuery());
	TArray<TSharedPtr<FJsonValue>> Arr;
	for (const FActiveGameplayEffectHandle& Handle : Handles)
	{
		const FActiveGameplayEffect* Active = ASC->GetActiveGameplayEffect(Handle);
		if (!Active || !Active->Spec.Def) continue;

		const UGameplayEffect* Def = Active->Spec.Def;
		const float TotalDuration = Active->GetDuration();
		const float Remaining = (TotalDuration > 0.f) ? Active->GetTimeRemaining(Now) : -1.f;

		const FGameplayTagContainer& GrantedTags = Def->GetGrantedTags();

		TArray<TSharedPtr<FJsonValue>> TagArr;
		for (const FGameplayTag& Tag : GrantedTags) TagArr.Add(MakeShared<FJsonValueString>(Tag.ToString()));

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("effect_name"), Def->GetClass()->GetName());
		Entry->SetNumberField(TEXT("stack_count"), Active->Spec.GetStackCount());
		Entry->SetNumberField(TEXT("total_duration"), TotalDuration);
		Entry->SetNumberField(TEXT("remaining_duration"), Remaining);
		Entry->SetNumberField(TEXT("level"), Active->Spec.GetLevel());
		Entry->SetArrayField(TEXT("granted_tags"), TagArr);
		Arr.Add(MakeShared<FJsonValueObject>(Entry));
	}

	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetBoolField(TEXT("success"), true);
	R->SetStringField(TEXT("actor_label"), Actor->GetActorLabel());
	R->SetNumberField(TEXT("count"), Arr.Num());
	R->SetArrayField(TEXT("effects"), Arr);
	OutJsonString = SerializeJson(R);
}

void HandleGetGrantedAbilitiesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	AActor* Actor = ResolveActor(Args, OutError); if (!Actor) return;
	UAbilitySystemComponent* ASC = GetASC(Actor, OutError); if (!ASC) return;

	TArray<TSharedPtr<FJsonValue>> Arr;
	for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
	{
		if (!Spec.Ability) continue;
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("ability_name"), Spec.Ability->GetClass()->GetName());
		Entry->SetNumberField(TEXT("level"), Spec.Level);
		Entry->SetBoolField(TEXT("is_active"), Spec.IsActive());
		Entry->SetStringField(TEXT("handle"), Spec.Handle.ToString());
		Arr.Add(MakeShared<FJsonValueObject>(Entry));
	}

	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetBoolField(TEXT("success"), true);
	R->SetStringField(TEXT("actor_label"), Actor->GetActorLabel());
	R->SetNumberField(TEXT("count"), Arr.Num());
	R->SetArrayField(TEXT("abilities"), Arr);
	OutJsonString = SerializeJson(R);
}

void HandleGetActiveTagsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	AActor* Actor = ResolveActor(Args, OutError); if (!Actor) return;
	UAbilitySystemComponent* ASC = GetASC(Actor, OutError); if (!ASC) return;

	FGameplayTagContainer Tags;
	ASC->GetOwnedGameplayTags(Tags);

	TArray<TSharedPtr<FJsonValue>> Arr;
	for (const FGameplayTag& Tag : Tags) Arr.Add(MakeShared<FJsonValueString>(Tag.ToString()));

	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetBoolField(TEXT("success"), true);
	R->SetStringField(TEXT("actor_label"), Actor->GetActorLabel());
	R->SetNumberField(TEXT("count"), Arr.Num());
	R->SetArrayField(TEXT("tags"), Arr);
	OutJsonString = SerializeJson(R);
}

}
