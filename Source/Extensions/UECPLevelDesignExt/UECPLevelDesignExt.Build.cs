// Copyright 2026, BlueprintsLab, All rights reserved

using UnrealBuildTool;

public class UECPLevelDesignExt : ModuleRules
{
	public UECPLevelDesignExt(ReadOnlyTargetRules Target) : base(Target)
	{
		if (Target.Type != TargetType.Editor) return;
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		bUseUnity = false;

		if (Target.Version.MinorVersion < 5)
		{
			// FInstancedPropertyBag lives in the StructUtils Experimental plugin on UE 5.4
			// (PCG attribute-set authoring uses it). Promoted to CoreUObject in 5.5.
			PrivateDependencyModuleNames.Add("StructUtils");
		}

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
			// Editor + asset plumbing.
			"UnrealEd", "AssetTools", "AssetRegistry", "EditorScriptingUtilities",
			"Slate", "SlateCore", "Kismet", "KismetCompiler", "BlueprintGraph",
			// ULevelEditorSubsystem (open/save current level lifecycle tools).
			"LevelEditor",

			// LevelDesign-specific runtime + editor modules.
			"Foliage", "FoliageEdit",
			"PCG", "PCGEditor",
			"Landscape", "LandscapeEditor",
			"DataLayerEditor",
			// CinematicCamera — ACineCameraActor + UCineCameraComponent in
			// LevelActorTools::HandleSetCineCameraProperties.
			"CinematicCamera",
			// NavigationSystem + BSPUtils moved with NavMesh tools to UECPAIExt.
			// UPhysicalMaterial used by FoliageTools' assign_physical_material_to_foliage.
			"PhysicsCore",
		});
	}
}
