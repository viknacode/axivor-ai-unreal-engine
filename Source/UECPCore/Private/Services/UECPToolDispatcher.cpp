// Copyright 2026, BlueprintsLab, All rights reserved

#include "UECPToolDispatcher.h"
#include "UECPCoreModule.h"
#include "Services/IUECPExtensionService.h"

#include "Async/Async.h"
#include "Async/TaskGraphInterfaces.h"
#include "HAL/Event.h"
#include "HAL/PlatformProcess.h"

#include <initializer_list>

namespace
{
	int32 EditDistanceCI(const FString& A, const FString& B)
	{
		const int32 La = A.Len();
		const int32 Lb = B.Len();
		if (La == 0 || Lb == 0) return TNumericLimits<int32>::Max();

		TArray<int32> Prev, Curr;
		Prev.SetNumUninitialized(Lb + 1);
		Curr.SetNumUninitialized(Lb + 1);
		for (int32 j = 0; j <= Lb; ++j) Prev[j] = j;

		for (int32 i = 1; i <= La; ++i)
		{
			Curr[0] = i;
			const TCHAR Ca = FChar::ToLower(A[i - 1]);
			for (int32 j = 1; j <= Lb; ++j)
			{
				const TCHAR Cb = FChar::ToLower(B[j - 1]);
				const int32 Cost = (Ca == Cb) ? 0 : 1;
				Curr[j] = FMath::Min3(
					Prev[j] + 1,
					Curr[j - 1] + 1,
					Prev[j - 1] + Cost
				);
			}
			Swap(Prev, Curr);
		}
		return Prev[Lb];
	}

	TSet<FString> Tokenise(const FString& In)
	{
		TSet<FString> Out;
		TArray<FString> Parts;
		In.ToLower().ParseIntoArray(Parts, TEXT("_"), true);
		for (FString& P : Parts)
		{
			P.TrimStartAndEndInline();
			if (P.Len() >= 2) Out.Add(P);
		}
		return Out;
	}

	float JaccardSimilarity(const TSet<FString>& A, const TSet<FString>& B)
	{
		if (A.Num() == 0 || B.Num() == 0) return 0.0f;
		int32 Intersection = 0;
		for (const FString& T : A) if (B.Contains(T)) ++Intersection;
		const int32 Union = A.Num() + B.Num() - Intersection;
		return Union > 0 ? (float)Intersection / (float)Union : 0.0f;
	}

	FString BuildSuggestionsHint(const FString& Query, const TArray<FName>& Known)
	{
		if (Query.IsEmpty() || Known.Num() == 0) return FString();

		const TSet<FString> QueryTokens = Tokenise(Query);

		struct FCandidate { FString Name; float Score; };
		TArray<FCandidate> Scored;
		Scored.Reserve(Known.Num());

		const int32 MaxAllowedDist = FMath::Max(3, Query.Len() / 2);
		for (const FName& KnownName : Known)
		{
			const FString Name = KnownName.ToString();
			const int32 Dist = EditDistanceCI(Query, Name);
			const TSet<FString> NameTokens = Tokenise(Name);
			const float Jaccard = JaccardSimilarity(QueryTokens, NameTokens);
			const bool bContains = Name.Contains(Query, ESearchCase::IgnoreCase)
				|| Query.Contains(Name, ESearchCase::IgnoreCase);

			int32 LongestSharedTok = 0;
			for (const FString& QT : QueryTokens)
				if (NameTokens.Contains(QT)) LongestSharedTok = FMath::Max(LongestSharedTok, QT.Len());

			const bool bMeaningful = Dist <= MaxAllowedDist
				|| Jaccard >= 0.25f
				|| bContains;
			if (!bMeaningful) continue;

			float Score = (float)Dist
				- (Jaccard * 16.0f)
				- ((float)LongestSharedTok * 2.0f)
				- (bContains ? 3.0f : 0.0f);
			Scored.Add({Name, Score});
		}

		if (Scored.Num() == 0) return FString();

		Scored.Sort([](const FCandidate& A, const FCandidate& B) { return A.Score < B.Score; });
		const int32 Take = FMath::Min(5, Scored.Num());
		TArray<FString> TopNames;
		for (int32 i = 0; i < Take; ++i) TopNames.Add(Scored[i].Name);

		return FString::Printf(TEXT(" Did you mean: %s?"), *FString::Join(TopNames, TEXT(", ")));
	}
}

static void NormalizePathArgs(const TSharedPtr<FJsonObject>& Args)
{
	if (!Args.IsValid()) return;
	for (auto& KV : Args->Values)
	{
		const FString Key = FString(*KV.Key);
		const bool bPathKey = Key.EndsWith(TEXT("_path")) || Key == TEXT("path");
		if (!bPathKey) continue;
		FString StrVal;
		if (!KV.Value.IsValid() || !KV.Value->TryGetString(StrVal)) continue;
		StrVal.TrimStartAndEndInline();
		while (StrVal.Len() > 1 && (StrVal.EndsWith(TEXT("/")) || StrVal.EndsWith(TEXT("\\"))))
			StrVal = StrVal.LeftChop(1);
		KV.Value = MakeShared<FJsonValueString>(StrVal);
	}
}

// ---------------------------------------------------------------------------------------------
// Params-spec parser: turns FUECPToolMeta::Params strings into JSON schema properties.
// ---------------------------------------------------------------------------------------------
namespace UECPParamSpec
{
	constexpr int32 MaxNestingDepth = 3;

	bool IsIdentStart(TCHAR C) { return FChar::IsAlpha(C) || C == TEXT('_'); }
	bool IsIdentChar(TCHAR C)  { return FChar::IsAlnum(C) || C == TEXT('_'); }

	// True when Name equals Word or ends with "_Word" (so "count" and "actor_count" both match).
	bool MatchesWord(const FString& Name, const TCHAR* Word)
	{
		const FString W(Word);
		if (Name == W) return true;
		return Name.Len() > W.Len() + 1 && Name.EndsWith(FString(TEXT("_")) + W);
	}

	bool MatchesAnyWord(const FString& Name, std::initializer_list<const TCHAR*> Words)
	{
		for (const TCHAR* W : Words) if (MatchesWord(Name, W)) return true;
		return false;
	}

	bool StartsWithAny(const FString& Name, std::initializer_list<const TCHAR*> Prefixes)
	{
		for (const TCHAR* P : Prefixes) if (Name.StartsWith(P)) return true;
		return false;
	}

