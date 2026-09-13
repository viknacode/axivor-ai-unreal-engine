// Copyright 2026, BlueprintsLab, All rights reserved

using UnrealBuildTool;

public class UECPMCPBridge : ModuleRules
{
	public UECPMCPBridge(ReadOnlyTargetRules Target) : base(Target)
	{
		if (Target.Type != TargetType.Editor)
		{
			return;
		}

		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		bUseUnity = false;

		// Sibling subdirectories include each other via "../<SubDir>/<Header>.h".
		// PrivateIncludePaths takes module-relative paths; UBT prepends ModuleDirectory.
		PrivateIncludePaths.Add(System.IO.Path.Combine(ModuleDirectory, "Private"));

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"HTTP",
			"Json",
			"JsonUtilities",
			"UECPCore",
		});

		// The bridge is now a thin transport + protocol layer. After the
		// MCPDispatch_*.cpp retirement it has zero per-tool code, so the
		// extensive editor-module list it used to inherit is gone.
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Projects",          // IPluginManager, plugin metadata
			"AssetRegistry",     // resources/list provider walks the registry
			"HTTPServer",        // the editor-side HTTP+SSE listener
			"DirectoryWatcher",  // broker watches the instance registry directory
			"Sockets",           // broker port-availability probe (avoids HTTP module bind warnings)
			"Networking",        // FInternetAddr for the probe
			"UnrealEd",          // editor subsystem hooks
			"Blutility",         // UMCP_EditorSubsystem inherits from UEditorUtilitySubsystem
		});
	}
}
