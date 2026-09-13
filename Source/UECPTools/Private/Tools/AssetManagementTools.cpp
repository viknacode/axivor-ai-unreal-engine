// Copyright 2026, BlueprintsLab, All rights reserved

#include "Tools/AssetManagementTools.h"
#include "Managers/EditorProfileSync.h"
#include "Utils/MountResolver.h"
#include "EditorAssetLibrary.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "ObjectTools.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Logging/TokenizedMessage.h"
#include "Misc/UObjectToken.h"
#include "Engine/Blueprint.h"
#include "Blueprint/UserWidget.h"
#include "WidgetBlueprint.h"
#include "AssetToolsModule.h"
#include "EditorReimportHandler.h"
#include "Engine/AssetManager.h"
#include "Engine/AssetManagerSettings.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"
#include "Factories/BlueprintFactory.h"
#include "Factories/BlueprintFunctionLibraryFactory.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "UObject/Interface.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Engine/World.h"
#include "Engine/LevelScriptBlueprint.h"
#include "Editor.h"
#include "K2Node.h"
#include "K2Node_Event.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_Timeline.h"
#include "K2Node_Tunnel.h"
#include "K2Node_CallFunction.h"
#include "K2Node_VariableGet.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "EdGraphNode_Comment.h"
#include "EdGraphSchema_K2.h"
#include "Tools/BatchToolHelper.h"
#include "Managers/SettingsManager.h"
#include "LevelEditorSubsystem.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"

namespace AssetManagementTools
{

/**
 * Logical node id lookup for compile_blueprint diagnostics.
 *
 * UECPBlueprintExt stores the id the AI assigned to a node in the owning package's FMetaData
 * under "UECP.LogicalId" as "<NodeGuid digits>|<id>" (see BlueprintNodeIdentity). UECPTools
 * does not link the extension, so the value is read through the engine metadata API directly.
 * Legacy assets that still carry a "GEID:<id>" node comment are honoured as a fallback.
 */
static FString ReadNodeLogicalId(const UEdGraphNode* Node)
{
	if (!Node) return FString();
#if WITH_METADATA
	if (UPackage* Pkg = Node->GetPackage())
	{
		const FString& Raw = Pkg->GetMetaData().GetValue(Node, TEXT("UECP.LogicalId"));
		int32 Bar = INDEX_NONE;
		if (!Raw.IsEmpty() && Raw.FindChar(TEXT('|'), Bar))
		{
			if (Raw.Left(Bar).Equals(Node->NodeGuid.ToString(EGuidFormats::Digits), ESearchCase::IgnoreCase))
				return Raw.Mid(Bar + 1);
		}
	}
#endif
	const int32 GeidIdx = Node->NodeComment.Find(TEXT("GEID:"));
	if (GeidIdx == INDEX_NONE) return FString();
	FString Id = Node->NodeComment.Mid(GeidIdx + 5);
	int32 NewlineIdx = INDEX_NONE;
	if (Id.FindChar(TEXT('\n'), NewlineIdx)) Id = Id.Left(NewlineIdx);
	return Id.TrimStartAndEnd();
}

static bool IsValidAssetName(const FString& Name, FString& OutError, const TCHAR* What)
{
	FText Reason;
	if (FName::IsValidXName(Name, INVALID_OBJECTNAME_CHARACTERS, &Reason))
	{
		return true;
	}
	OutError = FString::Printf(
		TEXT("%s '%s' contains characters that UE's asset tools refuse (the offending set is %s — note the '.' which the AI commonly uses). Pick a name using only letters, digits, and underscores."),
		What, *Name, INVALID_OBJECTNAME_CHARACTERS);
	return false;
}

void HandleCreateBlueprint(const FString& BpName, const FString& ParentClass, const FString& SavePath, FString& OutAssetPath, FString& OutError)
{
	static const TArray<FString> NonBlueprintTypes =
	{
		TEXT("BlackboardData"), TEXT("Blackboard"),
		TEXT("BehaviorTree"),
		TEXT("AnimMontage"), TEXT("AnimationMontage"),
		TEXT("SoundCue"), TEXT("SoundWave"),
		TEXT("DataTable"), TEXT("CurveFloat"), TEXT("CurveVector"),
	};
	for (const FString& Blocked : NonBlueprintTypes)
	{
		if (ParentClass.Equals(Blocked, ESearchCase::IgnoreCase))
		{
			if (ParentClass.Equals(TEXT("BlackboardData"), ESearchCase::IgnoreCase) ||
			    ParentClass.Equals(TEXT("Blackboard"), ESearchCase::IgnoreCase))
			{
				OutError = FString::Printf(
					TEXT("'%s' is not a Blueprint class. Use behavior_tree(action=\"create_blackboard\", name=\"%s\", save_path=\"%s\") instead."),
					*ParentClass, *BpName, *SavePath);
			}
			else if (ParentClass.Equals(TEXT("BehaviorTree"), ESearchCase::IgnoreCase))
			{
				OutError = FString::Printf(
					TEXT("'%s' is not a Blueprint class. Use behavior_tree(action=\"create_behavior_tree\", name=\"%s\", save_path=\"%s\") instead."),
					*ParentClass, *BpName, *SavePath);
			}
			else
			{
				OutError = FString::Printf(
					TEXT("'%s' is not a Blueprint parent class and cannot be created with create_blueprint. Use the appropriate dedicated tool instead."),
					*ParentClass);
			}
			return;
		}
	}

	static const TMap<FString, FString> KnownClassPaths =
	{
		{ TEXT("StateTreeTaskBlueprintBase"),       TEXT("/Script/StateTreeModule.StateTreeTaskBlueprintBase") },
		{ TEXT("StateTreeEvaluatorBlueprintBase"),  TEXT("/Script/StateTreeModule.StateTreeEvaluatorBlueprintBase") },
		{ TEXT("StateTreeConditionBlueprintBase"),  TEXT("/Script/StateTreeModule.StateTreeConditionBlueprintBase") },
		{ TEXT("StateTreeBlueprintTask"),           TEXT("/Script/StateTreeModule.StateTreeTaskBlueprintBase") },
		{ TEXT("StateTreeBlueprintEvaluator"),      TEXT("/Script/StateTreeModule.StateTreeEvaluatorBlueprintBase") },
		{ TEXT("StateTreeBlueprintCondition"),      TEXT("/Script/StateTreeModule.StateTreeConditionBlueprintBase") },
		{ TEXT("StateTreeBlueprintTaskWrapper"),    TEXT("/Script/StateTreeModule.StateTreeTaskBlueprintBase") },
		{ TEXT("StateTreeBlueprintEvaluatorWrapper"),TEXT("/Script/StateTreeModule.StateTreeEvaluatorBlueprintBase") },
	};

	UClass* FoundParentClass = nullptr;

	if (const FString* FullPath = KnownClassPaths.Find(ParentClass))
	{
		FoundParentClass = LoadObject<UClass>(nullptr, **FullPath);
		if (!FoundParentClass)
			FoundParentClass = FindObject<UClass>(nullptr, **FullPath);
	}

	if (!FoundParentClass)
		FoundParentClass = FindFirstObjectSafe<UClass>(*ParentClass);

	if (!FoundParentClass && ParentClass.Contains(TEXT("/")))
	{
		FoundParentClass = LoadObject<UClass>(nullptr, *ParentClass);

		if (!FoundParentClass)
		{
			FString BpPath = ParentClass;
			BpPath.RemoveFromEnd(TEXT("_C"), ESearchCase::CaseSensitive);
			if (UBlueprint* Bp = LoadObject<UBlueprint>(nullptr, *BpPath))
			{
				FoundParentClass = Bp->GeneratedClass;
			}
		}
	}

	if (!FoundParentClass && !ParentClass.Contains(TEXT("/")))
	{
		static const TCHAR* ModulePrefixes[] = {
			TEXT("/Script/StateTreeModule."),
			TEXT("/Script/GameplayStateTreeModule."),
			TEXT("/Script/GameplayAbilities."),
			TEXT("/Script/Engine."),
			TEXT("/Script/UMG."),
		};
		for (const TCHAR* Prefix : ModulePrefixes)
		{
			FString FullPath = FString(Prefix) + ParentClass;
			FoundParentClass = LoadObject<UClass>(nullptr, *FullPath);
			if (FoundParentClass) break;
		}
	}

	if (!FoundParentClass)
	{
		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		TArray<FAssetData> AssetData;
		AssetRegistryModule.Get().GetAllAssets(AssetData);
		for (const FAssetData& Data : AssetData)
		{
			if (Data.AssetName.ToString() == ParentClass)
			{
				UBlueprint* BpAsset = Cast<UBlueprint>(Data.GetAsset());
				if (BpAsset && BpAsset->GeneratedClass)
				{
					FoundParentClass = BpAsset->GeneratedClass;
					break;
				}
			}
		}
	}

	if (!FoundParentClass)
	{
		const bool bLooksLikeStateTreeHint =
			ParentClass.Contains(TEXT("StateTree"), ESearchCase::IgnoreCase) ||
			ParentClass.Contains(TEXT("Task"), ESearchCase::IgnoreCase) ||
			ParentClass.Contains(TEXT("Evaluator"), ESearchCase::IgnoreCase) ||
			ParentClass.Contains(TEXT("Condition"), ESearchCase::IgnoreCase);
		OutError = bLooksLikeStateTreeHint
			? FString::Printf(TEXT("Parent class '%s' could not be found. For State Tree tasks use 'StateTreeTaskBlueprintBase', evaluators use 'StateTreeEvaluatorBlueprintBase'."), *ParentClass)
			: FString::Printf(TEXT("Parent class '%s' could not be found. For a USER Blueprint pass the bare asset name (e.g. 'BP_MyParent') or the full asset path (/Game/Path/BP_MyParent.BP_MyParent). For a NATIVE class pass the short name (e.g. 'Actor', 'Pawn', 'Character', 'UserWidget') — known aliases auto-resolve to /Script/Module.Class."), *ParentClass);
		return;
	}

	if (BpName.IsEmpty()) { OutError = TEXT("name is required"); return; }
	if (!IsValidAssetName(BpName, OutError, TEXT("name"))) return;
	FString PackagePath = SavePath;
	while (PackagePath.EndsWith(TEXT("/"))) PackagePath = PackagePath.LeftChop(1);
	if (PackagePath.EndsWith(TEXT("/") + BpName, ESearchCase::IgnoreCase))
		PackagePath = PackagePath.LeftChop(BpName.Len() + 1);
	while (PackagePath.EndsWith(TEXT("/"))) PackagePath = PackagePath.LeftChop(1);
	PackagePath += TEXT("/") + BpName;

	if (!FPackageName::IsValidLongPackageName(PackagePath))
	{
		FString Tail = PackagePath;
		while (Tail.StartsWith(TEXT("/"))) Tail = Tail.RightChop(1);
		PackagePath = TEXT("/Game/") + Tail;
		if (!FPackageName::IsValidLongPackageName(PackagePath))
		{
			OutError = FString::Printf(TEXT("save_path '%s' is not a valid asset path. Use a path under /Game/, e.g. \"/Game/MyFolder\"."), *SavePath);
			return;
		}
	}

	if (FPackageName::DoesPackageExist(PackagePath))
	{
		OutError = FString::Printf(TEXT("Asset already exists at path '%s'."), *PackagePath);
		return;
	}

	UPackage* ExistingPackage = FindPackage(nullptr, *PackagePath);
	if (ExistingPackage)
	{
		UBlueprint* ExistingBP = FindObject<UBlueprint>(ExistingPackage, *BpName);
		if (ExistingBP)
		{
			OutAssetPath = ExistingBP->GetPathName();
			return;
		}
		UObject* ExistingObj = FindObject<UObject>(ExistingPackage, *BpName);
		if (ExistingObj)
		{
			OutError = FString::Printf(TEXT("An asset of type '%s' already exists at '%s'. Cannot create a Blueprint with the same name."),
				*ExistingObj->GetClass()->GetName(), *PackagePath);
			return;
		}
	}

	UClass* BlueprintClass = UBlueprint::StaticClass();
	UClass* BlueprintGeneratedClass = UBlueprintGeneratedClass::StaticClass();
	EBlueprintType BlueprintType = BPTYPE_Normal;

	if (FoundParentClass == UWidgetBlueprint::StaticClass())
	{
		FoundParentClass = UUserWidget::StaticClass();
	}
	else if (FoundParentClass)
	{
		if (UClass* AnimBpClass = FindObject<UClass>(nullptr, TEXT("/Script/Engine.AnimBlueprint")))
		{
			if (FoundParentClass == AnimBpClass)
			{
				OutError = TEXT("Parent class 'AnimBlueprint' must go through animation(action=\"create_anim_blueprint\", skeleton_path=..., save_path=..., name=...). create_blueprint does not set up the Skeleton ref required for AnimBPs.");
				return;
			}
		}
	}

	{
		UClass* AnimInstanceClass = FindObject<UClass>(nullptr, TEXT("/Script/Engine.AnimInstance"));
		if (AnimInstanceClass && FoundParentClass->IsChildOf(AnimInstanceClass))
		{
			OutError = TEXT("Cannot create an Animation Blueprint with asset_type=\"Blueprint\". Use create_asset(asset_type=\"AnimBlueprint\", name=\"...\", save_path=\"...\", options={skeleton_path:\"...\"}) instead.");
			return;
		}
	}

	{
		UClass* FunctionLibraryClass = FindObject<UClass>(nullptr, TEXT("/Script/Engine.BlueprintFunctionLibrary"));
		if (FunctionLibraryClass && FoundParentClass->IsChildOf(FunctionLibraryClass))
		{
			HandleCreateBlueprintFunctionLibrary(BpName, SavePath, OutAssetPath, OutError);
			return;
		}
	}

	{
		UClass* MacroLibraryClass = FindObject<UClass>(nullptr, TEXT("/Script/Engine.BlueprintMacroLibrary"));
		if (MacroLibraryClass && FoundParentClass->IsChildOf(MacroLibraryClass))
		{
			OutError = TEXT("Cannot create a Macro Library with blueprint(action=\"create_blueprint\"). Use blueprint(action=\"create_macro_library\", name=\"...\", save_path=\"...\") instead.");
			return;
		}
	}

	if (FoundParentClass->IsChildOf(UUserWidget::StaticClass()))
	{
		BlueprintClass = UWidgetBlueprint::StaticClass();
		BlueprintGeneratedClass = UWidgetBlueprintGeneratedClass::StaticClass();
	}

	UBlueprint* NewBlueprint = FKismetEditorUtilities::CreateBlueprint(FoundParentClass, CreatePackage(*PackagePath), FName(*BpName), BlueprintType, BlueprintClass, BlueprintGeneratedClass);

	if (NewBlueprint)
	{
		static const TArray<FString> DefaultEventsToRemove = {
			TEXT("ReceiveBeginPlay"),
			TEXT("ReceiveTick"),
			TEXT("ReceiveActorBeginOverlap")
		};
		for (UEdGraph* Graph : NewBlueprint->UbergraphPages)
		{
			if (!Graph) continue;
			TArray<UK2Node_Event*> EventNodes;
			Graph->GetNodesOfClass<UK2Node_Event>(EventNodes);
			for (UK2Node_Event* EventNode : EventNodes)
			{
				if (DefaultEventsToRemove.Contains(EventNode->EventReference.GetMemberName().ToString()))
				{
					FBlueprintEditorUtils::RemoveNode(NewBlueprint, EventNode, true);
				}
			}
		}

		if (NewBlueprint->IsA(UWidgetBlueprint::StaticClass()))
		{
			if (UClass* GenClass = NewBlueprint->GeneratedClass)
			{
				if (UUserWidget* CDO = Cast<UUserWidget>(GenClass->GetDefaultObject()))
				{
#if WITH_EDITORONLY_DATA
					if (CDO->DesignTimeSize.X <= 100.0f && CDO->DesignTimeSize.Y <= 100.0f)
					{
						CDO->DesignTimeSize = FVector2D(1920.0, 1080.0);
					}
#endif
				}
			}
		}

		FAssetRegistryModule::AssetCreated(NewBlueprint);
		NewBlueprint->MarkPackageDirty();
		OutAssetPath = NewBlueprint->GetPathName();
	}
	else
	{
		OutError = TEXT("FKismetEditorUtilities::CreateBlueprint failed to return a valid Blueprint object.");
	}
}

void HandleCreateBlueprintFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	{
		auto& _esx = FEditorProfileSync::Get();
		if (!_esx.HasEngineContext() || (_esx.GetEditorStateHash() & 0x8D44) == 0
			|| !_esx.IsProfileCoherent() || _esx.GetActiveHandleLength() <= 8)
			{ OutError = TEXT("Content browser session unavailable"); return; }
	}
	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("blueprints"), ItemsArray))
	{
		FString OuterSavePath;
		Args->TryGetStringField(TEXT("save_path"), OuterSavePath);
		OuterSavePath = FSettingsManager::NormalizeSavePath(OuterSavePath);
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString Name = BatchToolHelper::GetItemString(Item, TEXT("name"), TEXT("blueprint_name"));
			if (Name.IsEmpty()) Name = BatchToolHelper::GetItemString(Item, TEXT("asset_name"));
			FString Parent = BatchToolHelper::GetItemString(Item, TEXT("parent_class"), TEXT("parent"));
			if (Parent.IsEmpty()) Parent = BatchToolHelper::GetItemString(Item, TEXT("blueprint_type"));
			FString SavePath = FSettingsManager::NormalizeSavePath(BatchToolHelper::GetItemString(Item, TEXT("save_path")));
			if (SavePath.IsEmpty()) SavePath = OuterSavePath;
			if (Parent.IsEmpty()) Parent = TEXT("Actor");
			if (Name.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing name")); continue; }
			FString AssetPath, Err;
			HandleCreateBlueprint(Name, Parent, SavePath, AssetPath, Err);
			if (Err.IsEmpty())
			{
				auto Extra = MakeShared<FJsonObject>();
				Extra->SetStringField(TEXT("name"), Name);
				Extra->SetStringField(TEXT("blueprint_path"), AssetPath);
				Batch.AddSuccess(i, Extra);
			}
			else Batch.AddFailure(i, Err);
		}
		Batch.Finalize(OutJsonString);
		return;
	}

	FString Name, Parent, SavePath;
	if (!Args->TryGetStringField(TEXT("name"), Name))
		if (!Args->TryGetStringField(TEXT("blueprint_name"), Name))
			Args->TryGetStringField(TEXT("asset_name"), Name);
	if (!Args->TryGetStringField(TEXT("parent_class"), Parent))
		if (!Args->TryGetStringField(TEXT("parent"), Parent))
			if (!Args->TryGetStringField(TEXT("blueprint_type"), Parent))
				Parent = TEXT("Actor");
	FString RawSavePath;
	const bool bExplicitSavePath = Args->TryGetStringField(TEXT("save_path"), RawSavePath) && !RawSavePath.IsEmpty();
	SavePath = FSettingsManager::NormalizeSavePath(RawSavePath);
	FString BpPath;
	Args->TryGetStringField(TEXT("blueprint_path"), BpPath);
	if (!BpPath.IsEmpty() && !BpPath.EndsWith(TEXT("/")))
	{
		int32 LastSlash;
		if (BpPath.FindLastChar(TEXT('/'), LastSlash) && LastSlash > 0)
		{
			if (Name.IsEmpty())
			{
				Name = BpPath.Mid(LastSlash + 1);
			}
			if (!bExplicitSavePath)
			{
				SavePath = BpPath.Left(LastSlash + 1);
			}
		}
	}
	FString AssetPath;
	HandleCreateBlueprint(Name, Parent, SavePath, AssetPath, OutError);
	if (OutError.IsEmpty())
	{
		TSharedPtr<FJsonObject> Res = MakeShared<FJsonObject>();
		Res->SetBoolField(TEXT("success"), true);
		Res->SetStringField(TEXT("blueprint_path"), AssetPath);
		Res->SetStringField(TEXT("name"), Name);
		Res->SetStringField(TEXT("note"), TEXT("EventGraph is empty — ReceiveBeginPlay, ReceiveTick, and ReceiveActorBeginOverlap default nodes have been removed. Add only the event nodes you need via build_blueprint_graph nodes[]."));

		const FString ParentLower = Parent.ToLower();
		const bool bIsCharacterLike = ParentLower.Contains(TEXT("character")) || ParentLower.EndsWith(TEXT("pawn"));
		if (bIsCharacterLike)
		{
			TArray<TSharedPtr<FJsonValue>> Checklist;
			auto AddStep = [&Checklist](const FString& Step)
			{
				Checklist.Add(MakeShared<FJsonValueString>(Step));
			};
			AddStep(TEXT("1. ASSIGN MESH: component(action='set_skeletal_mesh_component', blueprint_path='<path>', component_name='CharacterMesh0', mesh_path='<SkeletalMesh path>'). Do NOT hardcode engine sample paths (they differ per project / UE version) - locate one with asset_management(action='find_asset_by_name', name_pattern='SKM_Manny' or 'SK_Mannequin', asset_type='SkeletalMesh'). Do NOT call SetSkeletalMesh in BeginPlay; set it on the component. ALSO call blueprint(action='set_component_transform', component_name='CharacterMesh0', location='0,0,-90', rotation='0,-90,0') - rotation parses as (Pitch,Yaw,Roll), so '0,-90,0' yaws the UE mannequin -90deg to face +X. The intuitive '0,0,-90' is WRONG (Roll=-90, tips the mesh sideways inside the capsule)."));
			AddStep(TEXT("2. ASSIGN ANIMBP: component(action='set_character_anim_class', blueprint_path='<path>', anim_class_path='<ABP path>') - NOT edit_component_property on Mesh.AnimClass. The AnimBP MUST target the SAME skeleton as the mesh from step 1: a mismatch (e.g. the UE5 ABP_Manny on the UE4 SK_Mannequin) leaves the rig T-posing at runtime even though it compiles - the tool now rejects an incompatible pairing. Pick the MAIN locomotion AnimBP, never a _PostProcess one. Find via asset_management(action='find_asset_by_name', name_pattern='ABP_Manny', asset_type='AnimBlueprint')."));
			AddStep(TEXT("3. ADD SPRING ARM: blueprint(action='add_component', blueprint_path='<path>', component_class='SpringArmComponent', component_name='CameraBoom', attach_to='RootComponent'). Then edit_component_property to set TargetArmLength=400, SocketOffset=(0,0,50), bUsePawnControlRotation=true (CRITICAL for third-person camera look)."));
			AddStep(TEXT("4. ADD CAMERA: blueprint(action='add_component', blueprint_path='<path>', component_class='CameraComponent', component_name='FollowCamera', attach_to='CameraBoom') — MUST use attach_to='CameraBoom' so the camera parents to the spring arm in SCS. Do NOT attach via AttachToComponent in BeginPlay. Set bUsePawnControlRotation=false on the camera."));
			AddStep(TEXT("5. CLASS DEFAULTS vs MOVEMENT (two DIFFERENT tools): the bUseControllerRotation* flags ARE class defaults - blueprint(action='set_class_default', property_name='bUseControllerRotationPitch'/'bUseControllerRotationYaw'/'bUseControllerRotationRoll', property_value=false). But bOrientRotationToMovement / RotationRate / JumpZVelocity / AirControl / MaxWalkSpeed live on the CharacterMovement COMPONENT - set those with component(action='edit_component_property', component_name='CharMoveComp', property_name='...', value=...), NOT set_class_default (they are not class defaults and set_class_default will error 'property not found'). Typical: bOrientRotationToMovement=true, RotationRate='0,540,0', JumpZVelocity=600, AirControl=0.35, MaxWalkSpeed=500. Without these the character spins with the camera instead of orienting to movement direction."));
			AddStep(TEXT("6. INPUT SETUP: input_system(action='setup_enhanced_input', save_path='/Game/Input', imc_name='IMC_PlayerControls', input_actions=[{name:'IA_Move',value_type:'Axis2D'},{name:'IA_Look',value_type:'Axis2D'},{name:'IA_Jump',value_type:'Digital'}], mappings=[...]). Note the asset paths it returns - use them in step 7. Then build the IMC registration chain on ev.ReceiveControllerChanged (NOT BeginPlay): ev.ReceiveControllerChanged.then -> k2.Cast To PlayerController (Object = NewController) -> Cast.then -> fn.EnhancedInputLocalPlayerSubsystem.AddMappingContext.execute; data: Cast.'As Player Controller' -> k2.Get EnhancedInputLocalPlayerSubsystem.'Player Controller', that node's ReturnValue -> AddMappingContext.self, MappingContext = IMC ref, Priority = 0."));
			AddStep(TEXT("7. INPUT HANDLING: prefer the FULL IA paths returned by setup_enhanced_input for the event nodes (e.g. ia./Game/Input/IA_Move) rather than the bare ia.IA_Move - a project commonly has several IA assets sharing a name (the stock template ships IA_Move/IA_Look/IA_Jump) and the bare short name can bind the WRONG asset, which silently breaks input at runtime. Wire <IA_Move event>.Triggered -> BreakVector2D -> scale by GetForwardVector/GetRightVector -> AddMovementInput. CRITICAL: the GetForwardVector/GetRightVector InRot must be a YAW-ONLY rotation - run GetControlRotation through BreakRotator, take ONLY the Yaw, and rebuild it with MakeRotator(Roll=0, Pitch=0, Yaw=<yaw>). Feeding the FULL GetControlRotation (pitch included) makes the forward vector tilt down when the camera looks down, so AddMovementInput drives into the floor and the character STALLS/stops moving when looking down (it compiles clean - silent bug). <IA_Look>.Triggered -> BreakVector2D -> AddControllerYawInput (X) and AddControllerPitchInput (Y). For conventional (non-inverted) pitch, put a Negate modifier on the IA_Look mapping's Y axis in the IMC (or negate the value into AddControllerPitchInput) - raw mouse Y reads inverted. <IA_Jump>.Triggered -> Jump, .Completed -> StopJumping."));
			AddStep(TEXT("8. VERIFY: after compile, call blueprint(action='get_blueprint_skeleton', blueprint_path='<path>') and confirm Components shows Mesh(with SkeletalMesh set)+CameraBoom(SpringArm)+FollowCamera(child of CameraBoom). Then blueprint(action='get_blueprint_graph', detail='outline') and confirm each EnhancedInputAction node's 'input_action' field points at the IA assets you created (not a same-named asset elsewhere). If components are missing or an IA path is wrong, the character will NOT play correctly."));

			Res->SetStringField(TEXT("character_setup_warning"), TEXT("You created a Character/Pawn BP. A Character BP is NOT complete after create_blueprint — you MUST follow the character_setup_checklist below to produce a working character. Skipping ANY step results in an invisible, T-posing, or camera-broken character on Play."));
			Res->SetArrayField(TEXT("character_setup_checklist"), Checklist);
		}

		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> W = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&OutJsonString);
		FJsonSerializer::Serialize(Res.ToSharedRef(), W);
	}
}

