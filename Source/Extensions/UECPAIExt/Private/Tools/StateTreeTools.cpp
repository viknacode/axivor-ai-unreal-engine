// Copyright 2026, BlueprintsLab, All rights reserved.

#include "Tools/StateTreeTools.h"
#include "Managers/ProjectStateCache.h"
#include "Managers/SettingsManager.h"
#include "Tools/BatchToolHelper.h"

#include "StateTree.h"
#include "StateTreeEditorData.h"
#include "StateTreeState.h"
#include "StateTreeTypes.h"
#include "StateTreeDelegates.h"
#include "Editor.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "TimerManager.h"
#include "Misc/EngineVersionComparison.h"
#if UE_VERSION_OLDER_THAN(5, 5, 0)
#include "InstancedStruct.h"
#else
#include "StructUtils/InstancedStruct.h"
#endif
#include "Misc/EngineVersionComparison.h"
#if UE_VERSION_OLDER_THAN(5, 5, 0)
#include "PropertyBag.h"
#else
#include "StructUtils/PropertyBag.h"
#endif
#include "GameplayTagContainer.h"
#include "StateTreeEditorNode.h"
#include "StateTreeNodeBase.h"
#include "StateTreePropertyBindings.h"
#include "StateTreeEditorPropertyBindings.h"
#include "StateTreeCompiler.h"
#include "StateTreeCompilerLog.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "EditorAssetLibrary.h"
#include "UObject/Package.h"
#include "Misc/PackageName.h"
#include "UObject/UObjectIterator.h"
#include "Modules/ModuleManager.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "GameFramework/Pawn.h"

namespace StateTreeTools
{

static int32 SanitizeStateTreeHierarchy(UStateTreeEditorData* ED);

static UStateTreeEditorData* GetEditorData(UStateTree* ST)
{
	if (!ST) return nullptr;
	UStateTreeEditorData* ED = Cast<UStateTreeEditorData>(ST->EditorData);
	if (ED)
	{
		SanitizeStateTreeHierarchy(ED);
	}
	return ED;
}

static bool ApplyParameterDefault(FInstancedPropertyBag* Bag, const FName ParamName,
	const TSharedPtr<FJsonObject>& Args, FString& OutDescription, FString& OutErr);

static FInstancedPropertyBag* GetMutableRootParameterBag(UStateTreeEditorData* ED)
{
	if (!ED) return nullptr;
	if (FStructProperty* NewField = FindFProperty<FStructProperty>(
		UStateTreeEditorData::StaticClass(), TEXT("RootParameterPropertyBag")))
	{
		return NewField->ContainerPtrToValuePtr<FInstancedPropertyBag>(ED);
	}
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	return &ED->RootParameters.Parameters;
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

static FGuid GetRootParametersBindingId(UStateTreeEditorData* ED)
{
	if (!ED) return FGuid();
	if (FStructProperty* NewField = FindFProperty<FStructProperty>(
		UStateTreeEditorData::StaticClass(), TEXT("RootParametersGuid")))
	{
		return *NewField->ContainerPtrToValuePtr<FGuid>(ED);
	}
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	return ED->RootParameters.ID;
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

static void RefreshOpenStateTreeEditor(UStateTree* ST)
{
	if (!ST || !GEditor) return;
	UAssetEditorSubsystem* AES = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
	if (!AES) return;
	if (AES->FindEditorsForAsset(ST).Num() == 0) return;

	AES->CloseAllEditorsForAsset(ST);
	TWeakObjectPtr<UStateTree> WeakST(ST);
	GEditor->GetTimerManager()->SetTimerForNextTick(
		FTimerDelegate::CreateLambda([WeakST]()
		{
			if (UStateTree* STAlive = WeakST.Get())
			{
				if (UAssetEditorSubsystem* AES2 = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
				{
					AES2->OpenEditorForAsset(STAlive);
				}
			}
		}));
}

static void BroadcastParametersChanged(UStateTree* ST, UStateTreeEditorData* ED)
{
	if (!ST || !ED) return;

	const FName PropName = FindFProperty<FProperty>(
		UStateTreeEditorData::StaticClass(), TEXT("RootParameterPropertyBag"))
		? FName(TEXT("RootParameterPropertyBag"))
		: FName(TEXT("RootParameters"));
	if (FProperty* Prop = FindFProperty<FProperty>(UStateTreeEditorData::StaticClass(), PropName))
	{
		FPropertyChangedEvent Evt(Prop, EPropertyChangeType::ValueSet);
		ED->PostEditChangeProperty(Evt);
	}

	UE::StateTree::Delegates::OnParametersChanged.Broadcast(*ST);
}

static UStateTreeState* FindStateByName(UStateTreeEditorData* EditorData, const FName& StateName)
{
	if (!EditorData) return nullptr;
	TArray<UStateTreeState*> Queue;
	for (UStateTreeState* S : EditorData->SubTrees)
		if (S) Queue.Add(S);

	while (Queue.Num() > 0)
	{
		UStateTreeState* Current = Queue[0];
		Queue.RemoveAt(0);
		if (Current->Name == StateName) return Current;
		for (UStateTreeState* Child : Current->Children)
			if (Child) Queue.Add(Child);
	}
	return nullptr;
}

static bool StateTreeHierarchyHasNullState(UStateTreeEditorData* EditorData)
{
	if (!EditorData) return false;
	TArray<UStateTreeState*> Stack;
	for (UStateTreeState* S : EditorData->SubTrees)
	{
		if (!S) return true;
		Stack.Add(S);
	}
	while (Stack.Num() > 0)
	{
		UStateTreeState* Cur = Stack.Pop();
		for (UStateTreeState* Child : Cur->Children)
		{
			if (!Child) return true;
			Stack.Add(Child);
		}
	}
	return false;
}

static int32 SanitizeStateTreeHierarchy(UStateTreeEditorData* ED)
{
	if (!ED) return 0;
	int32 Removed = 0;

	for (int32 i = ED->SubTrees.Num() - 1; i >= 0; --i)
	{
		if (!ED->SubTrees[i]) { ED->SubTrees.RemoveAt(i); ++Removed; }
	}

	TArray<UStateTreeState*> Stack;
	for (UStateTreeState* S : ED->SubTrees) if (S) Stack.Add(S);
	while (Stack.Num() > 0)
	{
		UStateTreeState* Cur = Stack.Pop();
		if (!Cur) continue;
		for (int32 i = Cur->Children.Num() - 1; i >= 0; --i)
		{
			if (!Cur->Children[i]) { Cur->Children.RemoveAt(i); ++Removed; }
		}
		for (UStateTreeState* C : Cur->Children) if (C) Stack.Add(C);
	}
	return Removed;
}

static TArray<FString> ListAvailableStateNames(UStateTreeEditorData* EditorData)
{
	TArray<FString> Names;
	if (!EditorData) return Names;
	TArray<UStateTreeState*> Queue;
	for (UStateTreeState* S : EditorData->SubTrees)
		if (S) Queue.Add(S);
	while (Queue.Num() > 0)
	{
		UStateTreeState* Cur = Queue[0];
		Queue.RemoveAt(0);
		if (Cur && !Cur->Name.IsNone() && Cur->Name.ToString() != TEXT("Root"))
			Names.AddUnique(Cur->Name.ToString());
		if (Cur)
			for (UStateTreeState* Child : Cur->Children)
				if (Child) Queue.Add(Child);
	}
	return Names;
}

static FString FormatStateNotFoundError(UStateTreeEditorData* EditorData, const FString& StateName)
{
	const TArray<FString> Available = ListAvailableStateNames(EditorData);
	const FString List = Available.Num() > 0 ? FString::Join(Available, TEXT(", ")) : TEXT("(none — tree is empty)");
	if (StateName.IsEmpty())
		return FString::Printf(TEXT("state_name is empty. Pass a non-empty state name. Available states: %s"), *List);
	return FString::Printf(TEXT("State '%s' not found. Available states: %s"), *StateName, *List);
}

static EStateTreeTransitionTrigger TriggerFromString(const FString& Str)
{
	if (Str.Equals(TEXT("OnCompletion"), ESearchCase::IgnoreCase) || Str.Equals(TEXT("OnStateCompleted"), ESearchCase::IgnoreCase))
		return EStateTreeTransitionTrigger::OnStateCompleted;
	if (Str.Equals(TEXT("OnSucceeded"), ESearchCase::IgnoreCase) || Str.Equals(TEXT("OnStateSucceeded"), ESearchCase::IgnoreCase))
		return EStateTreeTransitionTrigger::OnStateSucceeded;
	if (Str.Equals(TEXT("OnFailed"), ESearchCase::IgnoreCase) || Str.Equals(TEXT("OnStateFailed"), ESearchCase::IgnoreCase))
		return EStateTreeTransitionTrigger::OnStateFailed;
	if (Str.Equals(TEXT("OnTick"), ESearchCase::IgnoreCase) || Str.Equals(TEXT("OnCondition"), ESearchCase::IgnoreCase))
		return EStateTreeTransitionTrigger::OnTick;
	if (Str.Equals(TEXT("OnEvent"), ESearchCase::IgnoreCase))
		return EStateTreeTransitionTrigger::OnEvent;
	return EStateTreeTransitionTrigger::OnStateCompleted;
}

static UClass* FindSchemaClass(const FString& SchemaHint)
{
	if (SchemaHint.IsEmpty()) return nullptr;

	static const TCHAR* ModulesToLoad[] = {
		TEXT("GameplayStateTree"),
		TEXT("GameplayStateTreeEditor"),
		TEXT("StateTreeModule"),
	};
	for (const TCHAR* ModName : ModulesToLoad)
	{
		if (!FModuleManager::Get().IsModuleLoaded(ModName))
			FModuleManager::Get().LoadModule(ModName);
	}

	bool bWantAI = SchemaHint.Contains(TEXT("AI"), ESearchCase::IgnoreCase)
		|| SchemaHint.Contains(TEXT("Controller"), ESearchCase::IgnoreCase);

	TArray<FString> Candidates;
	if (bWantAI || SchemaHint.Equals(TEXT("StateTreeAIComponentSchema"), ESearchCase::IgnoreCase))
	{
		Candidates.Add(TEXT("/Script/GameplayStateTreeModule.StateTreeAIComponentSchema"));
		Candidates.Add(TEXT("/Script/GameplayStateTree.StateTreeAIComponentSchema"));
	}
	Candidates.Add(FString(TEXT("/Script/GameplayStateTreeModule.")) + SchemaHint);
	Candidates.Add(FString(TEXT("/Script/GameplayStateTree.")) + SchemaHint);
	Candidates.Add(FString(TEXT("/Script/StateTreeModule.")) + SchemaHint);
	Candidates.Add(FString(TEXT("/Script/AIModule.")) + SchemaHint);

	for (const FString& Path : Candidates)
	{
		if (UClass* C = LoadObject<UClass>(nullptr, *Path))
		{
			return C;
		}
	}

	if (UClass* C = FindFirstObject<UClass>(*SchemaHint, EFindFirstObjectOptions::NativeFirst))
	{
		return C;
	}

	for (TObjectIterator<UClass> It; It; ++It)
	{
		const FString Name = It->GetName();
		if (Name.Equals(SchemaHint, ESearchCase::IgnoreCase))
		{
			return *It;
		}
	}
	if (bWantAI)
	{
		for (TObjectIterator<UClass> It; It; ++It)
		{
			if (It->GetName().Contains(TEXT("StateTreeAIComponentSchema")))
			{
				return *It;
			}
		}
	}

	return nullptr;
}

static FString ApplySchemaHint(UStateTreeEditorData* EditorData, const FString& SchemaHint)
{
	if (!EditorData || SchemaHint.IsEmpty()) return TEXT("");
	UClass* SchemaClass = FindSchemaClass(SchemaHint);
	if (!SchemaClass) return TEXT("");
	FObjectProperty* SchemaProp = FindFProperty<FObjectProperty>(EditorData->GetClass(), TEXT("Schema"));
	if (!SchemaProp) return TEXT("");
	UObject* SchemaObj = NewObject<UObject>(EditorData, SchemaClass, NAME_None, RF_Transactional);
	if (!SchemaObj) return TEXT("");
	SchemaProp->SetObjectPropertyValue_InContainer(EditorData, SchemaObj);
	return SchemaClass->GetName();
}

void HandleCreateStateTree(const FString& AssetName, const FString& SavePath,
	const FString& SchemaClass,
	FString& OutJsonString, FString& OutError)
{

	if (!UEditorAssetLibrary::DoesDirectoryExist(SavePath))
		UEditorAssetLibrary::MakeDirectory(SavePath);

	if (AssetName.IsEmpty()) { OutError = TEXT("name is required"); return; }
	FString PackagePath = SavePath;
	while (PackagePath.EndsWith(TEXT("/"))) PackagePath = PackagePath.LeftChop(1);
	PackagePath += TEXT("/") + AssetName;

	if (FPackageName::DoesPackageExist(PackagePath))
	{
		OutJsonString = FString::Printf(
			TEXT("{\"success\":true,\"asset_path\":\"%s\",\"message\":\"StateTree already exists.\"}"),
			*PackagePath);
		return;
	}

	UPackage* Package = CreatePackage(*PackagePath);
	UStateTree* ST = NewObject<UStateTree>(Package, FName(*AssetName), RF_Public | RF_Standalone | RF_Transactional);
	if (!ST) { OutError = TEXT("Failed to allocate StateTree"); return; }

	UStateTreeEditorData* EditorData = NewObject<UStateTreeEditorData>(ST, FName(), RF_Transactional);
	UStateTreeState& ContainerState = EditorData->AddSubTree(FName("Root"));
	ContainerState.ID = FGuid::NewGuid();
	ST->EditorData = EditorData;

	FString SchemaApplied = ApplySchemaHint(EditorData, SchemaClass);

	FAssetRegistryModule::AssetCreated(ST);
	ST->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(ST->GetPathName(), false);

	FString SchemaUsed = SchemaApplied.IsEmpty()
		? TEXT("none — set schema manually in asset settings")
		: SchemaApplied;

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"asset_path\":\"%s\",\"schema\":\"%s\",\"message\":\"StateTree created. Use add_state_tree_state and add_state_tree_transition to populate it.\"}"),
		*ST->GetPathName(), *SchemaUsed);
}

void HandleAddStateTreeState(const FString& AssetPath, const FString& StateName,
	const FString& ParentStateName,
	FString& OutJsonString, FString& OutError)
{

	UStateTree* ST = Cast<UStateTree>(UEditorAssetLibrary::LoadAsset(AssetPath));
	if (!ST) { OutError = TEXT("StateTree not found: ") + AssetPath; return; }

	UStateTreeEditorData* EditorData = GetEditorData(ST);
	if (!EditorData)
	{
		EditorData = NewObject<UStateTreeEditorData>(ST, FName(), RF_Transactional);
		UStateTreeState& ContainerState = EditorData->AddSubTree(FName("Root"));
		ContainerState.ID = FGuid::NewGuid();
		ST->EditorData = EditorData;
	}

	FName StateNameFName(*StateName);

	if (FindStateByName(EditorData, StateNameFName))
	{
		OutJsonString = FString::Printf(TEXT("{\"success\":true,\"state\":\"%s\",\"message\":\"State already exists\"}"), *StateName);
		return;
	}

	UStateTreeState* NewState = nullptr;

	if (ParentStateName.IsEmpty())
	{
		if (EditorData->SubTrees.Num() > 0 && EditorData->SubTrees[0])
		{
			NewState = &EditorData->SubTrees[0]->AddChildState(StateNameFName);
		}
		else
		{
			UStateTreeState& Container = EditorData->AddSubTree(FName("Root"));
			Container.ID = FGuid::NewGuid();
			NewState = &Container.AddChildState(StateNameFName);
		}
	}
	else
	{
		UStateTreeState* ParentState = FindStateByName(EditorData, FName(*ParentStateName));
		if (!ParentState)
		{
			OutError = FString::Printf(TEXT("Parent state '%s' not found"), *ParentStateName);
			return;
		}
		NewState = &ParentState->AddChildState(StateNameFName);
	}

	NewState->ID = FGuid::NewGuid();

	EditorData->MarkPackageDirty();
	ST->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(AssetPath, false);
	RefreshOpenStateTreeEditor(ST);

	FString ActualParent = ParentStateName.IsEmpty()
		? (EditorData->SubTrees.Num() > 0 && EditorData->SubTrees[0] ? EditorData->SubTrees[0]->Name.ToString() : TEXT("root"))
		: ParentStateName;
	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"state\":\"%s\",\"parent\":\"%s\",\"total_subtrees\":%d}"),
		*StateName, *ActualParent, EditorData->SubTrees.Num());
}

void HandleAddStateTreeTransition(const FString& AssetPath,
	const FString& FromState, const FString& ToState,
	const FString& TriggerType,
	FString& OutJsonString, FString& OutError)
{

	UStateTree* ST = Cast<UStateTree>(UEditorAssetLibrary::LoadAsset(AssetPath));
	if (!ST) { OutError = TEXT("StateTree not found: ") + AssetPath; return; }

	UStateTreeEditorData* EditorData = GetEditorData(ST);
	if (!EditorData) { OutError = TEXT("StateTree has no EditorData — call create_state_tree first"); return; }

	UStateTreeState* SourceState = FindStateByName(EditorData, FName(*FromState));
	if (!SourceState)
	{
		OutError = FString::Printf(TEXT("State '%s' not found. Call add_state_tree_state first."), *FromState);
		return;
	}

	UStateTreeState* TargetState = FindStateByName(EditorData, FName(*ToState));
	if (!TargetState)
	{
		OutError = FString::Printf(TEXT("Target state '%s' not found. Call add_state_tree_state first."), *ToState);
		return;
	}

	EStateTreeTransitionTrigger Trigger = TriggerFromString(TriggerType);
	FStateTreeTransition& Transition = SourceState->AddTransition(Trigger, EStateTreeTransitionType::GotoState, TargetState);
	Transition.ID = FGuid::NewGuid();

	EditorData->MarkPackageDirty();
	ST->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(AssetPath, false);
	RefreshOpenStateTreeEditor(ST);

	FString Warning;
	if (Trigger == EStateTreeTransitionTrigger::OnEvent)
	{
		Warning = TEXT(",\"warning\":\"OnEvent transitions require a Gameplay Tag and/or payload to be configured. This is NOT currently settable via tools and will cause compile errors. Use OnTick with add_state_tree_transition_condition instead for condition-based transitions.\"");
	}

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"from\":\"%s\",\"to\":\"%s\",\"trigger\":\"%s\"%s}"),
		*FromState, *ToState, *TriggerType, *Warning);
}

static void AppendStateSummary(UStateTreeState* State, FString& Out, bool& bFirst, int32 Depth = 0)
{
	if (!State) return;
	if (!bFirst) Out += TEXT(",");
	bFirst = false;

	FString Indent;
	for (int32 i = 0; i < Depth; ++i) Indent += TEXT("  ");
	Out += FString::Printf(TEXT("{\"name\":\"%s\",\"depth\":%d,\"children\":%d,\"transitions\":%d}"),
		*State->Name.ToString(), Depth, State->Children.Num(), State->Transitions.Num());

	for (UStateTreeState* Child : State->Children)
		AppendStateSummary(Child, Out, bFirst, Depth + 1);
}

void HandleGetStateTreeSummary(const FString& AssetPath,
	FString& OutJsonString, FString& OutError)
{

	UStateTree* ST = Cast<UStateTree>(UEditorAssetLibrary::LoadAsset(AssetPath));
	if (!ST) { OutError = TEXT("StateTree not found: ") + AssetPath; return; }

	UStateTreeEditorData* EditorData = GetEditorData(ST);

	FString StatesJson = TEXT("[");
	bool bFirst = true;
	int32 TotalStates = 0;

	if (EditorData)
	{
		for (UStateTreeState* SubTree : EditorData->SubTrees)
		{
			AppendStateSummary(SubTree, StatesJson, bFirst, 0);
			++TotalStates;
			TotalStates += SubTree ? SubTree->Children.Num() : 0;
		}
	}
	StatesJson += TEXT("]");

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"asset_path\":\"%s\",\"subtree_count\":%d,\"states\":%s}"),
		*AssetPath, EditorData ? EditorData->SubTrees.Num() : 0, *StatesJson);
}

void HandleGetStateTreeSchema(const FString& AssetPath,
	FString& OutJsonString, FString& OutError)
{

	UStateTree* ST = Cast<UStateTree>(UEditorAssetLibrary::LoadAsset(AssetPath));
	if (!ST) { OutError = TEXT("StateTree not found: ") + AssetPath; return; }

	UStateTreeEditorData* ED = GetEditorData(ST);

	FString SchemaClass = TEXT("none");
	FString SchemaPath = TEXT("");
	if (ED && ED->Schema)
	{
		SchemaClass = ED->Schema->GetClass()->GetName();
		SchemaPath = ED->Schema->GetClass()->GetPathName();
	}

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"asset_path\":\"%s\",\"schema_class\":\"%s\",\"schema_path\":\"%s\",\"subtree_count\":%d}"),
		*AssetPath, *SchemaClass, *SchemaPath,
		ED ? ED->SubTrees.Num() : 0);
}

