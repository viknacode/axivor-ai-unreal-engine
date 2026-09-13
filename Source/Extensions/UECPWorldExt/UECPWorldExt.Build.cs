using UnrealBuildTool;

// Axivor AI — World Builder (umbrella `world`): biome layers composed from populate_landscape,
// spline roads / rivers (Water plugin optional, resolved by reflection), grid snapping, surface
// alignment, prefab placement (Blueprints + Level Instances), filtered mass edits and direct
// landscape heightmap import / sculpt / layer painting through FLandscapeEditDataInterface.
public class UECPWorldExt : ModuleRules
{
	public UECPWorldExt(ReadOnlyTargetRules Target) : base(Target)
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
			"UnrealEd", "AssetRegistry", "EditorScriptingUtilities", "Projects",
			// Landscape data access (FLandscapeEditDataInterface, edit layers) + heightmap file
			// import / resampling helpers (FLandscapeImportHelper, PNG / RAW / R16 formats).
			"Landscape", "LandscapeEditor", "Foliage",
			// UPCGComponent::GetGridBounds() — world_build_road's clear_vegetation intersects road
			// bounds against every level PCG component's generated bounds.
			"PCG",
		});
	}
}
