// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"

class UBehaviorTree;
class UBehaviorTreeGraphNode;
class UEdGraphNode;

class UECPCORE_API FBtGraphDescriber
{
public:

    FString Describe(UBehaviorTree* BehaviorTree);

    FString DescribeSelection(const TSet<UObject*>& SelectedNodes);

private:

    void DescribeNodeRecursively(UBehaviorTreeGraphNode* Node, TStringBuilder<4096>& Report, int32 IndentLevel, TSet<const UEdGraphNode*>& VisitedNodes);
};