	// Splits at depth-0 ',' and at depth-0 '|' with whitespace on at least one side
	// ("a? | b" is an either-group). A tight "a|b" stays inside one token because it names
	// alternative spellings of the same parameter.
	void SplitTopLevel(const FString& In, TArray<FString>& Out)
	{
		int32 Depth = 0;
		FString Cur;
		for (int32 i = 0; i < In.Len(); ++i)
		{
			const TCHAR C = In[i];
			if (C == TEXT('(') || C == TEXT('[') || C == TEXT('{')) ++Depth;
			else if (C == TEXT(')') || C == TEXT(']') || C == TEXT('}')) Depth = FMath::Max(0, Depth - 1);

			if (Depth == 0)
			{
				if (C == TEXT(','))
				{
					Out.Add(Cur); Cur.Reset();
					continue;
				}
				if (C == TEXT('|'))
				{
					const bool bSpaceBefore = i > 0 && FChar::IsWhitespace(In[i - 1]);
					const bool bSpaceAfter  = (i + 1) < In.Len() && FChar::IsWhitespace(In[i + 1]);
					if (bSpaceBefore || bSpaceAfter)
					{
						Out.Add(Cur); Cur.Reset();
						continue;
					}
				}
			}
			Cur.AppendChar(C);
		}
		Out.Add(Cur);
	}

	// Index one past the bracket group opened at In[OpenIdx]; INDEX_NONE when unbalanced.
	int32 FindGroupEnd(const FString& In, int32 OpenIdx)
	{
		int32 Depth = 0;
		for (int32 i = OpenIdx; i < In.Len(); ++i)
		{
			const TCHAR C = In[i];
			if (C == TEXT('(') || C == TEXT('[') || C == TEXT('{')) ++Depth;
			else if (C == TEXT(')') || C == TEXT(']') || C == TEXT('}'))
			{
				--Depth;
				if (Depth == 0) return i + 1;
			}
		}
		return INDEX_NONE;
	}

	// First depth-0 '[' or '{' in Rest (a '(' note group is skipped over).
	int32 FindFirstStructuralBracket(const FString& Rest)
	{
		for (int32 i = 0; i < Rest.Len(); ++i)
		{
			const TCHAR C = Rest[i];
			if (C == TEXT('[') || C == TEXT('{')) return i;
			if (C == TEXT('('))
			{
				const int32 End = FindGroupEnd(Rest, i);
				if (End == INDEX_NONE) return INDEX_NONE;
				i = End - 1;
			}
		}
		return INDEX_NONE;
	}

	// Extracts the literal after a leading '=' in Rest ("=true", "='%s'", "=[...]", "=0.5 (note)").
	FString ExtractDefaultLiteral(const FString& Rest)
	{
		if (Rest.IsEmpty() || Rest[0] != TEXT('=')) return FString();
		if (Rest.Len() < 2) return FString();
		const TCHAR First = Rest[1];
		FString Lit;
		if (First == TEXT('[') || First == TEXT('{') || First == TEXT('('))
		{
			const int32 End = FindGroupEnd(Rest, 1);
			Lit = (End == INDEX_NONE) ? Rest.Mid(1) : Rest.Mid(1, End - 1);
		}
		else
		{
			int32 P = 1;
			while (P < Rest.Len() && !FChar::IsWhitespace(Rest[P]) && Rest[P] != TEXT('|')) ++P;
			Lit = Rest.Mid(1, P - 1);
		}
		Lit.TrimStartAndEndInline();
		if (Lit.Len() >= 2 && ((Lit[0] == TEXT('\'') && Lit[Lit.Len() - 1] == TEXT('\''))
			|| (Lit[0] == TEXT('"') && Lit[Lit.Len() - 1] == TEXT('"'))))
		{
			Lit = Lit.Mid(1, Lit.Len() - 2);
		}
		return Lit;
	}

	FString TypeFromDefaultLiteral(const FString& Lit)
	{
		if (Lit.IsEmpty()) return FString();
		const FString L = Lit.ToLower();
		if (L == TEXT("true") || L == TEXT("false")) return TEXT("boolean");
		if (Lit[0] == TEXT('[')) return TEXT("array");
		if (Lit[0] == TEXT('{')) return TEXT("object");
		if (Lit.IsNumeric()) return TEXT("number");
		return TEXT("string");
	}

	// Exact "scale"/"offset" are FVector-shaped in the level tools (ExtractVector); the
	// "_scale"/"_offset" suffixes ("lod_scale", "trace_offset") are plain numbers.
	// "random_location_min" -> "random_location": a _min/_max range bound keeps its base shape.
	FString StripRangeSuffix(const FString& L)
	{
		if (L.EndsWith(TEXT("_min")) || L.EndsWith(TEXT("_max"))) return L.LeftChop(4);
		return L;
	}

	bool IsVectorLikeName(const FString& InL)
	{
		const FString L = StripRangeSuffix(InL);
		if (L == TEXT("scale") || L == TEXT("offset") || L == TEXT("translation") || L == TEXT("velocity"))
			return true;
		return MatchesAnyWord(L, { TEXT("location"), TEXT("position"), TEXT("center"), TEXT("extent"),
			TEXT("direction"), TEXT("normal"), TEXT("origin") });
	}

	bool IsRotatorLikeName(const FString& InL)
	{
		const FString L = StripRangeSuffix(InL);
		return MatchesWord(L, TEXT("rotation")) || MatchesWord(L, TEXT("rotator"));
	}

