// Copyright 2026, BlueprintsLab, All rights reserved

using UnrealBuildTool;

public class UECPNiagaraExt : ModuleRules
{
	public UECPNiagaraExt(ReadOnlyTargetRules Target) : base(Target)
	{
		if (Target.Type != TargetType.Editor) return;
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		bUseUnity = false;

		// In UE 5.6+ the Niagara stateless emitter headers moved to Internal/ paths
		// that are no longer transitively accessible. Add them explicitly so this
		// extension can introspect FountainLightweight-style stateless modules.
		if (Target.Version.MinorVersion >= 6)
		{
			PrivateIncludePaths.Add(System.IO.Path.Combine(EngineDirectory, "Plugins/FX/Niagara/Source/Niagara/Internal"));
			PrivateIncludePaths.Add(System.IO.Path.Combine(EngineDirectory, "Plugins/FX/Niagara/Source/NiagaraShader/Internal"));
		}

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine", "Json", "JsonUtilities",
			// UECPCore is the SDK surface — IUECPExtensionService, IUECPToolDispatcher,
			// FUECPToolResult, BatchToolHelper. UECPTools is NOT listed: this extension
			// has no internal-helper dependencies, so it can drop into a Base-only host
			// (mini-plugin) and work unchanged.
			"UECPCore",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			// Editor + asset plumbing.
			"UnrealEd", "AssetTools", "AssetRegistry", "EditorScriptingUtilities",
			"Slate", "SlateCore", "Kismet", "KismetCompiler", "BlueprintGraph",

			// Niagara — system / emitter / module / renderer / data interface
			// authoring + introspection. Includes the stateless-emitter API
			// (UNiagaraStatelessEmitter sub-objects in system packages) used by
			// CollectStatelessModules + DuplicateEmitterInSystem for FountainLightweight-
			// style sources.
			"Niagara", "NiagaraEditor",
			// NiagaraEditor's view models pull in ISequencerModule.h transitively
			// (NiagaraSystemScalabilityViewModel.h:12).
			"Sequencer",
		});
	}
}
