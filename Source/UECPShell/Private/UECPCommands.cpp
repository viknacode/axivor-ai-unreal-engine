// Copyright 2026, BlueprintsLab, All rights reserved

#include "UECPCommands.h"

#define LOCTEXT_NAMESPACE "FUECPShellModule"

void FUECPCommands::RegisterCommands()
{
	UI_COMMAND(PluginAction, "Axivor AI", "Open Axivor AI", EUserInterfaceActionType::Button, FInputChord());
}

#undef LOCTEXT_NAMESPACE
