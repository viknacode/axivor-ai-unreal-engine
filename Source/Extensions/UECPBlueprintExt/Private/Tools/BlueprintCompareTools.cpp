// Copyright 2026, BlueprintsLab, All rights reserved

#include "Tools/BlueprintCompareTools.h"
#include "Tools/BlueprintNodeIdentity.h"

#include "Engine/Blueprint.h"
#include "Engine/LevelScriptBlueprint.h"
#include "Engine/World.h"
#include "Editor.h"
#include "EditorAssetLibrary.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphNode_Comment.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_Tunnel.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectHash.h"
#include "UObject/Interface.h"
#include "Misc/DateTime.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "MCPToolsLog.h"

namespace BlueprintCompareTools
{

// ---------------------------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------------------------

static TSharedPtr<FJsonValue> JStr(const FString& S) { return MakeShared<FJsonValueString>(S); }
static TSharedPtr<FJsonValue> JObj(const TSharedPtr<FJsonObject>& O) { return MakeShared<FJsonValueObject>(O); }

static TSharedPtr<FJsonObject> MakeBucket(
	const TArray<TSharedPtr<FJsonValue>>& Added,
	const TArray<TSharedPtr<FJsonValue>>& Removed,
	const TArray<TSharedPtr<FJsonValue>>& Modified)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetArrayField(TEXT("added"), Added);
	Result->SetArrayField(TEXT("removed"), Removed);
	Result->SetArrayField(TEXT("modified"), Modified);
	return Result;
}

static FString PinTypeToString(const FEdGraphPinType& PinType)
{
	FString TypeStr = PinType.PinCategory.ToString();
	if (!PinType.PinSubCategory.IsNone()) TypeStr += TEXT(":") + PinType.PinSubCategory.ToString();
	if (PinType.PinSubCategoryObject.IsValid())
	{
		TypeStr += TEXT("/") + PinType.PinSubCategoryObject->GetName();
	}
	if (PinType.IsArray()) TypeStr += TEXT("[]");
	else if (PinType.IsSet()) TypeStr += TEXT(" (Set)");
	else if (PinType.IsMap()) TypeStr += TEXT(" (Map)");
	if (PinType.bIsReference) TypeStr += TEXT("&");
	return TypeStr;
}

static UBlueprint* LoadBlueprintForCompare(const FString& Path)
{
	UBlueprint* BP = nullptr;
	if (Path.Equals(TEXT("@level_blueprint"), ESearchCase::IgnoreCase))
	{
		UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
		if (World && World->PersistentLevel)
			BP = World->PersistentLevel->GetLevelScriptBlueprint(false);
	}
	else
	{
		BP = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(Path));
	}
	BlueprintNodeIdentity::MigrateLegacyIds(BP);
	return BP;
}

// ---------------------------------------------------------------------------------------------
// Graph records
// ---------------------------------------------------------------------------------------------

struct FNodeRecord
{
	const UEdGraphNode* Node = nullptr;
	FGuid    Identity;      // NodeGuid, possibly remapped
	FString  LogicalId;
	FString  ClassName;
	FString  Title;
	TMap<FString, FString> PinDefaults;   // input pin name -> literal default (unlinked pins only)

	FString DisplayId() const
	{
		return LogicalId.IsEmpty() ? Identity.ToString(EGuidFormats::Digits) : LogicalId;
	}

	TSharedPtr<FJsonObject> ToJson() const
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("id"), DisplayId());
		O->SetStringField(TEXT("guid"), Identity.ToString(EGuidFormats::Digits));
		O->SetStringField(TEXT("class"), ClassName);
		O->SetStringField(TEXT("title"), Title);
		return O;
	}
};

struct FLinkRecord
{
	FGuid   FromIdentity;
	FString FromPin;
	FGuid   ToIdentity;
	FString ToPin;
};

struct FGraphRecord
{
	FString Name;
	TMap<FGuid, FNodeRecord> Nodes;
	TArray<FLinkRecord> Links;
};

static FString PinDefaultString(const UEdGraphPin* Pin)
{
	if (!Pin->DefaultValue.IsEmpty()) return Pin->DefaultValue;
	if (Pin->DefaultObject) return Pin->DefaultObject->GetPathName();
	if (!Pin->DefaultTextValue.IsEmpty()) return Pin->DefaultTextValue.ToString();
	return FString();
}

static FGraphRecord RecordGraph(
	UBlueprint* BP, UEdGraph* Graph,
	const TMap<FGuid, FString>* LogicalIds,
	const TMap<FGuid, FGuid>* GuidRemap)
{
	FGraphRecord Rec;
	if (!Graph) return Rec;
	Rec.Name = Graph->GetName();

	auto IdentityOf = [GuidRemap](const UEdGraphNode* N) -> FGuid
	{
		if (GuidRemap)
		{
			if (const FGuid* Mapped = GuidRemap->Find(N->NodeGuid)) return *Mapped;
		}
		return N->NodeGuid;
	};

	for (UEdGraphNode* N : Graph->Nodes)
	{
		if (!IsValid(N) || N->IsA<UEdGraphNode_Comment>()) continue;

		FNodeRecord R;
		R.Node      = N;
		R.Identity  = IdentityOf(N);
		R.ClassName = N->GetClass()->GetName();
		R.Title     = N->GetNodeTitle(ENodeTitleType::ListView).ToString();
		if (LogicalIds)
		{
			if (const FString* Lid = LogicalIds->Find(R.Identity)) R.LogicalId = *Lid;
		}
		else
		{
			R.LogicalId = BlueprintNodeIdentity::GetLogicalId(BP, N);
		}

		for (const UEdGraphPin* Pin : N->Pins)
		{
			if (!Pin || Pin->bHidden || Pin->bOrphanedPin) continue;
			if (Pin->Direction == EGPD_Input)
			{
				if (Pin->LinkedTo.Num() > 0) continue;
				if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec) continue;
				const FString Def = PinDefaultString(Pin);
				if (!Def.IsEmpty()) R.PinDefaults.Add(Pin->PinName.ToString(), Def);
			}
			else
			{
				for (const UEdGraphPin* Linked : Pin->LinkedTo)
				{
					if (!Linked) continue;
					const UEdGraphNode* Other = Linked->GetOwningNodeUnchecked();
					if (!Other || !IsValid(Other) || Other->IsA<UEdGraphNode_Comment>()) continue;
					FLinkRecord L;
					L.FromIdentity = R.Identity;
					L.FromPin      = Pin->PinName.ToString();
					L.ToIdentity   = IdentityOf(Other);
					L.ToPin        = Linked->PinName.ToString();
					Rec.Links.Add(MoveTemp(L));
				}
			}
		}

		Rec.Nodes.Add(R.Identity, MoveTemp(R));
	}
	return Rec;
}

