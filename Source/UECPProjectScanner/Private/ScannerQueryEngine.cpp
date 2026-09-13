// Copyright 2026, BlueprintsLab, All rights reserved

#include "ScannerQueryEngine.h"
#include "Services/ProjectIndexSchema.h"
#include "Managers/SettingsManager.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"

namespace UECPScannerQuery
{

namespace
{

	FString Normalise(const FString& In)
	{
		FString Out;
		Out.Reserve(In.Len());
		for (TCHAR C : In)
		{
			if (FChar::IsAlnum(C)) Out.AppendChar(FChar::ToLower(C));
			else if (FChar::IsWhitespace(C) || C == TEXT('_') || C == TEXT('-')) Out.AppendChar(TEXT(' '));
		}
		FString Compact;
		Compact.Reserve(Out.Len());
		bool bPrevSpace = true;
		for (TCHAR C : Out)
		{
			if (C == TEXT(' '))
			{
				if (!bPrevSpace) Compact.AppendChar(C);
				bPrevSpace = true;
			}
			else
			{
				Compact.AppendChar(C);
				bPrevSpace = false;
			}
		}
		return Compact.TrimStartAndEnd();
	}

	bool ContainsWord(const FString& Haystack, const TCHAR* Word)
	{
		const FString Needle = FString::Printf(TEXT(" %s "), Word);
		const FString Padded = FString::Printf(TEXT(" %s "), *Haystack);
		return Padded.Contains(Needle);
	}

	bool ContainsAny(const FString& Haystack, std::initializer_list<const TCHAR*> Words)
	{
		for (const TCHAR* W : Words) if (ContainsWord(Haystack, W)) return true;
		return false;
	}

	FString DisplayName(const FString& Path)
	{
		FString Name = Path;
		int32 LastSlash = INDEX_NONE;
		if (Name.FindLastChar(TEXT('/'), LastSlash)) Name = Name.RightChop(LastSlash + 1);
		int32 Dot = INDEX_NONE;
		if (Name.FindChar(TEXT('.'), Dot)) Name = Name.Left(Dot);
		return Name;
	}

	FString AssetLink(const FString& Path)
	{
		const FString Name = DisplayName(Path);
		return FString::Printf(TEXT("[%s](ue://asset?name=%s)"), *Name, *Name);
	}

	template<typename FnT>
	void ForEachAsset(const TSharedPtr<FJsonObject>& Index, FnT&& Fn)
	{
		if (!Index.IsValid()) return;
		for (const auto& Pair : Index->Values)
		{
			const FString Key(*Pair.Key);
			if (Key.StartsWith(TEXT("_"))) continue;
			if (!Pair.Value.IsValid()) continue;
			const TSharedPtr<FJsonObject>* Obj = nullptr;
			if (!Pair.Value->TryGetObject(Obj) || !Obj || !Obj->IsValid()) continue;
			Fn(Key, *Obj);
		}
	}

	int32 ReadCount(const TSharedPtr<FJsonObject>& Index, const TCHAR* Key)
	{
		const TSharedPtr<FJsonObject>* CountsObj = nullptr;
		if (!Index->TryGetObjectField(UECPProjectIndex::MetaCounts, CountsObj) || !CountsObj || !CountsObj->IsValid())
			return 0;
		double V = 0.0;
		(*CountsObj)->TryGetNumberField(Key, V);
		return (int32)V;
	}

	struct FTypeFilter
	{
		FString Type;
		FString Subtype;
	};

	bool ResolveTypeFilter(const FString& Normalised, FTypeFilter& Out)
	{
		if (ContainsAny(Normalised, { TEXT("widget"), TEXT("widgets"), TEXT("wbp"), TEXT("umg") }))
			{ Out.Type = UECPProjectIndex::Type::Blueprint; Out.Subtype = TEXT("Widget"); return true; }
		if (ContainsAny(Normalised, { TEXT("animbp"), TEXT("animations"), TEXT("animinstance"), TEXT("abp") }))
			{ Out.Type = UECPProjectIndex::Type::Blueprint; Out.Subtype = TEXT("AnimBP"); return true; }
		if (ContainsAny(Normalised, { TEXT("actor"), TEXT("actors") }))
			{ Out.Type = UECPProjectIndex::Type::Blueprint; Out.Subtype = TEXT("Actor"); return true; }
		if (ContainsAny(Normalised, { TEXT("behaviortree"), TEXT("behaviortrees"), TEXT("bt"), TEXT("behavior"), TEXT("tree"), TEXT("trees") }))
			{ Out.Type = UECPProjectIndex::Type::BehaviorTree; return true; }
		if (ContainsAny(Normalised, { TEXT("interface"), TEXT("interfaces"), TEXT("bpi") }))
			{ Out.Type = UECPProjectIndex::Type::Interface; return true; }
		if (ContainsAny(Normalised, { TEXT("enum"), TEXT("enums") }))
			{ Out.Type = UECPProjectIndex::Type::Enum; return true; }
		if (ContainsAny(Normalised, { TEXT("struct"), TEXT("structs") }))
			{ Out.Type = UECPProjectIndex::Type::Struct; return true; }
		if (ContainsAny(Normalised, { TEXT("dataasset"), TEXT("dataassets"), TEXT("da") }))
			{ Out.Type = UECPProjectIndex::Type::DataAsset; return true; }
		if (ContainsAny(Normalised, { TEXT("datatable"), TEXT("datatables"), TEXT("dt") }))
			{ Out.Type = UECPProjectIndex::Type::DataTable; return true; }
		if (ContainsAny(Normalised, { TEXT("level"), TEXT("levels"), TEXT("map"), TEXT("maps"), TEXT("world"), TEXT("worlds") }))
			{ Out.Type = UECPProjectIndex::Type::Level; return true; }
		if (ContainsAny(Normalised, { TEXT("material"), TEXT("materials"), TEXT("mat"), TEXT("mats") }))
			{ Out.Type = UECPProjectIndex::Type::Material; return true; }
		if (ContainsAny(Normalised, { TEXT("texture"), TEXT("textures"), TEXT("tex") }))
			{ Out.Type = UECPProjectIndex::Type::Texture; return true; }
		if (ContainsAny(Normalised, { TEXT("staticmesh"), TEXT("staticmeshes"), TEXT("sm") }))
			{ Out.Type = UECPProjectIndex::Type::StaticMesh; return true; }
		if (ContainsAny(Normalised, { TEXT("skeletalmesh"), TEXT("skeletalmeshes"), TEXT("skel"), TEXT("skeleton") }))
			{ Out.Type = UECPProjectIndex::Type::SkeletalMesh; return true; }
		if (ContainsAny(Normalised, { TEXT("mesh"), TEXT("meshes") }))
			{ Out.Type = UECPProjectIndex::Type::StaticMesh; return true; }
		if (ContainsAny(Normalised, { TEXT("blueprint"), TEXT("blueprints"), TEXT("bp"), TEXT("bps") }))
			{ Out.Type = UECPProjectIndex::Type::Blueprint; return true; }
		return false;
	}

