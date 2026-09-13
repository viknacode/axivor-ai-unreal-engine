// Copyright 2026, BlueprintsLab, All rights reserved.

#include "Tools/LandscapeTools.h"
#include "Misc/EngineVersionComparison.h"

#include "Landscape.h"
#include "LandscapeProxy.h"
#include "LandscapeInfo.h"
#include "LandscapeLayerInfoObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

#include "EditorAssetLibrary.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Editor.h"
#include "Materials/MaterialInterface.h"
#include "PCGGraph.h"
#include "PCGNode.h"
#include "PCGPin.h"
#include "PCGEdge.h"
#include "PCGSettings.h"
#include "PCGComponent.h"
#include "Components/BoxComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "PCGManagedResource.h"
#include "Tools/PCGTools.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/UnrealType.h"
#include "ScopedTransaction.h"

#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace LandscapeTools
{

static float FractalNoise(float NX, float NY, int32 Octaves, float BaseFreq, float Lacunarity = 2.0f, float Persistence = 0.5f, float SeedOffset = 0.0f)
{
	float Value = 0.f;
	float Amp   = 0.5f;
	float Freq  = BaseFreq;
	for (int32 i = 0; i < Octaves; i++)
	{
		Value += FMath::PerlinNoise2D(FVector2D(NX * Freq + 0.13f + SeedOffset, NY * Freq + 0.37f + SeedOffset)) * Amp;
		Amp  *= Persistence;
		Freq *= Lacunarity;
	}
	return FMath::Clamp((Value + 1.0f) * 0.5f, 0.0f, 1.0f);
}

static void HydraulicErosion(TArray<float>& HeightMap, int32 Width, int32 Height, int32 Iterations, float Strength, int32 Seed)
{
	if (Iterations <= 0 || Width < 3 || Height < 3) return;

	FRandomStream Rng(Seed);
	const float Gravity = 4.0f;
	const float Inertia = 0.05f;
	const float CapacityFactor = 4.0f * Strength;
	const float DepositSpeed = 0.3f;
	const float ErodeSpeed = 0.3f * Strength;
	const float EvaporateRate = 0.01f;
	const int32 MaxLifetime = 64;
	const int32 ErosionRadius = 3;

	TArray<int32> BrushOffsets;
	TArray<float> BrushWeights;
	float WeightSum = 0.0f;
	for (int32 by = -ErosionRadius; by <= ErosionRadius; by++)
	{
		for (int32 bx = -ErosionRadius; bx <= ErosionRadius; bx++)
		{
			float D = FMath::Sqrt(static_cast<float>(bx * bx + by * by));
			if (D <= ErosionRadius)
			{
				float W = FMath::Max(0.0f, ErosionRadius - D);
				BrushOffsets.Add(by * Width + bx);
				BrushWeights.Add(W);
				WeightSum += W;
			}
		}
	}
	for (float& W : BrushWeights) W /= WeightSum;

	auto GetGradient = [&](float PX, float PY, float& OutHeight) -> FVector2D
	{
		int32 IX = FMath::Clamp(static_cast<int32>(PX), 0, Width - 2);
		int32 IY = FMath::Clamp(static_cast<int32>(PY), 0, Height - 2);
		float FX = PX - IX;
		float FY = PY - IY;

		float H00 = HeightMap[IY * Width + IX];
		float H10 = HeightMap[IY * Width + IX + 1];
		float H01 = HeightMap[(IY + 1) * Width + IX];
		float H11 = HeightMap[(IY + 1) * Width + IX + 1];

		OutHeight = H00 * (1 - FX) * (1 - FY) + H10 * FX * (1 - FY) + H01 * (1 - FX) * FY + H11 * FX * FY;
		return FVector2D(
			(H10 - H00) * (1 - FY) + (H11 - H01) * FY,
			(H01 - H00) * (1 - FX) + (H11 - H10) * FX
		);
	};

	for (int32 Iter = 0; Iter < Iterations; Iter++)
	{
		float PosX = Rng.FRandRange(1.0f, static_cast<float>(Width - 2));
		float PosY = Rng.FRandRange(1.0f, static_cast<float>(Height - 2));
		float DirX = 0, DirY = 0;
		float Speed = 1.0f;
		float Water = 1.0f;
		float Sediment = 0.0f;

		for (int32 Life = 0; Life < MaxLifetime; Life++)
		{
			int32 NodeIdx = static_cast<int32>(PosY) * Width + static_cast<int32>(PosX);

			float OldHeight;
			FVector2D Grad = GetGradient(PosX, PosY, OldHeight);

			DirX = DirX * Inertia - Grad.X * (1.0f - Inertia);
			DirY = DirY * Inertia - Grad.Y * (1.0f - Inertia);
			float Len = FMath::Sqrt(DirX * DirX + DirY * DirY);
			if (Len < 0.0001f) break;
			DirX /= Len;
			DirY /= Len;

			float NewPosX = PosX + DirX;
			float NewPosY = PosY + DirY;

			if (NewPosX < 1 || NewPosX >= Width - 2 || NewPosY < 1 || NewPosY >= Height - 2) break;

			float NewHeight;
			GetGradient(NewPosX, NewPosY, NewHeight);
			float HeightDiff = NewHeight - OldHeight;

			float Capacity = FMath::Max(-HeightDiff * Speed * Water * CapacityFactor, 0.01f);

			if (Sediment > Capacity || HeightDiff > 0)
			{
				float Deposit = (HeightDiff > 0)
					? FMath::Min(HeightDiff, Sediment)
					: (Sediment - Capacity) * DepositSpeed;
				Sediment -= Deposit;

				for (int32 B = 0; B < BrushOffsets.Num(); B++)
				{
					int32 Idx = NodeIdx + BrushOffsets[B];
					if (Idx >= 0 && Idx < HeightMap.Num())
						HeightMap[Idx] += Deposit * BrushWeights[B];
				}
			}
			else
			{
				float Erode = FMath::Min((Capacity - Sediment) * ErodeSpeed, -HeightDiff);
				Sediment += Erode;

				for (int32 B = 0; B < BrushOffsets.Num(); B++)
				{
					int32 Idx = NodeIdx + BrushOffsets[B];
					if (Idx >= 0 && Idx < HeightMap.Num())
						HeightMap[Idx] -= Erode * BrushWeights[B];
				}
			}

			Speed = FMath::Sqrt(FMath::Max(0.0f, Speed * Speed + HeightDiff * Gravity));
			Water *= (1.0f - EvaporateRate);
			PosX = NewPosX;
			PosY = NewPosY;
		}
	}
}

static void ApplyIslandFalloff(TArray<float>& HeightMap, int32 Width, int32 Height, float Falloff, float SeaLevel, float SeedOffset)
{
	if (Falloff <= 0.0f) return;

	for (int32 Y = 0; Y < Height; Y++)
	{
		for (int32 X = 0; X < Width; X++)
		{
			float NX = static_cast<float>(X) / (Width - 1);
			float NY = static_cast<float>(Y) / (Height - 1);
			float DX = (NX - 0.5f) * 2.0f;
			float DY = (NY - 0.5f) * 2.0f;
			float Dist = FMath::Sqrt(DX * DX + DY * DY);

			float CoastNoise = FMath::PerlinNoise2D(FVector2D(NX * 3.0f + SeedOffset, NY * 3.0f + SeedOffset + 5.0f)) * 0.15f
			                 + FMath::PerlinNoise2D(FVector2D(NX * 7.0f + SeedOffset + 2.0f, NY * 7.0f + SeedOffset + 9.0f)) * 0.08f;
			Dist += CoastNoise;

			float InnerRadius = 0.3f;
			float OuterRadius = 0.9f;
			float Mask = 1.0f - FMath::SmoothStep(InnerRadius, OuterRadius, Dist);

			Mask = FMath::Pow(Mask, 1.0f + Falloff * 0.3f);

			float& H = HeightMap[Y * Width + X];
			H = H * Mask;
			if (Mask < 0.01f) H = 0.0f;
		}
	}
}

static void GenerateHeightmap(TArray<uint16>& HeightData, int32 VertsX, int32 VertsY,
	const FString& Preset, float CenterFlatRadius, float MountainHeight,
	int32 Octaves = 4, float Lacunarity = 2.0f, float Persistence = 0.5f,
	int32 ErosionIterations = 0, float ErosionStrength = 0.3f,
	float SeaLevel = 0.3f, float IslandFalloff = 0.0f, int32 Seed = 0)
{
	HeightData.SetNumUninitialized(VertsX * VertsY);
	const float MH = FMath::Clamp(MountainHeight, 0.0f, 1.0f);
	const float CFR = FMath::Clamp(CenterFlatRadius, 0.05f, 0.9f);
	const float SeedOff = Seed * 17.31f;

	TArray<float> FloatMap;
	FloatMap.SetNumUninitialized(VertsX * VertsY);

	for (int32 Y = 0; Y < VertsY; Y++)
	{
		for (int32 X = 0; X < VertsX; X++)
		{
			const float NX = static_cast<float>(X) / (VertsX - 1);
			const float NY = static_cast<float>(Y) / (VertsY - 1);
			const float DX = NX - 0.5f;
			const float DY = NY - 0.5f;
			const float Dist = FMath::Sqrt(DX * DX + DY * DY) * 2.0f;

			float Height = 0.f;

			if (Preset == TEXT("mountains_ring"))
			{
				float EdgeFactor  = FMath::SmoothStep(CFR, CFR + 0.30f, Dist);
				float PeakNoise   = FractalNoise(NX, NY, FMath::Max(Octaves, 5), 3.5f, Lacunarity, Persistence, SeedOff);
				float DetailNoise = FractalNoise(NX * 2.7f + 4.1f, NY * 2.7f + 7.3f, 5, 6.0f, 2.0f, 0.5f, SeedOff);
				float CenterNoise = FractalNoise(NX * 0.6f + 1.3f, NY * 0.6f + 2.9f, 3, 1.2f, 2.0f, 0.5f, SeedOff);
				float MountainH = EdgeFactor * (MH * 0.75f + PeakNoise * MH * 0.35f) + DetailNoise * 0.06f * EdgeFactor;
				float CenterH   = CenterNoise * 0.04f * (1.0f - EdgeFactor);
				Height = MountainH + CenterH;
			}
			else if (Preset == TEXT("rolling_hills"))
			{
				float LargeWave  = FractalNoise(NX, NY, Octaves, 1.2f, Lacunarity, Persistence, SeedOff);
				float MediumBump = FractalNoise(NX * 2.0f + 3.1f, NY * 2.0f + 5.7f, Octaves + 1, 2.8f, Lacunarity, Persistence, SeedOff);
				float Fine       = FractalNoise(NX * 5.0f + 1.9f, NY * 5.0f + 0.3f, 3, 5.5f, 2.0f, 0.5f, SeedOff);
				Height = (LargeWave * 0.55f + MediumBump * 0.30f + Fine * 0.08f) * MH;
			}
			else if (Preset == TEXT("valley"))
			{
				float RimFactor  = FMath::SmoothStep(CFR * 0.4f, CFR + 0.35f, Dist);
				float RimNoise   = FractalNoise(NX, NY, Octaves + 2, 3.2f, Lacunarity, Persistence, SeedOff);
				float FloorNoise = FractalNoise(NX * 3.0f + 2.2f, NY * 3.0f + 8.1f, 4, 4.5f, 2.0f, 0.5f, SeedOff);
				Height = RimFactor * (MH * 0.5f + RimNoise * MH * 0.25f) + FloorNoise * 0.03f * (1.0f - RimFactor);
			}
			else if (Preset == TEXT("island"))
			{
				float Continent = FractalNoise(NX, NY, Octaves, 2.0f, Lacunarity, Persistence, SeedOff);
				float Mountains = FractalNoise(NX * 1.5f + 2.1f, NY * 1.5f + 3.7f, Octaves + 2, 4.0f, Lacunarity, Persistence, SeedOff);
				float Detail    = FractalNoise(NX * 4.0f + 0.8f, NY * 4.0f + 6.2f, 3, 8.0f, 2.0f, 0.5f, SeedOff);
				Height = (Continent * 0.5f + Mountains * 0.4f + Detail * 0.05f) * MH;
			}
			else if (Preset == TEXT("archipelago"))
			{
				float Continents = FractalNoise(NX, NY, 3, 5.0f, 2.0f, 0.6f, SeedOff);
				float Detail     = FractalNoise(NX * 3.0f + 1.1f, NY * 3.0f + 4.3f, Octaves, 6.0f, Lacunarity, Persistence, SeedOff);
				float IslandMask = FMath::SmoothStep(0.45f, 0.55f, Continents);
				Height = (IslandMask * 0.6f + Detail * 0.15f * IslandMask) * MH;
			}
			else if (Preset == TEXT("canyon"))
			{
				float Base      = FractalNoise(NX, NY, Octaves, 2.5f, Lacunarity, Persistence, SeedOff) * MH;
				float CanyonLine = FMath::Abs(FMath::PerlinNoise2D(FVector2D(NY * 2.0f + SeedOff, 0.5f)));
				float CanyonDist = FMath::Abs(NX - 0.5f + CanyonLine * 0.15f);
				float CanyonMask = FMath::SmoothStep(0.02f, 0.12f, CanyonDist);
				Height = Base * CanyonMask + 0.02f * (1.0f - CanyonMask);
			}
			else if (Preset == TEXT("coastal_cliffs"))
			{
				float CliffNoise = FractalNoise(NX, NY, Octaves, 3.0f, Lacunarity, Persistence, SeedOff);
				float CliffLine  = NX + FMath::PerlinNoise2D(FVector2D(NY * 3.0f + SeedOff, 0.3f)) * 0.1f;
				float CliffMask  = FMath::SmoothStep(0.35f, 0.45f, CliffLine);
				Height = (CliffMask * MH * 0.7f + CliffNoise * 0.2f * CliffMask);
			}
			else if (Preset == TEXT("plateau"))
			{
				float Base = FractalNoise(NX, NY, Octaves, 2.0f, Lacunarity, Persistence, SeedOff);
				float PlateauDist = FMath::Max(FMath::Abs(DX), FMath::Abs(DY)) * 2.0f;
				float PlateauMask = 1.0f - FMath::SmoothStep(0.5f, 0.7f, PlateauDist);
				float PlateauH = FMath::Max(Base, 0.6f) * PlateauMask;
				float EdgeDetail = FractalNoise(NX * 4.0f, NY * 4.0f, 3, 6.0f, 2.0f, 0.5f, SeedOff);
				Height = (PlateauH + EdgeDetail * 0.05f * (1.0f - PlateauMask)) * MH;
			}
			else if (Preset == TEXT("volcanic"))
			{
				float ConeDist = Dist;
				float ConeH = FMath::Max(0.0f, 1.0f - ConeDist * 1.5f);
				ConeH = FMath::Pow(ConeH, 0.8f);
				float CraterMask = FMath::SmoothStep(0.0f, 0.15f, ConeDist);
				ConeH *= CraterMask + (1.0f - CraterMask) * 0.7f;
				float SurfaceNoise = FractalNoise(NX * 3.0f, NY * 3.0f, Octaves, 5.0f, Lacunarity, Persistence, SeedOff);
				Height = (ConeH * 0.85f + SurfaceNoise * 0.08f) * MH;
			}
			else
			{
				Height = 0.f;
			}

			FloatMap[Y * VertsX + X] = FMath::Clamp(Height, 0.0f, 1.0f);
		}
	}

	if (IslandFalloff > 0.0f)
	{
		ApplyIslandFalloff(FloatMap, VertsX, VertsY, IslandFalloff, SeaLevel, SeedOff);
	}

	if (ErosionIterations > 0)
	{
		HydraulicErosion(FloatMap, VertsX, VertsY, ErosionIterations, ErosionStrength, Seed);
	}

	for (int32 i = 0; i < FloatMap.Num(); i++)
	{
		float H = FMath::Clamp(FloatMap[i], 0.0f, 1.0f);
		HeightData[i] = static_cast<uint16>(32768 + H * 32767.0f);
	}
}

void HandleCreateLandscapeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString ActorLabel; Args->TryGetStringField(TEXT("actor_label"), ActorLabel);
	if (ActorLabel.IsEmpty()) Args->TryGetStringField(TEXT("name"), ActorLabel);
	if (ActorLabel.IsEmpty()) ActorLabel = TEXT("GeneratedLandscape");

	FString Preset; Args->TryGetStringField(TEXT("preset"), Preset);
	if (Preset.IsEmpty()) Preset = TEXT("rolling_hills");

	float CenterFlatRadius = 0.3f; Args->TryGetNumberField(TEXT("center_flat_radius"), CenterFlatRadius);
	float MountainHeight = 0.7f; Args->TryGetNumberField(TEXT("mountain_height"), MountainHeight);
	int32 SizePreset = 1; Args->TryGetNumberField(TEXT("size_preset"), SizePreset);
	float ScaleXY = 100.0f; Args->TryGetNumberField(TEXT("scale_xy"), ScaleXY);
	float ScaleZ = 100.0f; Args->TryGetNumberField(TEXT("scale_z"), ScaleZ);
	float LocationX = 0.0f; Args->TryGetNumberField(TEXT("location_x"), LocationX);
	float LocationY = 0.0f; Args->TryGetNumberField(TEXT("location_y"), LocationY);
	float LocationZ = 0.0f; Args->TryGetNumberField(TEXT("location_z"), LocationZ);

	int32 Octaves = 4; Args->TryGetNumberField(TEXT("octaves"), Octaves);
	double Lacunarity = 2.0; Args->TryGetNumberField(TEXT("lacunarity"), Lacunarity);
	double Persistence = 0.5; Args->TryGetNumberField(TEXT("persistence"), Persistence);
	int32 ErosionIterations = 0; Args->TryGetNumberField(TEXT("erosion_iterations"), ErosionIterations);
	double ErosionStrength = 0.3; Args->TryGetNumberField(TEXT("erosion_strength"), ErosionStrength);
	double SeaLevel = 0.3; Args->TryGetNumberField(TEXT("sea_level"), SeaLevel);
	double IslandFalloff = 0.0; Args->TryGetNumberField(TEXT("island_falloff"), IslandFalloff);
	int32 Seed = FMath::Rand(); Args->TryGetNumberField(TEXT("seed"), Seed);
	int32 Resolution = 0; Args->TryGetNumberField(TEXT("resolution"), Resolution);

	if (IslandFalloff <= 0.0f && (Preset == TEXT("island") || Preset == TEXT("archipelago")))
		IslandFalloff = 2.0f;
	if (ErosionIterations <= 0 && (Preset == TEXT("island") || Preset == TEXT("canyon") ||
		Preset == TEXT("mountains_ring") || Preset == TEXT("volcanic")))
		ErosionIterations = 5000;

	HandleCreateLandscape(ActorLabel, Preset, CenterFlatRadius, MountainHeight, SizePreset, ScaleXY, ScaleZ,
		LocationX, LocationY, LocationZ, Octaves, static_cast<float>(Lacunarity), static_cast<float>(Persistence),
		ErosionIterations, static_cast<float>(ErosionStrength), static_cast<float>(SeaLevel),
		static_cast<float>(IslandFalloff), Seed, Resolution, OutJsonString, OutError);
}

