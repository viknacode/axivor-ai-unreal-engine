// Copyright 2026, BlueprintsLab, All rights reserved

#include "UECPModuleRegistry.h"
#include "UECPCoreModule.h"

void FUECPModuleRegistryImpl::RegisterFeature(FUECPFeatureDescriptor Descriptor)
{
	if (Descriptor.FeatureId.IsNone())
	{
		UE_LOG(LogUECPCore, Warning, TEXT("RegisterFeature called with empty FeatureId — ignored."));
		return;
	}

	if (Descriptor.LicenseFeatureId.IsNone())
	{
		Descriptor.LicenseFeatureId = Descriptor.FeatureId;
	}

	const int32 ExistingIndex = Features.IndexOfByPredicate(
		[&Descriptor](const FUECPFeatureDescriptor& D) { return D.FeatureId == Descriptor.FeatureId; });

	if (ExistingIndex != INDEX_NONE)
	{
		Features[ExistingIndex] = MoveTemp(Descriptor);
	}
	else
	{
		Features.Add(MoveTemp(Descriptor));
	}

	RegistryChangedDelegate.Broadcast();
}

void FUECPModuleRegistryImpl::UnregisterFeature(FName FeatureId)
{
	const int32 RemovedCount = Features.RemoveAll(
		[FeatureId](const FUECPFeatureDescriptor& D) { return D.FeatureId == FeatureId; });

	if (RemovedCount > 0)
	{
		RegistryChangedDelegate.Broadcast();
	}
}

const TArray<FUECPFeatureDescriptor>& FUECPModuleRegistryImpl::GetFeatures() const
{
	return Features;
}

const FUECPFeatureDescriptor* FUECPModuleRegistryImpl::FindFeature(FName FeatureId) const
{
	return Features.FindByPredicate(
		[FeatureId](const FUECPFeatureDescriptor& D) { return D.FeatureId == FeatureId; });
}

IUECPModuleRegistry::FOnRegistryChanged& FUECPModuleRegistryImpl::OnRegistryChanged()
{
	return RegistryChangedDelegate;
}
