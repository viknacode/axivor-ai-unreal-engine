// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "Widget/UUECPBridgeBase.h"
#include "UUECPBugReportBridge.generated.h"

UCLASS()
class UUECPBugReportBridge : public UUECPBridgeBase
{
	GENERATED_BODY()

public:
	UFUNCTION() void AttachBugImage();
	UFUNCTION() void PasteBugImage();
	UFUNCTION() void ClearBugImages();
	UFUNCTION() void SubmitBugReport(const FString& Json);
};
