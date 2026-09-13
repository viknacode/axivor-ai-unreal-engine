// Copyright 2026, BlueprintsLab, All rights reserved

#include "Utils/TextureProcessingUtils.h"
#include "Engine/Texture2D.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "TextureResource.h"
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"

uint8 FTextureProcessingUtils::ToGrayscale(const FColor& Color)
{
	return static_cast<uint8>(0.299f * Color.R + 0.587f * Color.G + 0.114f * Color.B);
}

bool FTextureProcessingUtils::GetTexturePixels(UTexture2D* Texture, TArray<FColor>& OutPixels, int32& OutWidth, int32& OutHeight)
{
	if (!Texture)
	{
		return false;
	}

	if (Texture->Source.IsValid())
	{
		OutWidth = Texture->Source.GetSizeX();
		OutHeight = Texture->Source.GetSizeY();

		TArray64<uint8> RawData;
		if (Texture->Source.GetMipData(RawData, 0) && RawData.Num() > 0)
		{
			int32 PixelCount = OutWidth * OutHeight;
			OutPixels.SetNum(PixelCount);

			if (RawData.Num() >= PixelCount * 4)
			{
				const FColor* SrcColors = reinterpret_cast<const FColor*>(RawData.GetData());
				FMemory::Memcpy(OutPixels.GetData(), SrcColors, PixelCount * sizeof(FColor));
			}
			else
			{
				return false;
			}
			return true;
		}
	}

	if (Texture->GetPlatformData() && Texture->GetPlatformData()->Mips.Num() > 0)
	{
		FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
		OutWidth = Mip.SizeX;
		OutHeight = Mip.SizeY;

		const void* Data = Mip.BulkData.LockReadOnly();
		if (!Data)
		{
			return false;
		}

		int32 PixelCount = OutWidth * OutHeight;
		OutPixels.SetNum(PixelCount);
		FMemory::Memcpy(OutPixels.GetData(), Data, PixelCount * sizeof(FColor));
		Mip.BulkData.Unlock();
		return true;
	}

	return false;
}

UTexture2D* FTextureProcessingUtils::CreateTextureAsset(const TArray<FColor>& Pixels, int32 Width, int32 Height,
	const FString& AssetName, const FString& PackagePath, bool bSRGB, TextureCompressionSettings Compression)
{
	FString FullPath = PackagePath / AssetName;
	UPackage* Package = CreatePackage(*FullPath);
	if (!Package)
	{
		return nullptr;
	}

	UTexture2D* NewTexture = NewObject<UTexture2D>(Package, FName(*AssetName), RF_Public | RF_Standalone);
	if (!NewTexture)
	{
		return nullptr;
	}

	NewTexture->Source.Init(Width, Height, 1, 1, TSF_BGRA8);
	uint8* DestData = NewTexture->Source.LockMip(0);
	FMemory::Memcpy(DestData, Pixels.GetData(), Pixels.Num() * sizeof(FColor));
	NewTexture->Source.UnlockMip(0);

	NewTexture->SRGB = bSRGB;
	NewTexture->CompressionSettings = Compression;
	NewTexture->MipGenSettings = TMGS_FromTextureGroup;
	NewTexture->LODGroup = TEXTUREGROUP_World;
	NewTexture->UpdateResource();
	NewTexture->PostEditChange();

	Package->MarkPackageDirty();

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	AssetRegistryModule.AssetCreated(NewTexture);

	return NewTexture;
}

void FTextureProcessingUtils::GaussianBlur(TArray<uint8>& Data, int32 Width, int32 Height, int32 Radius)
{
	if (Radius <= 0) return;

	TArray<uint8> Temp;
	Temp.SetNum(Data.Num());

	for (int32 Y = 0; Y < Height; Y++)
	{
		for (int32 X = 0; X < Width; X++)
		{
			int32 Sum = 0;
			int32 Count = 0;
			for (int32 K = -Radius; K <= Radius; K++)
			{
				int32 SampleX = FMath::Clamp(X + K, 0, Width - 1);
				Sum += Data[Y * Width + SampleX];
				Count++;
			}
			Temp[Y * Width + X] = static_cast<uint8>(Sum / Count);
		}
	}

	for (int32 Y = 0; Y < Height; Y++)
	{
		for (int32 X = 0; X < Width; X++)
		{
			int32 Sum = 0;
			int32 Count = 0;
			for (int32 K = -Radius; K <= Radius; K++)
			{
				int32 SampleY = FMath::Clamp(Y + K, 0, Height - 1);
				Sum += Temp[SampleY * Width + X];
				Count++;
			}
			Data[Y * Width + X] = static_cast<uint8>(Sum / Count);
		}
	}
}