static UScriptStruct* FindTaskStruct(const FString& ClassHint)
{
	if (ClassHint.IsEmpty()) return nullptr;

	if (UScriptStruct* S = FindObject<UScriptStruct>(nullptr, *ClassHint)) return S;

	if (UScriptStruct* S = FindObject<UScriptStruct>(nullptr,
		*FString::Printf(TEXT("/Script/StateTreeModule.%s"), *ClassHint))) return S;

	for (TObjectIterator<UScriptStruct> It; It; ++It)
	{
		if (It->GetName().Equals(ClassHint, ESearchCase::IgnoreCase) ||
			It->GetName().Contains(ClassHint, ESearchCase::IgnoreCase))
		{
			if (It->IsChildOf(FindObject<UScriptStruct>(nullptr, TEXT("/Script/StateTreeModule.StateTreeTaskBase"))))
				return *It;
		}
	}

	for (TObjectIterator<UScriptStruct> It; It; ++It)
	{
		if (It->GetName().Contains(ClassHint, ESearchCase::IgnoreCase))
			return *It;
	}
	return nullptr;
}

static FString ListNodeStructNames(const TCHAR* BasePath, int32 Cap = 20)
{
	UScriptStruct* Base = FindObject<UScriptStruct>(nullptr, BasePath);
	if (!Base) return FString();
	TArray<FString> Names;
	for (TObjectIterator<UScriptStruct> It; It; ++It)
	{
		if (*It != Base && It->IsChildOf(Base))
			Names.AddUnique(It->GetName());
	}
	if (Names.Num() > Cap) Names.SetNum(Cap);
	return FString::Join(Names, TEXT(", "));
}

static uint8* AddNodeToArray(FArrayProperty* ArrayProp, void* Container, const FString& Label, UScriptStruct* NodeStruct = nullptr)
{
	if (!ArrayProp) return nullptr;
	FScriptArrayHelper Helper(ArrayProp, ArrayProp->ContainerPtrToValuePtr<void>(Container));
	int32 Idx = Helper.AddValue();
	uint8* NodePtr = Helper.GetRawPtr(Idx);

	FStructProperty* NodeStructProp = CastField<FStructProperty>(ArrayProp->Inner);
	if (NodeStructProp)
	{
		if (FProperty* IdProp = FindFProperty<FProperty>(NodeStructProp->Struct, TEXT("ID")))
		{
			FGuid NewId = FGuid::NewGuid();
			FMemory::Memcpy(IdProp->ContainerPtrToValuePtr<void>(NodePtr), &NewId, sizeof(FGuid));
		}

		if (NodeStruct)
		{
			if (FStructProperty* NodeFieldProp = FindFProperty<FStructProperty>(NodeStructProp->Struct, TEXT("Node")))
			{
				void* NodeFieldPtr = NodeFieldProp->ContainerPtrToValuePtr<void>(NodePtr);
				FInstancedStruct* NodeIS = reinterpret_cast<FInstancedStruct*>(NodeFieldPtr);
				if (NodeIS)
				{
					NodeIS->InitializeAs(NodeStruct);

					if (const FStateTreeNodeBase* NodeBase = NodeIS->GetPtr<FStateTreeNodeBase>())
					{
						if (const UStruct* InstanceType = NodeBase->GetInstanceDataType())
						{
							if (const UScriptStruct* InstanceScriptStruct = Cast<UScriptStruct>(InstanceType))
							{
								if (FStructProperty* InstProp = FindFProperty<FStructProperty>(NodeStructProp->Struct, TEXT("Instance")))
								{
									void* InstPtr = InstProp->ContainerPtrToValuePtr<void>(NodePtr);
									FInstancedStruct* InstIS = reinterpret_cast<FInstancedStruct*>(InstPtr);
									if (InstIS)
										InstIS->InitializeAs(InstanceScriptStruct);
								}
							}
						}
					}
				}
			}
		}

		if (!Label.IsEmpty())
		{
			if (FNameProperty* NameProp = FindFProperty<FNameProperty>(NodeStructProp->Struct, TEXT("Name")))
				NameProp->SetPropertyValue(NameProp->ContainerPtrToValuePtr<void>(NodePtr), FName(*Label));
		}
	}
	return NodePtr;
}

void HandleAddStateTreeTask(const FString& AssetPath, const FString& StateName,
	const FString& TaskClass, const FString& TaskLabel,
	FString& OutJsonString, FString& OutError)
{

	UStateTree* ST = Cast<UStateTree>(UEditorAssetLibrary::LoadAsset(AssetPath));
	if (!ST) { OutError = TEXT("StateTree not found: ") + AssetPath; return; }

	UStateTreeEditorData* ED = GetEditorData(ST);
	if (!ED) { OutError = TEXT("StateTree has no EditorData — call create_state_tree first"); return; }

	UStateTreeState* State = FindStateByName(ED, FName(*StateName));
	if (!State) { OutError = FormatStateNotFoundError(ED, StateName); return; }

	FArrayProperty* TasksProp = FindFProperty<FArrayProperty>(UStateTreeState::StaticClass(), TEXT("Tasks"));
	if (!TasksProp)
	{
		OutError = TEXT("Tasks property not found on UStateTreeState. Verify UE5.5 StateTree headers.");
		return;
	}

	UScriptStruct* TaskStruct = FindTaskStruct(TaskClass);
	if (!TaskClass.IsEmpty() && !TaskStruct)
	{
		OutError = FString::Printf(TEXT("Unknown task_class '%s'. Valid task classes: %s"),
			*TaskClass, *ListNodeStructNames(TEXT("/Script/StateTreeModule.StateTreeTaskBase")));
		return;
	}

	uint8* NodePtr = AddNodeToArray(TasksProp, State, TaskLabel, TaskStruct);
	if (!NodePtr)
	{
		OutError = TEXT("Failed to add task node to Tasks array");
		return;
	}

	int32 TaskCount = 0;
	{
		FScriptArrayHelper Helper(TasksProp, TasksProp->ContainerPtrToValuePtr<void>(State));
		TaskCount = Helper.Num();
	}

	ED->MarkPackageDirty();
	ST->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(AssetPath, false);
	RefreshOpenStateTreeEditor(ST);

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"state\":\"%s\",\"task_class\":\"%s\",\"label\":\"%s\",\"task_count\":%d}"),
		*StateName, TaskStruct ? *TaskStruct->GetName() : TEXT("default"),
		*TaskLabel, TaskCount);
}

void HandleSetStateTreeTaskProperty(const FString& AssetPath, const FString& StateName,
	const FString& TaskLabel, const FString& PropertyName, const FString& PropertyValue,
	FString& OutJsonString, FString& OutError)
{

	UStateTree* ST = Cast<UStateTree>(UEditorAssetLibrary::LoadAsset(AssetPath));
	if (!ST) { OutError = TEXT("StateTree not found: ") + AssetPath; return; }

	UStateTreeEditorData* ED = GetEditorData(ST);
	if (!ED) { OutError = TEXT("No EditorData"); return; }

	UStateTreeState* State = FindStateByName(ED, FName(*StateName));
	if (!State) { OutError = FormatStateNotFoundError(ED, StateName); return; }

	FArrayProperty* TasksProp = FindFProperty<FArrayProperty>(UStateTreeState::StaticClass(), TEXT("Tasks"));
	if (!TasksProp) { OutError = TEXT("Tasks property not found on UStateTreeState"); return; }

	FScriptArrayHelper Helper(TasksProp, TasksProp->ContainerPtrToValuePtr<void>(State));
	FStructProperty* NodeStruct = CastField<FStructProperty>(TasksProp->Inner);

	int32 FoundIdx = -1;
	for (int32 i = 0; i < Helper.Num(); ++i)
	{
		uint8* NodePtr = Helper.GetRawPtr(i);
		if (NodeStruct)
		{
			if (FNameProperty* NameProp = FindFProperty<FNameProperty>(NodeStruct->Struct, TEXT("Name")))
			{
				FName NodeName = NameProp->GetPropertyValue(NameProp->ContainerPtrToValuePtr<void>(NodePtr));
				if (NodeName.ToString().Equals(TaskLabel, ESearchCase::IgnoreCase))
				{
					FoundIdx = i;
					break;
				}
			}
		}
	}

	if (FoundIdx < 0)
	{
		if (Helper.Num() > 0)
		{
			FoundIdx = 0;
		}
		else
		{
			OutError = FString::Printf(TEXT("No tasks in state '%s'. Call add_state_tree_task first."), *StateName);
			return;
		}
	}

	uint8* NodePtr = Helper.GetRawPtr(FoundIdx);
	if (NodeStruct)
	{
		if (FStructProperty* InstProp = FindFProperty<FStructProperty>(NodeStruct->Struct, TEXT("Instance")))
		{
			void* InstPtr = InstProp->ContainerPtrToValuePtr<void>(NodePtr);
			FString PropText = FString::Printf(TEXT("(%s=%s)"), *PropertyName, *PropertyValue);
			const TCHAR* Result = InstProp->ImportText_Direct(*PropText, InstPtr, nullptr, PPF_None, nullptr);
			if (!Result)
			{
				OutError = FString::Printf(TEXT("Failed to set property '%s' on task Instance"), *PropertyName);
				return;
			}
		}
	}

	ED->MarkPackageDirty();
	ST->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(AssetPath, false);
	RefreshOpenStateTreeEditor(ST);

	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"state\":\"%s\",\"task_index\":%d,\"property\":\"%s\"}"),
		*StateName, FoundIdx, *PropertyName);
}

void HandleAddStateTreeEvaluator(const FString& AssetPath,
	const FString& EvaluatorClass, const FString& EvaluatorLabel,
	FString& OutJsonString, FString& OutError)
{

	UStateTree* ST = Cast<UStateTree>(UEditorAssetLibrary::LoadAsset(AssetPath));
	if (!ST) { OutError = TEXT("StateTree not found: ") + AssetPath; return; }

	UStateTreeEditorData* ED = GetEditorData(ST);
	if (!ED) { OutError = TEXT("StateTree has no EditorData"); return; }

	FArrayProperty* EvalProp = FindFProperty<FArrayProperty>(UStateTreeEditorData::StaticClass(), TEXT("Evaluators"));
	if (!EvalProp)
		EvalProp = FindFProperty<FArrayProperty>(UStateTreeEditorData::StaticClass(), TEXT("Nodes"));

	if (!EvalProp)
	{
		OutError = TEXT("Evaluators/Nodes property not found on UStateTreeEditorData");
		return;
	}

	UScriptStruct* EvalStruct = FindTaskStruct(EvaluatorClass);
	if (!EvaluatorClass.IsEmpty() && !EvalStruct)
	{
		OutError = FString::Printf(TEXT("Unknown evaluator_class '%s'. Valid evaluator classes: %s"),
			*EvaluatorClass, *ListNodeStructNames(TEXT("/Script/StateTreeModule.StateTreeEvaluatorBase")));
		return;
	}

	uint8* NodePtr = AddNodeToArray(EvalProp, ED, EvaluatorLabel, EvalStruct);
	if (!NodePtr) { OutError = TEXT("Failed to add evaluator node"); return; }

	int32 EvalCount = 0;
	{
		FScriptArrayHelper Helper(EvalProp, EvalProp->ContainerPtrToValuePtr<void>(ED));
		EvalCount = Helper.Num();
	}

	ED->MarkPackageDirty();
	ST->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(AssetPath, false);
	RefreshOpenStateTreeEditor(ST);

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"evaluator_class\":\"%s\",\"label\":\"%s\",\"evaluator_count\":%d}"),
		EvalStruct ? *EvalStruct->GetName() : TEXT("default"),
		*EvaluatorLabel, EvalCount);
}

static uint8 GetEnumOrByteProp(void* Container, UStruct* Struct, const TCHAR* PropName)
{
	if (FEnumProperty* EP = FindFProperty<FEnumProperty>(Struct, PropName))
		return (uint8)EP->GetUnderlyingProperty()->GetSignedIntPropertyValue(EP->ContainerPtrToValuePtr<void>(Container));
	if (FByteProperty* BP = FindFProperty<FByteProperty>(Struct, PropName))
		return BP->GetPropertyValue(BP->ContainerPtrToValuePtr<void>(Container));
	return 255;
}

static bool SetEnumOrByteProp(void* Container, UStruct* Struct, const TCHAR* PropName, uint8 Value)
{
	if (FEnumProperty* EP = FindFProperty<FEnumProperty>(Struct, PropName))
	{
		EP->GetUnderlyingProperty()->SetIntPropertyValue(EP->ContainerPtrToValuePtr<void>(Container), (int64)Value);
		return true;
	}
	if (FByteProperty* BP = FindFProperty<FByteProperty>(Struct, PropName))
	{
		BP->SetPropertyValue(BP->ContainerPtrToValuePtr<void>(Container), Value);
		return true;
	}
	return false;
}

static FString TriggerToString(uint8 Val)
{
	if (Val == 0)    return TEXT("None");
	if (Val == 0x3)  return TEXT("OnStateCompleted");
	if (Val == 0x1)  return TEXT("OnStateSucceeded");
	if (Val == 0x2)  return TEXT("OnStateFailed");
	if (Val == 0x4)  return TEXT("OnTick");
	if (Val == 0x8)  return TEXT("OnEvent");
	return FString::Printf(TEXT("Trigger(0x%X)"), Val);
}

static FString GetNodeLabels(FArrayProperty* Prop, void* Container)
{
	if (!Prop || !Container) return TEXT("[]");
	FScriptArrayHelper Helper(Prop, Prop->ContainerPtrToValuePtr<void>(Container));
	FStructProperty* NSP = CastField<FStructProperty>(Prop->Inner);
	FString Out = TEXT("[");
	for (int32 i = 0; i < Helper.Num(); ++i)
	{
		if (i > 0) Out += TEXT(",");
		FString NodeClass;
		if (NSP)
		{
			uint8* Ptr = Helper.GetRawPtr(i);
			if (FStructProperty* NodeFieldProp = FindFProperty<FStructProperty>(NSP->Struct, TEXT("Node")))
			{
				void* NodeFieldPtr = NodeFieldProp->ContainerPtrToValuePtr<void>(Ptr);
				const FInstancedStruct* IS = reinterpret_cast<const FInstancedStruct*>(NodeFieldPtr);
				if (IS && IS->GetScriptStruct())
					NodeClass = IS->GetScriptStruct()->GetName();
			}
		}
		Out += FString::Printf(TEXT("{\"index\":%d,\"class\":\"%s\"}"), i, *NodeClass);
	}
	Out += TEXT("]");
	return Out;
}

void HandleAddStateTreeCondition(const FString& AssetPath, const FString& StateName,
	const FString& ConditionClass, const FString& ConditionLabel,
	FString& OutJsonString, FString& OutError)
{

	UStateTree* ST = Cast<UStateTree>(UEditorAssetLibrary::LoadAsset(AssetPath));
	if (!ST) { OutError = TEXT("StateTree not found: ") + AssetPath; return; }

	UStateTreeEditorData* ED = GetEditorData(ST);
	if (!ED) { OutError = TEXT("No EditorData"); return; }

	UStateTreeState* State = FindStateByName(ED, FName(*StateName));
	if (!State) { OutError = FormatStateNotFoundError(ED, StateName); return; }

	FArrayProperty* CondProp = FindFProperty<FArrayProperty>(UStateTreeState::StaticClass(), TEXT("EnterConditions"));
	if (!CondProp) { OutError = TEXT("EnterConditions property not found on UStateTreeState"); return; }

	UScriptStruct* CondStruct = FindTaskStruct(ConditionClass);
	if (!ConditionClass.IsEmpty() && !CondStruct)
	{
		OutError = FString::Printf(TEXT("Unknown condition_class '%s'. Valid condition classes: %s"),
			*ConditionClass, *ListNodeStructNames(TEXT("/Script/StateTreeModule.StateTreeConditionBase")));
		return;
	}

	uint8* NodePtr = AddNodeToArray(CondProp, State, ConditionLabel, CondStruct);
	if (!NodePtr) { OutError = TEXT("Failed to add condition node"); return; }

	int32 CondCount = 0;
	{
		FScriptArrayHelper Helper(CondProp, CondProp->ContainerPtrToValuePtr<void>(State));
		CondCount = Helper.Num();
	}

	ED->MarkPackageDirty();
	ST->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(AssetPath, false);
	RefreshOpenStateTreeEditor(ST);

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"state\":\"%s\",\"condition_class\":\"%s\",\"label\":\"%s\",\"condition_count\":%d}"),
		*StateName, CondStruct ? *CondStruct->GetName() : TEXT("default"), *ConditionLabel, CondCount);
}

void HandleGetStateTreeStateDetails(const FString& AssetPath, const FString& StateName,
	FString& OutJsonString, FString& OutError)
{

	UStateTree* ST = Cast<UStateTree>(UEditorAssetLibrary::LoadAsset(AssetPath));
	if (!ST) { OutError = TEXT("StateTree not found: ") + AssetPath; return; }

	UStateTreeEditorData* ED = GetEditorData(ST);
	if (!ED) { OutError = TEXT("No EditorData"); return; }

	UStateTreeState* State = FindStateByName(ED, FName(*StateName));
	if (!State) { OutError = FormatStateNotFoundError(ED, StateName); return; }

	FArrayProperty* TasksProp = FindFProperty<FArrayProperty>(UStateTreeState::StaticClass(), TEXT("Tasks"));
	FString TasksJson = GetNodeLabels(TasksProp, State);

	FArrayProperty* CondProp = FindFProperty<FArrayProperty>(UStateTreeState::StaticClass(), TEXT("EnterConditions"));
	FString CondsJson = GetNodeLabels(CondProp, State);

	FString TransJson = TEXT("[");
	FArrayProperty* TransProp = FindFProperty<FArrayProperty>(UStateTreeState::StaticClass(), TEXT("Transitions"));
	if (TransProp)
	{
		FScriptArrayHelper TH(TransProp, TransProp->ContainerPtrToValuePtr<void>(State));
		FStructProperty* TSP = CastField<FStructProperty>(TransProp->Inner);
		bool bFirst = true;
		for (int32 i = 0; i < TH.Num(); ++i)
		{
			if (!bFirst) TransJson += TEXT(",");
			bFirst = false;
			uint8* TPtr = TH.GetRawPtr(i);
			FString TrigStr = TEXT("Unknown"), TargetStr = TEXT("unknown");
			int32 CondCnt = 0;
			if (TSP)
			{
				uint8 TrigVal = GetEnumOrByteProp(TPtr, TSP->Struct, TEXT("Trigger"));
				if (TrigVal != 255) TrigStr = TriggerToString(TrigVal);

				if (FStructProperty* LinkProp = FindFProperty<FStructProperty>(TSP->Struct, TEXT("State")))
				{
					void* LinkPtr = LinkProp->ContainerPtrToValuePtr<void>(TPtr);
					if (FNameProperty* NP = FindFProperty<FNameProperty>(LinkProp->Struct, TEXT("Name")))
						TargetStr = NP->GetPropertyValue(NP->ContainerPtrToValuePtr<void>(LinkPtr)).ToString();
				}

				if (FArrayProperty* TCondProp = FindFProperty<FArrayProperty>(TSP->Struct, TEXT("Conditions")))
				{
					FScriptArrayHelper CH(TCondProp, TCondProp->ContainerPtrToValuePtr<void>(TPtr));
					CondCnt = CH.Num();
				}
			}
			TransJson += FString::Printf(TEXT("{\"trigger\":\"%s\",\"target\":\"%s\",\"conditions\":%d}"),
				*TrigStr, *TargetStr, CondCnt);
		}
	}
	TransJson += TEXT("]");

	FString ChildrenJson = TEXT("[");
	for (int32 i = 0; i < State->Children.Num(); ++i)
	{
		if (i > 0) ChildrenJson += TEXT(",");
		ChildrenJson += FString::Printf(TEXT("\"%s\""),
			State->Children[i] ? *State->Children[i]->Name.ToString() : TEXT("null"));
	}
	ChildrenJson += TEXT("]");

	FString StateTypeStr = TEXT("State");
	uint8 TypeVal = GetEnumOrByteProp(State, UStateTreeState::StaticClass(), TEXT("Type"));
	if (TypeVal == 255)
		TypeVal = GetEnumOrByteProp(State, UStateTreeState::StaticClass(), TEXT("StateType"));
	if (TypeVal != 255)
	{
		const UEnum* TypeEnum = StaticEnum<EStateTreeStateType>();
		if (TypeEnum && TypeEnum->IsValidEnumValue((int64)TypeVal))
			StateTypeStr = TypeEnum->GetNameStringByValue((int64)TypeVal);
		else
			StateTypeStr = FString::Printf(TEXT("Unknown(%d)"), TypeVal);
	}

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"state\":\"%s\",\"type\":\"%s\",\"tasks\":%s,\"enter_conditions\":%s,\"transitions\":%s,\"children\":%s}"),
		*StateName, *StateTypeStr, *TasksJson, *CondsJson, *TransJson, *ChildrenJson);
}

