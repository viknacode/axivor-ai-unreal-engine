// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Services/UECPAssetGenTypes.h"

class UECPASSETGEN_API FSoundGenManager
{
public:
	static FSoundGenManager& Get();

	void GenerateSound(const FSoundGenRequest& Request, const FString& ApiKey,
	                   const FString& Endpoint, TFunction<void(const FSoundGenResult&)> OnComplete);
	void CancelGeneration();

private:
	FSoundGenManager() = default;
	~FSoundGenManager() = default;

	TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> ActiveRequest;

	enum class EProviderType { OpenAI, ElevenLabs, ElevenLabsSFX, GoogleTTS, StabilityAI, HuggingFace, Generic };
	static EProviderType DetectProvider(const FString& Endpoint);

	void OnRequestComplete(FHttpRequestPtr Req, FHttpResponsePtr Response, bool bWasSuccessful,
	                        FSoundGenRequest OrigRequest, TFunction<void(const FSoundGenResult&)> Callback);

	static FString GenerateAssetName(const FString& Text);
	static FString GetSoundGenDir();
};
