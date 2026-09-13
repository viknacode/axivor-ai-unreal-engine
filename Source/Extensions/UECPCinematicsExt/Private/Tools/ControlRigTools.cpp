// Copyright 2026, BlueprintsLab, All rights reserved.

#include "Tools/ControlRigTools.h"
#include "Managers/CapabilityProfile.h"
#include "Tools/BatchToolHelper.h"
#include "Managers/SettingsManager.h"

#include "EditorAssetLibrary.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/Package.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/Skeleton.h"
#include "UObject/UObjectIterator.h"
#include "Factories/Factory.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Logging/TokenizedMessage.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/EngineVersionComparison.h"
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7)
#include "ControlRigBlueprintLegacy.h"
#else
#include "ControlRigBlueprint.h"
#endif
#include "RigVMModel/RigVMController.h"
#include "RigVMModel/RigVMGraph.h"
#include "RigVMModel/RigVMNode.h"
#include "RigVMModel/RigVMPin.h"
#include "RigVMModel/Nodes/RigVMUnitNode.h"
#include "RigVMModel/Nodes/RigVMVariableNode.h"
#include "RigVMModel/Nodes/RigVMLibraryNode.h"
#include "RigVMModel/Nodes/RigVMFunctionReferenceNode.h"
#include "RigVMModel/RigVMFunctionLibrary.h"
#include "Rigs/RigHierarchyController.h"
#include "Rigs/RigHierarchy.h"
#include "Rigs/RigControlHierarchy.h"

namespace ControlRigTools
{

static UClass* GetControlRigBlueprintClass()
{
	static UClass* Cached = nullptr;
	if (Cached) return Cached;
	const TCHAR* Paths[] = {
		TEXT("/Script/ControlRigEditor.ControlRigBlueprint"),
		TEXT("/Script/ControlRig.ControlRigBlueprint"),
	};
	for (const TCHAR* Path : Paths)
	{
		Cached = FindObject<UClass>(nullptr, Path);
		if (Cached) return Cached;
	}
	for (TObjectIterator<UClass> It; It; ++It)
	{
		if (It->GetName() == TEXT("ControlRigBlueprint"))
		{
			Cached = *It;
			return Cached;
		}
	}
	return nullptr;
}

static UClass* GetControlRigBlueprintFactoryClass()
{
	static UClass* Cached = nullptr;
	if (Cached) return Cached;
	const TCHAR* Paths[] = {
		TEXT("/Script/ControlRigEditor.ControlRigBlueprintFactory"),
		TEXT("/Script/ControlRig.ControlRigBlueprintFactory"),
	};
	for (const TCHAR* Path : Paths)
	{
		Cached = FindObject<UClass>(nullptr, Path);
		if (Cached) return Cached;
	}
	for (TObjectIterator<UClass> It; It; ++It)
	{
		if (It->GetName() == TEXT("ControlRigBlueprintFactory"))
		{
			Cached = *It;
			return Cached;
		}
	}
	return nullptr;
}

void HandleCreateControlRig(const FString& AssetName, const FString& SavePath,
	const FString& SkeletalMeshPath, FString& OutJsonString, FString& OutError)
{

	UClass* CRBPClass = GetControlRigBlueprintClass();
	if (!CRBPClass)
	{
		OutError = TEXT("ControlRigBlueprint class not found. Ensure ControlRig plugin is enabled.");
		return;
	}

	if (AssetName.IsEmpty()) { OutError = TEXT("name is required"); return; }
	FString PackagePath = SavePath;
	while (PackagePath.EndsWith(TEXT("/"))) PackagePath = PackagePath.LeftChop(1);
	PackagePath += TEXT("/") + AssetName;

	if (FPackageName::DoesPackageExist(PackagePath))
	{
		OutJsonString = FString::Printf(TEXT("{\"success\":true,\"asset_path\":\"%s\",\"message\":\"ControlRig already exists.\"}"), *PackagePath);
		return;
	}

	UObject* CRBlueprint = nullptr;

	USkeletalMesh* SkMesh = nullptr;
	USkeleton* Skel = nullptr;
	if (!SkeletalMeshPath.IsEmpty())
	{
		UObject* PreviewAsset = UEditorAssetLibrary::LoadAsset(SkeletalMeshPath);
		if (!PreviewAsset)
		{
			OutError = FString::Printf(TEXT("SkeletalMesh not found: %s"), *SkeletalMeshPath);
			return;
		}
		SkMesh = Cast<USkeletalMesh>(PreviewAsset);
		if (SkMesh)
		{
			Skel = SkMesh->GetSkeleton();
		}
		else if (USkeleton* AsSkeleton = Cast<USkeleton>(PreviewAsset))
		{
			Skel = AsSkeleton;
			SkMesh = Skel->GetPreviewMesh(false);
		}
		else
		{
			OutError = FString::Printf(
				TEXT("skeletal_mesh_path '%s' is a %s, not a SkeletalMesh or Skeleton. Pass a SkeletalMesh asset (e.g. SKM_Manny) so the rig can import its bone hierarchy."),
				*SkeletalMeshPath, *PreviewAsset->GetClass()->GetName());
			return;
		}
	}

	{
		UClass* FactoryClass = GetControlRigBlueprintFactoryClass();
		UObject* Factory = FactoryClass ? NewObject<UObject>(GetTransientPackage(), FactoryClass) : nullptr;

		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
		CRBlueprint = AssetTools.CreateAsset(AssetName, SavePath, CRBPClass, Cast<UFactory>(Factory));
	}

	if (!CRBlueprint) { OutError = TEXT("Failed to create ControlRig Blueprint"); return; }

	int32 ImportedBoneCount = 0;
	FString BoundMeshPath;
	if (SkMesh || Skel)
	{
		UControlRigBlueprint* CRBP = static_cast<UControlRigBlueprint*>(CRBlueprint);

		if (SkMesh)
		{
			if (FSoftObjectProperty* PreviewProp = FindFProperty<FSoftObjectProperty>(CRBlueprint->GetClass(), TEXT("PreviewSkeletalMesh")))
			{
				FSoftObjectPtr SoftValue(SkMesh);
				PreviewProp->SetPropertyValue_InContainer(CRBlueprint, SoftValue);
				BoundMeshPath = SkMesh->GetPathName();
			}
		}

		if (CRBP)
		{
			if (URigHierarchy* Hier = CRBP->GetHierarchy())
			{
				if (URigHierarchyController* HCtrl = Hier->GetController(true))
				{
#if UE_VERSION_OLDER_THAN(5,6,0)
					USkeleton* ImportSkel = Skel ? Skel : (SkMesh ? SkMesh->GetSkeleton() : nullptr);
					if (ImportSkel)
						ImportedBoneCount = HCtrl->ImportBones(ImportSkel, NAME_None, true, true, false, false).Num();
#else
					if (SkMesh)
						ImportedBoneCount = HCtrl->ImportBonesFromSkeletalMesh(SkMesh, NAME_None, true, true, false, false).Num();
					else if (Skel)
						ImportedBoneCount = HCtrl->ImportBones(Skel, NAME_None, true, true, false, false).Num();
#endif
				}
			}
		}
	}

	if (!CRBlueprint->GetPathName().Contains(SavePath))
	{
		UEditorAssetLibrary::RenameAsset(CRBlueprint->GetPathName(), PackagePath);
	}

	{
		UControlRigBlueprint* CRBP = static_cast<UControlRigBlueprint*>(CRBlueprint);
		URigVMGraph* Graph = CRBP ? CRBP->GetDefaultModel() : nullptr;
		if (Graph)
		{
			URigVMController* Ctrl = CRBP->GetController(Graph);
			if (Ctrl)
			{
				bool bHasEntry = false;
				for (URigVMNode* N : Graph->GetNodes())
					if (N->GetName().Contains(TEXT("BeginExecution"))) { bHasEntry = true; break; }
				if (!bHasEntry)
					Ctrl->AddUnitNodeFromStructPath(TEXT("/Script/ControlRig.RigUnit_BeginExecution"), TEXT("Execute"), FVector2D(-400.f, 0.f), TEXT(""), true, false);
			}
		}
	}

	CRBlueprint->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(CRBlueprint->GetPathName(), false);

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"asset_path\":\"%s\",\"preview_skeletal_mesh\":\"%s\",\"bones_imported\":%d,\"message\":\"ControlRig created with BeginExecution entry node. Add controls via add_rig_control, then use build_rig_logic or add_rig_vm_node to build the ForwardSolve graph.\"}"),
		*CRBlueprint->GetPathName(), *BoundMeshPath, ImportedBoneCount);
}

static UControlRigBlueprint* LoadCRBlueprint(const FString& AssetPath, FString& OutError)
{
	UClass* CRBPClass = GetControlRigBlueprintClass();
	if (!CRBPClass) { OutError = TEXT("ControlRigBlueprint class not found. Ensure ControlRig plugin is enabled."); return nullptr; }
	UObject* Obj = StaticLoadObject(CRBPClass, nullptr, *AssetPath);
	if (!Obj) { OutError = FString::Printf(TEXT("ControlRigBlueprint not found: %s"), *AssetPath); return nullptr; }
	return static_cast<UControlRigBlueprint*>(Obj);
}

static URigVMController* GetRVMController(const FString& AssetPath, UControlRigBlueprint*& OutBP, FString& OutError)
{
	OutBP = LoadCRBlueprint(AssetPath, OutError);
	if (!OutBP) return nullptr;
	URigVMGraph* Graph = OutBP->GetDefaultModel();
	if (!Graph) { OutError = TEXT("No default RigVM model found."); return nullptr; }
	URigVMController* C = OutBP->GetController(Graph);
	if (!C) { OutError = TEXT("Could not get RigVMController."); return nullptr; }
	return C;
}

static bool ResolveEventToken(const FString& Token, FString& OutEntryStructPath, FString& OutDisplayName)
{
	const FString T = Token.ToLower().TrimStartAndEnd();
	if (T.IsEmpty() || T == TEXT("forward_solve") || T == TEXT("forward") || T == TEXT("forwardsolve") || T == TEXT("solve") || T == TEXT("begin") || T == TEXT("beginexecution"))
	{
		OutEntryStructPath = TEXT("/Script/ControlRig.RigUnit_BeginExecution");
		OutDisplayName     = TEXT("ForwardSolve");
		return true;
	}
	if (T == TEXT("construction") || T == TEXT("construction_event") || T == TEXT("constructionevent") || T == TEXT("prepare") || T == TEXT("prepareforexecution") || T == TEXT("init") || T == TEXT("setup"))
	{
		OutEntryStructPath = TEXT("/Script/ControlRig.RigUnit_PrepareForExecution");
		OutDisplayName     = TEXT("Construction");
		return true;
	}
	if (T == TEXT("backward_solve") || T == TEXT("backwardsolve") || T == TEXT("backward") || T == TEXT("inverse") || T == TEXT("inverseexecution"))
	{
		OutEntryStructPath = TEXT("/Script/ControlRig.RigUnit_InverseExecution");
		OutDisplayName     = TEXT("BackwardSolve");
		return true;
	}
	if (T == TEXT("interaction") || T == TEXT("interaction_event") || T == TEXT("interactionevent") || T == TEXT("interactionexecution"))
	{
		OutEntryStructPath = TEXT("/Script/ControlRig.RigUnit_InteractionExecution");
		OutDisplayName     = TEXT("Interaction");
		return true;
	}
	return false;
}

static URigVMController* GetRVMControllerForEvent(const FString& AssetPath,
	const FString& EventToken, UControlRigBlueprint*& OutBP, FString& OutError,
	FString* OutResolvedEvent = nullptr)
{
	OutBP = LoadCRBlueprint(AssetPath, OutError);
	if (!OutBP) return nullptr;

	FString EntryStructPath, DisplayName;
	if (!ResolveEventToken(EventToken, EntryStructPath, DisplayName))
	{
		OutError = FString::Printf(TEXT("event '%s' not recognised. Valid: forward_solve (default), construction, backward_solve, interaction."), *EventToken);
		return nullptr;
	}
	if (OutResolvedEvent) *OutResolvedEvent = DisplayName;

	const TArray<URigVMGraph*> AllModels = OutBP->GetAllModels();
	URigVMGraph* TargetGraph = nullptr;

	for (URigVMGraph* G : AllModels)
	{
		if (!G) continue;
		for (URigVMNode* N : G->GetNodes())
		{
			URigVMUnitNode* UN = Cast<URigVMUnitNode>(N);
			if (!UN || !UN->GetScriptStruct()) continue;
			if (UN->GetScriptStruct()->GetPathName() == EntryStructPath)
			{
				TargetGraph = G;
				break;
			}
		}
		if (TargetGraph) break;
	}

	if (!TargetGraph) TargetGraph = OutBP->GetDefaultModel();
	if (!TargetGraph) { OutError = TEXT("No RigVM model available."); return nullptr; }

	URigVMController* C = OutBP->GetController(TargetGraph);
	if (!C) { OutError = TEXT("Could not get RigVMController for the resolved graph."); return nullptr; }

	bool bHasEntry = false;
	for (URigVMNode* N : TargetGraph->GetNodes())
	{
		URigVMUnitNode* UN = Cast<URigVMUnitNode>(N);
		if (UN && UN->GetScriptStruct() && UN->GetScriptStruct()->GetPathName() == EntryStructPath)
		{
			bHasEntry = true;
			break;
		}
	}
	if (!bHasEntry)
	{
		C->AddUnitNodeFromStructPath(EntryStructPath, TEXT("Execute"), FVector2D(-400.f, 0.f), TEXT(""), true, false);
	}

	return C;
}

static URigVMController* GetRVMControllerForFunctionBody(const FString& AssetPath,
	const FString& FunctionName, UControlRigBlueprint*& OutBP, FString& OutError)
{
	OutBP = LoadCRBlueprint(AssetPath, OutError);
	if (!OutBP) return nullptr;
	URigVMFunctionLibrary* Lib = OutBP->GetLocalFunctionLibrary();
	if (!Lib) { OutError = TEXT("No local function library on this rig."); return nullptr; }
	URigVMLibraryNode* Fn = Lib->FindFunction(FName(*FunctionName));
	if (!Fn) { OutError = FString::Printf(TEXT("function '%s' not found in the library. Create it with add_rig_function first."), *FunctionName); return nullptr; }
	URigVMGraph* Body = Fn->GetContainedGraph();
	if (!Body) { OutError = FString::Printf(TEXT("function '%s' has no contained graph."), *FunctionName); return nullptr; }
	URigVMController* C = OutBP->GetController(Body);
	if (!C) { OutError = TEXT("Could not get RigVMController for the function body."); return nullptr; }
	return C;
}

static URigVMController* GetRVMControllerForTarget(const FString& AssetPath,
	const FString& EventName, const FString& FunctionName,
	UControlRigBlueprint*& OutBP, FString& OutError, FString* OutResolved = nullptr)
{
	if (!FunctionName.IsEmpty())
	{
		URigVMController* C = GetRVMControllerForFunctionBody(AssetPath, FunctionName, OutBP, OutError);
		if (OutResolved) *OutResolved = FString::Printf(TEXT("function:%s"), *FunctionName);
		return C;
	}
	return GetRVMControllerForEvent(AssetPath, EventName, OutBP, OutError, OutResolved);
}

static bool FindExecChainTail(URigVMGraph* Graph, FString& OutTailExecPath)
{
	if (!Graph) return false;
	URigVMNode* Entry = nullptr;
	for (URigVMNode* N : Graph->GetNodes())
	{
		URigVMUnitNode* UN = Cast<URigVMUnitNode>(N);
		if (UN && UN->GetScriptStruct() &&
			UN->GetScriptStruct()->GetPathName() == TEXT("/Script/ControlRig.RigUnit_BeginExecution"))
		{
			Entry = N;
			break;
		}
	}
	if (!Entry) return false;

	URigVMNode* Current = Entry;
	for (int32 Hop = 0; Hop < 256; ++Hop)
	{
		URigVMPin* OutPin = Current->FindPin(TEXT("ExecutePin"));
		if (!OutPin) break;
		TArray<URigVMPin*> Linked = OutPin->GetLinkedTargetPins();
		if (Linked.Num() == 0)
		{
			OutTailExecPath = Current->GetNodePath() + TEXT(".ExecutePin");
			return true;
		}
		URigVMPin* TargetPin = Linked[0];
		if (!TargetPin || !TargetPin->GetNode()) break;
		Current = TargetPin->GetNode();
	}
	return false;
}

static ERigControlType StringToControlType(const FString& S)
{
	if (S.Equals(TEXT("Float"),    ESearchCase::IgnoreCase)) return ERigControlType::Float;
	if (S.Equals(TEXT("Vector"),   ESearchCase::IgnoreCase)) return ERigControlType::Position;
	if (S.Equals(TEXT("Rotator"),  ESearchCase::IgnoreCase)) return ERigControlType::Rotator;
	if (S.Equals(TEXT("Bool"),     ESearchCase::IgnoreCase)) return ERigControlType::Bool;
	if (S.Equals(TEXT("Integer"),  ESearchCase::IgnoreCase)) return ERigControlType::Integer;
	return ERigControlType::Transform;
}

static FName DefaultShapeNameForType(ERigControlType )
{
	return NAME_None;
}

static FTransform DefaultShapeTransformForType(ERigControlType InType)
{
	switch (InType)
	{
		case ERigControlType::Float:
		case ERigControlType::Bool:
		case ERigControlType::Integer:
			return FTransform(FQuat::Identity, FVector::ZeroVector, FVector(1.0f));
		default:
			return FTransform(FQuat::Identity, FVector::ZeroVector, FVector(2.0f));
	}
}