	// Name/token heuristics (see IUECPToolDispatcher.h). `Rest` is RawToken minus the identifier.
	FString InferType(const FString& Name, const FString& Rest)
	{
		const FString L = Name.ToLower();

		// 1. A literal default is the strongest signal ("=true", "=0.5", "=[{...}]").
		{
			const FString DefType = TypeFromDefaultLiteral(ExtractDefaultLiteral(Rest));
			if (!DefType.IsEmpty()) return DefType;
		}

		// 1b. An explicit shape right after the name ("rotation_min? {yaw,pitch,roll}", "color? [R,G,B]")
		//     beats the name suffix heuristics below.
		{
			const FString Body = Rest.TrimStart();
			if (Body.StartsWith(TEXT("{"))) return TEXT("object");
			if (Body.StartsWith(TEXT("["))) return TEXT("array");
		}

		// 2. Name conventions.
		if (L.EndsWith(TEXT("_paths"))) return TEXT("array");
		if (L.EndsWith(TEXT("_path")) || L == TEXT("path")) return TEXT("string");
		if (IsVectorLikeName(L) || IsRotatorLikeName(L)) return TEXT("object");

		if (MatchesAnyWord(L, {
			TEXT("points"), TEXT("nodes"), TEXT("links"), TEXT("items"), TEXT("actors"), TEXT("meshes"),
			TEXT("animations"), TEXT("files"), TEXT("tags"), TEXT("sections"), TEXT("notifies"),
			TEXT("plugins"), TEXT("cvars"), TEXT("labels"), TEXT("names"), TEXT("ids"), TEXT("guids"),
			TEXT("entries"), TEXT("questions"), TEXT("layers"), TEXT("rows"), TEXT("keys"), TEXT("bones"),
			TEXT("sockets"), TEXT("components"), TEXT("materials"), TEXT("textures"), TEXT("variables"),
			TEXT("functions"), TEXT("events"), TEXT("pins"), TEXT("tracks"), TEXT("channels"),
			TEXT("emitters"), TEXT("modules"), TEXT("classes"), TEXT("assets"), TEXT("folders"),
			TEXT("options"), TEXT("extensions"), TEXT("filters"), TEXT("patterns"), TEXT("steps") }))
		{
			return TEXT("array");
		}

		if (StartsWithAny(L, { TEXT("num_"), TEXT("max_"), TEXT("min_") })
			|| MatchesAnyWord(L, {
			TEXT("count"), TEXT("index"), TEXT("limit"), TEXT("size"), TEXT("seconds"), TEXT("secs"),
			TEXT("ms"), TEXT("degrees"), TEXT("deg"), TEXT("scale"), TEXT("min"), TEXT("max"),
			TEXT("width"), TEXT("height"), TEXT("radius"), TEXT("strength"), TEXT("density"),
			TEXT("spacing"), TEXT("x"), TEXT("y"), TEXT("z"), TEXT("yaw"), TEXT("pitch"), TEXT("roll"),
			TEXT("r"), TEXT("g"), TEXT("b"), TEXT("a"), TEXT("intensity"), TEXT("mass"), TEXT("angle"),
			TEXT("speed"), TEXT("distance"), TEXT("duration"), TEXT("frame"), TEXT("fps"), TEXT("time"),
			TEXT("length"), TEXT("damping"), TEXT("frequency"), TEXT("rate"), TEXT("amount"),
			TEXT("factor"), TEXT("weight"), TEXT("threshold"), TEXT("bias"), TEXT("percent"),
			TEXT("ratio"), TEXT("priority"), TEXT("tolerance"), TEXT("aperture"), TEXT("temperature"),
			TEXT("depth"), TEXT("iterations"), TEXT("seed"), TEXT("offset") }))
		{
			return TEXT("number");
		}

		if (StartsWithAny(L, { TEXT("b_"), TEXT("is_"), TEXT("has_"), TEXT("enable_"), TEXT("use_"),
			TEXT("align_"), TEXT("snap_"), TEXT("create_"), TEXT("include_"), TEXT("allow_"),
			TEXT("uniform_"), TEXT("should_"), TEXT("can_"), TEXT("force_"), TEXT("auto_"),
			TEXT("skip_"), TEXT("keep_"), TEXT("simulate_"), TEXT("cast_"), TEXT("replicate") })
			|| MatchesAnyWord(L, {
			TEXT("enabled"), TEXT("visible"), TEXT("recursive"), TEXT("overwrite"), TEXT("force"),
			TEXT("only"), TEXT("flag"), TEXT("save"), TEXT("compile"), TEXT("dry_run"), TEXT("verbose"),
			TEXT("stage_all"), TEXT("shadows") }))
		{
			return TEXT("boolean");
		}

		if (L == TEXT("color") || L == TEXT("colour")) return TEXT("array");

		// 3. Structural brackets in the token body: whichever comes first wins.
		{
			const int32 B = FindFirstStructuralBracket(Rest);
			if (B != INDEX_NONE) return Rest[B] == TEXT('{') ? TEXT("object") : TEXT("array");
		}

		return TEXT("string");
	}

	struct FInternalParam
	{
		FString Name;
		FString RawToken;
		FString JsonType;
		bool    bOptional = false;
		TSharedPtr<FJsonObject> Schema;
	};

	void ParseSpecInto(const FString& Spec, int32 Depth, TArray<FInternalParam>& Out, TArray<FString>* Unparseable);

	TSharedPtr<FJsonObject> MakeTypedProp(const TCHAR* Type)
	{
		TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetStringField(TEXT("type"), Type);
		return P;
	}

	void AddChildrenAsProperties(const TArray<FInternalParam>& Children, const TSharedPtr<FJsonObject>& Target)
	{
		if (Children.Num() == 0) return;
		TSharedPtr<FJsonObject> Props = MakeShared<FJsonObject>();
		for (const FInternalParam& C : Children)
		{
			if (!Props->HasField(C.Name)) Props->SetObjectField(C.Name, C.Schema);
		}
		Target->SetObjectField(TEXT("properties"), Props);
	}

	// Builds {type, items?/properties?} for one parameter, recursing into "[{...}]" / "{...}" bodies.
	TSharedPtr<FJsonObject> BuildPropertySchema(const FString& Name, const FString& Rest,
		const FString& JsonType, int32 Depth)
	{
		TSharedPtr<FJsonObject> Schema = MakeShared<FJsonObject>();
		Schema->SetStringField(TEXT("type"), JsonType);
		const FString L = Name.ToLower();

		if (JsonType == TEXT("array"))
		{
			TSharedPtr<FJsonObject> Items;
			const int32 B = FindFirstStructuralBracket(Rest);
			if (B != INDEX_NONE && Rest[B] == TEXT('['))
			{
				const int32 End = FindGroupEnd(Rest, B);
				const FString Inner = (End == INDEX_NONE ? Rest.Mid(B + 1) : Rest.Mid(B + 1, End - B - 2)).TrimStartAndEnd();
				if (Inner.StartsWith(TEXT("{")) && Depth < MaxNestingDepth)
				{
					const int32 ObjEnd = FindGroupEnd(Inner, 0);
					const FString ObjInner = ObjEnd == INDEX_NONE ? Inner.Mid(1) : Inner.Mid(1, ObjEnd - 2);
					TArray<FInternalParam> Children;
					ParseSpecInto(ObjInner, Depth + 1, Children, nullptr);
					Items = MakeTypedProp(TEXT("object"));
					AddChildrenAsProperties(Children, Items);
				}
				else if (Inner.StartsWith(TEXT("[")))
				{
					Items = MakeTypedProp(TEXT("array"));
					Items->SetObjectField(TEXT("items"), MakeTypedProp(TEXT("number")));
				}
			}
			if (!Items.IsValid())
			{
				const bool bNumericItems = (L == TEXT("color") || L == TEXT("colour")
					|| MatchesAnyWord(L, { TEXT("points"), TEXT("values"), TEXT("weights"), TEXT("times") }));
				Items = MakeTypedProp(bNumericItems ? TEXT("number") : TEXT("string"));
			}
			Schema->SetObjectField(TEXT("items"), Items);
		}
		else if (JsonType == TEXT("object"))
		{
			TArray<FInternalParam> Children;
			const int32 B = FindFirstStructuralBracket(Rest);
			if (B != INDEX_NONE && Rest[B] == TEXT('{') && Depth < MaxNestingDepth)
			{
				const int32 End = FindGroupEnd(Rest, B);
				const FString Inner = End == INDEX_NONE ? Rest.Mid(B + 1) : Rest.Mid(B + 1, End - B - 2);
				ParseSpecInto(Inner, Depth + 1, Children, nullptr);
			}
			if (Children.Num() == 0)
			{
				// Well-known UE value shapes: give the model concrete sub-fields (handlers also accept
				// [x,y,z] arrays, which the description still mentions).
				const TCHAR* Fields[3] = { nullptr, nullptr, nullptr };
				if (IsVectorLikeName(L))       { Fields[0] = TEXT("x");     Fields[1] = TEXT("y");   Fields[2] = TEXT("z"); }
				else if (IsRotatorLikeName(L)) { Fields[0] = TEXT("pitch"); Fields[1] = TEXT("yaw"); Fields[2] = TEXT("roll"); }
				if (Fields[0])
				{
					for (const TCHAR* F : Fields)
					{
						FInternalParam C;
						C.Name = F; C.RawToken = F; C.JsonType = TEXT("number");
						C.Schema = MakeTypedProp(TEXT("number"));
						Children.Add(MoveTemp(C));
					}
				}
			}
			AddChildrenAsProperties(Children, Schema);
		}
		else if (JsonType == TEXT("string"))
		{
			// "mode? a|b|c" -> enum, only when the body is exactly a tight identifier alternation.
			const FString Body = Rest.TrimStartAndEnd();
			if (Body.Contains(TEXT("|")) && Body.Len() < 200)
			{
				bool bTightAlternation = true;
				for (int32 i = 0; i < Body.Len() && bTightAlternation; ++i)
					bTightAlternation = IsIdentChar(Body[i]) || Body[i] == TEXT('|');
				if (bTightAlternation)
				{
					TArray<FString> Alts;
					Body.ParseIntoArray(Alts, TEXT("|"), true);
					if (Alts.Num() >= 2)
					{
						TArray<TSharedPtr<FJsonValue>> Enum;
						for (const FString& A : Alts) Enum.Add(MakeShared<FJsonValueString>(A));
						Schema->SetArrayField(TEXT("enum"), Enum);
					}
				}
			}
		}
		return Schema;
	}

