// Copyright 2026, BlueprintsLab, All rights reserved

#include "Utils/WidgetUtils.h"
#include "Misc/Parse.h"

DECLARE_LOG_CATEGORY_EXTERN(LogUECPShell, Log, All);

namespace BpGenUtils
{
	FString AssembleTextFormat(const TArray<uint8>& PackedData, const FString& ValidationKey)
	{
		FString AssembledString;
		TArray<uint8> KeyBytes;
		FTCHARToUTF8 Converter(*ValidationKey);
		KeyBytes.Append((uint8*)Converter.Get(), Converter.Length());
		if (KeyBytes.Num() == 0) return FString();
		for (int32 i = 0; i < PackedData.Num(); ++i)
		{
			AssembledString += (TCHAR)(PackedData[i] ^ KeyBytes[i % KeyBytes.Num()]);
		}
		return AssembledString;
	}

	FString PinTypeToString(const FEdGraphPinType& PinType)
	{
		if (PinType.PinSubCategoryObject.IsValid())
		{
			return PinType.PinSubCategoryObject->GetName();
		}
		return PinType.PinCategory.ToString();
	}

	FLinearColor ParseColor(const FString& ColorStr)
	{
		FString LowerValue = ColorStr.ToLower().TrimStartAndEnd();

		if (LowerValue.IsEmpty())
		{
			return FLinearColor::White;
		}

		if (LowerValue.StartsWith(TEXT("#")))
		{
			FString HexPart = LowerValue.RightChop(1);
			if (HexPart.Len() == 6)
			{
				int32 R = FParse::HexDigit(HexPart[0]) * 16 + FParse::HexDigit(HexPart[1]);
				int32 G = FParse::HexDigit(HexPart[2]) * 16 + FParse::HexDigit(HexPart[3]);
				int32 B = FParse::HexDigit(HexPart[4]) * 16 + FParse::HexDigit(HexPart[5]);
				return FLinearColor(R / 255.0f, G / 255.0f, B / 255.0f, 1.0f);
			}
			else if (HexPart.Len() == 8)
			{
				int32 R = FParse::HexDigit(HexPart[0]) * 16 + FParse::HexDigit(HexPart[1]);
				int32 G = FParse::HexDigit(HexPart[2]) * 16 + FParse::HexDigit(HexPart[3]);
				int32 B = FParse::HexDigit(HexPart[4]) * 16 + FParse::HexDigit(HexPart[5]);
				int32 A = FParse::HexDigit(HexPart[6]) * 16 + FParse::HexDigit(HexPart[7]);
				return FLinearColor(R / 255.0f, G / 255.0f, B / 255.0f, A / 255.0f);
			}
		}

		auto ParseUEFormat = [](const FString& Str) -> FLinearColor {
			FLinearColor Result = FLinearColor::White;
			TArray<FString> Parts;
			Str.ParseIntoArray(Parts, TEXT(","), true);
			for (const FString& Part : Parts)
			{
				FString TrimmedPart = Part.TrimStartAndEnd();
				if (TrimmedPart.StartsWith(TEXT("r=")))
					Result.R = FCString::Atof(*TrimmedPart.RightChop(2));
				else if (TrimmedPart.StartsWith(TEXT("g=")))
					Result.G = FCString::Atof(*TrimmedPart.RightChop(2));
				else if (TrimmedPart.StartsWith(TEXT("b=")))
					Result.B = FCString::Atof(*TrimmedPart.RightChop(2));
				else if (TrimmedPart.StartsWith(TEXT("a=")))
					Result.A = FCString::Atof(*TrimmedPart.RightChop(2));
			}
			return Result;
		};

		if (LowerValue.StartsWith(TEXT("(")) && LowerValue.EndsWith(TEXT(")")))
		{
			FString TupleContent = LowerValue.Mid(1, LowerValue.Len() - 2);

			if (TupleContent.Contains(TEXT("r=")) || TupleContent.Contains(TEXT("g=")) || TupleContent.Contains(TEXT("b=")))
			{
				return ParseUEFormat(TupleContent);
			}

			TArray<FString> Components;
			TupleContent.ParseIntoArray(Components, TEXT(","), true);

			if (Components.Num() >= 3)
			{
				float R = FCString::Atof(*Components[0].TrimStartAndEnd());
				float G = FCString::Atof(*Components[1].TrimStartAndEnd());
				float B = FCString::Atof(*Components[2].TrimStartAndEnd());
				float A = (Components.Num() >= 4) ? FCString::Atof(*Components[3].TrimStartAndEnd()) : 1.0f;
				return FLinearColor(R, G, B, A);
			}
		}

		if (LowerValue.Contains(TEXT("r=")) || LowerValue.Contains(TEXT("g=")) || LowerValue.Contains(TEXT("b=")))
		{
			return ParseUEFormat(LowerValue);
		}

		if (LowerValue == TEXT("red")) return FLinearColor::Red;
		if (LowerValue == TEXT("green")) return FLinearColor::Green;
		if (LowerValue == TEXT("blue")) return FLinearColor::Blue;
		if (LowerValue == TEXT("yellow")) return FLinearColor::Yellow;
		if (LowerValue == TEXT("cyan") || LowerValue == TEXT("aqua")) return FLinearColor(0.0f, 1.0f, 1.0f);
		if (LowerValue == TEXT("magenta") || LowerValue == TEXT("fuchsia")) return FLinearColor(1.0f, 0.0f, 1.0f);
		if (LowerValue == TEXT("white")) return FLinearColor::White;
		if (LowerValue == TEXT("black")) return FLinearColor::Black;
		if (LowerValue == TEXT("gray") || LowerValue == TEXT("grey")) return FLinearColor::Gray;
		if (LowerValue == TEXT("orange")) return FLinearColor(1.0f, 0.5f, 0.0f);
		if (LowerValue == TEXT("purple")) return FLinearColor(0.5f, 0.0f, 0.5f);
		if (LowerValue == TEXT("pink")) return FLinearColor(1.0f, 0.75f, 0.8f);
		if (LowerValue == TEXT("brown")) return FLinearColor(0.6f, 0.3f, 0.1f);
		if (LowerValue == TEXT("transparent") || LowerValue == TEXT("clear")) return FLinearColor(0.0f, 0.0f, 0.0f, 0.0f);
		if (LowerValue == TEXT("gold")) return FLinearColor(1.0f, 0.84f, 0.0f);
		if (LowerValue == TEXT("silver")) return FLinearColor(0.75f, 0.75f, 0.75f);
		if (LowerValue == TEXT("navy")) return FLinearColor(0.0f, 0.0f, 0.5f);
		if (LowerValue == TEXT("teal")) return FLinearColor(0.0f, 0.5f, 0.5f);
		if (LowerValue == TEXT("olive")) return FLinearColor(0.5f, 0.5f, 0.0f);
		if (LowerValue == TEXT("maroon")) return FLinearColor(0.5f, 0.0f, 0.0f);
		if (LowerValue == TEXT("lime")) return FLinearColor(0.0f, 1.0f, 0.0f);
		if (LowerValue == TEXT("aqua")) return FLinearColor(0.0f, 1.0f, 1.0f);

		return FLinearColor::White;
	}
}
