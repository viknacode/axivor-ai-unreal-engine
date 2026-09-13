// Copyright 2026, BlueprintsLab, All rights reserved

using UnrealBuildTool;

public class UECPPackagingExt : ModuleRules
{
	public UECPPackagingExt(ReadOnlyTargetRules Target) : base(Target)
	{
		if (Target.Type != TargetType.Editor) return;
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		bUseUnity = false;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine", "Json", "JsonUtilities",
			// UECPCore is the SDK surface — IUECPExtensionService, IUECPToolDispatcher,
			// FUECPToolResult. No UECPTools dependency: this extension is self-contained
			// and drops into a Base-only host unchanged.
			"UECPCore",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			// Project plumbing (project file path, plugin manager).
			"Projects",

			// UProjectPackagingSettings lives here (Settings/ProjectPackagingSettings.h).
			"DeveloperToolSettings",

			// ITargetPlatformManagerModule / ITargetPlatform for platform enumeration.
			"TargetPlatform",

			// UGameMapsSettings (default map validation).
			"EngineSettings",
		});
	}
}
