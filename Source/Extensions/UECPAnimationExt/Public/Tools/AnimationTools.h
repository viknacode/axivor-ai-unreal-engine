// Copyright 2026, BlueprintsLab, All rights reserved.

#pragma once

#include "CoreMinimal.h"

namespace AnimationTools
{
	UECPANIMATIONEXT_API void HandleCreateAnimBlueprint(
		const FString& Name,
		const FString& SavePath,
		const FString& SkeletonPath,
		const FString& StateMachineName,
		FString& OutJsonString, FString& OutError);

	UECPANIMATIONEXT_API void HandleAddAnimState(
		const FString& AnimBlueprintPath,
		const FString& StateMachineName,
		const FString& StateName,
		const FString& AnimationPath,
		int32 PosX, int32 PosY,
		FString& OutJsonString, FString& OutError,
		const FString& BlendVariableX = TEXT(""),
		const FString& BlendVariableY = TEXT(""));

	UECPANIMATIONEXT_API void HandleAddStateTransition(
		const FString& AnimBlueprintPath,
		const FString& StateMachineName,
		const FString& FromState,
		const FString& ToState,
		bool bBidirectional,
		float CrossfadeDuration,
		FString& OutJsonString, FString& OutError);

	UECPANIMATIONEXT_API void HandleSetTransitionRule(
		const FString& AnimBlueprintPath,
		const FString& StateMachineName,
		const FString& FromState,
		const FString& ToState,
		const FString& RuleType,
		const FString& VariableName,
		const FString& CompareOp,
		float CompareValue,
		FString& OutJsonString, FString& OutError);

	UECPANIMATIONEXT_API void HandleCreateBlendspace(
		const FString& Name,
		const FString& SavePath,
		const FString& SkeletonPath,
		bool bIs1D,
		const FString& AxisName,
		float AxisMin,
		float AxisMax,
		FString& OutJsonString, FString& OutError);

	// TimeNormalized >= 0 overrides TimePosition with a 0..1 position along the clip.
	UECPANIMATIONEXT_API void HandleAddAnimNotify(
		const FString& AnimationPath,
		const FString& NotifyName,
		const FString& NotifyClass,
		float TimePosition,
		FString& OutJsonString, FString& OutError,
		int32 TrackIndex = 0,
		float TimeNormalized = -1.0f);

	UECPANIMATIONEXT_API void HandleCreateAnimMontage(
		const FString& Name,
		const FString& SavePath,
		const FString& SkeletonPath,
		FString& OutJsonString, FString& OutError);

	UECPANIMATIONEXT_API void HandleCreateAnimSequence(
		const FString& Name,
		const FString& SavePath,
		const FString& SkeletonPath,
		float Fps,
		float Duration,
		FString& OutJsonString, FString& OutError);

	UECPANIMATIONEXT_API void HandleCreateAnimSequenceFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPANIMATIONEXT_API void HandleSetAnimSequenceKeysFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPANIMATIONEXT_API void HandleSetAnimTransformCurveKeysFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPANIMATIONEXT_API void HandleGetAnimSequenceTracksFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPANIMATIONEXT_API void HandleAnalyzeAnimMotionFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPANIMATIONEXT_API void HandleDetectAnimDiscontinuitiesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPANIMATIONEXT_API void HandleRenderAnimSequenceThumbnailFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPANIMATIONEXT_API void HandleAddMontageSection(
		const FString& MontagePath,
		const FString& SectionName,
		float StartTime,
		FString& OutJsonString, FString& OutError);

	UECPANIMATIONEXT_API void HandleLinkMontageSlot(
		const FString& MontagePath,
		const FString& SlotName,
		const FString& AnimationPath,
		FString& OutJsonString, FString& OutError,
		float StartTime = -1.0f);

	UECPANIMATIONEXT_API void HandleSetMontageSlotName(
		const FString& MontagePath,
		const FString& OldSlotName,
		const FString& NewSlotName,
		FString& OutJsonString, FString& OutError);

	UECPANIMATIONEXT_API void HandleAddAnimConduit(
		const FString& AnimBlueprintPath,
		const FString& ConduitName,
		const FString& StateMachineName,
		int32 PosX, int32 PosY,
		FString& OutJsonString, FString& OutError);

	UECPANIMATIONEXT_API void HandleCreateAnimComposite(
		const FString& Name,
		const FString& SavePath,
		const FString& SkeletonPath,
		FString& OutJsonString, FString& OutError);

	UECPANIMATIONEXT_API void HandleCreatePoseAsset(
		const FString& Name,
		const FString& SavePath,
		const FString& AnimationPath,
		const FString& SkeletonPath,
		FString& OutJsonString, FString& OutError);

	UECPANIMATIONEXT_API void HandleCreateAimOffset(
		const FString& Name,
		const FString& SavePath,
		const FString& SkeletonPath,
		bool bIs1D,
		FString& OutJsonString, FString& OutError);