static FLinearColor DefaultShapeColorForName(const FString& ControlName)
{
	if (ControlName.EndsWith(TEXT("_L"), ESearchCase::IgnoreCase) ||
		ControlName.EndsWith(TEXT("_Left"), ESearchCase::IgnoreCase))
	{
		return FLinearColor(1.0f, 0.85f, 0.1f);
	}
	if (ControlName.EndsWith(TEXT("_R"), ESearchCase::IgnoreCase) ||
		ControlName.EndsWith(TEXT("_Right"), ESearchCase::IgnoreCase))
	{
		return FLinearColor(0.1f, 0.85f, 0.2f);
	}
	if (ControlName.EndsWith(TEXT("_C"), ESearchCase::IgnoreCase) ||
		ControlName.EndsWith(TEXT("_Center"), ESearchCase::IgnoreCase))
	{
		return FLinearColor(0.3f, 0.6f, 1.0f);
	}
	return FLinearColor(0.85f, 0.85f, 0.85f);
}

static void AddRigControlImpl(const FString& AssetPath, const FString& ControlName,
	const FString& ControlType, const FString& ParentBone,
	const TSharedPtr<FJsonObject>& OptionsArgs,
	FString& OutJsonString, FString& OutError)
{
	UControlRigBlueprint* BP = LoadCRBlueprint(AssetPath, OutError);
	if (!BP) return;

	URigHierarchy* Hier = BP->GetHierarchy();
	if (!Hier) { OutError = TEXT("Could not get RigHierarchy."); return; }
	URigHierarchyController* HCtrl = Hier->GetController(true);
	if (!HCtrl) { OutError = TEXT("Could not get RigHierarchyController."); return; }

	FRigControlSettings Settings;
	Settings.ControlType = StringToControlType(ControlType);
	Settings.DisplayName = FName(*ControlName);

	{
		FString ParentControl;
		if (OptionsArgs.IsValid()) OptionsArgs->TryGetStringField(TEXT("parent_control"), ParentControl);
		if (!ParentControl.IsEmpty())
		{
			const FRigElementKey ParentCtrlKey(FName(*ParentControl), ERigElementType::Control);
			if (!Hier->Find<FRigControlElement>(ParentCtrlKey))
			{
				OutError = FString::Printf(TEXT("parent_control '%s' not found (must be an existing Control)."), *ParentControl);
				return;
			}
			FRigElementKey ChannelKey = HCtrl->AddAnimationChannel(
				FName(*ControlName), ParentCtrlKey, Settings, false, false);
			if (!ChannelKey.IsValid())
			{
				OutError = FString::Printf(TEXT("Failed to add animation channel '%s' under '%s'."), *ControlName, *ParentControl);
				return;
			}
			FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
			UEditorAssetLibrary::SaveAsset(BP->GetPathName(), false);
			OutJsonString = FString::Printf(
				TEXT("{\"success\":true,\"control_name\":\"%s\",\"control_type\":\"%s\",\"animation_channel\":true,\"parent_control\":\"%s\"}"),
				*ControlName, *ControlType, *ParentControl);
			return;
		}
	}

	Settings.ShapeName  = DefaultShapeNameForType(Settings.ControlType);
	Settings.ShapeColor = DefaultShapeColorForName(ControlName);

	if (OptionsArgs.IsValid())
	{
		FString ShapeNameStr;
		if (OptionsArgs->TryGetStringField(TEXT("shape_name"), ShapeNameStr) && !ShapeNameStr.IsEmpty())
			Settings.ShapeName = FName(*ShapeNameStr);

		FString ShapeColorStr;
		if (OptionsArgs->TryGetStringField(TEXT("shape_color"), ShapeColorStr) && !ShapeColorStr.IsEmpty())
		{
			FLinearColor Parsed;
			if (Parsed.InitFromString(ShapeColorStr))
				Settings.ShapeColor = Parsed;
		}
	}

	FRigElementKey ParentKey;
	FString GenericParent;
	if (OptionsArgs.IsValid()) OptionsArgs->TryGetStringField(TEXT("parent"), GenericParent);
	if (!GenericParent.IsEmpty())
	{
		for (ERigElementType T : { ERigElementType::Control, ERigElementType::Null, ERigElementType::Bone })
		{
			FRigElementKey Candidate(FName(*GenericParent), T);
			if (Hier->Contains(Candidate)) { ParentKey = Candidate; break; }
		}
		if (!ParentKey.IsValid())
		{
			OutError = FString::Printf(TEXT("parent '%s' not found as Control/Null/Bone in hierarchy."), *GenericParent);
			return;
		}
	}
	else if (!ParentBone.IsEmpty())
	{
		ParentKey = FRigElementKey(FName(*ParentBone), ERigElementType::Bone);
		if (!Hier->Contains(ParentKey))
		{
			const int32 BoneCount = Hier->GetBones().Num();
			OutError = FString::Printf(
				TEXT("parent_bone '%s' not found in the rig hierarchy (bone_count=%d). %s"),
				*ParentBone, BoneCount,
				BoneCount == 0
					? TEXT("The rig has NO bones — it needs a skeletal mesh assigned first (create the Control Rig with skeletal_mesh_path, or the bone hierarchy won't import).")
					: TEXT("Call get_control_rig_summary to see the actual bone names."));
			return;
		}
	}

	FTransform OffsetTransform = FTransform::Identity;
	FString TargetBoneStr;
	if (OptionsArgs.IsValid() && OptionsArgs->TryGetStringField(TEXT("target_bone"), TargetBoneStr) && !TargetBoneStr.IsEmpty())
	{
		const FRigElementKey TargetKey(FName(*TargetBoneStr), ERigElementType::Bone);
		if (Hier->Find<FRigBoneElement>(TargetKey))
		{
			const FTransform TargetGlobal = Hier->GetInitialGlobalTransform(TargetKey);
			if (ParentKey.IsValid())
			{
				const FTransform ParentGlobal = Hier->GetInitialGlobalTransform(ParentKey);
				OffsetTransform = TargetGlobal.GetRelativeTransform(ParentGlobal);
			}
			else
			{
				OffsetTransform = TargetGlobal;
			}
		}
		else
		{
			OutError = FString::Printf(TEXT("target_bone '%s' not found in rig hierarchy."), *TargetBoneStr);
			return;
		}
	}

	FTransform ShapeTransform = DefaultShapeTransformForType(Settings.ControlType);
	if (OptionsArgs.IsValid())
	{
		double ShapeScale = 0.0;
		if (OptionsArgs->TryGetNumberField(TEXT("shape_scale"), ShapeScale) && ShapeScale > 0.0)
		{
			ShapeTransform.SetScale3D(FVector((float)ShapeScale));
		}
	}

	bool bShapeOffsetApplied = false;
	{
		bool bWantDefaultOffset = !ParentBone.IsEmpty() && TargetBoneStr.IsEmpty();
		FString ShapeOffsetStr;
		if (OptionsArgs.IsValid() && OptionsArgs->TryGetStringField(TEXT("shape_offset"), ShapeOffsetStr) && !ShapeOffsetStr.IsEmpty())
		{
			FVector ExplicitOffset;
			if (ExplicitOffset.InitFromString(ShapeOffsetStr))
			{
				ShapeTransform.SetTranslation(ExplicitOffset);
				bShapeOffsetApplied = true;
				bWantDefaultOffset = false;
			}
		}
		if (bWantDefaultOffset)
		{
			const FRigElementKey ParentBoneKey(FName(*ParentBone), ERigElementType::Bone);
			if (Hier->Contains(ParentBoneKey))
			{
				const FQuat BoneRot = Hier->GetInitialGlobalTransform(ParentBoneKey).GetRotation();
				const FVector LocalForward = BoneRot.UnrotateVector(FVector(20.f, 0.f, 0.f));
				ShapeTransform.SetTranslation(LocalForward);
				bShapeOffsetApplied = true;
			}
		}
	}

	FRigElementKey ControlKey = HCtrl->AddControl(
		FName(*ControlName), ParentKey, Settings,
		Settings.GetIdentityValue(), OffsetTransform, ShapeTransform);

	if (!ControlKey.IsValid()) { OutError = FString::Printf(TEXT("Failed to add control '%s'."), *ControlName); return; }

	FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
	UEditorAssetLibrary::SaveAsset(BP->GetPathName(), false);

	const TCHAR* ParentTypeStr = ParentKey.IsValid()
		? (ParentKey.Type == ERigElementType::Bone ? TEXT("Bone")
			: ParentKey.Type == ERigElementType::Null ? TEXT("Null")
			: ParentKey.Type == ERigElementType::Control ? TEXT("Control") : TEXT("Other"))
		: TEXT("None");
	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"control_name\":\"%s\",\"control_type\":\"%s\",\"parent_bone\":\"%s\","
			 "\"parent\":\"%s\",\"parent_type\":\"%s\","
			 "\"shape_name\":\"%s\",\"shape_color\":\"%s\",\"target_bone\":\"%s\",\"shape_offset_applied\":%s}"),
		*ControlName, *ControlType, *ParentBone,
		*ParentKey.Name.ToString(), ParentTypeStr,
		*Settings.ShapeName.ToString(), *Settings.ShapeColor.ToString(), *TargetBoneStr,
		bShapeOffsetApplied ? TEXT("true") : TEXT("false"));
}

void HandleAddRigControl(const FString& AssetPath, const FString& ControlName,
	const FString& ControlType, const FString& ParentBone,
	FString& OutJsonString, FString& OutError)
{
	AddRigControlImpl(AssetPath, ControlName, ControlType, ParentBone,
		nullptr, OutJsonString, OutError);
}

static void SnapControlOffsetToGlobal(URigHierarchy* Hier, const FRigElementKey& ControlKey, const FTransform& DesiredGlobal)
{
	if (!Hier) return;
	FTransform LocalOffset = DesiredGlobal;
	const FRigElementKey ParentKey = Hier->GetFirstParent(ControlKey);
	if (ParentKey.IsValid())
	{
		const FTransform ParentGlobal = Hier->GetInitialGlobalTransform(ParentKey);
		LocalOffset = DesiredGlobal.GetRelativeTransform(ParentGlobal);
	}
	Hier->SetControlOffsetTransform(ControlKey, LocalOffset, true, true, false, false);
}

static void AddRigTwoBoneIKImpl(const FString& AssetPath,
	const FString& RootBone, const FString& MidBone, const FString& TipBone,
	int32 PosX, int32 PosY, const TSharedPtr<FJsonObject>& OptionsArgs,
	FString& OutJsonString, FString& OutError)
{
	UControlRigBlueprint* BP = nullptr;
	URigVMController* Controller = GetRVMController(AssetPath, BP, OutError);
	if (!Controller) return;

	URigVMUnitNode* IKNode = Controller->AddUnitNodeFromStructPath(
		TEXT("/Script/ControlRig.RigUnit_TwoBoneIKSimplePerItem"),
		TEXT("Execute"), FVector2D((float)PosX, (float)PosY), TEXT(""), true, false);

	if (!IKNode) { OutError = TEXT("Failed to spawn TwoBoneIK unit node."); return; }

	auto MakeRigKey = [](const FString& BoneName, const TCHAR* ElemType) -> FString {
		return FString::Printf(TEXT("(Type=%s,Name=\"%s\")"), ElemType, *BoneName);
	};

	const FString NodePath = IKNode->GetNodePath();
	if (!RootBone.IsEmpty()) Controller->SetPinDefaultValue(NodePath + TEXT(".ItemA"),        MakeRigKey(RootBone, TEXT("Bone")), true, true);
	if (!MidBone.IsEmpty())  Controller->SetPinDefaultValue(NodePath + TEXT(".ItemB"),        MakeRigKey(MidBone,  TEXT("Bone")), true, true);
	if (!TipBone.IsEmpty())  Controller->SetPinDefaultValue(NodePath + TEXT(".EffectorItem"), MakeRigKey(TipBone,  TEXT("Bone")), true, true);

	URigVMGraph* Graph = IKNode->GetGraph();
	bool bAutoWiredExec = false;
	bool bAutoWiredEffector = false;
	bool bAutoWiredPole = false;
	bool bEffectorSnapped = false;
	FString EffectorControl, PoleControl;
	if (OptionsArgs.IsValid())
	{
		OptionsArgs->TryGetStringField(TEXT("effector_control"), EffectorControl);
		OptionsArgs->TryGetStringField(TEXT("pole_control"),     PoleControl);
	}

	if (!EffectorControl.IsEmpty() && !TipBone.IsEmpty())
	{
		if (URigHierarchy* Hier = BP->GetHierarchy())
		{
			const FRigElementKey CtrlKey(FName(*EffectorControl), ERigElementType::Control);
			const FRigElementKey TipKey (FName(*TipBone),         ERigElementType::Bone);
			if (Hier->Contains(CtrlKey) && Hier->Contains(TipKey))
			{
				SnapControlOffsetToGlobal(Hier, CtrlKey, Hier->GetInitialGlobalTransform(TipKey));
				bEffectorSnapped = true;
			}
		}
	}

	if (Graph)
	{
		FString TailExecPath;
		if (FindExecChainTail(Graph, TailExecPath))
		{
			const FString IKPin = NodePath + TEXT(".ExecutePin");
			bAutoWiredExec = Controller->AddLink(TailExecPath, IKPin, true);
		}

		auto WireControlToInput = [&](const FString& ControlName, const TCHAR* TargetInputPin,
			float YOffset, bool& OutWired) -> void
		{
			if (ControlName.IsEmpty()) return;
			URigVMUnitNode* GetNode = Controller->AddUnitNodeFromStructPath(
				TEXT("/Script/ControlRig.RigUnit_GetTransform"),
				TEXT("Execute"),
				FVector2D((float)PosX - 320.f, (float)PosY + YOffset),
				TEXT(""), true, false);
			if (!GetNode) return;
			const FString GetPath = GetNode->GetNodePath();
			Controller->SetPinDefaultValue(GetPath + TEXT(".Item"),
				MakeRigKey(ControlName, TEXT("Control")), true, true);
			OutWired = Controller->AddLink(
				GetPath + TEXT(".Transform"),
				NodePath + TEXT(".") + TargetInputPin,
				true);
		};
		WireControlToInput(EffectorControl, TEXT("Effector"),   0.f,    bAutoWiredEffector);
		WireControlToInput(PoleControl,     TEXT("PoleVector"), 200.f,  bAutoWiredPole);
	}

	FString PoleVectorStr;
	bool bPoleFromRestPose = false;
	bool bAxesFromRestPose = false;
	if (OptionsArgs.IsValid())
	{
		OptionsArgs->TryGetStringField(TEXT("pole_vector"), PoleVectorStr);
	}
	if (URigHierarchy* Hier = BP->GetHierarchy())
	{
		const FRigElementKey RootKey(FName(*RootBone), ERigElementType::Bone);
		const FRigElementKey MidKey (FName(*MidBone),  ERigElementType::Bone);
		const FRigElementKey TipKey (FName(*TipBone),  ERigElementType::Bone);
		if (Hier->Contains(RootKey) && Hier->Contains(MidKey) && Hier->Contains(TipKey))
		{
			const FTransform RootXf = Hier->GetGlobalTransform(RootKey, true);
			const FTransform MidXf  = Hier->GetGlobalTransform(MidKey,  true);
			const FVector RootP = RootXf.GetLocation();
			const FVector MidP  = MidXf.GetLocation();
			const FVector TipP  = Hier->GetGlobalTransform(TipKey, true).GetLocation();

			const FString Names = (RootBone + TEXT(" ") + MidBone + TEXT(" ") + TipBone).ToLower();
			const bool bIsLeg = Names.Contains(TEXT("leg")) || Names.Contains(TEXT("thigh")) ||
				Names.Contains(TEXT("calf")) || Names.Contains(TEXT("shin")) ||
				Names.Contains(TEXT("knee")) || Names.Contains(TEXT("foot")) ||
				Names.Contains(TEXT("ankle"));
			const FVector WorldFacing = bIsLeg ? FVector(1.f, 0.f, 0.f) : FVector(-1.f, 0.f, 0.f);

			const FVector LimbDir = (TipP - RootP).GetSafeNormal();
			FVector PoleDir = (WorldFacing - LimbDir * FVector::DotProduct(WorldFacing, LimbDir)).GetSafeNormal();
			if (PoleDir.IsNearlyZero())
				PoleDir = bIsLeg ? FVector(0.f, 0.f, 1.f) : FVector(0.f, 0.f, -1.f);

			const FVector PrimaryWorld = (MidP - RootP).GetSafeNormal();
			FVector PrimaryLocal = RootXf.InverseTransformVectorNoScale(PrimaryWorld).GetSafeNormal();
			FVector SecondaryWorld = (PoleDir - PrimaryWorld * FVector::DotProduct(PoleDir, PrimaryWorld)).GetSafeNormal();
			FVector SecondaryLocal = RootXf.InverseTransformVectorNoScale(SecondaryWorld).GetSafeNormal();
			if (!PrimaryLocal.IsNearlyZero() && !SecondaryLocal.IsNearlyZero())
			{
				Controller->SetPinDefaultValue(NodePath + TEXT(".PrimaryAxis"),
					FString::Printf(TEXT("(X=%f,Y=%f,Z=%f)"), PrimaryLocal.X, PrimaryLocal.Y, PrimaryLocal.Z), true, true);
				Controller->SetPinDefaultValue(NodePath + TEXT(".SecondaryAxis"),
					FString::Printf(TEXT("(X=%f,Y=%f,Z=%f)"), SecondaryLocal.X, SecondaryLocal.Y, SecondaryLocal.Z), true, true);
				bAxesFromRestPose = true;
			}

			if (PoleVectorStr.IsEmpty() && PoleControl.IsEmpty())
			{
				const float LimbLen = FVector::Dist(RootP, MidP) + FVector::Dist(MidP, TipP);
				const FVector PoleLoc = MidP + PoleDir * FMath::Max(LimbLen, 60.f);
				PoleVectorStr = FString::Printf(TEXT("(X=%f,Y=%f,Z=%f)"), PoleLoc.X, PoleLoc.Y, PoleLoc.Z);
				bPoleFromRestPose = true;
			}
		}
	}
	if (!PoleVectorStr.IsEmpty())
	{
		Controller->SetPinDefaultValue(NodePath + TEXT(".PoleVector"), PoleVectorStr, true, true);
		if (bPoleFromRestPose)
			Controller->SetPinDefaultValue(NodePath + TEXT(".PoleVectorKind"), TEXT("Location"), true, true);
	}

	FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
	UEditorAssetLibrary::SaveAsset(BP->GetPathName(), false);

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"node_name\":\"%s\",\"root_bone\":\"%s\",\"mid_bone\":\"%s\",\"tip_bone\":\"%s\","
			 "\"auto_wired_exec\":%s,\"auto_wired_effector\":%s,\"auto_wired_pole\":%s,"
			 "\"effector_snapped_to_tip\":%s,\"axes_from_rest_pose\":%s,"
			 "\"pole_vector_from_rest_pose\":%s,\"pole_vector\":\"%s\","
			 "\"effector_control\":\"%s\",\"pole_control\":\"%s\"}"),
		*IKNode->GetName(), *RootBone, *MidBone, *TipBone,
		bAutoWiredExec     ? TEXT("true") : TEXT("false"),
		bAutoWiredEffector ? TEXT("true") : TEXT("false"),
		bAutoWiredPole     ? TEXT("true") : TEXT("false"),
		bEffectorSnapped   ? TEXT("true") : TEXT("false"),
		bAxesFromRestPose  ? TEXT("true") : TEXT("false"),
		bPoleFromRestPose  ? TEXT("true") : TEXT("false"),
		*PoleVectorStr.ReplaceCharWithEscapedChar(),
		*EffectorControl, *PoleControl);
}

