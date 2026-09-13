// Copyright 2026, BlueprintsLab, All rights reserved

#include "Managers/SettingsManager.h"
#include "Utils/MountResolver.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"

FSettingsManager& FSettingsManager::Get()
{
	static FSettingsManager Instance;
	return Instance;
}

FString FSettingsManager::GetGlobalDataDir()
{
	static FString Dir;
	if (Dir.IsEmpty())
	{
		Dir = FPaths::Combine(FPlatformProcess::UserSettingsDir(), TEXT("UltimateCoPilot"));
		IPlatformFile& PF = FPlatformFileManager::Get().GetPlatformFile();
		if (!PF.DirectoryExists(*Dir))
			PF.CreateDirectoryTree(*Dir);
	}
	return Dir;
}

const FString& FSettingsManager::GetGlobalConfigPath()
{
	static FString Path;
	static bool bInitialized = false;
	if (!bInitialized)
	{
		bInitialized = true;
		Path = GetGlobalDataDir() / TEXT("settings.ini");

		if (GConfig)
		{
			IPlatformFile& PF = FPlatformFileManager::Get().GetPlatformFile();
			if (!PF.FileExists(*Path))
				FFileHelper::SaveStringToFile(TEXT("[BpGeneratorUltimate]\n"), *Path);

			GConfig->LoadFile(Path);

			FString ExistingProvider;
			const bool bHasGlobalSettings =
				GConfig->GetString(TEXT("BpGeneratorUltimate"), TEXT("Provider"), ExistingProvider, Path)
				&& !ExistingProvider.IsEmpty();

			if (!bHasGlobalSettings)
			{
				static const TCHAR* UserGlobalKeys[] = {
					TEXT("Provider"), TEXT("ApiKey"), TEXT("CustomBaseURL"), TEXT("CustomModelName"),
					TEXT("CustomInstructions"), TEXT("InteractionMode"),
					TEXT("MeshyApiKey"), TEXT("VoiceApiKey"), TEXT("VoiceEnabled"),
					TEXT("AutoPlayResponse"), TEXT("TTSSpeed"), TEXT("TTSVoice"), TEXT("TTSModel"),
					TEXT("AutoSendVoice"), TEXT("SoundGenProvider"), TEXT("SoundGenKeyCache"),
					TEXT("SoundOnCompletion"),
					TEXT("MaxBatchSize"), TEXT("MaxToolCallDepth"),
					TEXT("DiscoverNodesLimit"), TEXT("DestructiveOpsConfirm"),
					TEXT("AutoValidateBlueprints"), TEXT("EnablePreFlight"),
					TEXT("Language"), TEXT("ChatFontSize"), TEXT("ChatDensity"),
					TEXT("AccentColor"), TEXT("ColorTheme"), TEXT("CodeFontFamily"),
					TEXT("UiFontSize"), TEXT("UiFontFamily"), TEXT("BgColor"),
					TEXT("SurfaceColor"), TEXT("TextColor"), TEXT("AutoContrast"),
					TEXT("ReducedMotion"), TEXT("HighContrast"), TEXT("ShowTimestamps"),
					TEXT("ToastDuration"), TEXT("ThemePreset"), TEXT("CustomThemeColors"),
					TEXT("VisualStyle"), TEXT("GlowEffects"), TEXT("GlassPanels"), TEXT("BrandMark"),
					TEXT("TurboMode"), TEXT("TurboSandbox"),
					TEXT("ConfirmTimeoutSeconds"), TEXT("ConfirmProceedWhenUnanswered"),
					TEXT("PCGInstanceBudget"),
					TEXT("ArrangeNodesModifier"), TEXT("ArrangeNodesKey"),
					TEXT("VoicePTTModifier"), TEXT("VoicePTTKey"),
					TEXT("MemoryExtractionThreshold"),
					TEXT("DismissedWelcomeVersion"), TEXT("LastDismissedAnnouncementId"),
				};

				bool bDidMigrate = false;
				for (const TCHAR* Key : UserGlobalKeys)
				{
					FString Val;
					if (GConfig->GetString(TEXT("BpGeneratorUltimate"), Key, Val, GEditorPerProjectIni) && !Val.IsEmpty())
					{
						GConfig->SetString(TEXT("BpGeneratorUltimate"), Key, *Val, Path);
						bDidMigrate = true;
					}
				}

				FString FtSlot, FtModel;
				if (GConfig->GetString(TEXT("FreeTier"), TEXT("SelectedSlot"), FtSlot, GEditorPerProjectIni) && !FtSlot.IsEmpty())
				{
					GConfig->SetString(TEXT("FreeTier"), TEXT("SelectedSlot"), *FtSlot, Path);
					bDidMigrate = true;
				}
				if (GConfig->GetString(TEXT("FreeTier"), TEXT("Slot2Model"), FtModel, GEditorPerProjectIni) && !FtModel.IsEmpty())
				{
					GConfig->SetString(TEXT("FreeTier"), TEXT("Slot2Model"), *FtModel, Path);
					bDidMigrate = true;
				}

				FString UsageStat;
				if (GConfig->GetString(TEXT("Editor.Stats"), TEXT("LastKnownState"), UsageStat, GEditorPerProjectIni) && !UsageStat.IsEmpty())
				{
					GConfig->SetString(TEXT("Editor.Stats"), TEXT("LastKnownState"), *UsageStat, Path);
					bDidMigrate = true;
				}

				static const TCHAR* UpdateKeys[] = { TEXT("LastUpdateCheck"), TEXT("AutoCheckEnabled") };
				for (const TCHAR* Key : UpdateKeys)
				{
					FString Val;
					if (GConfig->GetString(TEXT("BpGeneratorUltimate.Updates"), Key, Val, GEditorPerProjectIni) && !Val.IsEmpty())
					{
						GConfig->SetString(TEXT("BpGeneratorUltimate.Updates"), Key, *Val, Path);
						bDidMigrate = true;
					}
				}

				static const TCHAR* ModelKeys[] = {
					TEXT("GeminiModelName"), TEXT("GeminiThinkingEffort"),
					TEXT("OpenAIModelName"), TEXT("OpenAIThinkingEffort"),
					TEXT("ClaudeModelName"), TEXT("ClaudeThinkingEffort"),
					TEXT("DeepSeekModelName"), TEXT("DeepSeekThinkingEffort"),
				};
				for (const TCHAR* Key : ModelKeys)
				{
					FString Val;
					if (GConfig->GetString(TEXT("BpGeneratorUltimate"), Key, Val, GEditorPerProjectIni) && !Val.IsEmpty())
					{
						GConfig->SetString(TEXT("BpGeneratorUltimate"), Key, *Val, Path);
						bDidMigrate = true;
					}
				}

				if (bDidMigrate)
				{
					GConfig->Flush(false, Path);
					UE_LOG(LogTemp, Log, TEXT("BpGenerator: Migrated user settings from project config to %s"), *Path);
				}
			}
		}
	}
	return Path;
}

