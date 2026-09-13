// Copyright 2026, BlueprintsLab, All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace BehaviorTreeTools
{
	UECPAIEXT_API void HandleCreateBehaviorTree(const FString& Name, const FString& SavePath, const FString& BlackboardPath, FString& OutJsonString, FString& OutError);

	UECPAIEXT_API void HandleCreateBlackboard(const FString& Name, const FString& SavePath, const TArray<TSharedPtr<FJsonValue>>& Keys, FString& OutJsonString, FString& OutError);

	UECPAIEXT_API void HandleAddBlackboardKey(const FString& BBPath, const FString& KeyName, const FString& KeyType, const FString& ClassName, FString& OutJsonString, FString& OutError);

	UECPAIEXT_API void HandleAddBTComposite(const FString& BTPath, const FString& CompositeType, const FString& ParentNodeName, int32 PosX, int32 PosY, FString& OutJsonString, FString& OutError);

	UECPAIEXT_API void HandleAddBTTaskNode(const FString& BTPath, const FString& TaskClassPath, const FString& ParentNodeName, int32 PosX, int32 PosY, FString& OutJsonString, FString& OutError);

	UECPAIEXT_API void HandleAddBTService(const FString& BTPath, const FString& NodeName, const FString& ServiceClassPath, FString& OutJsonString, FString& OutError);

	UECPAIEXT_API void HandleAddBTDecorator(const FString& BTPath, const FString& NodeName, const FString& DecoratorClassPath, FString& OutJsonString, FString& OutError);

	UECPAIEXT_API void HandleSetBTNodeProperty(const FString& BTPath, const FString& NodeName,
		const FString& PropertyName, const FString& PropertyValue,
		FString& OutJsonString, FString& OutError);

	UECPAIEXT_API void HandleGetBTNodeProperty(const FString& BTPath, const FString& NodeName,
		const FString& PropertyName, FString& OutJsonString, FString& OutError);

	UECPAIEXT_API void HandleGetBlackboardKeys(const FString& BBPath, FString& OutJsonString, FString& OutError);

	UECPAIEXT_API void HandleRemoveBTNode(const FString& BTPath, const FString& NodeName, FString& OutJsonString, FString& OutError);

	UECPAIEXT_API void HandleRemoveBTDecorator(const FString& BTPath, const FString& NodeName, const FString& DecoratorClass, FString& OutJsonString, FString& OutError);

	UECPAIEXT_API void HandleRemoveBTService(const FString& BTPath, const FString& NodeName, const FString& ServiceClass, FString& OutJsonString, FString& OutError);

	UECPAIEXT_API void HandleReorderBTChildren(const FString& BTPath, const FString& ParentNodeName, const TArray<FString>& OrderedChildNames, FString& OutJsonString, FString& OutError);

	UECPAIEXT_API void HandleRemoveBlackboardKey(const FString& BBPath, const FString& KeyName, FString& OutJsonString, FString& OutError);

	UECPAIEXT_API void HandleRenameBlackboardKey(const FString& BBPath, const FString& OldName, const FString& NewName, FString& OutJsonString, FString& OutError);

	UECPAIEXT_API void HandleGetBTNodeDetails(const FString& BTPath, const FString& NodeName, FString& OutJsonString, FString& OutError);

	UECPAIEXT_API void HandleListBTNativeClasses(const FString& ClassType, FString& OutJsonString, FString& OutError);

	UECPAIEXT_API void HandleMoveBTNode(const FString& BTPath, const FString& NodeName, int32 PosX, int32 PosY, FString& OutJsonString, FString& OutError);

	UECPAIEXT_API void HandleSetBlackboardAsset(const FString& BTPath, const FString& BBPath, FString& OutJsonString, FString& OutError);

	UECPAIEXT_API void HandleGetBTGraphNodes(const FString& BTPath, FString& OutJsonString, FString& OutError);

	UECPAIEXT_API void HandleReparentBTNode(const FString& BTPath, const FString& NodeName, const FString& NewParentName, FString& OutJsonString, FString& OutError);

	UECPAIEXT_API void HandleSetBlackboardParent(const FString& BBPath, const FString& ParentBBPath, FString& OutJsonString, FString& OutError);

	UECPAIEXT_API void HandleAddBlackboardKeyFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPAIEXT_API void HandleRemoveBlackboardKeyFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPAIEXT_API void HandleAddBTCompositeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPAIEXT_API void HandleAddBTTaskNodeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPAIEXT_API void HandleAddBTServiceFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPAIEXT_API void HandleAddBTDecoratorFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPAIEXT_API void HandleRemoveBTNodeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPAIEXT_API void HandleAutoLayoutBT(const FString& BTPath, FString& OutJsonString, FString& OutError);

	UECPAIEXT_API void HandleCreateBehaviorTreeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPAIEXT_API void HandleCreateBlackboardFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPAIEXT_API void HandleSetBTNodePropertyFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPAIEXT_API void HandleGetBTNodePropertyFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPAIEXT_API void HandleGetBlackboardKeysFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPAIEXT_API void HandleRemoveBTDecoratorFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPAIEXT_API void HandleRemoveBTServiceFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPAIEXT_API void HandleReorderBTChildrenFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPAIEXT_API void HandleRenameBlackboardKeyFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPAIEXT_API void HandleGetBTNodeDetailsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPAIEXT_API void HandleListBTNativeClassesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPAIEXT_API void HandleMoveBTNodeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPAIEXT_API void HandleSetBlackboardAssetFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPAIEXT_API void HandleGetBTGraphNodesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPAIEXT_API void HandleReparentBTNodeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPAIEXT_API void HandleSetBlackboardParentFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPAIEXT_API void HandleAutoLayoutBTFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPAIEXT_API void HandleBuildBTTreeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPAIEXT_API void HandleGetBehaviorTreeSummaryFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
}
