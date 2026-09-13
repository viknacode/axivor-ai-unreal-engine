// Copyright 2026, BlueprintsLab, All rights reserved

#include "Meshy/Providers/MeshyBalanceProvider.h"
#include "Meshy/MeshyHttpClient.h"

void FMeshyBalanceProvider::Query(
	const FString& ApiKey,
	TFunction<void(const FMeshyBalanceResult&)> OnDone)
{
	if (ApiKey.IsEmpty())
	{
		FMeshyBalanceResult Result;
		Result.ErrorMessage = TEXT("Meshy API key is empty");
		OnDone(Result);
		return;
	}

	FMeshyHttpClient::Get().Get(TEXT("https://api.meshy.ai/openapi/v1/balance"), ApiKey,
		[OnDone = MoveTemp(OnDone)](const FMeshyHttpResult& HttpResult) mutable
		{
			FMeshyBalanceResult Result;

			if (!HttpResult.bSuccess || !HttpResult.ResponseJson.IsValid())
			{
				Result.ErrorMessage = HttpResult.Error.IsEmpty()
					? FString::Printf(TEXT("Balance check failed (HTTP %d)"), HttpResult.HttpCode)
					: HttpResult.Error.ToDisplayString();
				OnDone(Result);
				return;
			}

			int32 Balance = 0;
			HttpResult.ResponseJson->TryGetNumberField(TEXT("balance"), Balance);
			Result.bSuccess = true;
			Result.Credits  = Balance;
			OnDone(Result);
		});
}
