// Copyright 2026, BlueprintsLab, All rights reserved

using UnrealBuildTool;

public class UECPInputExt : ModuleRules
{
	public UECPInputExt(ReadOnlyTargetRules Target) : base(Target)
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
			"Slate", "SlateCore", "InputCore",

			// Enhanced Input — Input Action / Mapping Context assets, triggers,
			// modifiers, the K2Node graph nodes for IA event bindings.
			"EnhancedInput", "InputBlueprintNodes",
		});
	}
}
