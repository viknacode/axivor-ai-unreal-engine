// Copyright 2026, BlueprintsLab, All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace StringTableTools
{

	UECPTOOLS_API void HandleCreateStringTable(const FString& Name, const FString& SavePath,
		FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleAddStringTableEntry(const FString& AssetPath, const FString& Key, const FString& Value,
		FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleGetStringTableEntries(const FString& AssetPath,
		FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleRemoveStringTableEntry(const FString& AssetPath, const FString& Key,
		FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleEditStringTableEntry(const FString& AssetPath, const FString& Key, const FString& NewValue,
		FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleCreateStringTableFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleAddStringTableEntryFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleGetStringTableEntriesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPTOOLS_API void HandleRemoveStringTableEntryFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPTOOLS_API void HandleEditStringTableEntryFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
}