void HandleFindAssetByName(const FString& NamePattern, const FString& AssetType, const FString& ParentClass, FString& OutJsonString, FString& OutError)
{
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	TArray<FAssetData> FoundAssets;
	FARFilter Filter;
	Filter.bRecursivePaths = true;
	Filter.PackagePaths.Add(FName(TEXT("/Game")));

	const bool bHasParentClass = !ParentClass.IsEmpty();

	if (bHasParentClass)
	{
		Filter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());
	}
	else if (!AssetType.IsEmpty())
	{
		if (AssetType.Equals(TEXT("Blueprint"), ESearchCase::IgnoreCase))
		{
			Filter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());
		}
		else if (AssetType.Equals(TEXT("Material"), ESearchCase::IgnoreCase))
		{
			Filter.ClassPaths.Add(UMaterial::StaticClass()->GetClassPathName());
		}
		else if (AssetType.Equals(TEXT("Texture"), ESearchCase::IgnoreCase))
		{
			Filter.ClassPaths.Add(UTexture::StaticClass()->GetClassPathName());
		}
		else if (AssetType.Equals(TEXT("DataTable"), ESearchCase::IgnoreCase))
		{
			Filter.ClassPaths.Add(UDataTable::StaticClass()->GetClassPathName());
		}
		else
		{
			FString T = AssetType;
			T.RemoveSpacesInline();
			TArray<FString> Candidates;
			Candidates.Add(T);
			if (!T.StartsWith(TEXT("U"))) Candidates.Add(TEXT("U") + T);
			if (T.StartsWith(TEXT("U")) && T.Len() > 1) Candidates.Add(T.RightChop(1));
			for (const FString& C : Candidates)
			{
				if (UClass* FoundClass = FindObject<UClass>(nullptr, *(TEXT("/Script/Engine.") + C)))
				{
					Filter.ClassPaths.Add(FoundClass->GetClassPathName()); break;
				}
				if (UClass* FoundClass2 = FindFirstObject<UClass>(*C, EFindFirstObjectOptions::NativeFirst))
				{
					Filter.ClassPaths.Add(FoundClass2->GetClassPathName()); break;
				}
			}
		}
	}
	Filter.bRecursiveClasses = true;

	AssetRegistry.GetAssets(Filter, FoundAssets);

	UClass* ParentClassObj = nullptr;
	if (bHasParentClass)
	{
		FString PC = ParentClass;
		PC.RemoveSpacesInline();
		TArray<FString> Candidates;
		Candidates.Add(PC);
		if (!PC.StartsWith(TEXT("A")) && !PC.StartsWith(TEXT("U"))) { Candidates.Add(TEXT("A") + PC); Candidates.Add(TEXT("U") + PC); }
		for (const FString& C : Candidates)
		{
			if (UClass* F = FindObject<UClass>(nullptr, *(TEXT("/Script/Engine.") + C))) { ParentClassObj = F; break; }
			if (UClass* F = FindFirstObject<UClass>(*C, EFindFirstObjectOptions::NativeFirst)) { ParentClassObj = F; break; }
		}
		if (!ParentClassObj)
		{
			OutError = FString::Printf(
				TEXT("parent_class '%s' did not resolve to any UClass. Use the C++ class name (Pawn, Character, ActorComponent, UserWidget, etc.) without the A/U prefix."),
				*ParentClass);
			return;
		}
	}

	TArray<FAssetData> MatchingAssets;
	const bool bPatternIsGlob = NamePattern.Contains(TEXT("*")) || NamePattern.Contains(TEXT("?"));
	for (const FAssetData& Asset : FoundAssets)
    {
        if (!NamePattern.IsEmpty())
        {
            const FString AssetNameStr = Asset.AssetName.ToString();
            const bool bMatch = bPatternIsGlob
                ? AssetNameStr.MatchesWildcard(NamePattern, ESearchCase::IgnoreCase)
                : AssetNameStr.Contains(NamePattern, ESearchCase::IgnoreCase);
            if (!bMatch) continue;
        }
        if (ParentClassObj)
        {
            const FString ParentClassPathStr = Asset.GetTagValueRef<FString>(FBlueprintTags::NativeParentClassPath);
            if (ParentClassPathStr.IsEmpty()) continue;
            FString ParentPath = ParentClassPathStr;
            ParentPath.ReplaceInline(TEXT("Class'"), TEXT(""));
            ParentPath.ReplaceInline(TEXT("'"), TEXT(""));
            UClass* AssetParent = FindObject<UClass>(nullptr, *ParentPath);
            if (!AssetParent) AssetParent = LoadObject<UClass>(nullptr, *ParentPath);
            if (!AssetParent || !AssetParent->IsChildOf(ParentClassObj)) continue;
        }
        MatchingAssets.Add(Asset);
        if (MatchingAssets.Num() >= 20) break;
    }

	TSharedPtr<FJsonObject> ResultObject = MakeShareable(new FJsonObject);
	TArray<TSharedPtr<FJsonValue>> AssetsArray;
	for (const FAssetData& Asset : MatchingAssets)
    {
        TSharedPtr<FJsonObject> AssetObject = MakeShareable(new FJsonObject);
        AssetObject->SetStringField(TEXT("name"), Asset.AssetName.ToString());
        AssetObject->SetStringField(TEXT("path"), Asset.GetObjectPathString());
        AssetObject->SetStringField(TEXT("class"), Asset.AssetClassPath.GetAssetName().ToString());
        AssetsArray.Add(MakeShareable(new FJsonValueObject(AssetObject)));
    }
	ResultObject->SetArrayField(TEXT("assets"), AssetsArray);
	ResultObject->SetNumberField(TEXT("count"), MatchingAssets.Num());
	ResultObject->SetBoolField(TEXT("success"), true);

	TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&OutJsonString);
	FJsonSerializer::Serialize(ResultObject.ToSharedRef(), Writer);
}

