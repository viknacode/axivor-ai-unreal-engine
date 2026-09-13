// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "Services/IUECPNotificationService.h"

class FUECPNullNotificationService : public IUECPNotificationService
{
public:
	virtual void PushToast(const FString& , const FString& ) override {}
	virtual bool HasAttachedSink() const override { return false; }
};
