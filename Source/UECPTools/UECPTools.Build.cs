// Copyright 2026, BlueprintsLab, All rights reserved

using UnrealBuildTool;

public class UECPTools : ModuleRules
{
	public UECPTools(ReadOnlyTargetRules Target) : base(Target)
	{
		if (Target.Type != TargetType.Editor)
		{
			return;
		}

		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		bUseUnity = false;

		if (Target.Version.MinorVersion >= 6)
		{
			// PropertyBindingUtils is a new module in 5.6 that StateTreeEditorPropertyBindings depends on.
			PrivateDependencyModuleNames.Add("PropertyBindingUtils");
		}
		// Niagara Internal/ include paths moved with the niagara umbrella to UECPNiagaraExt.

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"RenderCore",
			"ImageCore",
			"HTTP",
			"Json",
			"JsonUtilities",
			"EditorScriptingUtilities",
			"UECPCore"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Projects",
			"InputCore",
			"EditorFramework",
			"UnrealEd",
			"LevelEditor",
			"ToolMenus",
			"Slate",
			"SlateCore",
			"Kismet",
			"AssetRegistry",
			"KismetCompiler",
			"BlueprintGraph",
			"GraphEditor",
			"ApplicationCore",
			"UMG",
			"UMGEditor",
			"Blutility",
			"Sockets",
			"Networking",
			"ContentBrowser",
			"ContentBrowserData",
			// MaterialEditor still needed: MaterialGraphDescriber + MaterialNodeDescriber
			// stay in UECPTools (consumed by Shell @-mention summary and Analyst summary
			// alongside the material umbrella tools in UECPMaterialExt).
			"MaterialEditor",
			"AssetTools",
			// AIModule + BehaviorTreeEditor still needed: UBehaviorTree casts in
			// AnalysisTools/ProjectScanTools for project-wide asset summaries, plus
			// FBehaviorTreeEditor static_casts in AnalysisTools/GraphTools'
			// get_selected_nodes branch for the focused-editor describe path.
			// AIGraph + NavigationSystem moved with the AI umbrellas (behavior_tree
			// / eqs / navmesh_ai / state_tree) to UECPAIExt.
			"AIModule",
			"AIGraph",  // BehaviorTreeEditor.h transitively pulls AIGraphEditor.h
			"BehaviorTreeEditor",
			"AnimGraph",
			"DesktopPlatform",
			"WebBrowser",
			"WebBrowserWidget",
			// Input authoring tools moved to UECPInputExt, but EnhancedInput runtime
			// is still needed by PlayTestTools (PIE input simulation uses
			// EnhancedInputLocalPlayerSubsystem). InputBlueprintNodes moves with
			// the umbrella since UECPTools no longer needs the K2 graph nodes.
			"EnhancedInput",
			// Niagara, NiagaraEditor moved with the niagara umbrella to UECPNiagaraExt.
			// Foliage, FoliageEdit, PCG, PCGEditor, Landscape, LandscapeEditor moved
			// with the level-design umbrella set to UECPLevelDesignExt (opt-in).
			// AIGraph, BehaviorTreeEditor, NavigationSystem, StateTreeModule,
			// StateTreeEditorModule, GameplayStateTreeModule moved with the AI
			// umbrellas to UECPAIExt.
			// GameplayTagsEditor moved with the gameplay_tags umbrella to UECPBlueprintExt;
			// runtime FGameplayTag is still used by PlayTest in this module.
			"GameplayTags",
			// GAS authoring tools moved to UECPGASExt, but UAbilitySystemComponent
			// runtime introspection in PlayTestTools (GAS attribute snapshot,
			// get_gas_attributes) still needs the runtime headers.
			"GameplayAbilities",
			"LevelSequence",
			"MovieScene",
			"MovieSceneTracks",
			"ControlRig",
			"ControlRigEditor",
			"PhysicsUtilities",
			"PhysicsCore",
			// Chooser, ChooserEditor folded into UECPAnimationExt (chooser umbrella
			// is anim-adjacent — motion matching uses chooser tables).
			// BSPUtils, DataLayerEditor, CinematicCamera moved with the level-design
			// umbrella set to UECPLevelDesignExt (opt-in via Settings → Extensions).
			"ClothingSystemRuntimeInterface",
			"ClothingSystemRuntimeCommon",
			"TraceServices",
			"TraceAnalysis",
			"TraceLog",
			"HairStrandsCore",
			"SourceControl"
		});

		if (Target.Version.MinorVersion < 5)
		{
			// On UE 5.4, HairStrandsCore/Public/GroomComponent.h transitively includes
			// NiagaraDataInterfacePhysicsAsset.h which lives in the Niagara plugin's
			// public headers. From 5.5 the dependency was hoisted into HairStrandsCore's
			// own Build.cs so consumers get it for free. Add it explicitly on 5.4.
			PrivateDependencyModuleNames.Add("Niagara");
		}
	}
}