void HandleCreateLandscape(
	const FString& ActorLabel,
	const FString& Preset,
	float CenterFlatRadius,
	float MountainHeight,
	int32 SizePreset,
	float ScaleXY,
	float ScaleZ,
	float LocationX,
	float LocationY,
	float LocationZ,
	int32 Octaves,
	float Lacunarity,
	float Persistence,
	int32 ErosionIterations,
	float ErosionStrength,
	float SeaLevel,
	float IslandFalloff,
	int32 Seed,
	int32 Resolution,
	FString& OutJsonString,
	FString& OutError)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World) { OutError = TEXT("No editor world available"); return; }

	const int32 NumSubsections      = 1;
	const int32 SubsectionSizeQuads = 63;
	int32 ComponentsXY;
	if (Resolution >= 1009) ComponentsXY = 16;
	else if (Resolution >= 505) ComponentsXY = 8;
	else if (Resolution >= 253) ComponentsXY = 4;
	else if (Resolution > 0) ComponentsXY = 2;
	else ComponentsXY = (SizePreset >= 2) ? 16 : (SizePreset == 1) ? 8 : 4;
	const int32 TotalQuads          = ComponentsXY * SubsectionSizeQuads;
	const int32 VertsXY             = TotalQuads + 1;

	TArray<uint16> HeightData;
	GenerateHeightmap(HeightData, VertsXY, VertsXY, Preset, CenterFlatRadius, MountainHeight,
		Octaves, Lacunarity, Persistence, ErosionIterations, ErosionStrength, SeaLevel, IslandFalloff, Seed);

	FActorSpawnParameters Params;
	FVector Location(LocationX, LocationY, LocationZ);

	ALandscape* Landscape = World->SpawnActor<ALandscape>(ALandscape::StaticClass(), Location, FRotator::ZeroRotator, Params);
	if (!Landscape) { OutError = TEXT("Failed to spawn ALandscape actor"); return; }
	Landscape->SetActorLabel(ActorLabel);
	Landscape->SetLandscapeGuid(FGuid::NewGuid());

	Landscape->SetActorScale3D(FVector(ScaleXY, ScaleXY, ScaleZ));

	const FGuid LandscapeGuid = Landscape->GetLandscapeGuid();

	TMap<FGuid, TArray<uint16>>                   ImportHeightData;
	TMap<FGuid, TArray<FLandscapeImportLayerInfo>> ImportLayerInfos;
	ImportHeightData.Add(FGuid(), HeightData);
	ImportLayerInfos.Add(FGuid(), TArray<FLandscapeImportLayerInfo>());