void HandleAddRigTwoBoneIK(const FString& AssetPath,
	const FString& RootBone, const FString& MidBone, const FString& TipBone,
	int32 PosX, int32 PosY, FString& OutJsonString, FString& OutError)
{
	AddRigTwoBoneIKImpl(AssetPath, RootBone, MidBone, TipBone, PosX, PosY,
		nullptr, OutJsonString, OutError);
}

static FString ControlTypeToString(ERigControlType InType)
{
	switch (InType)
	{
		case ERigControlType::Float:           return TEXT("Float");
		case ERigControlType::Bool:            return TEXT("Bool");
		case ERigControlType::Integer:         return TEXT("Integer");
		case ERigControlType::Position:        return TEXT("Vector");
		case ERigControlType::Rotator:         return TEXT("Rotator");
		case ERigControlType::Scale:           return TEXT("Scale");
		case ERigControlType::Transform:       return TEXT("Transform");
		case ERigControlType::TransformNoScale:return TEXT("TransformNoScale");
		case ERigControlType::EulerTransform:  return TEXT("EulerTransform");
		default:                               return TEXT("Unknown");
	}
}

static FString AnimationTypeToString(ERigControlAnimationType InType)
{
	switch (InType)
	{
		case ERigControlAnimationType::AnimationControl: return TEXT("AnimationControl");
		case ERigControlAnimationType::AnimationChannel: return TEXT("AnimationChannel");
		case ERigControlAnimationType::ProxyControl:     return TEXT("ProxyControl");
		case ERigControlAnimationType::VisualCue:        return TEXT("VisualCue");
		default:                                         return TEXT("AnimationControl");
	}
}

static TSharedRef<FJsonObject> BuildControlInfoJson(const URigHierarchy* Hierarchy, FRigControlElement* Ctrl)
{
	TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetStringField(TEXT("name"), Ctrl->GetName());
	O->SetStringField(TEXT("type"), ControlTypeToString(Ctrl->Settings.ControlType));
	O->SetStringField(TEXT("animation_type"), AnimationTypeToString(Ctrl->Settings.AnimationType));
	O->SetStringField(TEXT("display_name"), Ctrl->Settings.DisplayName.ToString());
	O->SetStringField(TEXT("shape_name"), Ctrl->Settings.ShapeName.ToString());
	O->SetStringField(TEXT("shape_color"), Ctrl->Settings.ShapeColor.ToString());
	O->SetBoolField  (TEXT("shape_visible"), Ctrl->Settings.bShapeVisible);
	{
		const FRigElementKey ParentKey = Hierarchy->GetFirstParent(Ctrl->GetKey());
		if (ParentKey.IsValid())
		{
			O->SetStringField(TEXT("parent_name"), ParentKey.Name.ToString());
			O->SetStringField(TEXT("parent_type"),
				ParentKey.Type == ERigElementType::Bone ? TEXT("Bone") :
				ParentKey.Type == ERigElementType::Control ? TEXT("Control") :
				ParentKey.Type == ERigElementType::Null ? TEXT("Null") : TEXT("Other"));
		}
	}
	URigHierarchy* MutableH = const_cast<URigHierarchy*>(Hierarchy);
	const FTransform OffsetXf = MutableH->GetControlOffsetTransform(Ctrl, ERigTransformType::InitialLocal);
	const FTransform ShapeXf  = MutableH->GetControlShapeTransform (Ctrl, ERigTransformType::InitialLocal);
	const FTransform GlobalXf = MutableH->GetGlobalTransform(Ctrl->GetKey(), true);
	O->SetStringField(TEXT("offset_transform"), OffsetXf.ToString());
	O->SetStringField(TEXT("shape_transform"),  ShapeXf.ToString());
	O->SetStringField(TEXT("initial_global_transform"), GlobalXf.ToString());

	O->SetBoolField(TEXT("draw_limits"), Ctrl->Settings.bDrawLimits);
	{
		TArray<TSharedPtr<FJsonValue>> LimitFlags;
		bool bAnyLimit = false;
		for (const FRigControlLimitEnabled& L : Ctrl->Settings.LimitEnabled)
		{
			const bool bOn = L.bMinimum || L.bMaximum;
			bAnyLimit |= bOn;
			LimitFlags.Add(MakeShared<FJsonValueBoolean>(bOn));
		}
		O->SetArrayField(TEXT("limit_enabled"), LimitFlags);
		if (bAnyLimit)
		{
			const ERigControlType CT = Ctrl->Settings.ControlType;
			if (CT == ERigControlType::Float || CT == ERigControlType::ScaleFloat)
			{
				O->SetNumberField(TEXT("minimum_value"), Ctrl->Settings.MinimumValue.Get<float>());
				O->SetNumberField(TEXT("maximum_value"), Ctrl->Settings.MaximumValue.Get<float>());
			}
			else if (CT == ERigControlType::Integer)
			{
				O->SetNumberField(TEXT("minimum_value"), Ctrl->Settings.MinimumValue.Get<int32>());
				O->SetNumberField(TEXT("maximum_value"), Ctrl->Settings.MaximumValue.Get<int32>());
			}
		}
	}
	return O;
}

void HandleGetControlRigSummary(const FString& AssetPath,
	FString& OutJsonString, FString& OutError)
{
	UControlRigBlueprint* BP = LoadCRBlueprint(AssetPath, OutError);
	if (!BP) return;

	TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
	Out->SetBoolField  (TEXT("success"),    true);
	Out->SetStringField(TEXT("asset_path"), AssetPath);
	Out->SetStringField(TEXT("class"),      BP->GetClass()->GetName());

	if (const FSoftObjectProperty* PreviewProp = FindFProperty<FSoftObjectProperty>(BP->GetClass(), TEXT("PreviewSkeletalMesh")))
	{
		FSoftObjectPtr SoftPtr;
		PreviewProp->GetValue_InContainer(BP, &SoftPtr);
		Out->SetStringField(TEXT("preview_skeletal_mesh"), SoftPtr.ToString());
	}

	TArray<TSharedPtr<FJsonValue>> EventsJson;
	int32 TotalNodes = 0;
	for (URigVMGraph* G : BP->GetAllModels())
	{
		if (!G) continue;
		for (URigVMNode* N : G->GetNodes())
		{
			++TotalNodes;
			URigVMUnitNode* UN = Cast<URigVMUnitNode>(N);
			if (!UN || !UN->GetScriptStruct()) continue;
			const FString StructPath = UN->GetScriptStruct()->GetPathName();
			if (StructPath == TEXT("/Script/ControlRig.RigUnit_BeginExecution"))    EventsJson.Add(MakeShared<FJsonValueString>(TEXT("ForwardSolve")));
			else if (StructPath == TEXT("/Script/ControlRig.RigUnit_PrepareForExecution")) EventsJson.Add(MakeShared<FJsonValueString>(TEXT("Construction")));
			else if (StructPath == TEXT("/Script/ControlRig.RigUnit_InverseExecution"))    EventsJson.Add(MakeShared<FJsonValueString>(TEXT("BackwardSolve")));
			else if (StructPath == TEXT("/Script/ControlRig.RigUnit_InteractionExecution")) EventsJson.Add(MakeShared<FJsonValueString>(TEXT("Interaction")));
		}
	}
	Out->SetArrayField(TEXT("events"), EventsJson);
	Out->SetNumberField(TEXT("rig_vm_node_count"), TotalNodes);

	URigHierarchy* Hierarchy = BP->GetHierarchy();
	if (Hierarchy)
	{
		TArray<TSharedPtr<FJsonValue>> Bones, Controls, Nulls, Sockets, Curves;

		for (FRigBoneElement* E : Hierarchy->GetBones())
		{
			TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetStringField(TEXT("name"), E->GetName());
			const FRigElementKey P = Hierarchy->GetFirstParent(E->GetKey());
			if (P.IsValid()) O->SetStringField(TEXT("parent"), P.Name.ToString());
			Bones.Add(MakeShared<FJsonValueObject>(O));
		}
		for (FRigControlElement* C : Hierarchy->GetControls())
		{
			Controls.Add(MakeShared<FJsonValueObject>(BuildControlInfoJson(Hierarchy, C)));
		}
		for (FRigNullElement* E : Hierarchy->GetNulls())
		{
			TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetStringField(TEXT("name"), E->GetName());
			const FRigElementKey P = Hierarchy->GetFirstParent(E->GetKey());
			if (P.IsValid()) O->SetStringField(TEXT("parent"), P.Name.ToString());
			Nulls.Add(MakeShared<FJsonValueObject>(O));
		}
		for (FRigSocketElement* E : Hierarchy->GetSockets())
		{
			TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetStringField(TEXT("name"), E->GetName());
			const FRigElementKey P = Hierarchy->GetFirstParent(E->GetKey());
			if (P.IsValid()) O->SetStringField(TEXT("parent"), P.Name.ToString());
			Sockets.Add(MakeShared<FJsonValueObject>(O));
		}
		for (FRigCurveElement* E : Hierarchy->GetCurves())
		{
			TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
			O->SetStringField(TEXT("name"), E->GetName());
			O->SetNumberField(TEXT("value"), Hierarchy->GetCurveValue(E->GetKey()));
			Curves.Add(MakeShared<FJsonValueObject>(O));
		}

		Out->SetArrayField(TEXT("bones"),    Bones);
		Out->SetArrayField(TEXT("controls"), Controls);
		Out->SetArrayField(TEXT("nulls"),    Nulls);
		Out->SetArrayField(TEXT("sockets"),  Sockets);
		Out->SetArrayField(TEXT("curves"),   Curves);
		Out->SetNumberField(TEXT("bone_count"),    Bones.Num());
		Out->SetNumberField(TEXT("control_count"), Controls.Num());
		Out->SetNumberField(TEXT("null_count"),    Nulls.Num());
		Out->SetNumberField(TEXT("socket_count"),  Sockets.Num());
		Out->SetNumberField(TEXT("curve_count"),   Curves.Num());
	}

	FString Serialised;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Serialised);
	FJsonSerializer::Serialize(Out, Writer);
	OutJsonString = Serialised;
}

void HandleGetRigControl(const FString& AssetPath, const FString& ControlName,
	FString& OutJsonString, FString& OutError)
{
	UControlRigBlueprint* BP = LoadCRBlueprint(AssetPath, OutError);
	if (!BP) return;

	URigHierarchy* Hier = BP->GetHierarchy();
	if (!Hier) { OutError = TEXT("Could not get RigHierarchy."); return; }

	FRigControlElement* Ctrl = Hier->Find<FRigControlElement>(
		FRigElementKey(FName(*ControlName), ERigElementType::Control));
	if (!Ctrl) { OutError = FString::Printf(TEXT("Control '%s' not found."), *ControlName); return; }

	TSharedRef<FJsonObject> Out = BuildControlInfoJson(Hier, Ctrl);
	Out->SetBoolField(TEXT("success"), true);
	Out->SetStringField(TEXT("asset_path"), AssetPath);

	FString Serialised;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Serialised);
	FJsonSerializer::Serialize(Out, Writer);
	OutJsonString = Serialised;
}

void HandleGetRigControlFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString AssetPath, ControlName;
	if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath) ||
		!Args->TryGetStringField(TEXT("control_name"), ControlName))
	{
		OutError = TEXT("asset_path and control_name required");
		return;
	}
	HandleGetRigControl(AssetPath, ControlName, OutJsonString, OutError);
}

void HandleAddRigVMNode(const FString& AssetPath, const FString& UnitStructPath,
	const FString& MethodName, int32 PosX, int32 PosY,
	FString& OutJsonString, FString& OutError)
{

	UControlRigBlueprint* BP = nullptr;
	URigVMController* Controller = GetRVMController(AssetPath, BP, OutError);
	if (!Controller) return;

	FName MethodFName = MethodName.IsEmpty() ? TEXT("Execute") : FName(*MethodName);
	URigVMUnitNode* Node = Controller->AddUnitNodeFromStructPath(
		UnitStructPath, MethodFName, FVector2D((float)PosX, (float)PosY), TEXT(""), true, false);

	if (!Node) { OutError = FString::Printf(TEXT("Failed to add RigVM node for struct: %s"), *UnitStructPath); return; }

	FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"node_name\":\"%s\",\"node_path\":\"%s\"}"),
		*Node->GetName(), *Node->GetNodePath());
}

static void ResolveRigVMValueType(const FString& InType, FString& OutCppType, FName& OutObjectPath)
{
	const FString T = InType.IsEmpty() ? TEXT("FTransform") : InType;
	struct FMap { const TCHAR* In; const TCHAR* Cpp; const TCHAR* Obj; };
	static const FMap Types[] = {
		{ TEXT("FTransform"), TEXT("FTransform"), TEXT("/Script/CoreUObject.Transform") },
		{ TEXT("Transform"),  TEXT("FTransform"), TEXT("/Script/CoreUObject.Transform") },
		{ TEXT("FVector"),    TEXT("FVector"),    TEXT("/Script/CoreUObject.Vector") },
		{ TEXT("Vector"),     TEXT("FVector"),    TEXT("/Script/CoreUObject.Vector") },
		{ TEXT("FRotator"),   TEXT("FRotator"),   TEXT("/Script/CoreUObject.Rotator") },
		{ TEXT("Rotator"),    TEXT("FRotator"),   TEXT("/Script/CoreUObject.Rotator") },
		{ TEXT("FQuat"),      TEXT("FQuat"),      TEXT("/Script/CoreUObject.Quat") },
		{ TEXT("float"),      TEXT("float"),      TEXT("") },
		{ TEXT("double"),     TEXT("double"),     TEXT("") },
		{ TEXT("int32"),      TEXT("int32"),      TEXT("") },
		{ TEXT("int"),        TEXT("int32"),      TEXT("") },
		{ TEXT("bool"),       TEXT("bool"),       TEXT("") },
	};
	for (const FMap& M : Types)
	{
		if (T.Equals(M.In, ESearchCase::IgnoreCase))
		{
			OutCppType = M.Cpp;
			OutObjectPath = (*M.Obj) ? FName(M.Obj) : NAME_None;
			return;
		}
	}
	OutCppType = T;
	OutObjectPath = NAME_None;
}

