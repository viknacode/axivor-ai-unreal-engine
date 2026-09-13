// Copyright 2026, BlueprintsLab, All rights reserved.

#include "Tools/BehaviorTreeTools.h"
#include "Tools/BatchToolHelper.h"
#include "Describers/BtGraphDescriber.h"
#include "Managers/EditorProfileSync.h"
#include "EditorAssetLibrary.h"
#include "AssetToolsModule.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"

#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BlackboardData.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Vector.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Bool.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Float.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Int.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_String.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Name.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Rotator.h"
#include "BehaviorTree/Composites/BTComposite_Sequence.h"
#include "BehaviorTree/Composites/BTComposite_Selector.h"
#include "BehaviorTree/Composites/BTComposite_SimpleParallel.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Enum.h"
#include "BehaviorTree/Tasks/BTTask_BlueprintBase.h"
#include "BehaviorTree/Services/BTService_BlueprintBase.h"
#include "BehaviorTree/Decorators/BTDecorator_BlueprintBase.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BehaviorTree/BTService.h"
#include "BehaviorTree/BTDecorator.h"
#include "UObject/UObjectIterator.h"

#include "BehaviorTreeGraph.h"
#include "BehaviorTreeGraphNode.h"
#include "BehaviorTreeGraphNode_Root.h"
#include "BehaviorTreeGraphNode_Composite.h"
#include "BehaviorTreeGraphNode_Task.h"
#include "BehaviorTreeGraphNode_Service.h"
#include "BehaviorTreeGraphNode_Decorator.h"

namespace BehaviorTreeTools
{

static bool ApplyBTGraphLayout(UBehaviorTreeGraph* BTGraph);

static void BuildSuccessJson(const TSharedPtr<FJsonObject>& Obj, FString& OutJsonString)
{
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Obj.ToSharedRef(), Writer);
}

static void SetError(const FString& Msg, FString& OutJsonString, FString& OutError)
{
	OutError = Msg;
	TSharedPtr<FJsonObject> Fail = MakeShareable(new FJsonObject);
	Fail->SetBoolField(TEXT("success"), false);
	Fail->SetStringField(TEXT("error"), Msg);
	BuildSuccessJson(Fail, OutJsonString);
}

static UBehaviorTreeGraphNode* FindBTNodeByName(UBehaviorTreeGraph* BTGraph, const FString& NodeName)
{
	if (!BTGraph) return nullptr;

	bool bWantRoot = NodeName.IsEmpty() || NodeName.Equals(TEXT("Root"), ESearchCase::IgnoreCase);

	for (UEdGraphNode* RawNode : BTGraph->Nodes)
	{
		if (bWantRoot)
		{
			if (UBehaviorTreeGraphNode_Root* Root = Cast<UBehaviorTreeGraphNode_Root>(RawNode))
			{
				return Root;
			}
		}
		else
		{
			if (UBehaviorTreeGraphNode* BTNode = Cast<UBehaviorTreeGraphNode>(RawNode))
			{
				if (BTNode->NodeInstance && BTNode->NodeInstance->GetName() == NodeName)
				{
					return BTNode;
				}
			}
		}
	}
	return nullptr;
}

static void AddBlackboardKeyInternal(UBlackboardData* BB, const FString& KeyName, const FString& KeyType, const FString& ClassName, FString& OutError)
{
	if (!BB) { OutError = TEXT("Invalid BlackboardData"); return; }
	if (KeyName.IsEmpty()) { OutError = TEXT("key_name is required"); return; }

	FBlackboardEntry Entry;
	Entry.EntryName = FName(*KeyName);

	FString TypeLower = KeyType.ToLower();

	if (TypeLower == TEXT("object") || TypeLower == TEXT("actor"))
	{
		UBlackboardKeyType_Object* ObjKey = NewObject<UBlackboardKeyType_Object>(BB);
		if (!ClassName.IsEmpty())
		{
			UClass* BaseClass = FindObject<UClass>(nullptr, *FString::Printf(TEXT("/Script/Engine.%s"), *ClassName));
			if (!BaseClass) BaseClass = FindObject<UClass>(nullptr, *ClassName);
			if (BaseClass) ObjKey->BaseClass = BaseClass;
		}
		if (!ObjKey->BaseClass)
		{
			ObjKey->BaseClass = AActor::StaticClass();
		}
		Entry.KeyType = ObjKey;
	}
	else if (TypeLower == TEXT("vector"))
	{
		Entry.KeyType = NewObject<UBlackboardKeyType_Vector>(BB);
	}
	else if (TypeLower == TEXT("bool") || TypeLower == TEXT("boolean"))
	{
		Entry.KeyType = NewObject<UBlackboardKeyType_Bool>(BB);
	}
	else if (TypeLower == TEXT("float"))
	{
		Entry.KeyType = NewObject<UBlackboardKeyType_Float>(BB);
	}
	else if (TypeLower == TEXT("int") || TypeLower == TEXT("integer"))
	{
		Entry.KeyType = NewObject<UBlackboardKeyType_Int>(BB);
	}
	else if (TypeLower == TEXT("string"))
	{
		Entry.KeyType = NewObject<UBlackboardKeyType_String>(BB);
	}
	else if (TypeLower == TEXT("name"))
	{
		Entry.KeyType = NewObject<UBlackboardKeyType_Name>(BB);
	}
	else if (TypeLower == TEXT("rotator"))
	{
		Entry.KeyType = NewObject<UBlackboardKeyType_Rotator>(BB);
	}
	else if (TypeLower == TEXT("enum"))
	{
		UBlackboardKeyType_Enum* EnumKey = NewObject<UBlackboardKeyType_Enum>(BB);
		if (!ClassName.IsEmpty())
		{
			UEnum* EnumAsset = FindObject<UEnum>(nullptr, *ClassName);
			if (!EnumAsset)
			{
				for (TObjectIterator<UEnum> It; It; ++It)
				{
					if (It->GetName() == ClassName || It->GetFName().ToString() == ClassName)
					{
						EnumAsset = *It;
						break;
					}
				}
			}
			if (EnumAsset)
			{
				EnumKey->EnumType = EnumAsset;
			}
		}
		Entry.KeyType = EnumKey;
	}
	else
	{
		OutError = FString::Printf(TEXT("Unknown key type '%s'. Valid types: Object, Vector, Bool, Float, Int, String, Name, Rotator, Enum"), *KeyType);
		return;
	}

	BB->Keys.Add(Entry);
}

static UClass* ResolveBlueprintClass(const FString& ClassPath)
{
	if (ClassPath.IsEmpty()) return nullptr;

	UClass* Found = FindObject<UClass>(nullptr, *(ClassPath + TEXT("_C")));
	if (!Found) Found = FindObject<UClass>(nullptr, *ClassPath);

	if (!Found)
	{
		if (UBlueprint* BP = LoadObject<UBlueprint>(nullptr, *ClassPath))
		{
			Found = BP->GeneratedClass;
		}
	}

	if (!Found)
	{
		FAssetRegistryModule& Reg = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		TArray<FAssetData> Assets;
		Reg.Get().GetAllAssets(Assets);
		for (const FAssetData& D : Assets)
		{
			if (D.AssetName.ToString() == ClassPath || D.AssetName.ToString() == FPaths::GetBaseFilename(ClassPath))
			{
				if (UBlueprint* BP = Cast<UBlueprint>(D.GetAsset()))
				{
					Found = BP->GeneratedClass;
					break;
				}
			}
		}
	}

	return Found;
}

void HandleCreateBehaviorTree(const FString& Name, const FString& SavePath, const FString& BlackboardPath, FString& OutJsonString, FString& OutError)
{

	if (Name.IsEmpty()) { SetError(TEXT("name is required"), OutJsonString, OutError); return; }

	FString TargetSavePath = SavePath.IsEmpty() ? TEXT("/Game") : SavePath;

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));

	UClass* FactoryClass = FindObject<UClass>(nullptr, TEXT("/Script/BehaviorTreeEditor.BehaviorTreeFactory"));
	UFactory* Factory = FactoryClass ? NewObject<UFactory>(GetTransientPackage(), FactoryClass) : nullptr;

	UObject* NewAsset = AssetToolsModule.Get().CreateAsset(Name, TargetSavePath, UBehaviorTree::StaticClass(), Factory);
	if (!NewAsset)
	{
		SetError(FString::Printf(TEXT("Failed to create BehaviorTree '%s' in '%s'"), *Name, *TargetSavePath), OutJsonString, OutError);
		return;
	}

	UBehaviorTree* BT = Cast<UBehaviorTree>(NewAsset);

	if (!BT->BTGraph)
	{

		UBehaviorTreeGraph* NewGraph = NewObject<UBehaviorTreeGraph>(BT, UBehaviorTreeGraph::StaticClass(), NAME_None, RF_Transactional);
		BT->BTGraph = NewGraph;

		UBehaviorTreeGraphNode_Root* RootNode = NewObject<UBehaviorTreeGraphNode_Root>(NewGraph, NAME_None, RF_Transactional);
		RootNode->NodePosX = 0;
		RootNode->NodePosY = 0;
		RootNode->NodeGuid = FGuid::NewGuid();
		NewGraph->Nodes.Add(RootNode);
		RootNode->PostPlacedNewNode();
		RootNode->AllocateDefaultPins();

		NewGraph->UpdateAsset();
	}

	if (!BlackboardPath.IsEmpty())
	{
		UBlackboardData* BB = Cast<UBlackboardData>(UEditorAssetLibrary::LoadAsset(BlackboardPath));
		if (BB)
		{
			BT->BlackboardAsset = BB;
		}
	}

	BT->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), BT->GetPathName());
	Result->SetStringField(TEXT("message"), FString::Printf(TEXT("Created BehaviorTree '%s'. Add composites with add_bt_composite, then tasks with add_bt_task_node."), *Name));
	BuildSuccessJson(Result, OutJsonString);
}

void HandleCreateBlackboard(const FString& Name, const FString& SavePath, const TArray<TSharedPtr<FJsonValue>>& Keys, FString& OutJsonString, FString& OutError)
{

	if (Name.IsEmpty()) { SetError(TEXT("name is required"), OutJsonString, OutError); return; }

	FString TargetSavePath = SavePath.IsEmpty() ? TEXT("/Game") : SavePath;

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));

	UClass* FactoryClass = FindObject<UClass>(nullptr, TEXT("/Script/BehaviorTreeEditor.BlackboardDataFactory"));
	UFactory* Factory = FactoryClass ? NewObject<UFactory>(GetTransientPackage(), FactoryClass) : nullptr;

	UObject* NewAsset = AssetToolsModule.Get().CreateAsset(Name, TargetSavePath, UBlackboardData::StaticClass(), Factory);
	if (!NewAsset)
	{
		SetError(FString::Printf(TEXT("Failed to create Blackboard '%s'"), *Name), OutJsonString, OutError);
		return;
	}

	UBlackboardData* BB = Cast<UBlackboardData>(NewAsset);

	TArray<FString> AddedKeys;
	for (const TSharedPtr<FJsonValue>& KeyVal : Keys)
	{
		const TSharedPtr<FJsonObject>* KeyObjPtr = nullptr;
		if (!KeyVal->TryGetObject(KeyObjPtr) || !KeyObjPtr) continue;

		FString KeyName, KeyType, ClassName;
		(*KeyObjPtr)->TryGetStringField(TEXT("name"), KeyName);
		(*KeyObjPtr)->TryGetStringField(TEXT("type"), KeyType);
		(*KeyObjPtr)->TryGetStringField(TEXT("class_name"), ClassName);

		FString KeyError;
		AddBlackboardKeyInternal(BB, KeyName, KeyType, ClassName, KeyError);
		if (KeyError.IsEmpty())
		{
			AddedKeys.Add(KeyName);
		}
	}

	BB->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), BB->GetPathName());
	Result->SetNumberField(TEXT("keys_added"), AddedKeys.Num());
	BuildSuccessJson(Result, OutJsonString);
}

void HandleAddBlackboardKey(const FString& BBPath, const FString& KeyName, const FString& KeyType, const FString& ClassName, FString& OutJsonString, FString& OutError)
{

	UBlackboardData* BB = Cast<UBlackboardData>(UEditorAssetLibrary::LoadAsset(BBPath));
	if (!BB) { SetError(FString::Printf(TEXT("Could not load BlackboardData at '%s'"), *BBPath), OutJsonString, OutError); return; }

	const FScopedTransaction Transaction(FText::FromString(TEXT("MCP: Add Blackboard Key")));
	BB->Modify();

	AddBlackboardKeyInternal(BB, KeyName, KeyType, ClassName, OutError);
	if (!OutError.IsEmpty())
	{
		TSharedPtr<FJsonObject> Fail = MakeShareable(new FJsonObject);
		Fail->SetBoolField(TEXT("success"), false);
		Fail->SetStringField(TEXT("error"), OutError);
		BuildSuccessJson(Fail, OutJsonString);
		return;
	}

	BB->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("message"), FString::Printf(TEXT("Added key '%s' (%s) to '%s'"), *KeyName, *KeyType, *BBPath));
	BuildSuccessJson(Result, OutJsonString);
}

static UBehaviorTree* LoadBehaviorTree(const FString& BTPath)
{
	UBehaviorTree* BT = Cast<UBehaviorTree>(UEditorAssetLibrary::LoadAsset(BTPath));
	if (BT) return BT;

	FString PackagePath = BTPath;
	int32 DotIdx;
	if (PackagePath.FindChar('.', DotIdx))
	{
		PackagePath = PackagePath.Left(DotIdx);
	}
	if (PackagePath != BTPath)
	{
		BT = Cast<UBehaviorTree>(UEditorAssetLibrary::LoadAsset(PackagePath));
		if (BT) return BT;
	}

	BT = LoadObject<UBehaviorTree>(nullptr, *BTPath);
	return BT;
}

