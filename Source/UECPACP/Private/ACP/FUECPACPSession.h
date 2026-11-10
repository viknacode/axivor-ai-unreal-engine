// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "HAL/PlatformProcess.h"
#include "ACP/FUECPACPJsonRpc.h"
#include "ACP/FUECPACPSchema.h"

class FJsonObject;
class FJsonValue;
struct FUECPACPStreamEvent;

class FUECPACPSession : public TSharedFromThis<FUECPACPSession>
{
public:
	FUECPACPSession() = default;
	~FUECPACPSession();

	enum class EState : uint8
	{
		Uninit,
		Initializing,
		Ready,
		Prompting,
		Closing,
		Closed,
	};

	struct FSpawnArgs
	{

		FString Command;

		TArray<FString> Args;

		FString Cwd;

		TArray<UECPACPSchema::FMcpServerSpec> McpSpecs;

		FString LogTag;

		// Short-lived auth connection: offers terminal login and waits after `initialize`
		// instead of opening a session, so the caller picks authenticate, logout or session/new.
		bool bAuthConnection = false;

		// When set, the session first tries to reattach to this agent session (session/resume,
		// then session/load) so the agent keeps its full context after an editor restart or
		// crash. Falls back to session/new when the agent cannot resume it.
		FString ResumeSessionId;
	};

	bool Start(const FSpawnArgs& InArgs);

	bool Prompt(const FString& UserText);

	bool PromptRich(const TArray<UECPACPSchema::FPromptContentBlock>& Blocks);

	void Cancel();

	void ApplyUserPrefs(const FString& ModeId, const FString& ModelId,
		const TArray<TPair<FString, FString>>& OtherPrefs = {});

	void Stop();

	bool Tick();

	void RespondToPermission(const FString& ToolCallId, const FString& OptionId);

	void RequestSessionNew() { SendSessionNew(); }

	// Done receives an empty string on success, or the agent's error message.
	void SendAuthRequest(const FString& Method, const TSharedRef<FJsonObject>& Params,
		TFunction<void(const FString&)> Done);

	EState GetState() const { return State; }
	bool   IsRunning() const { return State != EState::Uninit && State != EState::Closed; }
	const FString& GetSessionId() const { return SessionId; }
	// True when the current session was reattached (resume/load) rather than freshly created.
	bool WasResumed() const { return bResumed; }
	const FString& GetAgentName() const { return AgentName; }
	const FString& GetAgentVersion() const { return AgentVersion; }

	DECLARE_DELEGATE_OneParam(FOnStreamEvent, const FUECPACPStreamEvent& );
	FOnStreamEvent OnStreamEvent;

	DECLARE_DELEGATE_OneParam(FOnAgentConfig, const UECPACPSchema::FConfigSnapshot& );
	FOnAgentConfig OnAgentConfig;

	DECLARE_DELEGATE_OneParam(FOnAuthRequired, const FString& );
	FOnAuthRequired OnAuthRequired;

	DECLARE_DELEGATE_ThreeParams(FOnAuthCapabilities, const TArray<FUECPACPAuthMethod>& , bool , bool );
	FOnAuthCapabilities OnAuthCapabilities;

	DECLARE_DELEGATE_ThreeParams(FOnAuthStatusUpdate, const FString& , const FString& , const FString& );
	FOnAuthStatusUpdate OnAuthStatusUpdate;

	DECLARE_DELEGATE_OneParam(FOnAuthSessionReady, const FString& );
	FOnAuthSessionReady OnAuthSessionReady;

	DECLARE_DELEGATE_TwoParams(FOnAuthSessionFailed, const FString& , int32 );
	FOnAuthSessionFailed OnAuthSessionFailed;

	// Fired once the session is usable: (sessionId, bResumed).
	DECLARE_DELEGATE_TwoParams(FOnSessionReady, const FString& , bool );
	FOnSessionReady OnSessionReady;

	DECLARE_DELEGATE_OneParam(FOnStderrLine, const FString& );
	FOnStderrLine OnStderrLine;

	using FPermissionOptionList = TArray<TPair<FString, FString>>;
	DECLARE_DELEGATE_FourParams(FOnPermissionRequest,
		const FString& , const FString& , const FString& ,
		const FPermissionOptionList& );
	FOnPermissionRequest OnPermissionRequest;

	DECLARE_DELEGATE_OneParam(FOnTurnComplete, const FString& );
	FOnTurnComplete OnTurnComplete;

	DECLARE_DELEGATE_OneParam(FOnClosed, int32 );
	FOnClosed OnClosed;

	DECLARE_DELEGATE_OneParam(FOnError, const FString& );
	FOnError OnError;

private:
	void SendInitialize();
	void SendSessionNew();
	// bUseResume: "session/resume" (no history replay); otherwise "session/load" (replays history,
	// which is swallowed because the chat already shows it).
	void SendSessionReattach(bool bUseResume);
	void FinishSessionReady(const TSharedPtr<FJsonObject>& Result);
	bool WriteFrame(const FString& Line);

	void HandleResponseFrame     (const TSharedPtr<FJsonObject>& Root);
	void HandleRequestFrame      (const TSharedPtr<FJsonObject>& Root);
	void HandleNotificationFrame (const TSharedPtr<FJsonObject>& Root);

	void HandleInitializeResult  (const TSharedPtr<FJsonObject>& Result);
	void HandleSessionNewResult  (const TSharedPtr<FJsonObject>& Result);
	void HandlePromptResult      (const TSharedPtr<FJsonObject>& Result);
	void HandlePermissionRequest (const TSharedPtr<FJsonValue>& Id, const TSharedPtr<FJsonObject>& Params);
	void HandleReverseRpcUnsupported(const TSharedPtr<FJsonValue>& Id, const FString& Method);
	void HandleSessionUpdate     (const TSharedPtr<FJsonObject>& NotifParams);

	void EnterError(const FString& Message);
	void EnterClosed();

	void MaybeFlushQueuedPrompt();

	FSpawnArgs Args;
	EState     State = EState::Uninit;
	FString    SessionId;
	bool       bResumed        = false;
	bool       bReplayingLoad  = false;
	bool       bAgentCanLoad   = false;
	bool       bAgentCanResume = false;
	FString    AgentName;
	FString    AgentVersion;
	int32      NegotiatedVersion = 0;

	FProcHandle Process;
	uint32      Pid = 0;
	void*       StdinWrite  = nullptr;
	void*       StdoutRead  = nullptr;
	void*       StderrRead  = nullptr;

	FUECPACPJsonRpc Codec;

	FString StderrBuffer;

	int32 NonJsonStdoutCount = 0;

	int32 PartialLineTickCount = 0;
	bool  bPartialLineWarned   = false;

	double SilentSinceSec = -1.0;
	bool   bSilenceWarned = false;

	TMap<int64, TFunction<void(const TSharedPtr<FJsonObject>&)>> Pending;

	TMap<int64, TFunction<void(const FString&, int32)>> PendingErrors;

	int64 CurrentPromptId = -1;

	TMap<FString, TSharedPtr<FJsonValue>> PendingPermissionIds;

	TSet<int64> PendingPrefsIds;

	FString QueuedPromptFrame;
	int64   QueuedPromptId = -1;

	TMap<FString, TPair<FString, FString>> CapturedToolArgs;
};
