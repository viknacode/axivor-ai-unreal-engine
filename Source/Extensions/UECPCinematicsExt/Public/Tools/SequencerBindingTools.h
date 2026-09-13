// Copyright 2026, BlueprintsLab, All rights reserved.

#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace SequencerBindingTools
{

	UECPCINEMATICSEXT_API void HandleTagBindingFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPCINEMATICSEXT_API void HandleUntagBindingFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPCINEMATICSEXT_API void HandleFindBindingByTagFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPCINEMATICSEXT_API void HandleFindBindingsByTagFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPCINEMATICSEXT_API void HandleGetAllBindingTagsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPCINEMATICSEXT_API void HandleGetBindingTagsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPCINEMATICSEXT_API void HandleRemoveBindingTagFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPCINEMATICSEXT_API void HandleFixActorReferencesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPCINEMATICSEXT_API void HandleRebindComponentFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPCINEMATICSEXT_API void HandleRemoveInvalidBindingsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPCINEMATICSEXT_API void HandleReplaceBindingWithActorsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPCINEMATICSEXT_API void HandleAddActorsToBindingFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPCINEMATICSEXT_API void HandleRemoveActorsFromBindingFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPCINEMATICSEXT_API void HandleRemoveAllBindingsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPCINEMATICSEXT_API void HandleConvertToSpawnableFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPCINEMATICSEXT_API void HandleConvertToPossessableFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPCINEMATICSEXT_API void HandleChangeActorTemplateClassFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPCINEMATICSEXT_API void HandleSaveDefaultSpawnableStateFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);

	UECPCINEMATICSEXT_API void HandleGetCustomBindingTypeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError);
}
