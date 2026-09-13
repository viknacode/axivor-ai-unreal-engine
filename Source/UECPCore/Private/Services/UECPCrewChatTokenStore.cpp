// Copyright 2026, BlueprintsLab, All rights reserved

#include "Services/UECPCrewChatTokenStore.h"
#include "Misc/Guid.h"

FUECPCrewChatTokenStore& FUECPCrewChatTokenStore::Get()
{
	static FUECPCrewChatTokenStore Singleton;
	return Singleton;
}

FString FUECPCrewChatTokenStore::IssueToken(const FString& ChatId)
{
	if (ChatId.IsEmpty()) return FString();

	const FString A = FGuid::NewGuid().ToString(EGuidFormats::Digits);
	const FString B = FGuid::NewGuid().ToString(EGuidFormats::Digits);
	const FString Tok = (A + B).ToLower();

	{
		FScopeLock _(&Lock);
		TokenByChatId.Add(ChatId, Tok);
	}
	return Tok;
}

bool FUECPCrewChatTokenStore::ValidateToken(const FString& ChatId, const FString& IncomingToken) const
{
	if (ChatId.IsEmpty() || IncomingToken.IsEmpty()) return false;
	FScopeLock _(&Lock);
	const FString* Found = TokenByChatId.Find(ChatId);
	if (!Found) return false;
	if (Found->Len() != IncomingToken.Len()) return false;
	int32 Diff = 0;
	for (int32 i = 0; i < Found->Len(); ++i)
	{
		Diff |= ((*Found)[i] ^ IncomingToken[i]);
	}
	return Diff == 0;
}

void FUECPCrewChatTokenStore::ClearToken(const FString& ChatId)
{
	if (ChatId.IsEmpty()) return;
	FScopeLock _(&Lock);
	TokenByChatId.Remove(ChatId);
}

void FUECPCrewChatTokenStore::Reset()
{
	FScopeLock _(&Lock);
	TokenByChatId.Reset();
}
