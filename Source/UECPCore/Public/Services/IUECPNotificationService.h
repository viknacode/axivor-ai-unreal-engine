// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"

class UECPCORE_API IUECPNotificationService
{
public:
	virtual ~IUECPNotificationService() = default;

	virtual void PushToast(const FString& Message, const FString& Severity = TEXT("info")) = 0;

	virtual bool HasAttachedSink() const { return false; }

	enum class EConfirmDecision : uint8 { Proceed, Skip, Stop, Timeout, Busy, NoSink };
	virtual EConfirmDecision RequestDestructiveConfirm(
		const FString& ToolName, const FString& ArgsPreview, double TimeoutSecs)
	{
		return EConfirmDecision::NoSink;
	}

	virtual FString RequestUserQuestions(const FString& QuestionsJson, double TimeoutSecs)
	{
		return FString();
	}
};