static void AddRigVMNodeForEvent(const FString& AssetPath, const FString& UnitStructPath,
	const FString& MethodName, int32 PosX, int32 PosY, const FString& EventName,
	const FString& CppType, const FString& FunctionName, FString& OutJsonString, FString& OutError)
{
	UControlRigBlueprint* BP = nullptr;
	FString ResolvedEvent;
	URigVMController* Controller = GetRVMControllerForTarget(AssetPath, EventName, FunctionName, BP, OutError, &ResolvedEvent);
	if (!Controller) return;

	const FVector2D Pos((float)PosX, (float)PosY);

	const FString Trimmed = UnitStructPath.TrimStartAndEnd();
	const bool bIsIf = Trimmed.Equals(TEXT("If"), ESearchCase::IgnoreCase) ||
		Trimmed.EndsWith(TEXT("RigVMDispatch_If")) || Trimmed.EndsWith(TEXT(".If"));
	const bool bIsSelect = Trimmed.Equals(TEXT("Select"), ESearchCase::IgnoreCase) ||
		Trimmed.EndsWith(TEXT("RigVMDispatch_Select")) || Trimmed.EndsWith(TEXT(".Select"));

	URigVMNode* Node = nullptr;
	if (bIsIf || bIsSelect)
	{
		FString ResolvedCpp; FName ResolvedObj;
		ResolveRigVMValueType(CppType, ResolvedCpp, ResolvedObj);
		Node = bIsIf
			? Controller->AddIfNode(ResolvedCpp, ResolvedObj, Pos, TEXT(""), true, false)
			: Controller->AddSelectNode(ResolvedCpp, ResolvedObj, Pos, TEXT(""), true, false);
		if (!Node)
		{
			OutError = FString::Printf(TEXT("Failed to add %s node (cpp_type='%s')."),
				bIsIf ? TEXT("If") : TEXT("Select"), *ResolvedCpp);
			return;
		}
	}
	else
	{
		const FName MethodFName = MethodName.IsEmpty() ? TEXT("Execute") : FName(*MethodName);
		Node = Controller->AddUnitNodeFromStructPath(UnitStructPath, MethodFName, Pos, TEXT(""), true, false);
		if (!Node) { OutError = FString::Printf(TEXT("Failed to add RigVM node for struct: %s"), *UnitStructPath); return; }
	}

	FString DeprecatedSince, Replacement;
	if (URigVMUnitNode* UNode = Cast<URigVMUnitNode>(Node))
	{
		if (UScriptStruct* SS = UNode->GetScriptStruct())
		{
			if (SS->HasMetaData(TEXT("Deprecated")))
			{
				DeprecatedSince = SS->GetMetaData(TEXT("Deprecated"));
				const FString DisplayName = SS->GetMetaData(TEXT("DisplayName"));
				if (!DisplayName.IsEmpty())
				{
					for (TObjectIterator<UScriptStruct> It; It; ++It)
					{
						UScriptStruct* Cand = *It;
						if (Cand == SS || !Cand) continue;
						if (Cand->HasMetaData(TEXT("Deprecated"))) continue;
						if (Cand->GetMetaData(TEXT("DisplayName")) != DisplayName) continue;
						if (SS->GetSuperStruct() && Cand->IsChildOf(SS->GetSuperStruct()))
						{
							Replacement = Cand->GetStructPathName().ToString();
							break;
						}
					}
				}
			}
		}
	}

	FBlueprintEditorUtils::MarkBlueprintAsModified(BP);

	TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
	Out->SetBoolField(TEXT("success"), true);
	Out->SetStringField(TEXT("node_name"), Node->GetName());
	Out->SetStringField(TEXT("node_path"), Node->GetNodePath());
	Out->SetStringField(TEXT("event"), ResolvedEvent);
	if (!DeprecatedSince.IsEmpty())
	{
		Out->SetBoolField(TEXT("deprecated"), true);
		Out->SetStringField(TEXT("deprecated_since"), DeprecatedSince);
		if (!Replacement.IsEmpty())
			Out->SetStringField(TEXT("use_instead"), Replacement);
		Out->SetStringField(TEXT("warning"),
			FString::Printf(TEXT("'%s' is deprecated (since %s)%s — it still works but will be removed. Prefer the non-deprecated equivalent."),
				*UnitStructPath, *DeprecatedSince,
				Replacement.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(": use %s"), *Replacement)));
	}
	FString Serialised;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Serialised);
	FJsonSerializer::Serialize(Out, Writer);
	OutJsonString = Serialised;
}

void HandleConnectRigPins(const FString& AssetPath,
	const FString& SourcePinPath, const FString& TargetPinPath,
	FString& OutJsonString, FString& OutError)
{

	UControlRigBlueprint* BP = nullptr;
	URigVMController* Controller = GetRVMController(AssetPath, BP, OutError);
	if (!Controller) return;

	bool bOk = Controller->AddLink(SourcePinPath, TargetPinPath, true, false);
	if (!bOk) { OutError = FString::Printf(TEXT("AddLink failed: %s → %s"), *SourcePinPath, *TargetPinPath); return; }

	FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"source_pin\":\"%s\",\"target_pin\":\"%s\"}"),
		*SourcePinPath, *TargetPinPath);
}

static void ConnectRigPinsForEvent(const FString& AssetPath,
	const FString& SourcePinPath, const FString& TargetPinPath, const FString& EventName,
	const FString& FunctionName, FString& OutJsonString, FString& OutError)
{
	UControlRigBlueprint* BP = nullptr;
	FString ResolvedEvent;
	URigVMController* Controller = GetRVMControllerForTarget(AssetPath, EventName, FunctionName, BP, OutError, &ResolvedEvent);
	if (!Controller) return;

	bool bOk = Controller->AddLink(SourcePinPath, TargetPinPath, true, false);
	if (!bOk) { OutError = FString::Printf(TEXT("AddLink failed: %s → %s (in %s graph)"), *SourcePinPath, *TargetPinPath, *ResolvedEvent); return; }

	FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"source_pin\":\"%s\",\"target_pin\":\"%s\",\"event\":\"%s\"}"),
		*SourcePinPath, *TargetPinPath, *ResolvedEvent);
}

void HandleSetRigPinValue(const FString& AssetPath,
	const FString& PinPath, const FString& Value,
	FString& OutJsonString, FString& OutError)
{

	UControlRigBlueprint* BP = nullptr;
	URigVMController* Controller = GetRVMController(AssetPath, BP, OutError);
	if (!Controller) return;

	bool bOk = Controller->SetPinDefaultValue(PinPath, Value, true, true, false, false, true);
	if (!bOk) { OutError = FString::Printf(TEXT("SetPinDefaultValue failed: %s"), *PinPath); return; }

	FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"pin_path\":\"%s\",\"value\":\"%s\"}"),
		*PinPath, *Value);
}

static void SetRigPinValueForEvent(const FString& AssetPath, const FString& PinPath,
	const FString& Value, const FString& EventName,
	const FString& FunctionName, FString& OutJsonString, FString& OutError)
{
	UControlRigBlueprint* BP = nullptr;
	FString ResolvedEvent;
	URigVMController* Controller = GetRVMControllerForTarget(AssetPath, EventName, FunctionName, BP, OutError, &ResolvedEvent);
	if (!Controller) return;

	bool bOk = Controller->SetPinDefaultValue(PinPath, Value, true, true, false, false, true);
	if (!bOk) { OutError = FString::Printf(TEXT("SetPinDefaultValue failed: %s (in %s graph)"), *PinPath, *ResolvedEvent); return; }

	FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"pin_path\":\"%s\",\"value\":\"%s\",\"event\":\"%s\"}"),
		*PinPath, *Value, *ResolvedEvent);
}

void HandleGetRigGraphNodes(const FString& AssetPath,
	FString& OutJsonString, FString& OutError)
{
	HandleGetRigGraphNodes(AssetPath, FString(), OutJsonString, OutError);
}

void HandleGetRigGraphNodes(const FString& AssetPath, const FString& EventName,
	FString& OutJsonString, FString& OutError)
{

	UControlRigBlueprint* BP = LoadCRBlueprint(AssetPath, OutError);
	if (!BP) return;

	URigVMGraph* Graph = nullptr;
	FString ResolvedEvent = TEXT("ForwardSolve");
	if (EventName.StartsWith(TEXT("function:")))
	{
		const FString FnName = EventName.Mid(9).TrimStartAndEnd();
		URigVMFunctionLibrary* Lib = BP->GetLocalFunctionLibrary();
		URigVMLibraryNode* Fn = Lib ? Lib->FindFunction(FName(*FnName)) : nullptr;
		if (!Fn) { OutError = FString::Printf(TEXT("function '%s' not found in the library."), *FnName); return; }
		Graph = Fn->GetContainedGraph();
		if (!Graph) { OutError = FString::Printf(TEXT("function '%s' has no contained graph."), *FnName); return; }
		ResolvedEvent = EventName;
	}
	else if (!EventName.IsEmpty())
	{
		FString EntryStructPath, DisplayName;
		if (!ResolveEventToken(EventName, EntryStructPath, DisplayName))
		{
			OutError = FString::Printf(TEXT("event '%s' not recognised. Valid: forward_solve (default), construction, backward_solve, interaction."), *EventName);
			return;
		}
		ResolvedEvent = DisplayName;
		const TArray<URigVMGraph*> AllModels = BP->GetAllModels();
		for (URigVMGraph* G : AllModels)
		{
			if (!G) continue;
			for (URigVMNode* N : G->GetNodes())
			{
				URigVMUnitNode* UN = Cast<URigVMUnitNode>(N);
				if (UN && UN->GetScriptStruct() && UN->GetScriptStruct()->GetPathName() == EntryStructPath) { Graph = G; break; }
			}
			if (Graph) break;
		}
	}
	if (!Graph) Graph = BP->GetDefaultModel();
	if (!Graph) { OutError = TEXT("No default RigVM graph."); return; }

	TSharedPtr<FJsonObject> Root = MakeShareable(new FJsonObject);
	Root->SetBoolField(TEXT("success"), true);
	Root->SetStringField(TEXT("asset_path"), AssetPath);
	Root->SetStringField(TEXT("event"), ResolvedEvent);

	TArray<TSharedPtr<FJsonValue>> NodesArr;
	for (URigVMNode* Node : Graph->GetNodes())
	{
		TSharedPtr<FJsonObject> NObj = MakeShareable(new FJsonObject);
		NObj->SetStringField(TEXT("name"), Node->GetName());
		NObj->SetStringField(TEXT("path"), Node->GetNodePath());
		FVector2D Pos = Node->GetPosition();
		NObj->SetNumberField(TEXT("pos_x"), Pos.X);
		NObj->SetNumberField(TEXT("pos_y"), Pos.Y);

		FString NodeType = TEXT("other");
		if (Cast<URigVMUnitNode>(Node))          NodeType = TEXT("unit");
		else if (Cast<URigVMVariableNode>(Node)) NodeType = TEXT("variable");
		NObj->SetStringField(TEXT("node_type"), NodeType);
		NObj->SetBoolField(TEXT("has_execute_context"), Node->FindPin(TEXT("ExecuteContext")) != nullptr);

		TArray<TSharedPtr<FJsonValue>> PinsArr;
		for (URigVMPin* Pin : Node->GetPins())
		{
			TSharedPtr<FJsonObject> PObj = MakeShareable(new FJsonObject);
			PObj->SetStringField(TEXT("name"), Pin->GetName());
			PObj->SetStringField(TEXT("path"), Pin->GetPinPath());
			PObj->SetStringField(TEXT("direction"), Pin->GetDirection() == ERigVMPinDirection::Input ? TEXT("input") : Pin->GetDirection() == ERigVMPinDirection::Output ? TEXT("output") : TEXT("io"));
			PObj->SetStringField(TEXT("default_value"), Pin->GetDefaultValue());
			PinsArr.Add(MakeShareable(new FJsonValueObject(PObj)));
		}
		NObj->SetArrayField(TEXT("pins"), PinsArr);
		NodesArr.Add(MakeShareable(new FJsonValueObject(NObj)));
	}
	Root->SetArrayField(TEXT("nodes"), NodesArr);

	TArray<TSharedPtr<FJsonValue>> LinksArr;
	for (URigVMLink* Link : Graph->GetLinks())
	{
		TSharedPtr<FJsonObject> LObj = MakeShareable(new FJsonObject);
		LObj->SetStringField(TEXT("source"), Link->GetSourcePin() ? Link->GetSourcePin()->GetPinPath() : TEXT(""));
		LObj->SetStringField(TEXT("target"), Link->GetTargetPin() ? Link->GetTargetPin()->GetPinPath() : TEXT(""));
		LinksArr.Add(MakeShareable(new FJsonValueObject(LObj)));
	}
	Root->SetArrayField(TEXT("links"), LinksArr);

	FString JsonStr;
	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&JsonStr);
	FJsonSerializer::Serialize(Root.ToSharedRef(), W);
	OutJsonString = JsonStr;
}

static void RemoveRigNodeForEvent(const FString& AssetPath, const FString& NodeName, const FString& EventName,
	const FString& FunctionName, FString& OutJsonString, FString& OutError)
{
	UControlRigBlueprint* BP = nullptr;
	FString ResolvedEvent;
	URigVMController* Controller = GetRVMControllerForTarget(AssetPath, EventName, FunctionName, BP, OutError, &ResolvedEvent);
	if (!Controller) return;

	const bool bOk = Controller->RemoveNodeByName(FName(*NodeName), true, false);
	if (!bOk) { OutError = FString::Printf(TEXT("Failed to remove node '%s' (in %s graph)"), *NodeName, *ResolvedEvent); return; }

	FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"node_name\":\"%s\",\"event\":\"%s\"}"), *NodeName, *ResolvedEvent);
}

static void DisconnectRigPinsForEvent(const FString& AssetPath,
	const FString& SourcePinPath, const FString& TargetPinPath, const FString& EventName,
	const FString& FunctionName, FString& OutJsonString, FString& OutError)
{
	UControlRigBlueprint* BP = nullptr;
	FString ResolvedEvent;
	URigVMController* Controller = GetRVMControllerForTarget(AssetPath, EventName, FunctionName, BP, OutError, &ResolvedEvent);
	if (!Controller) return;

	const bool bOk = Controller->BreakLink(SourcePinPath, TargetPinPath, true, false);
	if (!bOk) { OutError = FString::Printf(TEXT("BreakLink failed: %s -X-> %s (in %s graph)"), *SourcePinPath, *TargetPinPath, *ResolvedEvent); return; }

	FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"source_pin\":\"%s\",\"target_pin\":\"%s\",\"event\":\"%s\"}"),
		*SourcePinPath, *TargetPinPath, *ResolvedEvent);
}

void HandleRemoveRigNode(const FString& AssetPath, const FString& NodeName,
	FString& OutJsonString, FString& OutError)
{

	UControlRigBlueprint* BP = nullptr;
	URigVMController* Controller = GetRVMController(AssetPath, BP, OutError);
	if (!Controller) return;

	bool bOk = Controller->RemoveNodeByName(FName(*NodeName), true, false);
	if (!bOk) { OutError = FString::Printf(TEXT("RemoveNodeByName failed for: %s"), *NodeName); return; }

	FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"removed_node\":\"%s\"}"), *NodeName);
}

static bool ResolveRigVMVariableCppType(const FString& InType, FString& OutCppType)
{
	const FString T = InType.TrimStartAndEnd();
	struct FMap { const TCHAR* In; const TCHAR* Out; };
	static const FMap Types[] = {
		{ TEXT("bool"),    TEXT("bool") },
		{ TEXT("float"),   TEXT("float") },
		{ TEXT("double"),  TEXT("double") },
		{ TEXT("int32"),   TEXT("int32") },
		{ TEXT("int"),     TEXT("int32") },
		{ TEXT("integer"), TEXT("int32") },
		{ TEXT("FString"), TEXT("FString") },
		{ TEXT("string"),  TEXT("FString") },
		{ TEXT("FName"),   TEXT("FName") },
		{ TEXT("name"),    TEXT("FName") },
		{ TEXT("FText"),   TEXT("FText") },
		{ TEXT("FVector"),      TEXT("/Script/CoreUObject.Vector") },
		{ TEXT("Vector"),       TEXT("/Script/CoreUObject.Vector") },
		{ TEXT("FVector2D"),    TEXT("/Script/CoreUObject.Vector2D") },
		{ TEXT("Vector2D"),     TEXT("/Script/CoreUObject.Vector2D") },
		{ TEXT("FRotator"),     TEXT("/Script/CoreUObject.Rotator") },
		{ TEXT("Rotator"),      TEXT("/Script/CoreUObject.Rotator") },
		{ TEXT("FQuat"),        TEXT("/Script/CoreUObject.Quat") },
		{ TEXT("FTransform"),   TEXT("/Script/CoreUObject.Transform") },
		{ TEXT("Transform"),    TEXT("/Script/CoreUObject.Transform") },
		{ TEXT("FLinearColor"), TEXT("/Script/CoreUObject.LinearColor") },
		{ TEXT("LinearColor"),  TEXT("/Script/CoreUObject.LinearColor") },
	};
	for (const FMap& M : Types)
	{
		if (T.Equals(M.In, ESearchCase::IgnoreCase)) { OutCppType = M.Out; return true; }
	}
	if (T.StartsWith(TEXT("/Script/"))) { OutCppType = T; return true; }
	return false;
}

void HandleAddRigVariable(const FString& AssetPath, const FString& VariableName,
	const FString& CppType, bool bIsGetter, const FString& DefaultValue,
	int32 PosX, int32 PosY, FString& OutJsonString, FString& OutError)
{

	UControlRigBlueprint* BP = nullptr;
	URigVMController* Controller = GetRVMController(AssetPath, BP, OutError);
	if (!Controller) return;

	FString ResolvedCppType;
	if (!ResolveRigVMVariableCppType(CppType, ResolvedCppType))
	{
		OutError = FString::Printf(TEXT("cpp_type '%s' not supported — would crash the engine. Use: bool, float, double, int32, FString, FName, FText, FVector, FVector2D, FRotator, FQuat, FTransform, FLinearColor (or a /Script/... object path)."), *CppType);
		return;
	}

	bool bDeclared = false;
	{
		bool bAlready = false;
		for (const FBPVariableDescription& Var : BP->NewVariables)
		{
			if (Var.VarName == FName(*VariableName)) { bAlready = true; break; }
		}
		if (!bAlready)
		{
			const FName Result = BP->AddMemberVariable(FName(*VariableName), ResolvedCppType, false, false, DefaultValue);
			bDeclared = !Result.IsNone();
		}
		else
		{
			bDeclared = true;
		}
	}

	URigVMVariableNode* Node = Controller->AddVariableNode(
		FName(*VariableName), ResolvedCppType, nullptr, bIsGetter, DefaultValue,
		FVector2D((float)PosX, (float)PosY), TEXT(""), true, false);

	if (!Node)
	{
		OutError = FString::Printf(TEXT("Failed to add variable node '%s' (cpp_type='%s', declared_member=%s). For built-in types pass 'float' / 'double' / 'bool' / 'int32' / 'FString' / 'FName' / 'FVector' / 'FRotator' / 'FTransform'."), *VariableName, *CppType, bDeclared ? TEXT("yes") : TEXT("no"));
		return;
	}

	FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"node_name\":\"%s\",\"variable_name\":\"%s\",\"is_getter\":%s,\"declared_member\":%s}"),
		*Node->GetName(), *VariableName, bIsGetter ? TEXT("true") : TEXT("false"),
		bDeclared ? TEXT("true") : TEXT("false"));
}

