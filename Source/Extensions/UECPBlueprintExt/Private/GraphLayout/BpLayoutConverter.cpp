// Copyright 2026, BlueprintsLab, All rights reserved.

#include "BpLayoutConverter.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphNode_Comment.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_Knot.h"
#include "K2Node_CallFunction.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "K2Node_Event.h"
#include "K2Node_CustomEvent.h"

static FBpLayoutConversion BuildConversion(UEdGraph* InGraph, const FBpLayoutConfig& Config,
                                           const TSet<UEdGraphNode*>* SelectedNodes)
{
	FBpLayoutConversion Out;
	Out.Graph.Config = Config;

	if (!InGraph) return Out;

	TMap<UEdGraphNode*, int32> UENodeToId;
	TMap<UEdGraphPin*, int32> UEPinToId;

	int32 NextNodeId = 0;
	int32 NextPinId = 0;
	int32 NextEdgeId = 0;
	int32 NextCommentId = 0;

	for (UEdGraphNode* UENode : InGraph->Nodes)
	{
		if (!IsValid(UENode)) continue;

		if (UEdGraphNode_Comment* Comment = Cast<UEdGraphNode_Comment>(UENode))
		{
			FBpLayoutComment LayoutComment;
			LayoutComment.Id = NextCommentId++;
			LayoutComment.Name = Comment->NodeComment;
			int32 W = Comment->NodeWidth > 0 ? Comment->NodeWidth : 400;
			int32 H = Comment->NodeHeight > 0 ? Comment->NodeHeight : 200;
			LayoutComment.Position = FVector2D(Comment->NodePosX, Comment->NodePosY);
			LayoutComment.Size = FVector2D(W, H);
			Out.Graph.Comments.Add(MoveTemp(LayoutComment));
			continue;
		}

		const int32 NodeId = NextNodeId++;
		UENodeToId.Add(UENode, NodeId);

		while (Out.NodeMap.Num() <= NodeId)
			Out.NodeMap.Add(nullptr);
		Out.NodeMap[NodeId] = UENode;

		const bool bReroute = UENode->IsA<UK2Node_Knot>();
		const FVector2D NodeSize = bReroute
			? FVector2D(20.0, 20.0)
			: FBpLayoutConverter::EstimateNodeSize(UENode);

		FBpLayoutNode LayoutNode;
		LayoutNode.Id = NodeId;
		LayoutNode.Name = UENode->GetNodeTitle(ENodeTitleType::ListView).ToString();
		LayoutNode.Position = FVector2D(UENode->NodePosX, UENode->NodePosY);
		LayoutNode.Size = NodeSize;
		LayoutNode.bIsReroute = bReroute;

		int32 InputIdx = 0, OutputIdx = 0;
		for (UEdGraphPin* Pin : UENode->Pins)
		{
			if (!Pin || Pin->bHidden || Pin->bOrphanedPin) continue;

			const int32 PinId = NextPinId++;
			UEPinToId.Add(Pin, PinId);
			while (Out.PinMap.Num() <= PinId)
				Out.PinMap.Add(nullptr);
			Out.PinMap[PinId] = Pin;

			const bool bInput = (Pin->Direction == EGPD_Input);
			const bool bExec = (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec);
			const int32 PinIndex = bInput ? InputIdx++ : OutputIdx++;

			if (bExec) LayoutNode.bHasExecPins = true;

			FBpLayoutPin LayoutPin;
			LayoutPin.Id = PinId;
			LayoutPin.OwnerNodeId = NodeId;
			LayoutPin.bIsInput = bInput;
			LayoutPin.bIsExec = bExec;
			LayoutPin.Offset = FBpLayoutConverter::EstimatePinOffset(Pin, NodeSize, PinIndex, bReroute);
			Out.Graph.Pins.Add(MoveTemp(LayoutPin));
		}

		Out.Graph.Nodes.Add(MoveTemp(LayoutNode));
	}

	TMap<int32, FVector2D> PinOffsetMap;
	for (const FBpLayoutPin& LP : Out.Graph.Pins)
		PinOffsetMap.Add(LP.Id, LP.Offset);

	for (UEdGraphNode* UENode : InGraph->Nodes)
	{
		if (!IsValid(UENode) || UENode->IsA<UEdGraphNode_Comment>()) continue;
		if (!UENodeToId.Contains(UENode)) continue;

		const int32 SrcNodeId = UENodeToId[UENode];

		for (UEdGraphPin* Pin : UENode->Pins)
		{
			if (!Pin || Pin->bHidden || Pin->bOrphanedPin) continue;
			if (Pin->Direction != EGPD_Output) continue;
			if (!UEPinToId.Contains(Pin)) continue;

			const int32 SrcPinId = UEPinToId[Pin];
			const bool bExec = (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec);

			for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
			{
				if (!LinkedPin || !UEPinToId.Contains(LinkedPin)) continue;
				UEdGraphNode* TargetUENode = LinkedPin->GetOwningNodeUnchecked();
				if (!TargetUENode || !UENodeToId.Contains(TargetUENode)) continue;

				FBpLayoutEdge Edge;
				Edge.Id = NextEdgeId++;
				Edge.SourcePinId = SrcPinId;
				Edge.TargetPinId = UEPinToId[LinkedPin];
				Edge.SourceNodeId = SrcNodeId;
				Edge.TargetNodeId = UENodeToId[TargetUENode];
				Edge.bIsExec = bExec;

				{
					const FVector2D* SrcOff = PinOffsetMap.Find(SrcPinId);
					const FVector2D* TgtOff = PinOffsetMap.Find(Edge.TargetPinId);
					if (SrcOff && TgtOff)
						Edge.PinDiffY = TgtOff->Y - SrcOff->Y;
				}

				Out.Graph.Edges.Add(MoveTemp(Edge));
			}
		}
	}

	for (FBpLayoutComment& Cmt : Out.Graph.Comments)
	{
		const double CLeft = Cmt.Position.X;
		const double CTop = Cmt.Position.Y;
		const double CRight = CLeft + Cmt.Size.X;
		const double CBottom = CTop + Cmt.Size.Y;

		for (const FBpLayoutNode& Node : Out.Graph.Nodes)
		{
			double NCenterX = Node.Position.X + Node.Size.X * 0.5;
			double NCenterY = Node.Position.Y + Node.Size.Y * 0.5;
			if (NCenterX >= CLeft && NCenterX <= CRight && NCenterY >= CTop && NCenterY <= CBottom)
			{
				Cmt.ContainedNodeIds.Add(Node.Id);
				Out.Graph.Nodes[Node.Id].InsideCommentIds.Add(Cmt.Id);
			}
		}
	}

	if (SelectedNodes)
	{
		for (const auto& [UENode, NodeId] : UENodeToId)
		{
			if (SelectedNodes->Contains(UENode))
				Out.Graph.SelectedNodeIds.Add(NodeId);
		}
	}
	else
	{
		for (const FBpLayoutNode& Node : Out.Graph.Nodes)
			Out.Graph.SelectedNodeIds.Add(Node.Id);
	}

	return Out;
}

