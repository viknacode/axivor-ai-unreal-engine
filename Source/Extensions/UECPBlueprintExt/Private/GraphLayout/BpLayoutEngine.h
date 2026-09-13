// Copyright 2026, BlueprintsLab, All rights reserved.

#pragma once

#include "BpLayoutTypes.h"
#include "MCPToolsLog.h"

class FBpLayoutEngine
{
public:
	static FBpLayoutResult ArrangeGraph(FBpLayoutGraph& Graph)
	{
		FBpLayoutResult Result;
		if (Graph.Nodes.Num() == 0) return Result;

		UE_LOG(LogMCPTool, Log, TEXT("[Arranger] ===== START: %d nodes, %d edges, %d comments ====="),
			Graph.Nodes.Num(), Graph.Edges.Num(), Graph.Comments.Num());

		TMap<int32, TArray<int32>> OutEdges, InEdges;
		for (int32 i = 0; i < Graph.Edges.Num(); i++)
		{
			const FBpLayoutEdge& E = Graph.Edges[i];
			OutEdges.FindOrAdd(E.SourceNodeId).Add(i);
			InEdges.FindOrAdd(E.TargetNodeId).Add(i);
		}

		{
			TSet<int32> Visited, OnStack;
			for (const FBpLayoutNode& N : Graph.Nodes)
				if (!Visited.Contains(N.Id))
					DFSBreakCycles(Graph, OutEdges, N.Id, Visited, OnStack);
		}

		TArray<TArray<int32>> Components;
		FindComponents(Graph, Components);

		FLayoutState State;
		State.ChainBaseY = 0.0;

		for (const TArray<int32>& Comp : Components)
		{
			if (Comp.Num() == 0) continue;

			TSet<int32> CompSet(Comp);

			TSet<int32> HasIncomingExec;
			for (const FBpLayoutEdge& E : Graph.Edges)
			{
				if (E.bIsBroken || !E.bIsExec) continue;
				if (CompSet.Contains(E.SourceNodeId) && CompSet.Contains(E.TargetNodeId))
					HasIncomingExec.Add(E.TargetNodeId);
			}

			TArray<int32> ExecRoots;
			for (int32 NId : Comp)
				if (Graph.Nodes[NId].bHasExecPins && !HasIncomingExec.Contains(NId))
					ExecRoots.Add(NId);

			ExecRoots.Sort([&Graph](int32 A, int32 B)
			{ return Graph.Nodes[A].Position.Y < Graph.Nodes[B].Position.Y; });

			UE_LOG(LogMCPTool, Log, TEXT("[Arranger] Component %d/%d: %d nodes, %d exec roots, %d broken edges"),
				Components.IndexOfByKey(Comp) + 1, Components.Num(), Comp.Num(), ExecRoots.Num(),
				Graph.Edges.FilterByPredicate([](const FBpLayoutEdge& E) { return E.bIsBroken; }).Num());
			for (int32 RId : ExecRoots)
				UE_LOG(LogMCPTool, Log, TEXT("[Arranger]   Root: '%s' (id=%d)"), *Graph.Nodes[RId].Name, RId);

			for (int32 RootId : ExecRoots)
			{
				if (State.Positioned.Contains(RootId)) continue;

				State.MaxBottomY = State.ChainBaseY;
				WalkExecChainDFS(Graph, OutEdges, InEdges, CompSet,
				                  RootId, 0.0, State.ChainBaseY, State);

				State.ChainBaseY = State.MaxBottomY + Graph.Config.InterChainGap;
			}

			for (int32 NId : Comp)
			{
				if (State.Positioned.Contains(NId)) continue;

				double X = 0.0, Y = State.ChainBaseY;

				if (const TArray<int32>* Out = OutEdges.Find(NId))
				{
					for (int32 EIdx : *Out)
					{
						const FBpLayoutEdge& E = Graph.Edges[EIdx];
						if (!E.bIsBroken && State.Positioned.Contains(E.TargetNodeId))
						{
							X = Graph.Nodes[E.TargetNodeId].Position.X - 300.0;
							Y = Graph.Nodes[E.TargetNodeId].Position.Y;
							break;
						}
					}
				}

				Y = ResolveOverlapY(Graph, NId, X, Y, State.Positioned);
				Graph.Nodes[NId].Position.X = X;
				Graph.Nodes[NId].Position.Y = Y;
				State.Positioned.Add(NId);
				State.MaxBottomY = FMath::Max(State.MaxBottomY, Y + Graph.Nodes[NId].Size.Y);
				State.ChainBaseY = State.MaxBottomY + Graph.Config.InterChainGap;
			}
		}

		for (FBpLayoutEdge& E : Graph.Edges)
			E.bIsBroken = false;

		FitCommentBoxes(Graph);

		UE_LOG(LogMCPTool, Log, TEXT("[Arranger] ===== DONE: %d nodes positioned, %d unpositioned ====="),
			State.Positioned.Num(), Graph.Nodes.Num() - State.Positioned.Num());

		for (const FBpLayoutNode& Node : Graph.Nodes)
		{
			if (!Graph.SelectedNodeIds.Contains(Node.Id)) continue;
			FBpLayoutMove Move;
			Move.NodeId = Node.Id;
			Move.NewPosition = Node.Position;
			Result.NodeMoves.Add(MoveTemp(Move));
		}
		for (const FBpLayoutComment& Cmt : Graph.Comments)
		{
			FBpLayoutMove Move;
			Move.NodeId = Cmt.Id;
			Move.NewPosition = Cmt.Position;
			Result.CommentMoves.Add(MoveTemp(Move));
		}

		return Result;
	}

private:

