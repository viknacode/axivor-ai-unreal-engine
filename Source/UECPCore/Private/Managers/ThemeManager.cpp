// Copyright 2026, BlueprintsLab, All rights reserved

#include "Managers/ThemeManager.h"
#include "Managers/SettingsManager.h"
#include "Misc/ConfigCacheIni.h"

FThemeManager& FThemeManager::Get()
{
	static FThemeManager Instance;
	return Instance;
}

FBpGeneratorTheme FThemeManager::LoadTheme()
{
	int32 PresetInt = 0;
	GConfig->GetInt(TEXT("BpGeneratorUltimate"), TEXT("ThemePreset"), PresetInt, FSettingsManager::GetGlobalConfigPath());
	EThemePreset Preset = static_cast<EThemePreset>(PresetInt);

	FString ColorStr;
	if (GConfig->GetString(TEXT("BpGeneratorUltimate"), TEXT("CustomThemeColors"), ColorStr, FSettingsManager::GetGlobalConfigPath()))
	{
		TArray<FString> ColorValues;
		ColorStr.ParseIntoArray(ColorValues, TEXT("|"));

		if (ColorValues.Num() >= 15)
		{
			auto ParseColor = [](const FString& InColorStr) -> FLinearColor
			{
				FLinearColor Color = FLinearColor::White;
				Color.InitFromString(InColorStr);
				return Color;
			};

			FBpGeneratorTheme Theme;
			Theme.Preset = EThemePreset::Custom;
			Theme.PrimaryColor = ParseColor(ColorValues[0]);
			Theme.SecondaryColor = ParseColor(ColorValues[1]);
			Theme.TertiaryColor = ParseColor(ColorValues[2]);
			Theme.MainBackgroundColor = ParseColor(ColorValues[3]);
			Theme.PanelBackgroundColor = ParseColor(ColorValues[4]);
			Theme.BorderBackgroundColor = ParseColor(ColorValues[5]);
			Theme.PrimaryTextColor = ParseColor(ColorValues[6]);
			Theme.SecondaryTextColor = ParseColor(ColorValues[7]);
			Theme.HintTextColor = ParseColor(ColorValues[8]);
			Theme.AnalystColor = ParseColor(ColorValues[9]);
			Theme.ScannerColor = ParseColor(ColorValues[10]);
			Theme.ArchitectColor = ParseColor(ColorValues[11]);
			Theme.GradientTop = ParseColor(ColorValues[12]);
			Theme.GradientBottom = ParseColor(ColorValues[13]);
			Theme.bUseGradients = FCString::ToBool(*ColorValues[14]);
			return Theme;
		}
	}

	return FBpGeneratorTheme::GetPreset(Preset);
}

void FThemeManager::SaveTheme(const FBpGeneratorTheme& Theme)
{
	GConfig->SetInt(TEXT("BpGeneratorUltimate"), TEXT("ThemePreset"), static_cast<int32>(Theme.Preset), FSettingsManager::GetGlobalConfigPath());

	if (Theme.Preset == EThemePreset::Custom)
	{
		FString ColorStr = FString::Printf(TEXT("%s|%s|%s|%s|%s|%s|%s|%s|%s|%s|%s|%s|%s|%s|%s"),
			*Theme.PrimaryColor.ToString(),
			*Theme.SecondaryColor.ToString(),
			*Theme.TertiaryColor.ToString(),
			*Theme.MainBackgroundColor.ToString(),
			*Theme.PanelBackgroundColor.ToString(),
			*Theme.BorderBackgroundColor.ToString(),
			*Theme.PrimaryTextColor.ToString(),
			*Theme.SecondaryTextColor.ToString(),
			*Theme.HintTextColor.ToString(),
			*Theme.AnalystColor.ToString(),
			*Theme.ScannerColor.ToString(),
			*Theme.ArchitectColor.ToString(),
			*Theme.GradientTop.ToString(),
			*Theme.GradientBottom.ToString(),
			Theme.bUseGradients ? TEXT("true") : TEXT("false")
		);
		GConfig->SetString(TEXT("BpGeneratorUltimate"), TEXT("CustomThemeColors"), *ColorStr, FSettingsManager::GetGlobalConfigPath());
	}

	GConfig->Flush(false, FSettingsManager::GetGlobalConfigPath());
}

