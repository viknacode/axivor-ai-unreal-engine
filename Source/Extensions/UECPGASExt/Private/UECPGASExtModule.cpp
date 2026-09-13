// Copyright 2026, BlueprintsLab, All rights reserved

#include "UECPGASExtModule.h"

#include "Tools/GASTools.h"

#include "UECPCoreModule.h"
#include "Services/IUECPToolDispatcher.h"
#include "Services/IUECPCreateAssetRegistry.h"

DEFINE_LOG_CATEGORY(LogUECPGASExt);

namespace
{
	static const TArray<FName>& OwnedToolNames()
	{
		static const TArray<FName> Names = {
			TEXT("add_gameplay_attribute"),
			TEXT("add_gameplay_effect_modifier"),
			TEXT("add_ability_system_component"),
			TEXT("set_ability_tags"),
			TEXT("get_ability_summary"),
			TEXT("set_gameplay_effect_duration"),
			TEXT("grant_ability_to_blueprint"),
			TEXT("set_gameplay_effect_stacking"),
			TEXT("set_gameplay_effect_period"),
			TEXT("set_ability_cost"),
			TEXT("set_ability_cooldown"),
			TEXT("get_gameplay_effect_summary"),
			TEXT("get_attribute_set_summary"),
			TEXT("remove_gameplay_effect_modifier"),
			TEXT("list_gameplay_abilities"),
			TEXT("set_gameplay_effect_tags"),
			TEXT("set_gameplay_effect_chance"),
			TEXT("set_gameplay_effect_immunity"),
			TEXT("set_gameplay_effect_remove_other"),
			TEXT("set_gameplay_effect_custom_application"),
			TEXT("add_gameplay_effect_granted_ability"),
			TEXT("add_gameplay_effect_conditional_effect"),
			TEXT("set_gameplay_effect_block_ability_tags"),
			TEXT("set_gameplay_effect_cancel_ability_tags"),
			TEXT("add_gameplay_effect_cue"),
			TEXT("add_gameplay_effect_execution"),
			TEXT("set_gameplay_effect_overflow"),
			TEXT("set_modifier_tag_requirements"),
			TEXT("add_execution_conditional_effect"),
			TEXT("set_modifier_magnitude"),
			TEXT("set_ability_net_config"),
			TEXT("add_ability_trigger"),
			TEXT("gas_get_attribute_values"),
			TEXT("gas_get_active_effects"),
			TEXT("gas_get_granted_abilities"),
			TEXT("gas_get_active_tags"),
		};
		return Names;
	}

	static auto MakeHandler(TFunction<void(const TSharedPtr<FJsonObject>&, FString&, FString&)> Fn)
	{
		return [Fn = MoveTemp(Fn)](const TSharedPtr<FJsonObject>& Args) -> FUECPToolResult
		{
			FUECPToolResult R;
			Fn(Args, R.ResultJson, R.ErrorMessage);
			R.bSuccess = R.ErrorMessage.IsEmpty();
			return R;
		};
	}
}

