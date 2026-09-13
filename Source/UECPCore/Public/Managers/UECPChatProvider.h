// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

struct FApiKeySlot;

// Centralizes the per-provider request construction and response-text extraction that was
// otherwise copy-pasted across the chat/agent coordinators (Architect, Analyst, Scanner,
// AiMemory). This first cut covers the single-user-turn "completion" shape used by the
// background helpers; the streaming/tool-loop paths can adopt the same builders incrementally.
namespace UECPChatProvider
{
	// A fully-resolved, provider-specific HTTP request. Apply Headers via SetHeader and
	// serialize Body into the request content.
	struct FCompletionRequest
	{
		FString                 Provider;
		FString                 Endpoint;
		FString                 ResolvedModel;
		TMap<FString, FString>  Headers;
		TSharedPtr<FJsonObject> Body;
	};

	// Builds a single-user-message completion request for the slot's provider.
	// Supported providers: OpenAI, Claude, Gemini, Custom (OpenAI-compatible base URL).
	// Returns false + OutError for unsupported providers or a missing key, so callers can
	// skip exactly as the hand-rolled fan-outs did.
	UECPCORE_API bool BuildCompletionRequest(
		const FApiKeySlot& Slot,
		const FString& UserPrompt,
		int32 MaxTokens,
		FCompletionRequest& Out,
		FString& OutError);

	// Extracts assistant text from a provider response, trying the Gemini
	// (candidates->content->parts), Claude (content[].text) and OpenAI (choices[].message.content)
	// shapes in turn. Returns false if none matched.
	UECPCORE_API bool ExtractCompletionText(const TSharedPtr<FJsonObject>& Response, FString& OutText);
}
