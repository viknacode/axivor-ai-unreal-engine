// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "Textures/SlateIcon.h"
#include "Widgets/SWidget.h"

struct FUECPFeatureDescriptor
{

	FName      FeatureId;

	FText      DisplayName;
	FSlateIcon TabIcon;

	int32      TabPriority = 100;

	FName      LicenseFeatureId;

	bool       bShowWhenUnlicensed = true;

	TFunction<TSharedRef<SWidget>()> CreateView;

	TFunction<TSharedRef<SWidget>()> CreateLockedView;
};

class UECPCORE_API IUECPModuleRegistry
{
public:
	virtual ~IUECPModuleRegistry() = default;

	virtual void RegisterFeature(FUECPFeatureDescriptor Descriptor) = 0;
	virtual void UnregisterFeature(FName FeatureId) = 0;

	virtual const TArray<FUECPFeatureDescriptor>& GetFeatures() const = 0;
	virtual const FUECPFeatureDescriptor* FindFeature(FName FeatureId) const = 0;

	DECLARE_MULTICAST_DELEGATE(FOnRegistryChanged);
	virtual FOnRegistryChanged& OnRegistryChanged() = 0;
};
