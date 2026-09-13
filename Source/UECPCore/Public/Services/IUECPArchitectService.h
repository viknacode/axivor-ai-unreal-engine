// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "Interfaces/IHttpRequest.h"
#include "UObject/WeakObjectPtr.h"
#include "AssetReferenceTypes.h"
#include "Services/IUECPExtensionService.h"

class SUECPMainWidget;
class UUECPAppBridge;
struct FConversationInfo;
class FJsonValue;
class FJsonObject;

DECLARE_MULTICAST_DELEGATE_TwoParams(FOnArchitectTurnEnded, const FString& , bool );

struct FUECPAsyncTaskInfo
{
	FString  TaskId;
	FString  ChatId;
	FString  ToolName;
	FString  Label;
	FDateTime RegisteredAt;
};

class UECPCORE_API IUECPArchitectService
{
public:
	virtual ~IUECPArchitectService() = default;

	virtual void InitializeShellRefs(TWeakPtr<SUECPMainWidget> Shell,
		TWeakObjectPtr<UUECPAppBridge> Bridge) = 0;

	virtual void SendMessage(const FString& Message) = 0;
	virtual void StopGeneration() = 0;

	// "Send now": if the active chat is mid-turn, interrupt it (like Stop — tool calls that
	// already ran stay applied) and then send this message immediately as a fresh turn,
	// instead of queuing it. When the chat is idle it behaves like SendMessage.
	virtual void SendMessageInterrupt(const FString& Message) { SendMessage(Message); }

	virtual void StopGenerationForChat(const FString& ) {}

	virtual void StopAllGeneration() {}

	virtual bool DrainQueueForChat(const FString& ) { return false; }
	virtual void NewChat() = 0;
	virtual void SwitchChat(const FString& ChatID) = 0;
	virtual void AttachImage() = 0;
	virtual void ImportFileContext() = 0;

	virtual void SetPendingMessage(const FString& Message) = 0;

	virtual void SetInteractionMode(const FString& ModeKey) = 0;

	virtual void SetInteractionModeForChat(const FString& , const FString& ) {}

	virtual void ConfirmTool(const FString& Action) = 0;

	virtual void SendChatRequest() = 0;
	virtual void OnApiResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful) = 0;
	virtual void HandleStreamingProgress(FHttpRequestPtr Request, uint64 BytesSent, uint64 BytesReceived) = 0;

	virtual void ClearNativeHistoryForChat(const FString& ChatID) {}

	virtual void InvalidatePromptCaches() = 0;
	virtual void OnPromptAssetUpdated(const FString& AssetName) = 0;
	virtual FString GetHandleReferenceSections(const FString& SectionsCSV) const = 0;

	virtual FString GetSystemPrompt() = 0;

	virtual int32 GetCachedStaticPromptChars() const = 0;

	virtual FString BuildFullSystemPrompt(EAIInteractionMode Mode, const FString& ChatID, bool bIsCliAgent = false) = 0;

	virtual const TArray<TSharedPtr<FJsonValue>>& GetConversationHistory() const = 0;
	virtual const TArray<TSharedPtr<FConversationInfo>>& GetConversationList() const = 0;
	virtual const FString& GetActiveChatID() const = 0;
	virtual bool IsThinking() const = 0;

	virtual TArray<TSharedPtr<FJsonValue>> GetConversationHistoryForChat(const FString& ChatID) const = 0;

	virtual bool IsThinkingForChat(const FString& ChatID) const = 0;

	virtual int32 GetQueuedMessageCountForChat(const FString& ) const { return 0; }

	virtual EAIInteractionMode GetActiveInteractionMode() const = 0;

	virtual bool MaybeApplyCachedTemplate(const TSharedPtr<FJsonObject>& Args,
		FString& OutJsonString, FString& OutError) = 0;

	virtual const TArray<TSharedPtr<FJsonValue>>* GetStreamingDisplayHistory(const FString& ChatID) const { return nullptr; }

	virtual void SendMessageToChat(const FString& ChatID, const FString& Message, int32 ApiKeySlotIndex = -1) { SendMessage(Message); }

	virtual void SetChatDisplayName(const FString& ChatID, const FString& DisplayName) {}

	virtual FOnArchitectTurnEnded& OnTurnEnded()
	{
		static FOnArchitectTurnEnded Empty;
		return Empty;
	}

	virtual TArray<FUECPToolCatalogEntry> GetVisibleToolCatalog() const
	{
		return {};
	}

	virtual bool RegisterAsyncTask(const FUECPAsyncTaskInfo& ) { return false; }

	virtual bool ResolveAsyncTask(const FString& , const FString& , bool ) { return false; }

	virtual bool CancelAsyncTask(const FString& ) { return false; }

	virtual TArray<FUECPAsyncTaskInfo> GetPendingAsyncTasksForChat(const FString& ) const { return {}; }
};
