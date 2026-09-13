// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "Services/IUECPArchitectService.h"
#include "Services/IUECPAnalystService.h"
#include "Services/IUECPScannerService.h"
#include "Services/IUECPBugReportService.h"
#include "Services/IUECPAgentRunnerService.h"
#include "Services/IUECPACPRegistryService.h"
#include "Services/IUECPGddService.h"
#include "Services/IUECPMcpInfoService.h"
#include "Services/IUECPVoiceService.h"
#include "Services/IUECPAiMemoryService.h"
#include "Services/IUECPCrewService.h"
#include "Services/IUECPLearningService.h"
#include "Types/CrewTypes.h"
#include "Managers/ChatHistoryManager.h"
#include "Dom/JsonValue.h"

class FUECPNullArchitectService final : public IUECPArchitectService
{
public:
	virtual void InitializeShellRefs(TWeakPtr<SUECPMainWidget>, TWeakObjectPtr<UUECPAppBridge>) override {}
	virtual void SendMessage(const FString&) override {}
	virtual void StopGeneration() override {}
	virtual void NewChat() override {}
	virtual void SwitchChat(const FString&) override {}
	virtual void AttachImage() override {}
	virtual void ImportFileContext() override {}
	virtual void SetPendingMessage(const FString&) override {}
	virtual void SetInteractionMode(const FString&) override {}
	virtual void ConfirmTool(const FString&) override {}
	virtual void SendChatRequest() override {}
	virtual void OnApiResponseReceived(FHttpRequestPtr, FHttpResponsePtr, bool) override {}
	virtual void HandleStreamingProgress(FHttpRequestPtr, uint64, uint64) override {}
	virtual void InvalidatePromptCaches() override {}
	virtual void OnPromptAssetUpdated(const FString&) override {}
	virtual FString GetHandleReferenceSections(const FString&) const override { return FString(); }
	virtual const TArray<TSharedPtr<FJsonValue>>& GetConversationHistory() const override { return EmptyHistory; }
	virtual const TArray<TSharedPtr<FConversationInfo>>& GetConversationList() const override { return EmptyList; }
	virtual const FString& GetActiveChatID() const override { return EmptyChatID; }
	virtual bool IsThinking() const override { return false; }
	virtual TArray<TSharedPtr<FJsonValue>> GetConversationHistoryForChat(const FString&) const override { return TArray<TSharedPtr<FJsonValue>>(); }
	virtual bool IsThinkingForChat(const FString&) const override { return false; }
	virtual EAIInteractionMode GetActiveInteractionMode() const override { return EAIInteractionMode::AutoEdit; }
	virtual bool MaybeApplyCachedTemplate(const TSharedPtr<FJsonObject>&, FString&, FString&) override { return false; }
	virtual FString GetSystemPrompt() override { return FString(); }
	virtual int32 GetCachedStaticPromptChars() const override { return 0; }
	virtual FString BuildFullSystemPrompt(EAIInteractionMode, const FString&, bool) override { return FString(); }

private:
	TArray<TSharedPtr<FJsonValue>>          EmptyHistory;
	TArray<TSharedPtr<FConversationInfo>>   EmptyList;
	FString                                 EmptyChatID;
};

class FUECPNullAnalystService final : public IUECPAnalystService
{
public:
	virtual void InitializeShellRefs(TWeakPtr<SUECPMainWidget>, TWeakObjectPtr<UUECPAppBridge>) override {}
	virtual void SendMessage(const FString&) override {}
	virtual void StopGeneration() override {}
	virtual void NewChat() override {}
	virtual void AddAssetContext() override {}
	virtual void AddNodeContext() override {}
	virtual void ImportFileContext() override {}
	virtual void AttachImage() override {}
	virtual void RefreshChatHistoryView() override {}
	virtual void LoadChatHistory(const FString&) override {}
	virtual void SaveChatHistory(const FString&) override {}
	virtual void LoadManifest() override {}
	virtual void SaveManifest() override {}
	virtual void SelectChat(TSharedPtr<FConversationInfo>, bool) override {}
	virtual const TArray<TSharedPtr<FJsonValue>>& GetConversationHistory() const override { return EmptyHistory; }
	virtual const TArray<TSharedPtr<FConversationInfo>>& GetConversationList() const override { return EmptyList; }
	virtual const FString& GetActiveChatID() const override { return EmptyChatID; }
	virtual void ClearHistory() override {}

private:
	TArray<TSharedPtr<FJsonValue>>          EmptyHistory;
	TArray<TSharedPtr<FConversationInfo>>   EmptyList;
	FString                                 EmptyChatID;
};

