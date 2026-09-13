// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace EditorUITools
{

	UECPEDITORUIEXT_API void HandleSnapshotFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPEDITORUIEXT_API void HandleObserveFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPEDITORUIEXT_API void HandleUnobserveFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPEDITORUIEXT_API void HandleListObserversFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPEDITORUIEXT_API void HandleScreenshotFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPEDITORUIEXT_API void HandleClickFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPEDITORUIEXT_API void HandleHoverFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPEDITORUIEXT_API void HandleTypeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPEDITORUIEXT_API void HandlePressKeyFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPEDITORUIEXT_API void HandleSelectOptionFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPEDITORUIEXT_API void HandleDragFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPEDITORUIEXT_API void HandleWindowsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPEDITORUIEXT_API void HandleWaitForFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPEDITORUIEXT_API void HandleFillFormFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);
}
