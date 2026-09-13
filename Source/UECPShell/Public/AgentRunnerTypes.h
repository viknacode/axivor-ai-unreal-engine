// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once
#include "CoreMinimal.h"
#include "Containers/Ticker.h"

class FUECPACPSession;

enum class EAgentSourceView : uint8
{
	Architect,
	ProjectScanner
};

struct FAgentProviderConfig
{

	FString ProviderName;

	FString AgentId;

	FString ACPCommand;

	TArray<FString> ACPArgs;

	FString DefaultModel;
	FString DefaultThinkingEffort;
	FString ThinkingLabel;

	TArray<TSharedPtr<FString>> ModelOptions;
	TArray<TSharedPtr<FString>> ThinkingOptions;

	FString ModelName;
	FString ThinkingEffort;
};

struct FAgentRunnerInstance
{
	FString ChatID;
	FString ProviderName;
	EAgentSourceView SourceView = EAgentSourceView::Architect;

	FTSTicker::FDelegateHandle TickerHandle;
	bool bStarted = false;
	bool bStarting = false;

	FString TextAccumulator;
	FString ThinkingContent;
	bool bLiveMessageActive = false;
	bool bThinkingInProgress = false;
	int32 ThinkingHistoryIndex = -1;
	FString ThinkingBlockId;
	TMap<FString, int32> ToolBubbleMap;
	TSet<FString> HiddenToolUseIds;
	TArray<TSharedPtr<FJsonValue>> PendingRequestHistory;

	double LastRefreshTime = 0.0;
	bool bHasImportantUpdate = false;

	int32 QueryInputTokens = 0;
	int32 QueryOutputTokens = 0;

	bool bConfirmPending = false;
	FString ConfirmToolName;
	FString ConfirmPreview;

	FString ConfirmToolCallId;

	FString ConfirmProceedOptionId;

	TSharedPtr<FUECPACPSession> ACPSession;

	FString PendingACPPrompt;

	TArray<TPair<FString, FString>> PendingACPImages;

	bool bPrefsApplied = false;

	FString PendingModeChangeNotice;

	TMap<FString, FString> PendingToolActions;

	TArray<TSharedPtr<FJsonValue>> LocalHistory;

	void ResetStreamingState()
	{
		bLiveMessageActive = false;
		TextAccumulator.Empty();
		ThinkingContent.Empty();
		bThinkingInProgress = false;
		ThinkingHistoryIndex = -1;
		ThinkingBlockId.Empty();
		ToolBubbleMap.Empty();
		HiddenToolUseIds.Empty();
		QueryInputTokens = 0;
		QueryOutputTokens = 0;
		bConfirmPending = false;
		ConfirmToolName.Empty();
		ConfirmPreview.Empty();
		bHasImportantUpdate = false;
		LastRefreshTime = 0.0;
	}
};
