// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "Interfaces/IHttpRequest.h"
#include "Meshy/MeshyTypes.h"

class UECPASSETGEN_API FMeshyHttpClient
{
public:
	static FMeshyHttpClient& Get();

	void Post(
		const FString& Url,
		const FString& ApiKey,
		TSharedPtr<FJsonObject> Payload,
		TFunction<void(const FMeshyHttpResult&)> OnDone);

	void Get(
		const FString& Url,
		const FString& ApiKey,
		TFunction<void(const FMeshyHttpResult&)> OnDone);

	void Download(
		const FString& Url,
		const FString& DestPath,
		TFunction<void(bool , const FString& )> OnDone);

private:
	FMeshyHttpClient() = default;

	static constexpr int32 MaxRetries = 4;
	static constexpr float DefaultTimeoutSec = 300.f;

	static FString  BuildAuthHeader(const FString& ApiKey);
	static float    GetBackoffSeconds(int32 Attempt, const FString& RetryAfterHeader);
	static bool     ShouldRetry(int32 HttpCode);
	static void     ParseError(const FString& ResponseBody, TSharedPtr<FJsonObject> Json, FMeshyError& OutError);

	void SendJson(
		const FString& Verb,
		const FString& Url,
		const FString& ApiKey,
		const FString& Body,
		TFunction<void(const FMeshyHttpResult&)> OnDone,
		int32 Attempt);
};
