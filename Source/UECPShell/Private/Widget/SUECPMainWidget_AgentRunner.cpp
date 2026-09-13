// Copyright 2026, BlueprintsLab, All rights reserved

#include "SUECPMainWidget.h"
#include "UECPCoreModule.h"
#include "Services/IUECPAgentRunnerService.h"
#include "Services/IUECPACPRegistryService.h"
#include "ApiKeyManager.h"
#include "AgentRunnerTypes.h"

TArray<TSharedPtr<FJsonValue>>& SUECPMainWidget::GetAgentHistory(const FAgentRunnerInstance& Inst)
{
	if (Inst.SourceView == EAgentSourceView::ProjectScanner)
		return ProjectConversationHistory;
	return const_cast<TArray<TSharedPtr<FJsonValue>>&>(Inst.LocalHistory);
}

void SUECPMainWidget::RefreshAgentView(const FAgentRunnerInstance& Inst)
{
	IUECPCoreModule::Get().GetAgentRunnerService().RefreshAgentView(Inst);
}

void SUECPMainWidget::SaveAgentChatHistory(const FAgentRunnerInstance& Inst)
{
	IUECPCoreModule::Get().GetAgentRunnerService().SaveAgentChatHistory(Inst);
}

FAgentProviderConfig* SUECPMainWidget::GetActiveAgentProvider()
{
	FApiKeySlot Slot = FApiKeyManager::Get().GetActiveSlot();
	FString ProviderStr = Slot.Provider;
	TSharedPtr<FAgentProviderConfig>* Found = AgentProviders.Find(ProviderStr);
	if (!Found) return nullptr;

	FAgentProviderConfig* Cfg = Found->Get();
	if (!Slot.AgentModel.IsEmpty())  Cfg->ModelName = Slot.AgentModel;
	if (!Slot.AgentEffort.IsEmpty()) Cfg->ThinkingEffort = Slot.AgentEffort;
	return Cfg;
}

void SUECPMainWidget::RefreshAgentProvidersFromRegistry()
{
	IUECPACPRegistryService& Reg = IUECPCoreModule::Get().GetACPRegistryService();

	AgentProviders.Empty();
	SlotAgentOptions.Reset();

	for (const FUECPACPAgentEntry& Entry : Reg.GetAgents())
	{
		if (!Reg.IsInstalled(Entry.Id)) continue;
		const FUECPACPInstallMarker Marker = Reg.GetInstallMarker(Entry.Id);
		if (Marker.AgentId.IsEmpty() || Marker.EntrypointCommand.IsEmpty()) continue;

		auto Cfg = MakeShared<FAgentProviderConfig>();
		Cfg->ProviderName = Entry.Id;
		Cfg->AgentId      = Entry.Id;
		Cfg->ACPCommand   = Marker.EntrypointCommand;
		Cfg->ACPArgs      = Marker.EntrypointArgs;
		Cfg->DefaultThinkingEffort = TEXT("medium");
		Cfg->ThinkingEffort        = Cfg->DefaultThinkingEffort;
		Cfg->ThinkingLabel         = TEXT("Thinking");
		AgentProviders.Add(Entry.Id, Cfg);
		SlotAgentOptions.Add(MakeShared<FString>(Entry.Name));
	}
}

FAgentRunnerInstance* SUECPMainWidget::GetActiveAgentInstance()
{
	TSharedPtr<FAgentRunnerInstance>* Found = AgentInstances.Find(ActiveArchitectChatID);
	return Found ? Found->Get() : nullptr;
}

FAgentRunnerInstance* SUECPMainWidget::GetAgentInstanceForChat(const FString& ChatID)
{
	TSharedPtr<FAgentRunnerInstance>* Found = AgentInstances.Find(ChatID);
	return Found ? Found->Get() : nullptr;
}

void SUECPMainWidget::SpawnAgentInstance(const FString& ChatID, FAgentProviderConfig& Config, const FString& Prompt, EAgentSourceView SourceView)
{
	IUECPCoreModule::Get().GetAgentRunnerService().SpawnAgentInstance(ChatID, Config, Prompt, SourceView);
}

void SUECPMainWidget::StopAgentInstance(const FString& ChatID)
{
	IUECPCoreModule::Get().GetAgentRunnerService().StopAgentForChat(ChatID);
}

void SUECPMainWidget::StopAllAgentInstances()
{
	IUECPCoreModule::Get().GetAgentRunnerService().StopAllAgents();
}

void SUECPMainWidget::SendQueryToInstance(FAgentRunnerInstance& Inst, FAgentProviderConfig& Config, const FString& Prompt)
{
	IUECPCoreModule::Get().GetAgentRunnerService().SendQueryToInstance(Inst, Config, Prompt);
}

bool SUECPMainWidget::OnAgentInstanceTick(TSharedPtr<FAgentRunnerInstance> Inst, float DeltaTime)
{
	return IUECPCoreModule::Get().GetAgentRunnerService().OnAgentInstanceTick(Inst, DeltaTime);
}

void SUECPMainWidget::RefreshAgentLiveMessage(FAgentRunnerInstance& Inst, bool bProcessing)
{
	IUECPCoreModule::Get().GetAgentRunnerService().RefreshAgentLiveMessage(Inst, bProcessing);
}

void SUECPMainWidget::OnAgentConfirmAction(const FString& Decision)
{
	IUECPCoreModule::Get().GetAgentRunnerService().OnAgentConfirmAction(Decision);
}
