// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtr.h"

class SUECPMainWidget;
class UUECPAppBridge;
struct FConversationInfo;
class FJsonValue;

class UECPCORE_API IUECPAnalystService
{
public:
	virtual ~IUECPAnalystService() = default;

	virtual void InitializeShellRefs(TWeakPtr<SUECPMainWidget> Shell,
		TWeakObjectPtr<UUECPAppBridge> Bridge) = 0;

	virtual void SendMessage(const FString& Message) = 0;

	virtual void StopGeneration() = 0;

	virtual void NewChat() = 0;

	virtual void AddAssetContext() = 0;
	virtual void AddNodeContext() = 0;

	virtual void ImportFileContext() = 0;

	virtual void AttachImage() = 0;

	virtual void RefreshChatHistoryView() = 0;

	virtual void LoadChatHistory(const FString& ChatID) = 0;
	virtual void SaveChatHistory(const FString& ChatID) = 0;
	virtual void LoadManifest() = 0;
	virtual void SaveManifest() = 0;

	virtual void SelectChat(TSharedPtr<FConversationInfo> InItem, bool bDirect) = 0;

	virtual const TArray<TSharedPtr<FJsonValue>>& GetConversationHistory() const = 0;
	virtual const TArray<TSharedPtr<FConversationInfo>>& GetConversationList() const = 0;
	virtual const FString& GetActiveChatID() const = 0;

	virtual void ClearHistory() = 0;
};
