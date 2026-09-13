// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace CommonUITools
{

	UECPUIEXT_API void HandleCreateCommonButtonFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPUIEXT_API void HandleCreateActivatableWidgetFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPUIEXT_API void HandleCreateCommonTextBlockFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPUIEXT_API void HandleCreateCommonTextStyleFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPUIEXT_API void HandleCreateCommonButtonStyleFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPUIEXT_API void HandleCreateInputActionDataTableFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPUIEXT_API void HandleAddInputActionRowFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPUIEXT_API void HandleSetButtonStyleFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPUIEXT_API void HandleCreateWidgetStackFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPUIEXT_API void HandleGetCommonUISummary(FString& OutJson, FString& OutError);
	UECPUIEXT_API void HandleGetCommonUISummaryFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPUIEXT_API void HandleConfigureCommonButtonFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPUIEXT_API void HandleSetButtonInputActionFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPUIEXT_API void HandleSetCommonTextStylePropertiesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPUIEXT_API void HandleConfigureActivatableWidgetFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPUIEXT_API void HandleSetInputActionRowDataFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPUIEXT_API void HandleSetButtonTextStyleFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);
}
