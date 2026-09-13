// Copyright 2026, BlueprintsLab, All rights reserved

#include "Describers/BpGraphDescriber.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_Composite.h"
#include "K2Node_MacroInstance.h"

FString FBpGraphDescriber::Describe(const TSet<UObject*>& SelectedNodes)
{
    SelectedNodeSet.Empty();
    VisitedNodes.Empty();

    for (UObject* Obj : SelectedNodes)
    {
        if (UEdGraphNode* Node = Cast<UEdGraphNode>(Obj))
        {
            SelectedNodeSet.Add(Node);
        }
    }

    if (SelectedNodeSet.Num() == 0) return TEXT("No valid nodes were selected.");

    TArray<UEdGraphNode*> EntryPoints = FindEntryPoints();
    if (EntryPoints.Num() == 0)
    {
        if (SelectedNodeSet.Num() > 0)
        {
            EntryPoints.Add(*SelectedNodeSet.begin());
        }
    }

    FString Description;
    for (UEdGraphNode* EntryNode : EntryPoints)
    {
        DescribeNodeRecursively(EntryNode, Description, 0);
    }

    return Description.IsEmpty() ? TEXT("Could not generate a description for the selected nodes.") : Description;
}

static FString AxReadNodeLogicalId(const UEdGraphNode* Node)
{
    if (!Node) return FString();
#if WITH_METADATA
    if (UPackage* Pkg = Node->GetPackage())
    {
        const FString& Raw = Pkg->GetMetaData().GetValue(Node, TEXT("UECP.LogicalId"));
        int32 Bar = INDEX_NONE;
        if (!Raw.IsEmpty() && Raw.FindChar(TEXT('|'), Bar))
        {
            if (Raw.Left(Bar).Equals(Node->NodeGuid.ToString(EGuidFormats::Digits), ESearchCase::IgnoreCase))
                return Raw.Mid(Bar + 1);
        }
    }
#endif
    const int32 GeidIdx = Node->NodeComment.Find(TEXT("GEID:"));
    if (GeidIdx == INDEX_NONE) return FString();
    FString Id = Node->NodeComment.Mid(GeidIdx + 5);
    int32 NewlineIdx = INDEX_NONE;
    if (Id.FindChar(TEXT('\n'), NewlineIdx)) Id = Id.Left(NewlineIdx);
    return Id.TrimStartAndEnd();
}

static FString DescribeNodeIdTag(UEdGraphNode* Node)
{
    const FString Id = AxReadNodeLogicalId(Node);
    if (!Id.IsEmpty())
    {
        return FString::Printf(TEXT("[id:%s]"), *Id);
    }
    return FString::Printf(TEXT("#%u"), Node ? PointerHash(Node) : 0u);
}

static FString DescribeNodeIdBare(UEdGraphNode* Node)
{
    const FString Id = AxReadNodeLogicalId(Node);
    if (!Id.IsEmpty())
    {
        return Id;
    }
    return FString::Printf(TEXT("#%u"), Node ? PointerHash(Node) : 0u);
}

