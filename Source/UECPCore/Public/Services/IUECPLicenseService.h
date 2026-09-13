// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"

class UECPCORE_API IUECPLicenseService
{
public:
	virtual ~IUECPLicenseService() = default;

	virtual bool IsSessionActive() const = 0;

	virtual bool IsGraphContextReady() const = 0;

	virtual bool IsMCPContextValid() const = 0;

	virtual bool IsFeatureAvailable(FName FeatureId) const = 0;

	virtual FText GetUnavailableReason(FName FeatureId) const = 0;
};
