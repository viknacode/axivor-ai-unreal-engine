// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "Services/IUECPModuleRegistry.h"

class FUECPModuleRegistryImpl final : public IUECPModuleRegistry
{
public:
	virtual void RegisterFeature(FUECPFeatureDescriptor Descriptor) override;
	virtual void UnregisterFeature(FName FeatureId) override;
	virtual const TArray<FUECPFeatureDescriptor>& GetFeatures() const override;
	virtual const FUECPFeatureDescriptor* FindFeature(FName FeatureId) const override;
	virtual FOnRegistryChanged& OnRegistryChanged() override;

private:
	TArray<FUECPFeatureDescriptor> Features;
	FOnRegistryChanged RegistryChangedDelegate;
};
