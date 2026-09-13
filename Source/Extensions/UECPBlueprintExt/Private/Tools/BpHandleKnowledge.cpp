// Copyright 2026, BlueprintsLab, All rights reserved.

#include "Tools/BpHandleKnowledge.h"

#include "BpHandleKnowledge_Generated.inl"

namespace BpHandleKnowledge
{
	static const TMap<FString, TMap<FString, FString>>& GetPinRenameTable()
	{
		static const TMap<FString, TMap<FString, FString>> Table = {
			{ TEXT("fn.KismetArrayLibrary.Array_Add"), {
				{ TEXT("ItemToAdd"), TEXT("NewItem") },
				{ TEXT("Item"),      TEXT("NewItem") },
				{ TEXT("Value"),     TEXT("NewItem") },
				{ TEXT("Element"),   TEXT("NewItem") },
			}},
			{ TEXT("fn.KismetArrayLibrary.Array_AddUnique"), {
				{ TEXT("ItemToAdd"), TEXT("NewItem") },
				{ TEXT("Item"),      TEXT("NewItem") },
			}},

			{ TEXT("fn.KismetArrayLibrary.Array_Remove"), {
				{ TEXT("Item"),     TEXT("IndexToRemove") },
				{ TEXT("Value"),    TEXT("IndexToRemove") },
				{ TEXT("Element"),  TEXT("IndexToRemove") },
			}},
			{ TEXT("fn.KismetArrayLibrary.Array_RemoveItem"), {
				{ TEXT("IndexToRemove"), TEXT("Item") },
				{ TEXT("Index"),          TEXT("Item") },
			}},
			{ TEXT("fn.KismetArrayLibrary.Array_Contains"), {
				{ TEXT("Item"),  TEXT("ItemToFind") },
				{ TEXT("Value"), TEXT("ItemToFind") },
			}},
			{ TEXT("fn.KismetArrayLibrary.Array_Find"), {
				{ TEXT("Item"),  TEXT("ItemToFind") },
				{ TEXT("Value"), TEXT("ItemToFind") },
			}},

			{ TEXT("fn.KismetSystemLibrary.DrawDebugSphere"), {
				{ TEXT("Position"), TEXT("Center") },
				{ TEXT("Location"), TEXT("Center") },
			}},
			{ TEXT("fn.KismetSystemLibrary.DrawDebugBox"), {
				{ TEXT("Position"), TEXT("Center") },
				{ TEXT("Location"), TEXT("Center") },
			}},
			{ TEXT("fn.KismetSystemLibrary.DrawDebugPoint"), {
				{ TEXT("Location"), TEXT("Position") },
				{ TEXT("Center"),   TEXT("Position") },
			}},

			{ TEXT("fn.GameplayStatics.SpawnActorFromClass"), {
				{ TEXT("Class"),          TEXT("ActorClass") },
				{ TEXT("SpawnClass"),     TEXT("ActorClass") },
				{ TEXT("SpawnLocation"),  TEXT("Location") },
				{ TEXT("SpawnRotation"),  TEXT("Rotation") },
				{ TEXT("Outer"),          TEXT("Owner") },
			}},

			{ TEXT("fn.GameplayStatics.BeginDeferredActorSpawnFromClass"), {
				{ TEXT("Outer"),  TEXT("Owner") },
				{ TEXT("Class"),  TEXT("ActorClass") },
			}},
			{ TEXT("fn.GameplayStatics.FinishSpawningActor"), {
				{ TEXT("Owner"),  TEXT("Actor") },
			}},

			{ TEXT("fn.KismetMathLibrary.Vector_Distance"), {
				{ TEXT("A"), TEXT("V1") }, { TEXT("B"), TEXT("V2") },
			}},
			{ TEXT("fn.KismetMathLibrary.Vector_Distance2D"), {
				{ TEXT("A"), TEXT("V1") }, { TEXT("B"), TEXT("V2") },
			}},
			{ TEXT("fn.KismetMathLibrary.Vector_DistanceSquared"), {
				{ TEXT("A"), TEXT("V1") }, { TEXT("B"), TEXT("V2") },
			}},
			{ TEXT("fn.KismetMathLibrary.Vector_DotProduct"), {
				{ TEXT("A"), TEXT("V1") }, { TEXT("B"), TEXT("V2") },
			}},
			{ TEXT("fn.KismetMathLibrary.Vector_CrossProduct"), {
				{ TEXT("A"), TEXT("V1") }, { TEXT("B"), TEXT("V2") },
			}},

			{ TEXT("fn.KismetMathLibrary.Min"), {
				{ TEXT("ValueA"), TEXT("A") }, { TEXT("ValueB"), TEXT("B") },
			}},
			{ TEXT("fn.KismetMathLibrary.Max"), {
				{ TEXT("ValueA"), TEXT("A") }, { TEXT("ValueB"), TEXT("B") },
			}},
			{ TEXT("fn.KismetMathLibrary.FMin"), {
				{ TEXT("ValueA"), TEXT("A") }, { TEXT("ValueB"), TEXT("B") },
			}},
			{ TEXT("fn.KismetMathLibrary.FMax"), {
				{ TEXT("ValueA"), TEXT("A") }, { TEXT("ValueB"), TEXT("B") },
			}},

			{ TEXT("fn.KismetMathLibrary.SelectFloat"), {
				{ TEXT("AValue"), TEXT("A") }, { TEXT("BValue"), TEXT("B") },
				{ TEXT("PickA"),  TEXT("bPickA") },
			}},
			{ TEXT("fn.KismetMathLibrary.SelectInt"), {
				{ TEXT("AValue"), TEXT("A") }, { TEXT("BValue"), TEXT("B") },
				{ TEXT("PickA"),  TEXT("bPickA") },
			}},

			{ TEXT("fn.KismetStringLibrary.Conv_IntToString"), {
				{ TEXT("InputInt"), TEXT("InInt") },
				{ TEXT("Value"),    TEXT("InInt") },
				{ TEXT("Integer"),  TEXT("InInt") },
				{ TEXT("Int"),      TEXT("InInt") },
			}},
			{ TEXT("fn.KismetStringLibrary.Conv_DoubleToString"), {
				{ TEXT("InFloat"),    TEXT("InDouble") },
				{ TEXT("InputFloat"), TEXT("InDouble") },
				{ TEXT("Value"),      TEXT("InDouble") },
				{ TEXT("Float"),      TEXT("InDouble") },
				{ TEXT("Double"),     TEXT("InDouble") },
			}},
			{ TEXT("fn.KismetStringLibrary.Conv_BoolToString"), {
				{ TEXT("InBool"), TEXT("InBool") },
				{ TEXT("Value"),  TEXT("InBool") },
				{ TEXT("Bool"),   TEXT("InBool") },
			}},
			{ TEXT("fn.KismetStringLibrary.Conv_NameToString"), {
				{ TEXT("InName"),    TEXT("InName") },
				{ TEXT("Value"),     TEXT("InName") },
				{ TEXT("Name"),      TEXT("InName") },
				{ TEXT("InputName"), TEXT("InName") },
			}},
			{ TEXT("fn.KismetStringLibrary.Conv_VectorToString"), {
				{ TEXT("InVector"),    TEXT("InVec") },
				{ TEXT("Vector"),      TEXT("InVec") },
				{ TEXT("Value"),       TEXT("InVec") },
			}},
			{ TEXT("fn.KismetTextLibrary.Conv_StringToText"), {
				{ TEXT("InString"), TEXT("InString") },
				{ TEXT("String"),   TEXT("InString") },
				{ TEXT("Value"),    TEXT("InString") },
			}},

			{ TEXT("fn.BlueprintMapLibrary.Map_Add"), {
				{ TEXT("Map"),        TEXT("TargetMap") },
				{ TEXT("InMap"),      TEXT("TargetMap") },
				{ TEXT("Dictionary"), TEXT("TargetMap") },
			}},
			{ TEXT("fn.BlueprintMapLibrary.Map_Remove"), {
				{ TEXT("Map"),        TEXT("TargetMap") },
				{ TEXT("InMap"),      TEXT("TargetMap") },
			}},
			{ TEXT("fn.BlueprintMapLibrary.Map_Find"), {
				{ TEXT("Map"),        TEXT("TargetMap") },
				{ TEXT("InMap"),      TEXT("TargetMap") },
			}},
			{ TEXT("fn.BlueprintMapLibrary.Map_Contains"), {
				{ TEXT("Map"),        TEXT("TargetMap") },
				{ TEXT("InMap"),      TEXT("TargetMap") },
			}},
			{ TEXT("fn.BlueprintMapLibrary.Map_Keys"), {
				{ TEXT("Map"),        TEXT("TargetMap") },
				{ TEXT("InMap"),      TEXT("TargetMap") },
			}},
			{ TEXT("fn.BlueprintMapLibrary.Map_Values"), {
				{ TEXT("Map"),        TEXT("TargetMap") },
				{ TEXT("InMap"),      TEXT("TargetMap") },
			}},
			{ TEXT("fn.BlueprintMapLibrary.Map_Length"), {
				{ TEXT("Map"),        TEXT("TargetMap") },
				{ TEXT("InMap"),      TEXT("TargetMap") },
			}},
			{ TEXT("fn.BlueprintMapLibrary.Map_Clear"), {
				{ TEXT("Map"),        TEXT("TargetMap") },
				{ TEXT("InMap"),      TEXT("TargetMap") },
			}},

			{ TEXT("fn.KismetArrayLibrary.Array_Length"), {
				{ TEXT("Array"), TEXT("TargetArray") }, { TEXT("InArray"), TEXT("TargetArray") },
			}},
			{ TEXT("fn.KismetArrayLibrary.Array_Clear"), {
				{ TEXT("Array"), TEXT("TargetArray") }, { TEXT("InArray"), TEXT("TargetArray") },
			}},
			{ TEXT("fn.KismetArrayLibrary.Array_Get"), {
				{ TEXT("Array"), TEXT("TargetArray") }, { TEXT("InArray"), TEXT("TargetArray") },
			}},
			{ TEXT("fn.KismetArrayLibrary.Array_Set"), {
				{ TEXT("Array"), TEXT("TargetArray") }, { TEXT("InArray"), TEXT("TargetArray") },
				{ TEXT("Value"),  TEXT("Item") },
				{ TEXT("NewItem"), TEXT("Item") },
			}},

			{ TEXT("fn.KismetSystemLibrary.K2_SetTimer"), {
				{ TEXT("Function"), TEXT("FunctionName") },
				{ TEXT("Method"),   TEXT("FunctionName") },
				{ TEXT("Callback"), TEXT("FunctionName") },
				{ TEXT("Name"),     TEXT("FunctionName") },
				{ TEXT("Interval"), TEXT("Time") },
				{ TEXT("Duration"), TEXT("Time") },
				{ TEXT("Delay"),    TEXT("Time") },
				{ TEXT("Loop"),     TEXT("bLooping") },
				{ TEXT("Looping"),  TEXT("bLooping") },
			}},
			{ TEXT("fn.KismetSystemLibrary.K2_SetTimerDelegate"), {
				{ TEXT("Event"),    TEXT("Delegate") },
				{ TEXT("Function"), TEXT("Delegate") },
				{ TEXT("Interval"), TEXT("Time") },
			}},
			{ TEXT("fn.KismetSystemLibrary.K2_ClearTimer"), {
				{ TEXT("Function"), TEXT("FunctionName") },
				{ TEXT("Method"),   TEXT("FunctionName") },
			}},

			{ TEXT("fn.GameplayStatics.GetAllActorsOfClass"), {
				{ TEXT("Class"),     TEXT("ActorClass") },
				{ TEXT("ActorType"), TEXT("ActorClass") },
				{ TEXT("Type"),      TEXT("ActorClass") },
				{ TEXT("Results"),   TEXT("OutActors") },
				{ TEXT("Array"),     TEXT("OutActors") },
				{ TEXT("Actors"),    TEXT("OutActors") },
			}},
			{ TEXT("fn.GameplayStatics.GetAllActorsWithTag"), {
				{ TEXT("Tag"),     TEXT("Tag") },
				{ TEXT("Results"), TEXT("OutActors") },
			}},

			{ TEXT("fn.GameplayStatics.ApplyDamage"), {
				{ TEXT("Target"),         TEXT("DamagedActor") },
				{ TEXT("Actor"),          TEXT("DamagedActor") },
				{ TEXT("Victim"),         TEXT("DamagedActor") },
				{ TEXT("Amount"),         TEXT("BaseDamage") },
				{ TEXT("Damage"),         TEXT("BaseDamage") },
				{ TEXT("Instigator"),     TEXT("EventInstigator") },
				{ TEXT("DamageInstigator"), TEXT("EventInstigator") },
				{ TEXT("Causer"),         TEXT("DamageCauser") },
				{ TEXT("Source"),         TEXT("DamageCauser") },
				{ TEXT("Type"),           TEXT("DamageTypeClass") },
			}},

			{ TEXT("fn.KismetSystemLibrary.LineTraceSingle"), {
				{ TEXT("Start"),           TEXT("Start") },
				{ TEXT("End"),             TEXT("End") },
				{ TEXT("StartLocation"),   TEXT("Start") },
				{ TEXT("EndLocation"),     TEXT("End") },
				{ TEXT("Channel"),         TEXT("TraceChannel") },
				{ TEXT("Ignored"),         TEXT("ActorsToIgnore") },
				{ TEXT("Ignore"),          TEXT("ActorsToIgnore") },
				{ TEXT("IgnoreActors"),    TEXT("ActorsToIgnore") },
				{ TEXT("Result"),          TEXT("OutHit") },
				{ TEXT("Hit"),             TEXT("OutHit") },
				{ TEXT("HitResult"),       TEXT("OutHit") },
			}},
			{ TEXT("fn.KismetSystemLibrary.SphereTraceSingle"), {
				{ TEXT("StartLocation"),   TEXT("Start") },
				{ TEXT("EndLocation"),     TEXT("End") },
				{ TEXT("Channel"),         TEXT("TraceChannel") },
				{ TEXT("Ignored"),         TEXT("ActorsToIgnore") },
				{ TEXT("IgnoreActors"),    TEXT("ActorsToIgnore") },
				{ TEXT("HitResult"),       TEXT("OutHit") },
			}},

			{ TEXT("fn.KismetSystemLibrary.PrintString"), {
				{ TEXT("String"),    TEXT("InString") },
				{ TEXT("Text"),      TEXT("InString") },
				{ TEXT("Message"),   TEXT("InString") },
				{ TEXT("Msg"),       TEXT("InString") },
				{ TEXT("Value"),     TEXT("InString") },
				{ TEXT("PrintToScreen"), TEXT("bPrintToScreen") },
				{ TEXT("PrintToLog"),    TEXT("bPrintToLog") },
				{ TEXT("Screen"),        TEXT("bPrintToScreen") },
				{ TEXT("Log"),           TEXT("bPrintToLog") },
			}},
			{ TEXT("fn.KismetSystemLibrary.PrintText"), {
				{ TEXT("String"),    TEXT("InText") },
				{ TEXT("Text"),      TEXT("InText") },
				{ TEXT("Message"),   TEXT("InText") },
			}},

			{ TEXT("fn.DataTableFunctionLibrary.GetDataTableRow"), {
				{ TEXT("DataTable"), TEXT("Table") },
				{ TEXT("Row"),       TEXT("RowName") },
				{ TEXT("Key"),       TEXT("RowName") },
				{ TEXT("Name"),      TEXT("RowName") },
			}},
		};
		return Table;
	}

