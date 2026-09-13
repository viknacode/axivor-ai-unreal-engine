// Copyright 2026, BlueprintsLab, All rights reserved

#include "Managers/HttpCommunicationManager.h"

const FString FHttpCommunicationManager::DefaultOpenAIUrl = TEXT("https://api.openai.com/v1/chat/completions");
const FString FHttpCommunicationManager::DefaultClaudeUrl = TEXT("https://api.anthropic.com/v1/messages");
const FString FHttpCommunicationManager::DefaultDeepSeekUrl = TEXT("https://api.deepseek.com/v1/chat/completions");
const FString FHttpCommunicationManager::DefaultGeminiBaseUrl = TEXT("https://generativelanguage.googleapis.com/v1beta/models");

FHttpCommunicationManager& FHttpCommunicationManager::Get()
{
	static FHttpCommunicationManager Instance;
	return Instance;
}

FString FHttpCommunicationManager::BuildGeminiUrl(const FString& Model, const FString& ApiKey)
{
	return FString::Printf(TEXT("%s/%s:generateContent?key=%s"), *DefaultGeminiBaseUrl, *Model, *ApiKey);
}

FString FHttpCommunicationManager::BuildOpenAIUrl()
{
	return DefaultOpenAIUrl;
}

FString FHttpCommunicationManager::BuildClaudeUrl()
{
	return DefaultClaudeUrl;
}

FString FHttpCommunicationManager::BuildDeepSeekUrl()
{
	return DefaultDeepSeekUrl;
}

FString FHttpCommunicationManager::BuildCustomUrl(const FString& BaseUrl)
{
	FString TrimmedUrl = BaseUrl.TrimStartAndEnd();
	if (TrimmedUrl.IsEmpty())
	{
		return DefaultOpenAIUrl;
	}
	if (!TrimmedUrl.Contains(TEXT("anthropic.com"))
		&& !TrimmedUrl.Contains(TEXT("?"))
		&& !TrimmedUrl.EndsWith(TEXT("/chat/completions"))
		&& !TrimmedUrl.Contains(TEXT("/chat/completions?")))
	{
		TrimmedUrl.RemoveFromEnd(TEXT("/"));
		TrimmedUrl += TEXT("/chat/completions");
	}
	return TrimmedUrl;
}