class FUECPNullScannerService final : public IUECPScannerService
{
public:
	virtual void InitializeShellRefs(TWeakPtr<SUECPMainWidget>, TWeakObjectPtr<UUECPAppBridge>) override {}
	virtual void SendMessage(const FString&) override {}
	virtual void StopGeneration() override {}
	virtual void NewChat() override {}
	virtual void SwitchChat(const FString&) override {}
	virtual void AttachImage() override {}
	virtual void ScanProject() override {}
	virtual void ScanProjectAsync(FOnScanComplete OnDone) override
	{
		if (OnDone) OnDone(false, TEXT("Scanner service unavailable."));
	}
	virtual void GetProjectOverview() override {}
	virtual void GetPerformanceReport() override {}
	virtual void SendChatRequest() override {}
	virtual void OnApiResponseReceived(FHttpRequestPtr, FHttpResponsePtr, bool) override {}
	virtual bool QueryIndex(const FString&, FString&, FString& OutError) override
	{
		OutError = TEXT("Scanner service unavailable.");
		return false;
	}
	virtual const TArray<TSharedPtr<FJsonValue>>& GetConversationHistory() const override { return EmptyHistory; }
	virtual const TArray<TSharedPtr<FConversationInfo>>& GetConversationList() const override { return EmptyList; }
	virtual const FString& GetActiveChatID() const override { return EmptyChatID; }

private:
	TArray<TSharedPtr<FJsonValue>>          EmptyHistory;
	TArray<TSharedPtr<FConversationInfo>>   EmptyList;
	FString                                 EmptyChatID;
};

class FUECPNullBugReportService final : public IUECPBugReportService
{
public:
	virtual void InitializeShellRefs(TWeakPtr<SUECPMainWidget>, TWeakObjectPtr<UUECPAppBridge>) override {}
	virtual void Submit(const FString&, const FString&, const FString&) override {}
	virtual void AttachImageFromFile() override {}
	virtual void AttachImageFromClipboard() override {}
	virtual void ClearAttachedImages() override {}
};

class FUECPNullAgentRunnerService final : public IUECPAgentRunnerService
{
public:
	virtual void InitializeShellRefs(TWeakPtr<SUECPMainWidget>, TWeakObjectPtr<UUECPAppBridge>) override {}
	virtual void StopAllAgents() override {}
	virtual void StopAgentForChat(const FString&) override {}
	virtual bool HasActiveAgents() const override { return false; }
	virtual void RefreshAgentView(const FAgentRunnerInstance&) override {}
	virtual void SaveAgentChatHistory(const FAgentRunnerInstance&) override {}
	virtual void RefreshAgentLiveMessage(FAgentRunnerInstance&, bool) override {}
	virtual void OnAgentConfirmAction(const FString&) override {}
	virtual bool OnAgentInstanceTick(TSharedPtr<FAgentRunnerInstance>, float) override { return false; }
	virtual void SendQueryToInstance(FAgentRunnerInstance&, FAgentProviderConfig&, const FString&) override {}
	virtual void SpawnAgentInstance(const FString&, FAgentProviderConfig&, const FString&, EAgentSourceView) override {}
	virtual void DiscoverAgentConfig(const FString&) override {}
	virtual void NotifyInteractionModeChanged(const FString&) override {}
};

class FUECPNullACPRegistryService final : public IUECPACPRegistryService
{
public:
	virtual const TArray<FUECPACPAgentEntry>& GetAgents() const override { return EmptyCatalog; }
	virtual void RefreshCatalog(bool) override {}
	virtual bool IsInstalled(const FString&) const override { return false; }
	virtual FUECPACPInstallMarker GetInstallMarker(const FString&) const override { return {}; }
	virtual void InstallAgent(const FString&, FUECPACPInstallProgress, FUECPACPInstallComplete OnComplete) override
	{
		if (OnComplete.IsBound()) OnComplete.Execute(false, FUECPACPInstallMarker{});
	}
	virtual bool UninstallAgent(const FString&) override { return false; }
	virtual TArray<FUECPACPAgentEntry> GetInstalledAgentsFromDisk() const override { return {}; }
	virtual FOnCatalogChanged& OnCatalogChanged() override { return DummyDelegate; }

private:
	TArray<FUECPACPAgentEntry> EmptyCatalog;
	FOnCatalogChanged DummyDelegate;
};

