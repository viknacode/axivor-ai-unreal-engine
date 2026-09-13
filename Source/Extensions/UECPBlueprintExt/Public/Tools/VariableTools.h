// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace VariableTools
{
	UECPBLUEPRINTEXT_API void HandleAddVariable(const FString& BpPath, const FString& VarName, const FString& VarType, const FString& DefaultValue, const FString& Category, FString& OutError);

	UECPBLUEPRINTEXT_API void HandleAddLocalVariable(const FString& BpPath, const FString& FunctionName, const FString& VarName, const FString& VarType, const FString& DefaultValue, FString& OutJsonString, FString& OutError);

	UECPBLUEPRINTEXT_API void HandleAddVariablesBulk(const FString& BpPath, const TArray<TSharedPtr<FJsonValue>>& Variables, FString& OutJsonString, FString& OutError);
	UECPBLUEPRINTEXT_API void HandleCategorizeVariables(const FString& BpPath, const TSharedPtr<FJsonObject>* Categories, FString& OutJsonString, FString& OutError);

	UECPBLUEPRINTEXT_API void HandleSetBlueprintVariableDefault(const FString& BpPath, const FString& VarName, const FString& NewDefault, FString& OutJsonString, FString& OutError);

	UECPBLUEPRINTEXT_API void HandleRenameVariable(const FString& BpPath, const FString& OldName, const FString& NewName, FString& OutJsonString, FString& OutError);

	UECPBLUEPRINTEXT_API void HandleSetVariableFlags(const FString& BpPath, const FString& VarName, const FString& InstanceEditable, const FString& ExposeOnSpawn, const FString& Tooltip, const FString& BlueprintReadOnly, const FString& Replication, const FString& SaveGame, const FString& AdvancedDisplay, FString& OutJsonString, FString& OutError);

	UECPBLUEPRINTEXT_API void HandleSetVariableMetadataFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPBLUEPRINTEXT_API void HandleSetFunctionReplication(const FString& BpPath, const FString& FunctionName,
		const FString& Replication, bool bReliable, bool bWithValidation,
		FString& OutJsonString, FString& OutError);

	UECPBLUEPRINTEXT_API void HandleAddEventDispatcher(const FString& BpPath, const FString& DispatcherName,
		const TArray<TSharedPtr<FJsonValue>>* Params, FString& OutJsonString, FString& OutError);

	UECPBLUEPRINTEXT_API void HandleDeleteEventDispatcher(const FString& BpPath, const FString& DispatcherName,
		FString& OutJsonString, FString& OutError);

	UECPBLUEPRINTEXT_API void HandleAddDispatcherParam(const FString& BpPath, const FString& DispatcherName,
		const FString& ParamName, const FString& ParamType, FString& OutJsonString, FString& OutError);

	UECPBLUEPRINTEXT_API void HandleAddDispatcherParamFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPBLUEPRINTEXT_API void HandleRemoveDispatcherParam(const FString& BpPath, const FString& DispatcherName,
		const FString& ParamName, FString& OutJsonString, FString& OutError);

	UECPBLUEPRINTEXT_API void HandleAddFunctionParam(const FString& BpPath, const FString& FunctionName, const FString& ParamName, const FString& ParamType, bool bIsOutput, FString& OutJsonString, FString& OutError);

	UECPBLUEPRINTEXT_API void HandleAddFunctionParamFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPBLUEPRINTEXT_API void HandleRemoveFunctionParam(const FString& BpPath, const FString& FunctionName, const FString& ParamName, FString& OutJsonString, FString& OutError);

	UECPBLUEPRINTEXT_API void HandleSetFunctionAccess(const FString& BpPath, const FString& FunctionName, const FString& Access, FString& OutJsonString, FString& OutError);

	UECPBLUEPRINTEXT_API void HandleSetFunctionPure(const FString& BpPath, const FString& FunctionName, bool bPure, FString& OutJsonString, FString& OutError);

	UECPBLUEPRINTEXT_API void HandleListOverridableFunctions(const FString& BpPath, FString& OutJsonString, FString& OutError);

	UECPBLUEPRINTEXT_API void HandleReparentBlueprint(const FString& BpPath, const FString& NewParentClass, FString& OutJsonString, FString& OutError);

	UECPBLUEPRINTEXT_API void HandleAddVariableFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPBLUEPRINTEXT_API void HandleAddLocalVariableFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPBLUEPRINTEXT_API void HandleSetBlueprintVariableDefaultFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPBLUEPRINTEXT_API void HandleRenameVariableFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPBLUEPRINTEXT_API void HandleSetVariableFlagsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPBLUEPRINTEXT_API void HandleSetVariableExposeOnSpawnFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPBLUEPRINTEXT_API void HandleSetVariableReplicationConditionFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPBLUEPRINTEXT_API void HandleCategorizeVariablesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPBLUEPRINTEXT_API void HandleAddEventDispatcherFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPBLUEPRINTEXT_API void HandleDeleteEventDispatcherFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPBLUEPRINTEXT_API void HandleRemoveDispatcherParamFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPBLUEPRINTEXT_API void HandleSetFunctionReplicationFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPBLUEPRINTEXT_API void HandleRemoveFunctionParamFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPBLUEPRINTEXT_API void HandleSetFunctionAccessFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPBLUEPRINTEXT_API void HandleSetFunctionPureFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPBLUEPRINTEXT_API void HandleListOverridableFunctionsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPBLUEPRINTEXT_API void HandleReparentBlueprintFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPBLUEPRINTEXT_API void HandleSetVariableReplicationCondition(const FString& BpPath, const FString& VarName,
		const FString& Condition, FString& OutJsonString, FString& OutError);
}
