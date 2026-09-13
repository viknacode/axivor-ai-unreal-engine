// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "Interfaces/IHttpRequest.h"
#include "Services/IUECPScannerService.h"

class UUECPAppBridge;
class SUECPMainWidget;

class UECPPROJECTSCANNER_API FUECPScannerCoordinator final : public IUECPScannerService
{
public:
	FUECPScannerCoordinator() = default;

	virtual void InitializeShellRefs(TWeakPtr<SUECPMainWidget> InShell,
		TWeakObjectPtr<UUECPAppBridge> InBridge) override;

	virtual void SendMessage(const FString& Message) override;
	virtual void StopGeneration() override;
	virtual void NewChat() override;
	virtual void SwitchChat(const FString& ChatID) override;
	virtual void AttachImage() override;
	virtual void ScanProject() override;
	virtual void ScanProjectAsync(FOnScanComplete OnDone) override;
	virtual void GetProjectOverview() override;
	virtual void GetPerformanceReport() override;

	virtual const TArray<TSharedPtr<FJsonValue>>& GetConversationHistory() const override;
	virtual const TArray<TSharedPtr<FConversationInfo>>& GetConversationList() const override;
	virtual const FString& GetActiveChatID() const override;

	virtual void SendChatRequest() override;
	virtual void OnApiResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful) override;

	virtual bool QueryIndex(const FString& Query, FString& OutJson, FString& OutError) override;

private:
	TWeakPtr<SUECPMainWidget>      Shell;
	TWeakObjectPtr<UUECPAppBridge> Bridge;

	FString PendingProjectChatID;

	static TArray<TSharedPtr<FJsonValue>>        EmptyHistory;
	static TArray<TSharedPtr<FConversationInfo>> EmptyList;
	static FString                               EmptyChatID;

	void BuildContext(const FString& UserQuery, FString& OutContext);

	void PostInternalReport(const FString& Title, const FString& UserPrompt, const FString& ReportMarkdown);

	void DoHeavyScan(const TArray<struct FAssetData>& AssetDataList, FOnScanComplete OnDone);
};
