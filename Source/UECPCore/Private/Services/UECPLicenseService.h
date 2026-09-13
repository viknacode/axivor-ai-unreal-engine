// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "Services/IUECPLicenseService.h"

class FUECPLicenseServiceImpl final : public IUECPLicenseService
{
public:
	virtual bool IsSessionActive() const override;
	virtual bool IsGraphContextReady() const override;
	virtual bool IsMCPContextValid() const override;
	virtual bool IsFeatureAvailable(FName FeatureId) const override;
	virtual FText GetUnavailableReason(FName FeatureId) const override;
};
