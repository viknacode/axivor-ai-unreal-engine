// Copyright 2026, BlueprintsLab, All rights reserved

#include "VoiceManager.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/Base64.h"
#include "UIConfigManager.h"
#include "HAL/FileManager.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <mmsystem.h>
#include "Windows/HideWindowsPlatformTypes.h"
#pragma comment(lib, "winmm.lib")
#endif
#include "WebBrowserModule.h"
#include "SWebBrowser.h"

FVoiceManager& FVoiceManager::Get()
{
	static FVoiceManager Instance;
	return Instance;
}

FVoiceManager::~FVoiceManager()
{
	if (bIsRecording)
	{
		StopRecording();
	}
}

bool FVoiceManager::StartRecording()
{
	if (bIsRecording) return false;

	RecordingBuffer.Empty();
	RecordingBuffer.Reserve(48000 * 60);

	AudioCapture = MakeUnique<Audio::FAudioCaptureSynth>();

	Audio::FCaptureDeviceInfo DeviceInfo;
	if (AudioCapture->GetDefaultCaptureDeviceInfo(DeviceInfo))
	{
		CapturedSampleRate = DeviceInfo.PreferredSampleRate;
		UE_LOG(LogTemp, Log, TEXT("VoiceManager: Device '%s' — SampleRate=%d, Channels=%d"),
			*DeviceInfo.DeviceName, DeviceInfo.PreferredSampleRate, DeviceInfo.InputChannels);
	}
	else
	{
		CapturedSampleRate = 48000;
		UE_LOG(LogTemp, Warning, TEXT("VoiceManager: Could not get device info, assuming 48kHz"));
	}

	if (!AudioCapture->OpenDefaultStream())
	{
		UE_LOG(LogTemp, Error, TEXT("VoiceManager: Failed to open default audio capture device"));
		AudioCapture.Reset();
		return false;
	}

	AudioCapture->StartCapturing();
	bIsRecording = true;

	DrainTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateLambda([this](float DeltaTime) -> bool
		{
			if (bIsRecording && AudioCapture.IsValid())
			{
				TArray<float> ChunkData;
				AudioCapture->GetAudioData(ChunkData);
				if (ChunkData.Num() > 0)
				{
					RecordingBuffer.Append(ChunkData);
				}
			}
			return bIsRecording;
		}), 0.1f);

	UE_LOG(LogTemp, Log, TEXT("VoiceManager: Recording started (sample rate: %d)"), CapturedSampleRate);
	OnRecordingStateChanged.Broadcast(true);
	return true;
}

void FVoiceManager::StopRecording()
{
	if (!bIsRecording || !AudioCapture.IsValid()) return;

	bIsRecording = false;

	FTSTicker::GetCoreTicker().RemoveTicker(DrainTickerHandle);

	TArray<float> FinalChunk;
	AudioCapture->GetAudioData(FinalChunk);
	if (FinalChunk.Num() > 0)
	{
		RecordingBuffer.Append(FinalChunk);
	}

	AudioCapture->StopCapturing();
	AudioCapture->AbortCapturing();
	AudioCapture.Reset();

	float MaxAmplitude = 0.0f;
	for (float Sample : RecordingBuffer)
	{
		MaxAmplitude = FMath::Max(MaxAmplitude, FMath::Abs(Sample));
	}

	float DurationSec = RecordingBuffer.Num() > 0 ? static_cast<float>(RecordingBuffer.Num()) / CapturedSampleRate : 0.0f;
	UE_LOG(LogTemp, Log, TEXT("VoiceManager: Recording stopped. %d samples (%.2fs at %dHz), peak amplitude: %.4f"),
		RecordingBuffer.Num(), DurationSec, CapturedSampleRate, MaxAmplitude);

	if (MaxAmplitude < 0.01f)
	{
		UE_LOG(LogTemp, Warning, TEXT("VoiceManager: Recording appears to be silence (peak %.4f). Mic may not be working."), MaxAmplitude);
	}

	OnRecordingStateChanged.Broadcast(false);
}

