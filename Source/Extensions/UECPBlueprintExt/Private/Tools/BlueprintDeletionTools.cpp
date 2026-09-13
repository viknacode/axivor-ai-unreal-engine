// Copyright 2026, BlueprintsLab, All rights reserved

#include "Tools/BlueprintDeletionTools.h"
#include "Tools/BlueprintNodeIdentity.h"
#include "EditorAssetLibrary.h"
#include "Engine/LevelScriptBlueprint.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "BlueprintEditor.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "EdGraph/EdGraphNode.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "Editor.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Serialization/JsonSerializer.h"
#include "Tools/BatchToolHelper.h"

namespace BlueprintDeletionTools
{

static UBlueprint* LoadBPFromPath(const FString& BpPath)
{
	if (BpPath.Equals(TEXT("@level_blueprint"), ESearchCase::IgnoreCase))
	{
		UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
		if (World && World->PersistentLevel)
			return Cast<UBlueprint>(World->PersistentLevel->GetLevelScriptBlueprint(false));
		return nullptr;
	}
	return Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(BpPath));
}

void HandleDeleteVariable(const FString& BpPath, const FString& VarName, FString& OutError)
{
	UBlueprint* TargetBlueprint = LoadBPFromPath(BpPath);
	if (!TargetBlueprint)
	{
		OutError = FString::Printf(TEXT("Could not load Blueprint at path: %s"), *BpPath);
		return;
	}

	FGuid VarGuid = FBlueprintEditorUtils::FindMemberVariableGuidByName(TargetBlueprint, FName(*VarName));
	if (!VarGuid.IsValid())
	{
		OutError = FString::Printf(TEXT("Variable '%s' not found in Blueprint."), *VarName);
		return;
	}

	const FScopedTransaction Transaction(FText::FromString(TEXT("Delete Variable")));
	TargetBlueprint->Modify();
	FBlueprintEditorUtils::RemoveMemberVariable(TargetBlueprint, FName(*VarName));
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(TargetBlueprint);
}

void HandleDeleteComponent(const FString& BpPath, const FString& ComponentName, FString& OutError)
{
	UBlueprint* TargetBlueprint = LoadBPFromPath(BpPath);
	if (!TargetBlueprint)
	{
		OutError = FString::Printf(TEXT("Could not load Blueprint at path: %s"), *BpPath);
		return;
	}

	USCS_Node* ComponentNode = TargetBlueprint->SimpleConstructionScript->FindSCSNode(FName(*ComponentName));
	if (!ComponentNode)
	{
		OutError = FString::Printf(TEXT("Component '%s' not found in Blueprint."), *ComponentName);
		return;
	}

	if (ComponentNode->IsNative())
	{
		OutError = FString::Printf(TEXT("Cannot delete native/root component '%s'."), *ComponentName);
		return;
	}

	const FScopedTransaction Transaction(FText::FromString(TEXT("Delete Component")));
	TargetBlueprint->Modify();
	TargetBlueprint->SimpleConstructionScript->RemoveNode(ComponentNode);
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(TargetBlueprint);
}