#if UE_VERSION_OLDER_THAN(5, 5, 0)
	Landscape->Import(
		LandscapeGuid,
		0, 0, VertsXY - 1, VertsXY - 1,
		NumSubsections, SubsectionSizeQuads,
		ImportHeightData,
		nullptr,
		ImportLayerInfos,
		ELandscapeImportAlphamapType::Additive,
		nullptr);
#else
	Landscape->Import(
		LandscapeGuid,
		0, 0, VertsXY - 1, VertsXY - 1,
		NumSubsections, SubsectionSizeQuads,
		ImportHeightData,
		nullptr,
		ImportLayerInfos,
		ELandscapeImportAlphamapType::Additive,
		TArrayView<const FLandscapeLayer>());
#endif

	ULandscapeInfo* LandscapeInfo = Landscape->CreateLandscapeInfo();
	if (LandscapeInfo)
	{
		LandscapeInfo->UpdateLayerInfoMap(Landscape);
	}

	Landscape->MarkPackageDirty();
	World->MarkPackageDirty();
	if (GEditor) GEditor->RedrawAllViewports();

	const float TotalSizeM = (VertsXY - 1) * ScaleXY / 100.f;

	TSharedPtr<FJsonObject> Out = MakeShareable(new FJsonObject);
	Out->SetBoolField(TEXT("success"), true);
	Out->SetStringField(TEXT("actor_label"), Landscape->GetActorLabel());
	Out->SetStringField(TEXT("preset"), Preset);
	Out->SetNumberField(TEXT("verts_xy"), VertsXY);
	Out->SetNumberField(TEXT("total_size_m"), TotalSizeM);
	Out->SetStringField(TEXT("message"), FString::Printf(
		TEXT("Created landscape '%s' (%dx%d verts, %.0fm x %.0fm) with preset '%s'. Use set_landscape_material to apply a material, then use PCG with PCGSurfaceSamplerSettings to scatter objects on it."),
		*Landscape->GetActorLabel(), VertsXY, VertsXY, TotalSizeM, TotalSizeM, *Preset));

	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Out.ToSharedRef(), Writer);
}