void FUECPGASExtModule::StartupModule()
{
	if (!IUECPCoreModule::IsAvailable()) return;
	IUECPToolDispatcher& D = IUECPCoreModule::Get().GetToolDispatcher();

	D.RegisterHandler(TEXT("add_gameplay_attribute"),         MakeHandler(GASTools::HandleAddGameplayAttributeFromArgs));
	D.RegisterHandler(TEXT("add_gameplay_effect_modifier"),   MakeHandler(GASTools::HandleAddGameplayEffectModifierFromArgs));
	D.RegisterHandler(TEXT("add_ability_system_component"),   MakeHandler(GASTools::HandleAddAbilitySystemComponentFromArgs));
	D.RegisterHandler(TEXT("set_ability_tags"),               MakeHandler(GASTools::HandleSetAbilityTagsFromArgs));
	D.RegisterHandler(TEXT("get_ability_summary"),            MakeHandler(GASTools::HandleGetAbilitySummaryFromArgs));
	D.RegisterHandler(TEXT("set_gameplay_effect_duration"),   MakeHandler(GASTools::HandleSetGameplayEffectDurationFromArgs));
	D.RegisterHandler(TEXT("grant_ability_to_blueprint"),     MakeHandler(GASTools::HandleGrantAbilityToBlueprintFromArgs));
	D.RegisterHandler(TEXT("set_gameplay_effect_stacking"),   MakeHandler(GASTools::HandleSetGameplayEffectStackingFromArgs));
	D.RegisterHandler(TEXT("set_gameplay_effect_period"),     MakeHandler(GASTools::HandleSetGameplayEffectPeriodFromArgs));
	D.RegisterHandler(TEXT("set_ability_cost"),               MakeHandler(GASTools::HandleSetAbilityCostFromArgs));
	D.RegisterHandler(TEXT("set_ability_cooldown"),           MakeHandler(GASTools::HandleSetAbilityCooldownFromArgs));
	D.RegisterHandler(TEXT("get_gameplay_effect_summary"),    MakeHandler(GASTools::HandleGetGameplayEffectSummaryFromArgs));
	D.RegisterHandler(TEXT("get_attribute_set_summary"),      MakeHandler(GASTools::HandleGetAttributeSetSummaryFromArgs));
	D.RegisterHandler(TEXT("remove_gameplay_effect_modifier"),MakeHandler(GASTools::HandleRemoveGameplayEffectModifierFromArgs));
	D.RegisterHandler(TEXT("list_gameplay_abilities"),        MakeHandler(GASTools::HandleListGameplayAbilitiesFromArgs));
	D.RegisterHandler(TEXT("set_gameplay_effect_tags"),       MakeHandler(GASTools::HandleSetGameplayEffectTagsFromArgs));
	D.RegisterHandler(TEXT("set_gameplay_effect_chance"),     MakeHandler(GASTools::HandleSetGameplayEffectChanceFromArgs));
	D.RegisterHandler(TEXT("set_gameplay_effect_immunity"),   MakeHandler(GASTools::HandleSetGameplayEffectImmunityFromArgs));
	D.RegisterHandler(TEXT("set_gameplay_effect_remove_other"), MakeHandler(GASTools::HandleSetGameplayEffectRemoveOtherFromArgs));
	D.RegisterHandler(TEXT("set_gameplay_effect_custom_application"), MakeHandler(GASTools::HandleSetGameplayEffectCustomApplicationFromArgs));
	D.RegisterHandler(TEXT("add_gameplay_effect_granted_ability"), MakeHandler(GASTools::HandleAddGameplayEffectGrantedAbilityFromArgs));
	D.RegisterHandler(TEXT("add_gameplay_effect_conditional_effect"), MakeHandler(GASTools::HandleAddGameplayEffectConditionalEffectFromArgs));
	D.RegisterHandler(TEXT("set_gameplay_effect_block_ability_tags"), MakeHandler(GASTools::HandleSetGameplayEffectBlockAbilityTagsFromArgs));
	D.RegisterHandler(TEXT("set_gameplay_effect_cancel_ability_tags"), MakeHandler(GASTools::HandleSetGameplayEffectCancelAbilityTagsFromArgs));
	D.RegisterHandler(TEXT("add_gameplay_effect_cue"),        MakeHandler(GASTools::HandleAddGameplayEffectCueFromArgs));
	D.RegisterHandler(TEXT("add_gameplay_effect_execution"),  MakeHandler(GASTools::HandleAddGameplayEffectExecutionFromArgs));
	D.RegisterHandler(TEXT("set_gameplay_effect_overflow"),   MakeHandler(GASTools::HandleSetGameplayEffectOverflowFromArgs));
	D.RegisterHandler(TEXT("set_modifier_tag_requirements"),  MakeHandler(GASTools::HandleSetModifierTagRequirementsFromArgs));
	D.RegisterHandler(TEXT("add_execution_conditional_effect"), MakeHandler(GASTools::HandleAddExecutionConditionalEffectFromArgs));
	D.RegisterHandler(TEXT("set_modifier_magnitude"),         MakeHandler(GASTools::HandleSetModifierMagnitudeFromArgs));
	D.RegisterHandler(TEXT("set_ability_net_config"),         MakeHandler(GASTools::HandleSetAbilityNetConfigFromArgs));
	D.RegisterHandler(TEXT("add_ability_trigger"),            MakeHandler(GASTools::HandleAddAbilityTriggerFromArgs));
	D.RegisterHandler(TEXT("gas_get_attribute_values"),       MakeHandler(GASTools::HandleGetAttributeValuesFromArgs));
	D.RegisterHandler(TEXT("gas_get_active_effects"),         MakeHandler(GASTools::HandleGetActiveEffectsFromArgs));
	D.RegisterHandler(TEXT("gas_get_granted_abilities"),      MakeHandler(GASTools::HandleGetGrantedAbilitiesFromArgs));
	D.RegisterHandler(TEXT("gas_get_active_tags"),            MakeHandler(GASTools::HandleGetActiveTagsFromArgs));

	{
		const FName U(TEXT("gas"));
		const auto Meta = [&D, &U](const TCHAR* Action, const TCHAR* Summary, const TCHAR* Params)
		{ D.RegisterToolMetadata({ FName(Action), U, FString(Summary), FString(Params) }); };

		Meta(TEXT("add_gameplay_attribute"),      TEXT("Add an attribute (FGameplayAttributeData) to an AttributeSet Blueprint."), TEXT("blueprint_path, attribute_name, default_value"));
		Meta(TEXT("add_ability_system_component"),TEXT("Add an AbilitySystemComponent to a Blueprint (place on PlayerState for multiplayer)."), TEXT("blueprint_path, component_name=AbilitySystemComponent"));
		Meta(TEXT("set_ability_tags"),            TEXT("Set a GameplayAbility's tag containers (each provided array REPLACES that container; unknown tags auto-registered)."), TEXT("blueprint_path, ability_tags?, block_tags?, cancel_tags?, activation_owned_tags?, activation_required_tags?, activation_blocked_tags?, source_required_tags?, source_blocked_tags?, target_required_tags?, target_blocked_tags?"));
		Meta(TEXT("get_ability_summary"),         TEXT("Summarise a GameplayAbility: all tag containers, cooldown/cost effects, instancing/net policies, trigger count."), TEXT("blueprint_path"));
		Meta(TEXT("add_gameplay_effect_modifier"),TEXT("Add a modifier to a GameplayEffect (seeds a constant ScalableFloat — use set_modifier_magnitude for real types)."), TEXT("effect_path, attribute_name, modifier_op=Additive|Multiplicitive|Division|Override, magnitude"));
		Meta(TEXT("remove_gameplay_effect_modifier"), TEXT("Remove a GameplayEffect modifier by index (preferred) or by attribute_name."), TEXT("effect_path, modifier_index|attribute_name"));
		Meta(TEXT("set_modifier_magnitude"),      TEXT("Rewire a GE modifier to a real magnitude type: scalable_float / set_by_caller / attribute_based / custom (MMC)."), TEXT("effect_path, modifier_index|attribute_name, magnitude_type, <type-specific fields>"));
		Meta(TEXT("set_gameplay_effect_duration"),TEXT("Set a GameplayEffect's duration policy + seconds."), TEXT("effect_path, duration_policy, duration_seconds"));
		Meta(TEXT("set_gameplay_effect_period"),  TEXT("Set a GameplayEffect's periodic tick."), TEXT("effect_path, period_seconds, execute_on_application"));
		Meta(TEXT("set_gameplay_effect_stacking"),TEXT("Configure a GameplayEffect's stacking behaviour."), TEXT("effect_path, stacking_type, stack_limit, stack_duration_refresh, stack_period_reset, stack_expiration"));
		Meta(TEXT("set_gameplay_effect_tags"),    TEXT("Set a GameplayEffect's tags by category (granted|asset|application|ongoing|removal). granted = tags applied to the target while active (cooldown tags); asset = the effect's own identity tags."), TEXT("effect_path, tag_category, tags"));
		Meta(TEXT("set_gameplay_effect_chance"),  TEXT("Set the GameplayEffect's chance to apply to target (0-1)."), TEXT("effect_path, chance"));
		Meta(TEXT("set_gameplay_effect_immunity"), TEXT("Make the effect grant immunity to GEs whose asset tags match any of these."), TEXT("effect_path, immunity_tags"));
		Meta(TEXT("set_gameplay_effect_remove_other"), TEXT("On application, remove active GEs whose asset tags match any of these."), TEXT("effect_path, remove_tags"));
		Meta(TEXT("set_gameplay_effect_custom_application"), TEXT("Set custom application-requirement classes (UGameplayEffectCustomApplicationRequirement)."), TEXT("effect_path, requirement_classes"));
		Meta(TEXT("add_gameplay_effect_granted_ability"), TEXT("Grant a GameplayAbility while this effect is active."), TEXT("effect_path, ability_path, level?, removal_policy?(CancelAbilityImmediately|RemoveAbilityOnEnd|DoNothing)"));
		Meta(TEXT("add_gameplay_effect_conditional_effect"), TEXT("Apply another GameplayEffect on application (with optional required_source_tags) or on completion."), TEXT("effect_path, conditional_effect_path, trigger?(on_application|on_complete_always|on_complete_normal|on_complete_prematurely), required_source_tags?"));
		Meta(TEXT("set_gameplay_effect_block_ability_tags"), TEXT("Block abilities with these tags while the effect is active."), TEXT("effect_path, tags"));
		Meta(TEXT("set_gameplay_effect_cancel_ability_tags"), TEXT("Cancel abilities with (and optionally without) these tags on application."), TEXT("effect_path, tags, without_tags?"));
		Meta(TEXT("add_gameplay_effect_cue"),     TEXT("Add a GameplayCue tag (fires cue VFX/SFX handlers) with optional min/max level."), TEXT("effect_path, cue_tag, min_level?, max_level?"));
		Meta(TEXT("add_gameplay_effect_execution"), TEXT("Add an execution calculation (UGameplayEffectExecutionCalculation) to the effect."), TEXT("effect_path, calculation_class"));
		Meta(TEXT("set_gameplay_effect_overflow"), TEXT("Set stacking overflow effects + deny-overflow flag. (For premature-expiration effects use add_gameplay_effect_conditional_effect trigger='on_complete_prematurely'.)"), TEXT("effect_path, overflow_effects?, deny_overflow_application?"));
		Meta(TEXT("set_modifier_tag_requirements"), TEXT("Set source/target tag requirements (require/ignore) on a specific GE modifier."), TEXT("effect_path, modifier_index|attribute_name, source_required_tags?, source_ignored_tags?, target_required_tags?, target_ignored_tags?"));
		Meta(TEXT("add_execution_conditional_effect"), TEXT("Add a conditional GameplayEffect to an existing execution (add the execution first with add_gameplay_effect_execution)."), TEXT("effect_path, execution_index, conditional_effect_path, required_source_tags?"));
		Meta(TEXT("grant_ability_to_blueprint"),  TEXT("Grant a GameplayAbility to a Blueprint (default granted ability)."), TEXT("blueprint_path, ability_path"));
		Meta(TEXT("set_ability_cost"),            TEXT("Set a GameplayAbility's cost effect."), TEXT("blueprint_path, cost_effect_path"));
		Meta(TEXT("set_ability_cooldown"),        TEXT("Set a GameplayAbility's cooldown effect."), TEXT("blueprint_path, cooldown_effect_path"));
		Meta(TEXT("set_ability_net_config"),      TEXT("Set a GameplayAbility's net execution/security/replication/instancing policies (each optional; NonInstanced deprecated → InstancedPerActor)."), TEXT("ability_path, net_execution_policy?, net_security_policy?, replication_policy?, instancing_policy?"));
		Meta(TEXT("add_ability_trigger"),         TEXT("Add an auto-activation trigger (GameplayEvent/OwnedTagAdded/OwnedTagPresent) — the tag must already exist."), TEXT("ability_path, trigger_tag, trigger_source"));
		Meta(TEXT("list_gameplay_abilities"),     TEXT("List the abilities granted to a Blueprint."), TEXT("blueprint_path"));
		Meta(TEXT("get_gameplay_effect_summary"), TEXT("Summarise a GameplayEffect."), TEXT("path"));
		Meta(TEXT("get_attribute_set_summary"),   TEXT("Summarise an AttributeSet."), TEXT("path"));
		Meta(TEXT("gas_get_attribute_values"),    TEXT("Runtime: read a live actor's ASC attribute base + current values."), TEXT("actor_path|actor_label"));
		Meta(TEXT("gas_get_active_effects"),      TEXT("Runtime: list a live actor's active GameplayEffects (stacks, durations, granted tags)."), TEXT("actor_path|actor_label"));
		Meta(TEXT("gas_get_granted_abilities"),   TEXT("Runtime: list a live actor's granted abilities (level, is_active)."), TEXT("actor_path|actor_label"));
		Meta(TEXT("gas_get_active_tags"),         TEXT("Runtime: list the gameplay tags a live actor's ASC owns right now."), TEXT("actor_path|actor_label"));
	}

	{
		IUECPCreateAssetRegistry& Reg = IUECPCoreModule::Get().GetCreateAssetRegistry();
		const FName ExtId(TEXT("GAS"));
		Reg.RegisterType(TEXT("GameplayAbility"), UECPCreateAsset::FactoryFromArgsFn(&GASTools::HandleCreateGameplayAbilityFromArgs, TEXT("GameplayAbility")), ExtId);
		Reg.RegisterType(TEXT("GameplayCue"),     UECPCreateAsset::FactoryFromArgsFn(&GASTools::HandleCreateGameplayCueFromArgs,     TEXT("GameplayCue")),     ExtId);
		Reg.RegisterType(TEXT("GameplayEffect"),  UECPCreateAsset::FactoryFromArgsFn(&GASTools::HandleCreateGameplayEffectFromArgs,  TEXT("GameplayEffect")),  ExtId);
		Reg.RegisterType(TEXT("AttributeSet"),    UECPCreateAsset::FactoryFromArgsFn(&GASTools::HandleCreateAttributeSetFromArgs,    TEXT("AttributeSet")),    ExtId);
	}

	UE_LOG(LogUECPGASExt, Log, TEXT("Registered %d GAS tools (gas umbrella)"),
		OwnedToolNames().Num());
}

void FUECPGASExtModule::ShutdownModule()
{
	if (!IUECPCoreModule::IsAvailable()) return;
	IUECPToolDispatcher& D = IUECPCoreModule::Get().GetToolDispatcher();
	for (const FName& N : OwnedToolNames()) D.UnregisterHandler(N);
	IUECPCreateAssetRegistry& Reg = IUECPCoreModule::Get().GetCreateAssetRegistry();
	for (const TCHAR* T : { TEXT("GameplayAbility"), TEXT("GameplayCue"), TEXT("GameplayEffect"), TEXT("AttributeSet") })
	{
		Reg.UnregisterType(T);
	}
}

IMPLEMENT_MODULE(FUECPGASExtModule, UECPGASExt)