	bool TryStats(const FString& N, const TSharedPtr<FJsonObject>& Index, FResult& Out)
	{
		const bool bIsStatsQuestion =
			ContainsAny(N, { TEXT("stats"), TEXT("summary"), TEXT("overview"), TEXT("counts") })
			|| (ContainsAny(N, { TEXT("how"), TEXT("what") }) && ContainsWord(N, TEXT("many")) && !ContainsWord(N, TEXT("with")));
		if (!bIsStatsQuestion) return false;

		struct FRow { const TCHAR* Label; int32 V; };
		const FRow Rows[] = {
			{ TEXT("Widgets"),        ReadCount(Index, UECPProjectIndex::Count::Widget) },
			{ TEXT("Actors"),         ReadCount(Index, UECPProjectIndex::Count::Actor) },
			{ TEXT("Anim BPs"),       ReadCount(Index, UECPProjectIndex::Count::AnimBp) },
			{ TEXT("Behavior Trees"), ReadCount(Index, UECPProjectIndex::Count::BehaviorTree) },
			{ TEXT("Enums"),          ReadCount(Index, UECPProjectIndex::Count::Enum) },
			{ TEXT("Structs"),        ReadCount(Index, UECPProjectIndex::Count::Struct) },
			{ TEXT("Interfaces"),     ReadCount(Index, UECPProjectIndex::Count::Interface) },
			{ TEXT("Data Assets"),    ReadCount(Index, UECPProjectIndex::Count::DataAsset) },
			{ TEXT("Data Tables"),    ReadCount(Index, UECPProjectIndex::Count::DataTable) },
			{ TEXT("Levels"),         ReadCount(Index, UECPProjectIndex::Count::Level) },
		};

		TStringBuilder<4096> Md;
		Md.Append(TEXT("**Project stats**\n\n"));

		TArray<FString> ChartParts;
		for (const FRow& R : Rows) if (R.V > 0) ChartParts.Add(FString::Printf(TEXT("%s %d"), R.Label, R.V));
		if (ChartParts.Num() > 0)
		{
			Md.Append(TEXT("```chart\n"));
			Md.Append(FString::Join(ChartParts, TEXT(", ")));
			Md.Append(TEXT("\n```\n\n"));
		}

		Md.Append(TEXT("| Category | Count |\n|---|---|\n"));
		for (const FRow& R : Rows) Md.Appendf(TEXT("| %s | %d |\n"), R.Label, R.V);

		const int32 CppFiles = ReadCount(Index, UECPProjectIndex::Count::CppFile);
		if (CppFiles > 0)
		{
			Md.Appendf(TEXT("\nAlso indexed: **%d** C++ headers (%d classes, %d structs, %d enums).\n"),
				CppFiles,
				ReadCount(Index, UECPProjectIndex::Count::CppClass),
				ReadCount(Index, UECPProjectIndex::Count::CppStruct),
				ReadCount(Index, UECPProjectIndex::Count::CppEnum));
		}

		Out.bHandled = true;
		Out.StepInfo = TEXT("Reading project counts...");
		Out.ResponseMarkdown = FString(Md);
		return true;
	}

	bool TryTopComplexity(const FString& N, const TSharedPtr<FJsonObject>& Index, FResult& Out)
	{
		const bool bComplexityWord = ContainsAny(N, { TEXT("complex"), TEXT("complexity"), TEXT("biggest"), TEXT("largest"), TEXT("heaviest") });
		const bool bTopWord        = ContainsAny(N, { TEXT("top"), TEXT("most") });
		if (!bComplexityWord && !bTopWord) return false;
		if (!bComplexityWord && bTopWord && !ContainsAny(N, { TEXT("blueprint"), TEXT("blueprints"), TEXT("bp"), TEXT("bps"), TEXT("actor"), TEXT("actors") })) return false;

		int32 N_TOP = 10;
		{
			TArray<FString> Tokens; N.ParseIntoArray(Tokens, TEXT(" "), true);
			for (int32 i = 0; i < Tokens.Num() - 1; ++i)
			{
				if (Tokens[i] == TEXT("top") && Tokens[i+1].IsNumeric())
				{
					N_TOP = FCString::Atoi(*Tokens[i+1]);
					N_TOP = FMath::Clamp(N_TOP, 1, 50);
					break;
				}
			}
		}

		TArray<TPair<FString, int32>> Ranked;
		ForEachAsset(Index, [&Ranked](const FString& Path, const TSharedPtr<FJsonObject>& Entry)
		{
			double Score = 0.0;
			if (Entry->TryGetNumberField(UECPProjectIndex::FieldComplexity, Score) && Score > 0)
				Ranked.Add({ Path, (int32)Score });
		});
		Ranked.Sort([](const TPair<FString,int32>& A, const TPair<FString,int32>& B) { return A.Value > B.Value; });

		TStringBuilder<4096> Md;
		Md.Appendf(TEXT("**Top %d most complex blueprints**\n\n"), FMath::Min(N_TOP, Ranked.Num()));
		if (Ranked.Num() == 0)
		{
			Md.Append(TEXT("No complexity data in the index — re-scan to populate.\n"));
		}
		else
		{
			Md.Append(TEXT("| Rank | Blueprint | Complexity |\n|---|---|---|\n"));
			const int32 Take = FMath::Min(N_TOP, Ranked.Num());
			for (int32 i = 0; i < Take; ++i)
				Md.Appendf(TEXT("| %d | %s | %d |\n"), i + 1, *AssetLink(Ranked[i].Key), Ranked[i].Value);
		}
		Out.bHandled = true;
		Out.StepInfo = TEXT("Ranking by complexity...");
		Out.ResponseMarkdown = FString(Md);
		return true;
	}

	bool TryListByType(const FString& N, const TSharedPtr<FJsonObject>& Index, FResult& Out)
	{
		const bool bListVerb = ContainsAny(N, { TEXT("list"), TEXT("show"), TEXT("all") });
		if (!bListVerb) return false;

		FTypeFilter Filter;
		if (!ResolveTypeFilter(N, Filter)) return false;

		TArray<TPair<FString, TSharedPtr<FJsonObject>>> Matches;
		ForEachAsset(Index, [&Filter, &Matches](const FString& Path, const TSharedPtr<FJsonObject>& Entry)
		{
			FString Type, Subtype;
			Entry->TryGetStringField(UECPProjectIndex::FieldType, Type);
			Entry->TryGetStringField(UECPProjectIndex::FieldSubtype, Subtype);
			if (Type != Filter.Type) return;
			if (!Filter.Subtype.IsEmpty() && Subtype != Filter.Subtype) return;
			Matches.Add({ Path, Entry });
		});

		Matches.Sort([](const TPair<FString,TSharedPtr<FJsonObject>>& A,
		                 const TPair<FString,TSharedPtr<FJsonObject>>& B) { return A.Key < B.Key; });

		const FString TypeLabel = Filter.Subtype.IsEmpty() ? Filter.Type : Filter.Subtype;

		TStringBuilder<8192> Md;
		Md.Appendf(TEXT("**%s (%d)**\n\n"), *TypeLabel, Matches.Num());

		if (Matches.Num() == 0)
		{
			Md.Appendf(TEXT("No %s found in the index.\n"), *TypeLabel);
			Out.bHandled = true;
			Out.ResponseMarkdown = FString(Md);
			return true;
		}

		const bool bWantParent = (Filter.Type == UECPProjectIndex::Type::Blueprint);
		if (bWantParent) Md.Append(TEXT("| Asset | Parent | Complexity |\n|---|---|---|\n"));
		else             Md.Append(TEXT("| Asset | Folder |\n|---|---|\n"));

		const int32 MaxRows = 100;
		const int32 Shown = FMath::Min(MaxRows, Matches.Num());
		for (int32 i = 0; i < Shown; ++i)
		{
			const FString& Path = Matches[i].Key;
			const TSharedPtr<FJsonObject>& Entry = Matches[i].Value;
			if (bWantParent)
			{
				FString Parent;
				Entry->TryGetStringField(UECPProjectIndex::FieldParent, Parent);
				double Complexity = 0.0;
				Entry->TryGetNumberField(UECPProjectIndex::FieldComplexity, Complexity);
				Md.Appendf(TEXT("| %s | %s | %d |\n"),
					*AssetLink(Path),
					Parent.IsEmpty() ? TEXT("?") : *Parent,
					(int32)Complexity);
			}
			else
			{
				FString Folder = FPaths::GetPath(Path);
				Md.Appendf(TEXT("| %s | %s |\n"), *AssetLink(Path), *Folder);
			}
		}
		if (Matches.Num() > Shown) Md.Appendf(TEXT("\n_(%d more not shown — narrow your query if you need a specific one.)_\n"), Matches.Num() - Shown);

		Out.bHandled = true;
		Out.StepInfo = FString::Printf(TEXT("Listing %s..."), *TypeLabel);
		Out.ResponseMarkdown = FString(Md);
		return true;
	}

