// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

struct UECPCORE_API FUECPUserMcpServerConfig
{

	FString Id;

	FString DisplayName;

	FString Description;

	FString Command;

	TArray<FString> Args;

	TArray<TPair<FString, FString>> Env;

	FString Cwd;

	FString Url;

	TArray<TPair<FString, FString>> Headers;

	bool bEnabled = true;

	bool bAlwaysConfirm = false;

	bool bUseOAuth = false;

	FString OAuthIssuer;

	FString OAuthClientId;
};

class UECPCORE_API IUECPUserMcpService
{
public:
	virtual ~IUECPUserMcpService() = default;

	virtual TArray<FUECPUserMcpServerConfig> ListServers() const = 0;

	virtual const FUECPUserMcpServerConfig* FindServer(const FString& Id) const = 0;

	virtual bool UpsertServer(const FUECPUserMcpServerConfig& Config, FString& OutError) = 0;

	virtual TArray<FString> AddFromMcpServersBlock(const FString& JsonBlock,
		TMap<FString, FString>& OutErrors) = 0;

	virtual bool SetServerEnabled(const FString& Id, bool bEnabled, FString& OutError) = 0;

	virtual void RestartServer(const FString& Id) = 0;

	virtual void RemoveServer(const FString& Id) = 0;

	virtual void SignInOAuth(const FString& Id,
		TFunction<void(bool , FString )> OnComplete) = 0;

	virtual void SignOutOAuth(const FString& Id) = 0;

	virtual bool HasOAuthGrant(const FString& Id) const = 0;

	static FName MakeExtensionId(const FString& Id);

	static FString TryGetUserMcpId(FName ExtensionId);

	virtual bool ShouldConfirmUpstreamTool(const FString& NamespacedToolName,
		bool& bOutIsUpstreamTool) const = 0;

	DECLARE_MULTICAST_DELEGATE_OneParam(FOnUserMcpChanged, FString );

	virtual FOnUserMcpChanged& OnServerChanged() = 0;
};
