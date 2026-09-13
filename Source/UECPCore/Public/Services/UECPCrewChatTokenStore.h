// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"
#include "Containers/Map.h"

class UECPCORE_API FUECPCrewChatTokenStore
{
public:
	static FUECPCrewChatTokenStore& Get();

	FString IssueToken(const FString& ChatId);

	bool ValidateToken(const FString& ChatId, const FString& IncomingToken) const;

	void ClearToken(const FString& ChatId);

	void Reset();

private:
	mutable FCriticalSection Lock;
	TMap<FString, FString>   TokenByChatId;
};
