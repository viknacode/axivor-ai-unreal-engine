// Copyright 2026, BlueprintsLab, All rights reserved

#include "Managers/UECPChatProvider.h"

#include "ApiKeyManager.h"
#include "Managers/HttpCommunicationManager.h"

namespace UECPChatProvider
{
	namespace
	{
		TSharedPtr<FJsonObject> MakeUserMessage(const FString& Prompt)
		{
			TSharedPtr<FJsonObject> Msg = MakeShared<FJsonObject>();
			Msg->SetStringField(TEXT("role"), TEXT("user"));
			Msg->SetStringField(TEXT("content"), Prompt);
			return Msg;
		}

		// OpenAI-compatible body: { model, max_tokens, messages:[{role:user, content}] }.
		TSharedPtr<FJsonObject> MakeOpenAiStyleBody(const FString& Model, const FString& Prompt, int32 MaxTokens)
		{
			TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
			Body->SetStringField(TEXT("model"), Model);
			Body->SetNumberField(TEXT("max_tokens"), MaxTokens);
			TArray<TSharedPtr<FJsonValue>> Messages;
			Messages.Add(MakeShared<FJsonValueObject>(MakeUserMessage(Prompt)));
			Body->SetArrayField(TEXT("messages"), Messages);
			return Body;
		}
	}

	bool BuildCompletionRequest(const FApiKeySlot& Slot, const FString& UserPrompt, int32 MaxTokens,
		FCompletionRequest& Out, FString& OutError)
	{
		const FString Provider = Slot.Provider;
		const FString ApiKey   = Slot.ApiKey;

		if (ApiKey.IsEmpty())
		{
			OutError = TEXT("Active slot has no API key.");
			return false;
		}

		Out.Provider = Provider;
		Out.Headers.Add(TEXT("Content-Type"), TEXT("application/json"));

		if (Provider == TEXT("OpenAI"))
		{
			Out.ResolvedModel = Slot.OpenAIModel.IsEmpty() ? TEXT("gpt-4o-mini") : Slot.OpenAIModel;
			Out.Endpoint      = FHttpCommunicationManager::DefaultOpenAIUrl;
			Out.Headers.Add(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *ApiKey));
			Out.Body          = MakeOpenAiStyleBody(Out.ResolvedModel, UserPrompt, MaxTokens);
			return true;
		}

		if (Provider == TEXT("Claude"))
		{
			Out.ResolvedModel = Slot.ClaudeModel.IsEmpty() ? TEXT("claude-3-5-haiku-20241022") : Slot.ClaudeModel;
			Out.Endpoint      = FHttpCommunicationManager::DefaultClaudeUrl;
			Out.Headers.Add(TEXT("x-api-key"), ApiKey);
			Out.Headers.Add(TEXT("anthropic-version"), TEXT("2023-06-01"));
			Out.Body          = MakeOpenAiStyleBody(Out.ResolvedModel, UserPrompt, MaxTokens);
			return true;
		}

		if (Provider == TEXT("Gemini"))
		{
			Out.ResolvedModel = Slot.GeminiModel.IsEmpty() ? TEXT("gemini-2.0-flash") : Slot.GeminiModel;
			Out.Endpoint      = FHttpCommunicationManager::BuildGeminiUrl(Out.ResolvedModel, ApiKey);

			TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
			TSharedPtr<FJsonObject> ContentObj = MakeShared<FJsonObject>();
			ContentObj->SetStringField(TEXT("role"), TEXT("user"));
			TSharedPtr<FJsonObject> PartObj = MakeShared<FJsonObject>();
			PartObj->SetStringField(TEXT("text"), UserPrompt);
			TArray<TSharedPtr<FJsonValue>> Parts;
			Parts.Add(MakeShared<FJsonValueObject>(PartObj));
			ContentObj->SetArrayField(TEXT("parts"), Parts);
			TArray<TSharedPtr<FJsonValue>> Contents;
			Contents.Add(MakeShared<FJsonValueObject>(ContentObj));
			Body->SetArrayField(TEXT("contents"), Contents);
			Body->SetObjectField(TEXT("generationConfig"), MakeShared<FJsonObject>());
			Out.Body = Body;
			return true;
		}

		if (Provider == TEXT("Custom") && !Slot.CustomBaseURL.IsEmpty())
		{
			Out.ResolvedModel = Slot.CustomModelName;
			Out.Endpoint      = Slot.CustomBaseURL;
			Out.Headers.Add(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *ApiKey));
			Out.Body          = MakeOpenAiStyleBody(Out.ResolvedModel, UserPrompt, MaxTokens);
			return true;
		}

		OutError = FString::Printf(TEXT("Unsupported provider '%s'."), *Provider);
		return false;
	}

	bool ExtractCompletionText(const TSharedPtr<FJsonObject>& Response, FString& OutText)
	{
		if (!Response.IsValid()) return false;

		// Gemini: candidates[0].content.parts[0].text
		const TArray<TSharedPtr<FJsonValue>>* Candidates = nullptr;
		if (Response->TryGetArrayField(TEXT("candidates"), Candidates) && Candidates && Candidates->Num() > 0)
		{
			if (TSharedPtr<FJsonObject> Cand = (*Candidates)[0]->AsObject())
			{
				const TSharedPtr<FJsonObject>* ContentObj = nullptr;
				if (Cand->TryGetObjectField(TEXT("content"), ContentObj) && ContentObj)
				{
					const TArray<TSharedPtr<FJsonValue>>* Parts = nullptr;
					if ((*ContentObj)->TryGetArrayField(TEXT("parts"), Parts) && Parts && Parts->Num() > 0)
					{
						if (TSharedPtr<FJsonObject> Part = (*Parts)[0]->AsObject())
						{
							if (Part->TryGetStringField(TEXT("text"), OutText) && !OutText.IsEmpty()) return true;
						}
					}
				}
			}
		}

		// Claude: content[0].text
		const TArray<TSharedPtr<FJsonValue>>* Content = nullptr;
		if (Response->TryGetArrayField(TEXT("content"), Content) && Content && Content->Num() > 0)
		{
			if (TSharedPtr<FJsonObject> First = (*Content)[0]->AsObject())
			{
				if (First->TryGetStringField(TEXT("text"), OutText) && !OutText.IsEmpty()) return true;
			}
		}

		// OpenAI / Custom: choices[0].message.content
		const TArray<TSharedPtr<FJsonValue>>* Choices = nullptr;
		if (Response->TryGetArrayField(TEXT("choices"), Choices) && Choices && Choices->Num() > 0)
		{
			if (TSharedPtr<FJsonObject> Choice = (*Choices)[0]->AsObject())
			{
				const TSharedPtr<FJsonObject>* MsgPtr = nullptr;
				if (Choice->TryGetObjectField(TEXT("message"), MsgPtr) && MsgPtr && MsgPtr->IsValid())
				{
					if ((*MsgPtr)->TryGetStringField(TEXT("content"), OutText) && !OutText.IsEmpty()) return true;
				}
			}
		}

		return false;
	}
}