void HandleAddBTComposite(const FString& BTPath, const FString& CompositeType, const FString& ParentNodeName, int32 PosX, int32 PosY, FString& OutJsonString, FString& OutError)
{

	UBehaviorTree* BT = LoadBehaviorTree(BTPath);
	if (!BT) { SetError(FString::Printf(TEXT("Could not load BehaviorTree at '%s'"), *BTPath), OutJsonString, OutError); return; }

	UBehaviorTreeGraph* BTGraph = Cast<UBehaviorTreeGraph>(BT->BTGraph);
	if (!BTGraph) { SetError(TEXT("BehaviorTree has no graph. Make sure you created it with create_behavior_tree."), OutJsonString, OutError); return; }

	bool bIsSequence        = CompositeType.Equals(TEXT("Sequence"),       ESearchCase::IgnoreCase);
	bool bIsSelector        = CompositeType.Equals(TEXT("Selector"),       ESearchCase::IgnoreCase);
	bool bIsSimpleParallel  = CompositeType.Equals(TEXT("SimpleParallel"), ESearchCase::IgnoreCase);
	if (!bIsSequence && !bIsSelector && !bIsSimpleParallel)
	{
		SetError(FString::Printf(TEXT("Invalid composite_type '%s'. Use 'Sequence', 'Selector', or 'SimpleParallel'."), *CompositeType), OutJsonString, OutError);
		return;
	}

	int32 CompositeCount = 0;
	for (UEdGraphNode* RawNode : BTGraph->Nodes)
	{
		if (UBehaviorTreeGraphNode* N = Cast<UBehaviorTreeGraphNode>(RawNode))
		{
			if (N->NodeInstance && (N->NodeInstance->IsA<UBTComposite_Sequence>() ||
			                        N->NodeInstance->IsA<UBTComposite_Selector>()  ||
			                        N->NodeInstance->IsA<UBTComposite_SimpleParallel>()))
			{
				CompositeCount++;
			}
		}
	}
	FString NodeName = FString::Printf(TEXT("%s_%d"), *CompositeType, CompositeCount);

	const FScopedTransaction Transaction(FText::FromString(TEXT("MCP: Add BT Composite")));
	BTGraph->Modify();

	UBTCompositeNode* CompositeInstance = nullptr;
	if (bIsSequence)
	{
		CompositeInstance = NewObject<UBTComposite_Sequence>(BT, NAME_None, RF_Transactional);
	}
	else if (bIsSelector)
	{
		CompositeInstance = NewObject<UBTComposite_Selector>(BT, NAME_None, RF_Transactional);
	}
	else
	{
		CompositeInstance = NewObject<UBTComposite_SimpleParallel>(BT, NAME_None, RF_Transactional);
	}
	CompositeInstance->Rename(*NodeName, nullptr, REN_DontCreateRedirectors);

	UBehaviorTreeGraphNode_Composite* NewNode = NewObject<UBehaviorTreeGraphNode_Composite>(BTGraph, NAME_None, RF_Transactional);
	NewNode->NodeInstance = CompositeInstance;
	NewNode->NodePosX = PosX;
	NewNode->NodePosY = PosY;
	NewNode->NodeGuid = FGuid::NewGuid();
	BTGraph->Nodes.Add(NewNode);
	NewNode->PostPlacedNewNode();
	NewNode->AllocateDefaultPins();

	UBehaviorTreeGraphNode* ParentNode = FindBTNodeByName(BTGraph, ParentNodeName);
	if (!ParentNodeName.IsEmpty() && !ParentNode)
	{
		BTGraph->Nodes.Remove(NewNode);
		SetError(FString::Printf(TEXT("Parent node '%s' not found. Use get_bt_graph_nodes to see actual node names, then use the exact returned node_name."), *ParentNodeName), OutJsonString, OutError);
		return;
	}
	if (ParentNode)
	{
		UEdGraphPin* ParentOutputPin = nullptr;
		for (UEdGraphPin* Pin : ParentNode->Pins)
		{
			if (Pin && Pin->Direction == EGPD_Output) { ParentOutputPin = Pin; break; }
		}
		UEdGraphPin* NewInputPin = nullptr;
		for (UEdGraphPin* Pin : NewNode->Pins)
		{
			if (Pin && Pin->Direction == EGPD_Input) { NewInputPin = Pin; break; }
		}
		if (ParentOutputPin && NewInputPin)
		{
			ParentOutputPin->MakeLinkTo(NewInputPin);
		}
	}

	ApplyBTGraphLayout(BTGraph);
	BTGraph->UpdateAsset();
	BT->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("node_name"), NodeName);
	Result->SetStringField(TEXT("message"), FString::Printf(TEXT("Added %s '%s'. Reference this name when adding children or decorators/services."), *CompositeType, *NodeName));
	BuildSuccessJson(Result, OutJsonString);
}

void HandleAddBTTaskNode(const FString& BTPath, const FString& TaskClassPath, const FString& ParentNodeName, int32 PosX, int32 PosY, FString& OutJsonString, FString& OutError)
{

	UBehaviorTree* BT = LoadBehaviorTree(BTPath);
	if (!BT) { SetError(FString::Printf(TEXT("Could not load BehaviorTree at '%s'"), *BTPath), OutJsonString, OutError); return; }

	UBehaviorTreeGraph* BTGraph = Cast<UBehaviorTreeGraph>(BT->BTGraph);
	if (!BTGraph) { SetError(TEXT("BehaviorTree has no graph"), OutJsonString, OutError); return; }

	if (ParentNodeName.IsEmpty() || ParentNodeName.Equals(TEXT("Root"), ESearchCase::IgnoreCase))
	{
		SetError(TEXT("Tasks cannot attach directly to the BT root — they must be children of a composite (Sequence/Selector/SimpleParallel). Add a composite first via add_bt_composite, then pass that composite's returned node_name as parent_node_name."),
			OutJsonString, OutError);
		return;
	}

	UClass* TaskClass = ResolveBlueprintClass(TaskClassPath);

	if (!TaskClass && !TaskClassPath.IsEmpty())
	{
		FString ShortName = TaskClassPath;
		int32 DotIdx;
		if (ShortName.FindLastChar('.', DotIdx)) ShortName = ShortName.RightChop(DotIdx + 1);
		int32 SlashIdx;
		if (ShortName.FindLastChar('/', SlashIdx)) ShortName = ShortName.RightChop(SlashIdx + 1);
		TaskClass = FindObject<UClass>(nullptr, *FString::Printf(TEXT("/Script/AIModule.%s"), *ShortName));
		if (!TaskClass) TaskClass = FindObject<UClass>(nullptr, *FString::Printf(TEXT("/Script/AIModule.%s"), *TaskClassPath));
		if (!TaskClass) TaskClass = UBTTask_BlueprintBase::StaticClass();
	}

	if (!TaskClass) { SetError(FString::Printf(TEXT("Could not find task class '%s'"), *TaskClassPath), OutJsonString, OutError); return; }

	if (!TaskClass->IsChildOf(UBTTaskNode::StaticClass()))
	{
		SetError(FString::Printf(TEXT("Class '%s' is not a BTTask subclass"), *TaskClass->GetName()), OutJsonString, OutError);
		return;
	}

	if (UBlueprint* SourceBP = Cast<UBlueprint>(TaskClass->ClassGeneratedBy))
	{
		FKismetEditorUtilities::CompileBlueprint(SourceBP, EBlueprintCompileOptions::SkipGarbageCollection);
		TaskClass = SourceBP->GeneratedClass;
		if (!TaskClass) { SetError(TEXT("Task Blueprint failed to compile"), OutJsonString, OutError); return; }
	}

	int32 TaskCount = 0;
	for (UEdGraphNode* N : BTGraph->Nodes)
	{
		if (UBehaviorTreeGraphNode* BTNode = Cast<UBehaviorTreeGraphNode>(N))
		{
			if (BTNode->NodeInstance && BTNode->NodeInstance->IsA<UBTTaskNode>()) TaskCount++;
		}
	}
	FString NodeName = FString::Printf(TEXT("%s_%d"), *TaskClass->GetName(), TaskCount);

	const FScopedTransaction Transaction(FText::FromString(TEXT("MCP: Add BT Task Node")));
	BTGraph->Modify();

	UBTTaskNode* TaskInstance = NewObject<UBTTaskNode>(BT, TaskClass, NAME_None, RF_Transactional);
	TaskInstance->Rename(*NodeName, nullptr, REN_DontCreateRedirectors);

	UBehaviorTreeGraphNode_Task* NewNode = NewObject<UBehaviorTreeGraphNode_Task>(BTGraph, NAME_None, RF_Transactional);
	NewNode->NodeInstance = TaskInstance;
	NewNode->NodePosX = PosX;
	NewNode->NodePosY = PosY;
	NewNode->NodeGuid = FGuid::NewGuid();
	BTGraph->Nodes.Add(NewNode);
	NewNode->PostPlacedNewNode();
	NewNode->AllocateDefaultPins();

	UBehaviorTreeGraphNode* ParentNode = FindBTNodeByName(BTGraph, ParentNodeName);
	if (!ParentNodeName.IsEmpty() && !ParentNode)
	{
		BTGraph->Nodes.Remove(NewNode);
		SetError(FString::Printf(TEXT("Parent node '%s' not found. Use get_bt_graph_nodes to see actual node names, then use the exact returned node_name."), *ParentNodeName), OutJsonString, OutError);
		return;
	}
	if (ParentNode)
	{
		UEdGraphPin* ParentOutputPin = nullptr;
		for (UEdGraphPin* Pin : ParentNode->Pins)
		{
			if (Pin && Pin->Direction == EGPD_Output) { ParentOutputPin = Pin; break; }
		}
		UEdGraphPin* NewInputPin = nullptr;
		for (UEdGraphPin* Pin : NewNode->Pins)
		{
			if (Pin && Pin->Direction == EGPD_Input) { NewInputPin = Pin; break; }
		}
		if (ParentOutputPin && NewInputPin)
		{
			ParentOutputPin->MakeLinkTo(NewInputPin);
		}
	}

	ApplyBTGraphLayout(BTGraph);
	BTGraph->UpdateAsset();
	BT->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("node_name"), NodeName);
	Result->SetStringField(TEXT("task_class"), TaskClass->GetName());
	Result->SetStringField(TEXT("message"), FString::Printf(TEXT("Added task '%s' under '%s'"), *NodeName, *ParentNodeName));
	BuildSuccessJson(Result, OutJsonString);
}

void HandleAddBTService(const FString& BTPath, const FString& NodeName, const FString& ServiceClassPath, FString& OutJsonString, FString& OutError)
{

	UBehaviorTree* BT = LoadBehaviorTree(BTPath);
	if (!BT) { SetError(FString::Printf(TEXT("Could not load BehaviorTree at '%s'"), *BTPath), OutJsonString, OutError); return; }

	UBehaviorTreeGraph* BTGraph = Cast<UBehaviorTreeGraph>(BT->BTGraph);
	if (!BTGraph) { SetError(TEXT("BehaviorTree has no graph"), OutJsonString, OutError); return; }

	UBehaviorTreeGraphNode* TargetNode = FindBTNodeByName(BTGraph, NodeName);
	if (!TargetNode)
	{
		SetError(FString::Printf(TEXT("Could not find BT node '%s'"), *NodeName), OutJsonString, OutError);
		return;
	}

	UClass* ServiceClass = ResolveBlueprintClass(ServiceClassPath);
	if (!ServiceClass)
	{
		ServiceClass = FindObject<UClass>(nullptr, *FString::Printf(TEXT("/Script/AIModule.%s"), *ServiceClassPath));
	}
	if (!ServiceClass || !ServiceClass->IsChildOf(UBTService::StaticClass()))
	{
		SetError(FString::Printf(TEXT("Could not find a valid BTService class for '%s'"), *ServiceClassPath), OutJsonString, OutError);
		return;
	}

	if (UBlueprint* SourceBP = Cast<UBlueprint>(ServiceClass->ClassGeneratedBy))
	{
		FKismetEditorUtilities::CompileBlueprint(SourceBP, EBlueprintCompileOptions::SkipGarbageCollection);
		ServiceClass = SourceBP->GeneratedClass;
		if (!ServiceClass) { SetError(TEXT("Service Blueprint failed to compile"), OutJsonString, OutError); return; }
	}

	const FScopedTransaction Transaction(FText::FromString(TEXT("MCP: Add BT Service")));
	BTGraph->Modify();

	UBTService* ServiceInstance = NewObject<UBTService>(BT, ServiceClass, NAME_None, RF_Transactional);

	UBehaviorTreeGraphNode_Service* ServiceGraphNode = NewObject<UBehaviorTreeGraphNode_Service>(BTGraph, NAME_None, RF_Transactional);
	ServiceGraphNode->NodeInstance = ServiceInstance;
	ServiceGraphNode->UpdateNodeClassData();

	TargetNode->AddSubNode(ServiceGraphNode, BTGraph);
	BT->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("message"), FString::Printf(TEXT("Added service '%s' to node '%s'"), *ServiceClass->GetName(), *NodeName));
	BuildSuccessJson(Result, OutJsonString);
}

void HandleAddBTDecorator(const FString& BTPath, const FString& NodeName, const FString& DecoratorClassPath, FString& OutJsonString, FString& OutError)
{

	UBehaviorTree* BT = LoadBehaviorTree(BTPath);
	if (!BT) { SetError(FString::Printf(TEXT("Could not load BehaviorTree at '%s'"), *BTPath), OutJsonString, OutError); return; }

	UBehaviorTreeGraph* BTGraph = Cast<UBehaviorTreeGraph>(BT->BTGraph);
	if (!BTGraph) { SetError(TEXT("BehaviorTree has no graph"), OutJsonString, OutError); return; }

	UBehaviorTreeGraphNode* TargetNode = FindBTNodeByName(BTGraph, NodeName);
	if (!TargetNode)
	{
		SetError(FString::Printf(TEXT("Could not find BT node '%s'"), *NodeName), OutJsonString, OutError);
		return;
	}

	UClass* DecoratorClass = ResolveBlueprintClass(DecoratorClassPath);
	if (!DecoratorClass)
	{
		DecoratorClass = FindObject<UClass>(nullptr, *FString::Printf(TEXT("/Script/AIModule.%s"), *DecoratorClassPath));
	}
	if (!DecoratorClass || !DecoratorClass->IsChildOf(UBTDecorator::StaticClass()))
	{
		SetError(FString::Printf(TEXT("Could not find a valid BTDecorator class for '%s'"), *DecoratorClassPath), OutJsonString, OutError);
		return;
	}

	if (UBlueprint* SourceBP = Cast<UBlueprint>(DecoratorClass->ClassGeneratedBy))
	{
		FKismetEditorUtilities::CompileBlueprint(SourceBP, EBlueprintCompileOptions::SkipGarbageCollection);
		DecoratorClass = SourceBP->GeneratedClass;
		if (!DecoratorClass) { SetError(TEXT("Decorator Blueprint failed to compile"), OutJsonString, OutError); return; }
	}

	const FScopedTransaction Transaction(FText::FromString(TEXT("MCP: Add BT Decorator")));
	BTGraph->Modify();

	UBehaviorTreeGraphNode_Decorator* DecoratorGraphNode = NewObject<UBehaviorTreeGraphNode_Decorator>(BTGraph, NAME_None, RF_Transactional);
	UBTDecorator* DecoratorInstance = NewObject<UBTDecorator>(BT, DecoratorClass, NAME_None, RF_Transactional);
	DecoratorGraphNode->NodeInstance = DecoratorInstance;
	DecoratorGraphNode->UpdateNodeClassData();

	TargetNode->AddSubNode(DecoratorGraphNode, BTGraph);
	BT->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("message"), FString::Printf(TEXT("Added decorator '%s' to node '%s'"), *DecoratorClass->GetName(), *NodeName));
	BuildSuccessJson(Result, OutJsonString);
}