struct FGraphDiffStats
{
	int32 NodesAdded = 0, NodesRemoved = 0, NodesChanged = 0;
	int32 LinksAdded = 0, LinksRemoved = 0, ImpliedLinksSkipped = 0;
	bool Any() const { return NodesAdded || NodesRemoved || NodesChanged || LinksAdded || LinksRemoved; }
	void Accumulate(const FGraphDiffStats& O)
	{
		NodesAdded += O.NodesAdded; NodesRemoved += O.NodesRemoved; NodesChanged += O.NodesChanged;
		LinksAdded += O.LinksAdded; LinksRemoved += O.LinksRemoved; ImpliedLinksSkipped += O.ImpliedLinksSkipped;
	}
};

static TSharedPtr<FJsonObject> DiffGraphRecords(const FGraphRecord& A, const FGraphRecord& B, FGraphDiffStats& OutStats)
{
	// 1. Match by NodeGuid.
	TMap<FGuid, FGuid> AtoB;
	TSet<FGuid> MatchedB;
	for (const auto& Pair : A.Nodes)
	{
		if (B.Nodes.Contains(Pair.Key))
		{
			AtoB.Add(Pair.Key, Pair.Key);
			MatchedB.Add(Pair.Key);
		}
	}

	// 2. Fall back to the logical id for nodes the guid could not pair (e.g. two different
	//    assets built from the same spec, or a node that was re-created by a rebuild).
	TMap<FString, TArray<FGuid>> UnmatchedBByLogicalId;
	for (const auto& Pair : B.Nodes)
	{
		if (MatchedB.Contains(Pair.Key) || Pair.Value.LogicalId.IsEmpty()) continue;
		UnmatchedBByLogicalId.FindOrAdd(Pair.Value.LogicalId).Add(Pair.Key);
	}
	for (const auto& Pair : A.Nodes)
	{
		if (AtoB.Contains(Pair.Key) || Pair.Value.LogicalId.IsEmpty()) continue;
		TArray<FGuid>* Candidates = UnmatchedBByLogicalId.Find(Pair.Value.LogicalId);
		if (!Candidates) continue;
		int32 Pick = INDEX_NONE;
		for (int32 i = 0; i < Candidates->Num(); ++i)
		{
			if (MatchedB.Contains((*Candidates)[i])) continue;
			const FNodeRecord& Cand = B.Nodes[(*Candidates)[i]];
			if (Cand.ClassName == Pair.Value.ClassName) { Pick = i; break; }
			if (Pick == INDEX_NONE) Pick = i;
		}
		if (Pick == INDEX_NONE) continue;
		const FGuid BGuid = (*Candidates)[Pick];
		AtoB.Add(Pair.Key, BGuid);
		MatchedB.Add(BGuid);
	}

	TArray<TSharedPtr<FJsonValue>> AddedNodes, RemovedNodes, ChangedNodes;

	for (const auto& Pair : A.Nodes)
	{
		if (!AtoB.Contains(Pair.Key)) RemovedNodes.Add(JObj(Pair.Value.ToJson()));
	}
	for (const auto& Pair : B.Nodes)
	{
		if (!MatchedB.Contains(Pair.Key)) AddedNodes.Add(JObj(Pair.Value.ToJson()));
	}

	for (const auto& Match : AtoB)
	{
		const FNodeRecord& NA = A.Nodes[Match.Key];
		const FNodeRecord& NB = B.Nodes[Match.Value];

		TSharedPtr<FJsonObject> Change = NB.ToJson();
		bool bChanged = false;

		if (NA.ClassName != NB.ClassName)
		{
			Change->SetStringField(TEXT("class_a"), NA.ClassName);
			Change->SetStringField(TEXT("class_b"), NB.ClassName);
			bChanged = true;
		}
		if (NA.Title != NB.Title)
		{
			Change->SetStringField(TEXT("title_a"), NA.Title);
			Change->SetStringField(TEXT("title_b"), NB.Title);
			bChanged = true;
		}

		TSet<FString> PinNames;
		for (const auto& P : NA.PinDefaults) PinNames.Add(P.Key);
		for (const auto& P : NB.PinDefaults) PinNames.Add(P.Key);
		TArray<FString> SortedPins = PinNames.Array();
		SortedPins.Sort();

		TArray<TSharedPtr<FJsonValue>> PinChanges;
		for (const FString& PinName : SortedPins)
		{
			const FString* VA = NA.PinDefaults.Find(PinName);
			const FString* VB = NB.PinDefaults.Find(PinName);
			const FString SA = VA ? *VA : FString();
			const FString SB = VB ? *VB : FString();
			if (SA == SB) continue;
			TSharedPtr<FJsonObject> PC = MakeShared<FJsonObject>();
			PC->SetStringField(TEXT("pin"), PinName);
			PC->SetStringField(TEXT("a"), SA);
			PC->SetStringField(TEXT("b"), SB);
			PinChanges.Add(JObj(PC));
		}
		if (PinChanges.Num() > 0)
		{
			Change->SetArrayField(TEXT("pin_defaults"), PinChanges);
			bChanged = true;
		}

		if (bChanged) ChangedNodes.Add(JObj(Change));
	}

	// 3. Links. Endpoints are canonicalised to the B-side guid so both sides compare directly.
	//    Links touching an added/removed node are implied by that node and are not listed twice.
	auto CanonA = [&AtoB](const FGuid& G) -> FString
	{
		if (const FGuid* M = AtoB.Find(G)) return M->ToString(EGuidFormats::Digits);
		return TEXT("A:") + G.ToString(EGuidFormats::Digits);
	};
	auto LinkKey = [](const FString& From, const FString& FromPin, const FString& To, const FString& ToPin)
	{
		return From + TEXT("|") + FromPin + TEXT("|") + To + TEXT("|") + ToPin;
	};
	auto LinkJson = [](const FGraphRecord& G, const FLinkRecord& L) -> TSharedPtr<FJsonObject>
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		const FNodeRecord* From = G.Nodes.Find(L.FromIdentity);
		const FNodeRecord* To   = G.Nodes.Find(L.ToIdentity);
		O->SetStringField(TEXT("from"), From ? From->DisplayId() : L.FromIdentity.ToString(EGuidFormats::Digits));
		O->SetStringField(TEXT("from_pin"), L.FromPin);
		O->SetStringField(TEXT("to"), To ? To->DisplayId() : L.ToIdentity.ToString(EGuidFormats::Digits));
		O->SetStringField(TEXT("to_pin"), L.ToPin);
		return O;
	};

	TMap<FString, const FLinkRecord*> LinksA, LinksB;
	for (const FLinkRecord& L : A.Links)
	{
		if (!AtoB.Contains(L.FromIdentity) || !AtoB.Contains(L.ToIdentity)) { OutStats.ImpliedLinksSkipped++; continue; }
		LinksA.Add(LinkKey(CanonA(L.FromIdentity), L.FromPin, CanonA(L.ToIdentity), L.ToPin), &L);
	}
	for (const FLinkRecord& L : B.Links)
	{
		if (!MatchedB.Contains(L.FromIdentity) || !MatchedB.Contains(L.ToIdentity)) { OutStats.ImpliedLinksSkipped++; continue; }
		LinksB.Add(LinkKey(L.FromIdentity.ToString(EGuidFormats::Digits), L.FromPin, L.ToIdentity.ToString(EGuidFormats::Digits), L.ToPin), &L);
	}

	TArray<TSharedPtr<FJsonValue>> AddedLinks, RemovedLinks;
	for (const auto& Pair : LinksA)
	{
		if (!LinksB.Contains(Pair.Key)) RemovedLinks.Add(JObj(LinkJson(A, *Pair.Value)));
	}
	for (const auto& Pair : LinksB)
	{
		if (!LinksA.Contains(Pair.Key)) AddedLinks.Add(JObj(LinkJson(B, *Pair.Value)));
	}

	OutStats.NodesAdded   = AddedNodes.Num();
	OutStats.NodesRemoved = RemovedNodes.Num();
	OutStats.NodesChanged = ChangedNodes.Num();
	OutStats.LinksAdded   = AddedLinks.Num();
	OutStats.LinksRemoved = RemovedLinks.Num();

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("name"), B.Name.IsEmpty() ? A.Name : B.Name);
	Result->SetNumberField(TEXT("node_count_a"), A.Nodes.Num());
	Result->SetNumberField(TEXT("node_count_b"), B.Nodes.Num());
	Result->SetArrayField(TEXT("added_nodes"),   AddedNodes);
	Result->SetArrayField(TEXT("removed_nodes"), RemovedNodes);
	Result->SetArrayField(TEXT("changed_nodes"), ChangedNodes);
	Result->SetArrayField(TEXT("added_links"),   AddedLinks);
	Result->SetArrayField(TEXT("removed_links"), RemovedLinks);
	return Result;
}

