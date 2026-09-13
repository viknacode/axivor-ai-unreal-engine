// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "Widget/UUECPBridgeBase.h"
#include "UUECPMemoryBridge.generated.h"

UCLASS()
class UUECPMemoryBridge : public UUECPBridgeBase
{
	GENERATED_BODY()

public:
	UFUNCTION() void LoadMemories();
	UFUNCTION() void SaveMemory(const FString& Json);
	UFUNCTION() void UpdateMemory(const FString& Id, const FString& Content, const FString& Category);
	UFUNCTION() void LoadMemoryContent(const FString& Id);
	UFUNCTION() void DeleteMemory(const FString& Id);
	UFUNCTION() void ApproveMemory(const FString& Id);
	UFUNCTION() void RejectMemory(const FString& Id);
	UFUNCTION() void ApproveAllMemories();
	UFUNCTION() void ClearPendingMemories();
	UFUNCTION() void ToggleMemory(const FString& Id, bool bEnabled);
	UFUNCTION() void ExtractMemories();
	UFUNCTION() void SetExtractionThreshold(const FString& N);
	UFUNCTION() void SetAutoApproveMemories(const FString& OnOff);

private:
	FString BuildMemoryOverlayHtml() const;
};
