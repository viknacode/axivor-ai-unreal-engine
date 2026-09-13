// Copyright 2026, BlueprintsLab, All rights reserved

#include "Widget/UUECPBugReportBridge.h"
#include "UECPCoreModule.h"
#include "Services/IUECPBugReportService.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

void UUECPBugReportBridge::AttachBugImage()
{
	IUECPCoreModule::Get().GetBugReportService().AttachImageFromFile();
}

void UUECPBugReportBridge::PasteBugImage()
{
	IUECPCoreModule::Get().GetBugReportService().AttachImageFromClipboard();
}

void UUECPBugReportBridge::ClearBugImages()
{
	IUECPCoreModule::Get().GetBugReportService().ClearAttachedImages();
}

void UUECPBugReportBridge::SubmitBugReport(const FString& Json)
{
	TSharedPtr<FJsonObject> Obj;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	if (!FJsonSerializer::Deserialize(Reader, Obj) || !Obj.IsValid()) return;

	FString Type = TEXT("bug"), Description, ConversationId;
	Obj->TryGetStringField(TEXT("type"), Type);
	Obj->TryGetStringField(TEXT("description"), Description);
	Obj->TryGetStringField(TEXT("conversation_id"), ConversationId);

	IUECPCoreModule::Get().GetBugReportService().Submit(Description, Type, ConversationId);
}
