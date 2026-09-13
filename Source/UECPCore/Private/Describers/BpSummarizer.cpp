// Copyright 2026, BlueprintsLab, All rights reserved

#include "Describers/BpSummarizer.h"
#include "Describers/BpGraphDescriber.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/PanelWidget.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "HAL/FileManager.h"
#include "K2Node_Event.h"
#include "Misc/PackageName.h"
#include "WidgetBlueprint.h"

namespace
{
	void DescribeWidgetHierarchy(UWidget* Widget, FStringBuilderBase& Report, int32 Indent)
	{
		if (!Widget) return;

		Report.Append(FString::ChrN(Indent * 2, ' '));
		Report.Appendf(TEXT("- %s (Class: %s)\n"), *Widget->GetFName().ToString(), *Widget->GetClass()->GetName());

		if (UPanelWidget* Panel = Cast<UPanelWidget>(Widget))
		{
			for (int32 i = 0; i < Panel->GetChildrenCount(); ++i)
			{
				DescribeWidgetHierarchy(Panel->GetChildAt(i), Report, Indent + 1);
			}
		}
	}

	FString PinTypeToString(const FEdGraphPinType& PinType)
	{
		auto TerminalName = [](const FName& Cat, const TWeakObjectPtr<UObject>& Obj) -> FString
		{
			if (Obj.IsValid()) return Obj->GetName();
			return Cat.ToString();
		};

		const FString Inner = TerminalName(PinType.PinCategory, PinType.PinSubCategoryObject);

		switch (PinType.ContainerType)
		{
		case EPinContainerType::Array:
			return FString::Printf(TEXT("TArray<%s>"), *Inner);
		case EPinContainerType::Set:
			return FString::Printf(TEXT("TSet<%s>"), *Inner);
		case EPinContainerType::Map:
		{
			const FString ValInner = TerminalName(
				PinType.PinValueType.TerminalCategory,
				PinType.PinValueType.TerminalSubCategoryObject);
			return FString::Printf(TEXT("TMap<%s,%s>"), *Inner, *ValInner);
		}
		default:
			return Inner;
		}
	}
}

FString FBpSummarizer::Summarize(UBlueprint* Blueprint)
{
	TArray<FBpIssue> DiscardedIssues;
	return SummarizeWithIssues(Blueprint, DiscardedIssues);
}

