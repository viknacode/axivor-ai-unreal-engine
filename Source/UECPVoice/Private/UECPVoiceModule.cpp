// Copyright 2026, BlueprintsLab, All rights reserved

#include "UECPVoiceModule.h"
#include "FUECPVoiceCoordinator.h"
#include "VoiceManager.h"
#include "Managers/SettingsManager.h"
#include "UECPCoreModule.h"

DEFINE_LOG_CATEGORY(LogUECPVoice);

void FUECPVoiceModule::StartupModule()
{
	UE_LOG(LogUECPVoice, Log, TEXT("FUECPVoiceModule: StartupModule"));

	{
		FVoiceManager& VoiceMgr = FVoiceManager::Get();
		const FString& Cfg = FSettingsManager::GetGlobalConfigPath();

		FString ApiKey;
		if (GConfig->GetString(TEXT("BpGeneratorUltimate"), TEXT("VoiceApiKey"), ApiKey, Cfg))
			VoiceMgr.VoiceApiKey = ApiKey;

		bool bVoiceEnabled = false;
		if (GConfig->GetBool(TEXT("BpGeneratorUltimate"), TEXT("VoiceEnabled"), bVoiceEnabled, Cfg))
			VoiceMgr.bVoiceEnabled = bVoiceEnabled;

		bool bAutoPlay = false;
		if (GConfig->GetBool(TEXT("BpGeneratorUltimate"), TEXT("AutoPlayResponse"), bAutoPlay, Cfg))
			VoiceMgr.bAutoPlayResponse = bAutoPlay;

		float TTSSpeed = 1.0f;
		if (GConfig->GetFloat(TEXT("BpGeneratorUltimate"), TEXT("TTSSpeed"), TTSSpeed, Cfg))
			VoiceMgr.TTSSpeed = TTSSpeed;

		FString TTSVoice;
		if (GConfig->GetString(TEXT("BpGeneratorUltimate"), TEXT("TTSVoice"), TTSVoice, Cfg) && !TTSVoice.IsEmpty())
			VoiceMgr.TTSVoice = TTSVoice;

		FString TTSModel;
		if (GConfig->GetString(TEXT("BpGeneratorUltimate"), TEXT("TTSModel"), TTSModel, Cfg) && !TTSModel.IsEmpty())
			VoiceMgr.TTSModel = TTSModel;

		bool bAutoSend = false;
		if (GConfig->GetBool(TEXT("BpGeneratorUltimate"), TEXT("AutoSendVoice"), bAutoSend, Cfg))
			VoiceMgr.bAutoSendAfterTranscription = bAutoSend;
	}

	if (IUECPCoreModule::IsAvailable())
	{
		VoiceCoordinator = MakeShared<FUECPVoiceCoordinator>();
		IUECPCoreModule::Get().SetVoiceService(VoiceCoordinator);
	}
}

void FUECPVoiceModule::ShutdownModule()
{
	UE_LOG(LogUECPVoice, Log, TEXT("FUECPVoiceModule: ShutdownModule"));

	if (IUECPCoreModule::IsAvailable())
	{
		IUECPCoreModule::Get().SetVoiceService(nullptr);
	}
	VoiceCoordinator.Reset();
}

TSharedPtr<FUECPVoiceCoordinator> FUECPVoiceModule::GetVoiceCoordinator() const
{
	return VoiceCoordinator;
}

IMPLEMENT_MODULE(FUECPVoiceModule, UECPVoice)