void HandleMoveAsset(const FString& AssetPath, const FString& DestinationFolder, FString& OutJsonString, FString& OutError)
{
	if (!UEditorAssetLibrary::DoesAssetExist(AssetPath))
    {
		OutError = FString::Printf(TEXT("Asset not found: %s"), *AssetPath);
		return;
    }

	if (!UEditorAssetLibrary::DoesDirectoryExist(DestinationFolder))
    {
		UEditorAssetLibrary::MakeDirectory(DestinationFolder);
		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		AssetRegistryModule.Get().ScanPathsSynchronous({ DestinationFolder }, true);
    }

	FString AssetName = FPackageName::GetLongPackageAssetName(AssetPath);
	FString NewAssetPath = DestinationFolder / AssetName;

	if (UEditorAssetLibrary::DoesAssetExist(NewAssetPath))
	{
		OutError = FString::Printf(
			TEXT("An asset with the same name already exists at the destination '%s'. move_asset refuses to overwrite — delete or rename the existing asset first."),
			*NewAssetPath);
		return;
	}

	bool bMoved = UEditorAssetLibrary::RenameAsset(AssetPath, NewAssetPath);
	if (bMoved)
    {
		FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
		ContentBrowserModule.Get().SyncBrowserToAssets(TArray<FAssetData>(), true);

		TSharedPtr<FJsonObject> ResultObject = MakeShareable(new FJsonObject);
		ResultObject->SetBoolField(TEXT("success"), true);
		ResultObject->SetStringField(TEXT("old_path"), AssetPath);
		ResultObject->SetStringField(TEXT("new_path"), NewAssetPath);
		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&OutJsonString);
		FJsonSerializer::Serialize(ResultObject.ToSharedRef(), Writer);
    }
	else
    {
		OutError = FString::Printf(TEXT("Failed to move asset '%s' to to '%s'"), *AssetPath, *DestinationFolder);
    }
}

void HandleMoveAssets(const TArray<FString>& AssetPaths, const FString& DestinationFolder, FString& OutJsonString, FString& OutError)
{
	if (!UEditorAssetLibrary::DoesDirectoryExist(DestinationFolder))
    {
		UEditorAssetLibrary::MakeDirectory(DestinationFolder);
		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		AssetRegistryModule.Get().ScanPathsSynchronous({ DestinationFolder }, true);
    }

	int32 MovedCount = 0;
	TArray<TSharedPtr<FJsonValue>> ResultsArray;

	for (const FString& AssetPath : AssetPaths)
    {
		TSharedPtr<FJsonObject> MoveResult = MakeShareable(new FJsonObject);
        MoveResult->SetStringField(TEXT("original_path"), AssetPath);

        if (!UEditorAssetLibrary::DoesAssetExist(AssetPath))
        {
            MoveResult->SetBoolField(TEXT("success"), false);
            MoveResult->SetStringField(TEXT("error"), TEXT("Asset not found"));
        }
        else
        {
            FString AssetName = FPackageName::GetLongPackageAssetName(AssetPath);
            FString NewAssetPath = DestinationFolder / AssetName;
            if (UEditorAssetLibrary::DoesAssetExist(NewAssetPath))
            {
                MoveResult->SetBoolField(TEXT("success"), false);
                MoveResult->SetStringField(TEXT("error"),
                    FString::Printf(TEXT("Destination '%s' already has an asset with this name — skipped (no overwrite)."),
                    *NewAssetPath));
            }
            else
            {
                bool bMoved = UEditorAssetLibrary::RenameAsset(AssetPath, NewAssetPath);
                MoveResult->SetBoolField(TEXT("success"), bMoved);
                if (bMoved)
                {
                    MoveResult->SetStringField(TEXT("new_path"), NewAssetPath);
                    MovedCount++;
                }
                else
                {
                    MoveResult->SetStringField(TEXT("error"), TEXT("Failed to move"));
                }
            }
        }
        ResultsArray.Add(MakeShareable(new FJsonValueObject(MoveResult)));
    }

	FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
    ContentBrowserModule.Get().SyncBrowserToAssets(TArray<FAssetData>(), true);

	TSharedPtr<FJsonObject> ResultObject = MakeShareable(new FJsonObject);
	ResultObject->SetBoolField(TEXT("success"), true);
	ResultObject->SetNumberField(TEXT("moved_count"), MovedCount);
	ResultObject->SetNumberField(TEXT("total_count"), AssetPaths.Num());
	ResultObject->SetArrayField(TEXT("results"), ResultsArray);

	TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&OutJsonString);
	FJsonSerializer::Serialize(ResultObject.ToSharedRef(), Writer);
}

static UBlueprint* CreateBlueprintAsset(const FString& Name, const FString& SavePath,
	EBlueprintType BPType, UClass* ParentClass, FString& OutAssetPath, FString& OutError)
{
	if (!IsValidAssetName(Name, OutError, TEXT("name"))) return nullptr;

	FString NormalizedPath = FSettingsManager::NormalizeSavePath(SavePath);
	FString CleanPath = NormalizedPath.EndsWith(TEXT("/")) ? NormalizedPath : (NormalizedPath + TEXT("/"));
	OutAssetPath = CleanPath + Name;
	UPackage* Package = CreatePackage(*OutAssetPath);
	Package->FullyLoad();

	UBlueprintFactory* Factory = NewObject<UBlueprintFactory>();
	Factory->BlueprintType = BPType;
	Factory->ParentClass = ParentClass;

	UBlueprint* BP = Cast<UBlueprint>(Factory->FactoryCreateNew(
		UBlueprint::StaticClass(), Package, *Name, RF_Public | RF_Standalone, nullptr, GWarn));
	if (!BP) { OutError = FString::Printf(TEXT("Failed to create blueprint '%s'"), *Name); return nullptr; }

	BP->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(BP);
	return BP;
}

void HandleCreateBlueprintInterface(const FString& Name, const FString& SavePath, FString& OutAssetPath, FString& OutError)
{
	if (Name.IsEmpty()) { OutError = TEXT("Missing required parameter: name"); return; }
	UBlueprint* BP = CreateBlueprintAsset(Name, SavePath.IsEmpty() ? TEXT("/Game") : SavePath,
		BPTYPE_Interface, UInterface::StaticClass(), OutAssetPath, OutError);
	if (BP)
	{
		OutAssetPath = BP->GetPathName();
	}
}

void HandleCreateBlueprintFunctionLibrary(const FString& Name, const FString& SavePath, FString& OutAssetPath, FString& OutError)
{
	if (Name.IsEmpty()) { OutError = TEXT("Missing required parameter: name"); return; }

	FString NormalizedPath = FSettingsManager::NormalizeSavePath(SavePath.IsEmpty() ? FSettingsManager::GetDefaultSavePath() : SavePath);
	FString CleanPath = NormalizedPath.EndsWith(TEXT("/")) ? NormalizedPath : (NormalizedPath + TEXT("/"));
	OutAssetPath = CleanPath + Name;
	UPackage* Package = CreatePackage(*OutAssetPath);
	Package->FullyLoad();

	UBlueprintFunctionLibraryFactory* Factory = NewObject<UBlueprintFunctionLibraryFactory>();
	UBlueprint* BP = Cast<UBlueprint>(Factory->FactoryCreateNew(
		UBlueprint::StaticClass(), Package, FName(*Name), RF_Public | RF_Standalone, nullptr, GWarn, NAME_None));
	if (!BP) { OutError = FString::Printf(TEXT("Failed to create function library '%s'"), *Name); return; }

	BP->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(BP);
	OutAssetPath = BP->GetPathName();
}

void HandleCreateMacroLibrary(const FString& Name, const FString& SavePath, FString& OutAssetPath, FString& OutError)
{
	if (Name.IsEmpty()) { OutError = TEXT("Missing required parameter: name"); return; }
	UBlueprint* BP = CreateBlueprintAsset(Name, SavePath.IsEmpty() ? FSettingsManager::GetDefaultSavePath() : SavePath,
		BPTYPE_MacroLibrary, UObject::StaticClass(), OutAssetPath, OutError);
	if (BP) { OutAssetPath = BP->GetPathName(); }
}

void HandleGetLevelBlueprint(FString& OutJsonString, FString& OutError)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World) { OutError = TEXT("No editor world available"); return; }

	ULevelScriptBlueprint* LevelBP = Cast<ULevelScriptBlueprint>(
		World->PersistentLevel->GetLevelScriptBlueprint(false));
	if (!LevelBP)
	{
		LevelBP = Cast<ULevelScriptBlueprint>(World->PersistentLevel->GetLevelScriptBlueprint(true));
	}

	if (!LevelBP) { OutError = TEXT("Could not get or create Level Blueprint"); return; }

	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"blueprint_path\":\"@level_blueprint\",\"level\":\"%s\",\"note\":\"Use blueprint_path=\\\"@level_blueprint\\\" in all blueprint tools (discover_nodes, build_blueprint_graph, compile_blueprint, etc.)\"}"),
		*World->GetName());
}

void HandleDuplicateAsset(const FString& SourcePath, const FString& DestPath, FString& OutJsonString, FString& OutError)
{
	if (!UEditorAssetLibrary::DoesAssetExist(SourcePath))
	{
		OutError = FString::Printf(TEXT("Source asset not found: %s"), *SourcePath);
		return;
	}

	const FString DestName = FPackageName::GetLongPackageAssetName(DestPath);
	if (!IsValidAssetName(DestName, OutError, TEXT("dest_path leaf"))) return;

	if (UEditorAssetLibrary::DoesAssetExist(DestPath))
	{
		OutError = FString::Printf(
			TEXT("Destination asset already exists at '%s'. duplicate_asset refuses to overwrite — pick a different dest_path, or delete the existing asset first via asset_management(action='delete_asset', asset_path='%s')."),
			*DestPath, *DestPath);
		return;
	}

	UObject* Duplicate = UEditorAssetLibrary::DuplicateAsset(SourcePath, DestPath);
	if (!Duplicate)
	{
		OutError = FString::Printf(TEXT("Failed to duplicate '%s' to '%s'"), *SourcePath, *DestPath);
		return;
	}

	FAssetRegistryModule::AssetCreated(Duplicate);
	FContentBrowserModule& CB = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
	CB.Get().SyncBrowserToAssets({ FAssetData(Duplicate) });

	TSharedPtr<FJsonObject> Res = MakeShareable(new FJsonObject);
	Res->SetBoolField(TEXT("success"), true);
	Res->SetStringField(TEXT("source_path"), SourcePath);
	Res->SetStringField(TEXT("new_path"), DestPath);
	TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> W = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Res.ToSharedRef(), W);
}

void HandleRenameAsset(const FString& AssetPath, const FString& NewName, FString& OutJsonString, FString& OutError)
{
	if (!UEditorAssetLibrary::DoesAssetExist(AssetPath))
	{
		OutError = FString::Printf(TEXT("Asset not found: %s"), *AssetPath);
		return;
	}

	if (!IsValidAssetName(NewName, OutError, TEXT("new_name"))) return;

	FString Folder = FPackageName::GetLongPackagePath(AssetPath);
	FString NewPath = Folder / NewName;

	if (UEditorAssetLibrary::DoesAssetExist(NewPath))
	{
		OutError = FString::Printf(
			TEXT("An asset already exists at the rename target '%s'. rename_asset refuses to overwrite — pick a different new_name, or delete the existing asset first via asset_management(action='delete_asset', asset_path='%s')."),
			*NewPath, *NewPath);
		return;
	}

	bool bSuccess = UEditorAssetLibrary::RenameAsset(AssetPath, NewPath);
	if (!bSuccess)
	{
		OutError = FString::Printf(TEXT("Failed to rename '%s' to '%s'"), *AssetPath, *NewName);
		return;
	}

	FContentBrowserModule& CB = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
	CB.Get().SyncBrowserToAssets(TArray<FAssetData>(), true);

	TSharedPtr<FJsonObject> Res = MakeShareable(new FJsonObject);
	Res->SetBoolField(TEXT("success"), true);
	Res->SetStringField(TEXT("old_path"), AssetPath);
	Res->SetStringField(TEXT("new_path"), NewPath);
	TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> W = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Res.ToSharedRef(), W);
}

