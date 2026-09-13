// Copyright 2026, BlueprintsLab, All rights reserved

#include "Describers/BtGraphDescriber.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BTNode.h"
#include "BehaviorTree/BTCompositeNode.h"
#include "BehaviorTree/BlackboardData.h"
#include "BehaviorTreeGraph.h"
#include "BehaviorTreeGraphNode.h"
#include "BehaviorTreeGraphNode_Root.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"

void FBtGraphDescriber::DescribeNodeRecursively(UBehaviorTreeGraphNode* Node, TStringBuilder<4096>& Report, int32 IndentLevel, TSet<const UEdGraphNode*>& VisitedNodes)
{
    if (!Node || VisitedNodes.Contains(Node))
    {
        return;
    }
    VisitedNodes.Add(Node);

    Report.Append(FString::ChrN(IndentLevel * 2, ' '));
    Report.Appendf(TEXT("- %s"), *Node->GetNodeTitle(ENodeTitleType::ListView).ToString());

    auto AppendInline = [&](const TArray<UBehaviorTreeGraphNode*>& Children, const TCHAR* Label)
    {
        if (Children.Num() == 0) return;
        TArray<FString> Names;
        Names.Reserve(Children.Num());
        for (const UBehaviorTreeGraphNode* Child : Children)
        {
            if (Child) Names.Add(Child->GetNodeTitle(ENodeTitleType::ListView).ToString());
        }
        if (Names.Num() > 0) Report.Appendf(TEXT(" [%s: %s]"), Label, *FString::Join(Names, TEXT(", ")));
    };
    AppendInline(Node->Decorators, TEXT("Deco"));
    AppendInline(Node->Services, TEXT("Svc"));
    Report.Append(TEXT("\n"));

    for (const UEdGraphPin* Pin : Node->Pins)
    {
        if (Pin && Pin->Direction == EGPD_Output && Pin->LinkedTo.Num() > 0)
        {
            for (const UEdGraphPin* LinkedPin : Pin->LinkedTo)
            {
                if (UBehaviorTreeGraphNode* NextNode = Cast<UBehaviorTreeGraphNode>(LinkedPin->GetOwningNode()))
                {
                    DescribeNodeRecursively(NextNode, Report, IndentLevel + 1, VisitedNodes);
                }
            }
        }
    }
}

FString FBtGraphDescriber::Describe(UBehaviorTree* BehaviorTree)
{
    if (!BehaviorTree)
    {
        return TEXT("Invalid Behavior Tree asset provided.");
    }

    TStringBuilder<4096> Report;
    Report.Appendf(TEXT("--- BEHAVIOR TREE SUMMARY: %s ---\n\n"), *BehaviorTree->GetName());

    if (BehaviorTree->BlackboardAsset)
    {
        Report.Appendf(TEXT("Blackboard: %s\n\n"), *BehaviorTree->BlackboardAsset->GetName());
    }
    else
    {
        Report.Append(TEXT("Blackboard: None\n\n"));
    }

    UBehaviorTreeGraph* BTGraph = Cast<UBehaviorTreeGraph>(BehaviorTree->BTGraph);
    if (!BTGraph)
    {
        return FString(Report) + TEXT("Could not find a valid Behavior Tree graph associated with this asset.");
    }

    UBehaviorTreeGraphNode_Root* RootNode = nullptr;
    for (UEdGraphNode* Node : BTGraph->Nodes)
    {
        if (UBehaviorTreeGraphNode_Root* FoundRoot = Cast<UBehaviorTreeGraphNode_Root>(Node))
        {
            RootNode = FoundRoot;
            break;
        }
    }

    if (!RootNode)
    {
        return FString(Report) + TEXT("Could not find the Root node in the graph.");
    }

    Report.Append(TEXT("--- Tree Structure ---\n"));
    TSet<const UEdGraphNode*> VisitedNodes;

    for (const UEdGraphPin* Pin : RootNode->Pins)
    {
        if (Pin && Pin->Direction == EGPD_Output && Pin->LinkedTo.Num() > 0)
        {
             if (UBehaviorTreeGraphNode* FirstNode = Cast<UBehaviorTreeGraphNode>(Pin->LinkedTo[0]->GetOwningNode()))
             {
                 DescribeNodeRecursively(FirstNode, Report, 0, VisitedNodes);
             }
        }
    }

    return FString(Report);
}

FString FBtGraphDescriber::DescribeSelection(const TSet<UObject*>& SelectedNodes)
{
    if (SelectedNodes.Num() == 0)
    {
        return TEXT("No nodes are selected in the Behavior Tree editor.");
    }

    TStringBuilder<4096> Report;
    Report.Append(TEXT("--- SELECTED BEHAVIOR TREE NODES ---\n\n"));
    TSet<const UEdGraphNode*> VisitedNodes;

    for (UObject* Obj : SelectedNodes)
    {
        if (UBehaviorTreeGraphNode* Node = Cast<UBehaviorTreeGraphNode>(Obj))
        {
            DescribeNodeRecursively(Node, Report, 0, VisitedNodes);
            Report.Append(TEXT("\n"));
        }
    }

    return FString(Report);
}