void HandleDeleteStateTreeState(const FString& AssetPath, const FString& StateName,
	FString& OutJsonString, FString& OutError)
{

	UStateTree* ST = Cast<UStateTree>(UEditorAssetLibrary::LoadAsset(AssetPath));
	if (!ST) { OutError = TEXT("StateTree not found: ") + AssetPath; return; }

	UStateTreeEditorData* ED = GetEditorData(ST);
	if (!ED) { OutError = TEXT("No EditorData"); return; }

	FName StateNameFName(*StateName);
	bool bRemoved = false;

	for (int32 i = 0; i < ED->SubTrees.Num(); ++i)
	{
		if (ED->SubTrees[i] && ED->SubTrees[i]->Name == StateNameFName)
		{
			ED->SubTrees.RemoveAt(i);
			bRemoved = true;
			break;
		}
	}

	if (!bRemoved)
	{
		TArray<UStateTreeState*> Queue;
		for (UStateTreeState* S : ED->SubTrees) if (S) Queue.Add(S);

		while (!bRemoved && Queue.Num() > 0)
		{
			UStateTreeState* Parent = Queue[0];
			Queue.RemoveAt(0);
			for (int32 i = 0; i < Parent->Children.Num(); ++i)
			{
				if (Parent->Children[i] && Parent->Children[i]->Name == StateNameFName)
				{
					Parent->Children.RemoveAt(i);
					bRemoved = true;
					break;
				}
			}
			if (!bRemoved)
				for (UStateTreeState* C : Parent->Children) if (C) Queue.Add(C);
		}
	}

	if (!bRemoved) { OutError = FormatStateNotFoundError(ED, StateName); return; }

	int32 CleanedTransitions = 0;
	FArrayProperty* TransProp = FindFProperty<FArrayProperty>(UStateTreeState::StaticClass(), TEXT("Transitions"));
	if (TransProp)
	{
		FStructProperty* TransSP = CastField<FStructProperty>(TransProp->Inner);

		TArray<UStateTreeState*> AllStates;
		TArray<UStateTreeState*> Q;
		for (UStateTreeState* S : ED->SubTrees) if (S) Q.Add(S);
		while (Q.Num() > 0)
		{
			UStateTreeState* S = Q[0]; Q.RemoveAt(0);
			AllStates.Add(S);
			for (UStateTreeState* C : S->Children) if (C) Q.Add(C);
		}

		for (UStateTreeState* S : AllStates)
		{
			FScriptArrayHelper TH(TransProp, TransProp->ContainerPtrToValuePtr<void>(S));
			for (int32 i = TH.Num() - 1; i >= 0; --i)
			{
				uint8* TPtr = TH.GetRawPtr(i);
				if (!TransSP) continue;
				if (FStructProperty* LinkProp = FindFProperty<FStructProperty>(TransSP->Struct, TEXT("State")))
				{
					void* LinkPtr = LinkProp->ContainerPtrToValuePtr<void>(TPtr);
					if (FNameProperty* NP = FindFProperty<FNameProperty>(LinkProp->Struct, TEXT("Name")))
					{
						FName TargetName = NP->GetPropertyValue(NP->ContainerPtrToValuePtr<void>(LinkPtr));
						if (TargetName == StateNameFName)
						{
							TH.RemoveValues(i, 1);
							++CleanedTransitions;
						}
					}
				}
			}
		}
	}

	ED->MarkPackageDirty();
	ST->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(AssetPath, false);
	RefreshOpenStateTreeEditor(ST);

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"deleted_state\":\"%s\",\"remaining_subtrees\":%d,\"cleaned_transitions\":%d}"),
		*StateName, ED->SubTrees.Num(), CleanedTransitions);
}

void HandleSetStateTreeStateType(const FString& AssetPath, const FString& StateName,
	const FString& StateType,
	FString& OutJsonString, FString& OutError)
{

	UStateTree* ST = Cast<UStateTree>(UEditorAssetLibrary::LoadAsset(AssetPath));
	if (!ST) { OutError = TEXT("StateTree not found: ") + AssetPath; return; }

	UStateTreeEditorData* ED = GetEditorData(ST);
	if (!ED) { OutError = TEXT("No EditorData"); return; }

	UStateTreeState* State = FindStateByName(ED, FName(*StateName));
	if (!State) { OutError = FormatStateNotFoundError(ED, StateName); return; }

	const UEnum* TypeEnum = StaticEnum<EStateTreeStateType>();
	if (!TypeEnum) { OutError = TEXT("EStateTreeStateType enum not found"); return; }

	int64 TypeVal = TypeEnum->GetValueByNameString(StateType);
	if (TypeVal == INDEX_NONE)
		TypeVal = TypeEnum->GetValueByNameString(FString::Printf(TEXT("EStateTreeStateType::%s"), *StateType));
	if (TypeVal == INDEX_NONE)
	{
		FString Valid;
		for (int32 i = 0; i < TypeEnum->NumEnums() - 1; ++i)
		{
			if (i > 0) Valid += TEXT(", ");
			Valid += TypeEnum->GetNameStringByIndex(i);
		}
		OutError = FString::Printf(TEXT("Unknown state_type '%s'. Valid values: %s"), *StateType, *Valid);
		return;
	}

	bool bSet = SetEnumOrByteProp(State, UStateTreeState::StaticClass(), TEXT("Type"), (uint8)TypeVal);
	if (!bSet) bSet = SetEnumOrByteProp(State, UStateTreeState::StaticClass(), TEXT("StateType"), (uint8)TypeVal);

	if (!bSet) { OutError = TEXT("Type/StateType property not found on UStateTreeState"); return; }

	ED->MarkPackageDirty();
	ST->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(AssetPath, false);
	RefreshOpenStateTreeEditor(ST);

	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"state\":\"%s\",\"type\":\"%s\"}"),
		*StateName, *TypeEnum->GetNameStringByValue(TypeVal));
}

void HandleListStateTreeTaskClasses(FString& OutJsonString, FString& OutError)
{

	UScriptStruct* TaskBase = FindObject<UScriptStruct>(nullptr,
		TEXT("/Script/StateTreeModule.StateTreeTaskBase"));
	UScriptStruct* EvalBase = FindObject<UScriptStruct>(nullptr,
		TEXT("/Script/StateTreeModule.StateTreeEvaluatorBase"));
	UScriptStruct* CondBase = FindObject<UScriptStruct>(nullptr,
		TEXT("/Script/StateTreeModule.StateTreeConditionBase"));

	TArray<FString> TaskClasses, EvalClasses, CondClasses;

	for (TObjectIterator<UScriptStruct> It; It; ++It)
	{
		UScriptStruct* S = *It;
		if (S == TaskBase || S == EvalBase || S == CondBase) continue;

		if (TaskBase && S->IsChildOf(TaskBase))
			TaskClasses.AddUnique(S->GetName());
		else if (EvalBase && S->IsChildOf(EvalBase))
			EvalClasses.AddUnique(S->GetName());
		else if (CondBase && S->IsChildOf(CondBase))
			CondClasses.AddUnique(S->GetName());
	}

	auto Trim = [](TArray<FString>& Arr) { if (Arr.Num() > 50) Arr.SetNum(50); };
	Trim(TaskClasses); Trim(EvalClasses); Trim(CondClasses);

	auto ToJsonArr = [](const TArray<FString>& Arr)
	{
		FString R = TEXT("[");
		for (int32 i = 0; i < Arr.Num(); ++i)
		{
			if (i > 0) R += TEXT(",");
			R += TEXT("\"") + Arr[i] + TEXT("\"");
		}
		R += TEXT("]");
		return R;
	};

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"tasks\":%s,\"evaluators\":%s,\"conditions\":%s,\"note\":\"Pass class name as task_class or evaluator_class or condition_class param.\"}"),
		*ToJsonArr(TaskClasses), *ToJsonArr(EvalClasses), *ToJsonArr(CondClasses));
}

static int32 FindTransition(UStateTreeState* State, const FString& TriggerTypeStr, const FString& ToState);

static int32 FindNodeInArray(FArrayProperty* ArrayProp, void* Container, const FString& ClassOrIndex)
{
	if (!ArrayProp || !Container) return -1;
	FScriptArrayHelper Helper(ArrayProp, ArrayProp->ContainerPtrToValuePtr<void>(Container));
	FStructProperty* NSP = CastField<FStructProperty>(ArrayProp->Inner);

	if (ClassOrIndex.IsNumeric())
	{
		int32 Idx = FCString::Atoi(*ClassOrIndex);
		return (Idx >= 0 && Idx < Helper.Num()) ? Idx : -1;
	}

	int32 LabelMatch = -1;
	for (int32 i = 0; i < Helper.Num(); ++i)
	{
		uint8* Ptr = Helper.GetRawPtr(i);
		if (!NSP) continue;

		if (FStructProperty* NodeFieldProp = FindFProperty<FStructProperty>(NSP->Struct, TEXT("Node")))
		{
			void* NodeFieldPtr = NodeFieldProp->ContainerPtrToValuePtr<void>(Ptr);
			const FInstancedStruct* IS = reinterpret_cast<const FInstancedStruct*>(NodeFieldPtr);
			if (IS && IS->GetScriptStruct())
			{
				const FString ClassName = IS->GetScriptStruct()->GetName();
				if (ClassName.Equals(ClassOrIndex, ESearchCase::IgnoreCase) ||
					ClassName.Contains(ClassOrIndex, ESearchCase::IgnoreCase))
					return i;
			}
		}

		if (LabelMatch < 0)
		{
			if (FNameProperty* NameProp = FindFProperty<FNameProperty>(NSP->Struct, TEXT("Name")))
			{
				FName NodeName = NameProp->GetPropertyValue(NameProp->ContainerPtrToValuePtr<void>(Ptr));
				if (NodeName.ToString().Equals(ClassOrIndex, ESearchCase::IgnoreCase))
					LabelMatch = i;
			}
		}
	}
	return LabelMatch;
}

static bool SetNodeProperty(FArrayProperty* ArrayProp, void* Container, int32 NodeIdx,
	const FString& PropertyName, const FString& PropertyValue)
{
	FScriptArrayHelper Helper(ArrayProp, ArrayProp->ContainerPtrToValuePtr<void>(Container));
	if (NodeIdx < 0 || NodeIdx >= Helper.Num()) return false;
	uint8* NodePtr = Helper.GetRawPtr(NodeIdx);
	FStructProperty* NSP = CastField<FStructProperty>(ArrayProp->Inner);
	if (!NSP) return false;
	if (FStructProperty* InstProp = FindFProperty<FStructProperty>(NSP->Struct, TEXT("Instance")))
	{
		void* InstPtr = InstProp->ContainerPtrToValuePtr<void>(NodePtr);
		FString PropText = FString::Printf(TEXT("(%s=%s)"), *PropertyName, *PropertyValue);
		return InstProp->ImportText_Direct(*PropText, InstPtr, nullptr, PPF_None, nullptr) != nullptr;
	}
	return false;
}

void HandleDeleteStateTreeTask(const FString& AssetPath, const FString& StateName,
	const FString& TaskClassOrIndex, FString& OutJsonString, FString& OutError)
{

	UStateTree* ST = Cast<UStateTree>(UEditorAssetLibrary::LoadAsset(AssetPath));
	if (!ST) { OutError = TEXT("StateTree not found: ") + AssetPath; return; }
	UStateTreeEditorData* ED = GetEditorData(ST);
	if (!ED) { OutError = TEXT("No EditorData"); return; }
	UStateTreeState* State = FindStateByName(ED, FName(*StateName));
	if (!State) { OutError = FormatStateNotFoundError(ED, StateName); return; }

	FArrayProperty* TasksProp = FindFProperty<FArrayProperty>(UStateTreeState::StaticClass(), TEXT("Tasks"));
	if (!TasksProp) { OutError = TEXT("Tasks property not found"); return; }

	int32 Idx = FindNodeInArray(TasksProp, State, TaskClassOrIndex);
	if (Idx < 0) { OutError = FString::Printf(TEXT("Task '%s' not found in state '%s'"), *TaskClassOrIndex, *StateName); return; }

	FScriptArrayHelper Helper(TasksProp, TasksProp->ContainerPtrToValuePtr<void>(State));
	Helper.RemoveValues(Idx, 1);

	ED->MarkPackageDirty(); ST->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(AssetPath, false);
	RefreshOpenStateTreeEditor(ST);
	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"removed_index\":%d,\"remaining_tasks\":%d}"), Idx, Helper.Num());
}

void HandleDeleteStateTreeEvaluator(const FString& AssetPath,
	const FString& EvaluatorClassOrIndex, FString& OutJsonString, FString& OutError)
{

	UStateTree* ST = Cast<UStateTree>(UEditorAssetLibrary::LoadAsset(AssetPath));
	if (!ST) { OutError = TEXT("StateTree not found: ") + AssetPath; return; }
	UStateTreeEditorData* ED = GetEditorData(ST);
	if (!ED) { OutError = TEXT("No EditorData"); return; }

	FArrayProperty* EvalProp = FindFProperty<FArrayProperty>(UStateTreeEditorData::StaticClass(), TEXT("Evaluators"));
	if (!EvalProp) EvalProp = FindFProperty<FArrayProperty>(UStateTreeEditorData::StaticClass(), TEXT("Nodes"));
	if (!EvalProp) { OutError = TEXT("Evaluators property not found"); return; }

	int32 Idx = FindNodeInArray(EvalProp, ED, EvaluatorClassOrIndex);
	if (Idx < 0) { OutError = FString::Printf(TEXT("Evaluator '%s' not found"), *EvaluatorClassOrIndex); return; }

	FScriptArrayHelper Helper(EvalProp, EvalProp->ContainerPtrToValuePtr<void>(ED));
	Helper.RemoveValues(Idx, 1);

	ED->MarkPackageDirty(); ST->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(AssetPath, false);
	RefreshOpenStateTreeEditor(ST);
	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"removed_index\":%d,\"remaining_evaluators\":%d}"), Idx, Helper.Num());
}

void HandleDeleteStateTreeCondition(const FString& AssetPath, const FString& StateName,
	const FString& ConditionClassOrIndex, FString& OutJsonString, FString& OutError)
{

	UStateTree* ST = Cast<UStateTree>(UEditorAssetLibrary::LoadAsset(AssetPath));
	if (!ST) { OutError = TEXT("StateTree not found: ") + AssetPath; return; }
	UStateTreeEditorData* ED = GetEditorData(ST);
	if (!ED) { OutError = TEXT("No EditorData"); return; }
	UStateTreeState* State = FindStateByName(ED, FName(*StateName));
	if (!State) { OutError = FormatStateNotFoundError(ED, StateName); return; }

	FArrayProperty* CondProp = FindFProperty<FArrayProperty>(UStateTreeState::StaticClass(), TEXT("EnterConditions"));
	if (!CondProp) { OutError = TEXT("EnterConditions property not found"); return; }

	int32 Idx = FindNodeInArray(CondProp, State, ConditionClassOrIndex);
	if (Idx < 0) { OutError = FString::Printf(TEXT("Condition '%s' not found in state '%s'"), *ConditionClassOrIndex, *StateName); return; }

	FScriptArrayHelper Helper(CondProp, CondProp->ContainerPtrToValuePtr<void>(State));
	Helper.RemoveValues(Idx, 1);

	ED->MarkPackageDirty(); ST->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(AssetPath, false);
	RefreshOpenStateTreeEditor(ST);
	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"removed_index\":%d,\"remaining_conditions\":%d}"), Idx, Helper.Num());
}

void HandleSetStateTreeEvaluatorProperty(const FString& AssetPath,
	const FString& EvaluatorClassOrIndex, const FString& PropertyName, const FString& PropertyValue,
	FString& OutJsonString, FString& OutError)
{

	UStateTree* ST = Cast<UStateTree>(UEditorAssetLibrary::LoadAsset(AssetPath));
	if (!ST) { OutError = TEXT("StateTree not found: ") + AssetPath; return; }
	UStateTreeEditorData* ED = GetEditorData(ST);
	if (!ED) { OutError = TEXT("No EditorData"); return; }

	FArrayProperty* EvalProp = FindFProperty<FArrayProperty>(UStateTreeEditorData::StaticClass(), TEXT("Evaluators"));
	if (!EvalProp) EvalProp = FindFProperty<FArrayProperty>(UStateTreeEditorData::StaticClass(), TEXT("Nodes"));
	if (!EvalProp) { OutError = TEXT("Evaluators property not found"); return; }

	int32 Idx = FindNodeInArray(EvalProp, ED, EvaluatorClassOrIndex);
	if (Idx < 0) { OutError = FString::Printf(TEXT("Evaluator '%s' not found"), *EvaluatorClassOrIndex); return; }

	if (!SetNodeProperty(EvalProp, ED, Idx, PropertyName, PropertyValue))
	{
		OutError = FString::Printf(TEXT("Failed to set property '%s' on evaluator"), *PropertyName);
		return;
	}

	ED->MarkPackageDirty(); ST->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(AssetPath, false);
	RefreshOpenStateTreeEditor(ST);
	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"evaluator_index\":%d,\"property\":\"%s\"}"), Idx, *PropertyName);
}

void HandleSetStateTreeConditionProperty(const FString& AssetPath, const FString& StateName,
	const FString& ConditionClassOrIndex, const FString& PropertyName, const FString& PropertyValue,
	const FString& TriggerType, const FString& ToState,
	FString& OutJsonString, FString& OutError)
{

	UStateTree* ST = Cast<UStateTree>(UEditorAssetLibrary::LoadAsset(AssetPath));
	if (!ST) { OutError = TEXT("StateTree not found: ") + AssetPath; return; }
	UStateTreeEditorData* ED = GetEditorData(ST);
	if (!ED) { OutError = TEXT("No EditorData"); return; }
	UStateTreeState* State = FindStateByName(ED, FName(*StateName));
	if (!State) { OutError = FormatStateNotFoundError(ED, StateName); return; }

	if (!TriggerType.IsEmpty() || !ToState.IsEmpty())
	{
		int32 TransIdx = FindTransition(State, TriggerType, ToState);
		if (TransIdx < 0) { OutError = FString::Printf(TEXT("Transition not found in state '%s'"), *StateName); return; }

		FArrayProperty* TransProp = FindFProperty<FArrayProperty>(UStateTreeState::StaticClass(), TEXT("Transitions"));
		FScriptArrayHelper TH(TransProp, TransProp->ContainerPtrToValuePtr<void>(State));
		uint8* TPtr = TH.GetRawPtr(TransIdx);
		FStructProperty* TSP = CastField<FStructProperty>(TransProp->Inner);
		FArrayProperty* CondsProp = TSP ? FindFProperty<FArrayProperty>(TSP->Struct, TEXT("Conditions")) : nullptr;
		if (!CondsProp) { OutError = TEXT("Conditions array not found in transition struct"); return; }

		int32 Idx = FindNodeInArray(CondsProp, TPtr, ConditionClassOrIndex);
		if (Idx < 0) { OutError = FString::Printf(TEXT("Condition '%s' not found in transition"), *ConditionClassOrIndex); return; }

		if (!SetNodeProperty(CondsProp, TPtr, Idx, PropertyName, PropertyValue))
		{
			OutError = FString::Printf(TEXT("Failed to set property '%s' on transition condition"), *PropertyName);
			return;
		}

		ED->MarkPackageDirty(); ST->MarkPackageDirty();
		UEditorAssetLibrary::SaveAsset(AssetPath, false);
	RefreshOpenStateTreeEditor(ST);
		OutJsonString = FString::Printf(TEXT("{\"success\":true,\"state\":\"%s\",\"transition_condition_index\":%d,\"property\":\"%s\"}"), *StateName, Idx, *PropertyName);
		return;
	}

	FArrayProperty* CondProp = FindFProperty<FArrayProperty>(UStateTreeState::StaticClass(), TEXT("EnterConditions"));
	if (!CondProp) { OutError = TEXT("EnterConditions property not found"); return; }

	int32 Idx = FindNodeInArray(CondProp, State, ConditionClassOrIndex);
	if (Idx < 0) { OutError = FString::Printf(TEXT("Condition '%s' not found in state '%s'"), *ConditionClassOrIndex, *StateName); return; }

	if (!SetNodeProperty(CondProp, State, Idx, PropertyName, PropertyValue))
	{
		OutError = FString::Printf(TEXT("Failed to set property '%s' on condition"), *PropertyName);
		return;
	}

	ED->MarkPackageDirty(); ST->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(AssetPath, false);
	RefreshOpenStateTreeEditor(ST);
	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"state\":\"%s\",\"condition_index\":%d,\"property\":\"%s\"}"), *StateName, Idx, *PropertyName);
}

