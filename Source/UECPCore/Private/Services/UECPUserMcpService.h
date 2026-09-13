// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "Services/IUECPUserMcpService.h"

class FUECPUserMcpServiceImpl final : public IUECPUserMcpService
{
public:
	FUECPUserMcpServiceImpl();

	void Initialize();

	virtual TArray<FUECPUserMcpServerConfig> ListServers() const override;
	virtual const FUECPUserMcpServerConfig* FindServer(const FString& Id) const override;
	virtual bool UpsertServer(const FUECPUserMcpServerConfig& Config, FString& OutError) override;
	virtual TArray<FString> AddFromMcpServersBlock(const FString& JsonBlock,
		TMap<FString, FString>& OutErrors) override;
	virtual bool SetServerEnabled(const FString& Id, bool bEnabled, FString& OutError) override;
	virtual void RestartServer(const FString& Id) override;
	virtual void RemoveServer(const FString& Id) override;
	virtual void SignInOAuth(const FString& Id,
		TFunction<void(bool, FString)> OnComplete) override;
	virtual void SignOutOAuth(const FString& Id) override;
	virtual bool HasOAuthGrant(const FString& Id) const override;
	virtual bool ShouldConfirmUpstreamTool(const FString& NamespacedToolName,
		bool& bOutIsUpstreamTool) const override;
	virtual FOnUserMcpChanged& OnServerChanged() override { return ChangeDelegate; }

private:
	bool LoadFromDisk();
	bool SaveToDisk() const;

	void RegisterExtensionForConfig(const FUECPUserMcpServerConfig& Cfg);

	void UnregisterExtensionForConfig(const FString& Id);

	static FString GetSavePath();

	static bool ValidateId(const FString& Id, FString& OutError);

	static FString SanitiseId(const FString& In);

	TArray<FUECPUserMcpServerConfig> Servers;
	FOnUserMcpChanged ChangeDelegate;

	bool bInitialized = false;
};