	void ParseToken(const FString& Token, int32 Depth, TArray<FInternalParam>& Out, TArray<FString>* Unparseable)
	{
		const FString Raw = Token.TrimStartAndEnd();
		if (Raw.IsEmpty()) return;

		int32 Pos = 0;
		if (!IsIdentStart(Raw[0]))
		{
			if (Unparseable) Unparseable->Add(Raw);
			return;
		}
		while (Pos < Raw.Len() && IsIdentChar(Raw[Pos])) ++Pos;

		TArray<FString> Names;
		Names.Add(Raw.Left(Pos));
		bool bOptional = false;
		auto ConsumeOptional = [&]()
		{
			if (Pos < Raw.Len() && Raw[Pos] == TEXT('?')) { bOptional = true; ++Pos; }
		};
		ConsumeOptional();

		// "min_x/y/z" -> min_x, min_y, min_z ; "random_scale_min/max" -> random_scale_min, random_scale_max
		while (Pos < Raw.Len() && Raw[Pos] == TEXT('/'))
		{
			int32 P2 = Pos + 1;
			while (P2 < Raw.Len() && IsIdentChar(Raw[P2])) ++P2;
			if (P2 == Pos + 1) break;
			const FString Frag = Raw.Mid(Pos + 1, P2 - Pos - 1);
			const FString Base = Names[0];
			int32 Us = INDEX_NONE;
			const FString Prefix = Base.FindLastChar(TEXT('_'), Us) ? Base.Left(Us + 1) : FString();
			Names.Add(Prefix + Frag);
			Pos = P2;
			ConsumeOptional();
		}

		// "asset_path|asset_paths" -> both names (tight alternation directly after the identifier).
		while (Pos < Raw.Len() && Raw[Pos] == TEXT('|'))
		{
			int32 P2 = Pos + 1;
			while (P2 < Raw.Len() && IsIdentChar(Raw[P2])) ++P2;
			if (P2 == Pos + 1) break;
			Names.Add(Raw.Mid(Pos + 1, P2 - Pos - 1));
			Pos = P2;
			ConsumeOptional();
		}

		// ':' is an alternative spelling of '=' ("items:[{...}]").
		FString Rest = Raw.Mid(Pos);
		if (Rest.StartsWith(TEXT(":"))) Rest = FString(TEXT("=")) + Rest.Mid(1);
		if (!bOptional && Rest.StartsWith(TEXT("=")))
		{
			// A scalar default ("=true", "=0.5") implies optional; "=[...]" / "={...}" only describe the shape.
			const FString Lit = ExtractDefaultLiteral(Rest);
			const bool bStructural = !Lit.IsEmpty() && (Lit[0] == TEXT('[') || Lit[0] == TEXT('{'));
			bOptional = !Lit.IsEmpty() && !bStructural;
		}

		for (const FString& N : Names)
		{
			FInternalParam P;
			P.Name      = N;
			P.RawToken  = Raw;
			P.bOptional = bOptional;
			P.JsonType  = InferType(N, Rest);
			P.Schema    = BuildPropertySchema(N, Rest, P.JsonType, Depth);
			Out.Add(MoveTemp(P));
		}
	}

	void ParseSpecInto(const FString& Spec, int32 Depth, TArray<FInternalParam>& Out, TArray<FString>* Unparseable)
	{
		TArray<FString> Tokens;
		SplitTopLevel(Spec, Tokens);
		for (const FString& T : Tokens) ParseToken(T, Depth, Out, Unparseable);
	}
}

bool UECPToolDispatch::ParseParamSpec(const FString& Params, TArray<FUECPParsedParam>& OutParams,
	TArray<FString>* OutUnparseable)
{
	TArray<FString> LocalUnparseable;
	TArray<FString>* Unparseable = OutUnparseable ? OutUnparseable : &LocalUnparseable;
	const int32 Before = Unparseable->Num();

	TArray<UECPParamSpec::FInternalParam> Internal;
	UECPParamSpec::ParseSpecInto(Params, 0, Internal, Unparseable);

	OutParams.Reserve(OutParams.Num() + Internal.Num());
	for (UECPParamSpec::FInternalParam& P : Internal)
	{
		FUECPParsedParam Out;
		Out.Name      = MoveTemp(P.Name);
		Out.RawToken  = MoveTemp(P.RawToken);
		Out.JsonType  = MoveTemp(P.JsonType);
		Out.bOptional = P.bOptional;
		Out.Schema    = MoveTemp(P.Schema);
		OutParams.Add(MoveTemp(Out));
	}
	return Unparseable->Num() == Before;
}