FString FBpSummarizer::SummarizeWithIssues(UBlueprint* Blueprint, TArray<FBpIssue>& OutIssues)
{
	if (!Blueprint) return TEXT("Invalid Blueprint provided.");

	TStringBuilder<8192> Report;
	TArray<FString> PerformanceIssues;
	bool bTickIsUsed = false;

	auto AddIssue = [&PerformanceIssues, &OutIssues](FBpIssue::ESeverity Sev, const FString& Code, const FString& Msg)
	{
		PerformanceIssues.AddUnique(Msg);
		const bool bAlreadyPresent = OutIssues.ContainsByPredicate(
			[&Code](const FBpIssue& Existing) { return Existing.Code == Code; });
		if (!bAlreadyPresent)
		{
			OutIssues.Add(FBpIssue{ Sev, Code, Msg });
		}
	};

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	FAssetData AssetData = AssetRegistryModule.Get().GetAssetByObjectPath(FSoftObjectPath(Blueprint));

	Report.Appendf(TEXT("--- BLUEPRINT SUMMARY: %s ---\n\n"), *Blueprint->GetName());

	Report.Append(TEXT("--- ASSET ANALYSIS ---\n"));
	TArray<FAssetIdentifier> Referencers;
	AssetRegistryModule.Get().GetReferencers(AssetData.PackageName, Referencers);
	TArray<FAssetIdentifier> Dependencies;
	AssetRegistryModule.Get().GetDependencies(AssetData.PackageName, Dependencies);
	const FString FilePath = FPackageName::LongPackageNameToFilename(AssetData.PackageName.ToString(), FPackageName::GetAssetPackageExtension());
	const int64 FileSize = IFileManager::Get().FileSize(*FilePath);
	Report.Appendf(TEXT("On-Disk Size: %.2f KB\n"), FileSize / 1024.0f);
	Report.Appendf(TEXT("Referenced By (Referencers): %d other assets\n"), Referencers.Num());
	Report.Appendf(TEXT("References (Dependencies): %d other assets\n"), Dependencies.Num());
	Report.Append(TEXT("Hard Asset References:\n"));
	int32 HardRefCount = 0;
	for (const FAssetIdentifier& Dep : Dependencies)
	{
		if (Dep.IsPackage() && Dep.PackageName != AssetData.PackageName && !Dep.PackageName.ToString().StartsWith(TEXT("/Script")))
		{
			Report.Appendf(TEXT("  - %s\n"), *Dep.PackageName.ToString());
			HardRefCount++;
		}
	}
	if (HardRefCount == 0) Report.Append(TEXT("  - None\n"));
	Report.Append(TEXT("\n"));

	if (HardRefCount > 30)
	{
		AddIssue(FBpIssue::ESeverity::Warn, TEXT("many_hard_refs"),
			FString::Printf(TEXT("%d hard asset references — cold load cost; consider soft refs / async loading"), HardRefCount));
	}

	if (!Blueprint->ParentClass)
	{
		AddIssue(FBpIssue::ESeverity::Info, TEXT("orphan_blueprint"),
			TEXT("Blueprint has no parent class — likely orphaned or broken"));
	}

	Report.Appendf(TEXT("Parent Class: %s\n"), Blueprint->ParentClass ? *Blueprint->ParentClass->GetName() : TEXT("None (Invalid)"));

	if (Blueprint->ImplementedInterfaces.Num() > 0)
	{
		Report.Append(TEXT("Implemented Interfaces:\n"));
		for (const auto& Interface : Blueprint->ImplementedInterfaces)
		{
			if (Interface.Interface) Report.Appendf(TEXT("  - %s\n"), *Interface.Interface->GetName());
		}
	}
	Report.Append(TEXT("\n"));

	if (UWidgetBlueprint* WidgetBP = Cast<UWidgetBlueprint>(Blueprint))
	{
		if (WidgetBP->WidgetTree && WidgetBP->WidgetTree->RootWidget)
		{
			Report.Append(TEXT("--- WIDGET HIERARCHY ---\n"));
			DescribeWidgetHierarchy(WidgetBP->WidgetTree->RootWidget, Report, 0);
			Report.Append(TEXT("\n"));
		}
	}

	if (Blueprint->SimpleConstructionScript && Blueprint->SimpleConstructionScript->GetAllNodes().Num() > 0)
	{
		const int32 ComponentCount = Blueprint->SimpleConstructionScript->GetAllNodes().Num();
		Report.Append(TEXT("--- COMPONENTS ---\n"));
		for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
		{
			if (Node && Node->ComponentClass) Report.Appendf(TEXT("- %s (Name: %s)\n"), *Node->ComponentClass->GetName(), *Node->GetVariableName().ToString());
		}
		Report.Append(TEXT("\n"));

		if (ComponentCount > 15)
		{
			AddIssue(FBpIssue::ESeverity::Warn, TEXT("many_components"),
				FString::Printf(TEXT("%d components on actor — heavy CDO; consider composing via child actors or splitting"), ComponentCount));
		}
	}

	if (Blueprint->NewVariables.Num() > 0)
	{
		Report.Append(TEXT("--- VARIABLES ---\n"));
		for (const FBPVariableDescription& VarDesc : Blueprint->NewVariables)
		{
			FString TypeStr = PinTypeToString(VarDesc.VarType);
			FString MetaData;
			if ((VarDesc.PropertyFlags & CPF_Edit) != 0) MetaData += TEXT("(Instance Editable) ");
			if ((VarDesc.PropertyFlags & CPF_ExposeOnSpawn) != 0) MetaData += TEXT("(Expose on Spawn) ");
			FString DefaultStr;
			if (!VarDesc.DefaultValue.IsEmpty())
				DefaultStr = FString::Printf(TEXT(" = %s"), *VarDesc.DefaultValue);
			Report.Appendf(TEXT("- %s %s%s %s\n"), *TypeStr, *VarDesc.VarName.ToString(), *DefaultStr, *MetaData);
		}
		Report.Append(TEXT("\n"));

		if (Blueprint->NewVariables.Num() > 50)
		{
			AddIssue(FBpIssue::ESeverity::Warn, TEXT("many_variables"),
				FString::Printf(TEXT("%d variables — consider extracting state into structs or components"), Blueprint->NewVariables.Num()));
		}
	}

	if (Blueprint->FunctionGraphs.Num() > 30)
	{
		AddIssue(FBpIssue::ESeverity::Warn, TEXT("many_functions"),
			FString::Printf(TEXT("%d functions — consider splitting via Blueprint Function Libraries or interfaces"), Blueprint->FunctionGraphs.Num()));
	}

	TArray<UEdGraph*> GraphsToAnalyze;
	GraphsToAnalyze.Append(Blueprint->UbergraphPages);
	GraphsToAnalyze.Append(Blueprint->FunctionGraphs);
	FBpGraphDescriber GraphDescriber;

	for (UEdGraph* Graph : GraphsToAnalyze)
	{
		if (!Graph || !Graph->Nodes.Num()) continue;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (!Node) continue;
			UK2Node_Event* EventNode = Cast<UK2Node_Event>(Node);
			if (!EventNode) continue;
			if (EventNode->EventReference.GetMemberName() != FName(TEXT("Tick"))) continue;

			UEdGraphPin* ThenPin = EventNode->FindPin(UEdGraphSchema_K2::PN_Then);
			if (!ThenPin || ThenPin->LinkedTo.Num() == 0) continue;

			bTickIsUsed = true;

			int32 TickNodeCount = 0;
			TSet<UEdGraphNode*> VisitedNodes;
			TArray<UEdGraphNode*> NodesToCheck;
			NodesToCheck.Add(EventNode);

			while (NodesToCheck.Num() > 0)
			{
				UEdGraphNode* CurrentNode = NodesToCheck.Pop();
				if (!CurrentNode || VisitedNodes.Contains(CurrentNode)) continue;
				VisitedNodes.Add(CurrentNode);
				TickNodeCount++;

				const FString NodeName = CurrentNode->GetClass()->GetName();
				if (NodeName.Contains(TEXT("Cast")))
				{
					AddIssue(FBpIssue::ESeverity::Warn, TEXT("tick_cast"),
						TEXT("Cast<> in Event Tick - cache the reference instead"));
				}
				if (NodeName.Contains(TEXT("GetAllActorsOfClass")))
				{
					AddIssue(FBpIssue::ESeverity::Critical, TEXT("tick_get_all_actors"),
						TEXT("GetAllActorsOfClass in Event Tick - extremely expensive, use interfaces or tags"));
				}
				if (NodeName.Contains(TEXT("SpawnActor")) || NodeName.Contains(TEXT("Spawn")))
				{
					AddIssue(FBpIssue::ESeverity::Warn, TEXT("tick_spawn_actor"),
						TEXT("SpawnActor in Event Tick - object pooling recommended"));
				}
				if (NodeName.Contains(TEXT("BuildString")) || NodeName.Contains(TEXT("Concat")))
				{
					AddIssue(FBpIssue::ESeverity::Warn, TEXT("tick_string_ops"),
						TEXT("String operations in Event Tick - causes GC pressure"));
				}

				for (UEdGraphPin* Pin : CurrentNode->Pins)
				{
					if (Pin->Direction != EGPD_Output) continue;
					for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
					{
						if (UEdGraphNode* LinkedNode = LinkedPin ? LinkedPin->GetOwningNodeUnchecked() : nullptr)
						{
							NodesToCheck.Add(LinkedNode);
						}
					}
				}
			}

			if (TickNodeCount > 20)
			{
				AddIssue(FBpIssue::ESeverity::Warn, TEXT("tick_node_count_high"),
					FString::Printf(TEXT("Event Tick has %d nodes - consider complexity reduction"), TickNodeCount));
			}
			break;
		}
		if (bTickIsUsed) break;

		Report.Appendf(TEXT("--- Analyzing Graph: %s ---\n"), *Graph->GetName());
		TSet<UObject*> AllNodes;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (Node) AllNodes.Add(Node);
		}
		Report.Append(GraphDescriber.Describe(AllNodes));
		Report.Append(TEXT("\n"));
	}

	if (bTickIsUsed && PerformanceIssues.Num() == 0)
	{
		AddIssue(FBpIssue::ESeverity::Info, TEXT("tick_used"),
			TEXT("Uses Event Tick - review for optimization opportunities"));
	}

	int32 GetAllActorsHits = 0;
	int32 CastNodeCount = 0;
	for (UEdGraph* Graph : GraphsToAnalyze)
	{
		if (!Graph) continue;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (!Node) continue;
			const FString NodeName = Node->GetClass()->GetName();
			if (NodeName.Contains(TEXT("GetAllActorsOfClass"))) GetAllActorsHits++;
			if (NodeName.Contains(TEXT("DynamicCast"))) CastNodeCount++;
		}
	}
	if (GetAllActorsHits > 0 && !OutIssues.ContainsByPredicate([](const FBpIssue& I){ return I.Code == TEXT("tick_get_all_actors"); }))
	{
		AddIssue(FBpIssue::ESeverity::Warn, TEXT("get_all_actors_anywhere"),
			FString::Printf(TEXT("GetAllActorsOfClass used %d time(s) — prefer interfaces, tags, or a registered subsystem list"), GetAllActorsHits));
	}
	if (CastNodeCount > 10)
	{
		AddIssue(FBpIssue::ESeverity::Info, TEXT("cast_heavy"),
			FString::Printf(TEXT("%d Cast<> nodes across graphs — consider interfaces to decouple"), CastNodeCount));
	}

	if (PerformanceIssues.Num() > 0)
	{
		Report.Append(TEXT("\n--- POTENTIAL PERFORMANCE ISSUES ---\n"));
		for (const FString& Issue : PerformanceIssues)
		{
			Report.Appendf(TEXT("! %s\n"), *Issue);
		}
	}

	int32 TotalNodeCount = 0;
	const int32 GraphCount = Blueprint->UbergraphPages.Num() + Blueprint->FunctionGraphs.Num();
	for (UEdGraph* Graph : Blueprint->UbergraphPages)
	{
		if (Graph) TotalNodeCount += Graph->Nodes.Num();
	}
	for (UEdGraph* Graph : Blueprint->FunctionGraphs)
	{
		if (Graph) TotalNodeCount += Graph->Nodes.Num();
	}
	const int32 ComplexityScore = TotalNodeCount * FMath::Max(1, GraphCount);
	Report.Append(TEXT("\n--- COMPLEXITY ---\n"));
	Report.Appendf(TEXT("Total Nodes: %d\n"), TotalNodeCount);
	Report.Appendf(TEXT("Total Graphs: %d\n"), GraphCount);
	Report.Appendf(TEXT("Complexity Score: %d\n"), ComplexityScore);

	if (ComplexityScore >= 2500)
	{
		AddIssue(FBpIssue::ESeverity::Critical, TEXT("complexity_critical"),
			FString::Printf(TEXT("Complexity score %d — refactor strongly recommended"), ComplexityScore));
	}
	else if (ComplexityScore >= 1000)
	{
		AddIssue(FBpIssue::ESeverity::Warn, TEXT("complexity_high"),
			FString::Printf(TEXT("Complexity score %d — consider splitting"), ComplexityScore));
	}

	return FString(Report);
}