void HandleSetLandscapeMaterial(
	const FString& ActorLabel,
	const FString& MaterialPath,
	FString& OutJsonString,
	FString& OutError)
{

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World) { OutError = TEXT("No editor world available"); return; }

	ALandscape* Landscape = nullptr;
	for (TActorIterator<ALandscape> It(World); It; ++It)
	{
		if (It->GetActorLabel().Equals(ActorLabel, ESearchCase::IgnoreCase))
		{
			Landscape = *It;
			break;
		}
	}
	if (!Landscape) { OutError = FString::Printf(TEXT("No ALandscape actor with label '%s' found"), *ActorLabel); return; }

	UMaterialInterface* Material = nullptr;
	for (int32 Attempt = 0; Attempt < 40; Attempt++)
	{
		UObject* MatObj = UEditorAssetLibrary::LoadAsset(MaterialPath);
		Material = Cast<UMaterialInterface>(MatObj);
		if (Material) break;

		if (Attempt == 0)
			UE_LOG(LogTemp, Log, TEXT("SetLandscapeMaterial: Material '%s' not found yet, waiting for async generation..."), *MaterialPath);

		FPlatformProcess::Sleep(0.5f);
	}
	if (!Material) { OutError = FString::Printf(TEXT("Could not load material at '%s' (waited 20s for async generation)"), *MaterialPath); return; }

	Landscape->LandscapeMaterial = Material;

	FPropertyChangedEvent MatChangedEvent(ALandscapeProxy::StaticClass()->FindPropertyByName(TEXT("LandscapeMaterial")));
	Landscape->PostEditChangeProperty(MatChangedEvent);
	Landscape->MarkPackageDirty();

	if (GEditor) GEditor->RedrawAllViewports();

	TSharedPtr<FJsonObject> Out = MakeShareable(new FJsonObject);
	Out->SetBoolField(TEXT("success"), true);
	Out->SetStringField(TEXT("message"), FString::Printf(
		TEXT("Assigned material '%s' to landscape '%s'."), *MaterialPath, *ActorLabel));

	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Out.ToSharedRef(), Writer);
}

static ALandscape* FindLandscape(UWorld* World, const FString& ActorLabel, FString& OutError)
{
	if (!World) { OutError = TEXT("No editor world available"); return nullptr; }
	for (TActorIterator<ALandscape> It(World); It; ++It)
	{
		if (ActorLabel.IsEmpty() || It->GetActorLabel().Equals(ActorLabel, ESearchCase::IgnoreCase))
			return *It;
	}
	for (TActorIterator<ALandscapeProxy> It(World); It; ++It)
	{
		if (ActorLabel.IsEmpty() || It->GetActorLabel().Equals(ActorLabel, ESearchCase::IgnoreCase))
			return Cast<ALandscape>(*It);
	}
	OutError = FString::Printf(TEXT("No Landscape actor found%s"), ActorLabel.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" with label '%s'"), *ActorLabel));
	return nullptr;
}

void HandleGetLandscapeInfo(const FString& ActorLabel, FString& OutJsonString, FString& OutError)
{

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	ALandscape* Landscape = FindLandscape(World, ActorLabel, OutError);
	if (!Landscape) return;

	TSharedPtr<FJsonObject> Res = MakeShareable(new FJsonObject());
	Res->SetBoolField(TEXT("success"), true);
	Res->SetStringField(TEXT("actor_label"), Landscape->GetActorLabel());

	FVector Origin, Extent;
	Landscape->GetActorBounds(false, Origin, Extent);
	Res->SetStringField(TEXT("bounds_origin"), FString::Printf(TEXT("%.0f,%.0f,%.0f"), Origin.X, Origin.Y, Origin.Z));
	Res->SetStringField(TEXT("bounds_extent"), FString::Printf(TEXT("%.0f,%.0f,%.0f"), Extent.X, Extent.Y, Extent.Z));

	FVector Scale = Landscape->GetActorScale();
	Res->SetStringField(TEXT("scale"), FString::Printf(TEXT("%.1f,%.1f,%.1f"), Scale.X, Scale.Y, Scale.Z));

	if (Landscape->LandscapeMaterial)
		Res->SetStringField(TEXT("material"), Landscape->LandscapeMaterial->GetPathName());
	else
		Res->SetStringField(TEXT("material"), TEXT("None"));

	Res->SetNumberField(TEXT("component_count"), Landscape->LandscapeComponents.Num());

	Res->SetNumberField(TEXT("static_lighting_lod"), Landscape->StaticLightingLOD);

	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Res.ToSharedRef(), W);
}

void HandleSetLandscapeProperties(const FString& ActorLabel, const FString& PropertyName,
	const FString& PropertyValue, FString& OutJsonString, FString& OutError)
{

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	ALandscape* Landscape = FindLandscape(World, ActorLabel, OutError);
	if (!Landscape) return;

	FProperty* Prop = FindFProperty<FProperty>(Landscape->GetClass(), *PropertyName);
	if (!Prop)
	{
		OutError = FString::Printf(TEXT("Property '%s' not found on ALandscape"), *PropertyName);
		return;
	}

	Prop->ImportText_Direct(*PropertyValue, Prop->ContainerPtrToValuePtr<void>(Landscape), Landscape, PPF_None);
	Landscape->MarkPackageDirty();

	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"property\":\"%s\",\"value\":\"%s\"}"), *PropertyName, *PropertyValue);
}

void HandleSetLandscapeLOD(const FString& ActorLabel, int32 StaticLightingLOD,
	int32 LODBias, float LODDistanceFactor, FString& OutJsonString, FString& OutError)
{

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	ALandscape* Landscape = FindLandscape(World, ActorLabel, OutError);
	if (!Landscape) return;

	if (StaticLightingLOD >= 0) Landscape->StaticLightingLOD = StaticLightingLOD;

	Landscape->MarkPackageDirty();

	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"static_lighting_lod\":%d}"),
		Landscape->StaticLightingLOD);
}

