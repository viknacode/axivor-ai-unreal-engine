// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

enum class EUECPACPStreamEventKind : uint8
{
	Text,
	Thinking,
	ToolStart,
	ToolUpdate,
	ToolResult,
	UsageUpdate,
	SlashCommands,
	Unknown
};

struct FUECPACPStreamEvent
{
	EUECPACPStreamEventKind Kind = EUECPACPStreamEventKind::Unknown;

	FString Text;

	FString ToolCallId;
	FString ToolTitle;
	FString ToolKind;
	FString ToolStatus;
	FString ToolContentText;
	FString ToolRawInputJson;
	FString ToolAction;
	bool    bIsError = false;

	int32 InputTokens       = 0;
	int32 OutputTokens      = 0;
	int32 CachedReadTokens  = 0;
	int32 CachedWriteTokens = 0;
	int32 TotalTokens       = 0;
	double CostAmount       = 0.0;
	FString CostCurrency;

	FString UnknownDiscriminator;
	TSharedPtr<FJsonObject> RawUpdate;
};

namespace UECPACPEventMapper
{

	void Map(const TSharedPtr<FJsonObject>& UpdateObj,
		TArray<FUECPACPStreamEvent>& OutEvents);
}
