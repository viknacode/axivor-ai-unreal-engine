// Copyright 2026, BlueprintsLab, All rights reserved

#include "Utils/TokenUtils.h"

DEFINE_LOG_CATEGORY_STATIC(LogTokenUtils, Log, All);

namespace TokenUtils
{
	int32 EstimateFromString(const FString& Text)
	{
		return Text.Len() / 4;
	}

	int32 EstimateFromJsonArray(const TArray<TSharedPtr<FJsonValue>>& ConversationHistory)
	{
		int32 TotalChars = 0;
		for (const TSharedPtr<FJsonValue>& Message : ConversationHistory)
		{
			const TSharedPtr<FJsonObject>& MsgObj = Message->AsObject();
			FString Role;
			if (!MsgObj->TryGetStringField(TEXT("role"), Role)) continue;

			if (Role == TEXT("context"))
			{
				FString Content;
				if (MsgObj->TryGetStringField(TEXT("content"), Content))
				{
					TotalChars += Content.Len();
				}
				continue;
			}

			const TArray<TSharedPtr<FJsonValue>>* PartsArray;
			if (MsgObj->TryGetArrayField(TEXT("parts"), PartsArray))
			{
				for (const TSharedPtr<FJsonValue>& Part : *PartsArray)
				{
					FString Text;
					if (Part->AsObject()->TryGetStringField(TEXT("text"), Text))
					{
						TotalChars += Text.Len();
					}
				}
			}
		}
		return TotalChars / 4;
	}

	int32 EstimateFromStringArray(const TArray<FString>& Strings)
	{
		int32 TotalChars = 0;
		for (const FString& Str : Strings)
		{
			TotalChars += Str.Len();
		}
		return TotalChars / 4;
	}
}
