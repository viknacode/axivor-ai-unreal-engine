// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "Interfaces/IHttpRequest.h"
#include "UObject/WeakObjectPtr.h"

class SUECPMainWidget;
class UUECPAppBridge;
struct FConversationInfo;
class FJsonValue;

using FOnScanComplete = TFunction<void(bool bSuccess, const FString& Message)>;

namespace UECPScannerPrompt
{
	inline const TCHAR* GetSystemPrompt()
	{
		return TEXT(
			"You are the Project Scanner assistant for an Unreal Engine 5 project.\n"
			"\n"
			"Your single purpose: answer the user's questions about their project's content using the indexed asset data shown in the user message. You are a fast, focused reference for questions like \"where is X defined?\", \"which blueprints handle Y?\", or \"summarise our combat system\".\n"
			"\n"
			"Rules:\n"
			"- You have NO tools, no MCP, no ability to read files or modify the project. Do not suggest, mention, or attempt tool calls.\n"
			"- Answer ONLY from the project index data provided in the user message. If the indexed data does not contain enough information to answer, say so plainly — do not speculate or invent assets.\n"
			"- When citing assets, format the asset path as a markdown link, e.g. [BP_Player](/Game/Characters/BP_Player) so the user can click through.\n"
			"- Keep responses focused. The user wants quick, accurate references — not lengthy tutorials.\n"
			"\n"
			"For deeper work (refactors, building blueprints, applying changes), the user should switch to the Architect view. You handle the \"what is\" questions; Architect handles the \"how to change\" questions."
		);
	}
}

class UECPCORE_API IUECPScannerService
{
public:
	virtual ~IUECPScannerService() = default;

	virtual void InitializeShellRefs(TWeakPtr<SUECPMainWidget> Shell,
		TWeakObjectPtr<UUECPAppBridge> Bridge) = 0;

	virtual void SendMessage(const FString& Message) = 0;
	virtual void StopGeneration() = 0;
	virtual void NewChat() = 0;
	virtual void SwitchChat(const FString& ChatID) = 0;
	virtual void AttachImage() = 0;

	virtual void ScanProject() = 0;

	virtual void ScanProjectAsync(FOnScanComplete OnDone) = 0;

	virtual void GetProjectOverview() = 0;

	virtual void GetPerformanceReport() = 0;

	virtual void SendChatRequest() = 0;
	virtual void OnApiResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bWasSuccessful) = 0;

	virtual bool QueryIndex(const FString& Query, FString& OutJson, FString& OutError) = 0;

	virtual const TArray<TSharedPtr<FJsonValue>>& GetConversationHistory() const = 0;
	virtual const TArray<TSharedPtr<FConversationInfo>>& GetConversationList() const = 0;
	virtual const FString& GetActiveChatID() const = 0;
};
