// Copyright 2026, BlueprintsLab, All rights reserved.

#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace GASTools
{
	UECPGASEXT_API void HandleCreateAttributeSet(const FString& AssetName, const FString& SavePath,
		const TArray<FString>& AttributeNames,
		FString& OutJsonString, FString& OutError);

	UECPGASEXT_API void HandleAddGameplayAttribute(const FString& BlueprintPath, const FString& AttributeName,
		float DefaultValue,
		FString& OutJsonString, FString& OutError);

	UECPGASEXT_API void HandleCreateGameplayAbility(const FString& AssetName, const FString& SavePath,
		const FString& AbilityTagName,
		FString& OutJsonString, FString& OutError);

	UECPGASEXT_API void HandleCreateGameplayEffect(const FString& AssetName, const FString& SavePath,
		const FString& DurationPolicy,
		FString& OutJsonString, FString& OutError);

	UECPGASEXT_API void HandleAddAbilitySystemComponent(const FString& BlueprintPath, const FString& ComponentName,
		FString& OutJsonString, FString& OutError);

	UECPGASEXT_API void HandleSetAbilityTags(const FString& BlueprintPath,
		const TArray<FString>& AbilityTags,
		const TArray<FString>& BlockTags,
		const TArray<FString>& CancelTags,
		FString& OutJsonString, FString& OutError);

	UECPGASEXT_API void HandleCreateGameplayCue(const FString& AssetName, const FString& SavePath,
		const FString& CueTag,
		FString& OutJsonString, FString& OutError);

	UECPGASEXT_API void HandleAddGameplayEffectModifier(const FString& EffectPath, const FString& AttributeName,
		const FString& ModifierOp, float Magnitude,
		FString& OutJsonString, FString& OutError);

	UECPGASEXT_API void HandleSetGameplayEffectDuration(const FString& EffectPath, const FString& DurationPolicy,
		float DurationSeconds,
		FString& OutJsonString, FString& OutError);

	UECPGASEXT_API void HandleGrantAbilityToBlueprint(const FString& BlueprintPath, const FString& AbilityPath,
		FString& OutJsonString, FString& OutError);

	UECPGASEXT_API void HandleSetGameplayEffectStacking(const FString& EffectPath, const FString& StackingType,
		int32 StackLimitCount, const FString& StackDurationRefreshPolicy, const FString& StackPeriodResetPolicy,
		const FString& StackExpirationPolicy, FString& OutJsonString, FString& OutError);

	UECPGASEXT_API void HandleSetGameplayEffectPeriod(const FString& EffectPath, float Period,
		bool bExecuteOnApplication, FString& OutJsonString, FString& OutError);

	UECPGASEXT_API void HandleSetAbilityCost(const FString& AbilityPath, const FString& CostEffectPath,
		FString& OutJsonString, FString& OutError);

	UECPGASEXT_API void HandleSetAbilityCooldown(const FString& AbilityPath, const FString& CooldownEffectPath,
		FString& OutJsonString, FString& OutError);

	UECPGASEXT_API void HandleGetGameplayEffectSummary(const FString& EffectPath,
		FString& OutJsonString, FString& OutError);

	UECPGASEXT_API void HandleGetAttributeSetSummary(const FString& AttributeSetPath,
		FString& OutJsonString, FString& OutError);

	UECPGASEXT_API void HandleRemoveGameplayEffectModifier(const FString& EffectPath, const FString& AttributeName,
		int32 ModifierIndex, FString& OutJsonString, FString& OutError);

	UECPGASEXT_API void HandleListGameplayAbilities(const FString& BlueprintPath, FString& OutJsonString, FString& OutError);

	UECPGASEXT_API void HandleSetGameplayEffectTags(const FString& EffectPath, const FString& TagCategory,
		const TArray<FString>& Tags, FString& OutJsonString, FString& OutError);

	UECPGASEXT_API void HandleAddGameplayAttributeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPGASEXT_API void HandleAddGameplayEffectModifierFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPGASEXT_API void HandleCreateGameplayEffectFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPGASEXT_API void HandleCreateAttributeSetFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPGASEXT_API void HandleCreateGameplayAbilityFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPGASEXT_API void HandleAddAbilitySystemComponentFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPGASEXT_API void HandleSetAbilityTagsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPGASEXT_API void HandleCreateGameplayCueFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPGASEXT_API void HandleSetGameplayEffectDurationFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPGASEXT_API void HandleGrantAbilityToBlueprintFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPGASEXT_API void HandleSetGameplayEffectStackingFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPGASEXT_API void HandleSetGameplayEffectPeriodFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPGASEXT_API void HandleSetAbilityCostFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPGASEXT_API void HandleSetAbilityCooldownFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPGASEXT_API void HandleGetGameplayEffectSummaryFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPGASEXT_API void HandleGetAttributeSetSummaryFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPGASEXT_API void HandleRemoveGameplayEffectModifierFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPGASEXT_API void HandleListGameplayAbilitiesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPGASEXT_API void HandleSetGameplayEffectTagsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPGASEXT_API void HandleSetGameplayEffectChanceFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPGASEXT_API void HandleSetGameplayEffectImmunityFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPGASEXT_API void HandleSetGameplayEffectRemoveOtherFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPGASEXT_API void HandleSetGameplayEffectCustomApplicationFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPGASEXT_API void HandleAddGameplayEffectGrantedAbilityFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPGASEXT_API void HandleAddGameplayEffectConditionalEffectFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPGASEXT_API void HandleSetGameplayEffectBlockAbilityTagsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPGASEXT_API void HandleSetGameplayEffectCancelAbilityTagsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPGASEXT_API void HandleAddGameplayEffectCueFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPGASEXT_API void HandleAddGameplayEffectExecutionFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPGASEXT_API void HandleSetGameplayEffectOverflowFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPGASEXT_API void HandleGetAbilitySummaryFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPGASEXT_API void HandleSetModifierTagRequirementsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPGASEXT_API void HandleAddExecutionConditionalEffectFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPGASEXT_API void HandleGetAttributeValuesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPGASEXT_API void HandleGetActiveEffectsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPGASEXT_API void HandleGetGrantedAbilitiesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPGASEXT_API void HandleGetActiveTagsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPGASEXT_API void HandleSetModifierMagnitudeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPGASEXT_API void HandleSetAbilityNetConfigFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPGASEXT_API void HandleAddAbilityTriggerFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
}
