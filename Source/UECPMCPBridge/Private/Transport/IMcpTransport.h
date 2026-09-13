// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"

class IMcpTransport
{
public:
	virtual ~IMcpTransport() = default;

	virtual bool Start() = 0;

	virtual void Stop() = 0;

	virtual const TCHAR* GetName() const = 0;
};
