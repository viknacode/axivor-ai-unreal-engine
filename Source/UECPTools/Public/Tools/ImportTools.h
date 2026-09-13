// Copyright 2026, BlueprintsLab, All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace ImportTools
{

	UECPTOOLS_API void HandleImportTexture(const FString& FilePath, const FString& DestinationPath,
		bool bSRGB, const FString& CompressionSettings, const FString& LODGroup,
		FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleImportStaticMesh(const FString& FilePath, const FString& DestinationPath,
		float ImportScale, bool bCombineMeshes, bool bGenerateCollision, bool bAutoComputeLOD,
		FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleImportSkeletalMesh(const FString& FilePath, const FString& DestinationPath,
		const FString& SkeletonPath, float ImportScale,
		FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleImportSoundWave(const FString& FilePath, const FString& DestinationPath,
		FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleImportAnimation(const FString& FilePath, const FString& DestinationPath,
		const FString& SkeletonPath, FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleImportAsset(const FString& FilePath, const FString& DestinationPath,
		FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleImportTextureFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPTOOLS_API void HandleImportStaticMeshFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPTOOLS_API void HandleImportSkeletalMeshFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPTOOLS_API void HandleImportSoundWaveFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPTOOLS_API void HandleImportAnimationFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPTOOLS_API void HandleImportAssetFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
}
