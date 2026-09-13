// Copyright 2026, BlueprintsLab, All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Math/Vector2D.h"

struct FBpLayoutNode
{
	int32 Id = -1;
	FString Name;
	FVector2D Position = FVector2D::ZeroVector;
	FVector2D Size = FVector2D(200.0, 60.0);
	bool bIsReroute = false;
	bool bHasExecPins = false;

	TSet<int32> InsideCommentIds;

	TSet<int32> LeftEdgeOutsideCommentIds;
};

struct FBpLayoutPin
{
	int32 Id = -1;
	int32 OwnerNodeId = -1;
	FVector2D Offset = FVector2D::ZeroVector;
	bool bIsInput = false;
	bool bIsExec = false;
};

struct FBpLayoutEdge
{
	int32 Id = -1;
	int32 SourcePinId = -1;
	int32 TargetPinId = -1;
	int32 SourceNodeId = -1;
	int32 TargetNodeId = -1;
	bool bIsExec = false;
	bool bReversed = false;
	bool bIsBroken = false;
	double PinDiffY = 0.0;
};

struct FBpLayoutComment
{
	int32 Id = -1;
	FString Name;
	FVector2D Position = FVector2D::ZeroVector;
	FVector2D Size = FVector2D(400.0, 200.0);
	TArray<int32> ContainedNodeIds;
};

struct FBpLayoutConfig
{
	FVector2D NodeSpacing = FVector2D(80.0, 50.0);
	double ExecSpacingX = 80.0;
	FVector2D CommentPadding = FVector2D(40.0, 30.0);
	double ExecBranchGap = 40.0;
	double InterChainGap = 60.0;
	double CommentSpacingX = 40.0;
	double CommentSpacingY = 30.0;
	double CommentHeaderH = 30.0;
	bool bSelectedOnly = false;
};

struct FBpLayoutGraph
{
	FBpLayoutConfig Config;
	TArray<FBpLayoutNode> Nodes;
	TArray<FBpLayoutPin> Pins;
	TArray<FBpLayoutEdge> Edges;
	TArray<FBpLayoutComment> Comments;
	TSet<int32> SelectedNodeIds;
};

struct FBpLayoutMove
{
	int32 NodeId = -1;
	FVector2D NewPosition = FVector2D::ZeroVector;
};

struct FBpLayoutResult
{
	TArray<FBpLayoutMove> NodeMoves;
	TArray<FBpLayoutMove> CommentMoves;
};