static bool WriteBTPropertyFromString(void* Container, FProperty* Prop,
	const FString& PropertyName, const FString& PropertyValue, const FString& NodeClassName, FString& OutError)
{
	if (FFloatProperty* FP = CastField<FFloatProperty>(Prop))
		FP->SetPropertyValue_InContainer(Container, FCString::Atof(*PropertyValue));
	else if (FDoubleProperty* DP = CastField<FDoubleProperty>(Prop))
		DP->SetPropertyValue_InContainer(Container, FCString::Atod(*PropertyValue));
	else if (FIntProperty* IP = CastField<FIntProperty>(Prop))
		IP->SetPropertyValue_InContainer(Container, FCString::Atoi(*PropertyValue));
	else if (FBoolProperty* BP2 = CastField<FBoolProperty>(Prop))
		BP2->SetPropertyValue_InContainer(Container, PropertyValue.Equals(TEXT("true"), ESearchCase::IgnoreCase) || PropertyValue == TEXT("1"));
	else if (FStrProperty* SP = CastField<FStrProperty>(Prop))
		SP->SetPropertyValue_InContainer(Container, PropertyValue);
	else if (FNameProperty* NP = CastField<FNameProperty>(Prop))
		NP->SetPropertyValue_InContainer(Container, FName(*PropertyValue));
	else if (FEnumProperty* EP = CastField<FEnumProperty>(Prop))
	{
		UEnum* Enum = EP->GetEnum();
		int64 EnumVal = INDEX_NONE;
		if (Enum)
		{
			if (PropertyValue.IsNumeric()) EnumVal = FCString::Atoi64(*PropertyValue);
			else
			{
				for (int32 i = 0; i < Enum->NumEnums() - 1; i++)
				{
					FString DisplayName = Enum->GetDisplayNameTextByIndex(i).ToString();
					FString CppName = Enum->GetNameStringByIndex(i);
					if (DisplayName.Equals(PropertyValue, ESearchCase::IgnoreCase) ||
						CppName.Equals(PropertyValue, ESearchCase::IgnoreCase) ||
						CppName.EndsWith(TEXT("::") + PropertyValue))
					{ EnumVal = Enum->GetValueByIndex(i); break; }
				}
			}
		}
		if (EnumVal == INDEX_NONE) { OutError = FString::Printf(TEXT("Enum value '%s' not found on property '%s'"), *PropertyValue, *PropertyName); return false; }
		void* ValuePtr = EP->ContainerPtrToValuePtr<void>(Container);
		EP->GetUnderlyingProperty()->SetIntPropertyValue(ValuePtr, EnumVal);
	}
	else if (FByteProperty* ByteP = CastField<FByteProperty>(Prop))
	{
		if (UEnum* ByteEnum = ByteP->Enum)
		{
			int64 EnumVal = INDEX_NONE;
			if (PropertyValue.IsNumeric()) EnumVal = FCString::Atoi64(*PropertyValue);
			else
			{
				for (int32 i = 0; i < ByteEnum->NumEnums() - 1; i++)
				{
					FString DisplayName = ByteEnum->GetDisplayNameTextByIndex(i).ToString();
					FString CppName = ByteEnum->GetNameStringByIndex(i);
					if (DisplayName.Equals(PropertyValue, ESearchCase::IgnoreCase) ||
						CppName.Equals(PropertyValue, ESearchCase::IgnoreCase) ||
						CppName.EndsWith(TEXT("::") + PropertyValue))
					{ EnumVal = ByteEnum->GetValueByIndex(i); break; }
				}
			}
			if (EnumVal == INDEX_NONE) { OutError = FString::Printf(TEXT("Enum value '%s' not found on property '%s'"), *PropertyValue, *PropertyName); return false; }
			ByteP->SetPropertyValue_InContainer(Container, (uint8)EnumVal);
		}
		else
		{
			ByteP->SetPropertyValue_InContainer(Container, (uint8)FCString::Atoi(*PropertyValue));
		}
	}
	else if (FStructProperty* StructP = CastField<FStructProperty>(Prop))
	{
		void* StructPtr = StructP->ContainerPtrToValuePtr<void>(Container);
		if (StructP->Struct->GetFName() == TEXT("BlackboardKeySelector"))
		{
			FNameProperty* KeyNameProp = FindFProperty<FNameProperty>(StructP->Struct, TEXT("SelectedKeyName"));
			if (KeyNameProp) KeyNameProp->SetPropertyValue_InContainer(StructPtr, FName(*PropertyValue));
			else { OutError = TEXT("Could not find SelectedKeyName on FBlackboardKeySelector"); return false; }
		}
		else
		{
			FFloatProperty* DefaultValProp = FindFProperty<FFloatProperty>(StructP->Struct, TEXT("DefaultValue"));
			if (DefaultValProp) DefaultValProp->SetPropertyValue_InContainer(StructPtr, FCString::Atof(*PropertyValue));
			else { OutError = FString::Printf(TEXT("Unsupported struct type '%s' for property '%s' on %s. For blackboard key selectors, use the property name directly with the BB key name as the value."), *StructP->Struct->GetName(), *PropertyName, *NodeClassName); return false; }
		}
	}
	else { OutError = FString::Printf(TEXT("Unsupported property type for '%s'"), *PropertyName); return false; }
	return true;
}

static bool SetPropertyOnBTInstance(UObject* Inst, const FString& PropName, const FString& Value, FString& OutError)
{
	if (!Inst) { OutError = TEXT("null instance"); return false; }
	FProperty* Prop = FindFProperty<FProperty>(Inst->GetClass(), *PropName);
	if (!Prop)
		for (TFieldIterator<FProperty> It(Inst->GetClass()); It; ++It)
			if (It->GetName().Equals(PropName, ESearchCase::IgnoreCase)) { Prop = *It; break; }
	if (!Prop) { OutError = FString::Printf(TEXT("Property '%s' not found on %s"), *PropName, *Inst->GetClass()->GetName()); return false; }
	return WriteBTPropertyFromString(Inst, Prop, PropName, Value, Inst->GetClass()->GetName(), OutError);
}

static bool SetLastBTSubNodeProperty(const FString& BTPath, const FString& NodeName, bool bDecorator,
	const FString& PropName, const FString& Value, FString& OutError)
{
	UBehaviorTree* BT = LoadBehaviorTree(BTPath);
	if (!BT) { OutError = FString::Printf(TEXT("Could not load BehaviorTree at '%s'"), *BTPath); return false; }
	UBehaviorTreeGraph* G = Cast<UBehaviorTreeGraph>(BT->BTGraph);
	if (!G) { OutError = TEXT("BehaviorTree has no graph"); return false; }
	UBehaviorTreeGraphNode* GN = FindBTNodeByName(G, NodeName);
	if (!GN) { OutError = FString::Printf(TEXT("node '%s' not found"), *NodeName); return false; }
	const auto& Arr = bDecorator ? GN->Decorators : GN->Services;
	if (Arr.Num() == 0 || !Arr.Last() || !Arr.Last()->NodeInstance)
	{ OutError = TEXT("no sub-node instance to set property on"); return false; }
	if (!SetPropertyOnBTInstance(Arr.Last()->NodeInstance, PropName, Value, OutError)) return false;
	GN->Modify();
	BT->MarkPackageDirty();
	return true;
}

void HandleSetBTNodeProperty(const FString& BTPath, const FString& NodeName,
	const FString& PropertyName, const FString& PropertyValue,
	FString& OutJsonString, FString& OutError)
{
	UBehaviorTree* BT = Cast<UBehaviorTree>(UEditorAssetLibrary::LoadAsset(BTPath));
	if (!BT) { OutError = TEXT("Could not load BehaviorTree: ") + BTPath; return; }

	UBehaviorTreeGraph* BTGraph = Cast<UBehaviorTreeGraph>(BT->BTGraph);
	if (!BTGraph) { OutError = TEXT("BehaviorTree has no graph"); return; }

	UBehaviorTreeGraphNode* GraphNode = FindBTNodeByName(BTGraph, NodeName);
	if (!GraphNode || !GraphNode->NodeInstance) { OutError = FString::Printf(TEXT("Node '%s' not found in BehaviorTree"), *NodeName); return; }

	UBTNode* NodeInstance = Cast<UBTNode>(GraphNode->NodeInstance.Get());
	UClass* NodeClass = NodeInstance->GetClass();

	UObject* TargetObj = NodeInstance;
	FProperty* Prop = FindFProperty<FProperty>(NodeClass, *PropertyName);
	if (!Prop)
		for (TFieldIterator<FProperty> It(NodeClass); It; ++It)
			if (It->GetName().Equals(PropertyName, ESearchCase::IgnoreCase)) { Prop = *It; break; }

	if (!Prop)
	{
		if (UBehaviorTreeGraphNode* BTGNode = Cast<UBehaviorTreeGraphNode>(GraphNode))
		{
			for (const TObjectPtr<UBehaviorTreeGraphNode>& DecGNode : BTGNode->Decorators)
			{
				if (!DecGNode || !DecGNode->NodeInstance) continue;
				UObject* DecInst = DecGNode->NodeInstance;
				FProperty* P = FindFProperty<FProperty>(DecInst->GetClass(), *PropertyName);
				if (!P)
					for (TFieldIterator<FProperty> It(DecInst->GetClass()); It; ++It)
						if (It->GetName().Equals(PropertyName, ESearchCase::IgnoreCase)) { P = *It; break; }
				if (P) { Prop = P; TargetObj = DecInst; break; }
			}
		}
	}
	if (!Prop)
	{
		if (UBehaviorTreeGraphNode* BTGNode = Cast<UBehaviorTreeGraphNode>(GraphNode))
		{
			for (const TObjectPtr<UBehaviorTreeGraphNode>& SvcGNode : BTGNode->Services)
			{
				if (!SvcGNode || !SvcGNode->NodeInstance) continue;
				UObject* SvcInst = SvcGNode->NodeInstance;
				FProperty* P = FindFProperty<FProperty>(SvcInst->GetClass(), *PropertyName);
				if (!P)
					for (TFieldIterator<FProperty> It(SvcInst->GetClass()); It; ++It)
						if (It->GetName().Equals(PropertyName, ESearchCase::IgnoreCase)) { P = *It; break; }
				if (P) { Prop = P; TargetObj = SvcInst; break; }
			}
		}
	}
	if (!Prop) { OutError = FString::Printf(TEXT("Property '%s' not found on node '%s' (%s), its decorators, or its services"), *PropertyName, *NodeName, *NodeClass->GetName()); return; }

	if (!WriteBTPropertyFromString(TargetObj, Prop, PropertyName, PropertyValue, NodeClass->GetName(), OutError))
		return;

	BTGraph->Modify();
	BTGraph->UpdateAsset();
	BT->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(BTPath, false);

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"bt_path\":\"%s\",\"node_name\":\"%s\",\"property\":\"%s\",\"value\":\"%s\"}"),
		*BTPath, *NodeName, *PropertyName, *PropertyValue);
}

void HandleGetBlackboardKeys(const FString& BBPath, FString& OutJsonString, FString& OutError)
{
	UBlackboardData* BB = Cast<UBlackboardData>(UEditorAssetLibrary::LoadAsset(BBPath));
	if (!BB) { SetError(FString::Printf(TEXT("Could not load BlackboardData at '%s'"), *BBPath), OutJsonString, OutError); return; }

	TArray<TSharedPtr<FJsonValue>> KeysArray;
	TSet<FName> SeenKeys;

	UBlackboardData* Current = BB;
	while (Current)
	{
		for (const FBlackboardEntry& Entry : Current->Keys)
		{
			if (SeenKeys.Contains(Entry.EntryName)) continue;
			SeenKeys.Add(Entry.EntryName);

			TSharedPtr<FJsonObject> KeyObj = MakeShareable(new FJsonObject);
			KeyObj->SetStringField(TEXT("name"), Entry.EntryName.ToString());

			FString TypeName = TEXT("Unknown");
			if (Entry.KeyType)
			{
				TypeName = Entry.KeyType->GetClass()->GetName();
				TypeName.RemoveFromStart(TEXT("BlackboardKeyType_"));
			}
			KeyObj->SetStringField(TEXT("type"), TypeName);
			KeyObj->SetBoolField(TEXT("inherited"), Current != BB);
			KeysArray.Add(MakeShareable(new FJsonValueObject(KeyObj)));
		}
		Current = Current->Parent;
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("blackboard_path"), BBPath);
	Result->SetNumberField(TEXT("key_count"), KeysArray.Num());
	Result->SetArrayField(TEXT("keys"), KeysArray);
	BuildSuccessJson(Result, OutJsonString);
}

void HandleGetBTNodeProperty(const FString& BTPath, const FString& NodeName,
	const FString& PropertyName, FString& OutJsonString, FString& OutError)
{
	UBehaviorTree* BT = Cast<UBehaviorTree>(UEditorAssetLibrary::LoadAsset(BTPath));
	if (!BT) { SetError(TEXT("Could not load BehaviorTree: ") + BTPath, OutJsonString, OutError); return; }

	UBehaviorTreeGraph* BTGraph = Cast<UBehaviorTreeGraph>(BT->BTGraph);
	if (!BTGraph) { SetError(TEXT("BehaviorTree has no graph"), OutJsonString, OutError); return; }

	UBehaviorTreeGraphNode* GraphNode = FindBTNodeByName(BTGraph, NodeName);
	if (!GraphNode || !GraphNode->NodeInstance) { SetError(FString::Printf(TEXT("Node '%s' not found"), *NodeName), OutJsonString, OutError); return; }

	UObject* TargetObj = GraphNode->NodeInstance;
	FProperty* Prop = FindFProperty<FProperty>(TargetObj->GetClass(), *PropertyName);
	if (!Prop)
		for (TFieldIterator<FProperty> It(TargetObj->GetClass()); It; ++It)
			if (It->GetName().Equals(PropertyName, ESearchCase::IgnoreCase)) { Prop = *It; break; }

	if (!Prop)
	{
		for (const TObjectPtr<UBehaviorTreeGraphNode>& DecGNode : GraphNode->Decorators)
		{
			if (!DecGNode || !DecGNode->NodeInstance) continue;
			FProperty* P = FindFProperty<FProperty>(DecGNode->NodeInstance->GetClass(), *PropertyName);
			if (!P)
				for (TFieldIterator<FProperty> It(DecGNode->NodeInstance->GetClass()); It; ++It)
					if (It->GetName().Equals(PropertyName, ESearchCase::IgnoreCase)) { P = *It; break; }
			if (P) { Prop = P; TargetObj = DecGNode->NodeInstance; break; }
		}
	}
	if (!Prop)
	{
		for (const TObjectPtr<UBehaviorTreeGraphNode>& SvcGNode : GraphNode->Services)
		{
			if (!SvcGNode || !SvcGNode->NodeInstance) continue;
			FProperty* P = FindFProperty<FProperty>(SvcGNode->NodeInstance->GetClass(), *PropertyName);
			if (!P)
				for (TFieldIterator<FProperty> It(SvcGNode->NodeInstance->GetClass()); It; ++It)
					if (It->GetName().Equals(PropertyName, ESearchCase::IgnoreCase)) { P = *It; break; }
			if (P) { Prop = P; TargetObj = SvcGNode->NodeInstance; break; }
		}
	}

	if (!Prop) { SetError(FString::Printf(TEXT("Property '%s' not found on node '%s', its decorators, or its services"), *PropertyName, *NodeName), OutJsonString, OutError); return; }

	void* Container = TargetObj;
	FString ValueString;
	if (FFloatProperty* FP = CastField<FFloatProperty>(Prop))
		ValueString = FString::SanitizeFloat(FP->GetPropertyValue_InContainer(Container));
	else if (FDoubleProperty* DP = CastField<FDoubleProperty>(Prop))
		ValueString = FString::SanitizeFloat((float)DP->GetPropertyValue_InContainer(Container));
	else if (FIntProperty* IP = CastField<FIntProperty>(Prop))
		ValueString = FString::FromInt(IP->GetPropertyValue_InContainer(Container));
	else if (FBoolProperty* BP2 = CastField<FBoolProperty>(Prop))
		ValueString = BP2->GetPropertyValue_InContainer(Container) ? TEXT("true") : TEXT("false");
	else if (FStrProperty* SP = CastField<FStrProperty>(Prop))
		ValueString = SP->GetPropertyValue_InContainer(Container);
	else if (FNameProperty* NP = CastField<FNameProperty>(Prop))
		ValueString = NP->GetPropertyValue_InContainer(Container).ToString();
	else if (FStructProperty* StructP = CastField<FStructProperty>(Prop))
	{
		void* StructPtr = StructP->ContainerPtrToValuePtr<void>(Container);
		if (StructP->Struct->GetFName() == TEXT("BlackboardKeySelector"))
		{
			FNameProperty* KeyNameProp = FindFProperty<FNameProperty>(StructP->Struct, TEXT("SelectedKeyName"));
			ValueString = KeyNameProp ? KeyNameProp->GetPropertyValue_InContainer(StructPtr).ToString() : TEXT("[unresolved]");
		}
		else
		{
			FFloatProperty* DefaultValProp = FindFProperty<FFloatProperty>(StructP->Struct, TEXT("DefaultValue"));
			ValueString = DefaultValProp ? FString::SanitizeFloat(DefaultValProp->GetPropertyValue_InContainer(StructPtr)) : TEXT("[complex struct]");
		}
	}
	else
		ValueString = TEXT("[unsupported type]");

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("node_name"), NodeName);
	Result->SetStringField(TEXT("property_name"), PropertyName);
	Result->SetStringField(TEXT("property_type"), Prop->GetCPPType());
	Result->SetStringField(TEXT("value"), ValueString);
	Result->SetStringField(TEXT("found_on"), TargetObj->GetClass()->GetName());
	BuildSuccessJson(Result, OutJsonString);
}