int32 FBpSummarizer::ComputeComplexityScore(UBlueprint* Blueprint) const
{
	if (!Blueprint) return 0;
	int32 TotalNodeCount = 0;
	const int32 GraphCount = Blueprint->UbergraphPages.Num() + Blueprint->FunctionGraphs.Num();
	for (UEdGraph* Graph : Blueprint->UbergraphPages)
	{
		if (Graph) TotalNodeCount += Graph->Nodes.Num();
	}
	for (UEdGraph* Graph : Blueprint->FunctionGraphs)
	{
		if (Graph) TotalNodeCount += Graph->Nodes.Num();
	}
	return TotalNodeCount * FMath::Max(1, GraphCount);
}

FString FBpSummarizer::ComputeShortSummary(UBlueprint* Blueprint) const
{
	if (!Blueprint) return FString();

	const FString ParentName = Blueprint->ParentClass ? Blueprint->ParentClass->GetName() : TEXT("None");

	const int32 ComponentCount = (Blueprint->SimpleConstructionScript
		? Blueprint->SimpleConstructionScript->GetAllNodes().Num()
		: 0);
	const int32 VarCount = Blueprint->NewVariables.Num();
	const int32 FuncCount = Blueprint->FunctionGraphs.Num();
	const int32 Score = ComputeComplexityScore(Blueprint);

	TStringBuilder<512> Out;
	Out.Appendf(TEXT("%s inheriting %s — %d component(s), %d var(s), %d function(s), complexity %d"),
		*Blueprint->GetName(), *ParentName, ComponentCount, VarCount, FuncCount, Score);

	if (Blueprint->ImplementedInterfaces.Num() > 0)
	{
		Out.Append(TEXT(". Implements: "));
		TArray<FString> InterfaceNames;
		for (const auto& Iface : Blueprint->ImplementedInterfaces)
		{
			if (Iface.Interface) InterfaceNames.Add(Iface.Interface->GetName());
		}
		Out.Append(FString::Join(InterfaceNames, TEXT(", ")));
	}

	if (UWidgetBlueprint* WidgetBP = Cast<UWidgetBlueprint>(Blueprint))
	{
		Out.Append(TEXT(". (Widget Blueprint)"));
		(void)WidgetBP;
	}

	FString Result = FString(Out);
	if (Result.Len() > 220) Result = Result.Left(217) + TEXT("...");
	return Result;
}

FString FBpSummarizer::Summarize(UBlueprint* Blueprint, int32 MaxChars)
{
	FString Result = Summarize(Blueprint);
	if (MaxChars > 0 && Result.Len() > MaxChars)
	{
		const int32 Omitted = Result.Len() - MaxChars;
		Result = Result.Left(MaxChars);
		Result += FString::Printf(TEXT("\n\n[TRUNCATED — %d chars omitted. Use blueprint(action=\"get_blueprint_graph\", graph_name=\"FunctionName\") for full detail on any graph.]"), Omitted);
	}
	return Result;
}
