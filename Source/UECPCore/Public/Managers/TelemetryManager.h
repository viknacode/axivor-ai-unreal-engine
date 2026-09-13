// Copyright 2026, BlueprintsLab, All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"

class UECPCORE_API FTelemetryManager
{
public:
	static FTelemetryManager& Get()
	{
		static FTelemetryManager Instance;
		return Instance;
	}

	void Initialize();

	void ReportToolSample(const FString& ToolName, const FString& ResultJson,
		bool bSuccess, TFunction<void(bool)> Callback);

	bool IsInWarmup() const;
	bool IsUsageCacheValid() const;
	bool IsUsageWedged() const;

private:
	FTelemetryManager();
	FTelemetryManager(const FTelemetryManager&) = delete;
	FTelemetryManager& operator=(const FTelemetryManager&) = delete;

	static FString DecodeWithKey(const TArray<uint8>& D, const FString& K);

	mutable FCriticalSection Lock;
	bool     bInitialized = false;
	bool     bLastValid = false;
	bool     bGateClosed = false;
	bool     bHadServerResponse = false;
	FDateTime LastValidationTime;
	int32    CallSeq = 0;
	int32    OfflineStreak = 0;
};
