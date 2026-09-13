// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace StructEnumTools
{
	UECPDATAEXT_API void HandleCreateStruct(const FString& StructName, const FString& SavePath, const TArray<TSharedPtr<FJsonValue>>& Variables, FString& OutAssetPath, FString& OutError);

	UECPDATAEXT_API void HandleCreateEnum(const FString& EnumName, const FString& SavePath, const TArray<FString>& Enumerators, FString& OutAssetPath, FString& OutError);

	UECPDATAEXT_API void HandleAddEnumValue(const FString& EnumPath, const FString& NewValue, FString& OutJsonString, FString& OutError);

	UECPDATAEXT_API void HandleAddStructMember(const FString& StructPath, const FString& MemberName, const FString& MemberType, FString& OutJsonString, FString& OutError);

	UECPDATAEXT_API void HandleGetEnumValues(const FString& EnumPath, FString& OutJsonString, FString& OutError);

	UECPDATAEXT_API void HandleGetStructMembers(const FString& StructPath, FString& OutJsonString, FString& OutError);

	UECPDATAEXT_API void HandleRemoveEnumValue(const FString& EnumPath, const FString& ValueName, FString& OutJsonString, FString& OutError);

	UECPDATAEXT_API void HandleRemoveStructMember(const FString& StructPath, const FString& MemberName, FString& OutJsonString, FString& OutError);

	UECPDATAEXT_API void HandleCreateStructFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPDATAEXT_API void HandleAddStructMemberFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPDATAEXT_API void HandleRemoveStructMemberFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPDATAEXT_API void HandleCreateEnumFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPDATAEXT_API void HandleAddEnumValueFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPDATAEXT_API void HandleRemoveEnumValueFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);

	UECPDATAEXT_API void HandleGetEnumValuesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
	UECPDATAEXT_API void HandleGetStructMembersFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError);
}
