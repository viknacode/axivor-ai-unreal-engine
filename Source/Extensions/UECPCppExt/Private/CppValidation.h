// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"

namespace CppValidation
{
	struct FValidationResult
	{
		TArray<FString> Errors;
		TArray<FString> Warnings;
		FString FixedContent;
	};

	FString AutoFixMacros(const FString& Content);

	FValidationResult ValidateHeader(const FString& Content, const FString& FileName);

	FValidationResult ValidateSource(const FString& Content, const FString& FileName);
}
