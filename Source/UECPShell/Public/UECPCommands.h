// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "Framework/Commands/Commands.h"
#include "UECPStyle.h"

class FUECPCommands : public TCommands<FUECPCommands>
{
public:

	FUECPCommands()
		: TCommands<FUECPCommands>(TEXT("BpGeneratorUltimate"), NSLOCTEXT("Contexts", "BpGeneratorUltimate", "Axivor AI"), NAME_None, FUECPStyle::GetStyleSetName())
	{
	}

	virtual void RegisterCommands() override;

public:
	TSharedPtr< FUICommandInfo > PluginAction;
};