/** "In:Type, In2:Type -> Out:Type" for function / macro / dispatcher signature graphs. */
static FString GraphSignature(UEdGraph* Graph)
{
	if (!Graph) return FString();
	TArray<FString> Inputs, Outputs;
	for (UEdGraphNode* N : Graph->Nodes)
	{
		if (!IsValid(N)) continue;
		const bool bEntry  = N->IsA<UK2Node_FunctionEntry>() || (N->IsA<UK2Node_Tunnel>() && Cast<UK2Node_Tunnel>(N)->bCanHaveOutputs && !Cast<UK2Node_Tunnel>(N)->bCanHaveInputs);
		const bool bResult = N->IsA<UK2Node_FunctionResult>() || (N->IsA<UK2Node_Tunnel>() && Cast<UK2Node_Tunnel>(N)->bCanHaveInputs && !Cast<UK2Node_Tunnel>(N)->bCanHaveOutputs);
		if (!bEntry && !bResult) continue;
		for (const UEdGraphPin* Pin : N->Pins)
		{
			if (!Pin || Pin->bHidden || Pin->bOrphanedPin) continue;
			if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec) continue;
			if (Pin->PinName == UEdGraphSchema_K2::PN_Self) continue;
			const FString Entry = Pin->PinName.ToString() + TEXT(":") + PinTypeToString(Pin->PinType);
			if (bEntry && Pin->Direction == EGPD_Output) Inputs.Add(Entry);
			else if (bResult && Pin->Direction == EGPD_Input) Outputs.Add(Entry);
		}
	}
	FString Sig = FString::Join(Inputs, TEXT(", "));
	if (Outputs.Num() > 0) Sig += TEXT(" -> ") + FString::Join(Outputs, TEXT(", "));
	return Sig;
}

struct FGraphListDiff
{
	TSharedPtr<FJsonObject> Json;
	int32 Added = 0, Removed = 0, SignatureChanged = 0, GraphsModified = 0;
	FGraphDiffStats Stats;
};

static FGraphListDiff DiffGraphLists(
	UBlueprint* BpA, UBlueprint* BpB,
	const TArray<TObjectPtr<UEdGraph>>& GraphsA, const TArray<TObjectPtr<UEdGraph>>& GraphsB,
	bool bWithSignature, bool bIncludeUnchanged,
	const TMap<FGuid, FString>* LogicalIdsA, const TMap<FGuid, FString>* LogicalIdsB,
	const TMap<FGuid, FGuid>* GuidRemapA)
{
	TMap<FString, UEdGraph*> MapA, MapB;
	for (UEdGraph* G : GraphsA) if (G) MapA.Add(G->GetName(), G);
	for (UEdGraph* G : GraphsB) if (G) MapB.Add(G->GetName(), G);

	auto CountNodes = [](UEdGraph* G)
	{
		int32 N = 0;
		for (UEdGraphNode* Node : G->Nodes) if (IsValid(Node) && !Node->IsA<UEdGraphNode_Comment>()) N++;
		return N;
	};
	auto GraphStub = [&](const FString& Name, UEdGraph* G) -> TSharedPtr<FJsonObject>
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("name"), Name);
		O->SetNumberField(TEXT("node_count"), CountNodes(G));
		if (bWithSignature) O->SetStringField(TEXT("signature"), GraphSignature(G));
		return O;
	};

	TArray<FString> NamesA, NamesB;
	MapA.GetKeys(NamesA); MapB.GetKeys(NamesB);
	NamesA.Sort(); NamesB.Sort();

	FGraphListDiff Out;
	TArray<TSharedPtr<FJsonValue>> Added, Removed, Modified, Unchanged;

	for (const FString& Name : NamesB)
		if (!MapA.Contains(Name)) Added.Add(JObj(GraphStub(Name, MapB[Name])));
	for (const FString& Name : NamesA)
		if (!MapB.Contains(Name)) Removed.Add(JObj(GraphStub(Name, MapA[Name])));

	for (const FString& Name : NamesA)
	{
		UEdGraph** GB = MapB.Find(Name);
		if (!GB) continue;
		UEdGraph* GA = MapA[Name];

		FGraphDiffStats Stats;
		const FGraphRecord RecA = RecordGraph(BpA, GA,  LogicalIdsA, GuidRemapA);
		const FGraphRecord RecB = RecordGraph(BpB, *GB, LogicalIdsB, nullptr);
		TSharedPtr<FJsonObject> GraphDiff = DiffGraphRecords(RecA, RecB, Stats);

		bool bSigChanged = false;
		FString SigA, SigB;
		if (bWithSignature)
		{
			SigA = GraphSignature(GA);
			SigB = GraphSignature(*GB);
			bSigChanged = SigA != SigB;
		}

		if (Stats.Any() || bSigChanged)
		{
			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("name"), Name);
			if (bSigChanged)
			{
				Entry->SetStringField(TEXT("signature_a"), SigA);
				Entry->SetStringField(TEXT("signature_b"), SigB);
				Out.SignatureChanged++;
			}
			if (Stats.Any())
			{
				Entry->SetObjectField(TEXT("graph"), GraphDiff);
				Out.GraphsModified++;
			}
			Out.Stats.Accumulate(Stats);
			Modified.Add(JObj(Entry));
		}
		else if (bIncludeUnchanged)
		{
			Unchanged.Add(JStr(Name));
		}
	}

	Out.Added   = Added.Num();
	Out.Removed = Removed.Num();
	Out.Json = MakeBucket(Added, Removed, Modified);
	if (bIncludeUnchanged) Out.Json->SetArrayField(TEXT("unchanged"), Unchanged);
	return Out;
}

