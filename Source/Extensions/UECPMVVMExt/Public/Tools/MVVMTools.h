// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace MVVMTools
{

	UECPMVVMEXT_API void HandleCreateViewModelFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPMVVMEXT_API void HandleAddViewModelFieldFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPMVVMEXT_API void HandleAddWidgetViewModelContextFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPMVVMEXT_API void HandleRemoveWidgetViewModelContextFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPMVVMEXT_API void HandleAddWidgetBindingFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPMVVMEXT_API void HandleListWidgetBindingsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);
}