FBpGeneratorTheme FBpGeneratorTheme::GetPreset(EThemePreset Preset)
{
	switch (Preset)
	{
	case EThemePreset::SlateProfessional: return FThemeManager::GetSlateProfessionalTheme();
	case EThemePreset::RoseGold: return FThemeManager::GetRoseGoldTheme();
	case EThemePreset::Emerald: return FThemeManager::GetEmeraldTheme();
	case EThemePreset::Cyberpunk: return FThemeManager::GetCyberpunkTheme();
	case EThemePreset::SolarFlare: return FThemeManager::GetSolarFlareTheme();
	case EThemePreset::DeepOcean: return FThemeManager::GetDeepOceanTheme();
	case EThemePreset::LavenderDream: return FThemeManager::GetLavenderDreamTheme();
	case EThemePreset::GoldenTurquoise:
	default: return FThemeManager::GetGoldenTurquoiseTheme();
	}
}

FBpGeneratorTheme FThemeManager::GetGoldenTurquoiseTheme()
{
	FBpGeneratorTheme Theme;
	Theme.Preset = EThemePreset::GoldenTurquoise;

	Theme.PrimaryColor = FLinearColor(1.0f, 0.84f, 0.0f);
	Theme.SecondaryColor = FLinearColor(0.25f, 0.88f, 0.82f);
	Theme.TertiaryColor = FLinearColor(0.13f, 0.55f, 0.41f);

	Theme.MainBackgroundColor = FLinearColor(0.08f, 0.07f, 0.05f);
	Theme.PanelBackgroundColor = FLinearColor(0.12f, 0.10f, 0.08f);
	Theme.BorderBackgroundColor = FLinearColor(0.2f, 0.17f, 0.12f, 0.8f);

	Theme.PrimaryTextColor = FLinearColor(0.98f, 0.95f, 0.90f);
	Theme.SecondaryTextColor = FLinearColor(0.70f, 0.67f, 0.62f);
	Theme.HintTextColor = FLinearColor(0.50f, 0.47f, 0.42f);

	Theme.AnalystColor = FLinearColor(1.0f, 0.6f, 0.0f);
	Theme.ScannerColor = FLinearColor(0.0f, 0.5f, 1.0f);
	Theme.ArchitectColor = FLinearColor(0.0f, 0.7f, 0.3f);

	Theme.GradientTop = FLinearColor(0.15f, 0.12f, 0.08f);
	Theme.GradientBottom = FLinearColor(0.05f, 0.04f, 0.03f);
	Theme.bUseGradients = true;

	Theme.BorderOpacity = 0.8f;
	Theme.CornerRadius = 4.0f;

	return Theme;
}

FBpGeneratorTheme FThemeManager::GetSlateProfessionalTheme()
{
	FBpGeneratorTheme Theme;
	Theme.Preset = EThemePreset::SlateProfessional;

	Theme.PrimaryColor = FLinearColor(0.4f, 0.5f, 0.6f);
	Theme.SecondaryColor = FLinearColor(0.3f, 0.35f, 0.4f);
	Theme.TertiaryColor = FLinearColor(0.2f, 0.25f, 0.3f);

	Theme.MainBackgroundColor = FLinearColor(0.1f, 0.1f, 0.12f);
	Theme.PanelBackgroundColor = FLinearColor(0.15f, 0.15f, 0.17f);
	Theme.BorderBackgroundColor = FLinearColor(0.2f, 0.2f, 0.22f, 0.8f);

	Theme.PrimaryTextColor = FLinearColor(0.95f, 0.95f, 0.97f);
	Theme.SecondaryTextColor = FLinearColor(0.65f, 0.65f, 0.7f);
	Theme.HintTextColor = FLinearColor(0.45f, 0.45f, 0.5f);

	Theme.AnalystColor = FLinearColor(0.4f, 0.5f, 0.6f);
	Theme.ScannerColor = FLinearColor(0.3f, 0.5f, 0.7f);
	Theme.ArchitectColor = FLinearColor(0.3f, 0.6f, 0.4f);

	Theme.GradientTop = FLinearColor(0.18f, 0.18f, 0.2f);
	Theme.GradientBottom = FLinearColor(0.08f, 0.08f, 0.1f);
	Theme.bUseGradients = true;

	Theme.BorderOpacity = 0.7f;
	Theme.CornerRadius = 3.0f;

	return Theme;
}

