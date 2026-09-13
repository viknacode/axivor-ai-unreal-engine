// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"

class FSessionTokenStore
{
public:
	static FSessionTokenStore& Get();

	void Initialize();

	FString GetToken() const;

	bool ValidateBearer(const FString& IncomingToken) const;

	FString Rotate();

private:

	static FString GetTokenFilePath();

	static FString GenerateNewToken();

	mutable FCriticalSection TokenLock;
	FString                  Token;
	bool                     bInitialized = false;
};
