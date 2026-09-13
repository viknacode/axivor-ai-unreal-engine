using UnrealBuildTool;

// Axivor AI — domain workflow recipes (ordered build checklists with verification steps).
public class UECPWorkflowExt : ModuleRules
{
	public UECPWorkflowExt(ReadOnlyTargetRules Target) : base(Target)
	{
		if (Target.Type != TargetType.Editor) return;
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		bUseUnity = false;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "Json", "JsonUtilities", "UECPCore" });
		PrivateDependencyModuleNames.AddRange(new string[] { "UnrealEd" });
	}
}
