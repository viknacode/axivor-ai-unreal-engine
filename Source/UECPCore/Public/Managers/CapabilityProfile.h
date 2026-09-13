// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once
#include "CoreMinimal.h"

enum class ECapability : uint8
{
	GraphAuthoring,
	SceneAuthoring,
	AssetProvisioning,
	BridgeDispatch,
	AdvancedTooling,
};

class UECPCORE_API FCapabilityProfile
{
public:
	static FCapabilityProfile& Get();

	void Initialize();

	bool IsEnabled(ECapability Capability) const;

	bool IsEnabledByKey(const FString& CapabilityKey) const;

	FString GetProfileTier() const;

private:
	FCapabilityProfile() = default;
	FCapabilityProfile(const FCapabilityProfile&) = delete;
	FCapabilityProfile& operator=(const FCapabilityProfile&) = delete;

	bool ResolveProfileState() const;

	mutable bool bProfileResolved = false;
};