void HandleCompileStateTree(const FString& AssetPath, FString& OutJsonString, FString& OutError)
{

	UStateTree* ST = Cast<UStateTree>(UEditorAssetLibrary::LoadAsset(AssetPath));
	if (!ST) { OutError = TEXT("StateTree not found: ") + AssetPath; return; }

	struct FStateTreeCompilerLogExposed : public FStateTreeCompilerLog
	{
		const TArray<FStateTreeCompilerLogMessage>& GetMessages() const { return Messages; }
	};
	FStateTreeCompilerLogExposed Log;
	FStateTreeCompiler Compiler(Log);
	bool bSuccess = Compiler.Compile(*ST);

	TArray<FString> Errors, Warnings;
	for (const FStateTreeCompilerLogMessage& Msg : Log.GetMessages())
	{
		FString Escaped = Msg.Message.Replace(TEXT("\""), TEXT("\\\""));
		if (Msg.Severity <= EMessageSeverity::Error)
			Errors.Add(Escaped);
		else if (Msg.Severity == EMessageSeverity::Warning)
			Warnings.Add(Escaped);
	}

	ST->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(AssetPath, false);
	RefreshOpenStateTreeEditor(ST);

	auto ToArr = [](const TArray<FString>& A)
	{
		FString R = TEXT("[");
		for (int32 i = 0; i < A.Num(); ++i) { if (i) R += TEXT(","); R += TEXT("\"") + A[i] + TEXT("\""); }
		return R + TEXT("]");
	};

	OutJsonString = FString::Printf(TEXT("{\"success\":%s,\"errors\":%s,\"warnings\":%s,\"message\":\"%s\"}"),
		bSuccess ? TEXT("true") : TEXT("false"),
		*ToArr(Errors), *ToArr(Warnings),
		bSuccess ? TEXT("Compiled successfully") : TEXT("Compilation failed — see errors"));
}

void HandleRenameStateTreeState(const FString& AssetPath, const FString& StateName,
	const FString& NewName, FString& OutJsonString, FString& OutError)
{

	UStateTree* ST = Cast<UStateTree>(UEditorAssetLibrary::LoadAsset(AssetPath));
	if (!ST) { OutError = TEXT("StateTree not found: ") + AssetPath; return; }
	UStateTreeEditorData* ED = GetEditorData(ST);
	if (!ED) { OutError = TEXT("No EditorData"); return; }

	UStateTreeState* State = FindStateByName(ED, FName(*StateName));
	if (!State) { OutError = FormatStateNotFoundError(ED, StateName); return; }

	FName OldFName(*StateName);
	FName NewFName(*NewName);
	State->Name = NewFName;

	TArray<UStateTreeState*> AllStates;
	{
		TArray<UStateTreeState*> Q;
		for (UStateTreeState* S : ED->SubTrees) if (S) Q.Add(S);
		while (Q.Num() > 0)
		{
			UStateTreeState* S = Q[0]; Q.RemoveAt(0);
			AllStates.Add(S);
			for (UStateTreeState* C : S->Children) if (C) Q.Add(C);
		}
	}

	FArrayProperty* TransProp = FindFProperty<FArrayProperty>(UStateTreeState::StaticClass(), TEXT("Transitions"));
	if (TransProp)
	{
		FStructProperty* TSP = CastField<FStructProperty>(TransProp->Inner);
		for (UStateTreeState* S : AllStates)
		{
			FScriptArrayHelper TH(TransProp, TransProp->ContainerPtrToValuePtr<void>(S));
			for (int32 i = 0; i < TH.Num(); ++i)
			{
				uint8* TPtr = TH.GetRawPtr(i);
				if (TSP)
				{
					if (FStructProperty* LinkProp = FindFProperty<FStructProperty>(TSP->Struct, TEXT("State")))
					{
						void* LinkPtr = LinkProp->ContainerPtrToValuePtr<void>(TPtr);
						if (FNameProperty* NP = FindFProperty<FNameProperty>(LinkProp->Struct, TEXT("Name")))
						{
							if (NP->GetPropertyValue(NP->ContainerPtrToValuePtr<void>(LinkPtr)) == OldFName)
								NP->SetPropertyValue(NP->ContainerPtrToValuePtr<void>(LinkPtr), NewFName);
						}
					}
				}
			}
		}
	}

	FStructProperty* LinkedProp = FindFProperty<FStructProperty>(UStateTreeState::StaticClass(), TEXT("LinkedSubtree"));
	if (LinkedProp)
	{
		for (UStateTreeState* S : AllStates)
		{
			void* LinkedPtr = LinkedProp->ContainerPtrToValuePtr<void>(S);
			if (FNameProperty* NP = FindFProperty<FNameProperty>(LinkedProp->Struct, TEXT("Name")))
			{
				if (NP->GetPropertyValue(NP->ContainerPtrToValuePtr<void>(LinkedPtr)) == OldFName)
					NP->SetPropertyValue(NP->ContainerPtrToValuePtr<void>(LinkedPtr), NewFName);
			}
		}
	}

	ED->MarkPackageDirty(); ST->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(AssetPath, false);
	RefreshOpenStateTreeEditor(ST);
	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"old_name\":\"%s\",\"new_name\":\"%s\"}"), *StateName, *NewName);
}

void HandleAddStateTreeTransitionCondition(const FString& AssetPath, const FString& StateName,
	const FString& TriggerType, const FString& ToState,
	const FString& ConditionClass, FString& OutJsonString, FString& OutError)
{

	UStateTree* ST = Cast<UStateTree>(UEditorAssetLibrary::LoadAsset(AssetPath));
	if (!ST) { OutError = TEXT("StateTree not found: ") + AssetPath; return; }
	UStateTreeEditorData* ED = GetEditorData(ST);
	if (!ED) { OutError = TEXT("No EditorData"); return; }
	UStateTreeState* State = FindStateByName(ED, FName(*StateName));
	if (!State) { OutError = FormatStateNotFoundError(ED, StateName); return; }

	FArrayProperty* TransProp = FindFProperty<FArrayProperty>(UStateTreeState::StaticClass(), TEXT("Transitions"));
	if (!TransProp) { OutError = TEXT("Transitions property not found"); return; }

	EStateTreeTransitionTrigger WantedTrigger = TriggerFromString(TriggerType);
	FName WantedTarget(*ToState);

	FScriptArrayHelper TH(TransProp, TransProp->ContainerPtrToValuePtr<void>(State));
	FStructProperty* TSP = CastField<FStructProperty>(TransProp->Inner);
	int32 FoundTrans = -1;

	for (int32 i = 0; i < TH.Num(); ++i)
	{
		uint8* TPtr = TH.GetRawPtr(i);
		if (!TSP) continue;
		uint8 TrigVal = GetEnumOrByteProp(TPtr, TSP->Struct, TEXT("Trigger"));
		bool bTrigMatch = TriggerType.IsEmpty() || (TrigVal != 255 && (uint8)WantedTrigger == TrigVal);
		bool bTargetMatch = ToState.IsEmpty();
		if (!bTargetMatch)
		{
			if (FStructProperty* LinkProp = FindFProperty<FStructProperty>(TSP->Struct, TEXT("State")))
			{
				void* LinkPtr = LinkProp->ContainerPtrToValuePtr<void>(TPtr);
				if (FNameProperty* NP = FindFProperty<FNameProperty>(LinkProp->Struct, TEXT("Name")))
					bTargetMatch = NP->GetPropertyValue(NP->ContainerPtrToValuePtr<void>(LinkPtr)) == WantedTarget;
			}
		}
		if (bTrigMatch && bTargetMatch) { FoundTrans = i; break; }
	}

	if (FoundTrans < 0) { OutError = FString::Printf(TEXT("No transition from '%s' matching trigger=%s to=%s"), *StateName, *TriggerType, *ToState); return; }

	uint8* TransPtr = TH.GetRawPtr(FoundTrans);
	FArrayProperty* CondProp = FindFProperty<FArrayProperty>(TSP->Struct, TEXT("Conditions"));
	if (!CondProp) { OutError = TEXT("Conditions array not found on transition struct"); return; }

	UScriptStruct* CondStruct = FindTaskStruct(ConditionClass);
	if (!ConditionClass.IsEmpty() && !CondStruct)
	{
		OutError = FString::Printf(TEXT("Unknown condition_class '%s'. Valid condition classes: %s"),
			*ConditionClass, *ListNodeStructNames(TEXT("/Script/StateTreeModule.StateTreeConditionBase")));
		return;
	}
	uint8* NodePtr = AddNodeToArray(CondProp, TransPtr, TEXT(""), CondStruct);
	if (!NodePtr) { OutError = TEXT("Failed to add condition to transition"); return; }

	int32 CondCount = 0;
	{ FScriptArrayHelper CH(CondProp, CondProp->ContainerPtrToValuePtr<void>(TransPtr)); CondCount = CH.Num(); }

	ED->MarkPackageDirty(); ST->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(AssetPath, false);
	RefreshOpenStateTreeEditor(ST);
	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"state\":\"%s\",\"transition_index\":%d,\"condition_class\":\"%s\",\"condition_count\":%d}"),
		*StateName, FoundTrans, CondStruct ? *CondStruct->GetName() : TEXT("default"), CondCount);
}

void HandleSetStateTreeLinkedState(const FString& AssetPath, const FString& StateName,
	const FString& LinkedStateName, FString& OutJsonString, FString& OutError)
{

	UStateTree* ST = Cast<UStateTree>(UEditorAssetLibrary::LoadAsset(AssetPath));
	if (!ST) { OutError = TEXT("StateTree not found: ") + AssetPath; return; }
	UStateTreeEditorData* ED = GetEditorData(ST);
	if (!ED) { OutError = TEXT("No EditorData"); return; }

	UStateTreeState* State = FindStateByName(ED, FName(*StateName));
	if (!State) { OutError = FormatStateNotFoundError(ED, StateName); return; }
	UStateTreeState* LinkedState = FindStateByName(ED, FName(*LinkedStateName));
	if (!LinkedState) { OutError = FString::Printf(TEXT("Linked state '%s' not found"), *LinkedStateName); return; }

	FStructProperty* LinkedProp = FindFProperty<FStructProperty>(UStateTreeState::StaticClass(), TEXT("LinkedSubtree"));
	if (!LinkedProp) { OutError = TEXT("LinkedSubtree property not found on UStateTreeState"); return; }

	void* LinkedPtr = LinkedProp->ContainerPtrToValuePtr<void>(State);
	if (FNameProperty* NP = FindFProperty<FNameProperty>(LinkedProp->Struct, TEXT("Name")))
		NP->SetPropertyValue(NP->ContainerPtrToValuePtr<void>(LinkedPtr), FName(*LinkedStateName));
	if (FProperty* IDP = FindFProperty<FProperty>(LinkedProp->Struct, TEXT("ID")))
		FMemory::Memcpy(IDP->ContainerPtrToValuePtr<void>(LinkedPtr), &LinkedState->ID, sizeof(FGuid));

	ED->MarkPackageDirty(); ST->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(AssetPath, false);
	RefreshOpenStateTreeEditor(ST);
	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"state\":\"%s\",\"linked_to\":\"%s\"}"), *StateName, *LinkedStateName);
}

void HandleReorderStateTreeTasks(const FString& AssetPath, const FString& StateName,
	const FString& FromClassOrIndex, const FString& ToIndex,
	FString& OutJsonString, FString& OutError)
{

	UStateTree* ST = Cast<UStateTree>(UEditorAssetLibrary::LoadAsset(AssetPath));
	if (!ST) { OutError = TEXT("StateTree not found: ") + AssetPath; return; }
	UStateTreeEditorData* ED = GetEditorData(ST);
	if (!ED) { OutError = TEXT("No EditorData"); return; }
	UStateTreeState* State = FindStateByName(ED, FName(*StateName));
	if (!State) { OutError = FormatStateNotFoundError(ED, StateName); return; }

	FArrayProperty* TasksProp = FindFProperty<FArrayProperty>(UStateTreeState::StaticClass(), TEXT("Tasks"));
	if (!TasksProp) { OutError = TEXT("Tasks property not found"); return; }

	int32 FromIdx = FindNodeInArray(TasksProp, State, FromClassOrIndex);
	if (FromIdx < 0) { OutError = FString::Printf(TEXT("Task '%s' not found"), *FromClassOrIndex); return; }
	if (!ToIndex.IsNumeric()) { OutError = TEXT("to_index must be an integer"); return; }
	int32 ToIdx = FCString::Atoi(*ToIndex);

	FScriptArrayHelper Helper(TasksProp, TasksProp->ContainerPtrToValuePtr<void>(State));
	if (ToIdx < 0 || ToIdx >= Helper.Num()) { OutError = FString::Printf(TEXT("to_index %d out of range (0..%d)"), ToIdx, Helper.Num()-1); return; }
	if (FromIdx == ToIdx) { OutJsonString = TEXT("{\"success\":true,\"message\":\"Already at target index\"}"); return; }

	const int32 ElemSize = TasksProp->Inner->GetSize();
	TArray<TArray<uint8>> TaskBytes;
	for (int32 i = 0; i < Helper.Num(); ++i)
	{
		TArray<uint8> Bytes; Bytes.SetNumUninitialized(ElemSize);
		FMemory::Memcpy(Bytes.GetData(), Helper.GetRawPtr(i), ElemSize);
		TaskBytes.Add(MoveTemp(Bytes));
	}
	TArray<uint8> Moving = MoveTemp(TaskBytes[FromIdx]);
	TaskBytes.RemoveAt(FromIdx);
	TaskBytes.Insert(MoveTemp(Moving), ToIdx > FromIdx ? ToIdx : ToIdx);
	for (int32 i = 0; i < Helper.Num(); ++i)
		FMemory::Memcpy(Helper.GetRawPtr(i), TaskBytes[i].GetData(), ElemSize);

	ED->MarkPackageDirty(); ST->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(AssetPath, false);
	RefreshOpenStateTreeEditor(ST);
	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"moved_from\":%d,\"moved_to\":%d}"), FromIdx, ToIdx);
}

void HandleGetStateTreeEvaluators(const FString& AssetPath, FString& OutJsonString, FString& OutError)
{

	UStateTree* ST = Cast<UStateTree>(UEditorAssetLibrary::LoadAsset(AssetPath));
	if (!ST) { OutError = TEXT("StateTree not found: ") + AssetPath; return; }
	UStateTreeEditorData* ED = GetEditorData(ST);
	if (!ED) { OutError = TEXT("No EditorData"); return; }

	FArrayProperty* EvalProp = FindFProperty<FArrayProperty>(UStateTreeEditorData::StaticClass(), TEXT("Evaluators"));
	if (!EvalProp) EvalProp = FindFProperty<FArrayProperty>(UStateTreeEditorData::StaticClass(), TEXT("Nodes"));

	FString EvalsJson = GetNodeLabels(EvalProp, ED);
	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"evaluators\":%s}"), *EvalsJson);
}

static int32 FindTransition(UStateTreeState* State, const FString& TriggerTypeStr, const FString& ToState)
{
	FArrayProperty* TransProp = FindFProperty<FArrayProperty>(UStateTreeState::StaticClass(), TEXT("Transitions"));
	if (!TransProp || !State) return -1;
	FStructProperty* TSP = CastField<FStructProperty>(TransProp->Inner);
	FScriptArrayHelper TH(TransProp, TransProp->ContainerPtrToValuePtr<void>(State));
	EStateTreeTransitionTrigger WantedTrigger = TriggerFromString(TriggerTypeStr);
	for (int32 i = 0; i < TH.Num(); ++i)
	{
		uint8* TPtr = TH.GetRawPtr(i);
		bool bTrigMatch = TriggerTypeStr.IsEmpty();
		if (!bTrigMatch && TSP)
		{
			uint8 TrigVal = GetEnumOrByteProp(TPtr, TSP->Struct, TEXT("Trigger"));
			bTrigMatch = (TrigVal != 255) && ((uint8)WantedTrigger == TrigVal);
		}
		bool bTargetMatch = ToState.IsEmpty();
		if (!bTargetMatch && TSP)
		{
			if (FStructProperty* LinkProp = FindFProperty<FStructProperty>(TSP->Struct, TEXT("State")))
			{
				void* LinkPtr = LinkProp->ContainerPtrToValuePtr<void>(TPtr);
				if (FNameProperty* NP = FindFProperty<FNameProperty>(LinkProp->Struct, TEXT("Name")))
					bTargetMatch = NP->GetPropertyValue(NP->ContainerPtrToValuePtr<void>(LinkPtr)).ToString().Equals(ToState, ESearchCase::IgnoreCase);
			}
		}
		if (bTrigMatch && bTargetMatch) return i;
	}
	return -1;
}

void HandleDeleteStateTreeTransition(const FString& AssetPath, const FString& StateName,
	const FString& TriggerType, const FString& ToState,
	FString& OutJsonString, FString& OutError)
{
	UStateTree* ST = Cast<UStateTree>(UEditorAssetLibrary::LoadAsset(AssetPath));
	if (!ST) { OutError = TEXT("StateTree not found: ") + AssetPath; return; }
	UStateTreeEditorData* ED = GetEditorData(ST);
	if (!ED) { OutError = TEXT("No EditorData"); return; }
	UStateTreeState* State = FindStateByName(ED, FName(*StateName));
	if (!State) { OutError = FormatStateNotFoundError(ED, StateName); return; }

	int32 Idx = FindTransition(State, TriggerType, ToState);
	if (Idx < 0) { OutError = FString::Printf(TEXT("No transition matching trigger=%s to=%s"), *TriggerType, *ToState); return; }

	FArrayProperty* TransProp = FindFProperty<FArrayProperty>(UStateTreeState::StaticClass(), TEXT("Transitions"));
	FScriptArrayHelper TH(TransProp, TransProp->ContainerPtrToValuePtr<void>(State));
	TH.RemoveValues(Idx, 1);
	ED->MarkPackageDirty(); ST->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(AssetPath, false);
	RefreshOpenStateTreeEditor(ST);
	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"removed_index\":%d}"), Idx);
}

void HandleSetStateTreeTransition(const FString& AssetPath, const FString& StateName,
	const FString& TriggerType, const FString& ToState,
	const FString& NewTrigger, const FString& NewToState,
	FString& OutJsonString, FString& OutError)
{
	UStateTree* ST = Cast<UStateTree>(UEditorAssetLibrary::LoadAsset(AssetPath));
	if (!ST) { OutError = TEXT("StateTree not found: ") + AssetPath; return; }
	UStateTreeEditorData* ED = GetEditorData(ST);
	if (!ED) { OutError = TEXT("No EditorData"); return; }
	UStateTreeState* State = FindStateByName(ED, FName(*StateName));
	if (!State) { OutError = FormatStateNotFoundError(ED, StateName); return; }

	int32 Idx = FindTransition(State, TriggerType, ToState);
	if (Idx < 0) { OutError = FString::Printf(TEXT("No transition matching trigger=%s to=%s"), *TriggerType, *ToState); return; }

	FArrayProperty* TransProp = FindFProperty<FArrayProperty>(UStateTreeState::StaticClass(), TEXT("Transitions"));
	FStructProperty* TSP = CastField<FStructProperty>(TransProp->Inner);
	FScriptArrayHelper TH(TransProp, TransProp->ContainerPtrToValuePtr<void>(State));
	uint8* TPtr = TH.GetRawPtr(Idx);

	if (!NewTrigger.IsEmpty() && TSP)
	{
		EStateTreeTransitionTrigger NT = TriggerFromString(NewTrigger);
		if (!SetEnumOrByteProp(TPtr, TSP->Struct, TEXT("Trigger"), (uint8)NT))
		{
			OutError = TEXT("Trigger property not found on FStateTreeTransition");
			return;
		}
	}

	if (!NewToState.IsEmpty() && TSP)
	{
		if (FStructProperty* LinkProp = FindFProperty<FStructProperty>(TSP->Struct, TEXT("State")))
		{
			void* LinkPtr = LinkProp->ContainerPtrToValuePtr<void>(TPtr);
			if (FNameProperty* NP = FindFProperty<FNameProperty>(LinkProp->Struct, TEXT("Name")))
				NP->SetPropertyValue(NP->ContainerPtrToValuePtr<void>(LinkPtr), FName(*NewToState));
			UStateTreeState* Target = FindStateByName(ED, FName(*NewToState));
			if (Target)
			{
				if (FStructProperty* IDProp = FindFProperty<FStructProperty>(LinkProp->Struct, TEXT("ID")))
					FMemory::Memcpy(IDProp->ContainerPtrToValuePtr<void>(LinkPtr), &Target->ID, sizeof(FGuid));
			}
		}
	}

	ED->MarkPackageDirty(); ST->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(AssetPath, false);
	RefreshOpenStateTreeEditor(ST);

	FString TrigStr = NewTrigger.IsEmpty() ? FString(TEXT("(unchanged)")) : NewTrigger;
	if (TSP)
	{
		uint8 TrigVal = GetEnumOrByteProp(TPtr, TSP->Struct, TEXT("Trigger"));
		if (TrigVal != 255) TrigStr = TriggerToString(TrigVal);
	}
	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"transition_index\":%d,\"trigger\":\"%s\"}"), Idx, *TrigStr);
}