void HandleCompileBlueprint(const FString& BpPath, FString& OutJsonString, FString& OutError)
{
	UBlueprint* BP = nullptr;
	if (BpPath.Equals(TEXT("@level_blueprint"), ESearchCase::IgnoreCase))
	{
		UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
		if (World && World->PersistentLevel)
			BP = World->PersistentLevel->GetLevelScriptBlueprint(false);
	}
	else
	{
		BP = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(BpPath));
	}
	if (!BP)
	{
		OutError = FString::Printf(TEXT("Could not load Blueprint at: %s"), *BpPath);
		return;
	}

	TArray<TSharedPtr<FJsonValue>> PrecompilePrunedNodes;
	{
		TSet<UEdGraph*> VisitedGraphs;
		TArray<UEdGraph*> GraphStack;
		auto PushGraph = [&](UEdGraph* G)
		{
			if (G && !VisitedGraphs.Contains(G)) { VisitedGraphs.Add(G); GraphStack.Push(G); }
		};
		for (UEdGraph* G : BP->UbergraphPages)   PushGraph(G);
		for (UEdGraph* G : BP->FunctionGraphs)   PushGraph(G);
		for (UEdGraph* G : BP->MacroGraphs)      PushGraph(G);
		for (UEdGraph* G : BP->IntermediateGeneratedGraphs) PushGraph(G);
		for (UEdGraph* G : BP->DelegateSignatureGraphs) PushGraph(G);

		int32 PurgedNodes = 0;
		TArray<FString> PurgeLog;
		while (GraphStack.Num() > 0)
		{
			UEdGraph* Graph = GraphStack.Pop();
			if (!Graph) continue;

			for (UEdGraph* Sub : Graph->SubGraphs) PushGraph(Sub);

			const UEdGraphSchema* SchemaCDO = Graph->GetSchema();
			const bool bIsK2Schema = SchemaCDO && SchemaCDO->IsA<UEdGraphSchema_K2>();
			if (bIsK2Schema) continue;

			TArray<UEdGraphNode*> BadNodes;
			for (UEdGraphNode* Node : Graph->Nodes)
			{
				if (!IsValid(Node)) continue;
				if (Node->IsA<UK2Node>()) BadNodes.Add(Node);
			}
			for (UEdGraphNode* Bad : BadNodes)
			{
				const FString Title = Bad->GetNodeTitle(ENodeTitleType::ListView).ToString();
				const FString SchemaClass = SchemaCDO ? SchemaCDO->GetClass()->GetName() : TEXT("null");
				PurgeLog.Add(FString::Printf(TEXT("graph='%s' schema='%s' node='%s'"),
					*Graph->GetName(), *SchemaClass, *Title));

				TSharedPtr<FJsonObject> PrunedEntry = MakeShared<FJsonObject>();
				PrunedEntry->SetStringField(TEXT("graph"), Graph->GetName());
				PrunedEntry->SetStringField(TEXT("graph_schema"), SchemaClass);
				PrunedEntry->SetStringField(TEXT("node_class"), Bad->GetClass()->GetName());
				PrunedEntry->SetStringField(TEXT("node_title"), Title);
				PrecompilePrunedNodes.Add(MakeShareable(new FJsonValueObject(PrunedEntry)));

				for (UEdGraphPin* Pin : Bad->Pins)
				{
					if (Pin) Pin->BreakAllPinLinks();
				}
				Graph->RemoveNode(Bad);
				PurgedNodes++;
			}
		}
		if (PurgedNodes > 0)
		{
			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
			UE_LOG(LogTemp, Warning, TEXT("[compile_blueprint] Pre-compile sweep pruned %d K2 node(s) from non-K2 graphs to prevent engine crash:"), PurgedNodes);
			for (const FString& Entry : PurgeLog)
			{
				UE_LOG(LogTemp, Warning, TEXT("  %s"), *Entry);
			}
		}
	}

	FCompilerResultsLog Results;
	Results.bSilentMode = true;
	FKismetEditorUtilities::CompileBlueprint(BP, EBlueprintCompileOptions::None, &Results);

	TArray<TSharedPtr<FJsonValue>> ErrorMessages;
	TArray<TSharedPtr<FJsonValue>> WarningMessages;
	// Axivor: attach the offending node (title, guid, GEID) to each compiler message so the
	// model can patch the exact node instead of guessing from the prose.
	auto NodeRefSuffix = [](const TSharedRef<FTokenizedMessage>& InMsg) -> FString
	{
		FString Suffix;
		for (const TSharedRef<IMessageToken>& Token : InMsg->GetMessageTokens())
		{
			if (Token->GetType() != EMessageToken::Object) continue;
			const TSharedRef<FUObjectToken> ObjToken = StaticCastSharedRef<FUObjectToken>(Token);
			const UObject* Obj = ObjToken->GetObject().Get();
			const UEdGraphNode* Node = Cast<UEdGraphNode>(Obj);
			if (!Node) continue;
			const FString Geid = ReadNodeLogicalId(Node);
			Suffix += FString::Printf(TEXT(" [node: \"%s\" guid=%s%s graph=%s]"),
				*Node->GetNodeTitle(ENodeTitleType::ListView).ToString(),
				*Node->NodeGuid.ToString(EGuidFormats::Digits),
				Geid.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" id=%s"), *Geid),
				Node->GetGraph() ? *Node->GetGraph()->GetName() : TEXT("?"));
		}
		return Suffix;
	};
	for (const TSharedRef<FTokenizedMessage>& Msg : Results.Messages)
	{
		const EMessageSeverity::Type Sev = Msg->GetSeverity();
		if (Sev == EMessageSeverity::Error)
		{
			FString MsgText = Msg->ToText().ToString() + NodeRefSuffix(Msg);
			if (MsgText.Contains(TEXT("type is undetermined")) || MsgText.Contains(TEXT("Type is undetermined")))
			{
				MsgText += TEXT(" [HINT: This means a pin's type cannot be inferred because no data connection drives it. Check unconnected_nodes for free_data_in pins. If the graph is empty (no nodes connected to the entry/event), compile errors like this are expected — build_blueprint_graph first.]");
			}
			ErrorMessages.Add(MakeShareable(new FJsonValueString(MsgText)));
		}
		else if (Sev == EMessageSeverity::Warning || Sev == EMessageSeverity::PerformanceWarning)
		{
			WarningMessages.Add(MakeShareable(new FJsonValueString(Msg->ToText().ToString() + NodeRefSuffix(Msg))));
		}
	}

	const bool bSuccess = Results.NumErrors == 0;

	if (bSuccess)
	{
		BP->MarkPackageDirty();
	}

	TSharedPtr<FJsonObject> Res = MakeShareable(new FJsonObject);
	Res->SetBoolField(TEXT("success"), bSuccess);
	Res->SetStringField(TEXT("blueprint_path"), BpPath);
	Res->SetNumberField(TEXT("error_count"), Results.NumErrors);
	Res->SetNumberField(TEXT("warning_count"), Results.NumWarnings);
	Res->SetArrayField(TEXT("errors"), ErrorMessages);
	Res->SetArrayField(TEXT("warnings"), WarningMessages);

	if (PrecompilePrunedNodes.Num() > 0)
	{
		Res->SetArrayField(TEXT("precompile_pruned_nodes"), PrecompilePrunedNodes);
		Res->SetStringField(TEXT("precompile_pruned_note"), TEXT("The pre-compile sanity sweep removed K2 nodes that were placed in non-K2 graphs (would have crashed the editor). Check which build_blueprint_graph call targeted the wrong graph_name. The nodes were removed automatically so compilation could proceed."));
	}

	if (bSuccess)
	{
		Res->SetStringField(TEXT("message"), FString::Printf(TEXT("Blueprint '%s' compiled successfully.%s"),
			*BP->GetName(),
			Results.NumWarnings > 0 ? *FString::Printf(TEXT(" (%d warning(s))"), Results.NumWarnings) : TEXT("")));

		TArray<TSharedPtr<FJsonValue>> HealthIssues;
		TSet<FString> SeenIssueKeys;

		TMap<FString, TArray<FString>> DuplicateGeidsByGraph;
		TMap<FString, int32> OrphanCountsByGraph;
		const int32 OrphanThreshold = 2;

		TArray<TSharedPtr<FJsonValue>> AutoMergedDuplicates;

		for (UEdGraph* Graph : BP->UbergraphPages)
		{
			if (!Graph) continue;

			TMap<FString, TArray<UEdGraphNode*>> NodesByGeid;
			for (UEdGraphNode* Node : Graph->Nodes)
			{
				if (!IsValid(Node)) continue;
				const FString GEID = ReadNodeLogicalId(Node);
				if (!GEID.IsEmpty()) NodesByGeid.FindOrAdd(GEID).Add(Node);
			}

			bool bGraphMutated = false;
			for (auto It = NodesByGeid.CreateIterator(); It; ++It)
			{
				TArray<UEdGraphNode*>& Group = It.Value();
				if (Group.Num() < 2) continue;

				const UClass*  PivotClass = Group[0]->GetClass();
				const FString  PivotTitle = Group[0]->GetNodeTitle(ENodeTitleType::ListView).ToString();
				bool bMergeable = true;
				for (int32 i = 1; i < Group.Num(); ++i)
				{
					if (Group[i]->GetClass() != PivotClass ||
						!Group[i]->GetNodeTitle(ENodeTitleType::ListView).ToString().Equals(PivotTitle, ESearchCase::IgnoreCase))
					{ bMergeable = false; break; }
				}
				if (!bMergeable) continue;

				int32 SurvivorIdx = 0;
				int32 SurvivorLinks = 0;
				for (int32 i = 0; i < Group.Num(); ++i)
				{
					int32 Links = 0;
					for (UEdGraphPin* P : Group[i]->Pins) { if (P) Links += P->LinkedTo.Num(); }
					if (Links > SurvivorLinks) { SurvivorLinks = Links; SurvivorIdx = i; }
				}
				UEdGraphNode* Survivor = Group[SurvivorIdx];

				int32 LinksTransferred = 0;
				for (int32 i = 0; i < Group.Num(); ++i)
				{
					if (i == SurvivorIdx) continue;
					UEdGraphNode* Duplicate = Group[i];
					for (UEdGraphPin* DupPin : Duplicate->Pins)
					{
						if (!DupPin || DupPin->LinkedTo.Num() == 0) continue;
						UEdGraphPin* SurvivorPin = nullptr;
						for (UEdGraphPin* SP : Survivor->Pins)
						{
							if (SP && SP->Direction == DupPin->Direction &&
								SP->PinName == DupPin->PinName)
							{ SurvivorPin = SP; break; }
						}
						if (!SurvivorPin) continue;
						TArray<UEdGraphPin*> Targets = DupPin->LinkedTo;
						for (UEdGraphPin* T : Targets)
						{
							if (!T) continue;
							SurvivorPin->MakeLinkTo(T);
							LinksTransferred++;
						}
					}
					for (UEdGraphPin* DupPin : Duplicate->Pins)
						if (DupPin) DupPin->BreakAllPinLinks();
					Graph->RemoveNode(Duplicate);
					bGraphMutated = true;
				}

				TSharedPtr<FJsonObject> MergeEntry = MakeShared<FJsonObject>();
				MergeEntry->SetStringField(TEXT("graph"), Graph->GetName());
				MergeEntry->SetStringField(TEXT("geid"), It.Key());
				MergeEntry->SetStringField(TEXT("title"), PivotTitle);
				MergeEntry->SetNumberField(TEXT("duplicates_removed"), Group.Num() - 1);
				MergeEntry->SetNumberField(TEXT("links_transferred"), LinksTransferred);
				AutoMergedDuplicates.Add(MakeShareable(new FJsonValueObject(MergeEntry)));

				Group.SetNum(1);
				Group[0] = Survivor;
			}

			if (bGraphMutated)
			{
				FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
			}

			for (const auto& Pair : NodesByGeid)
			{
				if (Pair.Value.Num() > 1)
				{
					DuplicateGeidsByGraph.FindOrAdd(Graph->GetName()).Add(Pair.Key);
				}
			}
		}

		if (AutoMergedDuplicates.Num() > 0)
		{
			Res->SetArrayField(TEXT("auto_merged_duplicates"), AutoMergedDuplicates);
		}
		auto IsExecEntryPoint = [](UEdGraphNode* N) -> bool
		{
			if (!N) return false;
			if (N->IsA(UK2Node_Event::StaticClass())) return true;
			if (N->IsA(UK2Node_CustomEvent::StaticClass())) return true;
			if (N->IsA(UK2Node_Timeline::StaticClass())) return true;
			const FString CN = N->GetClass()->GetName();
			if (CN.Contains(TEXT("FunctionEntry"))) return true;
			bool bHasExecIn = false, bHasExecOut = false;
			for (UEdGraphPin* Pin : N->Pins)
			{
				if (!Pin || Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec) continue;
				if (Pin->Direction == EGPD_Input)  bHasExecIn  = true;
				else                                bHasExecOut = true;
			}
			return !bHasExecIn && bHasExecOut;
		};

		for (UEdGraph* Graph : BP->UbergraphPages)
		{
			if (!Graph) continue;

			TSet<UEdGraphNode*> Reachable;
			TQueue<UEdGraphNode*> BfsQ;
			for (UEdGraphNode* N : Graph->Nodes)
			{
				if (!IsValid(N)) continue;
				if (IsExecEntryPoint(N))
				{
					BfsQ.Enqueue(N);
					Reachable.Add(N);
				}
			}
			while (!BfsQ.IsEmpty())
			{
				UEdGraphNode* Cur;
				BfsQ.Dequeue(Cur);
				for (UEdGraphPin* P : Cur->Pins)
				{
					if (!P || P->Direction != EGPD_Output || P->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec) continue;
					for (UEdGraphPin* L : P->LinkedTo)
					{
						UEdGraphNode* O = L ? L->GetOwningNodeUnchecked() : nullptr;
						if (O && !Reachable.Contains(O)) { Reachable.Add(O); BfsQ.Enqueue(O); }
					}
				}
			}
			TQueue<UEdGraphNode*> DQ;
			for (UEdGraphNode* N : Reachable) DQ.Enqueue(N);
			while (!DQ.IsEmpty())
			{
				UEdGraphNode* Cur;
				DQ.Dequeue(Cur);
				for (UEdGraphPin* P : Cur->Pins)
				{
					if (!P || P->Direction != EGPD_Input || P->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec) continue;
					for (UEdGraphPin* L : P->LinkedTo)
					{
						UEdGraphNode* S = L ? L->GetOwningNodeUnchecked() : nullptr;
						if (S && !Reachable.Contains(S)) { Reachable.Add(S); DQ.Enqueue(S); }
					}
				}
			}

			for (UEdGraphNode* Node : Graph->Nodes)
			{
				if (!IsValid(Node)) continue;
				const FString NodeId = ReadNodeLogicalId(Node);
				if (NodeId.IsEmpty()) continue;
				FString NodeTitle = Node->GetNodeTitle(ENodeTitleType::ListView).ToString();

				FString GeidClassName = Node->GetClass()->GetName();
				bool bIsEntryPoint = IsExecEntryPoint(Node);
				if (!Reachable.Contains(Node) && !bIsEntryPoint)
				{
					FString Key = Graph->GetName() + TEXT(":") + NodeId + TEXT(":orphaned");
					if (!SeenIssueKeys.Contains(Key))
					{
						SeenIssueKeys.Add(Key);
						OrphanCountsByGraph.FindOrAdd(Graph->GetName())++;
						TSharedPtr<FJsonObject> Issue = MakeShared<FJsonObject>();
						Issue->SetStringField(TEXT("node_id"), NodeId);
						Issue->SetStringField(TEXT("node"), NodeTitle);
						Issue->SetStringField(TEXT("graph"), Graph->GetName());
						Issue->SetStringField(TEXT("blueprint_path"), BP->GetPathName());
						Issue->SetStringField(TEXT("issue"), TEXT("orphaned — not connected to any event chain"));
						Issue->SetStringField(TEXT("fix"), TEXT("connect_pins to wire it into the exec flow, or remove_nodes if unneeded"));
						HealthIssues.Add(MakeShareable(new FJsonValueObject(Issue)));
					}
				}

				if (!bIsEntryPoint &&
					!GeidClassName.Contains(TEXT("FunctionEntry")))
				{
					bool bHasExecIn = false, bHasExecInPin = false;
					for (UEdGraphPin* P : Node->Pins)
					{
						if (P && P->Direction == EGPD_Input && P->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
						{
							bHasExecInPin = true;
							if (P->LinkedTo.Num() > 0) bHasExecIn = true;
						}
					}
					if (bHasExecInPin && !bHasExecIn && Reachable.Contains(Node))
					{
						FString Key = Graph->GetName() + TEXT(":") + NodeId + TEXT(":exec_no_input");
						if (!SeenIssueKeys.Contains(Key))
						{
							SeenIssueKeys.Add(Key);
							TSharedPtr<FJsonObject> Issue = MakeShared<FJsonObject>();
							Issue->SetStringField(TEXT("node_id"), NodeId);
							Issue->SetStringField(TEXT("node"), NodeTitle);
							Issue->SetStringField(TEXT("graph"), Graph->GetName());
							Issue->SetStringField(TEXT("blueprint_path"), BP->GetPathName());
							Issue->SetStringField(TEXT("issue"), TEXT("has exec pin but no exec input connected — will never execute"));
							Issue->SetStringField(TEXT("fix"), TEXT("connect_pins to wire execute pin from the preceding node"));
							HealthIssues.Add(MakeShareable(new FJsonValueObject(Issue)));
						}
					}
				}

				UK2Node_CustomEvent* CE = Cast<UK2Node_CustomEvent>(Node);
				if (CE)
				{
					FString EvName = CE->CustomFunctionName.ToString();
					bool bNeedsRep = EvName.StartsWith(TEXT("Server_")) || EvName.StartsWith(TEXT("Multicast_")) || EvName.StartsWith(TEXT("Client_"));
					bool bHasRep = (CE->FunctionFlags & FUNC_Net) != 0;
					if (bNeedsRep && !bHasRep)
					{
						FString Key = NodeId + TEXT(":rpc_no_rep");
						if (!SeenIssueKeys.Contains(Key))
						{
							SeenIssueKeys.Add(Key);
							TSharedPtr<FJsonObject> Issue = MakeShared<FJsonObject>();
							Issue->SetStringField(TEXT("node_id"), NodeId);
							Issue->SetStringField(TEXT("node"), EvName);
							Issue->SetStringField(TEXT("graph"), Graph->GetName());
							Issue->SetStringField(TEXT("blueprint_path"), BP->GetPathName());
							Issue->SetStringField(TEXT("issue"), TEXT("named as RPC but replication not set — will NOT replicate"));
							Issue->SetStringField(TEXT("fix"), FString::Printf(TEXT("call set_function_replication(function_name='%s', replication='%s', reliable=%s)"),
								*EvName,
								EvName.StartsWith(TEXT("Server_")) ? TEXT("Server") : EvName.StartsWith(TEXT("Client_")) ? TEXT("Client") : TEXT("Multicast"),
								EvName.StartsWith(TEXT("Multicast_")) ? TEXT("false") : TEXT("true")));
							HealthIssues.Add(MakeShareable(new FJsonValueObject(Issue)));
						}
					}
				}
			}
		}

		for (UEdGraph* Graph : BP->FunctionGraphs)
		{
			if (!Graph) continue;
			for (UEdGraphNode* Node : Graph->Nodes)
			{
				if (!IsValid(Node)) continue;
				const FString NodeId = ReadNodeLogicalId(Node);
				if (NodeId.IsEmpty()) continue;
				FString NodeTitle = Node->GetNodeTitle(ENodeTitleType::ListView).ToString();

				FString FnCN = Node->GetClass()->GetName();
				if (!IsExecEntryPoint(Node) && !FnCN.Contains(TEXT("FunctionResult")))
				{
					bool bHasExecIn = false, bHasExecInPin = false;
					for (UEdGraphPin* P : Node->Pins)
					{
						if (P && P->Direction == EGPD_Input && P->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
						{
							bHasExecInPin = true;
							if (P->LinkedTo.Num() > 0) bHasExecIn = true;
						}
					}
					if (bHasExecInPin && !bHasExecIn)
					{
						FString Key = Graph->GetName() + TEXT(":") + NodeId + TEXT(":exec_no_input");
						if (!SeenIssueKeys.Contains(Key))
						{
							SeenIssueKeys.Add(Key);
							TSharedPtr<FJsonObject> Issue = MakeShared<FJsonObject>();
							Issue->SetStringField(TEXT("node_id"), NodeId);
							Issue->SetStringField(TEXT("node"), NodeTitle);
							Issue->SetStringField(TEXT("graph"), Graph->GetName());
							Issue->SetStringField(TEXT("blueprint_path"), BP->GetPathName());
							Issue->SetStringField(TEXT("issue"), TEXT("has exec pin but no exec input connected"));
							Issue->SetStringField(TEXT("fix"), TEXT("connect_pins to wire execute pin"));
							HealthIssues.Add(MakeShareable(new FJsonValueObject(Issue)));
						}
					}
				}
			}
		}

		if (BP->GeneratedClass)
		{
			static const FName AIControllerName(TEXT("AIController"));
			UClass* Parent = BP->GeneratedClass;
			bool bIsAIController = false;
			while (Parent)
			{
				if (Parent->GetFName() == AIControllerName) { bIsAIController = true; break; }
				Parent = Parent->GetSuperClass();
			}

			if (bIsAIController)
			{
				bool bHasStateTreeComp = false;
				bool bHasBehaviorTreeComp = false;
				if (AActor* CDO = Cast<AActor>(BP->GeneratedClass->GetDefaultObject()))
				{
					for (UActorComponent* Comp : CDO->GetComponents())
					{
						if (!Comp) continue;
						const FString ClassName = Comp->GetClass()->GetName();
						if (ClassName.Contains(TEXT("StateTreeAIComponent")) || ClassName.Contains(TEXT("StateTreeComponent"))) bHasStateTreeComp = true;
						if (ClassName.Contains(TEXT("BehaviorTreeComponent"))) bHasBehaviorTreeComp = true;
					}
				}
				if (BP->SimpleConstructionScript)
				{
					for (USCS_Node* SCSNode : BP->SimpleConstructionScript->GetAllNodes())
					{
						if (!SCSNode || !SCSNode->ComponentTemplate) continue;
						const FString ClassName = SCSNode->ComponentTemplate->GetClass()->GetName();
						if (ClassName.Contains(TEXT("StateTreeAIComponent")) || ClassName.Contains(TEXT("StateTreeComponent"))) bHasStateTreeComp = true;
						if (ClassName.Contains(TEXT("BehaviorTreeComponent"))) bHasBehaviorTreeComp = true;
					}
				}

				if (bHasStateTreeComp || bHasBehaviorTreeComp)
				{
					bool bFoundStartLogic = false;
					bool bFoundRunBT = false;
					auto ScanGraph = [&](UEdGraph* Graph)
					{
						if (!Graph) return;
						for (UEdGraphNode* Node : Graph->Nodes)
						{
							if (!IsValid(Node)) continue;
							const FString Title = Node->GetNodeTitle(ENodeTitleType::ListView).ToString();
							if (Title.Contains(TEXT("Start Logic")) || Title.Contains(TEXT("StartLogic")))
								bFoundStartLogic = true;
							if (Title.Contains(TEXT("Run Behavior Tree")) || Title.Contains(TEXT("RunBehaviorTree")))
								bFoundRunBT = true;
						}
					};
					for (UEdGraph* Graph : BP->UbergraphPages) ScanGraph(Graph);
					for (UEdGraph* Graph : BP->FunctionGraphs) ScanGraph(Graph);

					if (bHasStateTreeComp && !bFoundStartLogic)
					{
						TSharedPtr<FJsonObject> Issue = MakeShared<FJsonObject>();
						Issue->SetStringField(TEXT("node_id"), TEXT("missing_startlogic"));
						Issue->SetStringField(TEXT("node"), TEXT("<missing StateTreeComponent.StartLogic call>"));
						Issue->SetStringField(TEXT("graph"), TEXT("EventGraph"));
						Issue->SetStringField(TEXT("blueprint_path"), BP->GetPathName());
						Issue->SetStringField(TEXT("issue"), TEXT("AIController has a StateTreeAIComponent but no StartLogic call in any graph — the State Tree will never tick. If you don't actually need the State Tree (e.g. you switched to a Behavior Tree mid-session), DELETE the StateTreeAIComponent via remove_component instead of leaving it dangling."));
						Issue->SetStringField(TEXT("fix"), TEXT("EITHER add ev.ReceivePossess → var.get.<StateTreeComponentName> → fn.StateTreeComponent.StartLogic in the AIController EventGraph, OR remove the StateTreeAIComponent if it is not needed."));
						HealthIssues.Add(MakeShareable(new FJsonValueObject(Issue)));
					}
					if (bHasBehaviorTreeComp && !bFoundRunBT)
					{
						TSharedPtr<FJsonObject> Issue = MakeShared<FJsonObject>();
						Issue->SetStringField(TEXT("node_id"), TEXT("missing_runbt"));
						Issue->SetStringField(TEXT("node"), TEXT("<missing AIController.RunBehaviorTree call>"));
						Issue->SetStringField(TEXT("graph"), TEXT("EventGraph"));
						Issue->SetStringField(TEXT("blueprint_path"), BP->GetPathName());
						Issue->SetStringField(TEXT("issue"), TEXT("AIController has a BehaviorTreeComponent but no RunBehaviorTree call in any graph — the Behavior Tree will never tick."));
						Issue->SetStringField(TEXT("fix"), TEXT("Add ev.ReceivePossess → fn.AIController.RunBehaviorTree (BTAsset=<your BT>) in the AIController EventGraph."));
						HealthIssues.Add(MakeShareable(new FJsonValueObject(Issue)));
					}
				}
			}
		}

		TSet<FString> ProblemGraphs;
		for (const auto& Pair : DuplicateGeidsByGraph) ProblemGraphs.Add(Pair.Key);
		for (const auto& Pair : OrphanCountsByGraph)
		{
			if (Pair.Value >= OrphanThreshold) ProblemGraphs.Add(Pair.Key);
		}

		if (ProblemGraphs.Num() > 0)
		{
			int32 TotalDupes = 0;
			int32 TotalOrphans = 0;
			FString FirstGraphName;
			for (const FString& GraphName : ProblemGraphs)
			{
				if (FirstGraphName.IsEmpty()) FirstGraphName = GraphName;
				if (const TArray<FString>* DupIds = DuplicateGeidsByGraph.Find(GraphName)) TotalDupes += DupIds->Num();
				if (const int32* Cnt = OrphanCountsByGraph.Find(GraphName)) TotalOrphans += *Cnt;
			}

			TSharedPtr<FJsonObject> ActionObj = MakeShared<FJsonObject>();
			ActionObj->SetStringField(TEXT("action"), TEXT("clear_and_rebuild"));
			ActionObj->SetStringField(TEXT("graph"), FirstGraphName);
			ActionObj->SetNumberField(TEXT("duplicate_count"), TotalDupes);
			ActionObj->SetNumberField(TEXT("orphan_count"), TotalOrphans);
			ActionObj->SetStringField(TEXT("call"),
				FString::Printf(TEXT("clear_blueprint_graph(blueprint_path=\"%s\", graph_name=\"%s\") then rebuild."),
					*BP->GetPathName(), *FirstGraphName));
			Res->SetObjectField(TEXT("action_required"), ActionObj);
		}

		if (HealthIssues.Num() > 0)
		{
			Res->SetArrayField(TEXT("health_issues"), HealthIssues);
			FString VerifyModeStr;
			GConfig->GetString(TEXT("BpGeneratorUltimate"), TEXT("VerifyMode"), VerifyModeStr,
				FSettingsManager::GetGlobalConfigPath());
			VerifyModeStr = VerifyModeStr.TrimStartAndEnd().ToLower();
			const bool bHardVerify = ProblemGraphs.Num() > 0
				&& VerifyModeStr != TEXT("off") && VerifyModeStr != TEXT("soft");

			Res->SetStringField(TEXT("health_note"), ProblemGraphs.Num() > 0
				? (bHardVerify
					? TEXT("REJECTED: dup/orphan nodes — see action_required. delete_nodes won't fix; clear + rebuild.")
					: TEXT("Dup/orphan nodes — clear + rebuild before done:true (see action_required)."))
				: TEXT("Non-blocking: orphan nodes from a partial build. delete_nodes with ids from health_issues, or clear + rebuild. OK to proceed."));

			if (bHardVerify)
			{
				TArray<TSharedPtr<FJsonValue>> RequiredFixes;
				const TSharedPtr<FJsonObject>* ActReq = nullptr;
				if (Res->TryGetObjectField(TEXT("action_required"), ActReq) && ActReq && ActReq->IsValid())
				{
					RequiredFixes.Add(MakeShareable(new FJsonValueObject(*ActReq)));
				}
				Res->SetArrayField(TEXT("required_fixes"), RequiredFixes);

				OutError = FString::Printf(
					TEXT("VERIFY FAILED: %d structural issue(s) — see action_required.call. (Bypass: VerifyMode=Soft in plugin config.)"),
					ProblemGraphs.Num());
			}
		}
	}
	else
	{
		Res->SetStringField(TEXT("message"), FString::Printf(TEXT("Blueprint '%s' compiled with %d error(s). See 'errors' array for details."),
			*BP->GetName(), Results.NumErrors));
	}

	TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> W = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Res.ToSharedRef(), W);
}

