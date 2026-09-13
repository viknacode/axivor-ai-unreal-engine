// Copyright 2026, BlueprintsLab, All rights reserved.

#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace RenderingTools
{
	UECPTOOLS_API void HandleConfigureLumen(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);
	UECPTOOLS_API void HandleConfigureNanite(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);
	UECPTOOLS_API void HandleSetRayTracing(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);
	UECPTOOLS_API void HandleConfigureGlobalIllumination(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);
	UECPTOOLS_API void HandleSetShadowQuality(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);
	UECPTOOLS_API void HandleSetAntiAliasing(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);
	UECPTOOLS_API void HandleSetScreenPercentage(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);
	UECPTOOLS_API void HandleSetRenderingQuality(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);
	UECPTOOLS_API void HandleGetRenderSettings(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPTOOLS_API void HandleCreateIESProfileFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);
	UECPTOOLS_API void HandleSpawnHDRIBackdropFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);
}