FString UECPToolDispatch::InferParamJsonType(const FString& Name, const FString& RawToken)
{
	// Skip the identifier(s) and their `?` / `/alt` / `|alt` decorations to reach the token body.
	FString Rest = RawToken.TrimStartAndEnd();
	if (Rest.StartsWith(Name)) Rest = Rest.Mid(Name.Len());
	while (!Rest.IsEmpty() && (Rest[0] == TEXT('?') || Rest[0] == TEXT('/') || Rest[0] == TEXT('|')
		|| UECPParamSpec::IsIdentChar(Rest[0])))
	{
		Rest = Rest.Mid(1);
	}
	if (Rest.StartsWith(TEXT(":"))) Rest = FString(TEXT("=")) + Rest.Mid(1);
	return UECPParamSpec::InferType(Name, Rest);
}

bool UECPToolDispatch::IsUmbrellaName(FName Name)
{
	static const TSet<FName> SeedUmbrellas = {
		TEXT("niagara"),        TEXT("pcg"),             TEXT("sequencer"),
		TEXT("behavior_tree"),  TEXT("animation"),       TEXT("ik_retarget"),
		TEXT("control_rig"),    TEXT("gas"),             TEXT("environment"),
		TEXT("audio"),          TEXT("landscape"),       TEXT("foliage"),
		TEXT("material"),       TEXT("level_actor"),     TEXT("widget"),
		TEXT("input_system"),   TEXT("spline"),          TEXT("curve"),
		TEXT("data"),           TEXT("physics"),         TEXT("eqs"),
		TEXT("navmesh_ai"),     TEXT("gameplay_tags"),   TEXT("state_tree"),
		TEXT("pose_search"),    TEXT("chooser"),         TEXT("blueprint"),
		TEXT("editor_utility"), TEXT("mesh"),            TEXT("asset_management"),
		TEXT("component"),      TEXT("water"),           TEXT("level_streaming"),
		TEXT("metasound"),      TEXT("string_table"),    TEXT("post_process"),
		TEXT("play_test"),      TEXT("vehicle"),         TEXT("mover"),
		TEXT("mass_entity"),    TEXT("project_viz"),     TEXT("cpp_tools"),
		TEXT("media"),          TEXT("geometry_script"), TEXT("render"),
		TEXT("level_snapshot"), TEXT("groom"),           TEXT("common_ui"),
		TEXT("mvvm"),           TEXT("game_features"),   TEXT("live_link"),
		TEXT("project_plan"),   TEXT("memory"),          TEXT("git_tools"),
		TEXT("working_notes"),  TEXT("config"),          TEXT("python_tools"),
		TEXT("meshy"),
	};
	if (SeedUmbrellas.Contains(Name)) return true;

	if (Name.IsNone() || !IUECPCoreModule::IsAvailable()) return false;
	return IUECPCoreModule::Get().GetExtensionService().IsUmbrellaProvidedByLoadedExtension(Name);
}

void FUECPToolDispatcherImpl::RegisterHandler(FName ToolName, FToolHandler Handler,
	EUECPToolThreadAffinity Affinity)
{
	checkf(IsInGameThread(),
		TEXT("IUECPToolDispatcher::RegisterHandler must be called on the game thread"));
	if (ToolName.IsNone() || !Handler) return;

	FScopeLock Lock(&RegistryLock);

	if (const FRegistration* Existing = Handlers.Find(ToolName))
	{
		if (Existing->Owner != CurrentRegistrant)
		{
			UE_LOG(LogUECPCore, Error,
				TEXT("[Dispatcher] Tool '%s' is already registered by '%s' — refusing shadow registration from '%s'. Keeping the original handler."),
				*ToolName.ToString(),
				*(Existing->Owner.IsNone() ? FString(TEXT("core")) : Existing->Owner.ToString()),
				*(CurrentRegistrant.IsNone() ? FString(TEXT("core")) : CurrentRegistrant.ToString()));
			return;
		}
	}

	Handlers.Add(ToolName, FRegistration{ MoveTemp(Handler), Affinity, CurrentRegistrant });
}

void FUECPToolDispatcherImpl::SetRegistrantContext(FName ExtensionId)
{
	checkf(IsInGameThread(),
		TEXT("IUECPToolDispatcher::SetRegistrantContext must be called on the game thread"));
	CurrentRegistrant = ExtensionId;
}

void FUECPToolDispatcherImpl::UnregisterHandler(FName ToolName)
{
	checkf(IsInGameThread(),
		TEXT("IUECPToolDispatcher::UnregisterHandler must be called on the game thread"));
	FScopeLock Lock(&RegistryLock);
	if (const FRegistration* Reg = Handlers.Find(ToolName))
	{
		if (Reg->Owner != CurrentRegistrant)
		{
			UE_LOG(LogUECPCore, Warning,
				TEXT("[Dispatcher] Refusing to unregister tool '%s' — it is owned by '%s', not the current registrant '%s'."),
				*ToolName.ToString(),
				*(Reg->Owner.IsNone() ? FString(TEXT("core")) : Reg->Owner.ToString()),
				*(CurrentRegistrant.IsNone() ? FString(TEXT("core")) : CurrentRegistrant.ToString()));
			return;
		}
		Handlers.Remove(ToolName);
	}
}

