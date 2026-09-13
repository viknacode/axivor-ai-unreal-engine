// Copyright 2026, BlueprintsLab, All rights reserved

using UnrealBuildTool;

public class UECPCinematicsExt : ModuleRules
{
	public UECPCinematicsExt(ReadOnlyTargetRules Target) : base(Target)
	{
		if (Target.Type != TargetType.Editor) return;
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		bUseUnity = false;

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

			// Sequencer surface.
			"LevelSequence", "LevelSequenceEditor", "MovieScene", "MovieSceneTools", "MovieSceneTracks",
			"Sequencer", "CinematicCamera",

			// Movie Render Queue.
			"MovieRenderPipelineCore", "MovieRenderPipelineEditor",
			"MovieRenderPipelineRenderPasses",

			// Control Rig + RigVM authoring. RigVMDeveloper hosts URigVMController,
			// which the rig graph editing tools call directly.
			"ControlRig", "ControlRigEditor", "RigVM", "RigVMDeveloper",
		});

		// UControlRigBlueprint moved to ControlRigDeveloper before 5.7. Required for
		// any code that resolves a CR blueprint asset to its generated UClass.
		PrivateDependencyModuleNames.Add("ControlRigDeveloper");
	}
}