	bool TryFindByName(const FString& N, const FString& OriginalQuestion,
		const TSharedPtr<FJsonObject>& Index, FResult& Out)
	{
		const bool bFindVerb = ContainsAny(N, { TEXT("find"), TEXT("show"), TEXT("describe") })
			|| (ContainsWord(N, TEXT("what")) && ContainsWord(N, TEXT("is")));
		if (!bFindVerb) return false;

		TArray<FString> Tokens;
		OriginalQuestion.ParseIntoArrayWS(Tokens);
		FString Candidate;
		for (const FString& T : Tokens)
		{
			const FString Lower = T.ToLower();
			if (Lower == TEXT("find") || Lower == TEXT("show") || Lower == TEXT("describe")
				|| Lower == TEXT("what") || Lower == TEXT("is") || Lower == TEXT("the")
				|| Lower == TEXT("me") || Lower == TEXT("a") || Lower == TEXT("an")) continue;
			if (T.Len() > Candidate.Len()) Candidate = T;
		}
		while (Candidate.Len() > 0 && !FChar::IsAlnum(Candidate[Candidate.Len()-1]))
			Candidate.LeftChopInline(1, EAllowShrinking::No);
		if (Candidate.Len() < 2) return false;

		struct FHit { FString Path; TSharedPtr<FJsonObject> Entry; int32 Score; };
		TArray<FHit> Hits;
		ForEachAsset(Index, [&Hits, &Candidate](const FString& Path, const TSharedPtr<FJsonObject>& Entry)
		{
			const FString Name = DisplayName(Path);
			int32 Score = 0;
			if (Name.Equals(Candidate, ESearchCase::IgnoreCase))        Score = 100;
			else if (Name.Contains(Candidate, ESearchCase::IgnoreCase)) Score = 50;
			else if (Candidate.Contains(Name, ESearchCase::IgnoreCase) && Name.Len() >= 3) Score = 25;
			if (Score > 0) Hits.Add({ Path, Entry, Score });
		});
		Hits.Sort([](const FHit& A, const FHit& B) { return A.Score > B.Score; });

		if (Hits.Num() == 0) return false;

		TStringBuilder<8192> Md;
		const FHit& Top = Hits[0];
		FString Type, Subtype, Parent;
		Top.Entry->TryGetStringField(UECPProjectIndex::FieldType, Type);
		Top.Entry->TryGetStringField(UECPProjectIndex::FieldSubtype, Subtype);
		Top.Entry->TryGetStringField(UECPProjectIndex::FieldParent, Parent);

		Md.Appendf(TEXT("**%s** — %s%s%s\n\n"),
			*DisplayName(Top.Path),
			*Type,
			Subtype.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" (%s)"), *Subtype),
			Parent.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" — inherits %s"), *Parent));
		Md.Appendf(TEXT("Path: `%s`\n\n"), *Top.Path);

		FString Summary;
		if (Top.Entry->TryGetStringField(UECPProjectIndex::FieldSummary, Summary) && !Summary.IsEmpty())
		{
			Md.Append(TEXT("```text\n"));
			const int32 MaxInline = 4000;
			Md.Append(Summary.Len() > MaxInline ? Summary.Left(MaxInline) + TEXT("\n... [truncated]") : Summary);
			Md.Append(TEXT("\n```\n"));
		}

		const TArray<TSharedPtr<FJsonValue>>* Issues = nullptr;
		if (Top.Entry->TryGetArrayField(UECPProjectIndex::FieldIssues, Issues) && Issues && Issues->Num() > 0)
		{
			Md.Appendf(TEXT("\n**Issues (%d)**\n\n| Severity | Code | Detail |\n|---|---|---|\n"), Issues->Num());
			for (const TSharedPtr<FJsonValue>& IssueVal : *Issues)
			{
				if (!IssueVal.IsValid()) continue;
				const TSharedPtr<FJsonObject>* IssueObj = nullptr;
				if (!IssueVal->TryGetObject(IssueObj) || !IssueObj || !IssueObj->IsValid()) continue;
				FString Sev, Code, Msg;
				(*IssueObj)->TryGetStringField(UECPProjectIndex::IssueSeverity, Sev);
				(*IssueObj)->TryGetStringField(UECPProjectIndex::IssueCode, Code);
				(*IssueObj)->TryGetStringField(UECPProjectIndex::IssueMessage, Msg);
				Md.Appendf(TEXT("| %s | `%s` | %s |\n"), *Sev, *Code, *Msg);
			}
		}

		if (Hits.Num() > 1)
		{
			Md.Appendf(TEXT("\n_%d other match%s:_ "), Hits.Num() - 1, Hits.Num() == 2 ? TEXT("") : TEXT("es"));
			TArray<FString> Other;
			const int32 ShowOther = FMath::Min(5, Hits.Num() - 1);
			for (int32 i = 1; i <= ShowOther; ++i) Other.Add(AssetLink(Hits[i].Path));
			Md.Append(FString::Join(Other, TEXT(" · ")));
			if (Hits.Num() - 1 > ShowOther) Md.Appendf(TEXT(" _and %d more_"), Hits.Num() - 1 - ShowOther);
			Md.Append(TEXT("\n"));
		}

		Out.bHandled = true;
		Out.StepInfo = FString::Printf(TEXT("Looking up '%s'..."), *Candidate);
		Out.ResponseMarkdown = FString(Md);
		return true;
	}

	bool TryIssues(const FString& N, const TSharedPtr<FJsonObject>& Index, FResult& Out)
	{
		if (!ContainsAny(N, { TEXT("issues"), TEXT("problems"), TEXT("performance"), TEXT("perf"), TEXT("slow"), TEXT("antipattern"), TEXT("antipatterns") }))
			return false;

		struct FAggIssue { FString Code; int32 Critical = 0; int32 Warn = 0; int32 Info = 0; FString Message; };
		TMap<FString, FAggIssue> ByCode;
		int32 AssetsWithIssues = 0;
		int32 TotalCritical = 0, TotalWarn = 0, TotalInfo = 0;

		ForEachAsset(Index, [&](const FString& Path, const TSharedPtr<FJsonObject>& Entry)
		{
			const TArray<TSharedPtr<FJsonValue>>* Issues = nullptr;
			if (!Entry->TryGetArrayField(UECPProjectIndex::FieldIssues, Issues) || !Issues || Issues->Num() == 0) return;
			AssetsWithIssues++;
			for (const TSharedPtr<FJsonValue>& IssueVal : *Issues)
			{
				if (!IssueVal.IsValid()) continue;
				const TSharedPtr<FJsonObject>* IssueObj = nullptr;
				if (!IssueVal->TryGetObject(IssueObj) || !IssueObj || !IssueObj->IsValid()) continue;
				FString Sev, Code, Msg;
				(*IssueObj)->TryGetStringField(UECPProjectIndex::IssueSeverity, Sev);
				(*IssueObj)->TryGetStringField(UECPProjectIndex::IssueCode, Code);
				(*IssueObj)->TryGetStringField(UECPProjectIndex::IssueMessage, Msg);
				FAggIssue& Agg = ByCode.FindOrAdd(Code);
				Agg.Code = Code;
				if (Agg.Message.IsEmpty()) Agg.Message = Msg;
				if (Sev == TEXT("critical")) { Agg.Critical++; TotalCritical++; }
				else if (Sev == TEXT("warn")) { Agg.Warn++; TotalWarn++; }
				else { Agg.Info++; TotalInfo++; }
			}
		});

		TStringBuilder<4096> Md;
		const double HealthScore = FMath::Clamp(
			10.0 - (TotalCritical * 1.0) - (TotalWarn * 0.2) - (TotalInfo * 0.05),
			0.0, 10.0);
		Md.Appendf(TEXT("**Performance summary** — health %.1f / 10\n\n"), HealthScore);
		Md.Appendf(TEXT("**%d** assets with issues. **%d** critical, **%d** warnings, **%d** info.\n\n"),
			AssetsWithIssues, TotalCritical, TotalWarn, TotalInfo);

		if (TotalCritical + TotalWarn + TotalInfo == 0)
		{
			Md.Append(TEXT("No issues detected. Use the **Performance Report** button for a fresh deep-dive.\n"));
		}
		else
		{
			TArray<FAggIssue> Sorted; ByCode.GenerateValueArray(Sorted);
			Sorted.Sort([](const FAggIssue& A, const FAggIssue& B)
			{
				const int32 Aw = A.Critical * 100 + A.Warn * 10 + A.Info;
				const int32 Bw = B.Critical * 100 + B.Warn * 10 + B.Info;
				return Aw > Bw;
			});

			Md.Append(TEXT("| Code | Critical | Warn | Info | Detail |\n|---|---|---|---|---|\n"));
			for (const FAggIssue& A : Sorted)
				Md.Appendf(TEXT("| `%s` | %d | %d | %d | %s |\n"),
					*A.Code, A.Critical, A.Warn, A.Info, *A.Message);

			Md.Append(TEXT("\n_Use the **Performance Report** button for the full per-asset list._\n"));
		}

		Out.bHandled = true;
		Out.StepInfo = TEXT("Aggregating issues from the index...");
		Out.ResponseMarkdown = FString(Md);
		return true;
	}

	bool TryWithTick(const FString& N, const TSharedPtr<FJsonObject>& Index, FResult& Out)
	{
		const bool bTickHit = ContainsWord(N, TEXT("tick"));
		if (!bTickHit) return false;
		if (!ContainsAny(N, { TEXT("with"), TEXT("uses"), TEXT("users"), TEXT("using"), TEXT("has"), TEXT("have"), TEXT("on"), TEXT("in"), TEXT("event") }))
			return false;

		struct FRow { FString Path; FString Worst; int32 IssueCount; };
		TArray<FRow> Rows;

		ForEachAsset(Index, [&Rows](const FString& Path, const TSharedPtr<FJsonObject>& Entry)
		{
			const TArray<TSharedPtr<FJsonValue>>* Issues = nullptr;
			if (!Entry->TryGetArrayField(UECPProjectIndex::FieldIssues, Issues) || !Issues) return;
			FString Worst;
			int32 Hits = 0;
			for (const TSharedPtr<FJsonValue>& Val : *Issues)
			{
				if (!Val.IsValid()) continue;
				const TSharedPtr<FJsonObject>* Obj = nullptr;
				if (!Val->TryGetObject(Obj) || !Obj || !Obj->IsValid()) continue;
				FString Code, Sev;
				(*Obj)->TryGetStringField(UECPProjectIndex::IssueCode, Code);
				(*Obj)->TryGetStringField(UECPProjectIndex::IssueSeverity, Sev);
				if (!Code.StartsWith(TEXT("tick_"))) continue;
				Hits++;
				if (Sev == TEXT("critical") || (Worst.IsEmpty() && Sev == TEXT("warn")) || Worst.IsEmpty())
					Worst = Sev;
			}
			if (Hits > 0) Rows.Add({ Path, Worst, Hits });
		});
		Rows.Sort([](const FRow& A, const FRow& B)
		{
			auto Rank = [](const FString& S){ return S == TEXT("critical") ? 2 : S == TEXT("warn") ? 1 : 0; };
			const int32 Ra = Rank(A.Worst), Rb = Rank(B.Worst);
			if (Ra != Rb) return Ra > Rb;
			return A.IssueCount > B.IssueCount;
		});

		TStringBuilder<4096> Md;
		Md.Appendf(TEXT("**Blueprints with Tick-related issues (%d)**\n\n"), Rows.Num());
		if (Rows.Num() == 0)
		{
			Md.Append(TEXT("No Tick antipatterns detected. Good news.\n"));
		}
		else
		{
			Md.Append(TEXT("| Asset | Worst Severity | Issue Count |\n|---|---|---|\n"));
			const int32 Take = FMath::Min(50, Rows.Num());
			for (int32 i = 0; i < Take; ++i)
				Md.Appendf(TEXT("| %s | %s | %d |\n"), *AssetLink(Rows[i].Path), *Rows[i].Worst, Rows[i].IssueCount);
			if (Rows.Num() > Take) Md.Appendf(TEXT("\n_(%d more not shown.)_\n"), Rows.Num() - Take);
		}
		Out.bHandled = true;
		Out.StepInfo = TEXT("Filtering by Tick issues...");
		Out.ResponseMarkdown = FString(Md);
		return true;
	}

	bool TryRecent(const FString& N, const TSharedPtr<FJsonObject>& Index, FResult& Out)
	{
		if (!ContainsAny(N, { TEXT("recent"), TEXT("recently"), TEXT("latest"), TEXT("newest") })) return false;

		struct FRow { FString Path; int64 Mtime; FString Type; };
		TArray<FRow> Rows;
		ForEachAsset(Index, [&Rows](const FString& Path, const TSharedPtr<FJsonObject>& Entry)
		{
			double Mtime = 0.0;
			Entry->TryGetNumberField(UECPProjectIndex::FieldMtime, Mtime);
			if (Mtime <= 0) return;
			FString Type;
			Entry->TryGetStringField(UECPProjectIndex::FieldType, Type);
			Rows.Add({ Path, (int64)Mtime, Type });
		});
		Rows.Sort([](const FRow& A, const FRow& B) { return A.Mtime > B.Mtime; });

		TStringBuilder<4096> Md;
		Md.Append(TEXT("**Recently modified assets**\n\n"));
		if (Rows.Num() == 0)
		{
			Md.Append(TEXT("No timestamps in the index. Re-scan to populate.\n"));
		}
		else
		{
			Md.Append(TEXT("| Asset | Type | Modified |\n|---|---|---|\n"));
			const int32 Take = FMath::Min(20, Rows.Num());
			for (int32 i = 0; i < Take; ++i)
			{
				const FDateTime When = FDateTime::FromUnixTimestamp(Rows[i].Mtime);
				Md.Appendf(TEXT("| %s | %s | %s |\n"),
					*AssetLink(Rows[i].Path), *Rows[i].Type, *When.ToString(TEXT("%Y-%m-%d %H:%M")));
			}
		}
		Out.bHandled = true;
		Out.StepInfo = TEXT("Sorting by modification time...");
		Out.ResponseMarkdown = FString(Md);
		return true;
	}

	bool TryUnused(const FString& N, const TSharedPtr<FJsonObject>& Index, FResult& Out)
	{
		if (!ContainsAny(N, { TEXT("unused"), TEXT("orphan"), TEXT("orphans"), TEXT("orphaned"), TEXT("dead"), TEXT("unreferenced") })) return false;

		FAssetRegistryModule& AR = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));

		struct FRow { FString Path; FString Type; };
		TArray<FRow> Rows;

		ForEachAsset(Index, [&Rows, &AR](const FString& Path, const TSharedPtr<FJsonObject>& Entry)
		{
			if (Path.StartsWith(TEXT("cpp://"))) return;

			FString Package = Path;
			int32 Dot = INDEX_NONE;
			if (Package.FindChar(TEXT('.'), Dot)) Package = Package.Left(Dot);

			TArray<FAssetIdentifier> Referencers;
			AR.Get().GetReferencers(FName(*Package), Referencers);
			int32 RealRefs = 0;
			for (const FAssetIdentifier& Ref : Referencers)
			{
				if (!Ref.IsPackage()) continue;
				const FString Name = Ref.PackageName.ToString();
				if (Name == Package) continue;
				if (Name.StartsWith(TEXT("/Script"))) continue;
				RealRefs++;
			}
			if (RealRefs == 0)
			{
				FString Type;
				Entry->TryGetStringField(UECPProjectIndex::FieldType, Type);
				Rows.Add({ Path, Type });
			}
		});
		Rows.Sort([](const FRow& A, const FRow& B) { return A.Path < B.Path; });

		TStringBuilder<4096> Md;
		Md.Appendf(TEXT("**Unused assets (%d)** — zero references from other assets.\n\n"), Rows.Num());
		Md.Append(TEXT("_Note: an asset is \"unused\" only with respect to other assets in the index. Code-only references from C++ aren't counted; level / GameMode wiring is._\n\n"));
		if (Rows.Num() == 0)
		{
			Md.Append(TEXT("No unreferenced assets found — everything in the index is wired up somewhere.\n"));
		}
		else
		{
			Md.Append(TEXT("| Asset | Type |\n|---|---|\n"));
			const int32 Take = FMath::Min(100, Rows.Num());
			for (int32 i = 0; i < Take; ++i)
				Md.Appendf(TEXT("| %s | %s |\n"), *AssetLink(Rows[i].Path), *Rows[i].Type);
			if (Rows.Num() > Take) Md.Appendf(TEXT("\n_(%d more not shown.)_\n"), Rows.Num() - Take);
		}
		Out.bHandled = true;
		Out.StepInfo = TEXT("Querying asset registry for referencers...");
		Out.ResponseMarkdown = FString(Md);
		return true;
	}

	FString ExtractNameAfterVerb(const FString& OriginalQuestion, std::initializer_list<const TCHAR*> StopWords)
	{
		TArray<FString> Tokens;
		OriginalQuestion.ParseIntoArrayWS(Tokens);
		FString Candidate;
		for (const FString& T : Tokens)
		{
			const FString Lower = T.ToLower();
			bool bSkip = false;
			for (const TCHAR* SW : StopWords) if (Lower == SW) { bSkip = true; break; }
			if (bSkip) continue;
			if (T.Len() > Candidate.Len()) Candidate = T;
		}
		while (Candidate.Len() > 0 && !FChar::IsAlnum(Candidate[Candidate.Len()-1]))
			Candidate.LeftChopInline(1, EAllowShrinking::No);
		return Candidate;
	}

	bool TryReferences(const FString& N, const FString& OriginalQuestion,
		const TSharedPtr<FJsonObject>& Index, FResult& Out)
	{
		const bool bRefVerb = ContainsAny(N, { TEXT("references"), TEXT("referenced"), TEXT("ref"), TEXT("refs"), TEXT("depends"), TEXT("dependents"), TEXT("dependencies") });
		if (!bRefVerb) return false;
		const bool bInverse = ContainsAny(N, { TEXT("referenced"), TEXT("dependents"), TEXT("used"), TEXT("uses") });

		const FString Candidate = ExtractNameAfterVerb(OriginalQuestion,
			{ TEXT("references"), TEXT("referenced"), TEXT("ref"), TEXT("refs"), TEXT("by"),
			  TEXT("depends"), TEXT("on"), TEXT("dependencies"), TEXT("dependents"),
			  TEXT("what"), TEXT("the"), TEXT("a"), TEXT("an"), TEXT("show"), TEXT("me"), TEXT("find") });
		if (Candidate.Len() < 2) return false;

		FString MatchedPath;
		ForEachAsset(Index, [&Candidate, &MatchedPath](const FString& Path, const TSharedPtr<FJsonObject>&)
		{
			if (!MatchedPath.IsEmpty()) return;
			if (DisplayName(Path).Equals(Candidate, ESearchCase::IgnoreCase)) MatchedPath = Path;
		});
		if (MatchedPath.IsEmpty())
		{
			ForEachAsset(Index, [&Candidate, &MatchedPath](const FString& Path, const TSharedPtr<FJsonObject>&)
			{
				if (!MatchedPath.IsEmpty()) return;
				if (DisplayName(Path).Contains(Candidate, ESearchCase::IgnoreCase)) MatchedPath = Path;
			});
		}
		if (MatchedPath.IsEmpty()) return false;

		FString Package = MatchedPath;
		int32 Dot = INDEX_NONE;
		if (Package.FindChar(TEXT('.'), Dot)) Package = Package.Left(Dot);

		FAssetRegistryModule& AR = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		TArray<FAssetIdentifier> Hits;
		if (bInverse) AR.Get().GetReferencers(FName(*Package), Hits);
		else          AR.Get().GetDependencies(FName(*Package), Hits);

		TArray<FString> Names;
		for (const FAssetIdentifier& H : Hits)
		{
			if (!H.IsPackage()) continue;
			const FString Name = H.PackageName.ToString();
			if (Name == Package) continue;
			if (Name.StartsWith(TEXT("/Script"))) continue;
			Names.Add(Name);
		}
		Names.Sort();

		TStringBuilder<4096> Md;
		Md.Appendf(TEXT("**%s of %s (%d)**\n\n"),
			bInverse ? TEXT("Referencers") : TEXT("Dependencies"),
			*DisplayName(MatchedPath), Names.Num());
		Md.Appendf(TEXT("Anchor: %s\n\n"), *AssetLink(MatchedPath));
		if (Names.Num() == 0)
		{
			Md.Append(bInverse
				? TEXT("Nothing in the project references this asset.\n")
				: TEXT("This asset has no hard dependencies on other content.\n"));
		}
		else
		{
			Md.Append(TEXT("| Asset |\n|---|\n"));
			const int32 Take = FMath::Min(80, Names.Num());
			for (int32 i = 0; i < Take; ++i) Md.Appendf(TEXT("| %s |\n"), *AssetLink(Names[i]));
			if (Names.Num() > Take) Md.Appendf(TEXT("\n_(%d more not shown.)_\n"), Names.Num() - Take);
		}
		Out.bHandled = true;
		Out.StepInfo = FString::Printf(TEXT("Resolving %s of %s..."),
			bInverse ? TEXT("referencers") : TEXT("dependencies"), *Candidate);
		Out.ResponseMarkdown = FString(Md);
		return true;
	}

	bool TryInherits(const FString& N, const FString& OriginalQuestion,
		const TSharedPtr<FJsonObject>& Index, FResult& Out)
	{
		const bool bVerb = ContainsAny(N, { TEXT("inherits"), TEXT("inherit"), TEXT("extends"), TEXT("subclass"), TEXT("subclasses"), TEXT("children") });
		if (!bVerb) return false;

		const FString Candidate = ExtractNameAfterVerb(OriginalQuestion,
			{ TEXT("inherits"), TEXT("inherit"), TEXT("extends"), TEXT("subclass"), TEXT("subclasses"),
			  TEXT("children"), TEXT("from"), TEXT("of"), TEXT("the"), TEXT("a"), TEXT("an"),
			  TEXT("what"), TEXT("show"), TEXT("me"), TEXT("list") });
		if (Candidate.Len() < 2) return false;

		struct FRow { FString Path; FString Parent; };
		TArray<FRow> Rows;
		ForEachAsset(Index, [&Candidate, &Rows](const FString& Path, const TSharedPtr<FJsonObject>& Entry)
		{
			FString Parent;
			Entry->TryGetStringField(UECPProjectIndex::FieldParent, Parent);
			if (Parent.IsEmpty()) return;
			if (Parent.Equals(Candidate, ESearchCase::IgnoreCase) || Parent.Contains(Candidate, ESearchCase::IgnoreCase))
				Rows.Add({ Path, Parent });
		});
		Rows.Sort([](const FRow& A, const FRow& B) { return A.Path < B.Path; });

		TStringBuilder<4096> Md;
		Md.Appendf(TEXT("**Subclasses of %s (%d)**\n\n"), *Candidate, Rows.Num());
		if (Rows.Num() == 0)
		{
			Md.Appendf(TEXT("No blueprints in the index inherit from `%s`.\n"), *Candidate);
		}
		else
		{
			Md.Append(TEXT("| Asset | Parent (exact) |\n|---|---|\n"));
			for (const FRow& R : Rows) Md.Appendf(TEXT("| %s | %s |\n"), *AssetLink(R.Path), *R.Parent);
		}
		Out.bHandled = true;
		Out.StepInfo = FString::Printf(TEXT("Filtering by parent class '%s'..."), *Candidate);
		Out.ResponseMarkdown = FString(Md);
		return true;
	}

	bool TryUses(const FString& N, const FString& OriginalQuestion,
		const TSharedPtr<FJsonObject>& Index, FResult& Out)
	{
		if (!ContainsAny(N, { TEXT("uses"), TEXT("using"), TEXT("calls"), TEXT("calling"), TEXT("call") })) return false;

		const FString Candidate = ExtractNameAfterVerb(OriginalQuestion,
			{ TEXT("uses"), TEXT("using"), TEXT("calls"), TEXT("calling"), TEXT("call"),
			  TEXT("what"), TEXT("the"), TEXT("a"), TEXT("an"), TEXT("show"), TEXT("me"), TEXT("find"),
			  TEXT("anything"), TEXT("blueprints"), TEXT("that"), TEXT("which") });
		if (Candidate.Len() < 3) return false;

		struct FRow { FString Path; FString Type; };
		TArray<FRow> Rows;
		ForEachAsset(Index, [&Candidate, &Rows](const FString& Path, const TSharedPtr<FJsonObject>& Entry)
		{
			FString Summary;
			Entry->TryGetStringField(UECPProjectIndex::FieldSummary, Summary);
			if (Summary.IsEmpty() || !Summary.Contains(Candidate, ESearchCase::IgnoreCase)) return;
			FString Type;
			Entry->TryGetStringField(UECPProjectIndex::FieldType, Type);
			Rows.Add({ Path, Type });
		});
		Rows.Sort([](const FRow& A, const FRow& B) { return A.Path < B.Path; });

		TStringBuilder<4096> Md;
		Md.Appendf(TEXT("**Assets containing `%s` (%d)**\n\n"), *Candidate, Rows.Num());
		if (Rows.Num() == 0)
		{
			Md.Appendf(TEXT("Nothing in the index mentions `%s`. Try a different spelling or use `find <name>` for asset lookup.\n"), *Candidate);
		}
		else
		{
			Md.Append(TEXT("| Asset | Type |\n|---|---|\n"));
			const int32 Take = FMath::Min(80, Rows.Num());
			for (int32 i = 0; i < Take; ++i) Md.Appendf(TEXT("| %s | %s |\n"), *AssetLink(Rows[i].Path), *Rows[i].Type);
			if (Rows.Num() > Take) Md.Appendf(TEXT("\n_(%d more not shown.)_\n"), Rows.Num() - Take);
		}
		Out.bHandled = true;
		Out.StepInfo = FString::Printf(TEXT("Searching summaries for '%s'..."), *Candidate);
		Out.ResponseMarkdown = FString(Md);
		return true;
	}

	bool TryFindSymbol(const FString& N, const FString& OriginalQuestion,
		const TSharedPtr<FJsonObject>& Index, FResult& Out)
	{
		const bool bFindVerb = ContainsWord(N, TEXT("find"));
		if (!bFindVerb) return false;

		const TCHAR* Section = nullptr;
		const TCHAR* Kind = nullptr;
		if (ContainsAny(N, { TEXT("variable"), TEXT("variables"), TEXT("var"), TEXT("vars") }))
			{ Section = TEXT("--- VARIABLES ---"); Kind = TEXT("variable"); }
		else if (ContainsAny(N, { TEXT("component"), TEXT("components") }))
			{ Section = TEXT("--- COMPONENTS ---"); Kind = TEXT("component"); }
		else if (ContainsAny(N, { TEXT("function"), TEXT("functions"), TEXT("func"), TEXT("funcs") }))
			{ Section = TEXT("--- Analyzing Graph: "); Kind = TEXT("function"); }
		else
			return false;

		const FString Candidate = ExtractNameAfterVerb(OriginalQuestion,
			{ TEXT("find"), TEXT("show"), TEXT("me"), TEXT("a"), TEXT("an"), TEXT("the"),
			  TEXT("with"), TEXT("named"), TEXT("called"),
			  TEXT("variable"), TEXT("variables"), TEXT("var"), TEXT("vars"),
			  TEXT("component"), TEXT("components"),
			  TEXT("function"), TEXT("functions"), TEXT("func"), TEXT("funcs"),
			  TEXT("blueprints"), TEXT("blueprint"), TEXT("bp"), TEXT("bps"),
			  TEXT("what"), TEXT("which"), TEXT("has"), TEXT("have"), TEXT("uses") });
		if (Candidate.Len() < 2) return false;

		struct FRow { FString Path; };
		TArray<FRow> Rows;
		ForEachAsset(Index, [&Candidate, Section, &Rows](const FString& Path, const TSharedPtr<FJsonObject>& Entry)
		{
			FString Summary;
			Entry->TryGetStringField(UECPProjectIndex::FieldSummary, Summary);
			if (Summary.IsEmpty()) return;
			const int32 SectionIdx = Summary.Find(Section);
			if (SectionIdx == INDEX_NONE) return;
			const int32 SectionEnd = Summary.Find(TEXT("\n---"), ESearchCase::IgnoreCase, ESearchDir::FromStart, SectionIdx + 4);
			const FString Body = (SectionEnd == INDEX_NONE)
				? Summary.RightChop(SectionIdx)
				: Summary.Mid(SectionIdx, SectionEnd - SectionIdx);
			if (Body.Contains(Candidate, ESearchCase::IgnoreCase)) Rows.Add({ Path });
		});
		Rows.Sort([](const FRow& A, const FRow& B) { return A.Path < B.Path; });

		TStringBuilder<4096> Md;
		Md.Appendf(TEXT("**Blueprints with %s containing `%s` (%d)**\n\n"), Kind, *Candidate, Rows.Num());
		if (Rows.Num() == 0)
		{
			Md.Appendf(TEXT("No matches for %s `%s`.\n"), Kind, *Candidate);
		}
		else
		{
			Md.Append(TEXT("| Asset |\n|---|\n"));
			const int32 Take = FMath::Min(80, Rows.Num());
			for (int32 i = 0; i < Take; ++i) Md.Appendf(TEXT("| %s |\n"), *AssetLink(Rows[i].Path));
			if (Rows.Num() > Take) Md.Appendf(TEXT("\n_(%d more not shown.)_\n"), Rows.Num() - Take);
		}
		Out.bHandled = true;
		Out.StepInfo = FString::Printf(TEXT("Searching for %s '%s'..."), Kind, *Candidate);
		Out.ResponseMarkdown = FString(Md);
		return true;
	}

	bool TryDiff(const FString& N, const TSharedPtr<FJsonObject>& , FResult& Out)
	{
		const bool bDiffWord = ContainsAny(N, { TEXT("diff"), TEXT("changed"), TEXT("changes"), TEXT("delta"), TEXT("drift") });
		const bool bSinceWord = ContainsWord(N, TEXT("since")) || ContainsWord(N, TEXT("last"));
		if (!bDiffWord && !(bSinceWord && ContainsWord(N, TEXT("scan")))) return false;

		const FString Dir = FPaths::ProjectSavedDir() / TEXT("AI") / TEXT("scan_snapshots");
		TArray<FString> Files;
		IFileManager::Get().FindFiles(Files, *(Dir / TEXT("snapshot-*.json")), true, false);
		Files.Sort();

		if (Files.Num() < 2)
		{
			Out.bHandled = true;
			Out.ResponseMarkdown = FString::Printf(
				TEXT("Need at least two scans to diff. Currently have **%d** snapshot(s) in `Saved/AI/scan_snapshots/`. Re-scan a few times (or after making changes) to build history.\n"),
				Files.Num());
			return true;
		}

		auto LoadSnap = [&Dir](const FString& File) -> TSharedPtr<FJsonObject>
		{
			FString Content;
			if (!FFileHelper::LoadFileToString(Content, *(Dir / File))) return nullptr;
			TSharedPtr<FJsonObject> Obj;
			const TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(Content);
			if (!FJsonSerializer::Deserialize(R, Obj)) return nullptr;
			return Obj;
		};

		const FString OldFile = Files[Files.Num() - 2];
		const FString NewFile = Files[Files.Num() - 1];
		TSharedPtr<FJsonObject> Old = LoadSnap(OldFile);
		TSharedPtr<FJsonObject> New_ = LoadSnap(NewFile);
		if (!Old.IsValid() || !New_.IsValid())
		{
			Out.bHandled = true;
			Out.ResponseMarkdown = TEXT("Snapshot files are unreadable. Re-scan to regenerate.\n");
			return true;
		}

		FString OldTime, NewTime;
		Old->TryGetStringField(TEXT("timestamp"),  OldTime);
		New_->TryGetStringField(TEXT("timestamp"), NewTime);

		auto ReadCounts = [](const TSharedPtr<FJsonObject>& Snap) -> TMap<FString, int32>
		{
			TMap<FString, int32> Out_;
			const TSharedPtr<FJsonObject>* Obj = nullptr;
			if (!Snap->TryGetObjectField(TEXT("counts"), Obj) || !Obj || !Obj->IsValid()) return Out_;
			for (const auto& KV : (*Obj)->Values)
			{
				const FString K(*KV.Key);
				double V = 0.0;
				if (KV.Value.IsValid() && KV.Value->TryGetNumber(V))
					Out_.Add(K, (int32)V);
			}
			return Out_;
		};
		auto ReadCodes = [](const TSharedPtr<FJsonObject>& Snap) -> TMap<FString, int32>
		{
			TMap<FString, int32> Out_;
			const TSharedPtr<FJsonObject>* Obj = nullptr;
			if (!Snap->TryGetObjectField(TEXT("issue_counts_by_code"), Obj) || !Obj || !Obj->IsValid()) return Out_;
			for (const auto& KV : (*Obj)->Values)
			{
				const FString K(*KV.Key);
				double V = 0.0;
				if (KV.Value.IsValid() && KV.Value->TryGetNumber(V))
					Out_.Add(K, (int32)V);
			}
			return Out_;
		};
		auto ReadHealth = [](const TSharedPtr<FJsonObject>& Snap) -> double
		{
			double V = 0.0;
			Snap->TryGetNumberField(TEXT("health_score"), V);
			return V;
		};

		const TMap<FString, int32> OldCounts = ReadCounts(Old);
		const TMap<FString, int32> NewCounts = ReadCounts(New_);
		const TMap<FString, int32> OldCodes  = ReadCodes(Old);
		const TMap<FString, int32> NewCodes  = ReadCodes(New_);
		const double OldHealth = ReadHealth(Old);
		const double NewHealth = ReadHealth(New_);

		TStringBuilder<4096> Md;
		Md.Appendf(TEXT("**Changes since previous scan**\n\n"));
		Md.Appendf(TEXT("Previous: `%s` · Current: `%s`\n\n"), *OldTime, *NewTime);

		const double HealthDelta = NewHealth - OldHealth;
		const TCHAR* HealthArrow = HealthDelta > 0.05 ? TEXT("↑") : HealthDelta < -0.05 ? TEXT("↓") : TEXT("·");
		Md.Appendf(TEXT("**Health:** %.1f → %.1f %s (%+.2f)\n\n"), OldHealth, NewHealth, HealthArrow, HealthDelta);

		TSet<FString> AllCountKeys;
		for (const auto& KV : OldCounts) AllCountKeys.Add(KV.Key);
		for (const auto& KV : NewCounts) AllCountKeys.Add(KV.Key);
		TArray<TTuple<FString, int32, int32, int32>> Rows;
		for (const FString& K : AllCountKeys)
		{
			const int32 O = OldCounts.FindRef(K);
			const int32 NN = NewCounts.FindRef(K);
			if (O != NN) Rows.Add(MakeTuple(K, O, NN, NN - O));
		}
		Rows.Sort([](const TTuple<FString,int32,int32,int32>& A, const TTuple<FString,int32,int32,int32>& B)
		{ return FMath::Abs(A.Get<3>()) > FMath::Abs(B.Get<3>()); });

		Md.Append(TEXT("**Asset count deltas**\n\n"));
		if (Rows.Num() == 0)
		{
			Md.Append(TEXT("No asset-count changes — same totals as the previous snapshot.\n\n"));
		}
		else
		{
			Md.Append(TEXT("| Category | Was | Now | Δ |\n|---|---|---|---|\n"));
			for (const auto& R : Rows)
				Md.Appendf(TEXT("| %s | %d | %d | %+d |\n"), *R.Get<0>(), R.Get<1>(), R.Get<2>(), R.Get<3>());
			Md.Append(TEXT("\n"));
		}

		TSet<FString> AllCodes;
		for (const auto& KV : OldCodes) AllCodes.Add(KV.Key);
		for (const auto& KV : NewCodes) AllCodes.Add(KV.Key);
		TArray<TTuple<FString, int32, int32, int32>> CodeRows;
		for (const FString& K : AllCodes)
		{
			const int32 O = OldCodes.FindRef(K);
			const int32 NN = NewCodes.FindRef(K);
			if (O != NN) CodeRows.Add(MakeTuple(K, O, NN, NN - O));
		}
		CodeRows.Sort([](const TTuple<FString,int32,int32,int32>& A, const TTuple<FString,int32,int32,int32>& B)
		{ return A.Get<3>() < B.Get<3>(); });

		Md.Append(TEXT("**Issue-code deltas**\n\n"));
		if (CodeRows.Num() == 0)
		{
			Md.Append(TEXT("Issue profile unchanged.\n"));
		}
		else
		{
			Md.Append(TEXT("| Issue Code | Was | Now | Δ |\n|---|---|---|---|\n"));
			for (const auto& R : CodeRows)
				Md.Appendf(TEXT("| `%s` | %d | %d | %+d |\n"), *R.Get<0>(), R.Get<1>(), R.Get<2>(), R.Get<3>());
		}

		Out.bHandled = true;
		Out.StepInfo = TEXT("Loading scan snapshots...");
		Out.ResponseMarkdown = FString(Md);
		return true;
	}

	bool TryGraph(const FString& N, const FString& OriginalQuestion,
		const TSharedPtr<FJsonObject>& Index, FResult& Out)
	{
		if (!ContainsAny(N, { TEXT("graph"), TEXT("diagram"), TEXT("draw"), TEXT("visualise"), TEXT("visualize") })) return false;

		const FString Candidate = ExtractNameAfterVerb(OriginalQuestion,
			{ TEXT("graph"), TEXT("diagram"), TEXT("draw"), TEXT("visualise"), TEXT("visualize"),
			  TEXT("for"), TEXT("of"), TEXT("the"), TEXT("a"), TEXT("an"), TEXT("me"),
			  TEXT("show"), TEXT("display") });
		if (Candidate.Len() < 2) return false;

		FString MatchedPath;
		TSharedPtr<FJsonObject> MatchedEntry;
		ForEachAsset(Index, [&](const FString& Path, const TSharedPtr<FJsonObject>& Entry)
		{
			if (!MatchedPath.IsEmpty()) return;
			if (DisplayName(Path).Equals(Candidate, ESearchCase::IgnoreCase))
			{
				MatchedPath = Path;
				MatchedEntry = Entry;
			}
		});
		if (MatchedPath.IsEmpty())
		{
			ForEachAsset(Index, [&](const FString& Path, const TSharedPtr<FJsonObject>& Entry)
			{
				if (!MatchedPath.IsEmpty()) return;
				if (DisplayName(Path).Contains(Candidate, ESearchCase::IgnoreCase))
				{
					MatchedPath = Path;
					MatchedEntry = Entry;
				}
			});
		}
		if (MatchedPath.IsEmpty() || !MatchedEntry.IsValid()) return false;

		const FString Self = DisplayName(MatchedPath);

		FString Parent;
		MatchedEntry->TryGetStringField(UECPProjectIndex::FieldParent, Parent);

		TArray<FString> Interfaces;
		{
			FString Summary;
			MatchedEntry->TryGetStringField(UECPProjectIndex::FieldSummary, Summary);
			const int32 SectionIdx = Summary.Find(TEXT("Implemented Interfaces:"));
			if (SectionIdx != INDEX_NONE)
			{
				const FString Tail = Summary.Mid(SectionIdx);
				const int32 SectionEnd = Tail.Find(TEXT("\n\n"));
				const FString Block = (SectionEnd == INDEX_NONE) ? Tail : Tail.Left(SectionEnd);
				TArray<FString> Lines;
				Block.ParseIntoArrayLines(Lines);
				for (const FString& L : Lines)
				{
					FString T = L.TrimStartAndEnd();
					if (T.StartsWith(TEXT("- "))) Interfaces.Add(T.RightChop(2).TrimStartAndEnd());
				}
			}
		}

		TArray<FString> Referencers;
		TArray<FString> Dependencies;
		if (!MatchedPath.StartsWith(TEXT("cpp://")))
		{
			FString Package = MatchedPath;
			int32 Dot = INDEX_NONE;
			if (Package.FindChar(TEXT('.'), Dot)) Package = Package.Left(Dot);

			FAssetRegistryModule& AR = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
			TArray<FAssetIdentifier> RawRef, RawDep;
			AR.Get().GetReferencers(FName(*Package), RawRef);
			AR.Get().GetDependencies(FName(*Package), RawDep);

			auto Collect = [&Package](const TArray<FAssetIdentifier>& In, TArray<FString>& Out_)
			{
				for (const FAssetIdentifier& H : In)
				{
					if (!H.IsPackage()) continue;
					const FString Name = H.PackageName.ToString();
					if (Name == Package) continue;
					if (Name.StartsWith(TEXT("/Script"))) continue;
					int32 LastSlash = INDEX_NONE;
					FString Short = Name;
					if (Short.FindLastChar(TEXT('/'), LastSlash)) Short = Short.RightChop(LastSlash + 1);
					Out_.Add(Short);
				}
			};
			Collect(RawRef, Referencers);
			Collect(RawDep, Dependencies);
		}

		const int32 MaxEdges = 8;
		const bool bRefOverflow = Referencers.Num() > MaxEdges;
		const bool bDepOverflow = Dependencies.Num() > MaxEdges;
		if (bRefOverflow) Referencers.SetNum(MaxEdges);
		if (bDepOverflow) Dependencies.SetNum(MaxEdges);

		TStringBuilder<4096> Md;
		Md.Appendf(TEXT("**Reference graph: %s**\n\n"), *Self);
		Md.Append(TEXT("```flow\n"));
		if (!Parent.IsEmpty()) Md.Appendf(TEXT("[parent: %s] -> [%s]\n"), *Parent, *Self);
		for (const FString& R : Referencers) Md.Appendf(TEXT("[%s] -> [%s]\n"), *R, *Self);
		for (const FString& D : Dependencies) Md.Appendf(TEXT("[%s] -> [%s]\n"), *Self, *D);
		for (const FString& I : Interfaces)  Md.Appendf(TEXT("[%s] -> [interface: %s]\n"), *Self, *I);
		if (Referencers.Num() == 0 && Dependencies.Num() == 0 && Interfaces.Num() == 0 && Parent.IsEmpty())
		{
			Md.Appendf(TEXT("[%s]\n"), *Self);
		}
		Md.Append(TEXT("```\n\n"));

		if (bRefOverflow || bDepOverflow)
		{
			Md.Append(TEXT("_Graph capped to keep it readable. Use `referenced by " + Self + "` or `references " + Self + "` for the full list._\n\n"));
		}
		Md.Appendf(TEXT("Anchor: %s\n"), *AssetLink(MatchedPath));

		Out.bHandled = true;
		Out.StepInfo = FString::Printf(TEXT("Building reference graph for %s..."), *Self);
		Out.ResponseMarkdown = FString(Md);
		return true;
	}

	bool TryHelp(const FString& N, const TSharedPtr<FJsonObject>& , FResult& Out)
	{
		if (!ContainsAny(N, { TEXT("help"), TEXT("commands"), TEXT("intents") })
			&& !(ContainsWord(N, TEXT("what")) && ContainsAny(N, { TEXT("can"), TEXT("do") })))
			return false;

		Out.bHandled = true;
		Out.StepInfo = TEXT("");
		Out.ResponseMarkdown = TEXT(
			"**Built-in answers** — these run instantly from the project index, no AI call:\n\n"
			"**Overview**\n"
			"- **`stats`** / `summary` / `overview` — counts + pie chart for every asset category\n"
			"- **`top 10`** / `most complex` — leaderboard of the heaviest blueprints\n"
			"- **`issues`** / `performance` / `problems` — per-code rollup of detected antipatterns\n"
			"- **`recent`** / `recently changed` — most recently modified assets\n"
			"- **`unused`** / `orphaned` — assets nothing else references\n"
			"- **`diff`** / `what changed` — health + count drift since the previous scan\n\n"
			"**Filter**\n"
			"- **`list widgets`** / `show actors` / `list behavior trees` — filter by type (widgets, actors, anim bps, behavior trees, enums, structs, interfaces, data assets, data tables, levels, materials, textures, static meshes, skeletal meshes)\n"
			"- **`with tick`** / `tick users` — blueprints with Tick-related issues\n\n"
			"**Lookup**\n"
			"- **`find <name>`** / `what is <name>` — fuzzy asset lookup with full summary + issues\n"
			"- **`find variable Health`** / `find component Mesh` / `find function Attack` — search inside blueprints\n"
			"- **`uses <X>`** / `calls <X>` — assets whose summary mentions X (function name, node, etc.)\n\n"
			"**Graph**\n"
			"- **`graph <asset>`** / `diagram <asset>` — visual flowchart of parent, referencers, dependencies, interfaces\n"
			"- **`references <asset>`** — what the asset depends on\n"
			"- **`referenced by <asset>`** / `dependents of <asset>` — what depends on the asset\n"
			"- **`inherits Character`** / `subclasses of Pawn` — class-hierarchy filter\n\n"
			"- **`help`** — this list\n\n"
			"For anything else, your question is forwarded to the AI with the relevant project context attached."
		);
		return true;
	}
}

