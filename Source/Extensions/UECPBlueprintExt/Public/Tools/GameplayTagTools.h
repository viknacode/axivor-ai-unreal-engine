// Copyright 2026, BlueprintsLab, All rights reserved.

#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace GameplayTagTools
{
	UECPBLUEPRINTEXT_API void HandleAddGameplayTag(const FString& TagName, const FString& DevComment,
		FString& OutJsonString, FString& OutError);

	UECPBLUEPRINTEXT_API void HandleGetGameplayTags(FString& OutJsonString, FString& OutError);

	UECPBLUEPRINTEXT_API void HandleAssignGameplayTagToBlueprint(const FString& BlueprintPath,
		const FString& VariableName, const TArray<FString>& DefaultTags,
		FString& OutJsonString, FString& OutError);

	UECPBLUEPRINTEXT_API void HandleRemoveGameplayTag(const FString& TagName,
		FString& OutJsonString, FString& OutError);

	UECPBLUEPRINTEXT_API void HandleAddGameplayTagsBulk(const TArray<FString>& TagNames, const FString& DevComment,
		FString& OutJsonString, FString& OutError);

	UECPBLUEPRINTEXT_API void HandleFindReferencersByTagFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPBLUEPRINTEXT_API void HandleAddGameplayTagFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPBLUEPRINTEXT_API void HandleRemoveGameplayTagFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPBLUEPRINTEXT_API void HandleGetGameplayTagsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPBLUEPRINTEXT_API void HandleAssignGameplayTagToBlueprintFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPBLUEPRINTEXT_API void HandleAddGameplayTagsBulkFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
}