	UECPANIMATIONEXT_API void HandleAddAnimCurve(
		const FString& AnimationPath,
		const FString& CurveName,
		const FString& CurveType,
		FString& OutJsonString, FString& OutError);

	UECPANIMATIONEXT_API void HandleCreateIKRig(
		const FString& Name,
		const FString& SavePath,
		const FString& SkeletonPath,
		const FString& RootBone,
		FString& OutJsonString, FString& OutError);

	UECPANIMATIONEXT_API void HandleAddIKSolver(
		const FString& IKRigPath,
		const FString& SolverType,
		FString& OutJsonString, FString& OutError);

	UECPANIMATIONEXT_API void HandleAddIKGoal(
		const FString& IKRigPath,
		const FString& GoalName,
		const FString& BoneName,
		int32 SolverIndex,
		FString& OutJsonString, FString& OutError);

	UECPANIMATIONEXT_API void HandleAddRetargetChain(
		const FString& IKRigPath,
		const FString& ChainName,
		const FString& StartBone,
		const FString& EndBone,
		const FString& GoalName,
		FString& OutJsonString, FString& OutError);

	UECPANIMATIONEXT_API void HandleCreateIKRetargeter(
		const FString& Name,
		const FString& SavePath,
		const FString& SourceIKRigPath,
		const FString& TargetIKRigPath,
		FString& OutJsonString, FString& OutError);

	UECPANIMATIONEXT_API void HandleMapRetargetChain(
		const FString& RetargeterPath,
		const FString& SourceChain,
		const FString& TargetChain,
		bool bAutoMap,
		FString& OutJsonString, FString& OutError);

	UECPANIMATIONEXT_API void HandleAddBlendspaceSample(
		const FString& BlendspacePath,
		const FString& AnimationPath,
		float SampleX,
		float SampleY,
		float SampleZ,
		FString& OutJsonString, FString& OutError);

	UECPANIMATIONEXT_API void HandleSetAnimSequenceSettings(
		const FString& AnimationPath,
		float RateScale, bool bSetRateScale,
		bool bEnableRootMotion, bool bSetRootMotion,
		const FString& RootMotionRootLock,
		bool bLoop, bool bSetLoop,
		FString& OutJsonString, FString& OutError);

	UECPANIMATIONEXT_API void HandleSetMontageBlendSettings(
		const FString& MontagePath,
		float BlendInTime, bool bSetBlendIn,
		float BlendOutTime, bool bSetBlendOut,
		float RateScale, bool bSetRateScale,
		bool bLoop, bool bSetLoop,
		FString& OutJsonString, FString& OutError);

	// StartTimeNormalized >= 0 overrides StartTime with a 0..1 position along the clip.
	UECPANIMATIONEXT_API void HandleAddAnimNotifyState(
		const FString& AnimationPath,
		const FString& NotifyStateName,
		float StartTime,
		float Duration,
		const FString& NotifyStateClass,
		FString& OutJsonString, FString& OutError,
		int32 TrackIndex = 0,
		float StartTimeNormalized = -1.0f);

	UECPANIMATIONEXT_API void HandleAddPlayNiagaraNotifyFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPANIMATIONEXT_API void HandleAddPlaySoundNotifyFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPANIMATIONEXT_API void HandleSetTransitionBlendSettings(
		const FString& AnimBlueprintPath,
		const FString& StateMachineName,
		const FString& FromState,
		const FString& ToState,
		float CrossfadeDuration, bool bSetDuration,
		const FString& BlendMode,
		FString& OutJsonString, FString& OutError);

	UECPANIMATIONEXT_API void HandleSetAnimNotifyProperty(
		const FString& AssetPath,
		const FString& NotifyName,
		float TimePosition,
		const FString& PropertyName,
		const FString& PropertyValue,
		FString& OutJson, FString& OutError);

	UECPANIMATIONEXT_API void HandleMoveAnimNotify(
		const FString& AssetPath,
		const FString& NotifyName,
		float TimePosition,
		float NewTime,
		int32 NewTrackIndex,
		const FString& NewTrackName,
		FString& OutJson, FString& OutError);

	UECPANIMATIONEXT_API void HandleSetAnimNotifyDuration(
		const FString& AssetPath,
		const FString& NotifyName,
		float TimePosition,
		float NewDuration,
		FString& OutJson, FString& OutError);

	UECPANIMATIONEXT_API void HandleMoveMontageSection(
		const FString& MontagePath,
		const FString& SectionName,
		float NewStartTime,
		FString& OutJson, FString& OutError);

	UECPANIMATIONEXT_API void HandleCropAnimation(
		const FString& AssetPath,
		float NewStartTime, float NewEndTime,
		FString& OutJson, FString& OutError);

