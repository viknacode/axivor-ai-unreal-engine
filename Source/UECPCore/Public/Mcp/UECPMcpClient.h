// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "HAL/PlatformProcess.h"
#include "Mcp/UECPMcpJsonRpc.h"
#include "Services/IUECPToolDispatcher.h"

DECLARE_LOG_CATEGORY_EXTERN(LogUECPMcpClient, Log, All);

struct UECPCORE_API FUECPMcpServerSpec
{

	FString Command;

	TArray<FString> Args;

	TArray<TPair<FString, FString>> Env;

	FString Cwd;

	FString Url;

	TArray<TPair<FString, FString>> Headers;

	bool bUseOAuth = false;

	FString OAuthKey;

	FString LogTag;

	bool IsHttp() const { return !Url.IsEmpty(); }
};

struct UECPCORE_API FUECPMcpToolDescriptor
{
	FString Name;
	FString Description;
	TSharedPtr<FJsonObject> InputSchema;

	bool bReadOnlyHint    = false;
	bool bDestructiveHint = false;
};

class UECPCORE_API FUECPMcpClient : public TSharedFromThis<FUECPMcpClient>
{
public:
	FUECPMcpClient() = default;
	~FUECPMcpClient();

	enum class EState : uint8
	{
		Uninit,
		Initializing,
		FetchingTools,
		Ready,
		Closing,
		Closed,
	};

	bool Start(const FUECPMcpServerSpec& Spec, FString& OutError, float InitTimeoutSec = 10.0f);

	void StartAsync(const FUECPMcpServerSpec& Spec,
		TFunction<void(bool , FString )> OnComplete,
		float InitTimeoutSec = 10.0f);

	void Stop();

	bool IsConnected() const { return State == EState::Ready; }

	EState GetState() const { return State; }

	const TArray<FUECPMcpToolDescriptor>& GetTools() const { return CachedTools; }

	const FString& GetServerName() const;

	FUECPToolResult CallTool(const FString& ToolName,
		const TSharedPtr<FJsonObject>& Args,
		float CallTimeoutSec = 30.0f);

private:
	bool SpawnProcess(FString& OutError);
	void DrainPipe();
	bool WriteFrame(const FString& Frame);

	bool SendAndAwaitStdio(int64 Id, const FString& Frame,
		float TimeoutSec, TSharedPtr<FJsonObject>& OutResult, FString& OutError);

	bool SendAndAwaitHttp(int64 Id, const FString& Frame,
		float TimeoutSec, TSharedPtr<FJsonObject>& OutResult, FString& OutError);

	bool SendAndAwait(int64 Id, const FString& Frame,
		float TimeoutSec, TSharedPtr<FJsonObject>& OutResult, FString& OutError);

	bool RoundTripInitialize(float TimeoutSec, FString& OutError);
	bool RoundTripToolsList (float TimeoutSec, FString& OutError);

	FUECPMcpServerSpec Spec;
	EState State = EState::Uninit;

	FProcHandle Process;
	uint32 Pid = 0;
	void* StdinWrite  = nullptr;
	void* StdoutRead  = nullptr;
	void* ChildStdin  = nullptr;
	void* ChildStdout = nullptr;
	FUECPMcpJsonRpc Codec;

	FString HttpSessionId;

	TMap<int64, TSharedPtr<FJsonObject>> PendingResponses;

	FString ServerName;
	FString ServerVersion;
	TArray<FUECPMcpToolDescriptor> CachedTools;
};