void HandleDeleteStateTreeTransitionCondition(const FString& AssetPath, const FString& StateName,
	const FString& TriggerType, const FString& ToState,
	const FString& ConditionClassOrIndex, FString& OutJsonString, FString& OutError)
{
	UStateTree* ST = Cast<UStateTree>(UEditorAssetLibrary::LoadAsset(AssetPath));
	if (!ST) { OutError = TEXT("StateTree not found: ") + AssetPath; return; }
	UStateTreeEditorData* ED = GetEditorData(ST);
	if (!ED) { OutError = TEXT("No EditorData"); return; }
	UStateTreeState* State = FindStateByName(ED, FName(*StateName));
	if (!State) { OutError = FormatStateNotFoundError(ED, StateName); return; }

	int32 TransIdx = FindTransition(State, TriggerType, ToState);
	if (TransIdx < 0) { OutError = FString::Printf(TEXT("No transition matching trigger=%s to=%s"), *TriggerType, *ToState); return; }

	FArrayProperty* TransProp = FindFProperty<FArrayProperty>(UStateTreeState::StaticClass(), TEXT("Transitions"));
	FStructProperty* TSP = CastField<FStructProperty>(TransProp->Inner);
	FScriptArrayHelper TH(TransProp, TransProp->ContainerPtrToValuePtr<void>(State));
	uint8* TPtr = TH.GetRawPtr(TransIdx);

	FArrayProperty* CondsProp = TSP ? FindFProperty<FArrayProperty>(TSP->Struct, TEXT("Conditions")) : nullptr;
	if (!CondsProp) { OutError = TEXT("Conditions array not found in transition struct"); return; }

	int32 CondIdx = FindNodeInArray(CondsProp, TPtr, ConditionClassOrIndex);
	if (CondIdx < 0) { OutError = FString::Printf(TEXT("Condition '%s' not found in transition"), *ConditionClassOrIndex); return; }

	FScriptArrayHelper CH(CondsProp, CondsProp->ContainerPtrToValuePtr<void>(TPtr));
	CH.RemoveValues(CondIdx, 1);
	ED->MarkPackageDirty(); ST->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(AssetPath, false);
	RefreshOpenStateTreeEditor(ST);
	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"removed_index\":%d}"), CondIdx);
}

void HandleAddStateTreeParameter(const FString& AssetPath, const FString& ParamName,
	const FString& ParamType, FString& OutJsonString, FString& OutError)
{
	if (ParamName.IsEmpty()) { OutError = TEXT("param_name is required"); return; }
	UStateTree* ST = Cast<UStateTree>(UEditorAssetLibrary::LoadAsset(AssetPath));
	if (!ST) { OutError = TEXT("StateTree not found: ") + AssetPath; return; }
	UStateTreeEditorData* ED = GetEditorData(ST);
	if (!ED) { OutError = TEXT("No EditorData"); return; }

	const FString TL = ParamType.ToLower();
	EPropertyBagPropertyType BagType = EPropertyBagPropertyType::Float;
	UObject* ValueTypeObject = nullptr;
	bool bHandled = false;

	if      (TL == TEXT("bool"))                         BagType = EPropertyBagPropertyType::Bool;
	else if (TL == TEXT("int") || TL == TEXT("int32"))   BagType = EPropertyBagPropertyType::Int32;
	else if (TL == TEXT("int64"))                        BagType = EPropertyBagPropertyType::Int64;
	else if (TL == TEXT("float"))                        BagType = EPropertyBagPropertyType::Float;
	else if (TL == TEXT("double"))                       BagType = EPropertyBagPropertyType::Double;
	else if (TL == TEXT("string") || TL == TEXT("fstring")) BagType = EPropertyBagPropertyType::String;
	else if (TL == TEXT("name")   || TL == TEXT("fname"))   BagType = EPropertyBagPropertyType::Name;
	else if (TL == TEXT("text")   || TL == TEXT("ftext"))   BagType = EPropertyBagPropertyType::Text;
	else if (TL == TEXT("byte")   || TL == TEXT("uint8"))   BagType = EPropertyBagPropertyType::Byte;
	else if (TL == TEXT("object") || TL == TEXT("actor") || TL == TEXT("pawn") || TL == TEXT("class"))
	{
		BagType = EPropertyBagPropertyType::Object;
		if (TL == TEXT("pawn"))
			ValueTypeObject = APawn::StaticClass();
		else
			ValueTypeObject = AActor::StaticClass();
		bHandled = true;
	}
	else if (TL.StartsWith(TEXT("object:")) || TL.StartsWith(TEXT("class:")))
	{
		BagType = EPropertyBagPropertyType::Object;
		FString ClassName = ParamType.Mid(ParamType.Find(TEXT(":")) + 1).TrimStartAndEnd();
		UClass* ResolvedClass = FindFirstObject<UClass>(*ClassName);
		if (!ResolvedClass) ResolvedClass = FindFirstObject<UClass>(*(TEXT("A") + ClassName));
		if (!ResolvedClass) ResolvedClass = FindFirstObject<UClass>(*(TEXT("U") + ClassName));
		if (!ResolvedClass)
		{
			UObject* Loaded = UEditorAssetLibrary::LoadAsset(ClassName);
			if (UBlueprint* BP = Cast<UBlueprint>(Loaded))
				ResolvedClass = BP->GeneratedClass;
		}
		ValueTypeObject = ResolvedClass ? (UObject*)ResolvedClass : (UObject*)AActor::StaticClass();
		bHandled = true;
	}
	else if (TL == TEXT("vector") || TL == TEXT("fvector"))
	{
		BagType = EPropertyBagPropertyType::Struct;
		ValueTypeObject = TBaseStructure<FVector>::Get();
		bHandled = true;
	}
	else if (TL == TEXT("rotator") || TL == TEXT("frotator"))
	{
		BagType = EPropertyBagPropertyType::Struct;
		ValueTypeObject = TBaseStructure<FRotator>::Get();
		bHandled = true;
	}
	else if (TL == TEXT("transform") || TL == TEXT("ftransform"))
	{
		BagType = EPropertyBagPropertyType::Struct;
		ValueTypeObject = TBaseStructure<FTransform>::Get();
		bHandled = true;
	}
	else if (TL == TEXT("color") || TL == TEXT("fcolor"))
	{
		BagType = EPropertyBagPropertyType::Struct;
		ValueTypeObject = TBaseStructure<FColor>::Get();
		bHandled = true;
	}
	else if (TL == TEXT("linearcolor") || TL == TEXT("flinearcolor"))
	{
		BagType = EPropertyBagPropertyType::Struct;
		ValueTypeObject = TBaseStructure<FLinearColor>::Get();
		bHandled = true;
	}
	else if (TL == TEXT("gameplaytag") || TL == TEXT("fgameplaytag"))
	{
		BagType = EPropertyBagPropertyType::Struct;
		ValueTypeObject = TBaseStructure<FGameplayTag>::Get();
		bHandled = true;
	}
	else if (TL == TEXT("gameplaytagcontainer") || TL == TEXT("fgameplaytagcontainer") || TL == TEXT("tagcontainer"))
	{
		BagType = EPropertyBagPropertyType::Struct;
		ValueTypeObject = TBaseStructure<FGameplayTagContainer>::Get();
		bHandled = true;
	}
	else if (TL.StartsWith(TEXT("enum:")))
	{
		BagType = EPropertyBagPropertyType::Enum;
		FString EnumPath = ParamType.Mid(ParamType.Find(TEXT(":")) + 1).TrimStartAndEnd();
		UEnum* ResolvedEnum = LoadObject<UEnum>(nullptr, *EnumPath);
		if (!ResolvedEnum) ResolvedEnum = FindFirstObject<UEnum>(*EnumPath);
		if (!ResolvedEnum)
		{
			OutError = FString::Printf(TEXT("Could not resolve enum '%s' for param_type '%s'."), *EnumPath, *ParamType);
			return;
		}
		ValueTypeObject = ResolvedEnum;
		bHandled = true;
	}
	else if (TL.StartsWith(TEXT("struct:")))
	{
		BagType = EPropertyBagPropertyType::Struct;
		FString StructPath = ParamType.Mid(ParamType.Find(TEXT(":")) + 1).TrimStartAndEnd();
		UScriptStruct* ResolvedStruct = LoadObject<UScriptStruct>(nullptr, *StructPath);
		if (!ResolvedStruct) ResolvedStruct = FindFirstObject<UScriptStruct>(*StructPath);
		if (!ResolvedStruct)
		{
			OutError = FString::Printf(TEXT("Could not resolve struct '%s' for param_type '%s'."), *StructPath, *ParamType);
			return;
		}
		ValueTypeObject = ResolvedStruct;
		bHandled = true;
	}
	else
	{
		OutError = FString::Printf(TEXT("Unknown param_type '%s'. Supported: bool, int32, int64, float, double, string, name, text, byte, object, actor, pawn, vector, rotator, transform, color, linearcolor, gameplaytag, gameplaytagcontainer, object:ClassName, enum:EnumName, struct:/Script/Module.StructName"), *ParamType);
		return;
	}

	FInstancedPropertyBag* Bag = GetMutableRootParameterBag(ED);
	if (!Bag) { OutError = TEXT("Could not resolve root parameter bag"); return; }
	if (ValueTypeObject)
	{
		FPropertyBagPropertyDesc Desc(FName(*ParamName), BagType, ValueTypeObject);
		Bag->AddProperties({Desc});
	}
	else
	{
		Bag->AddProperty(FName(*ParamName), BagType);
	}
	BroadcastParametersChanged(ST, ED);
	ED->MarkPackageDirty(); ST->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(AssetPath, false);
	RefreshOpenStateTreeEditor(ST);
	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"name\":\"%s\",\"type\":\"%s\"}"), *ParamName, *ParamType);
}

void HandleGetStateTreeParameters(const FString& AssetPath, FString& OutJsonString, FString& OutError)
{
	UStateTree* ST = Cast<UStateTree>(UEditorAssetLibrary::LoadAsset(AssetPath));
	if (!ST) { OutError = TEXT("StateTree not found: ") + AssetPath; return; }
	UStateTreeEditorData* ED = GetEditorData(ST);
	if (!ED) { OutError = TEXT("No EditorData"); return; }

	FInstancedPropertyBag* Bag = GetMutableRootParameterBag(ED);
	const UPropertyBag* BagStruct = Bag ? Bag->GetPropertyBagStruct() : nullptr;
	FString ParamsJson = TEXT("[");
	bool bFirst = true;
	if (BagStruct)
	{
		for (const FPropertyBagPropertyDesc& Desc : BagStruct->GetPropertyDescs())
		{
			if (!bFirst) ParamsJson += TEXT(",");
			bFirst = false;
			FString TypeStr;
			switch (Desc.ValueType)
			{
			case EPropertyBagPropertyType::Bool:   TypeStr = TEXT("bool");   break;
			case EPropertyBagPropertyType::Int32:  TypeStr = TEXT("int32");  break;
			case EPropertyBagPropertyType::Int64:  TypeStr = TEXT("int64");  break;
			case EPropertyBagPropertyType::Float:  TypeStr = TEXT("float");  break;
			case EPropertyBagPropertyType::Double: TypeStr = TEXT("double"); break;
			case EPropertyBagPropertyType::String: TypeStr = TEXT("string"); break;
			case EPropertyBagPropertyType::Name:   TypeStr = TEXT("name");   break;
			case EPropertyBagPropertyType::Text:   TypeStr = TEXT("text");   break;
			case EPropertyBagPropertyType::Byte:   TypeStr = TEXT("byte");   break;
			case EPropertyBagPropertyType::Struct:
				TypeStr = Desc.ValueTypeObject ? Desc.ValueTypeObject->GetName() : TEXT("struct"); break;
			case EPropertyBagPropertyType::Object:
				TypeStr = Desc.ValueTypeObject ? Desc.ValueTypeObject->GetName() : TEXT("object"); break;
			default: TypeStr = TEXT("unknown"); break;
			}
			ParamsJson += FString::Printf(TEXT("{\"name\":\"%s\",\"type\":\"%s\"}"), *Desc.Name.ToString(), *TypeStr);
		}
	}
	ParamsJson += TEXT("]");
	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"parameters\":%s}"), *ParamsJson);
}

void HandleRemoveStateTreeParameter(const FString& AssetPath, const FString& ParamName,
	FString& OutJsonString, FString& OutError)
{
	if (ParamName.IsEmpty()) { OutError = TEXT("param_name is required"); return; }
	UStateTree* ST = Cast<UStateTree>(UEditorAssetLibrary::LoadAsset(AssetPath));
	if (!ST) { OutError = TEXT("StateTree not found: ") + AssetPath; return; }
	UStateTreeEditorData* ED = GetEditorData(ST);
	if (!ED) { OutError = TEXT("No EditorData"); return; }

	FInstancedPropertyBag* Bag = GetMutableRootParameterBag(ED);
	if (!Bag) { OutError = TEXT("Could not resolve root parameter bag"); return; }
	Bag->RemovePropertyByName(FName(*ParamName));
	BroadcastParametersChanged(ST, ED);
	ED->MarkPackageDirty(); ST->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(AssetPath, false);
	RefreshOpenStateTreeEditor(ST);
	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"removed\":\"%s\"}"), *ParamName);
}

void HandleSetStateTreeSchema(const FString& AssetPath, const FString& SchemaClass,
	FString& OutJsonString, FString& OutError)
{
	UStateTree* ST = Cast<UStateTree>(UEditorAssetLibrary::LoadAsset(AssetPath));
	if (!ST) { OutError = TEXT("StateTree not found: ") + AssetPath; return; }
	UStateTreeEditorData* ED = GetEditorData(ST);
	if (!ED) { OutError = TEXT("No EditorData"); return; }

	FString Applied = ApplySchemaHint(ED, SchemaClass);
	ED->MarkPackageDirty(); ST->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(AssetPath, false);
	RefreshOpenStateTreeEditor(ST);
	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"schema_applied\":\"%s\"}"), *Applied);
}

void HandleReorderStateTreeEvaluators(const FString& AssetPath,
	const FString& FromClassOrIndex, const FString& ToIndex,
	FString& OutJsonString, FString& OutError)
{
	UStateTree* ST = Cast<UStateTree>(UEditorAssetLibrary::LoadAsset(AssetPath));
	if (!ST) { OutError = TEXT("StateTree not found: ") + AssetPath; return; }
	UStateTreeEditorData* ED = GetEditorData(ST);
	if (!ED) { OutError = TEXT("No EditorData"); return; }

	FArrayProperty* EvalProp = FindFProperty<FArrayProperty>(UStateTreeEditorData::StaticClass(), TEXT("Evaluators"));
	if (!EvalProp) { OutError = TEXT("Evaluators property not found"); return; }

	int32 FromIdx = FindNodeInArray(EvalProp, ED, FromClassOrIndex);
	if (FromIdx < 0) { OutError = FString::Printf(TEXT("Evaluator '%s' not found"), *FromClassOrIndex); return; }
	if (!ToIndex.IsNumeric()) { OutError = TEXT("new_name must be an integer index"); return; }
	int32 ToIdx = FCString::Atoi(*ToIndex);

	FScriptArrayHelper Helper(EvalProp, EvalProp->ContainerPtrToValuePtr<void>(ED));
	if (ToIdx < 0 || ToIdx >= Helper.Num()) { OutError = FString::Printf(TEXT("to_index %d out of range (0..%d)"), ToIdx, Helper.Num()-1); return; }
	if (FromIdx == ToIdx) { OutJsonString = TEXT("{\"success\":true,\"message\":\"Already at target index\"}"); return; }

	const int32 ElemSize = EvalProp->Inner->GetSize();
	TArray<TArray<uint8>> EvalBytes;
	for (int32 i = 0; i < Helper.Num(); ++i)
	{
		TArray<uint8> Bytes; Bytes.SetNumUninitialized(ElemSize);
		FMemory::Memcpy(Bytes.GetData(), Helper.GetRawPtr(i), ElemSize);
		EvalBytes.Add(MoveTemp(Bytes));
	}
	TArray<uint8> Moving = MoveTemp(EvalBytes[FromIdx]);
	EvalBytes.RemoveAt(FromIdx);
	EvalBytes.Insert(MoveTemp(Moving), ToIdx);
	for (int32 i = 0; i < Helper.Num(); ++i)
		FMemory::Memcpy(Helper.GetRawPtr(i), EvalBytes[i].GetData(), ElemSize);

	ED->MarkPackageDirty(); ST->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(AssetPath, false);
	RefreshOpenStateTreeEditor(ST);
	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"moved_from\":%d,\"moved_to\":%d}"), FromIdx, ToIdx);
}

void HandleSetStateTreeStateSelectionBehavior(const FString& AssetPath, const FString& StateName,
	const FString& SelectionBehavior, FString& OutJsonString, FString& OutError)
{
	UStateTree* ST = Cast<UStateTree>(UEditorAssetLibrary::LoadAsset(AssetPath));
	if (!ST) { OutError = TEXT("StateTree not found: ") + AssetPath; return; }
	UStateTreeEditorData* ED = GetEditorData(ST);
	if (!ED) { OutError = TEXT("No EditorData"); return; }
	UStateTreeState* State = FindStateByName(ED, FName(*StateName));
	if (!State) { OutError = FormatStateNotFoundError(ED, StateName); return; }

	const FString BL = SelectionBehavior.ToLower();
	EStateTreeStateSelectionBehavior Behavior = EStateTreeStateSelectionBehavior::TrySelectChildrenInOrder;
	if      (BL == TEXT("tryselectchildreninorder")  || BL == TEXT("inorder"))          Behavior = EStateTreeStateSelectionBehavior::TrySelectChildrenInOrder;
#if !UE_VERSION_OLDER_THAN(5, 5, 0)
	else if (BL == TEXT("tryselectchildrenatrandom") || BL == TEXT("random"))            Behavior = EStateTreeStateSelectionBehavior::TrySelectChildrenAtRandom;
	else if (BL == TEXT("tryselectchildrenwithhighestutility") || BL == TEXT("utility")) Behavior = EStateTreeStateSelectionBehavior::TrySelectChildrenWithHighestUtility;
	else if (BL == TEXT("tryselectchildrenatrandomweightedbyutility") || BL == TEXT("weightedutility")) Behavior = EStateTreeStateSelectionBehavior::TrySelectChildrenAtRandomWeightedByUtility;
#endif
	else if (BL == TEXT("tryfollowtransitions") || BL == TEXT("follow"))                Behavior = EStateTreeStateSelectionBehavior::TryFollowTransitions;

	if (!SetEnumOrByteProp(State, UStateTreeState::StaticClass(), TEXT("SelectionBehavior"), (uint8)Behavior))
	{
		OutError = TEXT("SelectionBehavior property not found on UStateTreeState");
		return;
	}

	ED->MarkPackageDirty(); ST->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(AssetPath, false);
	RefreshOpenStateTreeEditor(ST);

	FString BehStr = SelectionBehavior;
	if (const UEnum* BehEnum = StaticEnum<EStateTreeStateSelectionBehavior>())
		BehStr = BehEnum->GetNameStringByValue((int64)Behavior);
	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"state\":\"%s\",\"selection_behavior\":\"%s\"}"), *StateName, *BehStr);
}

