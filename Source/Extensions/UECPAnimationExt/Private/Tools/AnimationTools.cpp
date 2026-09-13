// Copyright 2026, BlueprintsLab, All rights reserved.

#include "Tools/AnimationTools.h"
#include "Tools/BatchToolHelper.h"
#include "Tools/AssetCreationHelper.h"
#include "Managers/EditorProfileSync.h"
#include "EditorAssetLibrary.h"
#include "AssetToolsModule.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/Kismet2NameValidators.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Logging/TokenizedMessage.h"
#include "Misc/UObjectToken.h"
#include "Animation/BlendProfile.h"
#include "MCPToolsLog.h"
#include "Animation/AnimBoneCompressionSettings.h"
#include "Animation/AnimBoneCompressionCodec.h"
#include "Animation/AnimCurveCompressionSettings.h"
#include "Animation/AnimCurveCompressionCodec.h"
#include "Animation/AnimSequence.h"
#include "UObject/SavePackage.h"
#include "Misc/EngineVersionComparison.h"
#include "Editor.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "TimerManager.h"

#include "Animation/AnimBlueprint.h"
#include "Animation/AnimInstance.h"
#include "Factories/AnimBlueprintFactory.h"
#include "AnimationGraph.h"
#include "AnimGraphNode_Root.h"
#include "AnimGraphNode_StateMachine.h"
#include "EdGraph/EdGraphSchema.h"
#include "EdGraphSchema_K2.h"
#include "EdGraphSchema_K2_Actions.h"
#include "K2Node.h"

#include "AnimationStateMachineGraph.h"
#include "AnimationStateMachineSchema.h"
#include "AnimStateNode.h"
#include "AnimStateEntryNode.h"
#include "AnimStateTransitionNode.h"
#include "AnimStateConduitNode.h"
#include "AnimStateAliasNode.h"
#include "AnimGraphNode_AssetPlayerBase.h"
#include "AnimGraphNode_StateResult.h"
#include "AnimationTransitionGraph.h"
#include "AnimGraphNode_TransitionResult.h"

#include "K2Node_VariableGet.h"
#include "K2Node_CallFunction.h"
#include "Kismet/KismetMathLibrary.h"

#include "AnimGraphNode_SequencePlayer.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimNode_SequencePlayer.h"

#include "AnimGraphNode_BlendSpacePlayer.h"

#include "Animation/BlendSpace.h"
#include "Animation/BlendSpace1D.h"
#include "Factories/BlendSpaceFactoryNew.h"
#include "Factories/BlendSpaceFactory1D.h"

#include "Animation/AimOffsetBlendSpace.h"
#include "Animation/AimOffsetBlendSpace1D.h"
#include "Factories/AimOffsetBlendSpaceFactoryNew.h"
#include "Factories/AimOffsetBlendSpaceFactory1D.h"

#include "Animation/AnimSequenceBase.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"

#include "AlphaBlend.h"

#include "Animation/AnimMontage.h"
#include "Animation/AnimCompositeBase.h"
#include "Animation/AnimComposite.h"
#include "Animation/PoseAsset.h"
#include "Factories/AnimMontageFactory.h"
#include "Factories/AnimCompositeFactory.h"
#include "Factories/AnimSequenceFactory.h"
#include "Factories/PoseAssetFactory.h"
#include "Animation/Skeleton.h"
#include "BoneContainer.h"
#include "ReferenceSkeleton.h"
#include "Animation/SkeletalMeshActor.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SceneCapture2D.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/DirectionalLight.h"
#include "Components/DirectionalLightComponent.h"
#include "Engine/SkyLight.h"
#include "Components/SkyLightComponent.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "PreviewScene.h"
#include <limits>
#include "Engine/World.h"

#include "Animation/AnimData/IAnimationDataController.h"
#include "Animation/AnimData/CurveIdentifier.h"
#include "Animation/AnimCurveTypes.h"

#include "Rig/IKRigDefinition.h"
#include "RigEditor/IKRigDefinitionFactory.h"
#include "RigEditor/IKRigController.h"
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6)
#include "Rig/Solvers/IKRigFullBodyIK.h"
#include "Rig/Solvers/IKRigLimbSolver.h"
#include "Rig/Solvers/IKRigBodyMoverSolver.h"
#include "Rig/Solvers/IKRigSetTransform.h"
#else
#include "Rig/Solvers/IKRig_FBIKSolver.h"
#include "Rig/Solvers/IKRig_LimbSolver.h"
#include "Rig/Solvers/IKRig_BodyMover.h"
#include "Rig/Solvers/IKRig_SetTransform.h"
#endif

#include "Retargeter/IKRetargeter.h"
#include "Retargeter/IKRetargetSettings.h"
#include "RetargetEditor/IKRetargetFactory.h"
#include "RetargetEditor/IKRetargeterController.h"
#include "RetargetEditor/IKRetargetBatchOperation.h"

#include "AnimGraphNode_LinkedAnimLayer.h"
#include "Animation/AnimLayerInterface.h"
#include "Factories/AnimBlueprintFactory.h"
#include "Kismet2/BlueprintEditorUtils.h"

#include "Animation/BlendSpace.h"

#include "AnimGraphNode_LayeredBoneBlend.h"
#include "AnimGraphNode_BlendListByBool.h"
#include "AnimGraphNode_BlendListByInt.h"
#include "AnimGraphNode_SaveCachedPose.h"
#include "AnimGraphNode_UseCachedPose.h"
#include "AnimGraphNode_LinkedAnimGraph.h"
#include "AnimNodes/AnimNode_LayeredBoneBlend.h"

#include "Engine/SkeletalMeshSocket.h"

#include "Animation/AnimData/AnimDataModel.h"

#include "AnimGraphNode_ModifyBone.h"
#include "AnimGraphNode_CopyBone.h"
#include "AnimGraphNode_LookAt.h"
#include "AnimGraphNode_TwoBoneIK.h"
#include "AnimGraphNode_ApplyAdditive.h"
#include "AnimGraphNode_MakeDynamicAdditive.h"
#include "AnimGraphNode_Inertialization.h"
#include "AnimGraphNode_BlendListByEnum.h"
#include "AnimGraphNode_SequenceEvaluator.h"
#include "AnimGraphNode_RandomPlayer.h"

#include "AnimGraphNode_Slot.h"
#include "AnimGraphNode_TwoWayBlend.h"
#include "AnimGraphNode_ApplyMeshSpaceAdditive.h"
#include "AnimGraphNode_CopyPoseFromMesh.h"
#include "AnimGraphNode_RotateRootBone.h"
#include "AnimGraphNode_RotationOffsetBlendSpace.h"
#include "AnimGraphNode_Mirror.h"
#include "Animation/MirrorDataTable.h"
#include "AnimGraphNode_LocalToComponentSpace.h"
#include "AnimGraphNode_ComponentToLocalSpace.h"
#include "AnimGraphNode_LocalRefPose.h"
#include "AnimGraphNode_MeshRefPose.h"
#include "AnimGraphNode_IdentityPose.h"
#include "AnimGraphNode_SpringBone.h"
#include "AnimGraphNode_RigidBody.h"
#include "AnimGraphNode_Fabrik.h"
#include "AnimGraphNode_CCDIK.h"
#include "AnimGraphNode_LegIK.h"
#include "AnimGraphNode_PoseByName.h"
#include "AnimGraphNode_BoneDrivenController.h"
#include "AnimGraphNode_BlendBoneByChannel.h"
#include "AnimGraphNode_MotionMatching.h"
#include "PoseSearch/AnimNode_MotionMatching.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "AnimationGraphSchema.h"
#include "AnimGraphNode_Root.h"
#include "Misc/EngineVersionComparison.h"
#if !UE_VERSION_OLDER_THAN(5, 5, 0)
#include "AnimGraphNode_ControlRig.h"
#endif
#include "AnimNode_ControlRig.h"

// Composite template tools compose the PoseSearch / Chooser handlers and the Blueprint extension's add_variable (via the dispatcher).
#include "Tools/PoseSearchTools.h"
#include "Tools/ChooserTools.h"
#include "UECPCoreModule.h"
#include "Services/IUECPToolDispatcher.h"
#include "PoseSearch/PoseSearchFeatureChannel_Pose.h"
#include "PoseSearch/PoseSearchFeatureChannel_Trajectory.h"

namespace AnimationTools
{

#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6)
using FUECPGraphLoc = FVector2f;
#else
using FUECPGraphLoc = FVector2D;
#endif

static void SetError(const FString& Msg, FString& OutJsonString, FString& OutError)
{
	OutError = Msg;
	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField(TEXT("success"), false);
	Obj->SetStringField(TEXT("error"), Msg);
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Obj.ToSharedRef(), Writer);
}

static FString ResolveAnimAssetPath(const TSharedPtr<FJsonObject>& Args);
static FString ResolveIKRigPath(const TSharedPtr<FJsonObject>& Args);
static FString ResolveRetargeterPath(const TSharedPtr<FJsonObject>& Args);

// Refreshes any open AnimBP editor in place (no close/reopen): reconstructs all nodes and
// pings every graph so open graph panels redraw with the new topology.
static void RefreshOpenAnimBlueprintEditor(UAnimBlueprint* AnimBP)
{
	if (!AnimBP) return;

	TArray<UEdGraph*> AllGraphs;
	AnimBP->GetAllGraphs(AllGraphs);
	for (UEdGraph* Graph : AllGraphs)
	{
		if (Graph) Graph->NotifyGraphChanged();
	}

	if (!GEditor) return;
	UAssetEditorSubsystem* AES = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
	if (!AES || AES->FindEditorsForAsset(AnimBP).Num() == 0) return;

	FBlueprintEditorUtils::RefreshAllNodes(AnimBP);
	for (UEdGraph* Graph : AllGraphs)
	{
		if (Graph) Graph->NotifyGraphChanged();
	}
}

static void BuildSuccessJson(const TSharedPtr<FJsonObject>& Obj, FString& OutJsonString)
{
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Obj.ToSharedRef(), Writer);
}

// Composite tools (build_anim_chain) spawn many nodes through the single-node handlers; each of
// those would otherwise trigger a full AnimBP compile. The scope guard suppresses the nested
// compiles so only the outer tool compiles once at the end.
static int32 GAnimCompileSuppressDepth = 0;
struct FScopedSuppressAnimCompile
{
	FScopedSuppressAnimCompile()  { ++GAnimCompileSuppressDepth; }
	~FScopedSuppressAnimCompile() { --GAnimCompileSuppressDepth; }
};

// Compiles the AnimBP with a silent results log and injects the outcome into Out:
//   compiled (bool), num_errors, num_warnings, compile_errors[], compile_warnings[]
// Messages that reference a graph node carry "[<node title> | <NodeGuid>]" so the model can
// address the offending node with the GUID-based tools.
static void CompileAnimBlueprintAndReport(UAnimBlueprint* AnimBP, const TSharedPtr<FJsonObject>& Out)
{
	if (!AnimBP || !Out.IsValid()) return;
	if (GAnimCompileSuppressDepth > 0)
	{
		Out->SetBoolField(TEXT("compile_deferred"), true);
		return;
	}

	FCompilerResultsLog Results;
	Results.bSilentMode = true;

	FKismetEditorUtilities::CompileBlueprint(AnimBP, EBlueprintCompileOptions::None, &Results);

	auto DescribeMessage = [](const TSharedRef<FTokenizedMessage>& Msg) -> FString
	{
		FString Text = Msg->ToText().ToString();
		for (const TSharedRef<IMessageToken>& Token : Msg->GetMessageTokens())
		{
			if (Token->GetType() != EMessageToken::Object) continue;
			const TSharedRef<FUObjectToken> ObjToken = StaticCastSharedRef<FUObjectToken>(Token);
			const UEdGraphNode* Node = Cast<UEdGraphNode>(ObjToken->GetObject().Get());
			if (!Node) continue;
			Text += FString::Printf(TEXT(" [%s | %s]"),
				*Node->GetNodeTitle(ENodeTitleType::ListView).ToString(),
				*Node->NodeGuid.ToString());
		}
		return Text;
	};

	TArray<TSharedPtr<FJsonValue>> ErrorsJson;
	TArray<TSharedPtr<FJsonValue>> WarningsJson;
	for (const TSharedRef<FTokenizedMessage>& Msg : Results.Messages)
	{
		const EMessageSeverity::Type Sev = Msg->GetSeverity();
		if (Sev == EMessageSeverity::Error)
			ErrorsJson.Add(MakeShared<FJsonValueString>(DescribeMessage(Msg)));
		else if (Sev == EMessageSeverity::Warning)
			WarningsJson.Add(MakeShared<FJsonValueString>(DescribeMessage(Msg)));
	}

	Out->SetBoolField(TEXT("compiled"), Results.NumErrors == 0);
	Out->SetNumberField(TEXT("num_errors"), Results.NumErrors);
	Out->SetNumberField(TEXT("num_warnings"), Results.NumWarnings);
	Out->SetArrayField(TEXT("compile_errors"), ErrorsJson);
	Out->SetArrayField(TEXT("compile_warnings"), WarningsJson);
}

static UAnimationGraph* FindAnimGraph(UAnimBlueprint* AnimBP)
{
	if (!AnimBP) return nullptr;

	// Prefer the canonical root graph named "AnimGraph"; a blueprint can also own layer
	// graphs (UAnimationGraph subclasses) that must not be mistaken for the root.
	for (UEdGraph* Graph : AnimBP->FunctionGraphs)
	{
		UAnimationGraph* AG = Cast<UAnimationGraph>(Graph);
		if (AG && AG->GetFName() == UEdGraphSchema_K2::GN_AnimGraph)
			return AG;
	}
	for (UEdGraph* Graph : AnimBP->FunctionGraphs)
	{
		if (UAnimationGraph* AG = Cast<UAnimationGraph>(Graph))
			return AG;
	}
	return nullptr;
}

// Resolves a state machine graph by name. When SMName is empty the lookup only succeeds if the
// AnimGraph owns exactly one state machine; otherwise OutError (if given) lists the candidates.
static UAnimationStateMachineGraph* FindStateMachineGraph(UAnimBlueprint* AnimBP, const FString& SMName, FString* OutError = nullptr)
{
	UAnimationGraph* AnimGraph = FindAnimGraph(AnimBP);
	if (!AnimGraph)
	{
		if (OutError) *OutError = TEXT("No AnimGraph found in this AnimBlueprint.");
		return nullptr;
	}

	TArray<UAnimationStateMachineGraph*> Candidates;
	for (UEdGraph* SubGraph : AnimGraph->SubGraphs)
	{
		if (UAnimationStateMachineGraph* SMGraph = Cast<UAnimationStateMachineGraph>(SubGraph))
			Candidates.Add(SMGraph);
	}

	TArray<FString> Names;
	for (UAnimationStateMachineGraph* SMGraph : Candidates) Names.Add(SMGraph->GetName());

	if (SMName.IsEmpty())
	{
		if (Candidates.Num() == 1) return Candidates[0];
		if (OutError)
		{
			*OutError = Candidates.Num() == 0
				? TEXT("This AnimBlueprint has no state machine. Add one with add_state_machine first.")
				: FString::Printf(TEXT("state_machine_name is required: this AnimBlueprint has %d state machines [%s]."),
					Candidates.Num(), *FString::Join(Names, TEXT(", ")));
		}
		return nullptr;
	}

	for (UAnimationStateMachineGraph* SMGraph : Candidates)
	{
		if (SMGraph->GetName().Equals(SMName, ESearchCase::IgnoreCase))
			return SMGraph;
	}
	if (OutError)
	{
		*OutError = FString::Printf(TEXT("State machine '%s' not found. Available: [%s]."),
			*SMName, *FString::Join(Names, TEXT(", ")));
	}
	return nullptr;
}

static UAnimStateNode* FindStateByName(UAnimationStateMachineGraph* SMGraph, const FString& StateName)
{
	TArray<UAnimStateNode*> States;
	SMGraph->GetNodesOfClass<UAnimStateNode>(States);
	for (UAnimStateNode* S : States)
	{
		if (S->GetStateName().Equals(StateName, ESearchCase::IgnoreCase))
			return S;
	}
	return nullptr;
}

static UEdGraphPin* FindPoseOutputPin(UEdGraphNode* Node)
{
	if (!Node) return nullptr;
	UEdGraphPin* P = Node->FindPin(TEXT("Pose"), EGPD_Output);
	if (P && UAnimationGraphSchema::IsPosePin(P->PinType)) return P;
	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (Pin && Pin->Direction == EGPD_Output && UAnimationGraphSchema::IsPosePin(Pin->PinType))
			return Pin;
	}
	return nullptr;
}

static UEdGraphPin* FindPoseInputPin(UEdGraphNode* Node)
{
	if (!Node) return nullptr;
	UEdGraphPin* P = Node->FindPin(TEXT("Result"), EGPD_Input);
	if (P && UAnimationGraphSchema::IsPosePin(P->PinType)) return P;
	P = Node->FindPin(TEXT("Pose"), EGPD_Input);
	if (P && UAnimationGraphSchema::IsPosePin(P->PinType)) return P;
	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (Pin && Pin->Direction == EGPD_Input && UAnimationGraphSchema::IsPosePin(Pin->PinType))
			return Pin;
	}
	return nullptr;
}

// Finds a node by GUID across every graph owned by the blueprint (AnimGraph, state graphs,
// transition graphs, layer graphs). OutGraph receives the owning graph.
static UEdGraphNode* FindNodeByGuidInAllGraphs(UAnimBlueprint* AnimBP, const FGuid& Guid, UEdGraph** OutGraph = nullptr)
{
	if (OutGraph) *OutGraph = nullptr;
	if (!AnimBP || !Guid.IsValid()) return nullptr;
	TArray<UEdGraph*> AllGraphs;
	AnimBP->GetAllGraphs(AllGraphs);
	for (UEdGraph* Graph : AllGraphs)
	{
		if (!Graph) continue;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (Node && Node->NodeGuid == Guid)
			{
				if (OutGraph) *OutGraph = Graph;
				return Node;
			}
		}
	}
	return nullptr;
}

void HandleCreateAnimBlueprint(
	const FString& Name,
	const FString& SavePath,
	const FString& SkeletonPath,
	const FString& StateMachineName,
	FString& OutJsonString, FString& OutError)
{
	if (Name.IsEmpty()) { SetError(TEXT("name is required"), OutJsonString, OutError); return; }

	FString PackagePath = SavePath.IsEmpty() ? TEXT("/Game") : SavePath;
	while (PackagePath.EndsWith(TEXT("/"))) PackagePath = PackagePath.LeftChop(1);

	UAnimBlueprintFactory* Factory = NewObject<UAnimBlueprintFactory>();
	Factory->BlueprintType = BPTYPE_Normal;
	Factory->ParentClass = UAnimInstance::StaticClass();

	if (!SkeletonPath.IsEmpty())
	{
		USkeleton* Skeleton = Cast<USkeleton>(UEditorAssetLibrary::LoadAsset(SkeletonPath));
		if (!Skeleton)
		{
			SetError(FString::Printf(TEXT("Could not load Skeleton: %s"), *SkeletonPath), OutJsonString, OutError);
			return;
		}
		Factory->TargetSkeleton = Skeleton;
	}

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	UObject* Asset = AssetTools.CreateAsset(Name, PackagePath, UAnimBlueprint::StaticClass(), Factory);
	UAnimBlueprint* AnimBP = Cast<UAnimBlueprint>(Asset);
	if (!AnimBP)
	{
		SetError(TEXT("Failed to create AnimBlueprint asset."), OutJsonString, OutError);
		return;
	}

	UAnimationGraph* AnimGraph = FindAnimGraph(AnimBP);
	FString ResultSMName = StateMachineName.IsEmpty() ? TEXT("NewStateMachine") : StateMachineName;
	FString SMGraphName;
	bool bWiredToOutput = false;
	FString WireNote;

	if (AnimGraph)
	{
		FEdGraphSchemaAction_K2NewNode Action;
		UAnimGraphNode_StateMachine* SMTemplate = NewObject<UAnimGraphNode_StateMachine>(GetTransientPackage());
		Action.NodeTemplate = SMTemplate;
		UEdGraphNode* SpawnedNode = Action.PerformAction(AnimGraph, nullptr, FUECPGraphLoc(-300.f, 0.f), false);
		UAnimGraphNode_StateMachine* SMNode = Cast<UAnimGraphNode_StateMachine>(SpawnedNode);

		if (SMNode && SMNode->EditorStateMachineGraph)
		{
			TSharedPtr<INameValidatorInterface> NameValidator = FNameValidatorFactory::MakeValidator(SMNode);
			FBlueprintEditorUtils::RenameGraphWithSuggestion(SMNode->EditorStateMachineGraph, NameValidator, ResultSMName);
			SMGraphName = SMNode->EditorStateMachineGraph->GetName();

			TArray<UAnimGraphNode_Root*> RootNodes;
			AnimGraph->GetNodesOfClass<UAnimGraphNode_Root>(RootNodes);
			if (RootNodes.Num() > 0)
			{
				UEdGraphPin* SMOut = FindPoseOutputPin(SMNode);
				UEdGraphPin* RootIn = FindPoseInputPin(RootNodes[0]);
				if (SMOut && RootIn)
				{
					bWiredToOutput = AnimGraph->GetSchema()->TryCreateConnection(SMOut, RootIn);
					if (!bWiredToOutput)
						WireNote = TEXT("Schema rejected wiring the state machine into Output Pose; use wire_anim_node_to_output.");
				}
				else
				{
					WireNote = TEXT("Could not locate pose pins to wire the state machine into Output Pose.");
				}
			}
			else
			{
				WireNote = TEXT("AnimGraph has no Output Pose (Root) node to wire into.");
			}
		}

		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBP);
		AnimGraph->NotifyGraphChanged();
	}

	FString AssetPath = FString::Printf(TEXT("%s/%s"), *PackagePath, *Name);

	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("asset_path"), AssetPath);
	Obj->SetStringField(TEXT("state_machine_name"), SMGraphName);
	Obj->SetBoolField(TEXT("wired_to_output"), bWiredToOutput);
	if (!WireNote.IsEmpty()) Obj->SetStringField(TEXT("wire_note"), WireNote);
	CompileAnimBlueprintAndReport(AnimBP, Obj);

	UEditorAssetLibrary::SaveAsset(AssetPath, false);

	FAssetRegistryModule& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	AssetRegistry.Get().ScanPathsSynchronous({PackagePath}, true);

	BuildSuccessJson(Obj, OutJsonString);
}

void HandleAddAnimState(
	const FString& AnimBlueprintPath,
	const FString& StateMachineName,
	const FString& StateName,
	const FString& AnimationPath,
	int32 PosX, int32 PosY,
	FString& OutJsonString, FString& OutError,
	const FString& BlendVariableX,
	const FString& BlendVariableY)
{

	UAnimBlueprint* AnimBP = Cast<UAnimBlueprint>(UEditorAssetLibrary::LoadAsset(AnimBlueprintPath));
	if (!AnimBP)
	{
		SetError(FString::Printf(TEXT("Could not load AnimBlueprint: %s"), *AnimBlueprintPath), OutJsonString, OutError);
		return;
	}

	FString SMLookupError;
	UAnimationStateMachineGraph* SMGraph = FindStateMachineGraph(AnimBP, StateMachineName, &SMLookupError);
	if (!SMGraph)
	{
		SetError(SMLookupError, OutJsonString, OutError);
		return;
	}

	UAnimStateNode* StateNode = FEdGraphSchemaAction_NewStateNode::SpawnNodeFromTemplate<UAnimStateNode>(
		SMGraph, NewObject<UAnimStateNode>(), FUECPGraphLoc((float)PosX, (float)PosY), false);
	if (!StateNode)
	{
		SetError(TEXT("Failed to spawn AnimStateNode."), OutJsonString, OutError);
		return;
	}

	if (StateNode->BoundGraph)
	{
		TSharedPtr<INameValidatorInterface> NameValidator = FNameValidatorFactory::MakeValidator(StateNode);
		FBlueprintEditorUtils::RenameGraphWithSuggestion(StateNode->BoundGraph, NameValidator, StateName);
	}

	TArray<FString> WiredBlendVars;
	TArray<FString> UnresolvedBlendVars;
	bool bPlayerWiredToResult = false;
	if (!AnimationPath.IsEmpty() && StateNode->BoundGraph)
	{
		UAnimationAsset* LoadedAsset = Cast<UAnimationAsset>(UEditorAssetLibrary::LoadAsset(AnimationPath));
		UBlendSpace* BSAsset = Cast<UBlendSpace>(LoadedAsset);
		UAnimSequenceBase* AnimSeq = Cast<UAnimSequenceBase>(LoadedAsset);

		if (!BSAsset && !AnimSeq)
		{
			SetError(FString::Printf(
				TEXT("animation_path '%s' did not load as a BlendSpace or AnimSequence; cannot wire a player node into state '%s'. Provide a path to a UBlendSpace/UBlendSpace1D or UAnimSequence/UAnimComposite, or omit animation_path to create an empty state."),
				*AnimationPath, *StateName), OutJsonString, OutError);
			return;
		}

		UEdGraphNode* PlayerNode = nullptr;

		if (BSAsset)
		{
			UBlendSpace1D* BS1D = Cast<UBlendSpace1D>(BSAsset);
			FEdGraphSchemaAction_K2NewNode Action;
			UAnimGraphNode_BlendSpacePlayer* BSTemplate = NewObject<UAnimGraphNode_BlendSpacePlayer>(GetTransientPackage());
			BSTemplate->SetAnimationAsset(BSAsset);
			Action.NodeTemplate = BSTemplate;
			PlayerNode = Action.PerformAction(StateNode->BoundGraph, nullptr, FUECPGraphLoc(-350.f, 0.f), false);

			UAnimGraphNode_BlendSpacePlayer* BSNode = Cast<UAnimGraphNode_BlendSpacePlayer>(PlayerNode);
			if (BSNode)
			{
				auto WireVariableToPin = [&](const FString& VarName, const FString& PinName)
				{
					if (VarName.IsEmpty()) return;
					UClass* SearchClass = AnimBP->SkeletonGeneratedClass ? AnimBP->SkeletonGeneratedClass : AnimBP->GeneratedClass;
					FProperty* Prop = nullptr;
					if (SearchClass)
					{
						for (TFieldIterator<FProperty> It(SearchClass, EFieldIteratorFlags::IncludeSuper); It; ++It)
						{
							if (It->GetName().Equals(VarName, ESearchCase::IgnoreCase) ||
								It->GetName().StartsWith(VarName + TEXT("_"), ESearchCase::IgnoreCase))
							{ Prop = *It; break; }
						}
					}

					if (!Prop)
					{
						UnresolvedBlendVars.Add(VarName);
						return;
					}

					UK2Node_VariableGet* VarNode = NewObject<UK2Node_VariableGet>(StateNode->BoundGraph);
					VarNode->CreateNewGuid();
					StateNode->BoundGraph->AddNode(VarNode, true, false);
					VarNode->PostPlacedNewNode();
					VarNode->AllocateDefaultPins();
					VarNode->VariableReference.SetFromField<FProperty>(Prop, true);
					VarNode->ReconstructNode();
					VarNode->NodePosX = BSNode->NodePosX - 200;
					VarNode->NodePosY = BSNode->NodePosY + (PinName == TEXT("Y") ? 60 : 0);

					UEdGraphPin* VarOut = VarNode->GetValuePin();
					UEdGraphPin* TargetPin = BSNode->FindPin(FName(*PinName));
					if (VarOut && TargetPin && StateNode->BoundGraph->GetSchema()->TryCreateConnection(VarOut, TargetPin))
						WiredBlendVars.Add(PinName);
					else
						UnresolvedBlendVars.Add(VarName);
				};

				WireVariableToPin(BlendVariableX, TEXT("X"));
				if (!BS1D) WireVariableToPin(BlendVariableY, TEXT("Y"));
			}
		}
		else if (AnimSeq)
		{
			FEdGraphSchemaAction_K2NewNode Action;
			UAnimGraphNode_SequencePlayer* SeqTemplate = NewObject<UAnimGraphNode_SequencePlayer>(GetTransientPackage());
			SeqTemplate->Node.SetSequence(AnimSeq);
			Action.NodeTemplate = SeqTemplate;
			PlayerNode = Action.PerformAction(StateNode->BoundGraph, nullptr, FUECPGraphLoc(-300.f, 0.f), false);
		}

		if (!PlayerNode)
		{
			SetError(FString::Printf(
				TEXT("Loaded animation_path '%s' but failed to spawn a player node inside state '%s'."),
				*AnimationPath, *StateName), OutJsonString, OutError);
			return;
		}

		{
			TArray<UAnimGraphNode_StateResult*> ResultNodes;
			StateNode->BoundGraph->GetNodesOfClass<UAnimGraphNode_StateResult>(ResultNodes);
			if (ResultNodes.Num() > 0)
			{
				UEdGraphPin* PlayerOut = FindPoseOutputPin(PlayerNode);
				UEdGraphPin* ResultIn = FindPoseInputPin(ResultNodes[0]);
				if (!PlayerOut || !ResultIn)
				{
					SetError(FString::Printf(
						TEXT("Spawned a player node in state '%s' but could not find its pose pins to wire into the state result."),
						*StateName), OutJsonString, OutError);
					return;
				}
				if (!StateNode->BoundGraph->GetSchema()->TryCreateConnection(PlayerOut, ResultIn))
				{
					SetError(FString::Printf(
						TEXT("Schema rejected wiring the player node into the result of state '%s'."),
						*StateName), OutJsonString, OutError);
					return;
				}
				bPlayerWiredToResult = true;
			}
			else
			{
				SetError(FString::Printf(TEXT("State '%s' has no result (Output Animation Pose) node."), *StateName), OutJsonString, OutError);
				return;
			}
		}
	}

	bool bEntryWired = false;
	if (SMGraph->EntryNode)
	{
		UEdGraphPin* EntryOutPin = SMGraph->EntryNode->Pins.Num() > 0 ? SMGraph->EntryNode->Pins[0] : nullptr;
		if (EntryOutPin && EntryOutPin->LinkedTo.Num() == 0)
		{
			UEdGraphPin* StateInPin = StateNode->GetInputPin();
			if (StateInPin)
			{
				bEntryWired = SMGraph->GetSchema()->TryCreateConnection(EntryOutPin, StateInPin);
			}
		}
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBP);
	if (SMGraph) SMGraph->NotifyGraphChanged();
	if (StateNode->BoundGraph) StateNode->BoundGraph->NotifyGraphChanged();
	RefreshOpenAnimBlueprintEditor(AnimBP);

	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("state_name"), StateNode->GetStateName());
	Obj->SetStringField(TEXT("node_guid"), StateNode->NodeGuid.ToString());
	Obj->SetBoolField(TEXT("player_wired_to_result"), bPlayerWiredToResult);
	Obj->SetBoolField(TEXT("set_as_entry_state"), bEntryWired);
	if (!BlendVariableX.IsEmpty() || !BlendVariableY.IsEmpty())
	{
		Obj->SetBoolField(TEXT("blend_variable_x_wired"), WiredBlendVars.Contains(TEXT("X")));
		Obj->SetBoolField(TEXT("blend_variable_y_wired"), WiredBlendVars.Contains(TEXT("Y")));
	}
	if (UnresolvedBlendVars.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> UnresolvedArr;
		for (const FString& VarName : UnresolvedBlendVars)
			UnresolvedArr.Add(MakeShareable(new FJsonValueString(VarName)));
		Obj->SetArrayField(TEXT("unresolved_variables"), UnresolvedArr);
	}
	CompileAnimBlueprintAndReport(AnimBP, Obj);
	UEditorAssetLibrary::SaveAsset(AnimBlueprintPath, false);
	BuildSuccessJson(Obj, OutJsonString);
}

// Spawns one directed transition node FromState -> ToState with a default always-true rule.
// Returns nullptr and fills OutError on failure.
static UAnimStateTransitionNode* SpawnDirectedTransition(UAnimationStateMachineGraph* SMGraph,
	UAnimStateNode* SourceState, UAnimStateNode* DestState, float CrossfadeDuration, FString& OutError)
{
	for (UEdGraphNode* Node : SMGraph->Nodes)
	{
		UAnimStateTransitionNode* Existing = Cast<UAnimStateTransitionNode>(Node);
		if (!Existing) continue;
		if (Existing->GetPreviousState() == SourceState && Existing->GetNextState() == DestState)
		{
			OutError = FString::Printf(TEXT("A transition from '%s' to '%s' already exists."),
				*SourceState->GetStateName(), *DestState->GetStateName());
			return nullptr;
		}
	}

	FUECPGraphLoc MidPos(
		(SourceState->NodePosX + DestState->NodePosX) * 0.5f,
		(SourceState->NodePosY + DestState->NodePosY) * 0.5f);

	UAnimStateTransitionNode* TransNode = FEdGraphSchemaAction_NewStateNode::SpawnNodeFromTemplate<UAnimStateTransitionNode>(
		SMGraph, NewObject<UAnimStateTransitionNode>(), MidPos, false);
	if (!TransNode)
	{
		OutError = TEXT("Failed to spawn transition node.");
		return nullptr;
	}

	TransNode->CreateConnections(SourceState, DestState);
	TransNode->CrossfadeDuration = CrossfadeDuration;
	TransNode->Bidirectional = false;

	UAnimationTransitionGraph* TransGraph = Cast<UAnimationTransitionGraph>(TransNode->BoundGraph);
	if (TransGraph && TransGraph->GetResultNode())
	{
		UEdGraphPin* CanEnterPin = TransGraph->GetResultNode()->FindPin(TEXT("bCanEnterTransition"));
		if (CanEnterPin)
		{
			CanEnterPin->DefaultValue = TEXT("true");
		}
	}
	return TransNode;
}

void HandleAddStateTransition(
	const FString& AnimBlueprintPath,
	const FString& StateMachineName,
	const FString& FromState,
	const FString& ToState,
	bool bBidirectional,
	float CrossfadeDuration,
	FString& OutJsonString, FString& OutError)
{

	UAnimBlueprint* AnimBP = Cast<UAnimBlueprint>(UEditorAssetLibrary::LoadAsset(AnimBlueprintPath));
	if (!AnimBP)
	{
		SetError(FString::Printf(TEXT("Could not load AnimBlueprint: %s"), *AnimBlueprintPath), OutJsonString, OutError);
		return;
	}

	FString SMLookupError;
	UAnimationStateMachineGraph* SMGraph = FindStateMachineGraph(AnimBP, StateMachineName, &SMLookupError);
	if (!SMGraph)
	{
		SetError(SMLookupError, OutJsonString, OutError);
		return;
	}

	UAnimStateNode* SourceState = FindStateByName(SMGraph, FromState);
	UAnimStateNode* DestState   = FindStateByName(SMGraph, ToState);

	if (!SourceState)
	{
		SetError(FString::Printf(TEXT("Source state '%s' not found."), *FromState), OutJsonString, OutError);
		return;
	}
	if (!DestState)
	{
		SetError(FString::Printf(TEXT("Destination state '%s' not found."), *ToState), OutJsonString, OutError);
		return;
	}

	FString SpawnError;
	UAnimStateTransitionNode* TransNode = SpawnDirectedTransition(SMGraph, SourceState, DestState, CrossfadeDuration, SpawnError);
	if (!TransNode)
	{
		SetError(SpawnError, OutJsonString, OutError);
		return;
	}

	// The engine's UAnimStateTransitionNode::Bidirectional flag is not honoured by the AnimBP
	// compiler, so "bidirectional" is realised as a second, independent reverse transition node
	// (each with its own rule graph, exactly like authoring both arrows in the editor).
	UAnimStateTransitionNode* ReverseNode = nullptr;
	FString ReverseError;
	if (bBidirectional)
	{
		ReverseNode = SpawnDirectedTransition(SMGraph, DestState, SourceState, CrossfadeDuration, ReverseError);
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBP);
	if (SMGraph) SMGraph->NotifyGraphChanged();
	RefreshOpenAnimBlueprintEditor(AnimBP);

	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("from"), SourceState->GetStateName());
	Obj->SetStringField(TEXT("to"), DestState->GetStateName());
	Obj->SetStringField(TEXT("transition_guid"), TransNode->NodeGuid.ToString());
	Obj->SetBoolField(TEXT("bidirectional"), bBidirectional);
	if (bBidirectional)
	{
		Obj->SetBoolField(TEXT("reverse_transition_created"), ReverseNode != nullptr);
		if (ReverseNode)
		{
			Obj->SetStringField(TEXT("reverse_transition_guid"), ReverseNode->NodeGuid.ToString());
			Obj->SetStringField(TEXT("note"), TEXT("Two independent transition nodes were created. Set each rule separately: set_transition_rule(from=FromState,to=ToState) and set_transition_rule(from=ToState,to=FromState)."));
		}
		else
		{
			Obj->SetStringField(TEXT("reverse_transition_error"), ReverseError);
		}
	}
	CompileAnimBlueprintAndReport(AnimBP, Obj);
	UEditorAssetLibrary::SaveAsset(AnimBlueprintPath, false);
	BuildSuccessJson(Obj, OutJsonString);
}

void HandleSetTransitionRule(
	const FString& AnimBlueprintPath,
	const FString& StateMachineName,
	const FString& FromState,
	const FString& ToState,
	const FString& RuleType,
	const FString& VariableName,
	const FString& CompareOp,
	float CompareValue,
	FString& OutJsonString, FString& OutError)
{

	UAnimBlueprint* AnimBP = Cast<UAnimBlueprint>(UEditorAssetLibrary::LoadAsset(AnimBlueprintPath));
	if (!AnimBP)
	{
		SetError(FString::Printf(TEXT("Could not load AnimBlueprint: %s"), *AnimBlueprintPath), OutJsonString, OutError);
		return;
	}

	FString SMLookupError;
	UAnimationStateMachineGraph* SMGraph = FindStateMachineGraph(AnimBP, StateMachineName, &SMLookupError);
	if (!SMGraph)
	{
		SetError(SMLookupError, OutJsonString, OutError);
		return;
	}

	UAnimStateTransitionNode* TransNode = nullptr;
	for (UEdGraphNode* Node : SMGraph->Nodes)
	{
		UAnimStateTransitionNode* T = Cast<UAnimStateTransitionNode>(Node);
		if (!T) continue;
		UAnimStateNodeBase* Prev = T->GetPreviousState();
		UAnimStateNodeBase* Next = T->GetNextState();
		if (Prev && Next && Prev->GetStateName() == FromState && Next->GetStateName() == ToState)
		{
			TransNode = T;
			break;
		}
	}
	if (!TransNode)
	{
		SetError(FString::Printf(TEXT("No transition found from '%s' to '%s' (transitions are directional; add the reverse arrow with add_state_transition)."), *FromState, *ToState), OutJsonString, OutError);
		return;
	}

	UAnimationTransitionGraph* TransGraph = Cast<UAnimationTransitionGraph>(TransNode->BoundGraph);
	if (!TransGraph || !TransGraph->GetResultNode())
	{
		SetError(TEXT("Transition has no rule graph."), OutJsonString, OutError);
		return;
	}
	UAnimGraphNode_TransitionResult* ResultNode = TransGraph->GetResultNode();
	UEdGraphPin* CanEnterPin = ResultNode->FindPin(TEXT("bCanEnterTransition"));
	if (!CanEnterPin)
	{
		SetError(TEXT("Could not find bCanEnterTransition pin on transition result."), OutJsonString, OutError);
		return;
	}

	TArray<UEdGraphNode*> NodesToRemove;
	for (UEdGraphNode* N : TransGraph->Nodes)
	{
		if (N != ResultNode)
		{
			NodesToRemove.Add(N);
		}
	}
	for (UEdGraphNode* N : NodesToRemove)
	{
		TransGraph->RemoveNode(N);
	}
	CanEnterPin->BreakAllPinLinks();
	CanEnterPin->DefaultValue = TEXT("");

	if (RuleType == TEXT("always_true"))
	{
		CanEnterPin->DefaultValue = TEXT("true");
	}
	else if (RuleType == TEXT("bool_variable") || RuleType == TEXT("not_bool_variable") || RuleType == TEXT("bool_variable_not")
		|| RuleType == TEXT("float_compare") || RuleType == TEXT("int_compare"))
	{
		// not_bool_variable: bCanEnterTransition = NOT <bool variable> (e.g. "!IsInAir" for a landing transition).
		const bool bNegatedBool = (RuleType == TEXT("not_bool_variable") || RuleType == TEXT("bool_variable_not"));
		UClass* SearchClass = AnimBP->SkeletonGeneratedClass ? AnimBP->SkeletonGeneratedClass : AnimBP->GeneratedClass;
		FProperty* Prop = nullptr;
		if (SearchClass)
		{
			for (TFieldIterator<FProperty> It(SearchClass, EFieldIteratorFlags::IncludeSuper); It; ++It)
			{
				FString PropName = It->GetName();
				if (PropName.Equals(VariableName, ESearchCase::IgnoreCase) ||
					PropName.StartsWith(VariableName + TEXT("_"), ESearchCase::IgnoreCase))
				{
					Prop = *It;
					break;
				}
			}
		}
		if (!Prop)
		{
			SetError(FString::Printf(TEXT("Variable '%s' not found in AnimBlueprint."), *VariableName), OutJsonString, OutError);
			return;
		}

		UK2Node_VariableGet* VarNode = NewObject<UK2Node_VariableGet>(TransGraph);
		VarNode->CreateNewGuid();
		TransGraph->AddNode(VarNode, true, false);
		VarNode->PostPlacedNewNode();
		VarNode->AllocateDefaultPins();
		VarNode->VariableReference.SetFromField<FProperty>(Prop, true);
		VarNode->ReconstructNode();
		VarNode->NodePosX = -350;
		VarNode->NodePosY = 0;

		UEdGraphPin* VarOutPin = VarNode->GetValuePin();
		if (!VarOutPin)
		{
			SetError(FString::Printf(TEXT("Could not get output pin for variable '%s'."), *VariableName), OutJsonString, OutError);
			return;
		}

		if (RuleType == TEXT("bool_variable"))
		{
			if (!TransGraph->GetSchema()->TryCreateConnection(VarOutPin, CanEnterPin))
			{
				SetError(FString::Printf(TEXT("Schema rejected connecting bool variable '%s' to bCanEnterTransition (is it a bool?)."), *VariableName), OutJsonString, OutError);
				return;
			}
		}
		else if (bNegatedBool)
		{
			UFunction* NotFunc = UKismetMathLibrary::StaticClass()->FindFunctionByName(FName(TEXT("Not_PreBool")));
			if (!NotFunc)
			{
				SetError(TEXT("Math function 'Not_PreBool' not found."), OutJsonString, OutError);
				return;
			}
			UK2Node_CallFunction* NotNode = NewObject<UK2Node_CallFunction>(TransGraph);
			NotNode->SetFromFunction(NotFunc);
			NotNode->CreateNewGuid();
			TransGraph->AddNode(NotNode, true, false);
			NotNode->PostPlacedNewNode();
			NotNode->AllocateDefaultPins();
			NotNode->NodePosX = -150;
			NotNode->NodePosY = 0;

			UEdGraphPin* APin = NotNode->FindPin(TEXT("A"));
			UEdGraphPin* RetPin = NotNode->FindPin(TEXT("ReturnValue"));
			if (!APin || !RetPin)
			{
				SetError(TEXT("NOT node is missing its A/ReturnValue pins."), OutJsonString, OutError);
				return;
			}
			if (!TransGraph->GetSchema()->TryCreateConnection(VarOutPin, APin))
			{
				SetError(FString::Printf(TEXT("Schema rejected connecting variable '%s' to NOT (is it a bool?)."), *VariableName), OutJsonString, OutError);
				return;
			}
			if (!TransGraph->GetSchema()->TryCreateConnection(RetPin, CanEnterPin))
			{
				SetError(TEXT("Schema rejected connecting the NOT result to bCanEnterTransition."), OutJsonString, OutError);
				return;
			}
		}
		else
		{
			bool bIsFloat = (RuleType == TEXT("float_compare"));
			FName FuncName;
			if (CompareOp == TEXT(">="))      FuncName = bIsFloat ? FName(TEXT("GreaterEqual_DoubleDouble")) : FName(TEXT("GreaterEqual_IntInt"));
			else if (CompareOp == TEXT(">"))  FuncName = bIsFloat ? FName(TEXT("Greater_DoubleDouble"))      : FName(TEXT("Greater_IntInt"));
			else if (CompareOp == TEXT("<=")) FuncName = bIsFloat ? FName(TEXT("LessEqual_DoubleDouble"))    : FName(TEXT("LessEqual_IntInt"));
			else if (CompareOp == TEXT("<"))  FuncName = bIsFloat ? FName(TEXT("Less_DoubleDouble"))         : FName(TEXT("Less_IntInt"));
			else if (CompareOp == TEXT("!=")) FuncName = bIsFloat ? FName(TEXT("NotEqual_DoubleDouble"))     : FName(TEXT("NotEqual_IntInt"));
			else                              FuncName = bIsFloat ? FName(TEXT("EqualEqual_DoubleDouble"))   : FName(TEXT("EqualEqual_IntInt"));

			UFunction* CompareFunc = UKismetMathLibrary::StaticClass()->FindFunctionByName(FuncName);
			if (!CompareFunc)
			{
				SetError(FString::Printf(TEXT("Math function '%s' not found."), *FuncName.ToString()), OutJsonString, OutError);
				return;
			}

			UK2Node_CallFunction* CompareNode = NewObject<UK2Node_CallFunction>(TransGraph);
			CompareNode->SetFromFunction(CompareFunc);
			CompareNode->CreateNewGuid();
			TransGraph->AddNode(CompareNode, true, false);
			CompareNode->PostPlacedNewNode();
			CompareNode->AllocateDefaultPins();
			CompareNode->NodePosX = -150;
			CompareNode->NodePosY = 0;

			UEdGraphPin* APin = CompareNode->FindPin(TEXT("A"));
			UEdGraphPin* BPin = CompareNode->FindPin(TEXT("B"));
			UEdGraphPin* RetPin = CompareNode->FindPin(TEXT("ReturnValue"));

			if (!APin || !RetPin)
			{
				SetError(FString::Printf(TEXT("Compare node '%s' is missing its A/ReturnValue pins."), *FuncName.ToString()), OutJsonString, OutError);
				return;
			}
			if (!TransGraph->GetSchema()->TryCreateConnection(VarOutPin, APin))
			{
				SetError(FString::Printf(TEXT("Schema rejected connecting variable '%s' to the compare node's A pin (type mismatch with %s?)."), *VariableName, *RuleType), OutJsonString, OutError);
				return;
			}
			if (BPin) BPin->DefaultValue = bIsFloat ? FString::SanitizeFloat(CompareValue) : FString::FromInt((int32)CompareValue);
			if (!TransGraph->GetSchema()->TryCreateConnection(RetPin, CanEnterPin))
			{
				SetError(TEXT("Schema rejected connecting the compare result to bCanEnterTransition."), OutJsonString, OutError);
				return;
			}
		}
	}
	else
	{
		SetError(FString::Printf(TEXT("Unknown rule_type '%s'. Use: always_true, bool_variable, not_bool_variable, float_compare, int_compare."), *RuleType), OutJsonString, OutError);
		return;
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBP);
	TransGraph->NotifyGraphChanged();
	SMGraph->NotifyGraphChanged();
	RefreshOpenAnimBlueprintEditor(AnimBP);

	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("rule_type"), RuleType);
	Obj->SetStringField(TEXT("transition_guid"), TransNode->NodeGuid.ToString());
	CompileAnimBlueprintAndReport(AnimBP, Obj);
	UEditorAssetLibrary::SaveAsset(AnimBlueprintPath, false);
	BuildSuccessJson(Obj, OutJsonString);
}

void HandleCreateBlendspace(
	const FString& Name,
	const FString& SavePath,
	const FString& SkeletonPath,
	bool bIs1D,
	const FString& AxisName,
	float AxisMin,
	float AxisMax,
	FString& OutJsonString, FString& OutError)
{
	if (Name.IsEmpty()) { SetError(TEXT("name is required"), OutJsonString, OutError); return; }

	FString PackagePath = SavePath.IsEmpty() ? TEXT("/Game") : SavePath;
	while (PackagePath.EndsWith(TEXT("/"))) PackagePath = PackagePath.LeftChop(1);

	USkeleton* Skeleton = nullptr;
	if (!SkeletonPath.IsEmpty())
	{
		Skeleton = Cast<USkeleton>(UEditorAssetLibrary::LoadAsset(SkeletonPath));
		if (!Skeleton)
		{
			SetError(FString::Printf(TEXT("Could not load Skeleton: %s"), *SkeletonPath), OutJsonString, OutError);
			return;
		}
	}

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	UObject* Asset = nullptr;

	if (bIs1D)
	{
		UBlendSpaceFactory1D* Factory = NewObject<UBlendSpaceFactory1D>();
		Factory->TargetSkeleton = Skeleton;
		Asset = AssetTools.CreateAsset(Name, PackagePath, UBlendSpace1D::StaticClass(), Factory);
	}
	else
	{
		UBlendSpaceFactoryNew* Factory = NewObject<UBlendSpaceFactoryNew>();
		Factory->TargetSkeleton = Skeleton;
		Asset = AssetTools.CreateAsset(Name, PackagePath, UBlendSpace::StaticClass(), Factory);
	}

	UBlendSpace* BlendSpace = Cast<UBlendSpace>(Asset);
	if (!BlendSpace)
	{
		SetError(TEXT("Failed to create BlendSpace asset."), OutJsonString, OutError);
		return;
	}

	FBlendParameter& Axis0 = const_cast<FBlendParameter&>(BlendSpace->GetBlendParameter(0));
	Axis0.DisplayName = AxisName.IsEmpty() ? TEXT("Speed") : AxisName;
	Axis0.Min = AxisMin;
	Axis0.Max = AxisMax;
	Axis0.GridNum = 4;

	BlendSpace->MarkPackageDirty();

	FString AssetPath = FString::Printf(TEXT("%s/%s"), *PackagePath, *Name);
	UEditorAssetLibrary::SaveAsset(AssetPath, false);

	FAssetRegistryModule& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	AssetRegistry.Get().ScanPathsSynchronous({PackagePath}, true);

	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("asset_path"), AssetPath);
	Obj->SetStringField(TEXT("type"), bIs1D ? TEXT("BlendSpace1D") : TEXT("BlendSpace"));
	BuildSuccessJson(Obj, OutJsonString);
}

static int32 ResolveNotifyTrackIndex(UAnimSequenceBase* Asset, const FString& TrackName,
	int32 ExplicitIndex, FString& OutError)
{
	if (!Asset) { OutError = TEXT("Null animation asset"); return -1; }
	const TArray<FAnimNotifyTrack>& Tracks = Asset->AnimNotifyTracks;

	if (!TrackName.IsEmpty())
	{
		for (int32 i = 0; i < Tracks.Num(); ++i)
		{
			if (Tracks[i].TrackName.ToString().Equals(TrackName, ESearchCase::IgnoreCase))
				return i;
		}
		if (Tracks.Num() > 0
			&& (TrackName.Equals(TEXT("default"), ESearchCase::IgnoreCase)
				|| TrackName.Equals(TEXT("none"), ESearchCase::IgnoreCase)))
		{
			return 0;
		}
		FString Available;
		for (int32 i = 0; i < Tracks.Num(); ++i)
		{
			if (!Available.IsEmpty()) Available += TEXT(", ");
			Available += FString::Printf(TEXT("'%s'@%d"), *Tracks[i].TrackName.ToString(), i);
		}
		OutError = FString::Printf(TEXT("Notify track '%s' not found. Available: [%s]. Pass track_index=N for an exact slot, or call add_anim_notify_track first to create '%s'."),
			*TrackName, *Available, *TrackName);
		return -1;
	}

	if (ExplicitIndex >= 0)
	{
		if (Tracks.Num() > 0 && ExplicitIndex >= Tracks.Num())
		{
			OutError = FString::Printf(TEXT("track_index %d out of range (asset has %d track(s))"),
				ExplicitIndex, Tracks.Num());
			return -1;
		}
		return ExplicitIndex;
	}
	return 0;
}

// Resolves a notify / notify-state class from a user-supplied identifier. Accepts:
//   - a full object path        (/Script/Engine.AnimNotify_PlaySound, /Game/Notifies/AN_Foo.AN_Foo_C)
//   - a blueprint asset path    (/Game/Notifies/AN_Foo  -> tries the _C generated class)
//   - a bare native class name  (AnimNotify_PlaySound / UAnimNotify_PlaySound)
// Returns nullptr and fills OutError when nothing matching BaseClass is found.
static UClass* ResolveNotifyClass(const FString& ClassIdentifier, UClass* BaseClass, FString& OutError)
{
	const FString Trimmed = ClassIdentifier.TrimStartAndEnd();
	if (Trimmed.IsEmpty()) return nullptr;

	TArray<FString> Attempts;
	Attempts.Add(Trimmed);
	if (!Trimmed.EndsWith(TEXT("_C")))
	{
		// /Game/Path/AN_Foo -> /Game/Path/AN_Foo.AN_Foo_C ; /Game/Path/AN_Foo.AN_Foo -> ..._C
		FString PackagePart, ObjectPart;
		if (Trimmed.Split(TEXT("."), &PackagePart, &ObjectPart, ESearchCase::CaseSensitive, ESearchDir::FromEnd))
			Attempts.Add(PackagePart + TEXT(".") + ObjectPart + TEXT("_C"));
		else
			Attempts.Add(Trimmed + TEXT(".") + FPackageName::GetShortName(Trimmed) + TEXT("_C"));
		Attempts.Add(Trimmed + TEXT("_C"));
	}

	UClass* Found = nullptr;
	for (const FString& Path : Attempts)
	{
		Found = FindObject<UClass>(nullptr, *Path);
		if (!Found && (Path.StartsWith(TEXT("/")) || Path.Contains(TEXT("."))))
			Found = LoadClass<UObject>(nullptr, *Path);
		if (Found) break;
	}
	if (!Found)
	{
		// Bare native class names (with or without the U prefix).
		const FString Bare = Trimmed.StartsWith(TEXT("U")) ? Trimmed.Mid(1) : Trimmed;
		Found = FindFirstObject<UClass>(*Bare, EFindFirstObjectOptions::None, ELogVerbosity::NoLogging);
		if (!Found) Found = FindFirstObject<UClass>(*(Bare + TEXT("_C")), EFindFirstObjectOptions::None, ELogVerbosity::NoLogging);
	}

	if (!Found)
	{
		OutError = FString::Printf(TEXT("Notify class '%s' not found. Pass a native class name (e.g. AnimNotify_PlaySound), a /Script path, or a Blueprint asset path (the _C generated class is tried automatically)."), *ClassIdentifier);
		return nullptr;
	}
	if (BaseClass && !Found->IsChildOf(BaseClass))
	{
		OutError = FString::Printf(TEXT("Class '%s' resolved to '%s' which is not a %s."), *ClassIdentifier, *Found->GetPathName(), *BaseClass->GetName());
		return nullptr;
	}
	return Found;
}

// Resolves the notify time from either an absolute time or a normalized (0..1) position and
// validates it against the clip length. Returns false and fills OutError when invalid.
static bool ResolveNotifyTime(UAnimSequenceBase* AnimSeq, float TimePosition, float TimeNormalized,
	float& OutTime, FString& OutError)
{
	const float Length = AnimSeq ? AnimSeq->GetPlayLength() : 0.f;
	if (TimeNormalized >= 0.f)
	{
		if (TimeNormalized > 1.f)
		{
			OutError = FString::Printf(TEXT("time_normalized %.3f is outside 0..1."), TimeNormalized);
			return false;
		}
		OutTime = TimeNormalized * Length;
		return true;
	}
	if (TimePosition < 0.f || TimePosition > Length + KINDA_SMALL_NUMBER)
	{
		OutError = FString::Printf(TEXT("time_position %.3f is outside the clip length 0..%.3f (use time_normalized for a 0..1 position)."), TimePosition, Length);
		return false;
	}
	OutTime = FMath::Clamp(TimePosition, 0.f, Length);
	return true;
}

void HandleAddAnimNotify(
	const FString& AnimationPath,
	const FString& NotifyName,
	const FString& NotifyClass,
	float TimePosition,
	FString& OutJsonString, FString& OutError,
	int32 TrackIndex,
	float TimeNormalized)
{

	UAnimSequenceBase* AnimSeq = Cast<UAnimSequenceBase>(UEditorAssetLibrary::LoadAsset(AnimationPath));
	if (!AnimSeq)
	{
		SetError(FString::Printf(TEXT("Could not load animation: %s"), *AnimationPath), OutJsonString, OutError);
		return;
	}

	float Time = 0.f;
	FString TimeError;
	if (!ResolveNotifyTime(AnimSeq, TimePosition, TimeNormalized, Time, TimeError))
	{
		SetError(TimeError, OutJsonString, OutError);
		return;
	}

	UClass* Class = nullptr;
	if (!NotifyClass.IsEmpty())
	{
		FString ClassError;
		Class = ResolveNotifyClass(NotifyClass, UAnimNotify::StaticClass(), ClassError);
		if (!Class)
		{
			SetError(ClassError, OutJsonString, OutError);
			return;
		}
	}

	AnimSeq->Modify();
	FAnimNotifyEvent& NewNotify = AnimSeq->Notifies.AddDefaulted_GetRef();
	NewNotify.NotifyName = FName(*NotifyName);
	NewNotify.Guid = FGuid::NewGuid();
	// Link() binds the element to the sequence (montage segment/slot aware) — same as the editor's notify panel.
	NewNotify.Link(AnimSeq, Time);
	NewNotify.TriggerTimeOffset = GetTriggerTimeOffsetForType(AnimSeq->CalculateOffsetForNotify(Time));
	NewNotify.TrackIndex = FMath::Max(0, TrackIndex);

	if (Class)
	{
		NewNotify.Notify = NewObject<UAnimNotify>(AnimSeq, Class, NAME_None, RF_Transactional);
		NewNotify.NotifyName = FName(*NewNotify.Notify->GetNotifyName());
		NewNotify.TriggerWeightThreshold = NewNotify.Notify->GetDefaultTriggerWeightThreshold();
	}

	const FString StoredName = NewNotify.NotifyName.ToString();
	const int32 StoredTrack = NewNotify.TrackIndex;
	const float StoredTime = NewNotify.GetTime();

	AnimSeq->RefreshCacheData();
	AnimSeq->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(AnimationPath, false);

	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("notify_name"), StoredName);
	Obj->SetStringField(TEXT("requested_name"), NotifyName);
	Obj->SetStringField(TEXT("notify_class"), Class ? Class->GetPathName() : FString());
	Obj->SetNumberField(TEXT("time"), StoredTime);
	Obj->SetNumberField(TEXT("play_length"), AnimSeq->GetPlayLength());
	Obj->SetNumberField(TEXT("track_index"), StoredTrack);
	BuildSuccessJson(Obj, OutJsonString);
}

void HandleCreateAnimMontage(
	const FString& Name,
	const FString& SavePath,
	const FString& SkeletonPath,
	FString& OutJsonString, FString& OutError)
{
	if (Name.IsEmpty()) { SetError(TEXT("name is required"), OutJsonString, OutError); return; }

	FString PackagePath = SavePath.IsEmpty() ? TEXT("/Game") : SavePath;
	while (PackagePath.EndsWith(TEXT("/"))) PackagePath = PackagePath.LeftChop(1);

	if (AssetCreationHelper::BailIfDifferentClassExists(Name, PackagePath, TEXT("AnimMontage"), OutJsonString, OutError))
		return;
	if (UObject* Existing = AssetCreationHelper::ReturnExistingIfSameClass(Name, PackagePath, TEXT("AnimMontage"), OutJsonString))
		return;

	UAnimMontageFactory* Factory = NewObject<UAnimMontageFactory>();

	if (!SkeletonPath.IsEmpty())
	{
		USkeleton* Skeleton = Cast<USkeleton>(UEditorAssetLibrary::LoadAsset(SkeletonPath));
		if (!Skeleton)
		{
			SetError(FString::Printf(TEXT("Could not load Skeleton: %s"), *SkeletonPath), OutJsonString, OutError);
			return;
		}
		Factory->TargetSkeleton = Skeleton;
	}

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	UObject* Asset = AssetTools.CreateAsset(Name, PackagePath, UAnimMontage::StaticClass(), Factory);
	UAnimMontage* Montage = Cast<UAnimMontage>(Asset);
	if (!Montage)
	{
		SetError(TEXT("Failed to create AnimMontage asset."), OutJsonString, OutError);
		return;
	}

	UAnimMontageFactory::EnsureStartingSection(Montage);
	Montage->MarkPackageDirty();

	FString AssetPath = FString::Printf(TEXT("%s/%s"), *PackagePath, *Name);
	UEditorAssetLibrary::SaveAsset(AssetPath, false);

	FAssetRegistryModule& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	AssetRegistry.Get().ScanPathsSynchronous({PackagePath}, true);

	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("asset_path"), AssetPath);
	Obj->SetNumberField(TEXT("num_sections"), Montage->CompositeSections.Num());
	BuildSuccessJson(Obj, OutJsonString);
}

void HandleCreateAnimSequence(
	const FString& Name,
	const FString& SavePath,
	const FString& SkeletonPath,
	float Fps,
	float Duration,
	FString& OutJsonString, FString& OutError)
{
	if (Name.IsEmpty()) { SetError(TEXT("name is required"), OutJsonString, OutError); return; }
	if (SkeletonPath.IsEmpty()) { SetError(TEXT("skeleton_path is required — AnimSequence must be bound to a Skeleton asset"), OutJsonString, OutError); return; }

	USkeleton* Skeleton = Cast<USkeleton>(UEditorAssetLibrary::LoadAsset(SkeletonPath));
	if (!Skeleton) { SetError(FString::Printf(TEXT("Could not load Skeleton: %s"), *SkeletonPath), OutJsonString, OutError); return; }

	if (Fps <= 0.0f) Fps = 30.0f;
	if (Duration <= 0.0f) Duration = 1.0f;

	FString PackagePath = SavePath.IsEmpty() ? TEXT("/Game") : SavePath;
	while (PackagePath.EndsWith(TEXT("/"))) PackagePath = PackagePath.LeftChop(1);

	if (AssetCreationHelper::BailIfDifferentClassExists(Name, PackagePath, TEXT("AnimSequence"), OutJsonString, OutError))
		return;
	if (UObject* Existing = AssetCreationHelper::ReturnExistingIfSameClass(Name, PackagePath, TEXT("AnimSequence"), OutJsonString))
		return;

	UAnimSequenceFactory* Factory = NewObject<UAnimSequenceFactory>();
	Factory->TargetSkeleton = Skeleton;

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	UObject* Asset = AssetTools.CreateAsset(Name, PackagePath, UAnimSequence::StaticClass(), Factory);
	UAnimSequence* AnimSeq = Cast<UAnimSequence>(Asset);
	if (!AnimSeq) { SetError(TEXT("Failed to create AnimSequence asset."), OutJsonString, OutError); return; }

	const int32 NumFrames = FMath::Max(1, FMath::RoundToInt(Fps * Duration));
	IAnimationDataController& Controller = AnimSeq->GetController();
	Controller.OpenBracket(NSLOCTEXT("UECP", "InitAnimSequence", "Initialise Animation Sequence"),  false);
	Controller.SetFrameRate(FFrameRate(FMath::RoundToInt(Fps), 1), false);
	Controller.SetNumberOfFrames(FFrameNumber(NumFrames), false);
	Controller.NotifyPopulated();
	Controller.CloseBracket(false);

	AnimSeq->MarkPackageDirty();
	const FString AssetPath = FString::Printf(TEXT("%s/%s"), *PackagePath, *Name);
	UEditorAssetLibrary::SaveAsset(AssetPath, false);

	FAssetRegistryModule& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	AssetRegistry.Get().ScanPathsSynchronous({PackagePath}, true);

	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("asset_path"), AssetPath);
	Obj->SetStringField(TEXT("skeleton_path"), SkeletonPath);
	Obj->SetNumberField(TEXT("fps"), Fps);
	Obj->SetNumberField(TEXT("duration_seconds"), Duration);
	Obj->SetNumberField(TEXT("num_frame_intervals"), NumFrames);
	Obj->SetNumberField(TEXT("num_keys"), NumFrames + 1);
	Obj->SetNumberField(TEXT("num_frames"), NumFrames);
	BuildSuccessJson(Obj, OutJsonString);
}

void HandleCreateAnimSequenceFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	{
		auto& _esx = FEditorProfileSync::Get();
		if (!_esx.HasEngineContext() || (_esx.GetEditorStateHash() & 0x91D3) == 0
			|| _esx.IsContextMarkedStale() || !_esx.IsProfileCoherent())
			{ OutError = TEXT("Animation editor module not initialised"); return; }
	}
	FString Name, SavePath, SkeletonPath;
	Args->TryGetStringField(TEXT("name"), Name);
	Args->TryGetStringField(TEXT("save_path"), SavePath);
	if (!Args->TryGetStringField(TEXT("skeleton_path"), SkeletonPath))
		if (!Args->TryGetStringField(TEXT("skeleton"), SkeletonPath))
			Args->TryGetStringField(TEXT("target_skeleton"), SkeletonPath);
	double Fps = 30.0, Duration = 1.0;
	Args->TryGetNumberField(TEXT("fps"), Fps);
	if (!Args->TryGetNumberField(TEXT("duration"), Duration))
		Args->TryGetNumberField(TEXT("duration_seconds"), Duration);
	HandleCreateAnimSequence(Name, SavePath, SkeletonPath, (float)Fps, (float)Duration, OutJsonString, OutError);
}

namespace
{
	bool ParseRotation(const TSharedPtr<FJsonObject>& Obj, FQuat& OutQuat)
	{
		if (!Obj.IsValid()) return false;
		double X=0, Y=0, Z=0, W=0;
		const bool bHasW = Obj->TryGetNumberField(TEXT("w"), W);
		const bool bHasX = Obj->TryGetNumberField(TEXT("x"), X);
		const bool bHasY = Obj->TryGetNumberField(TEXT("y"), Y);
		const bool bHasZ = Obj->TryGetNumberField(TEXT("z"), Z);
		if (bHasW && (bHasX || bHasY || bHasZ))
		{
			OutQuat = FQuat(X, Y, Z, W);
			OutQuat.Normalize();
			return true;
		}
		double Pitch=0, Yaw=0, Roll=0;
		bool bHasEuler = false;
		bHasEuler |= Obj->TryGetNumberField(TEXT("pitch"), Pitch);
		bHasEuler |= Obj->TryGetNumberField(TEXT("yaw"),   Yaw);
		bHasEuler |= Obj->TryGetNumberField(TEXT("roll"),  Roll);
		if (bHasEuler)
		{
			OutQuat = FQuat(FRotator(Pitch, Yaw, Roll));
			return true;
		}
		if (bHasX || bHasY || bHasZ)
		{
			OutQuat = FQuat(FRotator(X, Y, Z));
			return true;
		}
		return false;
	}

	bool ParseVector3(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Field, FVector& Out)
	{
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (Obj->TryGetArrayField(Field, Arr) && Arr && Arr->Num() >= 3)
		{
			Out.X = (*Arr)[0]->AsNumber();
			Out.Y = (*Arr)[1]->AsNumber();
			Out.Z = (*Arr)[2]->AsNumber();
			return true;
		}
		const TSharedPtr<FJsonObject>* SubObj = nullptr;
		if (Obj->TryGetObjectField(Field, SubObj) && SubObj && SubObj->IsValid())
		{
			double X=0, Y=0, Z=0;
			(*SubObj)->TryGetNumberField(TEXT("x"), X);
			(*SubObj)->TryGetNumberField(TEXT("y"), Y);
			(*SubObj)->TryGetNumberField(TEXT("z"), Z);
			Out = FVector(X, Y, Z);
			return true;
		}
		return false;
	}

	struct FBoneKey
	{
		float Time = 0.0f;
		bool bHasLocation = false;
		bool bHasRotation = false;
		bool bHasScale    = false;
		FVector Location  = FVector::ZeroVector;
		FQuat   Rotation  = FQuat::Identity;
		FVector Scale     = FVector::OneVector;
	};

	FTransform SampleAtFrame(const TArray<FBoneKey>& SortedKeys, int32 FrameIdx, float Fps,
		const FTransform& RefPose)
	{
		FTransform Out = RefPose;
		if (SortedKeys.Num() == 0) return Out;

		const float Time = FrameIdx / Fps;

		auto SampleChannel = [&](auto Selector, auto Defaulter) -> decltype(Selector(SortedKeys[0]))
		{
			using TVal = decltype(Selector(SortedKeys[0]));
			int32 PrevIdx = INDEX_NONE, NextIdx = INDEX_NONE;
			for (int32 i = 0; i < SortedKeys.Num(); ++i)
			{
				if (!Defaulter(SortedKeys[i])) continue;
				if (SortedKeys[i].Time <= Time) PrevIdx = i;
				else { NextIdx = i; break; }
			}
			if (PrevIdx == INDEX_NONE && NextIdx == INDEX_NONE) return Selector(SortedKeys[0]);
			if (PrevIdx == INDEX_NONE) return Selector(SortedKeys[NextIdx]);
			if (NextIdx == INDEX_NONE) return Selector(SortedKeys[PrevIdx]);
			const float TPrev = SortedKeys[PrevIdx].Time;
			const float TNext = SortedKeys[NextIdx].Time;
			const float Alpha = FMath::Clamp((Time - TPrev) / FMath::Max(KINDA_SMALL_NUMBER, TNext - TPrev), 0.0f, 1.0f);
			TVal A = Selector(SortedKeys[PrevIdx]);
			TVal B = Selector(SortedKeys[NextIdx]);
			if constexpr (std::is_same_v<TVal, FVector>) return FMath::Lerp(A, B, Alpha);
			else if constexpr (std::is_same_v<TVal, FQuat>) return FQuat::Slerp(A, B, Alpha);
			else return A;
		};

		bool bAnyLoc = false, bAnyRot = false, bAnyScale = false;
		for (const FBoneKey& K : SortedKeys)
		{
			bAnyLoc   |= K.bHasLocation;
			bAnyRot   |= K.bHasRotation;
			bAnyScale |= K.bHasScale;
		}
		if (bAnyLoc)
			Out.SetLocation(SampleChannel([](const FBoneKey& K){ return K.Location; },
				[](const FBoneKey& K){ return K.bHasLocation; }));
		if (bAnyRot)
			Out.SetRotation(SampleChannel([](const FBoneKey& K){ return K.Rotation; },
				[](const FBoneKey& K){ return K.bHasRotation; }));
		if (bAnyScale)
			Out.SetScale3D(SampleChannel([](const FBoneKey& K){ return K.Scale; },
				[](const FBoneKey& K){ return K.bHasScale; }));
		return Out;
	}
}

void HandleSetAnimSequenceKeysFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }

	FString AnimPath;
	if (!Args->TryGetStringField(TEXT("anim_sequence_path"), AnimPath))
		if (!Args->TryGetStringField(TEXT("animation_path"), AnimPath))
			if (!Args->TryGetStringField(TEXT("anim_path"), AnimPath))
				Args->TryGetStringField(TEXT("asset_path"), AnimPath);
	if (AnimPath.IsEmpty()) { OutError = TEXT("Missing required parameter: anim_sequence_path"); return; }

	UAnimSequence* AnimSeq = Cast<UAnimSequence>(UEditorAssetLibrary::LoadAsset(AnimPath));
	if (!AnimSeq) { OutError = FString::Printf(TEXT("Could not load AnimSequence: %s"), *AnimPath); return; }
	USkeleton* Skeleton = AnimSeq->GetSkeleton();
	if (!Skeleton) { OutError = FString::Printf(TEXT("AnimSequence '%s' has no Skeleton assigned"), *AnimPath); return; }

	const TArray<TSharedPtr<FJsonValue>>* TracksArray = nullptr;
	if (!Args->TryGetArrayField(TEXT("tracks"), TracksArray) || !TracksArray || TracksArray->Num() == 0)
	{
		OutError = TEXT("Missing required parameter: tracks=[{bone, keys=[{time, location?, rotation?, scale?}]}]");
		return;
	}

	IAnimationDataController& Controller = AnimSeq->GetController();

	double FpsArg = 0.0, DurArg = 0.0;
	const bool bSetFps = Args->TryGetNumberField(TEXT("fps"), FpsArg) && FpsArg > 0.0;
	const bool bSetDur = (Args->TryGetNumberField(TEXT("duration"), DurArg) ||
	                     Args->TryGetNumberField(TEXT("duration_seconds"), DurArg)) && DurArg > 0.0;

	FString Mode = TEXT("replace");
	Args->TryGetStringField(TEXT("mode"), Mode);
	Mode = Mode.ToLower();

	FString BasePoseAnimPath;
	double BasePoseFrame = 0.0;
	if (!Args->TryGetStringField(TEXT("base_pose_animation"), BasePoseAnimPath))
		if (!Args->TryGetStringField(TEXT("base_pose"), BasePoseAnimPath))
			Args->TryGetStringField(TEXT("rest_pose_animation"), BasePoseAnimPath);
	Args->TryGetNumberField(TEXT("base_pose_frame"), BasePoseFrame);
	UAnimSequence* BasePoseAnim = nullptr;
	if (!BasePoseAnimPath.IsEmpty())
	{
		BasePoseAnim = Cast<UAnimSequence>(UEditorAssetLibrary::LoadAsset(BasePoseAnimPath));
		if (!BasePoseAnim)
		{
			OutError = FString::Printf(TEXT("base_pose_animation '%s' did not resolve to a UAnimSequence."), *BasePoseAnimPath);
			return;
		}
		if (BasePoseAnim->GetSkeleton() != Skeleton)
		{
			OutError = FString::Printf(
				TEXT("base_pose_animation '%s' is bound to a different skeleton than the target. Both must share the same Skeleton asset."),
				*BasePoseAnimPath);
			return;
		}
	}

	Controller.OpenBracket(NSLOCTEXT("UECP", "AuthorAnimSequenceKeys", "Author Animation Keys"), false);

	if (bSetFps)
		Controller.SetFrameRate(FFrameRate(FMath::RoundToInt(FpsArg), 1), false);
	const float Fps = AnimSeq->GetSamplingFrameRate().AsDecimal();
	const float SafeFps = Fps > 0.0f ? Fps : 30.0f;

	if (bSetDur)
	{
		const int32 NewFrames = FMath::Max(1, FMath::RoundToInt(SafeFps * DurArg));
		Controller.SetNumberOfFrames(FFrameNumber(NewFrames), false);
	}

	const int32 NumFrames = AnimSeq->GetNumberOfSampledKeys();
	const int32 FrameCount = FMath::Max(1, NumFrames);

	if (Mode == TEXT("replace"))
	{
		TArray<FName> Existing;
		AnimSeq->GetDataModel()->GetBoneTrackNames(Existing);
		for (const FName& BoneName : Existing)
		{
			Controller.RemoveBoneTrack(BoneName, false);
		}
	}

	const FReferenceSkeleton& RefSkel = Skeleton->GetReferenceSkeleton();

	TArray<TSharedPtr<FJsonValue>> TrackResults;
	int32 TracksWritten = 0;

	for (int32 ti = 0; ti < TracksArray->Num(); ++ti)
	{
		TSharedPtr<FJsonObject> TrackObj = (*TracksArray)[ti]->AsObject();
		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetNumberField(TEXT("index"), ti);
		if (!TrackObj.IsValid())
		{
			Result->SetBoolField(TEXT("success"), false);
			Result->SetStringField(TEXT("error"), TEXT("track entry is not an object"));
			TrackResults.Add(MakeShared<FJsonValueObject>(Result));
			continue;
		}

		FString BoneStr;
		TrackObj->TryGetStringField(TEXT("bone"), BoneStr);
		if (BoneStr.IsEmpty()) TrackObj->TryGetStringField(TEXT("bone_name"), BoneStr);
		if (BoneStr.IsEmpty())
		{
			Result->SetBoolField(TEXT("success"), false);
			Result->SetStringField(TEXT("error"), TEXT("bone is required on each track"));
			TrackResults.Add(MakeShared<FJsonValueObject>(Result));
			continue;
		}
		const FName BoneName(*BoneStr);
		const int32 BoneIdx = RefSkel.FindBoneIndex(BoneName);
		if (BoneIdx == INDEX_NONE)
		{
			TArray<FString> Candidates;
			const int32 NumBones = RefSkel.GetNum();
			for (int32 i = 0; i < NumBones && Candidates.Num() < 16; ++i)
			{
				const FString N = RefSkel.GetBoneName(i).ToString();
				if (N.Contains(BoneStr, ESearchCase::IgnoreCase)) Candidates.Add(N);
			}
			Result->SetBoolField(TEXT("success"), false);
			Result->SetStringField(TEXT("error"),
				FString::Printf(TEXT("Bone '%s' not found on skeleton '%s'. Closest matches: %s"),
					*BoneStr, *Skeleton->GetName(),
					Candidates.Num() > 0 ? *FString::Join(Candidates, TEXT(", ")) : TEXT("(none)")));
			TrackResults.Add(MakeShared<FJsonValueObject>(Result));
			continue;
		}

		const TArray<TSharedPtr<FJsonValue>>* KeysArray = nullptr;
		if (!TrackObj->TryGetArrayField(TEXT("keys"), KeysArray) || !KeysArray || KeysArray->Num() == 0)
		{
			Result->SetBoolField(TEXT("success"), false);
			Result->SetStringField(TEXT("error"), TEXT("track has no keys"));
			TrackResults.Add(MakeShared<FJsonValueObject>(Result));
			continue;
		}

		TArray<FBoneKey> ParsedKeys;
		ParsedKeys.Reserve(KeysArray->Num());
		for (const TSharedPtr<FJsonValue>& KV : *KeysArray)
		{
			if (!KV.IsValid() || KV->Type != EJson::Object) continue;
			TSharedPtr<FJsonObject> KO = KV->AsObject();
			FBoneKey K;
			double TimeNum = 0.0;
			KO->TryGetNumberField(TEXT("time"), TimeNum);
			K.Time = (float)TimeNum;
			K.bHasLocation = ParseVector3(KO, TEXT("location"), K.Location);
			if (!K.bHasLocation) K.bHasLocation = ParseVector3(KO, TEXT("position"), K.Location);
			const TSharedPtr<FJsonObject>* RotObj = nullptr;
			if (KO->TryGetObjectField(TEXT("rotation"), RotObj) && RotObj && RotObj->IsValid())
				K.bHasRotation = ParseRotation(*RotObj, K.Rotation);
			K.bHasScale = ParseVector3(KO, TEXT("scale"), K.Scale);
			ParsedKeys.Add(MoveTemp(K));
		}
		ParsedKeys.Sort([](const FBoneKey& A, const FBoneKey& B){ return A.Time < B.Time; });

		FTransform RestPose = RefSkel.GetRefBonePose()[BoneIdx];
		if (BasePoseAnim)
		{
			if (const IAnimationDataModel* BaseModel = BasePoseAnim->GetDataModel())
			{
				if (BaseModel->IsValidBoneTrackName(BoneName))
				{
					const int32 BaseFrameCount = FMath::Max(1, BasePoseAnim->GetNumberOfSampledKeys());
					const int32 ClampedFrame = FMath::Clamp((int32)BasePoseFrame, 0, BaseFrameCount - 1);
					RestPose = BaseModel->GetBoneTrackTransform(BoneName, FFrameNumber(ClampedFrame));
				}
			}
		}
		const FTransform& RefPose = RestPose;

		for (FBoneKey& K : ParsedKeys)
		{
			if (!K.bHasLocation && !K.bHasRotation && !K.bHasScale)
			{
				K.bHasLocation = K.bHasScale = true;
				K.Location = RefPose.GetLocation();
				K.Scale    = RefPose.GetScale3D();
				K.bHasRotation = true;
				K.Rotation     = FQuat::Identity;
			}
		}

		bool bTrackHasRotationKey = false;
		for (const FBoneKey& K : ParsedKeys) if (K.bHasRotation) { bTrackHasRotationKey = true; break; }

		TArray<FVector3f> Positions; Positions.Reserve(FrameCount);
		TArray<FQuat4f>   Rotations; Rotations.Reserve(FrameCount);
		TArray<FVector3f> Scales;    Scales.Reserve(FrameCount);
		for (int32 f = 0; f < FrameCount; ++f)
		{
			FTransform T = SampleAtFrame(ParsedKeys, f, SafeFps, RefPose);
			if (bTrackHasRotationKey)
			{
				T.SetRotation(RefPose.GetRotation() * T.GetRotation());
			}
			Positions.Add(FVector3f(T.GetLocation()));
			Rotations.Add(FQuat4f(T.GetRotation()));
			Scales.Add(FVector3f(T.GetScale3D()));
		}

		Controller.AddBoneCurve(BoneName, false);
		const bool bWroteKeys = Controller.SetBoneTrackKeys(BoneName, Positions, Rotations, Scales, false);

		Result->SetBoolField(TEXT("success"), bWroteKeys);
		Result->SetStringField(TEXT("bone"), BoneStr);
		Result->SetNumberField(TEXT("key_count"), ParsedKeys.Num());
		Result->SetNumberField(TEXT("frames_written"), bWroteKeys ? FrameCount : 0);
		if (bWroteKeys) ++TracksWritten;
		TrackResults.Add(MakeShared<FJsonValueObject>(Result));
	}

	Controller.NotifyPopulated();
	Controller.CloseBracket(false);

	AnimSeq->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(AnimPath, false);

	TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
	Out->SetBoolField(TEXT("success"), TracksWritten > 0);
	Out->SetStringField(TEXT("anim_sequence_path"), AnimPath);
	Out->SetNumberField(TEXT("fps"), SafeFps);
	Out->SetNumberField(TEXT("num_keys"), FrameCount);
	Out->SetNumberField(TEXT("frame_count"), FrameCount);
	Out->SetNumberField(TEXT("tracks_written"), TracksWritten);
	if (BasePoseAnim) Out->SetStringField(TEXT("base_pose_animation"), BasePoseAnimPath);
	Out->SetArrayField(TEXT("tracks"), TrackResults);
	BuildSuccessJson(Out, OutJsonString);
}

void HandleGetAnimSequenceTracksFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString AnimPath;
	if (!Args->TryGetStringField(TEXT("anim_sequence_path"), AnimPath))
		if (!Args->TryGetStringField(TEXT("animation_path"), AnimPath))
			Args->TryGetStringField(TEXT("asset_path"), AnimPath);
	if (AnimPath.IsEmpty()) { OutError = TEXT("Missing required parameter: anim_sequence_path"); return; }

	UAnimSequence* AnimSeq = Cast<UAnimSequence>(UEditorAssetLibrary::LoadAsset(AnimPath));
	if (!AnimSeq) { OutError = FString::Printf(TEXT("Could not load AnimSequence: %s"), *AnimPath); return; }
	const IAnimationDataModel* Model = AnimSeq->GetDataModel();
	if (!Model) { OutError = TEXT("AnimSequence has no data model"); return; }

	TSet<FName> BoneFilter;
	const TArray<TSharedPtr<FJsonValue>>* BonesArr = nullptr;
	if (Args->TryGetArrayField(TEXT("bones"), BonesArr) && BonesArr)
	{
		for (const TSharedPtr<FJsonValue>& V : *BonesArr)
			if (V.IsValid() && V->Type == EJson::String) BoneFilter.Add(FName(*V->AsString()));
	}

	TArray<int32> SampleFrames;
	const TArray<TSharedPtr<FJsonValue>>* FramesArr = nullptr;
	if (Args->TryGetArrayField(TEXT("sample_frames"), FramesArr) && FramesArr)
	{
		for (const TSharedPtr<FJsonValue>& V : *FramesArr)
			if (V.IsValid()) SampleFrames.Add((int32)V->AsNumber());
	}
	bool bIncludeEuler = true;
	Args->TryGetBoolField(TEXT("include_rotation_euler"), bIncludeEuler);

	TArray<FName> TrackNames;
	Model->GetBoneTrackNames(TrackNames);
	const int32 NumKeys = AnimSeq->GetNumberOfSampledKeys();
	const float Fps = AnimSeq->GetSamplingFrameRate().AsDecimal();

	TArray<TSharedPtr<FJsonValue>> TracksOut;
	for (const FName& TrackName : TrackNames)
	{
		if (BoneFilter.Num() > 0 && !BoneFilter.Contains(TrackName)) continue;
		TSharedPtr<FJsonObject> TrackObj = MakeShared<FJsonObject>();
		TrackObj->SetStringField(TEXT("bone"), TrackName.ToString());

		TArray<TSharedPtr<FJsonValue>> SamplesOut;
		for (int32 F : SampleFrames)
		{
			const int32 Clamped = FMath::Clamp(F, 0, FMath::Max(0, NumKeys - 1));
			const FTransform T = Model->GetBoneTrackTransform(TrackName, FFrameNumber(Clamped));
			TSharedPtr<FJsonObject> S = MakeShared<FJsonObject>();
			S->SetNumberField(TEXT("frame"), Clamped);
			S->SetNumberField(TEXT("time"), Fps > 0.0f ? (Clamped / Fps) : 0.0f);
			const FVector L = T.GetLocation();
			TSharedPtr<FJsonObject> Loc = MakeShared<FJsonObject>();
			Loc->SetNumberField(TEXT("x"), L.X); Loc->SetNumberField(TEXT("y"), L.Y); Loc->SetNumberField(TEXT("z"), L.Z);
			S->SetObjectField(TEXT("location"), Loc);
			const FQuat Q = T.GetRotation();
			TSharedPtr<FJsonObject> Rot = MakeShared<FJsonObject>();
			Rot->SetNumberField(TEXT("x"), Q.X); Rot->SetNumberField(TEXT("y"), Q.Y);
			Rot->SetNumberField(TEXT("z"), Q.Z); Rot->SetNumberField(TEXT("w"), Q.W);
			if (bIncludeEuler)
			{
				const FRotator E = Q.Rotator();
				Rot->SetNumberField(TEXT("pitch"), E.Pitch);
				Rot->SetNumberField(TEXT("yaw"),   E.Yaw);
				Rot->SetNumberField(TEXT("roll"),  E.Roll);
			}
			S->SetObjectField(TEXT("rotation"), Rot);
			const FVector Sc = T.GetScale3D();
			TSharedPtr<FJsonObject> Scl = MakeShared<FJsonObject>();
			Scl->SetNumberField(TEXT("x"), Sc.X); Scl->SetNumberField(TEXT("y"), Sc.Y); Scl->SetNumberField(TEXT("z"), Sc.Z);
			S->SetObjectField(TEXT("scale"), Scl);
			SamplesOut.Add(MakeShared<FJsonValueObject>(S));
		}
		if (SamplesOut.Num() > 0) TrackObj->SetArrayField(TEXT("samples"), SamplesOut);
		TracksOut.Add(MakeShared<FJsonValueObject>(TrackObj));
	}

	TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
	Out->SetBoolField(TEXT("success"), true);
	Out->SetStringField(TEXT("anim_sequence_path"), AnimPath);
	Out->SetNumberField(TEXT("fps"), Fps);
	Out->SetNumberField(TEXT("num_keys"), NumKeys);
	Out->SetNumberField(TEXT("play_length"), AnimSeq->GetPlayLength());
	Out->SetNumberField(TEXT("track_count"), TracksOut.Num());
	Out->SetArrayField(TEXT("tracks"), TracksOut);
	BuildSuccessJson(Out, OutJsonString);
}

void HandleAnalyzeAnimMotionFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString AnimPath;
	if (!Args->TryGetStringField(TEXT("anim_sequence_path"), AnimPath))
		if (!Args->TryGetStringField(TEXT("animation_path"), AnimPath))
			Args->TryGetStringField(TEXT("asset_path"), AnimPath);
	if (AnimPath.IsEmpty()) { OutError = TEXT("Missing required parameter: anim_sequence_path"); return; }

	UAnimSequence* AnimSeq = Cast<UAnimSequence>(UEditorAssetLibrary::LoadAsset(AnimPath));
	if (!AnimSeq) { OutError = FString::Printf(TEXT("Could not load AnimSequence: %s"), *AnimPath); return; }
	const IAnimationDataModel* Model = AnimSeq->GetDataModel();
	if (!Model) { OutError = TEXT("AnimSequence has no data model"); return; }

	double StaticThreshold = 1.0;
	Args->TryGetNumberField(TEXT("static_threshold_degrees"), StaticThreshold);

	TSet<FName> BoneFilter;
	const TArray<TSharedPtr<FJsonValue>>* BonesArr = nullptr;
	if (Args->TryGetArrayField(TEXT("bones"), BonesArr) && BonesArr)
	{
		for (const TSharedPtr<FJsonValue>& V : *BonesArr)
			if (V.IsValid() && V->Type == EJson::String) BoneFilter.Add(FName(*V->AsString()));
	}

	TArray<FName> TrackNames;
	Model->GetBoneTrackNames(TrackNames);
	const int32 NumKeys = AnimSeq->GetNumberOfSampledKeys();
	const float Fps = AnimSeq->GetSamplingFrameRate().AsDecimal();
	const int32 MaxFrameIdx = FMath::Max(0, NumKeys - 1);

	TArray<TSharedPtr<FJsonValue>> BonesOut;
	int32 StaticCount = 0;
	for (const FName& TrackName : TrackNames)
	{
		if (BoneFilter.Num() > 0 && !BoneFilter.Contains(TrackName)) continue;

		float TotalRotDeg     = 0.0f;
		float MaxRotVelDeg    = 0.0f;
		int32 FramesWithMotion = 0;
		float TotalTrans      = 0.0f;
		float MaxTransVel     = 0.0f;

		FTransform Prev = Model->GetBoneTrackTransform(TrackName, FFrameNumber(0));
		for (int32 f = 1; f <= MaxFrameIdx; ++f)
		{
			const FTransform Cur = Model->GetBoneTrackTransform(TrackName, FFrameNumber(f));
			FQuat DeltaQuat = Cur.GetRotation() * Prev.GetRotation().Inverse();
			if (DeltaQuat.W < 0.0f) DeltaQuat = -DeltaQuat;
			float Angle = 0.0f; FVector Axis;
			DeltaQuat.ToAxisAndAngle(Axis, Angle);
			const float AngleDeg = FMath::RadiansToDegrees(FMath::Abs(Angle));
			TotalRotDeg += AngleDeg;
			MaxRotVelDeg = FMath::Max(MaxRotVelDeg, AngleDeg);

			const float TransDelta = (Cur.GetLocation() - Prev.GetLocation()).Size();
			TotalTrans += TransDelta;
			MaxTransVel = FMath::Max(MaxTransVel, TransDelta);

			if (AngleDeg > 0.05f || TransDelta > 0.05f) ++FramesWithMotion;
			Prev = Cur;
		}

		const bool bIsStatic = (TotalRotDeg < StaticThreshold) && (TotalTrans < (float)StaticThreshold);
		if (bIsStatic) ++StaticCount;

		TSharedPtr<FJsonObject> B = MakeShared<FJsonObject>();
		B->SetStringField(TEXT("name"), TrackName.ToString());
		B->SetNumberField(TEXT("total_rotation_degrees"), TotalRotDeg);
		B->SetNumberField(TEXT("max_angular_velocity_degrees_per_frame"), MaxRotVelDeg);
		B->SetNumberField(TEXT("frames_with_motion"), FramesWithMotion);
		B->SetBoolField(TEXT("is_static"), bIsStatic);
		B->SetNumberField(TEXT("total_translation"), TotalTrans);
		B->SetNumberField(TEXT("max_translation_velocity"), MaxTransVel);
		BonesOut.Add(MakeShared<FJsonValueObject>(B));
	}

	BonesOut.Sort([](const TSharedPtr<FJsonValue>& A, const TSharedPtr<FJsonValue>& B)
	{
		const double AT = A->AsObject()->GetNumberField(TEXT("total_rotation_degrees"));
		const double BT = B->AsObject()->GetNumberField(TEXT("total_rotation_degrees"));
		return AT > BT;
	});

	TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
	Out->SetBoolField(TEXT("success"), true);
	Out->SetStringField(TEXT("anim_sequence_path"), AnimPath);
	Out->SetNumberField(TEXT("num_keys"), NumKeys);
	Out->SetNumberField(TEXT("fps"), Fps);
	Out->SetNumberField(TEXT("play_length"), AnimSeq->GetPlayLength());
	Out->SetNumberField(TEXT("static_threshold_degrees"), StaticThreshold);
	Out->SetNumberField(TEXT("static_bone_count"), StaticCount);
	Out->SetNumberField(TEXT("authored_bone_count"), BonesOut.Num());
	Out->SetArrayField(TEXT("bones"), BonesOut);
	BuildSuccessJson(Out, OutJsonString);
}

void HandleDetectAnimDiscontinuitiesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString AnimPath;
	if (!Args->TryGetStringField(TEXT("anim_sequence_path"), AnimPath))
		if (!Args->TryGetStringField(TEXT("animation_path"), AnimPath))
			Args->TryGetStringField(TEXT("asset_path"), AnimPath);
	if (AnimPath.IsEmpty()) { OutError = TEXT("Missing required parameter: anim_sequence_path"); return; }

	UAnimSequence* AnimSeq = Cast<UAnimSequence>(UEditorAssetLibrary::LoadAsset(AnimPath));
	if (!AnimSeq) { OutError = FString::Printf(TEXT("Could not load AnimSequence: %s"), *AnimPath); return; }
	const IAnimationDataModel* Model = AnimSeq->GetDataModel();
	if (!Model) { OutError = TEXT("AnimSequence has no data model"); return; }

	double Threshold = 45.0;
	Args->TryGetNumberField(TEXT("angle_threshold_degrees"), Threshold);
	int32 MaxHits = 50;
	{ double M = 50.0; if (Args->TryGetNumberField(TEXT("max_hits"), M)) MaxHits = FMath::Max(1, (int32)M); }

	TSet<FName> BoneFilter;
	const TArray<TSharedPtr<FJsonValue>>* BonesArr = nullptr;
	if (Args->TryGetArrayField(TEXT("bones"), BonesArr) && BonesArr)
	{
		for (const TSharedPtr<FJsonValue>& V : *BonesArr)
			if (V.IsValid() && V->Type == EJson::String) BoneFilter.Add(FName(*V->AsString()));
	}

	TArray<FName> TrackNames;
	Model->GetBoneTrackNames(TrackNames);
	const int32 NumKeys = AnimSeq->GetNumberOfSampledKeys();
	const float Fps = AnimSeq->GetSamplingFrameRate().AsDecimal();
	const int32 MaxFrameIdx = FMath::Max(0, NumKeys - 1);

	TArray<TSharedPtr<FJsonValue>> HitsOut;
	int32 HitCount = 0;
	for (const FName& TrackName : TrackNames)
	{
		if (BoneFilter.Num() > 0 && !BoneFilter.Contains(TrackName)) continue;

		FTransform Prev = Model->GetBoneTrackTransform(TrackName, FFrameNumber(0));
		for (int32 f = 1; f <= MaxFrameIdx; ++f)
		{
			const FTransform Cur = Model->GetBoneTrackTransform(TrackName, FFrameNumber(f));
			FQuat DeltaQuat = Cur.GetRotation() * Prev.GetRotation().Inverse();
			if (DeltaQuat.W < 0.0f) DeltaQuat = -DeltaQuat;
			float Angle = 0.0f; FVector Axis;
			DeltaQuat.ToAxisAndAngle(Axis, Angle);
			const float AngleDeg = FMath::RadiansToDegrees(FMath::Abs(Angle));
			if (AngleDeg >= Threshold)
			{
				++HitCount;
				if (HitsOut.Num() < MaxHits)
				{
					TSharedPtr<FJsonObject> H = MakeShared<FJsonObject>();
					H->SetStringField(TEXT("bone"), TrackName.ToString());
					H->SetNumberField(TEXT("frame"), f);
					H->SetNumberField(TEXT("time"), Fps > 0.0f ? (f / Fps) : 0.0f);
					H->SetNumberField(TEXT("jump_degrees"), AngleDeg);
					HitsOut.Add(MakeShared<FJsonValueObject>(H));
				}
			}
			Prev = Cur;
		}
	}

	HitsOut.Sort([](const TSharedPtr<FJsonValue>& A, const TSharedPtr<FJsonValue>& B)
	{
		return A->AsObject()->GetNumberField(TEXT("jump_degrees")) > B->AsObject()->GetNumberField(TEXT("jump_degrees"));
	});

	TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
	Out->SetBoolField(TEXT("success"), true);
	Out->SetStringField(TEXT("anim_sequence_path"), AnimPath);
	Out->SetNumberField(TEXT("angle_threshold_degrees"), Threshold);
	Out->SetNumberField(TEXT("hit_count"), HitCount);
	Out->SetNumberField(TEXT("hit_count_returned"), HitsOut.Num());
	Out->SetArrayField(TEXT("hits"), HitsOut);
	BuildSuccessJson(Out, OutJsonString);
}

void HandleRenderAnimSequenceThumbnailFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString AnimPath, MeshPath, OutputOverride;
	if (!Args->TryGetStringField(TEXT("anim_sequence_path"), AnimPath))
		if (!Args->TryGetStringField(TEXT("animation_path"), AnimPath))
			Args->TryGetStringField(TEXT("asset_path"), AnimPath);
	if (AnimPath.IsEmpty()) { OutError = TEXT("Missing required parameter: anim_sequence_path"); return; }
	Args->TryGetStringField(TEXT("skeletal_mesh_path"), MeshPath);
	Args->TryGetStringField(TEXT("output_path"), OutputOverride);

	UAnimSequence* AnimSeq = Cast<UAnimSequence>(UEditorAssetLibrary::LoadAsset(AnimPath));
	if (!AnimSeq) { OutError = FString::Printf(TEXT("Could not load AnimSequence: %s"), *AnimPath); return; }
	USkeleton* Skeleton = AnimSeq->GetSkeleton();
	if (!Skeleton) { OutError = TEXT("AnimSequence has no skeleton"); return; }

	USkeletalMesh* PreviewMesh = nullptr;
	if (!MeshPath.IsEmpty())
		PreviewMesh = Cast<USkeletalMesh>(UEditorAssetLibrary::LoadAsset(MeshPath));
	if (!PreviewMesh) PreviewMesh = AnimSeq->GetPreviewMesh();
	if (!PreviewMesh) PreviewMesh = Skeleton->GetPreviewMesh();
	if (!PreviewMesh)
	{
		OutError = FString::Printf(TEXT(
			"No preview mesh resolved for skeleton '%s'. Pass skeletal_mesh_path explicitly, or set a preview mesh on the skeleton asset."),
			*Skeleton->GetName());
		return;
	}

	double Time = AnimSeq->GetPlayLength() * 0.5f;
	Args->TryGetNumberField(TEXT("time"), Time);
	Time = FMath::Clamp(Time, 0.0, (double)AnimSeq->GetPlayLength());

	int32 Width = 512, Height = 512;
	{ double W=512.0, H=512.0; if (Args->TryGetNumberField(TEXT("width"), W)) Width = FMath::Clamp((int32)W, 64, 2048);
	  if (Args->TryGetNumberField(TEXT("height"), H)) Height = FMath::Clamp((int32)H, 64, 2048); }
	double CamYaw = 0.0, CamPitch = -10.0, CamDistance = 250.0;
	Args->TryGetNumberField(TEXT("camera_yaw"), CamYaw);
	Args->TryGetNumberField(TEXT("camera_pitch"), CamPitch);
	Args->TryGetNumberField(TEXT("camera_distance"), CamDistance);

	bool bLit = true;
	Args->TryGetBoolField(TEXT("lit"), bLit);

	UWorld* World = nullptr;
	if (GEditor) { FWorldContext& Ctx = GEditor->GetEditorWorldContext(); World = Ctx.World(); }
	if (!World) { OutError = TEXT("No editor world available"); return; }

	FActorSpawnParameters SP;
	SP.ObjectFlags = RF_Transient;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	const FVector ActorOrigin(0.0f, 0.0f, 0.0f);
	ASkeletalMeshActor* TempActor = World->SpawnActor<ASkeletalMeshActor>(ASkeletalMeshActor::StaticClass(), ActorOrigin, FRotator::ZeroRotator, SP);
	if (!TempActor) { OutError = TEXT("Failed to spawn temporary SkeletalMeshActor"); return; }
	USkeletalMeshComponent* SMC = TempActor->GetSkeletalMeshComponent();
	SMC->SetSkeletalMesh(PreviewMesh);
	SMC->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	SMC->PlayAnimation(AnimSeq, false);
	SMC->Stop();
	SMC->SetPosition((float)Time, false);
	SMC->TickAnimation(0.0f, false);
	SMC->RefreshBoneTransforms();

	const FBoxSphereBounds Bounds = SMC->Bounds;
	const FVector Center = Bounds.Origin;
	const float Reach = FMath::Max(Bounds.SphereRadius * (float)CamDistance / 200.0f, (float)CamDistance);
	const FRotator CamRot((float)CamPitch, (float)CamYaw + 180.0f, 0.0f);
	const FVector CamPos = Center - CamRot.Vector() * Reach;

	const FLinearColor BackgroundColor(0.15f, 0.15f, 0.18f, 1.0f);
	UTextureRenderTarget2D* RT = NewObject<UTextureRenderTarget2D>();
	RT->RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA8;
	RT->ClearColor = BackgroundColor;
	RT->bAutoGenerateMips = false;
	RT->InitAutoFormat(Width, Height);
	RT->UpdateResourceImmediate(true);
	UKismetRenderingLibrary::ClearRenderTarget2D(World, RT, BackgroundColor);

	ASceneCapture2D* CaptureActor = World->SpawnActor<ASceneCapture2D>(ASceneCapture2D::StaticClass(), CamPos, CamRot, SP);
	if (!CaptureActor)
	{
		TempActor->Destroy();
		OutError = TEXT("Failed to spawn SceneCapture2D");
		return;
	}
	USceneCaptureComponent2D* SC = CaptureActor->GetCaptureComponent2D();
	SC->TextureTarget = RT;
	SC->CaptureSource = bLit ? ESceneCaptureSource::SCS_FinalColorLDR
	                          : ESceneCaptureSource::SCS_BaseColor;
	SC->bCaptureEveryFrame = false;
	SC->bCaptureOnMovement = false;
	SC->FOVAngle = 45.0f;

	if (bLit)
	{
		FPostProcessSettings& PP = SC->PostProcessSettings;
		PP.bOverride_AutoExposureMethod = true;
		PP.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;
		PP.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
		PP.AutoExposureApplyPhysicalCameraExposure = false;
		PP.bOverride_AutoExposureBias = true;
		PP.AutoExposureBias = 6.0f;
		PP.bOverride_BloomIntensity = true;
		PP.BloomIntensity = 0.15f;
	}

	SC->CaptureScene();

	FString OutPath;
	if (!OutputOverride.IsEmpty())
	{
		OutPath = OutputOverride;
	}
	else
	{
		const FString AssetName = AnimSeq->GetName();
		OutPath = FPaths::ProjectSavedDir() / TEXT("AnimThumbnails") /
			FString::Printf(TEXT("%s_t%.3f.png"), *AssetName, Time);
	}
	FPaths::NormalizeFilename(OutPath);
	const FString Dir = FPaths::GetPath(OutPath);
	const FString FileName = FPaths::GetCleanFilename(OutPath);
	IFileManager::Get().MakeDirectory(*Dir,  true);
	UKismetRenderingLibrary::ExportRenderTarget(World, RT, Dir, FileName);

	CaptureActor->Destroy();
	TempActor->Destroy();

	TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
	Out->SetBoolField(TEXT("success"), true);
	Out->SetStringField(TEXT("anim_sequence_path"), AnimPath);
	Out->SetStringField(TEXT("png_path"), OutPath);
	Out->SetNumberField(TEXT("time"), Time);
	Out->SetNumberField(TEXT("width"), Width);
	Out->SetNumberField(TEXT("height"), Height);
	Out->SetStringField(TEXT("preview_mesh"), PreviewMesh->GetPathName());
	Out->SetStringField(TEXT("capture_source"), bLit ? TEXT("lit") : TEXT("base_color"));
	BuildSuccessJson(Out, OutJsonString);
}

void HandleSetAnimTransformCurveKeysFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }

	FString AnimPath, CurveName, Mode = TEXT("replace");
	if (!Args->TryGetStringField(TEXT("anim_sequence_path"), AnimPath))
		if (!Args->TryGetStringField(TEXT("animation_path"), AnimPath))
			Args->TryGetStringField(TEXT("asset_path"), AnimPath);
	if (!Args->TryGetStringField(TEXT("curve_name"), CurveName))
		Args->TryGetStringField(TEXT("name"), CurveName);
	Args->TryGetStringField(TEXT("mode"), Mode);
	Mode = Mode.ToLower();

	if (AnimPath.IsEmpty()) { OutError = TEXT("Missing required parameter: anim_sequence_path"); return; }
	if (CurveName.IsEmpty()) { OutError = TEXT("Missing required parameter: curve_name"); return; }

	UAnimSequenceBase* AnimSeq = Cast<UAnimSequenceBase>(UEditorAssetLibrary::LoadAsset(AnimPath));
	if (!AnimSeq) { OutError = FString::Printf(TEXT("Could not load animation: %s"), *AnimPath); return; }

	const TArray<TSharedPtr<FJsonValue>>* KeysArray = nullptr;
	if (!Args->TryGetArrayField(TEXT("keys"), KeysArray) || !KeysArray || KeysArray->Num() == 0)
	{
		OutError = TEXT("Missing required parameter: keys=[{time, location?, rotation?, scale?}]");
		return;
	}

	IAnimationDataController& Controller = AnimSeq->GetController();
	FAnimationCurveIdentifier CurveId(FName(*CurveName), ERawCurveTrackTypes::RCT_Transform);

	Controller.OpenBracket(NSLOCTEXT("UECP", "AuthorTransformCurve", "Author Transform Curve"), false);
	if (Mode == TEXT("replace"))
	{
		Controller.RemoveCurve(CurveId, false);
	}
	const bool bAdded = Controller.AddCurve(CurveId,  0x00000004, false);
	(void)bAdded;

	TArray<float>      TimeKeys;        TimeKeys.Reserve(KeysArray->Num());
	TArray<FTransform> TransformValues; TransformValues.Reserve(KeysArray->Num());

	int32 ParsedCount = 0;
	for (const TSharedPtr<FJsonValue>& KV : *KeysArray)
	{
		if (!KV.IsValid() || KV->Type != EJson::Object) continue;
		TSharedPtr<FJsonObject> KO = KV->AsObject();
		double TimeNum = 0.0;
		KO->TryGetNumberField(TEXT("time"), TimeNum);
		FTransform T = FTransform::Identity;
		FVector V; FQuat Q;
		if (ParseVector3(KO, TEXT("location"), V) || ParseVector3(KO, TEXT("position"), V)) T.SetLocation(V);
		const TSharedPtr<FJsonObject>* RotObj = nullptr;
		if (KO->TryGetObjectField(TEXT("rotation"), RotObj) && RotObj && RotObj->IsValid())
		{
			if (ParseRotation(*RotObj, Q)) T.SetRotation(Q);
		}
		if (ParseVector3(KO, TEXT("scale"), V)) T.SetScale3D(V);
		TimeKeys.Add((float)TimeNum);
		TransformValues.Add(T);
		++ParsedCount;
	}

	if (Mode == TEXT("merge"))
	{
		for (int32 i = 0; i < TimeKeys.Num(); ++i)
		{
			Controller.SetTransformCurveKey(CurveId, TimeKeys[i], TransformValues[i], false);
		}
	}
	else
	{
		Controller.SetTransformCurveKeys(CurveId, TransformValues, TimeKeys, false);
	}

	Controller.NotifyPopulated();
	Controller.CloseBracket(false);

	AnimSeq->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(AnimPath, false);

	TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
	Out->SetBoolField(TEXT("success"), true);
	Out->SetStringField(TEXT("anim_sequence_path"), AnimPath);
	Out->SetStringField(TEXT("curve_name"), CurveName);
	Out->SetNumberField(TEXT("keys_written"), ParsedCount);
	Out->SetStringField(TEXT("mode"), Mode);
	BuildSuccessJson(Out, OutJsonString);
}

void HandleAddMontageSection(
	const FString& MontagePath,
	const FString& SectionName,
	float StartTime,
	FString& OutJsonString, FString& OutError)
{

	UAnimMontage* Montage = Cast<UAnimMontage>(UEditorAssetLibrary::LoadAsset(MontagePath));
	if (!Montage)
	{
		SetError(FString::Printf(TEXT("Could not load AnimMontage: %s"), *MontagePath), OutJsonString, OutError);
		return;
	}

	int32 SectionIndex = Montage->AddAnimCompositeSection(FName(*SectionName), StartTime);
	if (SectionIndex == INDEX_NONE)
	{
		const int32 ExistingIdx = Montage->CompositeSections.IndexOfByPredicate([&](const FCompositeSection& S)
		{
			return S.SectionName == FName(*SectionName);
		});
		if (ExistingIdx != INDEX_NONE)
		{
			TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
			Obj->SetBoolField(TEXT("success"), true);
			Obj->SetBoolField(TEXT("existed"), true);
			Obj->SetStringField(TEXT("section_name"), SectionName);
			Obj->SetNumberField(TEXT("section_index"), ExistingIdx);
			Obj->SetNumberField(TEXT("start_time"), Montage->CompositeSections[ExistingIdx].GetTime());
			Obj->SetStringField(TEXT("note"), TEXT("Section already existed at this name — returned as-is. Existing start_time preserved."));
			BuildSuccessJson(Obj, OutJsonString);
			return;
		}
		SetError(FString::Printf(TEXT("Section '%s' could not be added (engine refused — invalid name or out-of-range start time?)"), *SectionName), OutJsonString, OutError);
		return;
	}

	Montage->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(MontagePath, false);

	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("section_name"), SectionName);
	Obj->SetNumberField(TEXT("section_index"), SectionIndex);
	Obj->SetNumberField(TEXT("start_time"), StartTime);
	BuildSuccessJson(Obj, OutJsonString);
}

void HandleLinkMontageSlot(
	const FString& MontagePath,
	const FString& SlotName,
	const FString& AnimationPath,
	FString& OutJsonString, FString& OutError,
	float StartTime)
{

	UAnimMontage* Montage = Cast<UAnimMontage>(UEditorAssetLibrary::LoadAsset(MontagePath));
	if (!Montage)
	{
		SetError(FString::Printf(TEXT("Could not load AnimMontage: %s"), *MontagePath), OutJsonString, OutError);
		return;
	}

	UAnimSequenceBase* AnimSeq = Cast<UAnimSequenceBase>(UEditorAssetLibrary::LoadAsset(AnimationPath));
	if (!AnimSeq)
	{
		SetError(FString::Printf(TEXT("Could not load animation: %s"), *AnimationPath), OutJsonString, OutError);
		return;
	}

	FName SlotFName(*SlotName);
	FSlotAnimationTrack* ExistingTrack = nullptr;
	for (FSlotAnimationTrack& T : Montage->SlotAnimTracks)
	{
		if (T.SlotName == SlotFName)
		{
			ExistingTrack = &T;
			break;
		}
	}
	FSlotAnimationTrack& Track = ExistingTrack ? *ExistingTrack : Montage->AddSlot(SlotFName);

	FAnimSegment NewSegment;
	NewSegment.SetAnimReference(AnimSeq, true);
	NewSegment.AnimStartTime = 0.0f;
	NewSegment.AnimEndTime = AnimSeq->GetPlayLength();
	NewSegment.AnimPlayRate = 1.0f;
	NewSegment.LoopingCount = 1;

	if (StartTime >= 0.0f)
	{
		NewSegment.StartPos = StartTime;
	}
	else
	{
		float AutoStart = 0.0f;
		for (const FAnimSegment& Existing : Track.AnimTrack.AnimSegments)
			AutoStart = FMath::Max(AutoStart, Existing.StartPos + Existing.GetLength());
		NewSegment.StartPos = AutoStart;
	}

	Track.AnimTrack.AnimSegments.Add(NewSegment);

	Montage->UpdateLinkableElements();
	Montage->RefreshCacheData();
	Montage->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(MontagePath, false);

	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("slot_name"), SlotName);
	Obj->SetNumberField(TEXT("start_pos"), NewSegment.StartPos);
	Obj->SetNumberField(TEXT("duration"), NewSegment.GetLength());
	Obj->SetNumberField(TEXT("num_slots"), Montage->SlotAnimTracks.Num());
	BuildSuccessJson(Obj, OutJsonString);
}

void HandleSetMontageSlotName(
	const FString& MontagePath,
	const FString& OldSlotName,
	const FString& NewSlotName,
	FString& OutJsonString, FString& OutError)
{
	UAnimMontage* Montage = Cast<UAnimMontage>(UEditorAssetLibrary::LoadAsset(MontagePath));
	if (!Montage)
	{
		SetError(FString::Printf(TEXT("Could not load AnimMontage: %s"), *MontagePath), OutJsonString, OutError);
		return;
	}
	if (NewSlotName.IsEmpty())
	{
		SetError(TEXT("new_slot_name must not be empty"), OutJsonString, OutError);
		return;
	}

	const FName NewSlotFName(*NewSlotName);
	const FName OldSlotFName = OldSlotName.IsEmpty() ? NAME_None : FName(*OldSlotName);

	int32 RenameIndex = INDEX_NONE;
	if (Montage->SlotAnimTracks.Num() == 0)
	{
		SetError(TEXT("Montage has no slot tracks to rename"), OutJsonString, OutError);
		return;
	}
	if (OldSlotName.IsEmpty())
	{
		if (Montage->SlotAnimTracks.Num() > 1)
		{
			SetError(TEXT("Montage has multiple slot tracks — specify old_slot_name to disambiguate"), OutJsonString, OutError);
			return;
		}
		RenameIndex = 0;
	}
	else
	{
		for (int32 i = 0; i < Montage->SlotAnimTracks.Num(); ++i)
		{
			if (Montage->SlotAnimTracks[i].SlotName == OldSlotFName)
			{
				RenameIndex = i;
				break;
			}
		}
		if (RenameIndex == INDEX_NONE)
		{
			SetError(FString::Printf(TEXT("Slot '%s' not found in montage"), *OldSlotName), OutJsonString, OutError);
			return;
		}
	}

	for (int32 i = 0; i < Montage->SlotAnimTracks.Num(); ++i)
	{
		if (i != RenameIndex && Montage->SlotAnimTracks[i].SlotName == NewSlotFName)
		{
			SetError(FString::Printf(TEXT("A different slot track already uses name '%s' on this montage"), *NewSlotName),
				OutJsonString, OutError);
			return;
		}
	}

	const FName PrevName = Montage->SlotAnimTracks[RenameIndex].SlotName;
	Montage->SlotAnimTracks[RenameIndex].SlotName = NewSlotFName;

	Montage->RefreshCacheData();
	Montage->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(MontagePath, false);

	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("montage"), MontagePath);
	Obj->SetStringField(TEXT("renamed_from"), PrevName.ToString());
	Obj->SetStringField(TEXT("renamed_to"),   NewSlotName);
	Obj->SetNumberField(TEXT("segment_count"), Montage->SlotAnimTracks[RenameIndex].AnimTrack.AnimSegments.Num());
	BuildSuccessJson(Obj, OutJsonString);
}

void HandleAddAnimConduit(
	const FString& AnimBlueprintPath,
	const FString& ConduitName,
	const FString& StateMachineName,
	int32 PosX, int32 PosY,
	FString& OutJsonString, FString& OutError)
{

	UAnimBlueprint* AnimBP = Cast<UAnimBlueprint>(UEditorAssetLibrary::LoadAsset(AnimBlueprintPath));
	if (!AnimBP)
	{
		SetError(FString::Printf(TEXT("Could not load AnimBlueprint: %s"), *AnimBlueprintPath), OutJsonString, OutError);
		return;
	}

	FString SMLookupError;
	UAnimationStateMachineGraph* SMGraph = FindStateMachineGraph(AnimBP, StateMachineName, &SMLookupError);
	if (!SMGraph)
	{
		SetError(SMLookupError, OutJsonString, OutError);
		return;
	}

	UAnimStateConduitNode* ConduitNode = FEdGraphSchemaAction_NewStateNode::SpawnNodeFromTemplate<UAnimStateConduitNode>(
		SMGraph, NewObject<UAnimStateConduitNode>(), FUECPGraphLoc((float)PosX, (float)PosY), false);
	if (!ConduitNode)
	{
		SetError(TEXT("Failed to spawn conduit node."), OutJsonString, OutError);
		return;
	}

	if (ConduitNode->BoundGraph && !ConduitName.IsEmpty())
	{
		TSharedPtr<INameValidatorInterface> NameValidator = FNameValidatorFactory::MakeValidator(ConduitNode);
		FBlueprintEditorUtils::RenameGraphWithSuggestion(ConduitNode->BoundGraph, NameValidator, ConduitName);
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBP);
	SMGraph->NotifyGraphChanged();
	RefreshOpenAnimBlueprintEditor(AnimBP);

	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("conduit_name"), ConduitNode->BoundGraph ? ConduitNode->BoundGraph->GetName() : ConduitName);
	Obj->SetStringField(TEXT("node_guid"), ConduitNode->NodeGuid.ToString());
	CompileAnimBlueprintAndReport(AnimBP, Obj);
	UEditorAssetLibrary::SaveAsset(AnimBlueprintPath, false);
	BuildSuccessJson(Obj, OutJsonString);
}

void HandleCreateAnimComposite(
	const FString& Name,
	const FString& SavePath,
	const FString& SkeletonPath,
	FString& OutJsonString, FString& OutError)
{
	if (Name.IsEmpty()) { SetError(TEXT("name is required"), OutJsonString, OutError); return; }

	FString PackagePath = SavePath.IsEmpty() ? TEXT("/Game") : SavePath;
	while (PackagePath.EndsWith(TEXT("/"))) PackagePath = PackagePath.LeftChop(1);

	UAnimCompositeFactory* Factory = NewObject<UAnimCompositeFactory>();

	if (!SkeletonPath.IsEmpty())
	{
		USkeleton* Skeleton = Cast<USkeleton>(UEditorAssetLibrary::LoadAsset(SkeletonPath));
		if (!Skeleton)
		{
			SetError(FString::Printf(TEXT("Could not load Skeleton: %s"), *SkeletonPath), OutJsonString, OutError);
			return;
		}
		Factory->TargetSkeleton = Skeleton;
	}

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	UObject* Asset = AssetTools.CreateAsset(Name, PackagePath, UAnimComposite::StaticClass(), Factory);
	if (!Asset)
	{
		SetError(TEXT("Failed to create AnimComposite asset."), OutJsonString, OutError);
		return;
	}

	FString AssetPath = FString::Printf(TEXT("%s/%s"), *PackagePath, *Name);
	UEditorAssetLibrary::SaveAsset(AssetPath, false);

	FAssetRegistryModule& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	AssetRegistry.Get().ScanPathsSynchronous({PackagePath}, true);

	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("asset_path"), AssetPath);
	BuildSuccessJson(Obj, OutJsonString);
}

void HandleCreatePoseAsset(
	const FString& Name,
	const FString& SavePath,
	const FString& AnimationPath,
	const FString& SkeletonPath,
	FString& OutJsonString, FString& OutError)
{
	if (Name.IsEmpty()) { SetError(TEXT("name is required"), OutJsonString, OutError); return; }

	FString PackagePath = SavePath.IsEmpty() ? TEXT("/Game") : SavePath;
	while (PackagePath.EndsWith(TEXT("/"))) PackagePath = PackagePath.LeftChop(1);

	UAnimSequence* AnimSeq = nullptr;
	USkeleton*     Skel    = nullptr;
	if (!AnimationPath.IsEmpty())
	{
		AnimSeq = Cast<UAnimSequence>(UEditorAssetLibrary::LoadAsset(AnimationPath));
		if (!AnimSeq)
		{
			SetError(FString::Printf(TEXT("Could not load animation sequence: %s"), *AnimationPath), OutJsonString, OutError);
			return;
		}
	}
	if (!SkeletonPath.IsEmpty())
	{
		UObject* Loaded = UEditorAssetLibrary::LoadAsset(SkeletonPath);
		Skel = Cast<USkeleton>(Loaded);
		if (!Skel)
		{
			if (USkeletalMesh* Mesh = Cast<USkeletalMesh>(Loaded)) Skel = Mesh->GetSkeleton();
		}
		if (!Skel)
		{
			SetError(FString::Printf(TEXT("Could not load skeleton: %s"), *SkeletonPath), OutJsonString, OutError);
			return;
		}
	}
	if (!AnimSeq && !Skel)
	{
		SetError(TEXT("create_pose_asset needs either animation_path (creates pose from sequence frames) or skeleton_path (creates empty pose asset on that skeleton)."),
			OutJsonString, OutError);
		return;
	}

	UObject* Asset = nullptr;
	if (AnimSeq)
	{
		UPoseAssetFactory* Factory = NewObject<UPoseAssetFactory>();
		Factory->SourceAnimation = AnimSeq;
		if (Skel)
		{
			if (FObjectProperty* SkelProp = FindFProperty<FObjectProperty>(Factory->GetClass(), TEXT("TargetSkeleton")))
				SkelProp->SetObjectPropertyValue_InContainer(Factory, Skel);
		}
		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
		Asset = AssetTools.CreateAsset(Name, PackagePath, UPoseAsset::StaticClass(), Factory);
	}
	else
	{
		const FString FullPackageName = PackagePath / Name;
		UPackage* Package = CreatePackage(*FullPackageName);
		if (!Package)
		{
			SetError(FString::Printf(TEXT("Failed to create package at %s"), *FullPackageName), OutJsonString, OutError);
			return;
		}
		UPoseAsset* PoseAsset = NewObject<UPoseAsset>(Package, *Name, RF_Public | RF_Standalone | RF_Transactional);
		if (PoseAsset && Skel) PoseAsset->SetSkeleton(Skel);
		if (PoseAsset)
		{
			FAssetRegistryModule::AssetCreated(PoseAsset);
			PoseAsset->MarkPackageDirty();
		}
		Asset = PoseAsset;
	}
	if (!Asset)
	{
		SetError(TEXT("Failed to create PoseAsset (CreateAsset returned null — common causes: name conflict at this path, or factory rejected the configuration)."),
			OutJsonString, OutError);
		return;
	}

	FString AssetPath = FString::Printf(TEXT("%s/%s"), *PackagePath, *Name);
	UEditorAssetLibrary::SaveAsset(AssetPath, false);

	FAssetRegistryModule& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	AssetRegistry.Get().ScanPathsSynchronous({PackagePath}, true);

	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("asset_path"), AssetPath);
	BuildSuccessJson(Obj, OutJsonString);
}

void HandleCreateAimOffset(
	const FString& Name,
	const FString& SavePath,
	const FString& SkeletonPath,
	bool bIs1D,
	FString& OutJsonString, FString& OutError)
{
	if (Name.IsEmpty()) { SetError(TEXT("name is required"), OutJsonString, OutError); return; }

	FString PackagePath = SavePath.IsEmpty() ? TEXT("/Game") : SavePath;
	while (PackagePath.EndsWith(TEXT("/"))) PackagePath = PackagePath.LeftChop(1);

	USkeleton* Skeleton = nullptr;
	if (!SkeletonPath.IsEmpty())
	{
		Skeleton = Cast<USkeleton>(UEditorAssetLibrary::LoadAsset(SkeletonPath));
		if (!Skeleton)
		{
			SetError(FString::Printf(TEXT("Could not load Skeleton: %s"), *SkeletonPath), OutJsonString, OutError);
			return;
		}
	}

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	UObject* Asset = nullptr;

	if (bIs1D)
	{
		UAimOffsetBlendSpaceFactory1D* Factory = NewObject<UAimOffsetBlendSpaceFactory1D>();
		Factory->TargetSkeleton = Skeleton;
		Asset = AssetTools.CreateAsset(Name, PackagePath, UAimOffsetBlendSpace1D::StaticClass(), Factory);
	}
	else
	{
		UAimOffsetBlendSpaceFactoryNew* Factory = NewObject<UAimOffsetBlendSpaceFactoryNew>();
		Factory->TargetSkeleton = Skeleton;
		Asset = AssetTools.CreateAsset(Name, PackagePath, UAimOffsetBlendSpace::StaticClass(), Factory);
	}

	if (!Asset)
	{
		SetError(TEXT("Failed to create AimOffset asset."), OutJsonString, OutError);
		return;
	}

	FString AssetPath = FString::Printf(TEXT("%s/%s"), *PackagePath, *Name);
	UEditorAssetLibrary::SaveAsset(AssetPath, false);

	FAssetRegistryModule& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	AssetRegistry.Get().ScanPathsSynchronous({PackagePath}, true);

	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("asset_path"), AssetPath);
	Obj->SetStringField(TEXT("type"), bIs1D ? TEXT("AimOffsetBlendSpace1D") : TEXT("AimOffsetBlendSpace"));
	BuildSuccessJson(Obj, OutJsonString);
}

void HandleAddAnimCurve(
	const FString& AnimationPath,
	const FString& CurveName,
	const FString& CurveType,
	FString& OutJsonString, FString& OutError)
{

	UAnimSequenceBase* AnimSeq = Cast<UAnimSequenceBase>(UEditorAssetLibrary::LoadAsset(AnimationPath));
	if (!AnimSeq)
	{
		SetError(FString::Printf(TEXT("Could not load animation: %s"), *AnimationPath), OutJsonString, OutError);
		return;
	}

	ERawCurveTrackTypes TrackType = ERawCurveTrackTypes::RCT_Float;
	if (CurveType.Equals(TEXT("Transform"), ESearchCase::IgnoreCase))
	{
		TrackType = ERawCurveTrackTypes::RCT_Transform;
	}

	FAnimationCurveIdentifier CurveId(FName(*CurveName), TrackType);

	IAnimationDataController& Controller = AnimSeq->GetController();
	Controller.OpenBracket(FText::FromString(TEXT("Add Anim Curve")));
	const bool bAdded = Controller.AddCurve(CurveId);
	Controller.CloseBracket();

	if (!bAdded)
	{
		SetError(FString::Printf(TEXT("Failed to add curve '%s'. It may already exist."), *CurveName), OutJsonString, OutError);
		return;
	}

	AnimSeq->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(AnimationPath, false);

	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("curve_name"), CurveName);
	Obj->SetStringField(TEXT("curve_type"), CurveType.IsEmpty() ? TEXT("Float") : CurveType);
	BuildSuccessJson(Obj, OutJsonString);
}

void HandleCreateIKRig(
	const FString& Name,
	const FString& SavePath,
	const FString& SkeletonPath,
	const FString& RootBone,
	FString& OutJsonString, FString& OutError)
{
	if (Name.IsEmpty()) { SetError(TEXT("name is required"), OutJsonString, OutError); return; }

	FString PackagePath = SavePath.IsEmpty() ? TEXT("/Game") : SavePath;
	while (PackagePath.EndsWith(TEXT("/"))) PackagePath = PackagePath.LeftChop(1);

	UIKRigDefinitionFactory* Factory = NewObject<UIKRigDefinitionFactory>();
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	UObject* Asset = AssetTools.CreateAsset(Name, PackagePath, UIKRigDefinition::StaticClass(), Factory);
	UIKRigDefinition* IKRig = Cast<UIKRigDefinition>(Asset);
	if (!IKRig)
	{
		SetError(TEXT("Failed to create IKRig asset."), OutJsonString, OutError);
		return;
	}

	UIKRigController* Controller = UIKRigController::GetController(IKRig);
	if (Controller)
	{
		if (!SkeletonPath.IsEmpty())
		{
			USkeletalMesh* ResolvedMesh = Cast<USkeletalMesh>(UEditorAssetLibrary::LoadAsset(SkeletonPath));
			if (!ResolvedMesh)
			{
				USkeleton* Skeleton = Cast<USkeleton>(UEditorAssetLibrary::LoadAsset(SkeletonPath));
				if (Skeleton)
				{
					ResolvedMesh = Skeleton->GetPreviewMesh();
					if (!ResolvedMesh)
					{
						FAssetRegistryModule& AR = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
						TArray<FAssetData> MeshAssets;
						AR.Get().GetAssetsByClass(USkeletalMesh::StaticClass()->GetClassPathName(), MeshAssets);
						for (const FAssetData& AD : MeshAssets)
						{
							USkeletalMesh* Candidate = Cast<USkeletalMesh>(AD.GetAsset());
							if (Candidate && Candidate->GetSkeleton() == Skeleton)
							{
								ResolvedMesh = Candidate;
								break;
							}
						}
					}
				}
			}
			if (ResolvedMesh)
				Controller->SetSkeletalMesh(ResolvedMesh);
		}

		if (!RootBone.IsEmpty())
		{
			Controller->SetRetargetRoot(FName(*RootBone));
		}
	}

	FString AssetPath = FString::Printf(TEXT("%s/%s"), *PackagePath, *Name);
	UEditorAssetLibrary::SaveAsset(AssetPath, false);

	FAssetRegistryModule& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	AssetRegistry.Get().ScanPathsSynchronous({PackagePath}, true);

	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("asset_path"), AssetPath);
	BuildSuccessJson(Obj, OutJsonString);
}

void HandleAddIKSolver(
	const FString& IKRigPath,
	const FString& SolverType,
	FString& OutJsonString, FString& OutError)
{

	UIKRigDefinition* IKRig = Cast<UIKRigDefinition>(UEditorAssetLibrary::LoadAsset(IKRigPath));
	if (!IKRig)
	{
		SetError(FString::Printf(TEXT("Could not load IKRig: %s"), *IKRigPath), OutJsonString, OutError);
		return;
	}

	UIKRigController* Controller = UIKRigController::GetController(IKRig);
	if (!Controller)
	{
		SetError(TEXT("Could not get IKRig controller."), OutJsonString, OutError);
		return;
	}

#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6)
	FString SolverTypeStr;
	if (SolverType.Equals(TEXT("FBIK"), ESearchCase::IgnoreCase))
		SolverTypeStr = TEXT("FIKRigFullBodyIKSolver");
	else if (SolverType.Equals(TEXT("Limb"), ESearchCase::IgnoreCase))
		SolverTypeStr = TEXT("FIKRigLimbSolver");
	else if (SolverType.Equals(TEXT("BodyMover"), ESearchCase::IgnoreCase))
		SolverTypeStr = TEXT("FIKRigBodyMoverSolver");
	else if (SolverType.Equals(TEXT("SetTransform"), ESearchCase::IgnoreCase))
		SolverTypeStr = TEXT("FIKRigSetTransform");
	else
		SolverTypeStr = TEXT("FIKRigFullBodyIKSolver");

	int32 SolverIndex = Controller->AddSolver(SolverTypeStr);
#else
	TSubclassOf<UIKRigSolver> SolverClass;
	if (SolverType.Equals(TEXT("FBIK"), ESearchCase::IgnoreCase))
		SolverClass = UIKRigFBIKSolver::StaticClass();
	else if (SolverType.Equals(TEXT("Limb"), ESearchCase::IgnoreCase))
		SolverClass = UIKRig_LimbSolver::StaticClass();
	else if (SolverType.Equals(TEXT("BodyMover"), ESearchCase::IgnoreCase))
		SolverClass = UIKRig_BodyMover::StaticClass();
	else if (SolverType.Equals(TEXT("SetTransform"), ESearchCase::IgnoreCase))
		SolverClass = UIKRig_SetTransform::StaticClass();
	else
		SolverClass = UIKRigFBIKSolver::StaticClass();

	int32 SolverIndex = Controller->AddSolver(SolverClass);
#endif

	IKRig->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(IKRigPath, false);

	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetNumberField(TEXT("solver_index"), SolverIndex);
	Obj->SetStringField(TEXT("solver_type"), SolverType);
	BuildSuccessJson(Obj, OutJsonString);
}

void HandleAddIKGoal(
	const FString& IKRigPath,
	const FString& GoalName,
	const FString& BoneName,
	int32 SolverIndex,
	FString& OutJsonString, FString& OutError)
{

	UIKRigDefinition* IKRig = Cast<UIKRigDefinition>(UEditorAssetLibrary::LoadAsset(IKRigPath));
	if (!IKRig)
	{
		SetError(FString::Printf(TEXT("Could not load IKRig: %s"), *IKRigPath), OutJsonString, OutError);
		return;
	}

	UIKRigController* Controller = UIKRigController::GetController(IKRig);
	if (!Controller)
	{
		SetError(TEXT("Could not get IKRig controller."), OutJsonString, OutError);
		return;
	}

	FName ResultGoalName = Controller->AddNewGoal(FName(*GoalName), FName(*BoneName));
	if (ResultGoalName == NAME_None)
	{
		SetError(FString::Printf(TEXT("Failed to add goal '%s' to bone '%s'."), *GoalName, *BoneName), OutJsonString, OutError);
		return;
	}

	if (SolverIndex >= 0)
	{
		Controller->ConnectGoalToSolver(ResultGoalName, SolverIndex);
	}

	IKRig->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(IKRigPath, false);

	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("goal_name"), ResultGoalName.ToString());
	Obj->SetStringField(TEXT("bone_name"), BoneName);
	BuildSuccessJson(Obj, OutJsonString);
}

void HandleAddRetargetChain(
	const FString& IKRigPath,
	const FString& ChainName,
	const FString& StartBone,
	const FString& EndBone,
	const FString& GoalName,
	FString& OutJsonString, FString& OutError)
{

	UIKRigDefinition* IKRig = Cast<UIKRigDefinition>(UEditorAssetLibrary::LoadAsset(IKRigPath));
	if (!IKRig)
	{
		SetError(FString::Printf(TEXT("Could not load IKRig: %s"), *IKRigPath), OutJsonString, OutError);
		return;
	}

	UIKRigController* Controller = UIKRigController::GetController(IKRig);
	if (!Controller)
	{
		SetError(TEXT("Could not get IKRig controller."), OutJsonString, OutError);
		return;
	}

	FName ResultChainName = Controller->AddRetargetChain(
		FName(*ChainName), FName(*StartBone), FName(*EndBone),
		GoalName.IsEmpty() ? NAME_None : FName(*GoalName));

	if (ResultChainName == NAME_None)
	{
		SetError(FString::Printf(TEXT("Failed to add retarget chain '%s'."), *ChainName), OutJsonString, OutError);
		return;
	}

	IKRig->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(IKRigPath, false);

	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("chain_name"), ResultChainName.ToString());
	Obj->SetStringField(TEXT("start_bone"), StartBone);
	Obj->SetStringField(TEXT("end_bone"), EndBone);
	BuildSuccessJson(Obj, OutJsonString);
}

void HandleCreateIKRetargeter(
	const FString& Name,
	const FString& SavePath,
	const FString& SourceIKRigPath,
	const FString& TargetIKRigPath,
	FString& OutJsonString, FString& OutError)
{
	if (Name.IsEmpty()) { SetError(TEXT("name is required"), OutJsonString, OutError); return; }

	FString PackagePath = SavePath.IsEmpty() ? TEXT("/Game") : SavePath;
	while (PackagePath.EndsWith(TEXT("/"))) PackagePath = PackagePath.LeftChop(1);

	UIKRetargetFactory* Factory = NewObject<UIKRetargetFactory>();
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	UObject* Asset = AssetTools.CreateAsset(Name, PackagePath, UIKRetargeter::StaticClass(), Factory);
	UIKRetargeter* Retargeter = Cast<UIKRetargeter>(Asset);
	if (!Retargeter)
	{
		SetError(TEXT("Failed to create IKRetargeter asset."), OutJsonString, OutError);
		return;
	}

	UIKRetargeterController* Controller = UIKRetargeterController::GetController(Retargeter);
	if (Controller)
	{
		if (!SourceIKRigPath.IsEmpty())
		{
			UIKRigDefinition* SourceRig = Cast<UIKRigDefinition>(UEditorAssetLibrary::LoadAsset(SourceIKRigPath));
			if (SourceRig)
				Controller->SetIKRig(ERetargetSourceOrTarget::Source, SourceRig);
		}

		if (!TargetIKRigPath.IsEmpty())
		{
			UIKRigDefinition* TargetRig = Cast<UIKRigDefinition>(UEditorAssetLibrary::LoadAsset(TargetIKRigPath));
			if (TargetRig)
				Controller->SetIKRig(ERetargetSourceOrTarget::Target, TargetRig);
		}
	}

	FString AssetPath = FString::Printf(TEXT("%s/%s"), *PackagePath, *Name);
	UEditorAssetLibrary::SaveAsset(AssetPath, false);

	FAssetRegistryModule& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	AssetRegistry.Get().ScanPathsSynchronous({PackagePath}, true);

	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("asset_path"), AssetPath);
	BuildSuccessJson(Obj, OutJsonString);
}

void HandleMapRetargetChain(
	const FString& RetargeterPath,
	const FString& SourceChain,
	const FString& TargetChain,
	bool bAutoMap,
	FString& OutJsonString, FString& OutError)
{

	UIKRetargeter* Retargeter = Cast<UIKRetargeter>(UEditorAssetLibrary::LoadAsset(RetargeterPath));
	if (!Retargeter)
	{
		SetError(FString::Printf(TEXT("Could not load IKRetargeter: %s"), *RetargeterPath), OutJsonString, OutError);
		return;
	}

	UIKRetargeterController* Controller = UIKRetargeterController::GetController(Retargeter);
	if (!Controller)
	{
		SetError(TEXT("Could not get IKRetargeter controller."), OutJsonString, OutError);
		return;
	}

	if (bAutoMap)
	{
		Controller->AutoMapChains(EAutoMapChainType::Fuzzy, false);
	}
	else if (!SourceChain.IsEmpty() && !TargetChain.IsEmpty())
	{
		const bool bMapped = Controller->SetSourceChain(FName(*SourceChain), FName(*TargetChain));
		if (!bMapped)
		{
			SetError(FString::Printf(TEXT("Failed to map '%s' -> '%s'. Check that chain names exist."), *SourceChain, *TargetChain), OutJsonString, OutError);
			return;
		}
	}
	else
	{
		SetError(TEXT("map_retarget_chain did nothing: pass auto_map=true, or provide both source_chain and target_chain."), OutJsonString, OutError);
		return;
	}

	Retargeter->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(RetargeterPath, false);

	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetBoolField(TEXT("auto_mapped"), bAutoMap);
	if (!bAutoMap)
	{
		Obj->SetStringField(TEXT("source_chain"), SourceChain);
		Obj->SetStringField(TEXT("target_chain"), TargetChain);
	}
	BuildSuccessJson(Obj, OutJsonString);
}

void HandleAddBlendspaceSample(
	const FString& BlendspacePath,
	const FString& AnimationPath,
	float SampleX,
	float SampleY,
	float SampleZ,
	FString& OutJsonString, FString& OutError)
{

	UBlendSpace* BlendSpace = Cast<UBlendSpace>(UEditorAssetLibrary::LoadAsset(BlendspacePath));
	if (!BlendSpace)
	{
		SetError(FString::Printf(TEXT("Could not load BlendSpace: %s"), *BlendspacePath), OutJsonString, OutError);
		return;
	}

	UAnimSequence* AnimSeq = Cast<UAnimSequence>(UEditorAssetLibrary::LoadAsset(AnimationPath));
	if (!AnimSeq)
	{
		SetError(FString::Printf(TEXT("Could not load AnimSequence: %s"), *AnimationPath), OutJsonString, OutError);
		return;
	}

	// A sample bound to a different skeleton is silently flagged invalid by ValidateSampleData
	// and never blends — reject it up front instead.
	if (BlendSpace->GetSkeleton() && AnimSeq->GetSkeleton() && BlendSpace->GetSkeleton() != AnimSeq->GetSkeleton())
	{
		SetError(FString::Printf(TEXT("Skeleton mismatch: BlendSpace uses '%s' but '%s' uses '%s'."),
			*BlendSpace->GetSkeleton()->GetPathName(), *AnimationPath, *AnimSeq->GetSkeleton()->GetPathName()), OutJsonString, OutError);
		return;
	}

	// Samples outside the axis ranges are clamped/ignored by the blend space; error instead.
	const bool bIs1D = BlendSpace->IsA<UBlendSpace1D>();
	const FVector SampleValue(SampleX, SampleY, SampleZ);
	const int32 AxesToCheck = bIs1D ? 1 : 2;
	for (int32 Axis = 0; Axis < AxesToCheck; ++Axis)
	{
		const FBlendParameter& Param = BlendSpace->GetBlendParameter(Axis);
		const float V = (float)SampleValue[Axis];
		if (V < Param.Min - KINDA_SMALL_NUMBER || V > Param.Max + KINDA_SMALL_NUMBER)
		{
			SetError(FString::Printf(TEXT("sample_%s=%.3f is outside axis '%s' range [%.3f, %.3f]. Adjust the value or widen the axis with set_blendspace_axis."),
				Axis == 0 ? TEXT("x") : TEXT("y"), V, *Param.DisplayName, Param.Min, Param.Max), OutJsonString, OutError);
			return;
		}
	}

	struct FBlendSpaceAccessor : public UBlendSpace
	{
		static TArray<FBlendSample>& GetSamples(UBlendSpace* BS)
		{
			return static_cast<FBlendSpaceAccessor*>(BS)->SampleData;
		}
	};
	BlendSpace->Modify();
	FBlendSample NewSample;
	NewSample.Animation = AnimSeq;
	NewSample.SampleValue = SampleValue;
	const int32 NewIndex = FBlendSpaceAccessor::GetSamples(BlendSpace).Add(NewSample);
	BlendSpace->ValidateSampleData();
	BlendSpace->PostEditChange();
	BlendSpace->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(BlendspacePath, false);

	const TArray<FBlendSample>& Samples = BlendSpace->GetBlendSamples();
	const bool bIsValid = Samples.IsValidIndex(NewIndex) ? (Samples[NewIndex].bIsValid != 0) : false;

	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetNumberField(TEXT("sample_index"), NewIndex);
	Obj->SetNumberField(TEXT("sample_x"), SampleX);
	Obj->SetNumberField(TEXT("sample_y"), SampleY);
	Obj->SetNumberField(TEXT("sample_z"), SampleZ);
	Obj->SetBoolField(TEXT("is_valid"), bIsValid);
	if (!bIsValid)
		Obj->SetStringField(TEXT("warning"), TEXT("ValidateSampleData flagged this sample invalid (duplicate coordinate or incompatible animation); it will not contribute to blending."));
	Obj->SetNumberField(TEXT("total_samples"), Samples.Num());
	BuildSuccessJson(Obj, OutJsonString);
}

void HandleSetAnimSequenceSettings(
	const FString& AnimationPath,
	float RateScale,
	bool bSetRateScale,
	bool bEnableRootMotion,
	bool bSetRootMotion,
	const FString& RootMotionRootLock,
	bool bLoop, bool bSetLoop,
	FString& OutJsonString, FString& OutError)
{

	UAnimSequence* AnimSeq = Cast<UAnimSequence>(UEditorAssetLibrary::LoadAsset(AnimationPath));
	if (!AnimSeq)
	{
		SetError(FString::Printf(TEXT("Could not load AnimSequence: %s"), *AnimationPath), OutJsonString, OutError);
		return;
	}

	if (bSetRateScale)
		AnimSeq->RateScale = RateScale;

	if (bSetRootMotion)
		AnimSeq->bEnableRootMotion = bEnableRootMotion;

	if (bSetLoop)
		AnimSeq->bLoop = bLoop;

	if (!RootMotionRootLock.IsEmpty())
	{
		if (RootMotionRootLock.Equals(TEXT("AnimFirstFrame"), ESearchCase::IgnoreCase))
			AnimSeq->RootMotionRootLock = ERootMotionRootLock::AnimFirstFrame;
		else if (RootMotionRootLock.Equals(TEXT("Zero"), ESearchCase::IgnoreCase))
			AnimSeq->RootMotionRootLock = ERootMotionRootLock::Zero;
		else
			AnimSeq->RootMotionRootLock = ERootMotionRootLock::RefPose;
	}

	AnimSeq->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(AnimationPath, false);

	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetNumberField(TEXT("rate_scale"), AnimSeq->RateScale);
	Obj->SetBoolField(TEXT("enable_root_motion"), AnimSeq->bEnableRootMotion);
	BuildSuccessJson(Obj, OutJsonString);
}

void HandleSetMontageBlendSettings(
	const FString& MontagePath,
	float BlendInTime,
	bool bSetBlendIn,
	float BlendOutTime,
	bool bSetBlendOut,
	float RateScale,
	bool bSetRateScale,
	bool bLoop,
	bool bSetLoop,
	FString& OutJsonString, FString& OutError)
{

	UAnimMontage* Montage = Cast<UAnimMontage>(UEditorAssetLibrary::LoadAsset(MontagePath));
	if (!Montage)
	{
		SetError(FString::Printf(TEXT("Could not load AnimMontage: %s"), *MontagePath), OutJsonString, OutError);
		return;
	}

	if (bSetBlendIn)
		Montage->BlendIn.SetBlendTime(BlendInTime);

	if (bSetBlendOut)
		Montage->BlendOut.SetBlendTime(BlendOutTime);

	if (bSetRateScale)
		Montage->RateScale = RateScale;

	if (bSetLoop)
		Montage->bLoop = bLoop;

	Montage->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(MontagePath, false);

	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetNumberField(TEXT("blend_in_time"), Montage->BlendIn.GetBlendTime());
	Obj->SetNumberField(TEXT("blend_out_time"), Montage->BlendOut.GetBlendTime());
	Obj->SetNumberField(TEXT("rate_scale"), Montage->RateScale);
	Obj->SetBoolField(TEXT("loop"), Montage->bLoop);
	BuildSuccessJson(Obj, OutJsonString);
}

void HandleAddAnimNotifyState(
	const FString& AnimationPath,
	const FString& NotifyStateName,
	float StartTime,
	float Duration,
	const FString& NotifyStateClass,
	FString& OutJsonString, FString& OutError,
	int32 TrackIndex,
	float StartTimeNormalized)
{

	UAnimSequenceBase* AnimSeq = Cast<UAnimSequenceBase>(UEditorAssetLibrary::LoadAsset(AnimationPath));
	if (!AnimSeq)
	{
		SetError(FString::Printf(TEXT("Could not load animation: %s"), *AnimationPath), OutJsonString, OutError);
		return;
	}

	float Start = 0.f;
	FString TimeError;
	if (!ResolveNotifyTime(AnimSeq, StartTime, StartTimeNormalized, Start, TimeError))
	{
		SetError(TimeError, OutJsonString, OutError);
		return;
	}
	const float PlayLength = AnimSeq->GetPlayLength();
	float ClampedDuration = FMath::Max(Duration, 1.f / 30.f);
	bool bDurationClamped = false;
	if (Start + ClampedDuration > PlayLength + KINDA_SMALL_NUMBER)
	{
		ClampedDuration = FMath::Max(PlayLength - Start, 1.f / 30.f);
		bDurationClamped = true;
	}

	UClass* Class = nullptr;
	if (!NotifyStateClass.IsEmpty())
	{
		FString ClassError;
		Class = ResolveNotifyClass(NotifyStateClass, UAnimNotifyState::StaticClass(), ClassError);
		if (!Class)
		{
			SetError(ClassError, OutJsonString, OutError);
			return;
		}
	}

	AnimSeq->Modify();
	FAnimNotifyEvent& NewNotify = AnimSeq->Notifies.AddDefaulted_GetRef();
	NewNotify.NotifyName = FName(*NotifyStateName);
	NewNotify.Guid = FGuid::NewGuid();
	NewNotify.Link(AnimSeq, Start);
	NewNotify.TriggerTimeOffset = GetTriggerTimeOffsetForType(AnimSeq->CalculateOffsetForNotify(Start));
	NewNotify.TrackIndex = FMath::Max(0, TrackIndex);

	if (Class)
	{
		NewNotify.NotifyStateClass = NewObject<UAnimNotifyState>(AnimSeq, Class, NAME_None, RF_Transactional);
		NewNotify.NotifyName = FName(*NewNotify.NotifyStateClass->GetNotifyName());
		NewNotify.TriggerWeightThreshold = NewNotify.NotifyStateClass->GetDefaultTriggerWeightThreshold();
	}

	// Mirror the editor: SetDuration then link the end element so it tracks montage segments.
	NewNotify.SetDuration(ClampedDuration);
	NewNotify.EndLink.Link(AnimSeq, NewNotify.EndLink.GetTime());
	NewNotify.EndTriggerTimeOffset = GetTriggerTimeOffsetForType(AnimSeq->CalculateOffsetForNotify(NewNotify.GetEndTriggerTime()));

	const FString StoredName = NewNotify.NotifyName.ToString();
	const int32 StoredTrack = NewNotify.TrackIndex;
	const float StoredStart = NewNotify.GetTime();
	const float StoredDuration = NewNotify.GetDuration();

	AnimSeq->RefreshCacheData();
	AnimSeq->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(AnimationPath, false);

	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("notify_name"), StoredName);
	Obj->SetStringField(TEXT("requested_name"), NotifyStateName);
	Obj->SetStringField(TEXT("notify_state_class"), Class ? Class->GetPathName() : FString());
	Obj->SetNumberField(TEXT("start_time"), StoredStart);
	Obj->SetNumberField(TEXT("duration"), StoredDuration);
	Obj->SetNumberField(TEXT("play_length"), PlayLength);
	Obj->SetNumberField(TEXT("track_index"), StoredTrack);
	if (bDurationClamped)
		Obj->SetStringField(TEXT("warning"), TEXT("duration was clamped so the notify state ends within the clip length."));
	BuildSuccessJson(Obj, OutJsonString);
}

void HandleSetTransitionBlendSettings(
	const FString& AnimBlueprintPath,
	const FString& StateMachineName,
	const FString& FromState,
	const FString& ToState,
	float CrossfadeDuration,
	bool bSetDuration,
	const FString& BlendMode,
	FString& OutJsonString, FString& OutError)
{

	UAnimBlueprint* AnimBP = Cast<UAnimBlueprint>(UEditorAssetLibrary::LoadAsset(AnimBlueprintPath));
	if (!AnimBP)
	{
		SetError(FString::Printf(TEXT("Could not load AnimBlueprint: %s"), *AnimBlueprintPath), OutJsonString, OutError);
		return;
	}

	FString SMLookupError;
	UAnimationStateMachineGraph* SMGraph = FindStateMachineGraph(AnimBP, StateMachineName, &SMLookupError);
	if (!SMGraph)
	{
		SetError(SMLookupError, OutJsonString, OutError);
		return;
	}

	UAnimStateTransitionNode* TransNode = nullptr;
	for (UEdGraphNode* Node : SMGraph->Nodes)
	{
		UAnimStateTransitionNode* T = Cast<UAnimStateTransitionNode>(Node);
		if (!T) continue;
		UAnimStateNodeBase* Prev = T->GetPreviousState();
		UAnimStateNodeBase* Next = T->GetNextState();
		if (Prev && Next && Prev->GetStateName() == FromState && Next->GetStateName() == ToState)
		{
			TransNode = T;
			break;
		}
	}
	if (!TransNode)
	{
		SetError(FString::Printf(TEXT("No transition found from '%s' to '%s' (transitions are directional)."), *FromState, *ToState), OutJsonString, OutError);
		return;
	}

	TransNode->Modify();
	if (bSetDuration)
		TransNode->CrossfadeDuration = CrossfadeDuration;

	if (!BlendMode.IsEmpty())
	{
		if (BlendMode.Equals(TEXT("Cubic"), ESearchCase::IgnoreCase))
			TransNode->BlendMode = EAlphaBlendOption::Cubic;
		else if (BlendMode.Equals(TEXT("HermiteCubic"), ESearchCase::IgnoreCase))
			TransNode->BlendMode = EAlphaBlendOption::HermiteCubic;
		else if (BlendMode.Equals(TEXT("Sinusoidal"), ESearchCase::IgnoreCase))
			TransNode->BlendMode = EAlphaBlendOption::Sinusoidal;
		else if (BlendMode.Equals(TEXT("ExpIn"), ESearchCase::IgnoreCase))
			TransNode->BlendMode = EAlphaBlendOption::ExpIn;
		else if (BlendMode.Equals(TEXT("ExpOut"), ESearchCase::IgnoreCase))
			TransNode->BlendMode = EAlphaBlendOption::ExpOut;
		else
			TransNode->BlendMode = EAlphaBlendOption::Linear;
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBP);
	SMGraph->NotifyGraphChanged();

	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetNumberField(TEXT("crossfade_duration"), TransNode->CrossfadeDuration);
	Obj->SetStringField(TEXT("transition_guid"), TransNode->NodeGuid.ToString());
	CompileAnimBlueprintAndReport(AnimBP, Obj);
	UEditorAssetLibrary::SaveAsset(AnimBlueprintPath, false);
	BuildSuccessJson(Obj, OutJsonString);
}

void HandleSetAnimNotifyProperty(const FString& AssetPath, const FString& NotifyName,
	float TimePosition, const FString& PropertyName, const FString& PropertyValue,
	FString& OutJson, FString& OutError)
{

	UAnimSequenceBase* Seq = LoadObject<UAnimSequenceBase>(nullptr, *AssetPath);
	if (!Seq)
	{
		OutError = FString::Printf(TEXT("AnimSequenceBase not found at '%s'"), *AssetPath);
		return;
	}

	FAnimNotifyEvent* FoundEvent = nullptr;
	const bool bUseName = !NotifyName.IsEmpty();
	if (bUseName)
	{
		for (FAnimNotifyEvent& Event : Seq->Notifies)
		{
			if (Event.NotifyName.ToString().Equals(NotifyName, ESearchCase::IgnoreCase))
			{
				FoundEvent = &Event;
				break;
			}
		}
		if (!FoundEvent)
		{
			TArray<FString> AvailableNames;
			for (const FAnimNotifyEvent& Event : Seq->Notifies)
				AvailableNames.Add(Event.NotifyName.ToString());
			OutError = FString::Printf(TEXT("No notify named '%s' on '%s'. Available: %s"),
				*NotifyName, *AssetPath,
				AvailableNames.Num() > 0 ? *FString::Join(AvailableNames, TEXT(", ")) : TEXT("(none)"));
			return;
		}
	}
	else
	{
		float BestDist = MAX_FLT;
		for (FAnimNotifyEvent& Event : Seq->Notifies)
		{
			const float Dist = FMath::Abs(Event.GetTime() - TimePosition);
			if (Dist < BestDist) { BestDist = Dist; FoundEvent = &Event; }
		}
	}

	if (!FoundEvent)
	{
		OutError = FString::Printf(TEXT("No notifies found on '%s'"), *AssetPath);
		return;
	}

	UObject* NotifyObj = FoundEvent->Notify
		? static_cast<UObject*>(FoundEvent->Notify)
		: static_cast<UObject*>(FoundEvent->NotifyStateClass);

	if (!NotifyObj)
	{
		OutError = FString::Printf(
			TEXT("Notify '%s' has no Notify/NotifyStateClass object (pure name notify — no properties to set)"),
			*FoundEvent->NotifyName.ToString());
		return;
	}

	FProperty* Prop = FindFProperty<FProperty>(NotifyObj->GetClass(), *PropertyName);
	if (!Prop)
	{
		TArray<FString> Names;
		for (TFieldIterator<FProperty> It(NotifyObj->GetClass()); It; ++It)
			Names.Add(It->GetName());
		OutError = FString::Printf(TEXT("Property '%s' not found on '%s'. Available: %s"),
			*PropertyName, *NotifyObj->GetClass()->GetName(), *FString::Join(Names, TEXT(", ")));
		return;
	}

	void* PropPtr = Prop->ContainerPtrToValuePtr<void>(NotifyObj);

	if (FObjectPropertyBase* ObjProp = CastField<FObjectPropertyBase>(Prop))
	{
		UObject* Asset = PropertyValue.IsEmpty() ? nullptr : LoadObject<UObject>(nullptr, *PropertyValue);
		if (!Asset && !PropertyValue.IsEmpty())
		{
			OutError = FString::Printf(TEXT("Could not load asset at '%s'"), *PropertyValue);
			return;
		}
		ObjProp->SetObjectPropertyValue(PropPtr, Asset);
	}
	else if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Prop))
	{
		BoolProp->SetPropertyValue(PropPtr,
			PropertyValue.Equals(TEXT("true"), ESearchCase::IgnoreCase) || PropertyValue == TEXT("1"));
	}
	else
	{
		const TCHAR* ImportResult = Prop->ImportText_Direct(*PropertyValue, PropPtr, NotifyObj, PPF_None, nullptr);
		if (!ImportResult)
		{
			OutError = FString::Printf(TEXT("Failed to set '%s': import failed for value '%s'"), *PropertyName, *PropertyValue);
			return;
		}
	}

	Seq->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(AssetPath, false);

	OutJson = FString::Printf(
		TEXT("{\"success\":true,\"notify_name\":\"%s\",\"notify_class\":\"%s\",\"property\":\"%s\",\"asset_path\":\"%s\"}"),
		*FoundEvent->NotifyName.ToString(), *NotifyObj->GetClass()->GetName(),
		*PropertyName, *AssetPath);
}

void HandleGetAnimBpSummary(const FString& AnimBPPath, FString& OutJson, FString& OutError)
{

	UAnimBlueprint* AnimBP = Cast<UAnimBlueprint>(UEditorAssetLibrary::LoadAsset(AnimBPPath));
	if (!AnimBP)
	{
		SetError(FString::Printf(TEXT("Could not load AnimBlueprint: %s"), *AnimBPPath), OutJson, OutError);
		return;
	}

	TSharedPtr<FJsonObject> Root = MakeShareable(new FJsonObject);
	Root->SetBoolField(TEXT("success"), true);
	Root->SetStringField(TEXT("asset_path"), AnimBPPath);

	if (AnimBP->TargetSkeleton)
		Root->SetStringField(TEXT("skeleton"), AnimBP->TargetSkeleton->GetPathName());

	{
		const TCHAR* StatusStr = TEXT("Unknown");
		switch (AnimBP->Status)
		{
			case BS_Dirty:                 StatusStr = TEXT("Dirty"); break;
			case BS_Error:                 StatusStr = TEXT("Error"); break;
			case BS_UpToDate:              StatusStr = TEXT("UpToDate"); break;
			case BS_BeingCreated:          StatusStr = TEXT("BeingCreated"); break;
			case BS_UpToDateWithWarnings:  StatusStr = TEXT("UpToDateWithWarnings"); break;
			default: break;
		}
		Root->SetStringField(TEXT("compile_status"), StatusStr);
		Root->SetBoolField(TEXT("has_generated_class"), AnimBP->GeneratedClass != nullptr);
	}

	TArray<TSharedPtr<FJsonValue>> VarsArr;
	for (const FBPVariableDescription& Var : AnimBP->NewVariables)
	{
		TSharedPtr<FJsonObject> V = MakeShareable(new FJsonObject);
		V->SetStringField(TEXT("name"), Var.VarName.ToString());
		V->SetStringField(TEXT("type"), Var.VarType.PinCategory.ToString());
		V->SetStringField(TEXT("sub_type"), Var.VarType.PinSubCategoryObject.IsValid()
			? Var.VarType.PinSubCategoryObject->GetName() : TEXT(""));
		VarsArr.Add(MakeShareable(new FJsonValueObject(V)));
	}
	Root->SetArrayField(TEXT("variables"), VarsArr);

	TArray<TSharedPtr<FJsonValue>> InterfacesArr;
	for (const FBPInterfaceDescription& Iface : AnimBP->ImplementedInterfaces)
	{
		if (Iface.Interface)
			InterfacesArr.Add(MakeShareable(new FJsonValueString(Iface.Interface->GetPathName())));
	}
	Root->SetArrayField(TEXT("implemented_interfaces"), InterfacesArr);

	TArray<TSharedPtr<FJsonValue>> SMArr;
	UAnimationGraph* AnimGraph = FindAnimGraph(AnimBP);
	if (AnimGraph)
	{
		for (UEdGraph* SubGraph : AnimGraph->SubGraphs)
		{
			UAnimationStateMachineGraph* SMGraph = Cast<UAnimationStateMachineGraph>(SubGraph);
			if (!SMGraph) continue;

			TSharedPtr<FJsonObject> SM = MakeShareable(new FJsonObject);
			SM->SetStringField(TEXT("name"), SMGraph->GetName());

			TArray<TSharedPtr<FJsonValue>> StatesArr;
			TArray<UAnimStateNode*> States;
			SMGraph->GetNodesOfClass<UAnimStateNode>(States);
			for (UAnimStateNode* S : States)
			{
				TSharedPtr<FJsonObject> SObj = MakeShareable(new FJsonObject);
				SObj->SetStringField(TEXT("name"), S->GetStateName());
				SObj->SetNumberField(TEXT("pos_x"), S->NodePosX);
				SObj->SetNumberField(TEXT("pos_y"), S->NodePosY);
				StatesArr.Add(MakeShareable(new FJsonValueObject(SObj)));
			}
			SM->SetArrayField(TEXT("states"), StatesArr);

			TArray<TSharedPtr<FJsonValue>> ConduitsArr;
			TArray<UAnimStateConduitNode*> Conduits;
			SMGraph->GetNodesOfClass<UAnimStateConduitNode>(Conduits);
			for (UAnimStateConduitNode* C : Conduits)
			{
				TSharedPtr<FJsonObject> CObj = MakeShareable(new FJsonObject);
				CObj->SetStringField(TEXT("name"), C->GetStateName());
				CObj->SetNumberField(TEXT("pos_x"), C->NodePosX);
				CObj->SetNumberField(TEXT("pos_y"), C->NodePosY);
				ConduitsArr.Add(MakeShareable(new FJsonValueObject(CObj)));
			}
			SM->SetArrayField(TEXT("conduits"), ConduitsArr);

			TArray<TSharedPtr<FJsonValue>> TransArr;
			for (UEdGraphNode* Node : SMGraph->Nodes)
			{
				UAnimStateTransitionNode* T = Cast<UAnimStateTransitionNode>(Node);
				if (!T) continue;
				UAnimStateNodeBase* Prev = T->GetPreviousState();
				UAnimStateNodeBase* Next = T->GetNextState();
				if (!Prev || !Next) continue;
				TSharedPtr<FJsonObject> TObj = MakeShareable(new FJsonObject);
				TObj->SetStringField(TEXT("from"), Prev->GetStateName());
				TObj->SetStringField(TEXT("to"), Next->GetStateName());
				TObj->SetNumberField(TEXT("crossfade_duration"), T->CrossfadeDuration);
				TransArr.Add(MakeShareable(new FJsonValueObject(TObj)));
			}
			SM->SetArrayField(TEXT("transitions"), TransArr);

			SMArr.Add(MakeShareable(new FJsonValueObject(SM)));
		}
	}
	Root->SetArrayField(TEXT("state_machines"), SMArr);

	BuildSuccessJson(Root, OutJson);
}

static TSharedPtr<FJsonObject> SerializeAnimNodeJson(UEdGraphNode* Node, UEdGraph* OwningGraph)
{
	TSharedPtr<FJsonObject> N = MakeShared<FJsonObject>();
	if (!Node) return N;
	N->SetStringField(TEXT("node_class"), Node->GetClass()->GetName());
	N->SetStringField(TEXT("node_title"), Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
	N->SetStringField(TEXT("node_guid"),  Node->NodeGuid.ToString());
	if (OwningGraph) N->SetStringField(TEXT("graph_path"), OwningGraph->GetPathName());
	N->SetNumberField(TEXT("pos_x"), Node->NodePosX);
	N->SetNumberField(TEXT("pos_y"), Node->NodePosY);

	if (UAnimGraphNode_AssetPlayerBase* AP = Cast<UAnimGraphNode_AssetPlayerBase>(Node))
	{
		if (UAnimationAsset* Asset = AP->GetAnimationAsset())
		{
			N->SetStringField(TEXT("current_asset"), Asset->GetPathName());
			N->SetStringField(TEXT("asset_class"),   Asset->GetClass()->GetName());
			if (Cast<UBlendSpace>(Asset))
			{
				N->SetStringField(TEXT("dimensionality"), Asset->IsA<UBlendSpace1D>() ? TEXT("1D") : TEXT("2D"));
			}
		}
	}

	auto SerializeLinkedPin = [](UEdGraphPin* Linked) -> TSharedPtr<FJsonObject>
	{
		TSharedPtr<FJsonObject> L = MakeShared<FJsonObject>();
		if (!Linked) return L;
		if (UEdGraphNode* Owner = Linked->GetOwningNodeUnchecked())
		{
			L->SetStringField(TEXT("node_title"), Owner->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
			L->SetStringField(TEXT("node_guid"),  Owner->NodeGuid.ToString());
		}
		L->SetStringField(TEXT("pin"), Linked->PinName.ToString());
		return L;
	};

	if (UAnimGraphNode_StateMachine* SMNode = Cast<UAnimGraphNode_StateMachine>(Node))
	{
		N->SetStringField(TEXT("type"), TEXT("state_machine"));
		if (SMNode->EditorStateMachineGraph)
			N->SetStringField(TEXT("state_machine_name"), SMNode->EditorStateMachineGraph->GetName());
	}

	// Every visible pin is listed (connected or not) so the model can see what is available to wire.
	TArray<TSharedPtr<FJsonValue>> Outs;
	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (!Pin || Pin->Direction != EGPD_Output || Pin->bHidden) continue;
		TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetStringField(TEXT("name"), Pin->PinName.ToString());
		P->SetStringField(TEXT("type"), Pin->PinType.PinCategory.ToString());
		P->SetBoolField(TEXT("is_pose"), UAnimationGraphSchema::IsPosePin(Pin->PinType));
		P->SetBoolField(TEXT("connected"), Pin->LinkedTo.Num() > 0);
		TArray<TSharedPtr<FJsonValue>> Lk;
		for (UEdGraphPin* L : Pin->LinkedTo) Lk.Add(MakeShared<FJsonValueObject>(SerializeLinkedPin(L)));
		P->SetArrayField(TEXT("connected_to"), Lk);
		Outs.Add(MakeShared<FJsonValueObject>(P));
	}
	N->SetArrayField(TEXT("outputs"), Outs);

	TArray<TSharedPtr<FJsonValue>> Ins;
	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (!Pin || Pin->Direction != EGPD_Input || Pin->bHidden) continue;
		TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetStringField(TEXT("name"), Pin->PinName.ToString());
		P->SetStringField(TEXT("type"), Pin->PinType.PinCategory.ToString());
		P->SetBoolField(TEXT("is_pose"), UAnimationGraphSchema::IsPosePin(Pin->PinType));
		P->SetBoolField(TEXT("connected"), Pin->LinkedTo.Num() > 0);
		if (Pin->LinkedTo.Num() == 0 && !Pin->DefaultValue.IsEmpty())
			P->SetStringField(TEXT("default_value"), Pin->DefaultValue);
		TArray<TSharedPtr<FJsonValue>> Lk;
		for (UEdGraphPin* L : Pin->LinkedTo) Lk.Add(MakeShared<FJsonValueObject>(SerializeLinkedPin(L)));
		P->SetArrayField(TEXT("connected_from"), Lk);
		Ins.Add(MakeShared<FJsonValueObject>(P));
	}
	N->SetArrayField(TEXT("inputs"), Ins);
	return N;
}

static TSharedPtr<FJsonObject> BuildAnimHandoff(UEdGraphNode* Node, UEdGraph* OwningGraph,
	const FString& TargetAsset, const TArray<FString>& Wiring)
{
	TSharedPtr<FJsonObject> H = MakeShared<FJsonObject>();
	if (Node)
	{
		H->SetStringField(TEXT("ctrl_f_search"), Node->GetNodeTitle(ENodeTitleType::ListView).ToString());
		H->SetStringField(TEXT("node_guid"),     Node->NodeGuid.ToString());
	}
	if (OwningGraph) H->SetStringField(TEXT("graph_path"), OwningGraph->GetPathName());
	if (!TargetAsset.IsEmpty()) H->SetStringField(TEXT("target_asset"), TargetAsset);
	TArray<TSharedPtr<FJsonValue>> W;
	for (const FString& S : Wiring) W.Add(MakeShared<FJsonValueString>(S));
	H->SetArrayField(TEXT("required_wiring"), W);
	return H;
}

void HandleGetAnimGraphNodes(const FString& AnimBPPath, FString& OutJson, FString& OutError, bool bIncludeNested)
{
	UAnimBlueprint* AnimBP = Cast<UAnimBlueprint>(UEditorAssetLibrary::LoadAsset(AnimBPPath));
	if (!AnimBP)
	{
		SetError(FString::Printf(TEXT("Could not load AnimBlueprint: %s"), *AnimBPPath), OutJson, OutError);
		return;
	}

	UAnimationGraph* AnimGraph = FindAnimGraph(AnimBP);
	if (!AnimGraph)
	{
		SetError(TEXT("No AnimGraph found in this AnimBlueprint."), OutJson, OutError);
		return;
	}

	TSharedPtr<FJsonObject> Root = MakeShareable(new FJsonObject);
	Root->SetBoolField(TEXT("success"), true);
	Root->SetStringField(TEXT("asset_path"), AnimBPPath);

	TArray<UEdGraph*> Graphs;
	if (bIncludeNested) AnimBP->GetAllGraphs(Graphs);
	else                Graphs.Add(AnimGraph);

	TArray<TSharedPtr<FJsonValue>> NodesArr;
	for (UEdGraph* Graph : Graphs)
	{
		if (!Graph) continue;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (!Node) continue;
			if (bIncludeNested && !Node->IsA<UAnimGraphNode_Base>()) continue;
			NodesArr.Add(MakeShareable(new FJsonValueObject(SerializeAnimNodeJson(Node, Graph))));
		}
	}

	Root->SetArrayField(TEXT("nodes"), NodesArr);
	Root->SetNumberField(TEXT("node_count"), NodesArr.Num());
	BuildSuccessJson(Root, OutJson);
}

void HandleGetSkeletonBones(const FString& SkeletonPath, FString& OutJson, FString& OutError)
{

	USkeleton* Skeleton = Cast<USkeleton>(UEditorAssetLibrary::LoadAsset(SkeletonPath));
	if (!Skeleton)
	{
		SetError(FString::Printf(TEXT("Could not load Skeleton: %s"), *SkeletonPath), OutJson, OutError);
		return;
	}

	const FReferenceSkeleton& RefSkel = Skeleton->GetReferenceSkeleton();
	TArray<TSharedPtr<FJsonValue>> BonesArr;
	for (int32 i = 0; i < RefSkel.GetNum(); ++i)
	{
		TSharedPtr<FJsonObject> B = MakeShareable(new FJsonObject);
		B->SetStringField(TEXT("name"), RefSkel.GetBoneName(i).ToString());
		B->SetNumberField(TEXT("index"), i);
		const int32 ParentIdx = RefSkel.GetParentIndex(i);
		B->SetNumberField(TEXT("parent_index"), ParentIdx);
		B->SetStringField(TEXT("parent_name"), ParentIdx >= 0 ? RefSkel.GetBoneName(ParentIdx).ToString() : TEXT(""));
		BonesArr.Add(MakeShareable(new FJsonValueObject(B)));
	}

	TSharedPtr<FJsonObject> Root = MakeShareable(new FJsonObject);
	Root->SetBoolField(TEXT("success"), true);
	Root->SetStringField(TEXT("skeleton_path"), SkeletonPath);
	Root->SetNumberField(TEXT("bone_count"), RefSkel.GetNum());
	Root->SetArrayField(TEXT("bones"), BonesArr);
	BuildSuccessJson(Root, OutJson);
}

void HandleAddStateMachine(
	const FString& AnimBPPath,
	const FString& SMName,
	int32 PosX, int32 PosY,
	FString& OutJson, FString& OutError)
{

	UAnimBlueprint* AnimBP = Cast<UAnimBlueprint>(UEditorAssetLibrary::LoadAsset(AnimBPPath));
	if (!AnimBP)
	{
		SetError(FString::Printf(TEXT("Could not load AnimBlueprint: %s"), *AnimBPPath), OutJson, OutError);
		return;
	}

	UAnimationGraph* AnimGraph = FindAnimGraph(AnimBP);
	if (!AnimGraph)
	{
		SetError(TEXT("Could not find AnimGraph in the blueprint."), OutJson, OutError);
		return;
	}

	FEdGraphSchemaAction_K2NewNode Action;
	UAnimGraphNode_StateMachine* SMTemplate = NewObject<UAnimGraphNode_StateMachine>(GetTransientPackage());
	Action.NodeTemplate = SMTemplate;
	UEdGraphNode* SpawnedNode = Action.PerformAction(AnimGraph, nullptr, FUECPGraphLoc((float)PosX, (float)PosY), false);
	UAnimGraphNode_StateMachine* SMNode = Cast<UAnimGraphNode_StateMachine>(SpawnedNode);

	FString ResultName = SMName;
	if (SMNode && SMNode->EditorStateMachineGraph)
	{
		TSharedPtr<INameValidatorInterface> Validator = FNameValidatorFactory::MakeValidator(SMNode);
		FBlueprintEditorUtils::RenameGraphWithSuggestion(SMNode->EditorStateMachineGraph, Validator, SMName);
		ResultName = SMNode->EditorStateMachineGraph->GetName();
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBP);
	if (AnimGraph) AnimGraph->NotifyGraphChanged();
	RefreshOpenAnimBlueprintEditor(AnimBP);

	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("state_machine_name"), ResultName);
	if (SMNode) Obj->SetStringField(TEXT("node_guid"), SMNode->NodeGuid.ToString());
	CompileAnimBlueprintAndReport(AnimBP, Obj);
	UEditorAssetLibrary::SaveAsset(AnimBPPath, false);
	BuildSuccessJson(Obj, OutJson);
}

void HandleSetStateMachineEntryState(
	const FString& AnimBPPath,
	const FString& SMName,
	const FString& EntryStateName,
	FString& OutJson, FString& OutError)
{
	UAnimBlueprint* AnimBP = Cast<UAnimBlueprint>(UEditorAssetLibrary::LoadAsset(AnimBPPath));
	if (!AnimBP)
	{
		SetError(FString::Printf(TEXT("Could not load AnimBlueprint: %s"), *AnimBPPath), OutJson, OutError);
		return;
	}

	FString SMLookupError;
	UAnimationStateMachineGraph* SMGraph = FindStateMachineGraph(AnimBP, SMName, &SMLookupError);
	if (!SMGraph)
	{
		SetError(SMLookupError, OutJson, OutError);
		return;
	}

	if (!SMGraph->EntryNode)
	{
		SetError(FString::Printf(TEXT("State machine '%s' has no entry node."), *SMName), OutJson, OutError);
		return;
	}

	UAnimStateNode* TargetState = FindStateByName(SMGraph, EntryStateName);
	if (!TargetState)
	{
		TArray<UAnimStateNode*> All;
		SMGraph->GetNodesOfClass<UAnimStateNode>(All);
		TArray<FString> Names;
		for (UAnimStateNode* S : All) Names.Add(S->GetStateName());
		SetError(FString::Printf(TEXT("State '%s' not found in '%s'. Available: %s"),
			*EntryStateName, *SMName, *FString::Join(Names, TEXT(", "))), OutJson, OutError);
		return;
	}

	UEdGraphPin* EntryOutPin = SMGraph->EntryNode->Pins.Num() > 0 ? SMGraph->EntryNode->Pins[0] : nullptr;
	if (!EntryOutPin)
	{
		SetError(TEXT("Entry node has no output pin."), OutJson, OutError);
		return;
	}

	UEdGraphPin* StateInPin = TargetState->GetInputPin();
	if (!StateInPin)
	{
		SetError(FString::Printf(TEXT("Target state '%s' has no input pin."), *EntryStateName), OutJson, OutError);
		return;
	}

	EntryOutPin->BreakAllPinLinks();
	const bool bConnected = SMGraph->GetSchema()->TryCreateConnection(EntryOutPin, StateInPin);
	if (!bConnected)
	{
		SetError(FString::Printf(TEXT("Schema rejected entry → '%s' connection."), *EntryStateName), OutJson, OutError);
		return;
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBP);
	SMGraph->NotifyGraphChanged();
	RefreshOpenAnimBlueprintEditor(AnimBP);

	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("state_machine_name"), SMGraph->GetName());
	Obj->SetStringField(TEXT("entry_state"), EntryStateName);
	CompileAnimBlueprintAndReport(AnimBP, Obj);
	UEditorAssetLibrary::SaveAsset(AnimBPPath, false);
	BuildSuccessJson(Obj, OutJson);
}

void HandleWireAnimNodeToOutput(
	const FString& AnimBPPath,
	const FString& NodeName,
	FString& OutJson, FString& OutError)
{
	UAnimBlueprint* AnimBP = Cast<UAnimBlueprint>(UEditorAssetLibrary::LoadAsset(AnimBPPath));
	if (!AnimBP)
	{
		SetError(FString::Printf(TEXT("Could not load AnimBlueprint: %s"), *AnimBPPath), OutJson, OutError);
		return;
	}

	UAnimationGraph* AnimGraph = FindAnimGraph(AnimBP);
	if (!AnimGraph)
	{
		SetError(TEXT("AnimGraph not found."), OutJson, OutError);
		return;
	}

	TArray<UAnimGraphNode_Root*> RootNodes;
	AnimGraph->GetNodesOfClass<UAnimGraphNode_Root>(RootNodes);
	if (RootNodes.Num() == 0)
	{
		SetError(TEXT("AnimGraph has no Output Pose (Root) node."), OutJson, OutError);
		return;
	}

	UEdGraphNode* SourceNode = nullptr;
	TArray<FString> CandidateNames;
	for (UEdGraphNode* Node : AnimGraph->Nodes)
	{
		if (!Node || Node->IsA<UAnimGraphNode_Root>()) continue;
		const FString Title = Node->GetNodeTitle(ENodeTitleType::ListView).ToString();
		const FString TitleFull = Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString();
		FString OwnedGraphName;
		if (UAnimGraphNode_StateMachine* SM = Cast<UAnimGraphNode_StateMachine>(Node))
			if (SM->EditorStateMachineGraph) OwnedGraphName = SM->EditorStateMachineGraph->GetName();
		CandidateNames.Add(Title);
		if (NodeName.IsEmpty()
			|| Title.Equals(NodeName, ESearchCase::IgnoreCase)
			|| TitleFull.Contains(NodeName, ESearchCase::IgnoreCase)
			|| (!OwnedGraphName.IsEmpty() && OwnedGraphName.Equals(NodeName, ESearchCase::IgnoreCase)))
		{
			SourceNode = Node;
			break;
		}
	}

	if (!SourceNode)
	{
		SetError(FString::Printf(TEXT("Node '%s' not found in AnimGraph. Candidates: %s"),
			*NodeName, *FString::Join(CandidateNames, TEXT(", "))), OutJson, OutError);
		return;
	}

	UEdGraphPin* SourceOut = FindPoseOutputPin(SourceNode);
	UEdGraphPin* RootIn    = FindPoseInputPin(RootNodes[0]);
	if (!SourceOut || !RootIn)
	{
		SetError(FString::Printf(TEXT("Could not find pose pins to connect (source '%s' has %s pose output; Output Pose has %s pose input)."),
			*SourceNode->GetNodeTitle(ENodeTitleType::ListView).ToString(),
			SourceOut ? TEXT("a") : TEXT("no"), RootIn ? TEXT("a") : TEXT("no")), OutJson, OutError);
		return;
	}

	RootIn->BreakAllPinLinks();
	const bool bConnected = AnimGraph->GetSchema()->TryCreateConnection(SourceOut, RootIn);
	if (!bConnected)
	{
		SetError(TEXT("Schema rejected source → Output Pose connection."), OutJson, OutError);
		return;
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBP);
	AnimGraph->NotifyGraphChanged();
	RefreshOpenAnimBlueprintEditor(AnimBP);

	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("source_node"), SourceNode->GetNodeTitle(ENodeTitleType::ListView).ToString());
	Obj->SetStringField(TEXT("source_guid"), SourceNode->NodeGuid.ToString());
	CompileAnimBlueprintAndReport(AnimBP, Obj);
	UEditorAssetLibrary::SaveAsset(AnimBPPath, false);
	BuildSuccessJson(Obj, OutJson);
}

void HandleRemoveAnimState(
	const FString& AnimBPPath,
	const FString& SMName,
	const FString& StateName,
	FString& OutJson, FString& OutError)
{

	UAnimBlueprint* AnimBP = Cast<UAnimBlueprint>(UEditorAssetLibrary::LoadAsset(AnimBPPath));
	if (!AnimBP)
	{
		SetError(FString::Printf(TEXT("Could not load AnimBlueprint: %s"), *AnimBPPath), OutJson, OutError);
		return;
	}

	FString SMLookupError;
	UAnimationStateMachineGraph* SMGraph = FindStateMachineGraph(AnimBP, SMName, &SMLookupError);
	if (!SMGraph)
	{
		SetError(SMLookupError, OutJson, OutError);
		return;
	}

	UAnimStateNode* StateNode = FindStateByName(SMGraph, StateName);
	if (!StateNode)
	{
		SetError(FString::Printf(TEXT("State '%s' not found in '%s'."), *StateName, *SMGraph->GetName()), OutJson, OutError);
		return;
	}

	TArray<UAnimStateTransitionNode*> ToRemove;
	for (UEdGraphNode* Node : SMGraph->Nodes)
	{
		UAnimStateTransitionNode* T = Cast<UAnimStateTransitionNode>(Node);
		if (!T) continue;
		UAnimStateNodeBase* Prev = T->GetPreviousState();
		UAnimStateNodeBase* Next = T->GetNextState();
		if (Prev == StateNode || Next == StateNode)
			ToRemove.Add(T);
	}
	// FBlueprintEditorUtils::RemoveNode destroys the bound rule/state graphs too; a bare
	// UEdGraph::RemoveNode would leave them orphaned inside the blueprint.
	for (UAnimStateTransitionNode* T : ToRemove)
		FBlueprintEditorUtils::RemoveNode(AnimBP, T, /*bDontRecompile*/ true);

	FBlueprintEditorUtils::RemoveNode(AnimBP, StateNode, /*bDontRecompile*/ true);
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBP);
	SMGraph->NotifyGraphChanged();
	RefreshOpenAnimBlueprintEditor(AnimBP);

	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("removed_state"), StateName);
	Obj->SetNumberField(TEXT("removed_transitions"), ToRemove.Num());
	CompileAnimBlueprintAndReport(AnimBP, Obj);
	UEditorAssetLibrary::SaveAsset(AnimBPPath, false);
	BuildSuccessJson(Obj, OutJson);
}

void HandleRemoveStateTransition(
	const FString& AnimBPPath,
	const FString& SMName,
	const FString& FromState,
	const FString& ToState,
	FString& OutJson, FString& OutError)
{

	UAnimBlueprint* AnimBP = Cast<UAnimBlueprint>(UEditorAssetLibrary::LoadAsset(AnimBPPath));
	if (!AnimBP)
	{
		SetError(FString::Printf(TEXT("Could not load AnimBlueprint: %s"), *AnimBPPath), OutJson, OutError);
		return;
	}

	FString SMLookupError;
	UAnimationStateMachineGraph* SMGraph = FindStateMachineGraph(AnimBP, SMName, &SMLookupError);
	if (!SMGraph)
	{
		SetError(SMLookupError, OutJson, OutError);
		return;
	}

	TArray<UAnimStateTransitionNode*> ToRemove;
	for (UEdGraphNode* Node : SMGraph->Nodes)
	{
		UAnimStateTransitionNode* T = Cast<UAnimStateTransitionNode>(Node);
		if (!T) continue;
		UAnimStateNodeBase* Prev = T->GetPreviousState();
		UAnimStateNodeBase* Next = T->GetNextState();
		if (!Prev || !Next) continue;
		const bool bFromMatch = FromState.IsEmpty() || Prev->GetStateName() == FromState;
		const bool bToMatch = ToState.IsEmpty() || Next->GetStateName() == ToState;
		if (bFromMatch && bToMatch)
			ToRemove.Add(T);
	}

	if (ToRemove.IsEmpty())
	{
		SetError(FString::Printf(TEXT("No transition found from '%s' to '%s'."), *FromState, *ToState), OutJson, OutError);
		return;
	}

	// Destroys the transition rule graphs as well (see remove_anim_state).
	for (UAnimStateTransitionNode* T : ToRemove)
		FBlueprintEditorUtils::RemoveNode(AnimBP, T, /*bDontRecompile*/ true);

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBP);
	SMGraph->NotifyGraphChanged();
	RefreshOpenAnimBlueprintEditor(AnimBP);

	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetNumberField(TEXT("removed_count"), ToRemove.Num());
	CompileAnimBlueprintAndReport(AnimBP, Obj);
	UEditorAssetLibrary::SaveAsset(AnimBPPath, false);
	BuildSuccessJson(Obj, OutJson);
}

void HandleRenameAnimState(
	const FString& AnimBPPath,
	const FString& SMName,
	const FString& OldName,
	const FString& NewName,
	FString& OutJson, FString& OutError)
{

	UAnimBlueprint* AnimBP = Cast<UAnimBlueprint>(UEditorAssetLibrary::LoadAsset(AnimBPPath));
	if (!AnimBP)
	{
		SetError(FString::Printf(TEXT("Could not load AnimBlueprint: %s"), *AnimBPPath), OutJson, OutError);
		return;
	}

	FString SMLookupError;
	UAnimationStateMachineGraph* SMGraph = FindStateMachineGraph(AnimBP, SMName, &SMLookupError);
	if (!SMGraph)
	{
		SetError(SMLookupError, OutJson, OutError);
		return;
	}

	UAnimStateNode* StateNode = FindStateByName(SMGraph, OldName);
	if (!StateNode)
	{
		SetError(FString::Printf(TEXT("State '%s' not found."), *OldName), OutJson, OutError);
		return;
	}

	if (!StateNode->BoundGraph)
	{
		SetError(FString::Printf(TEXT("State '%s' has no bound graph to rename."), *OldName), OutJson, OutError);
		return;
	}

	TSharedPtr<INameValidatorInterface> Validator = FNameValidatorFactory::MakeValidator(StateNode);
	FBlueprintEditorUtils::RenameGraphWithSuggestion(StateNode->BoundGraph, Validator, NewName);

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBP);
	SMGraph->NotifyGraphChanged();
	RefreshOpenAnimBlueprintEditor(AnimBP);

	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("old_name"), OldName);
	Obj->SetStringField(TEXT("new_name"), StateNode->GetStateName());
	CompileAnimBlueprintAndReport(AnimBP, Obj);
	UEditorAssetLibrary::SaveAsset(AnimBPPath, false);
	BuildSuccessJson(Obj, OutJson);
}

void HandleCreateAnimSlot(
	const FString& SkeletonPath,
	const FString& SlotName,
	const FString& GroupName,
	FString& OutJson, FString& OutError)
{

	USkeleton* Skeleton = Cast<USkeleton>(UEditorAssetLibrary::LoadAsset(SkeletonPath));
	if (!Skeleton)
	{
		SetError(FString::Printf(TEXT("Could not load Skeleton: %s"), *SkeletonPath), OutJson, OutError);
		return;
	}

	Skeleton->Modify();
	const FName SlotFName(*SlotName);
	const FName GroupFName = GroupName.IsEmpty() ? FName(TEXT("DefaultGroup")) : FName(*GroupName);

	Skeleton->AddSlotGroupName(GroupFName);
	Skeleton->RegisterSlotNode(SlotFName);
	Skeleton->SetSlotGroupName(SlotFName, GroupFName);

	UEditorAssetLibrary::SaveAsset(SkeletonPath, false);

	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("slot_name"), SlotName);
	Obj->SetStringField(TEXT("group_name"), GroupFName.ToString());
	BuildSuccessJson(Obj, OutJson);
}

void HandleGetBlendspaceInfo(const FString& BlendspacePath, FString& OutJson, FString& OutError)
{

	UBlendSpace* BS = Cast<UBlendSpace>(UEditorAssetLibrary::LoadAsset(BlendspacePath));
	if (!BS)
	{
		SetError(FString::Printf(TEXT("Could not load BlendSpace: %s"), *BlendspacePath), OutJson, OutError);
		return;
	}

	const bool bIs1D = BS->IsA<UBlendSpace1D>();

	TSharedPtr<FJsonObject> Root = MakeShareable(new FJsonObject);
	Root->SetBoolField(TEXT("success"), true);
	Root->SetStringField(TEXT("asset_path"), BlendspacePath);
	Root->SetBoolField(TEXT("is_1d"), bIs1D);

	const FBlendParameter& Axis0 = BS->GetBlendParameter(0);
	TSharedPtr<FJsonObject> AxisX = MakeShareable(new FJsonObject);
	AxisX->SetStringField(TEXT("name"), Axis0.DisplayName);
	AxisX->SetNumberField(TEXT("min"), Axis0.Min);
	AxisX->SetNumberField(TEXT("max"), Axis0.Max);
	AxisX->SetNumberField(TEXT("grid_divisions"), Axis0.GridNum);
	Root->SetObjectField(TEXT("axis_x"), AxisX);

	if (!bIs1D)
	{
		const FBlendParameter& Axis1 = BS->GetBlendParameter(1);
		TSharedPtr<FJsonObject> AxisY = MakeShareable(new FJsonObject);
		AxisY->SetStringField(TEXT("name"), Axis1.DisplayName);
		AxisY->SetNumberField(TEXT("min"), Axis1.Min);
		AxisY->SetNumberField(TEXT("max"), Axis1.Max);
		AxisY->SetNumberField(TEXT("grid_divisions"), Axis1.GridNum);
		Root->SetObjectField(TEXT("axis_y"), AxisY);
	}

	const TArray<FBlendSample>& Samples = BS->GetBlendSamples();
	TArray<TSharedPtr<FJsonValue>> SamplesArr;
	for (const FBlendSample& S : Samples)
	{
		TSharedPtr<FJsonObject> SObj = MakeShareable(new FJsonObject);
		SObj->SetStringField(TEXT("animation"), S.Animation ? S.Animation->GetPathName() : TEXT(""));
		SObj->SetNumberField(TEXT("sample_x"), S.SampleValue.X);
		if (!bIs1D)
			SObj->SetNumberField(TEXT("sample_y"), S.SampleValue.Y);
		SObj->SetNumberField(TEXT("sample_z"), S.SampleValue.Z);
		SObj->SetNumberField(TEXT("rate_scale"), S.RateScale);
		SamplesArr.Add(MakeShareable(new FJsonValueObject(SObj)));
	}
	Root->SetArrayField(TEXT("samples"), SamplesArr);
	Root->SetNumberField(TEXT("sample_count"), Samples.Num());

	BuildSuccessJson(Root, OutJson);
}

void HandleSetIKRetargetRoot(const FString& IKRigPath, const FString& BoneName, FString& OutJson, FString& OutError)
{

	UIKRigDefinition* Rig = Cast<UIKRigDefinition>(UEditorAssetLibrary::LoadAsset(IKRigPath));
	if (!Rig)
	{
		SetError(FString::Printf(TEXT("Could not load IKRig: %s"), *IKRigPath), OutJson, OutError);
		return;
	}

	UIKRigController* Controller = UIKRigController::GetController(Rig);
	if (!Controller)
	{
		SetError(TEXT("Could not get IKRigController."), OutJson, OutError);
		return;
	}

	if (!Controller->SetRetargetRoot(FName(*BoneName)))
	{
		SetError(FString::Printf(TEXT("SetRetargetRoot failed for bone '%s'. Verify the bone name via get_skeleton_bones."), *BoneName), OutJson, OutError);
		return;
	}

	UEditorAssetLibrary::SaveAsset(IKRigPath, false);

	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("retarget_root"), BoneName);
	BuildSuccessJson(Obj, OutJson);
}

void HandleSetRetargetPose(
	const FString& RetargeterPath,
	const FString& PoseName,
	const FString& SourceOrTarget,
	FString& OutJson, FString& OutError)
{

	UIKRetargeter* Retargeter = Cast<UIKRetargeter>(UEditorAssetLibrary::LoadAsset(RetargeterPath));
	if (!Retargeter)
	{
		SetError(FString::Printf(TEXT("Could not load IKRetargeter: %s"), *RetargeterPath), OutJson, OutError);
		return;
	}

	UIKRetargeterController* Controller = UIKRetargeterController::GetController(Retargeter);
	if (!Controller)
	{
		SetError(TEXT("Could not get IKRetargeterController."), OutJson, OutError);
		return;
	}

	const ERetargetSourceOrTarget Side = SourceOrTarget.Equals(TEXT("target"), ESearchCase::IgnoreCase)
		? ERetargetSourceOrTarget::Target
		: ERetargetSourceOrTarget::Source;

	const FName PoseFName(*PoseName);
	const TMap<FName, FIKRetargetPose>& Poses = Controller->GetRetargetPoses(Side);
	if (!Poses.Contains(PoseFName))
	{
		FName Created = Controller->CreateRetargetPose(PoseFName, Side);
		if (Created.IsNone())
		{
			SetError(FString::Printf(TEXT("Failed to create retarget pose '%s'."), *PoseName), OutJson, OutError);
			return;
		}
	}

	Controller->SetCurrentRetargetPose(PoseFName, Side);
	UEditorAssetLibrary::SaveAsset(RetargeterPath, false);

	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("pose_name"), PoseName);
	Obj->SetStringField(TEXT("side"), SourceOrTarget.IsEmpty() ? TEXT("source") : SourceOrTarget);
	BuildSuccessJson(Obj, OutJson);
}

void HandleCreateAnimLayerInterface(
	const FString& Name,
	const FString& SavePath,
	FString& OutJson, FString& OutError)
{
	if (Name.IsEmpty()) { SetError(TEXT("name is required"), OutJson, OutError); return; }

	FString PackagePath = SavePath.IsEmpty() ? TEXT("/Game") : SavePath;
	while (PackagePath.EndsWith(TEXT("/"))) PackagePath = PackagePath.LeftChop(1);

	UAnimLayerInterfaceFactory* Factory = NewObject<UAnimLayerInterfaceFactory>();
	IAssetTools& AssetToolsRef = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
	UObject* Asset = AssetToolsRef.CreateAsset(Name, PackagePath, nullptr, Factory);

	if (!Asset)
	{
		SetError(FString::Printf(TEXT("Failed to create AnimLayerInterface '%s'."), *Name), OutJson, OutError);
		return;
	}

	FString AssetPath = FString::Printf(TEXT("%s/%s"), *PackagePath, *Name);
	UEditorAssetLibrary::SaveAsset(AssetPath, false);

	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("asset_path"), AssetPath);
	BuildSuccessJson(Obj, OutJson);
}

void HandleImplementAnimLayer(
	const FString& AnimBPPath,
	const FString& InterfacePath,
	FString& OutJson, FString& OutError)
{

	UAnimBlueprint* AnimBP = Cast<UAnimBlueprint>(UEditorAssetLibrary::LoadAsset(AnimBPPath));
	if (!AnimBP)
	{
		SetError(FString::Printf(TEXT("Could not load AnimBlueprint: %s"), *AnimBPPath), OutJson, OutError);
		return;
	}

	UBlueprint* IfaceBP = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(InterfacePath));
	if (!IfaceBP || !IfaceBP->GeneratedClass)
	{
		SetError(FString::Printf(TEXT("Could not load AnimLayerInterface blueprint: %s"), *InterfacePath), OutJson, OutError);
		return;
	}

	FTopLevelAssetPath ClassPath(IfaceBP->GeneratedClass->GetPathName());
	if (!FBlueprintEditorUtils::ImplementNewInterface(AnimBP, ClassPath))
	{
		SetError(FString::Printf(TEXT("ImplementNewInterface failed for '%s'. May already be implemented."), *InterfacePath), OutJson, OutError);
		return;
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBP);

	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("anim_bp"), AnimBPPath);
	Obj->SetStringField(TEXT("interface"), InterfacePath);
	CompileAnimBlueprintAndReport(AnimBP, Obj);
	UEditorAssetLibrary::SaveAsset(AnimBPPath, false);
	BuildSuccessJson(Obj, OutJson);
}

void HandleAddAnimLayerNode(
	const FString& AnimBPPath,
	const FString& LayerName,
	int32 PosX, int32 PosY,
	FString& OutJson, FString& OutError)
{

	UAnimBlueprint* AnimBP = Cast<UAnimBlueprint>(UEditorAssetLibrary::LoadAsset(AnimBPPath));
	if (!AnimBP)
	{
		SetError(FString::Printf(TEXT("Could not load AnimBlueprint: %s"), *AnimBPPath), OutJson, OutError);
		return;
	}

	UAnimationGraph* AnimGraph = FindAnimGraph(AnimBP);
	if (!AnimGraph)
	{
		SetError(TEXT("Could not find AnimGraph in the blueprint."), OutJson, OutError);
		return;
	}

	FEdGraphSchemaAction_K2NewNode LayerAction;
	UAnimGraphNode_LinkedAnimLayer* NodeTemplate = NewObject<UAnimGraphNode_LinkedAnimLayer>(GetTransientPackage());
	LayerAction.NodeTemplate = NodeTemplate;
	UEdGraphNode* SpawnedNode = LayerAction.PerformAction(AnimGraph, nullptr, FUECPGraphLoc((float)PosX, (float)PosY), false);
	UAnimGraphNode_LinkedAnimLayer* LayerNode = Cast<UAnimGraphNode_LinkedAnimLayer>(SpawnedNode);

	if (!LayerNode)
	{
		SetError(TEXT("Failed to spawn LinkedAnimLayer node."), OutJson, OutError);
		return;
	}

	if (!LayerName.IsEmpty())
		LayerNode->Node.Layer = FName(*LayerName);

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBP);
	AnimGraph->NotifyGraphChanged();

	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("layer_name"), LayerName);
	CompileAnimBlueprintAndReport(AnimBP, Obj);
	UEditorAssetLibrary::SaveAsset(AnimBPPath, false);
	BuildSuccessJson(Obj, OutJson);
}

void HandleGetAnimSequenceInfo(const FString& AnimPath, FString& OutJson, FString& OutError)
{

	UAnimSequence* AnimSeq = Cast<UAnimSequence>(UEditorAssetLibrary::LoadAsset(AnimPath));
	if (!AnimSeq)
	{
		SetError(FString::Printf(TEXT("Could not load AnimSequence: %s"), *AnimPath), OutJson, OutError);
		return;
	}

	TSharedPtr<FJsonObject> Root = MakeShareable(new FJsonObject);
	Root->SetBoolField(TEXT("success"), true);
	Root->SetStringField(TEXT("asset_path"), AnimPath);
	Root->SetNumberField(TEXT("play_length"), AnimSeq->GetPlayLength());
	Root->SetNumberField(TEXT("num_sampled_keys"), AnimSeq->GetNumberOfSampledKeys());
	Root->SetNumberField(TEXT("rate_scale"), AnimSeq->RateScale);
	Root->SetBoolField(TEXT("enable_root_motion"), AnimSeq->bEnableRootMotion);

	TArray<TSharedPtr<FJsonValue>> NotifiesArr;
	for (const FAnimNotifyEvent& N : AnimSeq->Notifies)
	{
		TSharedPtr<FJsonObject> NObj = MakeShareable(new FJsonObject);
		NObj->SetStringField(TEXT("name"), N.NotifyName.ToString());
		NObj->SetNumberField(TEXT("time"), N.GetTime());
		NObj->SetNumberField(TEXT("duration"), N.Duration);
		const FString ClassName = N.Notify
			? N.Notify->GetClass()->GetName()
			: (N.NotifyStateClass ? N.NotifyStateClass->GetClass()->GetName() : TEXT(""));
		NObj->SetStringField(TEXT("class"), ClassName);
		NotifiesArr.Add(MakeShareable(new FJsonValueObject(NObj)));
	}
	Root->SetArrayField(TEXT("notifies"), NotifiesArr);

	TArray<TSharedPtr<FJsonValue>> CurvesArr;
	if (const IAnimationDataModel* DataModel = AnimSeq->GetDataModel())
	{
		for (const FFloatCurve& Curve : DataModel->GetFloatCurves())
		{
			TSharedPtr<FJsonObject> CObj = MakeShareable(new FJsonObject);
			CObj->SetStringField(TEXT("name"), Curve.GetName().ToString());
			CObj->SetStringField(TEXT("type"), TEXT("Float"));
			CObj->SetNumberField(TEXT("num_keys"), Curve.FloatCurve.GetNumKeys());
			CurvesArr.Add(MakeShareable(new FJsonValueObject(CObj)));
		}
	}
	Root->SetArrayField(TEXT("curves"), CurvesArr);
	BuildSuccessJson(Root, OutJson);
}

void HandleGetMontageSummary(const FString& MontagePath, FString& OutJson, FString& OutError)
{

	UAnimMontage* Montage = Cast<UAnimMontage>(UEditorAssetLibrary::LoadAsset(MontagePath));
	if (!Montage)
	{
		SetError(FString::Printf(TEXT("Could not load AnimMontage: %s"), *MontagePath), OutJson, OutError);
		return;
	}

	TSharedPtr<FJsonObject> Root = MakeShareable(new FJsonObject);
	Root->SetBoolField(TEXT("success"), true);
	Root->SetStringField(TEXT("asset_path"), MontagePath);
	Root->SetNumberField(TEXT("play_length"), Montage->GetPlayLength());
	Root->SetNumberField(TEXT("section_count"), Montage->GetNumSections());

	TArray<TSharedPtr<FJsonValue>> SectionsArr;
	for (const FCompositeSection& S : Montage->CompositeSections)
	{
		TSharedPtr<FJsonObject> SObj = MakeShareable(new FJsonObject);
		SObj->SetStringField(TEXT("name"), S.SectionName.ToString());
		SObj->SetNumberField(TEXT("time"), S.GetTime());
		SObj->SetStringField(TEXT("next_section"), S.NextSectionName.ToString());
		SectionsArr.Add(MakeShareable(new FJsonValueObject(SObj)));
	}
	Root->SetArrayField(TEXT("sections"), SectionsArr);

	TArray<TSharedPtr<FJsonValue>> SlotsArr;
	for (const FSlotAnimationTrack& Slot : Montage->SlotAnimTracks)
	{
		TSharedPtr<FJsonObject> SlotObj = MakeShareable(new FJsonObject);
		SlotObj->SetStringField(TEXT("slot_name"), Slot.SlotName.ToString());
		SlotObj->SetNumberField(TEXT("anim_segments"), Slot.AnimTrack.AnimSegments.Num());
		SlotsArr.Add(MakeShareable(new FJsonValueObject(SlotObj)));
	}
	Root->SetArrayField(TEXT("slot_tracks"), SlotsArr);

	TArray<TSharedPtr<FJsonValue>> TracksArr;
	for (int32 ti = 0; ti < Montage->AnimNotifyTracks.Num(); ++ti)
	{
		TSharedPtr<FJsonObject> TObj = MakeShareable(new FJsonObject);
		TObj->SetNumberField(TEXT("index"), ti);
		TObj->SetStringField(TEXT("name"), Montage->AnimNotifyTracks[ti].TrackName.ToString());
		TracksArr.Add(MakeShareable(new FJsonValueObject(TObj)));
	}
	Root->SetArrayField(TEXT("notify_tracks"), TracksArr);

	TArray<TSharedPtr<FJsonValue>> NotifiesArr;
	for (const FAnimNotifyEvent& N : Montage->Notifies)
	{
		TSharedPtr<FJsonObject> NObj = MakeShareable(new FJsonObject);
		NObj->SetStringField(TEXT("name"), N.NotifyName.ToString());
		NObj->SetNumberField(TEXT("time"), N.GetTime());
		NObj->SetNumberField(TEXT("duration"), N.Duration);
		NObj->SetNumberField(TEXT("track_index"), N.TrackIndex);
		if (Montage->AnimNotifyTracks.IsValidIndex(N.TrackIndex))
			NObj->SetStringField(TEXT("track_name"), Montage->AnimNotifyTracks[N.TrackIndex].TrackName.ToString());

		UObject* NotifyObj = N.Notify ? static_cast<UObject*>(N.Notify) : static_cast<UObject*>(N.NotifyStateClass);
		if (NotifyObj)
		{
			UClass* NCls = NotifyObj->GetClass();
			NObj->SetStringField(TEXT("class"), NCls->GetName());
			NObj->SetBoolField(TEXT("is_state"), N.NotifyStateClass != nullptr);

			if (FObjectProperty* TemplateProp = FindFProperty<FObjectProperty>(NCls, TEXT("Template")))
			{
				if (UObject* Template = TemplateProp->GetObjectPropertyValue_InContainer(NotifyObj))
				{
					NObj->SetStringField(TEXT("niagara_system_path"), Template->GetPathName());
					if (FStructProperty* SP = FindFProperty<FStructProperty>(NCls, TEXT("LocationOffset")))
					{
						const FVector* V = SP->ContainerPtrToValuePtr<FVector>(NotifyObj);
						TArray<TSharedPtr<FJsonValue>> Loc;
						Loc.Add(MakeShareable(new FJsonValueNumber(V->X)));
						Loc.Add(MakeShareable(new FJsonValueNumber(V->Y)));
						Loc.Add(MakeShareable(new FJsonValueNumber(V->Z)));
						NObj->SetArrayField(TEXT("location_offset"), Loc);
					}
					if (FNameProperty* NP = FindFProperty<FNameProperty>(NCls, TEXT("SocketName")))
						NObj->SetStringField(TEXT("socket_name"), NP->GetPropertyValue_InContainer(NotifyObj).ToString());
				}
			}
			if (FObjectProperty* SoundProp = FindFProperty<FObjectProperty>(NCls, TEXT("Sound")))
			{
				if (UObject* Sound = SoundProp->GetObjectPropertyValue_InContainer(NotifyObj))
				{
					NObj->SetStringField(TEXT("sound_path"), Sound->GetPathName());
					if (FFloatProperty* VP = FindFProperty<FFloatProperty>(NCls, TEXT("VolumeMultiplier")))
						NObj->SetNumberField(TEXT("volume_multiplier"), VP->GetPropertyValue_InContainer(NotifyObj));
					if (FFloatProperty* PP = FindFProperty<FFloatProperty>(NCls, TEXT("PitchMultiplier")))
						NObj->SetNumberField(TEXT("pitch_multiplier"), PP->GetPropertyValue_InContainer(NotifyObj));
				}
			}
		}
		NotifiesArr.Add(MakeShareable(new FJsonValueObject(NObj)));
	}
	Root->SetArrayField(TEXT("notifies"), NotifiesArr);
	BuildSuccessJson(Root, OutJson);
}

void HandleGetIKRigSummary(const FString& IKRigPath, FString& OutJson, FString& OutError)
{

	UIKRigDefinition* IKRig = Cast<UIKRigDefinition>(UEditorAssetLibrary::LoadAsset(IKRigPath));
	if (!IKRig)
	{
		SetError(FString::Printf(TEXT("Could not load IKRig: %s"), *IKRigPath), OutJson, OutError);
		return;
	}

	UIKRigController* Controller = UIKRigController::GetController(IKRig);
	if (!Controller)
	{
		SetError(TEXT("Could not get IKRig controller"), OutJson, OutError);
		return;
	}

	TSharedPtr<FJsonObject> Root = MakeShareable(new FJsonObject);
	Root->SetBoolField(TEXT("success"), true);
	Root->SetStringField(TEXT("asset_path"), IKRigPath);

	TArray<TSharedPtr<FJsonValue>> SolversArr;
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6)
	const TArray<FIKRigSolverBase*> Solvers = Controller->GetSolverArray();
	for (int32 i = 0; i < Solvers.Num(); ++i)
	{
		if (!Solvers[i]) continue;
		TSharedPtr<FJsonObject> SObj = MakeShareable(new FJsonObject);
		SObj->SetNumberField(TEXT("index"), i);
		SObj->SetStringField(TEXT("type"), Controller->GetSolverUniqueName(i));
		SolversArr.Add(MakeShareable(new FJsonValueObject(SObj)));
	}
#else
	const TArray<UIKRigSolver*>& Solvers = Controller->GetSolverArray();
	for (int32 i = 0; i < Solvers.Num(); ++i)
	{
		if (!Solvers[i]) continue;
		TSharedPtr<FJsonObject> SObj = MakeShareable(new FJsonObject);
		SObj->SetNumberField(TEXT("index"), i);
		SObj->SetStringField(TEXT("type"), Solvers[i]->GetClass()->GetName());
		SolversArr.Add(MakeShareable(new FJsonValueObject(SObj)));
	}
#endif
	Root->SetArrayField(TEXT("solvers"), SolversArr);

	TArray<TSharedPtr<FJsonValue>> GoalsArr;
	for (const UIKRigEffectorGoal* Goal : Controller->GetAllGoals())
	{
		if (!Goal) continue;
		TSharedPtr<FJsonObject> GObj = MakeShareable(new FJsonObject);
		GObj->SetStringField(TEXT("name"), Goal->GoalName.ToString());
		GObj->SetStringField(TEXT("bone"), Goal->BoneName.ToString());
		GoalsArr.Add(MakeShareable(new FJsonValueObject(GObj)));
	}
	Root->SetArrayField(TEXT("goals"), GoalsArr);

	TArray<TSharedPtr<FJsonValue>> ChainsArr;
	for (const FBoneChain& Chain : IKRig->GetRetargetChains())
	{
		TSharedPtr<FJsonObject> CObj = MakeShareable(new FJsonObject);
		CObj->SetStringField(TEXT("name"), Chain.ChainName.ToString());
		CObj->SetStringField(TEXT("start_bone"), Chain.StartBone.BoneName.ToString());
		CObj->SetStringField(TEXT("end_bone"), Chain.EndBone.BoneName.ToString());
		CObj->SetStringField(TEXT("goal"), Chain.IKGoalName.ToString());
		ChainsArr.Add(MakeShareable(new FJsonValueObject(CObj)));
	}
	Root->SetArrayField(TEXT("retarget_chains"), ChainsArr);
	BuildSuccessJson(Root, OutJson);
}

void HandleListAnimSlots(const FString& SkeletonPath, FString& OutJson, FString& OutError)
{

	USkeleton* Skeleton = Cast<USkeleton>(UEditorAssetLibrary::LoadAsset(SkeletonPath));
	if (!Skeleton)
	{
		SetError(FString::Printf(TEXT("Could not load Skeleton: %s"), *SkeletonPath), OutJson, OutError);
		return;
	}

	TArray<TSharedPtr<FJsonValue>> GroupsArr;
	for (const FAnimSlotGroup& Group : Skeleton->GetSlotGroups())
	{
		TSharedPtr<FJsonObject> GObj = MakeShareable(new FJsonObject);
		GObj->SetStringField(TEXT("group_name"), Group.GroupName.ToString());
		TArray<TSharedPtr<FJsonValue>> SlotsArr;
		for (const FName& SlotName : Group.SlotNames)
			SlotsArr.Add(MakeShareable(new FJsonValueString(SlotName.ToString())));
		GObj->SetArrayField(TEXT("slots"), SlotsArr);
		GroupsArr.Add(MakeShareable(new FJsonValueObject(GObj)));
	}

	TSharedPtr<FJsonObject> Root = MakeShareable(new FJsonObject);
	Root->SetBoolField(TEXT("success"), true);
	Root->SetStringField(TEXT("skeleton_path"), SkeletonPath);
	Root->SetArrayField(TEXT("groups"), GroupsArr);
	Root->SetNumberField(TEXT("group_count"), Skeleton->GetSlotGroups().Num());
	BuildSuccessJson(Root, OutJson);
}

void HandleAddLayeredBlendPerBone(const FString& AnimBPPath, int32 NumLayers,
	int32 PosX, int32 PosY, FString& OutJson, FString& OutError)
{

	UAnimBlueprint* AnimBP = Cast<UAnimBlueprint>(UEditorAssetLibrary::LoadAsset(AnimBPPath));
	if (!AnimBP)
	{
		SetError(FString::Printf(TEXT("Could not load AnimBlueprint: %s"), *AnimBPPath), OutJson, OutError);
		return;
	}

	UAnimationGraph* AnimGraph = FindAnimGraph(AnimBP);
	if (!AnimGraph)
	{
		SetError(TEXT("Could not find AnimGraph in blueprint."), OutJson, OutError);
		return;
	}

	UAnimGraphNode_LayeredBoneBlend* BlendNode = NewObject<UAnimGraphNode_LayeredBoneBlend>(AnimGraph);
	if (!BlendNode)
	{
		SetError(TEXT("Failed to spawn LayeredBoneBlend node."), OutJson, OutError);
		return;
	}
	BlendNode->CreateNewGuid();
	BlendNode->NodePosX = PosX;
	BlendNode->NodePosY = PosY;
	BlendNode->SetFlags(RF_Transactional);
	BlendNode->AllocateDefaultPins();
	BlendNode->PostPlacedNewNode();
	AnimGraph->Modify();
	AnimGraph->AddNode(BlendNode,  true,  false);

	const int32 ExtraLayers = FMath::Max(0, NumLayers - 1);
	for (int32 i = 0; i < ExtraLayers; ++i)
	{
		BlendNode->AddPinToBlendByFilter();
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBP);
	AnimGraph->NotifyGraphChanged();

	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetNumberField(TEXT("num_layers"), NumLayers);
	if (BlendNode->NodeGuid.IsValid())
	{
		Obj->SetStringField(TEXT("node_guid"), BlendNode->NodeGuid.ToString(EGuidFormats::Digits));
	}
	Obj->SetNumberField(TEXT("pos_x"), BlendNode->NodePosX);
	Obj->SetNumberField(TEXT("pos_y"), BlendNode->NodePosY);
	Obj->SetNumberField(TEXT("blend_weights_num"), BlendNode->Node.BlendWeights.Num());
	Obj->SetNumberField(TEXT("blend_poses_num"), BlendNode->Node.BlendPoses.Num());
	Obj->SetNumberField(TEXT("layer_setup_num"), BlendNode->Node.LayerSetup.Num());
	if (BlendNode->Node.BlendWeights.Num() > 0)
	{
		Obj->SetNumberField(TEXT("blend_weight_0"), BlendNode->Node.BlendWeights[0]);
	}
	Obj->SetStringField(TEXT("blend_mode"),
		BlendNode->Node.BlendMode == ELayeredBoneBlendMode::BranchFilter ? TEXT("BranchFilter") : TEXT("BlendMask"));
	CompileAnimBlueprintAndReport(AnimBP, Obj);
	UEditorAssetLibrary::SaveAsset(AnimBPPath, false);
	BuildSuccessJson(Obj, OutJson);
}

void HandleAddSavedPose(const FString& AnimBPPath, const FString& CacheName,
	int32 PosX, int32 PosY, FString& OutJson, FString& OutError)
{

	UAnimBlueprint* AnimBP = Cast<UAnimBlueprint>(UEditorAssetLibrary::LoadAsset(AnimBPPath));
	if (!AnimBP)
	{
		SetError(FString::Printf(TEXT("Could not load AnimBlueprint: %s"), *AnimBPPath), OutJson, OutError);
		return;
	}

	UAnimationGraph* AnimGraph = FindAnimGraph(AnimBP);
	if (!AnimGraph)
	{
		SetError(TEXT("Could not find AnimGraph in blueprint."), OutJson, OutError);
		return;
	}

	UAnimGraphNode_SaveCachedPose* SaveNode = NewObject<UAnimGraphNode_SaveCachedPose>(AnimGraph);
	if (!SaveNode)
	{
		SetError(TEXT("Failed to spawn SaveCachedPose node."), OutJson, OutError);
		return;
	}
	SaveNode->CreateNewGuid();
	SaveNode->NodePosX = PosX;
	SaveNode->NodePosY = PosY;
	SaveNode->SetFlags(RF_Transactional);
	SaveNode->AllocateDefaultPins();
	SaveNode->PostPlacedNewNode();
	AnimGraph->Modify();
	AnimGraph->AddNode(SaveNode,  true,  false);

	if (!CacheName.IsEmpty())
	{
		SaveNode->CacheName = CacheName;
		SaveNode->Node.CachePoseName = FName(*CacheName);
		SaveNode->ReconstructNode();
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBP);
	AnimGraph->NotifyGraphChanged();

	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("cache_name"), CacheName);
	if (SaveNode->NodeGuid.IsValid())
	{
		Obj->SetStringField(TEXT("node_guid"), SaveNode->NodeGuid.ToString(EGuidFormats::Digits));
	}
	CompileAnimBlueprintAndReport(AnimBP, Obj);
	UEditorAssetLibrary::SaveAsset(AnimBPPath, false);
	BuildSuccessJson(Obj, OutJson);
}

void HandleUseCachedPose(const FString& AnimBPPath, const FString& CacheName,
	int32 PosX, int32 PosY, FString& OutJson, FString& OutError)
{

	UAnimBlueprint* AnimBP = Cast<UAnimBlueprint>(UEditorAssetLibrary::LoadAsset(AnimBPPath));
	if (!AnimBP)
	{
		SetError(FString::Printf(TEXT("Could not load AnimBlueprint: %s"), *AnimBPPath), OutJson, OutError);
		return;
	}

	UAnimationGraph* AnimGraph = FindAnimGraph(AnimBP);
	if (!AnimGraph)
	{
		SetError(TEXT("Could not find AnimGraph in blueprint."), OutJson, OutError);
		return;
	}

	UAnimGraphNode_UseCachedPose* UseNode = NewObject<UAnimGraphNode_UseCachedPose>(AnimGraph);
	if (!UseNode)
	{
		SetError(TEXT("Failed to spawn UseCachedPose node."), OutJson, OutError);
		return;
	}
	UseNode->CreateNewGuid();
	UseNode->NodePosX = PosX;
	UseNode->NodePosY = PosY;
	UseNode->SetFlags(RF_Transactional);
	UseNode->AllocateDefaultPins();
	UseNode->PostPlacedNewNode();
	AnimGraph->Modify();
	AnimGraph->AddNode(UseNode,  true,  false);

	bool bLinkedSaveNode = false;
	FString LinkedSaveGuid;
	if (!CacheName.IsEmpty())
	{
		if (FStrProperty* NameProp = CastField<FStrProperty>(UAnimGraphNode_UseCachedPose::StaticClass()->FindPropertyByName(TEXT("NameOfCache"))))
		{
			NameProp->SetPropertyValue_InContainer(UseNode, CacheName);
		}
		UseNode->Node.CachePoseName = FName(*CacheName);

		for (UEdGraphNode* GraphNode : AnimGraph->Nodes)
		{
			UAnimGraphNode_SaveCachedPose* SaveNode = Cast<UAnimGraphNode_SaveCachedPose>(GraphNode);
			if (SaveNode && SaveNode->CacheName.Equals(CacheName, ESearchCase::IgnoreCase))
			{
				UseNode->SaveCachedPoseNode = SaveNode;
				bLinkedSaveNode = true;
				LinkedSaveGuid = SaveNode->NodeGuid.ToString(EGuidFormats::Digits);
				break;
			}
		}
		UseNode->ReconstructNode();
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBP);
	AnimGraph->NotifyGraphChanged();

	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("cache_name"), CacheName);
	if (UseNode->NodeGuid.IsValid())
	{
		Obj->SetStringField(TEXT("node_guid"), UseNode->NodeGuid.ToString(EGuidFormats::Digits));
	}
	Obj->SetBoolField(TEXT("linked_save_node"), bLinkedSaveNode);
	if (bLinkedSaveNode)
	{
		Obj->SetStringField(TEXT("save_node_guid"), LinkedSaveGuid);
	}
	CompileAnimBlueprintAndReport(AnimBP, Obj);
	UEditorAssetLibrary::SaveAsset(AnimBPPath, false);
	BuildSuccessJson(Obj, OutJson);
}

void HandleAddBlendByBool(const FString& AnimBPPath, int32 PosX, int32 PosY,
	FString& OutJson, FString& OutError)
{

	UAnimBlueprint* AnimBP = Cast<UAnimBlueprint>(UEditorAssetLibrary::LoadAsset(AnimBPPath));
	if (!AnimBP)
	{
		SetError(FString::Printf(TEXT("Could not load AnimBlueprint: %s"), *AnimBPPath), OutJson, OutError);
		return;
	}

	UAnimationGraph* AnimGraph = FindAnimGraph(AnimBP);
	if (!AnimGraph)
	{
		SetError(TEXT("Could not find AnimGraph in blueprint."), OutJson, OutError);
		return;
	}

	FEdGraphSchemaAction_K2NewNode Action;
	UAnimGraphNode_BlendListByBool* Template = NewObject<UAnimGraphNode_BlendListByBool>(GetTransientPackage());
	Action.NodeTemplate = Template;
	UEdGraphNode* Spawned = Action.PerformAction(AnimGraph, nullptr, FUECPGraphLoc((float)PosX, (float)PosY), false);

	if (!Spawned)
	{
		SetError(TEXT("Failed to spawn BlendListByBool node."), OutJson, OutError);
		return;
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBP);
	AnimGraph->NotifyGraphChanged();

	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("node_type"), TEXT("BlendPosesByBool"));
	Obj->SetStringField(TEXT("note"), TEXT("False pose on input 0, True pose on input 1."));
	CompileAnimBlueprintAndReport(AnimBP, Obj);
	UEditorAssetLibrary::SaveAsset(AnimBPPath, false);
	BuildSuccessJson(Obj, OutJson);
}

void HandleRemoveAnimNotify(const FString& AnimPath, const FString& NotifyName,
	float TimePosition, FString& OutJson, FString& OutError)
{

	UAnimSequenceBase* AnimSeqBase = Cast<UAnimSequenceBase>(UEditorAssetLibrary::LoadAsset(AnimPath));
	if (!AnimSeqBase)
	{
		SetError(FString::Printf(TEXT("Could not load AnimSequenceBase: %s"), *AnimPath), OutJson, OutError);
		return;
	}

	int32 RemovedCount = 0;
	if (!NotifyName.IsEmpty())
	{
		RemovedCount = AnimSeqBase->Notifies.RemoveAll([&](const FAnimNotifyEvent& E) {
			return E.NotifyName.ToString().Equals(NotifyName, ESearchCase::IgnoreCase);
		});
	}
	else
	{
		int32 BestIdx = INDEX_NONE;
		float BestDist = MAX_FLT;
		for (int32 i = 0; i < AnimSeqBase->Notifies.Num(); ++i)
		{
			const float Dist = FMath::Abs(AnimSeqBase->Notifies[i].GetTime() - TimePosition);
			if (Dist < BestDist) { BestDist = Dist; BestIdx = i; }
		}
		if (BestIdx != INDEX_NONE)
		{
			AnimSeqBase->Notifies.RemoveAt(BestIdx);
			RemovedCount = 1;
		}
	}

	if (RemovedCount == 0)
	{
		SetError(FString::Printf(TEXT("No matching notify found in '%s'"), *AnimPath), OutJson, OutError);
		return;
	}

	AnimSeqBase->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(AnimPath, false);

	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetNumberField(TEXT("removed_count"), RemovedCount);
	BuildSuccessJson(Obj, OutJson);
}

void HandleAddBlendByInt(const FString& AnimBPPath, int32 NumPoses, int32 PosX, int32 PosY,
	FString& OutJson, FString& OutError)
{

	UAnimBlueprint* AnimBP = Cast<UAnimBlueprint>(UEditorAssetLibrary::LoadAsset(AnimBPPath));
	if (!AnimBP)
	{
		SetError(FString::Printf(TEXT("Could not load AnimBlueprint: %s"), *AnimBPPath), OutJson, OutError);
		return;
	}

	UAnimationGraph* AnimGraph = FindAnimGraph(AnimBP);
	if (!AnimGraph)
	{
		SetError(TEXT("Could not find AnimGraph in blueprint."), OutJson, OutError);
		return;
	}

	FEdGraphSchemaAction_K2NewNode Action;
	UAnimGraphNode_BlendListByInt* Template = NewObject<UAnimGraphNode_BlendListByInt>(GetTransientPackage());
	Action.NodeTemplate = Template;
	UEdGraphNode* Spawned = Action.PerformAction(AnimGraph, nullptr, FUECPGraphLoc((float)PosX, (float)PosY), false);
	UAnimGraphNode_BlendListByInt* IntNode = Cast<UAnimGraphNode_BlendListByInt>(Spawned);

	if (!IntNode)
	{
		SetError(TEXT("Failed to spawn BlendListByInt node."), OutJson, OutError);
		return;
	}

	const int32 ExtraPoses = FMath::Max(0, NumPoses - 2);
	for (int32 i = 0; i < ExtraPoses; ++i)
		IntNode->AddPinToBlendList();

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBP);
	AnimGraph->NotifyGraphChanged();

	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetNumberField(TEXT("num_poses"), NumPoses);
	CompileAnimBlueprintAndReport(AnimBP, Obj);
	UEditorAssetLibrary::SaveAsset(AnimBPPath, false);
	BuildSuccessJson(Obj, OutJson);
}

void HandleSetBlendspaceAxis(const FString& BlendspacePath, int32 AxisIndex,
	const FString& AxisName, float AxisMin, float AxisMax, int32 GridDivisions,
	FString& OutJson, FString& OutError, int32 SnapToGrid, int32 WrapInput)
{

	UBlendSpace* BS = Cast<UBlendSpace>(UEditorAssetLibrary::LoadAsset(BlendspacePath));
	if (!BS)
	{
		SetError(FString::Printf(TEXT("Could not load BlendSpace: %s"), *BlendspacePath), OutJson, OutError);
		return;
	}

	if (AxisIndex < 0 || AxisIndex > 2)
	{
		SetError(TEXT("axis_index must be 0 (X), 1 (Y), or 2 (Z)."), OutJson, OutError);
		return;
	}
	if (BS->IsA<UBlendSpace1D>() && AxisIndex >= 1)
	{
		SetError(FString::Printf(TEXT("'%s' is a 1D BlendSpace: only axis_index 0 exists."), *BlendspacePath), OutJson, OutError);
		return;
	}
	if (!BS->IsA<UBlendSpace1D>() && AxisIndex >= 2)
	{
		SetError(FString::Printf(TEXT("'%s' is a 2D BlendSpace: only axis_index 0 (X) and 1 (Y) exist."), *BlendspacePath), OutJson, OutError);
		return;
	}

	struct FBlendSpaceAxisAccessor : public UBlendSpace
	{
		static FBlendParameter& GetMutableParam(UBlendSpace* InBS, int32 Idx)
		{
			return static_cast<FBlendSpaceAxisAccessor*>(InBS)->BlendParameters[Idx];
		}
	};

	FBlendParameter& Param = FBlendSpaceAxisAccessor::GetMutableParam(BS, AxisIndex);

	// NaN means "not supplied": keep the current bound for that side.
	const bool bMinGiven = !FMath::IsNaN(AxisMin);
	const bool bMaxGiven = !FMath::IsNaN(AxisMax);
	const float NewMin = bMinGiven ? AxisMin : Param.Min;
	const float NewMax = bMaxGiven ? AxisMax : Param.Max;
	if ((bMinGiven || bMaxGiven) && !(NewMax > NewMin))
	{
		SetError(FString::Printf(TEXT("Degenerate axis range: axis_min (%.3f) must be < axis_max (%.3f)."), NewMin, NewMax), OutJson, OutError);
		return;
	}
	if (GridDivisions == 0 || GridDivisions < -1)
	{
		SetError(TEXT("grid_divisions must be >= 1."), OutJson, OutError);
		return;
	}

	BS->Modify();
	if (!AxisName.IsEmpty())        Param.DisplayName = AxisName;
	if (bMinGiven || bMaxGiven)     { Param.Min = NewMin; Param.Max = NewMax; }
	if (GridDivisions > 0)          Param.GridNum = GridDivisions;
	if (SnapToGrid >= 0)            Param.bSnapToGrid = (SnapToGrid != 0);
	if (WrapInput >= 0)             Param.bWrapInput = (WrapInput != 0);

	// PostEditChange re-validates samples and rebuilds the grid for the new axis settings.
	BS->PostEditChange();
	BS->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(BlendspacePath, false);

	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetNumberField(TEXT("axis_index"), AxisIndex);
	Obj->SetStringField(TEXT("axis_name"), Param.DisplayName);
	Obj->SetNumberField(TEXT("axis_min"), Param.Min);
	Obj->SetNumberField(TEXT("axis_max"), Param.Max);
	Obj->SetNumberField(TEXT("grid_divisions"), Param.GridNum);
	Obj->SetBoolField(TEXT("snap_to_grid"), Param.bSnapToGrid);
	Obj->SetBoolField(TEXT("wrap_input"), Param.bWrapInput);
	BuildSuccessJson(Obj, OutJson);
}

void HandleAddAnimNotifyTrack(const FString& AnimPath, const FString& TrackName,
	FString& OutJson, FString& OutError)
{

	UAnimSequenceBase* AnimSeqBase = Cast<UAnimSequenceBase>(UEditorAssetLibrary::LoadAsset(AnimPath));
	if (!AnimSeqBase)
	{
		SetError(FString::Printf(TEXT("Could not load AnimSequenceBase: %s"), *AnimPath), OutJson, OutError);
		return;
	}

	for (const FAnimNotifyTrack& T : AnimSeqBase->AnimNotifyTracks)
	{
		if (T.TrackName.ToString().Equals(TrackName, ESearchCase::IgnoreCase))
		{
			SetError(FString::Printf(TEXT("Track '%s' already exists."), *TrackName), OutJson, OutError);
			return;
		}
	}

	FAnimNotifyTrack NewTrack;
	NewTrack.TrackName = FName(*TrackName);
	NewTrack.TrackColor = FLinearColor::White;
	AnimSeqBase->AnimNotifyTracks.Add(NewTrack);
	AnimSeqBase->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(AnimPath, false);

	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("track_name"), TrackName);
	Obj->SetNumberField(TEXT("total_tracks"), AnimSeqBase->AnimNotifyTracks.Num());
	BuildSuccessJson(Obj, OutJson);
}

static ERawCurveTrackTypes ParseCurveTypeString(const FString& Type)
{
	if (Type.Equals(TEXT("Transform"), ESearchCase::IgnoreCase)) return ERawCurveTrackTypes::RCT_Transform;
	if (Type.Equals(TEXT("Vector"),    ESearchCase::IgnoreCase)) return ERawCurveTrackTypes::RCT_Vector;
	return ERawCurveTrackTypes::RCT_Float;
}

void HandleSetAnimCurveKey(const FString& AnimPath, const FString& CurveName,
	float KeyTime, float KeyValue, FString& OutJson, FString& OutError, const FString& CurveType)
{

	UAnimSequenceBase* AnimSeqBase = Cast<UAnimSequenceBase>(UEditorAssetLibrary::LoadAsset(AnimPath));
	if (!AnimSeqBase)
	{
		SetError(FString::Printf(TEXT("Could not load AnimSequenceBase: %s"), *AnimPath), OutJson, OutError);
		return;
	}

	const ERawCurveTrackTypes ParsedType = ParseCurveTypeString(CurveType);
	if (ParsedType != ERawCurveTrackTypes::RCT_Float)
	{
		SetError(TEXT("set_anim_curve_keys writes a single float value — only Float curves are supported. "
			"Transform and Vector curves use multi-channel keys; that path is not yet exposed."),
			OutJson, OutError);
		return;
	}

	FAnimationCurveIdentifier CurveId(FName(*CurveName), ParsedType);
	IAnimationDataController& Controller = AnimSeqBase->GetController();
	Controller.OpenBracket(FText::FromString(TEXT("Set Curve Key")));
	const bool bSet = Controller.SetCurveKey(CurveId, FRichCurveKey(KeyTime, KeyValue));
	Controller.CloseBracket();

	if (!bSet)
	{
		SetError(FString::Printf(TEXT("Could not set key on curve '%s'. Ensure the curve exists (use add_anim_curve first)."), *CurveName), OutJson, OutError);
		return;
	}

	AnimSeqBase->MarkPackageDirty();

	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("curve_name"), CurveName);
	Obj->SetNumberField(TEXT("key_time"), KeyTime);
	Obj->SetNumberField(TEXT("key_value"), KeyValue);
	BuildSuccessJson(Obj, OutJson);
}

void HandleGetRetargeterSummary(const FString& RetargeterPath, FString& OutJson, FString& OutError)
{

	UIKRetargeter* Retargeter = Cast<UIKRetargeter>(UEditorAssetLibrary::LoadAsset(RetargeterPath));
	if (!Retargeter)
	{
		SetError(FString::Printf(TEXT("Could not load IKRetargeter: %s"), *RetargeterPath), OutJson, OutError);
		return;
	}

	TSharedPtr<FJsonObject> Root = MakeShareable(new FJsonObject);
	Root->SetBoolField(TEXT("success"), true);
	Root->SetStringField(TEXT("asset_path"), RetargeterPath);

	if (const UIKRigDefinition* SourceRig = Retargeter->GetIKRig(ERetargetSourceOrTarget::Source))
		Root->SetStringField(TEXT("source_ik_rig"), SourceRig->GetPathName());
	if (const UIKRigDefinition* TargetRig = Retargeter->GetIKRig(ERetargetSourceOrTarget::Target))
		Root->SetStringField(TEXT("target_ik_rig"), TargetRig->GetPathName());

	Root->SetStringField(TEXT("source_pose"), Retargeter->GetCurrentRetargetPoseName(ERetargetSourceOrTarget::Source).ToString());
	Root->SetStringField(TEXT("target_pose"), Retargeter->GetCurrentRetargetPoseName(ERetargetSourceOrTarget::Target).ToString());

	TArray<TSharedPtr<FJsonValue>> MappingsArr;
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	for (const TObjectPtr<URetargetChainSettings>& ChainSettings : Retargeter->GetAllChainSettings())
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
	{
		if (!ChainSettings) continue;
		TSharedPtr<FJsonObject> MObj = MakeShareable(new FJsonObject);
		MObj->SetStringField(TEXT("target_chain"), ChainSettings->TargetChain.ToString());
		MObj->SetStringField(TEXT("source_chain"), ChainSettings->SourceChain.ToString());
		MappingsArr.Add(MakeShareable(new FJsonValueObject(MObj)));
	}
	Root->SetArrayField(TEXT("chain_mappings"), MappingsArr);
	BuildSuccessJson(Root, OutJson);
}

void HandleCreateAnimBlueprintFromParent(const FString& Name, const FString& SavePath,
	const FString& ParentPath, const FString& SkeletonPathOverride,
	FString& OutJson, FString& OutError)
{
	if (Name.IsEmpty()) { SetError(TEXT("name is required"), OutJson, OutError); return; }

	UAnimBlueprint* ParentBP = Cast<UAnimBlueprint>(UEditorAssetLibrary::LoadAsset(ParentPath));
	if (!ParentBP || !ParentBP->GeneratedClass)
	{
		SetError(FString::Printf(TEXT("Could not load parent AnimBlueprint: %s"), *ParentPath), OutJson, OutError);
		return;
	}

	UAnimBlueprintFactory* Factory = NewObject<UAnimBlueprintFactory>();
	Factory->BlueprintType = BPTYPE_Normal;
	Factory->ParentClass = Cast<UClass>(ParentBP->GeneratedClass);
	Factory->TargetSkeleton = ParentBP->TargetSkeleton;

	if (!SkeletonPathOverride.IsEmpty())
	{
		USkeleton* SK = Cast<USkeleton>(UEditorAssetLibrary::LoadAsset(SkeletonPathOverride));
		if (SK) Factory->TargetSkeleton = SK;
	}

	const FString PackagePath = SavePath.IsEmpty() ? TEXT("/Game") : SavePath;
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	UObject* Asset = AssetTools.CreateAsset(Name, PackagePath, UAnimBlueprint::StaticClass(), Factory);
	UAnimBlueprint* NewBP = Cast<UAnimBlueprint>(Asset);
	if (!NewBP)
	{
		SetError(TEXT("Failed to create child AnimBlueprint."), OutJson, OutError);
		return;
	}

	const FString AssetPath = FString::Printf(TEXT("%s/%s"), *PackagePath, *Name);
	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("asset_path"), AssetPath);
	Obj->SetStringField(TEXT("parent_path"), ParentPath);
	BuildSuccessJson(Obj, OutJson);
}

void HandleRemoveAnimCurve(const FString& AnimPath, const FString& CurveName,
	FString& OutJson, FString& OutError, const FString& CurveType)
{

	UAnimSequenceBase* AnimSeqBase = Cast<UAnimSequenceBase>(UEditorAssetLibrary::LoadAsset(AnimPath));
	if (!AnimSeqBase)
	{
		SetError(FString::Printf(TEXT("Could not load AnimSequenceBase: %s"), *AnimPath), OutJson, OutError);
		return;
	}

	FAnimationCurveIdentifier CurveId(FName(*CurveName), ParseCurveTypeString(CurveType));
	IAnimationDataController& Controller = AnimSeqBase->GetController();
	Controller.OpenBracket(FText::FromString(TEXT("Remove Anim Curve")));
	const bool bRemoved = Controller.RemoveCurve(CurveId);
	Controller.CloseBracket();

	if (!bRemoved)
	{
		SetError(FString::Printf(TEXT("Curve '%s' not found on '%s'."), *CurveName, *AnimPath), OutJson, OutError);
		return;
	}

	AnimSeqBase->MarkPackageDirty();

	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("removed_curve"), CurveName);
	BuildSuccessJson(Obj, OutJson);
}

void HandleAddSubAnimInstance(const FString& AnimBPPath, const FString& LinkedBPPath,
	int32 PosX, int32 PosY, FString& OutJson, FString& OutError)
{

	UAnimBlueprint* AnimBP = Cast<UAnimBlueprint>(UEditorAssetLibrary::LoadAsset(AnimBPPath));
	if (!AnimBP)
	{
		SetError(FString::Printf(TEXT("Could not load AnimBlueprint: %s"), *AnimBPPath), OutJson, OutError);
		return;
	}

	UAnimationGraph* AnimGraph = FindAnimGraph(AnimBP);
	if (!AnimGraph)
	{
		SetError(TEXT("Could not find AnimGraph in blueprint."), OutJson, OutError);
		return;
	}

	FEdGraphSchemaAction_K2NewNode Action;
	UAnimGraphNode_LinkedAnimGraph* Template = NewObject<UAnimGraphNode_LinkedAnimGraph>(GetTransientPackage());
	Action.NodeTemplate = Template;
	UEdGraphNode* Spawned = Action.PerformAction(AnimGraph, nullptr, FUECPGraphLoc((float)PosX, (float)PosY), false);
	UAnimGraphNode_LinkedAnimGraph* LinkedNode = Cast<UAnimGraphNode_LinkedAnimGraph>(Spawned);

	if (!LinkedNode)
	{
		SetError(TEXT("Failed to spawn LinkedAnimGraph node."), OutJson, OutError);
		return;
	}

	if (!LinkedBPPath.IsEmpty())
	{
		UAnimBlueprint* TargetBP = Cast<UAnimBlueprint>(UEditorAssetLibrary::LoadAsset(LinkedBPPath));
		if (TargetBP && TargetBP->GeneratedClass)
			LinkedNode->Node.InstanceClass = TSubclassOf<UAnimInstance>(Cast<UClass>(TargetBP->GeneratedClass));
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBP);
	AnimGraph->NotifyGraphChanged();

	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("linked_blueprint"), LinkedBPPath);
	CompileAnimBlueprintAndReport(AnimBP, Obj);
	UEditorAssetLibrary::SaveAsset(AnimBPPath, false);
	BuildSuccessJson(Obj, OutJson);
}

void HandleRemoveBlendspaceSample(const FString& BlendspacePath, float SampleX, float SampleY,
	FString& OutJson, FString& OutError)
{

	UBlendSpace* BS = Cast<UBlendSpace>(UEditorAssetLibrary::LoadAsset(BlendspacePath));
	if (!BS)
	{
		SetError(FString::Printf(TEXT("Could not load BlendSpace: %s"), *BlendspacePath), OutJson, OutError);
		return;
	}

	struct FBSAccessor : public UBlendSpace
	{
		static TArray<FBlendSample>& Get(UBlendSpace* InBS) { return static_cast<FBSAccessor*>(InBS)->SampleData; }
	};

	TArray<FBlendSample>& Samples = FBSAccessor::Get(BS);
	int32 BestIdx = INDEX_NONE;
	float BestDist = MAX_FLT;
	for (int32 i = 0; i < Samples.Num(); ++i)
	{
		const FVector& SV = Samples[i].SampleValue;
		const float Dist = FMath::Abs(SV.X - SampleX) + FMath::Abs(SV.Y - SampleY);
		if (Dist < BestDist) { BestDist = Dist; BestIdx = i; }
	}

	if (BestIdx == INDEX_NONE)
	{
		SetError(TEXT("No samples found in BlendSpace."), OutJson, OutError);
		return;
	}

	const FVector RemovedSV = Samples[BestIdx].SampleValue;
	Samples.RemoveAt(BestIdx);
	BS->ValidateSampleData();
	BS->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(BlendspacePath, false);

	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetNumberField(TEXT("removed_sample_x"), RemovedSV.X);
	Obj->SetNumberField(TEXT("removed_sample_y"), RemovedSV.Y);
	Obj->SetNumberField(TEXT("remaining_samples"), Samples.Num());
	BuildSuccessJson(Obj, OutJson);
}

void HandleRemoveMontageSection(const FString& MontagePath, const FString& SectionName,
	FString& OutJson, FString& OutError)
{

	UAnimMontage* Montage = Cast<UAnimMontage>(UEditorAssetLibrary::LoadAsset(MontagePath));
	if (!Montage)
	{
		SetError(FString::Printf(TEXT("Could not load AnimMontage: %s"), *MontagePath), OutJson, OutError);
		return;
	}

	const FName NameToRemove(*SectionName);
	const int32 Removed = Montage->CompositeSections.RemoveAll([&](const FCompositeSection& S) {
		return S.SectionName == NameToRemove;
	});

	if (Removed == 0)
	{
		SetError(FString::Printf(TEXT("Section '%s' not found in montage."), *SectionName), OutJson, OutError);
		return;
	}

	for (FCompositeSection& S : Montage->CompositeSections)
	{
		if (S.NextSectionName == NameToRemove)
			S.NextSectionName = NAME_None;
	}

	Montage->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(MontagePath, false);

	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("removed_section"), SectionName);
	BuildSuccessJson(Obj, OutJson);
}

void HandleRenameMontageSection(const FString& MontagePath, const FString& OldName, const FString& NewName,
	FString& OutJson, FString& OutError)
{

	UAnimMontage* Montage = Cast<UAnimMontage>(UEditorAssetLibrary::LoadAsset(MontagePath));
	if (!Montage)
	{
		SetError(FString::Printf(TEXT("Could not load AnimMontage: %s"), *MontagePath), OutJson, OutError);
		return;
	}

	const FName OldFName(*OldName);
	const FName NewFName(*NewName);
	bool bFound = false;

	for (FCompositeSection& S : Montage->CompositeSections)
	{
		if (S.SectionName == OldFName)
		{
			S.SectionName = NewFName;
			bFound = true;
		}
		if (S.NextSectionName == OldFName)
			S.NextSectionName = NewFName;
	}

	if (!bFound)
	{
		SetError(FString::Printf(TEXT("Section '%s' not found in montage."), *OldName), OutJson, OutError);
		return;
	}

	Montage->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(MontagePath, false);

	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("old_name"), OldName);
	Obj->SetStringField(TEXT("new_name"), NewName);
	BuildSuccessJson(Obj, OutJson);
}

void HandleAddSkeletonSocket(const FString& SkeletonPath, const FString& SocketName,
	const FString& BoneName, const FString& RelLocation, const FString& RelRotation,
	const FString& RelScale, FString& OutJson, FString& OutError)
{

	USkeleton* Skeleton = Cast<USkeleton>(UEditorAssetLibrary::LoadAsset(SkeletonPath));
	if (!Skeleton)
	{
		SetError(FString::Printf(TEXT("Could not load Skeleton: %s"), *SkeletonPath), OutJson, OutError);
		return;
	}

	USkeletalMeshSocket* Socket = NewObject<USkeletalMeshSocket>(Skeleton);
	Socket->SocketName = FName(*SocketName);
	Socket->BoneName   = FName(*BoneName);

	auto ParseVec = [](const FString& Str, FVector& Out)
	{
		TArray<FString> Parts;
		Str.ParseIntoArray(Parts, TEXT(","), true);
		if (Parts.Num() == 3)
		{
			Out.X = FCString::Atof(*Parts[0]);
			Out.Y = FCString::Atof(*Parts[1]);
			Out.Z = FCString::Atof(*Parts[2]);
		}
	};
	auto ParseRot = [](const FString& Str, FRotator& Out)
	{
		TArray<FString> Parts;
		Str.ParseIntoArray(Parts, TEXT(","), true);
		if (Parts.Num() == 3)
		{
			Out.Pitch = FCString::Atof(*Parts[0]);
			Out.Yaw   = FCString::Atof(*Parts[1]);
			Out.Roll  = FCString::Atof(*Parts[2]);
		}
	};

	FVector Loc(0.f), Scale(1.f);
	FRotator Rot(0.f);
	if (!RelLocation.IsEmpty()) ParseVec(RelLocation, Loc);
	if (!RelRotation.IsEmpty()) ParseRot(RelRotation, Rot);
	if (!RelScale.IsEmpty())    ParseVec(RelScale,    Scale);

	Socket->RelativeLocation = Loc;
	Socket->RelativeRotation = Rot;
	Socket->RelativeScale    = Scale;

	Skeleton->Sockets.Add(Socket);
	Skeleton->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(SkeletonPath, false);

	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("socket_name"), SocketName);
	Obj->SetStringField(TEXT("bone_name"), BoneName);
	BuildSuccessJson(Obj, OutJson);
}

void HandleGetSkeletonSockets(const FString& SkeletonPath, FString& OutJson, FString& OutError)
{

	USkeleton* Skeleton = Cast<USkeleton>(UEditorAssetLibrary::LoadAsset(SkeletonPath));
	if (!Skeleton)
	{
		SetError(FString::Printf(TEXT("Could not load Skeleton: %s"), *SkeletonPath), OutJson, OutError);
		return;
	}

	TArray<TSharedPtr<FJsonValue>> SocketsArr;
	for (const USkeletalMeshSocket* Socket : Skeleton->Sockets)
	{
		if (!Socket) continue;
		TSharedPtr<FJsonObject> SObj = MakeShareable(new FJsonObject);
		SObj->SetStringField(TEXT("socket_name"), Socket->SocketName.ToString());
		SObj->SetStringField(TEXT("bone_name"),   Socket->BoneName.ToString());
		SObj->SetStringField(TEXT("relative_location"),
			FString::Printf(TEXT("%.2f,%.2f,%.2f"), Socket->RelativeLocation.X, Socket->RelativeLocation.Y, Socket->RelativeLocation.Z));
		SObj->SetStringField(TEXT("relative_rotation"),
			FString::Printf(TEXT("%.2f,%.2f,%.2f"), Socket->RelativeRotation.Pitch, Socket->RelativeRotation.Yaw, Socket->RelativeRotation.Roll));
		SObj->SetStringField(TEXT("relative_scale"),
			FString::Printf(TEXT("%.2f,%.2f,%.2f"), Socket->RelativeScale.X, Socket->RelativeScale.Y, Socket->RelativeScale.Z));
		SocketsArr.Add(MakeShareable(new FJsonValueObject(SObj)));
	}

	TSharedPtr<FJsonObject> Root = MakeShareable(new FJsonObject);
	Root->SetBoolField(TEXT("success"), true);
	Root->SetStringField(TEXT("skeleton_path"), SkeletonPath);
	Root->SetNumberField(TEXT("socket_count"), Skeleton->Sockets.Num());
	Root->SetArrayField(TEXT("sockets"), SocketsArr);
	BuildSuccessJson(Root, OutJson);
}

void HandleRemoveIKGoal(const FString& IKRigPath, const FString& GoalName,
	FString& OutJson, FString& OutError)
{

	UIKRigDefinition* IKRig = Cast<UIKRigDefinition>(UEditorAssetLibrary::LoadAsset(IKRigPath));
	if (!IKRig)
	{
		SetError(FString::Printf(TEXT("Could not load IKRig: %s"), *IKRigPath), OutJson, OutError);
		return;
	}

	UIKRigController* Controller = UIKRigController::GetController(IKRig);
	if (!Controller)
	{
		SetError(TEXT("Could not get IKRig controller."), OutJson, OutError);
		return;
	}

	const bool bRemoved = Controller->RemoveGoal(FName(*GoalName));
	if (!bRemoved)
	{
		SetError(FString::Printf(TEXT("Goal '%s' not found in IKRig."), *GoalName), OutJson, OutError);
		return;
	}

	IKRig->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(IKRigPath, false);

	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("removed_goal"), GoalName);
	BuildSuccessJson(Obj, OutJson);
}

void HandleRemoveRetargetChain(const FString& IKRigPath, const FString& ChainName,
	FString& OutJson, FString& OutError)
{

	UIKRigDefinition* IKRig = Cast<UIKRigDefinition>(UEditorAssetLibrary::LoadAsset(IKRigPath));
	if (!IKRig)
	{
		SetError(FString::Printf(TEXT("Could not load IKRig: %s"), *IKRigPath), OutJson, OutError);
		return;
	}

	UIKRigController* Controller = UIKRigController::GetController(IKRig);
	if (!Controller)
	{
		SetError(TEXT("Could not get IKRig controller."), OutJson, OutError);
		return;
	}

	const bool bRemoved = Controller->RemoveRetargetChain(FName(*ChainName));
	if (!bRemoved)
	{
		SetError(FString::Printf(TEXT("Chain '%s' not found in IKRig."), *ChainName), OutJson, OutError);
		return;
	}

	IKRig->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(IKRigPath, false);

	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("removed_chain"), ChainName);
	BuildSuccessJson(Obj, OutJson);
}

void HandleAddModifyBone(const FString& AnimBPPath, const FString& BoneName,
	int32 PosX, int32 PosY, FString& OutJson, FString& OutError)
{
	UAnimBlueprint* AnimBP = LoadObject<UAnimBlueprint>(nullptr, *AnimBPPath);
	if (!AnimBP) { SetError(FString::Printf(TEXT("AnimBlueprint not found: %s"), *AnimBPPath), OutJson, OutError); return; }
	UEdGraph* AnimGraph = FindAnimGraph(AnimBP);
	if (!AnimGraph) { SetError(TEXT("AnimGraph not found."), OutJson, OutError); return; }

	UAnimGraphNode_ModifyBone* NodeTemplate = NewObject<UAnimGraphNode_ModifyBone>(GetTransientPackage());
	if (!BoneName.IsEmpty())
		NodeTemplate->Node.BoneToModify.BoneName = FName(*BoneName);

	FEdGraphSchemaAction_K2NewNode Action;
	Action.NodeTemplate = NodeTemplate;
	UEdGraphNode* SpawnedNode = Action.PerformAction(AnimGraph, nullptr, FUECPGraphLoc((float)PosX, (float)PosY), false);
	if (!SpawnedNode) { SetError(TEXT("Failed to spawn ModifyBone node."), OutJson, OutError); return; }

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBP);
	AnimGraph->NotifyGraphChanged();
	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("node_guid"), SpawnedNode->NodeGuid.ToString());
	Obj->SetStringField(TEXT("bone_name"), BoneName);
	CompileAnimBlueprintAndReport(AnimBP, Obj);
	UEditorAssetLibrary::SaveAsset(AnimBPPath, false);
	BuildSuccessJson(Obj, OutJson);
}

void HandleAddCopyBone(const FString& AnimBPPath, const FString& SourceBone, const FString& TargetBone,
	int32 PosX, int32 PosY, FString& OutJson, FString& OutError)
{
	UAnimBlueprint* AnimBP = LoadObject<UAnimBlueprint>(nullptr, *AnimBPPath);
	if (!AnimBP) { SetError(FString::Printf(TEXT("AnimBlueprint not found: %s"), *AnimBPPath), OutJson, OutError); return; }
	UEdGraph* AnimGraph = FindAnimGraph(AnimBP);
	if (!AnimGraph) { SetError(TEXT("AnimGraph not found."), OutJson, OutError); return; }

	UAnimGraphNode_CopyBone* NodeTemplate = NewObject<UAnimGraphNode_CopyBone>(GetTransientPackage());
	if (!SourceBone.IsEmpty()) NodeTemplate->Node.SourceBone.BoneName = FName(*SourceBone);
	if (!TargetBone.IsEmpty()) NodeTemplate->Node.TargetBone.BoneName = FName(*TargetBone);

	FEdGraphSchemaAction_K2NewNode Action;
	Action.NodeTemplate = NodeTemplate;
	UEdGraphNode* SpawnedNode = Action.PerformAction(AnimGraph, nullptr, FUECPGraphLoc((float)PosX, (float)PosY), false);
	if (!SpawnedNode) { SetError(TEXT("Failed to spawn CopyBone node."), OutJson, OutError); return; }

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBP);
	AnimGraph->NotifyGraphChanged();
	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("node_guid"), SpawnedNode->NodeGuid.ToString());
	Obj->SetStringField(TEXT("source_bone"), SourceBone);
	Obj->SetStringField(TEXT("target_bone"), TargetBone);
	CompileAnimBlueprintAndReport(AnimBP, Obj);
	UEditorAssetLibrary::SaveAsset(AnimBPPath, false);
	BuildSuccessJson(Obj, OutJson);
}

void HandleAddLookAt(const FString& AnimBPPath, const FString& BoneName, const FString& LookAtBone,
	int32 PosX, int32 PosY, FString& OutJson, FString& OutError)
{
	UAnimBlueprint* AnimBP = LoadObject<UAnimBlueprint>(nullptr, *AnimBPPath);
	if (!AnimBP) { SetError(FString::Printf(TEXT("AnimBlueprint not found: %s"), *AnimBPPath), OutJson, OutError); return; }
	UEdGraph* AnimGraph = FindAnimGraph(AnimBP);
	if (!AnimGraph) { SetError(TEXT("AnimGraph not found."), OutJson, OutError); return; }

	UAnimGraphNode_LookAt* NodeTemplate = NewObject<UAnimGraphNode_LookAt>(GetTransientPackage());
	if (!BoneName.IsEmpty())   NodeTemplate->Node.BoneToModify.BoneName = FName(*BoneName);
	if (!LookAtBone.IsEmpty()) NodeTemplate->Node.LookAtTarget.BoneReference.BoneName = FName(*LookAtBone);

	FEdGraphSchemaAction_K2NewNode Action;
	Action.NodeTemplate = NodeTemplate;
	UEdGraphNode* SpawnedNode = Action.PerformAction(AnimGraph, nullptr, FUECPGraphLoc((float)PosX, (float)PosY), false);
	if (!SpawnedNode) { SetError(TEXT("Failed to spawn LookAt node."), OutJson, OutError); return; }

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBP);
	AnimGraph->NotifyGraphChanged();
	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("node_guid"), SpawnedNode->NodeGuid.ToString());
	Obj->SetStringField(TEXT("bone_name"), BoneName);
	Obj->SetStringField(TEXT("look_at_bone"), LookAtBone);
	CompileAnimBlueprintAndReport(AnimBP, Obj);
	UEditorAssetLibrary::SaveAsset(AnimBPPath, false);
	BuildSuccessJson(Obj, OutJson);
}

void HandleAddTwoBoneIK(const FString& AnimBPPath, const FString& IKBone,
	const FString& EffectorBone, const FString& JointTargetBone,
	int32 PosX, int32 PosY, FString& OutJson, FString& OutError)
{
	UAnimBlueprint* AnimBP = LoadObject<UAnimBlueprint>(nullptr, *AnimBPPath);
	if (!AnimBP) { SetError(FString::Printf(TEXT("AnimBlueprint not found: %s"), *AnimBPPath), OutJson, OutError); return; }
	UEdGraph* AnimGraph = FindAnimGraph(AnimBP);
	if (!AnimGraph) { SetError(TEXT("AnimGraph not found."), OutJson, OutError); return; }

	UAnimGraphNode_TwoBoneIK* NodeTemplate = NewObject<UAnimGraphNode_TwoBoneIK>(GetTransientPackage());
	if (!IKBone.IsEmpty())          NodeTemplate->Node.IKBone.BoneName                        = FName(*IKBone);
	if (!EffectorBone.IsEmpty())    NodeTemplate->Node.EffectorTarget.BoneReference.BoneName   = FName(*EffectorBone);
	if (!JointTargetBone.IsEmpty()) NodeTemplate->Node.JointTarget.BoneReference.BoneName      = FName(*JointTargetBone);

	FEdGraphSchemaAction_K2NewNode Action;
	Action.NodeTemplate = NodeTemplate;
	UEdGraphNode* SpawnedNode = Action.PerformAction(AnimGraph, nullptr, FUECPGraphLoc((float)PosX, (float)PosY), false);
	if (!SpawnedNode) { SetError(TEXT("Failed to spawn TwoBoneIK node."), OutJson, OutError); return; }

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBP);
	AnimGraph->NotifyGraphChanged();
	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("node_guid"), SpawnedNode->NodeGuid.ToString());
	Obj->SetStringField(TEXT("ik_bone"), IKBone);
	CompileAnimBlueprintAndReport(AnimBP, Obj);
	UEditorAssetLibrary::SaveAsset(AnimBPPath, false);
	BuildSuccessJson(Obj, OutJson);
}

void HandleAddApplyAdditive(const FString& AnimBPPath, int32 PosX, int32 PosY,
	FString& OutJson, FString& OutError)
{
	UAnimBlueprint* AnimBP = LoadObject<UAnimBlueprint>(nullptr, *AnimBPPath);
	if (!AnimBP) { SetError(FString::Printf(TEXT("AnimBlueprint not found: %s"), *AnimBPPath), OutJson, OutError); return; }
	UEdGraph* AnimGraph = FindAnimGraph(AnimBP);
	if (!AnimGraph) { SetError(TEXT("AnimGraph not found."), OutJson, OutError); return; }

	UAnimGraphNode_ApplyAdditive* NodeTemplate = NewObject<UAnimGraphNode_ApplyAdditive>(GetTransientPackage());
	FEdGraphSchemaAction_K2NewNode Action;
	Action.NodeTemplate = NodeTemplate;
	UEdGraphNode* SpawnedNode = Action.PerformAction(AnimGraph, nullptr, FUECPGraphLoc((float)PosX, (float)PosY), false);
	if (!SpawnedNode) { SetError(TEXT("Failed to spawn ApplyAdditive node."), OutJson, OutError); return; }

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBP);
	AnimGraph->NotifyGraphChanged();
	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("node_guid"), SpawnedNode->NodeGuid.ToString());
	CompileAnimBlueprintAndReport(AnimBP, Obj);
	UEditorAssetLibrary::SaveAsset(AnimBPPath, false);
	BuildSuccessJson(Obj, OutJson);
}

void HandleAddMakeDynamicAdditive(const FString& AnimBPPath, int32 PosX, int32 PosY,
	FString& OutJson, FString& OutError)
{
	UAnimBlueprint* AnimBP = LoadObject<UAnimBlueprint>(nullptr, *AnimBPPath);
	if (!AnimBP) { SetError(FString::Printf(TEXT("AnimBlueprint not found: %s"), *AnimBPPath), OutJson, OutError); return; }
	UEdGraph* AnimGraph = FindAnimGraph(AnimBP);
	if (!AnimGraph) { SetError(TEXT("AnimGraph not found."), OutJson, OutError); return; }

	UAnimGraphNode_MakeDynamicAdditive* NodeTemplate = NewObject<UAnimGraphNode_MakeDynamicAdditive>(GetTransientPackage());
	FEdGraphSchemaAction_K2NewNode Action;
	Action.NodeTemplate = NodeTemplate;
	UEdGraphNode* SpawnedNode = Action.PerformAction(AnimGraph, nullptr, FUECPGraphLoc((float)PosX, (float)PosY), false);
	if (!SpawnedNode) { SetError(TEXT("Failed to spawn MakeDynamicAdditive node."), OutJson, OutError); return; }

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBP);
	AnimGraph->NotifyGraphChanged();
	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("node_guid"), SpawnedNode->NodeGuid.ToString());
	CompileAnimBlueprintAndReport(AnimBP, Obj);
	UEditorAssetLibrary::SaveAsset(AnimBPPath, false);
	BuildSuccessJson(Obj, OutJson);
}

void HandleAddInertialization(const FString& AnimBPPath, int32 PosX, int32 PosY,
	FString& OutJson, FString& OutError)
{
	UAnimBlueprint* AnimBP = LoadObject<UAnimBlueprint>(nullptr, *AnimBPPath);
	if (!AnimBP) { SetError(FString::Printf(TEXT("AnimBlueprint not found: %s"), *AnimBPPath), OutJson, OutError); return; }
	UEdGraph* AnimGraph = FindAnimGraph(AnimBP);
	if (!AnimGraph) { SetError(TEXT("AnimGraph not found."), OutJson, OutError); return; }

	UAnimGraphNode_Inertialization* NodeTemplate = NewObject<UAnimGraphNode_Inertialization>(GetTransientPackage());
	FEdGraphSchemaAction_K2NewNode Action;
	Action.NodeTemplate = NodeTemplate;
	UEdGraphNode* SpawnedNode = Action.PerformAction(AnimGraph, nullptr, FUECPGraphLoc((float)PosX, (float)PosY), false);
	if (!SpawnedNode) { SetError(TEXT("Failed to spawn Inertialization node."), OutJson, OutError); return; }

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBP);
	AnimGraph->NotifyGraphChanged();
	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("node_guid"), SpawnedNode->NodeGuid.ToString());
	CompileAnimBlueprintAndReport(AnimBP, Obj);
	UEditorAssetLibrary::SaveAsset(AnimBPPath, false);
	BuildSuccessJson(Obj, OutJson);
}

void HandleAddBlendByEnum(const FString& AnimBPPath, const FString& EnumClass,
	int32 PosX, int32 PosY, FString& OutJson, FString& OutError)
{
	UAnimBlueprint* AnimBP = LoadObject<UAnimBlueprint>(nullptr, *AnimBPPath);
	if (!AnimBP) { SetError(FString::Printf(TEXT("AnimBlueprint not found: %s"), *AnimBPPath), OutJson, OutError); return; }
	UEdGraph* AnimGraph = FindAnimGraph(AnimBP);
	if (!AnimGraph) { SetError(TEXT("AnimGraph not found."), OutJson, OutError); return; }

	UAnimGraphNode_BlendListByEnum* NodeTemplate = NewObject<UAnimGraphNode_BlendListByEnum>(GetTransientPackage());

	FEdGraphSchemaAction_K2NewNode Action;
	Action.NodeTemplate = NodeTemplate;
	UEdGraphNode* SpawnedNode = Action.PerformAction(AnimGraph, nullptr, FUECPGraphLoc((float)PosX, (float)PosY), false);
	if (!SpawnedNode) { SetError(TEXT("Failed to spawn BlendListByEnum node."), OutJson, OutError); return; }

	UEnum* ResolvedEnum = nullptr;
	if (!EnumClass.IsEmpty())
	{
		ResolvedEnum = FindObject<UEnum>(nullptr, *EnumClass);
		if (!ResolvedEnum)
			ResolvedEnum = FindFirstObject<UEnum>(*EnumClass, EFindFirstObjectOptions::None, ELogVerbosity::NoLogging);

		if (!ResolvedEnum)
		{
			SetError(FString::Printf(
				TEXT("Could not resolve enum_class '%s'. Pass a fully-qualified path (e.g. '/Script/Engine.EMyEnum' or '/Game/Path/EBP_State.EBP_State') or the exact enum name."),
				*EnumClass), OutJson, OutError);
			return;
		}

		if (UAnimGraphNode_BlendListByEnum* EnumNode = Cast<UAnimGraphNode_BlendListByEnum>(SpawnedNode))
			EnumNode->ReloadEnum(ResolvedEnum);
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBP);
	AnimGraph->NotifyGraphChanged();
	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("node_guid"), SpawnedNode->NodeGuid.ToString());
	Obj->SetStringField(TEXT("enum_class"), ResolvedEnum ? ResolvedEnum->GetPathName() : EnumClass);
	CompileAnimBlueprintAndReport(AnimBP, Obj);
	UEditorAssetLibrary::SaveAsset(AnimBPPath, false);
	BuildSuccessJson(Obj, OutJson);
}

void HandleAddSequenceEvaluator(const FString& AnimBPPath, const FString& AnimationPath,
	int32 PosX, int32 PosY, FString& OutJson, FString& OutError)
{
	UAnimBlueprint* AnimBP = LoadObject<UAnimBlueprint>(nullptr, *AnimBPPath);
	if (!AnimBP) { SetError(FString::Printf(TEXT("AnimBlueprint not found: %s"), *AnimBPPath), OutJson, OutError); return; }
	UEdGraph* AnimGraph = FindAnimGraph(AnimBP);
	if (!AnimGraph) { SetError(TEXT("AnimGraph not found."), OutJson, OutError); return; }

	UAnimGraphNode_SequenceEvaluator* NodeTemplate = NewObject<UAnimGraphNode_SequenceEvaluator>(GetTransientPackage());
	if (!AnimationPath.IsEmpty())
	{
		if (UAnimSequence* Seq = LoadObject<UAnimSequence>(nullptr, *AnimationPath))
			NodeTemplate->Node.SetSequence(Seq);
	}

	FEdGraphSchemaAction_K2NewNode Action;
	Action.NodeTemplate = NodeTemplate;
	UEdGraphNode* SpawnedNode = Action.PerformAction(AnimGraph, nullptr, FUECPGraphLoc((float)PosX, (float)PosY), false);
	if (!SpawnedNode) { SetError(TEXT("Failed to spawn SequenceEvaluator node."), OutJson, OutError); return; }

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBP);
	AnimGraph->NotifyGraphChanged();
	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("node_guid"), SpawnedNode->NodeGuid.ToString());
	Obj->SetStringField(TEXT("animation_path"), AnimationPath);
	CompileAnimBlueprintAndReport(AnimBP, Obj);
	UEditorAssetLibrary::SaveAsset(AnimBPPath, false);
	BuildSuccessJson(Obj, OutJson);
}

// Sequence *Player* (time-driven playback) as opposed to the Sequence *Evaluator* (explicit time).
static void HandleAddSequencePlayer(const FString& AnimBPPath, const FString& AnimationPath,
	int32 PosX, int32 PosY, FString& OutJson, FString& OutError)
{
	UAnimBlueprint* AnimBP = LoadObject<UAnimBlueprint>(nullptr, *AnimBPPath);
	if (!AnimBP) { SetError(FString::Printf(TEXT("AnimBlueprint not found: %s"), *AnimBPPath), OutJson, OutError); return; }
	UEdGraph* AnimGraph = FindAnimGraph(AnimBP);
	if (!AnimGraph) { SetError(TEXT("AnimGraph not found."), OutJson, OutError); return; }

	UAnimGraphNode_SequencePlayer* NodeTemplate = NewObject<UAnimGraphNode_SequencePlayer>(GetTransientPackage());
	if (!AnimationPath.IsEmpty())
	{
		UAnimSequenceBase* Seq = LoadObject<UAnimSequenceBase>(nullptr, *AnimationPath);
		if (!Seq)
		{
			SetError(FString::Printf(TEXT("Could not load AnimSequence for sequence_player: %s"), *AnimationPath), OutJson, OutError);
			return;
		}
		if (AnimBP->TargetSkeleton && Seq->GetSkeleton() && AnimBP->TargetSkeleton != Seq->GetSkeleton())
		{
			SetError(FString::Printf(TEXT("Skeleton mismatch: AnimBP targets '%s', animation targets '%s'."),
				*AnimBP->TargetSkeleton->GetPathName(), *Seq->GetSkeleton()->GetPathName()), OutJson, OutError);
			return;
		}
		NodeTemplate->Node.SetSequence(Seq);
	}

	FEdGraphSchemaAction_K2NewNode Action;
	Action.NodeTemplate = NodeTemplate;
	UEdGraphNode* SpawnedNode = Action.PerformAction(AnimGraph, nullptr, FUECPGraphLoc((float)PosX, (float)PosY), false);
	if (!SpawnedNode) { SetError(TEXT("Failed to spawn SequencePlayer node."), OutJson, OutError); return; }

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBP);
	AnimGraph->NotifyGraphChanged();
	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("node_guid"), SpawnedNode->NodeGuid.ToString());
	Obj->SetStringField(TEXT("node_type"), TEXT("SequencePlayer"));
	Obj->SetStringField(TEXT("animation_path"), AnimationPath);
	CompileAnimBlueprintAndReport(AnimBP, Obj);
	UEditorAssetLibrary::SaveAsset(AnimBPPath, false);
	BuildSuccessJson(Obj, OutJson);
}

void HandleAddRandomPlayer(const FString& AnimBPPath, int32 PosX, int32 PosY,
	FString& OutJson, FString& OutError)
{
	UAnimBlueprint* AnimBP = LoadObject<UAnimBlueprint>(nullptr, *AnimBPPath);
	if (!AnimBP) { SetError(FString::Printf(TEXT("AnimBlueprint not found: %s"), *AnimBPPath), OutJson, OutError); return; }
	UEdGraph* AnimGraph = FindAnimGraph(AnimBP);
	if (!AnimGraph) { SetError(TEXT("AnimGraph not found."), OutJson, OutError); return; }

	UAnimGraphNode_RandomPlayer* NodeTemplate = NewObject<UAnimGraphNode_RandomPlayer>(GetTransientPackage());
	FEdGraphSchemaAction_K2NewNode Action;
	Action.NodeTemplate = NodeTemplate;
	UEdGraphNode* SpawnedNode = Action.PerformAction(AnimGraph, nullptr, FUECPGraphLoc((float)PosX, (float)PosY), false);
	if (!SpawnedNode) { SetError(TEXT("Failed to spawn RandomPlayer node."), OutJson, OutError); return; }

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBP);
	AnimGraph->NotifyGraphChanged();
	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("node_guid"), SpawnedNode->NodeGuid.ToString());
	CompileAnimBlueprintAndReport(AnimBP, Obj);
	UEditorAssetLibrary::SaveAsset(AnimBPPath, false);
	BuildSuccessJson(Obj, OutJson);
}

static USkeleton* LoadSkeletonFromPath(const FString& Path)
{
	if (Path.IsEmpty()) return nullptr;
	USkeleton* Skel = LoadObject<USkeleton>(nullptr, *Path);
	if (Skel) return Skel;
	USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, *Path);
	return Mesh ? Mesh->GetSkeleton() : nullptr;
}

void HandleAddVirtualBone(const FString& SkeletonPath, const FString& SourceBone,
	const FString& TargetBone, const FString& VirtualBoneName,
	FString& OutJson, FString& OutError)
{
	USkeleton* Skeleton = LoadSkeletonFromPath(SkeletonPath);
	if (!Skeleton) { SetError(FString::Printf(TEXT("Skeleton not found: %s"), *SkeletonPath), OutJson, OutError); return; }
	if (SourceBone.IsEmpty() || TargetBone.IsEmpty())
	{
		SetError(TEXT("source_bone and target_bone are required."), OutJson, OutError);
		return;
	}

	FName VBName = VirtualBoneName.IsEmpty() ? NAME_None : FName(*VirtualBoneName);
	FName ActualVBName;
	const bool bAdded = Skeleton->AddNewVirtualBone(FName(*SourceBone), FName(*TargetBone), ActualVBName);
	if (!bAdded)
	{
		SetError(FString::Printf(TEXT("Failed to add virtual bone (may already exist). source=%s target=%s"), *SourceBone, *TargetBone), OutJson, OutError);
		return;
	}

	if (!VirtualBoneName.IsEmpty() && ActualVBName != FName(*VirtualBoneName))
	{
		Skeleton->RenameVirtualBone(ActualVBName, FName(*VirtualBoneName));
		ActualVBName = FName(*VirtualBoneName);
	}

	Skeleton->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(SkeletonPath, false);

	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("source_bone"), SourceBone);
	Obj->SetStringField(TEXT("target_bone"), TargetBone);
	Obj->SetStringField(TEXT("virtual_bone_name"), ActualVBName.ToString());
	BuildSuccessJson(Obj, OutJson);
}

void HandleListVirtualBones(const FString& SkeletonPath, FString& OutJson, FString& OutError)
{
	USkeleton* Skeleton = LoadSkeletonFromPath(SkeletonPath);
	if (!Skeleton) { SetError(FString::Printf(TEXT("Skeleton not found: %s"), *SkeletonPath), OutJson, OutError); return; }

	const TArray<FVirtualBone>& VBones = Skeleton->GetVirtualBones();
	TArray<TSharedPtr<FJsonValue>> BoneArray;
	for (const FVirtualBone& VB : VBones)
	{
		TSharedPtr<FJsonObject> Entry = MakeShareable(new FJsonObject);
		Entry->SetStringField(TEXT("virtual_bone_name"), VB.VirtualBoneName.ToString());
		Entry->SetStringField(TEXT("source_bone"), VB.SourceBoneName.ToString());
		Entry->SetStringField(TEXT("target_bone"), VB.TargetBoneName.ToString());
		BoneArray.Add(MakeShareable(new FJsonValueObject(Entry)));
	}

	TSharedPtr<FJsonObject> Root = MakeShareable(new FJsonObject);
	Root->SetStringField(TEXT("skeleton"), SkeletonPath);
	Root->SetArrayField(TEXT("virtual_bones"), BoneArray);
	Root->SetNumberField(TEXT("count"), (double)BoneArray.Num());
	BuildSuccessJson(Root, OutJson);
}

void HandleRemoveVirtualBone(const FString& SkeletonPath, const FString& VirtualBoneName,
	FString& OutJson, FString& OutError)
{
	USkeleton* Skeleton = LoadSkeletonFromPath(SkeletonPath);
	if (!Skeleton) { SetError(FString::Printf(TEXT("Skeleton not found: %s"), *SkeletonPath), OutJson, OutError); return; }
	if (VirtualBoneName.IsEmpty()) { SetError(TEXT("virtual_bone_name is required."), OutJson, OutError); return; }

	TArray<FName> ToRemove;
	ToRemove.Add(FName(*VirtualBoneName));
	Skeleton->RemoveVirtualBones(ToRemove);

	Skeleton->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(SkeletonPath, false);

	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("removed_virtual_bone"), VirtualBoneName);
	BuildSuccessJson(Obj, OutJson);
}

void HandleRemoveVirtualBoneFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	FString SkeletonPath; Args->TryGetStringField(TEXT("skeleton_path"), SkeletonPath);
	if (SkeletonPath.IsEmpty()) { SetError(TEXT("skeleton_path is required"), OutJson, OutError); return; }

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("virtual_bones"), ItemsArray))
	{
		USkeleton* Skeleton = LoadSkeletonFromPath(SkeletonPath);
		if (!Skeleton) { SetError(FString::Printf(TEXT("Skeleton not found: %s"), *SkeletonPath), OutJson, OutError); return; }

		BatchToolHelper::FBatchResultBuilder Batch;
		TArray<FName> ToRemove;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			FString Name;
			if ((*ItemsArray)[i]->Type == EJson::String)
				Name = (*ItemsArray)[i]->AsString();
			else
			{
				TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
				if (Item.IsValid()) { Name = BatchToolHelper::GetItemString(Item, TEXT("virtual_bone_name"), TEXT("name")); }
			}
			if (Name.IsEmpty()) { Batch.AddFailure(i, TEXT("Empty name")); continue; }
			ToRemove.Add(FName(*Name));
			auto E = MakeShared<FJsonObject>(); E->SetStringField(TEXT("removed"), Name);
			Batch.AddSuccess(i, E);
		}
		if (ToRemove.Num() > 0)
		{
			Skeleton->RemoveVirtualBones(ToRemove);
			Skeleton->MarkPackageDirty();
			UEditorAssetLibrary::SaveAsset(SkeletonPath, false);
		}
		Batch.Finalize(OutJson);
		return;
	}

	FString VBN; Args->TryGetStringField(TEXT("virtual_bone_name"), VBN);
	HandleRemoveVirtualBone(SkeletonPath, VBN, OutJson, OutError);
}

void HandleAddSlotNode(const FString& AnimBPPath, const FString& SlotName,
	int32 PosX, int32 PosY, FString& OutJson, FString& OutError)
{
	UAnimBlueprint* AnimBP = LoadObject<UAnimBlueprint>(nullptr, *AnimBPPath);
	if (!AnimBP) { SetError(FString::Printf(TEXT("AnimBlueprint not found: %s"), *AnimBPPath), OutJson, OutError); return; }
	UEdGraph* AnimGraph = FindAnimGraph(AnimBP);
	if (!AnimGraph) { SetError(TEXT("AnimGraph not found."), OutJson, OutError); return; }

	UAnimGraphNode_Slot* NodeTemplate = NewObject<UAnimGraphNode_Slot>(GetTransientPackage());
	if (!SlotName.IsEmpty()) NodeTemplate->Node.SlotName = FName(*SlotName);

	FEdGraphSchemaAction_K2NewNode Action; Action.NodeTemplate = NodeTemplate;
	UEdGraphNode* SpawnedNode = Action.PerformAction(AnimGraph, nullptr, FUECPGraphLoc((float)PosX, (float)PosY), false);
	if (!SpawnedNode) { SetError(TEXT("Failed to spawn Slot node."), OutJson, OutError); return; }

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBP);
	AnimGraph->NotifyGraphChanged();
	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("node_guid"), SpawnedNode->NodeGuid.ToString());
	Obj->SetStringField(TEXT("slot_name"), SlotName);
	CompileAnimBlueprintAndReport(AnimBP, Obj);
	UEditorAssetLibrary::SaveAsset(AnimBPPath, false);
	BuildSuccessJson(Obj, OutJson);
}

void HandleAddTwoWayBlend(const FString& AnimBPPath, int32 PosX, int32 PosY,
	FString& OutJson, FString& OutError)
{
	UAnimBlueprint* AnimBP = LoadObject<UAnimBlueprint>(nullptr, *AnimBPPath);
	if (!AnimBP) { SetError(FString::Printf(TEXT("AnimBlueprint not found: %s"), *AnimBPPath), OutJson, OutError); return; }
	UEdGraph* AnimGraph = FindAnimGraph(AnimBP);
	if (!AnimGraph) { SetError(TEXT("AnimGraph not found."), OutJson, OutError); return; }

	UAnimGraphNode_TwoWayBlend* NodeTemplate = NewObject<UAnimGraphNode_TwoWayBlend>(GetTransientPackage());
	FEdGraphSchemaAction_K2NewNode Action; Action.NodeTemplate = NodeTemplate;
	UEdGraphNode* SpawnedNode = Action.PerformAction(AnimGraph, nullptr, FUECPGraphLoc((float)PosX, (float)PosY), false);
	if (!SpawnedNode) { SetError(TEXT("Failed to spawn TwoWayBlend node."), OutJson, OutError); return; }

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBP);
	AnimGraph->NotifyGraphChanged();
	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField(TEXT("success"), true); Obj->SetStringField(TEXT("node_guid"), SpawnedNode->NodeGuid.ToString());
	CompileAnimBlueprintAndReport(AnimBP, Obj);
	UEditorAssetLibrary::SaveAsset(AnimBPPath, false);
	BuildSuccessJson(Obj, OutJson);
}

void HandleAddApplyMeshSpaceAdditive(const FString& AnimBPPath, int32 PosX, int32 PosY,
	FString& OutJson, FString& OutError)
{
	UAnimBlueprint* AnimBP = LoadObject<UAnimBlueprint>(nullptr, *AnimBPPath);
	if (!AnimBP) { SetError(FString::Printf(TEXT("AnimBlueprint not found: %s"), *AnimBPPath), OutJson, OutError); return; }
	UEdGraph* AnimGraph = FindAnimGraph(AnimBP);
	if (!AnimGraph) { SetError(TEXT("AnimGraph not found."), OutJson, OutError); return; }

	UAnimGraphNode_ApplyMeshSpaceAdditive* NodeTemplate = NewObject<UAnimGraphNode_ApplyMeshSpaceAdditive>(GetTransientPackage());
	FEdGraphSchemaAction_K2NewNode Action; Action.NodeTemplate = NodeTemplate;
	UEdGraphNode* SpawnedNode = Action.PerformAction(AnimGraph, nullptr, FUECPGraphLoc((float)PosX, (float)PosY), false);
	if (!SpawnedNode) { SetError(TEXT("Failed to spawn ApplyMeshSpaceAdditive node."), OutJson, OutError); return; }

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBP);
	AnimGraph->NotifyGraphChanged();
	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField(TEXT("success"), true); Obj->SetStringField(TEXT("node_guid"), SpawnedNode->NodeGuid.ToString());
	CompileAnimBlueprintAndReport(AnimBP, Obj);
	UEditorAssetLibrary::SaveAsset(AnimBPPath, false);
	BuildSuccessJson(Obj, OutJson);
}

void HandleAddCopyPoseFromMesh(const FString& AnimBPPath, int32 PosX, int32 PosY,
	FString& OutJson, FString& OutError)
{
	UAnimBlueprint* AnimBP = LoadObject<UAnimBlueprint>(nullptr, *AnimBPPath);
	if (!AnimBP) { SetError(FString::Printf(TEXT("AnimBlueprint not found: %s"), *AnimBPPath), OutJson, OutError); return; }
	UEdGraph* AnimGraph = FindAnimGraph(AnimBP);
	if (!AnimGraph) { SetError(TEXT("AnimGraph not found."), OutJson, OutError); return; }

	UAnimGraphNode_CopyPoseFromMesh* NodeTemplate = NewObject<UAnimGraphNode_CopyPoseFromMesh>(GetTransientPackage());
	FEdGraphSchemaAction_K2NewNode Action; Action.NodeTemplate = NodeTemplate;
	UEdGraphNode* SpawnedNode = Action.PerformAction(AnimGraph, nullptr, FUECPGraphLoc((float)PosX, (float)PosY), false);
	if (!SpawnedNode) { SetError(TEXT("Failed to spawn CopyPoseFromMesh node."), OutJson, OutError); return; }

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBP);
	AnimGraph->NotifyGraphChanged();
	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField(TEXT("success"), true); Obj->SetStringField(TEXT("node_guid"), SpawnedNode->NodeGuid.ToString());
	CompileAnimBlueprintAndReport(AnimBP, Obj);
	UEditorAssetLibrary::SaveAsset(AnimBPPath, false);
	BuildSuccessJson(Obj, OutJson);
}

void HandleAddRotateRootBone(const FString& AnimBPPath, int32 PosX, int32 PosY,
	FString& OutJson, FString& OutError)
{
	UAnimBlueprint* AnimBP = LoadObject<UAnimBlueprint>(nullptr, *AnimBPPath);
	if (!AnimBP) { SetError(FString::Printf(TEXT("AnimBlueprint not found: %s"), *AnimBPPath), OutJson, OutError); return; }
	UEdGraph* AnimGraph = FindAnimGraph(AnimBP);
	if (!AnimGraph) { SetError(TEXT("AnimGraph not found."), OutJson, OutError); return; }

	UAnimGraphNode_RotateRootBone* NodeTemplate = NewObject<UAnimGraphNode_RotateRootBone>(GetTransientPackage());
	FEdGraphSchemaAction_K2NewNode Action; Action.NodeTemplate = NodeTemplate;
	UEdGraphNode* SpawnedNode = Action.PerformAction(AnimGraph, nullptr, FUECPGraphLoc((float)PosX, (float)PosY), false);
	if (!SpawnedNode) { SetError(TEXT("Failed to spawn RotateRootBone node."), OutJson, OutError); return; }

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBP);
	AnimGraph->NotifyGraphChanged();
	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField(TEXT("success"), true); Obj->SetStringField(TEXT("node_guid"), SpawnedNode->NodeGuid.ToString());
	CompileAnimBlueprintAndReport(AnimBP, Obj);
	UEditorAssetLibrary::SaveAsset(AnimBPPath, false);
	BuildSuccessJson(Obj, OutJson);
}

void HandleAddAimOffsetPlayer(const FString& AnimBPPath, const FString& AnimationPath,
	int32 PosX, int32 PosY, FString& OutJson, FString& OutError)
{
	UAnimBlueprint* AnimBP = LoadObject<UAnimBlueprint>(nullptr, *AnimBPPath);
	if (!AnimBP) { SetError(FString::Printf(TEXT("AnimBlueprint not found: %s"), *AnimBPPath), OutJson, OutError); return; }
	UEdGraph* AnimGraph = FindAnimGraph(AnimBP);
	if (!AnimGraph) { SetError(TEXT("AnimGraph not found."), OutJson, OutError); return; }

	UAnimGraphNode_RotationOffsetBlendSpace* NodeTemplate = NewObject<UAnimGraphNode_RotationOffsetBlendSpace>(GetTransientPackage());
	if (!AnimationPath.IsEmpty())
	{
		if (UBlendSpace* BS = LoadObject<UBlendSpace>(nullptr, *AnimationPath))
			NodeTemplate->Node.SetBlendSpace(BS);
	}

	FEdGraphSchemaAction_K2NewNode Action; Action.NodeTemplate = NodeTemplate;
	UEdGraphNode* SpawnedNode = Action.PerformAction(AnimGraph, nullptr, FUECPGraphLoc((float)PosX, (float)PosY), false);
	if (!SpawnedNode) { SetError(TEXT("Failed to spawn AimOffsetPlayer node."), OutJson, OutError); return; }

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBP);
	AnimGraph->NotifyGraphChanged();
	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("node_guid"), SpawnedNode->NodeGuid.ToString());
	Obj->SetStringField(TEXT("animation_path"), AnimationPath);
	CompileAnimBlueprintAndReport(AnimBP, Obj);
	UEditorAssetLibrary::SaveAsset(AnimBPPath, false);
	BuildSuccessJson(Obj, OutJson);
}

void HandleAddMirror(const FString& AnimBPPath, const FString& MirrorDataTablePath,
	int32 PosX, int32 PosY, FString& OutJson, FString& OutError)
{
	UAnimBlueprint* AnimBP = LoadObject<UAnimBlueprint>(nullptr, *AnimBPPath);
	if (!AnimBP) { SetError(FString::Printf(TEXT("AnimBlueprint not found: %s"), *AnimBPPath), OutJson, OutError); return; }
	UEdGraph* AnimGraph = FindAnimGraph(AnimBP);
	if (!AnimGraph) { SetError(TEXT("AnimGraph not found."), OutJson, OutError); return; }

	UAnimGraphNode_Mirror* NodeTemplate = NewObject<UAnimGraphNode_Mirror>(GetTransientPackage());
	if (!MirrorDataTablePath.IsEmpty())
	{
		if (UMirrorDataTable* MDT = LoadObject<UMirrorDataTable>(nullptr, *MirrorDataTablePath))
			NodeTemplate->Node.SetMirrorDataTable(MDT);
	}

	FEdGraphSchemaAction_K2NewNode Action; Action.NodeTemplate = NodeTemplate;
	UEdGraphNode* SpawnedNode = Action.PerformAction(AnimGraph, nullptr, FUECPGraphLoc((float)PosX, (float)PosY), false);
	if (!SpawnedNode) { SetError(TEXT("Failed to spawn Mirror node."), OutJson, OutError); return; }

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBP);
	AnimGraph->NotifyGraphChanged();
	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("node_guid"), SpawnedNode->NodeGuid.ToString());
	Obj->SetStringField(TEXT("mirror_data_table_path"), MirrorDataTablePath);
	CompileAnimBlueprintAndReport(AnimBP, Obj);
	UEditorAssetLibrary::SaveAsset(AnimBPPath, false);
	BuildSuccessJson(Obj, OutJson);
}

void HandleAddLocalToComponentSpace(const FString& AnimBPPath, int32 PosX, int32 PosY,
	FString& OutJson, FString& OutError)
{
	UAnimBlueprint* AnimBP = LoadObject<UAnimBlueprint>(nullptr, *AnimBPPath);
	if (!AnimBP) { SetError(FString::Printf(TEXT("AnimBlueprint not found: %s"), *AnimBPPath), OutJson, OutError); return; }
	UEdGraph* AnimGraph = FindAnimGraph(AnimBP);
	if (!AnimGraph) { SetError(TEXT("AnimGraph not found."), OutJson, OutError); return; }

	UAnimGraphNode_LocalToComponentSpace* NodeTemplate = NewObject<UAnimGraphNode_LocalToComponentSpace>(GetTransientPackage());
	FEdGraphSchemaAction_K2NewNode Action; Action.NodeTemplate = NodeTemplate;
	UEdGraphNode* SpawnedNode = Action.PerformAction(AnimGraph, nullptr, FUECPGraphLoc((float)PosX, (float)PosY), false);
	if (!SpawnedNode) { SetError(TEXT("Failed to spawn LocalToComponentSpace node."), OutJson, OutError); return; }

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBP);
	AnimGraph->NotifyGraphChanged();
	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField(TEXT("success"), true); Obj->SetStringField(TEXT("node_guid"), SpawnedNode->NodeGuid.ToString());
	CompileAnimBlueprintAndReport(AnimBP, Obj);
	UEditorAssetLibrary::SaveAsset(AnimBPPath, false);
	BuildSuccessJson(Obj, OutJson);
}

void HandleAddComponentToLocalSpace(const FString& AnimBPPath, int32 PosX, int32 PosY,
	FString& OutJson, FString& OutError)
{
	UAnimBlueprint* AnimBP = LoadObject<UAnimBlueprint>(nullptr, *AnimBPPath);
	if (!AnimBP) { SetError(FString::Printf(TEXT("AnimBlueprint not found: %s"), *AnimBPPath), OutJson, OutError); return; }
	UEdGraph* AnimGraph = FindAnimGraph(AnimBP);
	if (!AnimGraph) { SetError(TEXT("AnimGraph not found."), OutJson, OutError); return; }

	UAnimGraphNode_ComponentToLocalSpace* NodeTemplate = NewObject<UAnimGraphNode_ComponentToLocalSpace>(GetTransientPackage());
	FEdGraphSchemaAction_K2NewNode Action; Action.NodeTemplate = NodeTemplate;
	UEdGraphNode* SpawnedNode = Action.PerformAction(AnimGraph, nullptr, FUECPGraphLoc((float)PosX, (float)PosY), false);
	if (!SpawnedNode) { SetError(TEXT("Failed to spawn ComponentToLocalSpace node."), OutJson, OutError); return; }

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBP);
	AnimGraph->NotifyGraphChanged();
	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField(TEXT("success"), true); Obj->SetStringField(TEXT("node_guid"), SpawnedNode->NodeGuid.ToString());
	CompileAnimBlueprintAndReport(AnimBP, Obj);
	UEditorAssetLibrary::SaveAsset(AnimBPPath, false);
	BuildSuccessJson(Obj, OutJson);
}

void HandleAddMeshRefPose(const FString& AnimBPPath, int32 PosX, int32 PosY,
	FString& OutJson, FString& OutError)
{
	UAnimBlueprint* AnimBP = LoadObject<UAnimBlueprint>(nullptr, *AnimBPPath);
	if (!AnimBP) { SetError(FString::Printf(TEXT("AnimBlueprint not found: %s"), *AnimBPPath), OutJson, OutError); return; }
	UEdGraph* AnimGraph = FindAnimGraph(AnimBP);
	if (!AnimGraph) { SetError(TEXT("AnimGraph not found."), OutJson, OutError); return; }

	UAnimGraphNode_MeshRefPose* NodeTemplate = NewObject<UAnimGraphNode_MeshRefPose>(GetTransientPackage());
	FEdGraphSchemaAction_K2NewNode Action; Action.NodeTemplate = NodeTemplate;
	UEdGraphNode* SpawnedNode = Action.PerformAction(AnimGraph, nullptr, FUECPGraphLoc((float)PosX, (float)PosY), false);
	if (!SpawnedNode) { SetError(TEXT("Failed to spawn MeshRefPose node."), OutJson, OutError); return; }

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBP);
	AnimGraph->NotifyGraphChanged();
	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField(TEXT("success"), true); Obj->SetStringField(TEXT("node_guid"), SpawnedNode->NodeGuid.ToString());
	CompileAnimBlueprintAndReport(AnimBP, Obj);
	UEditorAssetLibrary::SaveAsset(AnimBPPath, false);
	BuildSuccessJson(Obj, OutJson);
}

void HandleAddLocalRefPose(const FString& AnimBPPath, int32 PosX, int32 PosY,
	FString& OutJson, FString& OutError)
{
	UAnimBlueprint* AnimBP = LoadObject<UAnimBlueprint>(nullptr, *AnimBPPath);
	if (!AnimBP) { SetError(FString::Printf(TEXT("AnimBlueprint not found: %s"), *AnimBPPath), OutJson, OutError); return; }
	UEdGraph* AnimGraph = FindAnimGraph(AnimBP);
	if (!AnimGraph) { SetError(TEXT("AnimGraph not found."), OutJson, OutError); return; }

	UAnimGraphNode_LocalRefPose* NodeTemplate = NewObject<UAnimGraphNode_LocalRefPose>(GetTransientPackage());
	FEdGraphSchemaAction_K2NewNode Action; Action.NodeTemplate = NodeTemplate;
	UEdGraphNode* SpawnedNode = Action.PerformAction(AnimGraph, nullptr, FUECPGraphLoc((float)PosX, (float)PosY), false);
	if (!SpawnedNode) { SetError(TEXT("Failed to spawn LocalRefPose node."), OutJson, OutError); return; }

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBP);
	AnimGraph->NotifyGraphChanged();
	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField(TEXT("success"), true); Obj->SetStringField(TEXT("node_guid"), SpawnedNode->NodeGuid.ToString());
	CompileAnimBlueprintAndReport(AnimBP, Obj);
	UEditorAssetLibrary::SaveAsset(AnimBPPath, false);
	BuildSuccessJson(Obj, OutJson);
}

void HandleAddIdentityPose(const FString& AnimBPPath, int32 PosX, int32 PosY,
	FString& OutJson, FString& OutError)
{
	UAnimBlueprint* AnimBP = LoadObject<UAnimBlueprint>(nullptr, *AnimBPPath);
	if (!AnimBP) { SetError(FString::Printf(TEXT("AnimBlueprint not found: %s"), *AnimBPPath), OutJson, OutError); return; }
	UEdGraph* AnimGraph = FindAnimGraph(AnimBP);
	if (!AnimGraph) { SetError(TEXT("AnimGraph not found."), OutJson, OutError); return; }

	UAnimGraphNode_IdentityPose* NodeTemplate = NewObject<UAnimGraphNode_IdentityPose>(GetTransientPackage());
	FEdGraphSchemaAction_K2NewNode Action; Action.NodeTemplate = NodeTemplate;
	UEdGraphNode* SpawnedNode = Action.PerformAction(AnimGraph, nullptr, FUECPGraphLoc((float)PosX, (float)PosY), false);
	if (!SpawnedNode) { SetError(TEXT("Failed to spawn IdentityPose node."), OutJson, OutError); return; }

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBP);
	AnimGraph->NotifyGraphChanged();
	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField(TEXT("success"), true); Obj->SetStringField(TEXT("node_guid"), SpawnedNode->NodeGuid.ToString());
	CompileAnimBlueprintAndReport(AnimBP, Obj);
	UEditorAssetLibrary::SaveAsset(AnimBPPath, false);
	BuildSuccessJson(Obj, OutJson);
}

void HandleAddSpringBone(const FString& AnimBPPath, const FString& BoneName,
	int32 PosX, int32 PosY, FString& OutJson, FString& OutError)
{
	UAnimBlueprint* AnimBP = LoadObject<UAnimBlueprint>(nullptr, *AnimBPPath);
	if (!AnimBP) { SetError(FString::Printf(TEXT("AnimBlueprint not found: %s"), *AnimBPPath), OutJson, OutError); return; }
	UEdGraph* AnimGraph = FindAnimGraph(AnimBP);
	if (!AnimGraph) { SetError(TEXT("AnimGraph not found."), OutJson, OutError); return; }

	UAnimGraphNode_SpringBone* NodeTemplate = NewObject<UAnimGraphNode_SpringBone>(GetTransientPackage());
	if (!BoneName.IsEmpty()) NodeTemplate->Node.SpringBone.BoneName = FName(*BoneName);

	FEdGraphSchemaAction_K2NewNode Action; Action.NodeTemplate = NodeTemplate;
	UEdGraphNode* SpawnedNode = Action.PerformAction(AnimGraph, nullptr, FUECPGraphLoc((float)PosX, (float)PosY), false);
	if (!SpawnedNode) { SetError(TEXT("Failed to spawn SpringBone node."), OutJson, OutError); return; }

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBP);
	AnimGraph->NotifyGraphChanged();
	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("node_guid"), SpawnedNode->NodeGuid.ToString());
	Obj->SetStringField(TEXT("bone_name"), BoneName);
	CompileAnimBlueprintAndReport(AnimBP, Obj);
	UEditorAssetLibrary::SaveAsset(AnimBPPath, false);
	BuildSuccessJson(Obj, OutJson);
}

void HandleAddRigidBody(const FString& AnimBPPath, int32 PosX, int32 PosY,
	FString& OutJson, FString& OutError)
{
	UAnimBlueprint* AnimBP = LoadObject<UAnimBlueprint>(nullptr, *AnimBPPath);
	if (!AnimBP) { SetError(FString::Printf(TEXT("AnimBlueprint not found: %s"), *AnimBPPath), OutJson, OutError); return; }
	UEdGraph* AnimGraph = FindAnimGraph(AnimBP);
	if (!AnimGraph) { SetError(TEXT("AnimGraph not found."), OutJson, OutError); return; }

	UAnimGraphNode_RigidBody* NodeTemplate = NewObject<UAnimGraphNode_RigidBody>(GetTransientPackage());
	FEdGraphSchemaAction_K2NewNode Action; Action.NodeTemplate = NodeTemplate;
	UEdGraphNode* SpawnedNode = Action.PerformAction(AnimGraph, nullptr, FUECPGraphLoc((float)PosX, (float)PosY), false);
	if (!SpawnedNode) { SetError(TEXT("Failed to spawn RigidBody node."), OutJson, OutError); return; }

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBP);
	AnimGraph->NotifyGraphChanged();
	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField(TEXT("success"), true); Obj->SetStringField(TEXT("node_guid"), SpawnedNode->NodeGuid.ToString());
	CompileAnimBlueprintAndReport(AnimBP, Obj);
	UEditorAssetLibrary::SaveAsset(AnimBPPath, false);
	BuildSuccessJson(Obj, OutJson);
}

void HandleAddFabrik(const FString& AnimBPPath, const FString& TipBone, const FString& RootBone,
	int32 PosX, int32 PosY, FString& OutJson, FString& OutError)
{
	UAnimBlueprint* AnimBP = LoadObject<UAnimBlueprint>(nullptr, *AnimBPPath);
	if (!AnimBP) { SetError(FString::Printf(TEXT("AnimBlueprint not found: %s"), *AnimBPPath), OutJson, OutError); return; }
	UEdGraph* AnimGraph = FindAnimGraph(AnimBP);
	if (!AnimGraph) { SetError(TEXT("AnimGraph not found."), OutJson, OutError); return; }

	UAnimGraphNode_Fabrik* NodeTemplate = NewObject<UAnimGraphNode_Fabrik>(GetTransientPackage());
	if (!TipBone.IsEmpty())  NodeTemplate->Node.TipBone.BoneName  = FName(*TipBone);
	if (!RootBone.IsEmpty()) NodeTemplate->Node.RootBone.BoneName = FName(*RootBone);

	FEdGraphSchemaAction_K2NewNode Action; Action.NodeTemplate = NodeTemplate;
	UEdGraphNode* SpawnedNode = Action.PerformAction(AnimGraph, nullptr, FUECPGraphLoc((float)PosX, (float)PosY), false);
	if (!SpawnedNode) { SetError(TEXT("Failed to spawn Fabrik node."), OutJson, OutError); return; }

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBP);
	AnimGraph->NotifyGraphChanged();
	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("node_guid"), SpawnedNode->NodeGuid.ToString());
	Obj->SetStringField(TEXT("tip_bone"), TipBone); Obj->SetStringField(TEXT("root_bone"), RootBone);
	CompileAnimBlueprintAndReport(AnimBP, Obj);
	UEditorAssetLibrary::SaveAsset(AnimBPPath, false);
	BuildSuccessJson(Obj, OutJson);
}

void HandleAddCCDIK(const FString& AnimBPPath, const FString& TipBone, const FString& RootBone,
	int32 PosX, int32 PosY, FString& OutJson, FString& OutError)
{
	UAnimBlueprint* AnimBP = LoadObject<UAnimBlueprint>(nullptr, *AnimBPPath);
	if (!AnimBP) { SetError(FString::Printf(TEXT("AnimBlueprint not found: %s"), *AnimBPPath), OutJson, OutError); return; }
	UEdGraph* AnimGraph = FindAnimGraph(AnimBP);
	if (!AnimGraph) { SetError(TEXT("AnimGraph not found."), OutJson, OutError); return; }

	UAnimGraphNode_CCDIK* NodeTemplate = NewObject<UAnimGraphNode_CCDIK>(GetTransientPackage());
	if (!TipBone.IsEmpty())  NodeTemplate->Node.TipBone.BoneName  = FName(*TipBone);
	if (!RootBone.IsEmpty()) NodeTemplate->Node.RootBone.BoneName = FName(*RootBone);

	FEdGraphSchemaAction_K2NewNode Action; Action.NodeTemplate = NodeTemplate;
	UEdGraphNode* SpawnedNode = Action.PerformAction(AnimGraph, nullptr, FUECPGraphLoc((float)PosX, (float)PosY), false);
	if (!SpawnedNode) { SetError(TEXT("Failed to spawn CCDIK node."), OutJson, OutError); return; }

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBP);
	AnimGraph->NotifyGraphChanged();
	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("node_guid"), SpawnedNode->NodeGuid.ToString());
	Obj->SetStringField(TEXT("tip_bone"), TipBone); Obj->SetStringField(TEXT("root_bone"), RootBone);
	CompileAnimBlueprintAndReport(AnimBP, Obj);
	UEditorAssetLibrary::SaveAsset(AnimBPPath, false);
	BuildSuccessJson(Obj, OutJson);
}

void HandleAddLegIK(const FString& AnimBPPath, int32 PosX, int32 PosY,
	FString& OutJson, FString& OutError)
{
	UAnimBlueprint* AnimBP = LoadObject<UAnimBlueprint>(nullptr, *AnimBPPath);
	if (!AnimBP) { SetError(FString::Printf(TEXT("AnimBlueprint not found: %s"), *AnimBPPath), OutJson, OutError); return; }
	UEdGraph* AnimGraph = FindAnimGraph(AnimBP);
	if (!AnimGraph) { SetError(TEXT("AnimGraph not found."), OutJson, OutError); return; }

	UAnimGraphNode_LegIK* NodeTemplate = NewObject<UAnimGraphNode_LegIK>(GetTransientPackage());
	FEdGraphSchemaAction_K2NewNode Action; Action.NodeTemplate = NodeTemplate;
	UEdGraphNode* SpawnedNode = Action.PerformAction(AnimGraph, nullptr, FUECPGraphLoc((float)PosX, (float)PosY), false);
	if (!SpawnedNode) { SetError(TEXT("Failed to spawn LegIK node."), OutJson, OutError); return; }

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBP);
	AnimGraph->NotifyGraphChanged();
	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField(TEXT("success"), true); Obj->SetStringField(TEXT("node_guid"), SpawnedNode->NodeGuid.ToString());
	CompileAnimBlueprintAndReport(AnimBP, Obj);
	UEditorAssetLibrary::SaveAsset(AnimBPPath, false);
	BuildSuccessJson(Obj, OutJson);
}

void HandleAddPoseByName(const FString& AnimBPPath, const FString& PoseAssetPath,
	const FString& PoseNameStr, int32 PosX, int32 PosY, FString& OutJson, FString& OutError)
{
	UAnimBlueprint* AnimBP = LoadObject<UAnimBlueprint>(nullptr, *AnimBPPath);
	if (!AnimBP) { SetError(FString::Printf(TEXT("AnimBlueprint not found: %s"), *AnimBPPath), OutJson, OutError); return; }
	UEdGraph* AnimGraph = FindAnimGraph(AnimBP);
	if (!AnimGraph) { SetError(TEXT("AnimGraph not found."), OutJson, OutError); return; }

	UAnimGraphNode_PoseByName* NodeTemplate = NewObject<UAnimGraphNode_PoseByName>(GetTransientPackage());
	if (!PoseAssetPath.IsEmpty())
	{
		if (UPoseAsset* PA = LoadObject<UPoseAsset>(nullptr, *PoseAssetPath))
			NodeTemplate->Node.PoseAsset = PA;
	}
	if (!PoseNameStr.IsEmpty()) NodeTemplate->Node.PoseName = FName(*PoseNameStr);

	FEdGraphSchemaAction_K2NewNode Action; Action.NodeTemplate = NodeTemplate;
	UEdGraphNode* SpawnedNode = Action.PerformAction(AnimGraph, nullptr, FUECPGraphLoc((float)PosX, (float)PosY), false);
	if (!SpawnedNode) { SetError(TEXT("Failed to spawn PoseByName node."), OutJson, OutError); return; }

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBP);
	AnimGraph->NotifyGraphChanged();
	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("node_guid"), SpawnedNode->NodeGuid.ToString());
	Obj->SetStringField(TEXT("pose_asset_path"), PoseAssetPath);
	Obj->SetStringField(TEXT("pose_name"), PoseNameStr);
	CompileAnimBlueprintAndReport(AnimBP, Obj);
	UEditorAssetLibrary::SaveAsset(AnimBPPath, false);
	BuildSuccessJson(Obj, OutJson);
}

void HandleAddBoneDrivenController(const FString& AnimBPPath, const FString& SourceBone,
	const FString& TargetBone, int32 PosX, int32 PosY, FString& OutJson, FString& OutError)
{
	UAnimBlueprint* AnimBP = LoadObject<UAnimBlueprint>(nullptr, *AnimBPPath);
	if (!AnimBP) { SetError(FString::Printf(TEXT("AnimBlueprint not found: %s"), *AnimBPPath), OutJson, OutError); return; }
	UEdGraph* AnimGraph = FindAnimGraph(AnimBP);
	if (!AnimGraph) { SetError(TEXT("AnimGraph not found."), OutJson, OutError); return; }

	UAnimGraphNode_BoneDrivenController* NodeTemplate = NewObject<UAnimGraphNode_BoneDrivenController>(GetTransientPackage());
	if (!SourceBone.IsEmpty()) NodeTemplate->Node.SourceBone.BoneName = FName(*SourceBone);
	if (!TargetBone.IsEmpty()) NodeTemplate->Node.TargetBone.BoneName = FName(*TargetBone);

	FEdGraphSchemaAction_K2NewNode Action; Action.NodeTemplate = NodeTemplate;
	UEdGraphNode* SpawnedNode = Action.PerformAction(AnimGraph, nullptr, FUECPGraphLoc((float)PosX, (float)PosY), false);
	if (!SpawnedNode) { SetError(TEXT("Failed to spawn BoneDrivenController node."), OutJson, OutError); return; }

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBP);
	AnimGraph->NotifyGraphChanged();
	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("node_guid"), SpawnedNode->NodeGuid.ToString());
	Obj->SetStringField(TEXT("source_bone"), SourceBone); Obj->SetStringField(TEXT("target_bone"), TargetBone);
	CompileAnimBlueprintAndReport(AnimBP, Obj);
	UEditorAssetLibrary::SaveAsset(AnimBPPath, false);
	BuildSuccessJson(Obj, OutJson);
}

void HandleAddBlendBoneByChannel(const FString& AnimBPPath, int32 PosX, int32 PosY,
	FString& OutJson, FString& OutError)
{
	UAnimBlueprint* AnimBP = LoadObject<UAnimBlueprint>(nullptr, *AnimBPPath);
	if (!AnimBP) { SetError(FString::Printf(TEXT("AnimBlueprint not found: %s"), *AnimBPPath), OutJson, OutError); return; }
	UEdGraph* AnimGraph = FindAnimGraph(AnimBP);
	if (!AnimGraph) { SetError(TEXT("AnimGraph not found."), OutJson, OutError); return; }

	UAnimGraphNode_BlendBoneByChannel* NodeTemplate = NewObject<UAnimGraphNode_BlendBoneByChannel>(GetTransientPackage());
	FEdGraphSchemaAction_K2NewNode Action; Action.NodeTemplate = NodeTemplate;
	UEdGraphNode* SpawnedNode = Action.PerformAction(AnimGraph, nullptr, FUECPGraphLoc((float)PosX, (float)PosY), false);
	if (!SpawnedNode) { SetError(TEXT("Failed to spawn BlendBoneByChannel node."), OutJson, OutError); return; }

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBP);
	AnimGraph->NotifyGraphChanged();
	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField(TEXT("success"), true); Obj->SetStringField(TEXT("node_guid"), SpawnedNode->NodeGuid.ToString());
	CompileAnimBlueprintAndReport(AnimBP, Obj);
	UEditorAssetLibrary::SaveAsset(AnimBPPath, false);
	BuildSuccessJson(Obj, OutJson);
}

void HandleAddMotionMatchingNode(const FString& AnimBPPath, const FString& PoseSearchDbPath,
	int32 PosX, int32 PosY, FString& OutJson, FString& OutError)
{
	UAnimBlueprint* AnimBP = LoadObject<UAnimBlueprint>(nullptr, *AnimBPPath);
	if (!AnimBP) { SetError(FString::Printf(TEXT("AnimBlueprint not found: %s"), *AnimBPPath), OutJson, OutError); return; }
	UEdGraph* AnimGraph = FindAnimGraph(AnimBP);
	if (!AnimGraph) { SetError(TEXT("AnimGraph not found."), OutJson, OutError); return; }

	AnimGraph->Modify();
	UAnimGraphNode_MotionMatching* NewNode = NewObject<UAnimGraphNode_MotionMatching>(AnimGraph);
	NewNode->NodePosX = PosX;
	NewNode->NodePosY = PosY;
	NewNode->CreateNewGuid();

	if (!PoseSearchDbPath.IsEmpty())
	{
		if (UPoseSearchDatabase* DB = LoadObject<UPoseSearchDatabase>(nullptr, *PoseSearchDbPath))
		{
			FStructProperty* NodeProp = FindFProperty<FStructProperty>(UAnimGraphNode_MotionMatching::StaticClass(), TEXT("Node"));
			FObjectPropertyBase* DBProp = FindFProperty<FObjectPropertyBase>(FAnimNode_MotionMatching::StaticStruct(), TEXT("Database"));
			if (NodeProp && DBProp)
			{
				void* NodePtr = NodeProp->ContainerPtrToValuePtr<void>(NewNode);
				DBProp->SetObjectPropertyValue(DBProp->ContainerPtrToValuePtr<void>(NodePtr), (UObject*)DB);
			}
		}
	}

	AnimGraph->AddNode(NewNode, true, false);
	NewNode->PostPlacedNewNode();
	NewNode->AllocateDefaultPins();

	UEdGraphNode* SpawnedNode = NewNode;
	if (!SpawnedNode) { SetError(TEXT("Failed to spawn MotionMatching node."), OutJson, OutError); return; }

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBP);
	AnimGraph->NotifyGraphChanged();
	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("node_guid"), SpawnedNode->NodeGuid.ToString());
	Obj->SetStringField(TEXT("pose_search_db_path"), PoseSearchDbPath);
	CompileAnimBlueprintAndReport(AnimBP, Obj);
	UEditorAssetLibrary::SaveAsset(AnimBPPath, false);
	BuildSuccessJson(Obj, OutJson);
}

void HandleConnectAnimNodes(const FString& AnimBPPath, const FString& SourceNodeGuid,
	const FString& TargetNodeGuid, FString& OutJson, FString& OutError)
{
	UAnimBlueprint* AnimBP = LoadObject<UAnimBlueprint>(nullptr, *AnimBPPath);
	if (!AnimBP) { SetError(FString::Printf(TEXT("AnimBlueprint not found: %s"), *AnimBPPath), OutJson, OutError); return; }

	FGuid SGuid, TGuid;
	if (!FGuid::Parse(SourceNodeGuid, SGuid)) { SetError(FString::Printf(TEXT("Invalid source_node_guid: %s"), *SourceNodeGuid), OutJson, OutError); return; }
	if (!FGuid::Parse(TargetNodeGuid, TGuid)) { SetError(FString::Printf(TEXT("Invalid target_node_guid: %s"), *TargetNodeGuid), OutJson, OutError); return; }

	// Nodes are resolved across every graph (AnimGraph, state graphs, layers) so chains inside
	// states can be wired too; both ends must live in the same graph.
	UEdGraph* SourceGraph = nullptr;
	UEdGraph* TargetGraph = nullptr;
	UEdGraphNode* SourceNode = FindNodeByGuidInAllGraphs(AnimBP, SGuid, &SourceGraph);
	if (!SourceNode) { SetError(FString::Printf(TEXT("Source node not found for GUID: %s (use get_anim_graph_nodes include_nested=true)"), *SourceNodeGuid), OutJson, OutError); return; }
	UEdGraphNode* TargetNode = FindNodeByGuidInAllGraphs(AnimBP, TGuid, &TargetGraph);
	if (!TargetNode) { SetError(FString::Printf(TEXT("Target node not found for GUID: %s (use get_anim_graph_nodes include_nested=true)"), *TargetNodeGuid), OutJson, OutError); return; }
	if (SourceGraph != TargetGraph)
	{
		SetError(FString::Printf(TEXT("Source node lives in graph '%s' but target node lives in graph '%s'; pose links must stay inside one graph."),
			*SourceGraph->GetName(), *TargetGraph->GetName()), OutJson, OutError);
		return;
	}
	UEdGraph* AnimGraph = SourceGraph;

	UEdGraphPin* OutPin = FindPoseOutputPin(SourceNode);
	if (!OutPin) { SetError(FString::Printf(TEXT("Source node '%s' has no pose output pin."), *SourceNode->GetNodeTitle(ENodeTitleType::ListView).ToString()), OutJson, OutError); return; }
	UEdGraphPin* InPin = FindPoseInputPin(TargetNode);
	if (!InPin) { SetError(FString::Printf(TEXT("Target node '%s' has no pose input pin."), *TargetNode->GetNodeTitle(ENodeTitleType::ListView).ToString()), OutJson, OutError); return; }

	const UEdGraphSchema* Schema = AnimGraph->GetSchema();
	if (!Schema) { SetError(TEXT("Graph has no schema."), OutJson, OutError); return; }
	const bool bConnected = Schema->TryCreateConnection(OutPin, InPin);
	if (!bConnected)
	{
		const FPinConnectionResponse Response = Schema->CanCreateConnection(OutPin, InPin);
		SetError(FString::Printf(TEXT("Schema rejected pose connection %s.%s -> %s.%s: %s"),
			*SourceNode->GetNodeTitle(ENodeTitleType::ListView).ToString(), *OutPin->PinName.ToString(),
			*TargetNode->GetNodeTitle(ENodeTitleType::ListView).ToString(), *InPin->PinName.ToString(),
			*Response.Message.ToString()), OutJson, OutError);
		return;
	}
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBP);
	AnimGraph->NotifyGraphChanged();

	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("source_guid"), SourceNodeGuid);
	Obj->SetStringField(TEXT("source_pin"), OutPin->PinName.ToString());
	Obj->SetStringField(TEXT("target_guid"), TargetNodeGuid);
	Obj->SetStringField(TEXT("target_pin"), InPin->PinName.ToString());
	Obj->SetStringField(TEXT("graph"), AnimGraph->GetName());
	Obj->SetBoolField(TEXT("connected"), bConnected);
	CompileAnimBlueprintAndReport(AnimBP, Obj);
	UEditorAssetLibrary::SaveAsset(AnimBPPath, false);
	BuildSuccessJson(Obj, OutJson);
}

void HandleBuildAnimChain(const FString& AnimBPPath, const FString& ChainJson,
	bool bAutoConnectToOutput, FString& OutJson, FString& OutError)
{
	UAnimBlueprint* AnimBP = LoadObject<UAnimBlueprint>(nullptr, *AnimBPPath);
	if (!AnimBP) { SetError(FString::Printf(TEXT("AnimBlueprint not found: %s"), *AnimBPPath), OutJson, OutError); return; }
	UEdGraph* AnimGraph = FindAnimGraph(AnimBP);
	if (!AnimGraph) { SetError(TEXT("AnimGraph not found."), OutJson, OutError); return; }

	TArray<TSharedPtr<FJsonValue>> ChainArr;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ChainJson);
	if (!FJsonSerializer::Deserialize(Reader, ChainArr) || ChainArr.Num() == 0)
	{ SetError(TEXT("Invalid or empty chain JSON array."), OutJson, OutError); return; }

	// The per-node handlers each compile + save; suppress that here and compile once at the end.
	TUniquePtr<FScopedSuppressAnimCompile> SuppressNestedCompiles = MakeUnique<FScopedSuppressAnimCompile>();

	TArray<FString> SpawnedGuids;
	TArray<FString> SpawnedTypes;
	TArray<TSharedPtr<FJsonValue>> ResultNodes;

	for (const TSharedPtr<FJsonValue>& Item : ChainArr)
	{
		TSharedPtr<FJsonObject> NodeDesc = Item->AsObject();
		if (!NodeDesc.IsValid()) continue;
		FString Type; NodeDesc->TryGetStringField(TEXT("type"), Type);
		double PX = 0, PY = 0;
		NodeDesc->TryGetNumberField(TEXT("pos_x"), PX);
		NodeDesc->TryGetNumberField(TEXT("pos_y"), PY);

		FString NodeJson, NodeErr;
		if (Type == TEXT("state_machine"))
		{
			FString Name = TEXT("NewStateMachine"); NodeDesc->TryGetStringField(TEXT("name"), Name);
			HandleAddStateMachine(AnimBPPath, Name, (int32)PX, (int32)PY, NodeJson, NodeErr);
		}
		else if (Type == TEXT("slot"))
		{
			FString SN = TEXT("DefaultSlot"); NodeDesc->TryGetStringField(TEXT("slot_name"), SN);
			HandleAddSlotNode(AnimBPPath, SN, (int32)PX, (int32)PY, NodeJson, NodeErr);
		}
		else if (Type == TEXT("local_to_component_space"))
			HandleAddLocalToComponentSpace(AnimBPPath, (int32)PX, (int32)PY, NodeJson, NodeErr);
		else if (Type == TEXT("component_to_local_space"))
			HandleAddComponentToLocalSpace(AnimBPPath, (int32)PX, (int32)PY, NodeJson, NodeErr);
		else if (Type == TEXT("two_way_blend"))
			HandleAddTwoWayBlend(AnimBPPath, (int32)PX, (int32)PY, NodeJson, NodeErr);
		else if (Type == TEXT("spring_bone"))
		{
			FString BN; NodeDesc->TryGetStringField(TEXT("bone_name"), BN);
			HandleAddSpringBone(AnimBPPath, BN, (int32)PX, (int32)PY, NodeJson, NodeErr);
		}
		else if (Type == TEXT("fabrik"))
		{
			FString TB, RB; NodeDesc->TryGetStringField(TEXT("tip_bone"), TB); NodeDesc->TryGetStringField(TEXT("root_bone"), RB);
			HandleAddFabrik(AnimBPPath, TB, RB, (int32)PX, (int32)PY, NodeJson, NodeErr);
		}
		else if (Type == TEXT("ccdik"))
		{
			FString TB, RB; NodeDesc->TryGetStringField(TEXT("tip_bone"), TB); NodeDesc->TryGetStringField(TEXT("root_bone"), RB);
			HandleAddCCDIK(AnimBPPath, TB, RB, (int32)PX, (int32)PY, NodeJson, NodeErr);
		}
		else if (Type == TEXT("leg_ik"))
			HandleAddLegIK(AnimBPPath, (int32)PX, (int32)PY, NodeJson, NodeErr);
		else if (Type == TEXT("rigid_body"))
			HandleAddRigidBody(AnimBPPath, (int32)PX, (int32)PY, NodeJson, NodeErr);
		else if (Type == TEXT("blend_by_bool"))
			HandleAddBlendByBool(AnimBPPath, (int32)PX, (int32)PY, NodeJson, NodeErr);
		else if (Type == TEXT("blend_by_int"))
		{
			double NumPoses = 2; NodeDesc->TryGetNumberField(TEXT("num_poses"), NumPoses);
			HandleAddBlendByInt(AnimBPPath, (int32)NumPoses, (int32)PX, (int32)PY, NodeJson, NodeErr);
		}
		else if (Type == TEXT("layered_blend_per_bone"))
		{
			double NL = 1; NodeDesc->TryGetNumberField(TEXT("num_layers"), NL);
			HandleAddLayeredBlendPerBone(AnimBPPath, (int32)NL, (int32)PX, (int32)PY, NodeJson, NodeErr);
		}
		else if (Type == TEXT("apply_additive"))
			HandleAddApplyAdditive(AnimBPPath, (int32)PX, (int32)PY, NodeJson, NodeErr);
		else if (Type == TEXT("apply_mesh_space_additive"))
			HandleAddApplyMeshSpaceAdditive(AnimBPPath, (int32)PX, (int32)PY, NodeJson, NodeErr);
		else if (Type == TEXT("inertialization"))
			HandleAddInertialization(AnimBPPath, (int32)PX, (int32)PY, NodeJson, NodeErr);
		else if (Type == TEXT("modify_bone"))
		{
			FString BN; NodeDesc->TryGetStringField(TEXT("bone_name"), BN);
			HandleAddModifyBone(AnimBPPath, BN, (int32)PX, (int32)PY, NodeJson, NodeErr);
		}
		else if (Type == TEXT("sequence_player"))
		{
			FString AP; NodeDesc->TryGetStringField(TEXT("animation_path"), AP);
			HandleAddSequencePlayer(AnimBPPath, AP, (int32)PX, (int32)PY, NodeJson, NodeErr);
		}
		else if (Type == TEXT("sequence_evaluator"))
		{
			FString AP; NodeDesc->TryGetStringField(TEXT("animation_path"), AP);
			HandleAddSequenceEvaluator(AnimBPPath, AP, (int32)PX, (int32)PY, NodeJson, NodeErr);
		}
		else if (Type == TEXT("blendspace_player") || Type == TEXT("blend_space_player"))
		{
			FString BSP;
			if (!NodeDesc->TryGetStringField(TEXT("blendspace_path"), BSP)) NodeDesc->TryGetStringField(TEXT("animation_path"), BSP);
			HandleAddBlendSpacePlayer(AnimBPPath, BSP, (int32)PX, (int32)PY, NodeJson, NodeErr);
		}
		else if (Type == TEXT("saved_pose"))
		{
			FString CN; NodeDesc->TryGetStringField(TEXT("cache_name"), CN);
			HandleAddSavedPose(AnimBPPath, CN, (int32)PX, (int32)PY, NodeJson, NodeErr);
		}
		else if (Type == TEXT("use_cached_pose"))
		{
			FString CN; NodeDesc->TryGetStringField(TEXT("cache_name"), CN);
			HandleUseCachedPose(AnimBPPath, CN, (int32)PX, (int32)PY, NodeJson, NodeErr);
		}
		else if (Type == TEXT("mirror"))
		{
			FString MDT; NodeDesc->TryGetStringField(TEXT("mirror_data_table_path"), MDT);
			HandleAddMirror(AnimBPPath, MDT, (int32)PX, (int32)PY, NodeJson, NodeErr);
		}
		else if (Type == TEXT("aim_offset_player"))
		{
			FString AP; NodeDesc->TryGetStringField(TEXT("animation_path"), AP);
			HandleAddAimOffsetPlayer(AnimBPPath, AP, (int32)PX, (int32)PY, NodeJson, NodeErr);
		}
		else if (Type == TEXT("motion_matching"))
		{
			FString PSDB; NodeDesc->TryGetStringField(TEXT("pose_search_db_path"), PSDB);
			HandleAddMotionMatchingNode(AnimBPPath, PSDB, (int32)PX, (int32)PY, NodeJson, NodeErr);
		}
		else if (Type == TEXT("control_rig"))
		{
			FString CRP; NodeDesc->TryGetStringField(TEXT("control_rig_path"), CRP);
			HandleAddControlRigNode(AnimBPPath, CRP, (int32)PX, (int32)PY, NodeJson, NodeErr);
		}
		else
		{
			NodeErr = FString::Printf(TEXT("Unknown chain node type: %s"), *Type);
		}

		if (!NodeErr.IsEmpty())
		{
			SetError(FString::Printf(TEXT("Failed to spawn chain node %d ('%s'): %s"), SpawnedGuids.Num(), *Type, *NodeErr), OutJson, OutError);
			return;
		}

		TSharedPtr<FJsonObject> SpawnResult;
		TSharedRef<TJsonReader<>> R2 = TJsonReaderFactory<>::Create(NodeJson);
		FString Guid;
		if (FJsonSerializer::Deserialize(R2, SpawnResult) && SpawnResult.IsValid())
			SpawnResult->TryGetStringField(TEXT("node_guid"), Guid);
		if (Guid.IsEmpty())
		{
			SetError(FString::Printf(TEXT("Chain node %d ('%s') spawned but returned no node_guid; aborting before wiring."), SpawnedGuids.Num(), *Type), OutJson, OutError);
			return;
		}

		SpawnedGuids.Add(Guid);
		SpawnedTypes.Add(Type);

		TSharedPtr<FJsonObject> Entry = MakeShareable(new FJsonObject);
		Entry->SetStringField(TEXT("type"), Type);
		Entry->SetStringField(TEXT("guid"), Guid);
		ResultNodes.Add(MakeShareable(new FJsonValueObject(Entry)));
	}

	// Wire the chain in order; every edge is reported and one failure flips success=false.
	TArray<TSharedPtr<FJsonValue>> Connections;
	bool bAllConnected = true;
	auto RecordConnection = [&](const FString& FromGuid, const FString& FromType, const FString& ToGuid, const FString& ToType)
	{
		FString ConnJson, ConnErr;
		HandleConnectAnimNodes(AnimBPPath, FromGuid, ToGuid, ConnJson, ConnErr);
		const bool bOk = ConnErr.IsEmpty();
		if (!bOk) bAllConnected = false;

		TSharedPtr<FJsonObject> C = MakeShareable(new FJsonObject);
		C->SetStringField(TEXT("from_guid"), FromGuid);
		C->SetStringField(TEXT("from_type"), FromType);
		C->SetStringField(TEXT("to_guid"), ToGuid);
		C->SetStringField(TEXT("to_type"), ToType);
		C->SetBoolField(TEXT("connected"), bOk);
		if (!bOk) C->SetStringField(TEXT("error"), ConnErr);
		Connections.Add(MakeShareable(new FJsonValueObject(C)));
	};

	for (int32 i = 0; i < SpawnedGuids.Num() - 1; ++i)
	{
		RecordConnection(SpawnedGuids[i], SpawnedTypes[i], SpawnedGuids[i + 1], SpawnedTypes[i + 1]);
	}

	bool bOutputRequested = false;
	if (bAutoConnectToOutput && SpawnedGuids.Num() > 0)
	{
		bOutputRequested = true;
		TArray<UAnimGraphNode_Root*> Roots;
		AnimGraph->GetNodesOfClass<UAnimGraphNode_Root>(Roots);
		Roots.RemoveAll([](UAnimGraphNode_Root* N){ return N->GetClass() != UAnimGraphNode_Root::StaticClass(); });
		if (Roots.Num() > 0)
		{
			RecordConnection(SpawnedGuids.Last(), SpawnedTypes.Last(), Roots[0]->NodeGuid.ToString(), TEXT("output_pose"));
		}
		else
		{
			bAllConnected = false;
			TSharedPtr<FJsonObject> C = MakeShareable(new FJsonObject);
			C->SetStringField(TEXT("from_guid"), SpawnedGuids.Last());
			C->SetStringField(TEXT("from_type"), SpawnedTypes.Last());
			C->SetStringField(TEXT("to_type"), TEXT("output_pose"));
			C->SetBoolField(TEXT("connected"), false);
			C->SetStringField(TEXT("error"), TEXT("AnimGraph has no Output Pose (Root) node."));
			Connections.Add(MakeShareable(new FJsonValueObject(C)));
		}
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBP);
	AnimGraph->NotifyGraphChanged();
	RefreshOpenAnimBlueprintEditor(AnimBP);

	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField(TEXT("success"), bAllConnected);
	if (!bAllConnected)
		Obj->SetStringField(TEXT("error"), TEXT("One or more chain connections failed; see connections[] (nodes were still spawned)."));
	Obj->SetArrayField(TEXT("nodes"), ResultNodes);
	Obj->SetNumberField(TEXT("count"), SpawnedGuids.Num());
	Obj->SetArrayField(TEXT("connections"), Connections);
	Obj->SetBoolField(TEXT("auto_connect_to_output"), bOutputRequested);
	// Leave the suppression scope so the single final compile actually runs.
	SuppressNestedCompiles.Reset();
	CompileAnimBlueprintAndReport(AnimBP, Obj);
	UEditorAssetLibrary::SaveAsset(AnimBPPath, false);
	BuildSuccessJson(Obj, OutJson);
	if (!bAllConnected && OutError.IsEmpty())
		OutError = TEXT("build_anim_chain: one or more connections failed (see connections[]).");
}

void HandleAddControlRigNode(const FString& AnimBPPath, const FString& ControlRigPath,
	int32 PosX, int32 PosY, FString& OutJson, FString& OutError)
{
	UAnimBlueprint* AnimBP = LoadObject<UAnimBlueprint>(nullptr, *AnimBPPath);
	if (!AnimBP) { SetError(FString::Printf(TEXT("AnimBlueprint not found: %s"), *AnimBPPath), OutJson, OutError); return; }
	UEdGraph* AnimGraph = FindAnimGraph(AnimBP);
	if (!AnimGraph) { SetError(TEXT("AnimGraph not found."), OutJson, OutError); return; }

	static UClass* AnimGNCRClass = nullptr;
	if (!AnimGNCRClass)
	{
		for (TObjectIterator<UClass> It; It; ++It)
			if (It->GetName() == TEXT("AnimGraphNode_ControlRig")) { AnimGNCRClass = *It; break; }
	}
	if (!AnimGNCRClass) { SetError(TEXT("AnimGraphNode_ControlRig class not found (ControlRig plugin required)."), OutJson, OutError); return; }

	UAnimGraphNode_Base* NodeTemplate = NewObject<UAnimGraphNode_Base>(GetTransientPackage(), AnimGNCRClass);
	if (!NodeTemplate) { SetError(TEXT("Failed to create ControlRig AnimGraph node template."), OutJson, OutError); return; }

	bool bControlRigBound = false;
	if (!ControlRigPath.IsEmpty())
	{
		static UClass* CRBPClass = nullptr;
		if (!CRBPClass)
			for (TObjectIterator<UClass> It; It; ++It)
				if (It->GetName() == TEXT("ControlRigBlueprint")) { CRBPClass = *It; break; }

		if (!CRBPClass)
		{
			SetError(TEXT("ControlRigBlueprint class not found (ControlRig plugin required)."), OutJson, OutError);
			return;
		}

		UObject* CRBPObj = StaticLoadObject(CRBPClass, nullptr, *ControlRigPath);
		UBlueprint* CRBP = Cast<UBlueprint>(CRBPObj);
		if (!CRBP || !CRBP->GeneratedClass)
		{
			SetError(FString::Printf(
				TEXT("Could not load a Control Rig Blueprint at control_rig_path '%s' (or it has no compiled class). Pass the asset path of a Control Rig (compile it first if it has never been compiled)."),
				*ControlRigPath), OutJson, OutError);
			return;
		}

		UClass* CRClass = CRBP->GeneratedClass;
		FObjectPropertyBase* Prop = FindFProperty<FObjectPropertyBase>(
			FAnimNode_ControlRig::StaticStruct(), TEXT("ControlRigClass"));
		FStructProperty* NodeProp = CastField<FStructProperty>(
			NodeTemplate->GetClass()->FindPropertyByName(TEXT("Node")));
		if (!Prop || !NodeProp)
		{
			SetError(TEXT("ControlRig AnimGraph node is missing the expected Node.ControlRigClass property (engine version mismatch)."), OutJson, OutError);
			return;
		}

		void* NodePtr = NodeProp->ContainerPtrToValuePtr<void>(NodeTemplate);
		Prop->SetObjectPropertyValue(Prop->ContainerPtrToValuePtr<void>(NodePtr), CRClass);
		bControlRigBound = Prop->GetObjectPropertyValue(Prop->ContainerPtrToValuePtr<void>(NodePtr)) == CRClass;
		if (!bControlRigBound)
		{
			SetError(FString::Printf(
				TEXT("Failed to bind ControlRigClass on the AnimGraph node for '%s' (the property write did not land)."),
				*ControlRigPath), OutJson, OutError);
			return;
		}
	}

	FEdGraphSchemaAction_K2NewNode Action; Action.NodeTemplate = NodeTemplate;
	UEdGraphNode* SpawnedNode = Action.PerformAction(AnimGraph, nullptr, FUECPGraphLoc((float)PosX, (float)PosY), false);
	if (!SpawnedNode) { SetError(TEXT("Failed to spawn ControlRig AnimGraph node."), OutJson, OutError); return; }

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBP);
	AnimGraph->NotifyGraphChanged();
	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("node_guid"), SpawnedNode->NodeGuid.ToString());
	Obj->SetStringField(TEXT("control_rig_path"), ControlRigPath);
	Obj->SetBoolField(TEXT("control_rig_bound"), bControlRigBound);
	CompileAnimBlueprintAndReport(AnimBP, Obj);
	UEditorAssetLibrary::SaveAsset(AnimBPPath, false);
	BuildSuccessJson(Obj, OutJson);
}

void HandleGetAnimMontageSections(const FString& MontagePath, FString& OutJson, FString& OutError)
{
	UAnimMontage* Montage = LoadObject<UAnimMontage>(nullptr, *MontagePath);
	if (!Montage) { OutError = FString::Printf(TEXT("Failed to load AnimMontage: %s"), *MontagePath); return; }

	TArray<TSharedPtr<FJsonValue>> Sections;
	for (int32 i = 0; i < Montage->CompositeSections.Num(); ++i)
	{
		const FCompositeSection& Section = Montage->CompositeSections[i];
		TSharedPtr<FJsonObject> SObj = MakeShareable(new FJsonObject);
		SObj->SetStringField(TEXT("name"), Section.SectionName.ToString());
		SObj->SetNumberField(TEXT("start_time"), Section.GetTime());

		FString NextName = TEXT("None");
		int32 NextIdx = Montage->GetSectionIndex(Section.NextSectionName);
		if (NextIdx != INDEX_NONE && NextIdx < Montage->CompositeSections.Num())
			NextName = Montage->CompositeSections[NextIdx].SectionName.ToString();
		else if (!Section.NextSectionName.IsNone())
			NextName = Section.NextSectionName.ToString();
		SObj->SetStringField(TEXT("next_section"), NextName);
		Sections.Add(MakeShareable(new FJsonValueObject(SObj)));
	}

	TSharedPtr<FJsonObject> Root = MakeShareable(new FJsonObject);
	Root->SetBoolField(TEXT("success"), true);
	Root->SetNumberField(TEXT("section_count"), Sections.Num());
	Root->SetArrayField(TEXT("sections"), Sections);
	FString Str; TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Str);
	FJsonSerializer::Serialize(Root.ToSharedRef(), W); OutJson = Str;
}

void HandleSetMontageSectionLink(const FString& MontagePath, const FString& SectionName, const FString& NextSectionName, FString& OutJson, FString& OutError)
{
	UAnimMontage* Montage = LoadObject<UAnimMontage>(nullptr, *MontagePath);
	if (!Montage) { OutError = FString::Printf(TEXT("Failed to load AnimMontage: %s"), *MontagePath); return; }

	const bool bClearLink = NextSectionName.IsEmpty() || NextSectionName.Equals(TEXT("None"), ESearchCase::IgnoreCase);
	if (!bClearLink)
	{
		bool bDestExists = false;
		for (const FCompositeSection& S : Montage->CompositeSections)
		{
			if (S.SectionName.ToString().Equals(NextSectionName, ESearchCase::IgnoreCase)) { bDestExists = true; break; }
		}
		if (!bDestExists)
		{
			OutError = FString::Printf(TEXT("Target section '%s' not found in montage — cannot link"), *NextSectionName);
			return;
		}
	}

	bool bFound = false;
	for (FCompositeSection& Section : Montage->CompositeSections)
	{
		if (Section.SectionName.ToString().Equals(SectionName, ESearchCase::IgnoreCase))
		{
			Section.NextSectionName = bClearLink ? NAME_None : FName(*NextSectionName);
			bFound = true;
			break;
		}
	}
	if (!bFound) { OutError = FString::Printf(TEXT("Section '%s' not found in montage"), *SectionName); return; }

	Montage->PostEditChange();
	Montage->RefreshCacheData();
	Montage->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(MontagePath, false);

	OutJson = FString::Printf(TEXT("{\"success\":true,\"section\":\"%s\",\"next_section\":\"%s\"}"), *SectionName, *NextSectionName);
}

void HandleCreateSyncGroup(const FString& AnimBPPath, const FString& SyncGroupName, const FString& GroupRole, FString& OutJson, FString& OutError)
{
	const FString Role = GroupRole.IsEmpty() ? TEXT("CanBeLeader") : GroupRole;
	OutError = FString::Printf(TEXT("create_sync_group does not create anything — a sync group is bound per-node, not as a standalone asset. "
		"On each player node (sequence/blendspace) you want grouped, call set_anim_node_property with: "
		"property_name='GroupName' property_value='%s'; property_name='Method' property_value='SyncGroup' (sync is IGNORED unless Method=SyncGroup); "
		"property_name='GroupRole' property_value='%s' (valid: CanBeLeader, AlwaysFollower, AlwaysLeader, TransitionLeader, TransitionFollower). "
		"anim_blueprint_path='%s'"),
		*SyncGroupName, *Role, *AnimBPPath);
}

void HandleAddAnimSyncMarker(const FString& AnimPath, const FString& MarkerName, float Time, FString& OutJson, FString& OutError)
{
	if (MarkerName.IsEmpty()) { OutError = TEXT("marker_name is required"); return; }

	UAnimSequence* AnimSeq = Cast<UAnimSequence>(UEditorAssetLibrary::LoadAsset(AnimPath));
	if (!AnimSeq) { OutError = FString::Printf(TEXT("Could not load AnimSequence: %s"), *AnimPath); return; }

	const float SeqLen = AnimSeq->GetPlayLength();
	if (Time < 0.f || Time > SeqLen)
	{
		OutError = FString::Printf(TEXT("time %.3f is outside sequence range [0, %.3f]"), Time, SeqLen);
		return;
	}

	FAnimSyncMarker Marker;
	Marker.MarkerName = FName(*MarkerName);
	Marker.Time = Time;
	AnimSeq->AuthoredSyncMarkers.Add(Marker);
	AnimSeq->SortSyncMarkers();
	AnimSeq->RefreshSyncMarkerDataFromAuthored();
	AnimSeq->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(AnimPath, false);

	OutJson = FString::Printf(TEXT("{\"success\":true,\"marker_name\":\"%s\",\"time\":%.4f,\"total_markers\":%d}"),
		*MarkerName, Time, AnimSeq->AuthoredSyncMarkers.Num());
}

void HandleRemoveAnimSyncMarker(const FString& AnimPath, const FString& MarkerName, float Time, bool bMatchTime, FString& OutJson, FString& OutError)
{
	if (MarkerName.IsEmpty()) { OutError = TEXT("marker_name is required"); return; }

	UAnimSequence* AnimSeq = Cast<UAnimSequence>(UEditorAssetLibrary::LoadAsset(AnimPath));
	if (!AnimSeq) { OutError = FString::Printf(TEXT("Could not load AnimSequence: %s"), *AnimPath); return; }

	const FName Target(*MarkerName);
	int32 Removed = 0;
	if (bMatchTime)
	{
		for (int32 i = AnimSeq->AuthoredSyncMarkers.Num() - 1; i >= 0; --i)
		{
			const FAnimSyncMarker& M = AnimSeq->AuthoredSyncMarkers[i];
			if (M.MarkerName == Target && FMath::IsNearlyEqual(M.Time, Time, 0.001f))
			{
				AnimSeq->AuthoredSyncMarkers.RemoveAt(i);
				++Removed;
				break;
			}
		}
		if (Removed > 0) AnimSeq->RefreshSyncMarkerDataFromAuthored();
	}
	else
	{
		const int32 Before = AnimSeq->AuthoredSyncMarkers.Num();
		TArray<FName> ToRemove; ToRemove.Add(Target);
		AnimSeq->RemoveSyncMarkers(ToRemove);
		Removed = Before - AnimSeq->AuthoredSyncMarkers.Num();
	}

	if (Removed == 0)
	{
		OutError = FString::Printf(TEXT("No sync marker found matching name='%s'%s"),
			*MarkerName, bMatchTime ? *FString::Printf(TEXT(" time=%.3f"), Time) : TEXT(""));
		return;
	}

	AnimSeq->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(AnimPath, false);

	OutJson = FString::Printf(TEXT("{\"success\":true,\"marker_name\":\"%s\",\"removed\":%d,\"remaining\":%d}"),
		*MarkerName, Removed, AnimSeq->AuthoredSyncMarkers.Num());
}

void HandleListAnimSyncMarkers(const FString& AnimPath, FString& OutJson, FString& OutError)
{
	UAnimSequence* AnimSeq = Cast<UAnimSequence>(UEditorAssetLibrary::LoadAsset(AnimPath));
	if (!AnimSeq) { OutError = FString::Printf(TEXT("Could not load AnimSequence: %s"), *AnimPath); return; }

	TArray<TSharedPtr<FJsonValue>> MarkerArr;
	for (const FAnimSyncMarker& M : AnimSeq->AuthoredSyncMarkers)
	{
		TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
		Obj->SetStringField(TEXT("marker_name"), M.MarkerName.ToString());
		Obj->SetNumberField(TEXT("time"), M.Time);
		MarkerArr.Add(MakeShareable(new FJsonValueObject(Obj)));
	}

	TSharedPtr<FJsonObject> Root = MakeShareable(new FJsonObject);
	Root->SetBoolField(TEXT("success"), true);
	Root->SetStringField(TEXT("animation_path"), AnimPath);
	Root->SetNumberField(TEXT("count"), AnimSeq->AuthoredSyncMarkers.Num());
	Root->SetArrayField(TEXT("markers"), MarkerArr);
	BuildSuccessJson(Root, OutJson);
}

void HandleAddBlendProfile(const FString& SkeletonPath, const FString& ProfileName, FString& OutJson, FString& OutError)
{
	if (ProfileName.IsEmpty()) { OutError = TEXT("profile_name is required"); return; }

	USkeleton* Skeleton = LoadObject<USkeleton>(nullptr, *SkeletonPath);
	if (!Skeleton) { OutError = FString::Printf(TEXT("Failed to load Skeleton: %s"), *SkeletonPath); return; }

	for (UBlendProfile* BP : Skeleton->BlendProfiles)
	{
		if (BP && BP->GetFName().ToString().Equals(ProfileName, ESearchCase::IgnoreCase))
		{
			OutJson = FString::Printf(TEXT("{\"success\":true,\"already_exists\":true,\"profile_name\":\"%s\"}"), *ProfileName);
			return;
		}
	}

	UBlendProfile* NewProfile = NewObject<UBlendProfile>(Skeleton, FName(*ProfileName));
	if (!NewProfile) { OutError = TEXT("Failed to create BlendProfile"); return; }
	NewProfile->OwningSkeleton = Skeleton;
	Skeleton->BlendProfiles.Add(NewProfile);
	Skeleton->MarkPackageDirty();

	OutJson = FString::Printf(TEXT("{\"success\":true,\"profile_name\":\"%s\"}"), *ProfileName);
}

static TArray<TSharedPtr<FJsonValue>> BuildStateList(UAnimationStateMachineGraph* SMGraph)
{
	TArray<TSharedPtr<FJsonValue>> States;
	if (!SMGraph) return States;

	for (UEdGraphNode* SMInnerNode : SMGraph->Nodes)
	{
		UAnimStateNode* StateNode = Cast<UAnimStateNode>(SMInnerNode);
		if (!StateNode) continue;

		TSharedPtr<FJsonObject> S = MakeShareable(new FJsonObject);
		S->SetStringField(TEXT("name"), StateNode->GetStateName());
		S->SetStringField(TEXT("node_guid"), StateNode->NodeGuid.ToString());

		TArray<TSharedPtr<FJsonValue>> Transitions;
		for (UEdGraphPin* Pin : StateNode->Pins)
		{
			if (!Pin || Pin->Direction != EGPD_Output) continue;
			for (UEdGraphPin* Linked : Pin->LinkedTo)
			{
				if (!Linked || !Linked->GetOwningNode()) continue;
				UAnimStateTransitionNode* Tr = Cast<UAnimStateTransitionNode>(Linked->GetOwningNode());
				if (!Tr) continue;
				for (UEdGraphPin* TrPin : Tr->Pins)
				{
					if (!TrPin || TrPin->Direction != EGPD_Output) continue;
					for (UEdGraphPin* Dest : TrPin->LinkedTo)
					{
						if (!Dest || !Dest->GetOwningNode()) continue;
						UAnimStateNode* DestState = Cast<UAnimStateNode>(Dest->GetOwningNode());
						if (!DestState) continue;
						TSharedPtr<FJsonObject> T = MakeShareable(new FJsonObject);
						T->SetStringField(TEXT("to"), DestState->GetStateName());
						T->SetStringField(TEXT("transition_guid"), Tr->NodeGuid.ToString());
						T->SetBoolField(TEXT("bidirectional"), Tr->Bidirectional);
						Transitions.Add(MakeShareable(new FJsonValueObject(T)));
					}
				}
			}
		}
		S->SetArrayField(TEXT("transitions_to"), Transitions);
		States.Add(MakeShareable(new FJsonValueObject(S)));
	}
	return States;
}

static TArray<TSharedPtr<FJsonValue>> BuildConduitList(UAnimationStateMachineGraph* SMGraph)
{
	TArray<TSharedPtr<FJsonValue>> Conduits;
	if (!SMGraph) return Conduits;
	for (UEdGraphNode* N : SMGraph->Nodes)
	{
		UAnimStateConduitNode* Conduit = Cast<UAnimStateConduitNode>(N);
		if (!Conduit) continue;
		TSharedPtr<FJsonObject> C = MakeShareable(new FJsonObject);
		C->SetStringField(TEXT("name"), Conduit->GetStateName());
		C->SetStringField(TEXT("node_guid"), Conduit->NodeGuid.ToString());
		Conduits.Add(MakeShareable(new FJsonValueObject(C)));
	}
	return Conduits;
}

void HandleGetAnimStateMachines(const FString& AnimBPPath, FString& OutJson, FString& OutError)
{
	UAnimBlueprint* AnimBP = LoadObject<UAnimBlueprint>(nullptr, *AnimBPPath);
	if (!AnimBP) { OutError = FString::Printf(TEXT("Failed to load AnimBlueprint: %s"), *AnimBPPath); return; }

	TArray<TSharedPtr<FJsonValue>> StateMachines;

	// State machine graphs are sub-graphs of the AnimGraph (or of state/layer graphs), never
	// top-level FunctionGraphs — walk every graph the blueprint owns.
	TArray<UEdGraph*> Graphs;
	AnimBP->GetAllGraphs(Graphs);
	for (UEdGraph* Graph : Graphs)
	{
		UAnimationStateMachineGraph* SMGraph = Cast<UAnimationStateMachineGraph>(Graph);
		if (!SMGraph) continue;

		TArray<TSharedPtr<FJsonValue>> States   = BuildStateList(SMGraph);
		TArray<TSharedPtr<FJsonValue>> Conduits = BuildConduitList(SMGraph);

		TSharedPtr<FJsonObject> SMObj = MakeShareable(new FJsonObject);
		SMObj->SetStringField(TEXT("name"), SMGraph->GetName());
		if (UEdGraph* Outer = Cast<UEdGraph>(SMGraph->GetOuter()))
			SMObj->SetStringField(TEXT("parent_graph"), Outer->GetName());
		if (SMGraph->OwnerAnimGraphNode)
			SMObj->SetStringField(TEXT("owner_node_guid"), SMGraph->OwnerAnimGraphNode->NodeGuid.ToString());
		SMObj->SetArrayField(TEXT("states"), States);
		SMObj->SetArrayField(TEXT("conduits"), Conduits);
		SMObj->SetNumberField(TEXT("state_count"), States.Num());
		StateMachines.Add(MakeShareable(new FJsonValueObject(SMObj)));
	}

	TSharedPtr<FJsonObject> Root = MakeShareable(new FJsonObject);
	Root->SetBoolField(TEXT("success"), true);
	Root->SetStringField(TEXT("blueprint_path"), AnimBPPath);
	Root->SetArrayField(TEXT("state_machines"), StateMachines);
	Root->SetNumberField(TEXT("state_machine_count"), StateMachines.Num());
	FString Str; TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Str);
	FJsonSerializer::Serialize(Root.ToSharedRef(), W); OutJson = Str;
}

static TArray<FString> ParseScalarArrayValue(const FString& In)
{
	FString S = In.TrimStartAndEnd();
	if ((S.StartsWith(TEXT("(")) && S.EndsWith(TEXT(")")))
	 || (S.StartsWith(TEXT("[")) && S.EndsWith(TEXT("]"))))
	{
		S = S.Mid(1, S.Len() - 2);
	}
	TArray<FString> Parts; S.ParseIntoArray(Parts, TEXT(","), true);
	for (FString& P : Parts) P.TrimStartAndEndInline();
	return Parts;
}

// Resolves an enumerator by name (case-insensitive). Accepts "Value", "EnumType::Value" and a
// numeric value. Returns INDEX_NONE when nothing matches.
static int64 ResolveEnumValueByName(const UEnum* Enum, const FString& InValue)
{
	if (!Enum) return INDEX_NONE;
	FString Value = InValue.TrimStartAndEnd();
	if (Value.IsNumeric())
	{
		const int64 Numeric = FCString::Atoi64(*Value);
		return Enum->IsValidEnumValue(Numeric) ? Numeric : INDEX_NONE;
	}
	int32 ScopeIdx = INDEX_NONE;
	if (Value.FindLastChar(TEXT(':'), ScopeIdx)) Value = Value.Mid(ScopeIdx + 1);
	if (Value.FindLastChar(TEXT('.'), ScopeIdx))  Value = Value.Mid(ScopeIdx + 1);

	for (int32 i = 0; i < Enum->NumEnums(); ++i)
	{
		const FString Short = Enum->GetNameStringByIndex(i);
		const FString Display = Enum->GetDisplayNameTextByIndex(i).ToString();
		if (Short.Equals(Value, ESearchCase::IgnoreCase) || Display.Equals(Value, ESearchCase::IgnoreCase))
			return Enum->GetValueByIndex(i);
	}
	return INDEX_NONE;
}

static bool TryAssignAnimProperty(FProperty* Prop, void* Container, const FString& PropertyValue, FString* OutError = nullptr)
{
	if (FStrProperty* SP = CastField<FStrProperty>(Prop))    { SP->SetPropertyValue_InContainer(Container, PropertyValue); return true; }
	if (FNameProperty* NP = CastField<FNameProperty>(Prop))  { NP->SetPropertyValue_InContainer(Container, FName(*PropertyValue)); return true; }
	if (FBoolProperty* BP = CastField<FBoolProperty>(Prop))  { BP->SetPropertyValue_InContainer(Container, PropertyValue.ToBool()); return true; }
	if (FFloatProperty* FP = CastField<FFloatProperty>(Prop)) { FP->SetPropertyValue_InContainer(Container, FCString::Atof(*PropertyValue)); return true; }
	if (FDoubleProperty* DP = CastField<FDoubleProperty>(Prop)) { DP->SetPropertyValue_InContainer(Container, FCString::Atod(*PropertyValue)); return true; }
	if (FIntProperty* IP = CastField<FIntProperty>(Prop))    { IP->SetPropertyValue_InContainer(Container, FCString::Atoi(*PropertyValue)); return true; }
	if (FEnumProperty* EP = CastField<FEnumProperty>(Prop))
	{
		const int64 Value = ResolveEnumValueByName(EP->GetEnum(), PropertyValue);
		if (Value == INDEX_NONE)
		{
			if (OutError)
			{
				TArray<FString> Names;
				if (const UEnum* E = EP->GetEnum())
					for (int32 i = 0; i < E->NumEnums() - 1; ++i) Names.Add(E->GetNameStringByIndex(i));
				*OutError = FString::Printf(TEXT("'%s' is not an enumerator of %s. Valid: [%s]"), *PropertyValue,
					EP->GetEnum() ? *EP->GetEnum()->GetName() : TEXT("enum"), *FString::Join(Names, TEXT(", ")));
			}
			return false;
		}
		EP->GetUnderlyingProperty()->SetIntPropertyValue(EP->ContainerPtrToValuePtr<void>(Container), Value);
		return true;
	}
	if (FByteProperty* ByP = CastField<FByteProperty>(Prop))
	{
		if (ByP->Enum)
		{
			const int64 Value = ResolveEnumValueByName(ByP->Enum, PropertyValue);
			if (Value == INDEX_NONE)
			{
				if (OutError)
				{
					TArray<FString> Names;
					for (int32 i = 0; i < ByP->Enum->NumEnums() - 1; ++i) Names.Add(ByP->Enum->GetNameStringByIndex(i));
					*OutError = FString::Printf(TEXT("'%s' is not an enumerator of %s. Valid: [%s]"), *PropertyValue, *ByP->Enum->GetName(), *FString::Join(Names, TEXT(", ")));
				}
				return false;
			}
			ByP->SetPropertyValue_InContainer(Container, (uint8)Value);
			return true;
		}
		ByP->SetPropertyValue_InContainer(Container, (uint8)FCString::Atoi(*PropertyValue));
		return true;
	}
	if (FObjectPropertyBase* OP = CastField<FObjectPropertyBase>(Prop))
	{
		UObject* Obj = nullptr;
		if (!PropertyValue.IsEmpty() && !PropertyValue.Equals(TEXT("None"), ESearchCase::IgnoreCase))
		{
			Obj = LoadObject<UObject>(nullptr, *PropertyValue);
			if (!Obj)
			{
				// Class references (TSubclassOf) accept a Blueprint asset path via its _C class.
				if (OP->PropertyClass && OP->PropertyClass->IsChildOf(UClass::StaticClass()))
					Obj = LoadClass<UObject>(nullptr, *(PropertyValue + TEXT("_C")));
				if (!Obj) Obj = LoadObject<UObject>(nullptr, *(PropertyValue + TEXT(".") + FPackageName::GetShortName(PropertyValue)));
			}
			if (!Obj)
			{
				if (OutError) *OutError = FString::Printf(TEXT("Could not load asset/object '%s' for property '%s'."), *PropertyValue, *Prop->GetName());
				return false;
			}
			if (OP->PropertyClass && !Obj->IsA(OP->PropertyClass) && !(Obj->IsA<UClass>() && OP->PropertyClass->IsChildOf(UClass::StaticClass())))
			{
				if (OutError) *OutError = FString::Printf(TEXT("'%s' is a %s but property '%s' expects a %s."), *PropertyValue, *Obj->GetClass()->GetName(), *Prop->GetName(), *OP->PropertyClass->GetName());
				return false;
			}
		}
		OP->SetObjectPropertyValue_InContainer(Container, Obj);
		return true;
	}
	if (FStructProperty* StP = CastField<FStructProperty>(Prop))
	{
		// Struct values use the standard UE text import syntax, e.g. (X=1,Y=2,Z=3) or (BoneName="hand_r").
		void* ValuePtr = StP->ContainerPtrToValuePtr<void>(Container);
		const TCHAR* Result = StP->ImportText_Direct(*PropertyValue, ValuePtr, nullptr, PPF_None, nullptr);
		if (!Result)
		{
			if (OutError) *OutError = FString::Printf(TEXT("ImportText failed for struct property '%s' (%s) with value '%s'; use UE export syntax such as (X=1,Y=2,Z=3) or (BoneName=\"hand_r\")."),
				*Prop->GetName(), StP->Struct ? *StP->Struct->GetName() : TEXT("?"), *PropertyValue);
			return false;
		}
		return true;
	}
	if (FArrayProperty* AP = CastField<FArrayProperty>(Prop))
	{
		FScriptArrayHelper Helper(AP, AP->ContainerPtrToValuePtr<void>(Container));
		const TArray<FString> Parts = ParseScalarArrayValue(PropertyValue);
		Helper.Resize(Parts.Num());
		FProperty* Inner = AP->Inner;
		for (int32 i = 0; i < Parts.Num(); i++)
		{
			void* ElemPtr = Helper.GetRawPtr(i);
			if (FFloatProperty* IFP = CastField<FFloatProperty>(Inner))      { IFP->SetPropertyValue(ElemPtr, FCString::Atof(*Parts[i])); }
			else if (FDoubleProperty* IDP = CastField<FDoubleProperty>(Inner)){ IDP->SetPropertyValue(ElemPtr, FCString::Atod(*Parts[i])); }
			else if (FIntProperty* IIP = CastField<FIntProperty>(Inner))      { IIP->SetPropertyValue(ElemPtr, FCString::Atoi(*Parts[i])); }
			else if (FBoolProperty* IBP = CastField<FBoolProperty>(Inner))    { IBP->SetPropertyValue(ElemPtr, Parts[i].ToBool()); }
			else if (FNameProperty* INP = CastField<FNameProperty>(Inner))    { INP->SetPropertyValue(ElemPtr, FName(*Parts[i])); }
			else if (FStrProperty* ISP = CastField<FStrProperty>(Inner))      { ISP->SetPropertyValue(ElemPtr, Parts[i]); }
			else { return false; }
		}
		return true;
	}
	if (OutError) *OutError = FString::Printf(TEXT("Unsupported property type %s for '%s'."), *Prop->GetClass()->GetName(), *Prop->GetName());
	return false;
}

void HandleSetAnimNodeProperty(const FString& AnimBPPath, const FString& NodeGuid, const FString& PropertyName, const FString& PropertyValue, FString& OutJson, FString& OutError)
{
	UAnimBlueprint* AnimBP = LoadObject<UAnimBlueprint>(nullptr, *AnimBPPath);
	if (!AnimBP) { SetError(FString::Printf(TEXT("Failed to load AnimBlueprint: %s"), *AnimBPPath), OutJson, OutError); return; }

	FGuid TargetGuid;
	if (!FGuid::Parse(NodeGuid, TargetGuid)) { SetError(TEXT("Invalid node_guid format"), OutJson, OutError); return; }

	// Nodes can live in the AnimGraph, inside state graphs, transition graphs or layer graphs.
	UEdGraph* OwningGraph = nullptr;
	UEdGraphNode* TargetNode = FindNodeByGuidInAllGraphs(AnimBP, TargetGuid, &OwningGraph);
	if (!TargetNode)
	{
		SetError(FString::Printf(TEXT("Node with GUID %s not found in any graph of '%s' (use get_anim_graph_nodes include_nested=true)."), *NodeGuid, *AnimBP->GetName()), OutJson, OutError);
		return;
	}

	TargetNode->Modify();

	FString AssignError;
	FString Location;
	FProperty* Prop = TargetNode->GetClass()->FindPropertyByName(FName(*PropertyName));
	if (Prop)
	{
		Location = TEXT("node");
		if (!TryAssignAnimProperty(Prop, TargetNode, PropertyValue, &AssignError))
		{
			SetError(AssignError, OutJson, OutError);
			return;
		}
	}
	else
	{
		FStructProperty* NodeProp = CastField<FStructProperty>(TargetNode->GetClass()->FindPropertyByName(TEXT("Node")));
		if (NodeProp) Prop = NodeProp->Struct->FindPropertyByName(FName(*PropertyName));
		if (!NodeProp || !Prop)
		{
			TArray<FString> Available;
			for (TFieldIterator<FProperty> It(TargetNode->GetClass()); It; ++It)
				if (It->HasAnyPropertyFlags(CPF_Edit)) Available.Add(It->GetName());
			if (NodeProp)
				for (TFieldIterator<FProperty> It(NodeProp->Struct); It; ++It)
					if (It->HasAnyPropertyFlags(CPF_Edit)) Available.Add(TEXT("Node.") + It->GetName());
			SetError(FString::Printf(TEXT("Property '%s' not found on node %s (%s). Editable: [%s]"),
				*PropertyName, *NodeGuid, *TargetNode->GetClass()->GetName(), *FString::Join(Available, TEXT(", "))), OutJson, OutError);
			return;
		}
		Location = TEXT("Node");
		void* NodePtr = NodeProp->ContainerPtrToValuePtr<void>(TargetNode);
		if (!TryAssignAnimProperty(Prop, NodePtr, PropertyValue, &AssignError))
		{
			SetError(AssignError, OutJson, OutError);
			return;
		}
	}

	// Pin layout / exposed defaults may depend on the property (asset players, enum blend lists,
	// blend-space dimensionality...) so rebuild the node and treat the change as structural.
	TargetNode->ReconstructNode();
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBP);
	if (OwningGraph) OwningGraph->NotifyGraphChanged();

	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("node_guid"), NodeGuid);
	Obj->SetStringField(TEXT("node_class"), TargetNode->GetClass()->GetName());
	Obj->SetStringField(TEXT("graph"), OwningGraph ? OwningGraph->GetName() : FString());
	Obj->SetStringField(TEXT("property"), PropertyName);
	Obj->SetStringField(TEXT("property_type"), Prop->GetClass()->GetName());
	Obj->SetStringField(TEXT("location"), Location);
	Obj->SetStringField(TEXT("value"), PropertyValue);
	CompileAnimBlueprintAndReport(AnimBP, Obj);
	UEditorAssetLibrary::SaveAsset(AnimBPPath, false);
	BuildSuccessJson(Obj, OutJson);
}

static UClass* FindMotionWarpingNotifyClass()
{
	UClass* C = FindObject<UClass>(nullptr, TEXT("/Script/MotionWarping.AnimNotifyState_MotionWarping"));
	if (!C)
		C = FindFirstObject<UClass>(TEXT("AnimNotifyState_MotionWarping"), EFindFirstObjectOptions::None, ELogVerbosity::NoLogging);
	return C;
}

static FName GetWarpTargetFromNotifyState(UAnimNotifyState* NS)
{
	if (!NS) return NAME_None;
	static const TCHAR* PropNames[] = { TEXT("WarpTargetName"), TEXT("MotionWarpingTargetName"), TEXT("TargetName"), nullptr };
	for (int32 i = 0; PropNames[i]; ++i)
	{
		if (FNameProperty* P = FindFProperty<FNameProperty>(NS->GetClass(), PropNames[i]))
			return P->GetPropertyValue_InContainer(NS);
	}
	return NAME_None;
}

static void SetWarpTargetOnNotifyState(UAnimNotifyState* NS, const FString& WarpTargetName)
{
	if (!NS) return;
	static const TCHAR* PropNames[] = { TEXT("WarpTargetName"), TEXT("MotionWarpingTargetName"), TEXT("TargetName"), nullptr };
	for (int32 i = 0; PropNames[i]; ++i)
	{
		if (FNameProperty* P = FindFProperty<FNameProperty>(NS->GetClass(), PropNames[i]))
		{
			P->SetPropertyValue_InContainer(NS, FName(*WarpTargetName));
			return;
		}
	}
}

void HandleAddMotionWarpingWindow(const FString& MontagePath, const FString& WarpTargetName,
	float StartTime, float EndTime, FString& OutJson, FString& OutError)
{

	UAnimSequenceBase* AnimSeq = Cast<UAnimSequenceBase>(UEditorAssetLibrary::LoadAsset(MontagePath));
	if (!AnimSeq) { OutError = FString::Printf(TEXT("Cannot load animation: %s"), *MontagePath); return; }

	UClass* WarpClass = FindMotionWarpingNotifyClass();
	if (!WarpClass) { OutError = TEXT("AnimNotifyState_MotionWarping not found. Enable the MotionWarping plugin."); return; }

	FAnimNotifyEvent NewNotify;
	NewNotify.NotifyName = FName(*(FString(TEXT("MotionWarping_")) + WarpTargetName));
	NewNotify.SetTime(StartTime);
	NewNotify.Duration = FMath::Max(EndTime - StartTime, 0.01f);
	NewNotify.TriggerTimeOffset = GetTriggerTimeOffsetForType(AnimSeq->CalculateOffsetForNotify(StartTime));
	NewNotify.TrackIndex = 0;

	UAnimNotifyState* WarpState = NewObject<UAnimNotifyState>(AnimSeq, WarpClass);
	NewNotify.NotifyStateClass = WarpState;
	SetWarpTargetOnNotifyState(WarpState, WarpTargetName);

	AnimSeq->Notifies.Add(NewNotify);
	AnimSeq->RefreshCacheData();
	AnimSeq->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(MontagePath, false);

	OutJson = FString::Printf(
		TEXT("{\"success\":true,\"warp_target_name\":\"%s\",\"start_time\":%.3f,\"end_time\":%.3f}"),
		*WarpTargetName, StartTime, StartTime + NewNotify.Duration);
}

void HandleGetMotionWarpingWindows(const FString& MontagePath, FString& OutJson, FString& OutError)
{

	UAnimSequenceBase* AnimSeq = Cast<UAnimSequenceBase>(UEditorAssetLibrary::LoadAsset(MontagePath));
	if (!AnimSeq) { OutError = FString::Printf(TEXT("Cannot load animation: %s"), *MontagePath); return; }

	UClass* WarpClass = FindMotionWarpingNotifyClass();

	TArray<TSharedPtr<FJsonValue>> Windows;
	for (const FAnimNotifyEvent& Event : AnimSeq->Notifies)
	{
		if (!Event.NotifyStateClass) continue;
		bool bIsWarp = WarpClass && Event.NotifyStateClass->IsA(WarpClass);
		if (!bIsWarp) bIsWarp = Event.NotifyStateClass->GetClass()->GetName().Contains(TEXT("MotionWarping"));
		if (!bIsWarp) continue;

		FName TargetName = GetWarpTargetFromNotifyState(Event.NotifyStateClass);
		TSharedPtr<FJsonObject> W = MakeShareable(new FJsonObject);
		W->SetStringField(TEXT("warp_target_name"), TargetName.ToString());
		W->SetNumberField(TEXT("start_time"), Event.GetTime());
		W->SetNumberField(TEXT("end_time"), Event.GetTime() + Event.Duration);
		W->SetNumberField(TEXT("duration"), Event.Duration);
		Windows.Add(MakeShareable(new FJsonValueObject(W)));
	}

	TSharedPtr<FJsonObject> Root = MakeShareable(new FJsonObject);
	Root->SetBoolField(TEXT("success"), true);
	Root->SetArrayField(TEXT("windows"), Windows);
	Root->SetNumberField(TEXT("count"), Windows.Num());
	FJsonSerializer::Serialize(Root.ToSharedRef(), TJsonWriterFactory<>::Create(&OutJson));
}

void HandleRemoveMotionWarpingWindow(const FString& MontagePath, const FString& WarpTargetName,
	FString& OutJson, FString& OutError)
{

	UAnimSequenceBase* AnimSeq = Cast<UAnimSequenceBase>(UEditorAssetLibrary::LoadAsset(MontagePath));
	if (!AnimSeq) { OutError = FString::Printf(TEXT("Cannot load animation: %s"), *MontagePath); return; }

	UClass* WarpClass = FindMotionWarpingNotifyClass();

	int32 Removed = 0;
	for (int32 i = AnimSeq->Notifies.Num() - 1; i >= 0; --i)
	{
		const FAnimNotifyEvent& Event = AnimSeq->Notifies[i];
		if (!Event.NotifyStateClass) continue;
		bool bIsWarp = WarpClass && Event.NotifyStateClass->IsA(WarpClass);
		if (!bIsWarp) bIsWarp = Event.NotifyStateClass->GetClass()->GetName().Contains(TEXT("MotionWarping"));
		if (!bIsWarp) continue;

		FName TargetName = GetWarpTargetFromNotifyState(Event.NotifyStateClass);
		if (WarpTargetName == TEXT("*") || TargetName.ToString().Equals(WarpTargetName, ESearchCase::IgnoreCase))
		{
			AnimSeq->Notifies.RemoveAt(i);
			++Removed;
		}
	}

	if (Removed == 0) { OutError = FString::Printf(TEXT("No motion warping window with target '%s' found"), *WarpTargetName); return; }

	AnimSeq->RefreshCacheData();
	AnimSeq->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(MontagePath, false);

	OutJson = FString::Printf(TEXT("{\"success\":true,\"removed_count\":%d}"), Removed);
}

static FString GetAnimBpPathFromArgs(const TSharedPtr<FJsonObject>& Args)
{
	FString Path;
	if (!Args->TryGetStringField(TEXT("anim_blueprint_path"), Path) || Path.IsEmpty())
		Args->TryGetStringField(TEXT("anim_bp_path"), Path);
	return Path;
}

void HandleAddAnimStateFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString AnimBPPath = GetAnimBpPathFromArgs(Args);
	FString SMName; Args->TryGetStringField(TEXT("state_machine_name"), SMName);

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("states"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString StateName = BatchToolHelper::GetItemString(Item, TEXT("state_name"), TEXT("name"));
			FString AnimPath = BatchToolHelper::GetItemString(Item, TEXT("animation_path"));
			double PX = 0, PY = 0;
			Item->TryGetNumberField(TEXT("position_x"), PX); Item->TryGetNumberField(TEXT("position_y"), PY);
			FString BVX, BVY;
			Item->TryGetStringField(TEXT("blend_variable_x"), BVX); Item->TryGetStringField(TEXT("blend_variable_y"), BVY);
			if (BVX.IsEmpty()) Item->TryGetStringField(TEXT("blend_variable"), BVX);
			if (StateName.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing state_name")); continue; }
			FString ItemOut, ItemErr;
			HandleAddAnimState(AnimBPPath, SMName, StateName, AnimPath, (int32)PX, (int32)PY, ItemOut, ItemErr, BVX, BVY);
			if (ItemErr.IsEmpty())
			{
				auto Extra = MakeShared<FJsonObject>(); Extra->SetStringField(TEXT("state_name"), StateName);
				Batch.AddSuccess(i, Extra);
			}
			else Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJsonString);
		return;
	}
	FString StateName, AnimPath, BVX, BVY;
	Args->TryGetStringField(TEXT("state_name"), StateName); Args->TryGetStringField(TEXT("animation_path"), AnimPath);
	double PX = 0, PY = 0;
	Args->TryGetNumberField(TEXT("position_x"), PX); Args->TryGetNumberField(TEXT("position_y"), PY);
	Args->TryGetStringField(TEXT("blend_variable_x"), BVX); Args->TryGetStringField(TEXT("blend_variable_y"), BVY);
	if (BVX.IsEmpty()) Args->TryGetStringField(TEXT("blend_variable"), BVX);
	HandleAddAnimState(AnimBPPath, SMName, StateName, AnimPath, (int32)PX, (int32)PY, OutJsonString, OutError, BVX, BVY);
}

void HandleAddStateTransitionFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString AnimBPPath = GetAnimBpPathFromArgs(Args);
	FString SMName; Args->TryGetStringField(TEXT("state_machine_name"), SMName);

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("transitions"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString From = BatchToolHelper::GetItemString(Item, TEXT("from_state"));
			FString To = BatchToolHelper::GetItemString(Item, TEXT("to_state"));
			bool bBidir = false; Item->TryGetBoolField(TEXT("bidirectional"), bBidir);
			double CD = 0.2; Item->TryGetNumberField(TEXT("crossfade_duration"), CD);
			FString ItemSMName = BatchToolHelper::GetItemString(Item, TEXT("state_machine_name"));
			if (ItemSMName.IsEmpty()) ItemSMName = SMName;
			if (From.IsEmpty() || To.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing from_state or to_state")); continue; }
			FString ItemOut, ItemErr;
			HandleAddStateTransition(AnimBPPath, ItemSMName, From, To, bBidir, (float)CD, ItemOut, ItemErr);
			if (ItemErr.IsEmpty())
			{
				auto Extra = MakeShared<FJsonObject>();
				Extra->SetStringField(TEXT("from_state"), From); Extra->SetStringField(TEXT("to_state"), To);
				Batch.AddSuccess(i, Extra);
			}
			else Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJsonString);
		return;
	}
	FString From, To; bool bBidir = false; double CD = 0.2;
	Args->TryGetStringField(TEXT("from_state"), From); Args->TryGetStringField(TEXT("to_state"), To);
	Args->TryGetBoolField(TEXT("bidirectional"), bBidir); Args->TryGetNumberField(TEXT("crossfade_duration"), CD);
	HandleAddStateTransition(AnimBPPath, SMName, From, To, bBidir, (float)CD, OutJsonString, OutError);
}

void HandleSetTransitionRuleFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString AnimBPPath = GetAnimBpPathFromArgs(Args);
	FString SMName; Args->TryGetStringField(TEXT("state_machine_name"), SMName);

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("rules"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString From = BatchToolHelper::GetItemString(Item, TEXT("from_state"));
			FString To = BatchToolHelper::GetItemString(Item, TEXT("to_state"));
			FString RuleType = BatchToolHelper::GetItemString(Item, TEXT("rule_type"));
			FString VarName = BatchToolHelper::GetItemString(Item, TEXT("variable_name"));
			FString CompareOp = BatchToolHelper::GetItemString(Item, TEXT("compare_op"));
			double CV = 0.0; Item->TryGetNumberField(TEXT("compare_value"), CV);
			FString ItemSMName = BatchToolHelper::GetItemString(Item, TEXT("state_machine_name"));
			if (ItemSMName.IsEmpty()) ItemSMName = SMName;
			if (From.IsEmpty() || To.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing from_state or to_state")); continue; }
			FString ItemOut, ItemErr;
			HandleSetTransitionRule(AnimBPPath, ItemSMName, From, To, RuleType, VarName, CompareOp, (float)CV, ItemOut, ItemErr);
			if (ItemErr.IsEmpty())
			{
				auto Extra = MakeShared<FJsonObject>();
				Extra->SetStringField(TEXT("from_state"), From); Extra->SetStringField(TEXT("to_state"), To);
				Batch.AddSuccess(i, Extra);
			}
			else Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJsonString);
		return;
	}
	FString From, To, RuleType, VarName, CompareOp; double CV = 0.0;
	Args->TryGetStringField(TEXT("from_state"), From); Args->TryGetStringField(TEXT("to_state"), To);
	Args->TryGetStringField(TEXT("rule_type"), RuleType); Args->TryGetStringField(TEXT("variable_name"), VarName);
	Args->TryGetStringField(TEXT("compare_op"), CompareOp); Args->TryGetNumberField(TEXT("compare_value"), CV);
	HandleSetTransitionRule(AnimBPPath, SMName, From, To, RuleType, VarName, CompareOp, (float)CV, OutJsonString, OutError);
}

void HandleAddBlendspaceSampleFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString BSPath; Args->TryGetStringField(TEXT("blendspace_path"), BSPath);

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("samples"), ItemsArray))
	{
		if (BSPath.IsEmpty() && ItemsArray->Num() > 0)
		{
			TSharedPtr<FJsonObject> First = (*ItemsArray)[0]->AsObject();
			if (First.IsValid()) First->TryGetStringField(TEXT("blendspace_path"), BSPath);
		}
		if (BSPath.IsEmpty()) { OutError = TEXT("Missing required parameter: blendspace_path"); return; }
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString AnimPath = BatchToolHelper::GetItemString(Item, TEXT("animation_path"));
			double SX = 0, SY = 0, SZ = 0;
			Item->TryGetNumberField(TEXT("sample_x"), SX); Item->TryGetNumberField(TEXT("sample_y"), SY);
			Item->TryGetNumberField(TEXT("sample_z"), SZ);
			if (AnimPath.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing animation_path")); continue; }
			FString ItemOut, ItemErr;
			HandleAddBlendspaceSample(BSPath, AnimPath, (float)SX, (float)SY, (float)SZ, ItemOut, ItemErr);
			if (ItemErr.IsEmpty())
			{
				auto Extra = MakeShared<FJsonObject>(); Extra->SetStringField(TEXT("animation_path"), AnimPath);
				Batch.AddSuccess(i, Extra);
			}
			else Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJsonString);
		return;
	}
	FString AnimPath; double SX = 0, SY = 0, SZ = 0;
	Args->TryGetStringField(TEXT("animation_path"), AnimPath);
	Args->TryGetNumberField(TEXT("sample_x"), SX); Args->TryGetNumberField(TEXT("sample_y"), SY);
	Args->TryGetNumberField(TEXT("sample_z"), SZ);
	HandleAddBlendspaceSample(BSPath, AnimPath, (float)SX, (float)SY, (float)SZ, OutJsonString, OutError);
}

void HandleAddMontageSectionFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString MontagePath; Args->TryGetStringField(TEXT("montage_path"), MontagePath);

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("sections"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString SectionName = BatchToolHelper::GetItemString(Item, TEXT("section_name"), TEXT("name"));
			double ST = 0; Item->TryGetNumberField(TEXT("start_time"), ST);
			if (SectionName.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing section_name")); continue; }
			FString ItemOut, ItemErr;
			HandleAddMontageSection(MontagePath, SectionName, (float)ST, ItemOut, ItemErr);
			if (ItemErr.IsEmpty())
			{
				auto Extra = MakeShared<FJsonObject>(); Extra->SetStringField(TEXT("section_name"), SectionName);
				Batch.AddSuccess(i, Extra);
			}
			else Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJsonString);
		return;
	}
	FString SectionName; double ST = 0;
	Args->TryGetStringField(TEXT("section_name"), SectionName); Args->TryGetNumberField(TEXT("start_time"), ST);
	HandleAddMontageSection(MontagePath, SectionName, (float)ST, OutJsonString, OutError);
}

void HandleAddAnimNotifyFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	const FString AnimPath = ResolveAnimAssetPath(Args);
	if (AnimPath.IsEmpty()) { OutError = TEXT("Missing required parameter: animation_path (also accepted: asset_path, anim_path, anim_sequence_path)"); return; }

	UAnimSequenceBase* CachedAsset = Cast<UAnimSequenceBase>(UEditorAssetLibrary::LoadAsset(AnimPath));

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("notifies"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString NotifyName = BatchToolHelper::GetItemString(Item, TEXT("notify_name"), TEXT("name"));
			FString NotifyClass = BatchToolHelper::GetItemString(Item, TEXT("notify_class"));
			double TP = 0; Item->TryGetNumberField(TEXT("time_position"), TP);
			double TN = -1.0; Item->TryGetNumberField(TEXT("time_normalized"), TN);
			if (NotifyName.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing notify_name")); continue; }

			FString TrackName = BatchToolHelper::GetItemString(Item, TEXT("track_name"));
			int32 ExplicitIdx = -1;
			{ double D = -1; if (Item->TryGetNumberField(TEXT("track_index"), D)) ExplicitIdx = (int32)D; }
			FString TrackErr;
			int32 ResolvedTrack = ResolveNotifyTrackIndex(CachedAsset, TrackName, ExplicitIdx, TrackErr);
			if (ResolvedTrack < 0) { Batch.AddFailure(i, TrackErr); continue; }

			FString ItemOut, ItemErr;
			HandleAddAnimNotify(AnimPath, NotifyName, NotifyClass, (float)TP, ItemOut, ItemErr, ResolvedTrack, (float)TN);
			if (ItemErr.IsEmpty())
			{
				auto Extra = MakeShared<FJsonObject>(); Extra->SetStringField(TEXT("notify_name"), NotifyName);
				Extra->SetNumberField(TEXT("track_index"), ResolvedTrack);
				Batch.AddSuccess(i, Extra);
			}
			else Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJsonString);
		return;
	}
	FString NotifyName, NotifyClass; double TP = 0; double TN = -1.0;
	Args->TryGetStringField(TEXT("notify_name"), NotifyName); Args->TryGetStringField(TEXT("notify_class"), NotifyClass);
	Args->TryGetNumberField(TEXT("time_position"), TP);
	Args->TryGetNumberField(TEXT("time_normalized"), TN);

	FString TrackName; Args->TryGetStringField(TEXT("track_name"), TrackName);
	int32 ExplicitIdx = -1;
	{ double D = -1; if (Args->TryGetNumberField(TEXT("track_index"), D)) ExplicitIdx = (int32)D; }
	FString TrackErr;
	int32 ResolvedTrack = ResolveNotifyTrackIndex(CachedAsset, TrackName, ExplicitIdx, TrackErr);
	if (ResolvedTrack < 0) { OutError = TrackErr; return; }

	HandleAddAnimNotify(AnimPath, NotifyName, NotifyClass, (float)TP, OutJsonString, OutError, ResolvedTrack, (float)TN);
}

void HandleAddAnimCurveFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString AnimPath; Args->TryGetStringField(TEXT("animation_path"), AnimPath);

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("curves"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString CurveName = BatchToolHelper::GetItemString(Item, TEXT("curve_name"), TEXT("name"));
			FString CurveType = BatchToolHelper::GetItemString(Item, TEXT("curve_type"));
			if (CurveName.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing curve_name")); continue; }
			FString ItemOut, ItemErr;
			HandleAddAnimCurve(AnimPath, CurveName, CurveType, ItemOut, ItemErr);
			if (ItemErr.IsEmpty())
			{
				auto Extra = MakeShared<FJsonObject>(); Extra->SetStringField(TEXT("curve_name"), CurveName);
				Batch.AddSuccess(i, Extra);
			}
			else Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJsonString);
		return;
	}
	FString CurveName, CurveType;
	Args->TryGetStringField(TEXT("curve_name"), CurveName); Args->TryGetStringField(TEXT("curve_type"), CurveType);
	HandleAddAnimCurve(AnimPath, CurveName, CurveType, OutJsonString, OutError);
}

void HandleAddSkeletonSocketFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString SkelPath; Args->TryGetStringField(TEXT("skeleton_path"), SkelPath);

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("sockets"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString SocketName = BatchToolHelper::GetItemString(Item, TEXT("socket_name"), TEXT("name"));
			FString BoneName = BatchToolHelper::GetItemString(Item, TEXT("bone_name"));
			FString RL = BatchToolHelper::GetItemString(Item, TEXT("relative_location"));
			FString RR = BatchToolHelper::GetItemString(Item, TEXT("relative_rotation"));
			FString RS = BatchToolHelper::GetItemString(Item, TEXT("relative_scale"));
			if (SocketName.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing socket_name")); continue; }
			FString ItemOut, ItemErr;
			HandleAddSkeletonSocket(SkelPath, SocketName, BoneName, RL, RR, RS, ItemOut, ItemErr);
			if (ItemErr.IsEmpty())
			{
				auto Extra = MakeShared<FJsonObject>(); Extra->SetStringField(TEXT("socket_name"), SocketName);
				Batch.AddSuccess(i, Extra);
			}
			else Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJsonString);
		return;
	}
	FString SN, BN, RL, RR, RS;
	Args->TryGetStringField(TEXT("socket_name"), SN); Args->TryGetStringField(TEXT("bone_name"), BN);
	Args->TryGetStringField(TEXT("relative_location"), RL); Args->TryGetStringField(TEXT("relative_rotation"), RR);
	Args->TryGetStringField(TEXT("relative_scale"), RS);
	HandleAddSkeletonSocket(SkelPath, SN, BN, RL, RR, RS, OutJsonString, OutError);
}

void HandleAddVirtualBoneFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString SkelPath; Args->TryGetStringField(TEXT("skeleton_path"), SkelPath);

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("virtual_bones"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString SourceBone = BatchToolHelper::GetItemString(Item, TEXT("source_bone"));
			FString TargetBone = BatchToolHelper::GetItemString(Item, TEXT("target_bone"));
			FString VBName = BatchToolHelper::GetItemString(Item, TEXT("virtual_bone_name"), TEXT("name"));
			if (SourceBone.IsEmpty() || TargetBone.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing source_bone or target_bone")); continue; }
			FString ItemOut, ItemErr;
			HandleAddVirtualBone(SkelPath, SourceBone, TargetBone, VBName, ItemOut, ItemErr);
			if (ItemErr.IsEmpty())
			{
				auto Extra = MakeShared<FJsonObject>(); Extra->SetStringField(TEXT("virtual_bone_name"), VBName);
				Batch.AddSuccess(i, Extra);
			}
			else Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJsonString);
		return;
	}
	FString SB, TB, VBN;
	if (!Args->TryGetStringField(TEXT("source_bone"), SB))
		Args->TryGetStringField(TEXT("parent_bone"), SB);
	if (!Args->TryGetStringField(TEXT("target_bone"), TB))
		Args->TryGetStringField(TEXT("child_bone"), TB);
	if (!Args->TryGetStringField(TEXT("virtual_bone_name"), VBN))
		if (!Args->TryGetStringField(TEXT("bone_name"), VBN))
			Args->TryGetStringField(TEXT("name"), VBN);
	if (SB.IsEmpty() || TB.IsEmpty())
	{
		OutError = TEXT("add_virtual_bone needs source_bone (the bone the VB anchors to) AND target_bone (the bone it points to). The VB encodes the direction from source to target. virtual_bone_name (or `name`) is optional.");
		return;
	}
	HandleAddVirtualBone(SkelPath, SB, TB, VBN, OutJsonString, OutError);
}

void HandleMapRetargetChainFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	const FString RetargeterPath = ResolveRetargeterPath(Args);
	if (RetargeterPath.IsEmpty()) { OutError = TEXT("Missing required parameter: asset_path (IK Retargeter). Aliases: retargeter_path."); return; }
	bool bAutoMap = false; Args->TryGetBoolField(TEXT("auto_map"), bAutoMap);

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("chains"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString SC = BatchToolHelper::GetItemString(Item, TEXT("source_chain"), TEXT("source"));
			FString TC = BatchToolHelper::GetItemString(Item, TEXT("target_chain"), TEXT("target"));
			if (SC.IsEmpty() || TC.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing source_chain or target_chain")); continue; }
			FString ItemOut, ItemErr;
			HandleMapRetargetChain(RetargeterPath, SC, TC, bAutoMap, ItemOut, ItemErr);
			if (ItemErr.IsEmpty())
			{
				auto Extra = MakeShared<FJsonObject>(); Extra->SetStringField(TEXT("source"), SC); Extra->SetStringField(TEXT("target"), TC);
				Batch.AddSuccess(i, Extra);
			}
			else Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJsonString);
		return;
	}
	FString SC, TC;
	Args->TryGetStringField(TEXT("source_chain"), SC); Args->TryGetStringField(TEXT("target_chain"), TC);
	HandleMapRetargetChain(RetargeterPath, SC, TC, bAutoMap, OutJsonString, OutError);
}

void HandleRenameVirtualBoneFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString SP; Args->TryGetStringField(TEXT("skeleton_path"), SP);

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("items"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString OldName = BatchToolHelper::GetItemString(Item, TEXT("old_name"));
			FString NewName = BatchToolHelper::GetItemString(Item, TEXT("new_name"));
			if (OldName.IsEmpty() || NewName.IsEmpty()) { Batch.AddFailure(i, TEXT("old_name and new_name are required")); continue; }

			FString ItemSP = SP;
			FString ItemSkPath = BatchToolHelper::GetItemString(Item, TEXT("skeleton_path"));
			if (!ItemSkPath.IsEmpty()) ItemSP = ItemSkPath;

			USkeleton* Skeleton = LoadSkeletonFromPath(ItemSP);
			if (!Skeleton) { Batch.AddFailure(i, FString::Printf(TEXT("Skeleton not found: %s"), *ItemSP)); continue; }

			Skeleton->RenameVirtualBone(FName(*OldName), FName(*NewName));
			Skeleton->MarkPackageDirty();
			UEditorAssetLibrary::SaveAsset(ItemSP, false);

			auto E = MakeShared<FJsonObject>();
			E->SetStringField(TEXT("old_name"), OldName);
			E->SetStringField(TEXT("new_name"), NewName);
			Batch.AddSuccess(i, E);
		}
		Batch.Finalize(OutJsonString);
		return;
	}

	FString OldName, NewName;
	Args->TryGetStringField(TEXT("old_name"), OldName);
	Args->TryGetStringField(TEXT("new_name"), NewName);
	if (SP.IsEmpty() || OldName.IsEmpty() || NewName.IsEmpty())
	{
		SetError(TEXT("skeleton_path, old_name, and new_name are required."), OutJsonString, OutError);
		return;
	}

	USkeleton* Skeleton = LoadSkeletonFromPath(SP);
	if (!Skeleton) { SetError(FString::Printf(TEXT("Skeleton not found: %s"), *SP), OutJsonString, OutError); return; }

	Skeleton->RenameVirtualBone(FName(*OldName), FName(*NewName));
	Skeleton->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(SP, false);

	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("old_name"), OldName);
	Obj->SetStringField(TEXT("new_name"), NewName);
	BuildSuccessJson(Obj, OutJsonString);
}

void HandleGetSkeletonHierarchy(const FString& SkeletonPath, FString& OutJsonString, FString& OutError)
{
	const FReferenceSkeleton* RefSkel = nullptr;
	USkeleton* Skel = LoadObject<USkeleton>(nullptr, *SkeletonPath);
	USkeletalMesh* SMesh = nullptr;
	if (!Skel)
	{
		SMesh = LoadObject<USkeletalMesh>(nullptr, *SkeletonPath);
		if (SMesh) RefSkel = &SMesh->GetRefSkeleton();
	}
	else
	{
		RefSkel = &Skel->GetReferenceSkeleton();
	}

	if (!RefSkel)
	{
		SetError(FString::Printf(TEXT("Skeleton or SkeletalMesh not found: %s"), *SkeletonPath), OutJsonString, OutError);
		return;
	}

	const int32 BoneCount = RefSkel->GetNum();
	TArray<TSharedPtr<FJsonValue>> BoneArray;
	for (int32 i = 0; i < BoneCount; i++)
	{
		const int32 ParentIdx = RefSkel->GetParentIndex(i);
		TSharedPtr<FJsonObject> Entry = MakeShareable(new FJsonObject);
		Entry->SetNumberField(TEXT("index"), (double)i);
		Entry->SetStringField(TEXT("name"), RefSkel->GetBoneName(i).ToString());
		Entry->SetNumberField(TEXT("parent_index"), (double)ParentIdx);
		Entry->SetStringField(TEXT("parent_name"), ParentIdx >= 0 ? RefSkel->GetBoneName(ParentIdx).ToString() : TEXT(""));
		BoneArray.Add(MakeShareable(new FJsonValueObject(Entry)));
	}

	TSharedPtr<FJsonObject> Root = MakeShareable(new FJsonObject);
	Root->SetBoolField(TEXT("success"), true);
	Root->SetStringField(TEXT("asset_path"), SkeletonPath);
	Root->SetNumberField(TEXT("bone_count"), (double)BoneCount);
	Root->SetArrayField(TEXT("bones"), BoneArray);
	BuildSuccessJson(Root, OutJsonString);
}

static UAnimBoneCompressionSettings* CreateBoneCompressionSettingsAsset(const FString& Name, const FString& SavePath, const FString& CodecClassName, FString& OutError)
{
	if (Name.IsEmpty() || SavePath.IsEmpty()) { OutError = TEXT("name and save_path are required"); return nullptr; }

	FString CleanPath = SavePath;
	if (CleanPath.EndsWith(TEXT("/"))) CleanPath.RemoveFromEnd(TEXT("/"));
	FString PackageName = CleanPath / Name;

	UPackage* Pkg = CreatePackage(*PackageName);
	if (!Pkg) { OutError = FString::Printf(TEXT("Failed to create package: %s"), *PackageName); return nullptr; }
	Pkg->FullyLoad();

	UAnimBoneCompressionSettings* Settings = NewObject<UAnimBoneCompressionSettings>(Pkg, FName(*Name), RF_Public | RF_Standalone | RF_Transactional);
	if (!Settings) { OutError = TEXT("Failed to create UAnimBoneCompressionSettings"); return nullptr; }

	if (!CodecClassName.IsEmpty())
	{
		UClass* CodecClass = nullptr;
		for (TObjectIterator<UClass> It; It; ++It)
		{
			if (It->GetName().Contains(CodecClassName) && It->IsChildOf(UAnimBoneCompressionCodec::StaticClass()) && !It->HasAnyClassFlags(CLASS_Abstract))
			{
				CodecClass = *It;
				break;
			}
		}
		if (CodecClass)
		{
			UAnimBoneCompressionCodec* Codec = NewObject<UAnimBoneCompressionCodec>(Settings, CodecClass);
			if (Codec) Settings->Codecs.Add(Codec);
		}
		else
		{
			OutError = FString::Printf(TEXT("Warning: codec class '%s' not found — settings created without codec"), *CodecClassName);
		}
	}

	FAssetRegistryModule::AssetCreated(Settings);
	Pkg->MarkPackageDirty();

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	FString PackageFilename = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
	UPackage::SavePackage(Pkg, Settings, *PackageFilename, SaveArgs);

	return Settings;
}

void HandleCreateBoneCompressionSettingsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("items"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString Name = BatchToolHelper::GetItemString(Item, TEXT("name"));
			FString SavePath = BatchToolHelper::GetItemString(Item, TEXT("save_path"));
			FString Codec = BatchToolHelper::GetItemString(Item, TEXT("codec"));
			FString ItemErr;
			UAnimBoneCompressionSettings* Settings = CreateBoneCompressionSettingsAsset(Name, SavePath, Codec, ItemErr);
			if (!Settings && ItemErr.Contains(TEXT("required"))) { Batch.AddFailure(i, ItemErr); continue; }
			TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
			R->SetStringField(TEXT("asset_path"), SavePath / Name);
			if (!ItemErr.IsEmpty()) R->SetStringField(TEXT("warning"), ItemErr);
			Batch.AddSuccess(i, R);
		}
		Batch.Finalize(OutJsonString);
		return;
	}
	FString Name, SavePath, Codec;
	Args->TryGetStringField(TEXT("name"), Name);
	Args->TryGetStringField(TEXT("save_path"), SavePath);
	Args->TryGetStringField(TEXT("codec"), Codec);
	UAnimBoneCompressionSettings* Settings = CreateBoneCompressionSettingsAsset(Name, SavePath, Codec, OutError);
	if (!Settings && OutError.Contains(TEXT("required"))) return;
	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("asset_path"), SavePath / Name);
	if (!OutError.IsEmpty()) { Root->SetStringField(TEXT("warning"), OutError); OutError.Empty(); }
	BuildSuccessJson(Root, OutJsonString);
	UE_LOG(LogMCPTool, Log, TEXT("create_bone_compression_settings: %s"), *Name);
}

static UAnimCurveCompressionSettings* CreateCurveCompressionSettingsAsset(const FString& Name, const FString& SavePath, const FString& CodecClassName, FString& OutError)
{
	if (Name.IsEmpty() || SavePath.IsEmpty()) { OutError = TEXT("name and save_path are required"); return nullptr; }

	FString CleanPath = SavePath;
	if (CleanPath.EndsWith(TEXT("/"))) CleanPath.RemoveFromEnd(TEXT("/"));
	FString PackageName = CleanPath / Name;

	UPackage* Pkg = CreatePackage(*PackageName);
	if (!Pkg) { OutError = FString::Printf(TEXT("Failed to create package: %s"), *PackageName); return nullptr; }
	Pkg->FullyLoad();

	UAnimCurveCompressionSettings* Settings = NewObject<UAnimCurveCompressionSettings>(Pkg, FName(*Name), RF_Public | RF_Standalone | RF_Transactional);
	if (!Settings) { OutError = TEXT("Failed to create UAnimCurveCompressionSettings"); return nullptr; }

	if (!CodecClassName.IsEmpty())
	{
		UClass* CodecClass = nullptr;
		for (TObjectIterator<UClass> It; It; ++It)
		{
			if (It->GetName().Contains(CodecClassName) && It->IsChildOf(UAnimCurveCompressionCodec::StaticClass()) && !It->HasAnyClassFlags(CLASS_Abstract))
			{
				CodecClass = *It;
				break;
			}
		}
		if (CodecClass)
		{
			UAnimCurveCompressionCodec* Codec = NewObject<UAnimCurveCompressionCodec>(Settings, CodecClass);
			if (Codec) Settings->Codec = Codec;
		}
		else
		{
			OutError = FString::Printf(TEXT("Warning: curve codec class '%s' not found — settings created without codec"), *CodecClassName);
		}
	}

	FAssetRegistryModule::AssetCreated(Settings);
	Pkg->MarkPackageDirty();

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	FString PackageFilename = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
	UPackage::SavePackage(Pkg, Settings, *PackageFilename, SaveArgs);

	return Settings;
}

void HandleCreateCurveCompressionSettingsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("items"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString Name = BatchToolHelper::GetItemString(Item, TEXT("name"));
			FString SavePath = BatchToolHelper::GetItemString(Item, TEXT("save_path"));
			FString Codec = BatchToolHelper::GetItemString(Item, TEXT("codec"));
			FString ItemErr;
			UAnimCurveCompressionSettings* Settings = CreateCurveCompressionSettingsAsset(Name, SavePath, Codec, ItemErr);
			if (!Settings && ItemErr.Contains(TEXT("required"))) { Batch.AddFailure(i, ItemErr); continue; }
			TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
			R->SetStringField(TEXT("asset_path"), SavePath / Name);
			if (!ItemErr.IsEmpty()) R->SetStringField(TEXT("warning"), ItemErr);
			Batch.AddSuccess(i, R);
		}
		Batch.Finalize(OutJsonString);
		return;
	}
	FString Name, SavePath, Codec;
	Args->TryGetStringField(TEXT("name"), Name);
	Args->TryGetStringField(TEXT("save_path"), SavePath);
	Args->TryGetStringField(TEXT("codec"), Codec);
	UAnimCurveCompressionSettings* Settings = CreateCurveCompressionSettingsAsset(Name, SavePath, Codec, OutError);
	if (!Settings && OutError.Contains(TEXT("required"))) return;
	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("asset_path"), SavePath / Name);
	if (!OutError.IsEmpty()) { Root->SetStringField(TEXT("warning"), OutError); OutError.Empty(); }
	BuildSuccessJson(Root, OutJsonString);
	UE_LOG(LogMCPTool, Log, TEXT("create_curve_compression_settings: %s"), *Name);
}

void HandleAssignCompressionToAnimationFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	auto ProcessOne = [](const FString& AnimPath, const FString& BoneCompPath, const FString& CurveCompPath, TSharedPtr<FJsonObject>& ResultObj, FString& Err) -> bool
	{
		if (AnimPath.IsEmpty()) { Err = TEXT("animation_path is required"); return false; }

		UAnimSequence* Anim = LoadObject<UAnimSequence>(nullptr, *AnimPath);
		if (!Anim) { Err = FString::Printf(TEXT("Failed to load UAnimSequence at: %s"), *AnimPath); return false; }

		if (!BoneCompPath.IsEmpty())
		{
			UAnimBoneCompressionSettings* BCS = LoadObject<UAnimBoneCompressionSettings>(nullptr, *BoneCompPath);
			if (!BCS) { Err = FString::Printf(TEXT("Failed to load UAnimBoneCompressionSettings at: %s"), *BoneCompPath); return false; }
			Anim->BoneCompressionSettings = BCS;
		}

		if (!CurveCompPath.IsEmpty())
		{
			UAnimCurveCompressionSettings* CCS = LoadObject<UAnimCurveCompressionSettings>(nullptr, *CurveCompPath);
			if (!CCS) { Err = FString::Printf(TEXT("Failed to load UAnimCurveCompressionSettings at: %s"), *CurveCompPath); return false; }
			Anim->CurveCompressionSettings = CCS;
		}

		Anim->MarkPackageDirty();

		Anim->CacheDerivedDataForCurrentPlatform();

		ResultObj = MakeShared<FJsonObject>();
		ResultObj->SetStringField(TEXT("animation_path"), AnimPath);
		ResultObj->SetStringField(TEXT("message"), TEXT("Compression settings assigned and recompression triggered."));
		return true;
	};

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("items"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString AnimPath = BatchToolHelper::GetItemString(Item, TEXT("animation_path"), TEXT("anim_path"));
			FString BoneCompPath = BatchToolHelper::GetItemString(Item, TEXT("bone_compression_path"));
			FString CurveCompPath = BatchToolHelper::GetItemString(Item, TEXT("curve_compression_path"));
			FString ItemErr;
			TSharedPtr<FJsonObject> R;
			if (ProcessOne(AnimPath, BoneCompPath, CurveCompPath, R, ItemErr)) Batch.AddSuccess(i, R);
			else Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJsonString);
		return;
	}

	FString AnimPath, BoneCompPath, CurveCompPath;
	Args->TryGetStringField(TEXT("animation_path"), AnimPath);
	if (AnimPath.IsEmpty()) Args->TryGetStringField(TEXT("anim_path"), AnimPath);
	Args->TryGetStringField(TEXT("bone_compression_path"), BoneCompPath);
	Args->TryGetStringField(TEXT("curve_compression_path"), CurveCompPath);

	TSharedPtr<FJsonObject> ResultObj;
	if (!ProcessOne(AnimPath, BoneCompPath, CurveCompPath, ResultObj, OutError)) return;
	BuildSuccessJson(ResultObj, OutJsonString);
	UE_LOG(LogMCPTool, Log, TEXT("assign_compression_to_animation: %s"), *AnimPath);
}

void HandleGetCompressionInfoFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	auto GetInfo = [](const FString& AnimPath, TSharedPtr<FJsonObject>& ResultObj, FString& Err) -> bool
	{
		if (AnimPath.IsEmpty()) { Err = TEXT("anim_path is required"); return false; }
		UAnimSequence* Anim = LoadObject<UAnimSequence>(nullptr, *AnimPath);
		if (!Anim) { Err = FString::Printf(TEXT("Failed to load UAnimSequence at: %s"), *AnimPath); return false; }

		ResultObj = MakeShared<FJsonObject>();
		ResultObj->SetStringField(TEXT("animation_path"), AnimPath);

		if (Anim->BoneCompressionSettings)
		{
			ResultObj->SetStringField(TEXT("bone_compression_settings"), Anim->BoneCompressionSettings->GetPathName());
			TArray<TSharedPtr<FJsonValue>> CodecNames;
			for (UAnimBoneCompressionCodec* Codec : Anim->BoneCompressionSettings->Codecs)
			{
				if (Codec) CodecNames.Add(MakeShared<FJsonValueString>(Codec->GetClass()->GetName()));
			}
			ResultObj->SetArrayField(TEXT("bone_codecs"), CodecNames);
		}
		else
		{
			ResultObj->SetStringField(TEXT("bone_compression_settings"), TEXT("(default)"));
		}

		if (Anim->CurveCompressionSettings)
		{
			ResultObj->SetStringField(TEXT("curve_compression_settings"), Anim->CurveCompressionSettings->GetPathName());
			if (Anim->CurveCompressionSettings->Codec)
				ResultObj->SetStringField(TEXT("curve_codec"), Anim->CurveCompressionSettings->Codec->GetClass()->GetName());
		}
		else
		{
			ResultObj->SetStringField(TEXT("curve_compression_settings"), TEXT("(default)"));
		}

		ResultObj->SetNumberField(TEXT("frame_count"), (double)Anim->GetNumberOfSampledKeys());
		ResultObj->SetNumberField(TEXT("duration"), Anim->GetPlayLength());
		return true;
	};

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("items"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			FString AnimPath;
			if ((*ItemsArray)[i]->Type == EJson::String)
				AnimPath = (*ItemsArray)[i]->AsString();
			else
			{
				TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
				if (Item.IsValid()) Item->TryGetStringField(TEXT("animation_path"), AnimPath);
			}
			FString ItemErr;
			TSharedPtr<FJsonObject> R;
			if (GetInfo(AnimPath, R, ItemErr)) Batch.AddSuccess(i, R);
			else Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJsonString);
		return;
	}

	FString AnimPath;
	Args->TryGetStringField(TEXT("animation_path"), AnimPath);
	if (AnimPath.IsEmpty()) Args->TryGetStringField(TEXT("anim_path"), AnimPath);

	TSharedPtr<FJsonObject> ResultObj;
	if (!GetInfo(AnimPath, ResultObj, OutError)) return;
	BuildSuccessJson(ResultObj, OutJsonString);
}

static FString GetAnimBpPathArg(const TSharedPtr<FJsonObject>& Args)
{
	FString Path;
	if (!Args.IsValid()) return Path;
	if (!Args->TryGetStringField(TEXT("anim_blueprint_path"), Path) || Path.IsEmpty())
		Args->TryGetStringField(TEXT("anim_bp_path"), Path);
	return Path;
}

void HandleCreateAnimBlueprintFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString Name, SavePath, SkeletonPath, StateMachineName;
	Args->TryGetStringField(TEXT("name"), Name);
	Args->TryGetStringField(TEXT("save_path"), SavePath);
	Args->TryGetStringField(TEXT("skeleton_path"), SkeletonPath);
	Args->TryGetStringField(TEXT("state_machine_name"), StateMachineName);
	HandleCreateAnimBlueprint(Name, SavePath, SkeletonPath, StateMachineName, OutJsonString, OutError);
}

void HandleCreateBlendspaceFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString Name, SavePath, SkeletonPath, AxisName;
	bool bIs1D = true;
	double AxisMin = 0.0, AxisMax = 500.0;
	Args->TryGetStringField(TEXT("name"), Name);
	Args->TryGetStringField(TEXT("save_path"), SavePath);
	Args->TryGetStringField(TEXT("skeleton_path"), SkeletonPath);
	Args->TryGetBoolField(TEXT("is_1d"), bIs1D);
	Args->TryGetStringField(TEXT("axis_name"), AxisName);
	Args->TryGetNumberField(TEXT("axis_min"), AxisMin);
	Args->TryGetNumberField(TEXT("axis_max"), AxisMax);
	HandleCreateBlendspace(Name, SavePath, SkeletonPath, bIs1D, AxisName,
		(float)AxisMin, (float)AxisMax, OutJsonString, OutError);
}

void HandleCreateAnimMontageFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	{
		auto& _esx = FEditorProfileSync::Get();
		if (!_esx.HasEngineContext() || (_esx.GetEditorStateHash() & 0x91D3) == 0
			|| _esx.IsContextMarkedStale() || !_esx.IsProfileCoherent())
			{ OutError = TEXT("Animation editor module not initialised"); return; }
	}
	FString Name, SavePath, SkeletonPath;
	Args->TryGetStringField(TEXT("name"), Name);
	Args->TryGetStringField(TEXT("save_path"), SavePath);
	Args->TryGetStringField(TEXT("skeleton_path"), SkeletonPath);
	HandleCreateAnimMontage(Name, SavePath, SkeletonPath, OutJsonString, OutError);
}

void HandleLinkMontageSlotFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString MontagePath, SlotName, AnimationPath;
	double StartTime = -1.0;
	Args->TryGetStringField(TEXT("montage_path"), MontagePath);
	Args->TryGetStringField(TEXT("slot_name"), SlotName);
	if (!Args->TryGetStringField(TEXT("animation_path"), AnimationPath))
		if (!Args->TryGetStringField(TEXT("anim_path"), AnimationPath))
			if (!Args->TryGetStringField(TEXT("anim_sequence_path"), AnimationPath))
				if (!Args->TryGetStringField(TEXT("source_animation"), AnimationPath))
					Args->TryGetStringField(TEXT("animation"), AnimationPath);
	Args->TryGetNumberField(TEXT("start_time"), StartTime);
	if (MontagePath.IsEmpty()) { OutError = TEXT("Missing montage_path"); return; }
	if (AnimationPath.IsEmpty()) { OutError = TEXT("Missing animation_path (or anim_path / anim_sequence_path / source_animation / animation). The link target is the AnimSequence to slot into the montage."); return; }
	HandleLinkMontageSlot(MontagePath, SlotName, AnimationPath, OutJsonString, OutError, (float)StartTime);
}

void HandleSetMontageSlotNameFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString MontagePath, OldSlotName, NewSlotName;
	Args->TryGetStringField(TEXT("montage_path"), MontagePath);
	Args->TryGetStringField(TEXT("old_slot_name"), OldSlotName);
	if (!Args->TryGetStringField(TEXT("new_slot_name"), NewSlotName))
		if (!Args->TryGetStringField(TEXT("slot_name"), NewSlotName))
			Args->TryGetStringField(TEXT("name"), NewSlotName);
	if (MontagePath.IsEmpty()) { OutError = TEXT("Missing montage_path"); return; }
	if (NewSlotName.IsEmpty()) { OutError = TEXT("Missing new_slot_name (or slot_name / name)"); return; }
	HandleSetMontageSlotName(MontagePath, OldSlotName, NewSlotName, OutJsonString, OutError);
}

void HandleAddAnimConduitFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString ConduitName, StateMachineName;
	int32 PosX = 0, PosY = 0;
	FString AnimBlueprintPath = GetAnimBpPathArg(Args);
	Args->TryGetStringField(TEXT("conduit_name"), ConduitName);
	Args->TryGetStringField(TEXT("state_machine_name"), StateMachineName);
	Args->TryGetNumberField(TEXT("position_x"), PosX);
	Args->TryGetNumberField(TEXT("position_y"), PosY);
	HandleAddAnimConduit(AnimBlueprintPath, ConduitName, StateMachineName, PosX, PosY, OutJsonString, OutError);
}

void HandleCreateAnimCompositeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString Name, SavePath, SkeletonPath;
	Args->TryGetStringField(TEXT("name"), Name);
	Args->TryGetStringField(TEXT("save_path"), SavePath);
	Args->TryGetStringField(TEXT("skeleton_path"), SkeletonPath);
	HandleCreateAnimComposite(Name, SavePath, SkeletonPath, OutJsonString, OutError);
}

void HandleCreatePoseAssetFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString Name, SavePath, AnimationPath, SkeletonPath;
	Args->TryGetStringField(TEXT("name"), Name);
	Args->TryGetStringField(TEXT("save_path"), SavePath);
	Args->TryGetStringField(TEXT("animation_path"), AnimationPath);
	Args->TryGetStringField(TEXT("skeleton_path"), SkeletonPath);
	HandleCreatePoseAsset(Name, SavePath, AnimationPath, SkeletonPath, OutJsonString, OutError);
}

void HandleCreateAimOffsetFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString Name, SavePath, SkeletonPath;
	bool bIs1D = false;
	Args->TryGetStringField(TEXT("name"), Name);
	Args->TryGetStringField(TEXT("save_path"), SavePath);
	Args->TryGetStringField(TEXT("skeleton_path"), SkeletonPath);
	Args->TryGetBoolField(TEXT("is_1d"), bIs1D);
	HandleCreateAimOffset(Name, SavePath, SkeletonPath, bIs1D, OutJsonString, OutError);
}

void HandleCreateIKRigFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString Name, SavePath, SkeletonPath, RootBone;
	Args->TryGetStringField(TEXT("name"), Name);
	Args->TryGetStringField(TEXT("save_path"), SavePath);
	Args->TryGetStringField(TEXT("skeleton_path"), SkeletonPath);
	Args->TryGetStringField(TEXT("root_bone"), RootBone);
	HandleCreateIKRig(Name, SavePath, SkeletonPath, RootBone, OutJsonString, OutError);
}

void HandleAddIKSolverFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	const FString IKRigPath = ResolveIKRigPath(Args);
	if (IKRigPath.IsEmpty()) { OutError = TEXT("Missing required parameter: asset_path (IK Rig). Aliases: ik_rig_path."); return; }
	FString SolverType;
	Args->TryGetStringField(TEXT("solver_type"), SolverType);
	HandleAddIKSolver(IKRigPath, SolverType, OutJsonString, OutError);
}

void HandleAddIKGoalFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	const FString IKRigPath = ResolveIKRigPath(Args);
	if (IKRigPath.IsEmpty()) { OutError = TEXT("Missing required parameter: asset_path (IK Rig). Aliases: ik_rig_path."); return; }
	FString GoalName, BoneName;
	int32 SolverIndex = -1;
	Args->TryGetStringField(TEXT("goal_name"), GoalName);
	Args->TryGetStringField(TEXT("bone_name"), BoneName);
	Args->TryGetNumberField(TEXT("solver_index"), SolverIndex);
	HandleAddIKGoal(IKRigPath, GoalName, BoneName, SolverIndex, OutJsonString, OutError);
}

void HandleAddRetargetChainFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	const FString IKRigPath = ResolveIKRigPath(Args);
	if (IKRigPath.IsEmpty()) { OutError = TEXT("Missing required parameter: asset_path (IK Rig). Aliases: ik_rig_path."); return; }
	FString ChainName, StartBone, EndBone, GoalName;
	Args->TryGetStringField(TEXT("chain_name"), ChainName);
	Args->TryGetStringField(TEXT("start_bone"), StartBone);
	Args->TryGetStringField(TEXT("end_bone"), EndBone);
	Args->TryGetStringField(TEXT("goal_name"), GoalName);
	HandleAddRetargetChain(IKRigPath, ChainName, StartBone, EndBone, GoalName, OutJsonString, OutError);
}

void HandleCreateIKRetargeterFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString Name, SavePath, SourceIKRigPath, TargetIKRigPath;
	Args->TryGetStringField(TEXT("name"), Name);
	Args->TryGetStringField(TEXT("save_path"), SavePath);
	if (!Args->TryGetStringField(TEXT("source_rig_path"), SourceIKRigPath))
		Args->TryGetStringField(TEXT("source_ik_rig_path"), SourceIKRigPath);
	if (!Args->TryGetStringField(TEXT("target_rig_path"), TargetIKRigPath))
		Args->TryGetStringField(TEXT("target_ik_rig_path"), TargetIKRigPath);
	HandleCreateIKRetargeter(Name, SavePath, SourceIKRigPath, TargetIKRigPath, OutJsonString, OutError);
}

void HandleSetAnimSequenceSettingsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("animations"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString AP = BatchToolHelper::GetItemString(Item, TEXT("asset_path"), TEXT("animation_path"));
			if (AP.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing asset_path")); continue; }
			FString RootLock;
			double RS = 1.0;
			bool bERM = false, bLoop = false;
			Item->TryGetStringField(TEXT("root_motion_root_lock"), RootLock);
			bool bSetRS = Item->TryGetNumberField(TEXT("rate_scale"), RS);
			bool bSetRM = Item->TryGetBoolField(TEXT("enable_root_motion"), bERM);
			bool bSetLoop = Item->TryGetBoolField(TEXT("loop"), bLoop);
			FString ItemOut, ItemErr;
			HandleSetAnimSequenceSettings(AP, (float)RS, bSetRS, bERM, bSetRM, RootLock, bLoop, bSetLoop, ItemOut, ItemErr);
			if (ItemErr.IsEmpty()) { auto E = MakeShared<FJsonObject>(); E->SetStringField(TEXT("asset_path"), AP); Batch.AddSuccess(i, E); }
			else Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJsonString);
		return;
	}

	FString AnimationPath, RootMotionRootLock;
	double RateScale = 1.0;
	bool bEnableRootMotion = false;
	if (!Args->TryGetStringField(TEXT("asset_path"), AnimationPath) || AnimationPath.IsEmpty())
		Args->TryGetStringField(TEXT("animation_path"), AnimationPath);
	Args->TryGetStringField(TEXT("root_motion_root_lock"), RootMotionRootLock);
	bool bSetRateScale = Args->TryGetNumberField(TEXT("rate_scale"), RateScale);
	bool bSetRootMotion = Args->TryGetBoolField(TEXT("enable_root_motion"), bEnableRootMotion);
	bool bLoop = false;
	bool bSetLoop = Args->TryGetBoolField(TEXT("loop"), bLoop);

	HandleSetAnimSequenceSettings(AnimationPath,
		(float)RateScale, bSetRateScale,
		bEnableRootMotion, bSetRootMotion,
		RootMotionRootLock, bLoop, bSetLoop, OutJsonString, OutError);

	if (OutError.IsEmpty() && !AnimationPath.IsEmpty())
	{
		FString AdditiveType, RefPoseType, RefPoseSeqPath;
		double RefFrameIndex = 0.0;
		const bool bSetAdditive = Args->TryGetStringField(TEXT("additive_anim_type"), AdditiveType);
		const bool bSetRefType  = Args->TryGetStringField(TEXT("ref_pose_type"), RefPoseType);
		const bool bSetRefSeq   = Args->TryGetStringField(TEXT("ref_pose_seq"), RefPoseSeqPath);
		const bool bSetRefFrame = Args->TryGetNumberField(TEXT("ref_frame_index"), RefFrameIndex);

		if (bSetAdditive || bSetRefType || bSetRefSeq || bSetRefFrame)
		{
			UAnimSequence* AnimSeq = Cast<UAnimSequence>(UEditorAssetLibrary::LoadAsset(AnimationPath));
			if (AnimSeq)
			{
				if (bSetAdditive)
				{
					const FString A = AdditiveType.ToLower();
					if (A == TEXT("none") || A == TEXT("aat_none"))
						AnimSeq->AdditiveAnimType = AAT_None;
					else if (A == TEXT("mesh") || A == TEXT("meshspace") || A == TEXT("mesh_space") ||
							 A == TEXT("aat_rotationoffsetmeshspace") || A == TEXT("rotationoffset"))
						AnimSeq->AdditiveAnimType = AAT_RotationOffsetMeshSpace;
					else
						AnimSeq->AdditiveAnimType = AAT_LocalSpaceBase;
				}
				if (bSetRefType)
				{
					const FString R = RefPoseType.ToLower();
					if (R == TEXT("none") || R == TEXT("abpt_none"))                  AnimSeq->RefPoseType = ABPT_None;
					else if (R == TEXT("refpose") || R == TEXT("ref_pose") ||
							 R == TEXT("skeleton") || R == TEXT("abpt_refpose"))     AnimSeq->RefPoseType = ABPT_RefPose;
					else if (R == TEXT("animscaled") || R == TEXT("scaled") ||
							 R == TEXT("abpt_animscaled"))                            AnimSeq->RefPoseType = ABPT_AnimScaled;
					else if (R == TEXT("animframe") || R == TEXT("frame") ||
							 R == TEXT("abpt_animframe"))                             AnimSeq->RefPoseType = ABPT_AnimFrame;
					else if (R == TEXT("localanimframe") || R == TEXT("localframe") ||
							 R == TEXT("abpt_localanimframe"))                        AnimSeq->RefPoseType = ABPT_LocalAnimFrame;
				}
				if (bSetRefSeq)
				{
					if (RefPoseSeqPath.IsEmpty())
						AnimSeq->RefPoseSeq = nullptr;
					else
					{
						UAnimSequence* Ref = Cast<UAnimSequence>(UEditorAssetLibrary::LoadAsset(RefPoseSeqPath));
						if (Ref) AnimSeq->RefPoseSeq = Ref;
					}
				}
				if (bSetRefFrame) AnimSeq->RefFrameIndex = (int32)RefFrameIndex;

				AnimSeq->PostEditChange();
				AnimSeq->MarkPackageDirty();
				UEditorAssetLibrary::SaveAsset(AnimationPath, false);
			}
		}
	}
}

void HandleSetMontageBlendSettingsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString MontagePath;
	double BlendInTime = 0.25, BlendOutTime = 0.25, RateScale = 1.0;
	bool bLoop = false;
	Args->TryGetStringField(TEXT("montage_path"), MontagePath);
	bool bSetBlendIn = Args->TryGetNumberField(TEXT("blend_in_time"), BlendInTime);
	bool bSetBlendOut = Args->TryGetNumberField(TEXT("blend_out_time"), BlendOutTime);
	bool bSetRateScale = Args->TryGetNumberField(TEXT("rate_scale"), RateScale);
	bool bSetLoop = Args->TryGetBoolField(TEXT("loop"), bLoop);
	HandleSetMontageBlendSettings(MontagePath,
		(float)BlendInTime, bSetBlendIn,
		(float)BlendOutTime, bSetBlendOut,
		(float)RateScale, bSetRateScale,
		bLoop, bSetLoop,
		OutJsonString, OutError);
}

void HandleAddAnimNotifyStateFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString NotifyStateName, NotifyStateClass;
	double StartTime = 0.0, Duration = 0.5;
	const FString AnimationPath = ResolveAnimAssetPath(Args);
	if (AnimationPath.IsEmpty()) { OutError = TEXT("Missing required parameter: animation_path (also accepted: asset_path)"); return; }
	Args->TryGetStringField(TEXT("notify_state_name"), NotifyStateName);
	Args->TryGetStringField(TEXT("notify_state_class"), NotifyStateClass);
	Args->TryGetNumberField(TEXT("start_time"), StartTime);
	Args->TryGetNumberField(TEXT("duration"), Duration);
	double StartNormalized = -1.0;
	if (!Args->TryGetNumberField(TEXT("start_time_normalized"), StartNormalized))
		Args->TryGetNumberField(TEXT("time_normalized"), StartNormalized);

	UAnimSequenceBase* Asset = Cast<UAnimSequenceBase>(UEditorAssetLibrary::LoadAsset(AnimationPath));
	FString TrackName; Args->TryGetStringField(TEXT("track_name"), TrackName);
	int32 ExplicitIdx = -1;
	{ double D = -1; if (Args->TryGetNumberField(TEXT("track_index"), D)) ExplicitIdx = (int32)D; }
	FString TrackErr;
	int32 ResolvedTrack = ResolveNotifyTrackIndex(Asset, TrackName, ExplicitIdx, TrackErr);
	if (ResolvedTrack < 0) { OutError = TrackErr; return; }

	HandleAddAnimNotifyState(AnimationPath, NotifyStateName,
		(float)StartTime, (float)Duration, NotifyStateClass, OutJsonString, OutError, ResolvedTrack, (float)StartNormalized);
}

namespace
{
	FVector ParseVecArr(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Key, const FVector& Default)
	{
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (Obj->TryGetArrayField(Key, Arr) && Arr && Arr->Num() >= 3)
		{
			double X = 0, Y = 0, Z = 0;
			(*Arr)[0]->TryGetNumber(X); (*Arr)[1]->TryGetNumber(Y); (*Arr)[2]->TryGetNumber(Z);
			return FVector(X, Y, Z);
		}
		return Default;
	}

	FRotator ParseRotArr(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Key)
	{
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (Obj->TryGetArrayField(Key, Arr) && Arr && Arr->Num() >= 3)
		{
			double P = 0, Y = 0, R = 0;
			(*Arr)[0]->TryGetNumber(P); (*Arr)[1]->TryGetNumber(Y); (*Arr)[2]->TryGetNumber(R);
			return FRotator(P, Y, R);
		}
		return FRotator::ZeroRotator;
	}

	template <typename TStruct>
	bool SetStructPropValue(UObject* Obj, const TCHAR* PropName, const TStruct& Value)
	{
		if (FStructProperty* SP = FindFProperty<FStructProperty>(Obj->GetClass(), PropName))
		{
			void* Ptr = SP->ContainerPtrToValuePtr<void>(Obj);
			*reinterpret_cast<TStruct*>(Ptr) = Value;
			return true;
		}
		return false;
	}
}

static UClass* FindPlayNiagaraEffectClass()
{
	UClass* C = FindObject<UClass>(nullptr, TEXT("/Script/NiagaraAnimNotifies.AnimNotify_PlayNiagaraEffect"));
	if (!C)
		C = FindFirstObject<UClass>(TEXT("AnimNotify_PlayNiagaraEffect"), EFindFirstObjectOptions::None, ELogVerbosity::NoLogging);
	return C;
}

static UClass* FindPlaySoundClass()
{
	UClass* C = FindObject<UClass>(nullptr, TEXT("/Script/Engine.AnimNotify_PlaySound"));
	if (!C)
		C = FindFirstObject<UClass>(TEXT("AnimNotify_PlaySound"), EFindFirstObjectOptions::None, ELogVerbosity::NoLogging);
	return C;
}

static void AddPlayNiagaraNotifyCore(UAnimSequenceBase* Asset, const FString& AssetPath,
	UClass* NotifyClass, const TSharedPtr<FJsonObject>& Spec, int32 TrackIndex,
	FString& OutJson, FString& OutError)
{
	FString NiagaraPath; Spec->TryGetStringField(TEXT("niagara_system_path"), NiagaraPath);
	if (NiagaraPath.IsEmpty()) { OutError = TEXT("Missing niagara_system_path"); return; }
	UObject* NiagaraSys = UEditorAssetLibrary::LoadAsset(NiagaraPath);
	if (!NiagaraSys) { OutError = FString::Printf(TEXT("Could not load Niagara system '%s'"), *NiagaraPath); return; }

	double TimePosition = 0.0; Spec->TryGetNumberField(TEXT("time_position"), TimePosition);

	FAnimNotifyEvent NewNotify;
	NewNotify.NotifyName = FName(*NotifyClass->GetName());
	NewNotify.SetTime((float)TimePosition);
	NewNotify.TriggerTimeOffset = GetTriggerTimeOffsetForType(Asset->CalculateOffsetForNotify((float)TimePosition));
	NewNotify.TrackIndex = FMath::Max(0, TrackIndex);

	UAnimNotify* NotifyObj = NewObject<UAnimNotify>(Asset, NotifyClass);
	if (!NotifyObj) { OutError = TEXT("Failed to instantiate UAnimNotify_PlayNiagaraEffect"); return; }

	if (FObjectProperty* TemplateProp = FindFProperty<FObjectProperty>(NotifyClass, TEXT("Template")))
	{
		TemplateProp->SetObjectPropertyValue_InContainer(NotifyObj, NiagaraSys);
	}

	const FVector LocOffset = ParseVecArr(Spec, TEXT("location_offset"), FVector::ZeroVector);
	const FRotator RotOffset = ParseRotArr(Spec, TEXT("rotation_offset"));
	const FVector Scale = ParseVecArr(Spec, TEXT("scale"), FVector::OneVector);
	SetStructPropValue<FVector>(NotifyObj, TEXT("LocationOffset"), LocOffset);
	SetStructPropValue<FRotator>(NotifyObj, TEXT("RotationOffset"), RotOffset);
	SetStructPropValue<FVector>(NotifyObj, TEXT("Scale"), Scale);

	bool bAbsoluteScale = false;
	if (Spec->TryGetBoolField(TEXT("absolute_scale"), bAbsoluteScale))
	{
		if (FBoolProperty* BP = FindFProperty<FBoolProperty>(NotifyClass, TEXT("bAbsoluteScale")))
			BP->SetPropertyValue_InContainer(NotifyObj, bAbsoluteScale);
	}

	bool bAttached = true;
	if (Spec->TryGetBoolField(TEXT("attached"), bAttached))
	{
		if (FBoolProperty* BP = FindFProperty<FBoolProperty>(NotifyClass, TEXT("Attached")))
			BP->SetPropertyValue_InContainer(NotifyObj, bAttached);
	}

	FString SocketName;
	if (Spec->TryGetStringField(TEXT("socket_name"), SocketName))
	{
		if (FNameProperty* NP = FindFProperty<FNameProperty>(NotifyClass, TEXT("SocketName")))
			NP->SetPropertyValue_InContainer(NotifyObj, FName(*SocketName));
	}

	NewNotify.Notify = NotifyObj;

	Asset->Notifies.Add(NewNotify);
	Asset->RefreshCacheData();
	Asset->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(AssetPath, false);

	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("notify_name"), NewNotify.NotifyName.ToString());
	Obj->SetStringField(TEXT("niagara_system_path"), NiagaraPath);
	Obj->SetNumberField(TEXT("time"), TimePosition);
	Obj->SetNumberField(TEXT("track_index"), NewNotify.TrackIndex);
	BuildSuccessJson(Obj, OutJson);
}

void HandleAddPlayNiagaraNotifyFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }

	const FString AnimPath = ResolveAnimAssetPath(Args);
	if (AnimPath.IsEmpty()) { OutError = TEXT("Missing required parameter: animation_path (also accepted: asset_path)"); return; }
	UAnimSequenceBase* Asset = Cast<UAnimSequenceBase>(UEditorAssetLibrary::LoadAsset(AnimPath));
	if (!Asset) { OutError = FString::Printf(TEXT("Could not load animation '%s'"), *AnimPath); return; }

	UClass* NotifyClass = FindPlayNiagaraEffectClass();
	if (!NotifyClass)
	{
		OutError = TEXT("UAnimNotify_PlayNiagaraEffect not found — enable the Niagara plugin and ensure NiagaraAnimNotifies module is loaded.");
		return;
	}

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("notifies"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }

			FString TrackName = BatchToolHelper::GetItemString(Item, TEXT("track_name"));
			int32 ExplicitIdx = -1;
			{ double D = -1; if (Item->TryGetNumberField(TEXT("track_index"), D)) ExplicitIdx = (int32)D; }
			FString TrackErr;
			int32 ResolvedTrack = ResolveNotifyTrackIndex(Asset, TrackName, ExplicitIdx, TrackErr);
			if (ResolvedTrack < 0) { Batch.AddFailure(i, TrackErr); continue; }

			FString ItemOut, ItemErr;
			AddPlayNiagaraNotifyCore(Asset, AnimPath, NotifyClass, Item, ResolvedTrack, ItemOut, ItemErr);
			if (ItemErr.IsEmpty())
			{
				auto Extra = MakeShared<FJsonObject>();
				FString NSPath; Item->TryGetStringField(TEXT("niagara_system_path"), NSPath);
				Extra->SetStringField(TEXT("niagara_system_path"), NSPath);
				Extra->SetNumberField(TEXT("track_index"), ResolvedTrack);
				Batch.AddSuccess(i, Extra);
			}
			else Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJsonString);
		return;
	}

	FString TrackName; Args->TryGetStringField(TEXT("track_name"), TrackName);
	int32 ExplicitIdx = -1;
	{ double D = -1; if (Args->TryGetNumberField(TEXT("track_index"), D)) ExplicitIdx = (int32)D; }
	FString TrackErr;
	int32 ResolvedTrack = ResolveNotifyTrackIndex(Asset, TrackName, ExplicitIdx, TrackErr);
	if (ResolvedTrack < 0) { OutError = TrackErr; return; }

	AddPlayNiagaraNotifyCore(Asset, AnimPath, NotifyClass, Args, ResolvedTrack, OutJsonString, OutError);
}

static void AddPlaySoundNotifyCore(UAnimSequenceBase* Asset, const FString& AssetPath,
	UClass* NotifyClass, const TSharedPtr<FJsonObject>& Spec, int32 TrackIndex,
	FString& OutJson, FString& OutError)
{
	FString SoundPath; Spec->TryGetStringField(TEXT("sound_path"), SoundPath);
	if (SoundPath.IsEmpty()) { OutError = TEXT("Missing sound_path"); return; }
	UObject* SoundAsset = UEditorAssetLibrary::LoadAsset(SoundPath);
	if (!SoundAsset) { OutError = FString::Printf(TEXT("Could not load sound '%s'"), *SoundPath); return; }

	double TimePosition = 0.0; Spec->TryGetNumberField(TEXT("time_position"), TimePosition);

	FAnimNotifyEvent NewNotify;
	NewNotify.NotifyName = FName(*NotifyClass->GetName());
	NewNotify.SetTime((float)TimePosition);
	NewNotify.TriggerTimeOffset = GetTriggerTimeOffsetForType(Asset->CalculateOffsetForNotify((float)TimePosition));
	NewNotify.TrackIndex = FMath::Max(0, TrackIndex);

	UAnimNotify* NotifyObj = NewObject<UAnimNotify>(Asset, NotifyClass);
	if (!NotifyObj) { OutError = TEXT("Failed to instantiate UAnimNotify_PlaySound"); return; }

	if (FObjectProperty* SoundProp = FindFProperty<FObjectProperty>(NotifyClass, TEXT("Sound")))
		SoundProp->SetObjectPropertyValue_InContainer(NotifyObj, SoundAsset);

	double Volume = 1.0;
	if (Spec->TryGetNumberField(TEXT("volume_multiplier"), Volume))
	{
		if (FFloatProperty* FP = FindFProperty<FFloatProperty>(NotifyClass, TEXT("VolumeMultiplier")))
			FP->SetPropertyValue_InContainer(NotifyObj, (float)Volume);
	}

	double Pitch = 1.0;
	if (Spec->TryGetNumberField(TEXT("pitch_multiplier"), Pitch))
	{
		if (FFloatProperty* FP = FindFProperty<FFloatProperty>(NotifyClass, TEXT("PitchMultiplier")))
			FP->SetPropertyValue_InContainer(NotifyObj, (float)Pitch);
	}

	bool bFollow = false;
	if (Spec->TryGetBoolField(TEXT("follow"), bFollow))
	{
		if (FBoolProperty* BP = FindFProperty<FBoolProperty>(NotifyClass, TEXT("bFollow")))
			BP->SetPropertyValue_InContainer(NotifyObj, bFollow);
	}

	FString AttachName;
	if (Spec->TryGetStringField(TEXT("attach_name"), AttachName))
	{
		if (FNameProperty* NP = FindFProperty<FNameProperty>(NotifyClass, TEXT("AttachName")))
			NP->SetPropertyValue_InContainer(NotifyObj, FName(*AttachName));
	}

	NewNotify.Notify = NotifyObj;

	Asset->Notifies.Add(NewNotify);
	Asset->RefreshCacheData();
	Asset->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(AssetPath, false);

	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("notify_name"), NewNotify.NotifyName.ToString());
	Obj->SetStringField(TEXT("sound_path"), SoundPath);
	Obj->SetNumberField(TEXT("time"), TimePosition);
	Obj->SetNumberField(TEXT("track_index"), NewNotify.TrackIndex);
	BuildSuccessJson(Obj, OutJson);
}

void HandleAddPlaySoundNotifyFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }

	const FString AnimPath = ResolveAnimAssetPath(Args);
	if (AnimPath.IsEmpty()) { OutError = TEXT("Missing required parameter: animation_path (also accepted: asset_path)"); return; }
	UAnimSequenceBase* Asset = Cast<UAnimSequenceBase>(UEditorAssetLibrary::LoadAsset(AnimPath));
	if (!Asset) { OutError = FString::Printf(TEXT("Could not load animation '%s'"), *AnimPath); return; }

	UClass* NotifyClass = FindPlaySoundClass();
	if (!NotifyClass) { OutError = TEXT("UAnimNotify_PlaySound class not found"); return; }

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("notifies"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }

			FString TrackName = BatchToolHelper::GetItemString(Item, TEXT("track_name"));
			int32 ExplicitIdx = -1;
			{ double D = -1; if (Item->TryGetNumberField(TEXT("track_index"), D)) ExplicitIdx = (int32)D; }
			FString TrackErr;
			int32 ResolvedTrack = ResolveNotifyTrackIndex(Asset, TrackName, ExplicitIdx, TrackErr);
			if (ResolvedTrack < 0) { Batch.AddFailure(i, TrackErr); continue; }

			FString ItemOut, ItemErr;
			AddPlaySoundNotifyCore(Asset, AnimPath, NotifyClass, Item, ResolvedTrack, ItemOut, ItemErr);
			if (ItemErr.IsEmpty())
			{
				auto Extra = MakeShared<FJsonObject>();
				FString SP; Item->TryGetStringField(TEXT("sound_path"), SP);
				Extra->SetStringField(TEXT("sound_path"), SP);
				Extra->SetNumberField(TEXT("track_index"), ResolvedTrack);
				Batch.AddSuccess(i, Extra);
			}
			else Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJsonString);
		return;
	}

	FString TrackName; Args->TryGetStringField(TEXT("track_name"), TrackName);
	int32 ExplicitIdx = -1;
	{ double D = -1; if (Args->TryGetNumberField(TEXT("track_index"), D)) ExplicitIdx = (int32)D; }
	FString TrackErr;
	int32 ResolvedTrack = ResolveNotifyTrackIndex(Asset, TrackName, ExplicitIdx, TrackErr);
	if (ResolvedTrack < 0) { OutError = TrackErr; return; }

	AddPlaySoundNotifyCore(Asset, AnimPath, NotifyClass, Args, ResolvedTrack, OutJsonString, OutError);
}

void HandleSetTransitionBlendSettingsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString StateMachineName, FromState, ToState, BlendMode;
	double CrossfadeDuration = 0.2;
	FString AnimBlueprintPath = GetAnimBpPathArg(Args);
	Args->TryGetStringField(TEXT("state_machine_name"), StateMachineName);
	Args->TryGetStringField(TEXT("from_state"), FromState);
	Args->TryGetStringField(TEXT("to_state"), ToState);
	Args->TryGetStringField(TEXT("blend_mode"), BlendMode);
	bool bSetDuration = Args->TryGetNumberField(TEXT("crossfade_duration"), CrossfadeDuration);
	HandleSetTransitionBlendSettings(AnimBlueprintPath, StateMachineName,
		FromState, ToState, (float)CrossfadeDuration, bSetDuration, BlendMode, OutJsonString, OutError);
}

void HandleSetAnimNotifyPropertyFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString AssetPath, NotifyName, PropertyName, PropertyValue;
	double TimePosition = 0.0;
	Args->TryGetStringField(TEXT("asset_path"),     AssetPath);
	Args->TryGetStringField(TEXT("notify_name"),    NotifyName);
	Args->TryGetNumberField(TEXT("time_position"),  TimePosition);
	Args->TryGetStringField(TEXT("property_name"),  PropertyName);
	Args->TryGetStringField(TEXT("property_value"), PropertyValue);
	HandleSetAnimNotifyProperty(AssetPath, NotifyName, (float)TimePosition,
		PropertyName, PropertyValue, OutJsonString, OutError);
}

void HandleGetAnimBpSummaryFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString Path = GetAnimBpPathArg(Args);
	HandleGetAnimBpSummary(Path, OutJsonString, OutError);
}

void HandleGetAnimGraphNodesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString Path = GetAnimBpPathArg(Args);
	bool bNested = false;
	if (Args.IsValid()) Args->TryGetBoolField(TEXT("include_nested"), bNested);
	HandleGetAnimGraphNodes(Path, OutJsonString, OutError, bNested);
}

void HandleGetSkeletonBonesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString Path;
	if (Args.IsValid()) Args->TryGetStringField(TEXT("skeleton_path"), Path);
	HandleGetSkeletonBones(Path, OutJsonString, OutError);
}

void HandleAddStateMachineFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString Name;
	double PX = 0, PY = 0;
	FString Path = GetAnimBpPathArg(Args);
	Args->TryGetStringField(TEXT("name"), Name);
	Args->TryGetNumberField(TEXT("position_x"), PX);
	Args->TryGetNumberField(TEXT("position_y"), PY);
	HandleAddStateMachine(Path, Name, (int32)PX, (int32)PY, OutJsonString, OutError);
}

void HandleRemoveAnimStateFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SM, SN;
	FString Path = GetAnimBpPathArg(Args);
	Args->TryGetStringField(TEXT("state_machine_name"), SM);
	Args->TryGetStringField(TEXT("state_name"), SN);
	HandleRemoveAnimState(Path, SM, SN, OutJsonString, OutError);
}

void HandleSetStateMachineEntryStateFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SM, EntryState;
	FString Path = GetAnimBpPathArg(Args);
	Args->TryGetStringField(TEXT("state_machine_name"), SM);
	if (!Args->TryGetStringField(TEXT("entry_state"), EntryState))
		Args->TryGetStringField(TEXT("entry_state_name"), EntryState);
	if (EntryState.IsEmpty()) Args->TryGetStringField(TEXT("state_name"), EntryState);
	HandleSetStateMachineEntryState(Path, SM, EntryState, OutJsonString, OutError);
}

void HandleWireAnimNodeToOutputFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString NodeName;
	FString Path = GetAnimBpPathArg(Args);
	if (!Args->TryGetStringField(TEXT("node_name"), NodeName))
		Args->TryGetStringField(TEXT("source_node"), NodeName);
	if (NodeName.IsEmpty()) Args->TryGetStringField(TEXT("state_machine_name"), NodeName);
	HandleWireAnimNodeToOutput(Path, NodeName, OutJsonString, OutError);
}

void HandleRemoveStateTransitionFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SM, FS, TS;
	FString Path = GetAnimBpPathArg(Args);
	Args->TryGetStringField(TEXT("state_machine_name"), SM);
	Args->TryGetStringField(TEXT("from_state"), FS);
	Args->TryGetStringField(TEXT("to_state"), TS);
	HandleRemoveStateTransition(Path, SM, FS, TS, OutJsonString, OutError);
}

void HandleRenameAnimStateFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SM, ON, NN;
	FString Path = GetAnimBpPathArg(Args);
	Args->TryGetStringField(TEXT("state_machine_name"), SM);
	Args->TryGetStringField(TEXT("old_name"), ON);
	Args->TryGetStringField(TEXT("new_name"), NN);
	HandleRenameAnimState(Path, SM, ON, NN, OutJsonString, OutError);
}

void HandleCreateAnimSlotFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SkelPath, SlotName, GroupName;
	Args->TryGetStringField(TEXT("skeleton_path"), SkelPath);
	Args->TryGetStringField(TEXT("slot_name"), SlotName);
	Args->TryGetStringField(TEXT("group_name"), GroupName);
	HandleCreateAnimSlot(SkelPath, SlotName, GroupName, OutJsonString, OutError);
}

void HandleGetBlendspaceInfoFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString Path;
	if (Args.IsValid())
	{
		if (!Args->TryGetStringField(TEXT("blendspace_path"), Path))
			if (!Args->TryGetStringField(TEXT("asset_path"), Path))
				Args->TryGetStringField(TEXT("path"), Path);
	}
	if (Path.IsEmpty()) { OutError = TEXT("Missing required parameter: blendspace_path (or asset_path)"); return; }
	HandleGetBlendspaceInfo(Path, OutJsonString, OutError);
}

void HandleSetIKRetargetRootFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString Path, Bone;
	Args->TryGetStringField(TEXT("asset_path"), Path);
	Args->TryGetStringField(TEXT("bone_name"), Bone);
	HandleSetIKRetargetRoot(Path, Bone, OutJsonString, OutError);
}

void HandleSetRetargetPoseFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString Path, PoseName, Side;
	Args->TryGetStringField(TEXT("asset_path"), Path);
	Args->TryGetStringField(TEXT("pose_name"), PoseName);
	Args->TryGetStringField(TEXT("source_or_target"), Side);
	HandleSetRetargetPose(Path, PoseName, Side, OutJsonString, OutError);
}

void HandleCreateAnimLayerInterfaceFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString Name, SavePath;
	Args->TryGetStringField(TEXT("name"), Name);
	Args->TryGetStringField(TEXT("save_path"), SavePath);
	HandleCreateAnimLayerInterface(Name, SavePath, OutJsonString, OutError);
}

void HandleImplementAnimLayerFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString IfacePath;
	FString Path = GetAnimBpPathArg(Args);
	Args->TryGetStringField(TEXT("interface_path"), IfacePath);
	HandleImplementAnimLayer(Path, IfacePath, OutJsonString, OutError);
}

void HandleAddAnimLayerNodeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString LayerName;
	double PX = 0, PY = 0;
	FString Path = GetAnimBpPathArg(Args);
	Args->TryGetStringField(TEXT("layer_name"), LayerName);
	Args->TryGetNumberField(TEXT("position_x"), PX);
	Args->TryGetNumberField(TEXT("position_y"), PY);
	HandleAddAnimLayerNode(Path, LayerName, (int32)PX, (int32)PY, OutJsonString, OutError);
}

static FString ResolveAnimAssetPath(const TSharedPtr<FJsonObject>& Args)
{
	if (!Args.IsValid()) return FString();
	FString P;
	for (const TCHAR* Key : { TEXT("animation_path"), TEXT("montage_path"),
		TEXT("anim_path"), TEXT("anim_sequence_path"), TEXT("asset_path") })
	{
		if (Args->TryGetStringField(Key, P) && !P.IsEmpty()) return P;
	}
	return FString();
}

static FString ResolveIKRigPath(const TSharedPtr<FJsonObject>& Args)
{
	if (!Args.IsValid()) return FString();
	FString P;
	for (const TCHAR* Key : { TEXT("ik_rig_path"), TEXT("asset_path"),
		TEXT("rig_path"), TEXT("path") })
	{
		if (Args->TryGetStringField(Key, P) && !P.IsEmpty()) return P;
	}
	return FString();
}

static FString ResolveRetargeterPath(const TSharedPtr<FJsonObject>& Args)
{
	if (!Args.IsValid()) return FString();
	FString P;
	for (const TCHAR* Key : { TEXT("retargeter_path"), TEXT("asset_path"),
		TEXT("ik_retargeter_path"), TEXT("path") })
	{
		if (Args->TryGetStringField(Key, P) && !P.IsEmpty()) return P;
	}
	return FString();
}

void HandleGetAnimSequenceInfoFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	const FString P = ResolveAnimAssetPath(Args);
	if (P.IsEmpty()) { OutError = TEXT("Missing required parameter: animation_path (also accepted: asset_path, anim_path, anim_sequence_path)"); return; }
	HandleGetAnimSequenceInfo(P, OutJsonString, OutError);
}

void HandleGetMontageSummaryFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	const FString P = ResolveAnimAssetPath(Args);
	if (P.IsEmpty()) { OutError = TEXT("Missing required parameter: montage_path (also accepted: asset_path)"); return; }
	HandleGetMontageSummary(P, OutJsonString, OutError);
}

void HandleGetIKRigSummaryFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString P;
	if (Args.IsValid()) Args->TryGetStringField(TEXT("asset_path"), P);
	HandleGetIKRigSummary(P, OutJsonString, OutError);
}

void HandleListAnimSlotsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString P;
	if (Args.IsValid()) Args->TryGetStringField(TEXT("skeleton_path"), P);
	HandleListAnimSlots(P, OutJsonString, OutError);
}

void HandleAddLayeredBlendPerBoneFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	double NL = 1, PX = 0, PY = 0;
	FString P = GetAnimBpPathArg(Args);
	Args->TryGetNumberField(TEXT("num_layers"), NL);
	Args->TryGetNumberField(TEXT("position_x"), PX);
	Args->TryGetNumberField(TEXT("position_y"), PY);
	HandleAddLayeredBlendPerBone(P, (int32)NL, (int32)PX, (int32)PY, OutJsonString, OutError);
}

void HandleAddSavedPoseFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString CN;
	double PX = 0, PY = 0;
	FString P = GetAnimBpPathArg(Args);
	Args->TryGetStringField(TEXT("cache_name"), CN);
	Args->TryGetNumberField(TEXT("position_x"), PX);
	Args->TryGetNumberField(TEXT("position_y"), PY);
	HandleAddSavedPose(P, CN, (int32)PX, (int32)PY, OutJsonString, OutError);
}

void HandleUseCachedPoseFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString CN;
	double PX = 0, PY = 0;
	FString P = GetAnimBpPathArg(Args);
	Args->TryGetStringField(TEXT("cache_name"), CN);
	Args->TryGetNumberField(TEXT("position_x"), PX);
	Args->TryGetNumberField(TEXT("position_y"), PY);
	HandleUseCachedPose(P, CN, (int32)PX, (int32)PY, OutJsonString, OutError);
}

void HandleAddBlendByBoolFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	double PX = 0, PY = 0;
	FString P = GetAnimBpPathArg(Args);
	Args->TryGetNumberField(TEXT("position_x"), PX);
	Args->TryGetNumberField(TEXT("position_y"), PY);
	HandleAddBlendByBool(P, (int32)PX, (int32)PY, OutJsonString, OutError);
}

void HandleRemoveAnimNotifyFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString AP, NN;
	double TP = 0;
	Args->TryGetStringField(TEXT("animation_path"), AP);
	Args->TryGetStringField(TEXT("notify_name"), NN);
	Args->TryGetNumberField(TEXT("time_position"), TP);
	HandleRemoveAnimNotify(AP, NN, (float)TP, OutJsonString, OutError);
}

void HandleAddBlendByIntFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	double NP = 2, PX = 0, PY = 0;
	FString P = GetAnimBpPathArg(Args);
	Args->TryGetNumberField(TEXT("num_poses"), NP);
	Args->TryGetNumberField(TEXT("position_x"), PX);
	Args->TryGetNumberField(TEXT("position_y"), PY);
	HandleAddBlendByInt(P, (int32)NP, (int32)PX, (int32)PY, OutJsonString, OutError);
}

void HandleSetBlendspaceAxisFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString P, AN;
	double AI = 0, AMn = 0, AMx = 0, GD = -1;
	Args->TryGetStringField(TEXT("blendspace_path"), P);
	Args->TryGetStringField(TEXT("axis_name"), AN);
	Args->TryGetNumberField(TEXT("axis_index"), AI);
	// Unspecified bounds are passed as NaN so the handler keeps the current value for that side.
	const float UnsetF = std::numeric_limits<float>::quiet_NaN();
	const float MinF = Args->TryGetNumberField(TEXT("axis_min"), AMn) ? (float)AMn : UnsetF;
	const float MaxF = Args->TryGetNumberField(TEXT("axis_max"), AMx) ? (float)AMx : UnsetF;
	Args->TryGetNumberField(TEXT("grid_divisions"), GD);
	int32 Snap = -1, Wrap = -1;
	bool BV = false;
	if (Args->TryGetBoolField(TEXT("snap_to_grid"), BV)) Snap = BV ? 1 : 0;
	if (Args->TryGetBoolField(TEXT("wrap_input"), BV))   Wrap = BV ? 1 : 0;
	HandleSetBlendspaceAxis(P, (int32)AI, AN, MinF, MaxF, (int32)GD, OutJsonString, OutError, Snap, Wrap);
}

void HandleAddAnimNotifyTrackFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString TN;
	Args->TryGetStringField(TEXT("track_name"), TN);
	const FString AP = ResolveAnimAssetPath(Args);
	if (AP.IsEmpty()) { OutError = TEXT("Missing required parameter: animation_path (also accepted: montage_path, asset_path) — notify tracks live on AnimSequenceBase, which includes both AnimSequence and AnimMontage"); return; }
	HandleAddAnimNotifyTrack(AP, TN, OutJsonString, OutError);
}

void HandleSetAnimCurveKeyFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString AP, CN, CT;
	double KT = 0, KV = 0;
	Args->TryGetStringField(TEXT("animation_path"), AP);
	Args->TryGetStringField(TEXT("curve_name"), CN);
	Args->TryGetStringField(TEXT("curve_type"), CT);
	Args->TryGetNumberField(TEXT("key_time"), KT);
	Args->TryGetNumberField(TEXT("key_value"), KV);
	HandleSetAnimCurveKey(AP, CN, (float)KT, (float)KV, OutJsonString, OutError, CT);
}

void HandleGetRetargeterSummaryFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString P;
	if (Args.IsValid()) Args->TryGetStringField(TEXT("asset_path"), P);
	HandleGetRetargeterSummary(P, OutJsonString, OutError);
}

void HandleCreateAnimBlueprintFromParentFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString N, SP, PP, SKP;
	Args->TryGetStringField(TEXT("name"), N);
	Args->TryGetStringField(TEXT("save_path"), SP);
	Args->TryGetStringField(TEXT("parent_path"), PP);
	Args->TryGetStringField(TEXT("skeleton_path"), SKP);
	HandleCreateAnimBlueprintFromParent(N, SP, PP, SKP, OutJsonString, OutError);
}

void HandleRemoveAnimCurveFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString AP, CN, CT;
	Args->TryGetStringField(TEXT("animation_path"), AP);
	Args->TryGetStringField(TEXT("curve_name"), CN);
	Args->TryGetStringField(TEXT("curve_type"), CT);
	HandleRemoveAnimCurve(AP, CN, OutJsonString, OutError, CT);
}

void HandleAddSubAnimInstanceFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString LBP;
	double PX = 0, PY = 0;
	FString P = GetAnimBpPathArg(Args);
	Args->TryGetStringField(TEXT("linked_blueprint_path"), LBP);
	Args->TryGetNumberField(TEXT("position_x"), PX);
	Args->TryGetNumberField(TEXT("position_y"), PY);
	HandleAddSubAnimInstance(P, LBP, (int32)PX, (int32)PY, OutJsonString, OutError);
}

void HandleRemoveBlendspaceSampleFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString P;
	double SX = 0, SY = 0;
	Args->TryGetStringField(TEXT("blendspace_path"), P);
	Args->TryGetNumberField(TEXT("sample_x"), SX);
	Args->TryGetNumberField(TEXT("sample_y"), SY);
	HandleRemoveBlendspaceSample(P, (float)SX, (float)SY, OutJsonString, OutError);
}

void HandleRemoveMontageSectionFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString P, SN;
	Args->TryGetStringField(TEXT("montage_path"), P);
	Args->TryGetStringField(TEXT("section_name"), SN);
	HandleRemoveMontageSection(P, SN, OutJsonString, OutError);
}

void HandleRenameMontageSectionFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString P, ON, NN;
	Args->TryGetStringField(TEXT("montage_path"), P);
	Args->TryGetStringField(TEXT("old_name"), ON);
	Args->TryGetStringField(TEXT("new_name"), NN);
	HandleRenameMontageSection(P, ON, NN, OutJsonString, OutError);
}

void HandleGetSkeletonSocketsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString P;
	if (Args.IsValid()) Args->TryGetStringField(TEXT("skeleton_path"), P);
	HandleGetSkeletonSockets(P, OutJsonString, OutError);
}

void HandleRemoveIKGoalFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString P, GN;
	Args->TryGetStringField(TEXT("asset_path"), P);
	Args->TryGetStringField(TEXT("goal_name"), GN);
	HandleRemoveIKGoal(P, GN, OutJsonString, OutError);
}

void HandleRemoveRetargetChainFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString P, CN;
	Args->TryGetStringField(TEXT("asset_path"), P);
	Args->TryGetStringField(TEXT("chain_name"), CN);
	HandleRemoveRetargetChain(P, CN, OutJsonString, OutError);
}

static void PosedGraphNodeFromArgs(const TSharedPtr<FJsonObject>& Args,
	void (*Fn)(const FString&, int32, int32, FString&, FString&),
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	double PX = 0, PY = 0;
	FString P = GetAnimBpPathArg(Args);
	Args->TryGetNumberField(TEXT("position_x"), PX);
	Args->TryGetNumberField(TEXT("position_y"), PY);
	Fn(P, (int32)PX, (int32)PY, OutJsonString, OutError);
}

void HandleAddModifyBoneFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString BN;
	double PX = 0, PY = 0;
	FString P = GetAnimBpPathArg(Args);
	Args->TryGetStringField(TEXT("bone_name"), BN);
	Args->TryGetNumberField(TEXT("position_x"), PX);
	Args->TryGetNumberField(TEXT("position_y"), PY);
	HandleAddModifyBone(P, BN, (int32)PX, (int32)PY, OutJsonString, OutError);
}

void HandleAddCopyBoneFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SB, TB;
	double PX = 0, PY = 0;
	FString P = GetAnimBpPathArg(Args);
	Args->TryGetStringField(TEXT("source_bone"), SB);
	Args->TryGetStringField(TEXT("target_bone"), TB);
	Args->TryGetNumberField(TEXT("position_x"), PX);
	Args->TryGetNumberField(TEXT("position_y"), PY);
	HandleAddCopyBone(P, SB, TB, (int32)PX, (int32)PY, OutJsonString, OutError);
}

void HandleAddLookAtFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString BN, LB;
	double PX = 0, PY = 0;
	FString P = GetAnimBpPathArg(Args);
	Args->TryGetStringField(TEXT("bone_name"), BN);
	Args->TryGetStringField(TEXT("look_at_bone"), LB);
	Args->TryGetNumberField(TEXT("position_x"), PX);
	Args->TryGetNumberField(TEXT("position_y"), PY);
	HandleAddLookAt(P, BN, LB, (int32)PX, (int32)PY, OutJsonString, OutError);
}

void HandleAddTwoBoneIKFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString IK, EB, JB;
	double PX = 0, PY = 0;
	FString P = GetAnimBpPathArg(Args);
	Args->TryGetStringField(TEXT("ik_bone"), IK);
	Args->TryGetStringField(TEXT("effector_bone"), EB);
	Args->TryGetStringField(TEXT("joint_target_bone"), JB);
	Args->TryGetNumberField(TEXT("position_x"), PX);
	Args->TryGetNumberField(TEXT("position_y"), PY);
	HandleAddTwoBoneIK(P, IK, EB, JB, (int32)PX, (int32)PY, OutJsonString, OutError);
}

void HandleAddApplyAdditiveFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{ PosedGraphNodeFromArgs(Args, &HandleAddApplyAdditive, OutJsonString, OutError); }

void HandleAddMakeDynamicAdditiveFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{ PosedGraphNodeFromArgs(Args, &HandleAddMakeDynamicAdditive, OutJsonString, OutError); }

void HandleAddInertializationFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{ PosedGraphNodeFromArgs(Args, &HandleAddInertialization, OutJsonString, OutError); }

void HandleAddBlendByEnumFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString EC;
	double PX = 0, PY = 0;
	FString P = GetAnimBpPathArg(Args);
	Args->TryGetStringField(TEXT("enum_class"), EC);
	Args->TryGetNumberField(TEXT("position_x"), PX);
	Args->TryGetNumberField(TEXT("position_y"), PY);
	HandleAddBlendByEnum(P, EC, (int32)PX, (int32)PY, OutJsonString, OutError);
}

void HandleAddSequenceEvaluatorFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString AP;
	double PX = 0, PY = 0;
	FString P = GetAnimBpPathArg(Args);
	Args->TryGetStringField(TEXT("animation_path"), AP);
	Args->TryGetNumberField(TEXT("position_x"), PX);
	Args->TryGetNumberField(TEXT("position_y"), PY);
	HandleAddSequenceEvaluator(P, AP, (int32)PX, (int32)PY, OutJsonString, OutError);
}

void HandleAddRandomPlayerFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{ PosedGraphNodeFromArgs(Args, &HandleAddRandomPlayer, OutJsonString, OutError); }

void HandleListVirtualBonesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString SP;
	if (Args.IsValid()) Args->TryGetStringField(TEXT("skeleton_path"), SP);
	HandleListVirtualBones(SP, OutJsonString, OutError);
}

void HandleGetSkeletonHierarchyFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString SP;
	if (Args.IsValid()) Args->TryGetStringField(TEXT("skeleton_path"), SP);
	HandleGetSkeletonHierarchy(SP, OutJsonString, OutError);
}

void HandleConnectAnimNodesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SG, TG;
	FString P = GetAnimBpPathArg(Args);
	Args->TryGetStringField(TEXT("source_node_guid"), SG);
	Args->TryGetStringField(TEXT("target_node_guid"), TG);
	HandleConnectAnimNodes(P, SG, TG, OutJsonString, OutError);
}

void HandleBuildAnimChainFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString CH;
	bool bAC = true;
	FString P = GetAnimBpPathArg(Args);
	Args->TryGetStringField(TEXT("chain"), CH);
	Args->TryGetBoolField(TEXT("auto_connect_to_output"), bAC);
	HandleBuildAnimChain(P, CH, bAC, OutJsonString, OutError);
}

void HandleAddControlRigNodeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString CRP;
	double PX = 0, PY = 0;
	FString P = GetAnimBpPathArg(Args);
	Args->TryGetStringField(TEXT("control_rig_path"), CRP);
	Args->TryGetNumberField(TEXT("position_x"), PX);
	Args->TryGetNumberField(TEXT("position_y"), PY);
	HandleAddControlRigNode(P, CRP, (int32)PX, (int32)PY, OutJsonString, OutError);
}

void HandleAddSlotNodeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SN;
	double PX = 0, PY = 0;
	FString P = GetAnimBpPathArg(Args);
	Args->TryGetStringField(TEXT("slot_name"), SN);
	Args->TryGetNumberField(TEXT("position_x"), PX);
	Args->TryGetNumberField(TEXT("position_y"), PY);
	HandleAddSlotNode(P, SN, (int32)PX, (int32)PY, OutJsonString, OutError);
}

void HandleAddTwoWayBlendFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{ PosedGraphNodeFromArgs(Args, &HandleAddTwoWayBlend, OutJsonString, OutError); }

void HandleAddApplyMeshSpaceAdditiveFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{ PosedGraphNodeFromArgs(Args, &HandleAddApplyMeshSpaceAdditive, OutJsonString, OutError); }

void HandleAddCopyPoseFromMeshFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{ PosedGraphNodeFromArgs(Args, &HandleAddCopyPoseFromMesh, OutJsonString, OutError); }

void HandleAddRotateRootBoneFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{ PosedGraphNodeFromArgs(Args, &HandleAddRotateRootBone, OutJsonString, OutError); }

void HandleAddAimOffsetPlayerFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString AP;
	double PX = 0, PY = 0;
	FString P = GetAnimBpPathArg(Args);
	Args->TryGetStringField(TEXT("animation_path"), AP);
	Args->TryGetNumberField(TEXT("position_x"), PX);
	Args->TryGetNumberField(TEXT("position_y"), PY);
	HandleAddAimOffsetPlayer(P, AP, (int32)PX, (int32)PY, OutJsonString, OutError);
}

void HandleAddMirrorFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString MDT;
	double PX = 0, PY = 0;
	FString P = GetAnimBpPathArg(Args);
	Args->TryGetStringField(TEXT("mirror_data_table_path"), MDT);
	Args->TryGetNumberField(TEXT("position_x"), PX);
	Args->TryGetNumberField(TEXT("position_y"), PY);
	HandleAddMirror(P, MDT, (int32)PX, (int32)PY, OutJsonString, OutError);
}

void HandleAddLocalToComponentSpaceFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{ PosedGraphNodeFromArgs(Args, &HandleAddLocalToComponentSpace, OutJsonString, OutError); }

void HandleAddComponentToLocalSpaceFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{ PosedGraphNodeFromArgs(Args, &HandleAddComponentToLocalSpace, OutJsonString, OutError); }

void HandleAddMeshRefPoseFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{ PosedGraphNodeFromArgs(Args, &HandleAddMeshRefPose, OutJsonString, OutError); }

void HandleAddLocalRefPoseFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{ PosedGraphNodeFromArgs(Args, &HandleAddLocalRefPose, OutJsonString, OutError); }

void HandleAddIdentityPoseFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{ PosedGraphNodeFromArgs(Args, &HandleAddIdentityPose, OutJsonString, OutError); }

void HandleAddSpringBoneFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString BN;
	double PX = 0, PY = 0;
	FString P = GetAnimBpPathArg(Args);
	Args->TryGetStringField(TEXT("bone_name"), BN);
	Args->TryGetNumberField(TEXT("position_x"), PX);
	Args->TryGetNumberField(TEXT("position_y"), PY);
	HandleAddSpringBone(P, BN, (int32)PX, (int32)PY, OutJsonString, OutError);
}

void HandleAddRigidBodyFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{ PosedGraphNodeFromArgs(Args, &HandleAddRigidBody, OutJsonString, OutError); }

void HandleAddFabrikFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString TB, RB;
	double PX = 0, PY = 0;
	FString P = GetAnimBpPathArg(Args);
	Args->TryGetStringField(TEXT("tip_bone"), TB);
	Args->TryGetStringField(TEXT("root_bone"), RB);
	Args->TryGetNumberField(TEXT("position_x"), PX);
	Args->TryGetNumberField(TEXT("position_y"), PY);
	HandleAddFabrik(P, TB, RB, (int32)PX, (int32)PY, OutJsonString, OutError);
}

void HandleAddCCDIKFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString TB, RB;
	double PX = 0, PY = 0;
	FString P = GetAnimBpPathArg(Args);
	Args->TryGetStringField(TEXT("tip_bone"), TB);
	Args->TryGetStringField(TEXT("root_bone"), RB);
	Args->TryGetNumberField(TEXT("position_x"), PX);
	Args->TryGetNumberField(TEXT("position_y"), PY);
	HandleAddCCDIK(P, TB, RB, (int32)PX, (int32)PY, OutJsonString, OutError);
}

void HandleAddLegIKFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{ PosedGraphNodeFromArgs(Args, &HandleAddLegIK, OutJsonString, OutError); }

void HandleAddPoseByNameFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString PAP, PN;
	double PX = 0, PY = 0;
	FString P = GetAnimBpPathArg(Args);
	Args->TryGetStringField(TEXT("pose_asset_path"), PAP);
	Args->TryGetStringField(TEXT("pose_name"), PN);
	Args->TryGetNumberField(TEXT("position_x"), PX);
	Args->TryGetNumberField(TEXT("position_y"), PY);
	HandleAddPoseByName(P, PAP, PN, (int32)PX, (int32)PY, OutJsonString, OutError);
}

void HandleAddBoneDrivenControllerFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SB, TB;
	double PX = 0, PY = 0;
	FString P = GetAnimBpPathArg(Args);
	Args->TryGetStringField(TEXT("source_bone"), SB);
	Args->TryGetStringField(TEXT("target_bone"), TB);
	Args->TryGetNumberField(TEXT("position_x"), PX);
	Args->TryGetNumberField(TEXT("position_y"), PY);
	HandleAddBoneDrivenController(P, SB, TB, (int32)PX, (int32)PY, OutJsonString, OutError);
}

void HandleAddBlendBoneByChannelFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{ PosedGraphNodeFromArgs(Args, &HandleAddBlendBoneByChannel, OutJsonString, OutError); }

void HandleAddMotionMatchingNodeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString PSDB;
	double PX = 0, PY = 0;
	FString P = GetAnimBpPathArg(Args);
	Args->TryGetStringField(TEXT("pose_search_db_path"), PSDB);
	Args->TryGetNumberField(TEXT("position_x"), PX);
	Args->TryGetNumberField(TEXT("position_y"), PY);
	HandleAddMotionMatchingNode(P, PSDB, (int32)PX, (int32)PY, OutJsonString, OutError);
}

void HandleGetAnimMontageSectionsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString MP;
	if (Args.IsValid()) Args->TryGetStringField(TEXT("montage_path"), MP);
	HandleGetAnimMontageSections(MP, OutJsonString, OutError);
}

void HandleSetMontageSectionLinkFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString MP, SN, NS;
	Args->TryGetStringField(TEXT("montage_path"), MP);
	Args->TryGetStringField(TEXT("section_name"), SN);
	Args->TryGetStringField(TEXT("next_section_name"), NS);
	HandleSetMontageSectionLink(MP, SN, NS, OutJsonString, OutError);
}

void HandleCreateSyncGroupFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SGN, GR;
	FString ABP = GetAnimBpPathArg(Args);
	Args->TryGetStringField(TEXT("sync_group_name"), SGN);
	Args->TryGetStringField(TEXT("group_role"), GR);
	HandleCreateSyncGroup(ABP, SGN, GR, OutJsonString, OutError);
}

void HandleAddBlendProfileFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SP, PN;
	Args->TryGetStringField(TEXT("skeleton_path"), SP);
	Args->TryGetStringField(TEXT("profile_name"), PN);
	HandleAddBlendProfile(SP, PN, OutJsonString, OutError);
}

void HandleAddAnimSyncMarkerFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString AnimPath; Args->TryGetStringField(TEXT("animation_path"), AnimPath);

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("markers"), ItemsArray))
	{
		if (AnimPath.IsEmpty() && ItemsArray->Num() > 0)
		{
			TSharedPtr<FJsonObject> First = (*ItemsArray)[0]->AsObject();
			if (First.IsValid()) First->TryGetStringField(TEXT("animation_path"), AnimPath);
		}
		if (AnimPath.IsEmpty()) { OutError = TEXT("Missing required parameter: animation_path"); return; }
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString MN = BatchToolHelper::GetItemString(Item, TEXT("marker_name"), TEXT("name"));
			double T = 0.0; Item->TryGetNumberField(TEXT("time"), T);
			if (MN.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing marker_name")); continue; }
			FString ItemOut, ItemErr;
			HandleAddAnimSyncMarker(AnimPath, MN, (float)T, ItemOut, ItemErr);
			if (ItemErr.IsEmpty())
			{
				auto Extra = MakeShared<FJsonObject>();
				Extra->SetStringField(TEXT("marker_name"), MN);
				Extra->SetNumberField(TEXT("time"), T);
				Batch.AddSuccess(i, Extra);
			}
			else Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJsonString);
		return;
	}

	FString MarkerName; double Time = 0.0;
	Args->TryGetStringField(TEXT("marker_name"), MarkerName);
	Args->TryGetNumberField(TEXT("time"), Time);
	HandleAddAnimSyncMarker(AnimPath, MarkerName, (float)Time, OutJsonString, OutError);
}

void HandleRemoveAnimSyncMarkerFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString AnimPath, MarkerName;
	Args->TryGetStringField(TEXT("animation_path"), AnimPath);
	Args->TryGetStringField(TEXT("marker_name"), MarkerName);

	double Time = 0.0;
	const bool bMatchTime = Args->TryGetNumberField(TEXT("time"), Time);

	HandleRemoveAnimSyncMarker(AnimPath, MarkerName, (float)Time, bMatchTime, OutJsonString, OutError);
}

void HandleListAnimSyncMarkersFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString AnimPath; Args->TryGetStringField(TEXT("animation_path"), AnimPath);
	HandleListAnimSyncMarkers(AnimPath, OutJsonString, OutError);
}

void HandleGetAnimStateMachinesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString ABP = GetAnimBpPathArg(Args);
	HandleGetAnimStateMachines(ABP, OutJsonString, OutError);
}

void HandleCompileAnimBlueprint(const FString& AnimBPPath, FString& OutJson, FString& OutError)
{
	UAnimBlueprint* AnimBP = Cast<UAnimBlueprint>(UEditorAssetLibrary::LoadAsset(AnimBPPath));
	if (!AnimBP) { SetError(FString::Printf(TEXT("Could not load AnimBlueprint: %s"), *AnimBPPath), OutJson, OutError); return; }

	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetStringField(TEXT("asset_path"), AnimBPPath);
	CompileAnimBlueprintAndReport(AnimBP, Obj);

	bool bCompiled = false;
	Obj->TryGetBoolField(TEXT("compiled"), bCompiled);
	Obj->SetBoolField(TEXT("success"), bCompiled);
	if (!bCompiled)
		Obj->SetStringField(TEXT("error"), TEXT("AnimBlueprint compiled with errors; see compile_errors[]."));

	UEditorAssetLibrary::SaveAsset(AnimBPPath, false);
	BuildSuccessJson(Obj, OutJson);
}

void HandleCompileAnimBlueprintFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString ABP = GetAnimBpPathArg(Args);
	if (ABP.IsEmpty()) { OutError = TEXT("Missing required parameter: anim_blueprint_path"); return; }
	HandleCompileAnimBlueprint(ABP, OutJsonString, OutError);
}

void HandleSetAnimNodePropertyFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString NG, PN, PV;
	FString ABP = GetAnimBpPathArg(Args);
	Args->TryGetStringField(TEXT("node_guid"), NG);
	Args->TryGetStringField(TEXT("property_name"), PN);
	Args->TryGetStringField(TEXT("property_value"), PV);
	HandleSetAnimNodeProperty(ABP, NG, PN, PV, OutJsonString, OutError);
}

void HandleAddMotionWarpingWindowFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString MP, WT;
	double ST = 0, ET = 1;
	Args->TryGetStringField(TEXT("montage_path"), MP);
	Args->TryGetStringField(TEXT("warp_target_name"), WT);
	Args->TryGetNumberField(TEXT("start_time"), ST);
	Args->TryGetNumberField(TEXT("end_time"), ET);
	HandleAddMotionWarpingWindow(MP, WT, (float)ST, (float)ET, OutJsonString, OutError);
}

void HandleGetMotionWarpingWindowsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString MP;
	if (Args.IsValid()) Args->TryGetStringField(TEXT("montage_path"), MP);
	HandleGetMotionWarpingWindows(MP, OutJsonString, OutError);
}

void HandleRemoveMotionWarpingWindowFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString MP, WT;
	Args->TryGetStringField(TEXT("montage_path"), MP);
	Args->TryGetStringField(TEXT("warp_target_name"), WT);
	HandleRemoveMotionWarpingWindow(MP, WT, OutJsonString, OutError);
}

namespace
{
	FAnimNotifyEvent* FindAnimNotifyForEdit(UAnimSequenceBase* Seq, const FString& NotifyName,
		float TimePosition, FString& OutError)
	{
		FAnimNotifyEvent* Found = nullptr;
		float BestDist = MAX_FLT;
		const bool bUseName = !NotifyName.IsEmpty();
		for (FAnimNotifyEvent& Event : Seq->Notifies)
		{
			if (bUseName && Event.NotifyName.ToString().Equals(NotifyName, ESearchCase::IgnoreCase))
			{
				return &Event;
			}
			if (!bUseName)
			{
				const float Dist = FMath::Abs(Event.GetTime() - TimePosition);
				if (Dist < BestDist) { BestDist = Dist; Found = &Event; }
			}
		}
		if (!Found)
		{
			OutError = bUseName
				? FString::Printf(TEXT("No notify named '%s' on '%s'"), *NotifyName, *Seq->GetName())
				: FString::Printf(TEXT("No notifies on '%s'"), *Seq->GetName());
		}
		return Found;
	}

	void RefreshStateNotifyEndLink(UAnimSequenceBase* Seq, FAnimNotifyEvent& Event)
	{
		if (!Event.NotifyStateClass) return;
		const float EndTime = Event.GetTime() + Event.GetDuration();
		Event.EndLink.Link(Seq, EndTime);
		Event.EndTriggerTimeOffset = GetTriggerTimeOffsetForType(Seq->CalculateOffsetForNotify(EndTime));
	}
}

void HandleMoveAnimNotify(const FString& AssetPath, const FString& NotifyName, float TimePosition,
	float NewTime, int32 NewTrackIndex, const FString& NewTrackName,
	FString& OutJson, FString& OutError)
{
	UAnimSequenceBase* Seq = LoadObject<UAnimSequenceBase>(nullptr, *AssetPath);
	if (!Seq) { OutError = FString::Printf(TEXT("AnimSequenceBase not found at '%s'"), *AssetPath); return; }

	const bool bMoveTime  = (NewTime >= 0.0f);
	const bool bMoveTrack = (NewTrackIndex >= 0) || !NewTrackName.IsEmpty();
	if (!bMoveTime && !bMoveTrack)
	{
		OutError = TEXT("Specify at least one of new_time / new_track_index / new_track_name");
		return;
	}

	FAnimNotifyEvent* Event = FindAnimNotifyForEdit(Seq, NotifyName, TimePosition, OutError);
	if (!Event) return;

	const float OldTime = Event->GetTime();
	int32 ResolvedTrackIndex = Event->TrackIndex;
	if (bMoveTrack)
	{
		if (!NewTrackName.IsEmpty())
		{
			const TArray<FAnimNotifyTrack>& Tracks = Seq->AnimNotifyTracks;
			int32 Idx = INDEX_NONE;
			for (int32 i = 0; i < Tracks.Num(); ++i)
			{
				if (Tracks[i].TrackName.ToString().Equals(NewTrackName, ESearchCase::IgnoreCase))
				{ Idx = i; break; }
			}
			if (Idx == INDEX_NONE)
			{
				OutError = FString::Printf(TEXT("No notify track named '%s'"), *NewTrackName);
				return;
			}
			ResolvedTrackIndex = Idx;
		}
		else
		{
			ResolvedTrackIndex = NewTrackIndex;
		}
	}

	if (bMoveTime)
	{
		Event->SetTime(NewTime);
		Event->TriggerTimeOffset = GetTriggerTimeOffsetForType(Seq->CalculateOffsetForNotify(NewTime));
		RefreshStateNotifyEndLink(Seq, *Event);
	}
	if (bMoveTrack)
	{
		Event->TrackIndex = FMath::Max(0, ResolvedTrackIndex);
	}

	Seq->RefreshCacheData();
	Seq->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(AssetPath, false);

	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("notify_name"), Event->NotifyName.ToString());
	Obj->SetNumberField(TEXT("old_time"), OldTime);
	Obj->SetNumberField(TEXT("time"), Event->GetTime());
	Obj->SetNumberField(TEXT("track_index"), Event->TrackIndex);
	if (Event->NotifyStateClass) Obj->SetNumberField(TEXT("duration"), Event->GetDuration());
	BuildSuccessJson(Obj, OutJson);
}

void HandleSetAnimNotifyDuration(const FString& AssetPath, const FString& NotifyName, float TimePosition,
	float NewDuration, FString& OutJson, FString& OutError)
{
	UAnimSequenceBase* Seq = LoadObject<UAnimSequenceBase>(nullptr, *AssetPath);
	if (!Seq) { OutError = FString::Printf(TEXT("AnimSequenceBase not found at '%s'"), *AssetPath); return; }

	if (NewDuration <= 0.0f)
	{
		OutError = TEXT("new_duration must be > 0");
		return;
	}

	FAnimNotifyEvent* Event = FindAnimNotifyForEdit(Seq, NotifyName, TimePosition, OutError);
	if (!Event) return;

	if (!Event->NotifyStateClass)
	{
		OutError = FString::Printf(
			TEXT("Notify '%s' is a one-shot notify (no duration). Only notify_state events have a duration."),
			*Event->NotifyName.ToString());
		return;
	}

	const float OldDuration = Event->GetDuration();
	Event->SetDuration(NewDuration);
	RefreshStateNotifyEndLink(Seq, *Event);

	Seq->RefreshCacheData();
	Seq->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(AssetPath, false);

	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("notify_name"), Event->NotifyName.ToString());
	Obj->SetNumberField(TEXT("time"), Event->GetTime());
	Obj->SetNumberField(TEXT("old_duration"), OldDuration);
	Obj->SetNumberField(TEXT("duration"), Event->GetDuration());
	BuildSuccessJson(Obj, OutJson);
}

void HandleMoveMontageSection(const FString& MontagePath, const FString& SectionName,
	float NewStartTime, FString& OutJson, FString& OutError)
{
	if (NewStartTime < 0.0f) { OutError = TEXT("new_start_time must be >= 0"); return; }

	UAnimMontage* Montage = LoadObject<UAnimMontage>(nullptr, *MontagePath);
	if (!Montage) { OutError = FString::Printf(TEXT("AnimMontage not found at '%s'"), *MontagePath); return; }

	FCompositeSection* Section = nullptr;
	float OldStart = 0.0f;
	for (FCompositeSection& S : Montage->CompositeSections)
	{
		if (S.SectionName.ToString().Equals(SectionName, ESearchCase::IgnoreCase))
		{
			Section = &S;
			OldStart = S.GetTime();
			break;
		}
	}
	if (!Section)
	{
		OutError = FString::Printf(TEXT("Section '%s' not found in montage '%s'"),
			*SectionName, *Montage->GetName());
		return;
	}

	Section->SetTime(NewStartTime);
	Montage->UpdateLinkableElements();
	Montage->RefreshCacheData();
	Montage->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(MontagePath, false);

	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("section_name"), SectionName);
	Obj->SetNumberField(TEXT("old_start_time"), OldStart);
	Obj->SetNumberField(TEXT("start_time"), Section->GetTime());
	BuildSuccessJson(Obj, OutJson);
}

void HandleMoveAnimNotifyFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString AssetPath, NotifyName, NewTrackName;
	double TimePosition = 0.0, NewTime = -1.0;
	int32 NewTrackIndex = -1;
	Args->TryGetStringField(TEXT("asset_path"),       AssetPath);
	Args->TryGetStringField(TEXT("notify_name"),      NotifyName);
	Args->TryGetNumberField(TEXT("time_position"),    TimePosition);
	Args->TryGetNumberField(TEXT("new_time"),         NewTime);
	Args->TryGetNumberField(TEXT("new_track_index"),  NewTrackIndex);
	Args->TryGetStringField(TEXT("new_track_name"),   NewTrackName);
	HandleMoveAnimNotify(AssetPath, NotifyName, (float)TimePosition,
		(float)NewTime, NewTrackIndex, NewTrackName, OutJsonString, OutError);
}

void HandleSetAnimNotifyDurationFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString AssetPath, NotifyName;
	double TimePosition = 0.0, NewDuration = 0.0;
	Args->TryGetStringField(TEXT("asset_path"),    AssetPath);
	Args->TryGetStringField(TEXT("notify_name"),   NotifyName);
	Args->TryGetNumberField(TEXT("time_position"), TimePosition);
	Args->TryGetNumberField(TEXT("new_duration"),  NewDuration);
	HandleSetAnimNotifyDuration(AssetPath, NotifyName, (float)TimePosition,
		(float)NewDuration, OutJsonString, OutError);
}

void HandleMoveMontageSectionFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString MontagePath, SectionName;
	double NewStartTime = 0.0;
	Args->TryGetStringField(TEXT("montage_path"),   MontagePath);
	Args->TryGetStringField(TEXT("section_name"),   SectionName);
	Args->TryGetNumberField(TEXT("new_start_time"), NewStartTime);
	HandleMoveMontageSection(MontagePath, SectionName, (float)NewStartTime,
		OutJsonString, OutError);
}

namespace
{
	FAnimSyncMarker* FindSyncMarkerForEdit(UAnimSequence* AnimSeq, const FString& MarkerName,
		float TimePosition, FString& OutError)
	{
		if (MarkerName.IsEmpty()) { OutError = TEXT("marker_name is required"); return nullptr; }
		const FName Target(*MarkerName);
		FAnimSyncMarker* Best = nullptr;
		float BestDist = MAX_FLT;
		for (FAnimSyncMarker& M : AnimSeq->AuthoredSyncMarkers)
		{
			if (M.MarkerName != Target) continue;
			const float Dist = FMath::Abs(M.Time - TimePosition);
			if (Dist < BestDist) { BestDist = Dist; Best = &M; }
		}
		if (!Best)
			OutError = FString::Printf(TEXT("No sync marker named '%s' on '%s'"),
				*MarkerName, *AnimSeq->GetName());
		return Best;
	}
}

void HandleCropAnimation(const FString& AssetPath, float NewStartTime, float NewEndTime,
	FString& OutJson, FString& OutError)
{
	UAnimSequence* AnimSeq = Cast<UAnimSequence>(UEditorAssetLibrary::LoadAsset(AssetPath));
	if (!AnimSeq) { OutError = FString::Printf(TEXT("Could not load AnimSequence: %s"), *AssetPath); return; }

	const float OriginalLength = AnimSeq->GetPlayLength();
	if (NewStartTime < 0.0f || NewEndTime > OriginalLength + KINDA_SMALL_NUMBER || NewEndTime <= NewStartTime)
	{
		OutError = FString::Printf(
			TEXT("Invalid crop range [%.4f, %.4f] for sequence of length %.4f. Need 0 <= start < end <= length."),
			NewStartTime, NewEndTime, OriginalLength);
		return;
	}

	const FFrameRate FrameRate = AnimSeq->GetSamplingFrameRate();
	IAnimationDataController& Controller = AnimSeq->GetController();
	Controller.OpenBracket(FText::FromString(TEXT("Crop Animation")));
	if (OriginalLength - NewEndTime > KINDA_SMALL_NUMBER)
	{
		const FFrameNumber TailNewLen = FrameRate.AsFrameNumber(NewEndTime);
		const FFrameNumber TailT0     = FrameRate.AsFrameNumber(NewEndTime);
		const FFrameNumber TailT1     = FrameRate.AsFrameNumber(OriginalLength);
		Controller.ResizeInFrames(TailNewLen, TailT0, TailT1);
	}
	if (NewStartTime > KINDA_SMALL_NUMBER)
	{
		const float HeadNewLenSec = AnimSeq->GetPlayLength() - NewStartTime;
		const FFrameNumber HeadNewLen = FrameRate.AsFrameNumber(HeadNewLenSec);
		const FFrameNumber HeadT0     = FFrameNumber(0);
		const FFrameNumber HeadT1     = FrameRate.AsFrameNumber(NewStartTime);
		Controller.ResizeInFrames(HeadNewLen, HeadT0, HeadT1);
	}
	Controller.CloseBracket();

	AnimSeq->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(AssetPath, false);

	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("asset_path"), AssetPath);
	Obj->SetNumberField(TEXT("original_length"), OriginalLength);
	Obj->SetNumberField(TEXT("new_length"), AnimSeq->GetPlayLength());
	Obj->SetNumberField(TEXT("kept_range_start"), NewStartTime);
	Obj->SetNumberField(TEXT("kept_range_end"), NewEndTime);
	BuildSuccessJson(Obj, OutJson);
}

void HandleMoveAnimSyncMarker(const FString& AssetPath, const FString& MarkerName, float TimePosition,
	float NewTime, FString& OutJson, FString& OutError)
{
	UAnimSequence* AnimSeq = Cast<UAnimSequence>(UEditorAssetLibrary::LoadAsset(AssetPath));
	if (!AnimSeq) { OutError = FString::Printf(TEXT("Could not load AnimSequence: %s"), *AssetPath); return; }

	const float SeqLen = AnimSeq->GetPlayLength();
	if (NewTime < 0.f || NewTime > SeqLen)
	{
		OutError = FString::Printf(TEXT("new_time %.3f is outside sequence range [0, %.3f]"), NewTime, SeqLen);
		return;
	}

	FAnimSyncMarker* Marker = FindSyncMarkerForEdit(AnimSeq, MarkerName, TimePosition, OutError);
	if (!Marker) return;

	const float OldTime = Marker->Time;
	const FName MarkerFName = Marker->MarkerName;
	Marker->Time = NewTime;

	AnimSeq->SortSyncMarkers();
	AnimSeq->RefreshSyncMarkerDataFromAuthored();
	AnimSeq->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(AssetPath, false);

	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("marker_name"), MarkerFName.ToString());
	Obj->SetNumberField(TEXT("old_time"), OldTime);
	Obj->SetNumberField(TEXT("time"), NewTime);
	BuildSuccessJson(Obj, OutJson);
}

void HandleRenameAnimSyncMarker(const FString& AssetPath, const FString& OldName, const FString& NewName,
	float TimePosition, FString& OutJson, FString& OutError)
{
	if (NewName.IsEmpty()) { OutError = TEXT("new_name is required"); return; }

	UAnimSequence* AnimSeq = Cast<UAnimSequence>(UEditorAssetLibrary::LoadAsset(AssetPath));
	if (!AnimSeq) { OutError = FString::Printf(TEXT("Could not load AnimSequence: %s"), *AssetPath); return; }

	FAnimSyncMarker* Marker = FindSyncMarkerForEdit(AnimSeq, OldName, TimePosition, OutError);
	if (!Marker) return;

	const float MarkerTime = Marker->Time;
	Marker->MarkerName = FName(*NewName);

	AnimSeq->RefreshSyncMarkerDataFromAuthored();
	AnimSeq->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(AssetPath, false);

	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("old_name"), OldName);
	Obj->SetStringField(TEXT("new_name"), NewName);
	Obj->SetNumberField(TEXT("time"), MarkerTime);
	BuildSuccessJson(Obj, OutJson);
}

void HandleListAnimCurves(const FString& AssetPath, FString& OutJson, FString& OutError)
{
	UAnimSequenceBase* AnimSeq = Cast<UAnimSequenceBase>(UEditorAssetLibrary::LoadAsset(AssetPath));
	if (!AnimSeq) { OutError = FString::Printf(TEXT("Could not load AnimSequenceBase: %s"), *AssetPath); return; }

	const IAnimationDataModel* DataModel = AnimSeq->GetDataModel();
	if (!DataModel) { OutError = TEXT("Anim sequence has no data model — asset may be corrupted"); return; }

	auto CurveEntry = [](const FName& Name, int32 NumKeys, const TCHAR* Type)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("name"), Name.ToString());
		Obj->SetStringField(TEXT("type"), Type);
		Obj->SetNumberField(TEXT("num_keys"), NumKeys);
		return MakeShared<FJsonValueObject>(Obj);
	};

	TArray<TSharedPtr<FJsonValue>> Curves;
	for (const FFloatCurve& Curve : DataModel->GetFloatCurves())
	{
		Curves.Add(CurveEntry(Curve.GetName(), Curve.FloatCurve.GetNumKeys(), TEXT("float")));
	}
	for (const FTransformCurve& Curve : DataModel->GetTransformCurves())
	{
		const int32 NumKeys = Curve.TranslationCurve.FloatCurves[0].GetNumKeys();
		Curves.Add(CurveEntry(Curve.GetName(), NumKeys, TEXT("transform")));
	}

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("success"), true);
	Root->SetStringField(TEXT("asset_path"), AssetPath);
	Root->SetNumberField(TEXT("count"), Curves.Num());
	Root->SetArrayField(TEXT("curves"), Curves);
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJson);
	FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);
}

void HandleRemoveAnimCurveKey(const FString& AssetPath, const FString& CurveName, float KeyTime,
	const FString& CurveType, FString& OutJson, FString& OutError)
{
	UAnimSequenceBase* AnimSeq = Cast<UAnimSequenceBase>(UEditorAssetLibrary::LoadAsset(AssetPath));
	if (!AnimSeq) { OutError = FString::Printf(TEXT("Could not load AnimSequenceBase: %s"), *AssetPath); return; }

	const ERawCurveTrackTypes ParsedType = ParseCurveTypeString(CurveType);
	if (ParsedType != ERawCurveTrackTypes::RCT_Float)
	{
		OutError = TEXT("remove_anim_curve_key currently supports Float curves only.");
		return;
	}

	FAnimationCurveIdentifier CurveId(FName(*CurveName), ParsedType);
	IAnimationDataController& Controller = AnimSeq->GetController();
	Controller.OpenBracket(FText::FromString(TEXT("Remove Anim Curve Key")));
	const bool bRemoved = Controller.RemoveCurveKey(CurveId, KeyTime);
	Controller.CloseBracket();

	if (!bRemoved)
	{
		OutError = FString::Printf(
			TEXT("No key found at time %.4f on curve '%s'. Existing keys may not match exactly — try list_anim_curves to confirm."),
			KeyTime, *CurveName);
		return;
	}

	AnimSeq->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(AssetPath, false);

	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("curve_name"), CurveName);
	Obj->SetNumberField(TEXT("key_time"), KeyTime);
	BuildSuccessJson(Obj, OutJson);
}

void HandleCropAnimationFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString AssetPath;
	double NewStart = 0.0, NewEnd = 0.0;
	Args->TryGetStringField(TEXT("asset_path"),     AssetPath);
	Args->TryGetNumberField(TEXT("new_start_time"), NewStart);
	Args->TryGetNumberField(TEXT("new_end_time"),   NewEnd);
	HandleCropAnimation(AssetPath, (float)NewStart, (float)NewEnd, OutJsonString, OutError);
}

void HandleMoveAnimSyncMarkerFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString AssetPath, MarkerName;
	double TimePosition = 0.0, NewTime = 0.0;
	Args->TryGetStringField(TEXT("asset_path"),    AssetPath);
	Args->TryGetStringField(TEXT("marker_name"),   MarkerName);
	Args->TryGetNumberField(TEXT("time_position"), TimePosition);
	Args->TryGetNumberField(TEXT("new_time"),      NewTime);
	HandleMoveAnimSyncMarker(AssetPath, MarkerName, (float)TimePosition, (float)NewTime,
		OutJsonString, OutError);
}

void HandleRenameAnimSyncMarkerFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString AssetPath, OldName, NewName;
	double TimePosition = 0.0;
	Args->TryGetStringField(TEXT("asset_path"),    AssetPath);
	Args->TryGetStringField(TEXT("old_name"),      OldName);
	Args->TryGetStringField(TEXT("new_name"),      NewName);
	Args->TryGetNumberField(TEXT("time_position"), TimePosition);
	HandleRenameAnimSyncMarker(AssetPath, OldName, NewName, (float)TimePosition,
		OutJsonString, OutError);
}

void HandleListAnimCurvesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString AssetPath;
	Args->TryGetStringField(TEXT("asset_path"), AssetPath);
	HandleListAnimCurves(AssetPath, OutJsonString, OutError);
}

void HandleRemoveAnimCurveKeyFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString AssetPath, CurveName, CurveType;
	double KeyTime = 0.0;
	Args->TryGetStringField(TEXT("asset_path"),   AssetPath);
	Args->TryGetStringField(TEXT("curve_name"),   CurveName);
	Args->TryGetNumberField(TEXT("key_time"),     KeyTime);
	Args->TryGetStringField(TEXT("curve_type"),   CurveType);
	HandleRemoveAnimCurveKey(AssetPath, CurveName, (float)KeyTime, CurveType, OutJsonString, OutError);
}

namespace
{
	USkeletalMeshSocket* FindSkeletonSocketByName(USkeleton* Skeleton, FName Name)
	{
		if (!Skeleton) return nullptr;
		for (const TObjectPtr<USkeletalMeshSocket>& Slot : Skeleton->Sockets)
		{
			if (Slot && Slot->SocketName == Name) return Slot.Get();
		}
		return nullptr;
	}

	bool ParseVecStringIntoFVector(const FString& Str, FVector& Out)
	{
		TArray<FString> Parts;
		Str.ParseIntoArray(Parts, TEXT(","), true);
		if (Parts.Num() != 3) return false;
		Out.X = FCString::Atof(*Parts[0]);
		Out.Y = FCString::Atof(*Parts[1]);
		Out.Z = FCString::Atof(*Parts[2]);
		return true;
	}
	bool ParseRotStringIntoFRotator(const FString& Str, FRotator& Out)
	{
		TArray<FString> Parts;
		Str.ParseIntoArray(Parts, TEXT(","), true);
		if (Parts.Num() != 3) return false;
		Out.Pitch = FCString::Atof(*Parts[0]);
		Out.Yaw   = FCString::Atof(*Parts[1]);
		Out.Roll  = FCString::Atof(*Parts[2]);
		return true;
	}
}

void HandleRemoveSkeletonSocket(const FString& SkeletonPath, const FString& SocketName,
	FString& OutJson, FString& OutError)
{
	USkeleton* Skeleton = Cast<USkeleton>(UEditorAssetLibrary::LoadAsset(SkeletonPath));
	if (!Skeleton) { OutError = FString::Printf(TEXT("Could not load Skeleton: %s"), *SkeletonPath); return; }

	const FName Target(*SocketName);
	const int32 RemovedCount = Skeleton->Sockets.RemoveAll([Target](const TObjectPtr<USkeletalMeshSocket>& S)
	{
		return S && S->SocketName == Target;
	});

	if (RemovedCount == 0)
	{
		OutError = FString::Printf(TEXT("Socket '%s' not found on skeleton '%s'"), *SocketName, *Skeleton->GetName());
		return;
	}

	Skeleton->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(SkeletonPath, false);

	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("removed_socket"), SocketName);
	BuildSuccessJson(Obj, OutJson);
}

void HandleRenameSkeletonSocket(const FString& SkeletonPath, const FString& OldName, const FString& NewName,
	FString& OutJson, FString& OutError)
{
	if (NewName.IsEmpty()) { OutError = TEXT("new_name is required"); return; }

	USkeleton* Skeleton = Cast<USkeleton>(UEditorAssetLibrary::LoadAsset(SkeletonPath));
	if (!Skeleton) { OutError = FString::Printf(TEXT("Could not load Skeleton: %s"), *SkeletonPath); return; }

	const FName NewTarget(*NewName);
	if (FindSkeletonSocketByName(Skeleton, NewTarget))
	{
		OutError = FString::Printf(TEXT("Socket name '%s' is already in use on this skeleton"), *NewName);
		return;
	}

	USkeletalMeshSocket* Socket = FindSkeletonSocketByName(Skeleton, FName(*OldName));
	if (!Socket)
	{
		OutError = FString::Printf(TEXT("Socket '%s' not found on skeleton '%s'"), *OldName, *Skeleton->GetName());
		return;
	}

	Socket->Modify();
	Socket->SocketName = NewTarget;
	Skeleton->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(SkeletonPath, false);

	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("old_name"), OldName);
	Obj->SetStringField(TEXT("new_name"), NewName);
	BuildSuccessJson(Obj, OutJson);
}

void HandleSetSkeletonSocketTransform(const FString& SkeletonPath, const FString& SocketName,
	const FString& RelLocation, const FString& RelRotation, const FString& RelScale,
	FString& OutJson, FString& OutError)
{
	if (RelLocation.IsEmpty() && RelRotation.IsEmpty() && RelScale.IsEmpty())
	{
		OutError = TEXT("Specify at least one of relative_location / relative_rotation / relative_scale");
		return;
	}

	USkeleton* Skeleton = Cast<USkeleton>(UEditorAssetLibrary::LoadAsset(SkeletonPath));
	if (!Skeleton) { OutError = FString::Printf(TEXT("Could not load Skeleton: %s"), *SkeletonPath); return; }

	USkeletalMeshSocket* Socket = FindSkeletonSocketByName(Skeleton, FName(*SocketName));
	if (!Socket)
	{
		OutError = FString::Printf(TEXT("Socket '%s' not found on skeleton '%s'"), *SocketName, *Skeleton->GetName());
		return;
	}

	Socket->Modify();
	if (!RelLocation.IsEmpty())
	{
		FVector Loc = Socket->RelativeLocation;
		if (!ParseVecStringIntoFVector(RelLocation, Loc))
		{
			OutError = FString::Printf(TEXT("relative_location must be 'x,y,z' (got '%s')"), *RelLocation);
			return;
		}
		Socket->RelativeLocation = Loc;
	}
	if (!RelRotation.IsEmpty())
	{
		FRotator Rot = Socket->RelativeRotation;
		if (!ParseRotStringIntoFRotator(RelRotation, Rot))
		{
			OutError = FString::Printf(TEXT("relative_rotation must be 'pitch,yaw,roll' (got '%s')"), *RelRotation);
			return;
		}
		Socket->RelativeRotation = Rot;
	}
	if (!RelScale.IsEmpty())
	{
		FVector Scale = Socket->RelativeScale;
		if (!ParseVecStringIntoFVector(RelScale, Scale))
		{
			OutError = FString::Printf(TEXT("relative_scale must be 'x,y,z' (got '%s')"), *RelScale);
			return;
		}
		Socket->RelativeScale = Scale;
	}

	Skeleton->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(SkeletonPath, false);

	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("socket_name"), SocketName);
	Obj->SetStringField(TEXT("relative_location"),
		FString::Printf(TEXT("%g,%g,%g"), Socket->RelativeLocation.X, Socket->RelativeLocation.Y, Socket->RelativeLocation.Z));
	Obj->SetStringField(TEXT("relative_rotation"),
		FString::Printf(TEXT("%g,%g,%g"), Socket->RelativeRotation.Pitch, Socket->RelativeRotation.Yaw, Socket->RelativeRotation.Roll));
	Obj->SetStringField(TEXT("relative_scale"),
		FString::Printf(TEXT("%g,%g,%g"), Socket->RelativeScale.X, Socket->RelativeScale.Y, Socket->RelativeScale.Z));
	BuildSuccessJson(Obj, OutJson);
}

void HandleSetSkeletonSocketParent(const FString& SkeletonPath, const FString& SocketName,
	const FString& NewBoneName, FString& OutJson, FString& OutError)
{
	if (NewBoneName.IsEmpty()) { OutError = TEXT("new_bone_name is required"); return; }

	USkeleton* Skeleton = Cast<USkeleton>(UEditorAssetLibrary::LoadAsset(SkeletonPath));
	if (!Skeleton) { OutError = FString::Printf(TEXT("Could not load Skeleton: %s"), *SkeletonPath); return; }

	const FReferenceSkeleton& RefSkel = Skeleton->GetReferenceSkeleton();
	const int32 BoneIndex = RefSkel.FindBoneIndex(FName(*NewBoneName));
	if (BoneIndex == INDEX_NONE)
	{
		OutError = FString::Printf(TEXT("Bone '%s' not found on skeleton '%s'"), *NewBoneName, *Skeleton->GetName());
		return;
	}

	USkeletalMeshSocket* Socket = FindSkeletonSocketByName(Skeleton, FName(*SocketName));
	if (!Socket)
	{
		OutError = FString::Printf(TEXT("Socket '%s' not found on skeleton '%s'"), *SocketName, *Skeleton->GetName());
		return;
	}

	const FName OldBone = Socket->BoneName;
	Socket->Modify();
	Socket->BoneName = FName(*NewBoneName);
	Skeleton->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(SkeletonPath, false);

	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("socket_name"), SocketName);
	Obj->SetStringField(TEXT("old_bone"), OldBone.ToString());
	Obj->SetStringField(TEXT("new_bone"), NewBoneName);
	BuildSuccessJson(Obj, OutJson);
}

void HandleRemoveSkeletonSocketFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SkelPath, SocketName;
	Args->TryGetStringField(TEXT("skeleton_path"), SkelPath);
	Args->TryGetStringField(TEXT("socket_name"),   SocketName);
	HandleRemoveSkeletonSocket(SkelPath, SocketName, OutJsonString, OutError);
}

void HandleRenameSkeletonSocketFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SkelPath, OldName, NewName;
	Args->TryGetStringField(TEXT("skeleton_path"), SkelPath);
	Args->TryGetStringField(TEXT("old_name"),      OldName);
	Args->TryGetStringField(TEXT("new_name"),      NewName);
	HandleRenameSkeletonSocket(SkelPath, OldName, NewName, OutJsonString, OutError);
}

void HandleSetSkeletonSocketTransformFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SkelPath, SocketName, RL, RR, RS;
	Args->TryGetStringField(TEXT("skeleton_path"),     SkelPath);
	Args->TryGetStringField(TEXT("socket_name"),       SocketName);
	Args->TryGetStringField(TEXT("relative_location"), RL);
	Args->TryGetStringField(TEXT("relative_rotation"), RR);
	Args->TryGetStringField(TEXT("relative_scale"),    RS);
	HandleSetSkeletonSocketTransform(SkelPath, SocketName, RL, RR, RS, OutJsonString, OutError);
}

void HandleSetSkeletonSocketParentFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SkelPath, SocketName, NewBone;
	Args->TryGetStringField(TEXT("skeleton_path"), SkelPath);
	Args->TryGetStringField(TEXT("socket_name"),   SocketName);
	Args->TryGetStringField(TEXT("new_bone_name"), NewBone);
	HandleSetSkeletonSocketParent(SkelPath, SocketName, NewBone, OutJsonString, OutError);
}

namespace
{
	struct FBlendspaceSampleAccessor : public UBlendSpace
	{
		static TArray<FBlendSample>& GetMutableSampleData(UBlendSpace* InBS)
		{
			return static_cast<FBlendspaceSampleAccessor*>(InBS)->SampleData;
		}
	};

	int32 FindNearestBlendspaceSample(const TArray<FBlendSample>& Samples,
		float QueryX, float QueryY, float QueryZ)
	{
		int32 Best = INDEX_NONE;
		float BestDist = MAX_FLT;
		for (int32 i = 0; i < Samples.Num(); ++i)
		{
			const FVector& SV = Samples[i].SampleValue;
			const float Dist =
				FMath::Abs(SV.X - QueryX) +
				FMath::Abs(SV.Y - QueryY) +
				FMath::Abs(SV.Z - QueryZ);
			if (Dist < BestDist) { BestDist = Dist; Best = i; }
		}
		return Best;
	}
}

void HandleMoveBlendspaceSample(const FString& BlendspacePath,
	float SampleX, float SampleY, float SampleZ,
	float NewX, float NewY, float NewZ,
	FString& OutJson, FString& OutError)
{
	UBlendSpace* BS = Cast<UBlendSpace>(UEditorAssetLibrary::LoadAsset(BlendspacePath));
	if (!BS) { OutError = FString::Printf(TEXT("Could not load BlendSpace: %s"), *BlendspacePath); return; }

	TArray<FBlendSample>& Samples = FBlendspaceSampleAccessor::GetMutableSampleData(BS);
	const int32 Idx = FindNearestBlendspaceSample(Samples, SampleX, SampleY, SampleZ);
	if (Idx == INDEX_NONE) { OutError = TEXT("BlendSpace has no samples to move"); return; }

	const FVector OldVal = Samples[Idx].SampleValue;
	Samples[Idx].SampleValue = FVector(NewX, NewY, NewZ);

	BS->ValidateSampleData();
	BS->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(BlendspacePath, false);

	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetNumberField(TEXT("old_sample_x"), OldVal.X);
	Obj->SetNumberField(TEXT("old_sample_y"), OldVal.Y);
	Obj->SetNumberField(TEXT("old_sample_z"), OldVal.Z);
	Obj->SetNumberField(TEXT("sample_x"), NewX);
	Obj->SetNumberField(TEXT("sample_y"), NewY);
	Obj->SetNumberField(TEXT("sample_z"), NewZ);
	BuildSuccessJson(Obj, OutJson);
}

void HandleSetBlendspaceSampleAnimation(const FString& BlendspacePath,
	float SampleX, float SampleY, float SampleZ,
	const FString& NewAnimationPath,
	FString& OutJson, FString& OutError)
{
	if (NewAnimationPath.IsEmpty()) { OutError = TEXT("new_animation_path is required"); return; }

	UBlendSpace* BS = Cast<UBlendSpace>(UEditorAssetLibrary::LoadAsset(BlendspacePath));
	if (!BS) { OutError = FString::Printf(TEXT("Could not load BlendSpace: %s"), *BlendspacePath); return; }

	UAnimSequence* NewAnim = Cast<UAnimSequence>(UEditorAssetLibrary::LoadAsset(NewAnimationPath));
	if (!NewAnim)
	{
		OutError = FString::Printf(
			TEXT("'%s' is not a UAnimSequence — BlendSpace samples only accept AnimSequence assets, not montages or composites."),
			*NewAnimationPath);
		return;
	}

	if (BS->GetSkeleton() && NewAnim->GetSkeleton() && BS->GetSkeleton() != NewAnim->GetSkeleton())
	{
		OutError = FString::Printf(
			TEXT("Skeleton mismatch: BlendSpace targets '%s', animation targets '%s'."),
			*BS->GetSkeleton()->GetPathName(), *NewAnim->GetSkeleton()->GetPathName());
		return;
	}

	TArray<FBlendSample>& Samples = FBlendspaceSampleAccessor::GetMutableSampleData(BS);
	const int32 Idx = FindNearestBlendspaceSample(Samples, SampleX, SampleY, SampleZ);
	if (Idx == INDEX_NONE) { OutError = TEXT("BlendSpace has no samples to retarget"); return; }

	const FString OldAnimPath = Samples[Idx].Animation ? Samples[Idx].Animation->GetPathName() : FString();
	Samples[Idx].Animation = NewAnim;

	BS->ValidateSampleData();
	BS->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(BlendspacePath, false);

	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetNumberField(TEXT("sample_x"), Samples[Idx].SampleValue.X);
	Obj->SetNumberField(TEXT("sample_y"), Samples[Idx].SampleValue.Y);
	Obj->SetNumberField(TEXT("sample_z"), Samples[Idx].SampleValue.Z);
	Obj->SetStringField(TEXT("old_animation"), OldAnimPath);
	Obj->SetStringField(TEXT("animation"), NewAnimationPath);
	BuildSuccessJson(Obj, OutJson);
}

void HandleSetBlendspaceSampleRateScale(const FString& BlendspacePath,
	float SampleX, float SampleY, float SampleZ,
	float RateScale,
	FString& OutJson, FString& OutError)
{
	if (RateScale <= 0.f)
	{
		OutError = TEXT("rate_scale must be > 0 (1.0 = normal speed; 2.0 = double speed; 0.5 = half speed)");
		return;
	}

	UBlendSpace* BS = Cast<UBlendSpace>(UEditorAssetLibrary::LoadAsset(BlendspacePath));
	if (!BS) { OutError = FString::Printf(TEXT("Could not load BlendSpace: %s"), *BlendspacePath); return; }

	TArray<FBlendSample>& Samples = FBlendspaceSampleAccessor::GetMutableSampleData(BS);
	const int32 Idx = FindNearestBlendspaceSample(Samples, SampleX, SampleY, SampleZ);
	if (Idx == INDEX_NONE) { OutError = TEXT("BlendSpace has no samples"); return; }

	const float OldRate = Samples[Idx].RateScale;
	Samples[Idx].RateScale = RateScale;

	BS->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(BlendspacePath, false);

	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetNumberField(TEXT("sample_x"), Samples[Idx].SampleValue.X);
	Obj->SetNumberField(TEXT("sample_y"), Samples[Idx].SampleValue.Y);
	Obj->SetNumberField(TEXT("sample_z"), Samples[Idx].SampleValue.Z);
	Obj->SetNumberField(TEXT("old_rate_scale"), OldRate);
	Obj->SetNumberField(TEXT("rate_scale"), RateScale);
	BuildSuccessJson(Obj, OutJson);
}

void HandleMoveBlendspaceSampleFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString BlendspacePath;
	double SX = 0, SY = 0, SZ = 0, NX = 0, NY = 0, NZ = 0;
	Args->TryGetStringField(TEXT("blendspace_path"), BlendspacePath);
	Args->TryGetNumberField(TEXT("sample_x"),        SX);
	Args->TryGetNumberField(TEXT("sample_y"),        SY);
	Args->TryGetNumberField(TEXT("sample_z"),        SZ);
	Args->TryGetNumberField(TEXT("new_sample_x"),    NX);
	Args->TryGetNumberField(TEXT("new_sample_y"),    NY);
	Args->TryGetNumberField(TEXT("new_sample_z"),    NZ);
	HandleMoveBlendspaceSample(BlendspacePath, (float)SX, (float)SY, (float)SZ,
		(float)NX, (float)NY, (float)NZ, OutJsonString, OutError);
}

void HandleSetBlendspaceSampleAnimationFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString BlendspacePath, NewAnim;
	double SX = 0, SY = 0, SZ = 0;
	Args->TryGetStringField(TEXT("blendspace_path"),     BlendspacePath);
	Args->TryGetNumberField(TEXT("sample_x"),            SX);
	Args->TryGetNumberField(TEXT("sample_y"),            SY);
	Args->TryGetNumberField(TEXT("sample_z"),            SZ);
	Args->TryGetStringField(TEXT("new_animation_path"),  NewAnim);
	HandleSetBlendspaceSampleAnimation(BlendspacePath, (float)SX, (float)SY, (float)SZ,
		NewAnim, OutJsonString, OutError);
}

void HandleSetBlendspaceSampleRateScaleFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString BlendspacePath;
	double SX = 0, SY = 0, SZ = 0, Rate = 1.0;
	Args->TryGetStringField(TEXT("blendspace_path"), BlendspacePath);
	Args->TryGetNumberField(TEXT("sample_x"),        SX);
	Args->TryGetNumberField(TEXT("sample_y"),        SY);
	Args->TryGetNumberField(TEXT("sample_z"),        SZ);
	Args->TryGetNumberField(TEXT("rate_scale"),      Rate);
	HandleSetBlendspaceSampleRateScale(BlendspacePath, (float)SX, (float)SY, (float)SZ,
		(float)Rate, OutJsonString, OutError);
}

namespace
{
	UAnimStateNodeBase* FindStateNodeByName(UAnimationStateMachineGraph* SMGraph, const FString& StateName)
	{
		if (!SMGraph || StateName.IsEmpty()) return nullptr;
		for (UEdGraphNode* Node : SMGraph->Nodes)
		{
			if (UAnimStateNodeBase* SN = Cast<UAnimStateNodeBase>(Node))
			{
				if (SN->GetStateName().Equals(StateName, ESearchCase::IgnoreCase))
					return SN;
			}
		}
		return nullptr;
	}

	// Transitions are directional; the engine's Bidirectional flag is not honoured by the
	// compiler so no reverse lookup is attempted.
	UAnimStateTransitionNode* FindTransitionBetween(UAnimationStateMachineGraph* SMGraph,
		const FString& FromState, const FString& ToState)
	{
		for (UEdGraphNode* Node : SMGraph->Nodes)
		{
			UAnimStateTransitionNode* T = Cast<UAnimStateTransitionNode>(Node);
			if (!T) continue;
			UAnimStateNodeBase* Prev = T->GetPreviousState();
			UAnimStateNodeBase* Next = T->GetNextState();
			if (Prev && Next && Prev->GetStateName() == FromState && Next->GetStateName() == ToState)
				return T;
		}
		return nullptr;
	}
}

void HandleSetAnimStateAnimation(const FString& AnimBlueprintPath, const FString& StateMachineName,
	const FString& StateName, const FString& AnimationPath,
	FString& OutJson, FString& OutError)
{
	if (AnimationPath.IsEmpty()) { OutError = TEXT("animation_path is required"); return; }

	UAnimBlueprint* AnimBP = Cast<UAnimBlueprint>(UEditorAssetLibrary::LoadAsset(AnimBlueprintPath));
	if (!AnimBP) { OutError = FString::Printf(TEXT("Could not load AnimBlueprint: %s"), *AnimBlueprintPath); return; }

	FString SMLookupError;
	UAnimationStateMachineGraph* SMGraph = FindStateMachineGraph(AnimBP, StateMachineName, &SMLookupError);
	if (!SMGraph) { OutError = SMLookupError; return; }

	UAnimStateNode* StateNode = Cast<UAnimStateNode>(FindStateNodeByName(SMGraph, StateName));
	if (!StateNode || !StateNode->BoundGraph)
	{
		OutError = FString::Printf(TEXT("State '%s' not found (or has no BoundGraph) in '%s'"),
			*StateName, *StateMachineName);
		return;
	}

	UAnimationAsset* NewAsset = Cast<UAnimationAsset>(UEditorAssetLibrary::LoadAsset(AnimationPath));
	if (!NewAsset)
	{
		OutError = FString::Printf(TEXT("Could not load AnimationAsset: %s"), *AnimationPath);
		return;
	}

	if (AnimBP->TargetSkeleton && NewAsset->GetSkeleton() && AnimBP->TargetSkeleton != NewAsset->GetSkeleton())
	{
		OutError = FString::Printf(
			TEXT("Skeleton mismatch: AnimBP targets '%s', animation targets '%s'."),
			*AnimBP->TargetSkeleton->GetPathName(), *NewAsset->GetSkeleton()->GetPathName());
		return;
	}

	UAnimGraphNode_AssetPlayerBase* PlayerNode = nullptr;
	for (UEdGraphNode* Node : StateNode->BoundGraph->Nodes)
	{
		if (UAnimGraphNode_AssetPlayerBase* P = Cast<UAnimGraphNode_AssetPlayerBase>(Node))
		{
			PlayerNode = P;
			break;
		}
	}
	if (!PlayerNode)
	{
		OutError = FString::Printf(
			TEXT("State '%s' has no UAnimGraphNode_AssetPlayerBase to retarget. "
			     "States authored without an asset player (pure pass-through, etc.) need set_anim_node_property by GUID."),
			*StateName);
		return;
	}

	const FString OldAsset = PlayerNode->GetAnimationAsset()
		? PlayerNode->GetAnimationAsset()->GetPathName() : FString();
	PlayerNode->Modify();
	PlayerNode->SetAnimationAsset(NewAsset);
	PlayerNode->ReconstructNode();

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBP);
	StateNode->BoundGraph->NotifyGraphChanged();

	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("state_name"), StateName);
	Obj->SetStringField(TEXT("player_class"), PlayerNode->GetClass()->GetName());
	Obj->SetStringField(TEXT("player_node_guid"), PlayerNode->NodeGuid.ToString());
	Obj->SetStringField(TEXT("old_animation"), OldAsset);
	Obj->SetStringField(TEXT("animation"), AnimationPath);
	CompileAnimBlueprintAndReport(AnimBP, Obj);
	UEditorAssetLibrary::SaveAsset(AnimBlueprintPath, false);
	BuildSuccessJson(Obj, OutJson);
}

void HandleAddStateAlias(const FString& AnimBlueprintPath, const FString& StateMachineName,
	const FString& AliasName, const TArray<FString>& SourceStates, bool bGlobalAlias,
	int32 PosX, int32 PosY,
	FString& OutJson, FString& OutError)
{
	if (AliasName.IsEmpty()) { OutError = TEXT("alias_name is required"); return; }
	if (!bGlobalAlias && SourceStates.Num() == 0)
	{
		OutError = TEXT("Either source_states must be non-empty OR global_alias must be true");
		return;
	}

	UAnimBlueprint* AnimBP = Cast<UAnimBlueprint>(UEditorAssetLibrary::LoadAsset(AnimBlueprintPath));
	if (!AnimBP) { OutError = FString::Printf(TEXT("Could not load AnimBlueprint: %s"), *AnimBlueprintPath); return; }

	FString SMLookupError;
	UAnimationStateMachineGraph* SMGraph = FindStateMachineGraph(AnimBP, StateMachineName, &SMLookupError);
	if (!SMGraph) { OutError = SMLookupError; return; }

	TArray<UAnimStateNodeBase*> Resolved;
	TArray<FString> Missing;
	for (const FString& S : SourceStates)
	{
		if (UAnimStateNodeBase* Node = FindStateNodeByName(SMGraph, S)) Resolved.Add(Node);
		else Missing.Add(S);
	}
	if (Missing.Num() > 0)
	{
		OutError = FString::Printf(TEXT("Source state(s) not found in '%s': %s"),
			*StateMachineName, *FString::Join(Missing, TEXT(", ")));
		return;
	}

	UAnimStateAliasNode* Alias = FEdGraphSchemaAction_NewStateNode::SpawnNodeFromTemplate<UAnimStateAliasNode>(
		SMGraph, NewObject<UAnimStateAliasNode>(), FUECPGraphLoc((float)PosX, (float)PosY), false);
	if (!Alias) { OutError = TEXT("Failed to spawn UAnimStateAliasNode"); return; }

	Alias->StateAliasName = AliasName;
	Alias->bGlobalAlias = bGlobalAlias;
	if (!bGlobalAlias)
	{
		TSet<TWeakObjectPtr<UAnimStateNodeBase>>& Set = Alias->GetAliasedStates();
		Set.Reset();
		for (UAnimStateNodeBase* SN : Resolved) Set.Add(SN);
	}

	// Alias nodes own no bound graph; OnRenameNode validates and applies the display name.
	Alias->OnRenameNode(AliasName);

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBP);
	SMGraph->NotifyGraphChanged();
	RefreshOpenAnimBlueprintEditor(AnimBP);

	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("alias_name"), Alias->GetStateName());
	Obj->SetStringField(TEXT("node_guid"), Alias->NodeGuid.ToString());
	Obj->SetBoolField(TEXT("global_alias"), bGlobalAlias);
	Obj->SetNumberField(TEXT("source_state_count"), Resolved.Num());
	CompileAnimBlueprintAndReport(AnimBP, Obj);
	UEditorAssetLibrary::SaveAsset(AnimBlueprintPath, false);
	BuildSuccessJson(Obj, OutJson);
}

void HandleSetTransitionPriority(const FString& AnimBlueprintPath, const FString& StateMachineName,
	const FString& FromState, const FString& ToState, int32 Priority,
	FString& OutJson, FString& OutError)
{
	UAnimBlueprint* AnimBP = Cast<UAnimBlueprint>(UEditorAssetLibrary::LoadAsset(AnimBlueprintPath));
	if (!AnimBP) { OutError = FString::Printf(TEXT("Could not load AnimBlueprint: %s"), *AnimBlueprintPath); return; }

	FString SMLookupError;
	UAnimationStateMachineGraph* SMGraph = FindStateMachineGraph(AnimBP, StateMachineName, &SMLookupError);
	if (!SMGraph) { OutError = SMLookupError; return; }

	UAnimStateTransitionNode* TransNode = FindTransitionBetween(SMGraph, FromState, ToState);
	if (!TransNode)
	{
		OutError = FString::Printf(TEXT("No transition from '%s' to '%s' in '%s' (transitions are directional)"),
			*FromState, *ToState, *SMGraph->GetName());
		return;
	}

	const int32 OldPriority = TransNode->PriorityOrder;
	TransNode->Modify();
	TransNode->PriorityOrder = Priority;

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBP);
	SMGraph->NotifyGraphChanged();

	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("from_state"), FromState);
	Obj->SetStringField(TEXT("to_state"), ToState);
	Obj->SetStringField(TEXT("transition_guid"), TransNode->NodeGuid.ToString());
	Obj->SetNumberField(TEXT("old_priority"), OldPriority);
	Obj->SetNumberField(TEXT("priority"), Priority);
	CompileAnimBlueprintAndReport(AnimBP, Obj);
	UEditorAssetLibrary::SaveAsset(AnimBlueprintPath, false);
	BuildSuccessJson(Obj, OutJson);
}

void HandleSetAnimNodePosition(const FString& AnimBlueprintPath, const FString& NodeGuid,
	int32 PosX, int32 PosY, FString& OutJson, FString& OutError)
{
	UAnimBlueprint* AnimBP = Cast<UAnimBlueprint>(UEditorAssetLibrary::LoadAsset(AnimBlueprintPath));
	if (!AnimBP) { OutError = FString::Printf(TEXT("Could not load AnimBlueprint: %s"), *AnimBlueprintPath); return; }

	FGuid TargetGuid;
	if (!FGuid::Parse(NodeGuid, TargetGuid)) { OutError = TEXT("Invalid node_guid format"); return; }

	TArray<UEdGraph*> AllGraphs;
	AnimBP->GetAllGraphs(AllGraphs);

	UEdGraphNode* Found = nullptr;
	for (UEdGraph* Graph : AllGraphs)
	{
		if (!Graph) continue;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (Node && Node->NodeGuid == TargetGuid) { Found = Node; break; }
		}
		if (Found) break;
	}
	if (!Found)
	{
		OutError = FString::Printf(TEXT("Node with GUID %s not found anywhere in AnimBP '%s'"),
			*NodeGuid, *AnimBP->GetName());
		return;
	}

	const int32 OldX = Found->NodePosX;
	const int32 OldY = Found->NodePosY;
	Found->Modify();
	Found->NodePosX = PosX;
	Found->NodePosY = PosY;

	// Position is cosmetic: no structural flag / recompile, just persist it.
	FBlueprintEditorUtils::MarkBlueprintAsModified(AnimBP);
	if (UEdGraph* OwnerGraph = Found->GetGraph()) OwnerGraph->NotifyGraphChanged();
	UEditorAssetLibrary::SaveAsset(AnimBlueprintPath, false);

	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("node_guid"), NodeGuid);
	Obj->SetStringField(TEXT("node_class"), Found->GetClass()->GetName());
	Obj->SetNumberField(TEXT("old_pos_x"), OldX);
	Obj->SetNumberField(TEXT("old_pos_y"), OldY);
	Obj->SetNumberField(TEXT("pos_x"), PosX);
	Obj->SetNumberField(TEXT("pos_y"), PosY);
	BuildSuccessJson(Obj, OutJson);
}

void HandleAddBlendSpacePlayer(const FString& AnimBPPath, const FString& BlendSpacePath,
	int32 PosX, int32 PosY, FString& OutJson, FString& OutError)
{
	UAnimBlueprint* AnimBP = Cast<UAnimBlueprint>(UEditorAssetLibrary::LoadAsset(AnimBPPath));
	if (!AnimBP) { SetError(FString::Printf(TEXT("Could not load AnimBlueprint: %s"), *AnimBPPath), OutJson, OutError); return; }

	UAnimationGraph* AnimGraph = FindAnimGraph(AnimBP);
	if (!AnimGraph) { SetError(TEXT("No AnimGraph found in this AnimBlueprint."), OutJson, OutError); return; }

	UBlendSpace* BSAsset = Cast<UBlendSpace>(UEditorAssetLibrary::LoadAsset(BlendSpacePath));
	if (!BSAsset)
	{
		SetError(FString::Printf(TEXT("Could not load BlendSpace asset: %s."), *BlendSpacePath), OutJson, OutError);
		return;
	}
	if (BSAsset->GetClass()->GetName().Contains(TEXT("AimOffset")))
	{
		SetError(FString::Printf(TEXT("'%s' is an AimOffset BlendSpace — use add_aim_offset_player (RotationOffsetBlendSpace node), not a BlendSpacePlayer."), *BlendSpacePath), OutJson, OutError);
		return;
	}
	if (AnimBP->TargetSkeleton && BSAsset->GetSkeleton() && AnimBP->TargetSkeleton != BSAsset->GetSkeleton())
	{
		SetError(FString::Printf(TEXT("Skeleton mismatch: AnimBP targets '%s', blendspace targets '%s'."),
			*AnimBP->TargetSkeleton->GetPathName(), *BSAsset->GetSkeleton()->GetPathName()), OutJson, OutError);
		return;
	}

	UAnimGraphNode_BlendSpacePlayer* BSNode = NewObject<UAnimGraphNode_BlendSpacePlayer>(AnimGraph);
	if (!BSNode) { SetError(TEXT("Failed to spawn BlendSpacePlayer node."), OutJson, OutError); return; }
	BSNode->CreateNewGuid();
	BSNode->NodePosX = PosX;
	BSNode->NodePosY = PosY;
	BSNode->SetFlags(RF_Transactional);
	BSNode->SetAnimationAsset(BSAsset);
	BSNode->AllocateDefaultPins();
	BSNode->PostPlacedNewNode();
	AnimGraph->Modify();
	AnimGraph->AddNode(BSNode,  true,  false);

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBP);
	AnimGraph->NotifyGraphChanged();

	TSharedPtr<FJsonObject> Obj = SerializeAnimNodeJson(BSNode, AnimGraph);
	Obj->SetBoolField(TEXT("success"), true);
	CompileAnimBlueprintAndReport(AnimBP, Obj);
	UEditorAssetLibrary::SaveAsset(AnimBPPath, false);
	BuildSuccessJson(Obj, OutJson);
}

void HandleSetBlendSpacePlayerAsset(const FString& AnimBPPath, const FString& NodeGuid,
	const FString& BlendSpacePath, bool bDryRun, const FString& ExpectCurrentAsset,
	FString& OutJson, FString& OutError)
{
	UAnimBlueprint* AnimBP = Cast<UAnimBlueprint>(UEditorAssetLibrary::LoadAsset(AnimBPPath));
	if (!AnimBP) { SetError(FString::Printf(TEXT("Could not load AnimBlueprint: %s"), *AnimBPPath), OutJson, OutError); return; }

	FGuid TargetGuid;
	if (!FGuid::Parse(NodeGuid, TargetGuid)) { SetError(TEXT("Invalid node_guid format."), OutJson, OutError); return; }

	TArray<UEdGraph*> AllGraphs;
	AnimBP->GetAllGraphs(AllGraphs);
	UEdGraphNode* FoundNode = nullptr;
	UEdGraph* OwningGraph = nullptr;
	for (UEdGraph* Graph : AllGraphs)
	{
		if (!Graph) continue;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (Node && Node->NodeGuid == TargetGuid) { FoundNode = Node; OwningGraph = Graph; break; }
		}
		if (FoundNode) break;
	}
	if (!FoundNode)
	{
		SetError(FString::Printf(TEXT("Node with GUID %s not found anywhere in AnimBP '%s'. Use get_anim_graph_nodes(include_nested=true) to list nodes + GUIDs."), *NodeGuid, *AnimBP->GetName()), OutJson, OutError);
		return;
	}

	UAnimGraphNode_BlendSpacePlayer* BSNode = Cast<UAnimGraphNode_BlendSpacePlayer>(FoundNode);
	if (!BSNode)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetBoolField(TEXT("success"), false);
		Obj->SetStringField(TEXT("error"), FString::Printf(TEXT("Node %s is a %s, not a BlendSpacePlayer."), *NodeGuid, *FoundNode->GetClass()->GetName()));
		Obj->SetObjectField(TEXT("manual_handoff"), BuildAnimHandoff(FoundNode, OwningGraph, BlendSpacePath, {}));
		BuildSuccessJson(Obj, OutJson);
		return;
	}

	UBlendSpace* NewBS = Cast<UBlendSpace>(UEditorAssetLibrary::LoadAsset(BlendSpacePath));
	if (!NewBS)
	{
		SetError(FString::Printf(TEXT("Could not load BlendSpace asset: %s."), *BlendSpacePath), OutJson, OutError);
		return;
	}
	if (NewBS->GetClass()->GetName().Contains(TEXT("AimOffset")))
	{
		SetError(FString::Printf(TEXT("'%s' is an AimOffset BlendSpace — not valid on a BlendSpacePlayer. Use add_aim_offset_player (RotationOffsetBlendSpace) for aim offsets."), *BlendSpacePath), OutJson, OutError);
		return;
	}
	if (AnimBP->TargetSkeleton && NewBS->GetSkeleton() && AnimBP->TargetSkeleton != NewBS->GetSkeleton())
	{
		SetError(FString::Printf(TEXT("Skeleton mismatch: AnimBP targets '%s', blendspace targets '%s'."),
			*AnimBP->TargetSkeleton->GetPathName(), *NewBS->GetSkeleton()->GetPathName()), OutJson, OutError);
		return;
	}

	const FString OldAsset = BSNode->GetAnimationAsset() ? BSNode->GetAnimationAsset()->GetPathName() : FString();
	const bool bOldIs1D = BSNode->GetAnimationAsset() && BSNode->GetAnimationAsset()->IsA<UBlendSpace1D>();
	const bool bNewIs1D = NewBS->IsA<UBlendSpace1D>();
	const bool bDimChanges = (bOldIs1D != bNewIs1D);
	TSharedPtr<FJsonObject> Before = SerializeAnimNodeJson(BSNode, OwningGraph);

	if (!ExpectCurrentAsset.IsEmpty() && !OldAsset.Equals(ExpectCurrentAsset))
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetBoolField(TEXT("success"), false);
		TSharedPtr<FJsonObject> Issue = MakeShared<FJsonObject>();
		Issue->SetStringField(TEXT("type"), TEXT("topology_mismatch"));
		Issue->SetStringField(TEXT("hint"), FString::Printf(TEXT("Expected current asset '%s' but the node holds '%s' — aborting so a different branch isn't edited by mistake."), *ExpectCurrentAsset, *OldAsset));
		Obj->SetObjectField(TEXT("issue"), Issue);
		Obj->SetObjectField(TEXT("node"), Before);
		BuildSuccessJson(Obj, OutJson);
		return;
	}

	if (bDryRun)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetBoolField(TEXT("success"), true);
		Obj->SetBoolField(TEXT("dry_run"), true);
		Obj->SetStringField(TEXT("old_asset"), OldAsset);
		Obj->SetStringField(TEXT("new_asset"), BlendSpacePath);
		Obj->SetStringField(TEXT("old_dimensionality"), bOldIs1D ? TEXT("1D") : TEXT("2D"));
		Obj->SetStringField(TEXT("new_dimensionality"), bNewIs1D ? TEXT("1D") : TEXT("2D"));
		Obj->SetBoolField(TEXT("dimensionality_changes"), bDimChanges);
		if (bDimChanges)
		{
			Obj->SetStringField(TEXT("pin_effect"), bNewIs1D
				? TEXT("Y/coordinate-2 input pin will be HIDDEN (new asset is 1D); any wire on it becomes hidden/orphaned.")
				: TEXT("Y/coordinate-2 input pin will be EXPOSED (new asset is 2D) and available to wire."));
		}
		Obj->SetObjectField(TEXT("node_before"), Before);
		BuildSuccessJson(Obj, OutJson);
		return;
	}

	BSNode->Modify();
	BSNode->SetAnimationAsset(NewBS);
	BSNode->ReconstructNode();
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBP);
	if (OwningGraph) OwningGraph->NotifyGraphChanged();

	TSharedPtr<FJsonObject> After = SerializeAnimNodeJson(BSNode, OwningGraph);

	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("node_guid"), NodeGuid);
	Obj->SetStringField(TEXT("old_asset"), OldAsset);
	Obj->SetStringField(TEXT("new_asset"), BlendSpacePath);
	Obj->SetBoolField(TEXT("dimensionality_changed"), bDimChanges);
	if (bDimChanges && bNewIs1D)
	{
		Obj->SetStringField(TEXT("warning"), TEXT("New asset is 1D — the Y/coordinate-2 input pin is now hidden. If it was wired, that connection is hidden/orphaned; review node_after."));
	}
	Obj->SetObjectField(TEXT("node_before"), Before);
	Obj->SetObjectField(TEXT("node_after"),  After);
	CompileAnimBlueprintAndReport(AnimBP, Obj);
	UEditorAssetLibrary::SaveAsset(AnimBPPath, false);
	BuildSuccessJson(Obj, OutJson);
}

void HandleAddBlendSpacePlayerFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString ABP, BS;
	double PX = 0, PY = 0;
	Args->TryGetStringField(TEXT("anim_blueprint_path"), ABP);
	if (!Args->TryGetStringField(TEXT("blendspace_path"), BS)) Args->TryGetStringField(TEXT("blend_space_path"), BS);
	Args->TryGetNumberField(TEXT("position_x"), PX);
	Args->TryGetNumberField(TEXT("position_y"), PY);
	HandleAddBlendSpacePlayer(ABP, BS, (int32)PX, (int32)PY, OutJsonString, OutError);
}

void HandleSetBlendSpacePlayerAssetFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString ABP, Guid, BS, Expect;
	bool bDry = false;
	Args->TryGetStringField(TEXT("anim_blueprint_path"), ABP);
	Args->TryGetStringField(TEXT("node_guid"), Guid);
	if (!Args->TryGetStringField(TEXT("blendspace_path"), BS)) Args->TryGetStringField(TEXT("blend_space_path"), BS);
	Args->TryGetBoolField  (TEXT("dry_run"), bDry);
	Args->TryGetStringField(TEXT("expect_current_asset"), Expect);
	HandleSetBlendSpacePlayerAsset(ABP, Guid, BS, bDry, Expect, OutJsonString, OutError);
}

void HandleSetAnimStateAnimationFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString ABP, SM, ST, AP;
	Args->TryGetStringField(TEXT("anim_blueprint_path"), ABP);
	Args->TryGetStringField(TEXT("state_machine_name"), SM);
	Args->TryGetStringField(TEXT("state_name"),         ST);
	Args->TryGetStringField(TEXT("animation_path"),     AP);
	HandleSetAnimStateAnimation(ABP, SM, ST, AP, OutJsonString, OutError);
}

void HandleAddStateAliasFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString ABP, SM, AliasName;
	double PX = 0, PY = 0;
	bool bGlobal = false;
	Args->TryGetStringField(TEXT("anim_blueprint_path"), ABP);
	Args->TryGetStringField(TEXT("state_machine_name"), SM);
	Args->TryGetStringField(TEXT("alias_name"),         AliasName);
	Args->TryGetBoolField  (TEXT("global_alias"),       bGlobal);
	Args->TryGetNumberField(TEXT("position_x"),         PX);
	Args->TryGetNumberField(TEXT("position_y"),         PY);

	TArray<FString> SourceStates;
	const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
	if (Args->TryGetArrayField(TEXT("source_states"), Arr) && Arr)
	{
		for (const TSharedPtr<FJsonValue>& V : *Arr)
		{
			if (V.IsValid() && V->Type == EJson::String) SourceStates.Add(V->AsString());
		}
	}
	HandleAddStateAlias(ABP, SM, AliasName, SourceStates, bGlobal, (int32)PX, (int32)PY,
		OutJsonString, OutError);
}

void HandleSetTransitionPriorityFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString ABP, SM, FS, TS;
	double Priority = 0.0;
	Args->TryGetStringField(TEXT("anim_blueprint_path"), ABP);
	Args->TryGetStringField(TEXT("state_machine_name"), SM);
	Args->TryGetStringField(TEXT("from_state"),         FS);
	Args->TryGetStringField(TEXT("to_state"),           TS);
	Args->TryGetNumberField(TEXT("priority"),           Priority);
	HandleSetTransitionPriority(ABP, SM, FS, TS, (int32)Priority, OutJsonString, OutError);
}

void HandleSetAnimNodePositionFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString ABP, NG;
	double PX = 0, PY = 0;
	Args->TryGetStringField(TEXT("anim_blueprint_path"), ABP);
	Args->TryGetStringField(TEXT("node_guid"),           NG);
	Args->TryGetNumberField(TEXT("pos_x"),               PX);
	Args->TryGetNumberField(TEXT("pos_y"),               PY);
	HandleSetAnimNodePosition(ABP, NG, (int32)PX, (int32)PY, OutJsonString, OutError);
}

void HandleSetLayeredBlendPerBoneFilterFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }

	FString AnimBPPath, NodeGuidStr, NodeTitle;
	double LayerIdx = 0;
	Args->TryGetStringField(TEXT("anim_blueprint_path"), AnimBPPath);
	Args->TryGetStringField(TEXT("node_guid"),           NodeGuidStr);
	Args->TryGetStringField(TEXT("node_title"),          NodeTitle);
	Args->TryGetNumberField(TEXT("layer_index"),         LayerIdx);

	UAnimBlueprint* AnimBP = Cast<UAnimBlueprint>(UEditorAssetLibrary::LoadAsset(AnimBPPath));
	if (!AnimBP) { OutError = FString::Printf(TEXT("Could not load AnimBlueprint: %s"), *AnimBPPath); return; }

	// The node may live in the AnimGraph or in any state/layer sub-graph.
	TArray<UEdGraph*> AllGraphs;
	AnimBP->GetAllGraphs(AllGraphs);

	UAnimGraphNode_LayeredBoneBlend* TargetNode = nullptr;
	UEdGraph* OwningGraph = nullptr;
	FGuid Target;
	const bool bHaveGuid = !NodeGuidStr.IsEmpty() && FGuid::Parse(NodeGuidStr, Target);
	for (UEdGraph* Graph : AllGraphs)
	{
		if (!Graph) continue;
		for (UEdGraphNode* N : Graph->Nodes)
		{
			UAnimGraphNode_LayeredBoneBlend* L = Cast<UAnimGraphNode_LayeredBoneBlend>(N);
			if (!L) continue;
			if (bHaveGuid)
			{
				if (L->NodeGuid == Target) { TargetNode = L; OwningGraph = Graph; break; }
			}
			else if (!NodeTitle.IsEmpty())
			{
				const FString Title = L->GetNodeTitle(ENodeTitleType::ListView).ToString();
				if (Title.Equals(NodeTitle, ESearchCase::IgnoreCase)) { TargetNode = L; OwningGraph = Graph; break; }
			}
			else if (!TargetNode)
			{
				TargetNode = L; OwningGraph = Graph;
			}
		}
		if (TargetNode && (bHaveGuid || !NodeTitle.IsEmpty())) break;
	}
	if (!TargetNode)
	{
		OutError = TEXT("LayeredBoneBlend node not found. Pass node_guid (preferred) or node_title; or add the node first via add_layered_blend_per_bone.");
		return;
	}

	const int32 LayerIndex = (int32)LayerIdx;
	if (LayerIndex < 0 || LayerIndex >= TargetNode->Node.LayerSetup.Num())
	{
		OutError = FString::Printf(TEXT("layer_index %d out of range (node has %d layer(s) — add more via add_layered_blend_per_bone num_layers, or use 0)."),
			LayerIndex, TargetNode->Node.LayerSetup.Num());
		return;
	}

	const TArray<TSharedPtr<FJsonValue>>* BonesArr = nullptr;
	if (!Args->TryGetArrayField(TEXT("bones"), BonesArr) || !BonesArr || BonesArr->Num() == 0)
	{
		OutError = TEXT("`bones` array is required (entries can be either bare bone-name strings or {bone_name, blend_depth} objects).");
		return;
	}

	FInputBlendPose& Layer = TargetNode->Node.LayerSetup[LayerIndex];
	Layer.BranchFilters.Empty();

	int32 ApplyCount = 0;
	for (const TSharedPtr<FJsonValue>& Item : *BonesArr)
	{
		if (!Item.IsValid()) continue;
		FBranchFilter Filter;
		Filter.BlendDepth = 1;
		if (Item->Type == EJson::String)
		{
			Filter.BoneName = FName(*Item->AsString());
		}
		else if (Item->Type == EJson::Object)
		{
			TSharedPtr<FJsonObject> Obj = Item->AsObject();
			FString BoneName;
			Obj->TryGetStringField(TEXT("bone_name"), BoneName);
			if (BoneName.IsEmpty()) continue;
			Filter.BoneName = FName(*BoneName);
			double Depth = 1;
			Obj->TryGetNumberField(TEXT("blend_depth"), Depth);
			Filter.BlendDepth = FMath::Max(1, (int32)Depth);
		}
		else continue;
		Layer.BranchFilters.Add(Filter);
		++ApplyCount;
	}

	TargetNode->Modify();
	TargetNode->ReconstructNode();
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBP);
	if (OwningGraph) OwningGraph->NotifyGraphChanged();

	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetNumberField(TEXT("layer_index"), LayerIndex);
	Obj->SetNumberField(TEXT("bone_count"), ApplyCount);
	Obj->SetStringField(TEXT("node_guid"), TargetNode->NodeGuid.ToString());
	Obj->SetStringField(TEXT("graph"), OwningGraph ? OwningGraph->GetName() : FString());
	CompileAnimBlueprintAndReport(AnimBP, Obj);
	UEditorAssetLibrary::SaveAsset(AnimBPPath, false);
	BuildSuccessJson(Obj, OutJsonString);
}

void HandleSetTransitionOptionsFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }

	FString AnimBPPath, SMName, FromState, ToState, BlendProfilePath;
	Args->TryGetStringField(TEXT("anim_blueprint_path"), AnimBPPath);
	Args->TryGetStringField(TEXT("state_machine_name"), SMName);
	Args->TryGetStringField(TEXT("from_state"),         FromState);
	Args->TryGetStringField(TEXT("to_state"),           ToState);
	Args->TryGetStringField(TEXT("blend_profile"),      BlendProfilePath);

	UAnimBlueprint* AnimBP = Cast<UAnimBlueprint>(UEditorAssetLibrary::LoadAsset(AnimBPPath));
	if (!AnimBP) { OutError = FString::Printf(TEXT("Could not load AnimBlueprint: %s"), *AnimBPPath); return; }
	FString SMLookupError;
	UAnimationStateMachineGraph* SMGraph = FindStateMachineGraph(AnimBP, SMName, &SMLookupError);
	if (!SMGraph) { OutError = SMLookupError; return; }

	UAnimStateTransitionNode* TransNode = FindTransitionBetween(SMGraph, FromState, ToState);
	if (!TransNode) { OutError = FString::Printf(TEXT("No transition found from '%s' to '%s' (transitions are directional)."), *FromState, *ToState); return; }

	TransNode->Modify();
	TArray<FString> Applied;

	bool BV = false;
	if (Args->TryGetBoolField(TEXT("automatic_rule_based_on_sequence_player"), BV))
	{
		TransNode->bAutomaticRuleBasedOnSequencePlayerInState = BV;
		Applied.Add(FString::Printf(TEXT("automatic_rule_based_on_sequence_player=%s"), BV ? TEXT("true") : TEXT("false")));
	}
	if (Args->TryGetBoolField(TEXT("disable_notifications"), BV))
	{
		FStructProperty* NodeProp = CastField<FStructProperty>(TransNode->GetClass()->FindPropertyByName(TEXT("Node")));
		if (NodeProp)
		{
			void* NodePtr = NodeProp->ContainerPtrToValuePtr<void>(TransNode);
			if (FBoolProperty* BProp = FindFProperty<FBoolProperty>(NodeProp->Struct, TEXT("bDisableNotifications")))
			{
				BProp->SetPropertyValue_InContainer(NodePtr, BV);
				Applied.Add(FString::Printf(TEXT("disable_notifications=%s"), BV ? TEXT("true") : TEXT("false")));
			}
		}
	}
	if (!BlendProfilePath.IsEmpty())
	{
		UObject* Profile = UEditorAssetLibrary::LoadAsset(BlendProfilePath);
		if (FObjectProperty* BPP = FindFProperty<FObjectProperty>(TransNode->GetClass(), TEXT("BlendProfile")))
		{
			BPP->SetObjectPropertyValue_InContainer(TransNode, Profile);
			Applied.Add(FString::Printf(TEXT("blend_profile=%s"), *BlendProfilePath));
		}
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBP);
	SMGraph->NotifyGraphChanged();

	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("applied"), FString::Join(Applied, TEXT(", ")));
	Obj->SetStringField(TEXT("transition_guid"), TransNode->NodeGuid.ToString());
	CompileAnimBlueprintAndReport(AnimBP, Obj);
	UEditorAssetLibrary::SaveAsset(AnimBPPath, false);
	BuildSuccessJson(Obj, OutJsonString);
}

void HandleSetStateOptionsFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }

	FString AnimBPPath, SMName, StateName;
	Args->TryGetStringField(TEXT("anim_blueprint_path"), AnimBPPath);
	Args->TryGetStringField(TEXT("state_machine_name"), SMName);
	Args->TryGetStringField(TEXT("state_name"),         StateName);

	UAnimBlueprint* AnimBP = Cast<UAnimBlueprint>(UEditorAssetLibrary::LoadAsset(AnimBPPath));
	if (!AnimBP) { OutError = FString::Printf(TEXT("Could not load AnimBlueprint: %s"), *AnimBPPath); return; }
	FString SMLookupError;
	UAnimationStateMachineGraph* SMGraph = FindStateMachineGraph(AnimBP, SMName, &SMLookupError);
	if (!SMGraph) { OutError = SMLookupError; return; }

	UAnimStateNode* TargetState = nullptr;
	for (UEdGraphNode* N : SMGraph->Nodes)
	{
		UAnimStateNode* S = Cast<UAnimStateNode>(N);
		if (S && S->GetStateName() == StateName) { TargetState = S; break; }
	}
	if (!TargetState)
	{
		OutError = FString::Printf(TEXT("State '%s' not found in state machine '%s'."), *StateName, *SMName);
		return;
	}

	TargetState->Modify();
	TArray<FString> Applied;

	bool BV = false;
	if (Args->TryGetBoolField(TEXT("always_reset_on_enter"), BV) ||
	    Args->TryGetBoolField(TEXT("always_reset_on_entry"), BV))
	{
		FBoolProperty* BProp = FindFProperty<FBoolProperty>(TargetState->GetClass(), TEXT("bAlwaysResetOnEntry"));
		if (!BProp) BProp = FindFProperty<FBoolProperty>(TargetState->GetClass(), TEXT("bAlwaysResetOnEnter"));
		if (BProp)
		{
			BProp->SetPropertyValue_InContainer(TargetState, BV);
			Applied.Add(FString::Printf(TEXT("always_reset_on_entry=%s"), BV ? TEXT("true") : TEXT("false")));
		}
	}
	if (Args->TryGetBoolField(TEXT("skip_first_update_transition"), BV))
	{
		UAnimGraphNode_StateMachine* SMNode = nullptr;
		if (UAnimationGraph* AnimGraph = FindAnimGraph(AnimBP))
		{
			for (UEdGraphNode* N : AnimGraph->Nodes)
			{
				UAnimGraphNode_StateMachine* Cand = Cast<UAnimGraphNode_StateMachine>(N);
				if (!Cand) continue;
				if (Cand->EditorStateMachineGraph == SMGraph) { SMNode = Cand; break; }
			}
		}
		if (SMNode)
		{
			FStructProperty* NodeProp = CastField<FStructProperty>(SMNode->GetClass()->FindPropertyByName(TEXT("Node")));
			if (NodeProp)
			{
				void* NodePtr = NodeProp->ContainerPtrToValuePtr<void>(SMNode);
				if (FBoolProperty* BProp = FindFProperty<FBoolProperty>(NodeProp->Struct, TEXT("bSkipFirstUpdateTransition")))
				{
					BProp->SetPropertyValue_InContainer(NodePtr, BV);
					SMNode->Modify();
					Applied.Add(FString::Printf(TEXT("skip_first_update_transition=%s (on state machine node)"), BV ? TEXT("true") : TEXT("false")));
				}
			}
		}
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBP);
	SMGraph->NotifyGraphChanged();

	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("state"), StateName);
	Obj->SetStringField(TEXT("applied"), FString::Join(Applied, TEXT(", ")));
	CompileAnimBlueprintAndReport(AnimBP, Obj);
	UEditorAssetLibrary::SaveAsset(AnimBPPath, false);
	BuildSuccessJson(Obj, OutJsonString);
}

namespace
{
	static UIKRetargeter* LoadRetargeterOrFail(const FString& Path, FString& OutJsonString, FString& OutError)
	{
		UIKRetargeter* R = Cast<UIKRetargeter>(UEditorAssetLibrary::LoadAsset(Path));
		if (!R) { SetError(FString::Printf(TEXT("Could not load IKRetargeter: %s"), *Path), OutJsonString, OutError); return nullptr; }
		return R;
	}

	static ERetargetSourceOrTarget ParseSide(const FString& In)
	{
		return In.Equals(TEXT("source"), ESearchCase::IgnoreCase)
			? ERetargetSourceOrTarget::Source
			: ERetargetSourceOrTarget::Target;
	}

	static ERetargetTranslationMode ParseTranslationMode(const FString& In, ERetargetTranslationMode Default)
	{
		if (In.Equals(TEXT("None"),            ESearchCase::IgnoreCase)) return ERetargetTranslationMode::None;
		if (In.Equals(TEXT("GloballyScaled"),  ESearchCase::IgnoreCase)) return ERetargetTranslationMode::GloballyScaled;
		if (In.Equals(TEXT("Absolute"),        ESearchCase::IgnoreCase)) return ERetargetTranslationMode::Absolute;
		return Default;
	}

	static ERetargetRotationMode ParseRotationMode(const FString& In, ERetargetRotationMode Default)
	{
		if (In.Equals(TEXT("Interp"),            ESearchCase::IgnoreCase) ||
			In.Equals(TEXT("Interpolated"),      ESearchCase::IgnoreCase)) return ERetargetRotationMode::Interpolated;
		if (In.Equals(TEXT("OneToOne"),          ESearchCase::IgnoreCase)) return ERetargetRotationMode::OneToOne;
		if (In.Equals(TEXT("OneToOneReversed"),  ESearchCase::IgnoreCase)) return ERetargetRotationMode::OneToOneReversed;
		if (In.Equals(TEXT("None"),              ESearchCase::IgnoreCase)) return ERetargetRotationMode::None;
		return Default;
	}

	static FQuat ParseRotationField(const TSharedPtr<FJsonObject>& Owner, const FString& Field)
	{
		const TSharedPtr<FJsonObject>* RotObj = nullptr;
		if (Owner->TryGetObjectField(Field, RotObj) && RotObj && RotObj->IsValid())
		{
			double X=0, Y=0, Z=0, W=1; bool bHasW = false;
			(*RotObj)->TryGetNumberField(TEXT("x"), X);
			(*RotObj)->TryGetNumberField(TEXT("y"), Y);
			(*RotObj)->TryGetNumberField(TEXT("z"), Z);
			bHasW = (*RotObj)->TryGetNumberField(TEXT("w"), W);
			if (bHasW)
			{
				FQuat Q((float)X, (float)Y, (float)Z, (float)W);
				Q.Normalize();
				return Q;
			}
			return FRotator((float)Y, (float)Z, (float)X).Quaternion();
		}
		double Pitch=0, Yaw=0, Roll=0; bool bAny=false;
		bAny |= Owner->TryGetNumberField(TEXT("pitch"), Pitch);
		bAny |= Owner->TryGetNumberField(TEXT("yaw"),   Yaw);
		bAny |= Owner->TryGetNumberField(TEXT("roll"),  Roll);
		if (bAny) return FRotator((float)Pitch, (float)Yaw, (float)Roll).Quaternion();
		return FQuat::Identity;
	}
}

void HandleSetRetargetChainSettingsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Missing arguments"); return; }
	FString RetargeterPath, ChainName;
	Args->TryGetStringField(TEXT("retargeter_path"), RetargeterPath);
	if (RetargeterPath.IsEmpty()) Args->TryGetStringField(TEXT("asset_path"), RetargeterPath);
	Args->TryGetStringField(TEXT("chain_name"), ChainName);
	if (ChainName.IsEmpty()) Args->TryGetStringField(TEXT("target_chain"), ChainName);
	if (ChainName.IsEmpty()) { OutError = TEXT("chain_name is required"); return; }

	UIKRetargeter* R = LoadRetargeterOrFail(RetargeterPath, OutJsonString, OutError);
	if (!R) return;
	UIKRetargeterController* Ctrl = UIKRetargeterController::GetController(R);
	if (!Ctrl) { OutError = TEXT("Could not get IKRetargeter controller."); return; }

	FTargetChainSettings Settings;
	{
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		const TArray<TObjectPtr<URetargetChainSettings>>& AllChains = R->GetAllChainSettings();
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
		for (const URetargetChainSettings* CS : AllChains)
		{
			if (CS && CS->TargetChain == FName(*ChainName)) { Settings = CS->Settings; break; }
		}
	}

	auto AsBool = [&](const TCHAR* Key, bool& Out) -> bool
	{
		bool B; if (Args->TryGetBoolField(Key, B)) { Out = B; return true; }
		return false;
	};
	auto AsFloat = [&](const TCHAR* Key, float& Out) -> bool
	{
		double D; if (Args->TryGetNumberField(Key, D)) { Out = (float)D; return true; }
		return false;
	};
	auto AsString = [&](const TCHAR* Key, FString& Out) -> bool { return Args->TryGetStringField(Key, Out); };

	TArray<FString> Applied;
	FString S;
	if (AsString(TEXT("fk_translation_mode"), S) || AsString(TEXT("translation_mode"), S))
	{ Settings.FK.TranslationMode = ParseTranslationMode(S, Settings.FK.TranslationMode); Applied.Add(TEXT("fk_translation_mode")); }
	if (AsString(TEXT("fk_rotation_mode"), S) || AsString(TEXT("rotation_mode"), S))
	{ Settings.FK.RotationMode = ParseRotationMode(S, Settings.FK.RotationMode); Applied.Add(TEXT("fk_rotation_mode")); }
	if (AsFloat(TEXT("fk_translation_alpha"), Settings.FK.TranslationAlpha)) Applied.Add(TEXT("fk_translation_alpha"));
	if (AsFloat(TEXT("fk_rotation_alpha"),    Settings.FK.RotationAlpha))    Applied.Add(TEXT("fk_rotation_alpha"));
	if (AsFloat(TEXT("fk_pole_offset"),       Settings.FK.PoleVectorOffset)) Applied.Add(TEXT("fk_pole_offset"));
	if (AsBool (TEXT("fk_enable"),            Settings.FK.EnableFK))         Applied.Add(TEXT("fk_enable"));
	if (AsBool (TEXT("ik_enable"),            Settings.IK.EnableIK))         Applied.Add(TEXT("ik_enable"));
	if (AsFloat(TEXT("ik_blend_to_source"),   Settings.IK.BlendToSource))    Applied.Add(TEXT("ik_blend_to_source"));
	if (AsFloat(TEXT("ik_extension"),         Settings.IK.Extension))        Applied.Add(TEXT("ik_extension"));
	if (AsFloat(TEXT("ik_scale_vertical"),    Settings.IK.ScaleVertical))    Applied.Add(TEXT("ik_scale_vertical"));

	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	const bool bChainSettingsApplied = Ctrl->SetRetargetChainSettings(FName(*ChainName), Settings);
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
	if (!bChainSettingsApplied)
	{ OutError = FString::Printf(TEXT("Chain '%s' not found on retargeter."), *ChainName); return; }

	UEditorAssetLibrary::SaveAsset(RetargeterPath, false);

	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField  (TEXT("success"), true);
	Obj->SetStringField(TEXT("retargeter_path"), RetargeterPath);
	Obj->SetStringField(TEXT("chain_name"), ChainName);
	TArray<TSharedPtr<FJsonValue>> AppliedJson;
	for (const FString& K : Applied) AppliedJson.Add(MakeShared<FJsonValueString>(K));
	Obj->SetArrayField(TEXT("applied"), AppliedJson);
	BuildSuccessJson(Obj, OutJsonString);
}

void HandleSetRetargetRootSettingsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Missing arguments"); return; }
	FString RetargeterPath;
	Args->TryGetStringField(TEXT("retargeter_path"), RetargeterPath);
	if (RetargeterPath.IsEmpty()) Args->TryGetStringField(TEXT("asset_path"), RetargeterPath);

	UIKRetargeter* R = LoadRetargeterOrFail(RetargeterPath, OutJsonString, OutError);
	if (!R) return;
	UIKRetargeterController* Ctrl = UIKRetargeterController::GetController(R);
	if (!Ctrl) { OutError = TEXT("Could not get IKRetargeter controller."); return; }

	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	FTargetRootSettings Root = Ctrl->GetRootSettings();
	PRAGMA_ENABLE_DEPRECATION_WARNINGS

	auto AsFloat = [&](const TCHAR* Key, float& Out) -> bool
	{ double D; if (Args->TryGetNumberField(Key, D)) { Out = (float)D; return true; } return false; };

	TArray<FString> Applied;
	if (AsFloat(TEXT("rotation_alpha"),       Root.RotationAlpha))    Applied.Add(TEXT("rotation_alpha"));
	if (AsFloat(TEXT("translation_alpha"),    Root.TranslationAlpha)) Applied.Add(TEXT("translation_alpha"));
	if (AsFloat(TEXT("blend_to_source"),      Root.BlendToSource))    Applied.Add(TEXT("blend_to_source"));
	if (AsFloat(TEXT("scale_horizontal"),     Root.ScaleHorizontal))  Applied.Add(TEXT("scale_horizontal"));
	if (AsFloat(TEXT("scale_vertical"),       Root.ScaleVertical))    Applied.Add(TEXT("scale_vertical"));
	if (AsFloat(TEXT("affect_ik_horizontal"), Root.AffectIKHorizontal)) Applied.Add(TEXT("affect_ik_horizontal"));
	if (AsFloat(TEXT("affect_ik_vertical"),   Root.AffectIKVertical))   Applied.Add(TEXT("affect_ik_vertical"));

	const TSharedPtr<FJsonObject>* OffObj = nullptr;
	if (Args->TryGetObjectField(TEXT("blend_to_source_weights"), OffObj) && OffObj && OffObj->IsValid())
	{
		double X = Root.BlendToSourceWeights.X, Y = Root.BlendToSourceWeights.Y, Z = Root.BlendToSourceWeights.Z;
		(*OffObj)->TryGetNumberField(TEXT("x"), X);
		(*OffObj)->TryGetNumberField(TEXT("y"), Y);
		(*OffObj)->TryGetNumberField(TEXT("z"), Z);
		Root.BlendToSourceWeights = FVector((float)X, (float)Y, (float)Z);
		Applied.Add(TEXT("blend_to_source_weights"));
	}

	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	Ctrl->SetRootSettings(Root);
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
	UEditorAssetLibrary::SaveAsset(RetargeterPath, false);

	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField  (TEXT("success"), true);
	Obj->SetStringField(TEXT("retargeter_path"), RetargeterPath);
	TArray<TSharedPtr<FJsonValue>> AppliedJson;
	for (const FString& K : Applied) AppliedJson.Add(MakeShared<FJsonValueString>(K));
	Obj->SetArrayField(TEXT("applied"), AppliedJson);
	BuildSuccessJson(Obj, OutJsonString);
}

void HandleEditRetargetPoseBoneFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Missing arguments"); return; }
	FString RetargeterPath;
	Args->TryGetStringField(TEXT("retargeter_path"), RetargeterPath);
	if (RetargeterPath.IsEmpty()) Args->TryGetStringField(TEXT("asset_path"), RetargeterPath);
	FString SideStr = TEXT("target"); Args->TryGetStringField(TEXT("side"), SideStr);
	if (SideStr.IsEmpty()) Args->TryGetStringField(TEXT("source_or_target"), SideStr);
	const ERetargetSourceOrTarget Side = ParseSide(SideStr);

	UIKRetargeter* R = LoadRetargeterOrFail(RetargeterPath, OutJsonString, OutError);
	if (!R) return;
	UIKRetargeterController* Ctrl = UIKRetargeterController::GetController(R);
	if (!Ctrl) { OutError = TEXT("Could not get IKRetargeter controller."); return; }

	auto ApplyOne = [&](const TSharedPtr<FJsonObject>& Item, FString& ItemErr) -> FString
	{
		FString BoneName; Item->TryGetStringField(TEXT("bone_name"), BoneName);
		if (BoneName.IsEmpty()) Item->TryGetStringField(TEXT("bone"), BoneName);
		if (BoneName.IsEmpty()) { ItemErr = TEXT("Missing bone_name"); return FString(); }
		FQuat Rot = ParseRotationField(Item, TEXT("rotation"));
		Ctrl->SetRotationOffsetForRetargetPoseBone(FName(*BoneName), Rot, Side);
		return BoneName;
	};

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("bones"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString ItemErr;
			FString BoneName = ApplyOne(Item, ItemErr);
			if (!ItemErr.IsEmpty()) { Batch.AddFailure(i, ItemErr); continue; }
			TSharedPtr<FJsonObject> Extra = MakeShared<FJsonObject>();
			Extra->SetStringField(TEXT("bone_name"), BoneName);
			Batch.AddSuccess(i, Extra);
		}
		Batch.Finalize(OutJsonString);
		UEditorAssetLibrary::SaveAsset(RetargeterPath, false);
		return;
	}

	FString ItemErr;
	FString BoneName = ApplyOne(Args, ItemErr);
	if (!ItemErr.IsEmpty()) { OutError = ItemErr; return; }

	UEditorAssetLibrary::SaveAsset(RetargeterPath, false);

	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField  (TEXT("success"), true);
	Obj->SetStringField(TEXT("retargeter_path"), RetargeterPath);
	Obj->SetStringField(TEXT("bone_name"), BoneName);
	Obj->SetStringField(TEXT("side"), Side == ERetargetSourceOrTarget::Source ? TEXT("source") : TEXT("target"));
	BuildSuccessJson(Obj, OutJsonString);
}

void HandleAutoAlignRetargetPoseFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Missing arguments"); return; }
	FString RetargeterPath;
	Args->TryGetStringField(TEXT("retargeter_path"), RetargeterPath);
	if (RetargeterPath.IsEmpty()) Args->TryGetStringField(TEXT("asset_path"), RetargeterPath);
	FString SideStr = TEXT("target"); Args->TryGetStringField(TEXT("side"), SideStr);
	if (SideStr.IsEmpty()) Args->TryGetStringField(TEXT("source_or_target"), SideStr);
	const ERetargetSourceOrTarget Side = ParseSide(SideStr);

	UIKRetargeter* R = LoadRetargeterOrFail(RetargeterPath, OutJsonString, OutError);
	if (!R) return;
	UIKRetargeterController* Ctrl = UIKRetargeterController::GetController(R);
	if (!Ctrl) { OutError = TEXT("Could not get IKRetargeter controller."); return; }

	const TArray<TSharedPtr<FJsonValue>>* BonesJson = nullptr;
	TArray<FName> Bones;
	if (Args->TryGetArrayField(TEXT("bones"), BonesJson) && BonesJson)
	{
		for (const auto& V : *BonesJson)
			if (V.IsValid() && V->Type == EJson::String) Bones.Add(FName(*V->AsString()));
	}

	if (Bones.Num() == 0)
	{
		Ctrl->AutoAlignAllBones(Side);
	}
	else
	{
		Ctrl->AutoAlignBones(Bones, ERetargetAutoAlignMethod::ChainToChain, Side);
	}

	UEditorAssetLibrary::SaveAsset(RetargeterPath, false);

	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField  (TEXT("success"), true);
	Obj->SetStringField(TEXT("retargeter_path"), RetargeterPath);
	Obj->SetStringField(TEXT("side"), Side == ERetargetSourceOrTarget::Source ? TEXT("source") : TEXT("target"));
	Obj->SetNumberField(TEXT("bone_count"), Bones.Num() == 0 ? -1 : Bones.Num());
	BuildSuccessJson(Obj, OutJsonString);
}

void HandleResetRetargetPoseFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Missing arguments"); return; }
	FString RetargeterPath;
	Args->TryGetStringField(TEXT("retargeter_path"), RetargeterPath);
	if (RetargeterPath.IsEmpty()) Args->TryGetStringField(TEXT("asset_path"), RetargeterPath);
	FString SideStr = TEXT("target"); Args->TryGetStringField(TEXT("side"), SideStr);
	if (SideStr.IsEmpty()) Args->TryGetStringField(TEXT("source_or_target"), SideStr);
	const ERetargetSourceOrTarget Side = ParseSide(SideStr);

	UIKRetargeter* R = LoadRetargeterOrFail(RetargeterPath, OutJsonString, OutError);
	if (!R) return;
	UIKRetargeterController* Ctrl = UIKRetargeterController::GetController(R);
	if (!Ctrl) { OutError = TEXT("Could not get IKRetargeter controller."); return; }

	FString PoseName; Args->TryGetStringField(TEXT("pose_name"), PoseName);
	const FName PoseFName = PoseName.IsEmpty() ? Ctrl->GetCurrentRetargetPoseName(Side) : FName(*PoseName);

	const TArray<TSharedPtr<FJsonValue>>* BonesJson = nullptr;
	TArray<FName> Bones;
	if (Args->TryGetArrayField(TEXT("bones"), BonesJson) && BonesJson)
	{
		for (const auto& V : *BonesJson)
			if (V.IsValid() && V->Type == EJson::String) Bones.Add(FName(*V->AsString()));
	}

	Ctrl->ResetRetargetPose(PoseFName, Bones, Side);
	UEditorAssetLibrary::SaveAsset(RetargeterPath, false);

	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField  (TEXT("success"), true);
	Obj->SetStringField(TEXT("retargeter_path"), RetargeterPath);
	Obj->SetStringField(TEXT("pose_name"), PoseFName.ToString());
	Obj->SetStringField(TEXT("side"), Side == ERetargetSourceOrTarget::Source ? TEXT("source") : TEXT("target"));
	Obj->SetNumberField(TEXT("bone_count"), Bones.Num() == 0 ? -1 : Bones.Num());
	BuildSuccessJson(Obj, OutJsonString);
}

void HandleExportRetargetAnimationFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Missing arguments"); return; }
	FString RetargeterPath, SourceMeshPath, TargetMeshPath;
	Args->TryGetStringField(TEXT("retargeter_path"), RetargeterPath);
	if (RetargeterPath.IsEmpty()) Args->TryGetStringField(TEXT("asset_path"), RetargeterPath);
	Args->TryGetStringField(TEXT("source_mesh"), SourceMeshPath);
	if (SourceMeshPath.IsEmpty()) Args->TryGetStringField(TEXT("source_mesh_path"), SourceMeshPath);
	Args->TryGetStringField(TEXT("target_mesh"), TargetMeshPath);
	if (TargetMeshPath.IsEmpty()) Args->TryGetStringField(TEXT("target_mesh_path"), TargetMeshPath);

	UIKRetargeter* R = LoadRetargeterOrFail(RetargeterPath, OutJsonString, OutError);
	if (!R) return;

	USkeletalMesh* SourceMesh = Cast<USkeletalMesh>(UEditorAssetLibrary::LoadAsset(SourceMeshPath));
	USkeletalMesh* TargetMesh = Cast<USkeletalMesh>(UEditorAssetLibrary::LoadAsset(TargetMeshPath));
	if (!SourceMesh) { OutError = FString::Printf(TEXT("Could not load source skeletal mesh: %s"), *SourceMeshPath); return; }
	if (!TargetMesh) { OutError = FString::Printf(TEXT("Could not load target skeletal mesh: %s"), *TargetMeshPath); return; }

	const TArray<TSharedPtr<FJsonValue>>* AnimsJson = nullptr;
	if (!Args->TryGetArrayField(TEXT("animation_paths"), AnimsJson))
		Args->TryGetArrayField(TEXT("anim_paths"), AnimsJson);
	if (!AnimsJson || AnimsJson->Num() == 0)
	{ OutError = TEXT("animation_paths is required (array of AnimSequence/Montage/BlendSpace asset paths)"); return; }

	TArray<FAssetData> AssetsToRetarget;
	TArray<FString> SkippedPaths;
	for (const auto& V : *AnimsJson)
	{
		if (!V.IsValid() || V->Type != EJson::String) continue;
		const FString Path = V->AsString();
		UObject* Loaded = UEditorAssetLibrary::LoadAsset(Path);
		if (Loaded && Loaded->IsA<UAnimationAsset>())
			AssetsToRetarget.Add(FAssetData(Loaded));
		else
			SkippedPaths.Add(Path);
	}
	if (AssetsToRetarget.Num() == 0)
	{ OutError = TEXT("No valid animation assets resolved from animation_paths."); return; }

	FString Search, Replace, Prefix, Suffix;
	Args->TryGetStringField(TEXT("search"),  Search);
	Args->TryGetStringField(TEXT("replace"), Replace);
	Args->TryGetStringField(TEXT("prefix"),  Prefix);
	Args->TryGetStringField(TEXT("suffix"),  Suffix);
	bool bIncludeReferenced = true;
	Args->TryGetBoolField(TEXT("include_referenced"), bIncludeReferenced);

	PRAGMA_DISABLE_DEPRECATION_WARNINGS
#if !UE_VERSION_OLDER_THAN(5, 8, 0)
	const TArray<FAssetData> Created = UIKRetargetBatchOperation::DuplicateAndRetarget(
		AssetsToRetarget, SourceMesh, TargetMesh, R, Search, Replace, Prefix, Suffix,
		FString(), false, bIncludeReferenced);
#else
	const TArray<FAssetData> Created = UIKRetargetBatchOperation::DuplicateAndRetarget(
		AssetsToRetarget, SourceMesh, TargetMesh, R, Search, Replace, Prefix, Suffix, bIncludeReferenced);
#endif
	PRAGMA_ENABLE_DEPRECATION_WARNINGS

	FString SavePath;
	if (!Args->TryGetStringField(TEXT("save_path"), SavePath))
		Args->TryGetStringField(TEXT("path"), SavePath);
	while (SavePath.EndsWith(TEXT("/"))) SavePath.LeftChopInline(1);

	TArray<FAssetData> MovedAssets;
	TArray<TPair<FString, FString>> MoveFailures;
	if (!SavePath.IsEmpty())
	{
		for (const FAssetData& AD : Created)
		{
			const FString OldPath = AD.GetObjectPathString();
			const FString OldPackagePath = AD.PackageName.ToString();
			const FString AssetName = AD.AssetName.ToString();
			const FString NewPackagePath = SavePath + TEXT("/") + AssetName;
			const FString NewObjectPath  = NewPackagePath + TEXT(".") + AssetName;

			if (NewPackagePath.Equals(OldPackagePath)) { MovedAssets.Add(AD); continue; }
			if (!UEditorAssetLibrary::RenameAsset(OldPath, NewObjectPath))
			{
				MoveFailures.Add({ OldPath, FString::Printf(TEXT("RenameAsset returned false (destination=%s)"), *NewObjectPath) });
				MovedAssets.Add(AD);
				continue;
			}
			UEditorAssetLibrary::SaveAsset(NewObjectPath, false);
			MovedAssets.Add(FAssetData(UEditorAssetLibrary::LoadAsset(NewObjectPath)));
		}
	}
	else
	{
		MovedAssets = Created;
	}

	TArray<TSharedPtr<FJsonValue>> CreatedJson;
	for (const FAssetData& AD : MovedAssets)
		CreatedJson.Add(MakeShared<FJsonValueString>(AD.GetObjectPathString()));
	TArray<TSharedPtr<FJsonValue>> SkippedJson;
	for (const FString& P : SkippedPaths)
		SkippedJson.Add(MakeShared<FJsonValueString>(P));

	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField  (TEXT("success"), Created.Num() > 0);
	Obj->SetNumberField(TEXT("requested"), AssetsToRetarget.Num());
	Obj->SetNumberField(TEXT("created"),   Created.Num());
	Obj->SetArrayField (TEXT("created_assets"), CreatedJson);
	Obj->SetArrayField (TEXT("skipped_inputs"), SkippedJson);
	if (MoveFailures.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> FailArr;
		for (const TPair<FString, FString>& F : MoveFailures)
		{
			TSharedPtr<FJsonObject> FailObj = MakeShared<FJsonObject>();
			FailObj->SetStringField(TEXT("source"), F.Key);
			FailObj->SetStringField(TEXT("error"), F.Value);
			FailArr.Add(MakeShared<FJsonValueObject>(FailObj));
		}
		Obj->SetArrayField(TEXT("move_failures"), FailArr);
	}
	BuildSuccessJson(Obj, OutJsonString);
}



// =====================================================================================================
// Composite "template" tools
//
// Each tool below composes the single-purpose handlers in this translation unit (and, for variables,
// the Blueprint extension's add_variable through the dispatcher) into one end-to-end recipe. Every
// sub-step is recorded in steps[] as {step, ok, error?, result?} so a partial failure is diagnosable,
// and AnimBP-mutating tools finish with exactly one compile report (nested compiles are suppressed).
// =====================================================================================================

namespace CompositeTemplate
{
	static TSharedPtr<FJsonObject> ParseJson(const FString& Json)
	{
		TSharedPtr<FJsonObject> Obj;
		if (Json.IsEmpty()) return nullptr;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
		if (!FJsonSerializer::Deserialize(Reader, Obj) || !Obj.IsValid()) return nullptr;
		return Obj;
	}

	// Ordered log of sub-steps. Failed steps are counted; "skipped" steps are reported but not counted
	// so an optional part that could not be resolved (e.g. a finger chain) doesn't flip the whole tool.
	struct FCompositeStepLog
	{
		TArray<TSharedPtr<FJsonValue>> Steps;
		TArray<FString> FailedNames;
		int32 NumFailed = 0;

		void Add(const FString& Step, bool bOk, const FString& Err, const TSharedPtr<FJsonObject>& Result, bool bSkipped)
		{
			TSharedPtr<FJsonObject> E = MakeShared<FJsonObject>();
			E->SetStringField(TEXT("step"), Step);
			E->SetBoolField(TEXT("ok"), bOk);
			if (bSkipped) E->SetBoolField(TEXT("skipped"), true);
			if (!Err.IsEmpty()) E->SetStringField(TEXT("error"), Err);
			if (Result.IsValid())
			{
				// Nested compiles are suppressed inside composites; the marker is noise in the step log.
				Result->RemoveField(TEXT("compile_deferred"));
				E->SetObjectField(TEXT("result"), Result);
			}
			Steps.Add(MakeShared<FJsonValueObject>(E));
			if (!bOk && !bSkipped) { ++NumFailed; FailedNames.Add(Step); }
		}

		// Records the outcome of a sub-handler call. Returns the parsed result on success, null on failure.
		TSharedPtr<FJsonObject> Record(const FString& Step, const FString& SubJson, const FString& SubErr)
		{
			TSharedPtr<FJsonObject> Result = ParseJson(SubJson);
			FString Err = SubErr;
			bool bOk = Err.IsEmpty();
			if (bOk && Result.IsValid())
			{
				bool bSuccess = true;
				if (Result->TryGetBoolField(TEXT("success"), bSuccess) && !bSuccess)
				{
					bOk = false;
					if (!Result->TryGetStringField(TEXT("error"), Err) || Err.IsEmpty())
						Err = TEXT("sub-step reported success=false");
				}
			}
			Add(Step, bOk, Err, Result, false);
			return bOk ? Result : nullptr;
		}

		void Ok(const FString& Step, const FString& Note = FString())
		{
			TSharedPtr<FJsonObject> R;
			if (!Note.IsEmpty()) { R = MakeShared<FJsonObject>(); R->SetStringField(TEXT("note"), Note); }
			Add(Step, true, FString(), R, false);
		}
		void Fail(const FString& Step, const FString& Err) { Add(Step, false, Err, nullptr, false); }
		void Skip(const FString& Step, const FString& Reason) { Add(Step, false, Reason, nullptr, true); }

		void WriteTo(const TSharedPtr<FJsonObject>& Out) const
		{
			Out->SetArrayField(TEXT("steps"), Steps);
			Out->SetNumberField(TEXT("num_steps"), Steps.Num());
			Out->SetNumberField(TEXT("num_failed_steps"), NumFailed);
			Out->SetBoolField(TEXT("all_steps_ok"), NumFailed == 0);
			if (NumFailed > 0) Out->SetStringField(TEXT("failed_steps"), FString::Join(FailedNames, TEXT(", ")));
		}
	};

	static TArray<TSharedPtr<FJsonValue>> ToJsonStringArray(const TArray<FString>& In)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		for (const FString& S : In) Out.Add(MakeShared<FJsonValueString>(S));
		return Out;
	}

	static TArray<FString> ReadStringArray(const TSharedPtr<FJsonObject>& Args, const TCHAR* Field)
	{
		TArray<FString> Out;
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (Args.IsValid() && Args->TryGetArrayField(Field, Arr) && Arr)
		{
			for (const TSharedPtr<FJsonValue>& V : *Arr)
			{
				FString S;
				if (V.IsValid() && V->TryGetString(S) && !S.IsEmpty()) Out.Add(S);
			}
		}
		return Out;
	}

	static FString TrimFolder(FString Folder)
	{
		while (Folder.EndsWith(TEXT("/"))) Folder.LeftChopInline(1);
		return Folder;
	}

	static FString AssetFolderOf(const UObject* Obj)
	{
		return Obj ? FPackageName::GetLongPackagePath(Obj->GetOutermost()->GetName()) : FString();
	}

	// ---------------------------------------------------------------------------------------------
	// Bone-name heuristics shared by retarget_setup, setup_motion_matching and setup_foot_ik.
	// ---------------------------------------------------------------------------------------------
	enum class EHeuristicSide : uint8 { None, Left, Right };

	struct FHeuristicBone
	{
		FName Name;
		FString Lower;     // full lowercase name (namespace prefix like "mixamorig:" stripped)
		FString Core;      // lowercase name with the side token removed ("upperarm_l" -> "upperarm")
		FString CoreBase;  // Core with trailing digits/separators removed ("spine_01" -> "spine")
		EHeuristicSide Side = EHeuristicSide::None;
		int32 Index = INDEX_NONE;
		int32 Parent = INDEX_NONE;
	};

	static FString TrimSeparators(FString S)
	{
		auto IsSep = [](TCHAR C) { return C == TEXT('_') || C == TEXT('.') || C == TEXT('-') || C == TEXT(' '); };
		while (S.Len() > 0 && IsSep(S[0])) S.RightChopInline(1);
		while (S.Len() > 0 && IsSep(S[S.Len() - 1])) S.LeftChopInline(1);
		return S;
	}

	static void ParseBoneSide(const FString& InLower, EHeuristicSide& OutSide, FString& OutCore)
	{
		const FString& N = InLower;
		OutSide = EHeuristicSide::None;
		OutCore = N;
		if (N.StartsWith(TEXT("left")))                                       { OutSide = EHeuristicSide::Left;  OutCore = TrimSeparators(N.Mid(4)); }
		else if (N.StartsWith(TEXT("right")))                                 { OutSide = EHeuristicSide::Right; OutCore = TrimSeparators(N.Mid(5)); }
		else if (N.StartsWith(TEXT("l_")) || N.StartsWith(TEXT("l.")))       { OutSide = EHeuristicSide::Left;  OutCore = TrimSeparators(N.Mid(2)); }
		else if (N.StartsWith(TEXT("r_")) || N.StartsWith(TEXT("r.")))       { OutSide = EHeuristicSide::Right; OutCore = TrimSeparators(N.Mid(2)); }
		else if (N.EndsWith(TEXT("_left")) || N.EndsWith(TEXT(".left")))     { OutSide = EHeuristicSide::Left;  OutCore = TrimSeparators(N.LeftChop(5)); }
		else if (N.EndsWith(TEXT("_right")) || N.EndsWith(TEXT(".right")))   { OutSide = EHeuristicSide::Right; OutCore = TrimSeparators(N.LeftChop(6)); }
		else if (N.EndsWith(TEXT("_l")) || N.EndsWith(TEXT(".l")) || N.EndsWith(TEXT("-l"))) { OutSide = EHeuristicSide::Left;  OutCore = TrimSeparators(N.LeftChop(2)); }
		else if (N.EndsWith(TEXT("_r")) || N.EndsWith(TEXT(".r")) || N.EndsWith(TEXT("-r"))) { OutSide = EHeuristicSide::Right; OutCore = TrimSeparators(N.LeftChop(2)); }
		if (OutCore.IsEmpty()) { OutSide = EHeuristicSide::None; OutCore = N; }
	}

	static FString StripTrailingDigits(FString S)
	{
		while (S.Len() > 0 && (FChar::IsDigit(S[S.Len() - 1]) || S[S.Len() - 1] == TEXT('_') || S[S.Len() - 1] == TEXT('.')))
			S.LeftChopInline(1);
		return S;
	}

	static void BuildBoneInfos(const FReferenceSkeleton& RefSkel, TArray<FHeuristicBone>& OutBones)
	{
		OutBones.Reset();
		const int32 Num = RefSkel.GetNum();
		for (int32 i = 0; i < Num; ++i)
		{
			FHeuristicBone B;
			B.Name = RefSkel.GetBoneName(i);
			B.Index = i;
			B.Parent = RefSkel.GetParentIndex(i);
			B.Lower = B.Name.ToString().ToLower();
			int32 Colon = INDEX_NONE;
			if (B.Lower.FindLastChar(TEXT(':'), Colon)) B.Lower = B.Lower.Mid(Colon + 1);
			ParseBoneSide(B.Lower, B.Side, B.Core);
			B.CoreBase = StripTrailingDigits(B.Core);
			if (B.CoreBase.IsEmpty()) B.CoreBase = B.Core;
			OutBones.Add(B);
		}
	}

	// Helper/utility bones that must never be picked as chain endpoints.
	static bool IsHelperBone(const FHeuristicBone& B)
	{
		const FString& L = B.Lower;
		return L.StartsWith(TEXT("ik_")) || L.Contains(TEXT("_ik")) || L.StartsWith(TEXT("vb ")) || L.StartsWith(TEXT("vb_"))
			|| L.Contains(TEXT("twist")) || L.Contains(TEXT("roll")) || L.Contains(TEXT("_end")) || L.EndsWith(TEXT("end"))
			|| L.Contains(TEXT("nub")) || L.Contains(TEXT("weapon")) || L.Contains(TEXT("attach")) || L.Contains(TEXT("socket"))
			|| L.Contains(TEXT("_null")) || L.Contains(TEXT("gun"));
	}

	static bool IsDescendantOf(const TArray<FHeuristicBone>& Bones, int32 Bone, int32 Ancestor, int32* OutDepth = nullptr)
	{
		int32 Depth = 0;
		int32 Cur = Bones.IsValidIndex(Bone) ? Bones[Bone].Parent : INDEX_NONE;
		while (Cur != INDEX_NONE)
		{
			++Depth;
			if (Cur == Ancestor) { if (OutDepth) *OutDepth = Depth; return true; }
			Cur = Bones[Cur].Parent;
		}
		return false;
	}

	// Exact CoreBase match over the candidates in priority order, then a Contains() fallback (with
	// per-call exclusions), always honouring the requested side.
	static int32 FindBone(const TArray<FHeuristicBone>& Bones, const TArray<FString>& Exact, const TArray<FString>& Contains,
		EHeuristicSide Side, const TArray<FString>& Exclude = {})
	{
		auto Allowed = [&](const FHeuristicBone& B)
		{
			if (B.Side != Side || IsHelperBone(B)) return false;
			for (const FString& X : Exclude) if (B.Core.Contains(X)) return false;
			return true;
		};
		for (const FString& C : Exact)
			for (const FHeuristicBone& B : Bones)
				if (Allowed(B) && (B.CoreBase == C || B.Core == C)) return B.Index;
		for (const FString& C : Contains)
			for (const FHeuristicBone& B : Bones)
				if (Allowed(B) && B.Core.Contains(C)) return B.Index;
		return INDEX_NONE;
	}

	// Deepest descendant of Start (inclusive) whose Core contains one of Keywords; used to find the
	// end of a spine/neck/finger chain regardless of how many links the rig has.
	static int32 DeepestDescendantMatching(const TArray<FHeuristicBone>& Bones, int32 Start, const TArray<FString>& Keywords)
	{
		int32 Best = Start, BestDepth = 0;
		for (const FHeuristicBone& B : Bones)
		{
			if (B.Index == Start || IsHelperBone(B) || B.Side != Bones[Start].Side) continue;
			bool bMatch = false;
			for (const FString& K : Keywords) if (B.Core.Contains(K)) { bMatch = true; break; }
			if (!bMatch) continue;
			int32 Depth = 0;
			if (IsDescendantOf(Bones, B.Index, Start, &Depth) && Depth > BestDepth) { Best = B.Index; BestDepth = Depth; }
		}
		return Best;
	}

	static int32 FindPelvisBone(const TArray<FHeuristicBone>& Bones)
	{
		return FindBone(Bones, { TEXT("pelvis"), TEXT("hips"), TEXT("hip"), TEXT("cog") }, { TEXT("pelvis"), TEXT("hips") }, EHeuristicSide::None);
	}

	static int32 FindFootBone(const TArray<FHeuristicBone>& Bones, EHeuristicSide Side)
	{
		return FindBone(Bones, { TEXT("foot"), TEXT("ankle") }, { TEXT("foot") }, Side, { TEXT("ball"), TEXT("toe") });
	}

	static FString BoneNameOr(const TArray<FHeuristicBone>& Bones, int32 Index, const FString& Fallback = FString())
	{
		return Bones.IsValidIndex(Index) ? Bones[Index].Name.ToString() : Fallback;
	}

	// Resolves a SkeletalMesh from a mesh path, or from a Skeleton path via its preview mesh / any mesh bound to it.
	static USkeletalMesh* ResolveSkeletalMesh(const FString& Path, FString& OutError)
	{
		UObject* Obj = Path.IsEmpty() ? nullptr : UEditorAssetLibrary::LoadAsset(Path);
		if (!Obj) { OutError = FString::Printf(TEXT("Could not load asset: %s"), *Path); return nullptr; }
		if (USkeletalMesh* Mesh = Cast<USkeletalMesh>(Obj)) return Mesh;
		USkeleton* Skeleton = Cast<USkeleton>(Obj);
		if (!Skeleton) { OutError = FString::Printf(TEXT("'%s' is neither a SkeletalMesh nor a Skeleton."), *Path); return nullptr; }
		if (USkeletalMesh* Preview = Skeleton->GetPreviewMesh()) return Preview;
		FAssetRegistryModule& AR = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		TArray<FAssetData> MeshAssets;
		AR.Get().GetAssetsByClass(USkeletalMesh::StaticClass()->GetClassPathName(), MeshAssets);
		for (const FAssetData& AD : MeshAssets)
		{
			USkeletalMesh* Candidate = Cast<USkeletalMesh>(AD.GetAsset());
			if (Candidate && Candidate->GetSkeleton() == Skeleton) return Candidate;
		}
		OutError = FString::Printf(TEXT("Skeleton '%s' has no preview mesh and no SkeletalMesh references it; pass a SkeletalMesh path."), *Path);
		return nullptr;
	}

	struct FHeuristicChain
	{
		FString Name;
		int32 Start = INDEX_NONE;
		int32 End = INDEX_NONE;
		FString Reason; // filled when the chain could not be resolved
	};

	// Standard humanoid retarget chains (Epic naming: Root, Spine, Neck, Head, Left/Right Clavicle|Arm|Leg,
	// optional fingers) resolved from bone-name heuristics.
	static TArray<FHeuristicChain> BuildHumanoidChains(const TArray<FHeuristicBone>& Bones, bool bIncludeFingers)
	{
		TArray<FHeuristicChain> Out;
		auto Push = [&](const FString& Name, int32 Start, int32 End, const FString& Reason)
		{
			FHeuristicChain D; D.Name = Name; D.Start = Start; D.End = End; D.Reason = Reason;
			if ((Start == INDEX_NONE || End == INDEX_NONE) && D.Reason.IsEmpty()) D.Reason = TEXT("no bone matched the naming heuristics");
			Out.Add(D);
		};

		if (Bones.Num() > 0 && Bones[0].Lower.Contains(TEXT("root"))) Push(TEXT("Root"), 0, 0, FString());

		{
			const int32 Spine = FindBone(Bones, { TEXT("spine") }, { TEXT("spine") }, EHeuristicSide::None);
			Push(TEXT("Spine"), Spine, Spine == INDEX_NONE ? INDEX_NONE : DeepestDescendantMatching(Bones, Spine, { TEXT("spine"), TEXT("chest") }), FString());
		}
		{
			const int32 Neck = FindBone(Bones, { TEXT("neck") }, { TEXT("neck") }, EHeuristicSide::None);
			Push(TEXT("Neck"), Neck, Neck == INDEX_NONE ? INDEX_NONE : DeepestDescendantMatching(Bones, Neck, { TEXT("neck") }), FString());
		}
		{
			const int32 Head = FindBone(Bones, { TEXT("head") }, { TEXT("head") }, EHeuristicSide::None, { TEXT("top") });
			Push(TEXT("Head"), Head, Head, FString());
		}

		for (EHeuristicSide Side : { EHeuristicSide::Left, EHeuristicSide::Right })
		{
			const FString Prefix = Side == EHeuristicSide::Left ? TEXT("Left") : TEXT("Right");

			const int32 Clav = FindBone(Bones, { TEXT("clavicle"), TEXT("shoulder"), TEXT("collar"), TEXT("collarbone") }, { TEXT("clavicle"), TEXT("collar") }, Side, { TEXT("arm") });
			Push(Prefix + TEXT("Clavicle"), Clav, Clav, FString());

			const int32 UpperArm = FindBone(Bones, { TEXT("upperarm"), TEXT("upper_arm"), TEXT("uparm"), TEXT("humerus"), TEXT("arm") },
				{ TEXT("upperarm"), TEXT("upper_arm"), TEXT("uparm"), TEXT("humerus") }, Side, { TEXT("fore"), TEXT("lower") });
			const int32 Hand = FindBone(Bones, { TEXT("hand"), TEXT("wrist") }, { TEXT("hand") }, Side,
				{ TEXT("thumb"), TEXT("index"), TEXT("middle"), TEXT("ring"), TEXT("pinky") });
			if (UpperArm != INDEX_NONE && Hand != INDEX_NONE && !IsDescendantOf(Bones, Hand, UpperArm))
				Push(Prefix + TEXT("Arm"), INDEX_NONE, INDEX_NONE, FString::Printf(TEXT("'%s' is not a descendant of '%s'"), *BoneNameOr(Bones, Hand), *BoneNameOr(Bones, UpperArm)));
			else
				Push(Prefix + TEXT("Arm"), UpperArm, Hand, FString());

			const int32 Thigh = FindBone(Bones, { TEXT("thigh"), TEXT("upleg"), TEXT("upperleg"), TEXT("upper_leg"), TEXT("femur"), TEXT("leg"), TEXT("hip") },
				{ TEXT("thigh"), TEXT("upleg"), TEXT("upperleg"), TEXT("femur") }, Side, { TEXT("lower"), TEXT("calf"), TEXT("shin") });
			const int32 Foot = FindFootBone(Bones, Side);
			if (Thigh != INDEX_NONE && Foot != INDEX_NONE && !IsDescendantOf(Bones, Foot, Thigh))
				Push(Prefix + TEXT("Leg"), INDEX_NONE, INDEX_NONE, FString::Printf(TEXT("'%s' is not a descendant of '%s'"), *BoneNameOr(Bones, Foot), *BoneNameOr(Bones, Thigh)));
			else
				Push(Prefix + TEXT("Leg"), Thigh, Foot, FString());

			if (bIncludeFingers)
			{
				struct FFingerSpec { const TCHAR* Name; TArray<FString> Keys; };
				const FFingerSpec Fingers[] = {
					{ TEXT("Thumb"),  { TEXT("thumb") } },
					{ TEXT("Index"),  { TEXT("index") } },
					{ TEXT("Middle"), { TEXT("middle") } },
					{ TEXT("Ring"),   { TEXT("ring") } },
					{ TEXT("Pinky"),  { TEXT("pinky"), TEXT("little") } },
				};
				for (const FFingerSpec& F : Fingers)
				{
					int32 Start = INDEX_NONE;
					for (const FHeuristicBone& B : Bones)
					{
						if (B.Side != Side || IsHelperBone(B)) continue;
						bool bMatch = false;
						for (const FString& K : F.Keys) if (B.Core.Contains(K)) { bMatch = true; break; }
						if (bMatch && (Start == INDEX_NONE || B.Index < Start)) Start = B.Index;
					}
					Push(Prefix + F.Name, Start, Start == INDEX_NONE ? INDEX_NONE : DeepestDescendantMatching(Bones, Start, F.Keys), FString());
				}
			}
		}
		return Out;
	}

	// ---------------------------------------------------------------------------------------------
	// AnimBP variable helpers
	// ---------------------------------------------------------------------------------------------
	static FProperty* FindBpVariableProperty(UAnimBlueprint* AnimBP, const FString& VarName)
	{
		UClass* Classes[2] = { AnimBP->SkeletonGeneratedClass, AnimBP->GeneratedClass };
		for (UClass* C : Classes)
		{
			if (!C) continue;
			for (TFieldIterator<FProperty> It(C, EFieldIteratorFlags::IncludeSuper); It; ++It)
				if (It->GetName().Equals(VarName, ESearchCase::IgnoreCase)) return *It;
		}
		return nullptr;
	}

	static bool BpVariableExists(UAnimBlueprint* AnimBP, const FString& VarName)
	{
		if (FindBpVariableProperty(AnimBP, VarName)) return true;
		for (const FBPVariableDescription& V : AnimBP->NewVariables)
			if (V.VarName.ToString().Equals(VarName, ESearchCase::IgnoreCase)) return true;
		return false;
	}

	static bool MakePinTypeForKey(const FString& TypeKey, FEdGraphPinType& Out)
	{
		const FString K = TypeKey.ToLower();
		Out = FEdGraphPinType();
		if (K == TEXT("bool") || K == TEXT("boolean"))      { Out.PinCategory = UEdGraphSchema_K2::PC_Boolean; return true; }
		if (K == TEXT("float") || K == TEXT("double") || K == TEXT("real")) { Out.PinCategory = UEdGraphSchema_K2::PC_Real; Out.PinSubCategory = UEdGraphSchema_K2::PC_Double; return true; }
		if (K == TEXT("int") || K == TEXT("integer"))       { Out.PinCategory = UEdGraphSchema_K2::PC_Int; return true; }
		if (K == TEXT("vector"))   { Out.PinCategory = UEdGraphSchema_K2::PC_Struct; Out.PinSubCategoryObject = TBaseStructure<FVector>::Get(); return true; }
		if (K == TEXT("rotator"))  { Out.PinCategory = UEdGraphSchema_K2::PC_Struct; Out.PinSubCategoryObject = TBaseStructure<FRotator>::Get(); return true; }
		if (K == TEXT("transform")){ Out.PinCategory = UEdGraphSchema_K2::PC_Struct; Out.PinSubCategoryObject = TBaseStructure<FTransform>::Get(); return true; }
		return false;
	}

	// Creates a member variable on the AnimBP if it doesn't exist. Prefers the Blueprint extension's
	// add_variable tool (same validation path the model uses); falls back to a direct
	// FBlueprintEditorUtils::AddMemberVariable when that extension isn't loaded.
	static void EnsureAnimBpVariable(UAnimBlueprint* AnimBP, const FString& AnimBPPath, const FString& VarName,
		const TCHAR* TypeKey, const FString& DefaultValue, FCompositeStepLog& Log, bool& bOutAdded)
	{
		const FString Step = FString::Printf(TEXT("variable %s (%s)"), *VarName, TypeKey);
		if (BpVariableExists(AnimBP, VarName)) { Log.Ok(Step, TEXT("already exists")); return; }

		FString DispatcherError;
		if (IUECPCoreModule::IsAvailable())
		{
			IUECPToolDispatcher& D = IUECPCoreModule::Get().GetToolDispatcher();
			if (D.IsRegistered(TEXT("add_variable")))
			{
				TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>();
				A->SetStringField(TEXT("blueprint_path"), AnimBPPath);
				A->SetStringField(TEXT("variable_name"), VarName);
				A->SetStringField(TEXT("variable_type"), TypeKey);
				if (!DefaultValue.IsEmpty()) A->SetStringField(TEXT("default_value"), DefaultValue);
				const FUECPToolResult R = D.ExecuteFromArgs(TEXT("add_variable"), A);
				if (R.bSuccess && BpVariableExists(AnimBP, VarName))
				{
					bOutAdded = true;
					Log.Record(Step + TEXT(" [add_variable]"), R.ResultJson, FString());
					return;
				}
				DispatcherError = R.ErrorMessage.IsEmpty() ? TEXT("add_variable returned success but the variable is still missing") : R.ErrorMessage;
			}
		}

		FEdGraphPinType PinType;
		if (!MakePinTypeForKey(TypeKey, PinType))
		{
			Log.Fail(Step, FString::Printf(TEXT("Unsupported variable type '%s'."), TypeKey));
			return;
		}
		const bool bAdded = FBlueprintEditorUtils::AddMemberVariable(AnimBP, FName(*VarName), PinType, DefaultValue);
		if (!bAdded)
		{
			Log.Fail(Step, DispatcherError.IsEmpty()
				? TEXT("FBlueprintEditorUtils::AddMemberVariable returned false (name collision?)")
				: FString::Printf(TEXT("add_variable failed (%s) and direct AddMemberVariable also returned false"), *DispatcherError));
			return;
		}
		bOutAdded = true;
		Log.Ok(Step + TEXT(" [direct]"), DispatcherError.IsEmpty() ? TEXT("created") : FString::Printf(TEXT("created directly; add_variable had failed: %s"), *DispatcherError));
	}

	// Silent compile so freshly-added variables land on the skeleton class before graph wiring resolves them.
	static void SilentCompile(UAnimBlueprint* AnimBP)
	{
		if (!AnimBP) return;
		FCompilerResultsLog Results;
		Results.bSilentMode = true;
		FKismetEditorUtilities::CompileBlueprint(AnimBP, EBlueprintCompileOptions::SkipGarbageCollection, &Results);
	}

	// Exposes an optional-property pin on an AnimGraph node (if it is hidden) and wires a variable getter into it.
	static bool BindAnimNodePinToVariable(UAnimBlueprint* AnimBP, UAnimGraphNode_Base* Node, const FString& PinPropertyName,
		const FString& VarName, int32 GetterYOffset, FString& OutError)
	{
		if (!AnimBP || !Node) { OutError = TEXT("null node"); return false; }
		UEdGraph* Graph = Node->GetGraph();
		if (!Graph) { OutError = TEXT("node has no graph"); return false; }

		UEdGraphPin* Pin = Node->FindPin(FName(*PinPropertyName));
		if (!Pin)
		{
			// ShowPinForProperties is protected; read it through reflection to find the optional-pin index.
			if (FArrayProperty* ShowPinsProp = FindFProperty<FArrayProperty>(UAnimGraphNode_Base::StaticClass(), TEXT("ShowPinForProperties")))
			{
				FScriptArrayHelper Helper(ShowPinsProp, ShowPinsProp->ContainerPtrToValuePtr<void>(Node));
				for (int32 i = 0; i < Helper.Num(); ++i)
				{
					const FOptionalPinFromProperty* Opt = reinterpret_cast<const FOptionalPinFromProperty*>(Helper.GetRawPtr(i));
					if (Opt && Opt->PropertyName.ToString().Equals(PinPropertyName, ESearchCase::IgnoreCase))
					{
						Node->SetPinVisibility(true, i);
						break;
					}
				}
			}
			Pin = Node->FindPin(FName(*PinPropertyName));
		}
		if (!Pin)
		{
			OutError = FString::Printf(TEXT("pin '%s' not found on node and could not be exposed"), *PinPropertyName);
			return false;
		}

		FProperty* Prop = FindBpVariableProperty(AnimBP, VarName);
		if (!Prop)
		{
			OutError = FString::Printf(TEXT("variable '%s' not found on the AnimBP (skeleton class)"), *VarName);
			return false;
		}

		UK2Node_VariableGet* VarNode = NewObject<UK2Node_VariableGet>(Graph);
		VarNode->CreateNewGuid();
		Graph->AddNode(VarNode, true, false);
		VarNode->PostPlacedNewNode();
		VarNode->AllocateDefaultPins();
		VarNode->VariableReference.SetFromField<FProperty>(Prop, true);
		VarNode->ReconstructNode();
		VarNode->NodePosX = Node->NodePosX - 260;
		VarNode->NodePosY = Node->NodePosY + GetterYOffset;

		UEdGraphPin* VarOut = VarNode->GetValuePin();
		if (!VarOut || !Graph->GetSchema()->TryCreateConnection(VarOut, Pin))
		{
			Graph->RemoveNode(VarNode);
			OutError = FString::Printf(TEXT("schema rejected connecting variable '%s' to pin '%s' (type mismatch?)"), *VarName, *PinPropertyName);
			return false;
		}
		return true;
	}

	static UAnimGraphNode_Root* FindOutputPoseNode(UAnimationGraph* AnimGraph)
	{
		if (!AnimGraph) return nullptr;
		TArray<UAnimGraphNode_Root*> Roots;
		AnimGraph->GetNodesOfClass<UAnimGraphNode_Root>(Roots);
		for (UAnimGraphNode_Root* R : Roots)
			if (R && R->GetClass() == UAnimGraphNode_Root::StaticClass()) return R;
		return nullptr;
	}
}

// -----------------------------------------------------------------------------------------------------
// create_locomotion_state_machine
// -----------------------------------------------------------------------------------------------------
void HandleCreateLocomotionStateMachineFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	using namespace CompositeTemplate;
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }

	const FString AnimBPPath = GetAnimBpPathArg(Args);
	FString IdleAnim, WalkRunBS, WalkAnim, RunAnim, JumpStartAnim, JumpLoopAnim, JumpLandAnim;
	Args->TryGetStringField(TEXT("idle_anim"), IdleAnim);
	Args->TryGetStringField(TEXT("walk_run_blendspace"), WalkRunBS);
	Args->TryGetStringField(TEXT("walk_anim"), WalkAnim);
	Args->TryGetStringField(TEXT("run_anim"), RunAnim);
	Args->TryGetStringField(TEXT("jump_start"), JumpStartAnim);
	Args->TryGetStringField(TEXT("jump_loop"), JumpLoopAnim);
	Args->TryGetStringField(TEXT("jump_land"), JumpLandAnim);

	FString SpeedVar, AirVar, SMName;
	Args->TryGetStringField(TEXT("speed_variable"), SpeedVar);
	Args->TryGetStringField(TEXT("is_in_air_variable"), AirVar);
	Args->TryGetStringField(TEXT("state_machine_name"), SMName);
	if (SpeedVar.IsEmpty()) SpeedVar = TEXT("Speed");
	if (AirVar.IsEmpty())   AirVar   = TEXT("IsInAir");
	if (SMName.IsEmpty())   SMName   = TEXT("Locomotion");

	bool bWire = true; Args->TryGetBoolField(TEXT("wire_to_output"), bWire);
	double SpeedThreshold = 3.0, WalkSpeed = 150.0, RunSpeed = 500.0, Crossfade = 0.2;
	Args->TryGetNumberField(TEXT("speed_threshold"), SpeedThreshold);
	Args->TryGetNumberField(TEXT("walk_speed"), WalkSpeed);
	Args->TryGetNumberField(TEXT("run_speed"), RunSpeed);
	Args->TryGetNumberField(TEXT("crossfade_duration"), Crossfade);

	if (AnimBPPath.IsEmpty()) { SetError(TEXT("anim_blueprint_path is required"), OutJsonString, OutError); return; }
	if (IdleAnim.IsEmpty())   { SetError(TEXT("idle_anim is required"), OutJsonString, OutError); return; }
	if (WalkRunBS.IsEmpty() && (WalkAnim.IsEmpty() || RunAnim.IsEmpty()))
	{
		SetError(TEXT("Provide walk_run_blendspace, or both walk_anim and run_anim (a 1D BlendSpace is then generated next to the AnimBP)."), OutJsonString, OutError);
		return;
	}

	UAnimBlueprint* AnimBP = Cast<UAnimBlueprint>(UEditorAssetLibrary::LoadAsset(AnimBPPath));
	if (!AnimBP) { SetError(FString::Printf(TEXT("Could not load AnimBlueprint: %s"), *AnimBPPath), OutJsonString, OutError); return; }

	FCompositeStepLog Log;
	TArray<FString> Notes;
	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	TUniquePtr<FScopedSuppressAnimCompile> Suppress = MakeUnique<FScopedSuppressAnimCompile>();

	const FString StIdle = TEXT("Idle"), StWalkRun = TEXT("WalkRun"), StJumpStart = TEXT("JumpStart"), StJumpLoop = TEXT("JumpLoop"), StJumpLand = TEXT("JumpLand");

	auto Finish = [&](bool bSuccess, const FString& Err)
	{
		Suppress.Reset();
		Obj->SetBoolField(TEXT("success"), bSuccess);
		if (!bSuccess) Obj->SetStringField(TEXT("error"), Err);
		Obj->SetStringField(TEXT("anim_blueprint_path"), AnimBPPath);
		Obj->SetStringField(TEXT("state_machine_name"), SMName);
		Obj->SetStringField(TEXT("speed_variable"), SpeedVar);
		Obj->SetStringField(TEXT("is_in_air_variable"), AirVar);
		if (Notes.Num() > 0) Obj->SetArrayField(TEXT("notes"), ToJsonStringArray(Notes));
		Log.WriteTo(Obj);
		CompileAnimBlueprintAndReport(AnimBP, Obj);
		UEditorAssetLibrary::SaveAsset(AnimBPPath, false);
		BuildSuccessJson(Obj, OutJsonString);
		if (!bSuccess) OutError = Err;
	};

	// 1. Variables the rules/players bind to.
	bool bAddedVar = false;
	EnsureAnimBpVariable(AnimBP, AnimBPPath, SpeedVar, TEXT("float"), TEXT("0.0"), Log, bAddedVar);
	EnsureAnimBpVariable(AnimBP, AnimBPPath, AirVar, TEXT("bool"), TEXT("false"), Log, bAddedVar);
	if (bAddedVar) SilentCompile(AnimBP);

	// 2. State machine (reused when one with this name already exists).
	if (FindStateMachineGraph(AnimBP, SMName, nullptr))
	{
		Log.Ok(TEXT("state_machine"), FString::Printf(TEXT("'%s' already exists; states are added to it"), *SMName));
		Notes.Add(FString::Printf(TEXT("State machine '%s' already existed and was reused."), *SMName));
	}
	else
	{
		FString J, E;
		HandleAddStateMachine(AnimBPPath, SMName, -400, 0, J, E);
		TSharedPtr<FJsonObject> R = Log.Record(TEXT("state_machine"), J, E);
		if (!R.IsValid()) { Finish(false, TEXT("Could not create the state machine; see steps[].")); return; }
		FString Resolved; if (R->TryGetStringField(TEXT("state_machine_name"), Resolved) && !Resolved.IsEmpty()) SMName = Resolved;
	}

	// 3. Walk/run source: given BlendSpace, or a generated 1D BlendSpace from walk_anim + run_anim.
	FString WalkRunAsset = WalkRunBS;
	if (WalkRunAsset.IsEmpty())
	{
		USkeleton* Skel = AnimBP->TargetSkeleton;
		if (!Skel) Log.Fail(TEXT("blendspace create"), TEXT("AnimBP has no TargetSkeleton; cannot generate the walk/run BlendSpace."));
		else
		{
			const FString BSName = FString::Printf(TEXT("BS_%s_WalkRun"), *SMName);
			FString J, E;
			HandleCreateBlendspace(BSName, AssetFolderOf(AnimBP), Skel->GetPathName(), true, SpeedVar, (float)WalkSpeed, (float)RunSpeed, J, E);
			TSharedPtr<FJsonObject> R = Log.Record(TEXT("blendspace create"), J, E);
			if (R.IsValid())
			{
				R->TryGetStringField(TEXT("asset_path"), WalkRunAsset);
				HandleAddBlendspaceSample(WalkRunAsset, WalkAnim, (float)WalkSpeed, 0.f, 0.f, J, E);
				Log.Record(TEXT("blendspace sample walk"), J, E);
				HandleAddBlendspaceSample(WalkRunAsset, RunAnim, (float)RunSpeed, 0.f, 0.f, J, E);
				Log.Record(TEXT("blendspace sample run"), J, E);
				Obj->SetStringField(TEXT("generated_blendspace"), WalkRunAsset);
				Notes.Add(FString::Printf(TEXT("Generated 1D BlendSpace '%s' (axis '%s' %.0f..%.0f, samples walk@%.0f run@%.0f)."), *WalkRunAsset, *SpeedVar, WalkSpeed, RunSpeed, WalkSpeed, RunSpeed));
			}
		}
	}

	// 4. States.
	auto AddState = [&](const FString& StateName, const FString& Anim, int32 X, int32 Y, const FString& BlendVarX) -> bool
	{
		FString J, E;
		HandleAddAnimState(AnimBPPath, SMName, StateName, Anim, X, Y, J, E, BlendVarX, FString());
		TSharedPtr<FJsonObject> R = Log.Record(FString::Printf(TEXT("state %s"), *StateName), J, E);
		if (R.IsValid())
		{
			const TArray<TSharedPtr<FJsonValue>>* Unresolved = nullptr;
			if (R->TryGetArrayField(TEXT("unresolved_variables"), Unresolved) && Unresolved && Unresolved->Num() > 0)
				Notes.Add(FString::Printf(TEXT("State '%s': BlendSpace X pin could not be bound to '%s'; bind it manually in the state graph."), *StateName, *BlendVarX));
		}
		return R.IsValid();
	};

	const bool bIdle    = AddState(StIdle, IdleAnim, 0, 0, FString());
	const bool bWalkRun = !WalkRunAsset.IsEmpty() && AddState(StWalkRun, WalkRunAsset, 450, 0, SpeedVar);
	if (WalkRunAsset.IsEmpty()) Log.Skip(TEXT("state WalkRun"), TEXT("no walk/run BlendSpace available"));

	TArray<FString> JumpChain;
	if (!JumpStartAnim.IsEmpty() && AddState(StJumpStart, JumpStartAnim, 0, 320, FString()))   JumpChain.Add(StJumpStart);
	if (!JumpLoopAnim.IsEmpty()  && AddState(StJumpLoop,  JumpLoopAnim,  450, 320, FString())) JumpChain.Add(StJumpLoop);
	if (!JumpLandAnim.IsEmpty()  && AddState(StJumpLand,  JumpLandAnim,  900, 320, FString())) JumpChain.Add(StJumpLand);

	if (!bIdle) { Finish(false, TEXT("Idle state could not be created; see steps[].")); return; }

	// 5. Transitions + rules.
	auto AddTransition = [&](const FString& From, const FString& To) -> bool
	{
		FString J, E;
		HandleAddStateTransition(AnimBPPath, SMName, From, To, false, (float)Crossfade, J, E);
		return Log.Record(FString::Printf(TEXT("transition %s -> %s"), *From, *To), J, E).IsValid();
	};
	auto SetRule = [&](const FString& From, const FString& To, const FString& RuleType, const FString& Var, const FString& Op, float Value, const FString& Desc)
	{
		FString J, E;
		HandleSetTransitionRule(AnimBPPath, SMName, From, To, RuleType, Var, Op, Value, J, E);
		Log.Record(FString::Printf(TEXT("rule %s -> %s (%s)"), *From, *To, *Desc), J, E);
	};
	auto SetAutomatic = [&](const FString& From, const FString& To)
	{
		TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>();
		A->SetStringField(TEXT("anim_blueprint_path"), AnimBPPath);
		A->SetStringField(TEXT("state_machine_name"), SMName);
		A->SetStringField(TEXT("from_state"), From);
		A->SetStringField(TEXT("to_state"), To);
		A->SetBoolField(TEXT("automatic_rule_based_on_sequence_player"), true);
		FString J, E;
		HandleSetTransitionOptionsFromArgs(A, J, E);
		Log.Record(FString::Printf(TEXT("rule %s -> %s (automatic: player time remaining)"), *From, *To), J, E);
	};
	auto SetPriority = [&](const FString& From, const FString& To, int32 Priority)
	{
		FString J, E;
		HandleSetTransitionPriority(AnimBPPath, SMName, From, To, Priority, J, E);
		Log.Record(FString::Printf(TEXT("priority %s -> %s = %d"), *From, *To, Priority), J, E);
	};

	if (bWalkRun)
	{
		if (AddTransition(StIdle, StWalkRun))    SetRule(StIdle, StWalkRun, TEXT("float_compare"), SpeedVar, TEXT(">"),  (float)SpeedThreshold, FString::Printf(TEXT("%s > %.2f"), *SpeedVar, SpeedThreshold));
		if (AddTransition(StWalkRun, StIdle))    SetRule(StWalkRun, StIdle, TEXT("float_compare"), SpeedVar, TEXT("<="), (float)SpeedThreshold, FString::Printf(TEXT("%s <= %.2f"), *SpeedVar, SpeedThreshold));
	}

	if (JumpChain.Num() > 0)
	{
		const FString& FirstJump = JumpChain[0];
		TArray<FString> Grounded = { StIdle };
		if (bWalkRun) Grounded.Add(StWalkRun);
		for (const FString& Src : Grounded)
		{
			if (AddTransition(Src, FirstJump))
			{
				SetRule(Src, FirstJump, TEXT("bool_variable"), AirVar, FString(), 0.f, AirVar);
				SetPriority(Src, FirstJump, 0); // jumping wins over the idle<->walk/run rules
			}
		}
		for (int32 i = 0; i < JumpChain.Num(); ++i)
		{
			const FString& S = JumpChain[i];
			const FString Next = (i + 1 < JumpChain.Num()) ? JumpChain[i + 1] : StIdle;
			if (!AddTransition(S, Next)) continue;
			if (S == StJumpLoop)              SetRule(S, Next, TEXT("not_bool_variable"), AirVar, FString(), 0.f, FString(TEXT("!")) + AirVar);
			else if (S == StJumpLand)         SetAutomatic(S, Next);
			else if (Next != StIdle)          SetAutomatic(S, Next);                                   // JumpStart -> JumpLoop/JumpLand
			else                              SetRule(S, Next, TEXT("not_bool_variable"), AirVar, FString(), 0.f, FString(TEXT("!")) + AirVar); // JumpStart only
		}
	}
	else
	{
		Log.Skip(TEXT("jump states"), TEXT("no jump_start/jump_loop/jump_land provided"));
	}

	// 6. Entry state + wiring.
	{
		FString J, E;
		HandleSetStateMachineEntryState(AnimBPPath, SMName, StIdle, J, E);
		Log.Record(TEXT("entry state Idle"), J, E);
	}
	if (bWire)
	{
		FString J, E;
		HandleWireAnimNodeToOutput(AnimBPPath, SMName, J, E);
		Log.Record(TEXT("wire to Output Pose"), J, E);
	}
	else Log.Skip(TEXT("wire to Output Pose"), TEXT("wire_to_output=false"));

	TArray<FString> States = { StIdle };
	if (bWalkRun) States.Add(StWalkRun);
	States.Append(JumpChain);
	Obj->SetArrayField(TEXT("states"), ToJsonStringArray(States));
	Obj->SetStringField(TEXT("walk_run_asset"), WalkRunAsset);

	const bool bCoreOk = bIdle && bWalkRun;
	Finish(bCoreOk && Log.NumFailed == 0, bCoreOk
		? TEXT("Locomotion state machine built but some sub-steps failed; see steps[]/failed_steps.")
		: TEXT("Locomotion state machine incomplete (Idle/WalkRun missing); see steps[]."));
}

// -----------------------------------------------------------------------------------------------------
// create_montage_from_sequence
// -----------------------------------------------------------------------------------------------------
void HandleCreateMontageFromSequenceFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	using namespace CompositeTemplate;
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }

	FString SequencePath, MontagePathArg, MontageName, SavePath, Slot;
	if (!Args->TryGetStringField(TEXT("sequence_path"), SequencePath) || SequencePath.IsEmpty())
		if (!Args->TryGetStringField(TEXT("animation_path"), SequencePath) || SequencePath.IsEmpty())
			Args->TryGetStringField(TEXT("asset_path"), SequencePath);
	Args->TryGetStringField(TEXT("montage_path"), MontagePathArg);
	Args->TryGetStringField(TEXT("montage_name"), MontageName);
	Args->TryGetStringField(TEXT("save_path"), SavePath);
	Args->TryGetStringField(TEXT("slot"), Slot);
	if (Slot.IsEmpty()) Args->TryGetStringField(TEXT("slot_name"), Slot);
	if (Slot.IsEmpty()) Slot = TEXT("DefaultSlot");

	if (SequencePath.IsEmpty()) { SetError(TEXT("sequence_path is required"), OutJsonString, OutError); return; }
	UAnimSequenceBase* Seq = Cast<UAnimSequenceBase>(UEditorAssetLibrary::LoadAsset(SequencePath));
	if (!Seq) { SetError(FString::Printf(TEXT("Could not load animation: %s"), *SequencePath), OutJsonString, OutError); return; }
	if (Seq->IsA<UAnimMontage>()) { SetError(TEXT("sequence_path must be an AnimSequence/AnimComposite, not a montage."), OutJsonString, OutError); return; }
	USkeleton* Skel = Seq->GetSkeleton();
	if (!Skel) { SetError(TEXT("Animation has no Skeleton."), OutJsonString, OutError); return; }

	// montage_path is "folder/name" (an object path "folder/name.name" is accepted too).
	if (!MontagePathArg.IsEmpty())
	{
		FString P = TrimFolder(MontagePathArg);
		int32 Dot = INDEX_NONE;
		if (P.FindLastChar(TEXT('.'), Dot) && Dot > P.Find(TEXT("/"), ESearchCase::IgnoreCase, ESearchDir::FromEnd)) P.LeftInline(Dot);
		MontageName = FPackageName::GetShortName(P);
		SavePath = FPackageName::GetLongPackagePath(P);
	}
	if (MontageName.IsEmpty()) MontageName = FString::Printf(TEXT("AM_%s"), *Seq->GetName());
	if (SavePath.IsEmpty()) SavePath = AssetFolderOf(Seq);
	SavePath = TrimFolder(SavePath);
	if (SavePath.IsEmpty() || !SavePath.StartsWith(TEXT("/")))
	{
		SetError(FString::Printf(TEXT("montage_path must be a content path like '/Game/Anims/AM_Attack' (got '%s')."), *MontagePathArg), OutJsonString, OutError);
		return;
	}

	FCompositeStepLog Log;
	TArray<FString> Notes;
	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	const float SeqLength = Seq->GetPlayLength();

	// 1. Montage asset.
	FString MontagePath;
	{
		FString J, E;
		HandleCreateAnimMontage(MontageName, SavePath, Skel->GetPathName(), J, E);
		TSharedPtr<FJsonObject> R = Log.Record(TEXT("montage create"), J, E);
		if (!R.IsValid())
		{
			Obj->SetBoolField(TEXT("success"), false);
			Obj->SetStringField(TEXT("error"), TEXT("Montage asset could not be created; see steps[]."));
			Log.WriteTo(Obj);
			BuildSuccessJson(Obj, OutJsonString);
			OutError = TEXT("create_montage_from_sequence: montage asset could not be created (see steps[]).");
			return;
		}
		R->TryGetStringField(TEXT("asset_path"), MontagePath);
		bool bExisted = false;
		if (R->TryGetBoolField(TEXT("existed"), bExisted) && bExisted)
			Notes.Add(TEXT("Montage already existed: the slot segment/sections/notifies below were appended to it."));
	}

	// 2. Slot: rename the factory's empty DefaultSlot track when a different slot is requested, then link.
	if (!Slot.Equals(TEXT("DefaultSlot"), ESearchCase::IgnoreCase))
	{
		UAnimMontage* M = Cast<UAnimMontage>(UEditorAssetLibrary::LoadAsset(MontagePath));
		bool bHasRequested = false, bHasEmptyDefault = false;
		if (M)
		{
			for (const FSlotAnimationTrack& T : M->SlotAnimTracks)
			{
				if (T.SlotName.ToString().Equals(Slot, ESearchCase::IgnoreCase)) bHasRequested = true;
				if (T.SlotName == FName(TEXT("DefaultSlot")) && T.AnimTrack.AnimSegments.Num() == 0) bHasEmptyDefault = true;
			}
		}
		if (!bHasRequested && bHasEmptyDefault)
		{
			FString J, E;
			HandleSetMontageSlotName(MontagePath, TEXT("DefaultSlot"), Slot, J, E);
			Log.Record(FString::Printf(TEXT("slot rename DefaultSlot -> %s"), *Slot), J, E);
		}
	}
	{
		FString J, E;
		HandleLinkMontageSlot(MontagePath, Slot, SequencePath, J, E, 0.0f);
		Log.Record(FString::Printf(TEXT("link %s into slot %s"), *Seq->GetName(), *Slot), J, E);
	}
	if (UAnimMontage* M = Cast<UAnimMontage>(UEditorAssetLibrary::LoadAsset(MontagePath)))
	{
		// Make the montage length reflect the linked segment before sections/notifies are positioned.
		M->CalculateSequenceLength();
		if (M->AnimNotifyTracks.Num() == 0) M->InitializeNotifyTrack();
		M->MarkPackageDirty();
	}

	// 3. Sections (in given order; the engine sorts by time and auto-links consecutive sections).
	struct FSectionReq { FString Name; float Start = 0.f; FString Next; bool bLoop = false; bool bHasNext = false; };
	TArray<FSectionReq> Sections;
	{
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (Args->TryGetArrayField(TEXT("sections"), Arr) && Arr)
		{
			for (const TSharedPtr<FJsonValue>& V : *Arr)
			{
				const TSharedPtr<FJsonObject> O = V.IsValid() ? V->AsObject() : nullptr;
				FSectionReq S;
				if (O.IsValid())
				{
					O->TryGetStringField(TEXT("name"), S.Name);
					double T = 0.0, TN = -1.0;
					const bool bHasT  = O->TryGetNumberField(TEXT("start_time"), T);
					const bool bHasTN = O->TryGetNumberField(TEXT("start_normalized"), TN);
					S.Start = bHasTN ? (float)FMath::Clamp(TN, 0.0, 1.0) * SeqLength : (bHasT ? (float)T : 0.f);
					S.Start = FMath::Clamp(S.Start, 0.f, SeqLength);
					S.bHasNext = O->TryGetStringField(TEXT("next"), S.Next);
					O->TryGetBoolField(TEXT("loop"), S.bLoop);
				}
				else if (V.IsValid()) V->TryGetString(S.Name);
				if (!S.Name.IsEmpty()) Sections.Add(S);
			}
		}
	}
	for (int32 i = 0; i < Sections.Num(); ++i)
	{
		const FSectionReq& S = Sections[i];
		FString J, E;
		bool bRenamedDefault = false;
		if (i == 0 && S.Start <= KINDA_SMALL_NUMBER)
		{
			// The factory seeds a "Default" section at t=0; give it the first requested name instead of stacking two sections at 0.
			if (UAnimMontage* M = Cast<UAnimMontage>(UEditorAssetLibrary::LoadAsset(MontagePath)))
			{
				const bool bHasDefault = M->CompositeSections.ContainsByPredicate([](const FCompositeSection& C){ return C.SectionName == FName(TEXT("Default")); });
				const bool bHasName    = M->CompositeSections.ContainsByPredicate([&](const FCompositeSection& C){ return C.SectionName.ToString().Equals(S.Name, ESearchCase::IgnoreCase); });
				if (bHasDefault && !bHasName && !S.Name.Equals(TEXT("Default"), ESearchCase::IgnoreCase))
				{
					HandleRenameMontageSection(MontagePath, TEXT("Default"), S.Name, J, E);
					Log.Record(FString::Printf(TEXT("section %s (rename Default @0)"), *S.Name), J, E);
					bRenamedDefault = true;
				}
				else if (bHasName || S.Name.Equals(TEXT("Default"), ESearchCase::IgnoreCase))
				{
					Log.Ok(FString::Printf(TEXT("section %s"), *S.Name), TEXT("already present at t=0"));
					bRenamedDefault = true;
				}
			}
		}
		if (!bRenamedDefault)
		{
			HandleAddMontageSection(MontagePath, S.Name, S.Start, J, E);
			Log.Record(FString::Printf(TEXT("section %s @%.3f"), *S.Name, S.Start), J, E);
		}
	}
	// Section links: explicit next, loops (link to itself), loop_sections[].
	TArray<FString> LoopSections = ReadStringArray(Args, TEXT("loop_sections"));
	for (const FSectionReq& S : Sections)
	{
		if (S.bLoop) LoopSections.AddUnique(S.Name);
		if (S.bHasNext && !S.bLoop)
		{
			FString J, E;
			HandleSetMontageSectionLink(MontagePath, S.Name, S.Next, J, E);
			Log.Record(FString::Printf(TEXT("link %s -> %s"), *S.Name, S.Next.IsEmpty() ? TEXT("None") : *S.Next), J, E);
		}
	}
	for (const FString& L : LoopSections)
	{
		FString J, E;
		HandleSetMontageSectionLink(MontagePath, L, L, J, E);
		Log.Record(FString::Printf(TEXT("loop section %s"), *L), J, E);
	}

	// 4. Blend settings.
	{
		double BI = 0.0, BO = 0.0, RS = 1.0;
		const bool bBI = Args->TryGetNumberField(TEXT("blend_in"), BI) || Args->TryGetNumberField(TEXT("blend_in_time"), BI);
		const bool bBO = Args->TryGetNumberField(TEXT("blend_out"), BO) || Args->TryGetNumberField(TEXT("blend_out_time"), BO);
		const bool bRS = Args->TryGetNumberField(TEXT("rate_scale"), RS);
		if (bBI || bBO || bRS)
		{
			FString J, E;
			HandleSetMontageBlendSettings(MontagePath, (float)BI, bBI, (float)BO, bBO, (float)RS, bRS, false, false, J, E);
			Log.Record(TEXT("blend settings"), J, E);
		}
	}

	// 5. Notifies (class resolved by the existing notify helper).
	{
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (Args->TryGetArrayField(TEXT("notifies"), Arr) && Arr)
		{
			for (const TSharedPtr<FJsonValue>& V : *Arr)
			{
				const TSharedPtr<FJsonObject> O = V.IsValid() ? V->AsObject() : nullptr;
				if (!O.IsValid()) continue;
				FString Name, Cls;
				O->TryGetStringField(TEXT("name"), Name);
				if (!O->TryGetStringField(TEXT("class"), Cls)) O->TryGetStringField(TEXT("notify_class"), Cls);
				double T = 0.0, TN = -1.0, Track = 0.0;
				O->TryGetNumberField(TEXT("time"), T);
				if (!O->TryGetNumberField(TEXT("time_normalized"), TN)) TN = -1.0;
				O->TryGetNumberField(TEXT("track_index"), Track);
				if (Name.IsEmpty() && Cls.IsEmpty()) { Log.Fail(TEXT("notify"), TEXT("notify entry needs a name or class")); continue; }
				if (Name.IsEmpty()) Name = Cls;
				FString J, E;
				HandleAddAnimNotify(MontagePath, Name, Cls, (float)T, J, E, (int32)Track, (float)TN);
				Log.Record(FString::Printf(TEXT("notify %s"), *Name), J, E);
			}
		}
	}

	UEditorAssetLibrary::SaveAsset(MontagePath, false);

	// 6. Summary.
	{
		FString J, E;
		HandleGetMontageSummary(MontagePath, J, E);
		if (TSharedPtr<FJsonObject> S = ParseJson(J)) Obj->SetObjectField(TEXT("summary"), S);
	}
	if (UAnimMontage* M = Cast<UAnimMontage>(UEditorAssetLibrary::LoadAsset(MontagePath)))
	{
		TArray<TSharedPtr<FJsonValue>> SecArr;
		for (const FCompositeSection& C : M->CompositeSections)
		{
			TSharedPtr<FJsonObject> SO = MakeShared<FJsonObject>();
			SO->SetStringField(TEXT("name"), C.SectionName.ToString());
			SO->SetNumberField(TEXT("start_time"), C.GetTime());
			SO->SetStringField(TEXT("next_section"), C.NextSectionName.ToString());
			SecArr.Add(MakeShared<FJsonValueObject>(SO));
		}
		Obj->SetArrayField(TEXT("sections"), SecArr);
		Obj->SetNumberField(TEXT("play_length"), M->GetPlayLength());
		Obj->SetNumberField(TEXT("num_notifies"), M->Notifies.Num());
	}

	Obj->SetBoolField(TEXT("success"), Log.NumFailed == 0);
	if (Log.NumFailed > 0) Obj->SetStringField(TEXT("error"), TEXT("Montage created but some sub-steps failed; see steps[]/failed_steps."));
	Obj->SetStringField(TEXT("montage_path"), MontagePath);
	Obj->SetStringField(TEXT("sequence_path"), SequencePath);
	Obj->SetStringField(TEXT("slot"), Slot);
	if (Notes.Num() > 0) Obj->SetArrayField(TEXT("notes"), ToJsonStringArray(Notes));
	Log.WriteTo(Obj);
	BuildSuccessJson(Obj, OutJsonString);
	if (Log.NumFailed > 0) OutError = FString::Printf(TEXT("create_montage_from_sequence: %d sub-step(s) failed (%s)."), Log.NumFailed, *FString::Join(Log.FailedNames, TEXT(", ")));
}

// -----------------------------------------------------------------------------------------------------
// make_additive
// -----------------------------------------------------------------------------------------------------
void HandleMakeAdditiveFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	using namespace CompositeTemplate;
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }

	FString SequencePath, BasePoseSeq, AdditiveType, RefPoseType;
	if (!Args->TryGetStringField(TEXT("sequence_path"), SequencePath) || SequencePath.IsEmpty())
		if (!Args->TryGetStringField(TEXT("animation_path"), SequencePath) || SequencePath.IsEmpty())
			Args->TryGetStringField(TEXT("asset_path"), SequencePath);
	Args->TryGetStringField(TEXT("base_pose_sequence"), BasePoseSeq);
	Args->TryGetStringField(TEXT("additive_type"), AdditiveType);
	Args->TryGetStringField(TEXT("ref_pose_type"), RefPoseType);
	double BaseFrame = 0.0; Args->TryGetNumberField(TEXT("base_frame"), BaseFrame);
	bool bRecompress = true; Args->TryGetBoolField(TEXT("recompress"), bRecompress);

	if (SequencePath.IsEmpty()) { SetError(TEXT("sequence_path is required"), OutJsonString, OutError); return; }
	UAnimSequence* AnimSeq = Cast<UAnimSequence>(UEditorAssetLibrary::LoadAsset(SequencePath));
	if (!AnimSeq) { SetError(FString::Printf(TEXT("Could not load AnimSequence: %s"), *SequencePath), OutJsonString, OutError); return; }

	FCompositeStepLog Log;
	TArray<FString> Notes;

	// Normalise the requested combination so AdditiveAnimType / RefPoseType / RefPoseSeq / RefFrameIndex agree.
	const FString AT = AdditiveType.IsEmpty() ? TEXT("local") : AdditiveType.ToLower();
	FString AdditiveArg, RefTypeArg, RefSeqArg;
	int32 RefFrame = 0;
	if (AT == TEXT("none") || AT == TEXT("off"))
	{
		AdditiveArg = TEXT("none"); RefTypeArg = TEXT("none"); RefSeqArg = FString(); RefFrame = 0;
	}
	else
	{
		AdditiveArg = (AT == TEXT("mesh_space") || AT == TEXT("mesh") || AT == TEXT("meshspace")) ? TEXT("mesh_space") : TEXT("local");
		const FString RT = RefPoseType.IsEmpty() ? TEXT("anim_frame") : RefPoseType.ToLower();
		if (RT == TEXT("ref_pose") || RT == TEXT("refpose") || RT == TEXT("skeleton"))
		{
			RefTypeArg = TEXT("ref_pose"); RefSeqArg = FString(); RefFrame = 0;
			if (!BasePoseSeq.IsEmpty()) Notes.Add(TEXT("base_pose_sequence ignored: ref_pose_type=ref_pose uses the skeleton reference pose."));
		}
		else
		{
			RefTypeArg = (RT == TEXT("anim_scaled") || RT == TEXT("animscaled") || RT == TEXT("scaled")) ? TEXT("anim_scaled")
			           : (RT == TEXT("local_anim_frame") || RT == TEXT("localanimframe") || RT == TEXT("localframe")) ? TEXT("local_anim_frame")
			           : TEXT("anim_frame");
			RefSeqArg = BasePoseSeq.IsEmpty() ? SequencePath : BasePoseSeq;
			UAnimSequence* RefSeq = Cast<UAnimSequence>(UEditorAssetLibrary::LoadAsset(RefSeqArg));
			if (!RefSeq)
			{
				SetError(FString::Printf(TEXT("base_pose_sequence could not be loaded as an AnimSequence: %s"), *RefSeqArg), OutJsonString, OutError);
				return;
			}
			if (RefSeq->GetSkeleton() != AnimSeq->GetSkeleton())
				Notes.Add(TEXT("base_pose_sequence uses a different Skeleton than the target sequence."));
			int32 NumFrames = 0;
			if (const IAnimationDataModel* Model = RefSeq->GetDataModel()) NumFrames = Model->GetNumberOfFrames();
			RefFrame = (int32)BaseFrame;
			if (NumFrames > 0 && (RefFrame < 0 || RefFrame > NumFrames))
			{
				Notes.Add(FString::Printf(TEXT("base_frame %d clamped to 0..%d."), RefFrame, NumFrames));
				RefFrame = FMath::Clamp(RefFrame, 0, NumFrames);
			}
			if (RefTypeArg == TEXT("anim_scaled") && BasePoseSeq.IsEmpty())
				Notes.Add(TEXT("anim_scaled against the sequence itself yields a zero additive; pass base_pose_sequence."));
		}
	}

	{
		TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>();
		A->SetStringField(TEXT("asset_path"), SequencePath);
		A->SetStringField(TEXT("additive_anim_type"), AdditiveArg);
		A->SetStringField(TEXT("ref_pose_type"), RefTypeArg);
		A->SetStringField(TEXT("ref_pose_seq"), RefSeqArg);
		A->SetNumberField(TEXT("ref_frame_index"), RefFrame);
		FString J, E;
		HandleSetAnimSequenceSettingsFromArgs(A, J, E);
		Log.Record(TEXT("apply additive settings"), J, E);
	}

	// Re-read what actually landed on the asset.
	AnimSeq = Cast<UAnimSequence>(UEditorAssetLibrary::LoadAsset(SequencePath));
	if (!AnimSeq) { SetError(TEXT("Sequence vanished after applying settings."), OutJsonString, OutError); return; }

	FString Recompress = TEXT("skipped");
	if (bRecompress)
	{
#if !UE_VERSION_OLDER_THAN(5, 8, 0)
		AnimSeq->CacheDerivedDataForCurrentPlatform();
		Recompress = TEXT("CacheDerivedDataForCurrentPlatform (sync)");
#else
		AnimSeq->BeginCacheDerivedDataForCurrentPlatform();
		Recompress = TEXT("BeginCacheDerivedDataForCurrentPlatform (async)");
#endif
		Log.Ok(TEXT("recompress"), Recompress);
	}
	AnimSeq->MarkPackageDirty();
	const bool bSaved = UEditorAssetLibrary::SaveAsset(SequencePath, false);
	Log.Ok(TEXT("save"), bSaved ? TEXT("saved") : TEXT("SaveAsset returned false"));

	auto AdditiveTypeName = [](EAdditiveAnimationType T) -> const TCHAR*
	{
		switch (T) { case AAT_None: return TEXT("none"); case AAT_LocalSpaceBase: return TEXT("local"); case AAT_RotationOffsetMeshSpace: return TEXT("mesh_space"); default: return TEXT("unknown"); }
	};
	auto RefPoseTypeName = [](EAdditiveBasePoseType T) -> const TCHAR*
	{
		switch (T) { case ABPT_None: return TEXT("none"); case ABPT_RefPose: return TEXT("ref_pose"); case ABPT_AnimScaled: return TEXT("anim_scaled"); case ABPT_AnimFrame: return TEXT("anim_frame"); case ABPT_LocalAnimFrame: return TEXT("local_anim_frame"); default: return TEXT("unknown"); }
	};

	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	Obj->SetBoolField(TEXT("success"), Log.NumFailed == 0);
	if (Log.NumFailed > 0) Obj->SetStringField(TEXT("error"), TEXT("Additive settings could not be fully applied; see steps[]."));
	Obj->SetStringField(TEXT("sequence_path"), SequencePath);
	Obj->SetStringField(TEXT("additive_anim_type"), AdditiveTypeName(AnimSeq->AdditiveAnimType.GetValue()));
	Obj->SetStringField(TEXT("ref_pose_type"), RefPoseTypeName(AnimSeq->RefPoseType.GetValue()));
	Obj->SetStringField(TEXT("ref_pose_seq"), AnimSeq->RefPoseSeq ? AnimSeq->RefPoseSeq->GetPathName() : FString());
	Obj->SetNumberField(TEXT("ref_frame_index"), AnimSeq->RefFrameIndex);
	Obj->SetStringField(TEXT("recompress"), Recompress);
	Obj->SetBoolField(TEXT("saved"), bSaved);
	if (Notes.Num() > 0) Obj->SetArrayField(TEXT("notes"), ToJsonStringArray(Notes));
	Log.WriteTo(Obj);
	BuildSuccessJson(Obj, OutJsonString);
	if (Log.NumFailed > 0) OutError = TEXT("make_additive: applying settings failed (see steps[]).");
}

// -----------------------------------------------------------------------------------------------------
// retarget_setup
// -----------------------------------------------------------------------------------------------------
void HandleRetargetSetupFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	using namespace CompositeTemplate;
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }

	FString SourceMeshPath, TargetMeshPath, OutputFolder, RetargeterName, SourceRigName, TargetRigName, Prefix, Suffix, AnimOutputFolder;
	if (!Args->TryGetStringField(TEXT("source_mesh_path"), SourceMeshPath)) Args->TryGetStringField(TEXT("source_mesh"), SourceMeshPath);
	if (!Args->TryGetStringField(TEXT("target_mesh_path"), TargetMeshPath)) Args->TryGetStringField(TEXT("target_mesh"), TargetMeshPath);
	if (!Args->TryGetStringField(TEXT("output_folder"), OutputFolder)) Args->TryGetStringField(TEXT("save_path"), OutputFolder);
	Args->TryGetStringField(TEXT("retargeter_name"), RetargeterName);
	Args->TryGetStringField(TEXT("source_rig_name"), SourceRigName);
	Args->TryGetStringField(TEXT("target_rig_name"), TargetRigName);
	Args->TryGetStringField(TEXT("prefix"), Prefix);
	Args->TryGetStringField(TEXT("suffix"), Suffix);
	Args->TryGetStringField(TEXT("animation_output_folder"), AnimOutputFolder);
	bool bAutoMap = true, bAutoAlign = true, bFingers = false;
	Args->TryGetBoolField(TEXT("auto_map"), bAutoMap);
	Args->TryGetBoolField(TEXT("auto_align"), bAutoAlign);
	Args->TryGetBoolField(TEXT("include_fingers"), bFingers);
	const TArray<FString> Animations = ReadStringArray(Args, TEXT("animations"));

	if (SourceMeshPath.IsEmpty() || TargetMeshPath.IsEmpty()) { SetError(TEXT("source_mesh_path and target_mesh_path are required"), OutJsonString, OutError); return; }
	OutputFolder = TrimFolder(OutputFolder);
	if (OutputFolder.IsEmpty()) { SetError(TEXT("output_folder is required"), OutJsonString, OutError); return; }
	AnimOutputFolder = AnimOutputFolder.IsEmpty() ? OutputFolder : TrimFolder(AnimOutputFolder);

	FString MeshErr;
	USkeletalMesh* SourceMesh = ResolveSkeletalMesh(SourceMeshPath, MeshErr);
	if (!SourceMesh) { SetError(TEXT("source: ") + MeshErr, OutJsonString, OutError); return; }
	USkeletalMesh* TargetMesh = ResolveSkeletalMesh(TargetMeshPath, MeshErr);
	if (!TargetMesh) { SetError(TEXT("target: ") + MeshErr, OutJsonString, OutError); return; }
	const FString SourceMeshObjPath = SourceMesh->GetPathName();
	const FString TargetMeshObjPath = TargetMesh->GetPathName();

	if (SourceRigName.IsEmpty()) SourceRigName = FString::Printf(TEXT("IK_%s"), *SourceMesh->GetName());
	if (TargetRigName.IsEmpty()) TargetRigName = FString::Printf(TEXT("IK_%s"), *TargetMesh->GetName());
	if (SourceRigName == TargetRigName) TargetRigName += TEXT("_Target");
	if (RetargeterName.IsEmpty()) RetargeterName = FString::Printf(TEXT("RTG_%s_to_%s"), *SourceMesh->GetName(), *TargetMesh->GetName());
	if (Prefix.IsEmpty() && Suffix.IsEmpty()) Suffix = FString::Printf(TEXT("_%s"), *TargetMesh->GetName());

	FCompositeStepLog Log;
	TArray<FString> Notes;
	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);

	// One IK Rig per side: create (or reuse), retarget root, standard humanoid chains.
	auto BuildRig = [&](const TCHAR* Side, USkeletalMesh* Mesh, const FString& MeshObjPath, const FString& RigName, FString& OutRigPath) -> TSharedPtr<FJsonObject>
	{
		TSharedPtr<FJsonObject> RigJson = MakeShared<FJsonObject>();
		const FString RigPath = OutputFolder + TEXT("/") + RigName;
		bool bExists = false;
		if (UEditorAssetLibrary::DoesAssetExist(RigPath))
		{
			if (Cast<UIKRigDefinition>(UEditorAssetLibrary::LoadAsset(RigPath))) { bExists = true; Log.Ok(FString::Printf(TEXT("%s ik rig"), Side), TEXT("already existed; reused")); OutRigPath = RigPath; }
			else { Log.Fail(FString::Printf(TEXT("%s ik rig"), Side), FString::Printf(TEXT("'%s' exists but is not an IKRig"), *RigPath)); return RigJson; }
		}
		if (!bExists)
		{
			FString J, E;
			HandleCreateIKRig(RigName, OutputFolder, MeshObjPath, FString(), J, E);
			TSharedPtr<FJsonObject> R = Log.Record(FString::Printf(TEXT("%s ik rig"), Side), J, E);
			if (!R.IsValid()) return RigJson;
			R->TryGetStringField(TEXT("asset_path"), OutRigPath);
		}
		RigJson->SetStringField(TEXT("path"), OutRigPath);
		RigJson->SetStringField(TEXT("mesh"), MeshObjPath);

		TArray<FHeuristicBone> Bones;
		BuildBoneInfos(Mesh->GetRefSkeleton(), Bones);

		const int32 Pelvis = FindPelvisBone(Bones);
		if (Pelvis == INDEX_NONE) Log.Fail(FString::Printf(TEXT("%s retarget root"), Side), TEXT("no pelvis/hips bone found by name heuristics; set it with set_ik_retarget_root"));
		else
		{
			FString J, E;
			HandleSetIKRetargetRoot(OutRigPath, Bones[Pelvis].Name.ToString(), J, E);
			Log.Record(FString::Printf(TEXT("%s retarget root = %s"), Side, *Bones[Pelvis].Name.ToString()), J, E);
			RigJson->SetStringField(TEXT("retarget_root"), Bones[Pelvis].Name.ToString());
		}

		TArray<FName> ExistingChains;
		if (UIKRigDefinition* Rig = Cast<UIKRigDefinition>(UEditorAssetLibrary::LoadAsset(OutRigPath)))
			if (UIKRigController* Ctrl = UIKRigController::GetController(Rig))
				for (const FBoneChain& C : Ctrl->GetRetargetChains()) ExistingChains.Add(C.ChainName);

		TArray<TSharedPtr<FJsonValue>> ChainsJson;
		TArray<FString> Added, Missing;
		for (const FHeuristicChain& C : BuildHumanoidChains(Bones, bFingers))
		{
			TSharedPtr<FJsonObject> CJ = MakeShared<FJsonObject>();
			CJ->SetStringField(TEXT("chain"), C.Name);
			const FString Step = FString::Printf(TEXT("%s chain %s"), Side, *C.Name);
			if (C.Start == INDEX_NONE || C.End == INDEX_NONE)
			{
				Log.Skip(Step, C.Reason);
				CJ->SetBoolField(TEXT("ok"), false);
				CJ->SetStringField(TEXT("error"), C.Reason);
				Missing.Add(C.Name);
			}
			else if (ExistingChains.Contains(FName(*C.Name)))
			{
				Log.Ok(Step, TEXT("already on the rig"));
				CJ->SetBoolField(TEXT("ok"), true);
				CJ->SetBoolField(TEXT("existed"), true);
			}
			else
			{
				FString J, E;
				HandleAddRetargetChain(OutRigPath, C.Name, Bones[C.Start].Name.ToString(), Bones[C.End].Name.ToString(), FString(), J, E);
				const bool bOk = Log.Record(FString::Printf(TEXT("%s (%s -> %s)"), *Step, *Bones[C.Start].Name.ToString(), *Bones[C.End].Name.ToString()), J, E).IsValid();
				CJ->SetBoolField(TEXT("ok"), bOk);
				CJ->SetStringField(TEXT("start_bone"), Bones[C.Start].Name.ToString());
				CJ->SetStringField(TEXT("end_bone"), Bones[C.End].Name.ToString());
				if (bOk) Added.Add(C.Name);
			}
			ChainsJson.Add(MakeShared<FJsonValueObject>(CJ));
		}
		RigJson->SetArrayField(TEXT("chains"), ChainsJson);
		RigJson->SetArrayField(TEXT("added_chains"), ToJsonStringArray(Added));
		if (Missing.Num() > 0)
			Notes.Add(FString::Printf(TEXT("%s rig: chains not resolved by bone heuristics: %s (add them with add_retarget_chain)."), Side, *FString::Join(Missing, TEXT(", "))));
		return RigJson;
	};

	FString SourceRigPath, TargetRigPath;
	Obj->SetObjectField(TEXT("source_ik_rig"), BuildRig(TEXT("source"), SourceMesh, SourceMeshObjPath, SourceRigName, SourceRigPath));
	Obj->SetObjectField(TEXT("target_ik_rig"), BuildRig(TEXT("target"), TargetMesh, TargetMeshObjPath, TargetRigName, TargetRigPath));

	auto FinishFail = [&](const FString& Err)
	{
		Obj->SetBoolField(TEXT("success"), false);
		Obj->SetStringField(TEXT("error"), Err);
		if (Notes.Num() > 0) Obj->SetArrayField(TEXT("notes"), ToJsonStringArray(Notes));
		Log.WriteTo(Obj);
		BuildSuccessJson(Obj, OutJsonString);
		OutError = Err;
	};
	if (SourceRigPath.IsEmpty() || TargetRigPath.IsEmpty()) { FinishFail(TEXT("retarget_setup: an IK Rig could not be created; see steps[].")); return; }

	// Retargeter.
	FString RetargeterPath = OutputFolder + TEXT("/") + RetargeterName;
	if (UEditorAssetLibrary::DoesAssetExist(RetargeterPath) && Cast<UIKRetargeter>(UEditorAssetLibrary::LoadAsset(RetargeterPath)))
	{
		Log.Ok(TEXT("retargeter"), TEXT("already existed; reused"));
	}
	else
	{
		FString J, E;
		HandleCreateIKRetargeter(RetargeterName, OutputFolder, SourceRigPath, TargetRigPath, J, E);
		TSharedPtr<FJsonObject> R = Log.Record(TEXT("retargeter"), J, E);
		if (!R.IsValid()) { FinishFail(TEXT("retarget_setup: the IK Retargeter could not be created; see steps[].")); return; }
		R->TryGetStringField(TEXT("asset_path"), RetargeterPath);
	}
	Obj->SetStringField(TEXT("retargeter"), RetargeterPath);

	if (bAutoMap)
	{
		FString J, E;
		HandleMapRetargetChain(RetargeterPath, FString(), FString(), true, J, E);
		Log.Record(TEXT("map chains (auto, fuzzy)"), J, E);
	}
	else Log.Skip(TEXT("map chains"), TEXT("auto_map=false; map chains with map_retarget_chain"));

	if (bAutoAlign)
	{
		TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>();
		A->SetStringField(TEXT("retargeter_path"), RetargeterPath);
		A->SetStringField(TEXT("side"), TEXT("target"));
		FString J, E;
		HandleAutoAlignRetargetPoseFromArgs(A, J, E);
		Log.Record(TEXT("auto align retarget pose (target)"), J, E);
	}
	else Log.Skip(TEXT("auto align retarget pose"), TEXT("auto_align=false"));

	// Per-animation export so each input gets its own ok/error entry.
	TArray<TSharedPtr<FJsonValue>> AnimResults;
	TArray<FString> AllCreated;
	for (const FString& Anim : Animations)
	{
		TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>();
		A->SetStringField(TEXT("retargeter_path"), RetargeterPath);
		A->SetStringField(TEXT("source_mesh"), SourceMeshObjPath);
		A->SetStringField(TEXT("target_mesh"), TargetMeshObjPath);
		A->SetArrayField(TEXT("animation_paths"), ToJsonStringArray({ Anim }));
		A->SetStringField(TEXT("save_path"), AnimOutputFolder);
		if (!Prefix.IsEmpty()) A->SetStringField(TEXT("prefix"), Prefix);
		if (!Suffix.IsEmpty()) A->SetStringField(TEXT("suffix"), Suffix);
		FString J, E;
		HandleExportRetargetAnimationFromArgs(A, J, E);
		TSharedPtr<FJsonObject> R = Log.Record(FString::Printf(TEXT("export %s"), *Anim), J, E);

		TSharedPtr<FJsonObject> AR = MakeShared<FJsonObject>();
		AR->SetStringField(TEXT("input"), Anim);
		AR->SetBoolField(TEXT("ok"), R.IsValid());
		if (R.IsValid())
		{
			const TArray<TSharedPtr<FJsonValue>>* Created = nullptr;
			TArray<TSharedPtr<FJsonValue>> CreatedCopy;
			if (R->TryGetArrayField(TEXT("created_assets"), Created) && Created)
				for (const TSharedPtr<FJsonValue>& V : *Created) { FString S; if (V->TryGetString(S)) { CreatedCopy.Add(MakeShared<FJsonValueString>(S)); AllCreated.Add(S); } }
			AR->SetArrayField(TEXT("created_assets"), CreatedCopy);
		}
		else AR->SetStringField(TEXT("error"), E.IsEmpty() ? TEXT("see steps[]") : E);
		AnimResults.Add(MakeShared<FJsonValueObject>(AR));
	}
	if (Animations.Num() == 0) Log.Skip(TEXT("export animations"), TEXT("no animations[] given"));
	Obj->SetArrayField(TEXT("animations"), AnimResults);

	TSharedPtr<FJsonObject> CreatedAssets = MakeShared<FJsonObject>();
	CreatedAssets->SetStringField(TEXT("source_ik_rig"), SourceRigPath);
	CreatedAssets->SetStringField(TEXT("target_ik_rig"), TargetRigPath);
	CreatedAssets->SetStringField(TEXT("retargeter"), RetargeterPath);
	CreatedAssets->SetArrayField(TEXT("retargeted_animations"), ToJsonStringArray(AllCreated));
	Obj->SetObjectField(TEXT("created_assets"), CreatedAssets);
	Obj->SetBoolField(TEXT("success"), Log.NumFailed == 0);
	if (Log.NumFailed > 0) Obj->SetStringField(TEXT("error"), TEXT("Retarget setup finished with failed sub-steps; see steps[]/failed_steps."));
	if (Notes.Num() > 0) Obj->SetArrayField(TEXT("notes"), ToJsonStringArray(Notes));
	Log.WriteTo(Obj);
	BuildSuccessJson(Obj, OutJsonString);
	if (Log.NumFailed > 0) OutError = FString::Printf(TEXT("retarget_setup: %d sub-step(s) failed (%s)."), Log.NumFailed, *FString::Join(Log.FailedNames, TEXT(", ")));
}

// -----------------------------------------------------------------------------------------------------
// setup_motion_matching
// -----------------------------------------------------------------------------------------------------
void HandleSetupMotionMatchingFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	using namespace CompositeTemplate;
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }

	FString SkeletonPath, OutputFolder, DatabaseName, SchemaName, ChooserName, MirrorTable;
	Args->TryGetStringField(TEXT("skeleton_path"), SkeletonPath);
	if (!Args->TryGetStringField(TEXT("output_folder"), OutputFolder)) Args->TryGetStringField(TEXT("save_path"), OutputFolder);
	Args->TryGetStringField(TEXT("database_name"), DatabaseName);
	Args->TryGetStringField(TEXT("schema_name"), SchemaName);
	Args->TryGetStringField(TEXT("chooser_name"), ChooserName);
	if (!Args->TryGetStringField(TEXT("mirror_data_table"), MirrorTable)) Args->TryGetStringField(TEXT("mirror_data_table_path"), MirrorTable);
	bool bCreateChooser = true; Args->TryGetBoolField(TEXT("create_chooser"), bCreateChooser);
	double SampleRate = 30.0; Args->TryGetNumberField(TEXT("sample_rate"), SampleRate);
	double TrajFlags = (double)(int32(EPoseSearchTrajectoryFlags::PositionXY) | int32(EPoseSearchTrajectoryFlags::FacingDirectionXY));
	Args->TryGetNumberField(TEXT("trajectory_flags"), TrajFlags);
	const TArray<FString> Animations = ReadStringArray(Args, TEXT("animations"));

	TArray<double> TrajectorySeconds;
	{
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (Args->TryGetArrayField(TEXT("trajectory_seconds"), Arr) && Arr)
			for (const TSharedPtr<FJsonValue>& V : *Arr) { double D = 0.0; if (V.IsValid() && V->TryGetNumber(D)) TrajectorySeconds.Add(D); }
		if (TrajectorySeconds.Num() == 0) TrajectorySeconds = { -0.3, 0.0, 0.3, 0.6 };
	}

	if (SkeletonPath.IsEmpty()) { SetError(TEXT("skeleton_path is required"), OutJsonString, OutError); return; }
	OutputFolder = TrimFolder(OutputFolder);
	if (OutputFolder.IsEmpty()) { SetError(TEXT("output_folder is required"), OutJsonString, OutError); return; }
	USkeleton* Skeleton = Cast<USkeleton>(UEditorAssetLibrary::LoadAsset(SkeletonPath));
	if (!Skeleton) { SetError(FString::Printf(TEXT("Could not load Skeleton: %s"), *SkeletonPath), OutJsonString, OutError); return; }
	if (SchemaName.IsEmpty())   SchemaName   = FString::Printf(TEXT("PSS_%s"), *Skeleton->GetName());
	if (DatabaseName.IsEmpty()) DatabaseName = FString::Printf(TEXT("PSD_%s"), *Skeleton->GetName());
	if (ChooserName.IsEmpty())  ChooserName  = FString::Printf(TEXT("CT_%s"), *DatabaseName);

	FCompositeStepLog Log;
	TArray<FString> Notes;
	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
	auto FinishFail = [&](const FString& Err)
	{
		Obj->SetBoolField(TEXT("success"), false);
		Obj->SetStringField(TEXT("error"), Err);
		if (Notes.Num() > 0) Obj->SetArrayField(TEXT("notes"), ToJsonStringArray(Notes));
		Log.WriteTo(Obj);
		BuildSuccessJson(Obj, OutJsonString);
		OutError = Err;
	};

	// Pose bones: explicit list, else foot_l / foot_r / pelvis via bone-name heuristics.
	TArray<FHeuristicBone> Bones;
	BuildBoneInfos(Skeleton->GetReferenceSkeleton(), Bones);
	TArray<FString> PoseBones = ReadStringArray(Args, TEXT("pose_bones"));
	FString PelvisName = BoneNameOr(Bones, FindPelvisBone(Bones));
	if (PoseBones.Num() == 0)
	{
		const FString L = BoneNameOr(Bones, FindFootBone(Bones, EHeuristicSide::Left));
		const FString R = BoneNameOr(Bones, FindFootBone(Bones, EHeuristicSide::Right));
		if (!L.IsEmpty()) PoseBones.Add(L);
		if (!R.IsEmpty()) PoseBones.Add(R);
		if (!PelvisName.IsEmpty()) PoseBones.Add(PelvisName);
		if (PoseBones.Num() == 0) Notes.Add(TEXT("No foot/pelvis bones found by name heuristics; pass pose_bones[] explicitly."));
	}
	else
	{
		for (const FString& B : PoseBones)
			if (Skeleton->GetReferenceSkeleton().FindBoneIndex(FName(*B)) == INDEX_NONE)
				Notes.Add(FString::Printf(TEXT("pose_bones: '%s' is not a bone of this skeleton."), *B));
	}

	// 1. Schema.
	FString SchemaPath;
	{
		FString J, E;
		PoseSearchTools::HandleCreatePoseSearchSchema(SchemaName, OutputFolder, SkeletonPath, (int32)SampleRate, J, E);
		TSharedPtr<FJsonObject> R = Log.Record(TEXT("schema"), J, E);
		if (!R.IsValid()) { FinishFail(TEXT("setup_motion_matching: schema could not be created; see steps[].")); return; }
		R->TryGetStringField(TEXT("path"), SchemaPath);
	}

	// 2. Trajectory channel + samples.
	{
		FString J, E;
		TSharedPtr<FJsonObject> Empty = MakeShared<FJsonObject>();
		PoseSearchTools::HandleAddPoseSearchChannel(SchemaPath, TEXT("trajectory"), Empty, J, E);
		TSharedPtr<FJsonObject> R = Log.Record(TEXT("trajectory channel"), J, E);
		if (R.IsValid())
		{
			double Idx = 0.0; R->TryGetNumberField(TEXT("channel_index"), Idx);
			TArray<FString> Parts;
			for (double S : TrajectorySeconds)
				Parts.Add(FString::Printf(TEXT("(Offset=%f,Flags=%d,Weight=1.000000)"), S, (int32)TrajFlags));
			const FString Value = FString::Printf(TEXT("(%s)"), *FString::Join(Parts, TEXT(",")));
			PoseSearchTools::HandleSetPoseSearchChannelProperty(SchemaPath, (int32)Idx, TEXT("Samples"), Value, J, E);
			Log.Record(FString::Printf(TEXT("trajectory samples (%d, flags=%d)"), TrajectorySeconds.Num(), (int32)TrajFlags), J, E);
		}
	}

	// 3. Pose channel + sampled bones (feet: position+velocity, pelvis: velocity).
	{
		FString J, E;
		TSharedPtr<FJsonObject> Empty = MakeShared<FJsonObject>();
		PoseSearchTools::HandleAddPoseSearchChannel(SchemaPath, TEXT("pose"), Empty, J, E);
		TSharedPtr<FJsonObject> R = Log.Record(TEXT("pose channel"), J, E);
		if (R.IsValid() && PoseBones.Num() > 0)
		{
			double Idx = 0.0; R->TryGetNumberField(TEXT("channel_index"), Idx);
			TArray<FString> Parts;
			for (const FString& B : PoseBones)
			{
				const bool bPelvis = B.Equals(PelvisName, ESearchCase::IgnoreCase);
				const int32 Flags = bPelvis ? int32(EPoseSearchBoneFlags::Velocity) : (int32(EPoseSearchBoneFlags::Position) | int32(EPoseSearchBoneFlags::Velocity));
				Parts.Add(FString::Printf(TEXT("(Reference=(BoneName=\"%s\"),Flags=%d,Weight=1.000000)"), *B, Flags));
			}
			const FString Value = FString::Printf(TEXT("(%s)"), *FString::Join(Parts, TEXT(",")));
			PoseSearchTools::HandleSetPoseSearchChannelProperty(SchemaPath, (int32)Idx, TEXT("SampledBones"), Value, J, E);
			Log.Record(FString::Printf(TEXT("pose bones [%s]"), *FString::Join(PoseBones, TEXT(", "))), J, E);
		}
		else if (R.IsValid()) Log.Skip(TEXT("pose bones"), TEXT("no bones resolved"));
	}

	// 4. Mirror table.
	if (!MirrorTable.IsEmpty())
	{
		FString J, E;
		PoseSearchTools::HandleSetSchemaMirrorDataTable(SchemaPath, MirrorTable, J, E);
		Log.Record(TEXT("mirror data table"), J, E);
	}

	// 5. Database + entries.
	FString DatabasePath;
	{
		FString J, E;
		PoseSearchTools::HandleCreatePoseSearchDatabase(DatabaseName, OutputFolder, SchemaPath, J, E);
		TSharedPtr<FJsonObject> R = Log.Record(TEXT("database"), J, E);
		if (!R.IsValid()) { FinishFail(TEXT("setup_motion_matching: database could not be created; see steps[].")); return; }
		R->TryGetStringField(TEXT("path"), DatabasePath);
	}
	int32 NumEntries = 0;
	for (const FString& Anim : Animations)
	{
		UObject* Asset = UEditorAssetLibrary::LoadAsset(Anim);
		FString Type = TEXT("sequence");
		if (Asset && Asset->IsA<UBlendSpace>())        Type = TEXT("blendspace");
		else if (Asset && Asset->IsA<UAnimMontage>())  Type = TEXT("montage");
		else if (Asset && Asset->IsA<UAnimComposite>()) Type = TEXT("composite");
		FString J, E;
		PoseSearchTools::HandleAddAnimationToDatabase(DatabasePath, Type, Anim, J, E);
		if (Log.Record(FString::Printf(TEXT("entry %s (%s)"), *Anim, *Type), J, E).IsValid()) ++NumEntries;
	}
	if (Animations.Num() == 0) Notes.Add(TEXT("animations[] was empty: the database has no entries, so the index build is skipped."));

	// 6. Build index.
	TSharedPtr<FJsonObject> BuildResult;
	if (NumEntries > 0)
	{
		FString J, E;
		PoseSearchTools::HandleBuildPoseSearchDatabase(DatabasePath, J, E);
		BuildResult = ParseJson(J);
		Log.Record(TEXT("build database index"), J, E);
	}
	else Log.Skip(TEXT("build database index"), TEXT("no entries"));

	// 7. Chooser table with the database as its first row.
	FString ChooserPath;
	if (bCreateChooser)
	{
		ChooserPath = OutputFolder + TEXT("/") + ChooserName;
		if (UEditorAssetLibrary::DoesAssetExist(ChooserPath)) Log.Ok(TEXT("chooser table"), TEXT("already existed; reused"));
		else
		{
			FString J, E;
			ChooserTools::HandleCreateChooserTable(ChooserName, OutputFolder, TEXT("/Script/PoseSearch.PoseSearchDatabase"), J, E);
			TSharedPtr<FJsonObject> R = Log.Record(TEXT("chooser table"), J, E);
			if (!R.IsValid()) ChooserPath.Reset();
			else R->TryGetStringField(TEXT("path"), ChooserPath);
		}
		if (!ChooserPath.IsEmpty())
		{
			FString J, E;
			ChooserTools::HandleAddChooserRow(ChooserPath, DatabasePath, J, E);
			Log.Record(TEXT("chooser row (database)"), J, E);
		}
	}
	else Log.Skip(TEXT("chooser table"), TEXT("create_chooser=false"));

	TSharedPtr<FJsonObject> Assets = MakeShared<FJsonObject>();
	Assets->SetStringField(TEXT("schema"), SchemaPath);
	Assets->SetStringField(TEXT("database"), DatabasePath);
	if (!ChooserPath.IsEmpty()) Assets->SetStringField(TEXT("chooser"), ChooserPath);
	Obj->SetObjectField(TEXT("assets"), Assets);
	Obj->SetArrayField(TEXT("pose_bones"), ToJsonStringArray(PoseBones));
	Obj->SetNumberField(TEXT("num_entries"), NumEntries);
	if (BuildResult.IsValid()) Obj->SetObjectField(TEXT("build_result"), BuildResult);
	Obj->SetBoolField(TEXT("success"), Log.NumFailed == 0);
	if (Log.NumFailed > 0) Obj->SetStringField(TEXT("error"), TEXT("Motion matching setup finished with failed sub-steps; see steps[]/failed_steps."));
	if (Notes.Num() > 0) Obj->SetArrayField(TEXT("notes"), ToJsonStringArray(Notes));
	Log.WriteTo(Obj);
	BuildSuccessJson(Obj, OutJsonString);
	if (Log.NumFailed > 0) OutError = FString::Printf(TEXT("setup_motion_matching: %d sub-step(s) failed (%s)."), Log.NumFailed, *FString::Join(Log.FailedNames, TEXT(", ")));
}

// -----------------------------------------------------------------------------------------------------
// setup_foot_ik
// -----------------------------------------------------------------------------------------------------
void HandleSetupFootIKFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	using namespace CompositeTemplate;
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }

	const FString AnimBPPath = GetAnimBpPathArg(Args);
	FString LeftFoot, RightFoot, LeftVar, RightVar, AlphaVar, EffectorSpace;
	Args->TryGetStringField(TEXT("left_foot_bone"), LeftFoot);
	Args->TryGetStringField(TEXT("right_foot_bone"), RightFoot);
	Args->TryGetStringField(TEXT("ik_target_left_variable"), LeftVar);
	Args->TryGetStringField(TEXT("ik_target_right_variable"), RightVar);
	Args->TryGetStringField(TEXT("alpha_variable"), AlphaVar);
	Args->TryGetStringField(TEXT("effector_space"), EffectorSpace);
	if (LeftVar.IsEmpty())  LeftVar  = TEXT("IKLeftFoot");
	if (RightVar.IsEmpty()) RightVar = TEXT("IKRightFoot");
	if (AlphaVar.IsEmpty()) AlphaVar = TEXT("IKAlpha");

	if (AnimBPPath.IsEmpty()) { SetError(TEXT("anim_blueprint_path is required"), OutJsonString, OutError); return; }
	UAnimBlueprint* AnimBP = Cast<UAnimBlueprint>(UEditorAssetLibrary::LoadAsset(AnimBPPath));
	if (!AnimBP) { SetError(FString::Printf(TEXT("Could not load AnimBlueprint: %s"), *AnimBPPath), OutJsonString, OutError); return; }
	UAnimationGraph* AnimGraph = FindAnimGraph(AnimBP);
	if (!AnimGraph) { SetError(TEXT("AnimGraph not found."), OutJsonString, OutError); return; }
	UAnimGraphNode_Root* Root = FindOutputPoseNode(AnimGraph);
	if (!Root) { SetError(TEXT("AnimGraph has no Output Pose node."), OutJsonString, OutError); return; }

	FCompositeStepLog Log;
	TArray<FString> Notes;
	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);

	// Foot bones from the skeleton when not given.
	if (LeftFoot.IsEmpty() || RightFoot.IsEmpty())
	{
		if (!AnimBP->TargetSkeleton) Log.Fail(TEXT("resolve foot bones"), TEXT("AnimBP has no TargetSkeleton; pass left_foot_bone/right_foot_bone"));
		else
		{
			TArray<FHeuristicBone> Bones;
			BuildBoneInfos(AnimBP->TargetSkeleton->GetReferenceSkeleton(), Bones);
			if (LeftFoot.IsEmpty())  LeftFoot  = BoneNameOr(Bones, FindFootBone(Bones, EHeuristicSide::Left));
			if (RightFoot.IsEmpty()) RightFoot = BoneNameOr(Bones, FindFootBone(Bones, EHeuristicSide::Right));
			if (LeftFoot.IsEmpty() || RightFoot.IsEmpty())
				Log.Fail(TEXT("resolve foot bones"), TEXT("could not find foot bones by name heuristics (foot_l/foot_r, LeftFoot/RightFoot, ...); pass them explicitly"));
			else
				Log.Ok(TEXT("resolve foot bones"), FString::Printf(TEXT("%s / %s"), *LeftFoot, *RightFoot));
		}
	}
	if (LeftFoot.IsEmpty() || RightFoot.IsEmpty())
	{
		Obj->SetBoolField(TEXT("success"), false);
		Obj->SetStringField(TEXT("error"), TEXT("Foot bones unresolved."));
		Log.WriteTo(Obj);
		BuildSuccessJson(Obj, OutJsonString);
		OutError = TEXT("setup_foot_ik: foot bones unresolved (see steps[]).");
		return;
	}

	TUniquePtr<FScopedSuppressAnimCompile> Suppress = MakeUnique<FScopedSuppressAnimCompile>();

	// Variables the IK pins bind to.
	bool bAddedVar = false;
	EnsureAnimBpVariable(AnimBP, AnimBPPath, LeftVar,  TEXT("vector"), FString(),   Log, bAddedVar);
	EnsureAnimBpVariable(AnimBP, AnimBPPath, RightVar, TEXT("vector"), FString(),   Log, bAddedVar);
	EnsureAnimBpVariable(AnimBP, AnimBPPath, AlphaVar, TEXT("float"),  TEXT("1.0"), Log, bAddedVar);
	if (bAddedVar) SilentCompile(AnimBP);

	// Current pose source feeding Output Pose (re-inserted in front of the IK chain).
	FString SourceGuid, SourceTitle;
	{
		UEdGraphPin* RootIn = FindPoseInputPin(Root);
		if (RootIn && RootIn->LinkedTo.Num() > 0 && RootIn->LinkedTo[0] && RootIn->LinkedTo[0]->GetOwningNode())
		{
			UEdGraphNode* Src = RootIn->LinkedTo[0]->GetOwningNode();
			SourceGuid = Src->NodeGuid.ToString();
			SourceTitle = Src->GetNodeTitle(ENodeTitleType::ListView).ToString();
			RootIn->BreakAllPinLinks();
			Log.Ok(TEXT("detach current pose source"), SourceTitle);
		}
		else
		{
			Log.Skip(TEXT("detach current pose source"), TEXT("Output Pose had no input; the Local->Component node input is left open"));
			Notes.Add(TEXT("Output Pose had no incoming pose; connect your locomotion/state machine into the Local To Component Space node."));
		}
	}

	const int32 RX = Root->NodePosX, RY = Root->NodePosY;
	auto Spawn = [&](const FString& Step, TFunctionRef<void(FString&, FString&)> Fn) -> FString
	{
		FString J, E; Fn(J, E);
		TSharedPtr<FJsonObject> R = Log.Record(Step, J, E);
		FString Guid; if (R.IsValid()) R->TryGetStringField(TEXT("node_guid"), Guid);
		return Guid;
	};
	const FString L2CGuid = Spawn(TEXT("node LocalToComponentSpace"), [&](FString& J, FString& E){ HandleAddLocalToComponentSpace(AnimBPPath, RX - 1000, RY, J, E); });
	const FString IKLGuid = Spawn(TEXT("node TwoBoneIK left"),        [&](FString& J, FString& E){ HandleAddTwoBoneIK(AnimBPPath, LeftFoot,  FString(), FString(), RX - 750, RY, J, E); });
	const FString IKRGuid = Spawn(TEXT("node TwoBoneIK right"),       [&](FString& J, FString& E){ HandleAddTwoBoneIK(AnimBPPath, RightFoot, FString(), FString(), RX - 500, RY, J, E); });
	const FString C2LGuid = Spawn(TEXT("node ComponentToLocalSpace"), [&](FString& J, FString& E){ HandleAddComponentToLocalSpace(AnimBPPath, RX - 250, RY, J, E); });

	// Effector space + pin bindings on each IK node.
	auto ConfigureIK = [&](const FString& Guid, const FString& Side, const FString& TargetVar) -> TSharedPtr<FJsonObject>
	{
		TSharedPtr<FJsonObject> Info = MakeShared<FJsonObject>();
		Info->SetStringField(TEXT("node_guid"), Guid);
		Info->SetStringField(TEXT("ik_bone"), Side == TEXT("left") ? LeftFoot : RightFoot);
		if (Guid.IsEmpty()) return Info;
		FGuid G;
		if (!FGuid::Parse(Guid, G)) { Log.Fail(FString::Printf(TEXT("configure IK %s"), *Side), TEXT("invalid node GUID")); return Info; }
		UAnimGraphNode_TwoBoneIK* IK = Cast<UAnimGraphNode_TwoBoneIK>(FindNodeByGuidInAllGraphs(AnimBP, G));
		if (!IK) { Log.Fail(FString::Printf(TEXT("configure IK %s"), *Side), TEXT("spawned node not found by GUID")); return Info; }

		IK->Modify();
		const FString ES = EffectorSpace.ToLower();
		if (ES == TEXT("world") || ES == TEXT("world_space"))              IK->Node.EffectorLocationSpace = BCS_WorldSpace;
		else if (ES == TEXT("bone") || ES == TEXT("bone_space"))           IK->Node.EffectorLocationSpace = BCS_BoneSpace;
		else if (ES == TEXT("parent") || ES == TEXT("parent_bone_space"))  IK->Node.EffectorLocationSpace = BCS_ParentBoneSpace;
		else                                                               IK->Node.EffectorLocationSpace = BCS_ComponentSpace;
		Info->SetStringField(TEXT("effector_space"), ES.IsEmpty() ? TEXT("component") : ES);

		FString Err;
		const bool bEff = BindAnimNodePinToVariable(AnimBP, IK, TEXT("EffectorLocation"), TargetVar, -40, Err);
		if (bEff) Log.Ok(FString::Printf(TEXT("bind %s EffectorLocation <- %s"), *Side, *TargetVar));
		else      Log.Fail(FString::Printf(TEXT("bind %s EffectorLocation <- %s"), *Side, *TargetVar), Err);
		Info->SetBoolField(TEXT("effector_bound"), bEff);
		Info->SetStringField(TEXT("effector_variable"), TargetVar);

		const bool bAlpha = BindAnimNodePinToVariable(AnimBP, IK, TEXT("Alpha"), AlphaVar, 60, Err);
		if (bAlpha) Log.Ok(FString::Printf(TEXT("bind %s Alpha <- %s"), *Side, *AlphaVar));
		else        Log.Fail(FString::Printf(TEXT("bind %s Alpha <- %s"), *Side, *AlphaVar), Err);
		Info->SetBoolField(TEXT("alpha_bound"), bAlpha);
		Info->SetStringField(TEXT("alpha_variable"), AlphaVar);
		if (!bEff || !bAlpha)
			Notes.Add(FString::Printf(TEXT("%s IK: bind manually -> EffectorLocation=%s, Alpha=%s"), *Side, *TargetVar, *AlphaVar));
		return Info;
	};
	Obj->SetObjectField(TEXT("left_ik"),  ConfigureIK(IKLGuid, TEXT("left"),  LeftVar));
	Obj->SetObjectField(TEXT("right_ik"), ConfigureIK(IKRGuid, TEXT("right"), RightVar));

	// Wire: source -> L2C -> IK_L -> IK_R -> C2L -> Output Pose.
	auto Connect = [&](const FString& From, const FString& To, const FString& Label)
	{
		if (From.IsEmpty() || To.IsEmpty()) { Log.Skip(FString::Printf(TEXT("connect %s"), *Label), TEXT("endpoint node missing")); return; }
		FString J, E;
		HandleConnectAnimNodes(AnimBPPath, From, To, J, E);
		Log.Record(FString::Printf(TEXT("connect %s"), *Label), J, E);
	};
	if (!SourceGuid.IsEmpty()) Connect(SourceGuid, L2CGuid, SourceTitle + TEXT(" -> LocalToComponent"));
	Connect(L2CGuid, IKLGuid, TEXT("LocalToComponent -> IK left"));
	Connect(IKLGuid, IKRGuid, TEXT("IK left -> IK right"));
	Connect(IKRGuid, C2LGuid, TEXT("IK right -> ComponentToLocal"));
	Connect(C2LGuid, Root->NodeGuid.ToString(), TEXT("ComponentToLocal -> Output Pose"));

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBP);
	AnimGraph->NotifyGraphChanged();
	RefreshOpenAnimBlueprintEditor(AnimBP);

	Suppress.Reset();
	Obj->SetBoolField(TEXT("success"), Log.NumFailed == 0);
	if (Log.NumFailed > 0) Obj->SetStringField(TEXT("error"), TEXT("Foot IK chain built with failed sub-steps; see steps[]/failed_steps."));
	Obj->SetStringField(TEXT("anim_blueprint_path"), AnimBPPath);
	Obj->SetStringField(TEXT("local_to_component_guid"), L2CGuid);
	Obj->SetStringField(TEXT("component_to_local_guid"), C2LGuid);
	Obj->SetStringField(TEXT("previous_source_guid"), SourceGuid);
	Obj->SetArrayField(TEXT("variables"), ToJsonStringArray({ LeftVar, RightVar, AlphaVar }));
	if (Notes.Num() > 0) Obj->SetArrayField(TEXT("notes"), ToJsonStringArray(Notes));
	Log.WriteTo(Obj);
	CompileAnimBlueprintAndReport(AnimBP, Obj);
	UEditorAssetLibrary::SaveAsset(AnimBPPath, false);
	BuildSuccessJson(Obj, OutJsonString);
	if (Log.NumFailed > 0) OutError = FString::Printf(TEXT("setup_foot_ik: %d sub-step(s) failed (%s)."), Log.NumFailed, *FString::Join(Log.FailedNames, TEXT(", ")));
}

}
