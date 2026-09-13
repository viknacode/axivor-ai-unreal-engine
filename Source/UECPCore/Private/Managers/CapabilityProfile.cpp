// Copyright 2026, BlueprintsLab, All rights reserved

#include "Managers/CapabilityProfile.h"
#include "Managers/EditorProfileSync.h"

FCapabilityProfile& FCapabilityProfile::Get()
{
	static FCapabilityProfile Instance;
	return Instance;
}

void FCapabilityProfile::Initialize()
{
	bProfileResolved = true;
	(void)ResolveProfileState();
}

bool FCapabilityProfile::ResolveProfileState() const
{
	auto& Sync = FEditorProfileSync::Get();
	if (Sync.IsContextMarkedStale())
	{
		return false;
	}
	if (Sync.GetSyncKey().Len() <= 8)
	{
		return false;
	}
	if (!Sync.IsProfileCoherent())
	{
		return false;
	}
	return Sync.HasContextClearanceFlag() || Sync.GetContextRefreshAge() < 336.0;
}

bool FCapabilityProfile::IsEnabled(ECapability ) const
{
	return ResolveProfileState();
}

bool FCapabilityProfile::IsEnabledByKey(const FString& ) const
{
	return ResolveProfileState();
}

FString FCapabilityProfile::GetProfileTier() const
{
	const FString Tier = FEditorProfileSync::Get().GetPresetTier();
	return Tier.IsEmpty() ? TEXT("standard") : Tier;
}