class FUECPNullVoiceService final : public IUECPVoiceService
{
public:
	virtual void InitializeShellRefs(TWeakPtr<SUECPMainWidget>, TWeakObjectPtr<UUECPAppBridge>) override {}
	virtual void ToggleMic() override {}
	virtual void PlayTTSForResponse(const FString&) override {}
	virtual void SpeakAsync(const FString&, FName) override {}
	virtual void StopQueue(FName) override {}
	virtual void StopAll() override {}
	virtual FUECPNarrationFinishedDelegate& OnNarrationFinished() override { return DummyDelegate; }
	virtual FUECPVoiceSettingsSnapshot GetSettings() const override { return {}; }
	virtual void ApplySettings(const FUECPVoiceSettingsSnapshot&) override {}
	virtual bool IsEnabled() const override { return false; }
	virtual bool IsRecording() const override { return false; }

private:
	FUECPNarrationFinishedDelegate DummyDelegate;
};

class FUECPNullMcpInfoService final : public IUECPMcpInfoService
{
public:
	virtual int32   GetHttpPort() const override { return 0; }
	virtual FString GetSessionToken() const override { return FString(); }
	virtual bool    IsLanAccessEnabled() const override { return false; }
	virtual void    SetLanAccessEnabled(bool) override {}
	virtual void    RotateSessionToken() override {}
	virtual bool    IsBroker() const override { return false; }
	virtual int32   GetBrokerPort() const override { return 0; }
	virtual int32   GetConfiguredBrokerPort() const override { return 0; }
	virtual FUECPMcpBrokerStateChanged& OnBrokerStateChanged() override
	{
		static FUECPMcpBrokerStateChanged Empty;
		return Empty;
	}
};

class FUECPNullGddService final : public IUECPGddService
{
public:
	virtual void InitializeShellRefs(TWeakPtr<SUECPMainWidget>, TWeakObjectPtr<UUECPAppBridge>) override {}
	virtual void ImportFiles() override {}
	virtual void SaveFile(const FString&, const FString&) override {}
	virtual void DeleteFile(const FString&) override {}
	virtual void ToggleFile(const FString&, bool) override {}
	virtual void LoadFileContent(const FString&) override {}
	virtual void RefreshOverlay() override {}
	virtual FString BuildOverlayHtml() const override { return FString(); }
	virtual FString GetContentForAI() const override { return FString(); }
	virtual int32   GetTokenCount() const override { return 0; }
};

class FUECPNullAiMemoryService final : public IUECPAiMemoryService
{
public:
	virtual void InitializeShellRefs(TWeakPtr<SUECPMainWidget>, TWeakObjectPtr<UUECPAppBridge>) override {}
	virtual void NotifyPendingMemoriesOnStartup() override {}

	virtual TArray<TSharedPtr<FAiMemoryEntry>>& GetAiMemories()       override { return EmptyList; }
	virtual TArray<TSharedPtr<FAiMemoryEntry>>& GetPendingMemories()  override { return EmptyList; }
	virtual void    SaveManifest() override {}
	virtual FString GetContentForAI() const override { return FString(); }
	virtual int32   GetTokenCount() const override { return 0; }
	virtual FString GetCategoryDisplayName(EAiMemoryCategory) const override { return FString(); }

	virtual void AddMemory() override {}
	virtual void UpdateMemory(const FString&, const FString&, const FString&) override {}
	virtual void DeleteMemory(const FString&) override {}
	virtual void ApproveMemory(const FString&) override {}
	virtual void RejectMemory(const FString&) override {}
	virtual void ApproveAllPendingMemories() override {}
	virtual void RejectAllPendingMemories() override {}
	virtual void ToggleMemory(const FString&, bool) override {}
	virtual void LoadMemoryContent(const FString&) override {}
	virtual void ExtractMemoriesFromConversation() override {}

	virtual void  SetExtractionThreshold(int32) override {}
	virtual int32 GetExtractionThreshold() const override { return 7; }

	virtual void    RefreshOverlay() override {}
	virtual FString BuildOverlayHtml() const override { return FString(); }

private:
	TArray<TSharedPtr<FAiMemoryEntry>> EmptyList;
};

class FUECPNullCrewService final : public IUECPCrewService
{
public:
	virtual void InitializeShellRefs(TWeakPtr<SUECPMainWidget>, TWeakObjectPtr<UUECPAppBridge>) override {}

	virtual TArray<FCrewTemplate> GetTemplates() const override { return {}; }
	virtual bool                  GetTemplate(const FString&, FCrewTemplate&) const override { return false; }
	virtual FString               CloneTemplate(const FString&, const FString&) override { return FString(); }
	virtual FString               CreateTemplate(const FString&) override { return FString(); }
	virtual bool                  UpdateTemplate(const FCrewTemplate&) override { return false; }
	virtual bool                  DeleteTemplate(const FString&) override { return false; }