static bool ResolveExposedPinType(const FString& In, FString& OutCppType, FName& OutObjectPath)
{
	FString Resolved;
	if (!ResolveRigVMVariableCppType(In, Resolved)) return false;
	if (Resolved.StartsWith(TEXT("/Script/")))
	{
		if (UScriptStruct* SS = LoadObject<UScriptStruct>(nullptr, *Resolved))
		{
			OutCppType = FString::Printf(TEXT("%s%s"), SS->GetPrefixCPP(), *SS->GetName());
			OutObjectPath = FName(*Resolved);
			return true;
		}
		return false;
	}
	OutCppType = Resolved;
	OutObjectPath = NAME_None;
	return true;
}

void HandleAddRigFunctionFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString AssetPath, FunctionName;
	if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath) ||
		!Args->TryGetStringField(TEXT("function_name"), FunctionName))
	{
		OutError = TEXT("asset_path and function_name required");
		return;
	}
	bool bMutable = true;
	Args->TryGetBoolField(TEXT("mutable"), bMutable);

	UControlRigBlueprint* BP = LoadCRBlueprint(AssetPath, OutError);
	if (!BP) return;
	URigVMFunctionLibrary* Lib = BP->GetLocalFunctionLibrary();
	if (!Lib) { OutError = TEXT("No local function library on this rig."); return; }
	if (Lib->FindFunction(FName(*FunctionName)))
	{
		OutError = FString::Printf(TEXT("function '%s' already exists."), *FunctionName);
		return;
	}
	URigVMController* LibController = BP->GetController(Lib);
	if (!LibController) { OutError = TEXT("Could not get the function library controller."); return; }

	URigVMLibraryNode* Fn = LibController->AddFunctionToLibrary(
		FName(*FunctionName), bMutable, FVector2D::ZeroVector, true, false);
	if (!Fn) { OutError = FString::Printf(TEXT("Failed to create function '%s'."), *FunctionName); return; }

	FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
	UEditorAssetLibrary::SaveAsset(BP->GetPathName(), false);
	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"function_name\":\"%s\",\"mutable\":%s}"),
		*Fn->GetName(), bMutable ? TEXT("true") : TEXT("false"));
}

void HandleAddRigFunctionPinFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString AssetPath, FunctionName, PinName, Direction, CppType, DefaultValue;
	if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath) ||
		!Args->TryGetStringField(TEXT("function_name"), FunctionName) ||
		!Args->TryGetStringField(TEXT("pin_name"), PinName) ||
		!Args->TryGetStringField(TEXT("cpp_type"), CppType))
	{
		OutError = TEXT("asset_path, function_name, pin_name, cpp_type required");
		return;
	}
	Args->TryGetStringField(TEXT("direction"), Direction);
	Args->TryGetStringField(TEXT("default_value"), DefaultValue);

	const FString D = Direction.ToLower();
	ERigVMPinDirection PinDir = ERigVMPinDirection::Input;
	if (D == TEXT("output") || D == TEXT("out") || D == TEXT("return")) PinDir = ERigVMPinDirection::Output;

	FString ResolvedCpp; FName ObjPath;
	if (!ResolveExposedPinType(CppType, ResolvedCpp, ObjPath))
	{
		OutError = FString::Printf(TEXT("cpp_type '%s' not supported. Use: bool, float, double, int32, FString, FName, FText, FVector, FVector2D, FRotator, FQuat, FTransform, FLinearColor (or a /Script/... object path)."), *CppType);
		return;
	}

	UControlRigBlueprint* BP = nullptr;
	URigVMController* BodyController = GetRVMControllerForFunctionBody(AssetPath, FunctionName, BP, OutError);
	if (!BodyController) return;

	const FName Added = BodyController->AddExposedPin(
		FName(*PinName), PinDir, ResolvedCpp, ObjPath, DefaultValue, true, false);
	if (Added.IsNone()) { OutError = FString::Printf(TEXT("Failed to add exposed pin '%s' to function '%s'."), *PinName, *FunctionName); return; }

	FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"function_name\":\"%s\",\"pin_name\":\"%s\",\"direction\":\"%s\",\"cpp_type\":\"%s\"}"),
		*FunctionName, *Added.ToString(), PinDir == ERigVMPinDirection::Output ? TEXT("output") : TEXT("input"), *ResolvedCpp);
}

void HandleAddRigFunctionNodeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString AssetPath, FunctionName, Event;
	int32 PosX = 0, PosY = 0;
	if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath) ||
		!Args->TryGetStringField(TEXT("function_name"), FunctionName))
	{
		OutError = TEXT("asset_path and function_name required");
		return;
	}
	Args->TryGetStringField(TEXT("event"), Event);
	Args->TryGetNumberField(TEXT("position_x"), PosX);
	Args->TryGetNumberField(TEXT("position_y"), PosY);

	UControlRigBlueprint* BP = nullptr;
	FString ResolvedEvent;
	URigVMController* Controller = GetRVMControllerForEvent(AssetPath, Event, BP, OutError, &ResolvedEvent);
	if (!Controller) return;

	URigVMFunctionLibrary* Lib = BP->GetLocalFunctionLibrary();
	URigVMLibraryNode* Fn = Lib ? Lib->FindFunction(FName(*FunctionName)) : nullptr;
	if (!Fn) { OutError = FString::Printf(TEXT("function '%s' not found. Create it with add_rig_function first."), *FunctionName); return; }

	URigVMFunctionReferenceNode* RefNode = Controller->AddFunctionReferenceNode(
		Fn, FVector2D((float)PosX, (float)PosY), TEXT(""), true, false);
	if (!RefNode) { OutError = FString::Printf(TEXT("Failed to instantiate function '%s' in %s."), *FunctionName, *ResolvedEvent); return; }

	FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"node_name\":\"%s\",\"node_path\":\"%s\",\"function_name\":\"%s\",\"event\":\"%s\"}"),
		*RefNode->GetName(), *RefNode->GetNodePath(), *FunctionName, *ResolvedEvent);
}

static void BuildRigLogicImpl(const FString& AssetPath,
	const FString& NodesJson, const FString& LinksJson, const FString& EventName,
	FString& OutJsonString, FString& OutError);

void HandleBuildRigLogic(const FString& AssetPath,
	const FString& NodesJson, const FString& LinksJson,
	FString& OutJsonString, FString& OutError)
{
	BuildRigLogicImpl(AssetPath, NodesJson, LinksJson, FString(), OutJsonString, OutError);
}

static void BuildRigLogicImpl(const FString& AssetPath,
	const FString& NodesJson, const FString& LinksJson, const FString& EventName,
	FString& OutJsonString, FString& OutError)
{

	UControlRigBlueprint* BP = nullptr;
	FString ResolvedEvent;
	URigVMController* Controller = GetRVMControllerForEvent(AssetPath, EventName, BP, OutError, &ResolvedEvent);
	if (!Controller) return;

	TArray<TSharedPtr<FJsonValue>> NodesArr;
	if (!NodesJson.IsEmpty())
	{
		TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(NodesJson);
		FJsonSerializer::Deserialize(R, NodesArr);
	}

	TMap<FString, FString> NamedNodes;
	TArray<TSharedPtr<FJsonValue>> CreatedNodes;

	for (const TSharedPtr<FJsonValue>& Item : NodesArr)
	{
		TSharedPtr<FJsonObject> NDesc = Item->AsObject();
		if (!NDesc.IsValid()) continue;

		FString Type, StructPath, NodeName, CppType, DefaultValue, VariableName;
		bool bIsGetter = true;
		double PX = 0, PY = 0;
		NDesc->TryGetStringField(TEXT("type"), Type);
		NDesc->TryGetStringField(TEXT("struct_path"), StructPath);
		NDesc->TryGetStringField(TEXT("name"), NodeName);
		NDesc->TryGetStringField(TEXT("cpp_type"), CppType);
		NDesc->TryGetStringField(TEXT("default_value"), DefaultValue);
		NDesc->TryGetStringField(TEXT("variable_name"), VariableName);
		NDesc->TryGetBoolField(TEXT("is_getter"), bIsGetter);
		NDesc->TryGetNumberField(TEXT("pos_x"), PX);
		NDesc->TryGetNumberField(TEXT("pos_y"), PY);

		FString ActualName;
		if (Type == TEXT("unit") && !StructPath.IsEmpty())
		{
			FString Method = TEXT("Execute"); NDesc->TryGetStringField(TEXT("method"), Method);
			URigVMUnitNode* N = Controller->AddUnitNodeFromStructPath(
				StructPath, FName(*Method), FVector2D((float)PX, (float)PY),
				NodeName.IsEmpty() ? TEXT("") : NodeName, true, false);
			if (!N) { OutError = FString::Printf(TEXT("Failed to add unit node: %s"), *StructPath); return; }
			ActualName = N->GetNodePath();
		}
		else if (Type == TEXT("variable") && !VariableName.IsEmpty())
		{
			FString ResolvedCppType;
			if (!ResolveRigVMVariableCppType(CppType, ResolvedCppType))
			{
				OutError = FString::Printf(TEXT("cpp_type '%s' on variable node '%s' not supported — would crash the engine. Use: bool, float, double, int32, FString, FName, FText, FVector, FVector2D, FRotator, FQuat, FTransform, FLinearColor (or a /Script/... object path)."), *CppType, *VariableName);
				return;
			}
			bool bAlready = false;
			for (const FBPVariableDescription& Var : BP->NewVariables)
			{
				if (Var.VarName == FName(*VariableName)) { bAlready = true; break; }
			}
			if (!bAlready)
			{
				BP->AddMemberVariable(FName(*VariableName), ResolvedCppType, false, false, DefaultValue);
			}
			URigVMVariableNode* N = Controller->AddVariableNode(
				FName(*VariableName), ResolvedCppType, nullptr, bIsGetter, DefaultValue,
				FVector2D((float)PX, (float)PY), NodeName.IsEmpty() ? TEXT("") : NodeName, true, false);
			if (!N) { OutError = FString::Printf(TEXT("Failed to add variable node: %s"), *VariableName); return; }
			ActualName = N->GetNodePath();
		}
		else
		{
			OutError = FString::Printf(TEXT("Unknown node type or missing struct_path: %s"), *Type); return;
		}

		if (!NodeName.IsEmpty()) NamedNodes.Add(NodeName, ActualName);

		const TSharedPtr<FJsonObject>* PinsObj;
		if (NDesc->TryGetObjectField(TEXT("pins"), PinsObj) && PinsObj->IsValid())
		{
			for (auto& KV : (*PinsObj)->Values)
			{
				FString PinPath = ActualName + TEXT(".") + FString(*KV.Key);
				FString Val = KV.Value->AsString();
				Controller->SetPinDefaultValue(PinPath, Val, true, true, false, false, true);
			}
		}

		TSharedPtr<FJsonObject> E = MakeShareable(new FJsonObject);
		E->SetStringField(TEXT("type"), Type);
		E->SetStringField(TEXT("name"), NodeName);
		E->SetStringField(TEXT("node_path"), ActualName);
		CreatedNodes.Add(MakeShareable(new FJsonValueObject(E)));
	}

	TArray<TSharedPtr<FJsonValue>> LinksArr;
	if (!LinksJson.IsEmpty())
	{
		TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(LinksJson);
		FJsonSerializer::Deserialize(R, LinksArr);
	}

	for (const TSharedPtr<FJsonValue>& LItem : LinksArr)
	{
		TSharedPtr<FJsonObject> LDesc = LItem->AsObject();
		if (!LDesc.IsValid()) continue;
		FString From, To;
		LDesc->TryGetStringField(TEXT("from"), From);
		LDesc->TryGetStringField(TEXT("to"), To);
		for (auto& KV : NamedNodes)
		{
			From.ReplaceInline(*FString::Printf(TEXT("%s."), *KV.Key), *FString::Printf(TEXT("%s."), *KV.Value));
			To.ReplaceInline(*FString::Printf(TEXT("%s."), *KV.Key), *FString::Printf(TEXT("%s."), *KV.Value));
		}
		Controller->AddLink(From, To, true, false);
	}

	{
		FString EntryStructPath, _DisplayName;
		ResolveEventToken(EventName, EntryStructPath, _DisplayName);

		URigVMGraph* Graph = nullptr;
		const TArray<URigVMGraph*> AllModels = BP->GetAllModels();
		for (URigVMGraph* G : AllModels)
		{
			if (!G) continue;
			for (URigVMNode* N : G->GetNodes())
			{
				URigVMUnitNode* UN = Cast<URigVMUnitNode>(N);
				if (UN && UN->GetScriptStruct() && UN->GetScriptStruct()->GetPathName() == EntryStructPath) { Graph = G; break; }
			}
			if (Graph) break;
		}
		if (!Graph) Graph = BP->GetDefaultModel();

		URigVMNode* PrevNode = nullptr;
		if (Graph)
		{
			for (URigVMNode* N : Graph->GetNodes())
			{
				URigVMUnitNode* UN = Cast<URigVMUnitNode>(N);
				if (UN && UN->GetScriptStruct() && UN->GetScriptStruct()->GetPathName() == EntryStructPath) { PrevNode = N; break; }
			}
		}
		if (PrevNode)
		{
			for (int32 Hop = 0; Hop < 256; ++Hop)
			{
				URigVMPin* OutPin = PrevNode->FindPin(TEXT("ExecuteContext"));
				if (!OutPin) OutPin = PrevNode->FindPin(TEXT("ExecutePin"));
				if (!OutPin) break;
				const TArray<URigVMPin*> Linked = OutPin->GetLinkedTargetPins();
				if (Linked.Num() == 0 || !Linked[0] || !Linked[0]->GetNode()) break;
				PrevNode = Linked[0]->GetNode();
			}
		}
		if (PrevNode && Graph)
		{
			for (const TSharedPtr<FJsonValue>& CE : CreatedNodes)
			{
				if (!CE.IsValid() || CE->Type != EJson::Object) continue;
				TSharedPtr<FJsonObject> CEObj = CE->AsObject();
				if (!CEObj.IsValid()) continue;
				FString NPath; CEObj->TryGetStringField(TEXT("node_path"), NPath);
				URigVMNode* CreatedNode = Graph->FindNode(NPath);
				if (!CreatedNode) continue;
				if (!CreatedNode->FindPin(TEXT("ExecuteContext"))) continue;
				Controller->AddLink(PrevNode->GetNodePath() + TEXT(".ExecuteContext"),
				                    NPath + TEXT(".ExecuteContext"), true, false);
				PrevNode = CreatedNode;
			}
		}
	}

	FBlueprintEditorUtils::MarkBlueprintAsModified(BP);

	TSharedPtr<FJsonObject> Res = MakeShareable(new FJsonObject);
	Res->SetBoolField(TEXT("success"), true);
	Res->SetStringField(TEXT("event"), ResolvedEvent);
	Res->SetArrayField(TEXT("created_nodes"), CreatedNodes);
	Res->SetNumberField(TEXT("count"), CreatedNodes.Num());
	FString JsonStr;
	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&JsonStr);
	FJsonSerializer::Serialize(Res.ToSharedRef(), W);
	OutJsonString = JsonStr;
}

void HandleDisconnectRigPins(const FString& AssetPath, const FString& SourcePinPath, const FString& TargetPinPath, FString& OutJsonString, FString& OutError)
{
	if (AssetPath.IsEmpty() || SourcePinPath.IsEmpty() || TargetPinPath.IsEmpty())
	{ OutError = TEXT("asset_path, source_pin_path, and target_pin_path are all required"); return; }

	UControlRigBlueprint* BP = nullptr;
	URigVMController* Controller = GetRVMController(AssetPath, BP, OutError);
	if (!Controller) return;

	const bool bOk = Controller->BreakLink(SourcePinPath, TargetPinPath, true, false);
	if (!bOk) { OutError = FString::Printf(TEXT("BreakLink failed: %s -X-> %s"), *SourcePinPath, *TargetPinPath); return; }

	FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"source_pin\":\"%s\",\"target_pin\":\"%s\"}"),
		*SourcePinPath, *TargetPinPath);
}

void HandleListRigVMNodeTypes(const FString& Filter, FString& OutJsonString, FString& OutError)
{

	TArray<TSharedPtr<FJsonValue>> TypeArr;
	int32 Count = 0;
	const int32 MaxResults = 100;

	for (TObjectIterator<UScriptStruct> It; It; ++It)
	{
		UScriptStruct* Struct = *It;
		if (!Struct) continue;

		FString StructName = Struct->GetName();
		FString FullPath = Struct->GetPathName();

		if (!StructName.StartsWith(TEXT("RigUnit")) &&
			!StructName.StartsWith(TEXT("RigVMFunction")) &&
			!FullPath.Contains(TEXT("ControlRig")))
			continue;

		if (!Filter.IsEmpty() && !StructName.Contains(Filter, ESearchCase::IgnoreCase))
			continue;

		TSharedPtr<FJsonObject> Entry = MakeShareable(new FJsonObject);
		Entry->SetStringField(TEXT("name"), StructName);
		Entry->SetStringField(TEXT("path"), FullPath);
		TypeArr.Add(MakeShareable(new FJsonValueObject(Entry)));

		if (++Count >= MaxResults) break;
	}

	TSharedPtr<FJsonObject> Root = MakeShareable(new FJsonObject);
	Root->SetBoolField(TEXT("success"), true);
	Root->SetNumberField(TEXT("count"), TypeArr.Num());
	Root->SetArrayField(TEXT("types"), TypeArr);
	if (Count >= MaxResults) Root->SetBoolField(TEXT("truncated"), true);
	FString Str; TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Str);
	FJsonSerializer::Serialize(Root.ToSharedRef(), W); OutJsonString = Str;
}

