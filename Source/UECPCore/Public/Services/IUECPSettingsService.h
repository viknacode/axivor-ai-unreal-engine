// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"

class UECPCORE_API IUECPSettingsService
{
public:
	virtual ~IUECPSettingsService() = default;

	virtual FString GetString(FName ModuleNamespace, const FString& Key, const FString& Default = TEXT("")) const = 0;
	virtual void    SetString(FName ModuleNamespace, const FString& Key, const FString& Value) = 0;

	virtual bool    GetBool  (FName ModuleNamespace, const FString& Key, bool Default = false) const = 0;
	virtual void    SetBool  (FName ModuleNamespace, const FString& Key, bool Value) = 0;

	virtual int32   GetInt   (FName ModuleNamespace, const FString& Key, int32 Default = 0) const = 0;
	virtual void    SetInt   (FName ModuleNamespace, const FString& Key, int32 Value) = 0;

	virtual float   GetFloat (FName ModuleNamespace, const FString& Key, float Default = 0.f) const = 0;
	virtual void    SetFloat (FName ModuleNamespace, const FString& Key, float Value) = 0;

	virtual void MigrateKey(const FString& OldFlatKey, FName NewNamespace, const FString& NewKey) = 0;

	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnSettingChanged, FName , FString );
	virtual FOnSettingChanged& OnSettingChanged() = 0;
};