void HandleExportLandscapeHeightmap(const FString& ActorLabel, const FString& FilePath,
	FString& OutJsonString, FString& OutError)
{

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	ALandscape* Landscape = FindLandscape(World, ActorLabel, OutError);
	if (!Landscape) return;

	if (FilePath.IsEmpty())
	{
		OutError = TEXT("file_path is required (absolute path to the .png/.raw heightmap file to write)");
		return;
	}

	ULandscapeInfo* Info = Landscape->GetLandscapeInfo();
	if (!Info) { OutError = TEXT("Could not get LandscapeInfo"); return; }

	int32 MinX, MinY, MaxX, MaxY;
	if (!Info->GetLandscapeExtent(MinX, MinY, MaxX, MaxY))
	{
		OutError = TEXT("Could not get landscape extent");
		return;
	}

	int32 Width = MaxX - MinX + 1;
	int32 Height = MaxY - MinY + 1;

	const FString FullPath = FPaths::ConvertRelativePathToFull(FilePath);

	IFileManager& FileMgr = IFileManager::Get();
	const FString ParentDir = FPaths::GetPath(FullPath);
	if (!ParentDir.IsEmpty())
	{
		FileMgr.MakeDirectory(*ParentDir, true);
	}

	Info->ExportHeightmap(FullPath);

	if (!FileMgr.FileExists(*FullPath))
	{
		OutError = FString::Printf(TEXT("ExportHeightmap did not produce a file at '%s' (check the path/extension is writable and the format is supported)"), *FullPath);
		return;
	}

	const int64 FileSize = FileMgr.FileSize(*FullPath);
	if (FileSize <= 0)
	{
		OutError = FString::Printf(TEXT("Heightmap file at '%s' was written empty (%lld bytes)"), *FullPath, FileSize);
		return;
	}

	TSharedPtr<FJsonObject> Res = MakeShareable(new FJsonObject());
	Res->SetBoolField(TEXT("success"), true);
	Res->SetStringField(TEXT("actor_label"), Landscape->GetActorLabel());
	Res->SetStringField(TEXT("file_path"), FullPath);
	Res->SetNumberField(TEXT("width"), Width);
	Res->SetNumberField(TEXT("height"), Height);
	Res->SetNumberField(TEXT("file_size_bytes"), (double)FileSize);
	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Res.ToSharedRef(), W);
}

void HandleAddLandscapeLayerInfo(const FString& ActorLabel, const FString& LayerName,
	const FString& SavePath, FString& OutJsonString, FString& OutError)
{

	if (LayerName.IsEmpty()) { OutError = TEXT("layer_name is required"); return; }

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	ALandscape* Landscape = FindLandscape(World, ActorLabel, OutError);
	if (!Landscape) return;

	ULandscapeInfo* Info = Landscape->GetLandscapeInfo();
	if (!Info) { OutError = TEXT("Could not get LandscapeInfo"); return; }

	const FName LayerFName(*LayerName);

	const FScopedTransaction Transaction(NSLOCTEXT("AxivorAI", "AddLandscapeLayerInfo", "Add Landscape Layer Info"));

	// Does the landscape already know about a target layer with this name (e.g. declared by the
	// material via a LandscapeLayerBlend/LayerWeight node, but not yet backed by a LayerInfo asset)?
	const bool bHasTargetLayer = Landscape->HasTargetLayer(LayerFName);
	ULandscapeLayerInfoObject* ExistingLayerInfoObj = nullptr;
	if (bHasTargetLayer)
	{
		if (const FLandscapeTargetLayerSettings* Existing = Landscape->GetTargetLayers().Find(LayerFName))
		{
			ExistingLayerInfoObj = Existing->LayerInfoObj;
		}
	}

	if (ExistingLayerInfoObj)
	{
		TArray<TSharedPtr<FJsonValue>> KnownLayers;
		for (const auto& TargetLayer : Landscape->GetTargetLayers())
		{
			KnownLayers.Add(MakeShareable(new FJsonValueString(TargetLayer.Key.ToString())));
		}
		TSharedPtr<FJsonObject> Res = MakeShareable(new FJsonObject());
		Res->SetBoolField(TEXT("success"), true);
		Res->SetStringField(TEXT("note"), FString::Printf(TEXT("Layer '%s' already exists"), *LayerName));
		Res->SetStringField(TEXT("layer_name"), LayerName);
		Res->SetStringField(TEXT("layer_info_path"), ExistingLayerInfoObj->GetPathName());
		Res->SetArrayField(TEXT("known_layers"), KnownLayers);
		TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJsonString);
		FJsonSerializer::Serialize(Res.ToSharedRef(), W);
		return;
	}

	FString CleanSavePath = SavePath.IsEmpty() ? TEXT("/Game/Landscape") : SavePath;
	while (CleanSavePath.EndsWith(TEXT("/"))) CleanSavePath = CleanSavePath.LeftChop(1);

	const FString AssetName = FString::Printf(TEXT("LI_%s"), *LayerName);
	const FString PackagePath = CleanSavePath + TEXT("/") + AssetName;

	if (FPackageName::DoesPackageExist(PackagePath))
	{
		OutError = FString::Printf(TEXT("A LayerInfo asset already exists at '%s' but the landscape doesn't reference it — pass a different save_path or remove the stale asset"), *PackagePath);
		return;
	}

	UPackage* Pkg = CreatePackage(*PackagePath);
	ULandscapeLayerInfoObject* NewLayerInfo = NewObject<ULandscapeLayerInfoObject>(Pkg, FName(*AssetName), RF_Public | RF_Standalone | RF_Transactional);
	if (!NewLayerInfo) { OutError = TEXT("Failed to create LandscapeLayerInfoObject"); return; }
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7)
	NewLayerInfo->SetLayerName(LayerFName, false);
#else
	NewLayerInfo->LayerName = LayerFName;
#endif
	FAssetRegistryModule::AssetCreated(NewLayerInfo);
	NewLayerInfo->MarkPackageDirty();

	if (bHasTargetLayer)
	{
		// Target layer exists (declared by the material) but had a null LayerInfoObj — attach ours.
		Landscape->UpdateTargetLayer(LayerFName, FLandscapeTargetLayerSettings(NewLayerInfo));
	}
	else
	{
		Landscape->AddTargetLayer(LayerFName, FLandscapeTargetLayerSettings(NewLayerInfo));
	}

	// Info->Layers is derived from the landscape actor's TargetLayers map; refresh it now so
	// GetLayerName()/painting tools see the new layer immediately instead of stale "None" entries.
	Info->UpdateLayerInfoMap(Landscape);

	Landscape->MarkPackageDirty();

	const bool bSaved = UEditorAssetLibrary::SaveAsset(PackagePath, false);

	TArray<TSharedPtr<FJsonValue>> KnownLayers;
	for (const auto& TargetLayer : Landscape->GetTargetLayers())
	{
		KnownLayers.Add(MakeShareable(new FJsonValueString(TargetLayer.Key.ToString())));
	}

	TSharedPtr<FJsonObject> Res = MakeShareable(new FJsonObject());
	Res->SetBoolField(TEXT("success"), true);
	Res->SetStringField(TEXT("layer_name"), LayerName);
	Res->SetStringField(TEXT("layer_info_path"), PackagePath);
	Res->SetBoolField(TEXT("saved"), bSaved);
	Res->SetArrayField(TEXT("known_layers"), KnownLayers);
	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Res.ToSharedRef(), W);
}

