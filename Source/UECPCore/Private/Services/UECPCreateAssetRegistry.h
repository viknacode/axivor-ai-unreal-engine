// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "Services/IUECPCreateAssetRegistry.h"

class FUECPCreateAssetRegistryImpl final : public IUECPCreateAssetRegistry
{
public:
	virtual void RegisterType(FName AssetType, FFactoryFn Factory, FName OwningExtension) override;
	virtual void UnregisterType(FName AssetType) override;
	virtual FUECPCreateAssetResult Create(
		const FString& AssetType,
		const FString& Name,
		const FString& SavePath,
		const TSharedPtr<FJsonObject>& Options,
		FString& OutError) override;
	virtual TArray<FName> ListTypes() const override;
	virtual FName GetOwningExtension(FName AssetType) const override;

private:
	struct FEntry
	{
		FFactoryFn Factory;
		FName      OwningExtension;
	};

	static FName Normalise(FName In);

	TMap<FName, FEntry> Types;
};
