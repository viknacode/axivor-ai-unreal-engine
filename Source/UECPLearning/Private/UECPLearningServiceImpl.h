// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "Services/IUECPLearningService.h"

class FUECPLearningServiceImpl final : public IUECPLearningService
{
public:
	virtual bool IsInitialized() const override;

	virtual void TrackEvent(const FString& EventType,
		const TSharedPtr<FJsonObject>& Data) override;

	virtual void TrackToolCompletion(const FString& ToolName,
		const TSharedPtr<FJsonObject>& Args,
		bool bSuccess,
		const FString& ResultJson,
		const FString& ErrorMessage,
		const FString& Source) override;

	virtual void WebsitePost(const FString& Endpoint,
		const TSharedPtr<FJsonObject>& Body,
		TFunction<void(bool, TSharedPtr<FJsonObject>)> Callback) override;

	virtual void MarkNodeComplete(const FString& NodeSlug) override;

	virtual void IncrementPromptCount() override;
};