void HandlePopulateLandscape(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World) { OutError = TEXT("No editor world available"); return; }

	FString LandscapeLabel;
	Args->TryGetStringField(TEXT("landscape_label"), LandscapeLabel);

	ALandscapeProxy* Landscape = nullptr;
	for (TActorIterator<ALandscapeProxy> It(World); It; ++It)
	{
		if (LandscapeLabel.IsEmpty() || It->GetActorLabel().Equals(LandscapeLabel, ESearchCase::IgnoreCase))
		{
			Landscape = *It;
			break;
		}
	}
	if (!Landscape) { OutError = TEXT("No landscape found in the level. Create one first with create_landscape."); return; }

	const TArray<TSharedPtr<FJsonValue>>* MeshArray = nullptr;
	if (!Args->TryGetArrayField(TEXT("mesh_paths"), MeshArray) || !MeshArray || MeshArray->Num() == 0)
	{
		OutError = TEXT("mesh_paths array is required (list of StaticMesh asset paths)");
		return;
	}

	TArray<FString> MeshPaths;
	for (const auto& V : *MeshArray)
	{
		FString Path;
		V->TryGetString(Path);
		if (!Path.IsEmpty()) MeshPaths.Add(Path);
	}
	if (MeshPaths.Num() == 0) { OutError = TEXT("No valid mesh paths provided"); return; }

	FString LayerName = TEXT("Scatter");
	Args->TryGetStringField(TEXT("layer_name"), LayerName);
	if (LayerName.IsEmpty()) LayerName = TEXT("Scatter");

	double Density = 0.005;
	Args->TryGetNumberField(TEXT("density"), Density);

	double MinScale = 0.8, MaxScale = 1.2;
	Args->TryGetNumberField(TEXT("min_scale"), MinScale);
	Args->TryGetNumberField(TEXT("max_scale"), MaxScale);

	double CullDistance = 0.0;
	Args->TryGetNumberField(TEXT("cull_distance"), CullDistance);

	// Slope filter (NormalToDensity + DensityFilter). <= 0 disables the pair.
	double MaxSlopeDegrees = 35.0;
	Args->TryGetNumberField(TEXT("max_slope_degrees"), MaxSlopeDegrees);

	// Minimum spacing between points (SelfPruning on enlarged point extents). 0 = off.
	double MinSpacing = 0.0;
	Args->TryGetNumberField(TEXT("min_spacing"), MinSpacing);

	FVector LandOrigin, LandExtent;
	Landscape->GetActorBounds(false, LandOrigin, LandExtent);

	if (CullDistance <= 0.0)
	{
		if (Density >= 0.05)       CullDistance = 3000.0;
		else if (Density >= 0.01)  CullDistance = 8000.0;
		else                       CullDistance = 15000.0;
	}

	UE_LOG(LogTemp, Log, TEXT("PopulateLandscape: '%s' with %d meshes, density=%.4f, landscape at (%.0f,%.0f,%.0f) extent=(%.0f,%.0f,%.0f)"),
		*LayerName, MeshPaths.Num(), Density, LandOrigin.X, LandOrigin.Y, LandOrigin.Z, LandExtent.X, LandExtent.Y, LandExtent.Z);

	FString GraphName = TEXT("PCG_") + LayerName;
	FString SavePath = TEXT("/Game/GeneratedPCG");

	if (!UEditorAssetLibrary::DoesDirectoryExist(SavePath))
		UEditorAssetLibrary::MakeDirectory(SavePath);

	FString FullGraphPath = SavePath / GraphName;

	if (UEditorAssetLibrary::DoesAssetExist(FullGraphPath))
		UEditorAssetLibrary::DeleteAsset(FullGraphPath);

	UPackage* GraphPackage = CreatePackage(*FullGraphPath);
	UPCGGraph* Graph = NewObject<UPCGGraph>(GraphPackage, FName(*GraphName), RF_Public | RF_Standalone);
	if (!Graph) { OutError = TEXT("Failed to create PCG graph"); return; }

	FAssetRegistryModule& ARModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	ARModule.AssetCreated(Graph);
	GraphPackage->MarkPackageDirty();

	UClass* GetLandscapeClass = nullptr;
	UClass* SurfaceSamplerClass = nullptr;
	UClass* TransformPointsClass = nullptr;
	UClass* MeshSpawnerClass = nullptr;
	UClass* NormalToDensityClass = nullptr;
	UClass* DensityFilterClass = nullptr;
	UClass* SelfPruningClass = nullptr;

	for (TObjectIterator<UClass> It; It; ++It)
	{
		FString Name = It->GetName();
		if (Name == TEXT("PCGGetLandscapeSettings")) GetLandscapeClass = *It;
		else if (Name == TEXT("PCGSurfaceSamplerSettings")) SurfaceSamplerClass = *It;
		else if (Name == TEXT("PCGTransformPointsSettings")) TransformPointsClass = *It;
		else if (Name == TEXT("PCGStaticMeshSpawnerSettings")) MeshSpawnerClass = *It;
		else if (Name == TEXT("PCGNormalToDensitySettings")) NormalToDensityClass = *It;
		else if (Name == TEXT("PCGDensityFilterSettings")) DensityFilterClass = *It;
		else if (Name == TEXT("PCGSelfPruningSettings")) SelfPruningClass = *It;
	}

	if (!GetLandscapeClass || !SurfaceSamplerClass || !MeshSpawnerClass)
	{
		OutError = TEXT("Could not find required PCG node classes");
		return;
	}

	UPCGSettings* LandscapeSettings = nullptr;
	UPCGNode* LandscapeNode = Graph->AddNodeOfType(GetLandscapeClass, LandscapeSettings);

	UPCGSettings* SamplerSettings = nullptr;
	UPCGNode* SamplerNode = Graph->AddNodeOfType(SurfaceSamplerClass, SamplerSettings);

	// Reflection helpers shared by the optional nodes below (mirrors PCGTools' SetVec/SetRot/SetBool approach).
	auto SetVecProp = [](UObject* Obj, const TCHAR* PropName, const FVector& V) -> bool
	{
		if (!Obj) return false;
		FStructProperty* Prop = FindFProperty<FStructProperty>(Obj->GetClass(), PropName);
		if (Prop && Prop->Struct == TBaseStructure<FVector>::Get())
		{
			*Prop->ContainerPtrToValuePtr<FVector>(Obj) = V;
			return true;
		}
		return false;
	};
	auto SetRotProp = [](UObject* Obj, const TCHAR* PropName, const FRotator& R) -> bool
	{
		if (!Obj) return false;
		FStructProperty* Prop = FindFProperty<FStructProperty>(Obj->GetClass(), PropName);
		if (Prop && Prop->Struct == TBaseStructure<FRotator>::Get())
		{
			*Prop->ContainerPtrToValuePtr<FRotator>(Obj) = R;
			return true;
		}
		return false;
	};
	auto SetBoolProp = [](UObject* Obj, const TCHAR* PropName, bool bVal) -> bool
	{
		if (!Obj) return false;
		FBoolProperty* Prop = FindFProperty<FBoolProperty>(Obj->GetClass(), PropName);
		if (!Prop) return false;
		Prop->SetPropertyValue(Prop->ContainerPtrToValuePtr<void>(Obj), bVal);
		return true;
	};
	auto SetFloatProp = [](UObject* Obj, const TCHAR* PropName, float Val) -> bool
	{
		if (!Obj) return false;
		if (FFloatProperty* FProp = FindFProperty<FFloatProperty>(Obj->GetClass(), PropName))
		{
			FProp->SetPropertyValue_InContainer(Obj, Val);
			return true;
		}
		if (FDoubleProperty* DProp = FindFProperty<FDoubleProperty>(Obj->GetClass(), PropName))
		{
			DProp->SetPropertyValue_InContainer(Obj, (double)Val);
			return true;
		}
		return false;
	};

	// Optional slope-filter pair: Sampler -> NormalToDensity -> DensityFilter.
	// NormalToDensity (Set mode, Normal=Up) writes cos(slope) into density; the filter keeps [cos(max_slope), 1].
	const bool bWantSlopeFilter = MaxSlopeDegrees > 0.0 && MaxSlopeDegrees < 90.0 && NormalToDensityClass && DensityFilterClass;
	UPCGNode* NormalToDensityNode = nullptr;
	UPCGNode* DensityFilterNode = nullptr;
	if (bWantSlopeFilter)
	{
		UPCGSettings* NDSettings = nullptr;
		NormalToDensityNode = Graph->AddNodeOfType(NormalToDensityClass, NDSettings);
		if (NDSettings)
		{
			SetVecProp(NDSettings, TEXT("Normal"), FVector::UpVector);
			SetFloatProp(NDSettings, TEXT("Offset"), 0.f);
			SetFloatProp(NDSettings, TEXT("Strength"), 1.f);
		}

		UPCGSettings* DFSettings = nullptr;
		DensityFilterNode = Graph->AddNodeOfType(DensityFilterClass, DFSettings);
		if (DFSettings)
		{
			const float SlopeThreshold = FMath::Cos(FMath::DegreesToRadians((float)MaxSlopeDegrees));
			SetFloatProp(DFSettings, TEXT("LowerBound"), SlopeThreshold);
			SetFloatProp(DFSettings, TEXT("UpperBound"), 1.f);
			SetBoolProp(DFSettings, TEXT("bInvertFilter"), false);
		}
	}

	// Optional minimum spacing: enlarge sampler point extents to min_spacing/2 and let SelfPruning
	// (LargeToSmall, randomized - class defaults) discard overlapping points.
	const bool bWantMinSpacing = MinSpacing > 0.0 && SelfPruningClass;
	UPCGNode* SelfPruningNode = nullptr;
	if (bWantMinSpacing)
	{
		UPCGSettings* PruneSettings = nullptr;
		SelfPruningNode = Graph->AddNodeOfType(SelfPruningClass, PruneSettings);
		if (SamplerSettings)
		{
			const double HalfSpacing = MinSpacing * 0.5;
			SetVecProp(SamplerSettings, TEXT("PointExtents"), FVector(HalfSpacing, HalfSpacing, HalfSpacing));
		}
	}

	UPCGNode* TransformNode = nullptr;
	bool bTransformConfigured = false;
	if (TransformPointsClass)
	{
		UPCGSettings* TransformSettings = nullptr;
		TransformNode = Graph->AddNodeOfType(TransformPointsClass, TransformSettings);
		if (TransformSettings)
		{
			// Yaw-only random rotation in absolute space keeps instances upright on slopes;
			// uniform scale from min_scale/max_scale (X=Y=Z).
			const FVector ScaleMinVec((float)MinScale, (float)MinScale, (float)MinScale);
			const FVector ScaleMaxVec((float)MaxScale, (float)MaxScale, (float)MaxScale);
			bTransformConfigured =
				SetBoolProp(TransformSettings, TEXT("bAbsoluteRotation"), true) &&
				SetRotProp(TransformSettings, TEXT("RotationMin"), FRotator(0.f, 0.f, 0.f)) &&
				SetRotProp(TransformSettings, TEXT("RotationMax"), FRotator(0.f, 360.f, 0.f)) &&
				SetVecProp(TransformSettings, TEXT("ScaleMin"), ScaleMinVec) &&
				SetVecProp(TransformSettings, TEXT("ScaleMax"), ScaleMaxVec) &&
				SetBoolProp(TransformSettings, TEXT("bUniformScale"), true);
			if (!bTransformConfigured)
			{
				UE_LOG(LogTemp, Warning, TEXT("PopulateLandscape: some TransformPoints properties were not found by reflection; node left partially configured"));
			}
		}
	}

	UPCGSettings* SpawnerSettings = nullptr;
	UPCGNode* SpawnerNode = Graph->AddNodeOfType(MeshSpawnerClass, SpawnerSettings);

	if (!LandscapeNode || !SamplerNode || !SpawnerNode)
	{
		OutError = TEXT("Failed to add PCG nodes to graph");
		return;
	}

