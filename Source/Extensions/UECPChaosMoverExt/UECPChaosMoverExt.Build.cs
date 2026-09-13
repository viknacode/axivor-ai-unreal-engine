// Copyright 2026, BlueprintsLab, All rights reserved

using UnrealBuildTool;

public class UECPChaosMoverExt : ModuleRules
{
	public UECPChaosMoverExt(ReadOnlyTargetRules Target) : base(Target)
	{
		if (Target.Type != TargetType.Editor) return;
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		bUseUnity = false;

		// NOTE: deliberately does NOT depend on the engine "ChaosMover" module.
		// ChaosMover only exists on UE 5.6+, but every extension module is compiled
		// for whatever engine the plugin is built against (incl. 5.4/5.5). All Chaos
		// classes are resolved dynamically by path at runtime, and the manifest's
		// min_engine_version gate keeps the extension unloadable below 5.6.
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine", "Json", "JsonUtilities",
			"UECPCore", "UECPTools", "UECPMoverExt",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"UnrealEd", "AssetTools", "AssetRegistry", "EditorScriptingUtilities", "Kismet",
			"Mover",
		});
	}
}
