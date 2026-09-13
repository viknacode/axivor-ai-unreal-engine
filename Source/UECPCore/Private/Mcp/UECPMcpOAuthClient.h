// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"

class FUECPMcpOAuthClient
{
public:
	struct FAuthResult
	{
		bool    bOk = false;
		FString Error;
		FString Issuer;
		FString ClientId;
	};

	static void BeginInteractiveAuth(
		const FString& ServerId,
		const FString& ServerUrl,
		const FString& KnownClientId,
		const FString& KnownIssuer,
		TFunction<void(const FAuthResult&)> OnComplete);

	static bool EnsureFreshToken(const FString& ServerId, double SkewSeconds = 60.0);

	static bool HasUsableGrant(const FString& ServerId);

	static void Disconnect(const FString& ServerId);
};