	UECPANIMATIONEXT_API void HandleMoveAnimSyncMarker(
		const FString& AssetPath,
		const FString& MarkerName, float TimePosition,
		float NewTime,
		FString& OutJson, FString& OutError);

	UECPANIMATIONEXT_API void HandleRenameAnimSyncMarker(
		const FString& AssetPath,
		const FString& OldName, const FString& NewName, float TimePosition,
		FString& OutJson, FString& OutError);

	UECPANIMATIONEXT_API void HandleListAnimCurves(const FString& AssetPath,
		FString& OutJson, FString& OutError);

	UECPANIMATIONEXT_API void HandleRemoveAnimCurveKey(const FString& AssetPath,
		const FString& CurveName, float KeyTime,
		const FString& CurveType,
		FString& OutJson, FString& OutError);

	UECPANIMATIONEXT_API void HandleRemoveSkeletonSocket(
		const FString& SkeletonPath, const FString& SocketName,
		FString& OutJson, FString& OutError);

	UECPANIMATIONEXT_API void HandleRenameSkeletonSocket(
		const FString& SkeletonPath, const FString& OldName, const FString& NewName,
		FString& OutJson, FString& OutError);

	UECPANIMATIONEXT_API void HandleSetSkeletonSocketTransform(
		const FString& SkeletonPath, const FString& SocketName,
		const FString& RelLocation, const FString& RelRotation, const FString& RelScale,
		FString& OutJson, FString& OutError);

	UECPANIMATIONEXT_API void HandleSetSkeletonSocketParent(
		const FString& SkeletonPath, const FString& SocketName, const FString& NewBoneName,
		FString& OutJson, FString& OutError);

	UECPANIMATIONEXT_API void HandleMoveBlendspaceSample(
		const FString& BlendspacePath, float SampleX, float SampleY, float SampleZ,
		float NewX, float NewY, float NewZ,
		FString& OutJson, FString& OutError);

	UECPANIMATIONEXT_API void HandleSetBlendspaceSampleAnimation(
		const FString& BlendspacePath, float SampleX, float SampleY, float SampleZ,
		const FString& NewAnimationPath,
		FString& OutJson, FString& OutError);

	UECPANIMATIONEXT_API void HandleSetBlendspaceSampleRateScale(
		const FString& BlendspacePath, float SampleX, float SampleY, float SampleZ,
		float RateScale,
		FString& OutJson, FString& OutError);

	UECPANIMATIONEXT_API void HandleSetAnimStateAnimation(
		const FString& AnimBlueprintPath, const FString& StateMachineName, const FString& StateName,
		const FString& AnimationPath,
		FString& OutJson, FString& OutError);

	UECPANIMATIONEXT_API void HandleAddStateAlias(
		const FString& AnimBlueprintPath, const FString& StateMachineName,
		const FString& AliasName, const TArray<FString>& SourceStates,
		bool bGlobalAlias, int32 PosX, int32 PosY,
		FString& OutJson, FString& OutError);

	UECPANIMATIONEXT_API void HandleSetTransitionPriority(
		const FString& AnimBlueprintPath, const FString& StateMachineName,
		const FString& FromState, const FString& ToState, int32 Priority,
		FString& OutJson, FString& OutError);

	UECPANIMATIONEXT_API void HandleSetAnimNodePosition(
		const FString& AnimBlueprintPath, const FString& NodeGuid, int32 PosX, int32 PosY,
		FString& OutJson, FString& OutError);

	UECPANIMATIONEXT_API void HandleGetAnimBpSummary(const FString& AnimBPPath, FString& OutJson, FString& OutError);

	UECPANIMATIONEXT_API void HandleGetAnimGraphNodes(const FString& AnimBPPath, FString& OutJson, FString& OutError, bool bIncludeNested = false);

	UECPANIMATIONEXT_API void HandleGetSkeletonBones(const FString& SkeletonPath, FString& OutJson, FString& OutError);

	UECPANIMATIONEXT_API void HandleAddStateMachine(const FString& AnimBPPath, const FString& SMName,
		int32 PosX, int32 PosY, FString& OutJson, FString& OutError);

	UECPANIMATIONEXT_API void HandleRemoveAnimState(const FString& AnimBPPath, const FString& SMName,
		const FString& StateName, FString& OutJson, FString& OutError);

	UECPANIMATIONEXT_API void HandleSetStateMachineEntryState(const FString& AnimBPPath, const FString& SMName,
		const FString& EntryStateName, FString& OutJson, FString& OutError);

	UECPANIMATIONEXT_API void HandleWireAnimNodeToOutput(const FString& AnimBPPath, const FString& NodeName,
		FString& OutJson, FString& OutError);

	UECPANIMATIONEXT_API void HandleRemoveStateTransition(const FString& AnimBPPath, const FString& SMName,
		const FString& FromState, const FString& ToState, FString& OutJson, FString& OutError);

