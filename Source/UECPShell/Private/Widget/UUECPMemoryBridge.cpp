// Copyright 2026, BlueprintsLab, All rights reserved

#include "Widget/UUECPMemoryBridge.h"
#include "SUECPMainWidget.h"
#include "UECPCoreModule.h"
#include "Services/IUECPAiMemoryService.h"
#include "Services/IUECPArchitectService.h"
#include "Managers/SettingsManager.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

FString UUECPMemoryBridge::BuildMemoryOverlayHtml() const
{
	return IUECPCoreModule::Get().GetAiMemoryService().BuildOverlayHtml();
}

void UUECPMemoryBridge::LoadMemories()
{
	PushOverlayHtml(BuildMemoryOverlayHtml());
}

void UUECPMemoryBridge::SaveMemory(const FString& )
{
	IUECPCoreModule::Get().GetAiMemoryService().AddMemory();
}

void UUECPMemoryBridge::DeleteMemory(const FString& Id)
{
	IUECPCoreModule::Get().GetAiMemoryService().DeleteMemory(Id);
}

void UUECPMemoryBridge::ApproveMemory(const FString& Id)
{
	IUECPCoreModule::Get().GetAiMemoryService().ApproveMemory(Id);
}

void UUECPMemoryBridge::RejectMemory(const FString& Id)
{
	IUECPCoreModule::Get().GetAiMemoryService().RejectMemory(Id);
}

void UUECPMemoryBridge::ApproveAllMemories()
{
	IUECPCoreModule::Get().GetAiMemoryService().ApproveAllPendingMemories();
}

void UUECPMemoryBridge::ClearPendingMemories()
{
	IUECPCoreModule::Get().GetAiMemoryService().RejectAllPendingMemories();
}

void UUECPMemoryBridge::ToggleMemory(const FString& Id, bool bEnabled)
{
	IUECPCoreModule::Get().GetAiMemoryService().ToggleMemory(Id, bEnabled);
}

void UUECPMemoryBridge::ExtractMemories()
{
	auto W = OwnerWidget.Pin();
	if (!W.IsValid()) return;

	if (W->IsArchitectThinkingForExtraction())
	{
		PushToast(TEXT("AI is currently responding — wait for it to finish before extracting memories"), TEXT("warn"));
		return;
	}

	const TArray<TSharedPtr<FJsonValue>>& History = W->GetArchitectConversationHistoryForExtraction();
	if (History.Num() > 0)
	{
		TSharedPtr<FJsonObject> Last = History.Last().IsValid() ? History.Last()->AsObject() : nullptr;
		FString LastRole;
		if (Last.IsValid()) Last->TryGetStringField(TEXT("role"), LastRole);
		if (LastRole == TEXT("user"))
		{
			PushToast(TEXT("Wait for AI to respond to the last message before extracting memories"), TEXT("warn"));
			return;
		}
	}

	IUECPCoreModule::Get().GetAiMemoryService().ExtractMemoriesFromConversation();

	const FString Instruction = TEXT(
		"Review our conversation so far and extract 1-5 durable facts worth remembering across sessions "
		"(project info, user preferences, architectural decisions, asset relationships, recurring patterns). "
		"For each, call memory(action=\"suggest_memory\", content=\"...\", category=\"project_info|recent_work|preferences|patterns|asset_relations|decisions\"). "
		"Skip trivia, greetings, and anything already clearly repeated. Then briefly summarize what you suggested.");
	IUECPCoreModule::Get().GetArchitectService().SendMessage(Instruction);
}

void UUECPMemoryBridge::SetExtractionThreshold(const FString& N)
{
	const int32 Value = FCString::Atoi(*N);
	IUECPCoreModule::Get().GetAiMemoryService().SetExtractionThreshold(Value);
}

void UUECPMemoryBridge::SetAutoApproveMemories(const FString& OnOff)
{
	const FString Lower = OnOff.ToLower();
	const bool bOn = (Lower == TEXT("true") || Lower == TEXT("1") || Lower == TEXT("on"));
	FSettingsManager::Get().SaveAutoApproveMemories(bOn);
	PushToast(
		bOn
			? TEXT("Auto-approve memories: ON — new suggestions skip the Pending tab")
			: TEXT("Auto-approve memories: OFF — new suggestions land in the Pending tab"),
		TEXT("info"));
}

void UUECPMemoryBridge::LoadMemoryContent(const FString& Id)
{
	IUECPCoreModule::Get().GetAiMemoryService().LoadMemoryContent(Id);
}

void UUECPMemoryBridge::UpdateMemory(const FString& Id, const FString& Content, const FString& Category)
{
	IUECPCoreModule::Get().GetAiMemoryService().UpdateMemory(Id, Content, Category);
}
