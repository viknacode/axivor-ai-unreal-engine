// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "AssetReferenceTypes.h"
#include "HAL/PlatformProcess.h"

enum class EApiProvider : uint8
{
	Gemini,
	OpenAI,
	Claude,
	DeepSeek,
	Custom
};

struct FApiSettings
{
	FString ApiKey;
	FString CustomBaseURL;
	FString CustomModelName;
	EApiProvider Provider = EApiProvider::Gemini;
};

class FSettingsManager
{
public:
	static UECPCORE_API FSettingsManager& Get();

	UECPCORE_API FApiSettings LoadSettings();

	UECPCORE_API void SaveSettings(const FApiSettings& Settings);

	UECPCORE_API FString LoadCustomInstructions();

	UECPCORE_API void SaveCustomInstructions(const FString& Instructions);

	static UECPCORE_API FString ProviderToString(EApiProvider Provider);

	static UECPCORE_API EApiProvider StringToProvider(const FString& ProviderString);

	UECPCORE_API void SaveInteractionMode(EAIInteractionMode Mode);
	UECPCORE_API EAIInteractionMode LoadInteractionMode();

	UECPCORE_API void SaveDefaultInteractionMode(EAIInteractionMode Mode);
	UECPCORE_API EAIInteractionMode LoadDefaultInteractionMode();

	UECPCORE_API void SaveAutoApproveMemories(bool bAutoApprove);
	UECPCORE_API bool LoadAutoApproveMemories();

	static UECPCORE_API FString GetDefaultSavePath();

	static UECPCORE_API FString NormalizeSavePath(const FString& SavePath);

	static UECPCORE_API bool GetSoundOnCompletion();

	static UECPCORE_API FString GetGlobalDataDir();

	static UECPCORE_API const FString& GetGlobalConfigPath();

private:
	FSettingsManager() = default;
	FSettingsManager(const FSettingsManager&) = delete;
	FSettingsManager& operator=(const FSettingsManager&) = delete;
};
