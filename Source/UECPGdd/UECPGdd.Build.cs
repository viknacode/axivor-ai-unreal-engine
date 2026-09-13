// Copyright 2026, BlueprintsLab, All rights reserved

using UnrealBuildTool;

public class UECPGdd : ModuleRules
{
	public UECPGdd(ReadOnlyTargetRules Target) : base(Target)
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
			"DesktopPlatform"
		});
	}
}
