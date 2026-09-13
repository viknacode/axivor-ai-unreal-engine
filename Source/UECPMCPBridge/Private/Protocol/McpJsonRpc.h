// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace McpJsonRpc
{

	enum class EError : int32
	{
		ParseError      = -32700,
		InvalidRequest  = -32600,
		MethodNotFound  = -32601,
		InvalidParams   = -32602,
		InternalError   = -32603,
	};

	struct FRequest
	{
		FString                  Method;
		TSharedPtr<FJsonObject>  Params;
		TSharedPtr<FJsonValue>   Id;
		bool                     bIsNotification = false;
	};

	bool ParseRequest(const FString& Json, FRequest& OutReq, FString& OutErrorEnvelope);

	FString MakeSuccess(const TSharedPtr<FJsonValue>& Id, const TSharedPtr<FJsonObject>& Result);

	FString MakeError(const TSharedPtr<FJsonValue>& Id, EError Code, const FString& Message);

	FString MakeErrorCustom(const TSharedPtr<FJsonValue>& Id, int32 Code, const FString& Message);
}