FResult TryHandle(const FString& Question, const TSharedPtr<FJsonObject>& Index)
{
	FResult Result;
	if (!Index.IsValid()) return Result;

	const FString N = Normalise(Question);
	if (N.IsEmpty()) return Result;

	if (TryHelp(N, Index, Result))                     return Result;
	if (TryStats(N, Index, Result))                    return Result;
	if (TryIssues(N, Index, Result))                   return Result;
	if (TryDiff(N, Index, Result))                     return Result;
	if (TryWithTick(N, Index, Result))                 return Result;
	if (TryRecent(N, Index, Result))                   return Result;
	if (TryUnused(N, Index, Result))                   return Result;
	if (TryGraph(N, Question, Index, Result))          return Result;
	if (TryReferences(N, Question, Index, Result))     return Result;
	if (TryInherits(N, Question, Index, Result))       return Result;
	if (TryUses(N, Question, Index, Result))           return Result;
	if (TryFindSymbol(N, Question, Index, Result))     return Result;
	if (TryTopComplexity(N, Index, Result))            return Result;
	if (TryFindByName(N, Question, Index, Result))     return Result;
	if (TryListByType(N, Index, Result))               return Result;

	return Result;
}

EResponseMode GetResponseMode()
{
	FString ModeStr;
	GConfig->GetString(TEXT("BpGeneratorUltimate"), TEXT("Scanner_ResponseMode"),
		ModeStr, FSettingsManager::GetGlobalConfigPath());
	ModeStr = ModeStr.ToLower();
	if (ModeStr == TEXT("ai"))    return EResponseMode::AiOnly;
	if (ModeStr == TEXT("index")) return EResponseMode::IndexOnly;
	return EResponseMode::Auto;
}

}
