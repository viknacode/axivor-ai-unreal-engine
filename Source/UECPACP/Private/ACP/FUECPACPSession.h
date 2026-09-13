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

	EState GetState() const { return State; }
	bool   IsRunning() const { return State != EState::Uninit && State != EState::Closed; }
	const FString& GetSessionId() const { return SessionId; }
	const FString& GetAgentName() const { return AgentName; }
	const FString& GetAgentVersion() const { return AgentVersion; }

	DECLARE_DELEGATE_OneParam(FOnStreamEvent, const FUECPACPStreamEvent& );
	FOnStreamEvent OnStreamEvent;

	DECLARE_DELEGATE_OneParam(FOnAgentConfig, const UECPACPSchema::FConfigSnapshot& );
	FOnAgentConfig OnAgentConfig;

	DECLARE_DELEGATE_OneParam(FOnAuthRequired, const FString& );
	FOnAuthRequired OnAuthRequired;

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

	int64 CurrentPromptId = -1;

	TMap<FString, TSharedPtr<FJsonValue>> PendingPermissionIds;

	TSet<int64> PendingPrefsIds;

	FString QueuedPromptFrame;
	int64   QueuedPromptId = -1;

	TMap<FString, TPair<FString, FString>> CapturedToolArgs;
};
