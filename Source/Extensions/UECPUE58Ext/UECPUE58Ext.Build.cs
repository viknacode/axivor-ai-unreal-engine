using UnrealBuildTool;

// Axivor AI — Unreal Engine 5.8 feature pack: Procedural Vegetation Editor, Control Rig
// Physics/Dynamics, Direct Mesh Controls, MetaHuman (Mesh to MetaHuman, build, crowds),
// 5.8 rendering presets (MegaLights, Lumen Lite, Substrate NPR, FSSS, Nanite Foliage),
// fast Physics Asset iteration and Game Animation Sample integration.
public class UECPUE58Ext : ModuleRules
{
	public UECPUE58Ext(ReadOnlyTargetRules Target) : base(Target)
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
			"UnrealEd", "AssetTools", "AssetRegistry", "Projects", "EditorScriptingUtilities", "EditorSubsystem",
			"Kismet", "KismetCompiler", "BlueprintGraph",
			"PhysicsUtilities", "PhysicsCore",
			"PythonScriptPlugin",
			"RenderCore",
		});
	}
}