	UECPANIMATIONEXT_API void HandleRenameAnimState(const FString& AnimBPPath, const FString& SMName,
		const FString& OldName, const FString& NewName, FString& OutJson, FString& OutError);

	UECPANIMATIONEXT_API void HandleCreateAnimSlot(const FString& SkeletonPath, const FString& SlotName,
		const FString& GroupName, FString& OutJson, FString& OutError);

	UECPANIMATIONEXT_API void HandleGetBlendspaceInfo(const FString& BlendspacePath, FString& OutJson, FString& OutError);

	UECPANIMATIONEXT_API void HandleSetIKRetargetRoot(const FString& IKRigPath, const FString& BoneName,
		FString& OutJson, FString& OutError);

	UECPANIMATIONEXT_API void HandleSetRetargetPose(const FString& RetargeterPath, const FString& PoseName,
		const FString& SourceOrTarget, FString& OutJson, FString& OutError);

	UECPANIMATIONEXT_API void HandleSetRetargetChainSettingsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPANIMATIONEXT_API void HandleSetRetargetRootSettingsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPANIMATIONEXT_API void HandleEditRetargetPoseBoneFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPANIMATIONEXT_API void HandleAutoAlignRetargetPoseFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPANIMATIONEXT_API void HandleResetRetargetPoseFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPANIMATIONEXT_API void HandleExportRetargetAnimationFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPANIMATIONEXT_API void HandleCreateAnimLayerInterface(const FString& Name, const FString& SavePath,
		FString& OutJson, FString& OutError);

	UECPANIMATIONEXT_API void HandleImplementAnimLayer(const FString& AnimBPPath, const FString& InterfacePath,
		FString& OutJson, FString& OutError);

	UECPANIMATIONEXT_API void HandleAddAnimLayerNode(const FString& AnimBPPath, const FString& LayerName,
		int32 PosX, int32 PosY, FString& OutJson, FString& OutError);

	UECPANIMATIONEXT_API void HandleGetAnimSequenceInfo(const FString& AnimPath, FString& OutJson, FString& OutError);

	UECPANIMATIONEXT_API void HandleGetMontageSummary(const FString& MontagePath, FString& OutJson, FString& OutError);

	UECPANIMATIONEXT_API void HandleGetIKRigSummary(const FString& IKRigPath, FString& OutJson, FString& OutError);

	UECPANIMATIONEXT_API void HandleListAnimSlots(const FString& SkeletonPath, FString& OutJson, FString& OutError);

	UECPANIMATIONEXT_API void HandleAddLayeredBlendPerBone(const FString& AnimBPPath, int32 NumLayers,
		int32 PosX, int32 PosY, FString& OutJson, FString& OutError);

	UECPANIMATIONEXT_API void HandleAddSavedPose(const FString& AnimBPPath, const FString& CacheName,
		int32 PosX, int32 PosY, FString& OutJson, FString& OutError);

	UECPANIMATIONEXT_API void HandleUseCachedPose(const FString& AnimBPPath, const FString& CacheName,
		int32 PosX, int32 PosY, FString& OutJson, FString& OutError);

	UECPANIMATIONEXT_API void HandleAddBlendByBool(const FString& AnimBPPath, int32 PosX, int32 PosY,
		FString& OutJson, FString& OutError);

	UECPANIMATIONEXT_API void HandleRemoveAnimNotify(const FString& AnimPath, const FString& NotifyName,
		float TimePosition, FString& OutJson, FString& OutError);

	UECPANIMATIONEXT_API void HandleAddBlendByInt(const FString& AnimBPPath, int32 NumPoses,
		int32 PosX, int32 PosY, FString& OutJson, FString& OutError);

	// AxisMin/AxisMax: pass NaN to keep the current bound. GridDivisions <= -1 keeps current.
	// SnapToGrid / WrapInput: -1 = leave unchanged, 0 = false, 1 = true.
	UECPANIMATIONEXT_API void HandleSetBlendspaceAxis(const FString& BlendspacePath, int32 AxisIndex,
		const FString& AxisName, float AxisMin, float AxisMax, int32 GridDivisions,
		FString& OutJson, FString& OutError, int32 SnapToGrid = -1, int32 WrapInput = -1);

	UECPANIMATIONEXT_API void HandleAddAnimNotifyTrack(const FString& AnimPath, const FString& TrackName,
		FString& OutJson, FString& OutError);

	UECPANIMATIONEXT_API void HandleSetAnimCurveKey(const FString& AnimPath, const FString& CurveName,
		float KeyTime, float KeyValue, FString& OutJson, FString& OutError,
		const FString& CurveType = TEXT(""));

	UECPANIMATIONEXT_API void HandleGetRetargeterSummary(const FString& RetargeterPath, FString& OutJson, FString& OutError);

