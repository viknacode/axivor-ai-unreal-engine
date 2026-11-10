using UnrealBuildTool;

// Axivor AI — Unreal Engine 5.9 feature pack: the agentic stack (native Unreal MCP,
// Toolset Registry toolsets, Agent Skills, Knowledge Base, Unreal Assistant, Unreal
// Canvas, AnimGen) — status, one-call enabling presets and Markdown skill access.
public class UECPUE59Ext : ModuleRules
{
	public UECPUE59Ext(ReadOnlyTargetRules Target) : base(Target)
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
			"UnrealEd", "Projects",
			"ToolsetRegistry", // FToolsetRegistry + MarkdownSkills (5.9)
		});
	}
}
