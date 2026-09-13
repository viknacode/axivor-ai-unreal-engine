// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"

enum class EThemePreset : uint8
{
	GoldenTurquoise,
	SlateProfessional,
	RoseGold,
	Emerald,
	Cyberpunk,
	SolarFlare,
	DeepOcean,
	LavenderDream,
	Custom
};

struct FBpGeneratorTheme
{
	FLinearColor PrimaryColor;
	FLinearColor SecondaryColor;
	FLinearColor TertiaryColor;

	FLinearColor MainBackgroundColor;
	FLinearColor PanelBackgroundColor;
	FLinearColor BorderBackgroundColor;

	FLinearColor PrimaryTextColor;
	FLinearColor SecondaryTextColor;
	FLinearColor HintTextColor;

	FLinearColor AnalystColor;
	FLinearColor ScannerColor;
	FLinearColor ArchitectColor;

	FLinearColor GradientTop;
	FLinearColor GradientBottom;
	bool bUseGradients;

	float BorderOpacity;
	float CornerRadius;

	EThemePreset Preset = EThemePreset::GoldenTurquoise;

	static UECPCORE_API FBpGeneratorTheme GetPreset(EThemePreset Preset);
};

class FThemeManager
{
public:
	static UECPCORE_API FThemeManager& Get();

	UECPCORE_API FBpGeneratorTheme LoadTheme();

	UECPCORE_API void SaveTheme(const FBpGeneratorTheme& Theme);

	static UECPCORE_API FBpGeneratorTheme GetGoldenTurquoiseTheme();
	static UECPCORE_API FBpGeneratorTheme GetSlateProfessionalTheme();
	static UECPCORE_API FBpGeneratorTheme GetRoseGoldTheme();
	static UECPCORE_API FBpGeneratorTheme GetEmeraldTheme();
	static UECPCORE_API FBpGeneratorTheme GetCyberpunkTheme();
	static UECPCORE_API FBpGeneratorTheme GetSolarFlareTheme();
	static UECPCORE_API FBpGeneratorTheme GetDeepOceanTheme();
	static UECPCORE_API FBpGeneratorTheme GetLavenderDreamTheme();

private:
	FThemeManager() = default;
	FThemeManager(const FThemeManager&) = delete;
	FThemeManager& operator=(const FThemeManager&) = delete;
};
