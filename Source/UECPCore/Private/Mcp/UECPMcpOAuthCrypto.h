// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"

namespace UECPMcpOAuthCrypto
{

	UECPCORE_API void Sha256(const uint8* Data, int32 Len, uint8 Out[32]);

	UECPCORE_API FString Base64UrlEncode(const uint8* Data, int32 Len);
	UECPCORE_API FString Base64UrlEncode(const TArray<uint8>& Data);

	UECPCORE_API FString GenerateCodeVerifier();

	UECPCORE_API FString CodeChallengeS256(const FString& Verifier);

	UECPCORE_API FString RandomUrlToken(int32 NumBytes = 32);

	UECPCORE_API void DeriveKey32(const FString& Seed, uint8 OutKey[32]);
}
