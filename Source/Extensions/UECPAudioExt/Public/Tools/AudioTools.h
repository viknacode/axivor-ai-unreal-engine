// Copyright 2026, BlueprintsLab, All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace AudioTools
{
	UECPAUDIOEXT_API void HandleCreateSoundCue(
		const FString& Name,
		const FString& SavePath,
		const TArray<FString>& SoundWavePaths,
		bool bLooping,
		float PitchMin, float PitchMax,
		float VolumeMin, float VolumeMax,
		FString& OutJsonString, FString& OutError);

	UECPAUDIOEXT_API void HandleAddSoundNode(
		const FString& SoundCuePath,
		const FString& NodeType,
		int32 PosX, int32 PosY,
		const FString& SoundWavePath,
		FString& OutJsonString, FString& OutError);

	UECPAUDIOEXT_API void HandleConnectSoundNodes(
		const FString& SoundCuePath,
		const FString& ChildNodeName,
		const FString& ParentNodeName,
		FString& OutJsonString, FString& OutError);

	UECPAUDIOEXT_API void HandleSetSoundCueOutput(
		const FString& SoundCuePath,
		const FString& NodeName,
		FString& OutJsonString, FString& OutError);

	UECPAUDIOEXT_API void HandleCreateSoundAttenuation(const FString& Name, const FString& SavePath,
		float InnerRadius, float FalloffDistance, FString& OutJsonString, FString& OutError);

	UECPAUDIOEXT_API void HandleCreateSoundClass(const FString& Name, const FString& SavePath,
		float Volume, float Pitch, FString& OutJsonString, FString& OutError);

	UECPAUDIOEXT_API void HandleSetSoundCueAttenuation(const FString& SoundCuePath, const FString& AttenuationPath,
		FString& OutJsonString, FString& OutError);

	UECPAUDIOEXT_API void HandleSetSoundCueSoundClass(const FString& SoundCuePath, const FString& SoundClassPath,
		FString& OutJsonString, FString& OutError);
	UECPAUDIOEXT_API void HandleSetSoundCueSoundClassFromArgs(const TSharedPtr<FJsonObject>& Args,
		FString& OutJsonString, FString& OutError);

	UECPAUDIOEXT_API void HandleSetSoundClassProperties(const FString& SoundClassPath,
		float Volume, float Pitch, float LPFFrequency, FString& OutJsonString, FString& OutError);

	UECPAUDIOEXT_API void HandleCreateSoundMix(const FString& Name, const FString& SavePath,
		float FadeInTime, float FadeOutTime, float Duration, FString& OutJsonString, FString& OutError);

	UECPAUDIOEXT_API void HandleSetSoundMixProperties(const FString& SoundMixPath, const FString& SoundClassPath,
		float VolumeAdjuster, float PitchAdjuster, bool bApplyToChildren, FString& OutJsonString, FString& OutError);

	UECPAUDIOEXT_API void HandleCreateAudioSubmix(const FString& Name, const FString& SavePath,
		float Volume, FString& OutJsonString, FString& OutError);

	UECPAUDIOEXT_API void HandleSetAudioSubmixProperties(const FString& SubmixPath,
		float Volume, const FString& ParentSubmixPath,
		FString& OutJsonString, FString& OutError);

	UECPAUDIOEXT_API void HandleCreateSoundConcurrency(const FString& Name, const FString& SavePath,
		FString& OutJsonString, FString& OutError);

	UECPAUDIOEXT_API void HandleSetSoundConcurrencyProperties(const FString& AssetPath,
		int32 MaxCount, const FString& ResolutionPolicy,
		float StealFadeoutTime, float VolumeScaleAttenuation,
		bool bLimitToOwner,
		FString& OutJsonString, FString& OutError);

	UECPAUDIOEXT_API void HandleAssignSoundConcurrency(const FString& AssetPath, const FString& ConcurrencyPath,
		FString& OutJsonString, FString& OutError);

	UECPAUDIOEXT_API void HandleGetSoundCueSummary(const FString& SoundCuePath, FString& OutJsonString, FString& OutError);

	UECPAUDIOEXT_API void HandleSetAttenuationShape(const FString& AttenuationPath, const FString& Shape,
		float InnerRadius, float FalloffDistance, FString& OutJsonString, FString& OutError);

	UECPAUDIOEXT_API void HandleSetAttenuationSpatialization(const FString& AttenuationPath,
		const FString& SpatializationMethod, bool bEnableBinaural, FString& OutJsonString, FString& OutError);

	UECPAUDIOEXT_API void HandleAddSubmixEffect(const FString& SubmixPath, const FString& EffectPresetPath,
		FString& OutJsonString, FString& OutError);

	UECPAUDIOEXT_API void HandleRemoveSoundNode(const FString& SoundCuePath, const FString& NodeName,
		FString& OutJsonString, FString& OutError);

	UECPAUDIOEXT_API void HandleCreateDialogueVoice(const FString& Name, const FString& SavePath,
		const FString& Gender, const FString& Plurality,
		FString& OutJsonString, FString& OutError);

	UECPAUDIOEXT_API void HandleCreateDialogueWave(const FString& Name, const FString& SavePath,
		const FString& SpokenText, const FString& SoundWavePath,
		FString& OutJsonString, FString& OutError);

	UECPAUDIOEXT_API void HandleSetSoundWaveProperties(const FString& SoundWavePath,
		float PitchMultiplier, float VolumeMultiplier,
		const FString& Loop, const FString& LoadingBehavior,
		const FString& SoundClassPath, const FString& AttenuationPath,
		FString& OutJsonString, FString& OutError);

	UECPAUDIOEXT_API void HandleGetSoundWaveInfo(const FString& SoundWavePath, FString& OutJsonString, FString& OutError);

	UECPAUDIOEXT_API void HandleCreateSoundCueFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPAUDIOEXT_API void HandleCreateSoundClassFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPAUDIOEXT_API void HandleCreateSoundAttenuationFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPAUDIOEXT_API void HandleCreateSoundConcurrencyFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPAUDIOEXT_API void HandleCreateAudioSubmixFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPAUDIOEXT_API void HandleCreateSoundMixFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPAUDIOEXT_API void HandleCreateSoundControlBusFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPAUDIOEXT_API void HandleCreateControlBusMixFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPAUDIOEXT_API void HandleSetControlBusMixStageFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPAUDIOEXT_API void HandleGetControlBusSummaryFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPAUDIOEXT_API void HandleCreateQuartzClock(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPAUDIOEXT_API void HandleSetQuartzClockSettings(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPAUDIOEXT_API void HandleGetQuartzClockInfo(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPAUDIOEXT_API void HandleGenerateSound(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPAUDIOEXT_API void HandleAddSoundNodeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPAUDIOEXT_API void HandleConnectSoundNodesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPAUDIOEXT_API void HandleSetSoundCueOutputFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPAUDIOEXT_API void HandleSetSoundCueAttenuationFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPAUDIOEXT_API void HandleSetSoundClassPropertiesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPAUDIOEXT_API void HandleSetSoundMixPropertiesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPAUDIOEXT_API void HandleSetAudioSubmixPropertiesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPAUDIOEXT_API void HandleSetSoundConcurrencyPropertiesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPAUDIOEXT_API void HandleAssignSoundConcurrencyFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPAUDIOEXT_API void HandleGetSoundCueSummaryFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPAUDIOEXT_API void HandleSetAttenuationShapeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPAUDIOEXT_API void HandleSetAttenuationSpatializationFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPAUDIOEXT_API void HandleAddSubmixEffectFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPAUDIOEXT_API void HandleRemoveSoundNodeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPAUDIOEXT_API void HandleCreateDialogueVoiceFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPAUDIOEXT_API void HandleCreateDialogueWaveFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPAUDIOEXT_API void HandleSetSoundWavePropertiesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPAUDIOEXT_API void HandleGetSoundWaveInfoFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPAUDIOEXT_API void HandleBuildSoundCueFromSpecFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPAUDIOEXT_API void HandleSetSoundNodePropertyFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
}
