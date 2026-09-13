// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtr.h"

class SUECPMainWidget;
class UUECPAppBridge;
struct FAgentRunnerInstance;
struct FAgentProviderConfig;
enum class EAgentSourceView : uint8;

class UECPCORE_API IUECPAgentRunnerService
{
public:
	virtual ~IUECPAgentRunnerService() = default;

	virtual void InitializeShellRefs(TWeakPtr<SUECPMainWidget> Shell,
		TWeakObjectPtr<UUECPAppBridge> Bridge) = 0;

	virtual void StopAllAgents() = 0;

	virtual void StopAgentForChat(const FString& ChatID) = 0;

	virtual bool HasActiveAgents() const = 0;

	virtual void RefreshAgentView(const FAgentRunnerInstance& Inst) = 0;
	virtual void SaveAgentChatHistory(const FAgentRunnerInstance& Inst) = 0;
	virtual void RefreshAgentLiveMessage(FAgentRunnerInstance& Inst, bool bProcessing) = 0;
	virtual void OnAgentConfirmAction(const FString& Decision) = 0;

	virtual bool OnAgentInstanceTick(TSharedPtr<FAgentRunnerInstance> Inst, float DeltaTime) = 0;
	virtual void SendQueryToInstance(FAgentRunnerInstance& Inst, FAgentProviderConfig& Config, const FString& Prompt) = 0;

	virtual void SpawnAgentInstance(const FString& ChatID, FAgentProviderConfig& Config, const FString& Prompt, EAgentSourceView SourceView) = 0;

	virtual void DiscoverAgentConfig(const FString& AgentId) = 0;

	virtual void NotifyInteractionModeChanged(const FString& ChatID) = 0;
};
