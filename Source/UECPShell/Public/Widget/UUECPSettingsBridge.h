// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "UUECPSettingsBridge.generated.h"

class SUECPMainWidget;
class SWebBrowser;

UCLASS()
class UECPSHELL_API UUECPSettingsBridge : public UObject
{
	GENERATED_BODY()

public:

	TWeakPtr<SUECPMainWidget> OwnerWidget;

	TWeakPtr<SWebBrowser> BrowserRef;

	void OnRegistryCatalogChanged(bool bFetchSucceeded);

	void OnMcpBrokerStateChanged();

	void OnExtensionStateChangedForUserMcp(FName ExtensionId);

	UFUNCTION() FString LoadAllSettings();

	UFUNCTION() void SaveSlot(const FString& SlotJson);

	UFUNCTION() void SaveImageGenConfig(const FString& Json);

	UFUNCTION() void SetActiveSlot(int32 Index);

	UFUNCTION() void SaveAgentConfig(const FString& Json);

	UFUNCTION() void SaveVoiceSettings(const FString& Json);

	UFUNCTION() void SaveTheme(const FString& Json);

	UFUNCTION() void SavePreferences(const FString& Json);

	UFUNCTION() void SetRebindCapturing(bool bCapturing);

	UFUNCTION() void RequestCliStatus();

	UFUNCTION() void InstallCli(const FString& ProviderName);

	UFUNCTION() void CloseSettings();

	UFUNCTION() void OpenExternalUrl(const FString& Url);

	UFUNCTION() void GetClipboardText();

	UFUNCTION() void CopyToClipboard(const FString& Text);

	UFUNCTION() void ShowNotification(const FString& Message);

	UFUNCTION() void MarkSettingsTourSeen();

	UFUNCTION() void ResetTutorials();

	UFUNCTION() void GitAction(const FString& Action, const FString& Param);

	UFUNCTION() void PerforceAction(const FString& Action, const FString& Param);

	UFUNCTION() FString RequestPluginInfo();

	UFUNCTION() void FetchMeshProviders();

	UFUNCTION() void SaveFreeTierSlot(const FString& Slot);

	UFUNCTION() void SaveToolSettings(const FString& Json);

	UFUNCTION() void RequestExtensionsCatalog();

	UFUNCTION() void ToggleExtension(const FString& ExtensionId, bool bEnable);

	UFUNCTION() void EnableExtensionPluginsAndRestart(const FString& ExtensionId);

	UFUNCTION() void RestartExtensionMcpServer(const FString& ExtensionId);

	UFUNCTION() void QueueExtensionChange(const FString& ExtensionId, bool bEnable);

	UFUNCTION() void UnqueueExtensionChange(const FString& ExtensionId);

	UFUNCTION() void CancelPendingExtensionChanges();

	UFUNCTION() void ApplyPendingExtensionChanges();

	void PushExtensionsCatalog();

	void PushExtensionsPending();

	UFUNCTION() void RequestMcpStatus();

	UFUNCTION() void SetMcpLanAccess(bool bEnabled);

	UFUNCTION() void RotateMcpToken();

	UFUNCTION() void RevealClaudeDesktopExtension();

	UFUNCTION() void RequestUserMcpServers();

	UFUNCTION() void AddUserMcpServersFromBlock(const FString& JsonBlock);

	UFUNCTION() void UpdateUserMcpServer(const FString& Json);

	UFUNCTION() void SetUserMcpServerEnabled(const FString& Id, bool bEnabled);

	UFUNCTION() void RestartUserMcpServer(const FString& Id);

	UFUNCTION() void RemoveUserMcpServer(const FString& Id);

	UFUNCTION() void TestUserMcpServer(const FString& Id);

	UFUNCTION() void OAuthSignInUserMcpServer(const FString& Id);

	UFUNCTION() void OAuthSignOutUserMcpServer(const FString& Id);

	void PushUserMcpServers();

	UFUNCTION() void RequestACPCatalog();
	UFUNCTION() void RefreshACPCatalog();
	UFUNCTION() void InstallACPAgent(const FString& AgentId);
	UFUNCTION() void UninstallACPAgent(const FString& AgentId);
	UFUNCTION() void SelectACPAgentForSlot(const FString& AgentId);

	UFUNCTION() void SignInACPAgent(const FString& AgentId);

	UFUNCTION() void SignInACPAgentCustom(const FString& AgentId, const FString& Subcommand);

	UFUNCTION() void OpenTerminalForACPAgent(const FString& AgentId);

	UFUNCTION() void RecheckACPAgent(const FString& AgentId);

	UFUNCTION() void SetACPAgentConfig(const FString& AgentId, const FString& Key, const FString& Value);

	void PushACPCatalog();

	void PushACPInstallProgress(const FString& AgentId, const FString& Stage, float Fraction,
		bool bDone, bool bSuccess);

	void PushACPAgentConfig(const FString& AgentId);

	void PushCliStatusToJs(bool bClaude, bool bClaudeAuth, bool bCodex, bool bCopilot, bool bGemini, bool bGhAuthed,
		const FString& NodeVer, const FString& NpmVer);

	void PushInstallResultToJs(const FString& ProviderName, bool bSuccess, const FString& Message);

	static FString BuildSettingsHtmlDataUri();

private:

	void SpawnAuthSubprocess(const FString& AgentId, const FString& Cmd, const FString& ArgsLine);
};
