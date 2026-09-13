// Copyright 2026, BlueprintsLab, All rights reserved

#include "Services/IUECPCrewService.h"
#include "Types/CrewTypes.h"

bool IUECPCrewService::ValidatePlan(const TArray<FCrewCheckpoint>& Plan, TArray<FString>& OutErrors)
{
	OutErrors.Reset();

	TSet<FString> SeenIds;
	for (const FCrewCheckpoint& C : Plan)
	{
		if (C.CheckpointId.IsEmpty())
		{
			OutErrors.Add(TEXT("checkpoint has empty id"));
			continue;
		}
		if (SeenIds.Contains(C.CheckpointId))
			OutErrors.Add(FString::Printf(TEXT("duplicate checkpoint id '%s' (each must be unique)"), *C.CheckpointId));
		else
			SeenIds.Add(C.CheckpointId);
	}

	for (const FCrewCheckpoint& C : Plan)
	{
		for (const FString& Dep : C.DependsOn)
		{
			if (Dep == C.CheckpointId)
				OutErrors.Add(FString::Printf(TEXT("checkpoint '%s' depends on itself"), *C.CheckpointId));
			else if (!SeenIds.Contains(Dep))
				OutErrors.Add(FString::Printf(TEXT("checkpoint '%s' depends on missing checkpoint '%s'"),
					*C.CheckpointId, *Dep));
		}
	}

	TMap<FString, const FCrewCheckpoint*> ById;
	for (const FCrewCheckpoint& C : Plan)
		if (!C.CheckpointId.IsEmpty()) ById.Add(C.CheckpointId, &C);

	for (const FCrewCheckpoint& Start : Plan)
	{
		if (Start.CheckpointId.IsEmpty()) continue;

		TArray<FString> Stack;
		TSet<FString>   InStack;
		Stack.Add(Start.CheckpointId);
		InStack.Add(Start.CheckpointId);

		bool bFoundCycle = false;
		while (Stack.Num() > 0 && !bFoundCycle)
		{
			const FString Top = Stack.Last();
			const FCrewCheckpoint* Node = ById.FindRef(Top);
			if (!Node) { Stack.Pop(); InStack.Remove(Top); continue; }

			bool bDescended = false;
			for (const FString& Dep : Node->DependsOn)
			{
				if (InStack.Contains(Dep))
				{
					FString Cycle = Dep;
					for (int32 i = Stack.Num() - 1; i >= 0; --i)
					{
						Cycle = Stack[i] + TEXT(" -> ") + Cycle;
						if (Stack[i] == Dep) break;
					}
					OutErrors.Add(FString::Printf(TEXT("dependency cycle: %s"), *Cycle));
					bFoundCycle = true;
					break;
				}
				if (ById.Contains(Dep))
				{
					Stack.Add(Dep);
					InStack.Add(Dep);
					bDescended = true;
					break;
				}
			}
			if (!bDescended && !bFoundCycle) { Stack.Pop(); InStack.Remove(Top); }
		}
		if (bFoundCycle) break;
	}

	return OutErrors.Num() == 0;
}
