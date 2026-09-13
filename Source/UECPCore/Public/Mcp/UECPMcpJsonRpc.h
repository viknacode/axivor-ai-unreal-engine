// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

class UECPCORE_API FUECPMcpJsonRpc
{
public:
	FUECPMcpJsonRpc() = default;

	void AppendBytes(const TArray<uint8>& Bytes);
	void AppendUtf8(const ANSICHAR* Data, int32 Count);
	void AppendFString(const FString& Chunk);

	bool TakeLine(FString& OutLine);

	int32 PendingBytes() const { return Buffer.Len(); }

	int64 NextRequestId() { return ++NextId; }

	static FString FormatRequest(int64 Id, const FString& Method, const TSharedPtr<FJsonObject>& Params);
	static FString FormatNotification(const FString& Method, const TSharedPtr<FJsonObject>& Params);
	static FString FormatResult(const TSharedPtr<FJsonValue>& Id, const TSharedPtr<FJsonObject>& Result);
	static FString FormatError(const TSharedPtr<FJsonValue>& Id, int32 Code, const FString& Message);

	static bool ParseFrame(const FString& Frame, TSharedPtr<FJsonObject>& OutRoot);

	static bool IsRequest     (const TSharedPtr<FJsonObject>& Root);
	static bool IsNotification(const TSharedPtr<FJsonObject>& Root);
	static bool IsResponse    (const TSharedPtr<FJsonObject>& Root);

private:
	FString Buffer;
	int64   NextId = 0;
};
