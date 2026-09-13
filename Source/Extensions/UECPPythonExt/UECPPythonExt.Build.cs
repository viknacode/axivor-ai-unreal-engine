// Copyright 2026, BlueprintsLab, All rights reserved

using UnrealBuildTool;

public class UECPPythonExt : ModuleRules
{
	public UECPPythonExt(ReadOnlyTargetRules Target) : base(Target)
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
			// Project plumbing.
			"Projects",

			// The Python plugin runs in-process; we call IPythonScriptPlugin::Get().
			// Project-Settings -> Plugins must have "Python Editor Script Plugin"
			// enabled or HandleExecutePython returns a friendly error.
			"PythonScriptPlugin",

			// HTTP for the recipe-lookup call to gamedevcore.
			"HTTP",
		});
	}
}
