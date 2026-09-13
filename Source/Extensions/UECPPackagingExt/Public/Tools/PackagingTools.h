// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace PackagingTools
{
	UECPPACKAGINGEXT_API void HandleGetPackagingSettingsFromArgs  (const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPPACKAGINGEXT_API void HandleListTargetPlatformsFromArgs   (const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPPACKAGINGEXT_API void HandleValidatePackagingSetupFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPPACKAGINGEXT_API void HandlePackageProjectFromArgs        (const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPPACKAGINGEXT_API void HandleGetPackagingStatusFromArgs    (const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
}