// ---------------------------------------------------------------------------------------------
// Variables / components / interfaces
// ---------------------------------------------------------------------------------------------

static bool IsDispatcherVariable(const FBPVariableDescription& V)
{
	return V.VarType.PinCategory == UEdGraphSchema_K2::PC_MCDelegate;
}

static TSharedPtr<FJsonObject> VarDescToJson(const FBPVariableDescription& V)
{
	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetStringField(TEXT("name"), V.VarName.ToString());
	Obj->SetStringField(TEXT("type"), PinTypeToString(V.VarType));
	Obj->SetStringField(TEXT("category"), V.Category.ToString());
	if (!V.DefaultValue.IsEmpty())
		Obj->SetStringField(TEXT("default_value"), V.DefaultValue);
	Obj->SetBoolField(TEXT("instance_editable"), (V.PropertyFlags & CPF_DisableEditOnInstance) == 0);
	Obj->SetBoolField(TEXT("replicated"), (V.PropertyFlags & CPF_Net) != 0);
	return Obj;
}

static TSharedPtr<FJsonObject> DiffVariables(UBlueprint* BpA, UBlueprint* BpB, int32& OutAdded, int32& OutRemoved, int32& OutModified)
{
	TMap<FName, const FBPVariableDescription*> MapA, MapB;
	for (const FBPVariableDescription& V : BpA->NewVariables) if (!IsDispatcherVariable(V)) MapA.Add(V.VarName, &V);
	for (const FBPVariableDescription& V : BpB->NewVariables) if (!IsDispatcherVariable(V)) MapB.Add(V.VarName, &V);

	TArray<TSharedPtr<FJsonValue>> Added, Removed, Modified;

	for (const auto& Pair : MapB)
		if (!MapA.Contains(Pair.Key)) Added.Add(JObj(VarDescToJson(*Pair.Value)));
	for (const auto& Pair : MapA)
		if (!MapB.Contains(Pair.Key)) Removed.Add(JObj(VarDescToJson(*Pair.Value)));

	for (const auto& Pair : MapA)
	{
		const FBPVariableDescription* const* pB = MapB.Find(Pair.Key);
		if (!pB) continue;
		const FBPVariableDescription& VA = *Pair.Value;
		const FBPVariableDescription& VB = **pB;

		TSharedPtr<FJsonObject> Diff = MakeShared<FJsonObject>();
		Diff->SetStringField(TEXT("name"), VA.VarName.ToString());
		bool bAny = false;

		const FString TypeA = PinTypeToString(VA.VarType), TypeB = PinTypeToString(VB.VarType);
		if (TypeA != TypeB) { Diff->SetStringField(TEXT("type_a"), TypeA); Diff->SetStringField(TEXT("type_b"), TypeB); bAny = true; }
		if (VA.DefaultValue != VB.DefaultValue) { Diff->SetStringField(TEXT("default_a"), VA.DefaultValue); Diff->SetStringField(TEXT("default_b"), VB.DefaultValue); bAny = true; }
		if (!VA.Category.EqualTo(VB.Category)) { Diff->SetStringField(TEXT("category_a"), VA.Category.ToString()); Diff->SetStringField(TEXT("category_b"), VB.Category.ToString()); bAny = true; }

		const bool EditA = (VA.PropertyFlags & CPF_DisableEditOnInstance) == 0, EditB = (VB.PropertyFlags & CPF_DisableEditOnInstance) == 0;
		if (EditA != EditB) { Diff->SetBoolField(TEXT("instance_editable_a"), EditA); Diff->SetBoolField(TEXT("instance_editable_b"), EditB); bAny = true; }
		const bool RepA = (VA.PropertyFlags & CPF_Net) != 0, RepB = (VB.PropertyFlags & CPF_Net) != 0;
		if (RepA != RepB) { Diff->SetBoolField(TEXT("replicated_a"), RepA); Diff->SetBoolField(TEXT("replicated_b"), RepB); bAny = true; }

		if (bAny) Modified.Add(JObj(Diff));
	}

	OutAdded = Added.Num(); OutRemoved = Removed.Num(); OutModified = Modified.Num();
	return MakeBucket(Added, Removed, Modified);
}

static TSharedPtr<FJsonObject> DiffComponents(UBlueprint* BpA, UBlueprint* BpB, int32& OutAdded, int32& OutRemoved, int32& OutModified)
{
	struct FCompInfo { FString Class; FString Parent; };
	auto CollectComponents = [](UBlueprint* BP) -> TMap<FName, FCompInfo>
	{
		TMap<FName, FCompInfo> Out;
		if (!BP->SimpleConstructionScript) return Out;
		for (USCS_Node* Node : BP->SimpleConstructionScript->GetAllNodes())
		{
			if (!Node) continue;
			FCompInfo Info;
			Info.Class = Node->ComponentClass ? Node->ComponentClass->GetName() : TEXT("Unknown");
			if (!Node->ParentComponentOrVariableName.IsNone()) Info.Parent = Node->ParentComponentOrVariableName.ToString();
			Out.Add(Node->GetVariableName(), Info);
		}
		return Out;
	};

	TMap<FName, FCompInfo> CompA = CollectComponents(BpA);
	TMap<FName, FCompInfo> CompB = CollectComponents(BpB);

	auto CompJson = [](const FName& Name, const FCompInfo& Info)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("name"), Name.ToString());
		Obj->SetStringField(TEXT("class"), Info.Class);
		if (!Info.Parent.IsEmpty()) Obj->SetStringField(TEXT("parent"), Info.Parent);
		return Obj;
	};

	TArray<TSharedPtr<FJsonValue>> Added, Removed, Modified;
	for (const auto& Pair : CompB) if (!CompA.Contains(Pair.Key)) Added.Add(JObj(CompJson(Pair.Key, Pair.Value)));
	for (const auto& Pair : CompA) if (!CompB.Contains(Pair.Key)) Removed.Add(JObj(CompJson(Pair.Key, Pair.Value)));
	for (const auto& Pair : CompA)
	{
		const FCompInfo* B = CompB.Find(Pair.Key);
		if (!B) continue;
		if (B->Class == Pair.Value.Class && B->Parent == Pair.Value.Parent) continue;
		TSharedPtr<FJsonObject> Diff = MakeShared<FJsonObject>();
		Diff->SetStringField(TEXT("name"), Pair.Key.ToString());
		if (B->Class != Pair.Value.Class) { Diff->SetStringField(TEXT("class_a"), Pair.Value.Class); Diff->SetStringField(TEXT("class_b"), B->Class); }
		if (B->Parent != Pair.Value.Parent) { Diff->SetStringField(TEXT("parent_a"), Pair.Value.Parent); Diff->SetStringField(TEXT("parent_b"), B->Parent); }
		Modified.Add(JObj(Diff));
	}

	OutAdded = Added.Num(); OutRemoved = Removed.Num(); OutModified = Modified.Num();
	return MakeBucket(Added, Removed, Modified);
}

