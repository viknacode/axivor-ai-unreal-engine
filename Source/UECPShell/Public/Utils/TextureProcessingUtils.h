// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "Engine/Texture2D.h"

class FTextureProcessingUtils
{
public:

	static UTexture2D* GenerateNormalMap(UTexture2D* BaseColor, const FString& AssetName, const FString& PackagePath, float Strength = 2.0f);

	static UTexture2D* GenerateRoughnessMap(UTexture2D* BaseColor, const FString& AssetName, const FString& PackagePath, float Scale = 1.0f);

	static UTexture2D* GenerateMetallicMap(UTexture2D* BaseColor, const FString& AssetName, const FString& PackagePath, bool bIsMetallic, float Threshold = 0.5f);

	static UTexture2D* GenerateAOMap(UTexture2D* BaseColor, const FString& AssetName, const FString& PackagePath, float Intensity = 1.0f);

private:
	static bool GetTexturePixels(UTexture2D* Texture, TArray<FColor>& OutPixels, int32& OutWidth, int32& OutHeight);
	static UTexture2D* CreateTextureAsset(const TArray<FColor>& Pixels, int32 Width, int32 Height,
		const FString& AssetName, const FString& PackagePath, bool bSRGB, TextureCompressionSettings Compression);
	static uint8 ToGrayscale(const FColor& Color);
	static void GaussianBlur(TArray<uint8>& Data, int32 Width, int32 Height, int32 Radius);
};
