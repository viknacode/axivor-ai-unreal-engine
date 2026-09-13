// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace MaterialTools
{
	UECPMATERIALEXT_API void HandleCreateMaterial(const FString& MaterialPath, const FLinearColor& Color, FString& OutError);

	UECPMATERIALEXT_API void HandleGenerateTexture(const FString& Prompt, const FString& SavePath, const FString& AspectRatio, const FString& AssetName, FString& OutJsonString, FString& OutError);

	UECPMATERIALEXT_API void HandleGeneratePBRMaterial(const FString& Prompt, const FString& SavePath, const FString& MaterialName, FString& OutJsonString, FString& OutError);
	UECPMATERIALEXT_API void HandleGeneratePBRMaterialFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPMATERIALEXT_API void HandleCreateMaterialFromTextures(const FString& SavePath, const FString& MaterialName, const TArray<TSharedPtr<FJsonValue>>& TexturePaths, FString& OutJsonString, FString& OutError);

	UECPMATERIALEXT_API void HandleCreateMaterialExtended(const FString& MaterialPath, const FLinearColor& Color, const FString& BlendMode, const FString& ShadingModel, FString& OutError);

	UECPMATERIALEXT_API void HandleCreateMaterialInstance(const FString& BaseMaterialPath, const FString& SavePath, const FString& InstanceName, FString& OutJsonString, FString& OutError);
	UECPMATERIALEXT_API void HandleSetMaterialInstanceParameter(const FString& InstancePath, const FString& ParamName, const TSharedPtr<FJsonObject>& ValueJson, FString& OutJsonString, FString& OutError);
	UECPMATERIALEXT_API void HandleGetMaterialInstanceParameters(const FString& InstancePath, FString& OutJsonString, FString& OutError);

	UECPMATERIALEXT_API void HandleSetMaterialInstanceStaticSwitch(const FString& InstancePath, const FString& ParamName, bool bValue, FString& OutJsonString, FString& OutError);

	UECPMATERIALEXT_API void HandleGetMaterialSummary(const FString& MaterialPath, FString& OutJsonString, FString& OutError);

	UECPMATERIALEXT_API void HandleListMaterialNodeTypes(const FString& Filter, FString& OutJsonString, FString& OutError);

	UECPMATERIALEXT_API void HandleCreateMaterialFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPMATERIALEXT_API void HandleCreateMaterialInstanceFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPMATERIALEXT_API void HandleSetMaterialInstanceParameterFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPMATERIALEXT_API void HandleGetMaterialInstanceParametersFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPMATERIALEXT_API void HandleSetMaterialInstanceStaticSwitchFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPMATERIALEXT_API void HandleGetMaterialSummaryFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPMATERIALEXT_API void HandleListMaterialNodeTypesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPMATERIALEXT_API void HandleCreateMaterialFromTexturesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPMATERIALEXT_API void HandleSetMaterialInstanceParent(const FString& InstancePath,
		const FString& NewParentPath, FString& OutJsonString, FString& OutError);

	UECPMATERIALEXT_API void HandleResetMaterialInstanceParameter(const FString& InstancePath,
		const FString& ParamName, const FString& ParamType,
		FString& OutJsonString, FString& OutError);

	UECPMATERIALEXT_API void HandleCreateMaterialParameterCollection(const FString& Name,
		const FString& SavePath, FString& OutJsonString, FString& OutError);

	UECPMATERIALEXT_API void HandleAddCollectionParameter(const FString& CollectionPath,
		const FString& ParamName, const FString& ParamType,
		float ScalarDefault, const FLinearColor& VectorDefault,
		FString& OutJsonString, FString& OutError);

	UECPMATERIALEXT_API void HandleSetCollectionParameterDefault(const FString& CollectionPath,
		const FString& ParamName, const FString& ParamType,
		float ScalarValue, const FLinearColor& VectorValue,
		FString& OutJsonString, FString& OutError);

	UECPMATERIALEXT_API void HandleSetMaterialInstanceParentFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPMATERIALEXT_API void HandleResetMaterialInstanceParameterFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPMATERIALEXT_API void HandleCreateMaterialParameterCollectionFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPMATERIALEXT_API void HandleAddCollectionParameterFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPMATERIALEXT_API void HandleSetCollectionParameterDefaultFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPMATERIALEXT_API void HandleCreateParticleMaterial(const FString& Name,
		const FString& SavePath, const FString& BlendMode, bool bSoftEdge,
		FString& OutJsonString, FString& OutError,
		bool bWithTexture = false,
		float ClipThreshold = 0.0f,
		const FString& TextureParamName = TEXT("BaseColorTex"));

	UECPMATERIALEXT_API void HandleCreateParticleMaterialFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
}
