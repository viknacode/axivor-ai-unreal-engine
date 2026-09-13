// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class UECPCORE_API IUECPLearningService
{
public:
	virtual ~IUECPLearningService() = default;

	virtual bool IsInitialized() const = 0;

	virtual void TrackEvent(const FString& EventType, const TSharedPtr<FJsonObject>& Data) = 0;

	virtual void TrackToolCompletion(const FString& ToolName,
		const TSharedPtr<FJsonObject>& Args,
		bool bSuccess,
		const FString& ResultJson,
		const FString& ErrorMessage,
		const FString& Source) = 0;

	virtual void WebsitePost(const FString& Endpoint,
		const TSharedPtr<FJsonObject>& Body,
		TFunction<void(bool, TSharedPtr<FJsonObject>)> Callback) = 0;

	virtual void MarkNodeComplete(const FString& NodeSlug) = 0;

	virtual void IncrementPromptCount() = 0;
};
