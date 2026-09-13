// Copyright 2026, BlueprintsLab, All rights reserved.

#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace ControlRigTools
{
	UECPCINEMATICSEXT_API void HandleCreateControlRig(const FString& AssetName, const FString& SavePath,
		const FString& SkeletalMeshPath, FString& OutJsonString, FString& OutError);

	UECPCINEMATICSEXT_API void HandleAddRigControl(const FString& AssetPath, const FString& ControlName,
		const FString& ControlType, const FString& ParentBone,
		FString& OutJsonString, FString& OutError);

	UECPCINEMATICSEXT_API void HandleAddRigTwoBoneIK(const FString& AssetPath,
		const FString& RootBone, const FString& MidBone, const FString& TipBone,
		int32 PosX, int32 PosY, FString& OutJsonString, FString& OutError);

	UECPCINEMATICSEXT_API void HandleGetControlRigSummary(const FString& AssetPath,
		FString& OutJsonString, FString& OutError);

	UECPCINEMATICSEXT_API void HandleAddRigVMNode(const FString& AssetPath, const FString& UnitStructPath,
		const FString& MethodName, int32 PosX, int32 PosY,
		FString& OutJsonString, FString& OutError);

	UECPCINEMATICSEXT_API void HandleConnectRigPins(const FString& AssetPath,
		const FString& SourcePinPath, const FString& TargetPinPath,
		FString& OutJsonString, FString& OutError);

	UECPCINEMATICSEXT_API void HandleSetRigPinValue(const FString& AssetPath,
		const FString& PinPath, const FString& Value,
		FString& OutJsonString, FString& OutError);

	UECPCINEMATICSEXT_API void HandleGetRigGraphNodes(const FString& AssetPath,
		FString& OutJsonString, FString& OutError);

	UECPCINEMATICSEXT_API void HandleGetRigGraphNodes(const FString& AssetPath, const FString& EventName,
		FString& OutJsonString, FString& OutError);

	UECPCINEMATICSEXT_API void HandleRemoveRigNode(const FString& AssetPath, const FString& NodeName,
		FString& OutJsonString, FString& OutError);

	UECPCINEMATICSEXT_API void HandleAddRigVariable(const FString& AssetPath, const FString& VariableName,
		const FString& CppType, bool bIsGetter, const FString& DefaultValue,
		int32 PosX, int32 PosY, FString& OutJsonString, FString& OutError);

	UECPCINEMATICSEXT_API void HandleBuildRigLogic(const FString& AssetPath,
		const FString& NodesJson, const FString& LinksJson,
		FString& OutJsonString, FString& OutError);

	UECPCINEMATICSEXT_API void HandleDisconnectRigPins(const FString& AssetPath,
		const FString& SourcePinPath, const FString& TargetPinPath,
		FString& OutJsonString, FString& OutError);

	UECPCINEMATICSEXT_API void HandleListRigVMNodeTypes(const FString& Filter,
		FString& OutJsonString, FString& OutError);

	UECPCINEMATICSEXT_API void HandleSetRigControlProperties(const FString& AssetPath, const FString& ControlName,
		const FString& PropertyName, const FString& PropertyValue,
		FString& OutJsonString, FString& OutError);

	UECPCINEMATICSEXT_API void HandleRemoveRigControl(const FString& AssetPath, const FString& ControlName,
		FString& OutJsonString, FString& OutError);

	UECPCINEMATICSEXT_API void HandleCompileControlRig(const FString& AssetPath,
		FString& OutJsonString, FString& OutError);
	UECPCINEMATICSEXT_API void HandleCompileControlRigFromArgs(const TSharedPtr<FJsonObject>& Args,
		FString& OutJsonString, FString& OutError);

	UECPCINEMATICSEXT_API void HandleGetRigControl(const FString& AssetPath, const FString& ControlName,
		FString& OutJsonString, FString& OutError);
	UECPCINEMATICSEXT_API void HandleGetRigControlFromArgs(const TSharedPtr<FJsonObject>& Args,
		FString& OutJsonString, FString& OutError);

	UECPCINEMATICSEXT_API void HandleAddRigNull(const FString& AssetPath, const FString& NullName,
		const FString& ParentName, FString& OutJsonString, FString& OutError);
	UECPCINEMATICSEXT_API void HandleAddRigNullFromArgs(const TSharedPtr<FJsonObject>& Args,
		FString& OutJsonString, FString& OutError);

	UECPCINEMATICSEXT_API void HandleReparentRigElement(const FString& AssetPath, const FString& ElementName,
		const FString& ElementType, const FString& NewParent, bool bMaintainGlobal,
		FString& OutJsonString, FString& OutError);
	UECPCINEMATICSEXT_API void HandleReparentRigElementFromArgs(const TSharedPtr<FJsonObject>& Args,
		FString& OutJsonString, FString& OutError);

	UECPCINEMATICSEXT_API void HandleAddRigSocketFromArgs(const TSharedPtr<FJsonObject>& Args,
		FString& OutJsonString, FString& OutError);

	UECPCINEMATICSEXT_API void HandleAddRigCurveFromArgs(const TSharedPtr<FJsonObject>& Args,
		FString& OutJsonString, FString& OutError);

	UECPCINEMATICSEXT_API void HandleAddRigFunctionFromArgs(const TSharedPtr<FJsonObject>& Args,
		FString& OutJsonString, FString& OutError);
	UECPCINEMATICSEXT_API void HandleAddRigFunctionPinFromArgs(const TSharedPtr<FJsonObject>& Args,
		FString& OutJsonString, FString& OutError);
	UECPCINEMATICSEXT_API void HandleAddRigFunctionNodeFromArgs(const TSharedPtr<FJsonObject>& Args,
		FString& OutJsonString, FString& OutError);

	UECPCINEMATICSEXT_API void HandleAddRigControlSpaceFromArgs(const TSharedPtr<FJsonObject>& Args,
		FString& OutJsonString, FString& OutError);

	UECPCINEMATICSEXT_API void HandleSetRigControlOffsetTransform(const FString& AssetPath, const FString& ControlName,
		const FString& TransformStr, FString& OutJsonString, FString& OutError);
	UECPCINEMATICSEXT_API void HandleSetRigControlOffsetTransformFromArgs(const TSharedPtr<FJsonObject>& Args,
		FString& OutJsonString, FString& OutError);
	UECPCINEMATICSEXT_API void HandleSetRigControlShapeTransform(const FString& AssetPath, const FString& ControlName,
		const FString& TransformStr, FString& OutJsonString, FString& OutError);
	UECPCINEMATICSEXT_API void HandleSetRigControlShapeTransformFromArgs(const TSharedPtr<FJsonObject>& Args,
		FString& OutJsonString, FString& OutError);

	UECPCINEMATICSEXT_API void HandleAddRigControlChainFromArgs(const TSharedPtr<FJsonObject>& Args,
		FString& OutJsonString, FString& OutError);

	UECPCINEMATICSEXT_API void HandleMirrorRigControlFromArgs(const TSharedPtr<FJsonObject>& Args,
		FString& OutJsonString, FString& OutError);

	UECPCINEMATICSEXT_API void HandleAddRigAimConstraintFromArgs(const TSharedPtr<FJsonObject>& Args,
		FString& OutJsonString, FString& OutError);

	UECPCINEMATICSEXT_API void HandleAddRigControlFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPCINEMATICSEXT_API void HandleAddRigVMNodeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPCINEMATICSEXT_API void HandleConnectRigPinsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPCINEMATICSEXT_API void HandleCreateControlRigFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPCINEMATICSEXT_API void HandleAddRigTwoBoneIKFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPCINEMATICSEXT_API void HandleGetControlRigSummaryFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPCINEMATICSEXT_API void HandleSetRigPinValueFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPCINEMATICSEXT_API void HandleGetRigGraphNodesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPCINEMATICSEXT_API void HandleRemoveRigNodeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPCINEMATICSEXT_API void HandleAddRigVariableFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPCINEMATICSEXT_API void HandleBuildRigLogicFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPCINEMATICSEXT_API void HandleDisconnectRigPinsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPCINEMATICSEXT_API void HandleListRigVMNodeTypesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPCINEMATICSEXT_API void HandleSetRigControlPropertiesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPCINEMATICSEXT_API void HandleRemoveRigControlFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
}
