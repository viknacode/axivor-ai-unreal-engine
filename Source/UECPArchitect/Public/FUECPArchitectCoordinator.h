// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "Interfaces/IHttpRequest.h"
#include "Services/IUECPArchitectService.h"
#include "FUECPClaudeAgentLoop.h"
#include "FUECPOpenAIAgentLoop.h"
#include "FUECPGeminiAgentLoop.h"

class UUECPAppBridge;
class SUECPMainWidget;

class UECPARCHITECT_API FUECPArchitectCoordinator final
	: public IUECPArchitectService
	, public TSharedFromThis<FUECPArchitectCoordinator>
{
public:
	FUECPArchitectCoordinator() = default;

	virtual void InitializeShellRefs(TWeakPtr<SUECPMainWidget> InShell,
		TWeakObjectPtr<UUECPAppBridge> InBridge) override;

	virtual void SendMessage(const FString& Message) override;
	virtual void SendMessageInterrupt(const FString& Message) override;
	virtual void StopGeneration() override;
	virtual void StopGenerationForChat(const FString& ChatID) override;
	virtual void StopAllGeneration() override;
	virtual bool DrainQueueForChat(const FString& ChatID) override;
	virtual void NewChat() override;
	virtual void SwitchChat(const FString& ChatID) override;
	virtual void AttachImage() override;
	virtual void ImportFileContext() override;
	virtual void SetPendingMessage(const FString& Message) override;

	virtual void SetInteractionMode(const FString& ModeKey) override;
	virtual void SetInteractionModeForChat(const FString& ChatID, const FString& ModeKey) override;
	virtual void ConfirmTool(const FString& Action) override;

	virtual void SendChatRequest() override;
	virtual void OnApiResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful) override;
	virtual void HandleStreamingProgress(FHttpRequestPtr Request, uint64 BytesSent, uint64 BytesReceived) override;

	virtual void ClearNativeHistoryForChat(const FString& ChatID) override;
	virtual void InvalidatePromptCaches() override;
	virtual void OnPromptAssetUpdated(const FString& AssetName) override;
	virtual FString GetHandleReferenceSections(const FString& SectionsCSV) const override;
	virtual FString GetSystemPrompt() override;
	virtual int32 GetCachedStaticPromptChars() const override;
	virtual FString BuildFullSystemPrompt(EAIInteractionMode Mode, const FString& ChatID, bool bIsCliAgent = false) override;

	virtual const TArray<TSharedPtr<FJsonValue>>& GetConversationHistory() const override;
	virtual const TArray<TSharedPtr<FConversationInfo>>& GetConversationList() const override;
	virtual const FString& GetActiveChatID() const override;
	virtual bool IsThinking() const override;
	virtual TArray<TSharedPtr<FJsonValue>> GetConversationHistoryForChat(const FString& ChatID) const override;
	virtual bool IsThinkingForChat(const FString& ChatID) const override;
	virtual int32 GetQueuedMessageCountForChat(const FString& ChatID) const override;
	virtual EAIInteractionMode GetActiveInteractionMode() const override;

	virtual bool MaybeApplyCachedTemplate(const TSharedPtr<FJsonObject>& Args,
		FString& OutJsonString, FString& OutError) override;

	virtual const TArray<TSharedPtr<FJsonValue>>* GetStreamingDisplayHistory(const FString& ChatID) const override
	{
		return NativeDisplayHistoryByChat.Find(ChatID);
	}

	virtual void                   SendMessageToChat(const FString& ChatID, const FString& Message, int32 ApiKeySlotIndex = -1) override;
	virtual void                   SetChatDisplayName(const FString& ChatID, const FString& DisplayName) override;
	virtual FOnArchitectTurnEnded& OnTurnEnded() override { return TurnEndedEvent; }
	virtual TArray<FUECPToolCatalogEntry> GetVisibleToolCatalog() const override;

	virtual bool RegisterAsyncTask(const FUECPAsyncTaskInfo& Info) override;
	virtual bool ResolveAsyncTask (const FString& TaskId, const FString& ResultJsonString, bool bSuccess) override;
	virtual bool CancelAsyncTask  (const FString& TaskId) override;
	virtual TArray<FUECPAsyncTaskInfo> GetPendingAsyncTasksForChat(const FString& ChatId) const override;

	void BroadcastTurnEnded(const FString& ChatID, bool bSuccess);