FBpLayoutConversion FBpLayoutConverter::ConvertGraph(UEdGraph* InGraph, const FBpLayoutConfig& Config)
{
	return BuildConversion(InGraph, Config, nullptr);
}

FBpLayoutConversion FBpLayoutConverter::ConvertGraph(UEdGraph* InGraph, const FBpLayoutConfig& Config,
                                                     const TSet<UEdGraphNode*>& SelectedNodes)
{
	return BuildConversion(InGraph, Config, &SelectedNodes);
}

FVector2D FBpLayoutConverter::EstimateNodeSize(const UEdGraphNode* Node)
{
	if (!Node) return FVector2D(200.0, 60.0);

	int32 InputCount = 0, OutputCount = 0;
	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (!Pin || Pin->bHidden || Pin->bOrphanedPin) continue;
		if (Pin->Direction == EGPD_Input) InputCount++;
		else OutputCount++;
	}
	int32 MaxPins = FMath::Max(InputCount, OutputCount);
	constexpr double WPad = 20.0;
	constexpr double HPad = 10.0;

	if (Node->IsA<UK2Node_VariableGet>())
	{
		FString Name = Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString();
		double W = FMath::Clamp(40.0 + Name.Len() * 8.0, 100.0, 280.0) + WPad;
		return FVector2D(W, 60.0 + HPad);
	}

	if (Node->IsA<UK2Node_VariableSet>())
	{
		FString Name = Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString();
		double W = FMath::Clamp(60.0 + Name.Len() * 8.0, 140.0, 320.0) + WPad;
		return FVector2D(W, 80.0 + HPad);
	}

	if (Node->IsA<UK2Node_IfThenElse>())
		return FVector2D(170.0 + WPad, 80.0 + HPad);

	if (Node->IsA<UK2Node_CustomEvent>() || Node->IsA<UK2Node_Event>())
	{
		FString Name = Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString();
		double W = FMath::Clamp(80.0 + Name.Len() * 7.0, 180.0, 420.0) + WPad;
		double H = FMath::Max(80.0, 48.0 + OutputCount * 24.0) + HPad;
		return FVector2D(W, H);
	}

	if (const UK2Node_CallFunction* Func = Cast<UK2Node_CallFunction>(Node))
	{
		const FName FN = Func->GetFunctionName();

		if (FN == FName("Add_DoubleDouble")      || FN == FName("Subtract_DoubleDouble")  ||
		    FN == FName("Multiply_DoubleDouble")  || FN == FName("Divide_DoubleDouble")    ||
		    FN == FName("Add_FloatFloat")         || FN == FName("Subtract_FloatFloat")    ||
		    FN == FName("Multiply_FloatFloat")    || FN == FName("Divide_FloatFloat")      ||
		    FN == FName("Add_IntInt")             || FN == FName("Subtract_IntInt")        ||
		    FN == FName("Multiply_IntInt")        || FN == FName("Divide_IntInt")          ||
		    FN == FName("Percent_IntInt")         || FN == FName("Percent_FloatFloat"))
			return FVector2D(130.0 + WPad, 80.0 + HPad);

		if (FN == FName("LessEqual_DoubleDouble")    || FN == FName("GreaterEqual_DoubleDouble") ||
		    FN == FName("Less_DoubleDouble")          || FN == FName("Greater_DoubleDouble")      ||
		    FN == FName("EqualEqual_DoubleDouble")    || FN == FName("NotEqual_DoubleDouble")     ||
		    FN == FName("LessEqual_FloatFloat")       || FN == FName("GreaterEqual_FloatFloat")   ||
		    FN == FName("Less_FloatFloat")            || FN == FName("Greater_FloatFloat")        ||
		    FN == FName("LessEqual_IntInt")           || FN == FName("GreaterEqual_IntInt")       ||
		    FN == FName("Less_IntInt")               || FN == FName("Greater_IntInt")            ||
		    FN == FName("EqualEqual_IntInt")          || FN == FName("NotEqual_IntInt"))
			return FVector2D(130.0 + WPad, 80.0 + HPad);

		if (FN == FName("FClamp") || FN == FName("Clamp_Int") || FN == FName("ClampAngle"))
			return FVector2D(200.0 + WPad, 105.0 + HPad);

		if (FN == FName("FMax") || FN == FName("FMin") ||
		    FN == FName("Max")  || FN == FName("Min"))
			return FVector2D(160.0 + WPad, 80.0 + HPad);

		if (FN == FName("FTrunc") || FN == FName("FRound") ||
		    FN == FName("FCeil")  || FN == FName("FFloor") ||
		    FN == FName("Abs")    || FN == FName("Abs_Int"))
			return FVector2D(170.0 + WPad, 65.0 + HPad);

		if (FN == FName("Conv_FloatToInt")    || FN == FName("Conv_IntToFloat")    ||
		    FN == FName("Conv_DoubleToInt")   || FN == FName("Conv_IntToDouble")   ||
		    FN == FName("Conv_FloatToDouble") || FN == FName("Conv_DoubleToFloat"))
			return FVector2D(160.0 + WPad, 65.0 + HPad);

		if (FN == FName("PrintString"))
			return FVector2D(250.0 + WPad, 150.0 + HPad);
		if (FN == FName("Append"))
			return FVector2D(220.0 + WPad, 90.0 + HPad);
		if (FN == FName("BuildString_Integer") || FN == FName("BuildString_Float") ||
		    FN == FName("BuildString_Bool")    || FN == FName("BuildString_Object"))
			return FVector2D(270.0 + WPad, 140.0 + HPad);
		if (FN == FName("Conv_IntToString")   || FN == FName("Conv_FloatToString") ||
		    FN == FName("Conv_BoolToString")  || FN == FName("Conv_DoubleToString"))
			return FVector2D(180.0 + WPad, 65.0 + HPad);
	}

	FString Title = Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString();
	double Width  = FMath::Clamp(150.0 + Title.Len() * 3.5, 180.0, 520.0);
	double Height = FMath::Max(60.0, 48.0 + MaxPins * 25.0 + (MaxPins > 4 ? 20.0 : 0.0));
	return FVector2D(Width, Height);
}

FVector2D FBpLayoutConverter::EstimatePinOffset(const UEdGraphPin* Pin, const FVector2D& NodeSize,
                                                 int32 PinIndex, bool bIsReroute)
{
	if (bIsReroute)
		return FVector2D(10.0, 10.0);

	double X = (Pin->Direction == EGPD_Input) ? 12.0 : NodeSize.X - 12.0;
	double Y = 30.0 + PinIndex * 26.0;
	return FVector2D(X, Y);
}
