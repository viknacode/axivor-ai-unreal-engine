// Copyright 2026, BlueprintsLab, All rights reserved.

#pragma once

#include "BpLayoutTypes.h"
#include "EdGraph/EdGraphNode.h"

class FBpLayoutApplier
{
public:
	static int32 Apply(const TArray<UEdGraphNode*>& NodeMap, const FBpLayoutResult& Result)
	{
		int32 MovedCount = 0;
		for (const FBpLayoutMove& Move : Result.NodeMoves)
		{
			if (Move.NodeId >= 0 && Move.NodeId < NodeMap.Num() && NodeMap[Move.NodeId] != nullptr)
			{
				UEdGraphNode* Node = NodeMap[Move.NodeId];
				const int32 NewX = FMath::RoundToInt32(Move.NewPosition.X);
				const int32 NewY = FMath::RoundToInt32(Move.NewPosition.Y);
				if (Node->NodePosX != NewX || Node->NodePosY != NewY)
				{
					Node->Modify();
					Node->NodePosX = NewX;
					Node->NodePosY = NewY;
					MovedCount++;
				}
			}
		}
		return MovedCount;
	}
};
