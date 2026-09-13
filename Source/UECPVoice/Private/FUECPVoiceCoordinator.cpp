// Copyright 2026, BlueprintsLab, All rights reserved

#include "FUECPVoiceCoordinator.h"
#include "VoiceManager.h"
#include "SUECPMainWidget.h"
#include "Widget/UUECPAppBridge.h"
#include "SWebBrowser.h"
#include "Async/Async.h"

void FUECPVoiceCoordinator::InitializeShellRefs(TWeakPtr<SUECPMainWidget> InShell,
	TWeakObjectPtr<UUECPAppBridge> InBridge)
{
	Shell  = InShell;
	Bridge = InBridge;

	if (!ManagerSpeakAsyncFinishedHandle.IsValid())
	{
		ManagerSpeakAsyncFinishedHandle = FVoiceManager::Get().OnSpeakAsyncFinished.AddLambda(
			[this](FName QueueId, bool bCompleted)
			{
				NarrationFinishedDelegate.Broadcast(QueueId, bCompleted);
			});
	}
}

void FUECPVoiceCoordinator::EnsurePlaybackBrowserBound()
{
	if (TSharedPtr<SUECPMainWidget> W = Shell.Pin())
	{
		if (TSharedPtr<SWebBrowser> Browser = W->GetAppBrowserForExtraction())
			FVoiceManager::Get().PlaybackBrowser = Browser;
	}
}

void FUECPVoiceCoordinator::ToggleMic()
{
	FVoiceManager& VoiceMgr = FVoiceManager::Get();

	auto PushVoiceState = [this](bool bRecording)
	{
		if (UUECPAppBridge* B = Bridge.Get())
		{
			B->ExecJs(FString::Printf(TEXT("if(typeof onVoiceState==='function')onVoiceState(%s)"),
				bRecording ? TEXT("true") : TEXT("false")));
		}
	};

	if (VoiceMgr.IsRecording())
	{
		VoiceMgr.StopRecording();
		PushVoiceState(false);

		const TArray<float>& Audio = VoiceMgr.GetCapturedAudio();
		const int32 SampleRate = VoiceMgr.GetCapturedSampleRate();
		if (Audio.Num() == 0) return;

		const float DurationSec = static_cast<float>(Audio.Num()) / FMath::Max(SampleRate, 1);
		UE_LOG(LogTemp, Log, TEXT("Voice: Transcribing %d samples (%.2fs at %dHz)..."),
			Audio.Num(), DurationSec, SampleRate);

		if (DurationSec < 0.5f)
		{
			UE_LOG(LogTemp, Warning, TEXT("Voice: Recording too short (%.2fs), skipping transcription"), DurationSec);
			return;
		}

		VoiceMgr.TranscribeAudio(Audio, SampleRate,
			[this](const FString& Text, bool bSuccess)
			{
				AsyncTask(ENamedThreads::GameThread, [this, Text, bSuccess]()
				{
					OnTranscriptionReceived(Text, bSuccess);
				});
			});
		return;
	}

	if (VoiceMgr.StartRecording())
		PushVoiceState(true);
	else
		UE_LOG(LogTemp, Error, TEXT("Voice: Failed to start recording. Check microphone."));
}

void FUECPVoiceCoordinator::OnTranscriptionReceived(const FString& Text, bool bSuccess)
{
	if (!bSuccess || Text.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("Voice: Transcription failed or empty"));
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("Voice: Transcription received: '%s'"), *Text);

	if (UUECPAppBridge* B = Bridge.Get())
	{
		FString Escaped = Text;
		Escaped.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
		Escaped.ReplaceInline(TEXT("\""), TEXT("\\\""));
		Escaped.ReplaceInline(TEXT("\n"), TEXT("\\n"));
		Escaped.ReplaceInline(TEXT("\r"), TEXT(""));
		B->ExecJs(FString::Printf(
			TEXT("if(typeof onVoiceTranscription==='function')onVoiceTranscription(\"%s\")"),
			*Escaped));
	}

	if (FVoiceManager::Get().bAutoSendAfterTranscription)
	{
		if (TSharedPtr<SUECPMainWidget> W = Shell.Pin())
		{
			W->VoiceAutoSendArchitectForExtraction(Text);
		}
	}
}

