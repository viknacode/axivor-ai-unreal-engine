// Copyright 2026, BlueprintsLab, All rights reserved

#include "Tools/BpFuzzyResolver.h"

#include "Managers/SettingsManager.h"
#include "Misc/ConfigCacheIni.h"

namespace BpFuzzyResolver
{

FString NormalizeForSearch(const FString& Input)
{
	FString Result;
	Result.Reserve(Input.Len());
	for (TCHAR Ch : Input)
	{
		if (FChar::IsAlnum(Ch))
		{
			Result.AppendChar(FChar::ToLower(Ch));
		}
		else if (Ch == '_' || Ch == ' ')
		{
			Result.AppendChar(' ');
		}
	}
	return Result;
}

static bool ParseConvPattern(const FString& Lower, FString& OutSrc, FString& OutDst)
{
	int32 ConvIdx = INDEX_NONE;
	if (!Lower.FindLastChar('.', ConvIdx)) ConvIdx = -1;
	const FString Tail = Lower.Mid(ConvIdx + 1);
	if (!Tail.StartsWith(TEXT("conv_"))) return false;
	const FString Inner = Tail.RightChop(5);
	int32 ToIdx = INDEX_NONE;
	for (int32 i = 1; i + 2 <= Inner.Len(); ++i)
	{
		if (Inner[i] == TEXT('t') && Inner[i+1] == TEXT('o'))
		{
			ToIdx = i; break;
		}
	}
	if (ToIdx == INDEX_NONE) return false;
	OutSrc = Inner.Left(ToIdx);
	OutDst = Inner.Mid(ToIdx + 2);
	return !OutSrc.IsEmpty() && !OutDst.IsEmpty();
}

float ScoreMatch(const FString& Query, const FString& Candidate)
{
	FString QueryLower = Query.ToLower();
	FString NameLower = Candidate.ToLower();

	if (QueryLower == NameLower) return 1.0f;

	{
		FString QSrc, QDst, NSrc, NDst;
		if (ParseConvPattern(QueryLower, QSrc, QDst) && ParseConvPattern(NameLower, NSrc, NDst))
		{
			const bool bSrcMatch = (QSrc == NSrc);
			const bool bDstMatch = (QDst == NDst);
			const bool bFloatDoubleSrc =
				((QSrc == TEXT("float") && NSrc == TEXT("double")) ||
				 (QSrc == TEXT("double") && NSrc == TEXT("float")));
			if (!bDstMatch) return 0.0f;
			if (!bSrcMatch && !bFloatDoubleSrc) return 0.0f;
		}
	}

	int32 LastDotIdx = INDEX_NONE;
	NameLower.FindLastChar('.', LastDotIdx);
	int32 QueryDotIdx = INDEX_NONE;
	QueryLower.FindLastChar('.', QueryDotIdx);

	float SameLibraryBonus = 0.0f;
	{
		int32 QFirstDot = INDEX_NONE, NFirstDot = INDEX_NONE;
		QueryLower.FindChar(TEXT('.'), QFirstDot);
		NameLower.FindChar(TEXT('.'), NFirstDot);
		if (QFirstDot != INDEX_NONE && NFirstDot != INDEX_NONE &&
		    QueryDotIdx != INDEX_NONE && LastDotIdx != INDEX_NONE &&
		    QFirstDot < QueryDotIdx && NFirstDot < LastDotIdx)
		{
			const FString QLib = QueryLower.Mid(QFirstDot + 1, QueryDotIdx - QFirstDot - 1);
			const FString NLib = NameLower.Mid(NFirstDot + 1, LastDotIdx - NFirstDot - 1);
			if (!QLib.IsEmpty() && QLib == NLib)
			{
				SameLibraryBonus = 0.10f;
			}
		}
	}

	auto WithBonus = [SameLibraryBonus](float Base) {
		return FMath::Min(0.99f, Base + SameLibraryBonus);
	};

	if (NameLower.Contains(QueryLower)) return WithBonus(0.85f);

	if (QueryDotIdx != INDEX_NONE && LastDotIdx != INDEX_NONE)
	{
		const FString QueryFuncPart = QueryLower.Mid(QueryDotIdx + 1);
		const FString CandFuncPart = NameLower.Mid(LastDotIdx + 1);
		if (!QueryFuncPart.IsEmpty() && !CandFuncPart.IsEmpty())
		{
			if (QueryFuncPart == CandFuncPart) return WithBonus(0.92f);
			if (QueryFuncPart.Len() >= 4 && CandFuncPart.StartsWith(QueryFuncPart)) return WithBonus(0.86f);
			if (CandFuncPart.Len() >= 4 && QueryFuncPart.StartsWith(CandFuncPart)) return WithBonus(0.84f);
		}
	}

	if (LastDotIdx != INDEX_NONE)
	{
		FString FuncPart = NameLower.Mid(LastDotIdx + 1);
		if (FuncPart.Contains(QueryLower)) return WithBonus(0.88f);
		FString QueryAsUnderscore = QueryLower.Replace(TEXT(" "), TEXT("_"));
		if (FuncPart.Contains(QueryAsUnderscore)) return WithBonus(0.87f);
	}

	FString QueryNorm = NormalizeForSearch(Query);
	FString NameNorm = NormalizeForSearch(Candidate);

	if (NameNorm.Contains(QueryNorm)) return WithBonus(0.82f);

	if (LastDotIdx != INDEX_NONE)
	{
		FString FuncPartNorm = NormalizeForSearch(Candidate.Mid(LastDotIdx + 1));
		if (FuncPartNorm.Contains(QueryNorm)) return WithBonus(0.83f);
	}

	TArray<FString> QueryWords;
	QueryNorm.ParseIntoArray(QueryWords, TEXT(" "), true);
	if (QueryWords.Num() == 0) return 0.0f;

	TArray<FString> NameWords;
	NameNorm.ParseIntoArray(NameWords, TEXT(" "), true);

	if (QueryWords.Num() > 0 && NameWords.Num() > 0 && QueryWords[0] == NameWords[0])
	{
		int32 ExtraMatched = 0, ExtraTotal = 0;
		for (int32 i = 1; i < QueryWords.Num(); i++)
		{
			if (QueryWords[i].Len() < 2) continue;
			ExtraTotal++;
			if (NameNorm.Contains(QueryWords[i])) ExtraMatched++;
		}
		float Coverage = ExtraTotal > 0 ? (float)ExtraMatched / (float)ExtraTotal : 1.0f;
		return 0.85f + Coverage * 0.1f;
	}

	int32 MatchedWords = 0;
	for (const FString& QWord : QueryWords)
	{
		if (QWord.Len() < 2) continue;
		for (const FString& NWord : NameWords)
		{
			if (NWord == QWord || NWord.StartsWith(QWord) || QWord.StartsWith(NWord) || NWord.Contains(QWord))
			{
				MatchedWords++;
				break;
			}
		}
	}

	int32 EffectiveQueryWords = 0;
	for (const FString& QWord : QueryWords) { if (QWord.Len() >= 2) EffectiveQueryWords++; }
	if (EffectiveQueryWords == 0) return 0.0f;

	float WordScore = (float)MatchedWords / (float)EffectiveQueryWords;
	if (WordScore >= 1.0f) return 0.75f;
	if (WordScore > 0.0f) return WordScore * 0.5f;

	return 0.0f;
}

TArray<FResolvedCandidate> RankCandidates(const FString& Query, const TArray<FString>& Candidates, int32 TopN)
{
	TArray<FResolvedCandidate> Scored;
	Scored.Reserve(Candidates.Num());

	FString PrefixFilter;
	int32 FirstDot;
	if (Query.FindChar(TEXT('.'), FirstDot) && FirstDot > 0 && FirstDot < 6)
	{
		PrefixFilter = Query.Left(FirstDot + 1).ToLower();
	}

	for (const FString& Candidate : Candidates)
	{
		if (!PrefixFilter.IsEmpty() && !Candidate.StartsWith(PrefixFilter, ESearchCase::IgnoreCase))
		{
			continue;
		}
		const float Confidence = ScoreMatch(Query, Candidate);
		if (Confidence > 0.0f)
		{
			Scored.Add({ Candidate, Confidence });
		}
	}

	Scored.Sort([](const FResolvedCandidate& A, const FResolvedCandidate& B)
	{
		if (!FMath::IsNearlyEqual(A.Confidence, B.Confidence)) return A.Confidence > B.Confidence;
		return A.CandidateKey.Len() < B.CandidateKey.Len();
	});

	if (Scored.Num() > TopN)
	{
		Scored.SetNum(TopN);
	}
	return Scored;
}

EPreFlightMode GetPreFlightMode()
{
	FString ModeStr;
	GConfig->GetString(TEXT("BpGeneratorUltimate"), TEXT("PreFlightMode"), ModeStr,
		FSettingsManager::GetGlobalConfigPath());

	ModeStr = ModeStr.TrimStartAndEnd().ToLower();
	if (ModeStr == TEXT("off") || ModeStr == TEXT("disabled")) return EPreFlightMode::Off;
	if (ModeStr == TEXT("conservative")) return EPreFlightMode::Conservative;
	return EPreFlightMode::Aggressive;
}

float GetMinConfidence()
{
	float Value = 0.80f;
	GConfig->GetFloat(TEXT("BpGeneratorUltimate"), TEXT("HandleResolverMinConfidence"), Value,
		FSettingsManager::GetGlobalConfigPath());
	return FMath::Clamp(Value, 0.5f, 1.0f);
}

}