private:
	FOnArchitectTurnEnded          TurnEndedEvent;
	TWeakPtr<SUECPMainWidget>      Shell;
	TWeakObjectPtr<UUECPAppBridge> Bridge;

	TMap<FString, TSharedPtr<FUECPClaudeAgentLoop>> NativeLoopsByChat;
	TMap<FString, TArray<TSharedPtr<FJsonValue>>>   NativeHistoryByChat;
	TMap<FString, TMap<FString, int32>>             NativeLoopToolBubbleMapByChat;

	TMap<FString, TArray<TSharedPtr<FJsonValue>>>   NativeDisplayHistoryByChat;

	TMap<FString, TSharedPtr<FUECPOpenAIAgentLoop>> OpenAILoopsByChat;
	TMap<FString, TArray<TSharedPtr<FJsonValue>>>   OpenAIHistoryByChat;

	TMap<FString, TSharedPtr<FUECPGeminiAgentLoop>> GeminiLoopsByChat;
	TMap<FString, TArray<TSharedPtr<FJsonValue>>>   GeminiHistoryByChat;

	struct FArchitectStreamingState
	{
		FString ChatID;
		FString Provider;
		FString PendingSseBuffer;
		FString AggregatedMessage;
		FString RawResponseText;
		bool bSawDone = false;
		mutable FCriticalSection Mutex;
	};

	TMap<IHttpRequest*, TSharedPtr<FArchitectStreamingState>> ArchitectStreamingStates;

	struct FArchitectRequestMetrics
	{
		FString ChatID;
		FString Provider;
		double RequestStartTime = 0.0;
		double FirstByteTime = 0.0;
		double PromptBuildMs = 0.0;
		int32 RequestChars = 0;
		int32 EstimatedTokens = 0;
	};

	TMap<IHttpRequest*, FArchitectRequestMetrics> ArchitectRequestMetrics;

	mutable FString CachedHandleCheatsheet;
	mutable bool bHandleCheatsheetCacheValid = false;
	mutable TMap<FString, FString> CachedCheatsheetSections;
	mutable bool bCheatsheetSectionsParsed = false;

	FString CachedStaticSystemPrompt;
	uint32 CachedStaticPromptHash = 0;
	bool bStaticPromptCacheValid = false;
	FString CachedProviderForPrompt;
	EAIInteractionMode CachedInteractionMode = EAIInteractionMode::AutoEdit;
	int32 CachedMaxBatchSizeForPrompt = 5;

	TSharedPtr<FUECPClaudeAgentLoop>  FindNativeLoop(const FString& ChatID) const
	{
		const TSharedPtr<FUECPClaudeAgentLoop>* P = NativeLoopsByChat.Find(ChatID);
		return P ? *P : nullptr;
	}
	TSharedPtr<FUECPOpenAIAgentLoop>  FindOpenAILoop(const FString& ChatID) const
	{
		const TSharedPtr<FUECPOpenAIAgentLoop>* P = OpenAILoopsByChat.Find(ChatID);
		return P ? *P : nullptr;
	}
	TSharedPtr<FUECPGeminiAgentLoop>  FindGeminiLoop(const FString& ChatID) const
	{
		const TSharedPtr<FUECPGeminiAgentLoop>* P = GeminiLoopsByChat.Find(ChatID);
		return P ? *P : nullptr;
	}

	TMap<FString, int32>                          ArchitectAutoContinueCountByChat;
	static constexpr int32 AutoContinueLimit = 2;

	bool                                          bAutoContinuingArchitect = false;

	bool MaybeAutoContinueArchitect(const FString& ChatID, EAIInteractionMode Mode);

	bool FireSyntheticContinue(const FString& ChatID, EAIInteractionMode Mode);

	/** Appends Text as a synthetic user turn (display + request history) and re-sends the chat. */
	bool FireSyntheticUserMessage(const FString& ChatID, EAIInteractionMode Mode, const FString& Text);

	TMap<FString, int32>                          ArchitectAutoValidatePassByChat;

	bool RunAutoValidateForChat(const TSharedPtr<class SUECPMainWidget>& W, const FString& ChatID, bool bActive, EAIInteractionMode Mode);

	/**
	 * Post-turn verification gate (blueprint health / pending inspection / unsaved level).
	 * AutoEdit and AskBeforeEdit only. Returns true when a synthetic feedback turn was fired.
	 */
	bool RunVerificationGateForChat(const TSharedPtr<class SUECPMainWidget>& W, const FString& ChatID, bool bActive, EAIInteractionMode Mode);

	TMap<FString, double>                         LastStreamSaveTimeByChat;
	void MaybeAutoSaveStreamingChat(const FString& ChatID);

	TMap<FString , FUECPAsyncTaskInfo>  PendingAsyncTasks;

	bool    bNativeConfirmPending  = false;
	FString NativeConfirmChatID;
	bool    bNativeConfirmAllowed  = false;

	TSet<FString> GuideAutopilotApprovedChats;

	static TArray<TSharedPtr<FJsonValue>>        EmptyHistory;
	static TArray<TSharedPtr<FConversationInfo>> EmptyList;
	static FString                               EmptyChatID;
	static TMap<FString, FString>                EmptyCheatsheetSections;

	TOptional<bool> ApplyGuideAutopilotGate(class SUECPMainWidget* W, const FString& ToolName,
		FName DispatchName, const TSharedPtr<class FJsonObject>& Args, const FString& CapChatID);

	FString GenerateSystemPrompt();
	bool    ShouldInvalidateStaticCache() const;
	FString GetCachedStaticSystemPrompt();
	FString GenerateStaticSystemPromptInternal();
	FString GetCachedHandleCheatsheet() const;
	const TMap<FString, FString>& GetCheatsheetSections() const;
};
