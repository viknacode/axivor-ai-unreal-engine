// Copyright 2026, BlueprintsLab, All rights reserved.

#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace StateTreeTools
{
	UECPAIEXT_API void HandleCreateStateTree(const FString& AssetName, const FString& SavePath,
		const FString& SchemaClass,
		FString& OutJsonString, FString& OutError);

	UECPAIEXT_API void HandleAddStateTreeState(const FString& AssetPath, const FString& StateName,
		const FString& ParentStateName,
		FString& OutJsonString, FString& OutError);

	UECPAIEXT_API void HandleAddStateTreeTransition(const FString& AssetPath,
		const FString& FromState, const FString& ToState,
		const FString& TriggerType,
		FString& OutJsonString, FString& OutError);

	UECPAIEXT_API void HandleGetStateTreeSummary(const FString& AssetPath,
		FString& OutJsonString, FString& OutError);

	UECPAIEXT_API void HandleGetStateTreeSchema(const FString& AssetPath,
		FString& OutJsonString, FString& OutError);

	UECPAIEXT_API void HandleAddStateTreeTask(const FString& AssetPath, const FString& StateName,
		const FString& TaskClass, const FString& TaskLabel,
		FString& OutJsonString, FString& OutError);

	UECPAIEXT_API void HandleSetStateTreeTaskProperty(const FString& AssetPath, const FString& StateName,
		const FString& TaskLabel, const FString& PropertyName, const FString& PropertyValue,
		FString& OutJsonString, FString& OutError);

	UECPAIEXT_API void HandleAddStateTreeEvaluator(const FString& AssetPath,
		const FString& EvaluatorClass, const FString& EvaluatorLabel,
		FString& OutJsonString, FString& OutError);

	UECPAIEXT_API void HandleAddStateTreeCondition(const FString& AssetPath, const FString& StateName,
		const FString& ConditionClass, const FString& ConditionLabel,
		FString& OutJsonString, FString& OutError);

	UECPAIEXT_API void HandleGetStateTreeStateDetails(const FString& AssetPath, const FString& StateName,
		FString& OutJsonString, FString& OutError);

	UECPAIEXT_API void HandleDeleteStateTreeState(const FString& AssetPath, const FString& StateName,
		FString& OutJsonString, FString& OutError);

	UECPAIEXT_API void HandleSetStateTreeStateType(const FString& AssetPath, const FString& StateName,
		const FString& StateType,
		FString& OutJsonString, FString& OutError);

	UECPAIEXT_API void HandleListStateTreeTaskClasses(FString& OutJsonString, FString& OutError);

	UECPAIEXT_API void HandleDeleteStateTreeTask(const FString& AssetPath, const FString& StateName,
		const FString& TaskClassOrIndex, FString& OutJsonString, FString& OutError);

	UECPAIEXT_API void HandleDeleteStateTreeEvaluator(const FString& AssetPath,
		const FString& EvaluatorClassOrIndex, FString& OutJsonString, FString& OutError);

	UECPAIEXT_API void HandleDeleteStateTreeCondition(const FString& AssetPath, const FString& StateName,
		const FString& ConditionClassOrIndex, FString& OutJsonString, FString& OutError);

	UECPAIEXT_API void HandleSetStateTreeEvaluatorProperty(const FString& AssetPath,
		const FString& EvaluatorClassOrIndex, const FString& PropertyName, const FString& PropertyValue,
		FString& OutJsonString, FString& OutError);

	UECPAIEXT_API void HandleSetStateTreeConditionProperty(const FString& AssetPath, const FString& StateName,
		const FString& ConditionClassOrIndex, const FString& PropertyName, const FString& PropertyValue,
		const FString& TriggerType, const FString& ToState,
		FString& OutJsonString, FString& OutError);

	UECPAIEXT_API void HandleCompileStateTree(const FString& AssetPath, FString& OutJsonString, FString& OutError);

	UECPAIEXT_API void HandleRenameStateTreeState(const FString& AssetPath, const FString& StateName,
		const FString& NewName, FString& OutJsonString, FString& OutError);

	UECPAIEXT_API void HandleAddStateTreeTransitionCondition(const FString& AssetPath, const FString& StateName,
		const FString& TriggerType, const FString& ToState,
		const FString& ConditionClass, FString& OutJsonString, FString& OutError);

	UECPAIEXT_API void HandleSetStateTreeLinkedState(const FString& AssetPath, const FString& StateName,
		const FString& LinkedStateName, FString& OutJsonString, FString& OutError);

	UECPAIEXT_API void HandleReorderStateTreeTasks(const FString& AssetPath, const FString& StateName,
		const FString& FromClassOrIndex, const FString& ToIndex,
		FString& OutJsonString, FString& OutError);

	UECPAIEXT_API void HandleGetStateTreeEvaluators(const FString& AssetPath, FString& OutJsonString, FString& OutError);

	UECPAIEXT_API void HandleDeleteStateTreeTransition(const FString& AssetPath, const FString& StateName,
		const FString& TriggerType, const FString& ToState,
		FString& OutJsonString, FString& OutError);

	UECPAIEXT_API void HandleSetStateTreeTransition(const FString& AssetPath, const FString& StateName,
		const FString& TriggerType, const FString& ToState,
		const FString& NewTrigger, const FString& NewToState,
		FString& OutJsonString, FString& OutError);

	UECPAIEXT_API void HandleDeleteStateTreeTransitionCondition(const FString& AssetPath, const FString& StateName,
		const FString& TriggerType, const FString& ToState,
		const FString& ConditionClassOrIndex, FString& OutJsonString, FString& OutError);

	UECPAIEXT_API void HandleAddStateTreeParameter(const FString& AssetPath, const FString& ParamName,
		const FString& ParamType, FString& OutJsonString, FString& OutError);

	UECPAIEXT_API void HandleSetStateTreeParameterDefaultFromArgs(const TSharedPtr<FJsonObject>& Args,
		FString& OutJsonString, FString& OutError);

	UECPAIEXT_API void HandleGetStateTreeParameterDefaultFromArgs(const TSharedPtr<FJsonObject>& Args,
		FString& OutJsonString, FString& OutError);

	UECPAIEXT_API void HandleGetStateTreeParameters(const FString& AssetPath, FString& OutJsonString, FString& OutError);

	UECPAIEXT_API void HandleRemoveStateTreeParameter(const FString& AssetPath, const FString& ParamName,
		FString& OutJsonString, FString& OutError);

	UECPAIEXT_API void HandleSetStateTreeSchema(const FString& AssetPath, const FString& SchemaClass,
		FString& OutJsonString, FString& OutError);

	UECPAIEXT_API void HandleReorderStateTreeEvaluators(const FString& AssetPath,
		const FString& FromClassOrIndex, const FString& ToIndex,
		FString& OutJsonString, FString& OutError);

	UECPAIEXT_API void HandleSetStateTreeStateSelectionBehavior(const FString& AssetPath, const FString& StateName,
		const FString& SelectionBehavior, FString& OutJsonString, FString& OutError);

	UECPAIEXT_API void HandleSetConditionOperand(const FString& AssetPath, const FString& StateName,
		const FString& ConditionClassOrIndex, const FString& Operand,
		const FString& Context, const FString& TriggerType, const FString& ToState,
		FString& OutJsonString, FString& OutError);

	UECPAIEXT_API void HandleBindStateTreeProperty(const FString& AssetPath, const FString& StateName,
		const FString& NodeClass, const FString& NodeType, const FString& PropertyName,
		const FString& Source, const FString& Context,
		const FString& TriggerType, const FString& ToState,
		FString& OutJsonString, FString& OutError);

	UECPAIEXT_API void HandleGetStateTreeBindings(const FString& AssetPath, FString& OutJsonString, FString& OutError);

	UECPAIEXT_API void HandleSetStateTreeTaskClass(const FString& AssetPath, const FString& StateName,
		const FString& TaskClassOrIndex, const FString& BlueprintClassPath,
		FString& OutJsonString, FString& OutError);

	UECPAIEXT_API void HandleSetStateTreeComponentAsset(const FString& BlueprintPath, const FString& ComponentName,
		const FString& StateTreePath, FString& OutJsonString, FString& OutError);

	UECPAIEXT_API void HandleRemoveStateTreeCondition(const FString& AssetPath, const FString& StateName,
		int32 ConditionIndex, FString& OutJsonString, FString& OutError);

	UECPAIEXT_API void HandleRemoveStateTreeEvaluator(const FString& AssetPath,
		int32 EvaluatorIndex, FString& OutJsonString, FString& OutError);

	UECPAIEXT_API void HandleAddStateTreeStateFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPAIEXT_API void HandleAddStateTreeTaskFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPAIEXT_API void HandleAddStateTreeTransitionFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPAIEXT_API void HandleDeleteStateTreeStateFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPAIEXT_API void HandleCreateStateTreeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPAIEXT_API void HandleGetStateTreeSummaryFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPAIEXT_API void HandleGetStateTreeSchemaFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPAIEXT_API void HandleSetStateTreeTaskPropertyFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPAIEXT_API void HandleAddStateTreeEvaluatorFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPAIEXT_API void HandleAddStateTreeConditionFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPAIEXT_API void HandleGetStateTreeStateDetailsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPAIEXT_API void HandleSetStateTreeStateTypeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPAIEXT_API void HandleListStateTreeTaskClassesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPAIEXT_API void HandleDeleteStateTreeTaskFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPAIEXT_API void HandleDeleteStateTreeEvaluatorFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPAIEXT_API void HandleDeleteStateTreeConditionFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPAIEXT_API void HandleSetStateTreeEvaluatorPropertyFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPAIEXT_API void HandleSetStateTreeConditionPropertyFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPAIEXT_API void HandleCompileStateTreeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPAIEXT_API void HandleRenameStateTreeStateFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPAIEXT_API void HandleAddStateTreeTransitionConditionFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPAIEXT_API void HandleSetStateTreeLinkedStateFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPAIEXT_API void HandleReorderStateTreeTasksFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPAIEXT_API void HandleGetStateTreeEvaluatorsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPAIEXT_API void HandleDeleteStateTreeTransitionFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPAIEXT_API void HandleSetStateTreeTransitionFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPAIEXT_API void HandleDeleteStateTreeTransitionConditionFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPAIEXT_API void HandleAddStateTreeParameterFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPAIEXT_API void HandleGetStateTreeParametersFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPAIEXT_API void HandleRemoveStateTreeParameterFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPAIEXT_API void HandleSetStateTreeSchemaFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPAIEXT_API void HandleReorderStateTreeEvaluatorsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPAIEXT_API void HandleSetStateTreeStateSelectionBehaviorFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPAIEXT_API void HandleSetConditionOperandFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPAIEXT_API void HandleBindStateTreePropertyFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPAIEXT_API void HandleGetStateTreeBindingsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPAIEXT_API void HandleSetStateTreeTaskClassFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPAIEXT_API void HandleSetStateTreeComponentAssetFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPAIEXT_API void HandleRemoveStateTreeConditionFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPAIEXT_API void HandleRemoveStateTreeEvaluatorFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
}