	UECPANIMATIONEXT_API void HandleCreateAnimBlueprintFromParent(const FString& Name, const FString& SavePath,
		const FString& ParentPath, const FString& SkeletonPath,
		FString& OutJson, FString& OutError);

	UECPANIMATIONEXT_API void HandleRemoveAnimCurve(const FString& AnimPath, const FString& CurveName,
		FString& OutJson, FString& OutError, const FString& CurveType = TEXT(""));

	UECPANIMATIONEXT_API void HandleAddSubAnimInstance(const FString& AnimBPPath, const FString& LinkedBlueprintPath,
		int32 PosX, int32 PosY, FString& OutJson, FString& OutError);

	UECPANIMATIONEXT_API void HandleRemoveBlendspaceSample(const FString& BlendspacePath, float SampleX, float SampleY,
		FString& OutJson, FString& OutError);

	UECPANIMATIONEXT_API void HandleRemoveMontageSection(const FString& MontagePath, const FString& SectionName,
		FString& OutJson, FString& OutError);

	UECPANIMATIONEXT_API void HandleRenameMontageSection(const FString& MontagePath, const FString& OldName,
		const FString& NewName, FString& OutJson, FString& OutError);

	UECPANIMATIONEXT_API void HandleAddSkeletonSocket(const FString& SkeletonPath, const FString& SocketName,
		const FString& BoneName, const FString& RelativeLocation,
		const FString& RelativeRotation, const FString& RelativeScale,
		FString& OutJson, FString& OutError);

	UECPANIMATIONEXT_API void HandleGetSkeletonSockets(const FString& SkeletonPath, FString& OutJson, FString& OutError);

	UECPANIMATIONEXT_API void HandleRemoveIKGoal(const FString& IKRigPath, const FString& GoalName,
		FString& OutJson, FString& OutError);

	UECPANIMATIONEXT_API void HandleRemoveRetargetChain(const FString& IKRigPath, const FString& ChainName,
		FString& OutJson, FString& OutError);

	UECPANIMATIONEXT_API void HandleAddModifyBone(const FString& AnimBPPath, const FString& BoneName,
		int32 PosX, int32 PosY, FString& OutJson, FString& OutError);
	UECPANIMATIONEXT_API void HandleAddCopyBone(const FString& AnimBPPath, const FString& SourceBone, const FString& TargetBone,
		int32 PosX, int32 PosY, FString& OutJson, FString& OutError);
	UECPANIMATIONEXT_API void HandleAddLookAt(const FString& AnimBPPath, const FString& BoneName, const FString& LookAtBone,
		int32 PosX, int32 PosY, FString& OutJson, FString& OutError);
	UECPANIMATIONEXT_API void HandleAddTwoBoneIK(const FString& AnimBPPath, const FString& IKBone,
		const FString& EffectorBone, const FString& JointTargetBone,
		int32 PosX, int32 PosY, FString& OutJson, FString& OutError);

	UECPANIMATIONEXT_API void HandleAddApplyAdditive(const FString& AnimBPPath, int32 PosX, int32 PosY,
		FString& OutJson, FString& OutError);
	UECPANIMATIONEXT_API void HandleAddMakeDynamicAdditive(const FString& AnimBPPath, int32 PosX, int32 PosY,
		FString& OutJson, FString& OutError);

	UECPANIMATIONEXT_API void HandleAddInertialization(const FString& AnimBPPath, int32 PosX, int32 PosY,
		FString& OutJson, FString& OutError);
	UECPANIMATIONEXT_API void HandleAddBlendByEnum(const FString& AnimBPPath, const FString& EnumClass,
		int32 PosX, int32 PosY, FString& OutJson, FString& OutError);

	UECPANIMATIONEXT_API void HandleAddSequenceEvaluator(const FString& AnimBPPath, const FString& AnimationPath,
		int32 PosX, int32 PosY, FString& OutJson, FString& OutError);
	UECPANIMATIONEXT_API void HandleAddRandomPlayer(const FString& AnimBPPath, int32 PosX, int32 PosY,
		FString& OutJson, FString& OutError);

