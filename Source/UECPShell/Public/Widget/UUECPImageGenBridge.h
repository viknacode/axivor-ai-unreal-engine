// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "Widget/UUECPBridgeBase.h"
#include "UUECPImageGenBridge.generated.h"

UCLASS()
class UUECPImageGenBridge : public UUECPBridgeBase
{
	GENERATED_BODY()

public:

	UFUNCTION() void GenerateImage(const FString& Json);

	UFUNCTION() void RequestImageGenConfig();

private:
	void PushResult(const FString& Json);
};
