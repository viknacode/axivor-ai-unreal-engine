// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"

enum class EConversationViewType : uint8
{
	Analyst,
	Project,
	Architect
};

struct FConversationInfo
{
	FString ID;
	FString Title;
	FString LastUpdated;

	int32 TotalPromptTokens = 0;
	int32 TotalCompletionTokens = 0;
	int32 TotalTokens = 0;

	FString Mode;

	int32 ApiKeySlotIndex = -1;
};

class FChatHistoryManager
{
public:
	static UECPCORE_API FChatHistoryManager& Get();

	static UECPCORE_API FString GetViewFolder(EConversationViewType ViewType);

	UECPCORE_API TArray<TSharedPtr<FConversationInfo>> LoadManifest(EConversationViewType ViewType);
	UECPCORE_API void SaveManifest(EConversationViewType ViewType, const TArray<TSharedPtr<FConversationInfo>>& Conversations);

	UECPCORE_API TArray<TSharedPtr<FJsonValue>> LoadChatHistory(EConversationViewType ViewType, const FString& ChatID);
	UECPCORE_API void SaveChatHistory(EConversationViewType ViewType, const FString& ChatID, const TArray<TSharedPtr<FJsonValue>>& History);

	static UECPCORE_API int32 EstimateConversationTokens(const TArray<TSharedPtr<FJsonValue>>& ConversationHistory);

	static UECPCORE_API FString ExportConversationToFile(
		const FString& ChatID, const FString& Title, const FString& FileFormat,
		const TArray<TSharedPtr<FJsonValue>>& ConversationHistory);

	UECPCORE_API void FlushPendingChatSaves();

private:
	FChatHistoryManager();
	FChatHistoryManager(const FChatHistoryManager&) = delete;
	FChatHistoryManager& operator=(const FChatHistoryManager&) = delete;

	void EnqueueChatWrite(const FString& FilePath, FString&& Content);

	mutable FCriticalSection ChatWriteLock;
	TMap<FString, FString> ChatPendingWrites;
	TSet<FString> ChatWritesInFlight;
};
