// Copyright 2026, BlueprintsLab, All rights reserved

#include "HeaderScanner.h"

namespace HeaderScanner
{

namespace
{
	FString StripCommentsAndStrings(const FString& In)
	{
		const int32 Len = In.Len();
		FString Out;
		Out.Reserve(Len);

		int32 i = 0;
		auto IsLineEnd = [](TCHAR C) { return C == TEXT('\n') || C == TEXT('\r'); };

		while (i < Len)
		{
			const TCHAR C = In[i];
			const TCHAR Next = (i + 1 < Len) ? In[i + 1] : TEXT('\0');

			if (C == TEXT('/') && Next == TEXT('/'))
			{
				while (i < Len && !IsLineEnd(In[i])) { Out.AppendChar(TEXT(' ')); ++i; }
			}
			else if (C == TEXT('/') && Next == TEXT('*'))
			{
				Out.AppendChar(TEXT(' '));
				Out.AppendChar(TEXT(' '));
				i += 2;
				while (i < Len)
				{
					if (In[i] == TEXT('*') && i + 1 < Len && In[i + 1] == TEXT('/'))
					{
						Out.AppendChar(TEXT(' '));
						Out.AppendChar(TEXT(' '));
						i += 2;
						break;
					}
					Out.AppendChar(IsLineEnd(In[i]) ? In[i] : TEXT(' '));
					++i;
				}
			}
			else if (C == TEXT('"'))
			{
				Out.AppendChar(TEXT(' '));
				++i;
				while (i < Len && In[i] != TEXT('"'))
				{
					if (In[i] == TEXT('\\') && i + 1 < Len)
					{
						Out.AppendChar(IsLineEnd(In[i])     ? In[i]     : TEXT(' '));
						Out.AppendChar(IsLineEnd(In[i + 1]) ? In[i + 1] : TEXT(' '));
						i += 2;
						continue;
					}
					Out.AppendChar(IsLineEnd(In[i]) ? In[i] : TEXT(' '));
					++i;
				}
				if (i < Len) { Out.AppendChar(TEXT(' ')); ++i; }
			}
			else if (C == TEXT('\''))
			{
				Out.AppendChar(TEXT(' '));
				++i;
				while (i < Len && In[i] != TEXT('\''))
				{
					if (In[i] == TEXT('\\') && i + 1 < Len)
					{
						Out.AppendChar(IsLineEnd(In[i])     ? In[i]     : TEXT(' '));
						Out.AppendChar(IsLineEnd(In[i + 1]) ? In[i + 1] : TEXT(' '));
						i += 2;
						continue;
					}
					Out.AppendChar(IsLineEnd(In[i]) ? In[i] : TEXT(' '));
					++i;
				}
				if (i < Len) { Out.AppendChar(TEXT(' ')); ++i; }
			}
			else
			{
				Out.AppendChar(C);
				++i;
			}
		}
		check(Out.Len() == In.Len());
		return Out;
	}

	bool IsIdentifierStart(TCHAR C) { return FChar::IsAlpha(C) || C == TEXT('_'); }
	bool IsIdentifierPart (TCHAR C) { return FChar::IsAlnum(C) || C == TEXT('_'); }

	bool IsAtWordBoundary(const FString& Str, int32 Pos, const TCHAR* Word, int32 WordLen)
	{
		if (Pos < 0 || Pos + WordLen > Str.Len()) return false;
		for (int32 k = 0; k < WordLen; ++k)
		{
			if (Str[Pos + k] != Word[k]) return false;
		}
		if (Pos > 0 && IsIdentifierPart(Str[Pos - 1])) return false;
		const int32 After = Pos + WordLen;
		if (After < Str.Len() && IsIdentifierPart(Str[After])) return false;
		return true;
	}

	int32 FindMatchingParen(const FString& Str, int32 OpenOffset)
	{
		check(Str.IsValidIndex(OpenOffset) && Str[OpenOffset] == TEXT('('));
		int32 Depth = 1;
		for (int32 i = OpenOffset + 1; i < Str.Len(); ++i)
		{
			const TCHAR C = Str[i];
			if      (C == TEXT('(')) ++Depth;
			else if (C == TEXT(')'))
			{
				--Depth;
				if (Depth == 0) return i;
			}
		}
		return INDEX_NONE;
	}