void HandleDeleteNodes(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{

	FString BpPath;
	if (Args.IsValid() && Args->TryGetStringField(TEXT("blueprint_path"), BpPath) && !BpPath.IsEmpty())
	{
		UBlueprint* TargetBlueprint = LoadBPFromPath(BpPath);
		if (!TargetBlueprint)
		{
			OutError = FString::Printf(TEXT("Could not load Blueprint at path: %s"), *BpPath);
			return;
		}

		TArray<FString> NodeIds;
		FString SingleId;
		if (Args->TryGetStringField(TEXT("node_id"), SingleId) && !SingleId.IsEmpty())
			NodeIds.Add(SingleId);
		const TArray<TSharedPtr<FJsonValue>>* IdsArray = nullptr;
		if (Args->TryGetArrayField(TEXT("node_ids"), IdsArray) && IdsArray)
		{
			for (const TSharedPtr<FJsonValue>& V : *IdsArray)
				if (V.IsValid()) NodeIds.AddUnique(V->AsString());
		}

		FString GraphName;
		Args->TryGetStringField(TEXT("graph_name"), GraphName);

		TArray<UEdGraph*> AllGraphs;
		if (!GraphName.IsEmpty())
		{
			for (UEdGraph* G : TargetBlueprint->UbergraphPages)
				if (G && G->GetFName().ToString().Equals(GraphName, ESearchCase::IgnoreCase)) { AllGraphs.Add(G); break; }
			if (AllGraphs.IsEmpty())
				for (UEdGraph* G : TargetBlueprint->FunctionGraphs)
					if (G && G->GetFName().ToString().Equals(GraphName, ESearchCase::IgnoreCase)) { AllGraphs.Add(G); break; }
		}
		if (AllGraphs.IsEmpty())
		{
			AllGraphs.Append(TargetBlueprint->UbergraphPages);
			AllGraphs.Append(TargetBlueprint->FunctionGraphs);
		}

		const FScopedTransaction Transaction(FText::FromString(TEXT("Delete Nodes By ID")));
		TargetBlueprint->Modify();

		int32 DeletedCount = 0;
		TArray<FString> NotFound;
		for (const FString& NodeId : NodeIds)
		{
			TArray<UEdGraphNode*> ToDelete;
			for (UEdGraph* Graph : AllGraphs)
			{
				if (!Graph) continue;
				for (UEdGraphNode* Node : Graph->Nodes)
				{
					if (!Node) continue;
					const FString LogicalId = BlueprintNodeIdentity::GetLogicalId(TargetBlueprint, Node);
					const bool bGeidMatch = !LogicalId.IsEmpty() && LogicalId.Equals(NodeId, ESearchCase::IgnoreCase);
					bool bMatch = bGeidMatch ||
					              Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens).Equals(NodeId, ESearchCase::IgnoreCase) ||
					              Node->NodeGuid.ToString(EGuidFormats::Digits).Equals(NodeId, ESearchCase::IgnoreCase);
					if (bMatch)
						ToDelete.Add(Node);
				}
			}
			if (ToDelete.Num() > 0)
			{
				for (UEdGraphNode* Node : ToDelete)
				{
					Node->Modify();
					BlueprintNodeIdentity::ClearLogicalId(TargetBlueprint, Node);
					Node->DestroyNode();
					DeletedCount++;
				}
			}
			else
			{
				NotFound.Add(NodeId);
			}
		}

		FBlueprintEditorUtils::MarkBlueprintAsModified(TargetBlueprint);

		TSharedPtr<FJsonObject> ResultObject = MakeShareable(new FJsonObject());
		const bool bAllFound = NotFound.Num() == 0;
		ResultObject->SetBoolField(TEXT("success"), bAllFound);
		ResultObject->SetNumberField(TEXT("deleted_count"), DeletedCount);
		if (NotFound.Num() > 0)
		{
			TArray<TSharedPtr<FJsonValue>> NF;
			for (const FString& S : NotFound) NF.Add(MakeShared<FJsonValueString>(S));
			ResultObject->SetArrayField(TEXT("not_found"), NF);
			ResultObject->SetStringField(TEXT("message"), FString::Printf(
				TEXT("RECOVERY: call compile_blueprint, then get_blueprint_graph(graph_name) (returns hex GUIDs that delete_nodes accepts). "
				     "DO NOT call clear_blueprint_graph — that wipes everything, not just the orphans. "
				     "Deleted %d node(s); %d ID(s) not found — logical ids from build_blueprint_graph are replaced when the same graph is rebuilt. "
				     "If you want to remove orphaned AI-built nodes specifically, compile_blueprint reports them under health_issues — pass those IDs to delete_nodes."),
				DeletedCount, NotFound.Num()));
		}
		else
		{
			ResultObject->SetStringField(TEXT("message"), FString::Printf(TEXT("Deleted %d node(s)"), DeletedCount));
		}
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
		FJsonSerializer::Serialize(ResultObject.ToSharedRef(), Writer);
		return;
	}

	UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
	if (!AssetEditorSubsystem)
	{
		OutError = TEXT("Could not get Asset Editor Subsystem.");
		TSharedPtr<FJsonObject> ResultObject = MakeShareable(new FJsonObject());
		ResultObject->SetBoolField(TEXT("success"), false);
		ResultObject->SetStringField(TEXT("error"), OutError);
		FString ResultString;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResultString);
		FJsonSerializer::Serialize(ResultObject.ToSharedRef(), Writer);
		OutJsonString = ResultString;
		return;
	}

	IAssetEditorInstance* ActiveEditor = nullptr;
	double LastActivationTime = 0.0;
	TArray<UObject*> EditedAssets = AssetEditorSubsystem->GetAllEditedAssets();

	for (UObject* Asset : EditedAssets)
	{
		TArray<IAssetEditorInstance*> Editors = AssetEditorSubsystem->FindEditorsForAsset(Asset);
		for (IAssetEditorInstance* Editor : Editors)
		{
			const FName EditorName = Editor->GetEditorName();
			if (EditorName == FName(TEXT("BlueprintEditor")) ||
				EditorName == FName(TEXT("AnimationBlueprintEditor")) ||
				EditorName == FName(TEXT("WidgetBlueprintEditor")))
			{
				if (Editor && Editor->GetLastActivationTime() > LastActivationTime)
				{
					LastActivationTime = Editor->GetLastActivationTime();
					ActiveEditor = Editor;
				}
			}
		}
	}

	if (!ActiveEditor)
	{
		OutError = TEXT("No active Blueprint editor found.");
		TSharedPtr<FJsonObject> ResultObject = MakeShareable(new FJsonObject());
		ResultObject->SetBoolField(TEXT("success"), false);
		ResultObject->SetStringField(TEXT("error"), OutError);
		FString ResultString;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResultString);
		FJsonSerializer::Serialize(ResultObject.ToSharedRef(), Writer);
		OutJsonString = ResultString;
		return;
	}

	TSharedPtr<FJsonObject> ResultObject = MakeShareable(new FJsonObject());
	int32 DeletedCount = 0;

	if (FBlueprintEditor* BlueprintEditor = static_cast<FBlueprintEditor*>(ActiveEditor))
	{
		const TSet<UObject*>& SelectedNodes = BlueprintEditor->GetSelectedNodes();
		if (SelectedNodes.Num() == 0)
		{
			OutError = TEXT("No nodes selected in active Blueprint editor.");
			ResultObject->SetBoolField(TEXT("success"), false);
			ResultObject->SetStringField(TEXT("error"), OutError);
			FString ResultString;
			TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResultString);
			FJsonSerializer::Serialize(ResultObject.ToSharedRef(), Writer);
			OutJsonString = ResultString;
			return;
		}

		const FScopedTransaction Transaction(FText::FromString(TEXT("Delete Nodes")));

		for (UObject* NodeObj : SelectedNodes)
		{
			if (UEdGraphNode* Node = Cast<UEdGraphNode>(NodeObj))
			{
				if (Node->GetClass()->GetName() == TEXT("EdGraphCommentNode"))
				{
					continue;
				}

				UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForNode(Node);
				if (Blueprint)
				{
					Blueprint->Modify();
					Node->Modify();
					Node->DestroyNode();
					DeletedCount++;
				}
			}
		}

		FBlueprintEditorUtils::MarkBlueprintAsModified(BlueprintEditor->GetBlueprintObj());

		if (DeletedCount == 0)
		{
			OutError = TEXT("No deletable nodes were found in selection (comment nodes are skipped).");
			ResultObject->SetBoolField(TEXT("success"), false);
			ResultObject->SetStringField(TEXT("error"), OutError);
			FString ResultString;
			TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResultString);
			FJsonSerializer::Serialize(ResultObject.ToSharedRef(), Writer);
			OutJsonString = ResultString;
			return;
		}
	}
	else
	{
		OutError = TEXT("Active editor is not a Blueprint editor.");
		ResultObject->SetBoolField(TEXT("success"), false);
		ResultObject->SetStringField(TEXT("error"), OutError);
		FString ResultString;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResultString);
		FJsonSerializer::Serialize(ResultObject.ToSharedRef(), Writer);
		OutJsonString = ResultString;
		return;
	}

	ResultObject->SetBoolField(TEXT("success"), true);
	ResultObject->SetNumberField(TEXT("deleted_count"), DeletedCount);
	ResultObject->SetStringField(TEXT("message"), FString::Printf(TEXT("Successfully deleted %d node(s)."), DeletedCount));

	FString ResultString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResultString);
	FJsonSerializer::Serialize(ResultObject.ToSharedRef(), Writer);
	OutJsonString = ResultString;
}

