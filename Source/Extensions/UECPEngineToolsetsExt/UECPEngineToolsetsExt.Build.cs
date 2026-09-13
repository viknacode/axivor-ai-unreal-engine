using UnrealBuildTool;

// Axivor AI — bridge to the Unreal Engine 5.8 native Toolset Registry (the same
// tool layer Epic's "Unreal MCP" server exposes), executed in-process, plus the
// File Sandbox (sandboxed edits with persist/discard).
public class UECPEngineToolsetsExt : ModuleRules
{
	public UECPEngineToolsetsExt(ReadOnlyTargetRules Target) : base(Target)
	{
		if (Target.Type != TargetType.Editor) return;
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		bUseUnity = false;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine", "Json", "JsonUtilities",
			"UECPCore",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"UnrealEd", "EditorSubsystem",
			"ToolsetRegistry",
			"FileSandboxCore",
		});
	}
}
