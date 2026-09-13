// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace UECPCallerContext
{

	inline const TCHAR* const CallerChatIdField = TEXT("_uecp_caller_chat");

	inline void WriteCallerChatId(TSharedPtr<FJsonObject>& Args, const FString& ChatId)
	{
		if (ChatId.IsEmpty()) return;
		if (!Args.IsValid()) Args = MakeShared<FJsonObject>();
		Args->SetStringField(CallerChatIdField, ChatId);
	}

	inline FString ReadCallerChatId(const TSharedPtr<FJsonObject>& Args)
	{
		if (!Args.IsValid()) return FString();
		FString Out;
		Args->TryGetStringField(CallerChatIdField, Out);
		return Out;
	}
}
