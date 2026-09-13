// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"

class IUECPToolDispatcher;

namespace UECPCrew::Tools
{

	void RegisterAll(IUECPToolDispatcher& Dispatcher);

	void UnregisterAll(IUECPToolDispatcher& Dispatcher);
}