	static const TMap<FString, TMap<FString, FString>>& GetForbiddenInputTable()
	{
		static const FString ForEachLoopBodyReason =
			TEXT("LoopBody is an exec OUTPUT of ForEachLoop — wire OUTWARD from it, never INTO it. ")
			TEXT("Wrong: 'someNode.then → forEach.LoopBody'. Correct: 'forEach.LoopBody → firstBodyNode.execute'.");
		static const FString ForEachCompletedReason =
			TEXT("Completed is an exec OUTPUT — chain post-loop logic FROM it, never TO it. ")
			TEXT("Wrong: 'lastBodyNode.then → forEach.Completed'. Correct: 'forEach.Completed → postLoopNode.execute'.");
		static const FString ForEachArrayElementReason =
			TEXT("ArrayElement is a DATA OUTPUT — use it as a wire source, never a target.");
		static const FString ForEachArrayIndexReason =
			TEXT("ArrayIndex is a DATA OUTPUT — use it as a wire source, never a target.");
		static const FString SequenceThenReason =
			TEXT("Sequence's 'Then N' pins are EXEC OUTPUTS — wire OUTWARD from them. ")
			TEXT("Sequence has ONE exec input ('execute'). Wrong: 'foo.then → seq.Then 0'. Correct: 'foo.then → seq.execute'.");
		static const FString BranchOutputReason =
			TEXT("Branch's True/False pins are EXEC OUTPUTS — wire OUTWARD from them. ")
			TEXT("Wrong: 'foo.then → branch.True'. Correct: 'foo.then → branch.execute'.");

		static const TMap<FString, TMap<FString, FString>> Table = {
			{ TEXT("k2.ForEachLoop"), {
				{ TEXT("LoopBody"),      ForEachLoopBodyReason },
				{ TEXT("Completed"),     ForEachCompletedReason },
				{ TEXT("ArrayElement"),  ForEachArrayElementReason },
				{ TEXT("Array Element"), ForEachArrayElementReason },
				{ TEXT("ArrayIndex"),    ForEachArrayIndexReason },
				{ TEXT("Array Index"),   ForEachArrayIndexReason },
			}},
			{ TEXT("k2.ForEachLoopWithBreak"), {
				{ TEXT("LoopBody"),      ForEachLoopBodyReason },
				{ TEXT("Completed"),     ForEachCompletedReason },
				{ TEXT("ArrayElement"),  ForEachArrayElementReason },
				{ TEXT("Array Element"), ForEachArrayElementReason },
				{ TEXT("ArrayIndex"),    ForEachArrayIndexReason },
				{ TEXT("Array Index"),   ForEachArrayIndexReason },
			}},
			{ TEXT("k2.ForLoop"), {
				{ TEXT("LoopBody"),  ForEachLoopBodyReason },
				{ TEXT("Completed"), ForEachCompletedReason },
				{ TEXT("Index"),     ForEachArrayIndexReason },
			}},
			{ TEXT("k2.Sequence"), {
				{ TEXT("Then 0"), SequenceThenReason }, { TEXT("Then 1"), SequenceThenReason },
				{ TEXT("Then 2"), SequenceThenReason }, { TEXT("Then 3"), SequenceThenReason },
				{ TEXT("Then 4"), SequenceThenReason }, { TEXT("Then 5"), SequenceThenReason },
				{ TEXT("Then0"),  SequenceThenReason }, { TEXT("Then1"),  SequenceThenReason },
				{ TEXT("Then2"),  SequenceThenReason }, { TEXT("Then3"),  SequenceThenReason },
				{ TEXT("Then4"),  SequenceThenReason }, { TEXT("Then5"),  SequenceThenReason },
			}},
			{ TEXT("k2.Branch"), {
				{ TEXT("True"),  BranchOutputReason },
				{ TEXT("False"), BranchOutputReason },
			}},
		};
		return Table;
	}

