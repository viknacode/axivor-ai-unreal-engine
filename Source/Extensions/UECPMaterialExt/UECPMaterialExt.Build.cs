// Copyright 2026, BlueprintsLab, All rights reserved

using UnrealBuildTool;

public class UECPMaterialExt : ModuleRules
{
	public UECPMaterialExt(ReadOnlyTargetRules Target) : base(Target)
	{
		if (Target.Type != TargetType.Editor) return;
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		bUseUnity = false;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine", "Json", "JsonUtilities",
			// UECPCore is the SDK surface — IUECPExtensionService, IUECPToolDispatcher,
			// FUECPToolResult, BatchToolHelper, plus FMaterialGraphDescriber and
			// UECPContentBrowserUtils which both used to live in UECPTools but are
			// now Core-owned shared infrastructure. UECPTools is NOT listed:
			// MaterialExt has no UECPTools-private dependencies, so it can drop into
			// a Base-only host (mini-plugin) and work unchanged.
			"UECPCore",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			// Editor + asset plumbing.
			"UnrealEd", "AssetTools", "AssetRegistry", "EditorScriptingUtilities",
			"Slate", "SlateCore", "Kismet", "KismetCompiler", "BlueprintGraph",

			// Material editor — node/graph editing, function authoring, parameter
			// inspection. UECPCore also keeps MaterialEditor because it owns the
			// shared FMaterialGraphDescriber + FMaterialNodeDescriber.
			"MaterialEditor",

			// RHI: GetFeatureLevelShaderPlatform + GRHIGlobals — used by validate_material
			// in 5.7+ where UMaterial::GetMaterialResource now takes EShaderPlatform
			// instead of ERHIFeatureLevel::Type.
			"RHI",
		});
	}
}
