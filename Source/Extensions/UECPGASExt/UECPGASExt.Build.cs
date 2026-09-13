// Copyright 2026, BlueprintsLab, All rights reserved

using UnrealBuildTool;

public class UECPGASExt : ModuleRules
{
	public UECPGASExt(ReadOnlyTargetRules Target) : base(Target)
	{
		if (Target.Type != TargetType.Editor) return;
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		bUseUnity = false;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine", "Json", "JsonUtilities",
			// UECPCore is the SDK surface — IUECPExtensionService, IUECPToolDispatcher,
			// FUECPToolResult, BatchToolHelper. UECPTools is NOT listed: this extension
			// has no internal-helper dependencies, so it can drop into a Base-only host
			// (mini-plugin) and work unchanged.
			"UECPCore",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			// Editor + asset plumbing.
			"UnrealEd", "AssetTools", "AssetRegistry", "EditorScriptingUtilities",
			"Slate", "SlateCore", "Kismet", "KismetCompiler", "BlueprintGraph",

			// Gameplay Ability System — abilities, attribute sets, gameplay effects,
			// gameplay cues, ability system component, modifier authoring.
			"GameplayAbilities", "GameplayTasks",

			// Runtime gameplay tag types — used for ability + effect tag containers.
			"GameplayTags",
			// Editor tag registration (AddNewGameplayTagToINI) so set_gameplay_effect_tags
			// can register tags that don't exist yet, instead of silently dropping them.
			"GameplayTagsEditor",
		});
	}
}