void HandleDeleteUnusedVariables(const FString& BpPath, FString& OutJsonString, FString& OutError)
{
	TSharedPtr<FJsonObject> ResultObject = MakeShareable(new FJsonObject());
	ResultObject->SetBoolField(TEXT("success"), false);
	ResultObject->SetStringField(TEXT("error"), TEXT("The delete_unused_variables tool is not yet fully implemented. Please manually delete unused variables from Blueprint Editor by right-clicking them in MyBlueprint panel."));
	ResultObject->SetStringField(TEXT("info"), TEXT("You can identify unused variables by looking at your graphs - variables not appearing in any node are likely unused."));

	FString ResultString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResultString);
	FJsonSerializer::Serialize(ResultObject.ToSharedRef(), Writer);
	OutJsonString = ResultString;
}

void HandleDeleteFunction(const FString& BpPath, const FString& FunctionName, FString& OutError)
{
	HandleDeleteFunctionWithForce(BpPath, FunctionName, false, OutError);
}

void HandleDeleteFunctionWithForce(const FString& BpPath, const FString& FunctionName, bool bForce, FString& OutError)
{
	UBlueprint* TargetBlueprint = LoadBPFromPath(BpPath);
	if (!TargetBlueprint)
	{
		OutError = FString::Printf(TEXT("Could not load Blueprint at path: %s"), *BpPath);
		return;
	}

	UEdGraph* FunctionGraph = nullptr;
	for (UEdGraph* Graph : TargetBlueprint->FunctionGraphs)
	{
		if (Graph->GetFName() == FName(*FunctionName))
		{
			FunctionGraph = Graph;
			break;
		}
	}

	if (!FunctionGraph)
	{
		OutError = FString::Printf(TEXT("Function '%s' not found in Blueprint."), *FunctionName);
		return;
	}

	if (!bForce)
	{
		int32 NonTrivialNodes = 0;
		for (UEdGraphNode* Node : FunctionGraph->Nodes)
		{
			if (!IsValid(Node)) continue;
			const FString CN = Node->GetClass()->GetName();
			if (CN.Contains(TEXT("FunctionEntry")) || CN.Contains(TEXT("FunctionResult"))) continue;
			NonTrivialNodes++;
		}
		const int32 NonTrivialThreshold = 3;
		if (NonTrivialNodes > NonTrivialThreshold)
		{
			OutError = FString::Printf(
				TEXT("delete_function refused: '%s' contains %d non-trivial node(s) — deleting destroys all that logic. "
				     "If you only need to change the function's parameters, use add_function_param / remove_function_param "
				     "(both preserve the graph). To rename, use rename_function. "
				     "If you genuinely want to delete this function and its logic, re-issue the call with force=true."),
				*FunctionName, NonTrivialNodes);
			return;
		}
	}

	{
		int32 RemovedStale = 0;
		for (int32 i = TargetBlueprint->ImplementedInterfaces.Num() - 1; i >= 0; --i)
		{
			if (TargetBlueprint->ImplementedInterfaces[i].Interface == nullptr)
			{
				TargetBlueprint->ImplementedInterfaces.RemoveAt(i);
				++RemovedStale;
			}
		}
		if (RemovedStale > 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("delete_function: stripped %d stale ImplementedInterfaces entry/entries on '%s' before RemoveGraph (would have crashed otherwise)."),
				RemovedStale, *TargetBlueprint->GetName());
		}
	}

	const FScopedTransaction Transaction(FText::FromString(TEXT("Delete Function")));
	TargetBlueprint->Modify();
	FBlueprintEditorUtils::RemoveGraph(TargetBlueprint, FunctionGraph);
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(TargetBlueprint);
}

void HandleDeleteVariableFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString BpPath;
	if (!Args->TryGetStringField(TEXT("blueprint_path"), BpPath) || BpPath.IsEmpty())
	{ OutError = TEXT("Missing required parameter: blueprint_path"); return; }

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("variables"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			FString VarName;
			if ((*ItemsArray)[i]->Type == EJson::String)
				VarName = (*ItemsArray)[i]->AsString();
			else if (auto Obj = (*ItemsArray)[i]->AsObject())
				VarName = BatchToolHelper::GetItemString(Obj, TEXT("variable_name"), TEXT("name"));
			if (VarName.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing variable name")); continue; }
			FString Err;
			HandleDeleteVariable(BpPath, VarName, Err);
			if (Err.IsEmpty())
			{
				auto Extra = MakeShared<FJsonObject>();
				Extra->SetStringField(TEXT("variable_name"), VarName);
				Batch.AddSuccess(i, Extra);
			}
			else Batch.AddFailure(i, Err);
		}
		Batch.Finalize(OutJsonString);
		return;
	}

	FString VarName;
	if (!Args->TryGetStringField(TEXT("variable_name"), VarName))
		Args->TryGetStringField(TEXT("name"), VarName);
	HandleDeleteVariable(BpPath, VarName, OutError);
	if (OutError.IsEmpty())
		OutJsonString = FString::Printf(TEXT("{\"success\":true,\"deleted\":\"%s\"}"), *VarName);
}

void HandleDeleteFunctionFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString BpPath;
	if (!Args->TryGetStringField(TEXT("blueprint_path"), BpPath) || BpPath.IsEmpty())
	{ OutError = TEXT("Missing required parameter: blueprint_path"); return; }

	bool bForce = false;
	Args->TryGetBoolField(TEXT("force"), bForce);

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("functions"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			FString FuncName;
			bool bItemForce = bForce;
			if ((*ItemsArray)[i]->Type == EJson::String)
				FuncName = (*ItemsArray)[i]->AsString();
			else if (auto Obj = (*ItemsArray)[i]->AsObject())
			{
				FuncName = BatchToolHelper::GetItemString(Obj, TEXT("function_name"), TEXT("name"));
				if (Obj->HasField(TEXT("force"))) Obj->TryGetBoolField(TEXT("force"), bItemForce);
			}
			if (FuncName.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing function name")); continue; }
			FString Err;
			HandleDeleteFunctionWithForce(BpPath, FuncName, bItemForce, Err);
			if (Err.IsEmpty())
			{
				auto Extra = MakeShared<FJsonObject>();
				Extra->SetStringField(TEXT("function_name"), FuncName);
				Batch.AddSuccess(i, Extra);
			}
			else Batch.AddFailure(i, Err);
		}
		Batch.Finalize(OutJsonString);
		return;
	}

	FString FuncName;
	if (!Args->TryGetStringField(TEXT("function_name"), FuncName))
		Args->TryGetStringField(TEXT("name"), FuncName);
	HandleDeleteFunctionWithForce(BpPath, FuncName, bForce, OutError);
	if (OutError.IsEmpty())
		OutJsonString = FString::Printf(TEXT("{\"success\":true,\"deleted\":\"%s\"}"), *FuncName);
}

void HandleDeleteComponentFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString BpPath;
	if (!Args->TryGetStringField(TEXT("blueprint_path"), BpPath) || BpPath.IsEmpty())
	{ OutError = TEXT("Missing required parameter: blueprint_path"); return; }

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("components"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			FString CompName;
			if ((*ItemsArray)[i]->Type == EJson::String)
				CompName = (*ItemsArray)[i]->AsString();
			else if (auto Obj = (*ItemsArray)[i]->AsObject())
				CompName = BatchToolHelper::GetItemString(Obj, TEXT("component_name"), TEXT("name"));
			if (CompName.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing component name")); continue; }
			FString Err;
			HandleDeleteComponent(BpPath, CompName, Err);
			if (Err.IsEmpty())
			{
				auto Extra = MakeShared<FJsonObject>();
				Extra->SetStringField(TEXT("component_name"), CompName);
				Batch.AddSuccess(i, Extra);
			}
			else Batch.AddFailure(i, Err);
		}
		Batch.Finalize(OutJsonString);
		return;
	}

	FString CompName;
	if (!Args->TryGetStringField(TEXT("component_name"), CompName))
		Args->TryGetStringField(TEXT("name"), CompName);
	HandleDeleteComponent(BpPath, CompName, OutError);
	if (OutError.IsEmpty())
		OutJsonString = FString::Printf(TEXT("{\"success\":true,\"deleted\":\"%s\"}"), *CompName);
}

void HandleDeleteUnusedVariablesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString BpPath;
	if (Args.IsValid()) Args->TryGetStringField(TEXT("blueprint_path"), BpPath);
	HandleDeleteUnusedVariables(BpPath, OutJsonString, OutError);
}

}
