// Copyright 2026, BlueprintsLab, All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class UMoverComponent;
class UBlueprint;
class UClass;
class UObject;

namespace MoverTools
{
	UECPMOVEREXT_API UClass* ResolveClassByPath(const TCHAR* Path);
	UECPMOVEREXT_API UClass* GetChaosMoverComponentClass();
	UECPMOVEREXT_API UClass* GetChaosSharedSettingsClass();
	UECPMOVEREXT_API bool    IsChaosMover(UMoverComponent* MoverTemplate);
	UECPMOVEREXT_API UClass* ResolveChaosModeClass(const FString& ModeName);

	UECPMOVEREXT_API UMoverComponent* FindMoverTemplate(UBlueprint* BP);
	UECPMOVEREXT_API FString GetBpPath(const TSharedPtr<FJsonObject>& Args);

	UECPMOVEREXT_API bool SafeContainsMode(UMoverComponent* MoverTemplate, const FName& ModeKey);
	UECPMOVEREXT_API bool SafeAddMode(UMoverComponent* MoverTemplate, const FName& ModeKey, UClass* ModeClass, FString& OutError);

	UECPMOVEREXT_API void RefreshMoverSharedSettings(UMoverComponent* MoverTemplate);

	UECPMOVEREXT_API UObject* FindSharedSettingsByClass(UMoverComponent* MoverTemplate, UClass* SettingsClass);
	UECPMOVEREXT_API bool RefSetFloat(UObject* Container, const TCHAR* PropName, double Val);
	UECPMOVEREXT_API bool RefSetBool (UObject* Container, const TCHAR* PropName, bool Val);
	UECPMOVEREXT_API bool RefSetName (UObject* Container, const TCHAR* PropName, const FString& Val);

	UECPMOVEREXT_API void ScaffoldInputProducer(const FString& BpPath, TArray<FString>& OutSteps, TArray<FString>& OutWarnings, bool bChaosPhysicsBody = false);
}
