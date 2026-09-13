// Copyright 2026, BlueprintsLab, All rights reserved

#pragma once

#include "CoreMinimal.h"

class UEdGraphNode;

class UECPCORE_API FBpGraphDescriber
{
public:
    FString Describe(const TSet<UObject*>& SelectedNodes);

private:
    void DescribeNodeRecursively(UEdGraphNode* Node, FString& OutDescription, int32 IndentLevel);
    TArray<UEdGraphNode*> FindEntryPoints();

    TSet<UEdGraphNode*> SelectedNodeSet;
    TSet<UEdGraphNode*> VisitedNodes;
};
