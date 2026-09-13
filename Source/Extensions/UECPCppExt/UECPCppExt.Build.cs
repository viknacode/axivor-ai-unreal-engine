// Copyright 2026, BlueprintsLab, All rights reserved

using UnrealBuildTool;

public class UECPCppExt : ModuleRules
{
	public UECPCppExt(ReadOnlyTargetRules Target) : base(Target)
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
			// Project + filesystem plumbing.
			"Projects",
		});

		// LiveCoding is Windows-only — used by compile_project to hot-patch the
		// running editor instead of waiting for a UBT relink (which fails when
		// the editor has the DLL loaded). On Mac/Linux we just fall through to
		// the UBT path.
		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			PrivateDependencyModuleNames.Add("LiveCoding");
		}
	}
}
