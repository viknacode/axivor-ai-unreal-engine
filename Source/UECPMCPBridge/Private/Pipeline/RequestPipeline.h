// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

#include <atomic>

class UMCP_EditorSubsystem;

struct FMcpPipelineRequest
{

	FString                  CommandType;
	TSharedPtr<FJsonObject>  JsonObject;

	std::atomic<bool>*       WriteUnlocked = nullptr;

	TFunction<bool()>        IsTransportAlive;

	FString                  CallerChatId;
	FString                  CallerChatToken;
};

struct FMcpPipelineResponse
{

	FString  Body;

	bool     bDenied = false;
};

class FRequestPipeline
{
public:

	static FMcpPipelineResponse Process(const FMcpPipelineRequest& Request);
};