	UECPANIMATIONEXT_API void HandleAddVirtualBone(const FString& SkeletonPath, const FString& SourceBone,
		const FString& TargetBone, const FString& VirtualBoneName,
		FString& OutJson, FString& OutError);
	UECPANIMATIONEXT_API void HandleListVirtualBones(const FString& SkeletonPath, FString& OutJson, FString& OutError);
	UECPANIMATIONEXT_API void HandleRemoveVirtualBone(const FString& SkeletonPath, const FString& VirtualBoneName,
		FString& OutJson, FString& OutError);
	UECPANIMATIONEXT_API void HandleRemoveVirtualBoneFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPANIMATIONEXT_API void HandleAddSlotNode(const FString& AnimBPPath, const FString& SlotName,
		int32 PosX, int32 PosY, FString& OutJson, FString& OutError);
	UECPANIMATIONEXT_API void HandleAddTwoWayBlend(const FString& AnimBPPath, int32 PosX, int32 PosY,
		FString& OutJson, FString& OutError);
	UECPANIMATIONEXT_API void HandleAddApplyMeshSpaceAdditive(const FString& AnimBPPath, int32 PosX, int32 PosY,
		FString& OutJson, FString& OutError);
	UECPANIMATIONEXT_API void HandleAddCopyPoseFromMesh(const FString& AnimBPPath, int32 PosX, int32 PosY,
		FString& OutJson, FString& OutError);
	UECPANIMATIONEXT_API void HandleAddRotateRootBone(const FString& AnimBPPath, int32 PosX, int32 PosY,
		FString& OutJson, FString& OutError);
	UECPANIMATIONEXT_API void HandleAddAimOffsetPlayer(const FString& AnimBPPath, const FString& AnimationPath,
		int32 PosX, int32 PosY, FString& OutJson, FString& OutError);
	UECPANIMATIONEXT_API void HandleAddMirror(const FString& AnimBPPath, const FString& MirrorDataTablePath,
		int32 PosX, int32 PosY, FString& OutJson, FString& OutError);

	UECPANIMATIONEXT_API void HandleAddLocalToComponentSpace(const FString& AnimBPPath, int32 PosX, int32 PosY,
		FString& OutJson, FString& OutError);
	UECPANIMATIONEXT_API void HandleAddComponentToLocalSpace(const FString& AnimBPPath, int32 PosX, int32 PosY,
		FString& OutJson, FString& OutError);

	UECPANIMATIONEXT_API void HandleAddMeshRefPose(const FString& AnimBPPath, int32 PosX, int32 PosY,
		FString& OutJson, FString& OutError);
	UECPANIMATIONEXT_API void HandleAddLocalRefPose(const FString& AnimBPPath, int32 PosX, int32 PosY,
		FString& OutJson, FString& OutError);
	UECPANIMATIONEXT_API void HandleAddIdentityPose(const FString& AnimBPPath, int32 PosX, int32 PosY,
		FString& OutJson, FString& OutError);

	UECPANIMATIONEXT_API void HandleAddSpringBone(const FString& AnimBPPath, const FString& BoneName,
		int32 PosX, int32 PosY, FString& OutJson, FString& OutError);
	UECPANIMATIONEXT_API void HandleAddRigidBody(const FString& AnimBPPath, int32 PosX, int32 PosY,
		FString& OutJson, FString& OutError);
	UECPANIMATIONEXT_API void HandleAddFabrik(const FString& AnimBPPath, const FString& TipBone, const FString& RootBone,
		int32 PosX, int32 PosY, FString& OutJson, FString& OutError);
	UECPANIMATIONEXT_API void HandleAddCCDIK(const FString& AnimBPPath, const FString& TipBone, const FString& RootBone,
		int32 PosX, int32 PosY, FString& OutJson, FString& OutError);
	UECPANIMATIONEXT_API void HandleAddLegIK(const FString& AnimBPPath, int32 PosX, int32 PosY,
		FString& OutJson, FString& OutError);

	UECPANIMATIONEXT_API void HandleAddPoseByName(const FString& AnimBPPath, const FString& PoseAssetPath,
		const FString& PoseNameStr, int32 PosX, int32 PosY, FString& OutJson, FString& OutError);
	UECPANIMATIONEXT_API void HandleAddBoneDrivenController(const FString& AnimBPPath, const FString& SourceBone,
		const FString& TargetBone, int32 PosX, int32 PosY, FString& OutJson, FString& OutError);
	UECPANIMATIONEXT_API void HandleAddBlendBoneByChannel(const FString& AnimBPPath, int32 PosX, int32 PosY,
		FString& OutJson, FString& OutError);

	UECPANIMATIONEXT_API void HandleAddMotionMatchingNode(const FString& AnimBPPath, const FString& PoseSearchDbPath,
		int32 PosX, int32 PosY, FString& OutJson, FString& OutError);

	UECPANIMATIONEXT_API void HandleConnectAnimNodes(const FString& AnimBPPath, const FString& SourceNodeGuid,
		const FString& TargetNodeGuid, FString& OutJson, FString& OutError);
	UECPANIMATIONEXT_API void HandleBuildAnimChain(const FString& AnimBPPath, const FString& ChainJson,
		bool bAutoConnectToOutput, FString& OutJson, FString& OutError);
	UECPANIMATIONEXT_API void HandleAddControlRigNode(const FString& AnimBPPath, const FString& ControlRigPath,
		int32 PosX, int32 PosY, FString& OutJson, FString& OutError);

	UECPANIMATIONEXT_API void HandleGetAnimMontageSections(const FString& MontagePath, FString& OutJson, FString& OutError);

