// Copyright 2026, BlueprintsLab, All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace PostProcessTools
{

	UECPLEVELDESIGNEXT_API void HandleCreatePostProcessVolume(const FString& Label, float LocX, float LocY, float LocZ,
		bool bUnbound, FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleSetPostProcessProperty(const FString& ActorLabel, const FString& PropertyName,
		const FString& PropertyValue, FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleCreateMaterialParameterCollection(const FString& Name, const FString& SavePath,
		FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleAddMPCParameter(const FString& AssetPath, const FString& ParamName,
		const FString& ParamType, const FString& DefaultValue, FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleCreateRenderTarget(const FString& Name, const FString& SavePath,
		int32 Width, int32 Height, const FString& Format, FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleGetPostProcessSummary(const FString& ActorLabel, FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleAddPostProcessBlendable(const FString& ActorLabel, const FString& MaterialPath, float Weight, FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleSetMPCDefaultValue(const FString& AssetPath, const FString& ParamName,
		float ScalarValue, bool bIsScalar, FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleRemovePostProcessBlendable(const FString& ActorLabel, const FString& MaterialPath,
		FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleSetPostProcessPriority(const FString& ActorLabel, float Priority, float BlendRadius, float BlendWeight,
		bool bPrioritySet, bool bRadiusSet, bool bWeightSet,
		FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleCreatePostProcessVolumeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleSetPostProcessPropertyFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleCreateMaterialParameterCollectionFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleAddMPCParameterFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleCreateRenderTargetFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleGetPostProcessSummaryFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleAddPostProcessBlendableFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleSetMPCDefaultValueFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleRemovePostProcessBlendableFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPLEVELDESIGNEXT_API void HandleSetPostProcessPriorityFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPLEVELDESIGNEXT_API void HandleGetMPCSummaryFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
}