TArray<uint8> FVoiceManager::EncodeToWav(const TArray<float>& Samples, int32 SampleRate, int32 NumChannels)
{
	TArray<uint8> WavData;

	int32 NumSamples = Samples.Num();
	int32 BitsPerSample = 16;
	int32 ByteRate = SampleRate * NumChannels * BitsPerSample / 8;
	int32 BlockAlign = NumChannels * BitsPerSample / 8;
	int32 DataSize = NumSamples * BitsPerSample / 8;
	int32 ChunkSize = 36 + DataSize;

	auto WriteInt32 = [&WavData](int32 Val) { WavData.Append(reinterpret_cast<const uint8*>(&Val), 4); };
	auto WriteInt16 = [&WavData](int16 Val) { WavData.Append(reinterpret_cast<const uint8*>(&Val), 2); };
	auto WriteStr = [&WavData](const char* Str, int32 Len) { WavData.Append(reinterpret_cast<const uint8*>(Str), Len); };

	WriteStr("RIFF", 4);
	WriteInt32(ChunkSize);
	WriteStr("WAVE", 4);
	WriteStr("fmt ", 4);
	WriteInt32(16);
	WriteInt16(1);
	WriteInt16(static_cast<int16>(NumChannels));
	WriteInt32(SampleRate);
	WriteInt32(ByteRate);
	WriteInt16(static_cast<int16>(BlockAlign));
	WriteInt16(static_cast<int16>(BitsPerSample));
	WriteStr("data", 4);
	WriteInt32(DataSize);

	for (int32 i = 0; i < NumSamples; i++)
	{
		float Clamped = FMath::Clamp(Samples[i], -1.0f, 1.0f);
		int16 Sample = static_cast<int16>(Clamped * 32767.0f);
		WavData.Append(reinterpret_cast<const uint8*>(&Sample), 2);
	}

	return WavData;
}

void FVoiceManager::TranscribeAudio(const TArray<float>& PcmData, int32 SampleRate,
	TFunction<void(const FString& Text, bool bSuccess)> OnComplete)
{
	if (VoiceApiKey.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("VoiceManager: No Voice API key configured"));
		OnComplete(TEXT(""), false);
		return;
	}

	if (PcmData.Num() == 0)
	{
		OnComplete(TEXT(""), false);
		return;
	}

	TArray<uint8> WavData = EncodeToWav(PcmData, SampleRate);

	UE_LOG(LogTemp, Log, TEXT("VoiceManager: Sending %d bytes of WAV to Whisper API..."), WavData.Num());

	FString Boundary = FString::Printf(TEXT("----VoiceMgr%s"), *FGuid::NewGuid().ToString().Replace(TEXT("-"), TEXT("")));

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(TEXT("https://api.openai.com/v1/audio/transcriptions"));
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *VoiceApiKey));
	Request->SetHeader(TEXT("Content-Type"), FString::Printf(TEXT("multipart/form-data; boundary=%s"), *Boundary));
	Request->SetTimeout(30.0f);

	TArray<uint8> Payload;
	auto AppendStr = [&Payload](const FString& Str)
	{
		FTCHARToUTF8 Utf8(*Str);
		Payload.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
	};

	AppendStr(FString::Printf(TEXT("--%s\r\nContent-Disposition: form-data; name=\"file\"; filename=\"recording.wav\"\r\nContent-Type: audio/wav\r\n\r\n"), *Boundary));
	Payload.Append(WavData);
	AppendStr(TEXT("\r\n"));

	AppendStr(FString::Printf(TEXT("--%s\r\nContent-Disposition: form-data; name=\"model\"\r\n\r\nwhisper-1\r\n"), *Boundary));

	AppendStr(FString::Printf(TEXT("--%s\r\nContent-Disposition: form-data; name=\"response_format\"\r\n\r\njson\r\n"), *Boundary));

	FString LangCode = FUIConfigManager::Get().GetLanguage();
	if (LangCode.IsEmpty()) LangCode = TEXT("en");
	AppendStr(FString::Printf(TEXT("--%s\r\nContent-Disposition: form-data; name=\"language\"\r\n\r\n%s\r\n"), *Boundary, *LangCode));

	AppendStr(FString::Printf(TEXT("--%s--\r\n"), *Boundary));

	Request->SetContent(Payload);

	Request->OnProcessRequestComplete().BindLambda(
		[OnComplete](FHttpRequestPtr Req, FHttpResponsePtr Resp, bool bConnected)
		{
			if (!bConnected || !Resp.IsValid() || Resp->GetResponseCode() != 200)
			{
				FString Err = Resp.IsValid() ? Resp->GetContentAsString().Left(200) : TEXT("Connection failed");
				UE_LOG(LogTemp, Error, TEXT("VoiceManager STT: Failed — %s"), *Err);
				OnComplete(TEXT(""), false);
				return;
			}

			FString ResponseStr = Resp->GetContentAsString();
			TSharedPtr<FJsonObject> Json;
			TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResponseStr);

			if (FJsonSerializer::Deserialize(Reader, Json) && Json.IsValid())
			{
				FString Text;
				Json->TryGetStringField(TEXT("text"), Text);
				UE_LOG(LogTemp, Log, TEXT("VoiceManager STT: Transcribed: '%s'"), *Text);
				OnComplete(Text, !Text.IsEmpty());
			}
			else
			{
				UE_LOG(LogTemp, Error, TEXT("VoiceManager STT: Failed to parse response: %s"), *ResponseStr.Left(200));
				OnComplete(TEXT(""), false);
			}
		});

	Request->ProcessRequest();
	ActiveSTTRequest = Request;
}

