// Copyright 2026, BlueprintsLab, All rights reserved

using UnrealBuildTool;

public class UECPGameFeaturesExt : ModuleRules
{
	public UECPGameFeaturesExt(ReadOnlyTargetRules Target) : base(Target)
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
		});

		// Optional UE plugin deps owned by this extension. Because the module is
		// bExplicitlyLoaded:true in the .uplugin, the engine never auto-loads the DLL
		// — these libraries only get pulled into memory when the user opts in via
		// Settings → Extensions → Game Features Integration.
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"UnrealEd",
			"AssetTools",
			"AssetRegistry",
			"EditorScriptingUtilities",
			"Projects",
			"GameFeatures",
			"ModularGameplay",
			"PluginUtils",
		});
	}
}
