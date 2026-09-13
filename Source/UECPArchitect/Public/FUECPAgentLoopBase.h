// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Interfaces/IHttpRequest.h"
#include "HAL/CriticalSection.h"
#include "Services/IUECPExtensionService.h"
#include "Services/IUECPToolDispatcher.h"
#include "AssetReferenceTypes.h"
#include <atomic>

class UECPARCHITECT_API FUECPAgentLoopBase
{
public:

	FString ApiKey;
	FString EndpointURL;
	FString ModelName;
	FString SystemPrompt;
	FString ChatID;

	EAIInteractionMode InteractionMode = EAIInteractionMode::AutoEdit;
	int32   MaxRounds         = 200;
	int32   FinalInputTokens  = 0;
	int32   FinalOutputTokens = 0;

	bool    bStalledNeedsContinue = false;

	TArray<TSharedPtr<FJsonValue>> History;

	TFunction<void(FHttpResponsePtr)>                                                        OnRoundResponse;

	TFunction<void(const FString& Chunk)>                                                    OnTextDelta;

	TFunction<void(const FString& ToolId, const FString& Label, const FString& ArgsPreview)> OnToolStart;

	TFunction<void(const FString& ToolId, const FString& Label, bool bSuccess, const FString& ResultJson)> OnToolResult;

	TFunction<void(bool bSuccess, const FString& ErrorMessage)>                              OnDone;

	TFunction<bool(const FString& ToolName, const TSharedPtr<FJsonObject>& Args, FName DispatchName, FString& OutDenyReason)> OnBeforeDispatch;

	TFunction<bool()> OnBeforeNextRound;

	TFunction<bool(const FString& ToolName, const TSharedPtr<FJsonObject>& Args, FUECPToolResult& Out)> OnWidgetTool;

	virtual ~FUECPAgentLoopBase() = default;

	virtual void Start() = 0;

	void Stop();

	bool IsRunning() const;

	virtual void TriggerPendingSendRound() {}

	static FString FormatHttpError(bool bWasSuccessful, int32 Code,
		const FString& Body, const FString& Provider);

	static bool ParseRetry429Delay(int32 Code, const FString& Body,
		FHttpResponsePtr Resp, float& OutDelaySec);

	static FString ExtractProviderErrorDetail(const FString& Body);

	static FString RecoverResponseBody(FHttpResponsePtr Response, const FString& StreamedBody);

	static FName ResolveDispatchName(const FString& ToolName,
		const TSharedPtr<FJsonObject>& Args);

	static bool IsUmbrellaName(const FString& Name);

	static TArray<FUECPToolCatalogEntry> GetStandardToolEntries();

	static bool TextEndsWithIntent(const FString& Text);

	/** Upper bound on the serialised request history (chars). Mirrors the coordinator's window. */
	static constexpr int32 MaxHistoryChars    = 320000;

	/** Per tool result cap (chars) before it is appended to the request history. */
	static constexpr int32 MaxToolResultChars = 16000;

	/** `[BpGeneratorUltimate] MaxOutputTokens` (default 16000, clamped 1024..64000). */
	static int32 ResolveMaxOutputTokens();

	/** `[BpGeneratorUltimate] RequestTimeoutSeconds` (default 600, clamped 60..3600). */
	static float ResolveRequestTimeoutSeconds();

	/** Truncates a tool result to MaxToolResultChars with a trailing marker the model can act on. */
	static FString CapToolResultText(const FString& In);

protected:

	enum class EState : uint8 { Idle, Streaming, Dispatching, Done };
	EState            State              = EState::Idle;
	std::atomic<bool> bStopped          { false };
	int32             Round             = 0;
	bool              bPendingSendRound = false;

	/** Consecutive transient failures (429/5xx/transport) for the current round. Reset on a 200. */
	int32             TransientRetryCount = 0;
	static constexpr int32 MaxTransientRetries = 3;

	int32             ToolCallsThisLoop = 0;

	FCriticalSection  SseMutex;
	FString           SseBuffer;

	FString           SseRemnant;

	FString           RawResponseBody;

	TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> ActiveRequest;

	void Finish(bool bSuccess, const FString& Error = FString());

	void ResetSseState();

	void OnStreamChunk(void* Ptr, int64& Length);

	void OnRequestProgress(FHttpRequestPtr Request, uint64 BytesSent, uint64 BytesReceived);

	virtual void DrainSseBuffer(const FString& NewData) = 0;

	/** Builds and sends one request for the current History. Implemented per provider. */
	virtual void SendRound() = 0;

	/** Weak self for deferred (ticker) callbacks; implemented via TSharedFromThis in each loop. */
	virtual TWeakPtr<FUECPAgentLoopBase> AsWeakBase() = 0;

	/** True for 429 / 500 / 502 / 503 / 504 / 529 and transport failures (no response). */
	static bool IsTransientHttpFailure(bool bWasSuccessful, int32 Code);

	/**
	 * Schedules a re-send of the current round with exponential backoff (1s, 3s, 8s + jitter)
	 * when the failure is transient and fewer than MaxTransientRetries attempts were made.
	 * bRoundHadOutput blocks the retry when text/tool deltas already reached the UI (a re-send
	 * would duplicate them). Returns true if a retry was scheduled — the caller must return.
	 */
	bool TryScheduleTransientRetry(bool bWasSuccessful, int32 Code, const FString& Body,
		FHttpResponsePtr Response, const FString& Provider, bool bRoundHadOutput);

	/** Applies the per-request timeout so a dropped connection cannot hang the loop forever. */
	void ConfigureRequestTimeout();

	/**
	 * Re-windows History to MaxHistoryChars before a request: elides the oldest tool results
	 * first (content replaced by a short {"elided":true,...} stub so tool_use/tool_result pairing
	 * stays valid), then the oldest assistant text. The most recent entries are never touched.
	 */
	void WindowHistoryForRequest();
};
