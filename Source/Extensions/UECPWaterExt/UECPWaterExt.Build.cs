// Copyright 2026, BlueprintsLab, All rights reserved

using UnrealBuildTool;

public class UECPWaterExt : ModuleRules
{
	public UECPWaterExt(ReadOnlyTargetRules Target) : base(Target)
	{
		if (Target.Type != TargetType.Editor) return;
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		bUseUnity = false;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine", "Json", "JsonUtilities",
			"UECPCore", "UECPTools",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"UnrealEd", "AssetTools", "AssetRegistry", "EditorScriptingUtilities",
			"Water", "WaterEditor",
		});
	}
}
