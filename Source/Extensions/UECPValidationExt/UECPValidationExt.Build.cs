// Copyright 2026, BlueprintsLab, All rights reserved

using UnrealBuildTool;

public class UECPValidationExt : ModuleRules
{
	public UECPValidationExt(ReadOnlyTargetRules Target) : base(Target)
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
			// GEditor + editor subsystem access.
			"UnrealEd",
			// Gathering FAssetData for folders / the whole project.
			"AssetRegistry",
			// UEditorValidatorSubsystem::ValidateAssetsWithSettings (Data Validation plugin).
			"DataValidation",
		});
	}
}