FApiSettings FSettingsManager::LoadSettings()
{
	FApiSettings Settings;
	const FString& CfgPath = GetGlobalConfigPath();

	FString ProviderString;
	GConfig->GetString(TEXT("BpGeneratorUltimate"), TEXT("Provider"), ProviderString, CfgPath);
	GConfig->GetString(TEXT("BpGeneratorUltimate"), TEXT("ApiKey"), Settings.ApiKey, CfgPath);
	GConfig->GetString(TEXT("BpGeneratorUltimate"), TEXT("CustomBaseURL"), Settings.CustomBaseURL, CfgPath);
	GConfig->GetString(TEXT("BpGeneratorUltimate"), TEXT("CustomModelName"), Settings.CustomModelName, CfgPath);

	Settings.Provider = StringToProvider(ProviderString);

	return Settings;
}

void FSettingsManager::SaveSettings(const FApiSettings& Settings)
{
	const FString& CfgPath = GetGlobalConfigPath();
	FString ProviderString = ProviderToString(Settings.Provider);

	GConfig->SetString(TEXT("BpGeneratorUltimate"), TEXT("Provider"), *ProviderString, CfgPath);
	GConfig->SetString(TEXT("BpGeneratorUltimate"), TEXT("ApiKey"), *Settings.ApiKey, CfgPath);
	GConfig->SetString(TEXT("BpGeneratorUltimate"), TEXT("CustomBaseURL"), *Settings.CustomBaseURL, CfgPath);
	GConfig->SetString(TEXT("BpGeneratorUltimate"), TEXT("CustomModelName"), *Settings.CustomModelName, CfgPath);
	GConfig->Flush(false, CfgPath);
}

FString FSettingsManager::LoadCustomInstructions()
{
	FString Instructions;
	GConfig->GetString(TEXT("BpGeneratorUltimate"), TEXT("CustomInstructions"), Instructions, GetGlobalConfigPath());
	return Instructions;
}

void FSettingsManager::SaveCustomInstructions(const FString& Instructions)
{
	const FString& CfgPath = GetGlobalConfigPath();
	GConfig->SetString(TEXT("BpGeneratorUltimate"), TEXT("CustomInstructions"), *Instructions, CfgPath);
	GConfig->Flush(false, CfgPath);
}

FString FSettingsManager::ProviderToString(EApiProvider Provider)
{
	switch (Provider)
	{
	case EApiProvider::OpenAI: return TEXT("OpenAI");
	case EApiProvider::Claude: return TEXT("Claude");
	case EApiProvider::DeepSeek: return TEXT("DeepSeek");
	case EApiProvider::Custom: return TEXT("Custom");
	case EApiProvider::Gemini:
	default: return TEXT("Gemini");
	}
}

