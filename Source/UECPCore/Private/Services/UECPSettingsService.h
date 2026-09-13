// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "Services/IUECPSettingsService.h"

class FUECPSettingsServiceImpl final : public IUECPSettingsService
{
public:
	virtual FString GetString(FName ModuleNamespace, const FString& Key, const FString& Default = TEXT("")) const override;
	virtual void    SetString(FName ModuleNamespace, const FString& Key, const FString& Value) override;
	virtual bool    GetBool  (FName ModuleNamespace, const FString& Key, bool Default = false) const override;
	virtual void    SetBool  (FName ModuleNamespace, const FString& Key, bool Value) override;
	virtual int32   GetInt   (FName ModuleNamespace, const FString& Key, int32 Default = 0) const override;
	virtual void    SetInt   (FName ModuleNamespace, const FString& Key, int32 Value) override;
	virtual float   GetFloat (FName ModuleNamespace, const FString& Key, float Default = 0.f) const override;
	virtual void    SetFloat (FName ModuleNamespace, const FString& Key, float Value) override;
	virtual void    MigrateKey(const FString& OldFlatKey, FName NewNamespace, const FString& NewKey) override;
	virtual FOnSettingChanged& OnSettingChanged() override;

private:
	FString MakeSection(FName Namespace) const;
	const FString& GetConfigFile() const;

	FOnSettingChanged SettingChangedDelegate;
};
