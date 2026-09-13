// Copyright 2026, BlueprintsLab, All rights reserved.

#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace LiveLinkTools
{

	UECPLIVELINKEXT_API void HandleListLiveLinkSources(FString& OutJson, FString& OutError);

	UECPLIVELINKEXT_API void HandleListLiveLinkSubjects(bool bIncludeDisabled, FString& OutJson, FString& OutError);

	UECPLIVELINKEXT_API void HandleAddMessageBusSourceFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPLIVELINKEXT_API void HandleRemoveLiveLinkSourceFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPLIVELINKEXT_API void HandleGetSubjectFrameDataFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPLIVELINKEXT_API void HandleCreateLiveLinkPreset(const FString& Name, const FString& SavePath, FString& OutJson, FString& OutError);

	UECPLIVELINKEXT_API void HandleApplyLiveLinkPreset(const FString& PresetPath, FString& OutJson, FString& OutError);

	UECPLIVELINKEXT_API void HandleGetLiveLinkSummary(FString& OutJson, FString& OutError);

	UECPLIVELINKEXT_API void HandleListLiveLinkSourcesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);
	UECPLIVELINKEXT_API void HandleListLiveLinkSubjectsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);
	UECPLIVELINKEXT_API void HandleCreateLiveLinkPresetFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);
	UECPLIVELINKEXT_API void HandleApplyLiveLinkPresetFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);
	UECPLIVELINKEXT_API void HandleGetLiveLinkSummaryFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);
}