void HandleSetRigControlProperties(const FString& AssetPath, const FString& ControlName, const FString& PropertyName, const FString& PropertyValue, FString& OutJsonString, FString& OutError)
{
	UControlRigBlueprint* BP = LoadCRBlueprint(AssetPath, OutError);
	if (!BP) return;

	URigHierarchy* Hier = BP->GetHierarchy();
	if (!Hier) { OutError = TEXT("Could not get RigHierarchy."); return; }
	URigHierarchyController* HCtrl = Hier->GetController(true);
	if (!HCtrl) { OutError = TEXT("Could not get RigHierarchyController."); return; }

	const FRigElementKey ControlKey(FName(*ControlName), ERigElementType::Control);
	if (!Hier->Contains(ControlKey))
	{
		OutError = FString::Printf(TEXT("Control '%s' not found in rig hierarchy."), *ControlName);
		return;
	}

	FRigControlElement* ControlElement = Hier->Find<FRigControlElement>(ControlKey);
	if (!ControlElement) { OutError = FString::Printf(TEXT("Could not access control element '%s'."), *ControlName); return; }

	FRigControlSettings Settings = ControlElement->Settings;

	const FString P = PropertyName.ToLower().TrimStartAndEnd();
	const FString V = PropertyValue.TrimStartAndEnd();

	auto ParseBool = [](const FString& Raw) -> bool { return Raw.Equals(TEXT("true"), ESearchCase::IgnoreCase) || Raw == TEXT("1"); };

	bool bApplied = false;

	if (P == TEXT("shape_name") || P == TEXT("shapename"))
	{
		Settings.ShapeName = FName(*V); bApplied = true;
	}
	else if (P == TEXT("shape_color") || P == TEXT("shapecolor") || P == TEXT("color"))
	{
		FLinearColor Color = FLinearColor::White;
		if (V.StartsWith(TEXT("(")) && V.EndsWith(TEXT(")")))
		{
			Color.InitFromString(V);
		}
		else
		{
			TArray<FString> Parts; V.ParseIntoArray(Parts, V.Contains(TEXT(",")) ? TEXT(",") : TEXT(" "), true);
			if (Parts.Num() >= 3) { Color.R = FCString::Atof(*Parts[0]); Color.G = FCString::Atof(*Parts[1]); Color.B = FCString::Atof(*Parts[2]); }
			Color.A = (Parts.Num() >= 4) ? FCString::Atof(*Parts[3]) : 1.0f;
		}
		Settings.ShapeColor = Color; bApplied = true;
	}
	else if (P == TEXT("display_name") || P == TEXT("displayname"))
	{
		Settings.DisplayName = FName(*V); bApplied = true;
	}
	else if (P == TEXT("draw_limits") || P == TEXT("bdrawlimits"))
	{
		Settings.bDrawLimits = ParseBool(V); bApplied = true;
	}
	else if (P == TEXT("shape_visible") || P == TEXT("bshapevisible"))
	{
		Settings.bShapeVisible = ParseBool(V); bApplied = true;
	}
	else if (P == TEXT("animation_type") || P == TEXT("animationtype"))
	{
		const FString L = V.ToLower();
		if      (L == TEXT("animationcontrol")  || L == TEXT("animation"))   Settings.AnimationType = ERigControlAnimationType::AnimationControl;
		else if (L == TEXT("animationchannel")  || L == TEXT("channel"))     Settings.AnimationType = ERigControlAnimationType::AnimationChannel;
		else if (L == TEXT("proxycontrol")      || L == TEXT("proxy"))       Settings.AnimationType = ERigControlAnimationType::ProxyControl;
		else if (L == TEXT("visualcue")         || L == TEXT("visual"))      Settings.AnimationType = ERigControlAnimationType::VisualCue;
		else { OutError = FString::Printf(TEXT("animation_type '%s' not recognised. Valid: AnimationControl, AnimationChannel, ProxyControl, VisualCue."), *V); return; }
		bApplied = true;
	}
	else if (P == TEXT("primary_axis") || P == TEXT("primaryaxis"))
	{
		const FString L = V.ToLower();
		if      (L == TEXT("x")) Settings.PrimaryAxis = ERigControlAxis::X;
		else if (L == TEXT("y")) Settings.PrimaryAxis = ERigControlAxis::Y;
		else if (L == TEXT("z")) Settings.PrimaryAxis = ERigControlAxis::Z;
		else { OutError = FString::Printf(TEXT("primary_axis '%s' not recognised. Valid: X, Y, Z."), *V); return; }
		bApplied = true;
	}
	else if (P == TEXT("limit_enabled") || P == TEXT("limits_enabled") || P == TEXT("limitenabled"))
	{
		Settings.LimitEnabled.Empty();
		const bool bAllOn  = V.Equals(TEXT("true"),  ESearchCase::IgnoreCase) || V == TEXT("1");
		const bool bAllOff = V.Equals(TEXT("false"), ESearchCase::IgnoreCase) || V == TEXT("0");
		if (bAllOn || bAllOff)
		{
			const int32 ChannelCount =
				(Settings.ControlType == ERigControlType::Transform || Settings.ControlType == ERigControlType::TransformNoScale) ? 6 :
				(Settings.ControlType == ERigControlType::EulerTransform) ? 9 :
				(Settings.ControlType == ERigControlType::Position || Settings.ControlType == ERigControlType::Rotator || Settings.ControlType == ERigControlType::Scale) ? 3 :
				(Settings.ControlType == ERigControlType::Vector2D) ? 2 : 1;
			for (int32 i = 0; i < ChannelCount; i++) Settings.LimitEnabled.Add(FRigControlLimitEnabled(bAllOn));
		}
		else
		{
			TArray<FString> Parts; V.ParseIntoArray(Parts, TEXT(","), true);
			for (const FString& Part : Parts) Settings.LimitEnabled.Add(FRigControlLimitEnabled(ParseBool(Part.TrimStartAndEnd())));
		}
		bApplied = true;
	}
	else if (P == TEXT("minimum_value") || P == TEXT("min_value") || P == TEXT("min") ||
	         P == TEXT("maximum_value") || P == TEXT("max_value") || P == TEXT("max"))
	{
		const bool bIsMin = P.Contains(TEXT("min"));
		FRigControlValue CV;
		bool bParsed = true;
		switch (Settings.ControlType)
		{
			case ERigControlType::Float:
			case ERigControlType::ScaleFloat:
				CV = FRigControlValue::Make<float>(FCString::Atof(*V));
				break;
			case ERigControlType::Integer:
				CV = FRigControlValue::Make<int32>(FCString::Atoi(*V));
				break;
			case ERigControlType::Vector2D:
			{
				TArray<FString> Parts; V.ParseIntoArray(Parts, TEXT(","), true);
				const float X = Parts.Num() > 0 ? FCString::Atof(*Parts[0]) : 0.f;
				const float Y = Parts.Num() > 1 ? FCString::Atof(*Parts[1]) : 0.f;
				CV = FRigControlValue::Make<FVector2f>(FVector2f(X, Y));
				break;
			}
			case ERigControlType::Position:
			case ERigControlType::Scale:
			{
				TArray<FString> Parts; V.ParseIntoArray(Parts, TEXT(","), true);
				const float X = Parts.Num() > 0 ? FCString::Atof(*Parts[0]) : 0.f;
				const float Y = Parts.Num() > 1 ? FCString::Atof(*Parts[1]) : 0.f;
				const float Z = Parts.Num() > 2 ? FCString::Atof(*Parts[2]) : 0.f;
				CV = FRigControlValue::Make<FVector3f>(FVector3f(X, Y, Z));
				break;
			}
			default:
				bParsed = false;
				break;
		}
		if (!bParsed)
		{
			OutError = FString::Printf(TEXT("min/max value not supported for control type '%s' — only Float/Integer/Vector2D/Vector/Scale."), *ControlTypeToString(Settings.ControlType));
			return;
		}
		if (bIsMin) Settings.MinimumValue = CV; else Settings.MaximumValue = CV;
		bApplied = true;
	}
	else
	{
		OutError = FString::Printf(TEXT("property_name '%s' not recognised. Valid: shape_name, shape_color, display_name, draw_limits, shape_visible, animation_type, primary_axis, limit_enabled, minimum_value, maximum_value."),
			*PropertyName);
		return;
	}

	if (!bApplied) { OutError = TEXT("No matching property was set."); return; }

	HCtrl->SetControlSettings(ControlKey, Settings, true);

	FBlueprintEditorUtils::MarkBlueprintAsModified(BP);

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"control_name\":\"%s\",\"property\":\"%s\",\"value\":\"%s\"}"),
		*ControlName, *PropertyName, *PropertyValue);
}

void HandleRemoveRigControl(const FString& AssetPath, const FString& ControlName,
	FString& OutJsonString, FString& OutError)
{

	UControlRigBlueprint* BP = LoadCRBlueprint(AssetPath, OutError);
	if (!BP) return;

	URigHierarchy* Hier = BP->GetHierarchy();
	if (!Hier) { OutError = TEXT("Could not get RigHierarchy."); return; }
	URigHierarchyController* HCtrl = Hier->GetController(true);
	if (!HCtrl) { OutError = TEXT("Could not get RigHierarchyController."); return; }

	FRigElementKey ControlKey(FName(*ControlName), ERigElementType::Control);
	if (!Hier->Contains(ControlKey))
	{
		OutError = FString::Printf(TEXT("Control '%s' not found in rig hierarchy."), *ControlName);
		return;
	}

	bool bRemoved = HCtrl->RemoveElement(ControlKey);
	if (!bRemoved)
	{
		OutError = FString::Printf(TEXT("Failed to remove control '%s'."), *ControlName);
		return;
	}

	FBlueprintEditorUtils::MarkBlueprintAsModified(BP);

	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"asset_path\":\"%s\",\"removed_control\":\"%s\"}"),
		*AssetPath, *ControlName);
}

void HandleAddRigControlFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString AssetPath; Args->TryGetStringField(TEXT("asset_path"), AssetPath);

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("controls"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString CN = BatchToolHelper::GetItemString(Item, TEXT("control_name"), TEXT("name"));
			FString CT = BatchToolHelper::GetItemString(Item, TEXT("control_type"));
			if (CT.IsEmpty()) CT = TEXT("Transform");
			FString PB = BatchToolHelper::GetItemString(Item, TEXT("parent_bone"));
			if (CN.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing control_name")); continue; }
			FString ItemOut, ItemErr;
			AddRigControlImpl(AssetPath, CN, CT, PB, Item, ItemOut, ItemErr);
			if (ItemErr.IsEmpty())
			{
				auto Extra = MakeShared<FJsonObject>(); Extra->SetStringField(TEXT("control_name"), CN);
				Batch.AddSuccess(i, Extra);
			}
			else Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJsonString);
		return;
	}
	FString CN, CT = TEXT("Transform"), PB;
	Args->TryGetStringField(TEXT("control_name"), CN); Args->TryGetStringField(TEXT("control_type"), CT);
	Args->TryGetStringField(TEXT("parent_bone"), PB);
	AddRigControlImpl(AssetPath, CN, CT, PB, Args, OutJsonString, OutError);
}

void HandleAddRigVMNodeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString AssetPath; Args->TryGetStringField(TEXT("asset_path"), AssetPath);
	FString Event; Args->TryGetStringField(TEXT("event"), Event);
	FString Function; Args->TryGetStringField(TEXT("function"), Function);

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("nodes"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString USP = BatchToolHelper::GetItemString(Item, TEXT("node_type"), TEXT("unit_struct_path"));
			FString MN = BatchToolHelper::GetItemString(Item, TEXT("method_name"));
			if (MN.IsEmpty()) MN = TEXT("Execute");
			double PX = 0, PY = 0;
			Item->TryGetNumberField(TEXT("position_x"), PX); Item->TryGetNumberField(TEXT("position_y"), PY);
			FString ItemEvent = Event;
			Item->TryGetStringField(TEXT("event"), ItemEvent);
			FString ItemFunction = Function;
			Item->TryGetStringField(TEXT("function"), ItemFunction);
			FString ItemCppType; Item->TryGetStringField(TEXT("cpp_type"), ItemCppType);
			if (USP.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing node_type")); continue; }
			FString ItemOut, ItemErr;
			AddRigVMNodeForEvent(AssetPath, USP, MN, (int32)PX, (int32)PY, ItemEvent, ItemCppType, ItemFunction, ItemOut, ItemErr);
			if (ItemErr.IsEmpty())
			{
				auto Extra = MakeShared<FJsonObject>(); Extra->SetStringField(TEXT("node_type"), USP);
				Batch.AddSuccess(i, Extra);
			}
			else Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJsonString);
		return;
	}
	FString USP, MN = TEXT("Execute"), CppType; int32 PX = 0, PY = 0;
	Args->TryGetStringField(TEXT("unit_struct_path"), USP); Args->TryGetStringField(TEXT("method_name"), MN);
	Args->TryGetStringField(TEXT("cpp_type"), CppType);
	Args->TryGetNumberField(TEXT("position_x"), PX); Args->TryGetNumberField(TEXT("position_y"), PY);
	AddRigVMNodeForEvent(AssetPath, USP, MN, PX, PY, Event, CppType, Function, OutJsonString, OutError);
}

void HandleConnectRigPinsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString AssetPath; Args->TryGetStringField(TEXT("asset_path"), AssetPath);
	FString Event; Args->TryGetStringField(TEXT("event"), Event);
	FString Function; Args->TryGetStringField(TEXT("function"), Function);

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("connections"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString SP = BatchToolHelper::GetItemString(Item, TEXT("source_pin_path"), TEXT("source"));
			FString TP = BatchToolHelper::GetItemString(Item, TEXT("target_pin_path"), TEXT("target"));
			FString ItemEvent = Event; Item->TryGetStringField(TEXT("event"), ItemEvent);
			FString ItemFunction = Function; Item->TryGetStringField(TEXT("function"), ItemFunction);
			if (SP.IsEmpty() || TP.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing source_pin_path or target_pin_path")); continue; }
			FString ItemOut, ItemErr;
			ConnectRigPinsForEvent(AssetPath, SP, TP, ItemEvent, ItemFunction, ItemOut, ItemErr);
			if (ItemErr.IsEmpty()) Batch.AddSuccess(i);
			else Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJsonString);
		return;
	}
	FString SP, TP;
	Args->TryGetStringField(TEXT("source_pin_path"), SP); Args->TryGetStringField(TEXT("target_pin_path"), TP);
	ConnectRigPinsForEvent(AssetPath, SP, TP, Event, Function, OutJsonString, OutError);
}

void HandleCreateControlRigFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!FCapabilityProfile::Get().IsEnabled(ECapability::AdvancedTooling)) { OutError = TEXT("Control Rig editor not initialised"); return; }
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString AssetName = TEXT("CR_NewControlRig"), SavePath, SkelMeshPath;
	if (!Args->TryGetStringField(TEXT("asset_name"), AssetName))
		Args->TryGetStringField(TEXT("name"), AssetName);
	Args->TryGetStringField(TEXT("save_path"), SavePath);
	if (SavePath.IsEmpty()) SavePath = FSettingsManager::GetDefaultSavePath();
	Args->TryGetStringField(TEXT("skeletal_mesh_path"), SkelMeshPath);
	HandleCreateControlRig(AssetName, SavePath, SkelMeshPath, OutJsonString, OutError);
}

void HandleAddRigTwoBoneIKFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString AssetPath, RootBone, MidBone, TipBone;
	int32 PosX = 0, PosY = 0;
	if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath) ||
		!Args->TryGetStringField(TEXT("root_bone"), RootBone) ||
		!Args->TryGetStringField(TEXT("mid_bone"), MidBone) ||
		!Args->TryGetStringField(TEXT("tip_bone"), TipBone))
	{
		OutError = TEXT("asset_path, root_bone, mid_bone, tip_bone required");
		return;
	}
	Args->TryGetNumberField(TEXT("position_x"), PosX);
	Args->TryGetNumberField(TEXT("position_y"), PosY);
	AddRigTwoBoneIKImpl(AssetPath, RootBone, MidBone, TipBone, PosX, PosY, Args, OutJsonString, OutError);
}

void HandleGetControlRigSummaryFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString AssetPath;
	if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath))
	{
		OutError = TEXT("asset_path required");
		return;
	}
	HandleGetControlRigSummary(AssetPath, OutJsonString, OutError);
}

void HandleSetRigPinValueFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString AssetPath, PinPath, Value, Event;
	if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath) ||
		!Args->TryGetStringField(TEXT("pin_path"), PinPath) ||
		!Args->TryGetStringField(TEXT("pin_value"), Value))
	{
		OutError = TEXT("asset_path, pin_path, pin_value required");
		return;
	}
	Args->TryGetStringField(TEXT("event"), Event);
	FString Function; Args->TryGetStringField(TEXT("function"), Function);
	SetRigPinValueForEvent(AssetPath, PinPath, Value, Event, Function, OutJsonString, OutError);
}

void HandleGetRigGraphNodesFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString AssetPath, Event;
	if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath))
	{
		OutError = TEXT("asset_path required");
		return;
	}
	Args->TryGetStringField(TEXT("event"), Event);
	FString Function; Args->TryGetStringField(TEXT("function"), Function);
	if (!Function.IsEmpty()) Event = FString::Printf(TEXT("function:%s"), *Function);
	HandleGetRigGraphNodes(AssetPath, Event, OutJsonString, OutError);
}

void HandleRemoveRigNodeFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString AssetPath, NodeName, Event;
	if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath) ||
		!Args->TryGetStringField(TEXT("node_name"), NodeName))
	{
		OutError = TEXT("asset_path and node_name required");
		return;
	}
	Args->TryGetStringField(TEXT("event"), Event);
	FString Function; Args->TryGetStringField(TEXT("function"), Function);
	if (Event.IsEmpty() && Function.IsEmpty()) HandleRemoveRigNode(AssetPath, NodeName, OutJsonString, OutError);
	else                 RemoveRigNodeForEvent(AssetPath, NodeName, Event, Function, OutJsonString, OutError);
}

void HandleAddRigVariableFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString AssetPath, VariableName, CppType, DefaultValue;
	bool bIsGetter = true;
	int32 PosX = 0, PosY = 0;
	if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath) ||
		!Args->TryGetStringField(TEXT("variable_name"), VariableName) ||
		!Args->TryGetStringField(TEXT("cpp_type"), CppType))
	{
		OutError = TEXT("asset_path, variable_name, cpp_type required");
		return;
	}
	Args->TryGetBoolField(TEXT("is_getter"), bIsGetter);
	Args->TryGetStringField(TEXT("default_value"), DefaultValue);
	Args->TryGetNumberField(TEXT("position_x"), PosX);
	Args->TryGetNumberField(TEXT("position_y"), PosY);
	HandleAddRigVariable(AssetPath, VariableName, CppType, bIsGetter, DefaultValue, PosX, PosY, OutJsonString, OutError);
}

void HandleBuildRigLogicFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString AssetPath, NodesJson, LinksJson, Event;
	if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath))
	{
		OutError = TEXT("asset_path required");
		return;
	}
	auto ReadJsonOrArrayField = [&](const TCHAR* FieldName) -> FString
	{
		FString S; if (Args->TryGetStringField(FieldName, S)) return S;
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (Args->TryGetArrayField(FieldName, Arr) && Arr)
		{
			FString Out; TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
			FJsonSerializer::Serialize(*Arr, W);
			return Out;
		}
		return FString();
	};
	NodesJson = ReadJsonOrArrayField(TEXT("nodes"));
	LinksJson = ReadJsonOrArrayField(TEXT("links"));
	Args->TryGetStringField(TEXT("event"), Event);
	BuildRigLogicImpl(AssetPath, NodesJson, LinksJson, Event, OutJsonString, OutError);
}

void HandleDisconnectRigPinsFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString AssetPath, SourcePinPath, TargetPinPath, Event;
	Args->TryGetStringField(TEXT("asset_path"), AssetPath);
	Args->TryGetStringField(TEXT("source_pin_path"), SourcePinPath);
	Args->TryGetStringField(TEXT("target_pin_path"), TargetPinPath);
	Args->TryGetStringField(TEXT("event"), Event);
	FString Function; Args->TryGetStringField(TEXT("function"), Function);
	if (Event.IsEmpty() && Function.IsEmpty()) HandleDisconnectRigPins(AssetPath, SourcePinPath, TargetPinPath, OutJsonString, OutError);
	else                 DisconnectRigPinsForEvent(AssetPath, SourcePinPath, TargetPinPath, Event, Function, OutJsonString, OutError);
}

void HandleListRigVMNodeTypesFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	FString Filter;
	if (Args.IsValid()) Args->TryGetStringField(TEXT("filter"), Filter);
	HandleListRigVMNodeTypes(Filter, OutJsonString, OutError);
}

void HandleSetRigControlPropertiesFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString AssetPath, ControlName, PropertyName, PropertyValue;
	Args->TryGetStringField(TEXT("asset_path"), AssetPath);
	Args->TryGetStringField(TEXT("control_name"), ControlName);
	Args->TryGetStringField(TEXT("property_name"), PropertyName);
	Args->TryGetStringField(TEXT("property_value"), PropertyValue);
	HandleSetRigControlProperties(AssetPath, ControlName, PropertyName, PropertyValue, OutJsonString, OutError);
}

void HandleRemoveRigControlFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString AssetPath, ControlName;
	Args->TryGetStringField(TEXT("asset_path"), AssetPath);
	Args->TryGetStringField(TEXT("control_name"), ControlName);
	HandleRemoveRigControl(AssetPath, ControlName, OutJsonString, OutError);
}

void HandleAddRigNull(const FString& AssetPath, const FString& NullName,
	const FString& ParentName, FString& OutJsonString, FString& OutError)
{
	UControlRigBlueprint* BP = LoadCRBlueprint(AssetPath, OutError);
	if (!BP) return;

	URigHierarchy* Hier = BP->GetHierarchy();
	if (!Hier) { OutError = TEXT("Could not get RigHierarchy."); return; }
	URigHierarchyController* HCtrl = Hier->GetController(true);
	if (!HCtrl) { OutError = TEXT("Could not get RigHierarchyController."); return; }

	FRigElementKey ParentKey;
	if (!ParentName.IsEmpty())
	{
		for (ERigElementType T : { ERigElementType::Bone, ERigElementType::Null, ERigElementType::Control })
		{
			FRigElementKey Candidate(FName(*ParentName), T);
			if (Hier->Contains(Candidate)) { ParentKey = Candidate; break; }
		}
		if (!ParentKey.IsValid())
		{
			OutError = FString::Printf(TEXT("parent '%s' not found as Bone/Null/Control in hierarchy."), *ParentName);
			return;
		}
	}

	FRigElementKey NullKey = HCtrl->AddNull(FName(*NullName), ParentKey, FTransform::Identity, false, false, false);
	if (!NullKey.IsValid()) { OutError = FString::Printf(TEXT("Failed to add null '%s'."), *NullName); return; }

	FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
	UEditorAssetLibrary::SaveAsset(BP->GetPathName(), false);

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"null_name\":\"%s\",\"parent\":\"%s\"}"),
		*NullName, *ParentName);
}

void HandleAddRigNullFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString AssetPath, NullName, ParentName;
	if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath) ||
		!Args->TryGetStringField(TEXT("null_name"),  NullName))
	{
		OutError = TEXT("asset_path and null_name required");
		return;
	}
	Args->TryGetStringField(TEXT("parent"), ParentName);
	HandleAddRigNull(AssetPath, NullName, ParentName, OutJsonString, OutError);
}

void HandleReparentRigElement(const FString& AssetPath, const FString& ElementName,
	const FString& ElementType, const FString& NewParent, bool bMaintainGlobal,
	FString& OutJsonString, FString& OutError)
{
	UControlRigBlueprint* BP = LoadCRBlueprint(AssetPath, OutError);
	if (!BP) return;

	URigHierarchy* Hier = BP->GetHierarchy();
	if (!Hier) { OutError = TEXT("Could not get RigHierarchy."); return; }
	URigHierarchyController* HCtrl = Hier->GetController(true);
	if (!HCtrl) { OutError = TEXT("Could not get RigHierarchyController."); return; }

	FRigElementKey ChildKey;
	if (ElementType.Equals(TEXT("Null"), ESearchCase::IgnoreCase))
		ChildKey = FRigElementKey(FName(*ElementName), ERigElementType::Null);
	else if (ElementType.Equals(TEXT("Control"), ESearchCase::IgnoreCase))
		ChildKey = FRigElementKey(FName(*ElementName), ERigElementType::Control);
	else
	{
		for (ERigElementType T : { ERigElementType::Control, ERigElementType::Null })
		{
			FRigElementKey Candidate(FName(*ElementName), T);
			if (Hier->Contains(Candidate)) { ChildKey = Candidate; break; }
		}
	}
	if (!ChildKey.IsValid() || !Hier->Contains(ChildKey))
	{
		OutError = FString::Printf(TEXT("element '%s' not found as a Control/Null in hierarchy."), *ElementName);
		return;
	}

	if (NewParent.IsEmpty())
	{
		OutError = TEXT("new_parent required (Control/Null/Bone name to reparent under).");
		return;
	}
	FRigElementKey NewParentKey;
	for (ERigElementType T : { ERigElementType::Control, ERigElementType::Null, ERigElementType::Bone })
	{
		FRigElementKey Candidate(FName(*NewParent), T);
		if (Hier->Contains(Candidate)) { NewParentKey = Candidate; break; }
	}
	if (!NewParentKey.IsValid())
	{
		OutError = FString::Printf(TEXT("new_parent '%s' not found as Control/Null/Bone in hierarchy."), *NewParent);
		return;
	}
	if (NewParentKey == ChildKey)
	{
		OutError = TEXT("Cannot parent an element to itself.");
		return;
	}

	const bool bOk = HCtrl->SetParent(ChildKey, NewParentKey, bMaintainGlobal, false, false);
	if (!bOk)
	{
		OutError = FString::Printf(TEXT("Failed to reparent '%s' under '%s' (a control can't parent under its own descendant)."), *ElementName, *NewParent);
		return;
	}

	FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
	UEditorAssetLibrary::SaveAsset(BP->GetPathName(), false);

	const TCHAR* ParentTypeStr =
		NewParentKey.Type == ERigElementType::Bone ? TEXT("Bone")
		: NewParentKey.Type == ERigElementType::Null ? TEXT("Null")
		: NewParentKey.Type == ERigElementType::Control ? TEXT("Control") : TEXT("Other");
	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"element\":\"%s\",\"new_parent\":\"%s\",\"new_parent_type\":\"%s\",\"maintain_global\":%s}"),
		*ElementName, *NewParent, ParentTypeStr, bMaintainGlobal ? TEXT("true") : TEXT("false"));
}

void HandleReparentRigElementFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString AssetPath, ElementName, ElementType, NewParent;
	if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath) ||
		!Args->TryGetStringField(TEXT("element_name"), ElementName))
	{
		OutError = TEXT("asset_path and element_name required");
		return;
	}
	Args->TryGetStringField(TEXT("element_type"), ElementType);
	Args->TryGetStringField(TEXT("new_parent"), NewParent);
	bool bMaintainGlobal = true;
	Args->TryGetBoolField(TEXT("maintain_global"), bMaintainGlobal);
	HandleReparentRigElement(AssetPath, ElementName, ElementType, NewParent, bMaintainGlobal, OutJsonString, OutError);
}

static bool ParseTransformLoose(const FString& In, FTransform& Out);

void HandleAddRigSocketFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString AssetPath, SocketName, ParentName;
	if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath) ||
		!Args->TryGetStringField(TEXT("socket_name"), SocketName))
	{
		OutError = TEXT("asset_path and socket_name required");
		return;
	}
	Args->TryGetStringField(TEXT("parent"), ParentName);

	UControlRigBlueprint* BP = LoadCRBlueprint(AssetPath, OutError);
	if (!BP) return;
	URigHierarchy* Hier = BP->GetHierarchy();
	if (!Hier) { OutError = TEXT("Could not get RigHierarchy."); return; }
	URigHierarchyController* HCtrl = Hier->GetController(true);
	if (!HCtrl) { OutError = TEXT("Could not get RigHierarchyController."); return; }

	FRigElementKey ParentKey;
	if (!ParentName.IsEmpty())
	{
		for (ERigElementType T : { ERigElementType::Bone, ERigElementType::Null, ERigElementType::Control })
		{
			FRigElementKey Candidate(FName(*ParentName), T);
			if (Hier->Contains(Candidate)) { ParentKey = Candidate; break; }
		}
		if (!ParentKey.IsValid())
		{
			OutError = FString::Printf(TEXT("parent '%s' not found as Bone/Null/Control in hierarchy."), *ParentName);
			return;
		}
	}

	FTransform Xf = FTransform::Identity;
	FString TransformStr;
	if (Args->TryGetStringField(TEXT("transform"), TransformStr) && !TransformStr.IsEmpty())
	{
		if (!ParseTransformLoose(TransformStr, Xf))
		{
			OutError = FString::Printf(TEXT("Could not parse transform '%s' (FTransform export string, 'X=,Y=,Z=' translation, or 'Scale=N')."), *TransformStr);
			return;
		}
	}

	FLinearColor Color = FLinearColor::White;
	FString ColorStr;
	if (Args->TryGetStringField(TEXT("color"), ColorStr) && !ColorStr.IsEmpty())
	{
		FLinearColor Parsed;
		if (Parsed.InitFromString(ColorStr)) Color = Parsed;
	}
	FString Description;
	Args->TryGetStringField(TEXT("description"), Description);

	const FRigElementKey SocketKey = HCtrl->AddSocket(
		FName(*SocketName), ParentKey, Xf, ParentKey.IsValid() ? false : true,
		Color, Description, false, false);
	if (!SocketKey.IsValid()) { OutError = FString::Printf(TEXT("Failed to add socket '%s'."), *SocketName); return; }

	FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
	UEditorAssetLibrary::SaveAsset(BP->GetPathName(), false);

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"socket_name\":\"%s\",\"parent\":\"%s\",\"color\":\"%s\"}"),
		*SocketName, *ParentName, *Color.ToString());
}

void HandleAddRigCurveFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString AssetPath, CurveName;
	if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath) ||
		!Args->TryGetStringField(TEXT("curve_name"), CurveName))
	{
		OutError = TEXT("asset_path and curve_name required");
		return;
	}
	double DefaultValue = 0.0;
	Args->TryGetNumberField(TEXT("default_value"), DefaultValue);

	UControlRigBlueprint* BP = LoadCRBlueprint(AssetPath, OutError);
	if (!BP) return;
	URigHierarchy* Hier = BP->GetHierarchy();
	if (!Hier) { OutError = TEXT("Could not get RigHierarchy."); return; }
	URigHierarchyController* HCtrl = Hier->GetController(true);
	if (!HCtrl) { OutError = TEXT("Could not get RigHierarchyController."); return; }

	const FRigElementKey CurveKey = HCtrl->AddCurve(
		FName(*CurveName), (float)DefaultValue, false, false);
	if (!CurveKey.IsValid()) { OutError = FString::Printf(TEXT("Failed to add curve '%s'."), *CurveName); return; }

	FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
	UEditorAssetLibrary::SaveAsset(BP->GetPathName(), false);

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"curve_name\":\"%s\",\"default_value\":%f}"),
		*CurveName, (float)DefaultValue);
}

void HandleAddRigControlSpaceFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString AssetPath, ControlName, SpaceName, DisplayLabel;
	if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath) ||
		!Args->TryGetStringField(TEXT("control_name"), ControlName) ||
		!Args->TryGetStringField(TEXT("space"), SpaceName))
	{
		OutError = TEXT("asset_path, control_name, space required");
		return;
	}
	Args->TryGetStringField(TEXT("display_label"), DisplayLabel);

	UControlRigBlueprint* BP = LoadCRBlueprint(AssetPath, OutError);
	if (!BP) return;
	URigHierarchy* Hier = BP->GetHierarchy();
	if (!Hier) { OutError = TEXT("Could not get RigHierarchy."); return; }
	URigHierarchyController* HCtrl = Hier->GetController(true);
	if (!HCtrl) { OutError = TEXT("Could not get RigHierarchyController."); return; }

	const FRigElementKey ControlKey(FName(*ControlName), ERigElementType::Control);
	if (!Hier->Find<FRigControlElement>(ControlKey))
	{
		OutError = FString::Printf(TEXT("Control '%s' not found."), *ControlName);
		return;
	}

	if (SpaceName.Equals(TEXT("World"), ESearchCase::IgnoreCase) ||
		SpaceName.Equals(TEXT("Default"), ESearchCase::IgnoreCase) ||
		SpaceName.Equals(TEXT("Parent"), ESearchCase::IgnoreCase))
	{
		FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
		OutJsonString = FString::Printf(
			TEXT("{\"success\":true,\"control_name\":\"%s\",\"space\":\"%s\",\"space_type\":\"World/Parent\",\"already_available\":true,"
				 "\"note\":\"World and Parent are built-in default spaces in the picker — no need to add. Only element spaces (control/null/bone) are added.\"}"),
			*ControlName, *SpaceName);
		return;
	}

	FRigElementKey SpaceKey;
	for (ERigElementType T : { ERigElementType::Control, ERigElementType::Null, ERigElementType::Bone })
	{
		FRigElementKey Candidate(FName(*SpaceName), T);
		if (Hier->Contains(Candidate)) { SpaceKey = Candidate; break; }
	}
	if (!SpaceKey.IsValid())
	{
		OutError = FString::Printf(TEXT("space '%s' not found as Control/Null/Bone (World/Parent are built-in, no need to add)."), *SpaceName);
		return;
	}

#if UE_VERSION_OLDER_THAN(5,5,0)
	(void)DisplayLabel; (void)SpaceKey;
	OutError = TEXT("Control space switching (AddAvailableSpace) requires UE 5.5 or newer.");
	return;
#else
  #if UE_VERSION_OLDER_THAN(5,6,0)
	(void)DisplayLabel;
	const bool bOk = HCtrl->AddAvailableSpace(ControlKey, SpaceKey, false, false);
  #else
	const bool bOk = HCtrl->AddAvailableSpace(ControlKey, SpaceKey,
		DisplayLabel.IsEmpty() ? NAME_None : FName(*DisplayLabel), false, false);
  #endif
	if (!bOk)
	{
		OutError = FString::Printf(TEXT("AddAvailableSpace failed for control '%s' (space '%s')."), *ControlName, *SpaceName);
		return;
	}

	FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
	UEditorAssetLibrary::SaveAsset(BP->GetPathName(), false);

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"control_name\":\"%s\",\"space\":\"%s\",\"space_type\":\"%s\"}"),
		*ControlName, *SpaceName,
		SpaceKey.Type == ERigElementType::Control ? TEXT("Control") :
		SpaceKey.Type == ERigElementType::Null ? TEXT("Null") :
		SpaceKey.Type == ERigElementType::Bone ? TEXT("Bone") : TEXT("World/Parent"));
#endif
}