void FSettingsManager::SaveInteractionMode(EAIInteractionMode Mode)
{
	const FString& CfgPath = GetGlobalConfigPath();
	int32 ModeInt = (int32)Mode;
	GConfig->SetInt(TEXT("BpGeneratorUltimate"), TEXT("InteractionMode"), ModeInt, CfgPath);
	GConfig->Flush(false, CfgPath);
}

EAIInteractionMode FSettingsManager::LoadInteractionMode()
{
	int32 ModeInt = (int32)EAIInteractionMode::AutoEdit;
	GConfig->GetInt(TEXT("BpGeneratorUltimate"), TEXT("InteractionMode"), ModeInt, GetGlobalConfigPath());
	if (ModeInt < 0 || ModeInt > 3) ModeInt = (int32)EAIInteractionMode::AutoEdit;
	return (EAIInteractionMode)ModeInt;
}

void FSettingsManager::SaveDefaultInteractionMode(EAIInteractionMode Mode)
{
	const FString& CfgPath = GetGlobalConfigPath();
	GConfig->SetInt(TEXT("BpGeneratorUltimate"), TEXT("DefaultInteractionMode"), (int32)Mode, CfgPath);
	GConfig->Flush(false, CfgPath);
}

EAIInteractionMode FSettingsManager::LoadDefaultInteractionMode()
{
	int32 ModeInt = (int32)EAIInteractionMode::AutoEdit;
	if (!GConfig->GetInt(TEXT("BpGeneratorUltimate"), TEXT("DefaultInteractionMode"), ModeInt, GetGlobalConfigPath()))
	{
		GConfig->GetInt(TEXT("BpGeneratorUltimate"), TEXT("InteractionMode"), ModeInt, GetGlobalConfigPath());
	}
	if (ModeInt < 0 || ModeInt > 3) ModeInt = (int32)EAIInteractionMode::AutoEdit;
	return (EAIInteractionMode)ModeInt;
}

void FSettingsManager::SaveAutoApproveMemories(bool bAutoApprove)
{
	const FString& CfgPath = GetGlobalConfigPath();
	GConfig->SetBool(TEXT("BpGeneratorUltimate"), TEXT("AutoApproveMemories"), bAutoApprove, CfgPath);
	GConfig->Flush(false, CfgPath);
}

bool FSettingsManager::LoadAutoApproveMemories()
{
	bool bVal = false;
	GConfig->GetBool(TEXT("BpGeneratorUltimate"), TEXT("AutoApproveMemories"), bVal, GetGlobalConfigPath());
	return bVal;
}

EApiProvider FSettingsManager::StringToProvider(const FString& ProviderString)
{
	if (ProviderString == TEXT("OpenAI"))
	{
		return EApiProvider::OpenAI;
	}
	else if (ProviderString == TEXT("Claude"))
	{
		return EApiProvider::Claude;
	}
	else if (ProviderString == TEXT("DeepSeek"))
	{
		return EApiProvider::DeepSeek;
	}
	else if (ProviderString == TEXT("Custom"))
	{
		return EApiProvider::Custom;
	}
	else
	{
		return EApiProvider::Gemini;
	}
}

FString FSettingsManager::GetDefaultSavePath()
{
	FString Path;
	GConfig->GetString(TEXT("BpGeneratorUltimate"), TEXT("DefaultSavePath"), Path, GEditorPerProjectIni);
	Path = Path.TrimStartAndEnd();

	while (Path.EndsWith(TEXT("/"))) Path = Path.LeftChop(1);

	if (Path.IsEmpty()) return TEXT("/Game/Generated");

	if (!Path.StartsWith(TEXT("/Game/")))
	{
		if (Path.StartsWith(TEXT("/"))) Path = Path.Mid(1);
		Path = TEXT("/Game/") + Path;
	}
	return Path;
}

FString FSettingsManager::NormalizeSavePath(const FString& SavePath)
{
	FString Path = SavePath.TrimStartAndEnd();
	while (Path.EndsWith(TEXT("/"))) Path = Path.LeftChop(1);

	if (Path.IsEmpty())
		return GetDefaultSavePath();

	if (Path.StartsWith(TEXT("/")))
	{
		if (UECPMountResolver::IsValidMountedPath(Path))
		{
			return Path;
		}
		Path = TEXT("/Game/") + Path.Mid(1);
		return Path;
	}

	return TEXT("/Game/") + Path;
}

bool FSettingsManager::GetSoundOnCompletion()
{
	bool bVal = false;
	GConfig->GetBool(TEXT("BpGeneratorUltimate"), TEXT("SoundOnCompletion"), bVal, GetGlobalConfigPath());
	return bVal;
}