void FVoiceManager::SynthesizeSpeech(const FString& Text,
	TFunction<void(const TArray<uint8>& AudioData, bool bSuccess)> OnComplete)
{
	if (VoiceApiKey.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("VoiceManager: No Voice API key configured"));
		static TArray<uint8> Empty;
		OnComplete(Empty, false);
		return;
	}

	if (Text.IsEmpty())
	{
		static TArray<uint8> Empty;
		OnComplete(Empty, false);
		return;
	}

	FString InputText = Text.Left(4096);

	TSharedPtr<FJsonObject> Body = MakeShareable(new FJsonObject);
	Body->SetStringField(TEXT("model"), TTSModel);
	Body->SetStringField(TEXT("input"), InputText);
	Body->SetStringField(TEXT("voice"), TTSVoice);
	Body->SetStringField(TEXT("response_format"), TEXT("pcm"));
	Body->SetNumberField(TEXT("speed"), TTSSpeed);

	FString RequestBody;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&RequestBody);
	FJsonSerializer::Serialize(Body.ToSharedRef(), Writer);

	UE_LOG(LogTemp, Log, TEXT("VoiceManager TTS: Requesting speech for %d chars (voice=%s, model=%s)"),
		InputText.Len(), *TTSVoice, *TTSModel);

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(TEXT("https://api.openai.com/v1/audio/speech"));
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Request->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *VoiceApiKey));
	Request->SetContentAsString(RequestBody);
	Request->SetTimeout(30.0f);

	Request->OnProcessRequestComplete().BindLambda(
		[OnComplete](FHttpRequestPtr Req, FHttpResponsePtr Resp, bool bConnected)
		{
			if (!bConnected || !Resp.IsValid() || Resp->GetResponseCode() != 200)
			{
				FString Err = Resp.IsValid() ? FString::Printf(TEXT("HTTP %d"), Resp->GetResponseCode()) : TEXT("Connection failed");
				UE_LOG(LogTemp, Error, TEXT("VoiceManager TTS: Failed — %s"), *Err);
				static TArray<uint8> Empty;
				OnComplete(Empty, false);
				return;
			}

			TArray<uint8> RawPCM = Resp->GetContent();
			UE_LOG(LogTemp, Log, TEXT("VoiceManager TTS: Received %d bytes of raw PCM"), RawPCM.Num());

			if (RawPCM.Num() > 0)
			{
				TArray<uint8> WavData;
				int32 SampleRate = 24000;
				int32 NumChannels = 1;
				int32 BitsPerSample = 16;
				int32 ByteRate = SampleRate * NumChannels * BitsPerSample / 8;
				int32 BlockAlign = NumChannels * BitsPerSample / 8;
				int32 DataSize = RawPCM.Num();
				int32 ChunkSize = 36 + DataSize;

				auto WriteInt32 = [&WavData](int32 Val) { WavData.Append(reinterpret_cast<const uint8*>(&Val), 4); };
				auto WriteInt16 = [&WavData](int16 Val) { WavData.Append(reinterpret_cast<const uint8*>(&Val), 2); };
				auto WriteStr = [&WavData](const char* Str, int32 Len) { WavData.Append(reinterpret_cast<const uint8*>(Str), Len); };

				WriteStr("RIFF", 4);
				WriteInt32(ChunkSize);
				WriteStr("WAVE", 4);
				WriteStr("fmt ", 4);
				WriteInt32(16);
				WriteInt16(1);
				WriteInt16(static_cast<int16>(NumChannels));
				WriteInt32(SampleRate);
				WriteInt32(ByteRate);
				WriteInt16(static_cast<int16>(BlockAlign));
				WriteInt16(static_cast<int16>(BitsPerSample));
				WriteStr("data", 4);
				WriteInt32(DataSize);

				WavData.Append(RawPCM);

				UE_LOG(LogTemp, Log, TEXT("VoiceManager TTS: Wrapped PCM in WAV header (%d bytes total)"), WavData.Num());
				OnComplete(WavData, true);
			}
			else
			{
				OnComplete(RawPCM, false);
			}
		});

	Request->ProcessRequest();
	ActiveTTSRequest = Request;
}

