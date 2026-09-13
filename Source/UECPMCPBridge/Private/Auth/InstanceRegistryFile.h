// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"

struct FInstanceRegistryEntry
{
	FString ProjectPath;
	FString ProjectName;
	int32   McpHttpPort = 0;
	int32   ProcessId   = 0;
	FString StartedAt;
};

namespace InstanceRegistry
{

	FString GetRegistryDir();

	FString GetInstanceFilePath();

	bool WriteEntry(const FInstanceRegistryEntry& Entry);

	void RemoveEntry();
}
