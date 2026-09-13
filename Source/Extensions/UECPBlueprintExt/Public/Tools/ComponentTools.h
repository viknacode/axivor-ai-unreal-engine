// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace ComponentTools
{
	UECPBLUEPRINTEXT_API void HandleAddComponent(const FString& BpPath, const FString& ComponentClass, const FString& ComponentName, const FString& AttachTo, FString& OutJsonString, FString& OutError);

	UECPBLUEPRINTEXT_API FString HandleEditComponentProperty(const FString& BpPath, const FString& ComponentName, const FString& PropertyName, const FString& PropertyValue);

	UECPBLUEPRINTEXT_API FString HandleGetComponentProperty(const FString& BpPath, const FString& ComponentName, const FString& PropertyName);

	UECPBLUEPRINTEXT_API void HandleSetComponentCollisionProfile(const FString& BpPathOrActor, const FString& ComponentName, const FString& ProfileName, FString& OutJsonString, FString& OutError);

	UECPBLUEPRINTEXT_API void HandleSetComponentCollisionResponse(const FString& BpPathOrActor, const FString& ComponentName, const FString& ChannelName, const FString& ResponseType, FString& OutJsonString, FString& OutError);

	UECPBLUEPRINTEXT_API void HandleSetComponentCollisionEnabled(const FString& BpPathOrActor, const FString& ComponentName, const FString& EnabledMode, FString& OutJsonString, FString& OutError);

	UECPBLUEPRINTEXT_API void HandleSetComponentMobility(const FString& BpPathOrActor, const FString& ComponentName, const FString& MobilityMode, FString& OutJsonString, FString& OutError);

	UECPBLUEPRINTEXT_API void HandleSetSkeletalMeshComponent(const FString& BpPathOrActor, const FString& ComponentName, const FString& MeshPath, const FString& AnimBPPath, FString& OutJsonString, FString& OutError);

	UECPBLUEPRINTEXT_API void HandleSetCameraComponentProperties(const FString& BpPathOrActor, const FString& ComponentName, float FOV, const FString& ProjectionMode, float OrthoWidth, FString& OutJsonString, FString& OutError);

	UECPBLUEPRINTEXT_API void HandleSetAudioComponentProperties(const FString& BpPathOrActor, const FString& ComponentName, float VolumeMultiplier, float PitchMultiplier, const FString& AttenuationPath, const FString& SoundClassPath, const FString& AutoActivate, FString& OutJsonString, FString& OutError);

	UECPBLUEPRINTEXT_API void HandleSetComponentCastShadows(const FString& BpPathOrActor, const FString& ComponentName, bool bCastShadow, FString& OutJsonString, FString& OutError);

	UECPBLUEPRINTEXT_API void HandleSetComponentActive(const FString& BpPathOrActor, const FString& ComponentName, bool bActive, FString& OutJsonString, FString& OutError);

	UECPBLUEPRINTEXT_API void HandleSetComponentReplication(const FString& BpPathOrActor, const FString& ComponentName, bool bReplicate, FString& OutJsonString, FString& OutError);

	UECPBLUEPRINTEXT_API void HandleSetComponentTransform(const FString& BpPathOrActor, const FString& ComponentName,
		const FString& LocationStr, const FString& RotationStr, const FString& ScaleStr,
		FString& OutJsonString, FString& OutError);

	UECPBLUEPRINTEXT_API void HandleRemoveComponent(const FString& BpPath, const FString& ComponentName, FString& OutJsonString, FString& OutError);

	UECPBLUEPRINTEXT_API void HandleReparentComponent(const FString& BpPath, const FString& ComponentName, const FString& NewParent, FString& OutJsonString, FString& OutError);
	UECPBLUEPRINTEXT_API void HandleReparentComponentFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPBLUEPRINTEXT_API void HandleSetRootComponent(const FString& BpPath, const FString& ComponentName, FString& OutJsonString, FString& OutError);
	UECPBLUEPRINTEXT_API void HandleSetRootComponentFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPBLUEPRINTEXT_API void HandleRenameComponent(const FString& BpPath, const FString& OldName, const FString& NewName, FString& OutJsonString, FString& OutError);

	UECPBLUEPRINTEXT_API void HandleAddComponentFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPBLUEPRINTEXT_API void HandleEditComponentPropertyFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPBLUEPRINTEXT_API void HandleGetComponentPropertyFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPBLUEPRINTEXT_API void HandleRemoveComponentFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPBLUEPRINTEXT_API void HandleRenameComponentFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPBLUEPRINTEXT_API void HandleSetComponentCollisionProfileFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPBLUEPRINTEXT_API void HandleSetComponentCollisionResponseFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPBLUEPRINTEXT_API void HandleSetComponentCollisionEnabledFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPBLUEPRINTEXT_API void HandleSetComponentMobilityFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPBLUEPRINTEXT_API void HandleSetSkeletalMeshComponentFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPBLUEPRINTEXT_API void HandleSetStaticMeshComponentFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPBLUEPRINTEXT_API void HandleSetCharacterAnimClassFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPBLUEPRINTEXT_API void HandleSetCameraComponentPropertiesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPBLUEPRINTEXT_API void HandleSetAudioComponentPropertiesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPBLUEPRINTEXT_API void HandleSetComponentCastShadowsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPBLUEPRINTEXT_API void HandleSetComponentActiveFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPBLUEPRINTEXT_API void HandleSetComponentReplicationFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPBLUEPRINTEXT_API void HandleSetComponentTransformFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
}