	UECPANIMATIONEXT_API void HandleSetMontageSectionLink(const FString& MontagePath, const FString& SectionName,
		const FString& NextSectionName, FString& OutJson, FString& OutError);

	UECPANIMATIONEXT_API void HandleCreateSyncGroup(const FString& AnimBPPath, const FString& SyncGroupName,
		const FString& GroupRole, FString& OutJson, FString& OutError);

	UECPANIMATIONEXT_API void HandleAddBlendProfile(const FString& SkeletonPath, const FString& ProfileName,
		FString& OutJson, FString& OutError);

	UECPANIMATIONEXT_API void HandleAddAnimSyncMarker(const FString& AnimPath, const FString& MarkerName,
		float Time, FString& OutJson, FString& OutError);

	UECPANIMATIONEXT_API void HandleRemoveAnimSyncMarker(const FString& AnimPath, const FString& MarkerName,
		float Time, bool bMatchTime, FString& OutJson, FString& OutError);

	UECPANIMATIONEXT_API void HandleListAnimSyncMarkers(const FString& AnimPath, FString& OutJson, FString& OutError);

	UECPANIMATIONEXT_API void HandleGetAnimGraphNodes(const FString& AnimBPPath, FString& OutJson, FString& OutError, bool bIncludeNested);

	UECPANIMATIONEXT_API void HandleAddBlendSpacePlayer(const FString& AnimBPPath, const FString& BlendSpacePath,
		int32 PosX, int32 PosY, FString& OutJson, FString& OutError);
	UECPANIMATIONEXT_API void HandleAddBlendSpacePlayerFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPANIMATIONEXT_API void HandleSetBlendSpacePlayerAsset(const FString& AnimBPPath, const FString& NodeGuid,
		const FString& BlendSpacePath, bool bDryRun, const FString& ExpectCurrentAsset, FString& OutJson, FString& OutError);
	UECPANIMATIONEXT_API void HandleSetBlendSpacePlayerAssetFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPANIMATIONEXT_API void HandleGetAnimStateMachines(const FString& AnimBPPath, FString& OutJson, FString& OutError);

	UECPANIMATIONEXT_API void HandleSetAnimNodeProperty(const FString& AnimBPPath, const FString& NodeGuid,
		const FString& PropertyName, const FString& PropertyValue,
		FString& OutJson, FString& OutError);

	UECPANIMATIONEXT_API void HandleAddMotionWarpingWindow(const FString& MontagePath, const FString& WarpTargetName,
		float StartTime, float EndTime, FString& OutJson, FString& OutError);

	UECPANIMATIONEXT_API void HandleGetMotionWarpingWindows(const FString& MontagePath, FString& OutJson, FString& OutError);

	UECPANIMATIONEXT_API void HandleRemoveMotionWarpingWindow(const FString& MontagePath, const FString& WarpTargetName,
		FString& OutJson, FString& OutError);

	UECPANIMATIONEXT_API void HandleAddAnimStateFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleAddStateTransitionFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleSetTransitionRuleFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleAddBlendspaceSampleFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleAddMontageSectionFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleAddAnimNotifyFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleAddAnimCurveFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleAddSkeletonSocketFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleAddVirtualBoneFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleMapRetargetChainFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPANIMATIONEXT_API void HandleRenameVirtualBoneFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPANIMATIONEXT_API void HandleGetSkeletonHierarchy(const FString& SkeletonPath, FString& OutJsonString, FString& OutError);