#if WITH_EDITOR
	{
		int32 PosX = 100;
		LandscapeNode->SetNodePosition(PosX, 0); PosX += 300;
		SamplerNode->SetNodePosition(PosX, 0); PosX += 300;
		if (NormalToDensityNode) { NormalToDensityNode->SetNodePosition(PosX, 0); PosX += 300; }
		if (DensityFilterNode)   { DensityFilterNode->SetNodePosition(PosX, 0);   PosX += 300; }
		if (SelfPruningNode)     { SelfPruningNode->SetNodePosition(PosX, 0);     PosX += 300; }
		if (TransformNode)       { TransformNode->SetNodePosition(PosX, 0);       PosX += 300; }
		SpawnerNode->SetNodePosition(PosX, 0);
	}
#endif

	UPCGNode* OutputNode = Graph->GetOutputNode();

	auto ConnectNodes = [](UPCGNode* From, const FName& FromPin, UPCGNode* To, const FName& ToPin) -> bool
	{
		if (!From || !To) return false;
		UPCGPin* OutPin = From->GetOutputPin(FromPin);
		UPCGPin* InPin = To->GetInputPin(ToPin);
		if (!OutPin || !InPin) return false;
		return OutPin->AddEdgeTo(InPin) != 0;
	};

	bool bEdge1 = ConnectNodes(LandscapeNode, FName(TEXT("Out")), SamplerNode, FName(TEXT("Surface")));

	// Chain the point pipeline in order, skipping whichever optional nodes were not created:
	// Sampler -> [NormalToDensity -> DensityFilter] -> [SelfPruning] -> [Transform] -> Spawner
	TArray<UPCGNode*> PointChain;
	PointChain.Add(SamplerNode);
	if (NormalToDensityNode) PointChain.Add(NormalToDensityNode);
	if (DensityFilterNode)   PointChain.Add(DensityFilterNode);
	if (SelfPruningNode)     PointChain.Add(SelfPruningNode);
	if (TransformNode)       PointChain.Add(TransformNode);
	PointChain.Add(SpawnerNode);

	bool bChainOk = true;
	for (int32 ChainIdx = 0; ChainIdx + 1 < PointChain.Num(); ++ChainIdx)
	{
		const bool bLinked = ConnectNodes(PointChain[ChainIdx], FName(TEXT("Out")), PointChain[ChainIdx + 1], FName(TEXT("In")));
		if (!bLinked)
		{
			bChainOk = false;
			UE_LOG(LogTemp, Warning, TEXT("PopulateLandscape: failed to connect %s -> %s"),
				*PointChain[ChainIdx]->GetName(), *PointChain[ChainIdx + 1]->GetName());
		}
	}

	UE_LOG(LogTemp, Log, TEXT("PopulateLandscape: Edges wired - Landscape->Sampler=%s, point chain (%d nodes)=%s"),
		bEdge1 ? TEXT("OK") : TEXT("FAIL"), PointChain.Num(), bChainOk ? TEXT("OK") : TEXT("PARTIAL"));

	if (SamplerSettings)
	{
		FProperty* DensityProp = SamplerSettings->GetClass()->FindPropertyByName(TEXT("PointsPerSquaredMeter"));
		if (DensityProp)
		{
			float DensityFloat = static_cast<float>(Density);
			DensityProp->SetValue_InContainer(SamplerSettings, &DensityFloat);
		}
	}

	Graph->MarkPackageDirty();

	{
		int32 SpawnerIdx = -1;
		TArray<UPCGNode*> AllNodes = Graph->GetNodes();
		for (int32 i = 0; i < AllNodes.Num(); i++)
		{
			if (AllNodes[i] == SpawnerNode) { SpawnerIdx = i + 2; break; }
		}

		if (SpawnerIdx >= 0)
		{
			TArray<float> Weights;
			FString MeshJson, MeshErr;
			PCGTools::HandleSetPCGMeshSpawner(FullGraphPath, SpawnerIdx, MeshPaths, Weights, MeshJson, MeshErr);
			if (!MeshErr.IsEmpty())
			{
				UE_LOG(LogTemp, Warning, TEXT("PopulateLandscape: Mesh spawner setup: %s"), *MeshErr);
			}
			else
			{
				UE_LOG(LogTemp, Log, TEXT("PopulateLandscape: Configured %d meshes on spawner"), MeshPaths.Num());
			}
		}
	}

	FString ActorLabel = TEXT("PCG_") + LayerName + TEXT("_Actor");

	const FScopedTransaction Transaction(NSLOCTEXT("AxivorAI", "PopulateLandscape", "Populate Landscape"));

	FActorSpawnParameters SpawnParams;

	AActor* PCGActor = World->SpawnActor<AActor>(AActor::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
	if (!PCGActor) { OutError = TEXT("Failed to spawn PCG actor"); return; }
	PCGActor->Modify();
	PCGActor->SetActorLabel(ActorLabel);

	UBoxComponent* BoxComp = NewObject<UBoxComponent>(PCGActor, TEXT("PCGVolume"));
	PCGActor->SetRootComponent(BoxComp);
	BoxComp->RegisterComponent();
	BoxComp->SetBoxExtent(FVector(LandExtent.X, LandExtent.Y, LandExtent.Z * 2.f));
	BoxComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	PCGActor->SetActorLocation(LandOrigin);

	UPCGComponent* PCGComp = NewObject<UPCGComponent>(PCGActor, TEXT("PCGComponent"));
	PCGActor->AddInstanceComponent(PCGComp);
	PCGComp->RegisterComponent();
	PCGComp->SetGraph(Graph);
	PCGComp->Activate(true);
	PCGComp->bActivated = true;

	PCGComp->GenerateLocal(true);

	int32 InstanceComponentCount = 0;
	float CullFloat = static_cast<float>(CullDistance);

	PCGComp->ForEachManagedResource([&](UPCGManagedResource* Resource)
	{
		UPCGManagedComponent* ManagedComp = Cast<UPCGManagedComponent>(Resource);
		if (!ManagedComp) return;

		UActorComponent* ActorComp = ManagedComp->GeneratedComponent.Get();
		if (UInstancedStaticMeshComponent* ISMComp = Cast<UInstancedStaticMeshComponent>(ActorComp))
		{
			ISMComp->SetCullDistances(0.0f, CullFloat);
			InstanceComponentCount++;
		}
	});

	UE_LOG(LogTemp, Log, TEXT("PopulateLandscape: Generated PCG layer '%s' with %d meshes, density=%.4f, cull_distance=%.0f (%d ISM components)"),
		*LayerName, MeshPaths.Num(), Density, CullDistance, InstanceComponentCount);

	World->MarkPackageDirty();
	if (GEditor) GEditor->RedrawAllViewports();

	TSharedPtr<FJsonObject> Out = MakeShareable(new FJsonObject);
	Out->SetBoolField(TEXT("success"), true);
	Out->SetStringField(TEXT("layer_name"), LayerName);
	Out->SetStringField(TEXT("actor_label"), ActorLabel);
	Out->SetStringField(TEXT("graph_path"), FullGraphPath);
	Out->SetNumberField(TEXT("mesh_count"), MeshPaths.Num());
	Out->SetNumberField(TEXT("density"), Density);
	Out->SetNumberField(TEXT("cull_distance"), CullDistance);
	Out->SetNumberField(TEXT("max_slope_degrees"), MaxSlopeDegrees);
	Out->SetNumberField(TEXT("min_spacing"), MinSpacing);
	Out->SetBoolField(TEXT("transform_configured"), bTransformConfigured);
	{
		TArray<TSharedPtr<FJsonValue>> OptionalNodes;
		if (NormalToDensityNode) OptionalNodes.Add(MakeShareable(new FJsonValueString(TEXT("NormalToDensity"))));
		if (DensityFilterNode)   OptionalNodes.Add(MakeShareable(new FJsonValueString(TEXT("DensityFilter"))));
		if (SelfPruningNode)     OptionalNodes.Add(MakeShareable(new FJsonValueString(TEXT("SelfPruning"))));
		if (TransformNode)       OptionalNodes.Add(MakeShareable(new FJsonValueString(TEXT("TransformPoints"))));
		Out->SetArrayField(TEXT("optional_nodes"), OptionalNodes);
	}
	Out->SetStringField(TEXT("message"), FString::Printf(
		TEXT("PCG scatter layer '%s' created with %d meshes at density %.4f, cull_distance=%.0f, max_slope=%.0f deg, min_spacing=%.0f cm. Actor '%s' positioned at landscape center."),
		*LayerName, MeshPaths.Num(), Density, CullDistance, MaxSlopeDegrees, MinSpacing, *ActorLabel));

	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Out.ToSharedRef(), Writer);
}

void HandleSetLandscapeMaterialFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString ActorLabel, MaterialPath;
	Args->TryGetStringField(TEXT("actor_label"), ActorLabel);
	Args->TryGetStringField(TEXT("material_path"), MaterialPath);
	if (ActorLabel.IsEmpty()) { OutError = TEXT("actor_label is required"); return; }
	if (MaterialPath.IsEmpty()) { OutError = TEXT("material_path is required"); return; }
	HandleSetLandscapeMaterial(ActorLabel, MaterialPath, OutJsonString, OutError);
}

void HandleGetLandscapeInfoFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString ActorLabel;
	Args->TryGetStringField(TEXT("actor_label"), ActorLabel);
	HandleGetLandscapeInfo(ActorLabel, OutJsonString, OutError);
}

void HandleSetLandscapePropertiesFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString ActorLabel, PropertyName, PropertyValue;
	Args->TryGetStringField(TEXT("actor_label"), ActorLabel);
	Args->TryGetStringField(TEXT("property_name"), PropertyName);
	Args->TryGetStringField(TEXT("property_value"), PropertyValue);
	HandleSetLandscapeProperties(ActorLabel, PropertyName, PropertyValue, OutJsonString, OutError);
}

void HandleSetLandscapeLODFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString ActorLabel;
	int32 StaticLightingLOD = 0, LODBias = -1;
	double LODDistanceFactor = 0;
	Args->TryGetStringField(TEXT("actor_label"), ActorLabel);
	Args->TryGetNumberField(TEXT("static_lighting_lod"), StaticLightingLOD);
	Args->TryGetNumberField(TEXT("lod_bias"), LODBias);
	Args->TryGetNumberField(TEXT("lod_distance_factor"), LODDistanceFactor);
	HandleSetLandscapeLOD(ActorLabel, StaticLightingLOD, LODBias, (float)LODDistanceFactor, OutJsonString, OutError);
}

void HandleExportLandscapeHeightmapFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString ActorLabel, FilePath;
	Args->TryGetStringField(TEXT("actor_label"), ActorLabel);
	Args->TryGetStringField(TEXT("file_path"), FilePath);
	HandleExportLandscapeHeightmap(ActorLabel, FilePath, OutJsonString, OutError);
}

void HandleAddLandscapeLayerInfoFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString ActorLabel, LayerName, SavePath;
	Args->TryGetStringField(TEXT("actor_label"), ActorLabel);
	Args->TryGetStringField(TEXT("layer_name"), LayerName);
	Args->TryGetStringField(TEXT("save_path"), SavePath);
	HandleAddLandscapeLayerInfo(ActorLabel, LayerName, SavePath, OutJsonString, OutError);
}

}