void HandleSetConditionOperand(const FString& AssetPath, const FString& StateName,
	const FString& ConditionClassOrIndex, const FString& Operand,
	const FString& Context, const FString& TriggerType, const FString& ToState,
	FString& OutJsonString, FString& OutError)
{
	UStateTree* ST = Cast<UStateTree>(UEditorAssetLibrary::LoadAsset(AssetPath));
	if (!ST) { OutError = TEXT("StateTree not found: ") + AssetPath; return; }
	UStateTreeEditorData* ED = GetEditorData(ST);
	if (!ED) { OutError = TEXT("No EditorData"); return; }

	const uint8 OperandVal = Operand.Equals(TEXT("Or"), ESearchCase::IgnoreCase) ? 1 : 0;
	UScriptStruct* NodeStruct = FStateTreeEditorNode::StaticStruct();
	FProperty* OperandProp = FindFProperty<FProperty>(NodeStruct, TEXT("ExpressionOperand"));
	if (!OperandProp) { OutError = TEXT("ExpressionOperand property not found on FStateTreeEditorNode"); return; }

	auto SetOnNode = [&](uint8* NodePtr) -> bool
	{
		if (FEnumProperty* EP = CastField<FEnumProperty>(OperandProp))
		{
			void* ValuePtr = EP->ContainerPtrToValuePtr<void>(NodePtr);
			EP->GetUnderlyingProperty()->SetIntPropertyValue(ValuePtr, (int64)OperandVal);
			return true;
		}
		if (FByteProperty* BP = CastField<FByteProperty>(OperandProp))
		{ *BP->ContainerPtrToValuePtr<uint8>(NodePtr) = OperandVal; return true; }
		return false;
	};

	const bool bTransitionCtx = Context.Equals(TEXT("transition"), ESearchCase::IgnoreCase);

	UStateTreeState* State = FindStateByName(ED, FName(*StateName));
	if (!State) { OutError = FormatStateNotFoundError(ED, StateName); return; }

	if (bTransitionCtx)
	{
		int32 TransIdx = FindTransition(State, TriggerType, ToState);
		if (TransIdx < 0) { OutError = FString::Printf(TEXT("Transition not found trigger=%s to=%s"), *TriggerType, *ToState); return; }

		FArrayProperty* TransProp = FindFProperty<FArrayProperty>(UStateTreeState::StaticClass(), TEXT("Transitions"));
		FStructProperty* TSP = CastField<FStructProperty>(TransProp->Inner);
		FScriptArrayHelper TH(TransProp, TransProp->ContainerPtrToValuePtr<void>(State));
		uint8* TPtr = TH.GetRawPtr(TransIdx);

		FArrayProperty* CondsProp = TSP ? FindFProperty<FArrayProperty>(TSP->Struct, TEXT("Conditions")) : nullptr;
		if (!CondsProp) { OutError = TEXT("Conditions array not found in transition"); return; }

		int32 CIdx = FindNodeInArray(CondsProp, TPtr, ConditionClassOrIndex);
		if (CIdx < 0) { OutError = FString::Printf(TEXT("Condition '%s' not found"), *ConditionClassOrIndex); return; }

		FScriptArrayHelper CH(CondsProp, CondsProp->ContainerPtrToValuePtr<void>(TPtr));
		if (!SetOnNode(CH.GetRawPtr(CIdx))) { OutError = TEXT("Could not set ExpressionOperand"); return; }
	}
	else
	{
		FArrayProperty* ECProp = FindFProperty<FArrayProperty>(UStateTreeState::StaticClass(), TEXT("EnterConditions"));
		if (!ECProp) { OutError = TEXT("EnterConditions property not found"); return; }

		int32 CIdx = FindNodeInArray(ECProp, State, ConditionClassOrIndex);
		if (CIdx < 0) { OutError = FString::Printf(TEXT("Condition '%s' not found"), *ConditionClassOrIndex); return; }

		FScriptArrayHelper CH(ECProp, ECProp->ContainerPtrToValuePtr<void>(State));
		if (!SetOnNode(CH.GetRawPtr(CIdx))) { OutError = TEXT("Could not set ExpressionOperand"); return; }
	}

	ED->MarkPackageDirty(); ST->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(AssetPath, false);
	RefreshOpenStateTreeEditor(ST);
	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"condition\":\"%s\",\"operand\":\"%s\"}"), *ConditionClassOrIndex, *Operand);
}

void HandleBindStateTreeProperty(const FString& AssetPath, const FString& StateName,
	const FString& NodeClass, const FString& NodeType, const FString& PropertyName,
	const FString& Source, const FString& Context,
	const FString& TriggerType, const FString& ToState,
	FString& OutJsonString, FString& OutError)
{

	UStateTree* ST = Cast<UStateTree>(UEditorAssetLibrary::LoadAsset(AssetPath));
	if (!ST) { OutError = TEXT("StateTree not found: ") + AssetPath; return; }
	UStateTreeEditorData* ED = GetEditorData(ST);
	if (!ED) { OutError = TEXT("No EditorData"); return; }

	FString SourceGroup, Remainder;
	if (!Source.Split(TEXT("."), &SourceGroup, &Remainder))
	{
		OutError = FString::Printf(TEXT(
			"bind_state_tree_property source '%s' must be a dotted path. Supported forms: "
			"'parameters.<ParamName>' (binds to a root parameter you added via add_state_tree_parameter), or "
			"'evaluators.<EvalLabel>.<PropName>' (binds to an evaluator's output property — add the evaluator via add_state_tree_evaluator first). "
			"NOTE: 'context.*' source paths (Owner / Controller / SchemaContextActor) are NOT exposed through this tool — those bindings are auto-injected by the StateTree schema and you don't need to add them manually."),
			*Source);
		return;
	}
	SourceGroup.TrimStartAndEndInline();
	Remainder.TrimStartAndEndInline();

	FGuid SourceStructID;
	FString SourcePropName;

	if (SourceGroup.Equals(TEXT("parameters"), ESearchCase::IgnoreCase))
	{
		SourcePropName = Remainder;
		SourceStructID = GetRootParametersBindingId(ED);
		if (!SourceStructID.IsValid())
		{
			OutError = TEXT("Root parameters binding GUID is invalid — add at least one parameter first with add_state_tree_parameter");
			return;
		}
	}
	else if (SourceGroup.Equals(TEXT("evaluators"), ESearchCase::IgnoreCase))
	{
		FString EvalLabel;
		if (!Remainder.Split(TEXT("."), &EvalLabel, &SourcePropName))
		{
			OutError = TEXT("evaluators source must be 'evaluators.Label.PropertyName'");
			return;
		}
		EvalLabel.TrimStartAndEndInline();
		SourcePropName.TrimStartAndEndInline();

		FArrayProperty* EvalProp = FindFProperty<FArrayProperty>(UStateTreeEditorData::StaticClass(), TEXT("Evaluators"));
		if (!EvalProp) { OutError = TEXT("Evaluators property not found on StateTreeEditorData"); return; }

		int32 EvalIdx = FindNodeInArray(EvalProp, ED, EvalLabel);
		if (EvalIdx < 0) { OutError = FString::Printf(TEXT("Evaluator '%s' not found — check label set via add_state_tree_evaluator"), *EvalLabel); return; }

		FScriptArrayHelper EvalHelper(EvalProp, EvalProp->ContainerPtrToValuePtr<void>(ED));
		uint8* EvalNodePtr = EvalHelper.GetRawPtr(EvalIdx);
		FStructProperty* EvalNSP = CastField<FStructProperty>(EvalProp->Inner);
		if (!EvalNSP) { OutError = TEXT("Bad evaluator array inner type"); return; }

		FProperty* IDProp = FindFProperty<FProperty>(EvalNSP->Struct, TEXT("ID"));
		if (!IDProp) { OutError = TEXT("Evaluator node has no ID field"); return; }
		FMemory::Memcpy(&SourceStructID, IDProp->ContainerPtrToValuePtr<void>(EvalNodePtr), sizeof(FGuid));
		if (!SourceStructID.IsValid()) { OutError = FString::Printf(TEXT("Evaluator '%s' has invalid ID — was it properly added?"), *EvalLabel); return; }

	}
	else
	{
		const bool bIsContext = SourceGroup.Equals(TEXT("context"), ESearchCase::IgnoreCase) ||
		                        SourceGroup.Equals(TEXT("schema"),  ESearchCase::IgnoreCase);
		OutError = FString::Printf(TEXT(
			"bind_state_tree_property source group '%s' is not supported. Supported groups: "
			"'parameters' (root params via add_state_tree_parameter) and 'evaluators' (eval outputs via add_state_tree_evaluator). %s"
			"If you want to read the AI's owning actor or controller, the StateTree schema auto-supplies those — your task/condition class likely has a 'Context' input pin already wired by the schema and doesn't need an explicit binding."),
			*SourceGroup,
			bIsContext
				? TEXT("'context.*' bindings (Owner / AIController / SchemaContextActor) are NOT exposed via bind_state_tree_property. ")
				: TEXT(""));
		return;
	}

	FGuid TargetNodeID;

	auto GetNodeID = [&](FArrayProperty* ArrProp, void* Container, const FString& ClassOrLabel) -> bool
	{
		int32 Idx = FindNodeInArray(ArrProp, Container, ClassOrLabel);
		if (Idx < 0) return false;
		FScriptArrayHelper H(ArrProp, ArrProp->ContainerPtrToValuePtr<void>(Container));
		uint8* NodePtr = H.GetRawPtr(Idx);
		FStructProperty* NSP = CastField<FStructProperty>(ArrProp->Inner);
		if (!NSP) return false;
		FProperty* IDProp = FindFProperty<FProperty>(NSP->Struct, TEXT("ID"));
		if (!IDProp) return false;
		FMemory::Memcpy(&TargetNodeID, IDProp->ContainerPtrToValuePtr<void>(NodePtr), sizeof(FGuid));
		return TargetNodeID.IsValid();
	};

	bool bFoundNode = false;
	if (NodeType.Equals(TEXT("evaluator"), ESearchCase::IgnoreCase))
	{
		FArrayProperty* EvalProp = FindFProperty<FArrayProperty>(UStateTreeEditorData::StaticClass(), TEXT("Evaluators"));
		if (EvalProp) bFoundNode = GetNodeID(EvalProp, ED, NodeClass);
	}
	else
	{
		UStateTreeState* State = StateName.IsEmpty() ? nullptr : FindStateByName(ED, FName(*StateName));
		if (!State) { OutError = FormatStateNotFoundError(ED, StateName); return; }

		if (NodeType.Equals(TEXT("task"), ESearchCase::IgnoreCase))
		{
			FArrayProperty* TaskProp = FindFProperty<FArrayProperty>(UStateTreeState::StaticClass(), TEXT("Tasks"));
			if (TaskProp) bFoundNode = GetNodeID(TaskProp, State, NodeClass);
		}
		else
		{
			if (!TriggerType.IsEmpty() || !ToState.IsEmpty())
			{
				int32 TransIdx = FindTransition(State, TriggerType, ToState);
				if (TransIdx < 0) { OutError = TEXT("Transition not found"); return; }
				FArrayProperty* TransProp = FindFProperty<FArrayProperty>(UStateTreeState::StaticClass(), TEXT("Transitions"));
				FScriptArrayHelper TH(TransProp, TransProp->ContainerPtrToValuePtr<void>(State));
				uint8* TPtr = TH.GetRawPtr(TransIdx);
				FStructProperty* TSP = CastField<FStructProperty>(TransProp->Inner);
				FArrayProperty* CondsProp = TSP ? FindFProperty<FArrayProperty>(TSP->Struct, TEXT("Conditions")) : nullptr;
				if (CondsProp) bFoundNode = GetNodeID(CondsProp, TPtr, NodeClass);
			}
			else
			{
				FArrayProperty* CondProp = FindFProperty<FArrayProperty>(UStateTreeState::StaticClass(), TEXT("EnterConditions"));
				if (CondProp) bFoundNode = GetNodeID(CondProp, State, NodeClass);
			}
		}
	}

	if (!bFoundNode) { OutError = FString::Printf(TEXT("Node '%s' (type=%s) not found"), *NodeClass, *NodeType); return; }
	if (!TargetNodeID.IsValid()) { OutError = TEXT("Target node has no valid ID"); return; }

	if (StateTreeHierarchyHasNullState(ED))
	{
		OutError = TEXT("State tree hierarchy contains a null/invalid state — binding aborted to avoid an engine crash "
		                "(UStateTreeEditorData::VisitHierarchy asserts on null states). Inspect with get_state_tree_summary "
		                "and recreate the malformed state, then retry the bind.");
		return;
	}

#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6)
	FPropertyBindingPath SourcePath(SourceStructID, FName(*SourcePropName));
	FPropertyBindingPath TargetPath(TargetNodeID, FName(*PropertyName));
	ED->EditorBindings.AddBinding(SourcePath, TargetPath);
#else
	FStateTreePropertyPath SourcePath(SourceStructID, FName(*SourcePropName));
	FStateTreePropertyPath TargetPath(TargetNodeID, FName(*PropertyName));
	ED->EditorBindings.AddPropertyBinding(SourcePath, TargetPath);
#endif

	int32 TotalBindings = ED->EditorBindings.GetBindings().Num();
	ED->MarkPackageDirty(); ST->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(AssetPath, false);
	RefreshOpenStateTreeEditor(ST);
	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"bound\":\"%s.%s\",\"to\":\"%s\",\"total_bindings\":%d}"),
		*NodeClass, *PropertyName, *Source, TotalBindings);
}

void HandleGetStateTreeBindings(const FString& AssetPath, FString& OutJsonString, FString& OutError)
{

	UStateTree* ST = Cast<UStateTree>(UEditorAssetLibrary::LoadAsset(AssetPath));
	if (!ST) { OutError = TEXT("StateTree not found: ") + AssetPath; return; }
	UStateTreeEditorData* ED = GetEditorData(ST);
	if (!ED) { OutError = TEXT("No EditorData"); return; }

	const FGuid RootParamsID = GetRootParametersBindingId(ED);

	TConstArrayView<FStateTreePropertyPathBinding> AllBindings = ED->EditorBindings.GetBindings();

	FString BindingsJson = TEXT("[");
	for (int32 i = 0; i < AllBindings.Num(); ++i)
	{
		const FStateTreePropertyPathBinding& Binding = AllBindings[i];
		const auto& SrcPath = Binding.GetSourcePath();
		const auto& TgtPath = Binding.GetTargetPath();

		FString SrcProp = SrcPath.NumSegments() > 0 ? SrcPath.GetSegment(0).GetName().ToString() : TEXT("");
		FString TgtProp = TgtPath.NumSegments() > 0 ? TgtPath.GetSegment(0).GetName().ToString() : TEXT("");
		FString SrcLabel = (SrcPath.GetStructID() == RootParamsID) ? TEXT("parameters") : SrcPath.GetStructID().ToString();

		if (i > 0) BindingsJson += TEXT(",");
		BindingsJson += FString::Printf(TEXT("{\"source\":\"%s.%s\",\"target_node_id\":\"%s\",\"target_property\":\"%s\"}"),
			*SrcLabel, *SrcProp, *TgtPath.GetStructID().ToString(), *TgtProp);
	}
	BindingsJson += TEXT("]");

	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"binding_count\":%d,\"bindings\":%s}"),
		AllBindings.Num(), *BindingsJson);
}

void HandleSetStateTreeTaskClass(const FString& AssetPath, const FString& StateName,
	const FString& TaskClassOrIndex, const FString& BlueprintClassPath,
	FString& OutJsonString, FString& OutError)
{

	UStateTree* ST = Cast<UStateTree>(UEditorAssetLibrary::LoadAsset(AssetPath));
	if (!ST) { OutError = TEXT("StateTree not found: ") + AssetPath; return; }
	UStateTreeEditorData* ED = GetEditorData(ST);
	if (!ED) { OutError = TEXT("No EditorData"); return; }

	UStateTreeState* State = FindStateByName(ED, FName(*StateName));
	if (!State) { OutError = FormatStateNotFoundError(ED, StateName); return; }

	FArrayProperty* TasksProp = FindFProperty<FArrayProperty>(UStateTreeState::StaticClass(), TEXT("Tasks"));
	if (!TasksProp) { OutError = TEXT("Tasks property not found"); return; }

	int32 Idx = FindNodeInArray(TasksProp, State, TaskClassOrIndex);
	if (Idx < 0) { OutError = FString::Printf(TEXT("Task '%s' not found in state '%s'"), *TaskClassOrIndex, *StateName); return; }

	FScriptArrayHelper Helper(TasksProp, TasksProp->ContainerPtrToValuePtr<void>(State));
	uint8* NodePtr = Helper.GetRawPtr(Idx);
	FStructProperty* NSP = CastField<FStructProperty>(TasksProp->Inner);
	if (!NSP) { OutError = TEXT("Bad node struct type"); return; }

	FStructProperty* InstProp = FindFProperty<FStructProperty>(NSP->Struct, TEXT("Instance"));
	if (!InstProp) { OutError = TEXT("Instance property not found on node"); return; }
	void* InstPtr = InstProp->ContainerPtrToValuePtr<void>(NodePtr);
	FInstancedStruct* IS = reinterpret_cast<FInstancedStruct*>(InstPtr);
	if (!IS || !IS->GetScriptStruct()) { OutError = TEXT("Task has no initialized Instance struct"); return; }

	FProperty* ClassProp = FindFProperty<FProperty>(IS->GetScriptStruct(), TEXT("TaskClass"));
	if (!ClassProp) ClassProp = FindFProperty<FProperty>(IS->GetScriptStruct(), TEXT("Class"));
	if (!ClassProp) { OutError = FString::Printf(TEXT("No 'TaskClass' property found on '%s' — only BlueprintTaskWrapper supports class assignment"), *IS->GetScriptStruct()->GetName()); return; }

	UClass* TargetClass = nullptr;
	auto TryLoadClass = [&](const FString& P) -> UClass*
	{
		if (P.IsEmpty()) return nullptr;
		return LoadObject<UClass>(nullptr, *P);
	};

	TargetClass = TryLoadClass(BlueprintClassPath);

	if (!TargetClass && BlueprintClassPath.StartsWith(TEXT("/Game/")))
	{
		FString AssetPathNoSuffix = BlueprintClassPath;
		if (AssetPathNoSuffix.EndsWith(TEXT("_C"))) AssetPathNoSuffix.LeftChopInline(2);
		int32 DotIdx;
		if (AssetPathNoSuffix.FindChar(TEXT('.'), DotIdx)) AssetPathNoSuffix.LeftInline(DotIdx);
		if (UObject* LoadedAsset = UEditorAssetLibrary::LoadAsset(AssetPathNoSuffix))
		{
			if (UBlueprint* BP = Cast<UBlueprint>(LoadedAsset))
			{
				if (BP->GeneratedClass) TargetClass = BP->GeneratedClass;
			}
			else if (UClass* AsClass = Cast<UClass>(LoadedAsset))
			{
				TargetClass = AsClass;
			}
		}
	}

	if (!TargetClass)
	{
		FString Package = BlueprintClassPath;
		int32 DotIdx;
		if (Package.FindChar(TEXT('.'), DotIdx)) Package.LeftInline(DotIdx);
		if (Package.EndsWith(TEXT("_C"))) Package.LeftChopInline(2);
		const FString Short = FPackageName::GetShortName(Package);
		const FString QualifiedClassPath = FString::Printf(TEXT("%s.%s_C"), *Package, *Short);
		TargetClass = TryLoadClass(QualifiedClassPath);
	}

	if (!TargetClass)
	{
		OutError = FString::Printf(TEXT("Could not resolve class from '%s'. Accepted forms: '/Game/BP_Name' (package path), '/Game/BP_Name.BP_Name_C' (qualified), or '/Script/Module.NativeClass'. Ensure the Blueprint has been compiled at least once (compile_blueprint) so GeneratedClass is valid."), *BlueprintClassPath);
		return;
	}

	void* DestPtr = ClassProp->ContainerPtrToValuePtr<void>(IS->GetMutableMemory());
	if (FClassProperty* CP = CastField<FClassProperty>(ClassProp))
		CP->SetObjectPropertyValue(DestPtr, TargetClass);
	else if (FSoftClassProperty* SCP = CastField<FSoftClassProperty>(ClassProp))
		SCP->SetPropertyValue(DestPtr, FSoftObjectPtr(TargetClass));
	else if (FObjectPropertyBase* OP = CastField<FObjectPropertyBase>(ClassProp))
		OP->SetObjectPropertyValue(DestPtr, TargetClass);
	else { OutError = FString::Printf(TEXT("TaskClass property type '%s' not handled"), *ClassProp->GetClass()->GetName()); return; }

	ED->MarkPackageDirty(); ST->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(AssetPath, false);
	RefreshOpenStateTreeEditor(ST);
	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"state\":\"%s\",\"task\":\"%s\",\"assigned_class\":\"%s\"}"),
		*StateName, *IS->GetScriptStruct()->GetName(), *TargetClass->GetName());
}

