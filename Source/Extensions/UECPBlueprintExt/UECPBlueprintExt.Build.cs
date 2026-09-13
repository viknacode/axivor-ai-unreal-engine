// Copyright 2026, BlueprintsLab, All rights reserved

using UnrealBuildTool;

public class UECPBlueprintExt : ModuleRules
{
	public UECPBlueprintExt(ReadOnlyTargetRules Target) : base(Target)
	{
		if (Target.Type != TargetType.Editor) return;
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		bUseUnity = false;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine", "Json", "JsonUtilities",
			// UECPCore is the SDK surface — IUECPExtensionService, IUECPToolDispatcher,
			// FUECPToolResult, BatchToolHelper, AssetCreationHelper, ExtensionToolUtils.
			// UECPTools is NOT listed: this extension owns the full Blueprint authoring
			// surface (handles, fuzzy resolver, graph layout, build/repair pipeline)
			// without depending on cross-cutting helpers in UECPTools.
			"UECPCore",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			// Editor + asset plumbing.
			"Projects", "UnrealEd", "AssetTools", "AssetRegistry", "EditorScriptingUtilities",
			"Slate", "SlateCore", "InputCore", "EditorFramework", "ToolMenus",
			"ApplicationCore", "ContentBrowser", "ContentBrowserData", "DesktopPlatform",

			// Blueprint core - graph editing, compilation, K2 schema, AnimGraph nodes
			// for component class authoring + AnimBP-aware tools. InputBlueprintNodes
			// for the Enhanced Input K2Node_EnhancedInputAction the build pipeline can spawn.
			"Kismet", "KismetCompiler", "BlueprintGraph", "GraphEditor",
			"AnimGraph", "UMG", "UMGEditor", "Blutility",
			"EnhancedInput", "InputBlueprintNodes",

			// BlueprintGraphTools spawns Perception/AI K2 nodes (AIPerception types,
			// gameplay tag containers) when wiring AI-flavored Blueprints.
			"AIModule",

			// Gameplay Tags umbrella.
			"GameplayTags", "GameplayTagsEditor",
		});
	}
}
