// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class UInputMappingContext;

namespace InputSystemTools
{
	UECPINPUTEXT_API void HandleCreateInputAction(const FString& ActionName, const FString& SavePath, const FString& ValueType, FString& OutJsonString, FString& OutError);

	UECPINPUTEXT_API void HandleCreateInputMappingContext(const FString& ContextName, const FString& SavePath, const TArray<TSharedPtr<FJsonValue>>& Mappings, FString& OutJsonString, FString& OutError);

	UECPINPUTEXT_API void HandleAddInputMapping(const FString& ContextPath, const FString& ActionPath, const FString& Key, const TArray<TSharedPtr<FJsonValue>>& Modifiers, const TArray<TSharedPtr<FJsonValue>>& Triggers, FString& OutJsonString, FString& OutError);

	UECPINPUTEXT_API void HandleApplyMappingsToContext(UInputMappingContext* Context, const TArray<TSharedPtr<FJsonValue>>& Mappings);

	UECPINPUTEXT_API void HandleGetInputMappingSummary(const FString& ContextPath, FString& OutJsonString, FString& OutError);

	UECPINPUTEXT_API void HandleRemoveInputMapping(const FString& ContextPath, const FString& ActionPath, const FString& Key, FString& OutJsonString, FString& OutError);

	UECPINPUTEXT_API void HandleSetInputActionProperties(const FString& ActionPath, const FString& ValueType,
		bool bSetConsumeInput, bool bConsumeInput,
		bool bSetTriggerWhenPaused, bool bTriggerWhenPaused,
		FString& OutJsonString, FString& OutError);

	UECPINPUTEXT_API void HandleGetInputActionSummary(const FString& ActionPath, FString& OutJsonString, FString& OutError);

	UECPINPUTEXT_API void HandleSetupEnhancedInput(const FString& SavePath, const TArray<TSharedPtr<FJsonValue>>& InputActionsJson, const FString& IMCName, const TArray<TSharedPtr<FJsonValue>>& MappingsJson, FString& OutJsonString, FString& OutError);

	UECPINPUTEXT_API void HandleCreateInputActionFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPINPUTEXT_API void HandleAddInputMappingFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPINPUTEXT_API void HandleRemoveInputMappingFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPINPUTEXT_API void HandleCreateInputMappingContextFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPINPUTEXT_API void HandleGetInputMappingSummaryFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPINPUTEXT_API void HandleSetInputActionPropertiesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPINPUTEXT_API void HandleGetInputActionSummaryFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPINPUTEXT_API void HandleSetupEnhancedInputFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
}
