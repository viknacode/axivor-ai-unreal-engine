// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "FUECPAgentLoopBase.h"

class UECPARCHITECT_API FUECPGeminiAgentLoop
	: public FUECPAgentLoopBase
	, public TSharedFromThis<FUECPGeminiAgentLoop>
{
public:
	FUECPGeminiAgentLoop()  = default;
	~FUECPGeminiAgentLoop() = default;

	virtual void Start() override;
	virtual void TriggerPendingSendRound() override;

	void SkipPausedDispatch();

	static TArray<TSharedPtr<FJsonValue>> BuildToolDefinitions();

	static TSharedPtr<FJsonValue> FilterHistoryEntry(const TSharedPtr<FJsonValue>& Entry);

private:
	int32 ToolSeq = 0;

	struct FGeminiToolCall
	{
		FString AutoId;
		FString Name;
		FString ArgsJson;
	};
	TArray<FGeminiToolCall>          PendingToolCalls;
	TArray<TSharedPtr<FJsonObject>>  PendingRawNonTextParts;

	FString TextAccum;
	FString FinishReason;

	virtual void SendRound() override;
	virtual TWeakPtr<FUECPAgentLoopBase> AsWeakBase() override
	{
		return TWeakPtr<FUECPAgentLoopBase>(AsShared());
	}
	void OnRequestComplete(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful);
	virtual void DrainSseBuffer(const FString& NewData) override;
	void ProcessSseData(const FString& DataJson);
	void DispatchToolsAndLoop();
	void AppendModelTurn();
	void AppendFunctionResponses(const TArray<TTuple<FString, FString, bool>>& Results);

	TArray<int32> PausedDispatchRemainingIndices;
	TArray<TTuple<FString, FString, bool>> PausedDispatchPartialResults;

	void DispatchOneTool(TArray<int32> Indices, int32 NextIndex,
		TArray<TTuple<FString, FString, bool>> Results);

	void FinaliseDispatchAndContinue(TArray<TTuple<FString, FString, bool>>& Results);
};
