// Copyright 2026, BlueprintsLab, All rights reserved

using UnrealBuildTool;

public class UECPDataExt : ModuleRules
{
	public UECPDataExt(ReadOnlyTargetRules Target) : base(Target)
	{
		if (Target.Type != TargetType.Editor) return;
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		bUseUnity = false;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine", "Json", "JsonUtilities",
			// UECPCore is the SDK surface — IUECPExtensionService, IUECPToolDispatcher,
			// FUECPToolResult, BatchToolHelper, AssetCreationHelper. UECPTools is NOT
			// listed: this extension owns the data umbrella (UStructs / UEnums /
			// DataTables / DataAssets) without any cross-cutting helpers.
			"UECPCore",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			// Editor + asset plumbing.
			"Projects", "UnrealEd", "AssetTools", "AssetRegistry", "EditorScriptingUtilities",
			"Slate", "SlateCore",

			// ContentBrowser singleton for resolving the focused-folder path
			// when DataTableTools needs to default-save into the user's
			// currently-open Content Browser folder.
			"ContentBrowser", "ContentBrowserData",

			// User-defined struct / enum factories live in BlueprintGraph + Kismet,
			// alongside the K2 type registry the data umbrella reflects against.
			"Kismet", "KismetCompiler", "BlueprintGraph",
		});
	}
}
