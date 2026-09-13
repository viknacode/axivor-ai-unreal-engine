// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "Widget/UUECPBridgeBase.h"
#include "UUECPGddBridge.generated.h"

UCLASS()
class UUECPGddBridge : public UUECPBridgeBase
{
	GENERATED_BODY()

public:
	UFUNCTION() void LoadGddFiles();
	UFUNCTION() void SaveGddFile(const FString& FileName, const FString& Content);
	UFUNCTION() void ImportGddFiles();
	UFUNCTION() void DeleteGddFile(const FString& FileName);
	UFUNCTION() void ToggleGddFile(const FString& FileName, bool bEnabled);
	UFUNCTION() void LoadGddFileContent(const FString& FileId);

private:
	FString BuildGddOverlayHtml() const;
};
