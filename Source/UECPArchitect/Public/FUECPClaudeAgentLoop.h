// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "FUECPAgentLoopBase.h"

class UECPARCHITECT_API FUECPClaudeAgentLoop
	: public FUECPAgentLoopBase
	, public TSharedFromThis<FUECPClaudeAgentLoop>
{
public:
	FUECPClaudeAgentLoop()  = default;
	~FUECPClaudeAgentLoop() = default;

	virtual void Start() override;
	virtual void TriggerPendingSendRound() override;

	void SkipPausedDispatch();

	static TArray<TSharedPtr<FJsonValue>> BuildToolDefinitions();

	static TArray<TSharedPtr<FJsonValue>> ConvertHistoryEntryToAnthropic(
		const TSharedPtr<FJsonValue>& GeminiEntry);

private:
	struct FContentBlock
	{
		int32   Index     = 0;
		FString Type;
		FString Id;
		FString ToolName;
		FString InputAccum;
		FString TextAccum;
	};
	TMap<int32, FContentBlock> CurrentBlocks;
	FString                    CurrentStopReason;
	FString                    AccumulatedText;

	/** Set by an SSE `error` event whose type is transient (overloaded / api / rate-limit) so the
	 *  completion handler can retry instead of failing the turn. */
	bool                       bStreamTransientError = false;
	FString                    StreamErrorDetail;

	virtual void SendRound() override;
	virtual TWeakPtr<FUECPAgentLoopBase> AsWeakBase() override
	{
		return TWeakPtr<FUECPAgentLoopBase>(AsShared());
	}
	void OnRequestComplete(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful);
	virtual void DrainSseBuffer(const FString& NewData) override;
	void ProcessSseEvent(const FString& EventType, const FString& DataJson);
	void DispatchToolsAndLoop();
	void AppendAssistantMessage();
	void AppendToolResults(const TArray<TTuple<FString, FString, bool>>& Results);

	TArray<int32> PausedDispatchRemainingIndices;
	TArray<TTuple<FString, FString, bool>> PausedDispatchPartialResults;

	void DispatchOneTool(TArray<int32> Indices, int32 NextIndex,
		TArray<TTuple<FString, FString, bool>> Results);

	void FinaliseDispatchAndContinue(TArray<TTuple<FString, FString, bool>>& Results);
};
