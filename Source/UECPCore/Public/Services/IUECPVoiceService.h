// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtr.h"

class SUECPMainWidget;
class UUECPAppBridge;

struct FUECPVoiceSettingsSnapshot
{
	FString VoiceApiKey;
	bool    bVoiceEnabled              = false;
	bool    bAutoPlayResponse          = false;
	bool    bAutoSendAfterTranscription = false;
	FString TTSVoice;
	FString TTSModel;
	float   TTSSpeed                   = 1.0f;
};

DECLARE_MULTICAST_DELEGATE_TwoParams(FUECPNarrationFinishedDelegate, FName , bool );

class UECPCORE_API IUECPVoiceService
{
public:
	virtual ~IUECPVoiceService() = default;

	virtual void InitializeShellRefs(TWeakPtr<SUECPMainWidget> Shell,
		TWeakObjectPtr<UUECPAppBridge> Bridge) = 0;

	virtual void ToggleMic() = 0;

	virtual void PlayTTSForResponse(const FString& ResponseText) = 0;

	virtual void SpeakAsync(const FString& Text, FName QueueId = NAME_None) = 0;

	virtual void StopQueue(FName QueueId) = 0;

	virtual void StopAll() = 0;

	virtual FUECPNarrationFinishedDelegate& OnNarrationFinished() = 0;

	virtual FUECPVoiceSettingsSnapshot GetSettings() const = 0;
	virtual void ApplySettings(const FUECPVoiceSettingsSnapshot& In) = 0;

	virtual bool IsEnabled() const = 0;
	virtual bool IsRecording() const = 0;
};
