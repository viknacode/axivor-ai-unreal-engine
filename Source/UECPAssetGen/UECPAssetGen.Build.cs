// Copyright 2026, BlueprintsLab, All rights reserved

using UnrealBuildTool;

public class UECPAssetGen : ModuleRules
{
	public UECPAssetGen(ReadOnlyTargetRules Target) : base(Target)
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
			"HTTP",
			"Json",
			"JsonUtilities",
			"UECPCore",
			"UECPTools"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Projects",
			"UnrealEd",
			"Slate",
			"SlateCore",
			"AssetRegistry",
			"AssetTools",
			"ContentBrowser",
			"ImageCore",
			"ImageWrapper",
			"RawMesh",
			"MeshDescription",
			"StaticMeshDescription",
			"MaterialEditor",
			"EditorScriptingUtilities"
		});
	}
}
