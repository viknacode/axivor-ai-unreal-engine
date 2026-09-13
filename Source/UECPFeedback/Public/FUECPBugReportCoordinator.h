// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "Services/IUECPBugReportService.h"
#include "SUECPMainWidget.h"

class UUECPAppBridge;

class UECPFEEDBACK_API FUECPBugReportCoordinator final : public IUECPBugReportService
{
public:
	FUECPBugReportCoordinator();

	virtual void InitializeShellRefs(TWeakPtr<SUECPMainWidget> InShell,
		TWeakObjectPtr<UUECPAppBridge> InBridge) override;
	virtual void Submit(const FString& UserMessage, const FString& ReportType,
		const FString& ConversationChatId) override;
	virtual void AttachImageFromFile() override;
	virtual void AttachImageFromClipboard() override;
	virtual void ClearAttachedImages() override;

private:
	TWeakPtr<SUECPMainWidget>      Shell;
	TWeakObjectPtr<UUECPAppBridge> Bridge;

	TArray<FAttachedImage> AttachedImages;

	void UploadImage(const TArray<uint8>& ImageData, const FString& FileName,
		TFunction<void(bool bSuccess, const FString& Url)> Callback);
	void FinalizeSubmit(TSharedPtr<FJsonObject> ReportBody);
	void PushImagesToJS();
};