void HandleSetStateTreeComponentAsset(const FString& BlueprintPath, const FString& ComponentName,
	const FString& StateTreePath, FString& OutJsonString, FString& OutError)
{
	UBlueprint* BP = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(BlueprintPath));
	if (!BP) { OutError = FString::Printf(TEXT("Could not load Blueprint: %s"), *BlueprintPath); return; }

	UStateTree* ST = LoadObject<UStateTree>(nullptr, *StateTreePath);
	if (!ST)
	{
		FString WithSuffix = StateTreePath + TEXT(".") + FPackageName::GetShortName(StateTreePath);
		ST = LoadObject<UStateTree>(nullptr, *WithSuffix);
	}
	if (!ST) { OutError = FString::Printf(TEXT("Could not load StateTree asset: %s"), *StateTreePath); return; }

	UActorComponent* ComponentTemplate = nullptr;
	if (BP->SimpleConstructionScript)
	{
		for (USCS_Node* Node : BP->SimpleConstructionScript->GetAllNodes())
		{
			if (Node && Node->GetVariableName().ToString().Equals(ComponentName, ESearchCase::IgnoreCase))
			{
				ComponentTemplate = Node->GetActualComponentTemplate(Cast<UBlueprintGeneratedClass>(BP->GeneratedClass));
				if (!ComponentTemplate)
					ComponentTemplate = Node->ComponentTemplate;
				break;
			}
		}
	}
	if (!ComponentTemplate)
	{
		OutError = FString::Printf(TEXT("Component '%s' not found in Blueprint '%s'"), *ComponentName, *BlueprintPath);
		return;
	}

	FStructProperty* RefProp = FindFProperty<FStructProperty>(ComponentTemplate->GetClass(), TEXT("StateTreeRef"));
	if (RefProp)
	{
		void* StructPtr = RefProp->ContainerPtrToValuePtr<void>(ComponentTemplate);
		FObjectProperty* InnerSTProp = FindFProperty<FObjectProperty>(RefProp->Struct, TEXT("StateTree"));
		if (InnerSTProp)
		{
			InnerSTProp->SetObjectPropertyValue(InnerSTProp->ContainerPtrToValuePtr<void>(StructPtr), ST);
		}
		else
		{
			OutError = TEXT("Could not find 'StateTree' inside FStateTreeReference struct");
			return;
		}
	}
	else
	{
		FObjectProperty* ObjProp = FindFProperty<FObjectProperty>(ComponentTemplate->GetClass(), TEXT("StateTree"));
		if (!ObjProp) { OutError = TEXT("Could not find 'StateTreeRef' or 'StateTree' property on component — is this a UStateTreeComponent/UStateTreeAIComponent?"); return; }
		ObjProp->SetObjectPropertyValue(ObjProp->ContainerPtrToValuePtr<void>(ComponentTemplate), ST);
	}

	FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
	UEditorAssetLibrary::SaveAsset(BlueprintPath, false);

	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"component\":\"%s\",\"state_tree\":\"%s\",\"blueprint\":\"%s\"}"),
		*ComponentName, *ST->GetName(), *BlueprintPath);
}

void HandleRemoveStateTreeCondition(const FString& AssetPath, const FString& StateName,
	int32 ConditionIndex, FString& OutJsonString, FString& OutError)
{
	HandleDeleteStateTreeCondition(AssetPath, StateName, FString::FromInt(ConditionIndex), OutJsonString, OutError);
}

void HandleRemoveStateTreeEvaluator(const FString& AssetPath, int32 EvaluatorIndex,
	FString& OutJsonString, FString& OutError)
{
	HandleDeleteStateTreeEvaluator(AssetPath, FString::FromInt(EvaluatorIndex), OutJsonString, OutError);
}

void HandleAddStateTreeStateFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString AssetPath;
	Args->TryGetStringField(TEXT("asset_path"), AssetPath);

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("states"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString StateName = BatchToolHelper::GetItemString(Item, TEXT("state_name"), TEXT("name"));
			FString ParentState = BatchToolHelper::GetItemString(Item, TEXT("parent_state"), TEXT("parent"));
			if (StateName.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing state_name")); continue; }
			FString ItemOut, ItemErr;
			HandleAddStateTreeState(AssetPath, StateName, ParentState, ItemOut, ItemErr);
			if (ItemErr.IsEmpty()) { auto E = MakeShared<FJsonObject>(); E->SetStringField(TEXT("state_name"), StateName); Batch.AddSuccess(i, E); }
			else Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJsonString);
		return;
	}
	FString StateName, ParentState;
	Args->TryGetStringField(TEXT("state_name"), StateName);
	Args->TryGetStringField(TEXT("parent_state"), ParentState);
	HandleAddStateTreeState(AssetPath, StateName, ParentState, OutJsonString, OutError);
}

void HandleAddStateTreeTaskFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString AssetPath;
	Args->TryGetStringField(TEXT("asset_path"), AssetPath);

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("tasks"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString StateName = BatchToolHelper::GetItemString(Item, TEXT("state_name"), TEXT("state"));
			FString TaskClass = BatchToolHelper::GetItemString(Item, TEXT("task_class"), TEXT("class"));
			FString TaskLabel = BatchToolHelper::GetItemString(Item, TEXT("task_label"), TEXT("label"));
			if (StateName.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing state_name")); continue; }
			FString ItemOut, ItemErr;
			HandleAddStateTreeTask(AssetPath, StateName, TaskClass, TaskLabel, ItemOut, ItemErr);
			if (ItemErr.IsEmpty()) { auto E = MakeShared<FJsonObject>(); E->SetStringField(TEXT("state_name"), StateName); E->SetStringField(TEXT("task_class"), TaskClass); Batch.AddSuccess(i, E); }
			else Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJsonString);
		return;
	}
	FString StateName, TaskClass, TaskLabel;
	Args->TryGetStringField(TEXT("state_name"), StateName);
	Args->TryGetStringField(TEXT("task_class"), TaskClass);
	Args->TryGetStringField(TEXT("task_label"), TaskLabel);
	HandleAddStateTreeTask(AssetPath, StateName, TaskClass, TaskLabel, OutJsonString, OutError);
}

void HandleAddStateTreeTransitionFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString AssetPath;
	Args->TryGetStringField(TEXT("asset_path"), AssetPath);

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("transitions"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString FromState = BatchToolHelper::GetItemString(Item, TEXT("from_state"), TEXT("state_name"));
			FString ToState = BatchToolHelper::GetItemString(Item, TEXT("to_state"));
			FString TriggerType = BatchToolHelper::GetItemString(Item, TEXT("trigger_type"), TEXT("trigger"));
			if (TriggerType.IsEmpty()) TriggerType = TEXT("OnStateCompleted");
			if (FromState.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing from_state")); continue; }
			FString ItemOut, ItemErr;
			HandleAddStateTreeTransition(AssetPath, FromState, ToState, TriggerType, ItemOut, ItemErr);
			if (ItemErr.IsEmpty()) { auto E = MakeShared<FJsonObject>(); E->SetStringField(TEXT("from"), FromState); E->SetStringField(TEXT("to"), ToState); Batch.AddSuccess(i, E); }
			else Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJsonString);
		return;
	}
	FString FromState, ToState, TriggerType = TEXT("OnStateCompleted");
	Args->TryGetStringField(TEXT("from_state"), FromState);
	Args->TryGetStringField(TEXT("to_state"), ToState);
	if (!Args->TryGetStringField(TEXT("trigger_type"), TriggerType))
		Args->TryGetStringField(TEXT("trigger"), TriggerType);
	HandleAddStateTreeTransition(AssetPath, FromState, ToState, TriggerType, OutJsonString, OutError);
}

void HandleDeleteStateTreeStateFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString AssetPath;
	Args->TryGetStringField(TEXT("asset_path"), AssetPath);

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("states"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			FString StateName;
			if ((*ItemsArray)[i]->Type == EJson::String)
				StateName = (*ItemsArray)[i]->AsString();
			else if (auto Item = (*ItemsArray)[i]->AsObject())
				StateName = BatchToolHelper::GetItemString(Item, TEXT("state_name"), TEXT("name"));
			if (StateName.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing state_name")); continue; }
			FString ItemOut, ItemErr;
			HandleDeleteStateTreeState(AssetPath, StateName, ItemOut, ItemErr);
			if (ItemErr.IsEmpty()) { auto E = MakeShared<FJsonObject>(); E->SetStringField(TEXT("state_name"), StateName); Batch.AddSuccess(i, E); }
			else Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJsonString);
		return;
	}
	FString StateName;
	Args->TryGetStringField(TEXT("state_name"), StateName);
	HandleDeleteStateTreeState(AssetPath, StateName, OutJsonString, OutError);
}

void HandleCreateStateTreeFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!FProjectStateCache::Get().IsScanContextReady()) { OutError = TEXT("StateTree editor not ready — reload the editor and try again."); return; }
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString AssetName = TEXT("ST_NewStateTree"), SavePath, Schema;
	if (!Args->TryGetStringField(TEXT("asset_name"), AssetName) || AssetName.IsEmpty())
		Args->TryGetStringField(TEXT("name"), AssetName);
	Args->TryGetStringField(TEXT("save_path"), SavePath);
	Args->TryGetStringField(TEXT("schema_class"), Schema);
	if (SavePath.IsEmpty()) SavePath = FSettingsManager::GetDefaultSavePath();
	HandleCreateStateTree(AssetName, SavePath, Schema, OutJsonString, OutError);
}

void HandleGetStateTreeSummaryFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString AssetPath; Args->TryGetStringField(TEXT("asset_path"), AssetPath);
	HandleGetStateTreeSummary(AssetPath, OutJsonString, OutError);
}

void HandleGetStateTreeSchemaFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString AssetPath; Args->TryGetStringField(TEXT("asset_path"), AssetPath);
	HandleGetStateTreeSchema(AssetPath, OutJsonString, OutError);
}

void HandleSetStateTreeTaskPropertyFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString AssetPath;
	Args->TryGetStringField(TEXT("asset_path"), AssetPath);

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("properties"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			const FString StateName     = BatchToolHelper::GetItemString(Item, TEXT("state_name"), TEXT("state"));
			const FString TaskLabel     = BatchToolHelper::GetItemString(Item, TEXT("task_label"), TEXT("task"));
			const FString PropertyName  = BatchToolHelper::GetItemString(Item, TEXT("property_name"), TEXT("name"));
			const FString PropertyValue = BatchToolHelper::GetItemString(Item, TEXT("property_value"), TEXT("value"));
			if (StateName.IsEmpty())    { Batch.AddFailure(i, TEXT("Missing state_name")); continue; }
			if (PropertyName.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing property_name")); continue; }
			FString ItemOut, ItemErr;
			HandleSetStateTreeTaskProperty(AssetPath, StateName, TaskLabel, PropertyName, PropertyValue, ItemOut, ItemErr);
			if (ItemErr.IsEmpty())
			{
				TSharedPtr<FJsonObject> Extra = MakeShared<FJsonObject>();
				Extra->SetStringField(TEXT("state_name"), StateName);
				Extra->SetStringField(TEXT("property_name"), PropertyName);
				Batch.AddSuccess(i, Extra);
			}
			else Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJsonString);
		return;
	}

	FString StateName, TaskLabel, PropertyName, PropertyValue;
	Args->TryGetStringField(TEXT("state_name"), StateName);
	Args->TryGetStringField(TEXT("task_label"), TaskLabel);
	Args->TryGetStringField(TEXT("property_name"), PropertyName);
	Args->TryGetStringField(TEXT("property_value"), PropertyValue);
	HandleSetStateTreeTaskProperty(AssetPath, StateName, TaskLabel, PropertyName, PropertyValue, OutJsonString, OutError);
}

void HandleAddStateTreeEvaluatorFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString AssetPath, EvaluatorClass, EvaluatorLabel;
	Args->TryGetStringField(TEXT("asset_path"), AssetPath);
	Args->TryGetStringField(TEXT("evaluator_class"), EvaluatorClass);
	Args->TryGetStringField(TEXT("evaluator_label"), EvaluatorLabel);
	HandleAddStateTreeEvaluator(AssetPath, EvaluatorClass, EvaluatorLabel, OutJsonString, OutError);
}

void HandleAddStateTreeConditionFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString AssetPath, StateName, ConditionClass, ConditionLabel;
	Args->TryGetStringField(TEXT("asset_path"), AssetPath);
	Args->TryGetStringField(TEXT("state_name"), StateName);
	Args->TryGetStringField(TEXT("condition_class"), ConditionClass);
	Args->TryGetStringField(TEXT("condition_label"), ConditionLabel);
	HandleAddStateTreeCondition(AssetPath, StateName, ConditionClass, ConditionLabel, OutJsonString, OutError);
}

void HandleGetStateTreeStateDetailsFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString AssetPath;
	Args->TryGetStringField(TEXT("asset_path"), AssetPath);

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("states"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			const FString StateName = BatchToolHelper::GetItemString(Item, TEXT("state_name"), TEXT("name"));
			if (StateName.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing state_name")); continue; }
			FString ItemOut, ItemErr;
			HandleGetStateTreeStateDetails(AssetPath, StateName, ItemOut, ItemErr);
			if (ItemErr.IsEmpty())
			{
				TSharedPtr<FJsonObject> Extra = MakeShared<FJsonObject>();
				Extra->SetStringField(TEXT("state_name"), StateName);
				Extra->SetStringField(TEXT("details"), ItemOut);
				Batch.AddSuccess(i, Extra);
			}
			else
			{
				Batch.AddFailure(i, ItemErr);
			}
		}
		Batch.Finalize(OutJsonString);
		return;
	}

	FString StateName;
	Args->TryGetStringField(TEXT("state_name"), StateName);
	HandleGetStateTreeStateDetails(AssetPath, StateName, OutJsonString, OutError);
}

void HandleSetStateTreeStateTypeFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString AssetPath, StateName, StateType;
	Args->TryGetStringField(TEXT("asset_path"), AssetPath);
	Args->TryGetStringField(TEXT("state_name"), StateName);
	Args->TryGetStringField(TEXT("state_type"), StateType);
	HandleSetStateTreeStateType(AssetPath, StateName, StateType, OutJsonString, OutError);
}

void HandleListStateTreeTaskClassesFromArgs(const TSharedPtr<FJsonObject>& ,
	FString& OutJsonString, FString& OutError)
{
	HandleListStateTreeTaskClasses(OutJsonString, OutError);
}

void HandleDeleteStateTreeTaskFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString P, SN, TC;
	Args->TryGetStringField(TEXT("asset_path"), P);
	Args->TryGetStringField(TEXT("state_name"), SN);
	Args->TryGetStringField(TEXT("task_class"), TC);
	HandleDeleteStateTreeTask(P, SN, TC, OutJsonString, OutError);
}

void HandleDeleteStateTreeEvaluatorFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString P, EC;
	Args->TryGetStringField(TEXT("asset_path"), P);
	Args->TryGetStringField(TEXT("evaluator_class"), EC);
	HandleDeleteStateTreeEvaluator(P, EC, OutJsonString, OutError);
}

void HandleDeleteStateTreeConditionFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString P, SN, CC;
	Args->TryGetStringField(TEXT("asset_path"), P);
	Args->TryGetStringField(TEXT("state_name"), SN);
	Args->TryGetStringField(TEXT("condition_class"), CC);
	HandleDeleteStateTreeCondition(P, SN, CC, OutJsonString, OutError);
}

void HandleSetStateTreeEvaluatorPropertyFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString P, EC, PN, PV;
	Args->TryGetStringField(TEXT("asset_path"), P);
	Args->TryGetStringField(TEXT("evaluator_class"), EC);
	Args->TryGetStringField(TEXT("property_name"), PN);
	Args->TryGetStringField(TEXT("property_value"), PV);
	HandleSetStateTreeEvaluatorProperty(P, EC, PN, PV, OutJsonString, OutError);
}

void HandleSetStateTreeConditionPropertyFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString P, SN, CC, PN, PV, TT, TS;
	Args->TryGetStringField(TEXT("asset_path"), P);
	Args->TryGetStringField(TEXT("state_name"), SN);
	Args->TryGetStringField(TEXT("condition_class"), CC);
	Args->TryGetStringField(TEXT("property_name"), PN);
	Args->TryGetStringField(TEXT("property_value"), PV);
	Args->TryGetStringField(TEXT("trigger_type"), TT);
	Args->TryGetStringField(TEXT("to_state"), TS);
	HandleSetStateTreeConditionProperty(P, SN, CC, PN, PV, TT, TS, OutJsonString, OutError);
}

void HandleCompileStateTreeFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString P; Args->TryGetStringField(TEXT("asset_path"), P);
	HandleCompileStateTree(P, OutJsonString, OutError);
}

void HandleRenameStateTreeStateFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString P, SN, NewN;
	Args->TryGetStringField(TEXT("asset_path"), P);
	Args->TryGetStringField(TEXT("state_name"), SN);
	Args->TryGetStringField(TEXT("new_name"), NewN);
	HandleRenameStateTreeState(P, SN, NewN, OutJsonString, OutError);
}

void HandleAddStateTreeTransitionConditionFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString P, SN, Trig, To, CC;
	Args->TryGetStringField(TEXT("asset_path"), P);
	if (!Args->TryGetStringField(TEXT("state_name"), SN) || SN.IsEmpty())
		Args->TryGetStringField(TEXT("from_state"), SN);
	Args->TryGetStringField(TEXT("trigger_type"), Trig);
	Args->TryGetStringField(TEXT("to_state"), To);
	Args->TryGetStringField(TEXT("condition_class"), CC);
	HandleAddStateTreeTransitionCondition(P, SN, Trig, To, CC, OutJsonString, OutError);
}

void HandleSetStateTreeLinkedStateFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString P, SN, To;
	Args->TryGetStringField(TEXT("asset_path"), P);
	Args->TryGetStringField(TEXT("state_name"), SN);
	Args->TryGetStringField(TEXT("to_state"), To);
	HandleSetStateTreeLinkedState(P, SN, To, OutJsonString, OutError);
}

void HandleReorderStateTreeTasksFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString P, SN, From, To;
	Args->TryGetStringField(TEXT("asset_path"), P);
	Args->TryGetStringField(TEXT("state_name"), SN);
	Args->TryGetStringField(TEXT("task_class"), From);
	Args->TryGetStringField(TEXT("new_name"), To);
	HandleReorderStateTreeTasks(P, SN, From, To, OutJsonString, OutError);
}

void HandleGetStateTreeEvaluatorsFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString P; Args->TryGetStringField(TEXT("asset_path"), P);
	HandleGetStateTreeEvaluators(P, OutJsonString, OutError);
}

void HandleDeleteStateTreeTransitionFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString P, SN, Trig, To;
	Args->TryGetStringField(TEXT("asset_path"), P);
	if (!Args->TryGetStringField(TEXT("state_name"), SN) || SN.IsEmpty())
		Args->TryGetStringField(TEXT("from_state"), SN);
	Args->TryGetStringField(TEXT("trigger_type"), Trig);
	Args->TryGetStringField(TEXT("to_state"), To);
	HandleDeleteStateTreeTransition(P, SN, Trig, To, OutJsonString, OutError);
}

void HandleSetStateTreeTransitionFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString P, SN, Trig, To, NT, NTo;
	Args->TryGetStringField(TEXT("asset_path"), P);
	if (!Args->TryGetStringField(TEXT("state_name"), SN) || SN.IsEmpty())
		Args->TryGetStringField(TEXT("from_state"), SN);
	Args->TryGetStringField(TEXT("trigger_type"), Trig);
	Args->TryGetStringField(TEXT("to_state"), To);
	Args->TryGetStringField(TEXT("new_trigger"), NT);
	Args->TryGetStringField(TEXT("new_to_state"), NTo);
	HandleSetStateTreeTransition(P, SN, Trig, To, NT, NTo, OutJsonString, OutError);
}

void HandleDeleteStateTreeTransitionConditionFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString P, SN, Trig, To, CC;
	Args->TryGetStringField(TEXT("asset_path"), P);
	if (!Args->TryGetStringField(TEXT("state_name"), SN) || SN.IsEmpty())
		Args->TryGetStringField(TEXT("from_state"), SN);
	Args->TryGetStringField(TEXT("trigger_type"), Trig);
	Args->TryGetStringField(TEXT("to_state"), To);
	Args->TryGetStringField(TEXT("condition_class"), CC);
	HandleDeleteStateTreeTransitionCondition(P, SN, Trig, To, CC, OutJsonString, OutError);
}

void HandleAddStateTreeParameterFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString AssetPath;
	Args->TryGetStringField(TEXT("asset_path"), AssetPath);

	auto MaybeApplyDefault = [&](const TSharedPtr<FJsonObject>& Source, const FString& ParamName)
	{
		static const TArray<FString> ValueKeys = {
			TEXT("bool_value"), TEXT("int_value"), TEXT("float_value"), TEXT("double_value"),
			TEXT("string_value"), TEXT("name_value"), TEXT("object_value"), TEXT("class_value"),
			TEXT("enum_value"), TEXT("vector_value"), TEXT("rotator_value"), TEXT("transform_value"),
			TEXT("color_value"), TEXT("linear_color_value"), TEXT("gameplay_tag_value"),
			TEXT("gameplay_tag_container_value")
		};
		bool bAnyValue = false;
		for (const FString& K : ValueKeys) { if (Source->HasField(K)) { bAnyValue = true; break; } }
		if (!bAnyValue) return;

		UStateTree* ST = Cast<UStateTree>(UEditorAssetLibrary::LoadAsset(AssetPath));
		if (!ST) return;
		UStateTreeEditorData* ED = GetEditorData(ST);
		if (!ED) return;
		FInstancedPropertyBag* Bag = GetMutableRootParameterBag(ED);
		FString Desc, InnerErr;
		if (ApplyParameterDefault(Bag, FName(*ParamName), Source, Desc, InnerErr))
		{
			BroadcastParametersChanged(ST, ED);
			ED->MarkPackageDirty(); ST->MarkPackageDirty();
			UEditorAssetLibrary::SaveAsset(AssetPath, false);
		}
	};

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("parameters"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString PN = BatchToolHelper::GetItemString(Item, TEXT("param_name"), TEXT("name"));
			FString PT = BatchToolHelper::GetItemString(Item, TEXT("param_type"), TEXT("type"));
			if (PN.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing param_name")); continue; }
			if (PT.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing param_type")); continue; }
			FString ItemOut, ItemErr;
			HandleAddStateTreeParameter(AssetPath, PN, PT, ItemOut, ItemErr);
			if (ItemErr.IsEmpty())
			{
				MaybeApplyDefault(Item, PN);
				auto E = MakeShared<FJsonObject>();
				E->SetStringField(TEXT("name"), PN);
				E->SetStringField(TEXT("type"), PT);
				Batch.AddSuccess(i, E);
			}
			else Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJsonString);
		return;
	}

	FString PN, PT;
	Args->TryGetStringField(TEXT("param_name"), PN);
	Args->TryGetStringField(TEXT("param_type"), PT);
	HandleAddStateTreeParameter(AssetPath, PN, PT, OutJsonString, OutError);
	if (OutError.IsEmpty()) MaybeApplyDefault(Args, PN);
}

void HandleGetStateTreeParametersFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString P; Args->TryGetStringField(TEXT("asset_path"), P);
	HandleGetStateTreeParameters(P, OutJsonString, OutError);
}

void HandleRemoveStateTreeParameterFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString P, PN;
	Args->TryGetStringField(TEXT("asset_path"), P);
	Args->TryGetStringField(TEXT("param_name"), PN);
	HandleRemoveStateTreeParameter(P, PN, OutJsonString, OutError);
}

void HandleSetStateTreeSchemaFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString P, SC;
	Args->TryGetStringField(TEXT("asset_path"), P);
	Args->TryGetStringField(TEXT("schema_class"), SC);
	HandleSetStateTreeSchema(P, SC, OutJsonString, OutError);
}

void HandleReorderStateTreeEvaluatorsFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString P, From, To;
	Args->TryGetStringField(TEXT("asset_path"), P);
	Args->TryGetStringField(TEXT("evaluator_class"), From);
	Args->TryGetStringField(TEXT("new_name"), To);
	HandleReorderStateTreeEvaluators(P, From, To, OutJsonString, OutError);
}

void HandleSetStateTreeStateSelectionBehaviorFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString P, SN, SB;
	Args->TryGetStringField(TEXT("asset_path"), P);
	Args->TryGetStringField(TEXT("state_name"), SN);
	if (!Args->TryGetStringField(TEXT("behavior"), SB))
		Args->TryGetStringField(TEXT("selection_behavior"), SB);
	HandleSetStateTreeStateSelectionBehavior(P, SN, SB, OutJsonString, OutError);
}

void HandleSetConditionOperandFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString P, SN, CC, Op, Ctx, Trig, To;
	Args->TryGetStringField(TEXT("asset_path"), P);
	Args->TryGetStringField(TEXT("state_name"), SN);
	Args->TryGetStringField(TEXT("condition_class"), CC);
	Args->TryGetStringField(TEXT("operand"), Op);
	Args->TryGetStringField(TEXT("context"), Ctx);
	Args->TryGetStringField(TEXT("trigger_type"), Trig);
	Args->TryGetStringField(TEXT("to_state"), To);
	HandleSetConditionOperand(P, SN, CC, Op, Ctx, Trig, To, OutJsonString, OutError);
}

void HandleBindStateTreePropertyFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString P, SN, NC, NT, PN, Src, Ctx, Trig, To;
	Args->TryGetStringField(TEXT("asset_path"), P);
	Args->TryGetStringField(TEXT("state_name"), SN);
	Args->TryGetStringField(TEXT("node_class"), NC);
	Args->TryGetStringField(TEXT("node_type"), NT);
	Args->TryGetStringField(TEXT("property_name"), PN);
	Args->TryGetStringField(TEXT("source"), Src);
	Args->TryGetStringField(TEXT("context"), Ctx);
	Args->TryGetStringField(TEXT("trigger_type"), Trig);
	Args->TryGetStringField(TEXT("to_state"), To);
	HandleBindStateTreeProperty(P, SN, NC, NT, PN, Src, Ctx, Trig, To, OutJsonString, OutError);
}

void HandleGetStateTreeBindingsFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString P; Args->TryGetStringField(TEXT("asset_path"), P);
	HandleGetStateTreeBindings(P, OutJsonString, OutError);
}

void HandleSetStateTreeTaskClassFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString P, SN, TC, BC;
	Args->TryGetStringField(TEXT("asset_path"), P);
	Args->TryGetStringField(TEXT("state_name"), SN);
	Args->TryGetStringField(TEXT("task_class"), TC);
	Args->TryGetStringField(TEXT("blueprint_class"), BC);
	HandleSetStateTreeTaskClass(P, SN, TC, BC, OutJsonString, OutError);
}

void HandleSetStateTreeComponentAssetFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString BP, CN, STP;
	Args->TryGetStringField(TEXT("blueprint_path"), BP);
	Args->TryGetStringField(TEXT("component_name"), CN);
	Args->TryGetStringField(TEXT("state_tree_path"), STP);
	HandleSetStateTreeComponentAsset(BP, CN, STP, OutJsonString, OutError);
}

void HandleRemoveStateTreeConditionFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString P, SN;
	int32 CI = 0;
	Args->TryGetStringField(TEXT("asset_path"), P);
	Args->TryGetStringField(TEXT("state_name"), SN);
	Args->TryGetNumberField(TEXT("condition_index"), CI);
	HandleRemoveStateTreeCondition(P, SN, CI, OutJsonString, OutError);
}

void HandleRemoveStateTreeEvaluatorFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString P;
	int32 EI = 0;
	Args->TryGetStringField(TEXT("asset_path"), P);
	Args->TryGetNumberField(TEXT("evaluator_index"), EI);
	HandleRemoveStateTreeEvaluator(P, EI, OutJsonString, OutError);
}

static bool ApplyParameterDefault(FInstancedPropertyBag* Bag, const FName ParamName,
	const TSharedPtr<FJsonObject>& Args, FString& OutDescription, FString& OutErr)
{
		if (!Bag) { OutErr = TEXT("Bag is null"); return false; }
		const UPropertyBag* BagStruct = Bag->GetPropertyBagStruct();
		if (!BagStruct) { OutErr = TEXT("Bag has no struct"); return false; }

		const FPropertyBagPropertyDesc* Desc = nullptr;
		for (const FPropertyBagPropertyDesc& D : BagStruct->GetPropertyDescs())
			if (D.Name == ParamName) { Desc = &D; break; }
		if (!Desc) { OutErr = FString::Printf(TEXT("Parameter '%s' not found on bag — call add_state_tree_parameter first."), *ParamName.ToString()); return false; }

		EPropertyBagResult Result = EPropertyBagResult::PropertyNotFound;

		auto Try = [&](const TCHAR* Key, auto&& Body) -> bool
		{
			const TSharedPtr<FJsonValue>* V = nullptr;
			if (Args->Values.Find(Key))
			{
				V = &Args->Values[Key];
				Body(*V);
				return true;
			}
			return false;
		};

		switch (Desc->ValueType)
		{
		case EPropertyBagPropertyType::Bool:
		{
			bool BV = false;
			if (!Args->TryGetBoolField(TEXT("bool_value"), BV)) { OutErr = TEXT("bool_value required for bool parameter"); return false; }
			Result = Bag->SetValueBool(ParamName, BV);
			OutDescription = BV ? TEXT("true") : TEXT("false");
			break;
		}
		case EPropertyBagPropertyType::Byte:
		{
			double NV = 0; if (!Args->TryGetNumberField(TEXT("int_value"), NV)) { OutErr = TEXT("int_value required for byte parameter"); return false; }
			Result = Bag->SetValueByte(ParamName, (uint8)NV);
			OutDescription = FString::Printf(TEXT("%d"), (int32)NV);
			break;
		}
		case EPropertyBagPropertyType::Int32:
		{
			double NV = 0; if (!Args->TryGetNumberField(TEXT("int_value"), NV)) { OutErr = TEXT("int_value required for int32 parameter"); return false; }
			Result = Bag->SetValueInt32(ParamName, (int32)NV);
			OutDescription = FString::Printf(TEXT("%d"), (int32)NV);
			break;
		}
		case EPropertyBagPropertyType::Int64:
		{
			double NV = 0; if (!Args->TryGetNumberField(TEXT("int_value"), NV)) { OutErr = TEXT("int_value required for int64 parameter"); return false; }
			Result = Bag->SetValueInt64(ParamName, (int64)NV);
			OutDescription = FString::Printf(TEXT("%lld"), (int64)NV);
			break;
		}
		case EPropertyBagPropertyType::Float:
		{
			double NV = 0; if (!Args->TryGetNumberField(TEXT("float_value"), NV) && !Args->TryGetNumberField(TEXT("double_value"), NV)) { OutErr = TEXT("float_value required for float parameter"); return false; }
			Result = Bag->SetValueFloat(ParamName, (float)NV);
			OutDescription = FString::Printf(TEXT("%g"), NV);
			break;
		}
		case EPropertyBagPropertyType::Double:
		{
			double NV = 0; if (!Args->TryGetNumberField(TEXT("double_value"), NV) && !Args->TryGetNumberField(TEXT("float_value"), NV)) { OutErr = TEXT("double_value required for double parameter"); return false; }
			Result = Bag->SetValueDouble(ParamName, NV);
			OutDescription = FString::Printf(TEXT("%g"), NV);
			break;
		}
		case EPropertyBagPropertyType::String:
		{
			FString SV; if (!Args->TryGetStringField(TEXT("string_value"), SV)) { OutErr = TEXT("string_value required for string parameter"); return false; }
			Result = Bag->SetValueString(ParamName, SV);
			OutDescription = SV;
			break;
		}
		case EPropertyBagPropertyType::Name:
		{
			FString SV; if (!Args->TryGetStringField(TEXT("name_value"), SV) && !Args->TryGetStringField(TEXT("string_value"), SV)) { OutErr = TEXT("name_value required for name parameter"); return false; }
			Result = Bag->SetValueName(ParamName, FName(*SV));
			OutDescription = SV;
			break;
		}
		case EPropertyBagPropertyType::Object:
		{
			FString SV; Args->TryGetStringField(TEXT("object_value"), SV);
			if (SV.IsEmpty()) Args->TryGetStringField(TEXT("string_value"), SV);
			if (SV.IsEmpty()) { OutErr = TEXT("object_value (asset path) required for object parameter"); return false; }
			UObject* Obj = UEditorAssetLibrary::LoadAsset(SV);
			Result = Bag->SetValueObject(ParamName, Obj);
			OutDescription = SV;
			break;
		}
		case EPropertyBagPropertyType::Class:
		{
			FString SV; Args->TryGetStringField(TEXT("class_value"), SV);
			if (SV.IsEmpty()) Args->TryGetStringField(TEXT("string_value"), SV);
			if (SV.IsEmpty()) { OutErr = TEXT("class_value (class path) required for class parameter"); return false; }
			UClass* Cls = LoadObject<UClass>(nullptr, *SV);
			if (!Cls) if (UBlueprint* BP = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(SV))) Cls = BP->GeneratedClass;
			Result = Bag->SetValueClass(ParamName, Cls);
			OutDescription = SV;
			break;
		}
		case EPropertyBagPropertyType::Enum:
		{
			FString SV; Args->TryGetStringField(TEXT("enum_value"), SV);
			if (SV.IsEmpty()) Args->TryGetStringField(TEXT("string_value"), SV);
			if (SV.IsEmpty()) { OutErr = TEXT("enum_value (member name) required for enum parameter"); return false; }
			const UEnum* EnumPtr = Cast<UEnum>(Desc->ValueTypeObject);
			if (!EnumPtr) { OutErr = TEXT("Enum parameter has no UEnum metadata"); return false; }
			int32 Idx = EnumPtr->GetIndexByName(FName(*SV));
			if (Idx == INDEX_NONE) Idx = EnumPtr->GetIndexByNameString(SV);
			if (Idx == INDEX_NONE) { OutErr = FString::Printf(TEXT("Enum value '%s' not found on '%s'"), *SV, *EnumPtr->GetName()); return false; }
			Result = Bag->SetValueEnum(ParamName, (uint8)EnumPtr->GetValueByIndex(Idx), EnumPtr);
			OutDescription = SV;
			break;
		}
		case EPropertyBagPropertyType::Struct:
		{
			FString StringForm;
			const UScriptStruct* StructPtr = Cast<UScriptStruct>(Desc->ValueTypeObject);
			const FString StructName = StructPtr ? StructPtr->GetName() : FString();

			if (StructName == TEXT("Vector"))
			{
				const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
				if (Args->TryGetArrayField(TEXT("vector_value"), A) && A && A->Num() >= 3)
					StringForm = FString::Printf(TEXT("(X=%g,Y=%g,Z=%g)"), (*A)[0]->AsNumber(), (*A)[1]->AsNumber(), (*A)[2]->AsNumber());
			}
			else if (StructName == TEXT("Rotator"))
			{
				const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
				if (Args->TryGetArrayField(TEXT("rotator_value"), A) && A && A->Num() >= 3)
					StringForm = FString::Printf(TEXT("(Pitch=%g,Yaw=%g,Roll=%g)"), (*A)[0]->AsNumber(), (*A)[1]->AsNumber(), (*A)[2]->AsNumber());
			}
			else if (StructName == TEXT("Transform"))
			{
				FString SV; if (Args->TryGetStringField(TEXT("transform_value"), SV)) StringForm = SV;
			}
			else if (StructName == TEXT("Color"))
			{
				const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
				if (Args->TryGetArrayField(TEXT("color_value"), A) && A && A->Num() >= 3)
					StringForm = FString::Printf(TEXT("(R=%d,G=%d,B=%d,A=%d)"),
						(int32)(*A)[0]->AsNumber(), (int32)(*A)[1]->AsNumber(), (int32)(*A)[2]->AsNumber(),
						A->Num() >= 4 ? (int32)(*A)[3]->AsNumber() : 255);
			}
			else if (StructName == TEXT("LinearColor"))
			{
				const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
				if (Args->TryGetArrayField(TEXT("linear_color_value"), A) && A && A->Num() >= 3)
					StringForm = FString::Printf(TEXT("(R=%g,G=%g,B=%g,A=%g)"),
						(*A)[0]->AsNumber(), (*A)[1]->AsNumber(), (*A)[2]->AsNumber(),
						A->Num() >= 4 ? (*A)[3]->AsNumber() : 1.0);
			}
			else if (StructName == TEXT("GameplayTag"))
			{
				FString SV; if (Args->TryGetStringField(TEXT("gameplay_tag_value"), SV) || Args->TryGetStringField(TEXT("string_value"), SV))
					StringForm = FString::Printf(TEXT("(TagName=\"%s\")"), *SV);
			}
			else if (StructName == TEXT("GameplayTagContainer"))
			{
				const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
				if (Args->TryGetArrayField(TEXT("gameplay_tag_container_value"), A) && A)
				{
					TArray<FString> Tags;
					for (const TSharedPtr<FJsonValue>& V : *A)
					{
						if (V.IsValid() && V->Type == EJson::String) Tags.Add(FString::Printf(TEXT("(TagName=\"%s\")"), *V->AsString()));
					}
					StringForm = FString::Printf(TEXT("(GameplayTags=(%s))"), *FString::Join(Tags, TEXT(",")));
				}
			}

			if (StringForm.IsEmpty())
			{
				FString SV;
				if (Args->TryGetStringField(TEXT("string_value"), SV)) StringForm = SV;
			}
			if (StringForm.IsEmpty())
			{
				OutErr = FString::Printf(TEXT("No value provided for struct parameter '%s' (type %s). Expected one of: vector_value, rotator_value, transform_value, color_value, linear_color_value, gameplay_tag_value, gameplay_tag_container_value, or string_value (raw ImportText)."),
					*ParamName.ToString(), *StructName);
				return false;
			}
			Result = Bag->SetValueSerializedString(ParamName, StringForm);
			OutDescription = StringForm;
			break;
		}
		default:
			OutErr = TEXT("Unsupported parameter type for default-value write");
			return false;
		}

		if (Result != EPropertyBagResult::Success)
		{
			OutErr = FString::Printf(TEXT("SetValue failed (EPropertyBagResult=%d) — value did not match parameter type."), (int32)Result);
			return false;
		}
		return true;
}

void HandleSetStateTreeParameterDefaultFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString AssetPath, ParamName;
	Args->TryGetStringField(TEXT("asset_path"), AssetPath);
	Args->TryGetStringField(TEXT("param_name"), ParamName);
	if (ParamName.IsEmpty()) { OutError = TEXT("param_name is required"); return; }

	UStateTree* ST = Cast<UStateTree>(UEditorAssetLibrary::LoadAsset(AssetPath));
	if (!ST) { OutError = TEXT("StateTree not found: ") + AssetPath; return; }
	UStateTreeEditorData* ED = GetEditorData(ST);
	if (!ED) { OutError = TEXT("No EditorData"); return; }

	FInstancedPropertyBag* Bag = GetMutableRootParameterBag(ED);
	FString Description;
	FString InnerErr;
	if (!ApplyParameterDefault(Bag, FName(*ParamName), Args, Description, InnerErr))
	{
		OutError = InnerErr;
		return;
	}

	BroadcastParametersChanged(ST, ED);
	ED->MarkPackageDirty(); ST->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(AssetPath, false);
	RefreshOpenStateTreeEditor(ST);

	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"name\":\"%s\",\"value\":\"%s\"}"),
		*ParamName, *Description);
}

void HandleGetStateTreeParameterDefaultFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString AssetPath, ParamName;
	Args->TryGetStringField(TEXT("asset_path"), AssetPath);
	Args->TryGetStringField(TEXT("param_name"), ParamName);
	if (ParamName.IsEmpty()) { OutError = TEXT("param_name is required"); return; }

	UStateTree* ST = Cast<UStateTree>(UEditorAssetLibrary::LoadAsset(AssetPath));
	if (!ST) { OutError = TEXT("StateTree not found: ") + AssetPath; return; }
	UStateTreeEditorData* ED = GetEditorData(ST);
	if (!ED) { OutError = TEXT("No EditorData"); return; }

	FInstancedPropertyBag* Bag = GetMutableRootParameterBag(ED);
	if (!Bag) { OutError = TEXT("No bag"); return; }

	const FConstStructView View = Bag->GetValue();
	const UPropertyBag* BagStruct = Bag->GetPropertyBagStruct();
	if (!BagStruct || !View.IsValid()) { OutError = TEXT("Bag is empty"); return; }

	const FPropertyBagPropertyDesc* Desc = nullptr;
	for (const FPropertyBagPropertyDesc& D : BagStruct->GetPropertyDescs())
		if (D.Name == FName(*ParamName)) { Desc = &D; break; }
	if (!Desc) { OutError = FString::Printf(TEXT("Parameter '%s' not found"), *ParamName); return; }
	if (!Desc->CachedProperty) { OutError = TEXT("Parameter has no cached property"); return; }

	FString Exported;
	const void* ValuePtr = Desc->CachedProperty->ContainerPtrToValuePtr<void>(View.GetMemory());
	Desc->CachedProperty->ExportTextItem_Direct(Exported, ValuePtr, ValuePtr, nullptr, PPF_None);

	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"name\":\"%s\",\"value\":\"%s\"}"),
		*ParamName, *Exported.ReplaceCharWithEscapedChar());
}

}
