// Copyright 2026, BlueprintsLab, All rights reserved

using UnrealBuildTool;

public class UECPAnimationExt : ModuleRules
{
	public UECPAnimationExt(ReadOnlyTargetRules Target) : base(Target)
	{
		if (Target.Type != TargetType.Editor) return;
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		bUseUnity = false;

		if (Target.Version.MinorVersion < 5)
		{
			// FInstancedStruct lives in the StructUtils Experimental plugin on UE 5.4.
			// From 5.5 it was promoted to CoreUObject so no extra dep is needed.
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

			// AnimBP graph authoring (state machines, AnimGraph nodes, transitions).
			"AnimGraph", "AnimGraphRuntime",

			// Persona / preview surface — needed by UAnimBlueprintFactory and the
			// AnimBlueprint editor it routes through.
			"Persona", "AdvancedPreviewScene",

			// IK Rig + IK Retargeter authoring.
			"IKRig", "IKRigEditor",

			// Pose Search (motion matching) authoring.
			"PoseSearch", "PoseSearchEditor",

			// Control Rig integration — AnimGraphNode_ControlRig + AnimNode_ControlRig.
			// The richer ControlRig authoring (RigVM editing, MRQ) lives in
			// UECPCinematicsExt, but the AnimBP-side wrappers stay here.
			"ControlRig",

			// Chooser tables — folded into Animation because they're most often
			// used for motion-matching pose selection alongside Pose Search.
			"Chooser", "ChooserEditor",
		});
	}
}