FUECPToolResult FUECPToolDispatcherImpl::ExecuteFromArgs(FName ToolName, const TSharedPtr<FJsonObject>& Args)
{
	NormalizePathArgs(Args);

	FToolHandler Handler;
	EUECPToolThreadAffinity Affinity = EUECPToolThreadAffinity::GameThread;
	{
		FScopeLock Lock(&RegistryLock);
		if (const FRegistration* Reg = Handlers.Find(ToolName))
		{
			Handler  = Reg->Handler;
			Affinity = Reg->Affinity;
		}
	}

	if (Handler)
	{
		const bool bOnGT       = IsInGameThread();
		const bool bRequiresGT = (Affinity == EUECPToolThreadAffinity::GameThread);

		// AnyThread tools are safe to run on the calling thread; GameThread tools
		// already on the game thread run inline. Both avoid a marshal round-trip.
		if (!bRequiresGT || bOnGT)
		{
			return Handler(Args);
		}

		// GameThread-affinity tool invoked from a background thread (e.g. the MCP HTTP
		// worker). Marshal onto the game thread. The result and the pooled event live in
		// a shared state kept alive by both this frame and the dispatched task, so a task
		// that completes after we time out never writes into an unwound stack frame or a
		// recycled event. The wait is bounded: if the game thread is wedged (modal dialog,
		// PIE, long import) we return a structured error instead of hanging the connection.
		struct FGtDispatchState
		{
			FUECPToolResult Result;
			FEvent*         Event = nullptr;
			~FGtDispatchState()
			{
				if (Event)
				{
					FPlatformProcess::ReturnSynchEventToPool(Event);
					Event = nullptr;
				}
			}
		};

		TSharedPtr<FGtDispatchState, ESPMode::ThreadSafe> State = MakeShared<FGtDispatchState, ESPMode::ThreadSafe>();
		State->Event = FPlatformProcess::GetSynchEventFromPool(false);

		AsyncTask(ENamedThreads::GameThread, [Handler, Args, State]()
		{
			State->Result = Handler(Args);
			State->Event->Trigger();
		});

		// 120s upper bound: long enough for slow-but-legitimate game-thread work
		// (asset saves, cooks-in-editor), short enough that a truly stuck editor
		// releases the caller instead of blocking forever.
		const uint32 TimeoutMs = 120 * 1000;
		if (State->Event->Wait(TimeoutMs))
		{
			return State->Result;
		}

		FUECPToolResult Busy;
		Busy.bSuccess = false;
		Busy.ErrorMessage = TEXT("The editor's game thread did not respond within 120s (it may be showing a modal dialog, in a Play-In-Editor session, or busy with a long operation). The tool was not confirmed to have run; retry once the editor is idle.");
		return Busy;
	}

	const FString Name = ToolName.ToString();

	int32 HandlerCount = 0;
	bool bHasBlueprintInfer = false;
	TArray<FName> KnownNames;
	{
		FScopeLock Lock(&RegistryLock);
		HandlerCount = Handlers.Num();
		bHasBlueprintInfer = Handlers.Contains(FName(TEXT("build_blueprint_graph")));
		Handlers.GenerateKeyArray(KnownNames);
	}

	FUECPToolResult Result;
	Result.bSuccess = false;
	if (UECPToolDispatch::IsUmbrellaName(ToolName))
	{
		if (Args.IsValid() && Name == TEXT("blueprint")
			&& Args->HasField(TEXT("graph_name")) && Args->HasField(TEXT("nodes")))
		{
			static const FName InferredAction(TEXT("build_blueprint_graph"));
			if (bHasBlueprintInfer)
			{
				UE_LOG(LogUECPCore, Log,
					TEXT("[Dispatcher] 'blueprint' called without `action`; inferred 'build_blueprint_graph' from graph_name+nodes and re-dispatched."));
				return ExecuteFromArgs(InferredAction, Args);
			}
		}

		FString ExtensionHint;
		if (IUECPCoreModule::IsAvailable())
		{
			if (TOptional<FUECPExtensionDescriptor> Ext =
				IUECPCoreModule::Get().GetExtensionService().FindExtensionByUmbrella(FName(*Name)); Ext.IsSet())
			{
				const EUECPExtensionState State =
					IUECPCoreModule::Get().GetExtensionService().GetExtensionState(Ext->ExtensionId);
				if (State != EUECPExtensionState::Loaded)
				{
					ExtensionHint = FString::Printf(
						TEXT(" This umbrella belongs to the **%s** extension which is not currently loaded — ")
						TEXT("the user must enable it in **Settings → Extensions**."),
						*Ext->DisplayName.ToString());
				}
			}
		}

		Result.ErrorMessage = FString::Printf(
			TEXT("'%s' is an umbrella tool — your call is missing its `action` argument. ")
			TEXT("Re-send the SAME call with `action='<action_name>'` added; the rest of your payload was fine, ")
			TEXT("so do NOT shrink it, probe with a smaller call, or clear/rebuild a graph. ")
			TEXT("Call get_tool_docs(category='%s') only if you're unsure of the action name.%s"),
			*Name, *Name, *ExtensionHint);
		UE_LOG(LogUECPCore, Warning,
			TEXT("[Dispatcher] Umbrella '%s' reached dispatcher without action rewrite — handler count=%d.%s"),
			*Name, HandlerCount,
			ExtensionHint.IsEmpty()
				? TEXT(" Check the Python MCP tool definition forwards `type` correctly.")
				: TEXT(" Owning extension is not loaded."));
	}
	else
	{
		if (HandlerCount == 0)
		{
			Result.ErrorMessage = FString::Printf(
				TEXT("No handler registered for tool '%s'. UECPTools StartupModule has not run yet."),
				*Name);
			UE_LOG(LogUECPCore, Warning,
				TEXT("[Dispatcher] No handler for '%s' (handler count=0). UECPTools StartupModule didn't run yet."),
				*Name);
		}
		else
		{
			FString ExtensionHint;
			if (IUECPCoreModule::IsAvailable())
			{
				if (TOptional<FUECPExtensionDescriptor> Ext =
					IUECPCoreModule::Get().GetExtensionService().FindExtensionByTool(ToolName); Ext.IsSet())
				{
					const EUECPExtensionState State =
						IUECPCoreModule::Get().GetExtensionService().GetExtensionState(Ext->ExtensionId);
					if (State != EUECPExtensionState::Loaded)
					{
						ExtensionHint = FString::Printf(
							TEXT(" This tool is provided by the **%s** extension which is not currently loaded — ")
							TEXT("the user must enable it in **Settings → Extensions**. Stop calling tools from this extension until they enable it."),
							*Ext->DisplayName.ToString());
					}
				}
			}

			const FString Hint = ExtensionHint.IsEmpty() ? BuildSuggestionsHint(Name, KnownNames) : FString();

			int32 HitCount = 1;
			{
				FScopeLock Lock(&UnknownLock);
				HitCount = ++UnknownToolHits.FindOrAdd(ToolName);
			}
			if (HitCount >= 2 && ExtensionHint.IsEmpty())
			{
				Result.ErrorMessage = FString::Printf(
					TEXT("STOP. Tool '%s' does NOT exist — you already tried this and got the same error. Don't retry '%s' again. Pick a real tool from%s, or call get_tool_docs(category='<umbrella>') for the full list."),
					*Name, *Name, Hint.IsEmpty() ? TEXT(" the suggestions above") : *Hint);
			}
			else
			{
				Result.ErrorMessage = FString::Printf(
					TEXT("Unknown tool '%s'.%s%s"),
					*Name,
					ExtensionHint.IsEmpty() ? TEXT(" Check the spelling or call get_tool_docs() to find the correct tool name.") : *ExtensionHint,
					*Hint);
			}
			UE_LOG(LogUECPCore, Warning,
				TEXT("[Dispatcher] Unknown tool '%s' (handler count=%d, hit count=%d).%s"),
				*Name, HandlerCount, HitCount,
				ExtensionHint.IsEmpty()
					? (Hint.IsEmpty() ? TEXT(" No close matches.") : *Hint)
					: TEXT(" Owning extension is not loaded."));
		}
	}
	return Result;
}

