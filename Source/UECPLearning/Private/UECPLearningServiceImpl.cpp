// Copyright 2026, BlueprintsLab, All rights reserved

#include "UECPLearningServiceImpl.h"
#include "LearningManager.h"

bool FUECPLearningServiceImpl::IsInitialized() const
{
	return FLearningManager::Get().IsInitialized();
}

void FUECPLearningServiceImpl::TrackEvent(const FString& EventType,
	const TSharedPtr<FJsonObject>& Data)
{
	FLearningManager::Get().TrackEvent(EventType, Data);
}

void FUECPLearningServiceImpl::TrackToolCompletion(const FString& ToolName,
	const TSharedPtr<FJsonObject>& Args,
	bool bSuccess,
	const FString& ResultJson,
	const FString& ErrorMessage,
	const FString& Source)
{
	FLearningManager::Get().TrackToolCompletion(ToolName, Args, bSuccess, ResultJson, ErrorMessage, Source);
}

void FUECPLearningServiceImpl::WebsitePost(const FString& Endpoint,
	const TSharedPtr<FJsonObject>& Body,
	TFunction<void(bool, TSharedPtr<FJsonObject>)> Callback)
{
	FLearningManager::Get().WebsitePost(Endpoint, Body, MoveTemp(Callback));
}

void FUECPLearningServiceImpl::MarkNodeComplete(const FString& NodeSlug)
{
	FLearningManager::Get().MarkNodeComplete(NodeSlug);
}

void FUECPLearningServiceImpl::IncrementPromptCount()
{
	FLearningManager::Get().IncrementPromptCount();
}
