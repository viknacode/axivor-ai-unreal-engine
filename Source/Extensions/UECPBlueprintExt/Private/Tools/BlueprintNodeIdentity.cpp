// Copyright 2026, BlueprintsLab, All rights reserved.

#include "Tools/BlueprintNodeIdentity.h"

#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"

namespace BlueprintNodeIdentity
{

static const TCHAR* const LogicalIdKey = TEXT("UECP.LogicalId");
static const TCHAR* const LegacyToken  = TEXT("GEID:");

const TCHAR* GetMetaDataKey()
{
	return LogicalIdKey;
}

static UPackage* PackageFor(UBlueprint* Blueprint, const UObject* Node)
{
	if (Node)
	{
		if (UPackage* Pkg = Node->GetPackage()) return Pkg;
	}
	return Blueprint ? Blueprint->GetPackage() : nullptr;
}

static FString GuidDigits(const UEdGraphNode* Node)
{
	return Node ? Node->NodeGuid.ToString(EGuidFormats::Digits) : FString();
}

/** Reads the sidecar value and validates the guid prefix. Empty when absent or stale. */
static FString ReadSidecar(UBlueprint* Blueprint, const UEdGraphNode* Node)
{
	if (!Node) return FString();
#if WITH_METADATA
	UPackage* Pkg = PackageFor(Blueprint, Node);
	if (!Pkg) return FString();

	const FString& Raw = Pkg->GetMetaData().GetValue(Node, LogicalIdKey);
	if (Raw.IsEmpty()) return FString();

	int32 Bar = INDEX_NONE;
	if (!Raw.FindChar(TEXT('|'), Bar)) return FString();

	// Object names can be reused across sessions; the guid prefix ties the entry to this node.
	if (!Raw.Left(Bar).Equals(GuidDigits(Node), ESearchCase::IgnoreCase)) return FString();
	return Raw.Mid(Bar + 1);
#else
	return FString();
#endif
}

bool StripLegacyIdToken(FString& InOutComment, FString* OutLegacyId)
{
	const int32 TokenIdx = InOutComment.Find(LegacyToken, ESearchCase::CaseSensitive);
	if (TokenIdx == INDEX_NONE) return false;

	int32 EndIdx = InOutComment.Len();
	for (int32 i = TokenIdx; i < InOutComment.Len(); ++i)
	{
		if (InOutComment[i] == TEXT('\n') || InOutComment[i] == TEXT('\r')) { EndIdx = i; break; }
	}

	FString Id = InOutComment.Mid(TokenIdx + FCString::Strlen(LegacyToken), EndIdx - TokenIdx - FCString::Strlen(LegacyToken));
	Id.TrimStartAndEndInline();
	if (OutLegacyId) *OutLegacyId = Id;

	FString Remaining = InOutComment.Left(TokenIdx) + InOutComment.Mid(EndIdx);
	Remaining.TrimStartAndEndInline();
	InOutComment = Remaining;
	return true;
}

void SetLogicalId(UBlueprint* Blueprint, UEdGraphNode* Node, const FString& LogicalId)
{
	if (!Node) return;

	// Always scrub the legacy token, even when the new id is empty.
	FString Comment = Node->NodeComment;
	if (StripLegacyIdToken(Comment))
	{
		Node->NodeComment = Comment;
	}

#if WITH_METADATA
	UPackage* Pkg = PackageFor(Blueprint, Node);
	if (!Pkg) return;

	if (LogicalId.IsEmpty())
	{
		Pkg->GetMetaData().RemoveValue(Node, LogicalIdKey);
		return;
	}
	const FString Value = FString::Printf(TEXT("%s|%s"), *GuidDigits(Node), *LogicalId);
	Pkg->GetMetaData().SetValue(Node, LogicalIdKey, *Value);
#else
	(void)Blueprint;
	(void)LogicalId;
#endif
}

FString GetLogicalId(UBlueprint* Blueprint, const UEdGraphNode* Node)
{
	if (!Node) return FString();

	FString Id = ReadSidecar(Blueprint, Node);
	if (!Id.IsEmpty()) return Id;

	// Legacy: the id used to live in the user-facing comment. Migrate on read.
	if (Node->NodeComment.Contains(LegacyToken))
	{
		FString Comment = Node->NodeComment;
		FString LegacyId;
		if (StripLegacyIdToken(Comment, &LegacyId) && !LegacyId.IsEmpty())
		{
			// Migration is a benign in-memory normalisation; it is idempotent and repeats on the
			// next load if the asset is not saved, so it deliberately does not dirty the package.
			SetLogicalId(Blueprint, const_cast<UEdGraphNode*>(Node), LegacyId);
			return LegacyId;
		}
	}
	return FString();
}

bool HasLogicalId(UBlueprint* Blueprint, const UEdGraphNode* Node)
{
	return !GetLogicalId(Blueprint, Node).IsEmpty();
}

void ClearLogicalId(UBlueprint* Blueprint, UEdGraphNode* Node)
{
	SetLogicalId(Blueprint, Node, FString());
}

UEdGraphNode* FindNodeInGraphByLogicalId(UEdGraph* Graph, const FString& LogicalId, bool bCaseSensitive)
{
	if (!Graph || LogicalId.IsEmpty()) return nullptr;
	const ESearchCase::Type Case = bCaseSensitive ? ESearchCase::CaseSensitive : ESearchCase::IgnoreCase;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (!IsValid(Node)) continue;
		const FString Id = GetLogicalId(nullptr, Node);
		if (!Id.IsEmpty() && Id.Equals(LogicalId, Case)) return Node;
	}
	return nullptr;
}