void FUECPToolDispatcherImpl::ExecuteFromArgsAsync(FName ToolName, const TSharedPtr<FJsonObject>& Args,
	TFunction<void(FUECPToolResult)> OnComplete)
{
	NormalizePathArgs(Args);

	FToolHandler HandlerCopy;
	EUECPToolThreadAffinity Affinity = EUECPToolThreadAffinity::GameThread;
	bool bFound = false;
	{
		FScopeLock Lock(&RegistryLock);
		if (const FRegistration* Reg = Handlers.Find(ToolName))
		{
			HandlerCopy = Reg->Handler;
			Affinity    = Reg->Affinity;
			bFound      = true;
		}
	}
	if (!bFound)
	{
		FUECPToolResult R = ExecuteFromArgs(ToolName, Args);
		if (OnComplete)
		{
			if (IsInGameThread()) OnComplete(MoveTemp(R));
			else AsyncTask(ENamedThreads::GameThread,
				[R = MoveTemp(R), OnComplete = MoveTemp(OnComplete)]() mutable { OnComplete(MoveTemp(R)); });
		}
		return;
	}

	const bool bRequiresGT = (Affinity == EUECPToolThreadAffinity::GameThread);
	const bool bOnGT       = IsInGameThread();

	auto DeliverOnGT = [OnComplete = MoveTemp(OnComplete)](FUECPToolResult R) mutable
	{
		if (!OnComplete) return;
		if (IsInGameThread()) OnComplete(MoveTemp(R));
		else AsyncTask(ENamedThreads::GameThread,
			[R = MoveTemp(R), OnComplete = MoveTemp(OnComplete)]() mutable { OnComplete(MoveTemp(R)); });
	};

	if (bRequiresGT && bOnGT)
	{
		DeliverOnGT(HandlerCopy(Args));
		return;
	}

	if (bRequiresGT)
	{
		AsyncTask(ENamedThreads::GameThread,
			[HandlerCopy, Args, DeliverOnGT = MoveTemp(DeliverOnGT)]() mutable
			{
				DeliverOnGT(HandlerCopy(Args));
			});
		return;
	}

	AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask,
		[HandlerCopy, Args, DeliverOnGT = MoveTemp(DeliverOnGT)]() mutable
		{
			DeliverOnGT(HandlerCopy(Args));
		});
}

TArray<FName> FUECPToolDispatcherImpl::ListTools() const
{
	FScopeLock Lock(&RegistryLock);
	TArray<FName> Names;
	Handlers.GenerateKeyArray(Names);
	return Names;
}

bool FUECPToolDispatcherImpl::IsRegistered(FName ToolName) const
{
	if (ToolName.IsNone()) return false;
	FScopeLock Lock(&RegistryLock);
	return Handlers.Contains(ToolName);
}

void FUECPToolDispatcherImpl::RegisterToolMetadata(const FUECPToolMeta& Meta)
{
	checkf(IsInGameThread(),
		TEXT("IUECPToolDispatcher::RegisterToolMetadata must be called on the game thread"));
	if (Meta.Action.IsNone()) return;
	FScopeLock Lock(&RegistryLock);
	if (const FName* PrevOwner = ToolMetaOwners.Find(Meta.Action))
	{
		if (*PrevOwner != CurrentRegistrant)
		{
			UE_LOG(LogUECPCore, Warning,
				TEXT("[Dispatcher] Tool metadata for action '%s' already registered by '%s' — ignoring shadow metadata from '%s'."),
				*Meta.Action.ToString(),
				*(PrevOwner->IsNone() ? FString(TEXT("core")) : PrevOwner->ToString()),
				*(CurrentRegistrant.IsNone() ? FString(TEXT("core")) : CurrentRegistrant.ToString()));
			return;
		}
	}
	if (const FUECPToolMeta* Prev = ToolMeta.Find(Meta.Action))
	{
		if (Prev->Umbrella != Meta.Umbrella) UmbrellaSchemaCache.Remove(Prev->Umbrella);
	}
	UmbrellaSchemaCache.Remove(Meta.Umbrella);
	ToolMeta.Add(Meta.Action, Meta);
	ToolMetaOwners.Add(Meta.Action, CurrentRegistrant);
}

void FUECPToolDispatcherImpl::GetAllToolMetadata(TArray<FUECPToolMeta>& Out) const
{
	FScopeLock Lock(&RegistryLock);
	ToolMeta.GenerateValueArray(Out);
}

TSharedPtr<FJsonObject> FUECPToolDispatcherImpl::GetUmbrellaSchema(FName Umbrella) const
{
	if (Umbrella.IsNone()) return nullptr;
	FScopeLock Lock(&RegistryLock);
	if (const TSharedPtr<FJsonObject>* Cached = UmbrellaSchemaCache.Find(Umbrella))
	{
		return *Cached;
	}
	TSharedPtr<FJsonObject> Built = BuildUmbrellaSchema_AssumesLocked(Umbrella);
	if (Built.IsValid())
	{
		UmbrellaSchemaCache.Add(Umbrella, Built);
	}
	return Built;
}