static TSharedPtr<FJsonObject> DiffInterfaces(UBlueprint* BpA, UBlueprint* BpB, int32& OutAdded, int32& OutRemoved)
{
	auto Collect = [](UBlueprint* BP)
	{
		TSet<FString> Out;
		for (const FBPInterfaceDescription& D : BP->ImplementedInterfaces)
			if (D.Interface) Out.Add(D.Interface->GetPathName());
		return Out;
	};
	const TSet<FString> A = Collect(BpA), B = Collect(BpB);
	TArray<TSharedPtr<FJsonValue>> Added, Removed;
	for (const FString& S : B) if (!A.Contains(S)) Added.Add(JStr(S));
	for (const FString& S : A) if (!B.Contains(S)) Removed.Add(JStr(S));
	OutAdded = Added.Num(); OutRemoved = Removed.Num();
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetArrayField(TEXT("added"), Added);
	Result->SetArrayField(TEXT("removed"), Removed);
	return Result;
}

// ---------------------------------------------------------------------------------------------
// CDO
// ---------------------------------------------------------------------------------------------

static bool ExportCDOProperties(UBlueprint* BP, TMap<FString, FString>& Out, FString& OutSkipReason)
{
	UClass* Cls = BP ? BP->GeneratedClass : nullptr;
	if (!Cls) { OutSkipReason = TEXT("no generated class (compile the Blueprint first)"); return false; }
	UObject* CDO = Cls->GetDefaultObject();
	if (!CDO) { OutSkipReason = TEXT("no class default object"); return false; }

	// Anything that names this class or its CDO differs between any two Blueprints (and between
	// a transient snapshot and its source), so those paths are normalised to placeholder tokens.
	const FString SelfClassPath = Cls->GetPathName();
	const FString SelfCDOPath   = CDO->GetPathName();

	for (TFieldIterator<FProperty> It(Cls); It; ++It)
	{
		FProperty* Prop = *It;
		if (!Prop) continue;
		if (Prop->HasAnyPropertyFlags(CPF_Transient | CPF_DuplicateTransient | CPF_NonPIEDuplicateTransient
			| CPF_EditorOnly | CPF_Deprecated | CPF_InstancedReference | CPF_ContainsInstancedReference))
			continue;
		if (CastField<FMulticastDelegateProperty>(Prop) || CastField<FDelegateProperty>(Prop)) continue;

		for (int32 Idx = 0; Idx < Prop->ArrayDim; ++Idx)
		{
			FString Value;
			Prop->ExportText_InContainer(Idx, Value, CDO, nullptr, CDO, PPF_None);
			Value.ReplaceInline(*SelfCDOPath,   TEXT("<SelfCDO>"));
			Value.ReplaceInline(*SelfClassPath, TEXT("<SelfClass>"));
			const FString Name = Prop->ArrayDim > 1
				? FString::Printf(TEXT("%s[%d]"), *Prop->GetName(), Idx)
				: Prop->GetName();
			Out.Add(Name, Value);
		}
	}
	return true;
}

static TSharedPtr<FJsonObject> DiffCDO(UBlueprint* BpA, UBlueprint* BpB, int32& OutModified)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	OutModified = 0;

	TMap<FString, FString> PropsA, PropsB;
	FString SkipA, SkipB;
	const bool bA = ExportCDOProperties(BpA, PropsA, SkipA);
	const bool bB = ExportCDOProperties(BpB, PropsB, SkipB);
	if (!bA || !bB)
	{
		Result->SetStringField(TEXT("skipped"), FString::Printf(TEXT("a: %s; b: %s"),
			bA ? TEXT("ok") : *SkipA, bB ? TEXT("ok") : *SkipB));
		const TArray<TSharedPtr<FJsonValue>> Empty;
		Result->SetArrayField(TEXT("modified"), Empty);
		return Result;
	}

	TArray<TSharedPtr<FJsonValue>> Modified;
	int32 Compared = 0, OnlyA = 0, OnlyB = 0;
	TArray<FString> Names;
	PropsA.GetKeys(Names);
	Names.Sort();
	for (const FString& Name : Names)
	{
		const FString* VB = PropsB.Find(Name);
		if (!VB) { OnlyA++; continue; }
		Compared++;
		const FString& VA = PropsA[Name];
		if (VA == *VB) continue;
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("property"), Name);
		Entry->SetStringField(TEXT("a"), VA);
		Entry->SetStringField(TEXT("b"), *VB);
		Modified.Add(JObj(Entry));
	}
	for (const auto& Pair : PropsB) if (!PropsA.Contains(Pair.Key)) OnlyB++;

	OutModified = Modified.Num();
	Result->SetArrayField(TEXT("modified"), Modified);
	Result->SetNumberField(TEXT("compared_property_count"), Compared);
	Result->SetNumberField(TEXT("only_in_a_count"), OnlyA);
	Result->SetNumberField(TEXT("only_in_b_count"), OnlyB);
	return Result;
}

// ---------------------------------------------------------------------------------------------
// Diff engine
// ---------------------------------------------------------------------------------------------

