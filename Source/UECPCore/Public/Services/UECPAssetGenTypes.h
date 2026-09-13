// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "AssetReferenceTypes.h"

struct FTextureGenRequest
{
	FString Prompt;
	FString AspectRatio;
	FString NegativePrompt;
	FString CustomAssetName;
	FString SavePath;
	int32   Seed = -1;
};

struct FTextureGenResult
{
	bool    bSuccess = false;
	FString ErrorMessage;
	FString ImagePath;
	FString AssetPath;
	FString MaterialPath;
	int32   Width = 0;
	int32   Height = 0;
};

struct FMeshCreationRequest
{
	FString Prompt;
	FString NegativePrompt;
	FString CustomAssetName;
	FString SavePath;
	int32   Seed = -1;
	bool    bAutoRefine = true;
	FString ImagePath;
	FString ImageBase64Data;
	FString PreviewTaskId;
	FString ModelFormat;
	FString PoseMode;
};

struct FMeshCreationResult
{
	bool    bSuccess = false;
	FString ErrorMessage;
	FString DownloadPath;
	FString AssetPath;
	FString TaskId;
	FString Status;
	FString ThumbnailUrl;
	int32   Progress = 0;
	bool    bIsPreview = true;
};

struct FRetextureRequest
{
	FString MeshAssetPath;
	FString StylePrompt;
	FString SavePath;
	FString MaterialName;
	bool    bEnablePBR = true;
};

struct FRetextureResult
{
	bool    bSuccess = false;
	FString ErrorMessage;
	FString TaskId;
	FString Status;
	int32   Progress = 0;
	FString BaseColorPath;
	FString MetallicPath;
	FString RoughnessPath;
	FString NormalPath;
	FString MaterialPath;
};

struct FSoundGenRequest
{
	FString Text;
	FString Voice;
	FString CustomAssetName;
	FString SavePath;
	FString Model;
	float   Speed = 1.0f;
};

struct FSoundGenResult
{
	bool    bSuccess = false;
	FString ErrorMessage;
	FString FilePath;
	FString AssetPath;
	FString AssetName;
	float   Duration = 0.0f;
};