TSharedPtr<FJsonObject> FUECPToolDispatcherImpl::BuildUmbrellaSchema_AssumesLocked(FName Umbrella) const
{
	// Cap on generated properties so a large umbrella (blueprint: 100+ actions) stays a sane payload.
	constexpr int32 MaxProperties      = 120;
	constexpr int32 MaxActionsInDesc   = 6;

	struct FMerged
	{
		FString                 Name;
		FString                 RawToken;
		FString                 JsonType;
		TSharedPtr<FJsonObject> Schema;
		TArray<FString>         UsedBy;
		int32                   FirstSeen = 0;
	};

	TArray<TSharedPtr<FJsonValue>> ActionEnum;
	TMap<FString, FMerged>         Merged;
	int32                          Order = 0;

	for (const TPair<FName, FUECPToolMeta>& KV : ToolMeta)
	{
		const FUECPToolMeta& M = KV.Value;
		if (M.Umbrella != Umbrella || M.Action.IsNone()) continue;

		const FString ActionStr = M.Action.ToString();
		ActionEnum.Add(MakeShared<FJsonValueString>(ActionStr));

		TArray<FUECPParsedParam> Params;
		UECPToolDispatch::ParseParamSpec(M.Params, Params, nullptr);

		TSet<FString> SeenInAction;
		for (FUECPParsedParam& P : Params)
		{
			if (P.Name.IsEmpty() || P.Name.Equals(TEXT("action"), ESearchCase::IgnoreCase)) continue;
			if (SeenInAction.Contains(P.Name)) continue;
			SeenInAction.Add(P.Name);

			FMerged* Existing = Merged.Find(P.Name);
			if (!Existing)
			{
				FMerged NewEntry;
				NewEntry.Name      = P.Name;
				NewEntry.RawToken  = P.RawToken;
				NewEntry.JsonType  = P.JsonType;
				NewEntry.Schema    = P.Schema;
				NewEntry.FirstSeen = Order++;
				NewEntry.UsedBy.Add(ActionStr);
				Merged.Add(P.Name, MoveTemp(NewEntry));
				continue;
			}

			Existing->UsedBy.Add(ActionStr);

			// Prefer the more informative spelling: a typed spec beats a bare "name" (string),
			// and a spec with nested structure beats one without.
			const bool bNewHasStructure = P.Schema.IsValid()
				&& (P.Schema->HasField(TEXT("properties")) || P.Schema->HasField(TEXT("enum"))
					|| (P.Schema->HasField(TEXT("items")) && P.Schema->GetObjectField(TEXT("items"))->HasField(TEXT("properties"))));
			const bool bOldHasStructure = Existing->Schema.IsValid()
				&& (Existing->Schema->HasField(TEXT("properties")) || Existing->Schema->HasField(TEXT("enum"))
					|| (Existing->Schema->HasField(TEXT("items")) && Existing->Schema->GetObjectField(TEXT("items"))->HasField(TEXT("properties"))));
			const bool bUpgrade = (Existing->JsonType == TEXT("string") && P.JsonType != TEXT("string"))
				|| (bNewHasStructure && !bOldHasStructure);
			if (bUpgrade)
			{
				Existing->JsonType = P.JsonType;
				Existing->Schema   = P.Schema;
			}
			if (P.RawToken.Len() > Existing->RawToken.Len()) Existing->RawToken = P.RawToken;
		}
	}

	if (ActionEnum.Num() == 0) return nullptr;

	TArray<FMerged*> Ordered;
	Ordered.Reserve(Merged.Num());
	for (TPair<FString, FMerged>& KV : Merged) Ordered.Add(&KV.Value);
	Ordered.Sort([](const FMerged& A, const FMerged& B)
	{
		if (A.UsedBy.Num() != B.UsedBy.Num()) return A.UsedBy.Num() > B.UsedBy.Num();
		return A.FirstSeen < B.FirstSeen;
	});

	const FString UmbrellaStr = Umbrella.ToString();

	TSharedPtr<FJsonObject> ActionProp = MakeShared<FJsonObject>();
	ActionProp->SetStringField(TEXT("type"), TEXT("string"));
	ActionProp->SetArrayField(TEXT("enum"), ActionEnum);
	ActionProp->SetStringField(TEXT("description"), FString::Printf(
		TEXT("The %s action to invoke. Each parameter below says which actions use it; ")
		TEXT("call get_tool_docs(category='%s', action='<action>') for one action's full parameter docs."),
		*UmbrellaStr, *UmbrellaStr));

	TSharedPtr<FJsonObject> Props = MakeShared<FJsonObject>();
	Props->SetObjectField(TEXT("action"), ActionProp);

	int32 Emitted = 0;
	for (FMerged* Mp : Ordered)
	{
		if (Emitted >= MaxProperties) break;
		FMerged& M = *Mp;
		if (M.Name.Equals(TEXT("action"), ESearchCase::IgnoreCase)) continue;

		// Copy so the cached per-parameter schema from the parser is never mutated in place.
		TSharedPtr<FJsonObject> PropSchema = MakeShared<FJsonObject>();
		if (M.Schema.IsValid())
		{
			FJsonObject::Duplicate(M.Schema, PropSchema);
		}
		else
		{
			PropSchema->SetStringField(TEXT("type"), M.JsonType.IsEmpty() ? TEXT("string") : *M.JsonType);
		}

		FString UsedBy;
		const int32 Shown = FMath::Min(MaxActionsInDesc, M.UsedBy.Num());
		for (int32 i = 0; i < Shown; ++i)
		{
			if (i > 0) UsedBy += TEXT(", ");
			UsedBy += M.UsedBy[i];
		}
		if (M.UsedBy.Num() > Shown)
		{
			UsedBy += FString::Printf(TEXT(" (+%d more)"), M.UsedBy.Num() - Shown);
		}
		PropSchema->SetStringField(TEXT("description"),
			FString::Printf(TEXT("used by: %s — %s"), *UsedBy, *M.RawToken));

		Props->SetObjectField(M.Name, PropSchema);
		++Emitted;
	}

	TSharedPtr<FJsonObject> Schema = MakeShared<FJsonObject>();
	Schema->SetStringField(TEXT("type"), TEXT("object"));
	Schema->SetObjectField(TEXT("properties"), Props);
	Schema->SetArrayField(TEXT("required"),
		TArray<TSharedPtr<FJsonValue>>{ MakeShared<FJsonValueString>(TEXT("action")) });
	Schema->SetBoolField(TEXT("additionalProperties"), true);
	return Schema;
}

void FUECPToolDispatcherImpl::RemoveToolMetadataForOwner(FName ExtensionId)
{
	checkf(IsInGameThread(),
		TEXT("IUECPToolDispatcher::RemoveToolMetadataForOwner must be called on the game thread"));
	if (ExtensionId.IsNone()) return;
	FScopeLock Lock(&RegistryLock);
	TArray<FName> ToRemove;
	for (const TPair<FName, FName>& KV : ToolMetaOwners)
		if (KV.Value == ExtensionId) ToRemove.Add(KV.Key);
	for (const FName& Action : ToRemove)
	{
		if (const FUECPToolMeta* M = ToolMeta.Find(Action)) UmbrellaSchemaCache.Remove(M->Umbrella);
		ToolMeta.Remove(Action);
		ToolMetaOwners.Remove(Action);
	}
}

void FUECPToolDispatcherImpl::RemoveHandlersForOwner(FName ExtensionId)
{
	checkf(IsInGameThread(),
		TEXT("IUECPToolDispatcher::RemoveHandlersForOwner must be called on the game thread"));
	if (ExtensionId.IsNone()) return;
	FScopeLock Lock(&RegistryLock);
	TArray<FName> ToRemove;
	for (const TPair<FName, FRegistration>& KV : Handlers)
		if (KV.Value.Owner == ExtensionId) ToRemove.Add(KV.Key);
	for (const FName& ToolName : ToRemove)
		Handlers.Remove(ToolName);
}

TArray<FName> FUECPToolDispatcherImpl::GetToolsForOwner(FName ExtensionId) const
{
	TArray<FName> Result;
	if (ExtensionId.IsNone()) return Result;
	FScopeLock Lock(&RegistryLock);
	for (const TPair<FName, FRegistration>& KV : Handlers)
		if (KV.Value.Owner == ExtensionId) Result.Add(KV.Key);
	return Result;
}
