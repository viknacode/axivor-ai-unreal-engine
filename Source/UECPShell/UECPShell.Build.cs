// Copyright 2026, BlueprintsLab, All rights reserved

using UnrealBuildTool;

public class UECPShell : ModuleRules
{
	public UECPShell(ReadOnlyTargetRules Target) : base(Target)
	{
		if (Target.Type != TargetType.Editor)
		{
			return;
		}

		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		bUseUnity = false;

		// In UE 5.6+ the Niagara stateless emitter headers moved to Internal/ paths.
		if (Target.Version.MinorVersion >= 6)
		{
			PrivateIncludePaths.Add(System.IO.Path.Combine(EngineDirectory, "Plugins/FX/Niagara/Source/Niagara/Internal"));
			PrivateIncludePaths.Add(System.IO.Path.Combine(EngineDirectory, "Plugins/FX/Niagara/Source/NiagaraShader/Internal"));
			PrivateDependencyModuleNames.Add("PropertyBindingUtils");
		}

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"HTTP",
			"Json",
			"JsonUtilities",
			"EditorScriptingUtilities",
			"UECPCore",
			"UECPTools",
			"UECPMCPBridge",
			"UECPLearning",
			"UECPAssetGen"
			// UECPGdd + UECPAiMemory + UECPVoice intentionally NOT listed. Their
			// feature modules depend on UECPShell (one-way) for shell types. Shell
			// consumes them via IUECP*Service interfaces in UECPCore instead.
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Projects",
			"InputCore",
			"EditorFramework",
			"UnrealEd",
			"ToolMenus",
			"Slate",
			"SlateCore",
			"Kismet",
			"AssetRegistry",
			"KismetCompiler",
			"BlueprintGraph",
			"GraphEditor",
			"ApplicationCore",
			"UMG",
			"UMGEditor",
			"Blutility",
			"Sockets",
			"Networking",
			"ContentBrowser",
			"ContentBrowserData",
			"MaterialEditor",
			"AssetTools",
			"Foliage",
			"FoliageEdit",
			"AIModule",
			"AIGraph",
			"BehaviorTreeEditor",
			"AnimGraph",
			"AnimGraphRuntime",
			"AudioEditor",
			"IKRig",
			"IKRigEditor",
			"DesktopPlatform",
			"WebBrowser",
			"WebBrowserWidget",
			"EnhancedInput",
			"InputBlueprintNodes",
			"Niagara",
			"NiagaraEditor",
			"PCG",
			"PCGEditor",
			"Landscape",
			"LandscapeEditor",
			"NavigationSystem",
			"GameplayTags",
			"GameplayTagsEditor",
			"GameplayAbilities",
			"MetasoundEngine",
			"MetasoundFrontend",
			"MetasoundEditor",
			"StateTreeModule",
			"StateTreeEditorModule",
			"GameplayStateTreeModule",
			// AssetReferenceManager + AssetMentionPopup resolve user-typed @
			// asset names to ULevelSequence / UMovieScene types. The richer
			// Sequencer / Movie Render Queue / Control Rig editor surface lives
			// in UECPCinematicsExt.
			"LevelSequence",
			"MovieScene",
			"PhysicsUtilities",
			"PhysicsCore",
			"PoseSearch",
			"PoseSearchEditor",
			"Chooser",
			"ChooserEditor",
			"BSPUtils",
			"DataLayerEditor",
			"ClothingSystemRuntimeInterface",
			"ClothingSystemRuntimeCommon",
			"Persona",
			"AdvancedPreviewScene",
			"TraceServices",
			"TraceAnalysis",
			"TraceLog",
			"AudioModulation",
			"AudioMixer",
			"AudioCapture",
			"AudioCaptureCore",
			"HairStrandsCore",
			"CommonUI",
			"CommonInput",
			"PythonScriptPlugin",
			"SourceControl"
		});
	}
}