	int32 FindMatchingBrace(const FString& Str, int32 OpenOffset)
	{
		check(Str.IsValidIndex(OpenOffset) && Str[OpenOffset] == TEXT('{'));
		int32 Depth = 1;
		for (int32 i = OpenOffset + 1; i < Str.Len(); ++i)
		{
			const TCHAR C = Str[i];
			if      (C == TEXT('{')) ++Depth;
			else if (C == TEXT('}'))
			{
				--Depth;
				if (Depth == 0) return i;
			}
		}
		return INDEX_NONE;
	}

	int32 SkipWhitespace(const FString& Str, int32 Pos)
	{
		while (Pos < Str.Len() && FChar::IsWhitespace(Str[Pos])) ++Pos;
		return Pos;
	}

	FString ReadIdentifier(const FString& Str, int32& Pos)
	{
		const int32 Start = Pos;
		if (Pos >= Str.Len() || !IsIdentifierStart(Str[Pos])) return FString();
		while (Pos < Str.Len() && IsIdentifierPart(Str[Pos])) ++Pos;
		return Str.Mid(Start, Pos - Start);
	}

	FString ReadUntil(const FString& Str, int32& Pos, TCHAR Term)
	{
		const int32 Start = Pos;
		int32 Depth = 0;
		while (Pos < Str.Len())
		{
			const TCHAR C = Str[Pos];
			if      (C == TEXT('(') || C == TEXT('[') || C == TEXT('<')) ++Depth;
			else if (C == TEXT(')') || C == TEXT(']') || C == TEXT('>')) --Depth;
			else if (Depth == 0 && C == Term)
			{
				FString Captured = Str.Mid(Start, Pos - Start).TrimStartAndEnd();
				++Pos;
				return Captured;
			}
			++Pos;
		}
		return Str.Mid(Start, Pos - Start).TrimStartAndEnd();
	}

	void StripLeadingQualifiers(FString& Decl, FUFunctionInfo& Out)
	{
		Decl = Decl.TrimStartAndEnd();
		while (true)
		{
			bool bMatched = false;
			for (const TCHAR* Q : { TEXT("virtual"), TEXT("static"), TEXT("inline"), TEXT("FORCEINLINE"), TEXT("explicit"), TEXT("constexpr") })
			{
				const int32 QLen = FCString::Strlen(Q);
				if (Decl.Len() > QLen && Decl.StartsWith(Q, ESearchCase::CaseSensitive)
					&& FChar::IsWhitespace(Decl[QLen]))
				{
					if (FCString::Strcmp(Q, TEXT("virtual")) == 0) Out.bVirtual = true;
					else if (FCString::Strcmp(Q, TEXT("static")) == 0) Out.bStatic = true;
					Decl.RightChopInline(QLen, EAllowShrinking::No);
					Decl.TrimStartInline();
					bMatched = true;
					break;
				}
			}
			if (!bMatched) break;
		}
	}

