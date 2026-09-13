// Copyright 2026, BlueprintsLab, All rights reserved

using UnrealBuildTool;

public class UECPAnalyst : ModuleRules
{
	public UECPAnalyst(ReadOnlyTargetRules Target) : base(Target)
	{
		if (Target.Type != TargetType.Editor)
		{
			return;
		}

		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		bUseUnity = false;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"Json",
			"JsonUtilities",
			"UECPCore",
			"UECPTools",
			"UECPShell"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Projects",
			"UnrealEd",
			"Slate",
			"SlateCore",
			"HTTP",
			"DesktopPlatform",
			"ContentBrowser",
			"ContentBrowserData",
			"AssetTools",
			"Kismet",
			"BlueprintGraph",
			"AssetRegistry",
			"UMG",
			"UMGEditor",
			"BehaviorTreeEditor",
			"AIGraph",
			"AIModule",
			"MaterialEditor"
		});
	}
}