FUECPVoiceSettingsSnapshot FUECPVoiceCoordinator::GetSettings() const
{
	const FVoiceManager& V = FVoiceManager::Get();
	FUECPVoiceSettingsSnapshot Out;
	Out.VoiceApiKey                 = V.VoiceApiKey;
	Out.bVoiceEnabled               = V.bVoiceEnabled;
	Out.bAutoPlayResponse           = V.bAutoPlayResponse;
	Out.bAutoSendAfterTranscription = V.bAutoSendAfterTranscription;
	Out.TTSVoice                    = V.TTSVoice;
	Out.TTSModel                    = V.TTSModel;
	Out.TTSSpeed                    = V.TTSSpeed;
	return Out;
}

void FUECPVoiceCoordinator::ApplySettings(const FUECPVoiceSettingsSnapshot& In)
{
	FVoiceManager& V = FVoiceManager::Get();
	V.VoiceApiKey                 = In.VoiceApiKey;
	V.bVoiceEnabled               = In.bVoiceEnabled;
	V.bAutoPlayResponse           = In.bAutoPlayResponse;
	V.bAutoSendAfterTranscription = In.bAutoSendAfterTranscription;
	if (!In.TTSVoice.IsEmpty()) V.TTSVoice = In.TTSVoice;
	if (!In.TTSModel.IsEmpty()) V.TTSModel = In.TTSModel;
	if (In.TTSSpeed > 0.0f)     V.TTSSpeed = In.TTSSpeed;
}

bool FUECPVoiceCoordinator::IsEnabled() const
{
	return FVoiceManager::Get().bVoiceEnabled;
}

bool FUECPVoiceCoordinator::IsRecording() const
{
	return FVoiceManager::Get().IsRecording();
}

void FUECPVoiceCoordinator::PlayTTSForResponse(const FString& ResponseText)
{
	FVoiceManager& VoiceMgr = FVoiceManager::Get();

	UE_LOG(LogTemp, Verbose,
		TEXT("Voice TTS: PlayTTSForResponse called — VoiceEnabled=%s, AutoPlay=%s, ApiKey=%s, TextLen=%d"),
		VoiceMgr.bVoiceEnabled ? TEXT("true") : TEXT("false"),
		VoiceMgr.bAutoPlayResponse ? TEXT("true") : TEXT("false"),
		VoiceMgr.VoiceApiKey.IsEmpty() ? TEXT("empty") : TEXT("set"),
		ResponseText.Len());

	if (!VoiceMgr.bAutoPlayResponse)
	{
		UE_LOG(LogTemp, Verbose, TEXT("Voice TTS: Skipped — auto-play off"));
		return;
	}

	FString CleanText = FVoiceManager::StripMarkdownForTTS(ResponseText);
	if (CleanText.IsEmpty())
	{
		UE_LOG(LogTemp, Verbose, TEXT("Voice TTS: Skipped — stripped text is empty"));
		return;
	}

	VoiceMgr.StopQueue(NAME_None);

	EnsurePlaybackBrowserBound();
	VoiceMgr.SpeakAsync(CleanText, NAME_None);
}

void FUECPVoiceCoordinator::SpeakAsync(const FString& Text, FName QueueId)
{
	EnsurePlaybackBrowserBound();
	FVoiceManager::Get().SpeakAsync(Text, QueueId);
}

void FUECPVoiceCoordinator::StopQueue(FName QueueId)
{
	FVoiceManager::Get().StopQueue(QueueId);
}

void FUECPVoiceCoordinator::StopAll()
{
	FVoiceManager::Get().StopAllQueues();
}
