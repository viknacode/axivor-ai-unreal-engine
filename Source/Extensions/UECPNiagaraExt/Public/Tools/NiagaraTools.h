// Copyright 2026, BlueprintsLab, All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace NiagaraTools
{

	UECPNIAGARAEXT_API void HandleCreateNiagaraSystem(const FString& Name, const FString& SavePath, const FString& TemplatePath, FString& OutJsonString, FString& OutError);

	UECPNIAGARAEXT_API void HandleCreateNiagaraEmitter(const FString& Name, const FString& SavePath, const FString& TemplatePath, FString& OutJsonString, FString& OutError);

	UECPNIAGARAEXT_API void HandleAddEmitterToSystem(const FString& SystemPath, const FString& EmitterPath, FString& OutJsonString, FString& OutError);

	UECPNIAGARAEXT_API void HandleSetNiagaraParameter(const FString& SystemPath, const FString& ParameterName, const TSharedPtr<FJsonObject>& ValueJson, FString& OutJsonString, FString& OutError);

	UECPNIAGARAEXT_API void HandleGetNiagaraSummary(const FString& SystemPath, FString& OutJsonString, FString& OutError);

	UECPNIAGARAEXT_API void HandleListNiagaraTemplates(const FString& Filter, bool bSystemsOnly, bool bEmittersOnly, FString& OutJsonString, FString& OutError);

	UECPNIAGARAEXT_API void HandleSetNiagaraRendererMaterial(const FString& SystemPath, const FString& EmitterName, const FString& MaterialPath, int32 RendererIndex, FString& OutJsonString, FString& OutError);

	UECPNIAGARAEXT_API void HandleGetNiagaraDetailedSummary(const FString& SystemPath, FString& OutJsonString, FString& OutError);

	UECPNIAGARAEXT_API void HandleSetNiagaraRendererProperty(const FString& SystemPath, const FString& EmitterName, int32 RendererIndex, const FString& PropertyName, const TSharedPtr<FJsonObject>& ValueJson, FString& OutJsonString, FString& OutError);

	UECPNIAGARAEXT_API void HandleSetMeshRendererMesh(const FString& SystemPath, const FString& EmitterName, int32 RendererIndex, int32 MeshIndex, const FString& MeshPath, const FString& MaterialPath, FString& OutJsonString, FString& OutError);

	UECPNIAGARAEXT_API void HandleSetNiagaraModuleParameter(const FString& SystemPath, const FString& EmitterName, const FString& ScriptSection, const FString& ParameterName, const FString& ModuleName, const TSharedPtr<FJsonObject>& ValueJson, FString& OutJsonString, FString& OutError);

	UECPNIAGARAEXT_API void HandleSetNiagaraModuleParametersBulk(const FString& SystemPath, const FString& EmitterName, const TSharedPtr<FJsonObject>& ArgsJson, FString& OutJsonString, FString& OutError);

	UECPNIAGARAEXT_API void HandleSetNiagaraDistributionCurve(const FString& SystemPath, const FString& EmitterName, const FString& ModuleName, const FString& PropertyName, const TSharedPtr<FJsonObject>& ArgsJson, FString& OutJsonString, FString& OutError);

	UECPNIAGARAEXT_API void HandleAddNiagaraModule(const FString& SystemPath, const FString& EmitterName, const FString& ScriptSection, const FString& ModulePath, int32 InsertIndex, FString& OutJsonString, FString& OutError);

	UECPNIAGARAEXT_API void HandleAddNiagaraModuleEx(const FString& SystemPath, const FString& EmitterName, const FString& ScriptSection, const FString& ModulePath, int32 InsertIndex, const FString& EventName, FString& OutJsonString, FString& OutError);

	UECPNIAGARAEXT_API void HandleListNiagaraModules(const FString& Filter, FString& OutJsonString, FString& OutError);

	UECPNIAGARAEXT_API void HandleRemoveEmitterFromSystem(const FString& SystemPath, const FString& EmitterName, FString& OutJsonString, FString& OutError);

	UECPNIAGARAEXT_API void HandleRemoveNiagaraModule(const FString& SystemPath, const FString& EmitterName, const FString& ModuleName, FString& OutJsonString, FString& OutError);

	UECPNIAGARAEXT_API void HandleSetEmitterProperties(const FString& SystemPath, const FString& EmitterName, const TSharedPtr<FJsonObject>& ValueJson, FString& OutJsonString, FString& OutError);

	UECPNIAGARAEXT_API void HandleAddRendererToEmitter(const FString& SystemPath, const FString& EmitterName, const FString& RendererType, FString& OutJsonString, FString& OutError);

	UECPNIAGARAEXT_API void HandleSetNiagaraSystemProperties(const FString& SystemPath, const TSharedPtr<FJsonObject>& ValueJson, FString& OutJsonString, FString& OutError);

	UECPNIAGARAEXT_API void HandleDuplicateEmitterInSystem(const FString& SystemPath, const FString& SourceEmitterName, const FString& NewEmitterName, FString& OutJsonString, FString& OutError);

	UECPNIAGARAEXT_API void HandleSetEmitterScalability(const FString& SystemPath, const FString& EmitterName, const TSharedPtr<FJsonObject>& ValueJson, FString& OutJsonString, FString& OutError);

	UECPNIAGARAEXT_API void HandleRemoveNiagaraRenderer(const FString& SystemPath, const FString& EmitterName, int32 RendererIndex, FString& OutJsonString, FString& OutError);

	UECPNIAGARAEXT_API void HandleGetEmitterModules(const FString& SystemPath, const FString& EmitterName, FString& OutJsonString, FString& OutError);

	UECPNIAGARAEXT_API void HandleSetEmitterSpawnRate(const FString& SystemPath, const FString& EmitterName, float SpawnRate, int32 BurstCount, float BurstTime, FString& OutJsonString, FString& OutError);

	UECPNIAGARAEXT_API void HandleSetNiagaraSimTarget(const FString& SystemPath, const FString& EmitterName, const FString& SimTarget, FString& OutJsonString, FString& OutError);

	UECPNIAGARAEXT_API void HandleRenameEmitterInSystem(const FString& SystemPath, const FString& OldEmitterName,
		const FString& NewEmitterName, FString& OutJsonString, FString& OutError);

	UECPNIAGARAEXT_API void HandleAddNiagaraEventHandler(const FString& SystemPath, const FString& EmitterName,
		const FString& SourceEmitterName, const FString& EventName, const FString& ScriptPath,
		FString& OutJsonString, FString& OutError);

	UECPNIAGARAEXT_API void HandleSetNiagaraDataInterface(const FString& SystemPath, const FString& EmitterName,
		const FString& DataInterfaceName, const FString& PropertyName, const FString& PropertyValue,
		FString& OutJsonString, FString& OutError);

	UECPNIAGARAEXT_API void HandleAddNiagaraModuleFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPNIAGARAEXT_API void HandleRemoveNiagaraModuleFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPNIAGARAEXT_API void HandleSetNiagaraModuleParameterFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPNIAGARAEXT_API void HandleAddEmitterToSystemFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPNIAGARAEXT_API void HandleDuplicateEmitterInSystemFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPNIAGARAEXT_API void HandleCreateNiagaraSystemFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPNIAGARAEXT_API void HandleCreateNiagaraEmitterFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPNIAGARAEXT_API void HandleSetNiagaraParameterFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPNIAGARAEXT_API void HandleGetNiagaraSummaryFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPNIAGARAEXT_API void HandleListNiagaraTemplatesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPNIAGARAEXT_API void HandleSetNiagaraRendererMaterialFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPNIAGARAEXT_API void HandleGetNiagaraDetailedSummaryFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPNIAGARAEXT_API void HandleSetNiagaraRendererPropertyFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPNIAGARAEXT_API void HandleSetMeshRendererMeshFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPNIAGARAEXT_API void HandleValidateNiagaraSystemFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPNIAGARAEXT_API void HandleResolveNiagaraDependenciesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPNIAGARAEXT_API void HandleSetNiagaraDistributionCurveFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPNIAGARAEXT_API void HandleListNiagaraModulesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPNIAGARAEXT_API void HandleRemoveEmitterFromSystemFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPNIAGARAEXT_API void HandleSetEmitterPropertiesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPNIAGARAEXT_API void HandleAddRendererToEmitterFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPNIAGARAEXT_API void HandleSetNiagaraSystemPropertiesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPNIAGARAEXT_API void HandleSetEmitterScalabilityFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPNIAGARAEXT_API void HandleRemoveNiagaraRendererFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPNIAGARAEXT_API void HandleGetEmitterModulesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPNIAGARAEXT_API void HandleSetEmitterSpawnRateFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPNIAGARAEXT_API void HandleSetNiagaraSimTargetFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPNIAGARAEXT_API void HandleRenameEmitterInSystemFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPNIAGARAEXT_API void HandleAddNiagaraEventHandlerFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPNIAGARAEXT_API void HandleSetNiagaraDataInterfaceFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPNIAGARAEXT_API void HandleSetNiagaraDistributionModeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPNIAGARAEXT_API void HandleSetNiagaraModuleEnabledFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPNIAGARAEXT_API void HandleBindNiagaraModuleInputFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPNIAGARAEXT_API void HandleUnbindNiagaraModuleInputFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPNIAGARAEXT_API void HandleSetNiagaraRendererBindingFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPNIAGARAEXT_API void HandleAddNiagaraScratchModuleFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPNIAGARAEXT_API void HandleGetNiagaraRuntimeStateFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPNIAGARAEXT_API void HandleCleanupNiagaraSystemOrphansFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
}