void HandleRemoveBTNode(const FString& BTPath, const FString& NodeName, FString& OutJsonString, FString& OutError)
{
	UBehaviorTree* BT = LoadBehaviorTree(BTPath);
	if (!BT) { SetError(FString::Printf(TEXT("Could not load BehaviorTree at '%s'"), *BTPath), OutJsonString, OutError); return; }

	UBehaviorTreeGraph* BTGraph = Cast<UBehaviorTreeGraph>(BT->BTGraph);
	if (!BTGraph) { SetError(TEXT("BehaviorTree has no graph"), OutJsonString, OutError); return; }

	UBehaviorTreeGraphNode* NodeToRemove = FindBTNodeByName(BTGraph, NodeName);
	if (!NodeToRemove) { SetError(FString::Printf(TEXT("Node '%s' not found in BehaviorTree"), *NodeName), OutJsonString, OutError); return; }

	if (NodeToRemove->IsA<UBehaviorTreeGraphNode_Root>())
	{
		SetError(TEXT("Cannot remove the Root node"), OutJsonString, OutError);
		return;
	}

	const FScopedTransaction Transaction(FText::FromString(TEXT("MCP: Remove BT Node")));
	BTGraph->Modify();
	NodeToRemove->Modify();

	for (UEdGraphPin* Pin : NodeToRemove->Pins)
	{
		if (Pin) Pin->BreakAllPinLinks();
	}

	BTGraph->Nodes.Remove(NodeToRemove);
	ApplyBTGraphLayout(BTGraph);
	BTGraph->UpdateAsset();
	BT->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("message"), FString::Printf(TEXT("Removed node '%s' from BehaviorTree. Any children it had are now disconnected."), *NodeName));
	BuildSuccessJson(Result, OutJsonString);
}

void HandleRemoveBTDecorator(const FString& BTPath, const FString& NodeName, const FString& DecoratorClass, FString& OutJsonString, FString& OutError)
{
	UBehaviorTree* BT = LoadBehaviorTree(BTPath);
	if (!BT) { SetError(FString::Printf(TEXT("Could not load BehaviorTree at '%s'"), *BTPath), OutJsonString, OutError); return; }

	UBehaviorTreeGraph* BTGraph = Cast<UBehaviorTreeGraph>(BT->BTGraph);
	if (!BTGraph) { SetError(TEXT("BehaviorTree has no graph"), OutJsonString, OutError); return; }

	UBehaviorTreeGraphNode* GraphNode = FindBTNodeByName(BTGraph, NodeName);
	if (!GraphNode) { SetError(FString::Printf(TEXT("Node '%s' not found"), *NodeName), OutJsonString, OutError); return; }

	int32 RemoveIdx = INDEX_NONE;
	FString RemovedClass;
	for (int32 i = 0; i < GraphNode->Decorators.Num(); i++)
	{
		UBehaviorTreeGraphNode* DecGNode = GraphNode->Decorators[i];
		if (!DecGNode) continue;
		if (DecGNode->NodeInstance)
		{
			FString ClassName = DecGNode->NodeInstance->GetClass()->GetName();
			if (ClassName.Equals(DecoratorClass, ESearchCase::IgnoreCase) ||
				ClassName.Contains(DecoratorClass) ||
				DecGNode->NodeInstance->GetName().Equals(DecoratorClass, ESearchCase::IgnoreCase))
			{
				RemoveIdx = i;
				RemovedClass = ClassName;
				break;
			}
		}
		else
		{
			FString StoredName = DecGNode->ClassData.GetClassName();
			StoredName.RemoveFromEnd(TEXT("_C"));
			if (!StoredName.IsEmpty() &&
				(StoredName.Equals(DecoratorClass, ESearchCase::IgnoreCase) || StoredName.Contains(DecoratorClass)))
			{
				RemoveIdx = i;
				RemovedClass = StoredName + TEXT(" (unresolved)");
				break;
			}
		}
	}

	if (RemoveIdx == INDEX_NONE)
	{
		SetError(FString::Printf(TEXT("Decorator matching '%s' not found on node '%s'. Check get_behavior_tree_summary for actual class names."), *DecoratorClass, *NodeName), OutJsonString, OutError);
		return;
	}

	const FScopedTransaction Transaction(FText::FromString(TEXT("MCP: Remove BT Decorator")));
	BTGraph->Modify();
	GraphNode->Modify();
	GraphNode->RemoveSubNode(GraphNode->Decorators[RemoveIdx]);
	BTGraph->UpdateAsset();
	BT->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("message"), FString::Printf(TEXT("Removed decorator '%s' from node '%s'"), *RemovedClass, *NodeName));
	BuildSuccessJson(Result, OutJsonString);
}

void HandleRemoveBTService(const FString& BTPath, const FString& NodeName, const FString& ServiceClass, FString& OutJsonString, FString& OutError)
{
	UBehaviorTree* BT = LoadBehaviorTree(BTPath);
	if (!BT) { SetError(FString::Printf(TEXT("Could not load BehaviorTree at '%s'"), *BTPath), OutJsonString, OutError); return; }

	UBehaviorTreeGraph* BTGraph = Cast<UBehaviorTreeGraph>(BT->BTGraph);
	if (!BTGraph) { SetError(TEXT("BehaviorTree has no graph"), OutJsonString, OutError); return; }

	UBehaviorTreeGraphNode* GraphNode = FindBTNodeByName(BTGraph, NodeName);
	if (!GraphNode) { SetError(FString::Printf(TEXT("Node '%s' not found"), *NodeName), OutJsonString, OutError); return; }

	int32 RemoveIdx = INDEX_NONE;
	FString RemovedClass;
	for (int32 i = 0; i < GraphNode->Services.Num(); i++)
	{
		UBehaviorTreeGraphNode* SvcGNode = GraphNode->Services[i];
		if (!SvcGNode) continue;
		if (SvcGNode->NodeInstance)
		{
			FString ClassName = SvcGNode->NodeInstance->GetClass()->GetName();
			if (ClassName.Equals(ServiceClass, ESearchCase::IgnoreCase) ||
				ClassName.Contains(ServiceClass) ||
				SvcGNode->NodeInstance->GetName().Equals(ServiceClass, ESearchCase::IgnoreCase))
			{
				RemoveIdx = i;
				RemovedClass = ClassName;
				break;
			}
		}
		else
		{
			FString StoredName = SvcGNode->ClassData.GetClassName();
			StoredName.RemoveFromEnd(TEXT("_C"));
			if (!StoredName.IsEmpty() &&
				(StoredName.Equals(ServiceClass, ESearchCase::IgnoreCase) || StoredName.Contains(ServiceClass)))
			{
				RemoveIdx = i;
				RemovedClass = StoredName + TEXT(" (unresolved)");
				break;
			}
		}
	}

	if (RemoveIdx == INDEX_NONE)
	{
		SetError(FString::Printf(TEXT("Service matching '%s' not found on node '%s'. Check get_behavior_tree_summary for actual class names."), *ServiceClass, *NodeName), OutJsonString, OutError);
		return;
	}

	const FScopedTransaction Transaction(FText::FromString(TEXT("MCP: Remove BT Service")));
	BTGraph->Modify();
	GraphNode->Modify();
	GraphNode->RemoveSubNode(GraphNode->Services[RemoveIdx]);
	BTGraph->UpdateAsset();
	BT->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("message"), FString::Printf(TEXT("Removed service '%s' from node '%s'"), *RemovedClass, *NodeName));
	BuildSuccessJson(Result, OutJsonString);
}

void HandleReorderBTChildren(const FString& BTPath, const FString& ParentNodeName, const TArray<FString>& OrderedChildNames, FString& OutJsonString, FString& OutError)
{
	UBehaviorTree* BT = LoadBehaviorTree(BTPath);
	if (!BT) { SetError(FString::Printf(TEXT("Could not load BehaviorTree at '%s'"), *BTPath), OutJsonString, OutError); return; }

	UBehaviorTreeGraph* BTGraph = Cast<UBehaviorTreeGraph>(BT->BTGraph);
	if (!BTGraph) { SetError(TEXT("BehaviorTree has no graph"), OutJsonString, OutError); return; }

	UBehaviorTreeGraphNode* ParentNode = FindBTNodeByName(BTGraph, ParentNodeName);
	if (!ParentNode) { SetError(FString::Printf(TEXT("Parent node '%s' not found"), *ParentNodeName), OutJsonString, OutError); return; }

	if (OrderedChildNames.IsEmpty()) { SetError(TEXT("ordered_child_names cannot be empty"), OutJsonString, OutError); return; }

	UEdGraphPin* OutputPin = nullptr;
	for (UEdGraphPin* Pin : ParentNode->Pins)
	{
		if (Pin && Pin->Direction == EGPD_Output) { OutputPin = Pin; break; }
	}

	if (!OutputPin || OutputPin->LinkedTo.IsEmpty())
	{
		SetError(FString::Printf(TEXT("Parent node '%s' has no connected children"), *ParentNodeName), OutJsonString, OutError);
		return;
	}

	TMap<FString, UBehaviorTreeGraphNode*> ChildMap;
	int32 ChildY = 400;
	for (UEdGraphPin* LinkedPin : OutputPin->LinkedTo)
	{
		if (!LinkedPin) continue;
		UBehaviorTreeGraphNode* Child = Cast<UBehaviorTreeGraphNode>(LinkedPin->GetOwningNode());
		if (Child && Child->NodeInstance)
		{
			ChildMap.Add(Child->NodeInstance->GetName(), Child);
			ChildY = Child->NodePosY;
		}
	}

	for (const FString& ChildName : OrderedChildNames)
	{
		if (!ChildMap.Contains(ChildName))
		{
			SetError(FString::Printf(TEXT("Child node '%s' is not connected under '%s'. Use get_behavior_tree_summary to see actual node names."), *ChildName, *ParentNodeName), OutJsonString, OutError);
			return;
		}
	}

	const FScopedTransaction Transaction(FText::FromString(TEXT("MCP: Reorder BT Children")));
	BTGraph->Modify();
	ParentNode->Modify();

	const int32 XSpacing = 300;
	const int32 StartX = ParentNode->NodePosX - (XSpacing * (OrderedChildNames.Num() - 1) / 2);
	for (int32 i = 0; i < OrderedChildNames.Num(); i++)
	{
		UBehaviorTreeGraphNode* Child = ChildMap[OrderedChildNames[i]];
		Child->Modify();
		Child->NodePosX = StartX + i * XSpacing;
		Child->NodePosY = ChildY;
	}

	{
		TArray<UEdGraphPin*> NewLinked;
		for (const FString& ChildName : OrderedChildNames)
		{
			UBehaviorTreeGraphNode* Child = ChildMap[ChildName];
			for (UEdGraphPin* P : Child->Pins)
			{
				if (P && P->Direction == EGPD_Input) { NewLinked.Add(P); break; }
			}
		}
		for (UEdGraphPin* P : OutputPin->LinkedTo)
		{
			if (P && !NewLinked.Contains(P)) NewLinked.Add(P);
		}
		OutputPin->LinkedTo = NewLinked;
	}

	ApplyBTGraphLayout(BTGraph);
	BTGraph->UpdateAsset();
	BT->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("message"), FString::Printf(TEXT("Reordered %d children under '%s' (left=first executed, right=last)"), OrderedChildNames.Num(), *ParentNodeName));
	BuildSuccessJson(Result, OutJsonString);
}

void HandleRemoveBlackboardKey(const FString& BBPath, const FString& KeyName, FString& OutJsonString, FString& OutError)
{

	UBlackboardData* BB = Cast<UBlackboardData>(UEditorAssetLibrary::LoadAsset(BBPath));
	if (!BB) { SetError(FString::Printf(TEXT("Could not load BlackboardData at '%s'"), *BBPath), OutJsonString, OutError); return; }

	int32 RemoveIdx = INDEX_NONE;
	for (int32 i = 0; i < BB->Keys.Num(); i++)
	{
		if (BB->Keys[i].EntryName.ToString().Equals(KeyName, ESearchCase::IgnoreCase))
		{
			RemoveIdx = i;
			break;
		}
	}

	if (RemoveIdx == INDEX_NONE)
	{
		SetError(FString::Printf(TEXT("Key '%s' not found in BlackboardData '%s'"), *KeyName, *BBPath), OutJsonString, OutError);
		return;
	}

	const FScopedTransaction Transaction(FText::FromString(TEXT("MCP: Remove Blackboard Key")));
	BB->Modify();
	BB->Keys.RemoveAt(RemoveIdx);
	BB->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("message"), FString::Printf(TEXT("Removed key '%s' from '%s'"), *KeyName, *BBPath));
	BuildSuccessJson(Result, OutJsonString);
}