void FVoiceManager::PlayAudioData(const TArray<uint8>& WavData, TFunction<void(bool)> OnComplete)
{
	if (WavData.Num() == 0)
	{
		if (OnComplete) AsyncTask(ENamedThreads::GameThread, [OnComplete]() { OnComplete(false); });
		return;
	}

	FString TempDir = FPaths::ProjectSavedDir() / TEXT("Temp");
	IFileManager::Get().MakeDirectory(*TempDir, true);
	FString TempPath = TempDir / TEXT("tts_response.wav");

	if (!FFileHelper::SaveArrayToFile(WavData, *TempPath))
	{
		UE_LOG(LogTemp, Error, TEXT("VoiceManager: Failed to save TTS audio to %s"), *TempPath);
		if (OnComplete) AsyncTask(ENamedThreads::GameThread, [OnComplete]() { OnComplete(false); });
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("VoiceManager: Playing TTS audio (%d bytes) from %s"), WavData.Num(), *TempPath);
	bIsPlaying = true;

	Async(EAsyncExecution::Thread, [TempPath, this, OnComplete]()
	{
#if PLATFORM_WINDOWS
		PlaySound(*TempPath, NULL, SND_FILENAME | SND_SYNC);
#else
	#if PLATFORM_MAC
		const TCHAR* PlayerExe = TEXT("/usr/bin/afplay");
	#else
		const TCHAR* PlayerExe = TEXT("/usr/bin/aplay");
	#endif
		const FString Args = FString::Printf(TEXT("\"%s\""), *TempPath);
		FProcHandle LocalProc = FPlatformProcess::CreateProc(
			PlayerExe, *Args, false, false, false, nullptr, 0, nullptr, nullptr);
		if (LocalProc.IsValid())
		{
			{
				FScopeLock Lock(&ActivePlaybackProcMutex);
				ActivePlaybackProc = LocalProc;
			}
			FPlatformProcess::WaitForProc(LocalProc);
			{
				FScopeLock Lock(&ActivePlaybackProcMutex);
				FPlatformProcess::CloseProc(LocalProc);
				ActivePlaybackProc.Reset();
			}
		}
#endif
		bIsPlaying = false;
		UE_LOG(LogTemp, Log, TEXT("VoiceManager: TTS playback finished"));

		if (OnComplete)
		{
			const bool bCancelled = bSpeechCancelled;
			AsyncTask(ENamedThreads::GameThread, [OnComplete, bCancelled]() { OnComplete(!bCancelled); });
		}
	});
}

void FVoiceManager::StopPlayback()
{
#if PLATFORM_WINDOWS
	PlaySound(NULL, NULL, 0);
#else
	FScopeLock Lock(&ActivePlaybackProcMutex);
	if (ActivePlaybackProc.IsValid())
	{
		FPlatformProcess::TerminateProc(ActivePlaybackProc);
	}
#endif
	bIsPlaying = false;
}

