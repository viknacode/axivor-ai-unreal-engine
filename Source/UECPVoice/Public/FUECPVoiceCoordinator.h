// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "Services/IUECPVoiceService.h"

class UUECPAppBridge;
class SUECPMainWidget;

class UECPVOICE_API FUECPVoiceCoordinator final : public IUECPVoiceService
{
public:
	FUECPVoiceCoordinator() = default;

	virtual void InitializeShellRefs(TWeakPtr<SUECPMainWidget> InShell,
		TWeakObjectPtr<UUECPAppBridge> InBridge) override;
	virtual void ToggleMic() override;
	virtual void PlayTTSForResponse(const FString& ResponseText) override;
	virtual void SpeakAsync(const FString& Text, FName QueueId = NAME_None) override;
	virtual void StopQueue(FName QueueId) override;
	virtual void StopAll() override;
	virtual FUECPNarrationFinishedDelegate& OnNarrationFinished() override { return NarrationFinishedDelegate; }
	virtual FUECPVoiceSettingsSnapshot GetSettings() const override;
	virtual void ApplySettings(const FUECPVoiceSettingsSnapshot& In) override;
	virtual bool IsEnabled() const override;
	virtual bool IsRecording() const override;

private:
	TWeakPtr<SUECPMainWidget>      Shell;
	TWeakObjectPtr<UUECPAppBridge> Bridge;

	FUECPNarrationFinishedDelegate NarrationFinishedDelegate;
	FDelegateHandle                ManagerSpeakAsyncFinishedHandle;

	void OnTranscriptionReceived(const FString& Text, bool bSuccess);
	void EnsurePlaybackBrowserBound();
};