void FBpGraphDescriber::DescribeNodeRecursively(UEdGraphNode* Node, FString& OutDescription, int32 IndentLevel)
{
    if (!Node || VisitedNodes.Contains(Node)) return;
    VisitedNodes.Add(Node);

    OutDescription.Append(FString::ChrN(IndentLevel * 2, ' '));
    OutDescription.Append(DescribeNodeIdTag(Node));
    OutDescription.Append(TEXT(" "));

    bool bIsPure = true;
    for (UEdGraphPin* Pin : Node->Pins)
    {
        if (Pin && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
        {
            bIsPure = false;
            break;
        }
    }
    OutDescription.Append(bIsPure ? TEXT("[Pure] ") : TEXT("[Impure] "));

    OutDescription.Append(Node->GetNodeTitle(ENodeTitleType::ListView).ToString());

    if (!Node->NodeComment.IsEmpty())
    {
        OutDescription.Append(FString::Printf(TEXT(" // %s"), *Node->NodeComment));
    }

    UEdGraph* InnerGraph = nullptr;
    FString InnerLabel;
    if (UK2Node_Composite* CompositeNode = Cast<UK2Node_Composite>(Node))
    {
        InnerGraph = CompositeNode->BoundGraph;
        InnerLabel = TEXT("COLLAPSED GRAPH");
    }
    else if (UK2Node_MacroInstance* MacroNode = Cast<UK2Node_MacroInstance>(Node))
    {
        InnerGraph = MacroNode->GetMacroGraph();
        InnerLabel = TEXT("MACRO");
    }

    if (InnerGraph && InnerGraph->Nodes.Num() > 0)
    {
        OutDescription.Append(FString::Printf(TEXT(" [%s — inner nodes below]"), *InnerLabel));
        OutDescription.Append(TEXT("\n"));

        TArray<UEdGraphNode*> InnerNodes;
        for (UEdGraphNode* InnerNode : InnerGraph->Nodes)
        {
            if (InnerNode && !Cast<UK2Node_Tunnel>(InnerNode))
            {
                InnerNodes.Add(InnerNode);
                SelectedNodeSet.Add(InnerNode);
            }
        }

        for (UEdGraphNode* GraphNode : InnerGraph->Nodes)
        {
            UK2Node_Tunnel* TunnelNode = Cast<UK2Node_Tunnel>(GraphNode);
            if (!TunnelNode) continue;
            for (UEdGraphPin* Pin : TunnelNode->Pins)
            {
                if (Pin && Pin->Direction == EGPD_Output && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec && Pin->LinkedTo.Num() > 0)
                {
                    UEdGraphNode* FirstInner = Pin->LinkedTo[0] ? Pin->LinkedTo[0]->GetOwningNodeUnchecked() : nullptr;
                    if (FirstInner && !VisitedNodes.Contains(FirstInner))
                    {
                        DescribeNodeRecursively(FirstInner, OutDescription, IndentLevel + 2);
                    }
                }
            }
        }

        for (UEdGraphNode* InnerNode : InnerNodes)
        {
            if (!VisitedNodes.Contains(InnerNode))
            {
                DescribeNodeRecursively(InnerNode, OutDescription, IndentLevel + 2);
            }
        }

        return;
    }

    for (UEdGraphPin* Pin : Node->Pins)
    {
        if (Pin && Pin->Direction == EGPD_Input && Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec && Pin->LinkedTo.Num() > 0)
        {
            UEdGraphPin* LinkedPin = Pin->LinkedTo[0];
            if (!LinkedPin) continue;
            UEdGraphNode* ConnectedNode = LinkedPin->GetOwningNodeUnchecked();
            if (!ConnectedNode) continue;
            const FString Src = SelectedNodeSet.Contains(ConnectedNode) ? DescribeNodeIdBare(ConnectedNode) : FString(TEXT("?"));
            OutDescription.Append(FString::Printf(TEXT(" [in:%s<-%s]"), *Pin->GetDisplayName().ToString(), *Src));
        }
        else if (Pin && Pin->Direction == EGPD_Input && Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec && Pin->LinkedTo.Num() == 0 && Pin->DefaultObject != nullptr)
        {
            OutDescription.Append(FString::Printf(TEXT(" [in:%s=%s]"), *Pin->GetDisplayName().ToString(), *Pin->DefaultObject->GetName()));
        }
        else if (Pin && Pin->Direction == EGPD_Input && Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec && !Pin->DefaultValue.IsEmpty() && Pin->DefaultValue != Pin->GetSchema()->GetPinDisplayName(Pin).ToString())
        {
            OutDescription.Append(FString::Printf(TEXT(" [in:%s=%s]"), *Pin->GetDisplayName().ToString(), *Pin->DefaultValue));
        }
    }

    for (UEdGraphPin* Pin : Node->Pins)
    {
        if (Pin && Pin->Direction == EGPD_Output && Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec && Pin->LinkedTo.Num() > 0)
        {
            TArray<FString> ConnectedTargets;
            for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
            {
                if (!LinkedPin) continue;
                UEdGraphNode* ConnectedNode = LinkedPin->GetOwningNodeUnchecked();
                if (!ConnectedNode) continue;
                ConnectedTargets.Add(SelectedNodeSet.Contains(ConnectedNode)
                    ? FString::Printf(TEXT("%s.%s"), *DescribeNodeIdBare(ConnectedNode), *LinkedPin->GetDisplayName().ToString())
                    : FString(TEXT("?")));
            }
            if (ConnectedTargets.Num() > 0)
            {
                OutDescription.Append(FString::Printf(TEXT(" [out:%s->%s]"), *Pin->GetDisplayName().ToString(), *FString::Join(ConnectedTargets, TEXT(","))));
            }
        }
    }

    OutDescription.Append(TEXT("\n"));

    for (UEdGraphPin* Pin : Node->Pins)
    {
        if (Pin && Pin->Direction == EGPD_Output && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec && Pin->LinkedTo.Num() > 0)
        {
            UEdGraphPin* NextPin = Pin->LinkedTo[0];
            if (!NextPin) continue;
            UEdGraphNode* NextNode = NextPin->GetOwningNodeUnchecked();
            if (!NextNode) continue;
            FString PinLabel = Pin->GetDisplayName().ToString();
            if (PinLabel.IsEmpty()) PinLabel = Pin->PinName.ToString();

            OutDescription.Append(FString::ChrN((IndentLevel * 2) + 1, ' '));

            if (SelectedNodeSet.Contains(NextNode))
            {
                if (VisitedNodes.Contains(NextNode))
                {
                    OutDescription.Append(FString::Printf(
                        TEXT("-> (%s) -> %s [seen]\n"),
                        *PinLabel, *DescribeNodeIdBare(NextNode)));
                }
                else
                {
                    OutDescription.Append(FString::Printf(TEXT("-> (%s) ->\n"), *PinLabel));
                    DescribeNodeRecursively(NextNode, OutDescription, IndentLevel + 1);
                }
            }
            else
            {
                OutDescription.Append(FString::Printf(
                    TEXT("-> (%s) -> %s [outside]\n"),
                    *PinLabel, *NextNode->GetNodeTitle(ENodeTitleType::ListView).ToString()));
            }
        }
    }
}

TArray<UEdGraphNode*> FBpGraphDescriber::FindEntryPoints()
{
    TArray<UEdGraphNode*> EntryPoints;
    for (UEdGraphNode* Node : SelectedNodeSet)
    {
        bool bIsEntryPoint = true;
        for (UEdGraphPin* Pin : Node->Pins)
        {
            if (Pin && Pin->Direction == EGPD_Input && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
            {
                if (Pin->LinkedTo.Num() > 0 && Pin->LinkedTo[0] && Pin->LinkedTo[0]->GetOwningNodeUnchecked() && SelectedNodeSet.Contains(Pin->LinkedTo[0]->GetOwningNodeUnchecked()))
                {
                    bIsEntryPoint = false;
                    break;
                }
            }
        }
        if (bIsEntryPoint)
        {
            EntryPoints.Add(Node);
        }
    }
    return EntryPoints;
}
