// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "Engine/TextureDefines.h"

struct FMeshyMeshImportResult
{
	bool    bSuccess = false;
	FString ErrorMessage;
	FString PackagePath;
	FString PrimaryAssetPath;
};

struct FMeshyTextureImportResult
{
	bool    bSuccess = false;
	FString ErrorMessage;
	FString AssetPath;
};

struct FMeshySkeletalImportResult
{
	bool    bSuccess = false;
	FString ErrorMessage;
	FString PackagePath;
	FString SkeletalMeshPath;
	FString SkeletonPath;
	FString PhysicsAssetPath;
};

struct FMeshyAnimationImportResult
{
	bool    bSuccess = false;
	FString ErrorMessage;
	FString AnimSequencePath;
};

class UECPASSETGEN_API FMeshyAssetImporter
{
public:
	static FMeshyMeshImportResult ImportMeshFromFile(
		const FString& FilePath,
		const FString& AssetName,
		const FString& PackagePath);

	static FMeshyTextureImportResult ImportTextureFromFile(
		const FString& FilePath,
		const FString& AssetName,
		const FString& PackagePath,
		bool bSRGB,
		TextureCompressionSettings Compression);

	static FMeshySkeletalImportResult ImportSkeletalMeshFromFile(
		const FString& FilePath,
		const FString& AssetName,
		const FString& PackagePath);

	static FMeshyAnimationImportResult ImportAnimationFromFile(
		const FString& FilePath,
		const FString& AssetName,
		const FString& PackagePath,
		const FString& SkeletonAssetPath);

	static FString ResolvePackagePath(const FString& Requested);

	static FString ExportStaticMeshToFbxTemp(const FString& MeshAssetPath, FString& OutError);

	static FString FileToBase64DataUri(const FString& FilePath, FString& OutError);

private:
	static constexpr const TCHAR* DefaultPackagePath = TEXT("/Game/MeshyGen");
};