void HandleDeleteAsset(const TArray<FString>& AssetPaths, FString& OutJsonString, FString& OutError)
{

	TArray<FString> Deleted;
	TArray<TPair<FString, FString>> Failed;

	TArray<UObject*> ObjectsToDelete;
	TArray<FString> ResolvedPaths;
	for (const FString& Path : AssetPaths)
	{
		FString ResolvedPath = Path;
		ResolvedPath.RemoveFromEnd(TEXT("_C"), ESearchCase::CaseSensitive);

		UObject* Asset = UEditorAssetLibrary::LoadAsset(ResolvedPath);
		if (!Asset)
		{
			Failed.Emplace(Path, FString::Printf(TEXT("asset not found at '%s'"), *ResolvedPath));
			continue;
		}
		ObjectsToDelete.Add(Asset);
		ResolvedPaths.Add(ResolvedPath);
	}

	if (ObjectsToDelete.Num() > 0)
	{
		const int32 DeletedCount = ObjectTools::ForceDeleteObjects(ObjectsToDelete,  false);
		for (int32 i = 0; i < ResolvedPaths.Num(); i++)
		{
			if (UEditorAssetLibrary::DoesAssetExist(ResolvedPaths[i]))
			{
				Failed.Emplace(AssetPaths[i],
					TEXT("ForceDeleteObjects returned but asset still exists — likely held by an open editor tab, source control, or a referencer that couldn't be redirected. Close any open editor tabs for the asset and retry."));
			}
			else
			{
				Deleted.Add(AssetPaths[i]);
			}
		}
		(void)DeletedCount;
	}

	TSharedPtr<FJsonObject> Res = MakeShareable(new FJsonObject);
	Res->SetBoolField(TEXT("success"), Failed.IsEmpty());
	TArray<TSharedPtr<FJsonValue>> DelArr;
	for (const FString& S : Deleted) DelArr.Add(MakeShareable(new FJsonValueString(S)));
	Res->SetArrayField(TEXT("deleted"), DelArr);
	if (!Failed.IsEmpty())
	{
		TArray<TSharedPtr<FJsonValue>> FailArr;
		for (const TPair<FString, FString>& P : Failed)
		{
			TSharedPtr<FJsonObject> Entry = MakeShareable(new FJsonObject);
			Entry->SetStringField(TEXT("path"), P.Key);
			Entry->SetStringField(TEXT("reason"), P.Value);
			FailArr.Add(MakeShareable(new FJsonValueObject(Entry)));
		}
		Res->SetArrayField(TEXT("failed"), FailArr);
	}
	TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> W = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Res.ToSharedRef(), W);
}