FBpGeneratorTheme FThemeManager::GetRoseGoldTheme()
{
	FBpGeneratorTheme Theme;
	Theme.Preset = EThemePreset::RoseGold;

	Theme.PrimaryColor = FLinearColor(1.0f, 0.7f, 0.75f);
	Theme.SecondaryColor = FLinearColor(0.8f, 0.5f, 0.55f);
	Theme.TertiaryColor = FLinearColor(0.6f, 0.3f, 0.35f);

	Theme.MainBackgroundColor = FLinearColor(0.08f, 0.05f, 0.06f);
	Theme.PanelBackgroundColor = FLinearColor(0.12f, 0.08f, 0.1f);
	Theme.BorderBackgroundColor = FLinearColor(0.2f, 0.15f, 0.16f, 0.8f);

	Theme.PrimaryTextColor = FLinearColor(0.98f, 0.94f, 0.95f);
	Theme.SecondaryTextColor = FLinearColor(0.7f, 0.66f, 0.67f);
	Theme.HintTextColor = FLinearColor(0.5f, 0.46f, 0.47f);

	Theme.AnalystColor = FLinearColor(1.0f, 0.7f, 0.75f);
	Theme.ScannerColor = FLinearColor(0.7f, 0.5f, 0.8f);
	Theme.ArchitectColor = FLinearColor(0.8f, 0.6f, 0.5f);

	Theme.GradientTop = FLinearColor(0.15f, 0.1f, 0.12f);
	Theme.GradientBottom = FLinearColor(0.05f, 0.03f, 0.04f);
	Theme.bUseGradients = true;

	Theme.BorderOpacity = 0.8f;
	Theme.CornerRadius = 5.0f;

	return Theme;
}

FBpGeneratorTheme FThemeManager::GetEmeraldTheme()
{
	FBpGeneratorTheme Theme;
	Theme.Preset = EThemePreset::Emerald;

	Theme.PrimaryColor = FLinearColor(0.2f, 0.8f, 0.5f);
	Theme.SecondaryColor = FLinearColor(0.15f, 0.6f, 0.4f);
	Theme.TertiaryColor = FLinearColor(0.1f, 0.4f, 0.3f);

	Theme.MainBackgroundColor = FLinearColor(0.05f, 0.08f, 0.06f);
	Theme.PanelBackgroundColor = FLinearColor(0.08f, 0.12f, 0.1f);
	Theme.BorderBackgroundColor = FLinearColor(0.12f, 0.18f, 0.14f, 0.8f);

	Theme.PrimaryTextColor = FLinearColor(0.92f, 0.97f, 0.94f);
	Theme.SecondaryTextColor = FLinearColor(0.62f, 0.72f, 0.67f);
	Theme.HintTextColor = FLinearColor(0.42f, 0.52f, 0.47f);

	Theme.AnalystColor = FLinearColor(0.2f, 0.8f, 0.5f);
	Theme.ScannerColor = FLinearColor(0.3f, 0.7f, 0.6f);
	Theme.ArchitectColor = FLinearColor(0.5f, 0.7f, 0.3f);

	Theme.GradientTop = FLinearColor(0.1f, 0.15f, 0.12f);
	Theme.GradientBottom = FLinearColor(0.04f, 0.06f, 0.05f);
	Theme.bUseGradients = true;

	Theme.BorderOpacity = 0.75f;
	Theme.CornerRadius = 4.0f;

	return Theme;
}

FBpGeneratorTheme FThemeManager::GetCyberpunkTheme()
{
	FBpGeneratorTheme Theme;
	Theme.Preset = EThemePreset::Cyberpunk;

	Theme.PrimaryColor = FLinearColor(1.0f, 0.0f, 0.8f);
	Theme.SecondaryColor = FLinearColor(0.0f, 1.0f, 1.0f);
	Theme.TertiaryColor = FLinearColor(0.6f, 0.0f, 1.0f);

	Theme.MainBackgroundColor = FLinearColor(0.05f, 0.02f, 0.08f);
	Theme.PanelBackgroundColor = FLinearColor(0.1f, 0.05f, 0.12f);
	Theme.BorderBackgroundColor = FLinearColor(0.2f, 0.1f, 0.3f, 0.8f);

	Theme.PrimaryTextColor = FLinearColor(0.95f, 0.95f, 1.0f);
	Theme.SecondaryTextColor = FLinearColor(0.6f, 0.6f, 0.7f);
	Theme.HintTextColor = FLinearColor(0.4f, 0.4f, 0.5f);

	Theme.AnalystColor = FLinearColor(1.0f, 0.0f, 0.8f);
	Theme.ScannerColor = FLinearColor(0.0f, 1.0f, 1.0f);
	Theme.ArchitectColor = FLinearColor(0.5f, 1.0f, 0.3f);

	Theme.GradientTop = FLinearColor(0.15f, 0.05f, 0.2f);
	Theme.GradientBottom = FLinearColor(0.03f, 0.01f, 0.06f);
	Theme.bUseGradients = true;

	Theme.BorderOpacity = 0.85f;
	Theme.CornerRadius = 3.0f;

	return Theme;
}

