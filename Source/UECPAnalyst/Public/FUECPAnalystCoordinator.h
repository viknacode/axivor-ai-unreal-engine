// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "Interfaces/IHttpRequest.h"
#include "Services/IUECPAnalystService.h"

class UUECPAppBridge;
class SUECPMainWidget;

class UECPANALYST_API FUECPAnalystCoordinator final : public IUECPAnalystService
{
public:
	FUECPAnalystCoordinator() = default;

	virtual void InitializeShellRefs(TWeakPtr<SUECPMainWidget> InShell,
		TWeakObjectPtr<UUECPAppBridge> InBridge) override;

	virtual void SendMessage(const FString& Message) override;
	virtual void StopGeneration() override;
	virtual void NewChat() override;
	virtual void AddAssetContext() override;
	virtual void AddNodeContext() override;
	virtual void ImportFileContext() override;
	virtual void AttachImage() override;
	virtual void RefreshChatHistoryView() override;

	virtual void LoadChatHistory(const FString& ChatID) override;
	virtual void SaveChatHistory(const FString& ChatID) override;
	virtual void LoadManifest() override;
	virtual void SaveManifest() override;

	virtual void SelectChat(TSharedPtr<FConversationInfo> InItem, bool bDirect) override;

	virtual const TArray<TSharedPtr<FJsonValue>>& GetConversationHistory() const override;
	virtual const TArray<TSharedPtr<FConversationInfo>>& GetConversationList() const override;
	virtual const FString& GetActiveChatID() const override;
	virtual void ClearHistory() override;

private:
	TWeakPtr<SUECPMainWidget>      Shell;
	TWeakObjectPtr<UUECPAppBridge> Bridge;

	static TArray<TSharedPtr<FJsonValue>>        EmptyHistory;
	static TArray<TSharedPtr<FConversationInfo>> EmptyList;
	static FString                               EmptyChatID;

	void SendChatRequest();

	void OnApiResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful);
};