	static const TMap<FString, TArray<FCompanionRule>>& GetCompanionRuleTable()
	{
		static const TMap<FString, TArray<FCompanionRule>> Table = {
			{ TEXT("fn.KismetSystemLibrary.K2_SetTimer"), {
				{ TEXT("fn.KismetSystemLibrary.K2_SetTimer"),
				  TEXT("Object"), TEXT("k2.Self"), TEXT("self"),
				  TEXT("K2_SetTimer.Object must point to the timer owner. Defaulted to k2.Self so the timer binds correctly.") },
			}},
			{ TEXT("fn.KismetSystemLibrary.K2_SetTimerDelegate"), {
			}},
		};
		return Table;
	}

	static TMap<FString, FString> BuildHandleRedirectTable()
	{
		TMap<FString, FString> Table;

		Table.Add(TEXT("fn.Actor.Destroy"),        TEXT("fn.Actor.K2_DestroyActor"));
		Table.Add(TEXT("fn.Actor.DestroyActor"),   TEXT("fn.Actor.K2_DestroyActor"));
		Table.Add(TEXT("fn.K2_DestroyActor"),      TEXT("fn.Actor.K2_DestroyActor"));

		const TArray<FString> MapFuncs = {
			TEXT("Map_Add"),      TEXT("Map_Remove"),  TEXT("Map_Find"),
			TEXT("Map_Contains"), TEXT("Map_Keys"),    TEXT("Map_Values"),
			TEXT("Map_Length"),   TEXT("Map_Clear"),   TEXT("Map_IsEmpty"),
			TEXT("Map_IsNotEmpty"),
		};
		for (const FString& Fn : MapFuncs)
		{
			Table.Add(FString::Printf(TEXT("fn.KismetMapLibrary.%s"), *Fn),
			          FString::Printf(TEXT("fn.BlueprintMapLibrary.%s"), *Fn));
		}

		for (const auto& KV : Generated::HandleRedirects)
		{
			if (!Table.Contains(KV.Key))
			{
				Table.Add(KV.Key, KV.Value);
			}
		}

		return Table;
	}