void HandleRenameBlackboardKey(const FString& BBPath, const FString& OldName, const FString& NewName, FString& OutJsonString, FString& OutError)
{

	if (NewName.IsEmpty()) { SetError(TEXT("new_key_name is required"), OutJsonString, OutError); return; }

	UBlackboardData* BB = Cast<UBlackboardData>(UEditorAssetLibrary::LoadAsset(BBPath));
	if (!BB) { SetError(FString::Printf(TEXT("Could not load BlackboardData at '%s'"), *BBPath), OutJsonString, OutError); return; }

	for (const FBlackboardEntry& Entry : BB->Keys)
	{
		if (Entry.EntryName.ToString().Equals(NewName, ESearchCase::IgnoreCase))
		{
			SetError(FString::Printf(TEXT("A key named '%s' already exists"), *NewName), OutJsonString, OutError);
			return;
		}
	}

	int32 FoundIdx = INDEX_NONE;
	for (int32 i = 0; i < BB->Keys.Num(); i++)
	{
		if (BB->Keys[i].EntryName.ToString().Equals(OldName, ESearchCase::IgnoreCase))
		{
			FoundIdx = i;
			break;
		}
	}

	if (FoundIdx == INDEX_NONE)
	{
		SetError(FString::Printf(TEXT("Key '%s' not found in BlackboardData '%s'"), *OldName, *BBPath), OutJsonString, OutError);
		return;
	}

	const FScopedTransaction Transaction(FText::FromString(TEXT("MCP: Rename Blackboard Key")));
	BB->Modify();
	BB->Keys[FoundIdx].EntryName = FName(*NewName);
	BB->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("message"), FString::Printf(TEXT("Renamed blackboard key '%s' -> '%s'"), *OldName, *NewName));
	BuildSuccessJson(Result, OutJsonString);
}

static TSharedPtr<FJsonObject> DumpNodeProperties(UObject* Obj)
{
	TSharedPtr<FJsonObject> PropsObj = MakeShareable(new FJsonObject);
	if (!Obj) return PropsObj;

	for (TFieldIterator<FProperty> It(Obj->GetClass()); It; ++It)
	{
		FProperty* Prop = *It;
		if (!Prop) continue;
		if (Prop->HasAnyPropertyFlags(CPF_NativeAccessSpecifierPrivate | CPF_NativeAccessSpecifierProtected)
			&& !Prop->HasAnyPropertyFlags(CPF_Edit | CPF_BlueprintVisible))
			continue;

		FString PropName = Prop->GetName();
		FString ValueStr;

		if (FFloatProperty* FP = CastField<FFloatProperty>(Prop))
			ValueStr = FString::SanitizeFloat(FP->GetPropertyValue_InContainer(Obj));
		else if (FDoubleProperty* DP = CastField<FDoubleProperty>(Prop))
			ValueStr = FString::SanitizeFloat((float)DP->GetPropertyValue_InContainer(Obj));
		else if (FIntProperty* IP = CastField<FIntProperty>(Prop))
			ValueStr = FString::FromInt(IP->GetPropertyValue_InContainer(Obj));
		else if (FBoolProperty* BP2 = CastField<FBoolProperty>(Prop))
			ValueStr = BP2->GetPropertyValue_InContainer(Obj) ? TEXT("true") : TEXT("false");
		else if (FStrProperty* SP = CastField<FStrProperty>(Prop))
			ValueStr = SP->GetPropertyValue_InContainer(Obj);
		else if (FNameProperty* NP = CastField<FNameProperty>(Prop))
			ValueStr = NP->GetPropertyValue_InContainer(Obj).ToString();
		else if (FStructProperty* StructP = CastField<FStructProperty>(Prop))
		{
			void* StructPtr = StructP->ContainerPtrToValuePtr<void>(Obj);
			if (StructP->Struct->GetFName() == TEXT("BlackboardKeySelector"))
			{
				FNameProperty* KeyNameProp = FindFProperty<FNameProperty>(StructP->Struct, TEXT("SelectedKeyName"));
				ValueStr = KeyNameProp ? FString::Printf(TEXT("[BBKey:%s]"), *KeyNameProp->GetPropertyValue_InContainer(StructPtr).ToString()) : TEXT("[BBKey:?]");
			}
			else
			{
				FFloatProperty* DefaultValProp = FindFProperty<FFloatProperty>(StructP->Struct, TEXT("DefaultValue"));
				ValueStr = DefaultValProp ? FString::SanitizeFloat(DefaultValProp->GetPropertyValue_InContainer(StructPtr)) : FString::Printf(TEXT("[struct:%s]"), *StructP->Struct->GetName());
			}
		}
		else if (FByteProperty* ByteP = CastField<FByteProperty>(Prop))
		{
			uint8 Val = ByteP->GetPropertyValue_InContainer(Obj);
			if (ByteP->Enum)
				ValueStr = ByteP->Enum->GetNameStringByValue(Val);
			else
				ValueStr = FString::FromInt(Val);
		}
		else if (FEnumProperty* EnumP = CastField<FEnumProperty>(Prop))
		{
			void* ValPtr = EnumP->ContainerPtrToValuePtr<void>(Obj);
			int64 RawVal = EnumP->GetUnderlyingProperty()->GetSignedIntPropertyValue(ValPtr);
			ValueStr = EnumP->GetEnum() ? EnumP->GetEnum()->GetNameStringByValue(RawVal) : FString::FromInt((int32)RawVal);
		}
		else
			continue;

		PropsObj->SetStringField(PropName, ValueStr);
	}
	return PropsObj;
}

void HandleGetBTNodeDetails(const FString& BTPath, const FString& NodeName, FString& OutJsonString, FString& OutError)
{

	UBehaviorTree* BT = LoadBehaviorTree(BTPath);
	if (!BT) { SetError(FString::Printf(TEXT("Could not load BehaviorTree at '%s'"), *BTPath), OutJsonString, OutError); return; }

	UBehaviorTreeGraph* BTGraph = Cast<UBehaviorTreeGraph>(BT->BTGraph);
	if (!BTGraph) { SetError(TEXT("BehaviorTree has no graph"), OutJsonString, OutError); return; }

	UBehaviorTreeGraphNode* GraphNode = FindBTNodeByName(BTGraph, NodeName);
	if (!GraphNode || !GraphNode->NodeInstance)
	{
		SetError(FString::Printf(TEXT("Node '%s' not found"), *NodeName), OutJsonString, OutError);
		return;
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("node_name"), NodeName);
	Result->SetStringField(TEXT("node_class"), GraphNode->NodeInstance->GetClass()->GetName());
	Result->SetObjectField(TEXT("properties"), DumpNodeProperties(GraphNode->NodeInstance));

	TArray<TSharedPtr<FJsonValue>> DecoratorsArray;
	for (const TObjectPtr<UBehaviorTreeGraphNode>& DecGNode : GraphNode->Decorators)
	{
		if (!DecGNode || !DecGNode->NodeInstance) continue;
		TSharedPtr<FJsonObject> DecObj = MakeShareable(new FJsonObject);
		DecObj->SetStringField(TEXT("class"), DecGNode->NodeInstance->GetClass()->GetName());
		DecObj->SetObjectField(TEXT("properties"), DumpNodeProperties(DecGNode->NodeInstance));
		DecoratorsArray.Add(MakeShareable(new FJsonValueObject(DecObj)));
	}
	Result->SetArrayField(TEXT("decorators"), DecoratorsArray);

	TArray<TSharedPtr<FJsonValue>> ServicesArray;
	for (const TObjectPtr<UBehaviorTreeGraphNode>& SvcGNode : GraphNode->Services)
	{
		if (!SvcGNode || !SvcGNode->NodeInstance) continue;
		TSharedPtr<FJsonObject> SvcObj = MakeShareable(new FJsonObject);
		SvcObj->SetStringField(TEXT("class"), SvcGNode->NodeInstance->GetClass()->GetName());
		SvcObj->SetObjectField(TEXT("properties"), DumpNodeProperties(SvcGNode->NodeInstance));
		ServicesArray.Add(MakeShareable(new FJsonValueObject(SvcObj)));
	}
	Result->SetArrayField(TEXT("services"), ServicesArray);

	BuildSuccessJson(Result, OutJsonString);
}

void HandleListBTNativeClasses(const FString& ClassType, FString& OutJsonString, FString& OutError)
{

	FString TypeLower = ClassType.ToLower();
	bool bTasks      = TypeLower == TEXT("task")      || TypeLower == TEXT("all") || TypeLower.IsEmpty();
	bool bServices   = TypeLower == TEXT("service")   || TypeLower == TEXT("all") || TypeLower.IsEmpty();
	bool bDecorators = TypeLower == TEXT("decorator") || TypeLower == TEXT("all") || TypeLower.IsEmpty();

	auto CollectClasses = [](UClass* BaseClass, TArray<TSharedPtr<FJsonValue>>& OutArray)
	{
		for (TObjectIterator<UClass> It; It; ++It)
		{
			UClass* C = *It;
			if (!C->IsChildOf(BaseClass)) continue;
			if (C == BaseClass) continue;
			if (C->HasAnyClassFlags(CLASS_Abstract)) continue;
			if (C->ClassGeneratedBy != nullptr) continue;
			TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
			Obj->SetStringField(TEXT("class_name"), C->GetName());
			Obj->SetStringField(TEXT("usage"), FString::Printf(TEXT("Pass \"%s\" as task_class_path/service_class_path/decorator_class_path"), *C->GetName()));
			OutArray.Add(MakeShareable(new FJsonValueObject(Obj)));
		}
	};

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);

	if (bTasks)
	{
		TArray<TSharedPtr<FJsonValue>> Tasks;
		CollectClasses(UBTTaskNode::StaticClass(), Tasks);
		Result->SetArrayField(TEXT("tasks"), Tasks);
	}
	if (bServices)
	{
		TArray<TSharedPtr<FJsonValue>> Services;
		CollectClasses(UBTService::StaticClass(), Services);
		Result->SetArrayField(TEXT("services"), Services);
	}
	if (bDecorators)
	{
		TArray<TSharedPtr<FJsonValue>> Decorators;
		CollectClasses(UBTDecorator::StaticClass(), Decorators);
		Result->SetArrayField(TEXT("decorators"), Decorators);
	}

	BuildSuccessJson(Result, OutJsonString);
}

void HandleMoveBTNode(const FString& BTPath, const FString& NodeName, int32 PosX, int32 PosY, FString& OutJsonString, FString& OutError)
{

	UBehaviorTree* BT = LoadBehaviorTree(BTPath);
	if (!BT) { SetError(FString::Printf(TEXT("Could not load BehaviorTree at '%s'"), *BTPath), OutJsonString, OutError); return; }

	UBehaviorTreeGraph* BTGraph = Cast<UBehaviorTreeGraph>(BT->BTGraph);
	if (!BTGraph) { SetError(TEXT("BehaviorTree has no graph"), OutJsonString, OutError); return; }

	UBehaviorTreeGraphNode* GraphNode = FindBTNodeByName(BTGraph, NodeName);
	if (!GraphNode) { SetError(FString::Printf(TEXT("Node '%s' not found"), *NodeName), OutJsonString, OutError); return; }

	const FScopedTransaction Transaction(FText::FromString(TEXT("MCP: Move BT Node")));
	BTGraph->Modify();
	GraphNode->Modify();
	GraphNode->NodePosX = PosX;
	GraphNode->NodePosY = PosY;

	BTGraph->UpdateAsset();
	BT->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("message"), FString::Printf(TEXT("Moved '%s' to (%d, %d)"), *NodeName, PosX, PosY));
	BuildSuccessJson(Result, OutJsonString);
}

void HandleSetBlackboardAsset(const FString& BTPath, const FString& BBPath, FString& OutJsonString, FString& OutError)
{

	UBehaviorTree* BT = LoadBehaviorTree(BTPath);
	if (!BT) { SetError(FString::Printf(TEXT("Could not load BehaviorTree at '%s'"), *BTPath), OutJsonString, OutError); return; }

	UBlackboardData* BB = Cast<UBlackboardData>(UEditorAssetLibrary::LoadAsset(BBPath));
	if (!BB) { SetError(FString::Printf(TEXT("Could not load BlackboardData at '%s'"), *BBPath), OutJsonString, OutError); return; }

	const FScopedTransaction Transaction(FText::FromString(TEXT("MCP: Set BT Blackboard Asset")));
	BT->Modify();
	BT->BlackboardAsset = BB;
	BT->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("message"), FString::Printf(TEXT("BehaviorTree '%s' now uses blackboard '%s'"), *BTPath, *BBPath));
	BuildSuccessJson(Result, OutJsonString);
}

