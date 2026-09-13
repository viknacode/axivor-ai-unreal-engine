// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace MediaTools
{
	UECPMEDIAEXT_API void HandleCreateMediaPlayerFromArgs(
		const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPMEDIAEXT_API void HandleCreateFileMediaSourceFromArgs(
		const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPMEDIAEXT_API void HandleCreateStreamMediaSourceFromArgs(
		const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPMEDIAEXT_API void HandleCreateMediaTextureFromArgs(
		const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPMEDIAEXT_API void HandleLinkTextureToPlayer(
		const FString& TexturePath, const FString& PlayerPath,
		FString& OutJsonString, FString& OutError);

	UECPMEDIAEXT_API void HandleLinkTextureToPlayerFromArgs(
		const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPMEDIAEXT_API void HandleSetMediaPlayerProperties(
		const FString& PlayerPath, const TSharedPtr<FJsonObject>& Args,
		FString& OutJsonString, FString& OutError);

	UECPMEDIAEXT_API void HandleSetMediaPlayerPropertiesFromArgs(
		const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPMEDIAEXT_API void HandleGetMediaPlayerInfo(
		const FString& PlayerPath, FString& OutJsonString, FString& OutError);

	UECPMEDIAEXT_API void HandleGetMediaPlayerInfoFromArgs(
		const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPMEDIAEXT_API void HandleOpenMediaSource(
		const FString& PlayerPath, const FString& SourcePath,
		FString& OutJsonString, FString& OutError);

	UECPMEDIAEXT_API void HandleOpenMediaSourceFromArgs(
		const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
}