FBpGeneratorTheme FThemeManager::GetSolarFlareTheme()
{
	FBpGeneratorTheme Theme;
	Theme.Preset = EThemePreset::SolarFlare;

	Theme.PrimaryColor = FLinearColor(1.0f, 0.5f, 0.0f);
	Theme.SecondaryColor = FLinearColor(1.0f, 0.9f, 0.2f);
	Theme.TertiaryColor = FLinearColor(1.0f, 0.3f, 0.1f);

	Theme.MainBackgroundColor = FLinearColor(0.08f, 0.04f, 0.02f);
	Theme.PanelBackgroundColor = FLinearColor(0.12f, 0.06f, 0.03f);
	Theme.BorderBackgroundColor = FLinearColor(0.25f, 0.15f, 0.08f, 0.8f);

	Theme.PrimaryTextColor = FLinearColor(1.0f, 0.95f, 0.9f);
	Theme.SecondaryTextColor = FLinearColor(0.75f, 0.65f, 0.55f);
	Theme.HintTextColor = FLinearColor(0.55f, 0.45f, 0.35f);

	Theme.AnalystColor = FLinearColor(1.0f, 0.5f, 0.0f);
	Theme.ScannerColor = FLinearColor(1.0f, 0.7f, 0.1f);
	Theme.ArchitectColor = FLinearColor(1.0f, 0.3f, 0.5f);

	Theme.GradientTop = FLinearColor(0.2f, 0.1f, 0.05f);
	Theme.GradientBottom = FLinearColor(0.05f, 0.02f, 0.01f);
	Theme.bUseGradients = true;

	Theme.BorderOpacity = 0.8f;
	Theme.CornerRadius = 4.0f;

	return Theme;
}

FBpGeneratorTheme FThemeManager::GetDeepOceanTheme()
{
	FBpGeneratorTheme Theme;
	Theme.Preset = EThemePreset::DeepOcean;

	Theme.PrimaryColor = FLinearColor(0.0f, 0.6f, 1.0f);
	Theme.SecondaryColor = FLinearColor(0.0f, 0.4f, 0.7f);
	Theme.TertiaryColor = FLinearColor(0.1f, 0.3f, 0.5f);

	Theme.MainBackgroundColor = FLinearColor(0.02f, 0.04f, 0.08f);
	Theme.PanelBackgroundColor = FLinearColor(0.04f, 0.06f, 0.12f);
	Theme.BorderBackgroundColor = FLinearColor(0.1f, 0.15f, 0.25f, 0.8f);

	Theme.PrimaryTextColor = FLinearColor(0.9f, 0.95f, 1.0f);
	Theme.SecondaryTextColor = FLinearColor(0.55f, 0.65f, 0.8f);
	Theme.HintTextColor = FLinearColor(0.35f, 0.45f, 0.6f);

	Theme.AnalystColor = FLinearColor(0.0f, 0.7f, 0.9f);
	Theme.ScannerColor = FLinearColor(0.2f, 0.5f, 1.0f);
	Theme.ArchitectColor = FLinearColor(0.0f, 0.8f, 0.7f);

	Theme.GradientTop = FLinearColor(0.08f, 0.12f, 0.18f);
	Theme.GradientBottom = FLinearColor(0.02f, 0.03f, 0.06f);
	Theme.bUseGradients = true;

	Theme.BorderOpacity = 0.75f;
	Theme.CornerRadius = 4.0f;

	return Theme;
}

FBpGeneratorTheme FThemeManager::GetLavenderDreamTheme()
{
	FBpGeneratorTheme Theme;
	Theme.Preset = EThemePreset::LavenderDream;

	Theme.PrimaryColor = FLinearColor(0.7f, 0.5f, 1.0f);
	Theme.SecondaryColor = FLinearColor(0.9f, 0.7f, 1.0f);
	Theme.TertiaryColor = FLinearColor(0.5f, 0.3f, 0.8f);

	Theme.MainBackgroundColor = FLinearColor(0.06f, 0.04f, 0.1f);
	Theme.PanelBackgroundColor = FLinearColor(0.1f, 0.07f, 0.15f);
	Theme.BorderBackgroundColor = FLinearColor(0.18f, 0.12f, 0.25f, 0.8f);

	Theme.PrimaryTextColor = FLinearColor(0.98f, 0.95f, 1.0f);
	Theme.SecondaryTextColor = FLinearColor(0.7f, 0.65f, 0.8f);
	Theme.HintTextColor = FLinearColor(0.5f, 0.45f, 0.6f);

	Theme.AnalystColor = FLinearColor(0.8f, 0.6f, 1.0f);
	Theme.ScannerColor = FLinearColor(0.6f, 0.4f, 0.9f);
	Theme.ArchitectColor = FLinearColor(0.9f, 0.6f, 0.8f);

	Theme.GradientTop = FLinearColor(0.12f, 0.08f, 0.18f);
	Theme.GradientBottom = FLinearColor(0.04f, 0.02f, 0.08f);
	Theme.bUseGradients = true;

	Theme.BorderOpacity = 0.8f;
	Theme.CornerRadius = 5.0f;

	return Theme;
}
