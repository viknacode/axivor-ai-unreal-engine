// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "Services/IUECPExtensionService.h"

class FJsonObject;

namespace UECPExtensionManifest
{

	inline constexpr int32 CurrentManifestVersion = 1;

	UECPCORE_API bool TryParseDescriptor(
		const FString& JsonText,
		const FString& PluginDir,
		const FString& ManifestDir,
		FUECPExtensionDescriptor& OutDescriptor,
		FString& OutError);

	UECPCORE_API bool TryParseDescriptor(
		const FString& JsonText,
		const FString& PluginDir,
		FUECPExtensionDescriptor& OutDescriptor,
		FString& OutError);

	UECPCORE_API int32 ScanAndRegisterAll();

	UECPCORE_API bool IsEngineVersionInRange(const FString& MinVer, const FString& MaxVer);
}
