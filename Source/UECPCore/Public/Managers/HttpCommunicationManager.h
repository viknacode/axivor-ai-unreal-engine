// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "Interfaces/IHttpRequest.h"

class FHttpCommunicationManager
{
public:
	static UECPCORE_API FHttpCommunicationManager& Get();

	static UECPCORE_API FString BuildGeminiUrl(const FString& Model, const FString& ApiKey);
	static UECPCORE_API FString BuildOpenAIUrl();
	static UECPCORE_API FString BuildClaudeUrl();
	static UECPCORE_API FString BuildDeepSeekUrl();
	static UECPCORE_API FString BuildCustomUrl(const FString& BaseUrl);

	static UECPCORE_API const FString DefaultOpenAIUrl;
	static UECPCORE_API const FString DefaultClaudeUrl;
	static UECPCORE_API const FString DefaultDeepSeekUrl;
	static UECPCORE_API const FString DefaultGeminiBaseUrl;

private:
	FHttpCommunicationManager() = default;
	FHttpCommunicationManager(const FHttpCommunicationManager&) = delete;
	FHttpCommunicationManager& operator=(const FHttpCommunicationManager&) = delete;
};