	struct FLayoutState
	{
		TSet<int32> Positioned;
		double ChainBaseY = 0.0;
		double MaxBottomY = 0.0;
	};

	static void WalkExecChainDFS(FBpLayoutGraph& Graph,
	                              const TMap<int32, TArray<int32>>& OutEdges,
	                              const TMap<int32, TArray<int32>>& InEdges,
	                              const TSet<int32>& CompSet,
	                              int32 NodeId, double X, double Y,
	                              FLayoutState& State)
	{
		if (State.Positioned.Contains(NodeId)) return;
		State.Positioned.Add(NodeId);

		const FBpLayoutConfig& C = Graph.Config;
		FBpLayoutNode& Node = Graph.Nodes[NodeId];

		Node.Position.X = X;
		Node.Position.Y = Y;
		State.MaxBottomY = FMath::Max(State.MaxBottomY, Y + Node.Size.Y);

		UE_LOG(LogMCPTool, Log, TEXT("[Arranger] Exec: '%s' (id=%d) → (%d, %d) size=(%d,%d)"),
			*Node.Name, NodeId, (int32)X, (int32)Y, (int32)Node.Size.X, (int32)Node.Size.Y);

		double PureStartY = Y + Node.Size.Y + 10.0;
		PlacePureDeps(Graph, InEdges, CompSet, NodeId, X, PureStartY, State);

		TArray<TPair<int32, double>> Successors;
		if (const TArray<int32>* Out = OutEdges.Find(NodeId))
		{
			for (int32 EIdx : *Out)
			{
				const FBpLayoutEdge& E = Graph.Edges[EIdx];
				if (E.bIsBroken || !E.bIsExec) continue;
				if (!CompSet.Contains(E.TargetNodeId)) continue;
				if (State.Positioned.Contains(E.TargetNodeId)) continue;

				double SrcPinY = GetPinOffset(Graph, E.SourcePinId).Y;
				Successors.Add({E.TargetNodeId, SrcPinY});
			}
		}
		Successors.Sort([](const TPair<int32, double>& A, const TPair<int32, double>& B)
		{ return A.Value < B.Value; });

		double NextX = X + Node.Size.X + C.ExecSpacingX;

		for (int32 i = 0; i < Successors.Num(); i++)
		{
			double BranchY;
			if (i == 0)
			{
				BranchY = Y;
			}
			else
			{
				BranchY = State.MaxBottomY + C.ExecBranchGap;
			}

			WalkExecChainDFS(Graph, OutEdges, InEdges, CompSet,
			                  Successors[i].Key, NextX, BranchY, State);
		}
	}

	static void PlacePureDeps(FBpLayoutGraph& Graph,
	                           const TMap<int32, TArray<int32>>& InEdges,
	                           const TSet<int32>& CompSet,
	                           int32 ConsumerNodeId, double ConsumerX, double PureStartY,
	                           FLayoutState& State)
	{
		if (!InEdges.Contains(ConsumerNodeId)) return;

		TArray<int32> DepIds;
		TSet<int32> DepSeen;
		for (int32 EIdx : InEdges[ConsumerNodeId])
		{
			const FBpLayoutEdge& E = Graph.Edges[EIdx];
			if (E.bIsBroken || E.bIsExec) continue;
			if (!CompSet.Contains(E.SourceNodeId)) continue;
			if (State.Positioned.Contains(E.SourceNodeId)) continue;
			if (Graph.Nodes[E.SourceNodeId].bHasExecPins) continue;
			if (DepSeen.Contains(E.SourceNodeId)) continue;
			DepSeen.Add(E.SourceNodeId);
			DepIds.Add(E.SourceNodeId);
		}
		if (DepIds.Num() == 0) return;

		double CurY = PureStartY;
		for (int32 NId : DepIds)
		{
			FBpLayoutNode& PureNode = Graph.Nodes[NId];
			PureNode.Position.X = ConsumerX - (PureNode.Size.X + 80.0);
			PureNode.Position.Y = CurY;
			State.Positioned.Add(NId);

			UE_LOG(LogMCPTool, Log, TEXT("[Arranger] Pure: '%s' (id=%d) → (%d, %d) [dep of id=%d]"),
				*PureNode.Name, NId, (int32)PureNode.Position.X, (int32)CurY, ConsumerNodeId);

			CurY += PureNode.Size.Y + 20.0;
			State.MaxBottomY = FMath::Max(State.MaxBottomY, PureNode.Position.Y + PureNode.Size.Y);

			PlacePureDeps(Graph, InEdges, CompSet, NId,
			              PureNode.Position.X, PureNode.Position.Y, State);
		}
	}

