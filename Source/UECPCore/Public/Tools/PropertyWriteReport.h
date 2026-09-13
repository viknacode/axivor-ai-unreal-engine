// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

class FProperty;

namespace UECPProps
{
	UECPCORE_API FString ExportPropertyValueString(const FProperty* Prop, const void* ValuePtr);

	struct FWriteReport
	{
		TSharedPtr<FJsonObject> Applied = MakeShared<FJsonObject>();
		TArray<TSharedPtr<FJsonValue>> Ignored;
		TArray<TSharedPtr<FJsonValue>> Failed;
		int32 AppliedCount = 0;

		void AddApplied(const FString& Name, const FString& ReadBackValue)
		{
			Applied->SetStringField(Name, ReadBackValue);
			++AppliedCount;
		}
		void AddIgnored(const FString& Name)
		{
			Ignored.Add(MakeShared<FJsonValueString>(Name));
		}
		void AddFailed(const FString& Name, const FString& Reason)
		{
			TSharedPtr<FJsonObject> F = MakeShared<FJsonObject>();
			F->SetStringField(TEXT("name"), Name);
			F->SetStringField(TEXT("reason"), Reason);
			Failed.Add(MakeShared<FJsonValueObject>(F));
		}

		void FillInto(const TSharedPtr<FJsonObject>& Root) const
		{
			Root->SetBoolField(TEXT("success"), Failed.Num() == 0);
			Root->SetObjectField(TEXT("applied"), Applied);
			if (Ignored.Num() > 0) Root->SetArrayField(TEXT("ignored"), Ignored);
			if (Failed.Num() > 0)  Root->SetArrayField(TEXT("failed"), Failed);
		}
	};
}
