// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"

struct FUECPMcpOAuthRecord
{
	FString   AccessToken;
	FString   RefreshToken;
	FString   Scope;
	FString   Issuer;
	FString   ClientId;
	FString   Resource;
	FString   TokenEndpoint;
	FDateTime ExpiresAtUtc = FDateTime(0);

	bool HasAccessToken() const { return !AccessToken.IsEmpty(); }
	bool HasRefreshToken() const { return !RefreshToken.IsEmpty(); }

	bool NeedsRefresh(double SkewSeconds) const
	{
		return (FDateTime::UtcNow() + FTimespan::FromSeconds(SkewSeconds)) >= ExpiresAtUtc;
	}
};

class FUECPMcpOAuthTokenStore
{
public:
	static FUECPMcpOAuthTokenStore& Get();

	void Initialize();

	bool GetRecord(const FString& ServerId, FUECPMcpOAuthRecord& Out) const;

	bool PutRecord(const FString& ServerId, const FUECPMcpOAuthRecord& Rec);

	void Remove(const FString& ServerId);

	bool HasRecord(const FString& ServerId) const;

	static bool HasOsSecretStore();

private:
	static FString GetStorePath();
	void LoadLocked();
	bool SaveLocked() const;

	static bool ProtectBytes(const TArray<uint8>& Plain, TArray<uint8>& OutBlob);
	static bool UnprotectBytes(const TArray<uint8>& Blob, TArray<uint8>& OutPlain);

	mutable FCriticalSection Lock;
	TMap<FString, FUECPMcpOAuthRecord> Records;
	bool bInitialized = false;
};