	virtual TArray<FCrewRun> GetRuns() const override { return {}; }
	virtual bool             GetRun(const FGuid&, FCrewRun&) const override { return false; }
	virtual FGuid            GetActiveRunId() const override { return FGuid(); }

	virtual FGuid CreateRun(const FString&, const FString&) override { return FGuid(); }
	virtual bool  SetRunRoles(const FGuid&, const TArray<FCrewRole>&) override { return false; }
	virtual bool  SetRunPlan(const FGuid&, const TArray<FCrewCheckpoint>&, TArray<FString>*) override { return false; }
	virtual bool  RetryCheckpoint(const FGuid&, const FString&) override { return false; }
	virtual bool  SetRoleApiKeySlot(const FGuid&, const FString&, int32) override { return false; }
	virtual bool  AnswerEscalation(const FGuid&, const FString&) override { return false; }
	virtual FGuid ForkRun(const FGuid&, const FString&) override { return FGuid(); }
	virtual bool  SetRunCaps(const FGuid&, const FCrewRunCaps&) override { return false; }
	virtual bool  SetRunDisplayName(const FGuid&, const FString&) override { return false; }

	virtual bool MarkRunReady(const FGuid&) override { return false; }
	virtual bool StartRun(const FGuid&, FString& OutReason) override { OutReason = TEXT("UECPCrew module not loaded"); return false; }
	virtual bool PauseRun(const FGuid&, const FString&) override { return false; }
	virtual bool ResumeRun(const FGuid&) override { return false; }
	virtual bool AbortRun(const FGuid&, const FString&) override { return false; }
	virtual bool CompleteRun(const FGuid&, const FString&) override { return false; }
	virtual bool DeleteRun(const FGuid&) override { return false; }

	virtual bool AppendHandoff(const FGuid&, const FCrewHandoff&) override { return false; }
	virtual bool SetCheckpointState(const FGuid&, const FString&, ECheckpointState, const FString&) override { return false; }

	virtual bool IsCrewChat(const FString&, FGuid&, FString&) const override { return false; }
	virtual bool IsToolAllowedForChat(const FString&, FName, FString&) const override { return true; }
	virtual bool SetRoleAllowlistOnRun(const FGuid&, const FString&, const TArray<FString>&) override { return false; }
	virtual bool ShouldAutoApproveDestructiveForChat(const FString&) const override { return false; }
	virtual bool ExportTemplate(const FString&, FString&) const override { return false; }
	virtual FString ImportTemplate(const FString&) override { return FString(); }

	virtual bool RouteInstructionDispatch(const FGuid&, const FString&, const FString&,
		const FString&, const FString&, FString& OutDenyReason) override
	{
		OutDenyReason = TEXT("UECPCrew module not loaded");
		return false;
	}
	virtual bool RouteRoleReport(const FGuid&, const FString&, const FString&,
		const FString&, const FString&, FString& OutDenyReason) override
	{
		OutDenyReason = TEXT("UECPCrew module not loaded");
		return false;
	}
	virtual bool RouteRoleQuestion(const FGuid&, const FString&, const FString&,
		FString& OutDenyReason) override
	{
		OutDenyReason = TEXT("UECPCrew module not loaded");
		return false;
	}

	virtual FOnCrewRunChanged&       OnRunChanged()       override { return DummyRunChanged; }
	virtual FOnCrewTemplatesChanged& OnTemplatesChanged() override { return DummyTemplatesChanged; }

private:
	FOnCrewRunChanged       DummyRunChanged;
	FOnCrewTemplatesChanged DummyTemplatesChanged;
};

class FUECPNullLearningService final : public IUECPLearningService
{
public:
	virtual bool IsInitialized() const override { return false; }

	virtual void TrackEvent(const FString&, const TSharedPtr<FJsonObject>&) override {}

	virtual void TrackToolCompletion(const FString&, const TSharedPtr<FJsonObject>&,
		bool, const FString&, const FString&, const FString&) override {}

	virtual void WebsitePost(const FString&, const TSharedPtr<FJsonObject>&,
		TFunction<void(bool, TSharedPtr<FJsonObject>)> Callback) override
	{
		if (Callback)
		{
			Callback(false, nullptr);
		}
	}

	virtual void MarkNodeComplete(const FString&) override {}

	virtual void IncrementPromptCount() override {}
};
