// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "Services/IUECPACPRegistryService.h"

namespace FUECPACPInstaller
{

	FString FindNpxPath();

	void Install(const FUECPACPAgentEntry& Entry,
		FUECPACPInstallProgress OnProgress,
		FUECPACPInstallComplete OnComplete);

	bool Uninstall(const FString& AgentId);

	bool WriteMarker(const FUECPACPInstallMarker& Marker);

	FString MarkerPath(const FString& AgentId);
}
