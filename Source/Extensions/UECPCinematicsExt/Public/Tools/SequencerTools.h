// Copyright 2026, BlueprintsLab, All rights reserved.

#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace SequencerTools
{
	UECPCINEMATICSEXT_API void HandleCreateLevelSequence(const FString& AssetName, const FString& SavePath,
		FString& OutJsonString, FString& OutError);

	UECPCINEMATICSEXT_API void HandleAddSequenceActorBinding(const FString& SequencePath, const FString& ActorLabel,
		FString& OutJsonString, FString& OutError);

	UECPCINEMATICSEXT_API void HandleAddSequenceTransformTrack(const FString& SequencePath, const FString& ActorLabel,
		FString& OutJsonString, FString& OutError);

	UECPCINEMATICSEXT_API void HandleAddSequenceKeyframe(const FString& SequencePath, const FString& ActorLabel,
		float TimeSeconds,
		float LocationX, float LocationY, float LocationZ,
		float RotationPitch, float RotationYaw, float RotationRoll,
		float ScaleX, float ScaleY, float ScaleZ, bool bSetScale,
		FString& OutJsonString, FString& OutError);

	UECPCINEMATICSEXT_API void HandleGetSequenceSummary(const FString& SequencePath,
		FString& OutJsonString, FString& OutError);

	UECPCINEMATICSEXT_API void HandleAddSequenceCameraCutTrack(const FString& SequencePath, const FString& CameraActorLabel,
		float StartTimeSec, float EndTimeSec, FString& OutJsonString, FString& OutError);

	UECPCINEMATICSEXT_API void HandleAddSequenceCameraTrack(const FString& SequencePath, const FString& CameraActorLabel,
		FString& OutJsonString, FString& OutError);

	UECPCINEMATICSEXT_API void HandleAddSequenceAudioTrack(const FString& SequencePath, const FString& SoundPath,
		float StartTimeSec, float EndTimeSec, FString& OutJsonString, FString& OutError);

	UECPCINEMATICSEXT_API void HandleAddSequenceEventTrack(const FString& SequencePath,
		FString& OutJsonString, FString& OutError);

	UECPCINEMATICSEXT_API void HandleSetSequencePlaybackSettings(const FString& SequencePath,
		float FrameRate, float DurationSeconds, FString& OutJsonString, FString& OutError);

	UECPCINEMATICSEXT_API void HandleAddSequenceFloatTrack(const FString& SequencePath, const FString& ActorLabel,
		const FString& PropertyName, const FString& PropertyPath,
		FString& OutJsonString, FString& OutError);

	UECPCINEMATICSEXT_API void HandleAddSequenceVisibilityTrack(const FString& SequencePath, const FString& ActorLabel,
		FString& OutJsonString, FString& OutError);

	UECPCINEMATICSEXT_API void HandleAddSequenceSkeletalAnimTrack(const FString& SequencePath, const FString& ActorLabel,
		const FString& AnimPath, float StartTimeSec,
		FString& OutJsonString, FString& OutError);

	UECPCINEMATICSEXT_API void HandleRemoveSequenceTrack(const FString& SequencePath, const FString& ActorLabel,
		const FString& TrackType, FString& OutJsonString, FString& OutError);

	UECPCINEMATICSEXT_API void HandleSetSequenceSectionRange(const FString& SequencePath,
		float StartSeconds, float EndSeconds, FString& OutJsonString, FString& OutError);

	UECPCINEMATICSEXT_API void HandleMoveSequencerSection(const FString& SequencePath,
		const FString& ActorLabel, const FString& TrackName, int32 SectionIndex,
		float NewStartSeconds, FString& OutJsonString, FString& OutError);

	UECPCINEMATICSEXT_API void HandleResizeSequencerSection(const FString& SequencePath,
		const FString& ActorLabel, const FString& TrackName, int32 SectionIndex,
		float NewEndSeconds, FString& OutJsonString, FString& OutError);

	UECPCINEMATICSEXT_API void HandleSplitSequencerSection(const FString& SequencePath,
		const FString& ActorLabel, const FString& TrackName, int32 SectionIndex,
		float SplitSeconds, FString& OutJsonString, FString& OutError);

	UECPCINEMATICSEXT_API void HandleRemoveSequencerSection(const FString& SequencePath,
		const FString& ActorLabel, const FString& TrackName, int32 SectionIndex,
		FString& OutJsonString, FString& OutError);

	UECPCINEMATICSEXT_API void HandleMoveSequencerKeyframe(const FString& SequencePath,
		const FString& ActorLabel, const FString& PropertyName,
		float TimeSeconds, float NewTimeSeconds,
		FString& OutJsonString, FString& OutError);

	UECPCINEMATICSEXT_API void HandleSetSequencerKeyframeValue(const FString& SequencePath,
		const FString& ActorLabel, const FString& PropertyName,
		float TimeSeconds, float Value, const FString& ValueType,
		FString& OutJsonString, FString& OutError);

	UECPCINEMATICSEXT_API void HandleRenameSequencerTrack(const FString& SequencePath,
		const FString& ActorLabel, const FString& TrackName, const FString& NewDisplayName,
		FString& OutJsonString, FString& OutError);

	UECPCINEMATICSEXT_API void HandleSetSequencerTrackEvalDisabled(const FString& SequencePath,
		const FString& ActorLabel, const FString& TrackName, bool bDisabled,
		FString& OutJsonString, FString& OutError);

	UECPCINEMATICSEXT_API void HandleSetSequencerTrackSortOrder(const FString& SequencePath,
		const FString& ActorLabel, const FString& TrackName, int32 SortOrder,
		FString& OutJsonString, FString& OutError);

	UECPCINEMATICSEXT_API void HandleRenameSequencerBinding(const FString& SequencePath,
		const FString& CurrentLabel, const FString& NewLabel,
		FString& OutJsonString, FString& OutError);

	UECPCINEMATICSEXT_API void HandleAddSequenceCameraShake(const FString& SequencePath,
		const FString& ActorLabel, const FString& ShakeClassPath,
		float StartSeconds, float DurationSeconds, float PlayScale,
		FString& OutJsonString, FString& OutError);

	UECPCINEMATICSEXT_API void HandleAddSequenceSubSequence(const FString& SequencePath, const FString& SubSequencePath,
		float StartTimeSec, float EndTimeSec, FString& OutJsonString, FString& OutError);

	UECPCINEMATICSEXT_API void HandleSetKeyframeInterpolation(const FString& SequencePath, const FString& ActorLabel,
		float TimeSeconds, const FString& InterpMode, FString& OutJsonString, FString& OutError);

	UECPCINEMATICSEXT_API void HandleGetSequenceBindings(const FString& SequencePath, FString& OutJsonString, FString& OutError);

	UECPCINEMATICSEXT_API void HandleAddSequenceMaterialTrack(const FString& SequencePath, const FString& ActorLabel,
		int32 MaterialIndex, const FString& ParameterName,
		FString& OutJsonString, FString& OutError);

	UECPCINEMATICSEXT_API void HandleRemoveSequenceKeyframe(const FString& SequencePath, const FString& ActorLabel,
		float TimeSeconds, FString& OutJsonString, FString& OutError);

	UECPCINEMATICSEXT_API void HandleAddSequenceFadeTrack(const FString& SequencePath, float FadeInDuration, float FadeOutDuration,
		FString& OutJsonString, FString& OutError);

	UECPCINEMATICSEXT_API void HandleSetSequenceDisplayRate(const FString& SequencePath, float DisplayFPS,
		FString& OutJsonString, FString& OutError);

	UECPCINEMATICSEXT_API void HandleAddSequenceColorTrack(const FString& SequencePath, const FString& ActorLabel,
		const FString& PropertyName, FString& OutJsonString, FString& OutError);

	UECPCINEMATICSEXT_API void HandleCreateMRQJob(const FString& SequencePath, const FString& JobName,
		const FString& MapPath, FString& OutJsonString, FString& OutError);

	UECPCINEMATICSEXT_API void HandleSetMRQOutputSettings(const FString& JobName, const FString& OutputDirectory,
		const FString& FileNameFormat, const FString& FileFormat, float FrameRate,
		FString& OutJsonString, FString& OutError);

	UECPCINEMATICSEXT_API void HandleAddMRQRenderPass(const FString& JobName, const FString& PassType,
		FString& OutJsonString, FString& OutError);

	UECPCINEMATICSEXT_API void HandleExecuteMRQRender(FString& OutJsonString, FString& OutError);

	UECPCINEMATICSEXT_API void HandleGetMRQQueueSummary(FString& OutJsonString, FString& OutError);

	UECPCINEMATICSEXT_API void HandleClearMRQQueue(FString& OutJsonString, FString& OutError);

	UECPCINEMATICSEXT_API void HandleAddFloatKeyframe(const FString& SequencePath, const FString& ActorLabel,
		const FString& PropertyName, float TimeSeconds, float Value,
		FString& OutJsonString, FString& OutError);

	UECPCINEMATICSEXT_API void HandleAddVisibilityKeyframe(const FString& SequencePath, const FString& ActorLabel,
		float TimeSeconds, bool bVisible, FString& OutJsonString, FString& OutError);

	UECPCINEMATICSEXT_API void HandleAddSequenceSpawnable(const FString& SequencePath, const FString& ActorClassPath,
		const FString& Label, FString& OutJsonString, FString& OutError);

	UECPCINEMATICSEXT_API void HandleGetSequenceKeyframes(const FString& SequencePath, const FString& ActorLabel,
		const FString& PropertyName, FString& OutJsonString, FString& OutError);

	UECPCINEMATICSEXT_API void HandleAddControlRigSequencerTrack(const FString& SequencePath, const FString& BindingLabel,
		const FString& ControlRigPath, FString& OutJsonString, FString& OutError);

	UECPCINEMATICSEXT_API void HandleListControlRigControls(const FString& SequencePath, const FString& BindingLabel,
		FString& OutJsonString, FString& OutError);

	UECPCINEMATICSEXT_API void HandleAddControlRigSection(const FString& SequencePath, const FString& BindingLabel,
		int32 StartFrame, int32 EndFrame, FString& OutJsonString, FString& OutError);

	UECPCINEMATICSEXT_API void HandleSetControlRigKeyframe(const FString& SequencePath, const FString& BindingLabel,
		const FString& ControlName, int32 Frame, const FString& ValueType,
		const TSharedPtr<class FJsonValue>& Value, FString& OutJsonString, FString& OutError);

	UECPCINEMATICSEXT_API void HandleAddControlRigSequencerTrackFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPCINEMATICSEXT_API void HandleListControlRigControlsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPCINEMATICSEXT_API void HandleAddControlRigSectionFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPCINEMATICSEXT_API void HandleSetControlRigKeyframeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPCINEMATICSEXT_API void HandleAddSequenceActorBindingFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPCINEMATICSEXT_API void HandleAddSequenceKeyframeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPCINEMATICSEXT_API void HandleAddSequenceTransformTrackFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPCINEMATICSEXT_API void HandleAddSequenceFloatTrackFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPCINEMATICSEXT_API void HandleRemoveSequenceTrackFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPCINEMATICSEXT_API void HandleAddFloatKeyframeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPCINEMATICSEXT_API void HandleAddVisibilityKeyframeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPCINEMATICSEXT_API void HandleAddSequenceSpawnableFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPCINEMATICSEXT_API void HandleSpawnCameraRigRailFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPCINEMATICSEXT_API void HandleSpawnCameraRigCraneFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPCINEMATICSEXT_API void HandleAttachCameraToRigFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPCINEMATICSEXT_API void HandleSetRigRailPositionFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPCINEMATICSEXT_API void HandleGetRigSummary(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPCINEMATICSEXT_API void HandleRemoveSequenceBinding(const FString& SequencePath, const FString& ActorLabel,
		FString& OutJsonString, FString& OutError);
	UECPCINEMATICSEXT_API void HandleSetSectionBlendType(const FString& SequencePath, const FString& ActorLabel,
		const FString& TrackType, int32 SectionIndex, const FString& BlendType,
		FString& OutJsonString, FString& OutError);
	UECPCINEMATICSEXT_API void HandleSetSectionCompletionMode(const FString& SequencePath, const FString& ActorLabel,
		const FString& TrackType, int32 SectionIndex, const FString& CompletionMode,
		FString& OutJsonString, FString& OutError);
	UECPCINEMATICSEXT_API void HandleAddSequenceBoolTrack(const FString& SequencePath, const FString& ActorLabel,
		const FString& PropertyName, FString& OutJsonString, FString& OutError);
	UECPCINEMATICSEXT_API void HandleAddBoolKeyframe(const FString& SequencePath, const FString& ActorLabel,
		const FString& PropertyName, float TimeSeconds, bool bValue,
		FString& OutJsonString, FString& OutError);
	UECPCINEMATICSEXT_API void HandleAddSequenceIntegerTrack(const FString& SequencePath, const FString& ActorLabel,
		const FString& PropertyName, FString& OutJsonString, FString& OutError);
	UECPCINEMATICSEXT_API void HandleAddIntegerKeyframe(const FString& SequencePath, const FString& ActorLabel,
		const FString& PropertyName, float TimeSeconds, int32 IntValue,
		FString& OutJsonString, FString& OutError);
	UECPCINEMATICSEXT_API void HandleAddColorKeyframe(const FString& SequencePath, const FString& ActorLabel,
		const FString& PropertyName, float TimeSeconds, float R, float G, float B, float A,
		FString& OutJsonString, FString& OutError);
	UECPCINEMATICSEXT_API void HandleAddBoolKeyframeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPCINEMATICSEXT_API void HandleAddColorKeyframeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPCINEMATICSEXT_API void HandleAddLevelVisibilitySection(const FString& SequencePath,
		const TArray<FString>& LevelNames, const FString& Visibility,
		float StartSeconds, float EndSeconds,
		FString& OutJsonString, FString& OutError);
	UECPCINEMATICSEXT_API void HandleListSubsequences(const FString& SequencePath, FString& OutJsonString, FString& OutError);
	UECPCINEMATICSEXT_API void HandleRemoveSubsequence(const FString& SequencePath, int32 SubsequenceIndex,
		FString& OutJsonString, FString& OutError);
	UECPCINEMATICSEXT_API void HandleSetSubsequenceParams(const FString& SequencePath, int32 SubsequenceIndex,
		int32 StartOffsetFrames, float TimeScale, bool bSetOffset, bool bSetScale,
		FString& OutJsonString, FString& OutError);
	UECPCINEMATICSEXT_API void HandleGetMRQRenderStatus(FString& OutJsonString, FString& OutError);
	UECPCINEMATICSEXT_API void HandleCancelMRQRender(FString& OutJsonString, FString& OutError);
	UECPCINEMATICSEXT_API void HandleDeleteMRQJob(const FString& JobName, FString& OutJsonString, FString& OutError);
	UECPCINEMATICSEXT_API void HandleListMRQRenderPasses(const FString& JobName, FString& OutJsonString, FString& OutError);
	UECPCINEMATICSEXT_API void HandleListSequences(const FString& SearchPath, FString& OutJsonString, FString& OutError);
	UECPCINEMATICSEXT_API void HandleGetSequenceFullData(const FString& SequencePath, FString& OutJsonString, FString& OutError);

	UECPCINEMATICSEXT_API void HandleCreateLevelSequenceFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPCINEMATICSEXT_API void HandleGetSequenceSummaryFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPCINEMATICSEXT_API void HandleAddSequenceCameraCutTrackFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPCINEMATICSEXT_API void HandleAddSequenceCameraTrackFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPCINEMATICSEXT_API void HandleAddSequenceAudioTrackFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPCINEMATICSEXT_API void HandleAddSequenceEventTrackFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPCINEMATICSEXT_API void HandleSetSequencePlaybackSettingsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPCINEMATICSEXT_API void HandleAddSequenceVisibilityTrackFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPCINEMATICSEXT_API void HandleAddSequenceSkeletalAnimTrackFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPCINEMATICSEXT_API void HandleSetSequenceSectionRangeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPCINEMATICSEXT_API void HandleAddSequenceSubSequenceFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPCINEMATICSEXT_API void HandleSetKeyframeInterpolationFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPCINEMATICSEXT_API void HandleGetSequenceBindingsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPCINEMATICSEXT_API void HandleAddSequenceMaterialTrackFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPCINEMATICSEXT_API void HandleRemoveSequenceKeyframeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPCINEMATICSEXT_API void HandleAddSequenceFadeTrackFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPCINEMATICSEXT_API void HandleSetSequenceDisplayRateFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPCINEMATICSEXT_API void HandleAddSequenceColorTrackFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPCINEMATICSEXT_API void HandleGetSequenceKeyframesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPCINEMATICSEXT_API void HandleCreateMRQJobFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPCINEMATICSEXT_API void HandleSetMRQOutputSettingsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPCINEMATICSEXT_API void HandleAddMRQRenderPassFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPCINEMATICSEXT_API void HandleExecuteMRQRenderFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPCINEMATICSEXT_API void HandleGetMRQQueueSummaryFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPCINEMATICSEXT_API void HandleClearMRQQueueFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPCINEMATICSEXT_API void HandleRemoveSequenceBindingFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPCINEMATICSEXT_API void HandleSetSectionBlendTypeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPCINEMATICSEXT_API void HandleSetSectionCompletionModeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPCINEMATICSEXT_API void HandleAddSequenceBoolTrackFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPCINEMATICSEXT_API void HandleAddSequenceIntegerTrackFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPCINEMATICSEXT_API void HandleAddIntegerKeyframeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPCINEMATICSEXT_API void HandleAddLevelVisibilitySectionFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPCINEMATICSEXT_API void HandleListSubsequencesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPCINEMATICSEXT_API void HandleRemoveSubsequenceFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPCINEMATICSEXT_API void HandleSetSubsequenceParamsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPCINEMATICSEXT_API void HandleGetMRQRenderStatusFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPCINEMATICSEXT_API void HandleCancelMRQRenderFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPCINEMATICSEXT_API void HandleDeleteMRQJobFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPCINEMATICSEXT_API void HandleListMRQRenderPassesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPCINEMATICSEXT_API void HandleListSequencesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPCINEMATICSEXT_API void HandleGetSequenceFullDataFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPCINEMATICSEXT_API void HandleMoveSequencerSectionFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPCINEMATICSEXT_API void HandleResizeSequencerSectionFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPCINEMATICSEXT_API void HandleSplitSequencerSectionFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPCINEMATICSEXT_API void HandleRemoveSequencerSectionFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPCINEMATICSEXT_API void HandleMoveSequencerKeyframeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPCINEMATICSEXT_API void HandleSetSequencerKeyframeValueFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPCINEMATICSEXT_API void HandleRenameSequencerTrackFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPCINEMATICSEXT_API void HandleSetSequencerTrackEvalDisabledFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPCINEMATICSEXT_API void HandleSetSequencerTrackSortOrderFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPCINEMATICSEXT_API void HandleRenameSequencerBindingFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPCINEMATICSEXT_API void HandleAddSequenceCameraShakeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
}
