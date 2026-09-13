// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"

struct FMeshyBalanceResult
{
	bool    bSuccess = false;
	int32   Credits = 0;
	FString ErrorMessage;
};

class UECPASSETGEN_API FMeshyBalanceProvider
{
public:
	static void Query(
		const FString& ApiKey,
		TFunction<void(const FMeshyBalanceResult&)> OnDone);
};