static bool ParseTransformLoose(const FString& In, FTransform& Out)
{
	if (In.IsEmpty()) return false;
	if (Out.InitFromString(In)) return true;
	if (In.StartsWith(TEXT("Scale="), ESearchCase::IgnoreCase))
	{
		const float S = FCString::Atof(*In.Mid(6));
		if (S > 0.0f) { Out = FTransform(FQuat::Identity, FVector::ZeroVector, FVector(S)); return true; }
	}
	if (In.Contains(TEXT("X=")) && In.Contains(TEXT("Y=")) && In.Contains(TEXT("Z=")))
	{
		FVector V;
		if (V.InitFromString(In)) { Out = FTransform(FQuat::Identity, V, FVector::OneVector); return true; }
	}
	return false;
}

void HandleSetRigControlOffsetTransform(const FString& AssetPath, const FString& ControlName,
	const FString& TransformStr, FString& OutJsonString, FString& OutError)
{
	UControlRigBlueprint* BP = LoadCRBlueprint(AssetPath, OutError);
	if (!BP) return;

	URigHierarchy* Hier = BP->GetHierarchy();
	if (!Hier) { OutError = TEXT("Could not get RigHierarchy."); return; }
	FRigElementKey Key(FName(*ControlName), ERigElementType::Control);
	if (!Hier->Find<FRigControlElement>(Key)) { OutError = FString::Printf(TEXT("Control '%s' not found."), *ControlName); return; }

	FTransform Xf;
	if (!ParseTransformLoose(TransformStr, Xf))
	{
		OutError = FString::Printf(TEXT("Could not parse transform '%s' — accepts FTransform export, 'X=…,Y=…,Z=…' translation, or 'Scale=N'."), *TransformStr);
		return;
	}

	Hier->SetControlOffsetTransform(Key, Xf, true, true, false, false);

	FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
	UEditorAssetLibrary::SaveAsset(BP->GetPathName(), false);

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"control_name\":\"%s\",\"offset_transform\":\"%s\"}"),
		*ControlName, *Xf.ToString());
}

void HandleSetRigControlShapeTransform(const FString& AssetPath, const FString& ControlName,
	const FString& TransformStr, FString& OutJsonString, FString& OutError)
{
	UControlRigBlueprint* BP = LoadCRBlueprint(AssetPath, OutError);
	if (!BP) return;

	URigHierarchy* Hier = BP->GetHierarchy();
	if (!Hier) { OutError = TEXT("Could not get RigHierarchy."); return; }
	FRigElementKey Key(FName(*ControlName), ERigElementType::Control);
	if (!Hier->Find<FRigControlElement>(Key)) { OutError = FString::Printf(TEXT("Control '%s' not found."), *ControlName); return; }

	FTransform Xf;
	if (!ParseTransformLoose(TransformStr, Xf))
	{
		OutError = FString::Printf(TEXT("Could not parse transform '%s' — accepts FTransform export, 'X=…,Y=…,Z=…' translation, or 'Scale=N'."), *TransformStr);
		return;
	}

	Hier->SetControlShapeTransform(Key, Xf, true, false);

	FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
	UEditorAssetLibrary::SaveAsset(BP->GetPathName(), false);

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"control_name\":\"%s\",\"shape_transform\":\"%s\"}"),
		*ControlName, *Xf.ToString());
}

void HandleSetRigControlOffsetTransformFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString AssetPath, ControlName, TransformStr;
	if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath) ||
		!Args->TryGetStringField(TEXT("control_name"), ControlName) ||
		!Args->TryGetStringField(TEXT("transform"), TransformStr))
	{
		OutError = TEXT("asset_path, control_name, transform required");
		return;
	}
	HandleSetRigControlOffsetTransform(AssetPath, ControlName, TransformStr, OutJsonString, OutError);
}

void HandleSetRigControlShapeTransformFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString AssetPath, ControlName, TransformStr;
	if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath) ||
		!Args->TryGetStringField(TEXT("control_name"), ControlName) ||
		!Args->TryGetStringField(TEXT("transform"), TransformStr))
	{
		OutError = TEXT("asset_path, control_name, transform required");
		return;
	}
	HandleSetRigControlShapeTransform(AssetPath, ControlName, TransformStr, OutJsonString, OutError);
}

void HandleAddRigControlChainFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString AssetPath;
	if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath)) { OutError = TEXT("asset_path required"); return; }

	const TArray<TSharedPtr<FJsonValue>>* BonesArr = nullptr;
	if (!Args->TryGetArrayField(TEXT("bone_chain"), BonesArr) || !BonesArr || BonesArr->Num() == 0)
	{
		OutError = TEXT("bone_chain (array of bone names) required");
		return;
	}

	FString NameTemplate = TEXT("Ctrl_{bone}");
	Args->TryGetStringField(TEXT("name_template"), NameTemplate);
	FString CT = TEXT("Transform");
	Args->TryGetStringField(TEXT("control_type"), CT);

	BatchToolHelper::FBatchResultBuilder Batch;
	for (int32 i = 0; i < BonesArr->Num(); i++)
	{
		const FString BoneName = (*BonesArr)[i]->AsString();
		if (BoneName.IsEmpty()) { Batch.AddFailure(i, TEXT("Empty bone name")); continue; }

		const FString CtrlName = NameTemplate.Replace(TEXT("{bone}"), *BoneName);

		TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
		FString ItemOut, ItemErr;
		AddRigControlImpl(AssetPath, CtrlName, CT, BoneName, Item, ItemOut, ItemErr);
		if (ItemErr.IsEmpty())
		{
			auto Extra = MakeShared<FJsonObject>();
			Extra->SetStringField(TEXT("control_name"), CtrlName);
			Extra->SetStringField(TEXT("bone"), BoneName);
			Batch.AddSuccess(i, Extra);
		}
		else Batch.AddFailure(i, ItemErr);
	}
	Batch.Finalize(OutJsonString);
}

void HandleMirrorRigControlFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString AssetPath, SourceName;
	if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath) ||
		!Args->TryGetStringField(TEXT("source_control"), SourceName))
	{
		OutError = TEXT("asset_path and source_control required");
		return;
	}
	FString MirrorAxis = TEXT("X");
	Args->TryGetStringField(TEXT("mirror_axis"), MirrorAxis);

	UControlRigBlueprint* BP = LoadCRBlueprint(AssetPath, OutError);
	if (!BP) return;
	URigHierarchy* Hier = BP->GetHierarchy();
	if (!Hier) { OutError = TEXT("Could not get RigHierarchy."); return; }
	URigHierarchyController* HCtrl = Hier->GetController(true);
	if (!HCtrl) { OutError = TEXT("Could not get RigHierarchyController."); return; }

	FRigControlElement* Source = Hier->Find<FRigControlElement>(FRigElementKey(FName(*SourceName), ERigElementType::Control));
	if (!Source) { OutError = FString::Printf(TEXT("Source control '%s' not found."), *SourceName); return; }

	FString MirrorName;
	if (FString OptName; Args->TryGetStringField(TEXT("mirror_name"), OptName) && !OptName.IsEmpty())
	{
		MirrorName = OptName;
	}
	else
	{
		struct FSidePair { const TCHAR* From; const TCHAR* To; };
		const FSidePair Pairs[] = {
			{ TEXT("_L"),    TEXT("_R")    },
			{ TEXT("_R"),    TEXT("_L")    },
			{ TEXT("_Left"), TEXT("_Right") },
			{ TEXT("_Right"),TEXT("_Left") },
		};
		for (const FSidePair& P : Pairs)
		{
			if (SourceName.EndsWith(P.From, ESearchCase::IgnoreCase))
			{
				MirrorName = SourceName.LeftChop(FCString::Strlen(P.From)) + P.To;
				break;
			}
		}
		if (MirrorName.IsEmpty())
		{
			OutError = FString::Printf(TEXT("source_control '%s' has no _L/_R/_Left/_Right suffix to swap — pass mirror_name explicitly."), *SourceName);
			return;
		}
	}

	if (Hier->Contains(FRigElementKey(FName(*MirrorName), ERigElementType::Control)))
	{
		OutError = FString::Printf(TEXT("mirror target '%s' already exists."), *MirrorName);
		return;
	}

	const FTransform SourceOffset = Hier->GetControlOffsetTransform(Source, ERigTransformType::InitialLocal);
	FTransform MirrorOffset = SourceOffset;
	FVector T = SourceOffset.GetTranslation();
	FQuat   Q = SourceOffset.GetRotation();
	if (MirrorAxis.Equals(TEXT("X"), ESearchCase::IgnoreCase))
	{
		T.X = -T.X;
		Q = FQuat(Q.X, -Q.Y, -Q.Z, Q.W);
	}
	else if (MirrorAxis.Equals(TEXT("Y"), ESearchCase::IgnoreCase))
	{
		T.Y = -T.Y;
		Q = FQuat(-Q.X, Q.Y, -Q.Z, Q.W);
	}
	else if (MirrorAxis.Equals(TEXT("Z"), ESearchCase::IgnoreCase))
	{
		T.Z = -T.Z;
		Q = FQuat(-Q.X, -Q.Y, Q.Z, Q.W);
	}
	MirrorOffset.SetTranslation(T);
	MirrorOffset.SetRotation(Q.GetNormalized());

	FRigControlSettings Settings = Source->Settings;
	Settings.DisplayName = FName(*MirrorName);
	Settings.ShapeColor  = DefaultShapeColorForName(MirrorName);

	const FTransform ShapeXf = Hier->GetControlShapeTransform(Source, ERigTransformType::InitialLocal);

	FRigElementKey ParentKey = Hier->GetFirstParent(Source->GetKey());
	if (ParentKey.IsValid() && ParentKey.Type == ERigElementType::Bone)
	{
		const FString PStr = ParentKey.Name.ToString();
		FString MirroredParent;
		if (PStr.EndsWith(TEXT("_l"), ESearchCase::IgnoreCase))      MirroredParent = PStr.LeftChop(2) + TEXT("_r");
		else if (PStr.EndsWith(TEXT("_r"), ESearchCase::IgnoreCase)) MirroredParent = PStr.LeftChop(2) + TEXT("_l");
		if (!MirroredParent.IsEmpty())
		{
			FRigElementKey Candidate(FName(*MirroredParent), ERigElementType::Bone);
			if (Hier->Contains(Candidate)) ParentKey = Candidate;
		}
	}

	FRigElementKey NewKey = HCtrl->AddControl(
		FName(*MirrorName), ParentKey, Settings,
		Settings.GetIdentityValue(), MirrorOffset, ShapeXf);
	if (!NewKey.IsValid()) { OutError = FString::Printf(TEXT("Failed to add mirrored control '%s'."), *MirrorName); return; }

	FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
	UEditorAssetLibrary::SaveAsset(BP->GetPathName(), false);

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"source_control\":\"%s\",\"mirror_control\":\"%s\",\"mirror_axis\":\"%s\","
			 "\"parent\":\"%s\",\"shape_color\":\"%s\"}"),
		*SourceName, *MirrorName, *MirrorAxis,
		*ParentKey.Name.ToString(), *Settings.ShapeColor.ToString());
}

void HandleAddRigAimConstraintFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString AssetPath, TargetBone, AimControl;
	if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath) ||
		!Args->TryGetStringField(TEXT("target_bone"), TargetBone) ||
		!Args->TryGetStringField(TEXT("aim_control"), AimControl))
	{
		OutError = TEXT("asset_path, target_bone, aim_control required");
		return;
	}
	FString PrimaryAxis = TEXT("(X=1.0,Y=0.0,Z=0.0)");
	Args->TryGetStringField(TEXT("primary_axis"), PrimaryAxis);
	double PosX = 200.0, PosY = 0.0;
	Args->TryGetNumberField(TEXT("position_x"), PosX);
	Args->TryGetNumberField(TEXT("position_y"), PosY);

	UControlRigBlueprint* BP = nullptr;
	URigVMController* Controller = GetRVMController(AssetPath, BP, OutError);
	if (!Controller) return;

	bool bAimControlPushedForward = false;
	if (URigHierarchy* Hier = BP->GetHierarchy())
	{
		const FRigElementKey TargetKey(FName(*TargetBone), ERigElementType::Bone);
		const FRigElementKey AimKey(FName(*AimControl), ERigElementType::Control);
		if (!Hier->Contains(TargetKey))
		{
			OutError = FString::Printf(TEXT("target_bone '%s' not found."), *TargetBone);
			return;
		}
		if (!Hier->Contains(AimKey))
		{
			OutError = FString::Printf(TEXT("aim_control '%s' not found."), *AimControl);
			return;
		}

		const FTransform BoneGlobal = Hier->GetInitialGlobalTransform(TargetKey);
		const FTransform AimGlobal  = Hier->GetGlobalTransform(AimKey, true);
		if (FVector::Dist(BoneGlobal.GetLocation(), AimGlobal.GetLocation()) < 2.0f)
		{
			FVector AxisLocal(1.f, 0.f, 0.f);
			AxisLocal.InitFromString(PrimaryAxis);
			const FVector ForwardWorld = BoneGlobal.GetRotation().RotateVector(AxisLocal.GetSafeNormal()) * 100.f;
			FTransform Pushed = AimGlobal;
			Pushed.SetLocation(AimGlobal.GetLocation() + ForwardWorld);
			SnapControlOffsetToGlobal(Hier, AimKey, Pushed);
			bAimControlPushedForward = true;
		}
	}

	URigVMUnitNode* AimNode = Controller->AddUnitNodeFromStructPath(
		TEXT("/Script/ControlRig.RigUnit_AimItem"),
		TEXT("Execute"), FVector2D((float)PosX, (float)PosY), TEXT(""), true, false);
	if (!AimNode) { OutError = TEXT("Failed to spawn AimItem unit node."); return; }

	const FString AimPath = AimNode->GetNodePath();
	Controller->SetPinDefaultValue(AimPath + TEXT(".Item"),
		FString::Printf(TEXT("(Type=Bone,Name=\"%s\")"), *TargetBone), true, true);
	Controller->SetPinDefaultValue(AimPath + TEXT(".Primary.Axis"), PrimaryAxis, true, true);

	URigVMUnitNode* GetNode = Controller->AddUnitNodeFromStructPath(
		TEXT("/Script/ControlRig.RigUnit_GetTransform"),
		TEXT("Execute"), FVector2D((float)PosX - 320.f, (float)PosY + 100.f), TEXT(""), true, false);
	bool bAutoWiredTarget = false;
	if (GetNode)
	{
		const FString GetPath = GetNode->GetNodePath();
		Controller->SetPinDefaultValue(GetPath + TEXT(".Item"),
			FString::Printf(TEXT("(Type=Control,Name=\"%s\")"), *AimControl), true, true);
		bAutoWiredTarget = Controller->AddLink(
			GetPath + TEXT(".Transform.Translation"),
			AimPath + TEXT(".Primary.Target"),
			true);
	}

	bool bAutoWiredExec = false;
	if (URigVMGraph* Graph = AimNode->GetGraph())
	{
		FString TailExecPath;
		if (FindExecChainTail(Graph, TailExecPath))
		{
			bAutoWiredExec = Controller->AddLink(
				TailExecPath,
				AimPath + TEXT(".ExecutePin"),
				true);
		}
	}

	FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
	UEditorAssetLibrary::SaveAsset(BP->GetPathName(), false);

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"aim_node\":\"%s\",\"get_node\":\"%s\",\"target_bone\":\"%s\","
			 "\"aim_control\":\"%s\",\"primary_axis\":\"%s\","
			 "\"auto_wired_exec\":%s,\"auto_wired_target\":%s,\"aim_control_pushed_forward\":%s}"),
		*AimNode->GetName(), GetNode ? *GetNode->GetName() : TEXT(""), *TargetBone, *AimControl, *PrimaryAxis,
		bAutoWiredExec   ? TEXT("true") : TEXT("false"),
		bAutoWiredTarget ? TEXT("true") : TEXT("false"),
		bAimControlPushedForward ? TEXT("true") : TEXT("false"));
}

void HandleCompileControlRig(const FString& AssetPath, FString& OutJsonString, FString& OutError)
{
	UControlRigBlueprint* BP = LoadCRBlueprint(AssetPath, OutError);
	if (!BP) return;

	FCompilerResultsLog Results;
	Results.bSilentMode = true;

	FKismetEditorUtilities::CompileBlueprint(BP,
		EBlueprintCompileOptions::None, &Results);

	TArray<TSharedPtr<FJsonValue>> ErrorsJson;
	TArray<TSharedPtr<FJsonValue>> WarningsJson;
	for (const TSharedRef<FTokenizedMessage>& Msg : Results.Messages)
	{
		const EMessageSeverity::Type Sev = Msg->GetSeverity();
		const FString Text = Msg->ToText().ToString();
		if (Sev == EMessageSeverity::Error)
			ErrorsJson.Add(MakeShared<FJsonValueString>(Text));
		else if (Sev == EMessageSeverity::Warning)
			WarningsJson.Add(MakeShared<FJsonValueString>(Text));
	}

	UEditorAssetLibrary::SaveAsset(BP->GetPathName(), false);

	TSharedRef<FJsonObject> Out = MakeShared<FJsonObject>();
	Out->SetBoolField(TEXT("success"), Results.NumErrors == 0);
	Out->SetStringField(TEXT("asset_path"), AssetPath);
	Out->SetNumberField(TEXT("num_errors"), Results.NumErrors);
	Out->SetNumberField(TEXT("num_warnings"), Results.NumWarnings);
	Out->SetArrayField(TEXT("errors"), ErrorsJson);
	Out->SetArrayField(TEXT("warnings"), WarningsJson);

	FString Serialised;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Serialised);
	FJsonSerializer::Serialize(Out, Writer);
	OutJsonString = Serialised;
}

void HandleCompileControlRigFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString AssetPath;
	if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath))
	{
		OutError = TEXT("asset_path required");
		return;
	}
	HandleCompileControlRig(AssetPath, OutJsonString, OutError);
}

}