	bool ParseUProperty(const FString& Stripped, const FString& Original,
		int32 MacroOffset, int32 ClassEndOffset, FUPropertyInfo& Out)
	{
		const int32 ParenOpen  = Stripped.Find(TEXT("("), ESearchCase::CaseSensitive, ESearchDir::FromStart, MacroOffset);
		if (ParenOpen == INDEX_NONE || ParenOpen >= ClassEndOffset) return false;
		const int32 ParenClose = FindMatchingParen(Stripped, ParenOpen);
		if (ParenClose == INDEX_NONE || ParenClose >= ClassEndOffset) return false;

		Out.Specifiers = Stripped.Mid(ParenOpen + 1, ParenClose - ParenOpen - 1).TrimStartAndEnd();
		Out.StartOffset = MacroOffset;

		int32 DeclPos = SkipWhitespace(Stripped, ParenClose + 1);
		const int32 SemiPos = [&]() -> int32 {
			int32 P = DeclPos;
			int32 Depth = 0;
			while (P < Stripped.Len() && P < ClassEndOffset)
			{
				const TCHAR C = Stripped[P];
				if      (C == TEXT('(') || C == TEXT('[') || C == TEXT('<') || C == TEXT('{')) ++Depth;
				else if (C == TEXT(')') || C == TEXT(']') || C == TEXT('>') || C == TEXT('}')) --Depth;
				else if (Depth == 0 && C == TEXT(';')) return P;
				++P;
			}
			return INDEX_NONE;
		}();
		if (SemiPos == INDEX_NONE) return false;

		Out.EndOffset = SemiPos + 1;
		Out.FullText = Original.Mid(MacroOffset, Out.EndOffset - MacroOffset);

		FString DeclText = Stripped.Mid(DeclPos, SemiPos - DeclPos);
		int32 EqualPos = INDEX_NONE;
		{
			int32 Depth = 0;
			for (int32 P = 0; P < DeclText.Len(); ++P)
			{
				const TCHAR C = DeclText[P];
				if      (C == TEXT('(') || C == TEXT('[') || C == TEXT('<')) ++Depth;
				else if (C == TEXT(')') || C == TEXT(']') || C == TEXT('>')) --Depth;
				else if (Depth == 0 && C == TEXT('='))
				{
					EqualPos = P;
					break;
				}
			}
		}
		FString TypeAndName = (EqualPos != INDEX_NONE) ? DeclText.Left(EqualPos) : DeclText;
		if (EqualPos != INDEX_NONE)
		{
			Out.DefaultValue = DeclText.Mid(EqualPos + 1).TrimStartAndEnd();
		}

		TypeAndName = TypeAndName.TrimStartAndEnd();
		int32 NameStart = TypeAndName.Len();
		while (NameStart > 0 && IsIdentifierPart(TypeAndName[NameStart - 1])) --NameStart;
		if (NameStart >= TypeAndName.Len()) return false;
		Out.Name = TypeAndName.Mid(NameStart);
		Out.Type = TypeAndName.Left(NameStart).TrimStartAndEnd();
		return true;
	}

	bool ParseUFunction(const FString& Stripped, const FString& Original,
		int32 MacroOffset, int32 ClassEndOffset, FUFunctionInfo& Out)
	{
		const int32 ParenOpen  = Stripped.Find(TEXT("("), ESearchCase::CaseSensitive, ESearchDir::FromStart, MacroOffset);
		if (ParenOpen == INDEX_NONE || ParenOpen >= ClassEndOffset) return false;
		const int32 ParenClose = FindMatchingParen(Stripped, ParenOpen);
		if (ParenClose == INDEX_NONE || ParenClose >= ClassEndOffset) return false;

		Out.Specifiers  = Stripped.Mid(ParenOpen + 1, ParenClose - ParenOpen - 1).TrimStartAndEnd();
		Out.StartOffset = MacroOffset;

		int32 SigPos = SkipWhitespace(Stripped, ParenClose + 1);

		int32 FuncParenOpen = INDEX_NONE;
		{
			int32 Depth = 0;
			for (int32 P = SigPos; P < Stripped.Len() && P < ClassEndOffset; ++P)
			{
				const TCHAR C = Stripped[P];
				if      (C == TEXT('<')) ++Depth;
				else if (C == TEXT('>')) --Depth;
				else if (Depth == 0 && C == TEXT('('))
				{
					FuncParenOpen = P;
					break;
				}
			}
		}
		if (FuncParenOpen == INDEX_NONE) return false;
		const int32 FuncParenClose = FindMatchingParen(Stripped, FuncParenOpen);
		if (FuncParenClose == INDEX_NONE) return false;

		Out.Params = Stripped.Mid(FuncParenOpen + 1, FuncParenClose - FuncParenOpen - 1).TrimStartAndEnd();

		FString Prefix = Stripped.Mid(SigPos, FuncParenOpen - SigPos).TrimStartAndEnd();
		StripLeadingQualifiers(Prefix, Out);

		int32 NameStart = Prefix.Len();
		while (NameStart > 0 && IsIdentifierPart(Prefix[NameStart - 1])) --NameStart;
		if (NameStart >= Prefix.Len()) return false;
		Out.Name = Prefix.Mid(NameStart);
		Out.ReturnType = Prefix.Left(NameStart).TrimStartAndEnd();

		int32 TailPos = FuncParenClose + 1;
		while (TailPos < Stripped.Len() && TailPos < ClassEndOffset)
		{
			const TCHAR C = Stripped[TailPos];
			if (C == TEXT(';') || C == TEXT('{')) break;
			++TailPos;
		}
		FString Trailing = Stripped.Mid(FuncParenClose + 1, TailPos - FuncParenClose - 1);
		if (Trailing.Contains(TEXT("const"),    ESearchCase::CaseSensitive)) Out.bConst    = true;
		if (Trailing.Contains(TEXT("override"), ESearchCase::CaseSensitive)) Out.bOverride = true;

		if (TailPos < Stripped.Len() && Stripped[TailPos] == TEXT('{'))
		{
			const int32 BraceClose = FindMatchingBrace(Stripped, TailPos);
			if (BraceClose == INDEX_NONE) return false;
			int32 P = BraceClose + 1;
			P = SkipWhitespace(Stripped, P);
			if (P < Stripped.Len() && Stripped[P] == TEXT(';')) ++P;
			Out.EndOffset = P;
		}
		else if (TailPos < Stripped.Len() && Stripped[TailPos] == TEXT(';'))
		{
			Out.EndOffset = TailPos + 1;
		}
		else
		{
			return false;
		}

		Out.FullText = Original.Mid(MacroOffset, Out.EndOffset - MacroOffset);
		return true;
	}

