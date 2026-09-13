// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "Services/IUECPNotificationService.h"
#include "Templates/SharedPointer.h"

class SUECPMainWidget;

class FUECPShellNotificationSink : public IUECPNotificationService
{
public:
	explicit FUECPShellNotificationSink(TWeakPtr<SUECPMainWidget> InWidget)
		: WeakWidget(MoveTemp(InWidget)) {}

	virtual void PushToast(const FString& Message, const FString& Severity) override;
	virtual bool HasAttachedSink() const override { return WeakWidget.IsValid(); }
	virtual EConfirmDecision RequestDestructiveConfirm(
		const FString& ToolName, const FString& ArgsPreview, double TimeoutSecs) override;
	virtual FString RequestUserQuestions(const FString& QuestionsJson, double TimeoutSecs) override;

private:
	TWeakPtr<SUECPMainWidget> WeakWidget;
};
