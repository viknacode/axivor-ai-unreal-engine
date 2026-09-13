// Copyright 2026, BlueprintsLab, All rights reserved

#include "CppValidation.h"
#include "Misc/Paths.h"

namespace CppValidation
{

FString AutoFixMacros(const FString& Content)
{
	FString Fixed = Content;
	struct FMacroFix { const TCHAR* Wrong; const TCHAR* Right; };
	static const FMacroFix MacroFixes[] = {
		{ TEXT("declare_DYNAMIC_MULTICAST_DELEGATE"), TEXT("DECLARE_DYNAMIC_MULTICAST_DELEGATE") },
		{ TEXT("declare_MULTICAST_DELEGATE"),         TEXT("DECLARE_MULTICAST_DELEGATE") },
		{ TEXT("declare_DELEGATE"),                   TEXT("DECLARE_DELEGATE") },
		{ TEXT("declare_EVENT"),                      TEXT("DECLARE_EVENT") },
		{ TEXT("declare_LOG_CATEGORY"),               TEXT("DECLARE_LOG_CATEGORY") },
		{ TEXT("Declare_Dynamic_Multicast_Delegate"), TEXT("DECLARE_DYNAMIC_MULTICAST_DELEGATE") },
		{ TEXT("uclass("),                            TEXT("UCLASS(") },
		{ TEXT("ustruct("),                           TEXT("USTRUCT(") },
		{ TEXT("uenum("),                             TEXT("UENUM(") },
		{ TEXT("uproperty("),                         TEXT("UPROPERTY(") },
		{ TEXT("ufunction("),                         TEXT("UFUNCTION(") },
		{ TEXT("generated_body()"),                   TEXT("GENERATED_BODY()") },
		{ TEXT("Generated_Body()"),                   TEXT("GENERATED_BODY()") },
	};

	for (const auto& Fix : MacroFixes)
	{
		if (Fixed.Contains(Fix.Wrong))
		{
			Fixed = Fixed.Replace(Fix.Wrong, Fix.Right);
		}
	}
	return Fixed;
}

FValidationResult ValidateHeader(const FString& Content, const FString& FileName)
{
	FValidationResult R;

	if (!Content.Contains(TEXT("#pragma once")))
	{
		R.Errors.Add(TEXT("Missing #pragma once - required for all UE headers"));
	}

	const bool bHasUObjectMacro =
		Content.Contains(TEXT("UCLASS(")) ||
		Content.Contains(TEXT("USTRUCT(")) ||
		Content.Contains(TEXT("UENUM("));
	const FString GeneratedInclude = FPaths::GetBaseFilename(FileName) + TEXT(".generated.h");
	const bool bHasGeneratedInclude = Content.Contains(GeneratedInclude);

	if (bHasUObjectMacro && !bHasGeneratedInclude)
	{
		R.Errors.Add(FString::Printf(
			TEXT("Missing #include \"%s\" - required for UCLASS/USTRUCT/UENUM"), *GeneratedInclude));
	}

	if (bHasGeneratedInclude)
	{
		const int32 GenIdx = Content.Find(GeneratedInclude);
		const int32 LastIncludeIdx = Content.Find(TEXT("#include"), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
		if (LastIncludeIdx > GenIdx + GeneratedInclude.Len())
		{
			R.Warnings.Add(FString::Printf(
				TEXT("\"%s\" must be the LAST #include in the file"), *GeneratedInclude));
		}
	}

	if ((Content.Contains(TEXT("UCLASS(")) || Content.Contains(TEXT("USTRUCT("))) &&
		!Content.Contains(TEXT("GENERATED_BODY()")))
	{
		R.Errors.Add(TEXT("Missing GENERATED_BODY() - required inside every UCLASS/USTRUCT"));
	}

	if (!Content.Contains(TEXT("CoreMinimal.h")))
	{
		R.Warnings.Add(TEXT("Missing #include \"CoreMinimal.h\" - should be the first include in UE headers"));
	}

	if (Content.Contains(TEXT("declare_DYNAMIC")) || Content.Contains(TEXT("declare_MULTICAST")) ||
		Content.Contains(TEXT("declare_DELEGATE")) || Content.Contains(TEXT("Declare_Dynamic")))
	{
		R.Warnings.Add(TEXT("Fixed lowercase DECLARE_ macro(s) - UE macros must be ALL CAPS"));
	}
	if (Content.Contains(TEXT("uclass(")) || Content.Contains(TEXT("ustruct(")) ||
		Content.Contains(TEXT("uenum(")) || Content.Contains(TEXT("uproperty(")) ||
		Content.Contains(TEXT("ufunction(")) || Content.Contains(TEXT("generated_body()")))
	{
		R.Warnings.Add(TEXT("Fixed lowercase UE reflection macro(s) - UCLASS/UPROPERTY/etc. must be ALL CAPS"));
	}

	const FString Fixed = AutoFixMacros(Content);
	if (Fixed != Content)
	{
		R.FixedContent = Fixed;
	}

	return R;
}

FValidationResult ValidateSource(const FString& Content, const FString& FileName)
{
	FValidationResult R;

	const FString HeaderName = FPaths::GetBaseFilename(FileName) + TEXT(".h");
	if (!Content.Contains(HeaderName))
	{
		R.Warnings.Add(FString::Printf(
			TEXT("Missing #include \"%s\" - source files should include their header"), *HeaderName));
	}

	const FString Fixed = AutoFixMacros(Content);
	if (Fixed != Content)
	{
		R.Warnings.Add(TEXT("Fixed lowercase UE macro(s) in source file"));
		R.FixedContent = Fixed;
	}

	return R;
}

}