	void ScanIncludesAndForwardDecls(const FString& Stripped, const FString& Original, FParseResult& Out)
	{
		Out.bHasPragmaOnce = Stripped.Contains(TEXT("#pragma once"), ESearchCase::CaseSensitive);

		int32 Pos = 0;
		const int32 Len = Stripped.Len();
		int32 LastIncludeEnd = INDEX_NONE;

		while (Pos < Len)
		{
			const int32 LineStart = Pos;
			while (Pos < Len && Stripped[Pos] != TEXT('\n')) ++Pos;
			const int32 LineEnd = Pos;
			if (Pos < Len) ++Pos;

			FString Line = Stripped.Mid(LineStart, LineEnd - LineStart).TrimStartAndEnd();

			if (Line.IsEmpty()) continue;

			if (Line.StartsWith(TEXT("#include")))
			{
				int32 OpenQuote = Line.Find(TEXT("\""));
				int32 OpenAngle = Line.Find(TEXT("<"));
				if (OpenQuote != INDEX_NONE)
				{
					const int32 CloseQuote = Line.Find(TEXT("\""), ESearchCase::CaseSensitive, ESearchDir::FromStart, OpenQuote + 1);
					if (CloseQuote != INDEX_NONE)
					{
						FString Inc = Line.Mid(OpenQuote + 1, CloseQuote - OpenQuote - 1);
						Out.Includes.Add(Inc);
						if (Inc.EndsWith(TEXT(".generated.h")))
						{
							Out.GeneratedHeaderInclude = Inc;
						}
					}
				}
				else if (OpenAngle != INDEX_NONE)
				{
					const int32 CloseAngle = Line.Find(TEXT(">"), ESearchCase::CaseSensitive, ESearchDir::FromStart, OpenAngle + 1);
					if (CloseAngle != INDEX_NONE)
					{
						Out.Includes.Add(Line.Mid(OpenAngle + 1, CloseAngle - OpenAngle - 1));
					}
				}
				LastIncludeEnd = LineEnd;
			}
			else if (Line.StartsWith(TEXT("class ")) || Line.StartsWith(TEXT("struct ")))
			{
				if (Line.EndsWith(TEXT(";")) && !Line.Contains(TEXT(":")) && !Line.Contains(TEXT("{")))
				{
					Out.ForwardDeclarations.Add(Line.LeftChop(1).TrimStartAndEnd());
				}
				else
				{
					break;
				}
			}
			else if (Line.StartsWith(TEXT("UCLASS(")) || Line.StartsWith(TEXT("USTRUCT(")) ||
			         Line.StartsWith(TEXT("UENUM("))  || Line.StartsWith(TEXT("UINTERFACE(")))
			{
				break;
			}
			else if (!Line.StartsWith(TEXT("//")) && !Line.StartsWith(TEXT("#"))
			         && !Line.StartsWith(TEXT("DECLARE_")))
			{
				break;
			}
		}

		Out.LastIncludeEndOffset = LastIncludeEnd;
	}
}

FParseResult ParseHeader(const FString& Content)
{
	FParseResult Out;
	Out.OriginalContent = Content;
	if (Content.IsEmpty()) return Out;

	const FString Stripped = StripCommentsAndStrings(Content);

	ScanIncludesAndForwardDecls(Stripped, Content, Out);

	struct FMacroEntry { const TCHAR* Macro; const TCHAR* Kind; };
	static const FMacroEntry Macros[] = {
		{ TEXT("UCLASS"),     TEXT("class")     },
		{ TEXT("USTRUCT"),    TEXT("struct")    },
		{ TEXT("UINTERFACE"), TEXT("interface") },
		{ TEXT("UENUM"),      TEXT("enum")      },
	};

	int32 ScanPos = 0;
	while (ScanPos < Stripped.Len())
	{
		int32 BestPos = INDEX_NONE;
		const FMacroEntry* BestMacro = nullptr;
		for (const FMacroEntry& M : Macros)
		{
			int32 P = ScanPos;
			while (P < Stripped.Len())
			{
				const int32 Found = Stripped.Find(M.Macro, ESearchCase::CaseSensitive, ESearchDir::FromStart, P);
				if (Found == INDEX_NONE) break;
				if (IsAtWordBoundary(Stripped, Found, M.Macro, FCString::Strlen(M.Macro)))
				{
					if (BestPos == INDEX_NONE || Found < BestPos)
					{
						BestPos = Found;
						BestMacro = &M;
					}
					break;
				}
				P = Found + 1;
			}
		}
		if (BestPos == INDEX_NONE) break;

		FClassBlock Block;
		Block.Kind = BestMacro->Kind;
		Block.MacroStartOffset = BestPos;

		const int32 SpecOpen = Stripped.Find(TEXT("("), ESearchCase::CaseSensitive, ESearchDir::FromStart, BestPos);
		if (SpecOpen == INDEX_NONE) break;
		const int32 SpecClose = FindMatchingParen(Stripped, SpecOpen);
		if (SpecClose == INDEX_NONE) break;
		Block.Specifiers = Stripped.Mid(SpecOpen + 1, SpecClose - SpecOpen - 1).TrimStartAndEnd();

		int32 ParsePos = SkipWhitespace(Stripped, SpecClose + 1);

		if (Block.Kind == TEXT("enum"))
		{
			if (Stripped.Mid(ParsePos, 5).StartsWith(TEXT("enum")))
			{
				ParsePos += 4;
				ParsePos = SkipWhitespace(Stripped, ParsePos);
				if (Stripped.Mid(ParsePos, 6).StartsWith(TEXT("class")))
				{
					ParsePos += 5;
					ParsePos = SkipWhitespace(Stripped, ParsePos);
				}
				Block.Name = ReadIdentifier(Stripped, ParsePos);
				ParsePos = SkipWhitespace(Stripped, ParsePos);
				if (ParsePos < Stripped.Len() && Stripped[ParsePos] == TEXT(':'))
				{
					++ParsePos;
					ParsePos = SkipWhitespace(Stripped, ParsePos);
					ReadIdentifier(Stripped, ParsePos);
					ParsePos = SkipWhitespace(Stripped, ParsePos);
				}
			}

			const int32 BodyOpen = Stripped.Find(TEXT("{"), ESearchCase::CaseSensitive, ESearchDir::FromStart, ParsePos);
			if (BodyOpen == INDEX_NONE) { ScanPos = BestPos + 1; continue; }
			const int32 BodyClose = FindMatchingBrace(Stripped, BodyOpen);
			if (BodyClose == INDEX_NONE) { ScanPos = BestPos + 1; continue; }
			Block.BodyStartOffset = BodyOpen;
			Block.BodyEndOffset   = BodyClose;
			Out.Classes.Add(MoveTemp(Block));
			ScanPos = BodyClose + 1;
			continue;
		}

		if (Stripped.Mid(ParsePos, 6).StartsWith(TEXT("class")))
		{
			ParsePos += 5;
		}
		else if (Stripped.Mid(ParsePos, 7).StartsWith(TEXT("struct")))
		{
			ParsePos += 6;
		}
		ParsePos = SkipWhitespace(Stripped, ParsePos);

		const int32 MaybeApiStart = ParsePos;
		FString MaybeApi = ReadIdentifier(Stripped, ParsePos);
		if (MaybeApi.EndsWith(TEXT("_API")))
		{
			Block.ApiMacro = MaybeApi;
			ParsePos = SkipWhitespace(Stripped, ParsePos);
		}
		else
		{
			ParsePos = MaybeApiStart;
		}

		Block.Name = ReadIdentifier(Stripped, ParsePos);
		ParsePos = SkipWhitespace(Stripped, ParsePos);

		if (ParsePos < Stripped.Len() && Stripped[ParsePos] == TEXT(':'))
		{
			++ParsePos;
			ParsePos = SkipWhitespace(Stripped, ParsePos);
			for (const TCHAR* Acc : { TEXT("public"), TEXT("protected"), TEXT("private"), TEXT("virtual") })
			{
				const int32 AccLen = FCString::Strlen(Acc);
				if (Stripped.Mid(ParsePos, AccLen).StartsWith(Acc) &&
					(ParsePos + AccLen >= Stripped.Len() || FChar::IsWhitespace(Stripped[ParsePos + AccLen])))
				{
					ParsePos += AccLen;
					ParsePos = SkipWhitespace(Stripped, ParsePos);
					break;
				}
			}
			Block.ParentName = ReadIdentifier(Stripped, ParsePos);
		}

		const int32 BodyOpen = Stripped.Find(TEXT("{"), ESearchCase::CaseSensitive, ESearchDir::FromStart, ParsePos);
		if (BodyOpen == INDEX_NONE) { ScanPos = BestPos + 1; continue; }
		const int32 BodyClose = FindMatchingBrace(Stripped, BodyOpen);
		if (BodyClose == INDEX_NONE) { ScanPos = BestPos + 1; continue; }

		Block.BodyStartOffset = BodyOpen;
		Block.BodyEndOffset   = BodyClose;

		{
			const int32 GBPos = Stripped.Find(TEXT("GENERATED_BODY"), ESearchCase::CaseSensitive, ESearchDir::FromStart, BodyOpen);
			if (GBPos != INDEX_NONE && GBPos < BodyClose)
			{
				Block.GeneratedBodyOffset = GBPos;
			}
		}

		int32 InnerPos = BodyOpen + 1;
		while (InnerPos < BodyClose)
		{
			const int32 PropPos = Stripped.Find(TEXT("UPROPERTY"), ESearchCase::CaseSensitive, ESearchDir::FromStart, InnerPos);
			const int32 FuncPos = Stripped.Find(TEXT("UFUNCTION"), ESearchCase::CaseSensitive, ESearchDir::FromStart, InnerPos);

			int32 NextPos = INDEX_NONE;
			bool bIsFunction = false;
			if (PropPos != INDEX_NONE && PropPos < BodyClose &&
				IsAtWordBoundary(Stripped, PropPos, TEXT("UPROPERTY"), 9))
			{
				NextPos = PropPos;
			}
			if (FuncPos != INDEX_NONE && FuncPos < BodyClose &&
				IsAtWordBoundary(Stripped, FuncPos, TEXT("UFUNCTION"), 9) &&
				(NextPos == INDEX_NONE || FuncPos < NextPos))
			{
				NextPos = FuncPos;
				bIsFunction = true;
			}
			if (NextPos == INDEX_NONE) break;

			if (bIsFunction)
			{
				FUFunctionInfo Fn;
				if (ParseUFunction(Stripped, Content, NextPos, BodyClose, Fn))
				{
					Block.Functions.Add(MoveTemp(Fn));
					InnerPos = Fn.EndOffset;
				}
				else
				{
					InnerPos = NextPos + 1;
				}
			}
			else
			{
				FUPropertyInfo Pr;
				if (ParseUProperty(Stripped, Content, NextPos, BodyClose, Pr))
				{
					Block.Properties.Add(MoveTemp(Pr));
					InnerPos = Pr.EndOffset;
				}
				else
				{
					InnerPos = NextPos + 1;
				}
			}
		}

		Out.Classes.Add(MoveTemp(Block));
		ScanPos = BodyClose + 1;
	}

	return Out;
}

const FClassBlock* FindClass(const FParseResult& Parsed, const FString& ClassName)
{
	for (const FClassBlock& Block : Parsed.Classes)
	{
		if (Block.Name == ClassName) return &Block;
	}
	return nullptr;
}

int32 GetMemberInsertionOffset(const FClassBlock& Class)
{
	return (Class.BodyEndOffset != INDEX_NONE) ? Class.BodyEndOffset : INDEX_NONE;
}

}
