// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "AudioCaptureCore.h"
#include "Containers/Ticker.h"
#include "HAL/PlatformProcess.h"
#include "HAL/CriticalSection.h"

enum class EVoiceProvider : uint8
{
	OpenAI,
	Offline
};

class UECPVOICE_API FVoiceManager
{
public:
	static FVoiceManager& Get();

	bool StartRecording();
	void StopRecording();
	bool IsRecording() const { return bIsRecording; }
	const TArray<float>& GetCapturedAudio() const { return RecordingBuffer; }
	int32 GetCapturedSampleRate() const { return CapturedSampleRate; }

	void TranscribeAudio(
		const TArray<float>& PcmData,
		int32 SampleRate,
		TFunction<void(const FString& Text, bool bSuccess)> OnComplete
	);

	void SynthesizeSpeech(
		const FString& Text,
		TFunction<void(const TArray<uint8>& AudioData, bool bSuccess)> OnComplete
	);

	void PlayAudioData(const TArray<uint8>& WavData, TFunction<void(bool )> OnComplete = nullptr);
	void StopPlayback();
	bool IsPlaying() const { return bIsPlaying; }

	void SpeakAsync(const FString& Text, FName QueueId = NAME_None);
	void StopQueue(FName QueueId);
	void StopAllQueues();
	bool IsQueueActive(FName QueueId) const;

	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnSpeakAsyncFinished, FName , bool );
	FOnSpeakAsyncFinished OnSpeakAsyncFinished;

	static TArray<uint8> EncodeToWav(const TArray<float>& Samples, int32 SampleRate, int32 NumChannels = 1);
	static FString StripMarkdownForTTS(const FString& MarkdownText);

	FString VoiceApiKey;
	FString TTSVoice = TEXT("alloy");
	FString TTSModel = TEXT("tts-1");
	float TTSSpeed = 1.0f;
	bool bAutoPlayResponse = false;
	bool bVoiceEnabled = false;
	bool bAutoSendAfterTranscription = false;

	TWeakPtr<class SWebBrowser> PlaybackBrowser;

	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnTranscriptionComplete, const FString& , bool );
	FOnTranscriptionComplete OnTranscriptionComplete;

	DECLARE_MULTICAST_DELEGATE_OneParam(FOnRecordingStateChanged, bool );
	FOnRecordingStateChanged OnRecordingStateChanged;

private:
	FVoiceManager() = default;
	~FVoiceManager();

	TUniquePtr<Audio::FAudioCaptureSynth> AudioCapture;
	TArray<float> RecordingBuffer;
	bool bIsRecording = false;
	int32 CapturedSampleRate = 48000;
	FTSTicker::FDelegateHandle DrainTickerHandle;

	TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> ActiveSTTRequest;
	TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> ActiveTTSRequest;

	bool bIsPlaying = false;

#if !PLATFORM_WINDOWS
	FProcHandle    ActivePlaybackProc;
	FCriticalSection ActivePlaybackProcMutex;
#endif

	TMap<FName, TArray<FString>> NarrationQueues;
	FName CurrentlyPlayingQueue = NAME_None;
	bool  bSpeechCancelled = false;

	void AdvanceQueue(FName QueueId);
};