	UECPANIMATIONEXT_API void HandleCreateBoneCompressionSettingsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPANIMATIONEXT_API void HandleCreateCurveCompressionSettingsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPANIMATIONEXT_API void HandleAssignCompressionToAnimationFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPANIMATIONEXT_API void HandleGetCompressionInfoFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPANIMATIONEXT_API void HandleCreateAnimBlueprintFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleCreateBlendspaceFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleCreateAnimMontageFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleLinkMontageSlotFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleSetMontageSlotNameFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleAddAnimConduitFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleCreateAnimCompositeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleCreatePoseAssetFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleCreateAimOffsetFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleCreateIKRigFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleAddIKSolverFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleAddIKGoalFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleAddRetargetChainFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleCreateIKRetargeterFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleSetAnimSequenceSettingsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleSetMontageBlendSettingsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleAddAnimNotifyStateFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleSetTransitionBlendSettingsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleSetAnimNotifyPropertyFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleGetAnimBpSummaryFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleGetAnimGraphNodesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleGetSkeletonBonesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleAddStateMachineFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleRemoveAnimStateFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleSetStateMachineEntryStateFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleWireAnimNodeToOutputFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleRemoveStateTransitionFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleRenameAnimStateFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleCreateAnimSlotFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleGetBlendspaceInfoFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleSetIKRetargetRootFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleSetRetargetPoseFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleCreateAnimLayerInterfaceFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleImplementAnimLayerFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleAddAnimLayerNodeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleGetAnimSequenceInfoFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleGetMontageSummaryFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleGetIKRigSummaryFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleListAnimSlotsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleAddLayeredBlendPerBoneFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleAddSavedPoseFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleUseCachedPoseFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleAddBlendByBoolFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleRemoveAnimNotifyFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleAddBlendByIntFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleSetBlendspaceAxisFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleAddAnimNotifyTrackFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleSetAnimCurveKeyFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleGetRetargeterSummaryFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleCreateAnimBlueprintFromParentFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleRemoveAnimCurveFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleAddSubAnimInstanceFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleRemoveBlendspaceSampleFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleRemoveMontageSectionFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleRenameMontageSectionFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleGetSkeletonSocketsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleRemoveIKGoalFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleRemoveRetargetChainFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleAddModifyBoneFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleAddCopyBoneFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleAddLookAtFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleAddTwoBoneIKFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleAddApplyAdditiveFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleAddMakeDynamicAdditiveFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleAddInertializationFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleAddBlendByEnumFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleAddSequenceEvaluatorFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleAddRandomPlayerFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleListVirtualBonesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleGetSkeletonHierarchyFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleConnectAnimNodesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleBuildAnimChainFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleAddControlRigNodeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleAddSlotNodeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleAddTwoWayBlendFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleAddApplyMeshSpaceAdditiveFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleAddCopyPoseFromMeshFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleAddRotateRootBoneFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleAddAimOffsetPlayerFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleAddMirrorFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleAddLocalToComponentSpaceFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleAddComponentToLocalSpaceFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleAddMeshRefPoseFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleAddLocalRefPoseFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleAddIdentityPoseFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleAddSpringBoneFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleAddRigidBodyFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleAddFabrikFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleAddCCDIKFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleAddLegIKFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleAddPoseByNameFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleAddBoneDrivenControllerFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleAddBlendBoneByChannelFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleAddMotionMatchingNodeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleGetAnimMontageSectionsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleSetMontageSectionLinkFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleCreateSyncGroupFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleAddBlendProfileFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleAddAnimSyncMarkerFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleRemoveAnimSyncMarkerFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleListAnimSyncMarkersFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleGetAnimStateMachinesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleSetAnimNodePropertyFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	// Compiles the AnimBP and reports errors/warnings (with node titles + GUIDs) without mutating the graph.
	UECPANIMATIONEXT_API void HandleCompileAnimBlueprint(const FString& AnimBPPath, FString& OutJson, FString& OutError);
	UECPANIMATIONEXT_API void HandleCompileAnimBlueprintFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleAddMotionWarpingWindowFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleGetMotionWarpingWindowsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleRemoveMotionWarpingWindowFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleMoveAnimNotifyFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleSetAnimNotifyDurationFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleMoveMontageSectionFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleCropAnimationFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleMoveAnimSyncMarkerFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleRenameAnimSyncMarkerFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleListAnimCurvesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleRemoveAnimCurveKeyFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleRemoveSkeletonSocketFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleRenameSkeletonSocketFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleSetSkeletonSocketTransformFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleSetSkeletonSocketParentFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleMoveBlendspaceSampleFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleSetBlendspaceSampleAnimationFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleSetBlendspaceSampleRateScaleFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleSetAnimStateAnimationFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleAddStateAliasFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleSetTransitionPriorityFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPANIMATIONEXT_API void HandleSetAnimNodePositionFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPANIMATIONEXT_API void HandleSetLayeredBlendPerBoneFilterFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPANIMATIONEXT_API void HandleSetTransitionOptionsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPANIMATIONEXT_API void HandleSetStateOptionsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	// ---- Composite template tools (each records every sub-step in steps[] and ends with a compile/save report) ----

	// Idle / WalkRun (BlendSpace bound to Speed) / JumpStart / JumpLoop / JumpLand state machine with rules, entry state and Output Pose wiring.
	UECPANIMATIONEXT_API void HandleCreateLocomotionStateMachineFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	// Montage asset from a sequence: slot link, sections (+links/loops), blend settings, notifies.
	UECPANIMATIONEXT_API void HandleCreateMontageFromSequenceFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	// Turns a sequence additive (type / ref pose / base sequence / frame kept consistent), recompresses, saves.
	UECPANIMATIONEXT_API void HandleMakeAdditiveFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	// Source + target IK Rigs (retarget root + humanoid chains by bone-name heuristics), IK Retargeter, chain mapping, auto-align, batch export.
	UECPANIMATIONEXT_API void HandleRetargetSetupFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	// Pose Search schema (trajectory + pose channels), database with entries, index build, optional Chooser table.
	UECPANIMATIONEXT_API void HandleSetupMotionMatchingFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	// Local->Component -> TwoBoneIK(L) -> TwoBoneIK(R) -> Component->Local chain in front of Output Pose, pins bound to IK target/alpha variables.
	UECPANIMATIONEXT_API void HandleSetupFootIKFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
}
