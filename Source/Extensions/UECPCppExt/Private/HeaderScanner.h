// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"

namespace HeaderScanner
{
	struct FUPropertyInfo
	{
		FString Name;
		FString Type;
		FString Specifiers;
		FString DefaultValue;
		FString FullText;
		int32   StartOffset = INDEX_NONE;
		int32   EndOffset   = INDEX_NONE;
	};

	struct FUFunctionInfo
	{
		FString Name;
		FString ReturnType;
		FString Params;
		FString Specifiers;
		bool    bVirtual  = false;
		bool    bStatic   = false;
		bool    bConst    = false;
		bool    bOverride = false;
		FString FullText;
		int32   StartOffset = INDEX_NONE;
		int32   EndOffset   = INDEX_NONE;
	};

	struct FClassBlock
	{
		FString Name;
		FString Kind;
		FString ParentName;
		FString ApiMacro;
		FString Specifiers;

		TArray<FUPropertyInfo> Properties;
		TArray<FUFunctionInfo> Functions;

		int32 MacroStartOffset    = INDEX_NONE;
		int32 BodyStartOffset     = INDEX_NONE;
		int32 BodyEndOffset       = INDEX_NONE;
		int32 GeneratedBodyOffset = INDEX_NONE;
	};

	struct FParseResult
	{
		TArray<FString>      Includes;
		TArray<FString>      ForwardDeclarations;
		TArray<FClassBlock>  Classes;
		FString              GeneratedHeaderInclude;
		int32                LastIncludeEndOffset = INDEX_NONE;
		bool                 bHasPragmaOnce = false;
		FString              OriginalContent;
	};

	UECPCPPEXT_API FParseResult ParseHeader(const FString& Content);

	UECPCPPEXT_API const FClassBlock* FindClass(const FParseResult& Parsed, const FString& ClassName);

	UECPCPPEXT_API int32 GetMemberInsertionOffset(const FClassBlock& Class);
}
