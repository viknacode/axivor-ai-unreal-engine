// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "FUECPAgentLoopBase.h"

class UECPARCHITECT_API FUECPOpenAIAgentLoop
	: public FUECPAgentLoopBase
	, public TSharedFromThis<FUECPOpenAIAgentLoop>
{
public:

	FString ProviderName;

	TMap<FString, FString> ExtraHeaders;

	FString ChooseTaskTier() const;

	FUECPOpenAIAgentLoop()  = default;
	~FUECPOpenAIAgentLoop() = default;

	virtual void Start() override;
	virtual void TriggerPendingSendRound() override;

	void SkipPausedDispatch();

	static TArray<TSharedPtr<FJsonValue>> BuildToolDefinitions();

	static TArray<TSharedPtr<FJsonValue>> ConvertHistoryEntryToOpenAI(
		const TSharedPtr<FJsonValue>& GeminiEntry);

private:
	struct FToolCallAccum
	{
		FString Id;
		FString Name;
		FString ArgumentsAccum;
	};
	TMap<int32, FToolCallAccum> PendingToolCalls;

	FString TextAccum;
	FString FinishReason;

	FString ReasoningAccum;

	bool    bInThinkBlock = false;
	FString PendingContentTail;

	virtual void SendRound() override;
	virtual TWeakPtr<FUECPAgentLoopBase> AsWeakBase() override
	{
		return TWeakPtr<FUECPAgentLoopBase>(AsShared());
	}
	void OnRequestComplete(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful);
	virtual void DrainSseBuffer(const FString& NewData) override;
	void ProcessSseData(const FString& DataJson);
	void DispatchToolsAndLoop();

	TArray<int32> PausedDispatchRemainingIndices;
	TArray<TTuple<FString, FString, bool>> PausedDispatchPartialResults;

	void DispatchOneTool(TArray<int32> Indices, int32 NextIndex,
		TArray<TTuple<FString, FString, bool>> Results);

	void FinaliseDispatchAndContinue(TArray<TTuple<FString, FString, bool>>& Results);
	void AppendAssistantMessage();
	void AppendToolResults(const TArray<TTuple<FString, FString, bool>>& Results);
};