FString FVoiceManager::StripMarkdownForTTS(const FString& MarkdownText)
{
	FString Result = MarkdownText;

	while (true)
	{
		int32 Start = Result.Find(TEXT("```"));
		if (Start == INDEX_NONE) break;
		int32 End = Result.Find(TEXT("```"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Start + 3);
		if (End == INDEX_NONE) { Result = Result.Left(Start); break; }
		Result = Result.Left(Start) + Result.Mid(End + 3);
	}

	Result.ReplaceInline(TEXT("`"), TEXT(""));

	Result.ReplaceInline(TEXT("**"), TEXT(""));
	Result.ReplaceInline(TEXT("__"), TEXT(""));
	Result.ReplaceInline(TEXT("*"), TEXT(""));
	Result.ReplaceInline(TEXT("_"), TEXT(" "));

	while (Result.Contains(TEXT("### "))) Result.ReplaceInline(TEXT("### "), TEXT(""));
	while (Result.Contains(TEXT("## "))) Result.ReplaceInline(TEXT("## "), TEXT(""));
	while (Result.Contains(TEXT("# "))) Result.ReplaceInline(TEXT("# "), TEXT(""));

	FRegexPattern LinkPattern(TEXT("\\[([^\\]]*)\\]\\([^)]*\\)"));
	FRegexMatcher LinkMatcher(LinkPattern, Result);
	while (LinkMatcher.FindNext())
	{
		Result = Result.Replace(*LinkMatcher.GetCaptureGroup(0), *LinkMatcher.GetCaptureGroup(1));
		LinkMatcher = FRegexMatcher(LinkPattern, Result);
	}

	while (true)
	{
		int32 Start = Result.Find(TEXT("{\"tool_name\""));
		if (Start == INDEX_NONE) break;
		int32 End = Result.Find(TEXT("}"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Start);
		if (End == INDEX_NONE) break;
		Result = Result.Left(Start) + Result.Mid(End + 1);
	}

	while (Result.Contains(TEXT("\n\n\n"))) Result.ReplaceInline(TEXT("\n\n\n"), TEXT("\n\n"));
	Result.TrimStartAndEndInline();

	return Result;
}

void FVoiceManager::SpeakAsync(const FString& Text, FName QueueId)
{
	check(IsInGameThread());

	if (Text.IsEmpty()) { OnSpeakAsyncFinished.Broadcast(QueueId, false); return; }
	if (!bVoiceEnabled || VoiceApiKey.IsEmpty())
	{
		OnSpeakAsyncFinished.Broadcast(QueueId, false);
		return;
	}

	NarrationQueues.FindOrAdd(QueueId).Add(Text);

	if (CurrentlyPlayingQueue.IsNone() && !bIsPlaying)
	{
		AdvanceQueue(QueueId);
	}
}

void FVoiceManager::StopQueue(FName QueueId)
{
	check(IsInGameThread());

	if (TArray<FString>* Q = NarrationQueues.Find(QueueId))
	{
		Q->Reset();
	}
	if (CurrentlyPlayingQueue == QueueId && bIsPlaying)
	{
		bSpeechCancelled = true;
		StopPlayback();
	}
}

void FVoiceManager::StopAllQueues()
{
	check(IsInGameThread());

	for (TPair<FName, TArray<FString>>& Pair : NarrationQueues)
	{
		Pair.Value.Reset();
	}
	if (bIsPlaying)
	{
		bSpeechCancelled = true;
		StopPlayback();
	}
}

bool FVoiceManager::IsQueueActive(FName QueueId) const
{
	if (CurrentlyPlayingQueue == QueueId && bIsPlaying) return true;
	if (const TArray<FString>* Q = NarrationQueues.Find(QueueId))
		return Q->Num() > 0;
	return false;
}

void FVoiceManager::AdvanceQueue(FName QueueId)
{
	check(IsInGameThread());

	TArray<FString>* Q = NarrationQueues.Find(QueueId);
	if (!Q || Q->Num() == 0)
	{
		CurrentlyPlayingQueue = NAME_None;
		for (TPair<FName, TArray<FString>>& Pair : NarrationQueues)
		{
			if (Pair.Value.Num() > 0) { AdvanceQueue(Pair.Key); return; }
		}
		return;
	}

	FString NextText = MoveTemp((*Q)[0]);
	Q->RemoveAt(0, 1, EAllowShrinking::No);

	CurrentlyPlayingQueue = QueueId;
	bSpeechCancelled = false;

	SynthesizeSpeech(NextText,
		[this, QueueId](const TArray<uint8>& AudioData, bool bSuccess)
		{
			if (!bSuccess || AudioData.Num() == 0)
			{
				OnSpeakAsyncFinished.Broadcast(QueueId, false);
				if (CurrentlyPlayingQueue == QueueId)
				{
					CurrentlyPlayingQueue = NAME_None;
					AdvanceQueue(QueueId);
				}
				return;
			}

			PlayAudioData(AudioData,
				[this, QueueId](bool bCompleted)
				{
					OnSpeakAsyncFinished.Broadcast(QueueId, bCompleted);
					if (CurrentlyPlayingQueue == QueueId)
					{
						CurrentlyPlayingQueue = NAME_None;
						AdvanceQueue(QueueId);
					}
				});
		});
}
