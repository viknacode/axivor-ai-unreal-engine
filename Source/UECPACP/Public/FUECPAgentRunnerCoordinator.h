// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "Services/IUECPAgentRunnerService.h"

class UUECPAppBridge;
class SUECPMainWidget;

class UECPACP_API FUECPAgentRunnerCoordinator final : public IUECPAgentRunnerService
{
public:
	FUECPAgentRunnerCoordinator() = default;

	virtual void InitializeShellRefs(TWeakPtr<SUECPMainWidget> InShell,
		TWeakObjectPtr<UUECPAppBridge> InBridge) override;

	virtual void StopAllAgents() override;
	virtual void StopAgentForChat(const FString& ChatID) override;
	virtual bool HasActiveAgents() const override;

	virtual void RefreshAgentView(const FAgentRunnerInstance& Inst) override;
	virtual void SaveAgentChatHistory(const FAgentRunnerInstance& Inst) override;
	virtual void RefreshAgentLiveMessage(FAgentRunnerInstance& Inst, bool bProcessing) override;
	virtual void OnAgentConfirmAction(const FString& Decision) override;
	virtual bool OnAgentInstanceTick(TSharedPtr<FAgentRunnerInstance> Inst, float DeltaTime) override;
	virtual void SendQueryToInstance(FAgentRunnerInstance& Inst, FAgentProviderConfig& Config, const FString& Prompt) override;
	virtual void SpawnAgentInstance(const FString& ChatID, FAgentProviderConfig& Config, const FString& Prompt, EAgentSourceView SourceView) override;
	virtual void NotifyInteractionModeChanged(const FString& ChatID) override;

private:
	TWeakPtr<SUECPMainWidget>      Shell;
	TWeakObjectPtr<UUECPAppBridge> Bridge;

	void ProcessAgentMessage(FAgentRunnerInstance& Inst, const FString& JsonStr);

	void SpawnACPInstance(const FString& ChatID, FAgentProviderConfig& Config,
		const FString& Prompt, EAgentSourceView SourceView);
	bool OnACPInstanceTick(TSharedPtr<FAgentRunnerInstance> Inst);

public:

	void DiscoverAgentConfig(const FString& AgentId);
};