	static const TSet<FString>& GetKnownPureSet()
	{
		static const TSet<FString> Set = {
		};
		return Set;
	}

	static const TSet<FString>& GetKnownImpureSet()
	{
		static const TSet<FString> Set = {
		};
		return Set;
	}

	static const TMap<FString, FString>& GetLibraryCanonicalTable()
	{
		static const TMap<FString, FString> Table = {
			{ TEXT("K2_SetTimer"),         TEXT("KismetSystemLibrary") },
			{ TEXT("K2_SetTimerDelegate"), TEXT("KismetSystemLibrary") },
			{ TEXT("K2_ClearTimer"),       TEXT("KismetSystemLibrary") },

			{ TEXT("PrintString"),       TEXT("KismetSystemLibrary") },
			{ TEXT("PrintText"),         TEXT("KismetSystemLibrary") },
			{ TEXT("DrawDebugSphere"),   TEXT("KismetSystemLibrary") },
			{ TEXT("DrawDebugLine"),     TEXT("KismetSystemLibrary") },
			{ TEXT("DrawDebugBox"),      TEXT("KismetSystemLibrary") },
			{ TEXT("DrawDebugPoint"),    TEXT("KismetSystemLibrary") },

			{ TEXT("Map_Add"),       TEXT("BlueprintMapLibrary") },
			{ TEXT("Map_Remove"),    TEXT("BlueprintMapLibrary") },
			{ TEXT("Map_Find"),      TEXT("BlueprintMapLibrary") },
			{ TEXT("Map_Contains"),  TEXT("BlueprintMapLibrary") },
			{ TEXT("Map_Keys"),      TEXT("BlueprintMapLibrary") },
			{ TEXT("Map_Values"),    TEXT("BlueprintMapLibrary") },
			{ TEXT("Map_Length"),    TEXT("BlueprintMapLibrary") },
			{ TEXT("Map_Clear"),     TEXT("BlueprintMapLibrary") },

			{ TEXT("Array_Add"),      TEXT("KismetArrayLibrary") },
			{ TEXT("Array_Remove"),   TEXT("KismetArrayLibrary") },
			{ TEXT("Array_RemoveItem"), TEXT("KismetArrayLibrary") },
			{ TEXT("Array_Contains"), TEXT("KismetArrayLibrary") },
			{ TEXT("Array_Find"),     TEXT("KismetArrayLibrary") },
			{ TEXT("Array_Length"),   TEXT("KismetArrayLibrary") },
			{ TEXT("Array_Clear"),    TEXT("KismetArrayLibrary") },
			{ TEXT("Array_Get"),      TEXT("KismetArrayLibrary") },

			{ TEXT("K2_DestroyActor"),      TEXT("Actor") },
			{ TEXT("GetAllActorsOfClass"),  TEXT("GameplayStatics") },
			{ TEXT("GetPlayerController"),  TEXT("GameplayStatics") },
			{ TEXT("GetPlayerPawn"),        TEXT("GameplayStatics") },
			{ TEXT("GetPlayerCharacter"),   TEXT("GameplayStatics") },

			{ TEXT("EqualEqual_IntInt"),             TEXT("KismetMathLibrary") },
			{ TEXT("NotEqual_IntInt"),               TEXT("KismetMathLibrary") },
			{ TEXT("Less_IntInt"),                   TEXT("KismetMathLibrary") },
			{ TEXT("Greater_IntInt"),                TEXT("KismetMathLibrary") },
			{ TEXT("LessEqual_IntInt"),              TEXT("KismetMathLibrary") },
			{ TEXT("GreaterEqual_IntInt"),           TEXT("KismetMathLibrary") },
			{ TEXT("EqualEqual_ByteByte"),           TEXT("KismetMathLibrary") },
			{ TEXT("NotEqual_ByteByte"),             TEXT("KismetMathLibrary") },
			{ TEXT("Less_ByteByte"),                 TEXT("KismetMathLibrary") },
			{ TEXT("Greater_ByteByte"),              TEXT("KismetMathLibrary") },
			{ TEXT("LessEqual_ByteByte"),            TEXT("KismetMathLibrary") },
			{ TEXT("GreaterEqual_ByteByte"),         TEXT("KismetMathLibrary") },
			{ TEXT("EqualEqual_DoubleDouble"),       TEXT("KismetMathLibrary") },
			{ TEXT("NotEqual_DoubleDouble"),         TEXT("KismetMathLibrary") },
			{ TEXT("Less_DoubleDouble"),             TEXT("KismetMathLibrary") },
			{ TEXT("Greater_DoubleDouble"),          TEXT("KismetMathLibrary") },
			{ TEXT("LessEqual_DoubleDouble"),        TEXT("KismetMathLibrary") },
			{ TEXT("GreaterEqual_DoubleDouble"),     TEXT("KismetMathLibrary") },
			{ TEXT("EqualEqual_StrStr"),             TEXT("KismetMathLibrary") },
			{ TEXT("NotEqual_StrStr"),               TEXT("KismetMathLibrary") },
			{ TEXT("EqualEqual_NameName"),           TEXT("KismetMathLibrary") },
			{ TEXT("NotEqual_NameName"),             TEXT("KismetMathLibrary") },
			{ TEXT("EqualEqual_ObjectObject"),       TEXT("KismetMathLibrary") },
			{ TEXT("NotEqual_ObjectObject"),         TEXT("KismetMathLibrary") },

			{ TEXT("Add_DoubleDouble"),              TEXT("KismetMathLibrary") },
			{ TEXT("Subtract_DoubleDouble"),         TEXT("KismetMathLibrary") },
			{ TEXT("Multiply_DoubleDouble"),         TEXT("KismetMathLibrary") },
			{ TEXT("Divide_DoubleDouble"),           TEXT("KismetMathLibrary") },
			{ TEXT("Add_IntInt"),                    TEXT("KismetMathLibrary") },
			{ TEXT("Subtract_IntInt"),               TEXT("KismetMathLibrary") },
			{ TEXT("Multiply_IntInt"),               TEXT("KismetMathLibrary") },
			{ TEXT("Divide_IntInt"),                 TEXT("KismetMathLibrary") },
			{ TEXT("Percent_DoubleDouble"),          TEXT("KismetMathLibrary") },
			{ TEXT("Percent_IntInt"),                TEXT("KismetMathLibrary") },
			{ TEXT("Not_PreBool"),                   TEXT("KismetMathLibrary") },
			{ TEXT("BooleanAND"),                    TEXT("KismetMathLibrary") },
			{ TEXT("BooleanOR"),                     TEXT("KismetMathLibrary") },

			{ TEXT("Conv_IntToString"),              TEXT("KismetStringLibrary") },
			{ TEXT("Conv_DoubleToString"),           TEXT("KismetStringLibrary") },
			{ TEXT("Conv_BoolToString"),             TEXT("KismetStringLibrary") },
			{ TEXT("Conv_VectorToString"),           TEXT("KismetStringLibrary") },
			{ TEXT("Conv_BoolToInt"),                TEXT("KismetMathLibrary") },
			{ TEXT("Conv_IntToBool"),                TEXT("KismetMathLibrary") },
			{ TEXT("FClamp"),                        TEXT("KismetMathLibrary") },
			{ TEXT("Clamp"),                         TEXT("KismetMathLibrary") },
		};
		return Table;
	}

