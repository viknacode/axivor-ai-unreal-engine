// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"

namespace CppCodegen
{
	struct FAddUPropertySpec
	{
		FString Type;
		FString Name;
		FString Specifiers;
		FString DefaultValue;
		FString Comment;
	};

	struct FAddUFunctionSpec
	{
		FString ReturnType;
		FString Name;
		FString Params;
		FString Specifiers;
		bool    bVirtual  = false;
		bool    bStatic   = false;
		bool    bConst    = false;
		FString Comment;
	};

	struct FUClassSpec
	{
		FString ClassName;
		FString ParentClass;
		FString ApiMacro;
		FString ModuleSubfolder;
		FString Description;
		bool    bAbstract       = false;
		bool    bBlueprintable  = true;
		bool    bBlueprintType  = true;
		bool    bMinimalAPI     = false;
		FString DisplayName;
		FString Category;

		bool    bIsActorComponent = false;
		bool    bIsActor          = false;

		TArray<FAddUPropertySpec> Properties;
		TArray<FAddUFunctionSpec> Functions;
	};

	struct FUStructMemberSpec
	{
		FString Type;
		FString Name;
		FString DefaultValue;
		FString Specifiers;
		FString Comment;
	};

	struct FUStructSpec
	{
		FString StructName;
		FString ApiMacro;
		FString Description;
		bool    bBlueprintType = true;
		TArray<FUStructMemberSpec> Members;
	};

	struct FUEnumValueSpec
	{
		FString Name;
		FString DisplayName;
		FString Comment;
	};

	struct FUEnumSpec
	{
		FString EnumName;
		FString UnderlyingType;
		FString Description;
		bool    bBlueprintType = true;
		TArray<FUEnumValueSpec> Values;
	};

	struct FUInterfaceMethodSpec
	{
		FString Name;
		FString ReturnType;
		FString Params;
		FString Specifiers;
		FString Comment;
	};

	struct FUInterfaceSpec
	{
		FString InterfaceName;
		FString ApiMacro;
		FString Description;
		bool    bMinimalAPI = false;
		TArray<FUInterfaceMethodSpec> Methods;
	};

	UECPCPPEXT_API FString EmitUClassHeader(const FUClassSpec& Spec);

	UECPCPPEXT_API FString EmitUClassSource(const FUClassSpec& Spec);

	UECPCPPEXT_API FString EmitUStructHeader(const FUStructSpec& Spec);

	UECPCPPEXT_API FString EmitUEnumHeader(const FUEnumSpec& Spec);

	UECPCPPEXT_API FString EmitUInterfaceHeader(const FUInterfaceSpec& Spec);

	UECPCPPEXT_API FString EmitUInterfaceSource(const FUInterfaceSpec& Spec);

	UECPCPPEXT_API FString EmitUProperty(const FAddUPropertySpec& Spec);

	UECPCPPEXT_API FString EmitUFunctionDeclaration(const FAddUFunctionSpec& Spec);

	UECPCPPEXT_API FString EmitUFunctionDefinition(const FString& OwningClass, const FAddUFunctionSpec& Spec);
}
