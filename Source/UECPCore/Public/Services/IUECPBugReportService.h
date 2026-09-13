// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtr.h"

class SUECPMainWidget;
class UUECPAppBridge;

class UECPCORE_API IUECPBugReportService
{
public:
	virtual ~IUECPBugReportService() = default;

	virtual void InitializeShellRefs(TWeakPtr<SUECPMainWidget> Shell,
		TWeakObjectPtr<UUECPAppBridge> Bridge) = 0;

	virtual void Submit(const FString& UserMessage, const FString& ReportType, const FString& ConversationChatId) = 0;

	virtual void AttachImageFromFile() = 0;

	virtual void AttachImageFromClipboard() = 0;

	virtual void ClearAttachedImages() = 0;
};
