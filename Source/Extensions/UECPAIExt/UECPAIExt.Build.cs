// Copyright 2026, BlueprintsLab, All rights reserved

using UnrealBuildTool;

public class UECPAIExt : ModuleRules
{
	public UECPAIExt(ReadOnlyTargetRules Target) : base(Target)
	{
		if (Target.Type != TargetType.Editor) return;
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		bUseUnity = false;

		if (Target.Version.MinorVersion >= 6)
		{
			// PropertyBindingUtils is a new module in 5.6 that StateTreeEditorPropertyBindings depends on.
			PrivateDependencyModuleNames.Add("PropertyBindingUtils");
		}

		if (Target.Version.MinorVersion < 5)
		{
			// FInstancedStruct + FInstancedPropertyBag live in the StructUtils Experimental
			// plugin on UE 5.4. From 5.5 they were promoted to CoreUObject.
			PrivateDependencyModuleNames.Add("StructUtils");
		}

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
			"Slate", "SlateCore", "Kismet", "KismetCompiler", "BlueprintGraph", "GraphEditor",

			// Behavior Tree authoring + Blackboard data.
			"AIModule", "AIGraph", "BehaviorTreeEditor",

			// EQS authoring (FEnvQueryItemType / generators / tests).
			// Lives in AIModule's runtime + AIGraph's editor surface.

			// Nav Mesh + AI Perception + Smart Objects.
			"NavigationSystem",

			// State Tree authoring — schemas, tasks, evaluators, conditions, transitions,
			// property bindings. GameplayStateTree adds the gameplay-side schemas.
			"StateTreeModule", "StateTreeEditorModule", "GameplayStateTreeModule",

			// GameplayTags — used by set_state_tree_parameter_default for FGameplayTag /
			// FGameplayTagContainer parameter types. Headers needed to resolve the structs.
			"GameplayTags",

			// FBSPOps used by NavMeshTools::HandleSpawnNavMeshBoundsVolume to set
			// the volume's BSP brush after spawn.
			"BSPUtils",
		});
	}
}