UTexture2D* FTextureProcessingUtils::GenerateNormalMap(UTexture2D* BaseColor, const FString& AssetName, const FString& PackagePath, float Strength)
{
	TArray<FColor> SrcPixels;
	int32 Width, Height;
	if (!GetTexturePixels(BaseColor, SrcPixels, Width, Height))
	{
		return nullptr;
	}

	TArray<uint8> HeightMap;
	HeightMap.SetNum(Width * Height);
	for (int32 i = 0; i < SrcPixels.Num(); i++)
	{
		HeightMap[i] = ToGrayscale(SrcPixels[i]);
	}

	TArray<FColor> NormalPixels;
	NormalPixels.SetNum(Width * Height);

	for (int32 Y = 0; Y < Height; Y++)
	{
		for (int32 X = 0; X < Width; X++)
		{
			auto Sample = [&](int32 SX, int32 SY) -> float
			{
				int32 WX = ((SX % Width) + Width) % Width;
				int32 WY = ((SY % Height) + Height) % Height;
				return HeightMap[WY * Width + WX] / 255.0f;
			};

			float dX = -Sample(X - 1, Y - 1) + Sample(X + 1, Y - 1)
			         - 2.0f * Sample(X - 1, Y) + 2.0f * Sample(X + 1, Y)
			         - Sample(X - 1, Y + 1) + Sample(X + 1, Y + 1);

			float dY = -Sample(X - 1, Y - 1) - 2.0f * Sample(X, Y - 1) - Sample(X + 1, Y - 1)
			         + Sample(X - 1, Y + 1) + 2.0f * Sample(X, Y + 1) + Sample(X + 1, Y + 1);

			dX *= Strength;
			dY *= Strength;

			FVector Normal(-dX, -dY, 1.0f);
			Normal.Normalize();

			NormalPixels[Y * Width + X] = FColor(
				static_cast<uint8>(FMath::Clamp((Normal.X * 0.5f + 0.5f) * 255.0f, 0.0f, 255.0f)),
				static_cast<uint8>(FMath::Clamp((Normal.Y * 0.5f + 0.5f) * 255.0f, 0.0f, 255.0f)),
				static_cast<uint8>(FMath::Clamp((Normal.Z * 0.5f + 0.5f) * 255.0f, 0.0f, 255.0f)),
				255
			);
		}
	}

	return CreateTextureAsset(NormalPixels, Width, Height, AssetName, PackagePath, false, TC_Normalmap);
}

UTexture2D* FTextureProcessingUtils::GenerateRoughnessMap(UTexture2D* BaseColor, const FString& AssetName, const FString& PackagePath, float Scale)
{
	TArray<FColor> SrcPixels;
	int32 Width, Height;
	if (!GetTexturePixels(BaseColor, SrcPixels, Width, Height))
	{
		return nullptr;
	}

	TArray<uint8> RoughnessData;
	RoughnessData.SetNum(Width * Height);
	for (int32 i = 0; i < SrcPixels.Num(); i++)
	{
		uint8 Gray = ToGrayscale(SrcPixels[i]);
		RoughnessData[i] = static_cast<uint8>(FMath::Clamp((255 - Gray) * Scale, 0.0f, 255.0f));
	}

	GaussianBlur(RoughnessData, Width, Height, 2);

	TArray<FColor> RoughnessPixels;
	RoughnessPixels.SetNum(Width * Height);
	for (int32 i = 0; i < RoughnessData.Num(); i++)
	{
		uint8 V = RoughnessData[i];
		RoughnessPixels[i] = FColor(V, V, V, 255);
	}

	return CreateTextureAsset(RoughnessPixels, Width, Height, AssetName, PackagePath, false, TC_Grayscale);
}

UTexture2D* FTextureProcessingUtils::GenerateMetallicMap(UTexture2D* BaseColor, const FString& AssetName, const FString& PackagePath, bool bIsMetallic, float Threshold)
{
	TArray<FColor> SrcPixels;
	int32 Width, Height;
	if (!GetTexturePixels(BaseColor, SrcPixels, Width, Height))
	{
		return nullptr;
	}

	TArray<FColor> MetallicPixels;
	MetallicPixels.SetNum(Width * Height);

	if (!bIsMetallic)
	{
		for (int32 i = 0; i < SrcPixels.Num(); i++)
		{
			MetallicPixels[i] = FColor(0, 0, 0, 255);
		}
	}
	else
	{
		for (int32 i = 0; i < SrcPixels.Num(); i++)
		{
			float Lum = ToGrayscale(SrcPixels[i]) / 255.0f;
			uint8 V = Lum > Threshold ? 255 : 0;
			MetallicPixels[i] = FColor(V, V, V, 255);
		}
	}

	return CreateTextureAsset(MetallicPixels, Width, Height, AssetName, PackagePath, false, TC_Grayscale);
}

UTexture2D* FTextureProcessingUtils::GenerateAOMap(UTexture2D* BaseColor, const FString& AssetName, const FString& PackagePath, float Intensity)
{
	TArray<FColor> SrcPixels;
	int32 Width, Height;
	if (!GetTexturePixels(BaseColor, SrcPixels, Width, Height))
	{
		return nullptr;
	}

	TArray<uint8> AOData;
	AOData.SetNum(Width * Height);
	for (int32 i = 0; i < SrcPixels.Num(); i++)
	{
		AOData[i] = 255 - ToGrayscale(SrcPixels[i]);
	}

	GaussianBlur(AOData, Width, Height, 8);

	TArray<FColor> AOPixels;
	AOPixels.SetNum(Width * Height);
	for (int32 i = 0; i < AOData.Num(); i++)
	{
		float CavityValue = AOData[i] / 255.0f;
		float AO = FMath::Lerp(1.0f, 1.0f - CavityValue, Intensity);
		uint8 V = static_cast<uint8>(FMath::Clamp(AO * 255.0f, 0.0f, 255.0f));
		AOPixels[i] = FColor(V, V, V, 255);
	}

	return CreateTextureAsset(AOPixels, Width, Height, AssetName, PackagePath, false, TC_Grayscale);
}