	static double ResolveOverlapY(const FBpLayoutGraph& Graph, int32 NodeId,
	                               double NodeX, double ProposedY,
	                               const TSet<int32>& Positioned)
	{
		const FBpLayoutNode& Node = Graph.Nodes[NodeId];
		double W = Node.Size.X, H = Node.Size.Y;
		double Y = ProposedY;

		for (int32 PId : Positioned)
		{
			const FBpLayoutNode& P = Graph.Nodes[PId];
			if ((NodeX + W > P.Position.X) && (P.Position.X + P.Size.X > NodeX))
			{
				double PBot = P.Position.Y + P.Size.Y;
				if (Y < PBot + 40.0 && Y + H > P.Position.Y)
					Y = FMath::Max(Y, PBot + 40.0);
			}
		}
		return Y;
	}

	static void DFSBreakCycles(FBpLayoutGraph& Graph, const TMap<int32, TArray<int32>>& OutEdges,
	                            int32 NodeId, TSet<int32>& Visited, TSet<int32>& OnStack)
	{
		Visited.Add(NodeId);
		OnStack.Add(NodeId);
		if (const TArray<int32>* Out = OutEdges.Find(NodeId))
		{
			for (int32 EIdx : *Out)
			{
				FBpLayoutEdge& E = Graph.Edges[EIdx];
				if (E.bIsBroken) continue;
				if (OnStack.Contains(E.TargetNodeId))
					E.bIsBroken = true;
				else if (!Visited.Contains(E.TargetNodeId))
					DFSBreakCycles(Graph, OutEdges, E.TargetNodeId, Visited, OnStack);
			}
		}
		OnStack.Remove(NodeId);
	}

	static void FindComponents(const FBpLayoutGraph& Graph, TArray<TArray<int32>>& Out)
	{
		TSet<int32> Unvisited;
		TMap<int32, TSet<int32>> Nbrs;
		for (const FBpLayoutNode& N : Graph.Nodes) Unvisited.Add(N.Id);
		for (const FBpLayoutEdge& E : Graph.Edges)
		{
			if (E.bIsBroken) continue;
			Nbrs.FindOrAdd(E.SourceNodeId).Add(E.TargetNodeId);
			Nbrs.FindOrAdd(E.TargetNodeId).Add(E.SourceNodeId);
		}
		while (Unvisited.Num() > 0)
		{
			int32 Start = *Unvisited.CreateConstIterator();
			TArray<int32> Comp, Q;
			Q.Add(Start); Unvisited.Remove(Start);
			while (Q.Num() > 0)
			{
				int32 Cur = Q.Pop(EAllowShrinking::No);
				Comp.Add(Cur);
				if (const TSet<int32>* N = Nbrs.Find(Cur))
					for (int32 Nb : *N)
						if (Unvisited.Contains(Nb)) { Unvisited.Remove(Nb); Q.Add(Nb); }
			}
			Comp.Sort([&Graph](int32 A, int32 B)
			{
				const FBpLayoutNode& NA = Graph.Nodes[A];
				const FBpLayoutNode& NB = Graph.Nodes[B];
				if (NA.bHasExecPins != NB.bHasExecPins) return NA.bHasExecPins;
				return NA.Position.Y < NB.Position.Y;
			});
			Out.Add(MoveTemp(Comp));
		}
	}

	static FVector2D GetPinOffset(const FBpLayoutGraph& Graph, int32 PinId)
	{
		for (const FBpLayoutPin& P : Graph.Pins)
			if (P.Id == PinId) return P.Offset;
		return FVector2D(0.0, 30.0);
	}

	static void FitCommentBoxes(FBpLayoutGraph& Graph)
	{
		const FVector2D& Pad = Graph.Config.CommentPadding;
		const double HeaderH = Graph.Config.CommentHeaderH;

		for (FBpLayoutComment& Cmt : Graph.Comments)
		{
			if (Cmt.ContainedNodeIds.Num() == 0) continue;

			double X0 = 1e9, Y0 = 1e9, X1 = -1e9, Y1 = -1e9;
			for (int32 NId : Cmt.ContainedNodeIds)
			{
				if (NId < 0 || NId >= Graph.Nodes.Num()) continue;
				const FBpLayoutNode& N = Graph.Nodes[NId];
				X0 = FMath::Min(X0, N.Position.X);
				Y0 = FMath::Min(Y0, N.Position.Y);
				X1 = FMath::Max(X1, N.Position.X + N.Size.X);
				Y1 = FMath::Max(Y1, N.Position.Y + N.Size.Y);
			}
			if (X0 > X1) continue;

			Cmt.Position.X = X0 - Pad.X;
			Cmt.Position.Y = Y0 - Pad.Y - HeaderH;
			Cmt.Size.X = (X1 - X0) + Pad.X * 2.0;
			Cmt.Size.Y = (Y1 - Y0) + Pad.Y * 2.0 + HeaderH;
		}
	}
};