UEdGraphNode* FindNodeByLogicalId(UBlueprint* Blueprint, const FString& LogicalId, UEdGraph* PreferredGraph)
{
	if (LogicalId.IsEmpty()) return nullptr;

	if (PreferredGraph)
	{
		if (UEdGraphNode* N = FindNodeInGraphByLogicalId(PreferredGraph, LogicalId, true)) return N;
		if (UEdGraphNode* N = FindNodeInGraphByLogicalId(PreferredGraph, LogicalId, false)) return N;
	}
	if (!Blueprint) return nullptr;

	TArray<UEdGraph*> AllGraphs;
	Blueprint->GetAllGraphs(AllGraphs);
	for (UEdGraph* G : AllGraphs)
	{
		if (G == PreferredGraph) continue;
		if (UEdGraphNode* N = FindNodeInGraphByLogicalId(G, LogicalId, true)) return N;
	}
	for (UEdGraph* G : AllGraphs)
	{
		if (G == PreferredGraph) continue;
		if (UEdGraphNode* N = FindNodeInGraphByLogicalId(G, LogicalId, false)) return N;
	}
	return nullptr;
}

FString GetNodeIdOrGuid(UBlueprint* Blueprint, const UEdGraphNode* Node)
{
	if (!Node) return FString();
	const FString Id = GetLogicalId(Blueprint, Node);
	return Id.IsEmpty() ? GuidDigits(Node) : Id;
}

int32 MigrateLegacyIds(UBlueprint* Blueprint)
{
	if (!Blueprint) return 0;
	int32 Migrated = 0;
	TArray<UEdGraph*> AllGraphs;
	Blueprint->GetAllGraphs(AllGraphs);
	for (UEdGraph* G : AllGraphs)
	{
		if (!G) continue;
		for (UEdGraphNode* Node : G->Nodes)
		{
			if (!IsValid(Node) || !Node->NodeComment.Contains(LegacyToken)) continue;
			const bool bHadSidecar = !ReadSidecar(Blueprint, Node).IsEmpty();
			if (bHadSidecar)
			{
				// Sidecar already authoritative — only scrub the comment.
				FString Comment = Node->NodeComment;
				if (StripLegacyIdToken(Comment)) Node->NodeComment = Comment;
			}
			else
			{
				GetLogicalId(Blueprint, Node);
			}
			Migrated++;
		}
	}
	return Migrated;
}

TMap<FGuid, FString> CollectLogicalIds(UBlueprint* Blueprint)
{
	TMap<FGuid, FString> Out;
	if (!Blueprint) return Out;
	TArray<UEdGraph*> AllGraphs;
	Blueprint->GetAllGraphs(AllGraphs);
	for (UEdGraph* G : AllGraphs)
	{
		if (!G) continue;
		for (UEdGraphNode* Node : G->Nodes)
		{
			if (!IsValid(Node)) continue;
			const FString Id = GetLogicalId(Blueprint, Node);
			if (!Id.IsEmpty()) Out.Add(Node->NodeGuid, Id);
		}
	}
	return Out;
}

}