	FString LookupCanonicalPinName(const FString& Handle, const FString& BadPinName)
	{
		const TMap<FString, TMap<FString, FString>>& Table = GetPinRenameTable();
		if (const TMap<FString, FString>* PerHandle = Table.Find(Handle))
		{
			if (const FString* Canonical = PerHandle->Find(BadPinName))
			{
				return *Canonical;
			}
		}
		if (const TMap<FString, FString>* GenPerHandle = Generated::PinRenames.Find(Handle))
		{
			if (const FString* Canonical = GenPerHandle->Find(BadPinName))
			{
				return *Canonical;
			}
		}
		return FString();
	}

	bool IsForbiddenInputPin(const FString& Handle, const FString& PinName, FString& OutReason)
	{
		const TMap<FString, TMap<FString, FString>>& Table = GetForbiddenInputTable();
		if (const TMap<FString, FString>* PerHandle = Table.Find(Handle))
		{
			if (const FString* Reason = PerHandle->Find(PinName))
			{
				OutReason = *Reason;
				return true;
			}
		}
		return false;
	}

	const TMap<FString, FString>& GetHandleRedirects()
	{
		static const TMap<FString, FString> Table = BuildHandleRedirectTable();
		return Table;
	}

	TArray<FCompanionRule> GetCompanionRules(const FString& Handle)
	{
		const TMap<FString, TArray<FCompanionRule>>& Table = GetCompanionRuleTable();
		if (const TArray<FCompanionRule>* Found = Table.Find(Handle))
		{
			return *Found;
		}
		return TArray<FCompanionRule>();
	}

	bool IsKnownPure(const FString& Handle)
	{
		return GetKnownPureSet().Contains(Handle);
	}

	bool IsKnownImpure(const FString& Handle)
	{
		return GetKnownImpureSet().Contains(Handle);
	}

	FString CanonicalLibraryFor(const FString& FunctionName)
	{
		const TMap<FString, FString>& Table = GetLibraryCanonicalTable();
		if (const FString* Lib = Table.Find(FunctionName))
		{
			return *Lib;
		}
		if (const FString* GenLib = Generated::LibraryCanonical.Find(FunctionName))
		{
			return *GenLib;
		}
		return FString();
	}
}
