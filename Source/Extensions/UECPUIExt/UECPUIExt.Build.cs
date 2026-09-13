// Copyright 2026, BlueprintsLab, All rights reserved

using UnrealBuildTool;

public class UECPUIExt : ModuleRules
{
	public UECPUIExt(ReadOnlyTargetRules Target) : base(Target)
	{
		if (Target.Type != TargetType.Editor) return;
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		bUseUnity = false;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine", "Json", "JsonUtilities",
			// UECPCore is the SDK surface — IUECPExtensionService, IUECPToolDispatcher,
			// FUECPToolResult, BatchToolHelper, ExtensionToolUtils. UECPTools is NOT
			// listed: this extension has no internal-helper dependencies, so it can
			// drop into a Base-only host (mini-plugin) and work unchanged.
			"UECPCore",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			// Editor + asset plumbing.
			"UnrealEd", "AssetTools", "AssetRegistry", "EditorScriptingUtilities",
			"Slate", "SlateCore", "Kismet", "KismetCompiler", "BlueprintGraph",

			// UMG widget surface.
			"UMG", "UMGEditor", "Blutility",

			// Widget animation tracks.
			"MovieScene", "MovieSceneTracks",

			// CommonUI authoring (button/text/activatable widget Blueprints + input action data tables).
			"CommonUI", "CommonInput", "EnhancedInput",
		});
	}
}
