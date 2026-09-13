// Copyright 2026, BlueprintsLab, All rights reserved

#include "FUECPShellNotificationSink.h"
#include "SUECPMainWidget.h"
#include "Widget/UUECPAppBridge.h"

void FUECPShellNotificationSink::PushToast(const FString& Message, const FString& Severity)
{
	if (TSharedPtr<SUECPMainWidget> Widget = WeakWidget.Pin())
	{
		Widget->RouteToast(Message, Severity);
	}
}

IUECPNotificationService::EConfirmDecision FUECPShellNotificationSink::RequestDestructiveConfirm(
	const FString& ToolName, const FString& ArgsPreview, double TimeoutSecs)
{
	TSharedPtr<SUECPMainWidget> Widget = WeakWidget.Pin();
	if (!Widget.IsValid()) return EConfirmDecision::NoSink;
	const SUECPMainWidget::EMCPConfirmDecision Inner =
		Widget->RequestMCPDestructiveConfirm(ToolName, ArgsPreview, TimeoutSecs);
	switch (Inner)
	{
	case SUECPMainWidget::EMCPConfirmDecision::Proceed: return EConfirmDecision::Proceed;
	case SUECPMainWidget::EMCPConfirmDecision::Skip:    return EConfirmDecision::Skip;
	case SUECPMainWidget::EMCPConfirmDecision::Stop:    return EConfirmDecision::Stop;
	case SUECPMainWidget::EMCPConfirmDecision::Busy:    return EConfirmDecision::Busy;
	case SUECPMainWidget::EMCPConfirmDecision::Timeout:
	default:                                            return EConfirmDecision::Timeout;
	}
}

FString FUECPShellNotificationSink::RequestUserQuestions(const FString& QuestionsJson, double TimeoutSecs)
{
	TSharedPtr<SUECPMainWidget> Widget = WeakWidget.Pin();
	if (!Widget.IsValid()) return FString();
	return Widget->RequestUserQuestions(QuestionsJson, TimeoutSecs);
}
