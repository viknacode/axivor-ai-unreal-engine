// Copyright 2026, BlueprintsLab, All rights reserved

using UnrealBuildTool;

public class UECPCore : ModuleRules
{
	public UECPCore(ReadOnlyTargetRules Target) : base(Target)
	{
		if (Target.Type != TargetType.Editor)
		{
			return;
		}

		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		bUseUnity = false;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"HTTP",
			"Json",
			"JsonUtilities",
			"Slate",
			"SlateCore"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Projects",
			"UnrealEd",
			"EditorFramework",
			"ToolMenus",
			"InputCore",
			"ApplicationCore",
			"DesktopPlatform",
			"Sockets",
			"Networking",
			"AssetRegistry",
			"AssetTools",
			"EditorScriptingUtilities",   // UEditorAssetLibrary — used by the shared PinTypeResolver
			"ContentBrowser",
			"ContentBrowserData",
			"WebBrowser",
			"WebBrowserWidget",

			// Describer infrastructure — UECPCore owns FBpGraphDescriber,
			// FBtGraphDescriber, FBpSummarizer, FMaterialGraphDescriber,
			// FMaterialNodeDescriber. Shell @-mentions, Analyst summaries,
			// Project Scanner index, and AnalysisTools all consume these,
			// so they live in Core to avoid duplication across modules.
			"BlueprintGraph", "Kismet", "GraphEditor",
			"AIModule", "AIGraph", "BehaviorTreeEditor",
			"MaterialEditor",
			"UMG", "UMGEditor",
		});

		// Upstream-MCP OAuth: tokens are encrypted at rest with the OS key store.
		// Windows DPAPI (CryptProtectData) needs Crypt32; macOS Keychain needs
		// Security + CoreFoundation. PKCE's SHA-256 is vendored (no OpenSSL dep)
		// so the crypto compiles identically across UE 5.4-5.8.
		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			PublicSystemLibraries.Add("Crypt32.lib");
		}
		else if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			PublicFrameworks.AddRange(new string[] { "Security", "CoreFoundation" });
		}
	}
}
