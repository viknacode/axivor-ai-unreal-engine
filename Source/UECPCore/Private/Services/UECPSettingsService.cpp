// Copyright 2026, BlueprintsLab, All rights reserved

#include "UECPSettingsService.h"
#include "Managers/SettingsManager.h"
#include "Misc/ConfigCacheIni.h"

namespace
{
	constexpr const TCHAR* LegacySection = TEXT("BpGeneratorUltimate");
}

FString FUECPSettingsServiceImpl::MakeSection(FName Namespace) const
{
	return FString::Printf(TEXT("UECP:%s"), *Namespace.ToString());
}

const FString& FUECPSettingsServiceImpl::GetConfigFile() const
{
	return FSettingsManager::GetGlobalConfigPath();
}

FString FUECPSettingsServiceImpl::GetString(FName ModuleNamespace, const FString& Key, const FString& Default) const
{
	FString Value;
	if (GConfig->GetString(*MakeSection(ModuleNamespace), *Key, Value, GetConfigFile()))
	{
		return Value;
	}
	return Default;
}

void FUECPSettingsServiceImpl::SetString(FName ModuleNamespace, const FString& Key, const FString& Value)
{
	GConfig->SetString(*MakeSection(ModuleNamespace), *Key, *Value, GetConfigFile());
	GConfig->Flush(false, GetConfigFile());
	SettingChangedDelegate.Broadcast(ModuleNamespace, Key);
}

bool FUECPSettingsServiceImpl::GetBool(FName ModuleNamespace, const FString& Key, bool Default) const
{
	bool Value = Default;
	GConfig->GetBool(*MakeSection(ModuleNamespace), *Key, Value, GetConfigFile());
	return Value;
}

void FUECPSettingsServiceImpl::SetBool(FName ModuleNamespace, const FString& Key, bool Value)
{
	GConfig->SetBool(*MakeSection(ModuleNamespace), *Key, Value, GetConfigFile());
	GConfig->Flush(false, GetConfigFile());
	SettingChangedDelegate.Broadcast(ModuleNamespace, Key);
}

int32 FUECPSettingsServiceImpl::GetInt(FName ModuleNamespace, const FString& Key, int32 Default) const
{
	int32 Value = Default;
	GConfig->GetInt(*MakeSection(ModuleNamespace), *Key, Value, GetConfigFile());
	return Value;
}

void FUECPSettingsServiceImpl::SetInt(FName ModuleNamespace, const FString& Key, int32 Value)
{
	GConfig->SetInt(*MakeSection(ModuleNamespace), *Key, Value, GetConfigFile());
	GConfig->Flush(false, GetConfigFile());
	SettingChangedDelegate.Broadcast(ModuleNamespace, Key);
}

float FUECPSettingsServiceImpl::GetFloat(FName ModuleNamespace, const FString& Key, float Default) const
{
	float Value = Default;
	GConfig->GetFloat(*MakeSection(ModuleNamespace), *Key, Value, GetConfigFile());
	return Value;
}

void FUECPSettingsServiceImpl::SetFloat(FName ModuleNamespace, const FString& Key, float Value)
{
	GConfig->SetFloat(*MakeSection(ModuleNamespace), *Key, Value, GetConfigFile());
	GConfig->Flush(false, GetConfigFile());
	SettingChangedDelegate.Broadcast(ModuleNamespace, Key);
}

void FUECPSettingsServiceImpl::MigrateKey(const FString& OldFlatKey, FName NewNamespace, const FString& NewKey)
{
	const FString& Ini = GetConfigFile();

	FString Existing;
	if (GConfig->GetString(*MakeSection(NewNamespace), *NewKey, Existing, Ini))
	{
		return;
	}

	FString LegacyValue;
	if (!GConfig->GetString(LegacySection, *OldFlatKey, LegacyValue, Ini))
	{
		return;
	}

	GConfig->SetString(*MakeSection(NewNamespace), *NewKey, *LegacyValue, Ini);
	GConfig->RemoveKey(LegacySection, *OldFlatKey, Ini);
	GConfig->Flush(false, Ini);

	SettingChangedDelegate.Broadcast(NewNamespace, NewKey);
}

IUECPSettingsService::FOnSettingChanged& FUECPSettingsServiceImpl::OnSettingChanged()
{
	return SettingChangedDelegate;
}