void HandleGetBTGraphNodes(const FString& BTPath, FString& OutJsonString, FString& OutError)
{

	UBehaviorTree* BT = LoadBehaviorTree(BTPath);
	if (!BT) { SetError(FString::Printf(TEXT("Could not load BehaviorTree at '%s'"), *BTPath), OutJsonString, OutError); return; }

	UBehaviorTreeGraph* BTGraph = Cast<UBehaviorTreeGraph>(BT->BTGraph);
	if (!BTGraph) { SetError(TEXT("BehaviorTree has no graph"), OutJsonString, OutError); return; }

	TMap<UEdGraphNode*, UEdGraphNode*> ParentMap;
	for (UEdGraphNode* RawNode : BTGraph->Nodes)
	{
		for (UEdGraphPin* Pin : RawNode->Pins)
		{
			if (Pin && Pin->Direction == EGPD_Output)
			{
				for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
				{
					if (LinkedPin) ParentMap.Add(LinkedPin->GetOwningNode(), RawNode);
				}
			}
		}
	}

	TArray<TSharedPtr<FJsonValue>> NodesArray;
	for (UEdGraphNode* RawNode : BTGraph->Nodes)
	{
		UBehaviorTreeGraphNode* BTNode = Cast<UBehaviorTreeGraphNode>(RawNode);
		if (!BTNode) continue;

		TSharedPtr<FJsonObject> NodeObj = MakeShareable(new FJsonObject);

		FString NodeName, NodeClass, NodeType;
		if (Cast<UBehaviorTreeGraphNode_Root>(BTNode))
		{
			NodeName  = TEXT("Root");
			NodeClass = TEXT("BehaviorTreeGraphNode_Root");
			NodeType  = TEXT("Root");
		}
		else if (BTNode->NodeInstance)
		{
			NodeName  = BTNode->NodeInstance->GetName();
			NodeClass = BTNode->NodeInstance->GetClass()->GetName();
			if (BTNode->NodeInstance->IsA<UBTCompositeNode>())
				NodeType = TEXT("Composite");
			else if (BTNode->NodeInstance->IsA<UBTTaskNode>())
				NodeType = TEXT("Task");
			else
				NodeType = TEXT("Unknown");
		}
		else
		{
			NodeName  = BTNode->GetName();
			NodeClass = BTNode->GetClass()->GetName();
			NodeType  = TEXT("Unknown");
		}

		NodeObj->SetStringField(TEXT("name"), NodeName);
		NodeObj->SetStringField(TEXT("type"), NodeType);
		NodeObj->SetStringField(TEXT("class"), NodeClass);
		NodeObj->SetNumberField(TEXT("pos_x"), BTNode->NodePosX);
		NodeObj->SetNumberField(TEXT("pos_y"), BTNode->NodePosY);

		UEdGraphNode** ParentPtr = ParentMap.Find(RawNode);
		if (ParentPtr && *ParentPtr)
		{
			if (Cast<UBehaviorTreeGraphNode_Root>(*ParentPtr))
			{
				NodeObj->SetStringField(TEXT("parent"), TEXT("Root"));
			}
			else if (UBehaviorTreeGraphNode* P = Cast<UBehaviorTreeGraphNode>(*ParentPtr))
			{
				NodeObj->SetStringField(TEXT("parent"), P->NodeInstance ? P->NodeInstance->GetName() : TEXT(""));
			}
			else
			{
				NodeObj->SetStringField(TEXT("parent"), TEXT(""));
			}
		}
		else
		{
			NodeObj->SetStringField(TEXT("parent"), TEXT(""));
		}

		TArray<TSharedPtr<FJsonValue>> ChildrenArray;
		for (UEdGraphPin* Pin : BTNode->Pins)
		{
			if (Pin && Pin->Direction == EGPD_Output)
			{
				for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
				{
					if (!LinkedPin) continue;
					if (UBehaviorTreeGraphNode* Child = Cast<UBehaviorTreeGraphNode>(LinkedPin->GetOwningNode()))
					{
						FString ChildName = Child->NodeInstance ? Child->NodeInstance->GetName() : Child->GetName();
						ChildrenArray.Add(MakeShareable(new FJsonValueString(ChildName)));
					}
				}
			}
		}
		NodeObj->SetArrayField(TEXT("children"), ChildrenArray);

		TArray<TSharedPtr<FJsonValue>> DecsArray;
		for (const TObjectPtr<UBehaviorTreeGraphNode>& DecGNode : BTNode->Decorators)
		{
			if (!DecGNode || !DecGNode->NodeInstance) continue;
			TSharedPtr<FJsonObject> DecObj = MakeShareable(new FJsonObject);
			DecObj->SetStringField(TEXT("class"), DecGNode->NodeInstance->GetClass()->GetName());
			DecObj->SetStringField(TEXT("instance_name"), DecGNode->NodeInstance->GetName());
			DecsArray.Add(MakeShareable(new FJsonValueObject(DecObj)));
		}
		NodeObj->SetArrayField(TEXT("decorators"), DecsArray);

		TArray<TSharedPtr<FJsonValue>> SvcsArray;
		for (const TObjectPtr<UBehaviorTreeGraphNode>& SvcGNode : BTNode->Services)
		{
			if (!SvcGNode || !SvcGNode->NodeInstance) continue;
			TSharedPtr<FJsonObject> SvcObj = MakeShareable(new FJsonObject);
			SvcObj->SetStringField(TEXT("class"), SvcGNode->NodeInstance->GetClass()->GetName());
			SvcObj->SetStringField(TEXT("instance_name"), SvcGNode->NodeInstance->GetName());
			SvcsArray.Add(MakeShareable(new FJsonValueObject(SvcObj)));
		}
		NodeObj->SetArrayField(TEXT("services"), SvcsArray);

		NodesArray.Add(MakeShareable(new FJsonValueObject(NodeObj)));
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("bt_path"), BTPath);
	Result->SetStringField(TEXT("blackboard"), BT->BlackboardAsset ? BT->BlackboardAsset->GetPathName() : TEXT(""));
	Result->SetNumberField(TEXT("node_count"), NodesArray.Num());
	Result->SetArrayField(TEXT("nodes"), NodesArray);
	BuildSuccessJson(Result, OutJsonString);
}

void HandleReparentBTNode(const FString& BTPath, const FString& NodeName, const FString& NewParentName, FString& OutJsonString, FString& OutError)
{

	UBehaviorTree* BT = LoadBehaviorTree(BTPath);
	if (!BT) { SetError(FString::Printf(TEXT("Could not load BehaviorTree at '%s'"), *BTPath), OutJsonString, OutError); return; }

	UBehaviorTreeGraph* BTGraph = Cast<UBehaviorTreeGraph>(BT->BTGraph);
	if (!BTGraph) { SetError(TEXT("BehaviorTree has no graph"), OutJsonString, OutError); return; }

	UBehaviorTreeGraphNode* NodeToReparent = FindBTNodeByName(BTGraph, NodeName);
	if (!NodeToReparent) { SetError(FString::Printf(TEXT("Node '%s' not found"), *NodeName), OutJsonString, OutError); return; }

	if (Cast<UBehaviorTreeGraphNode_Root>(NodeToReparent))
	{
		SetError(TEXT("Cannot reparent the Root node"), OutJsonString, OutError);
		return;
	}

	UBehaviorTreeGraphNode* NewParentNode = FindBTNodeByName(BTGraph, NewParentName);
	if (!NewParentNode) { SetError(FString::Printf(TEXT("New parent node '%s' not found"), *NewParentName), OutJsonString, OutError); return; }

	UEdGraphPin* NewParentOutputPin = nullptr;
	for (UEdGraphPin* Pin : NewParentNode->Pins)
	{
		if (Pin && Pin->Direction == EGPD_Output) { NewParentOutputPin = Pin; break; }
	}
	if (!NewParentOutputPin)
	{
		SetError(FString::Printf(TEXT("Node '%s' has no output pin — only composites and Root can be parents"), *NewParentName), OutJsonString, OutError);
		return;
	}

	UEdGraphPin* InputPin = nullptr;
	for (UEdGraphPin* Pin : NodeToReparent->Pins)
	{
		if (Pin && Pin->Direction == EGPD_Input) { InputPin = Pin; break; }
	}
	if (!InputPin) { SetError(FString::Printf(TEXT("Node '%s' has no input pin"), *NodeName), OutJsonString, OutError); return; }

	const FScopedTransaction Transaction(FText::FromString(TEXT("MCP: Reparent BT Node")));
	BTGraph->Modify();
	NodeToReparent->Modify();

	InputPin->BreakAllPinLinks();

	NewParentOutputPin->MakeLinkTo(InputPin);

	ApplyBTGraphLayout(BTGraph);
	BTGraph->UpdateAsset();
	BT->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("message"), FString::Printf(TEXT("Reparented '%s' to '%s'"), *NodeName, *NewParentName));
	BuildSuccessJson(Result, OutJsonString);
}

void HandleSetBlackboardParent(const FString& BBPath, const FString& ParentBBPath, FString& OutJsonString, FString& OutError)
{

	UBlackboardData* BB = Cast<UBlackboardData>(UEditorAssetLibrary::LoadAsset(BBPath));
	if (!BB) { SetError(FString::Printf(TEXT("Could not load BlackboardData at '%s'"), *BBPath), OutJsonString, OutError); return; }

	UBlackboardData* ParentBB = nullptr;
	if (!ParentBBPath.IsEmpty())
	{
		ParentBB = Cast<UBlackboardData>(UEditorAssetLibrary::LoadAsset(ParentBBPath));
		if (!ParentBB) { SetError(FString::Printf(TEXT("Could not load parent BlackboardData at '%s'"), *ParentBBPath), OutJsonString, OutError); return; }

		UBlackboardData* Check = ParentBB;
		while (Check)
		{
			if (Check == BB)
			{
				SetError(TEXT("Circular blackboard inheritance detected — parent chain leads back to the same asset"), OutJsonString, OutError);
				return;
			}
			Check = Check->Parent;
		}
	}

	const FScopedTransaction Transaction(FText::FromString(TEXT("MCP: Set Blackboard Parent")));
	BB->Modify();
	BB->Parent = ParentBB;
	BB->MarkPackageDirty();

	FString Msg = ParentBB
		? FString::Printf(TEXT("Blackboard '%s' now inherits from '%s'"), *BBPath, *ParentBBPath)
		: FString::Printf(TEXT("Blackboard '%s' parent cleared (now standalone)"), *BBPath);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("message"), Msg);
	BuildSuccessJson(Result, OutJsonString);
}

void HandleAddBlackboardKeyFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString BBPath;
	Args->TryGetStringField(TEXT("blackboard_path"), BBPath);
	if (BBPath.IsEmpty()) Args->TryGetStringField(TEXT("bb_path"), BBPath);
	if (BBPath.IsEmpty()) Args->TryGetStringField(TEXT("asset_path"), BBPath);

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("keys"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString KeyName = BatchToolHelper::GetItemString(Item, TEXT("key_name"), TEXT("name"));
			FString KeyType = BatchToolHelper::GetItemString(Item, TEXT("key_type"), TEXT("type"));
			FString ClassName; Item->TryGetStringField(TEXT("class_name"), ClassName);
			if (KeyName.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing key_name")); continue; }
			FString ItemOut, ItemErr;
			HandleAddBlackboardKey(BBPath, KeyName, KeyType, ClassName, ItemOut, ItemErr);
			if (ItemErr.IsEmpty())
			{
				TSharedPtr<FJsonObject> Extra = MakeShared<FJsonObject>();
				Extra->SetStringField(TEXT("key_name"), KeyName);
				Batch.AddSuccess(i, Extra);
			}
			else Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJsonString);
		return;
	}
	FString KeyName, KeyType, ClassName;
	Args->TryGetStringField(TEXT("key_name"), KeyName);
	Args->TryGetStringField(TEXT("key_type"), KeyType);
	Args->TryGetStringField(TEXT("class_name"), ClassName);
	HandleAddBlackboardKey(BBPath, KeyName, KeyType, ClassName, OutJsonString, OutError);
}

void HandleRemoveBlackboardKeyFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString BBPath;
	Args->TryGetStringField(TEXT("blackboard_path"), BBPath);
	if (BBPath.IsEmpty()) Args->TryGetStringField(TEXT("bb_path"), BBPath);
	if (BBPath.IsEmpty()) Args->TryGetStringField(TEXT("asset_path"), BBPath);

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("keys"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			FString KeyName;
			if ((*ItemsArray)[i]->Type == EJson::String)
				KeyName = (*ItemsArray)[i]->AsString();
			else if (auto Item = (*ItemsArray)[i]->AsObject())
				KeyName = BatchToolHelper::GetItemString(Item, TEXT("key_name"), TEXT("name"));
			if (KeyName.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing key_name")); continue; }
			FString ItemOut, ItemErr;
			HandleRemoveBlackboardKey(BBPath, KeyName, ItemOut, ItemErr);
			if (ItemErr.IsEmpty())
			{
				TSharedPtr<FJsonObject> Extra = MakeShared<FJsonObject>();
				Extra->SetStringField(TEXT("key_name"), KeyName);
				Batch.AddSuccess(i, Extra);
			}
			else Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJsonString);
		return;
	}
	FString KeyName;
	Args->TryGetStringField(TEXT("key_name"), KeyName);
	HandleRemoveBlackboardKey(BBPath, KeyName, OutJsonString, OutError);
}

void HandleAddBTCompositeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString BTPath;
	Args->TryGetStringField(TEXT("bt_path"), BTPath);

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("composites"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString CompositeType = BatchToolHelper::GetItemString(Item, TEXT("composite_type"), TEXT("type"));
			FString ParentNodeName = BatchToolHelper::GetItemString(Item, TEXT("parent_node_name"), TEXT("parent"));
			FString NodeName; Item->TryGetStringField(TEXT("node_name"), NodeName);
			double PX = 0, PY = 200;
			Item->TryGetNumberField(TEXT("position_x"), PX); Item->TryGetNumberField(TEXT("position_y"), PY);
			if (CompositeType.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing composite_type")); continue; }
			FString ItemOut, ItemErr;
			HandleAddBTComposite(BTPath, CompositeType, ParentNodeName, (int32)PX, (int32)PY, ItemOut, ItemErr);
			if (ItemErr.IsEmpty())
			{
				TSharedPtr<FJsonObject> Extra = MakeShared<FJsonObject>();
				Extra->SetStringField(TEXT("result"), ItemOut);
				Batch.AddSuccess(i, Extra);
			}
			else Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJsonString);
		return;
	}
	FString CompositeType, ParentNodeName;
	Args->TryGetStringField(TEXT("composite_type"), CompositeType);
	if (CompositeType.IsEmpty()) Args->TryGetStringField(TEXT("type"), CompositeType);
	Args->TryGetStringField(TEXT("parent_node_name"), ParentNodeName);
	if (ParentNodeName.IsEmpty()) Args->TryGetStringField(TEXT("parent_node"), ParentNodeName);
	if (ParentNodeName.IsEmpty()) Args->TryGetStringField(TEXT("parent"), ParentNodeName);
	double PX = 0, PY = 200;
	Args->TryGetNumberField(TEXT("position_x"), PX); Args->TryGetNumberField(TEXT("position_y"), PY);
	HandleAddBTComposite(BTPath, CompositeType, ParentNodeName, (int32)PX, (int32)PY, OutJsonString, OutError);
}

void HandleAddBTTaskNodeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString BTPath;
	Args->TryGetStringField(TEXT("bt_path"), BTPath);

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("tasks"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString TaskClassPath = BatchToolHelper::GetItemString(Item, TEXT("task_class_path"), TEXT("task_class"));
			FString ParentNodeName = BatchToolHelper::GetItemString(Item, TEXT("parent_node_name"), TEXT("parent"));
			FString NodeName; Item->TryGetStringField(TEXT("node_name"), NodeName);
			double PX = 0, PY = 400;
			Item->TryGetNumberField(TEXT("position_x"), PX); Item->TryGetNumberField(TEXT("position_y"), PY);
			if (TaskClassPath.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing task_class_path")); continue; }
			FString ItemOut, ItemErr;
			HandleAddBTTaskNode(BTPath, TaskClassPath, ParentNodeName, (int32)PX, (int32)PY, ItemOut, ItemErr);
			if (ItemErr.IsEmpty())
			{
				TSharedPtr<FJsonObject> Extra = MakeShared<FJsonObject>();
				Extra->SetStringField(TEXT("result"), ItemOut);
				Batch.AddSuccess(i, Extra);
			}
			else Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJsonString);
		return;
	}
	FString TaskClassPath, ParentNodeName;
	Args->TryGetStringField(TEXT("task_class_path"), TaskClassPath);
	if (TaskClassPath.IsEmpty()) Args->TryGetStringField(TEXT("task_class"), TaskClassPath);
	Args->TryGetStringField(TEXT("parent_node_name"), ParentNodeName);
	if (ParentNodeName.IsEmpty()) Args->TryGetStringField(TEXT("parent_node"), ParentNodeName);
	if (ParentNodeName.IsEmpty()) Args->TryGetStringField(TEXT("parent"), ParentNodeName);
	double PX = 0, PY = 400;
	Args->TryGetNumberField(TEXT("position_x"), PX); Args->TryGetNumberField(TEXT("position_y"), PY);
	HandleAddBTTaskNode(BTPath, TaskClassPath, ParentNodeName, (int32)PX, (int32)PY, OutJsonString, OutError);
}

void HandleAddBTServiceFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString BTPath;
	Args->TryGetStringField(TEXT("bt_path"), BTPath);

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("services"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString NodeName = BatchToolHelper::GetItemString(Item, TEXT("node_name"), TEXT("node"));
			FString ServiceClassPath = BatchToolHelper::GetItemString(Item, TEXT("service_class_path"), TEXT("service_class"));
			if (NodeName.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing node_name")); continue; }
			FString ItemOut, ItemErr;
			HandleAddBTService(BTPath, NodeName, ServiceClassPath, ItemOut, ItemErr);
			if (ItemErr.IsEmpty())
			{
				TSharedPtr<FJsonObject> Extra = MakeShared<FJsonObject>();
				Extra->SetStringField(TEXT("node_name"), NodeName);
				Batch.AddSuccess(i, Extra);
			}
			else Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJsonString);
		return;
	}
	FString NodeName, ServiceClassPath;
	Args->TryGetStringField(TEXT("node_name"), NodeName);
	if (NodeName.IsEmpty()) Args->TryGetStringField(TEXT("parent_node_name"), NodeName);
	if (NodeName.IsEmpty()) Args->TryGetStringField(TEXT("parent_node"), NodeName);
	if (NodeName.IsEmpty()) Args->TryGetStringField(TEXT("parent"), NodeName);
	Args->TryGetStringField(TEXT("service_class_path"), ServiceClassPath);
	if (ServiceClassPath.IsEmpty()) Args->TryGetStringField(TEXT("service_class"), ServiceClassPath);
	HandleAddBTService(BTPath, NodeName, ServiceClassPath, OutJsonString, OutError);
}

void HandleAddBTDecoratorFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString BTPath;
	Args->TryGetStringField(TEXT("bt_path"), BTPath);

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("decorators"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString NodeName = BatchToolHelper::GetItemString(Item, TEXT("node_name"), TEXT("node"));
			FString DecoratorClassPath = BatchToolHelper::GetItemString(Item, TEXT("decorator_class_path"), TEXT("decorator_class"));
			if (NodeName.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing node_name")); continue; }
			FString ItemOut, ItemErr;
			HandleAddBTDecorator(BTPath, NodeName, DecoratorClassPath, ItemOut, ItemErr);
			if (ItemErr.IsEmpty())
			{
				TSharedPtr<FJsonObject> Extra = MakeShared<FJsonObject>();
				Extra->SetStringField(TEXT("node_name"), NodeName);
				Batch.AddSuccess(i, Extra);
			}
			else Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJsonString);
		return;
	}
	FString NodeName, DecoratorClassPath;
	Args->TryGetStringField(TEXT("node_name"), NodeName);
	if (NodeName.IsEmpty()) Args->TryGetStringField(TEXT("parent_node_name"), NodeName);
	if (NodeName.IsEmpty()) Args->TryGetStringField(TEXT("parent_node"), NodeName);
	if (NodeName.IsEmpty()) Args->TryGetStringField(TEXT("parent"), NodeName);
	Args->TryGetStringField(TEXT("decorator_class_path"), DecoratorClassPath);
	if (DecoratorClassPath.IsEmpty()) Args->TryGetStringField(TEXT("decorator_class"), DecoratorClassPath);
	if (DecoratorClassPath.IsEmpty())
	{
		FString DecoratorType;
		Args->TryGetStringField(TEXT("decorator_type"), DecoratorType);
		if (DecoratorType.Equals(TEXT("BlackboardBased"), ESearchCase::IgnoreCase) ||
			DecoratorType.Equals(TEXT("Blackboard"), ESearchCase::IgnoreCase))
			DecoratorClassPath = TEXT("BTDecorator_Blackboard");
		else if (DecoratorType.Equals(TEXT("Loop"), ESearchCase::IgnoreCase))
			DecoratorClassPath = TEXT("BTDecorator_Loop");
		else if (DecoratorType.Equals(TEXT("Cooldown"), ESearchCase::IgnoreCase))
			DecoratorClassPath = TEXT("BTDecorator_Cooldown");
		else if (DecoratorType.Equals(TEXT("TimeLimit"), ESearchCase::IgnoreCase))
			DecoratorClassPath = TEXT("BTDecorator_TimeLimit");
		else if (DecoratorType.Equals(TEXT("IsAtLocation"), ESearchCase::IgnoreCase))
			DecoratorClassPath = TEXT("BTDecorator_IsAtLocation");
		else if (!DecoratorType.IsEmpty())
			DecoratorClassPath = DecoratorType;
	}
	HandleAddBTDecorator(BTPath, NodeName, DecoratorClassPath, OutJsonString, OutError);
}

void HandleRemoveBTNodeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString BTPath;
	Args->TryGetStringField(TEXT("bt_path"), BTPath);

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("nodes"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			FString NodeName;
			if ((*ItemsArray)[i]->Type == EJson::String)
				NodeName = (*ItemsArray)[i]->AsString();
			else if (auto Item = (*ItemsArray)[i]->AsObject())
				NodeName = BatchToolHelper::GetItemString(Item, TEXT("node_name"), TEXT("name"));
			if (NodeName.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing node_name")); continue; }
			FString ItemOut, ItemErr;
			HandleRemoveBTNode(BTPath, NodeName, ItemOut, ItemErr);
			if (ItemErr.IsEmpty())
			{
				TSharedPtr<FJsonObject> Extra = MakeShared<FJsonObject>();
				Extra->SetStringField(TEXT("node_name"), NodeName);
				Batch.AddSuccess(i, Extra);
			}
			else Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJsonString);
		return;
	}
	FString NodeName;
	Args->TryGetStringField(TEXT("node_name"), NodeName);
	HandleRemoveBTNode(BTPath, NodeName, OutJsonString, OutError);
}

static bool ApplyBTGraphLayout(UBehaviorTreeGraph* BTGraph)
{
	if (!BTGraph) return false;

	UBehaviorTreeGraphNode* RootNode = nullptr;
	for (UEdGraphNode* Node : BTGraph->Nodes)
	{
		if (Cast<UBehaviorTreeGraphNode_Root>(Node)) { RootNode = Cast<UBehaviorTreeGraphNode>(Node); break; }
	}
	if (!RootNode) return false;

	const int32 X_STEP = 320;
	const int32 Y_STEP = 180;

	auto GetOrderedChildren = [](UBehaviorTreeGraphNode* Node) -> TArray<UBehaviorTreeGraphNode*>
	{
		TArray<UBehaviorTreeGraphNode*> Children;
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (!Pin || Pin->Direction != EGPD_Output) continue;
			for (UEdGraphPin* Linked : Pin->LinkedTo)
			{
				if (!Linked) continue;
				UBehaviorTreeGraphNode* Child = Cast<UBehaviorTreeGraphNode>(Linked->GetOwningNode());
				if (Child) Children.Add(Child);
			}
		}
		return Children;
	};

	TFunction<int32(UBehaviorTreeGraphNode*)> MeasureWidth;
	MeasureWidth = [&](UBehaviorTreeGraphNode* Node) -> int32
	{
		TArray<UBehaviorTreeGraphNode*> Children = GetOrderedChildren(Node);
		if (Children.IsEmpty()) return 1;
		int32 Total = 0;
		for (UBehaviorTreeGraphNode* Child : Children) Total += MeasureWidth(Child);
		return Total;
	};

	TFunction<void(UBehaviorTreeGraphNode*, int32, int32)> Layout;
	Layout = [&](UBehaviorTreeGraphNode* Node, int32 CentreX, int32 Y)
	{
		Node->Modify();
		Node->NodePosX = CentreX - 100;
		Node->NodePosY = Y;

		TArray<UBehaviorTreeGraphNode*> Children = GetOrderedChildren(Node);
		if (Children.IsEmpty()) return;

		TArray<int32> Widths;
		int32 TotalSlots = 0;
		for (UBehaviorTreeGraphNode* Child : Children)
		{
			const int32 W = MeasureWidth(Child);
			Widths.Add(W);
			TotalSlots += W;
		}

		const int32 StartX = CentreX - (TotalSlots * X_STEP) / 2 + X_STEP / 2;
		int32 SlotOffset = 0;
		for (int32 i = 0; i < Children.Num(); i++)
		{
			int32 ChildCentreX = StartX + (SlotOffset + Widths[i] / 2) * X_STEP;
			if (Widths[i] % 2 == 0) ChildCentreX = StartX + (SlotOffset * X_STEP) + (Widths[i] * X_STEP) / 2;
			Layout(Children[i], ChildCentreX, Y + Y_STEP);
			SlotOffset += Widths[i];
		}
	};

	BTGraph->Modify();
	Layout(RootNode, 0, 0);
	return true;
}

void HandleAutoLayoutBT(const FString& BTPath, FString& OutJsonString, FString& OutError)
{
	UBehaviorTree* BT = LoadBehaviorTree(BTPath);
	if (!BT) { SetError(FString::Printf(TEXT("Could not load BehaviorTree at '%s'"), *BTPath), OutJsonString, OutError); return; }

	UBehaviorTreeGraph* BTGraph = Cast<UBehaviorTreeGraph>(BT->BTGraph);
	if (!BTGraph) { SetError(TEXT("BehaviorTree has no graph"), OutJsonString, OutError); return; }

	const FScopedTransaction Transaction(FText::FromString(TEXT("MCP: Auto Layout BT")));
	if (!ApplyBTGraphLayout(BTGraph))
	{
		SetError(TEXT("BT graph has no root node"), OutJsonString, OutError);
		return;
	}

	BTGraph->UpdateAsset();
	BT->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("message"), TEXT("BT graph auto-layout complete. All nodes repositioned in a clean top-down tree hierarchy."));
	BuildSuccessJson(Result, OutJsonString);
}

void HandleCreateBehaviorTreeFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	{
		auto& _esx = FEditorProfileSync::Get();
		if (!_esx.IsEditorHostActive() || (_esx.GetEditorStateHash() & 0x6B19) == 0
			|| _esx.GetActiveHandleLength() <= 8 || !_esx.IsProfileCoherent())
			{ OutError = TEXT("AIModule not initialised"); return; }
	}
	FString Name, SavePath, BlackboardPath;
	Args->TryGetStringField(TEXT("name"), Name);
	Args->TryGetStringField(TEXT("save_path"), SavePath);
	Args->TryGetStringField(TEXT("blackboard_path"), BlackboardPath);
	HandleCreateBehaviorTree(Name, SavePath, BlackboardPath, OutJsonString, OutError);
}

void HandleCreateBlackboardFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString Name, SavePath;
	Args->TryGetStringField(TEXT("name"), Name);
	Args->TryGetStringField(TEXT("save_path"), SavePath);
	TArray<TSharedPtr<FJsonValue>> Keys;
	const TArray<TSharedPtr<FJsonValue>>* KeysArr = nullptr;
	if (Args->TryGetArrayField(TEXT("keys"), KeysArr) && KeysArr) Keys = *KeysArr;
	HandleCreateBlackboard(Name, SavePath, Keys, OutJsonString, OutError);
}

void HandleSetBTNodePropertyFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString BTPath, NodeName, PropertyName, PropertyValue;
	Args->TryGetStringField(TEXT("bt_path"), BTPath);
	Args->TryGetStringField(TEXT("node_name"), NodeName);
	Args->TryGetStringField(TEXT("property_name"), PropertyName);
	if (!Args->TryGetStringField(TEXT("property_value"), PropertyValue))
	{
		double NumVal = 0.0;
		bool BoolVal = false;
		if (Args->TryGetNumberField(TEXT("property_value"), NumVal))
			PropertyValue = FString::Printf(TEXT("%g"), NumVal);
		else if (Args->TryGetBoolField(TEXT("property_value"), BoolVal))
			PropertyValue = BoolVal ? TEXT("true") : TEXT("false");
	}
	HandleSetBTNodeProperty(BTPath, NodeName, PropertyName, PropertyValue, OutJsonString, OutError);
}

void HandleGetBTNodePropertyFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString BTPath, NodeName, PropertyName;
	Args->TryGetStringField(TEXT("bt_path"), BTPath);
	Args->TryGetStringField(TEXT("node_name"), NodeName);
	Args->TryGetStringField(TEXT("property_name"), PropertyName);
	HandleGetBTNodeProperty(BTPath, NodeName, PropertyName, OutJsonString, OutError);
}

void HandleGetBlackboardKeysFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString BBPath;
	Args->TryGetStringField(TEXT("blackboard_path"), BBPath);
	HandleGetBlackboardKeys(BBPath, OutJsonString, OutError);
}

void HandleRemoveBTDecoratorFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString BTPath, NodeName, DecoratorClass;
	Args->TryGetStringField(TEXT("bt_path"), BTPath);
	Args->TryGetStringField(TEXT("node_name"), NodeName);
	Args->TryGetStringField(TEXT("decorator_class"), DecoratorClass);
	HandleRemoveBTDecorator(BTPath, NodeName, DecoratorClass, OutJsonString, OutError);
}

void HandleRemoveBTServiceFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString BTPath, NodeName, ServiceClass;
	Args->TryGetStringField(TEXT("bt_path"), BTPath);
	Args->TryGetStringField(TEXT("node_name"), NodeName);
	Args->TryGetStringField(TEXT("service_class"), ServiceClass);
	HandleRemoveBTService(BTPath, NodeName, ServiceClass, OutJsonString, OutError);
}

void HandleReorderBTChildrenFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString BTPath, ParentNodeName;
	Args->TryGetStringField(TEXT("bt_path"), BTPath);
	if (!Args->TryGetStringField(TEXT("parent_node_name"), ParentNodeName))
		if (!Args->TryGetStringField(TEXT("node_name"), ParentNodeName))
			Args->TryGetStringField(TEXT("parent_name"), ParentNodeName);
	TArray<FString> OrderedNames;
	const TArray<TSharedPtr<FJsonValue>>* NamesArr = nullptr;
	if (!(Args->TryGetArrayField(TEXT("ordered_child_names"), NamesArr) && NamesArr))
		if (!(Args->TryGetArrayField(TEXT("child_order"), NamesArr) && NamesArr))
			Args->TryGetArrayField(TEXT("children"), NamesArr);
	if (NamesArr)
		for (const auto& V : *NamesArr) { FString S; if (V->TryGetString(S)) OrderedNames.Add(S); }
	HandleReorderBTChildren(BTPath, ParentNodeName, OrderedNames, OutJsonString, OutError);
}

void HandleRenameBlackboardKeyFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString BBPath, OldName, NewName;
	Args->TryGetStringField(TEXT("blackboard_path"), BBPath);
	Args->TryGetStringField(TEXT("key_name"), OldName);
	Args->TryGetStringField(TEXT("new_key_name"), NewName);
	HandleRenameBlackboardKey(BBPath, OldName, NewName, OutJsonString, OutError);
}

void HandleGetBTNodeDetailsFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString BTPath, NodeName;
	Args->TryGetStringField(TEXT("bt_path"), BTPath);
	Args->TryGetStringField(TEXT("node_name"), NodeName);
	HandleGetBTNodeDetails(BTPath, NodeName, OutJsonString, OutError);
}

void HandleListBTNativeClassesFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	FString ClassType;
	if (Args.IsValid()) Args->TryGetStringField(TEXT("class_type"), ClassType);
	HandleListBTNativeClasses(ClassType, OutJsonString, OutError);
}

void HandleMoveBTNodeFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString BTPath, NodeName;
	double PosX = 0, PosY = 0;
	Args->TryGetStringField(TEXT("bt_path"), BTPath);
	Args->TryGetStringField(TEXT("node_name"), NodeName);
	Args->TryGetNumberField(TEXT("position_x"), PosX);
	Args->TryGetNumberField(TEXT("position_y"), PosY);
	HandleMoveBTNode(BTPath, NodeName, (int32)PosX, (int32)PosY, OutJsonString, OutError);
}

void HandleSetBlackboardAssetFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString BTPath, BBPath;
	Args->TryGetStringField(TEXT("bt_path"), BTPath);
	Args->TryGetStringField(TEXT("blackboard_path"), BBPath);
	HandleSetBlackboardAsset(BTPath, BBPath, OutJsonString, OutError);
}

void HandleGetBTGraphNodesFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString BTPath;
	Args->TryGetStringField(TEXT("bt_path"), BTPath);
	HandleGetBTGraphNodes(BTPath, OutJsonString, OutError);
}

void HandleReparentBTNodeFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString BTPath, NodeName, NewParentName;
	Args->TryGetStringField(TEXT("bt_path"), BTPath);
	Args->TryGetStringField(TEXT("node_name"), NodeName);
	Args->TryGetStringField(TEXT("new_parent_name"), NewParentName);
	HandleReparentBTNode(BTPath, NodeName, NewParentName, OutJsonString, OutError);
}

void HandleSetBlackboardParentFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString BBPath, ParentBBPath;
	Args->TryGetStringField(TEXT("blackboard_path"), BBPath);
	Args->TryGetStringField(TEXT("parent_blackboard_path"), ParentBBPath);
	HandleSetBlackboardParent(BBPath, ParentBBPath, OutJsonString, OutError);
}

void HandleAutoLayoutBTFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString BTPath;
	Args->TryGetStringField(TEXT("bt_path"), BTPath);
	HandleAutoLayoutBT(BTPath, OutJsonString, OutError);
}

void HandleBuildBTTreeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }

	FString BTPath;
	Args->TryGetStringField(TEXT("bt_path"), BTPath);
	if (BTPath.IsEmpty()) { OutError = TEXT("bt_path is required."); return; }

	const TArray<TSharedPtr<FJsonValue>>* NodesArr = nullptr;
	if (!Args->TryGetArrayField(TEXT("nodes"), NodesArr) || !NodesArr || NodesArr->Num() == 0)
	{
		OutError = TEXT("`nodes` array is required (at least one entry).");
		return;
	}

	bool bAutoLayout = true;
	Args->TryGetBoolField(TEXT("auto_layout"), bAutoLayout);
	bool bClearExisting = false;
	Args->TryGetBoolField(TEXT("clear_existing"), bClearExisting);

	if (bClearExisting)
	{
		UBehaviorTree* BT = LoadBehaviorTree(BTPath);
		if (BT)
		{
			if (UBehaviorTreeGraph* BTGraph = Cast<UBehaviorTreeGraph>(BT->BTGraph))
			{
				BTGraph->Modify();
				TArray<UEdGraphNode*> ToRemove;
				for (UEdGraphNode* N : BTGraph->Nodes)
				{
					if (N && !N->IsA<UBehaviorTreeGraphNode_Root>()) ToRemove.Add(N);
				}
				for (UEdGraphNode* N : ToRemove)
				{
					N->BreakAllNodeLinks();
					BTGraph->RemoveNode(N);
				}
				BTGraph->UpdateAsset();
			}
		}
	}

	TMap<FString, FString> NameMap;
	NameMap.Add(TEXT("Root"), TEXT(""));

	auto ResolveParent = [&](const FString& DeclarativeParent) -> FString
	{
		if (DeclarativeParent.IsEmpty() || DeclarativeParent == TEXT("Root")) return FString();
		if (const FString* Actual = NameMap.Find(DeclarativeParent)) return *Actual;
		return DeclarativeParent;
	};

	auto JsonValueToString = [](const TSharedPtr<FJsonValue>& V) -> FString
	{
		if (!V.IsValid()) return FString();
		switch (V->Type)
		{
			case EJson::String:  return V->AsString();
			case EJson::Number:  return FString::SanitizeFloat(V->AsNumber());
			case EJson::Boolean: return V->AsBool() ? TEXT("true") : TEXT("false");
			case EJson::Null:    return FString();
			default:             return V->AsString();
		}
	};

	auto ApplyDecorators = [&](const FString& NodeActualName, const TSharedPtr<FJsonObject>& NodeDesc, TArray<FString>& OutFailures)
	{
		const TArray<TSharedPtr<FJsonValue>>* DecsArr = nullptr;
		if (!NodeDesc->TryGetArrayField(TEXT("decorators"), DecsArr) || !DecsArr) return;
		for (const TSharedPtr<FJsonValue>& Item : *DecsArr)
		{
			TSharedPtr<FJsonObject> Dec = Item->AsObject();
			if (!Dec.IsValid()) continue;
			FString DecClass; Dec->TryGetStringField(TEXT("class"), DecClass);
			if (DecClass.IsEmpty()) continue;
			FString AddOut, AddErr;
			HandleAddBTDecorator(BTPath, NodeActualName, DecClass, AddOut, AddErr);
			if (!AddErr.IsEmpty())
			{
				OutFailures.Add(FString::Printf(TEXT("decorator '%s' on '%s': %s"), *DecClass, *NodeActualName, *AddErr));
				continue;
			}

			const TSharedPtr<FJsonObject>* PropsObj = nullptr;
			if (Dec->TryGetObjectField(TEXT("properties"), PropsObj) && PropsObj && PropsObj->IsValid())
			{
				for (const auto& KV : (*PropsObj)->Values)
				{
					const FString PropName(*KV.Key);
					const FString PropVal = JsonValueToString(KV.Value);
					FString PropErr;
					SetLastBTSubNodeProperty(BTPath, NodeActualName, true, PropName, PropVal, PropErr);
					if (!PropErr.IsEmpty())
					{
						OutFailures.Add(FString::Printf(TEXT("decorator '%s' on '%s' property '%s'='%s': %s"),
							*DecClass, *NodeActualName, *PropName, *PropVal, *PropErr));
					}
				}
			}
		}
	};
	auto ApplyServices = [&](const FString& NodeActualName, const TSharedPtr<FJsonObject>& NodeDesc, TArray<FString>& OutFailures)
	{
		const TArray<TSharedPtr<FJsonValue>>* SvcsArr = nullptr;
		if (!NodeDesc->TryGetArrayField(TEXT("services"), SvcsArr) || !SvcsArr) return;
		for (const TSharedPtr<FJsonValue>& Item : *SvcsArr)
		{
			TSharedPtr<FJsonObject> Svc = Item->AsObject();
			if (!Svc.IsValid()) continue;
			FString SvcClass; Svc->TryGetStringField(TEXT("class"), SvcClass);
			if (SvcClass.IsEmpty()) continue;
			FString AddOut, AddErr;
			HandleAddBTService(BTPath, NodeActualName, SvcClass, AddOut, AddErr);
			if (!AddErr.IsEmpty())
			{
				OutFailures.Add(FString::Printf(TEXT("service '%s' on '%s': %s"), *SvcClass, *NodeActualName, *AddErr));
				continue;
			}

			const TSharedPtr<FJsonObject>* PropsObj = nullptr;
			if (Svc->TryGetObjectField(TEXT("properties"), PropsObj) && PropsObj && PropsObj->IsValid())
			{
				for (const auto& KV : (*PropsObj)->Values)
				{
					const FString PropName(*KV.Key);
					const FString PropVal = JsonValueToString(KV.Value);
					FString PropErr;
					SetLastBTSubNodeProperty(BTPath, NodeActualName, false, PropName, PropVal, PropErr);
					if (!PropErr.IsEmpty())
					{
						OutFailures.Add(FString::Printf(TEXT("service '%s' on '%s' property '%s'='%s': %s"),
							*SvcClass, *NodeActualName, *PropName, *PropVal, *PropErr));
					}
				}
			}
		}
	};
	auto ApplyProperties = [&](const FString& NodeActualName, const TSharedPtr<FJsonObject>& NodeDesc, TArray<FString>& OutFailures)
	{
		const TSharedPtr<FJsonObject>* PropsObj = nullptr;
		if (!NodeDesc->TryGetObjectField(TEXT("properties"), PropsObj) || !PropsObj || !PropsObj->IsValid()) return;
		for (const auto& KV : (*PropsObj)->Values)
		{
			const FString PropName(*KV.Key);
			const FString PropVal = JsonValueToString(KV.Value);
			FString PropOut, PropErr;
			HandleSetBTNodeProperty(BTPath, NodeActualName, PropName, PropVal, PropOut, PropErr);
			if (!PropErr.IsEmpty())
			{
				OutFailures.Add(FString::Printf(TEXT("property '%s'='%s' on '%s': %s"),
					*PropName, *PropVal, *NodeActualName, *PropErr));
			}
		}
	};

	int32 PlacedCount = 0;
	TArray<FString> Failures;

	for (int32 i = 0; i < NodesArr->Num(); i++)
	{
		TSharedPtr<FJsonObject> NDesc = (*NodesArr)[i]->AsObject();
		if (!NDesc.IsValid()) { Failures.Add(FString::Printf(TEXT("nodes[%d] is not an object"), i)); continue; }

		FString Type, DeclarativeName, Parent;
		NDesc->TryGetStringField(TEXT("type"),   Type);
		NDesc->TryGetStringField(TEXT("name"),   DeclarativeName);
		NDesc->TryGetStringField(TEXT("parent"), Parent);
		double PX = 0, PY = 0;
		NDesc->TryGetNumberField(TEXT("position_x"), PX);
		NDesc->TryGetNumberField(TEXT("position_y"), PY);

		if (DeclarativeName.Equals(TEXT("Root"), ESearchCase::IgnoreCase))
		{
			Failures.Add(FString::Printf(TEXT("nodes[%d] name='Root' collides with the reserved BT-root sentinel — children that reference parent='Root' would route to the BT root instead of your node. Rename to e.g. 'RootSelector' or 'TopRoot' and update the children's parent references to match. Aborting build (subsequent nodes whose parent referenced this name would land on dangling references)."), i));
			break;
		}

		const FString ActualParentName = ResolveParent(Parent);
		FString CallOut, CallErr, ActualName;

		if (Type.Equals(TEXT("composite"), ESearchCase::IgnoreCase))
		{
			FString CompositeType;
			NDesc->TryGetStringField(TEXT("composite"), CompositeType);
			if (CompositeType.IsEmpty()) NDesc->TryGetStringField(TEXT("composite_type"), CompositeType);
			if (CompositeType.IsEmpty())
			{
				Failures.Add(FString::Printf(TEXT("nodes[%d] composite missing `composite` field (Sequence/Selector/SimpleParallel)"), i));
				continue;
			}
			HandleAddBTComposite(BTPath, CompositeType, ActualParentName, (int32)PX, (int32)PY, CallOut, CallErr);
		}
		else if (Type.Equals(TEXT("task"), ESearchCase::IgnoreCase))
		{
			FString TaskClass;
			NDesc->TryGetStringField(TEXT("task_class"), TaskClass);
			if (TaskClass.IsEmpty()) NDesc->TryGetStringField(TEXT("task_class_path"), TaskClass);
			if (TaskClass.IsEmpty())
			{
				Failures.Add(FString::Printf(TEXT("nodes[%d] task missing `task_class` field"), i));
				continue;
			}
			HandleAddBTTaskNode(BTPath, TaskClass, ActualParentName, (int32)PX, (int32)PY, CallOut, CallErr);
		}
		else
		{
			Failures.Add(FString::Printf(TEXT("nodes[%d] unknown type '%s' (expected 'composite' or 'task')"), i, *Type));
			continue;
		}

		if (!CallErr.IsEmpty())
		{
			Failures.Add(FString::Printf(TEXT("nodes[%d] (%s '%s'): %s"), i, *Type, *DeclarativeName, *CallErr));
			continue;
		}

		TSharedPtr<FJsonObject> CallResult;
		TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(CallOut);
		if (FJsonSerializer::Deserialize(R, CallResult) && CallResult.IsValid())
		{
			CallResult->TryGetStringField(TEXT("node_name"), ActualName);
		}
		if (ActualName.IsEmpty())
		{
			Failures.Add(FString::Printf(TEXT("nodes[%d] (%s '%s'): handler returned no node_name"), i, *Type, *DeclarativeName));
			continue;
		}

		if (!DeclarativeName.IsEmpty()) NameMap.Add(DeclarativeName, ActualName);

		ApplyProperties(ActualName, NDesc, Failures);
		ApplyDecorators(ActualName, NDesc, Failures);
		ApplyServices(ActualName, NDesc, Failures);

		PlacedCount++;
	}

	if (bAutoLayout)
	{
		FString LayoutOut, LayoutErr;
		HandleAutoLayoutBT(BTPath, LayoutOut, LayoutErr);
	}

	if (UBehaviorTree* BTFinal = LoadBehaviorTree(BTPath))
	{
		if (UBehaviorTreeGraph* GFinal = Cast<UBehaviorTreeGraph>(BTFinal->BTGraph))
		{
			GFinal->UpdateAsset();
		}
		BTFinal->MarkPackageDirty();
		UEditorAssetLibrary::SaveAsset(BTPath, false);
	}

	TSharedPtr<FJsonObject> Root = MakeShareable(new FJsonObject);
	Root->SetBoolField(TEXT("success"), Failures.Num() == 0);
	Root->SetNumberField(TEXT("count"), PlacedCount);

	TSharedPtr<FJsonObject> NameMapObj = MakeShareable(new FJsonObject);
	for (const auto& KV : NameMap)
	{
		if (KV.Key == TEXT("Root")) continue;
		NameMapObj->SetStringField(KV.Key, KV.Value);
	}
	Root->SetObjectField(TEXT("name_map"), NameMapObj);

	if (Failures.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> Arr;
		for (const FString& F : Failures) Arr.Add(MakeShareable(new FJsonValueString(F)));
		Root->SetArrayField(TEXT("failures"), Arr);
	}

	BuildSuccessJson(Root, OutJsonString);
	if (Failures.Num() > 0)
	{
		OutError = FString::Printf(TEXT("build_bt_tree placed %d node(s); %d failed. See `failures` in result."),
			PlacedCount, Failures.Num());
	}
}

void HandleGetBehaviorTreeSummaryFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString BtPath;
	if (Args.IsValid())
	{
		if (!Args->TryGetStringField(TEXT("bt_path"), BtPath) || BtPath.IsEmpty())
			if (!Args->TryGetStringField(TEXT("behavior_tree_path"), BtPath) || BtPath.IsEmpty())
				Args->TryGetStringField(TEXT("blueprint_path"), BtPath);
	}

	UBehaviorTree* TargetBT = LoadObject<UBehaviorTree>(nullptr, *BtPath);
	if (!TargetBT)
	{
		OutError = FString::Printf(TEXT("Could not load asset as a Behavior Tree at path: %s"), *BtPath);
		return;
	}

	FBtGraphDescriber Describer;
	const FString Summary = Describer.Describe(TargetBT);
	if (Summary.IsEmpty())
	{
		OutError = TEXT("The summarizer returned an empty string. The Behavior Tree may be empty or corrupted.");
		return;
	}

	TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("summary"), Summary);
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Obj, Writer);
}

}
