// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "EditorUtilitySubsystem.h"
#include <atomic>

#include "MCP_EditorSubsystem.generated.h"

UCLASS()
class UECPMCPBRIDGE_API UMCP_EditorSubsystem : public UEditorUtilitySubsystem
{
	GENERATED_BODY()

public:

	std::atomic<bool> bMCPWriteUnlocked{false};
};
