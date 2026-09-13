// Copyright 2026, BlueprintsLab, All rights reserved.

#pragma once

#include "BpLayoutTypes.h"

class UEdGraph;
class UEdGraphNode;
class UEdGraphPin;

struct FBpLayoutConversion
{
	FBpLayoutGraph Graph;
	TArray<UEdGraphNode*> NodeMap;
	TArray<UEdGraphPin*> PinMap;
};

class FBpLayoutConverter
{
public:
	static FBpLayoutConversion ConvertGraph(UEdGraph* InGraph, const FBpLayoutConfig& Config);

	static FBpLayoutConversion ConvertGraph(UEdGraph* InGraph, const FBpLayoutConfig& Config,
	                                        const TSet<UEdGraphNode*>& SelectedNodes);

	static FVector2D EstimateNodeSize(const UEdGraphNode* Node);
	static FVector2D EstimatePinOffset(const UEdGraphPin* Pin, const FVector2D& NodeSize,
	                                    int32 PinIndex, bool bIsReroute);
};