TSharedPtr<FJsonObject> DiffBlueprints(
	UBlueprint* A, UBlueprint* B,
	bool bIncludeCDO,
	bool bIncludeUnchangedGraphs,
	const TMap<FGuid, FString>* LogicalIdsA,
	const TMap<FGuid, FString>* LogicalIdsB,
	const TMap<FGuid, FGuid>* GuidRemapA)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	if (!A || !B) return Result;

	int32 VarAdd = 0, VarRem = 0, VarMod = 0;
	int32 CompAdd = 0, CompRem = 0, CompMod = 0;
	int32 IfaceAdd = 0, IfaceRem = 0;
	int32 CdoMod = 0;

	const TSharedPtr<FJsonObject> VarDiff   = DiffVariables(A, B, VarAdd, VarRem, VarMod);
	const TSharedPtr<FJsonObject> CompDiff  = DiffComponents(A, B, CompAdd, CompRem, CompMod);
	const TSharedPtr<FJsonObject> IfaceDiff = DiffInterfaces(A, B, IfaceAdd, IfaceRem);

	const FGraphListDiff Dispatchers = DiffGraphLists(A, B, A->DelegateSignatureGraphs, B->DelegateSignatureGraphs, true,  bIncludeUnchangedGraphs, LogicalIdsA, LogicalIdsB, GuidRemapA);
	const FGraphListDiff Functions   = DiffGraphLists(A, B, A->FunctionGraphs,          B->FunctionGraphs,          true,  bIncludeUnchangedGraphs, LogicalIdsA, LogicalIdsB, GuidRemapA);
	const FGraphListDiff Macros      = DiffGraphLists(A, B, A->MacroGraphs,             B->MacroGraphs,             true,  bIncludeUnchangedGraphs, LogicalIdsA, LogicalIdsB, GuidRemapA);
	const FGraphListDiff Graphs      = DiffGraphLists(A, B, A->UbergraphPages,          B->UbergraphPages,          false, bIncludeUnchangedGraphs, LogicalIdsA, LogicalIdsB, GuidRemapA);

	TSharedPtr<FJsonObject> CdoDiff;
	if (bIncludeCDO) CdoDiff = DiffCDO(A, B, CdoMod);

	const FString ParentA = A->ParentClass ? A->ParentClass->GetName() : TEXT("None");
	const FString ParentB = B->ParentClass ? B->ParentClass->GetName() : TEXT("None");
	const bool bParentChanged = ParentA != ParentB;

	FGraphDiffStats NodeStats;
	NodeStats.Accumulate(Dispatchers.Stats);
	NodeStats.Accumulate(Functions.Stats);
	NodeStats.Accumulate(Macros.Stats);
	NodeStats.Accumulate(Graphs.Stats);

	const int32 Additions = VarAdd + CompAdd + IfaceAdd
		+ Dispatchers.Added + Functions.Added + Macros.Added + Graphs.Added
		+ NodeStats.NodesAdded + NodeStats.LinksAdded;
	const int32 Removals = VarRem + CompRem + IfaceRem
		+ Dispatchers.Removed + Functions.Removed + Macros.Removed + Graphs.Removed
		+ NodeStats.NodesRemoved + NodeStats.LinksRemoved;
	const int32 Modifications = VarMod + CompMod + CdoMod
		+ Dispatchers.SignatureChanged + Functions.SignatureChanged + Macros.SignatureChanged
		+ NodeStats.NodesChanged + (bParentChanged ? 1 : 0);

	TSharedPtr<FJsonObject> Summary = MakeShared<FJsonObject>();
	Summary->SetNumberField(TEXT("total_diffs"), Additions + Removals + Modifications);
	Summary->SetNumberField(TEXT("additions"), Additions);
	Summary->SetNumberField(TEXT("removals"), Removals);
	Summary->SetNumberField(TEXT("modifications"), Modifications);
	Summary->SetNumberField(TEXT("net_changes"), Additions + Removals);
	Summary->SetNumberField(TEXT("node_additions"), NodeStats.NodesAdded);
	Summary->SetNumberField(TEXT("node_removals"), NodeStats.NodesRemoved);
	Summary->SetNumberField(TEXT("node_changes"), NodeStats.NodesChanged);
	Summary->SetNumberField(TEXT("link_additions"), NodeStats.LinksAdded);
	Summary->SetNumberField(TEXT("link_removals"), NodeStats.LinksRemoved);
	Summary->SetNumberField(TEXT("implied_links_skipped"), NodeStats.ImpliedLinksSkipped);
	Summary->SetNumberField(TEXT("graphs_modified"), Dispatchers.GraphsModified + Functions.GraphsModified + Macros.GraphsModified + Graphs.GraphsModified);
	Summary->SetNumberField(TEXT("cdo_modifications"), CdoMod);
	Summary->SetBoolField(TEXT("identical"), Additions + Removals + Modifications == 0);

	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("parent_class_a"), ParentA);
	Result->SetStringField(TEXT("parent_class_b"), ParentB);
	Result->SetBoolField(TEXT("parent_class_changed"), bParentChanged);
	Result->SetObjectField(TEXT("variables"),         VarDiff);
	Result->SetObjectField(TEXT("components"),        CompDiff);
	Result->SetObjectField(TEXT("interfaces"),        IfaceDiff);
	Result->SetObjectField(TEXT("event_dispatchers"), Dispatchers.Json);
	Result->SetObjectField(TEXT("functions"),         Functions.Json);
	Result->SetObjectField(TEXT("macros"),            Macros.Json);
	Result->SetObjectField(TEXT("graphs"),            Graphs.Json);
	if (CdoDiff.IsValid()) Result->SetObjectField(TEXT("cdo"), CdoDiff);
	Result->SetObjectField(TEXT("summary"), Summary);
	Result->SetStringField(TEXT("legend"),
		TEXT("a = before / first asset, b = after / second asset. Node ids are the logical ids assigned by build_blueprint_graph (falling back to the 32-hex NodeGuid). "
		     "Links attached to an added or removed node are implied by that node and counted under implied_links_skipped."));
	return Result;
}

static void WriteJson(const TSharedPtr<FJsonObject>& Obj, FString& OutJsonString)
{
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Obj.ToSharedRef(), Writer);
}

// ---------------------------------------------------------------------------------------------
// compare_blueprints
// ---------------------------------------------------------------------------------------------

void HandleCompareBlueprintsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }

	FString PathA, PathB;
	if (!Args->TryGetStringField(TEXT("asset_path_a"), PathA))
		Args->TryGetStringField(TEXT("blueprint_path_a"), PathA);
	if (PathA.IsEmpty()) Args->TryGetStringField(TEXT("blueprint_a"), PathA);
	if (!Args->TryGetStringField(TEXT("asset_path_b"), PathB))
		Args->TryGetStringField(TEXT("blueprint_path_b"), PathB);
	if (PathB.IsEmpty()) Args->TryGetStringField(TEXT("blueprint_b"), PathB);
	if (PathA.IsEmpty()) { OutError = TEXT("Missing required parameter: blueprint_path_a (alias: asset_path_a)"); return; }
	if (PathB.IsEmpty()) { OutError = TEXT("Missing required parameter: blueprint_path_b (alias: asset_path_b)"); return; }

	bool bIncludeCDO = true;
	Args->TryGetBoolField(TEXT("include_cdo"), bIncludeCDO);
	bool bIncludeUnchanged = false;
	Args->TryGetBoolField(TEXT("include_unchanged_graphs"), bIncludeUnchanged);

	UBlueprint* BpA = LoadBlueprintForCompare(PathA);
	if (!BpA) { OutError = FString::Printf(TEXT("Could not load Blueprint A: %s"), *PathA); return; }

	UBlueprint* BpB = LoadBlueprintForCompare(PathB);
	if (!BpB) { OutError = FString::Printf(TEXT("Could not load Blueprint B: %s"), *PathB); return; }

	TSharedPtr<FJsonObject> Result = DiffBlueprints(BpA, BpB, bIncludeCDO, bIncludeUnchanged);
	Result->SetStringField(TEXT("asset_path_a"), PathA);
	Result->SetStringField(TEXT("asset_path_b"), PathB);
	WriteJson(Result, OutJsonString);
}