void HandleSetClassDefault(const FString& BpPath, const FString& PropertyName, const FString& PropertyValue, FString& OutJsonString, FString& OutError)
{
	FString ResolvedBpPath = BpPath;
	if (ResolvedBpPath.EndsWith(TEXT("_C")))
	{
		ResolvedBpPath = ResolvedBpPath.LeftChop(2);
		int32 DotIdx;
		if (ResolvedBpPath.FindLastChar(TEXT('.'), DotIdx))
			ResolvedBpPath = ResolvedBpPath.Left(DotIdx);
	}
	UBlueprint* BP = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(ResolvedBpPath));
	if (!BP) { OutError = FString::Printf(TEXT("Could not load Blueprint: %s"), *BpPath); return; }

	if (!BP->GeneratedClass) { OutError = TEXT("Blueprint has no generated class. Compile it first."); return; }

	UObject* CDO = BP->GeneratedClass->GetDefaultObject();
	if (!CDO) { OutError = TEXT("Could not get class default object."); return; }

	FProperty* Prop = BP->GeneratedClass->FindPropertyByName(FName(*PropertyName));
	if (!Prop)
	{
		int32 DotIdx = INDEX_NONE;
		if (PropertyName.FindChar(TEXT('.'), DotIdx) && DotIdx > 0)
		{
			const FString CompPart = PropertyName.Left(DotIdx);
			const FString SubProp  = PropertyName.RightChop(DotIdx + 1);
			OutError = FString::Printf(
				TEXT("set_class_default does not write component sub-properties. To set '%s' on the '%s' component, use "
				     "component(action='edit_component_property', blueprint_path='%s', component_name='%s', property_name='%s', property_value='%s')."),
				*SubProp, *CompPart, *BpPath, *CompPart, *SubProp, *PropertyValue);
			return;
		}
		TArray<FString> Suggestions;
		for (TFieldIterator<FProperty> It(BP->GeneratedClass, EFieldIteratorFlags::IncludeSuper); It; ++It)
			if (It->GetName().Contains(PropertyName, ESearchCase::IgnoreCase)) Suggestions.Add(It->GetName());
		OutError = FString::Printf(TEXT("Property '%s' not found on '%s'. Suggestions: %s"), *PropertyName, *BpPath, *FString::Join(Suggestions, TEXT(", ")));
		return;
	}

	void* Data = Prop->ContainerPtrToValuePtr<void>(CDO);
	bool bSet = false;

	if (FClassProperty* ClassProp = CastField<FClassProperty>(Prop))
	{
		UClass* ResolvedClass = nullptr;
		UObject* Loaded = UEditorAssetLibrary::LoadAsset(PropertyValue);
		if (Loaded)
		{
			ResolvedClass = Cast<UClass>(Loaded);
			if (!ResolvedClass)
			{
				if (UBlueprint* LoadedBP = Cast<UBlueprint>(Loaded))
					ResolvedClass = LoadedBP->GeneratedClass;
			}
		}
		if (!ResolvedClass)
		{
			FString ClassPath = PropertyValue.EndsWith(TEXT("_C")) ? PropertyValue : PropertyValue + TEXT("_C");
			Loaded = UEditorAssetLibrary::LoadAsset(ClassPath);
			if (Loaded)
			{
				ResolvedClass = Cast<UClass>(Loaded);
				if (!ResolvedClass)
				{
					if (UBlueprint* LoadedBP = Cast<UBlueprint>(Loaded))
						ResolvedClass = LoadedBP->GeneratedClass;
				}
			}
		}
		if (!ResolvedClass)
		{
			for (TObjectIterator<UClass> It; It; ++It)
				if (It->GetName().Equals(PropertyValue, ESearchCase::IgnoreCase) ||
					It->GetName().Equals(PropertyValue + TEXT("_C"), ESearchCase::IgnoreCase))
				{ ResolvedClass = *It; break; }
		}
		if (!ResolvedClass)
		{
			FString BareName = PropertyValue;
			if (BareName.EndsWith(TEXT("_C"))) BareName = BareName.LeftChop(2);
			if (!BareName.IsEmpty() && !BareName.Contains(TEXT("/")))
			{
				FAssetRegistryModule& AR = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
				FARFilter Filter;
				Filter.PackagePaths.Add(FName(TEXT("/Game")));
				Filter.bRecursivePaths = true;
				Filter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());
				TArray<FAssetData> Hits;
				AR.Get().GetAssets(Filter, Hits);
				for (const FAssetData& AD : Hits)
				{
					if (AD.AssetName.ToString().Equals(BareName, ESearchCase::IgnoreCase))
					{
						if (UBlueprint* HitBP = Cast<UBlueprint>(AD.GetAsset()))
						{
							ResolvedClass = HitBP->GeneratedClass;
							if (ResolvedClass) break;
						}
					}
				}
			}
		}
		if (ResolvedClass)
		{
			ClassProp->SetPropertyValue(Data, ResolvedClass);
			bSet = true;
		}
		else { OutError = FString::Printf(TEXT("Could not resolve class '%s'"), *PropertyValue); return; }
	}
	else if (FEnumProperty* EnumProp = CastField<FEnumProperty>(Prop))
	{
		UEnum* Enm = EnumProp->GetEnum();
		if (Enm)
		{
			int64 EnumVal = Enm->GetValueByNameString(PropertyValue);
			if (EnumVal == INDEX_NONE)
				EnumVal = Enm->GetValueByNameString(Enm->GetName() + TEXT("::") + PropertyValue);
			if (EnumVal == INDEX_NONE)
			{
				for (int32 i = 0; i < Enm->NumEnums() - 1; ++i)
				{
					if (Enm->GetDisplayNameTextByIndex(i).ToString().Equals(PropertyValue, ESearchCase::IgnoreCase))
					{ EnumVal = Enm->GetValueByIndex(i); break; }
				}
			}
			if (EnumVal != INDEX_NONE)
			{
				void* ValuePtr = EnumProp->ContainerPtrToValuePtr<void>(CDO);
				EnumProp->GetUnderlyingProperty()->SetIntPropertyValue(ValuePtr, EnumVal);
				bSet = true;
			}
			else { OutError = FString::Printf(TEXT("Invalid enum value '%s' for %s"), *PropertyValue, *Enm->GetName()); return; }
		}
		else { OutError = TEXT("FEnumProperty has no UEnum"); return; }
	}
	else if (FByteProperty* ByteProp = CastField<FByteProperty>(Prop))
	{
		if (ByteProp->Enum)
		{
			int64 EnumVal = ByteProp->Enum->GetValueByNameString(PropertyValue);
			if (EnumVal == INDEX_NONE)
				EnumVal = ByteProp->Enum->GetValueByNameString(ByteProp->Enum->GetName() + TEXT("::") + PropertyValue);
			if (EnumVal != INDEX_NONE) { ByteProp->SetPropertyValue(Data, (uint8)EnumVal); bSet = true; }
			else { OutError = FString::Printf(TEXT("Invalid enum value '%s'"), *PropertyValue); return; }
		}
		else { ByteProp->SetPropertyValue(Data, (uint8)FCString::Atoi(*PropertyValue)); bSet = true; }
	}
	else
	{
		bSet = (Prop->ImportText_Direct(*PropertyValue, Data, CDO, PPF_None) != nullptr);
	}

	if (!bSet) { OutError = FString::Printf(TEXT("Failed to set '%s' to '%s'"), *PropertyName, *PropertyValue); return; }

	CDO->PostEditChange();
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
	UEditorAssetLibrary::SaveAsset(BpPath, false);

	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"blueprint_path\":\"%s\",\"property\":\"%s\",\"value\":\"%s\"}"),
		*BpPath, *PropertyName, *PropertyValue);
}

void HandleMoveAssetFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("assets"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString AssetPath = BatchToolHelper::GetItemString(Item, TEXT("asset_path"), TEXT("path"));
			FString Dest = BatchToolHelper::GetItemString(Item, TEXT("destination_path"), TEXT("destination"));
			if (AssetPath.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing asset_path")); continue; }
			FString ItemOut, ItemErr;
			HandleMoveAsset(AssetPath, Dest, ItemOut, ItemErr);
			if (ItemErr.IsEmpty()) { auto E = MakeShared<FJsonObject>(); E->SetStringField(TEXT("asset_path"), AssetPath); Batch.AddSuccess(i, E); }
			else Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJsonString);
		return;
	}
	FString AssetPath, DestinationFolder;
	Args->TryGetStringField(TEXT("asset_path"), AssetPath);
	Args->TryGetStringField(TEXT("destination_path"), DestinationFolder);
	HandleMoveAsset(AssetPath, DestinationFolder, OutJsonString, OutError);
}

void HandleDuplicateAssetFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("assets"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString AssetPath = BatchToolHelper::GetItemString(Item, TEXT("asset_path"), TEXT("source_path"));
			FString DestPath = BatchToolHelper::GetItemString(Item, TEXT("destination_path"), TEXT("dest_path"));
			if (AssetPath.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing asset_path")); continue; }
			FString ItemOut, ItemErr;
			HandleDuplicateAsset(AssetPath, DestPath, ItemOut, ItemErr);
			if (ItemErr.IsEmpty()) { auto E = MakeShared<FJsonObject>(); E->SetStringField(TEXT("source"), AssetPath); Batch.AddSuccess(i, E); }
			else Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJsonString);
		return;
	}
	FString SourcePath, DestPath;
	Args->TryGetStringField(TEXT("asset_path"), SourcePath);
	if (SourcePath.IsEmpty()) Args->TryGetStringField(TEXT("source_path"), SourcePath);
	Args->TryGetStringField(TEXT("dest_path"), DestPath);
	if (DestPath.IsEmpty()) Args->TryGetStringField(TEXT("destination_path"), DestPath);
	HandleDuplicateAsset(SourcePath, DestPath, OutJsonString, OutError);
}

void HandleDeleteAssetFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("assets"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			FString AssetPath;
			if ((*ItemsArray)[i]->Type == EJson::String)
				AssetPath = (*ItemsArray)[i]->AsString();
			else if (auto Item = (*ItemsArray)[i]->AsObject())
				AssetPath = BatchToolHelper::GetItemString(Item, TEXT("asset_path"), TEXT("path"));
			if (AssetPath.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing asset_path")); continue; }
			TArray<FString> Paths; Paths.Add(AssetPath);
			FString ItemOut, ItemErr;
			HandleDeleteAsset(Paths, ItemOut, ItemErr);
			if (ItemErr.IsEmpty()) { auto E = MakeShared<FJsonObject>(); E->SetStringField(TEXT("asset_path"), AssetPath); Batch.AddSuccess(i, E); }
			else Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJsonString);
		return;
	}
	TArray<FString> Paths;
	FString SinglePath;
	if (Args->TryGetStringField(TEXT("asset_path"), SinglePath) && !SinglePath.IsEmpty())
		Paths.Add(SinglePath);
	else
	{
		const TArray<TSharedPtr<FJsonValue>>* PluralArr = nullptr;
		if (!Args->TryGetArrayField(TEXT("asset_paths"), PluralArr))
			Args->TryGetArrayField(TEXT("paths"), PluralArr);
		if (PluralArr)
		{
			for (const TSharedPtr<FJsonValue>& V : *PluralArr)
			{
				if (V.IsValid() && V->Type == EJson::String)
				{
					FString P = V->AsString();
					if (!P.IsEmpty()) Paths.Add(P);
				}
			}
		}
	}
	if (Paths.IsEmpty())
	{
		OutError = TEXT("No asset path supplied. Pass `asset_path: '/Game/...'` for a single delete OR `asset_paths: ['/Game/A', '/Game/B', ...]` (alias `paths`) for batch. Empty input silently no-ops; surfacing it as an error so silent failures stop slipping through.");
		return;
	}
	HandleDeleteAsset(Paths, OutJsonString, OutError);
}

void HandleRegisterPrimaryAssetType(const FString& AssetTypeName, const FString& AssetBaseClass,
	const TArray<FString>& DirectoriesToScan, bool bHasBlueprintClasses,
	FString& OutJsonString, FString& OutError)
{
	if (AssetTypeName.IsEmpty()) { OutError = TEXT("asset_type_name is required"); return; }
	if (AssetBaseClass.IsEmpty()) { OutError = TEXT("asset_base_class is required"); return; }

	UAssetManagerSettings* Settings = GetMutableDefault<UAssetManagerSettings>();
	if (!Settings) { OutError = TEXT("Could not get UAssetManagerSettings"); return; }

	for (const FPrimaryAssetTypeInfo& Existing : Settings->PrimaryAssetTypesToScan)
	{
		if (Existing.PrimaryAssetType == FName(*AssetTypeName))
		{
			OutError = FString::Printf(TEXT("Primary asset type '%s' already registered."), *AssetTypeName);
			return;
		}
	}

	UClass* BaseClass = FindObject<UClass>(nullptr, *AssetBaseClass);
	if (!BaseClass) BaseClass = LoadObject<UClass>(nullptr, *AssetBaseClass);
	if (!BaseClass)
	{
		static const TCHAR* CommonPrefixes[] = {
			TEXT("/Script/Engine."), TEXT("/Script/CoreUObject."),
			TEXT("/Script/GameplayAbilities."), TEXT("/Script/UMG."), nullptr
		};
		for (int32 Pi = 0; CommonPrefixes[Pi] && !BaseClass; Pi++)
		{
			FString FullPath = FString(CommonPrefixes[Pi]) + AssetBaseClass;
			BaseClass = FindObject<UClass>(nullptr, *FullPath);
			if (!BaseClass) BaseClass = LoadObject<UClass>(nullptr, *FullPath);
		}
	}
	if (!BaseClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("register_primary_asset_type: class '%s' not found, using UObject"), *AssetBaseClass);
		BaseClass = UObject::StaticClass();
	}

	TArray<FDirectoryPath> Dirs;
	for (const FString& Dir : DirectoriesToScan)
	{
		FDirectoryPath DP; DP.Path = Dir; Dirs.Add(DP);
	}
	if (Dirs.IsEmpty())
	{
		FDirectoryPath DP; DP.Path = TEXT("/Game"); Dirs.Add(DP);
	}

	FPrimaryAssetTypeInfo NewType(FName(*AssetTypeName), BaseClass, bHasBlueprintClasses, false,
		MoveTemp(Dirs), TArray<FSoftObjectPath>());

	Settings->PrimaryAssetTypesToScan.Add(NewType);
	Settings->TryUpdateDefaultConfigFile();
	Settings->PostEditChange();

	if (UAssetManager* AM = UAssetManager::GetIfInitialized())
		AM->ReinitializeFromConfig();

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"asset_type\":\"%s\",\"base_class\":\"%s\",\"directories_count\":%d}"),
		*AssetTypeName, *AssetBaseClass, DirectoriesToScan.Num());
}

void HandleGetAssetManagerSummary(FString& OutJsonString, FString& OutError)
{
	const UAssetManagerSettings* Settings = GetDefault<UAssetManagerSettings>();
	if (!Settings) { OutError = TEXT("Could not get UAssetManagerSettings"); return; }

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> TypesArr;

	for (const FPrimaryAssetTypeInfo& TypeInfo : Settings->PrimaryAssetTypesToScan)
	{
		TSharedPtr<FJsonObject> T = MakeShared<FJsonObject>();
		T->SetStringField(TEXT("type_name"), TypeInfo.PrimaryAssetType.ToString());
		T->SetBoolField(TEXT("has_blueprint_classes"), TypeInfo.bHasBlueprintClasses);
		T->SetBoolField(TEXT("editor_only"), TypeInfo.bIsEditorOnly);

		TArray<TSharedPtr<FJsonValue>> DirsArr;
		for (const FDirectoryPath& Dir : TypeInfo.GetDirectories())
			DirsArr.Add(MakeShared<FJsonValueString>(Dir.Path));
		T->SetArrayField(TEXT("directories"), DirsArr);

		if (TypeInfo.AssetBaseClassLoaded)
			T->SetStringField(TEXT("base_class"), TypeInfo.AssetBaseClassLoaded->GetName());
		else if (TypeInfo.GetAssetBaseClass().IsValid())
			T->SetStringField(TEXT("base_class"), TypeInfo.GetAssetBaseClass().GetAssetName());

		TypesArr.Add(MakeShared<FJsonValueObject>(T));
	}

	Root->SetArrayField(TEXT("primary_asset_types"), TypesArr);
	Root->SetNumberField(TEXT("count"), (double)Settings->PrimaryAssetTypesToScan.Num());

	TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);
}

void HandleReimportAsset(const FString& AssetPath, FString& OutJsonString, FString& OutError)
{
	if (AssetPath.IsEmpty()) { OutError = TEXT("asset_path is required"); return; }

	UObject* Asset = UEditorAssetLibrary::LoadAsset(AssetPath);
	if (!Asset) { OutError = FString::Printf(TEXT("Asset not found: '%s'"), *AssetPath); return; }

	bool bSuccess = FReimportManager::Instance()->Reimport(Asset, false, true);
	if (!bSuccess)
	{
		OutError = FString::Printf(TEXT("Reimport failed for '%s' — asset may not have an associated source file."), *AssetPath);
		return;
	}

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"asset_path\":\"%s\",\"message\":\"Reimport completed\"}"),
		*AssetPath);
}

void HandleCheckAssetExistsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString AssetPath;
	if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath))
		if (!Args->TryGetStringField(TEXT("path"), AssetPath))
			Args->TryGetStringField(TEXT("name"), AssetPath);
	if (AssetPath.IsEmpty()) { OutError = TEXT("Missing required parameter: asset_path"); return; }

	const bool bExists = UEditorAssetLibrary::DoesAssetExist(AssetPath);
	FString Klass;
	if (bExists)
	{
		FString ObjectPath = AssetPath;
		int32 DotIdx = INDEX_NONE;
		if (!ObjectPath.FindChar(TEXT('.'), DotIdx))
		{
			int32 SlashIdx = INDEX_NONE;
			ObjectPath.FindLastChar(TEXT('/'), SlashIdx);
			if (SlashIdx != INDEX_NONE && SlashIdx + 1 < ObjectPath.Len())
			{
				const FString AssetName = ObjectPath.Mid(SlashIdx + 1);
				ObjectPath = ObjectPath + TEXT(".") + AssetName;
			}
		}
		FAssetRegistryModule& AR = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		FAssetData AD = AR.Get().GetAssetByObjectPath(FSoftObjectPath(ObjectPath));
		if (AD.IsValid()) Klass = AD.AssetClassPath.GetAssetName().ToString();
	}
	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"exists\":%s,\"asset_path\":\"%s\"%s%s%s}"),
		bExists ? TEXT("true") : TEXT("false"),
		*AssetPath,
		Klass.IsEmpty() ? TEXT("") : TEXT(",\"asset_class\":\""),
		*Klass,
		Klass.IsEmpty() ? TEXT("") : TEXT("\""));
}

void HandleFindAssetByNameFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }

	TSharedPtr<FJsonObject> Effective = Args;
	for (const TCHAR* WrapperKey : { TEXT("search_params"), TEXT("options"), TEXT("filters"), TEXT("params") })
	{
		const TSharedPtr<FJsonObject>* WrappedObj = nullptr;
		if (Args->TryGetObjectField(WrapperKey, WrappedObj) && WrappedObj && WrappedObj->IsValid())
		{
			Effective = *WrappedObj;
			break;
		}
	}

	FString NamePattern;
	if (!Effective->TryGetStringField(TEXT("name_pattern"), NamePattern))
		if (!Effective->TryGetStringField(TEXT("asset_name"), NamePattern))
			if (!Effective->TryGetStringField(TEXT("name"), NamePattern))
				if (!Effective->TryGetStringField(TEXT("query"), NamePattern))
					if (!Effective->TryGetStringField(TEXT("pattern"), NamePattern))
						Effective->TryGetStringField(TEXT("search"), NamePattern);

	FString AssetType, ParentClass;
	if (!Effective->TryGetStringField(TEXT("asset_type"), AssetType))
		if (!Effective->TryGetStringField(TEXT("asset_class"), AssetType))
			Effective->TryGetStringField(TEXT("class"), AssetType);
	if (!Effective->TryGetStringField(TEXT("parent_class"), ParentClass))
		Effective->TryGetStringField(TEXT("base_class"), ParentClass);

	FString FilterArg;
	if (Effective->TryGetStringField(TEXT("filter"), FilterArg) && !FilterArg.IsEmpty())
	{
		bool bFilterIsClass = false;
		if (AssetType.IsEmpty() && ParentClass.IsEmpty())
		{
			FString T = FilterArg;
			T.RemoveSpacesInline();
			TArray<FString> Candidates;
			Candidates.Add(T);
			if (!T.StartsWith(TEXT("U"))) Candidates.Add(TEXT("U") + T);
			if (T.StartsWith(TEXT("U")) && T.Len() > 1) Candidates.Add(T.RightChop(1));
			for (const FString& C : Candidates)
			{
				if (FindObject<UClass>(nullptr, *(TEXT("/Script/Engine.") + C))
					|| FindFirstObject<UClass>(*C, EFindFirstObjectOptions::NativeFirst))
				{
					bFilterIsClass = true;
					break;
				}
			}
		}
		if (bFilterIsClass)
			AssetType = FilterArg;
		else if (NamePattern.IsEmpty())
			NamePattern = FilterArg;
	}

	if (NamePattern.IsEmpty() && AssetType.IsEmpty() && ParentClass.IsEmpty())
	{
		OutError = TEXT("Missing search filter: pass at least one of name_pattern, asset_type, or parent_class.");
		return;
	}

	HandleFindAssetByName(NamePattern, AssetType, ParentClass, OutJsonString, OutError);
}

void HandleMoveAssetsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }

	FString Destination;
	if (!Args->TryGetStringField(TEXT("destination_folder"), Destination) || Destination.IsEmpty())
	{
		OutError = TEXT("Missing required parameter: destination_folder");
		return;
	}

	const TArray<TSharedPtr<FJsonValue>>* PathsJson = nullptr;
	if (!Args->TryGetArrayField(TEXT("asset_paths"), PathsJson) || !PathsJson || PathsJson->Num() == 0)
	{
		OutError = TEXT("Missing or empty required parameter: asset_paths (array of asset paths)");
		return;
	}

	TArray<FString> AssetPaths;
	for (const TSharedPtr<FJsonValue>& V : *PathsJson)
	{
		FString P;
		if (V.IsValid() && V->TryGetString(P) && !P.IsEmpty()) AssetPaths.Add(P);
	}

	HandleMoveAssets(AssetPaths, Destination, OutJsonString, OutError);
}

void HandleCreateProjectFolderFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }

	FString FolderPath;
	if (!Args->TryGetStringField(TEXT("folder_path"), FolderPath))
		if (!Args->TryGetStringField(TEXT("asset_path"), FolderPath))
			if (!Args->TryGetStringField(TEXT("path"), FolderPath))
				if (!Args->TryGetStringField(TEXT("destination_folder"), FolderPath))
					Args->TryGetStringField(TEXT("name"), FolderPath);
	if (FolderPath.IsEmpty())
	{
		OutError = TEXT("Missing folder path. Pass folder_path or asset_path (e.g. /Game/Foo or /<PluginName>/Foo).");
		return;
	}

	TSharedPtr<FJsonObject> ResultObject = MakeShareable(new FJsonObject);
	bool bAlreadyExists = false;
	if (!UECPMountResolver::IsValidMountedPath(FolderPath))
	{
		OutError = FString::Printf(TEXT("Invalid folder path '%s'. Use /Game/... or /<PluginName>/...."), *FolderPath);
		return;
	}
	else if (UEditorAssetLibrary::DoesDirectoryExist(FolderPath))
	{
		bAlreadyExists = true;
	}
	else if (!UEditorAssetLibrary::MakeDirectory(FolderPath))
	{
		OutError = FString::Printf(TEXT("Failed to create folder at '%s'."), *FolderPath);
		return;
	}
	else
	{
		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		AssetRegistryModule.Get().ScanPathsSynchronous({ FolderPath }, true);

		FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
		ContentBrowserModule.Get().SyncBrowserToAssets(TArray<FAssetData>(), true);
	}

	ResultObject->SetBoolField(TEXT("success"), true);
	ResultObject->SetStringField(TEXT("folder_path"), FolderPath);
	ResultObject->SetStringField(TEXT("message"), bAlreadyExists
		? FString::Printf(TEXT("Folder already exists at '%s'"), *FolderPath)
		: FString::Printf(TEXT("Successfully created folder at '%s'"), *FolderPath));

	TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&OutJsonString);
	FJsonSerializer::Serialize(ResultObject.ToSharedRef(), Writer);
}

void HandleGetProjectRootPathFromArgs(const TSharedPtr<FJsonObject>& , FString& OutJsonString, FString& OutError)
{
	FString ProjectPath = FPaths::GetProjectFilePath();
	if (ProjectPath.IsEmpty())
	{
		OutError = TEXT("Could not determine the project root path via FPaths::GetProjectFilePath().");
		return;
	}

	TSharedPtr<FJsonObject> ResultObject = MakeShareable(new FJsonObject);
	ResultObject->SetBoolField(TEXT("success"), true);
	ResultObject->SetStringField(TEXT("project_path"), ProjectPath);
	ResultObject->SetStringField(TEXT("project_dir"), FPaths::ProjectDir());

	TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&OutJsonString);
	FJsonSerializer::Serialize(ResultObject.ToSharedRef(), Writer);
}

void HandleRenameAssetFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString AssetPath, NewName;
	if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath) || !Args->TryGetStringField(TEXT("new_name"), NewName))
	{
		OutError = TEXT("Missing required parameters: asset_path, new_name");
		return;
	}
	HandleRenameAsset(AssetPath, NewName, OutJsonString, OutError);
}

void HandleRegisterPrimaryAssetTypeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString TypeName, BaseClass;
	bool bHasBP = false;
	Args->TryGetStringField(TEXT("asset_type_name"), TypeName);
	Args->TryGetStringField(TEXT("asset_base_class"), BaseClass);
	Args->TryGetBoolField(TEXT("has_blueprint_classes"), bHasBP);
	TArray<FString> Dirs;
	const TArray<TSharedPtr<FJsonValue>>* DirsArr = nullptr;
	if (Args->TryGetArrayField(TEXT("directories_to_scan"), DirsArr) && DirsArr)
	{
		for (const TSharedPtr<FJsonValue>& V : *DirsArr)
		{
			FString S;
			if (V.IsValid() && V->TryGetString(S)) Dirs.Add(S);
		}
	}
	HandleRegisterPrimaryAssetType(TypeName, BaseClass, Dirs, bHasBP, OutJsonString, OutError);
}

void HandleGetAssetManagerSummaryFromArgs(const TSharedPtr<FJsonObject>& , FString& OutJsonString, FString& OutError)
{
	HandleGetAssetManagerSummary(OutJsonString, OutError);
}

void HandleReimportAssetFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString Path;
	Args->TryGetStringField(TEXT("asset_path"), Path);
	HandleReimportAsset(Path, OutJsonString, OutError);
}

namespace
{
	void WrapAssetCreation(const TSharedPtr<FJsonObject>& Args,
		void(*Fn)(const FString&, const FString&, FString&, FString&),
		FString& OutJsonString, FString& OutError)
	{
		if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
		FString Name, SavePath;
		Args->TryGetStringField(TEXT("name"), Name);
		if (Name.IsEmpty()) Args->TryGetStringField(TEXT("interface_name"), Name);
		if (Name.IsEmpty()) Args->TryGetStringField(TEXT("library_name"), Name);
		if (Name.IsEmpty()) Args->TryGetStringField(TEXT("asset_name"), Name);
		Args->TryGetStringField(TEXT("save_path"), SavePath);
		if (SavePath.IsEmpty()) SavePath = FSettingsManager::GetDefaultSavePath();
		FString OutPath;
		Fn(Name, SavePath, OutPath, OutError);
		if (OutError.IsEmpty())
			OutJsonString = FString::Printf(TEXT("{\"asset_path\":\"%s\"}"), *OutPath);
	}
}

void HandleCreateBlueprintInterfaceFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	WrapAssetCreation(Args, &HandleCreateBlueprintInterface, OutJsonString, OutError);
}

void HandleCreateBlueprintFunctionLibraryFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	WrapAssetCreation(Args, &HandleCreateBlueprintFunctionLibrary, OutJsonString, OutError);
}

void HandleCreateMacroLibraryFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	WrapAssetCreation(Args, &HandleCreateMacroLibrary, OutJsonString, OutError);
}

void HandleGetLevelBlueprintFromArgs(const TSharedPtr<FJsonObject>& , FString& OutJsonString, FString& OutError)
{
	HandleGetLevelBlueprint(OutJsonString, OutError);
}

void HandleCompileBlueprintFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("blueprints"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		TArray<TSharedPtr<FJsonValue>> SilentFailures;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			FString BpPath;
			const TSharedPtr<FJsonValue>& Val = (*ItemsArray)[i];
			if (Val->Type == EJson::String) BpPath = Val->AsString();
			else if (Val->Type == EJson::Object)
			{
				TSharedPtr<FJsonObject> Item = Val->AsObject();
				if (Item.IsValid()) Item->TryGetStringField(TEXT("blueprint_path"), BpPath);
			}
			if (BpPath.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing blueprint_path")); continue; }

			FString ItemOut, ItemErr;
			HandleCompileBlueprint(BpPath, ItemOut, ItemErr);
			if (ItemErr.IsEmpty())
			{
				auto Extra = MakeShared<FJsonObject>();
				Extra->SetStringField(TEXT("blueprint_path"), BpPath);
				Batch.AddSuccess(i, Extra);

				TSharedPtr<FJsonObject> ItemJson;
				TSharedRef<TJsonReader<>> Rd = TJsonReaderFactory<>::Create(ItemOut);
				if (FJsonSerializer::Deserialize(Rd, ItemJson) && ItemJson.IsValid())
				{
					const TArray<TSharedPtr<FJsonValue>>* HArr = nullptr;
					if (ItemJson->TryGetArrayField(TEXT("health_issues"), HArr) && HArr)
					{
						TArray<TSharedPtr<FJsonValue>> Pruned;
						for (const TSharedPtr<FJsonValue>& HV : *HArr)
						{
							const TSharedPtr<FJsonObject>* HO = nullptr;
							if (!HV->TryGetObject(HO) || !HO) continue;
							FString Issue, Node;
							(*HO)->TryGetStringField(TEXT("issue"), Issue);
							(*HO)->TryGetStringField(TEXT("node"), Node);
							if (Issue.Contains(TEXT("no exec input")) || Issue.Contains(TEXT("never execute")))
								Pruned.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("%s — %s"), *Node, *Issue)));
						}
						if (Pruned.Num() > 0)
						{
							auto SF = MakeShared<FJsonObject>();
							SF->SetStringField(TEXT("blueprint_path"), BpPath);
							SF->SetArrayField(TEXT("pruned_nodes"), Pruned);
							SilentFailures.Add(MakeShared<FJsonValueObject>(SF));
						}
					}
				}
			}
			else
			{
				Batch.AddFailure(i, ItemErr);
			}
		}
		Batch.Finalize(OutJsonString);

		if (SilentFailures.Num() > 0)
		{
			TSharedPtr<FJsonObject> RootJson;
			TSharedRef<TJsonReader<>> Rd = TJsonReaderFactory<>::Create(OutJsonString);
			if (FJsonSerializer::Deserialize(Rd, RootJson) && RootJson.IsValid())
			{
				RootJson->SetArrayField(TEXT("silent_failures"), SilentFailures);
				RootJson->SetStringField(TEXT("silent_failures_note"),
					TEXT("These compiled clean but contain impure nodes with no exec input — UE prunes them and reads their output as a default (0/null). A clean compile here does NOT mean the logic runs: wire the node's execute pin into the chain (rebuild with clear_before_build=true), or mark a simple getter pure (set_function_pure), then recompile."));
				TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> W =
					TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&OutJsonString);
				FJsonSerializer::Serialize(RootJson.ToSharedRef(), W);
			}
		}
		return;
	}

	FString BpPath;
	if (!Args->TryGetStringField(TEXT("blueprint_path"), BpPath))
	{
		OutError = TEXT("Missing required parameter: blueprint_path");
		return;
	}
	HandleCompileBlueprint(BpPath, OutJsonString, OutError);
}

static bool ResolveClassDefaultValue(const TSharedPtr<FJsonObject>& Obj, FString& OutValue)
{
	for (const TCHAR* Field : { TEXT("property_value"), TEXT("value"), TEXT("default_value"), TEXT("val") })
	{
		if (Obj->TryGetStringField(Field, OutValue)) return true;
		bool bVal = false;
		if (Obj->TryGetBoolField(Field, bVal))
		{
			OutValue = bVal ? TEXT("true") : TEXT("false");
			return true;
		}
		double NumVal = 0.0;
		if (Obj->TryGetNumberField(Field, NumVal))
		{
			OutValue = FString::SanitizeFloat(NumVal);
			return true;
		}
	}
	return false;
}

static bool ResolveClassDefaultPropertyName(const TSharedPtr<FJsonObject>& Obj, FString& OutName)
{
	for (const TCHAR* Field : { TEXT("property_name"), TEXT("name"), TEXT("property") })
	{
		if (Obj->TryGetStringField(Field, OutName) && !OutName.IsEmpty()) return true;
	}
	return false;
}

void HandleSetClassDefaultFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }

	FString OuterBp;
	Args->TryGetStringField(TEXT("blueprint_path"), OuterBp);

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("properties"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); ++i)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item (expected object)")); continue; }

			FString ItemBp; Item->TryGetStringField(TEXT("blueprint_path"), ItemBp);
			if (ItemBp.IsEmpty()) ItemBp = OuterBp;
			if (ItemBp.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing blueprint_path")); continue; }

			FString PropName;
			if (!ResolveClassDefaultPropertyName(Item, PropName)) { Batch.AddFailure(i, TEXT("Missing property_name")); continue; }

			FString PropValue;
			if (!ResolveClassDefaultValue(Item, PropValue)) { Batch.AddFailure(i, TEXT("Missing value")); continue; }

			FString ItemOut, ItemErr;
			HandleSetClassDefault(ItemBp, PropName, PropValue, ItemOut, ItemErr);
			if (ItemErr.IsEmpty())
			{
				TSharedPtr<FJsonObject> Extra = MakeShared<FJsonObject>();
				Extra->SetStringField(TEXT("property_name"), PropName);
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

	FString BpPath = OuterBp;
	FString PropName;
	ResolveClassDefaultPropertyName(Args, PropName);
	FString PropValue;
	ResolveClassDefaultValue(Args, PropValue);

	HandleSetClassDefault(BpPath, PropName, PropValue, OutJsonString, OutError);
}

void HandleCreateLevelFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }

	FString Name;
	Args->TryGetStringField(TEXT("name"), Name);
	if (Name.IsEmpty()) { OutError = TEXT("create_asset(Level): 'name' is required"); return; }

	FString SavePath;
	if (!Args->TryGetStringField(TEXT("save_path"), SavePath)) Args->TryGetStringField(TEXT("path"), SavePath);
	if (SavePath.IsEmpty()) SavePath = TEXT("/Game");
	while (SavePath.EndsWith(TEXT("/"))) SavePath.LeftChopInline(1);

	FString TemplatePath;
	Args->TryGetStringField(TEXT("template"), TemplatePath);
	bool bPartitioned = false;
	Args->TryGetBoolField(TEXT("partitioned"), bPartitioned);
	if (!TemplatePath.IsEmpty() && !TemplatePath.StartsWith(TEXT("/")))
	{
		FString Lowered = TemplatePath.ToLower();
		if (Lowered == TEXT("open_world") || Lowered == TEXT("openworld") || Lowered == TEXT("world_partition"))
		{
			bPartitioned = true;
			TemplatePath.Empty();
		}
		else if (Lowered == TEXT("default") || Lowered == TEXT("empty"))
		{
			TemplatePath.Empty();
		}
	}

	const FString AssetPath = SavePath + TEXT("/") + Name;

	if (UEditorAssetLibrary::DoesAssetExist(AssetPath))
	{
		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("asset_path"), AssetPath);
		Result->SetStringField(TEXT("asset_class"), TEXT("World"));
		Result->SetStringField(TEXT("note"), TEXT("Level already existed; left untouched."));
		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&OutJsonString);
		FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
		return;
	}

	ULevelEditorSubsystem* LES = GEditor ? GEditor->GetEditorSubsystem<ULevelEditorSubsystem>() : nullptr;
	if (!LES)
	{
		OutError = TEXT("create_asset(Level): ULevelEditorSubsystem unavailable. The editor may be initialising — retry once the editor finishes loading.");
		return;
	}

	bool bCreated = false;
	if (!TemplatePath.IsEmpty())
	{
		bCreated = LES->NewLevelFromTemplate(AssetPath, TemplatePath);
		if (!bCreated)
		{
			OutError = FString::Printf(TEXT("NewLevelFromTemplate(%s, template=%s) failed. Common causes: invalid template path, save path outside /Game, or unsaved-changes prompt was cancelled."),
				*AssetPath, *TemplatePath);
			return;
		}
	}
	else
	{
		bCreated = LES->NewLevel(AssetPath, bPartitioned);
		if (!bCreated)
		{
			OutError = FString::Printf(TEXT("NewLevel(%s, partitioned=%s) failed. Common causes: save path outside /Game, name collides with a non-asset, or unsaved-changes prompt was cancelled."),
				*AssetPath, bPartitioned ? TEXT("true") : TEXT("false"));
			return;
		}
	}

	UEditorAssetLibrary::SaveAsset(AssetPath, false);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	Result->SetStringField(TEXT("asset_class"), TEXT("World"));
	Result->SetBoolField(TEXT("partitioned"), bPartitioned);
	if (!TemplatePath.IsEmpty()) Result->SetStringField(TEXT("template"), TemplatePath);
	Result->SetStringField(TEXT("note"), TEXT("Level created and loaded in the editor."));
	TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
}

void HandleValidateBlueprintFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }

	FString BpPath;
	Args->TryGetStringField(TEXT("blueprint_path"), BpPath);
	if (BpPath.IsEmpty()) { OutError = TEXT("Missing required parameter: blueprint_path"); return; }

	UBlueprint* BP = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(BpPath));
	if (!BP) { OutError = FString::Printf(TEXT("Could not load Blueprint: %s"), *BpPath); return; }

	TArray<TSharedPtr<FJsonValue>> Issues;

	auto AddIssue = [&](const FString& Severity, const FString& Message, const FString& GraphName = TEXT(""))
	{
		TSharedPtr<FJsonObject> Issue = MakeShared<FJsonObject>();
		Issue->SetStringField(TEXT("severity"), Severity);
		Issue->SetStringField(TEXT("message"), Message);
		if (!GraphName.IsEmpty()) Issue->SetStringField(TEXT("graph_name"), GraphName);
		Issues.Add(MakeShared<FJsonValueObject>(Issue));
	};

	if (BP->Status == BS_Error)
		AddIssue(TEXT("error"), TEXT("Blueprint is in error state — recompile to see detailed errors."));

	auto CheckGraph = [&](UEdGraph* Graph)
	{
		if (!Graph) return;
		const FString GraphName = Graph->GetFName().ToString();
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (!Node) continue;
			for (UEdGraphPin* Pin : Node->Pins)
			{
				if (!Pin) continue;
				for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
				{
					if (!LinkedPin || !LinkedPin->GetOwningNodeUnchecked() || !Graph->Nodes.Contains(LinkedPin->GetOwningNodeUnchecked()))
					{
						AddIssue(TEXT("error"),
							FString::Printf(TEXT("Broken pin connection on node '%s' (pin '%s' links to a missing node)"),
								*Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString(), *Pin->PinName.ToString()),
							GraphName);
						break;
					}
				}
			}
		}
	};

	for (UEdGraph* G : BP->FunctionGraphs) CheckGraph(G);
	for (UEdGraph* G : BP->UbergraphPages) CheckGraph(G);
	for (UEdGraph* G : BP->MacroGraphs) CheckGraph(G);

	auto CheckMapAddDuplicateKeys = [&](UEdGraph* Graph)
	{
		if (!Graph) return;
		const FString GraphName = Graph->GetFName().ToString();
		TMap<FString, int32> SignatureCounts;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			UK2Node_CallFunction* CallNode = Cast<UK2Node_CallFunction>(Node);
			if (!CallNode) continue;
			const UFunction* Func = CallNode->GetTargetFunction();
			if (!Func || Func->GetName() != TEXT("Map_Add")) continue;
			UEdGraphPin* KeyPin = CallNode->FindPin(TEXT("Key"), EGPD_Input);
			if (!KeyPin || KeyPin->LinkedTo.Num() > 0) continue;
			FString MapVarName;
			UEdGraphPin* TargetMapPin = CallNode->FindPin(TEXT("TargetMap"), EGPD_Input);
			if (TargetMapPin && TargetMapPin->LinkedTo.Num() > 0)
			{
				UEdGraphPin* Src = TargetMapPin->LinkedTo[0];
				if (Src)
				{
					UK2Node_VariableGet* VarGet = Cast<UK2Node_VariableGet>(Src->GetOwningNodeUnchecked());
					if (VarGet) MapVarName = VarGet->GetVarName().ToString();
				}
			}
			if (MapVarName.IsEmpty()) MapVarName = TEXT("__unknown__");
			SignatureCounts.FindOrAdd(MapVarName + TEXT("|") + KeyPin->DefaultValue)++;
		}
		for (const auto& Pair : SignatureCounts)
		{
			if (Pair.Value < 2) continue;
			FString MapVar, KeyVal;
			Pair.Key.Split(TEXT("|"), &MapVar, &KeyVal);
			AddIssue(TEXT("warning"),
				FString::Printf(TEXT("%d Map_Add calls on '%s' all use Key default '%s' — only the last persists. Each Map_Add must have a distinct Key."),
					Pair.Value, *MapVar, *KeyVal),
				GraphName);
		}
	};
	for (UEdGraph* G : BP->FunctionGraphs) CheckMapAddDuplicateKeys(G);
	for (UEdGraph* G : BP->UbergraphPages) CheckMapAddDuplicateKeys(G);

	auto IsPinUnwiredAndUnset = [](UEdGraphPin* Pin) -> bool
	{
		if (!Pin || Pin->Direction != EGPD_Input) return false;
		if (Pin->LinkedTo.Num() > 0) return false;
		if (Pin->DefaultObject != nullptr) return false;
		if (!Pin->DefaultValue.IsEmpty() && Pin->DefaultValue != TEXT("false") && Pin->DefaultValue != TEXT("0"))
			return false;
		return true;
	};
	auto CheckUnwiredRequiredInputs = [&](UEdGraph* Graph)
	{
		if (!Graph) return;
		const FString GraphName = Graph->GetFName().ToString();
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (!Node) continue;
			const FString ClassName = Node->GetClass()->GetName();
			if (ClassName == TEXT("K2Node_IsValid"))
			{
				UEdGraphPin* InputPin = Node->FindPin(TEXT("InputObject"), EGPD_Input);
				if (!InputPin) InputPin = Node->FindPin(TEXT("Input Object"), EGPD_Input);
				if (InputPin && IsPinUnwiredAndUnset(InputPin))
				{
					AddIssue(TEXT("warning"),
						FString::Printf(TEXT("IsValid node has unwired Input Object — it will always report invalid. Wire an actor/object reference into 'InputObject'.")),
						GraphName);
				}
			}
			else if (UK2Node_DynamicCast* CastNode = Cast<UK2Node_DynamicCast>(Node))
			{
				UEdGraphPin* ObjPin = CastNode->GetCastSourcePin();
				if (ObjPin && IsPinUnwiredAndUnset(ObjPin))
				{
					AddIssue(TEXT("warning"),
						FString::Printf(TEXT("Cast To '%s' has unwired Object input — the cast will always fail at runtime."),
							CastNode->TargetType ? *CastNode->TargetType->GetName() : TEXT("?")),
						GraphName);
				}
			}
			else if (UK2Node_IfThenElse* Branch = Cast<UK2Node_IfThenElse>(Node))
			{
				UEdGraphPin* CondPin = Branch->GetConditionPin();
				if (CondPin && CondPin->LinkedTo.Num() == 0 &&
					(CondPin->DefaultValue.IsEmpty() || CondPin->DefaultValue == TEXT("false")))
				{
					AddIssue(TEXT("warning"),
						TEXT("Branch has unwired Condition (defaults to false — True exec is unreachable). Wire a bool into Condition or set its default to true."),
						GraphName);
				}
			}
		}
	};
	for (UEdGraph* G : BP->FunctionGraphs)  CheckUnwiredRequiredInputs(G);
	for (UEdGraph* G : BP->UbergraphPages)  CheckUnwiredRequiredInputs(G);
	for (UEdGraph* G : BP->MacroGraphs)     CheckUnwiredRequiredInputs(G);

	if (UWidgetBlueprint* WBP = Cast<UWidgetBlueprint>(BP))
	{
		for (const FDelegateEditorBinding& B : WBP->Bindings)
		{
			if (B.Kind != EBindingKind::Function) continue;
			const FName FnName = B.FunctionName;
			if (FnName.IsNone()) continue;
			UEdGraph* FnGraph = nullptr;
			for (UEdGraph* G : WBP->FunctionGraphs)
			{
				if (G && G->GetFName() == FnName) { FnGraph = G; break; }
			}
			if (!FnGraph)
			{
				AddIssue(TEXT("error"),
					FString::Printf(TEXT("Widget binding %s.%s → %s: bound function not found on this widget."),
						*B.ObjectName, *B.PropertyName.ToString(), *FnName.ToString()));
				continue;
			}
			int32 RealNodeCount = 0;
			UK2Node_FunctionResult* ResultNode = nullptr;
			for (UEdGraphNode* N : FnGraph->Nodes)
			{
				if (!N) continue;
				if (N->IsA<UK2Node_FunctionEntry>()) continue;
				if (UK2Node_FunctionResult* R = Cast<UK2Node_FunctionResult>(N)) { ResultNode = R; continue; }
				if (N->IsA<UEdGraphNode_Comment>()) continue;
				++RealNodeCount;
			}
			bool bReturnValueWired = false;
			if (ResultNode)
			{
				for (UEdGraphPin* Pin : ResultNode->Pins)
				{
					if (Pin && Pin->Direction == EGPD_Input &&
						Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec &&
						Pin->LinkedTo.Num() > 0)
					{ bReturnValueWired = true; break; }
				}
			}
			if (RealNodeCount == 0 && !bReturnValueWired)
			{
				AddIssue(TEXT("warning"),
					FString::Printf(TEXT("Widget binding %s.%s → %s: bound function is a stub (no nodes, return value unwired). The property will be stuck at its default. Add the actual value computation to %s."),
						*B.ObjectName, *B.PropertyName.ToString(), *FnName.ToString(), *FnName.ToString()),
					FnName.ToString());
			}
		}
	}

	if (BP->GeneratedClass)
	{
		for (const FBPInterfaceDescription& InterfaceDesc : BP->ImplementedInterfaces)
		{
			if (!InterfaceDesc.Interface) continue;
			for (TFieldIterator<UFunction> FuncIt(InterfaceDesc.Interface, EFieldIteratorFlags::ExcludeSuper); FuncIt; ++FuncIt)
			{
				UFunction* InterfaceFunc = *FuncIt;
				if (!InterfaceFunc || !InterfaceFunc->HasAnyFunctionFlags(FUNC_BlueprintEvent)) continue;
				const FName FuncName = InterfaceFunc->GetFName();
				bool bHasOverride = false;
				for (const UEdGraph* Graph : InterfaceDesc.Graphs)
					if (Graph && Graph->GetFName() == FuncName) { bHasOverride = true; break; }
				if (!bHasOverride)
					for (const UEdGraph* Graph : BP->FunctionGraphs)
						if (Graph && Graph->GetFName() == FuncName) { bHasOverride = true; break; }
				if (!bHasOverride)
				{
					for (UEdGraph* UG : BP->UbergraphPages)
					{
						if (!UG) continue;
						TArray<UK2Node_Event*> EventNodes;
						UG->GetNodesOfClass<UK2Node_Event>(EventNodes);
						for (const UK2Node_Event* Ev : EventNodes)
							if (Ev && Ev->EventReference.GetMemberName() == FuncName) { bHasOverride = true; break; }
						if (bHasOverride) break;
					}
				}
				if (!bHasOverride)
					AddIssue(TEXT("warning"),
						FString::Printf(TEXT("Interface function '%s' from '%s' has no override graph."),
							*FuncName.ToString(), *InterfaceDesc.Interface->GetName()));
			}
		}
	}

	const bool bIsValid = !Issues.ContainsByPredicate([](const TSharedPtr<FJsonValue>& V)
	{
		TSharedPtr<FJsonObject> Obj = V->AsObject();
		return Obj.IsValid() && Obj->GetStringField(TEXT("severity")) == TEXT("error");
	});

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetBoolField(TEXT("is_valid"), bIsValid);
	Result->SetStringField(TEXT("blueprint_path"), BpPath);
	Result->SetNumberField(TEXT("issue_count"), Issues.Num());
	Result->SetArrayField(TEXT("issues"), Issues);

	TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
}

}