// ---------------------------------------------------------------------------------------------
// Snapshots
// ---------------------------------------------------------------------------------------------

namespace
{
	struct FSnapshotEntry
	{
		FString Id;
		FString BlueprintPath;
		FString Label;
		FDateTime CreatedUtc;
		TWeakObjectPtr<UBlueprint> Snapshot;   // rooted while registered
		TMap<FGuid, FString> LogicalIds;       // keyed by SOURCE NodeGuid
		TMap<FGuid, FGuid>   GuidRemap;        // snapshot NodeGuid -> source NodeGuid (only where they differ)
		int32 GraphCount = 0;
		int32 NodeCount  = 0;
	};

	static TMap<FString, FSnapshotEntry>& SnapshotRegistry()
	{
		static TMap<FString, FSnapshotEntry> Registry;
		return Registry;
	}

	/** Insertion-ordered snapshot ids per blueprint path (oldest first). */
	static TMap<FString, TArray<FString>>& SnapshotIdsByPath()
	{
		static TMap<FString, TArray<FString>> Ids;
		return Ids;
	}

	static constexpr int32 MaxSnapshotsPerPath = 3;

	static void ReleaseSnapshot(const FString& Id)
	{
		FSnapshotEntry Entry;
		if (!SnapshotRegistry().RemoveAndCopyValue(Id, Entry)) return;
		if (TArray<FString>* Ids = SnapshotIdsByPath().Find(Entry.BlueprintPath))
		{
			Ids->Remove(Id);
			if (Ids->Num() == 0) SnapshotIdsByPath().Remove(Entry.BlueprintPath);
		}
		if (UBlueprint* BP = Entry.Snapshot.Get())
		{
			if (BP->IsRooted()) BP->RemoveFromRoot();
			BP->MarkAsGarbage();
		}
	}

	static FString NormalizeBlueprintPath(const FString& Path, UBlueprint* BP)
	{
		if (Path.Equals(TEXT("@level_blueprint"), ESearchCase::IgnoreCase) || !BP) return Path;
		return BP->GetPathName();
	}

	/**
	 * Duplicate a Blueprint into the transient package for later comparison. Mirrors the
	 * approach the shell uses for its visual diff bar: the duplicate is fully transient, its
	 * generated classes are marked deprecated with bytecode stripped so nothing can instantiate
	 * them, and the object is rooted so GC keeps it until the snapshot is released.
	 */
	static UBlueprint* DuplicateForSnapshot(UBlueprint* BP, FString& OutError)
	{
		for (const UClass* C = BP->GetClass(); C; C = C->GetSuperClass())
		{
			if (C->GetFName() == TEXT("WidgetBlueprint"))
			{
				OutError = FString::Printf(TEXT("'%s' is a %s — Widget Blueprints recompile during duplication (GC mid-duplicate crashes the editor), so snapshots are not supported for them."),
					*BP->GetName(), *BP->GetClass()->GetName());
				return nullptr;
			}
		}

		UPackage* TransientPkg = GetTransientPackage();
		const FName UniqueName = MakeUniqueObjectName(TransientPkg, UBlueprint::StaticClass(),
			*FString::Printf(TEXT("UECPSnap_%s"), *BP->GetName()));
		UBlueprint* Snapshot = DuplicateObject<UBlueprint>(BP, TransientPkg, UniqueName);
		if (!Snapshot) { OutError = TEXT("DuplicateObject failed."); return nullptr; }

		auto MarkTransient = [](UObject* Obj)
		{
			if (!Obj) return;
			Obj->SetFlags(RF_Transient);
			Obj->ClearFlags(RF_Public | RF_Standalone);
		};
		MarkTransient(Snapshot);

		TArray<UObject*> SubObjects;
		GetObjectsWithOuter(Snapshot, SubObjects, true);
		for (UObject* Sub : SubObjects)
		{
			if (Sub && Sub->HasAnyFlags(RF_ClassDefaultObject)) continue;
			MarkTransient(Sub);
		}

		Snapshot->Status = BS_UpToDate;
		Snapshot->bHasBeenRegenerated = true;

		auto NeutraliseClass = [&MarkTransient](UClass* Cls)
		{
			if (!Cls) return;
			MarkTransient(Cls);
			Cls->ClassFlags |= CLASS_Deprecated;
			for (TFieldIterator<UFunction> FI(Cls, EFieldIteratorFlags::ExcludeSuper); FI; ++FI)
			{
				if (UFunction* F = *FI) F->Script.Empty();
			}
		};
		NeutraliseClass(Snapshot->GeneratedClass);
		NeutraliseClass(Snapshot->SkeletonGeneratedClass);

		Snapshot->AddToRoot();
		return Snapshot;
	}
}

void ReleaseAllSnapshots()
{
	TArray<FString> Ids;
	SnapshotRegistry().GetKeys(Ids);
	for (const FString& Id : Ids) ReleaseSnapshot(Id);
}

void HandleSnapshotBlueprintFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }

	FString Path;
	Args->TryGetStringField(TEXT("blueprint_path"), Path);
	if (Path.IsEmpty()) Args->TryGetStringField(TEXT("asset_path"), Path);
	if (Path.IsEmpty()) { OutError = TEXT("Missing required parameter: blueprint_path"); return; }
	FString Label;
	Args->TryGetStringField(TEXT("label"), Label);

	UBlueprint* BP = LoadBlueprintForCompare(Path);
	if (!BP) { OutError = FString::Printf(TEXT("Could not load Blueprint: %s"), *Path); return; }
	const FString Key = NormalizeBlueprintPath(Path, BP);

	UBlueprint* Snapshot = DuplicateForSnapshot(BP, OutError);
	if (!Snapshot) return;

	FSnapshotEntry Entry;
	Entry.Id = FString::Printf(TEXT("snap_%s_%s"), *BP->GetName(), *FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(8));
	Entry.BlueprintPath = Key;
	Entry.Label = Label;
	Entry.CreatedUtc = FDateTime::UtcNow();
	Entry.Snapshot = Snapshot;
	Entry.LogicalIds = BlueprintNodeIdentity::CollectLogicalIds(BP);

	// Node guids are a UPROPERTY and survive DuplicateObject, but PostDuplicate hooks may
	// regenerate some. Pair nodes positionally (graphs and node arrays keep their order) so the
	// diff can still key by the source guid.
	{
		TArray<UEdGraph*> SrcGraphs, SnapGraphs;
		BP->GetAllGraphs(SrcGraphs);
		Snapshot->GetAllGraphs(SnapGraphs);
		Entry.GraphCount = SrcGraphs.Num();
		if (SrcGraphs.Num() == SnapGraphs.Num())
		{
			for (int32 g = 0; g < SrcGraphs.Num(); ++g)
			{
				UEdGraph* SG = SrcGraphs[g];
				UEdGraph* DG = SnapGraphs[g];
				if (!SG || !DG || SG->Nodes.Num() != DG->Nodes.Num()) continue;
				for (int32 n = 0; n < SG->Nodes.Num(); ++n)
				{
					UEdGraphNode* SN = SG->Nodes[n];
					UEdGraphNode* DN = DG->Nodes[n];
					if (!SN || !DN) continue;
					if (SN->GetClass() != DN->GetClass()) continue;
					if (SN->NodeGuid != DN->NodeGuid) Entry.GuidRemap.Add(DN->NodeGuid, SN->NodeGuid);
				}
			}
		}
		for (UEdGraph* G : SrcGraphs)
		{
			if (!G) continue;
			for (UEdGraphNode* N : G->Nodes)
				if (IsValid(N) && !N->IsA<UEdGraphNode_Comment>()) Entry.NodeCount++;
		}
	}

	// Keep only the newest MaxSnapshotsPerPath - 1 existing snapshots so this one fits.
	{
		TArray<FString> ToRelease;
		if (const TArray<FString>* Existing = SnapshotIdsByPath().Find(Key))
		{
			const int32 Excess = Existing->Num() - (MaxSnapshotsPerPath - 1);
			for (int32 i = 0; i < Excess; ++i) ToRelease.Add((*Existing)[i]);
		}
		for (const FString& Old : ToRelease) ReleaseSnapshot(Old);
	}
	const FString SnapshotId = Entry.Id;
	SnapshotIdsByPath().FindOrAdd(Key).Add(SnapshotId);
	SnapshotRegistry().Add(SnapshotId, MoveTemp(Entry));

	const FSnapshotEntry& Stored = SnapshotRegistry()[SnapshotId];
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("snapshot_id"), SnapshotId);
	Result->SetStringField(TEXT("blueprint_path"), Key);
	if (!Label.IsEmpty()) Result->SetStringField(TEXT("label"), Label);
	Result->SetStringField(TEXT("created_at_utc"), Stored.CreatedUtc.ToIso8601());
	Result->SetNumberField(TEXT("graph_count"), Stored.GraphCount);
	Result->SetNumberField(TEXT("node_count"), Stored.NodeCount);
	TArray<TSharedPtr<FJsonValue>> IdsJson;
	for (const FString& Id : SnapshotIdsByPath()[Key]) IdsJson.Add(JStr(Id));
	Result->SetArrayField(TEXT("snapshots_for_path"), IdsJson);
	Result->SetStringField(TEXT("note"), FString::Printf(
		TEXT("In-memory snapshot (not saved). Call diff_blueprint_since_snapshot(blueprint_path) after editing to see exactly what changed. At most %d snapshots are kept per Blueprint; older ones are released automatically."),
		MaxSnapshotsPerPath));
	WriteJson(Result, OutJsonString);
}

void HandleDiffBlueprintSinceSnapshotFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }

	FString Path;
	Args->TryGetStringField(TEXT("blueprint_path"), Path);
	if (Path.IsEmpty()) Args->TryGetStringField(TEXT("asset_path"), Path);
	FString SnapshotId;
	Args->TryGetStringField(TEXT("snapshot_id"), SnapshotId);
	if (Path.IsEmpty() && SnapshotId.IsEmpty()) { OutError = TEXT("Missing required parameter: blueprint_path (or snapshot_id)"); return; }

	bool bIncludeCDO = true;
	Args->TryGetBoolField(TEXT("include_cdo"), bIncludeCDO);
	bool bIncludeUnchanged = false;
	Args->TryGetBoolField(TEXT("include_unchanged_graphs"), bIncludeUnchanged);
	bool bRelease = false;
	Args->TryGetBoolField(TEXT("release_snapshot"), bRelease);

	// Resolve the snapshot first so a missing one gives an actionable error.
	if (SnapshotId.IsEmpty())
	{
		UBlueprint* Probe = LoadBlueprintForCompare(Path);
		if (!Probe) { OutError = FString::Printf(TEXT("Could not load Blueprint: %s"), *Path); return; }
		const FString Key = NormalizeBlueprintPath(Path, Probe);
		const TArray<FString>* Ids = SnapshotIdsByPath().Find(Key);
		if (!Ids || Ids->Num() == 0)
		{
			OutError = FString::Printf(TEXT("No snapshot exists for '%s'. Call snapshot_blueprint(blueprint_path) before making changes, then diff afterwards."), *Key);
			return;
		}
		SnapshotId = Ids->Last();
	}

	FSnapshotEntry* Entry = SnapshotRegistry().Find(SnapshotId);
	if (!Entry)
	{
		OutError = FString::Printf(TEXT("Unknown snapshot_id '%s' (snapshots live in memory for the editor session only and at most %d are kept per Blueprint)."), *SnapshotId, MaxSnapshotsPerPath);
		return;
	}
	UBlueprint* Before = Entry->Snapshot.Get();
	if (!Before)
	{
		const FString Stale = SnapshotId;
		ReleaseSnapshot(Stale);
		OutError = FString::Printf(TEXT("Snapshot '%s' was garbage collected; take a new one with snapshot_blueprint."), *Stale);
		return;
	}

	const FString LivePath = Path.IsEmpty() ? Entry->BlueprintPath : Path;
	UBlueprint* After = LoadBlueprintForCompare(LivePath);
	if (!After) { OutError = FString::Printf(TEXT("Could not load Blueprint: %s"), *LivePath); return; }

	TSharedPtr<FJsonObject> Result = DiffBlueprints(
		Before, After, bIncludeCDO, bIncludeUnchanged,
		&Entry->LogicalIds, nullptr,
		Entry->GuidRemap.Num() > 0 ? &Entry->GuidRemap : nullptr);

	Result->SetStringField(TEXT("blueprint_path"), Entry->BlueprintPath);
	Result->SetStringField(TEXT("snapshot_id"), SnapshotId);
	if (!Entry->Label.IsEmpty()) Result->SetStringField(TEXT("snapshot_label"), Entry->Label);
	Result->SetStringField(TEXT("snapshot_created_at_utc"), Entry->CreatedUtc.ToIso8601());
	Result->SetNumberField(TEXT("snapshot_age_seconds"), (FDateTime::UtcNow() - Entry->CreatedUtc).GetTotalSeconds());
	Result->SetStringField(TEXT("asset_path_a"), FString::Printf(TEXT("snapshot:%s"), *SnapshotId));
	Result->SetStringField(TEXT("asset_path_b"), Entry->BlueprintPath);

	if (bRelease)
	{
		ReleaseSnapshot(SnapshotId);
		Result->SetBoolField(TEXT("snapshot_released"), true);
	}
	WriteJson(Result, OutJsonString);
}

}
