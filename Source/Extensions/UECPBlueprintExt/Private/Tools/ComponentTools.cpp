// Copyright 2026, BlueprintsLab, All rights reserved

#include "Tools/ComponentTools.h"
#include "Tools/PropertyWriteReport.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "EditorAssetLibrary.h"
#include "Engine/Blueprint.h"
#include "Misc/PackageName.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "Engine/InheritableComponentHandler.h"
#include "Serialization/JsonSerializer.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/AudioComponent.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundClass.h"
#include "Engine/CollisionProfile.h"
#include "Engine/World.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimBlueprintGeneratedClass.h"
#include "Animation/Skeleton.h"
#include "GameFramework/Character.h"
#include "EngineUtils.h"
#include "Editor.h"
#include "Dom/JsonObject.h"
#include "Tools/BatchToolHelper.h"

namespace ComponentTools
{

static bool CoerceJsonValueToString(const TSharedPtr<FJsonObject>& Obj, const FString& Field, FString& OutValue)
{
	if (!Obj.IsValid()) return false;
	TSharedPtr<FJsonValue> Val = Obj->TryGetField(Field);
	if (!Val.IsValid() || Val->IsNull()) return false;

	switch (Val->Type)
	{
		case EJson::String:
			OutValue = Val->AsString();
			return true;
		case EJson::Number:
			OutValue = FString::SanitizeFloat(Val->AsNumber());
			return true;
		case EJson::Boolean:
			OutValue = Val->AsBool() ? TEXT("true") : TEXT("false");
			return true;
		case EJson::Object:
		{
			const TSharedPtr<FJsonObject>& ObjVal = Val->AsObject();
			if (!ObjVal.IsValid()) return false;
			TArray<FString> Parts;
			for (const auto& Pair : ObjVal->Values)
			{
				if (!Pair.Value.IsValid()) continue;
				FString Key(*Pair.Key);
				FString Inner;
				if (!CoerceJsonValueToString(ObjVal, Key, Inner)) continue;
				if (Key.Len() == 1) Key = Key.ToUpper();
				Parts.Add(FString::Printf(TEXT("%s=%s"), *Key, *Inner));
			}
			if (Parts.Num() == 0) return false;
			OutValue = FString::Printf(TEXT("(%s)"), *FString::Join(Parts, TEXT(",")));
			return true;
		}
		case EJson::Array:
		{
			const TArray<TSharedPtr<FJsonValue>>& Arr = Val->AsArray();
			TArray<FString> Parts;
			for (const TSharedPtr<FJsonValue>& Elem : Arr)
			{
				if (!Elem.IsValid()) continue;
				if (Elem->Type == EJson::Number)      Parts.Add(FString::SanitizeFloat(Elem->AsNumber()));
				else if (Elem->Type == EJson::String) Parts.Add(Elem->AsString());
				else if (Elem->Type == EJson::Boolean)Parts.Add(Elem->AsBool() ? TEXT("true") : TEXT("false"));
			}
			if (Parts.Num() == 0) return false;
			OutValue = FString::Join(Parts, TEXT(","));
			return true;
		}
		default:
			return false;
	}
}

void HandleAddComponent(const FString& BpPath, const FString& ComponentClass, const FString& ComponentName, const FString& AttachTo, FString& OutJsonString, FString& OutError)
{
	UBlueprint* TargetBlueprint = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(BpPath));
	if (!TargetBlueprint)
	{
		OutError = FString::Printf(TEXT("Could not load Blueprint at path: %s"), *BpPath);
		return;
	}

	{
		UClass* BPClass = TargetBlueprint->GeneratedClass ? TargetBlueprint->GeneratedClass.Get() : TargetBlueprint->ParentClass.Get();
		if (!BPClass || !BPClass->IsChildOf(AActor::StaticClass()))
		{
			OutError = FString::Printf(
				TEXT("add_component only works on Actor Blueprints. '%s' is a %s, which has no component hierarchy. For non-Actor Blueprints (ActorComponent / Widget / UObject) build behaviour via variables, functions and the event graph, not components."),
				*BpPath, BPClass ? *BPClass->GetName() : TEXT("Blueprint of unresolved class"));
			return;
		}
		if (!TargetBlueprint->SimpleConstructionScript)
		{
			OutError = FString::Printf(TEXT("Actor Blueprint '%s' has no SimpleConstructionScript yet — compile it first (compile_blueprint), then add components."), *BpPath);
			return;
		}
	}

	FString ResolvedClass = ComponentClass;
	{
		static TMap<FString, FString> Aliases;
		if (Aliases.Num() == 0)
		{
			Aliases.Add(TEXT("boxcollision"), TEXT("BoxComponent"));
			Aliases.Add(TEXT("spherecollision"), TEXT("SphereComponent"));
			Aliases.Add(TEXT("capsulecollision"), TEXT("CapsuleComponent"));
			Aliases.Add(TEXT("arrowcomponent"), TEXT("ArrowComponent"));
			Aliases.Add(TEXT("staticmesh"), TEXT("StaticMeshComponent"));
			Aliases.Add(TEXT("skeletalmesh"), TEXT("SkeletalMeshComponent"));
			Aliases.Add(TEXT("box"), TEXT("BoxComponent"));
			Aliases.Add(TEXT("sphere"), TEXT("SphereComponent"));
			Aliases.Add(TEXT("capsule"), TEXT("CapsuleComponent"));
			Aliases.Add(TEXT("camera"), TEXT("CameraComponent"));
			Aliases.Add(TEXT("audio"), TEXT("AudioComponent"));
			Aliases.Add(TEXT("particlesystem"), TEXT("ParticleSystemComponent"));
			Aliases.Add(TEXT("pointlight"), TEXT("PointLightComponent"));
			Aliases.Add(TEXT("spotlight"), TEXT("SpotLightComponent"));
			Aliases.Add(TEXT("directionallight"), TEXT("DirectionalLightComponent"));
			Aliases.Add(TEXT("widget"), TEXT("WidgetComponent"));
			Aliases.Add(TEXT("widgetcomponent"), TEXT("WidgetComponent"));
		}
		FString* Found = Aliases.Find(ComponentClass.ToLower());
		if (Found) ResolvedClass = *Found;
	}

	UClass* FoundComponentClass = FindFirstObjectSafe<UClass>(*ResolvedClass);
	if (!FoundComponentClass)
	{
		FoundComponentClass = FindFirstObjectSafe<UClass>(*(ResolvedClass + TEXT("Component")));
	}

	if (!FoundComponentClass)
	{
		FString ResolvedBpPath;
		if (ResolvedClass.StartsWith(TEXT("/")))
		{
			ResolvedBpPath = ResolvedClass.Contains(TEXT(".")) ? ResolvedClass : ResolvedClass + TEXT(".") + FPackageName::GetShortName(ResolvedClass);
		}
		else
		{
			IAssetRegistry& AR = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
			FARFilter Filter;
			Filter.bRecursivePaths = true;
			Filter.PackagePaths.Add(FName(TEXT("/Game")));
			Filter.ClassPaths.Add(FTopLevelAssetPath(TEXT("/Script/Engine"), TEXT("Blueprint")));
			TArray<FAssetData> Candidates;
			AR.GetAssets(Filter, Candidates);
			for (const FAssetData& AD : Candidates)
			{
				if (AD.AssetName.ToString().Equals(ResolvedClass, ESearchCase::IgnoreCase))
				{
					ResolvedBpPath = AD.GetObjectPathString();
					break;
				}
			}
		}
		if (!ResolvedBpPath.IsEmpty())
		{
			if (UBlueprint* FoundBP = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(ResolvedBpPath)))
			{
				if (FoundBP->GeneratedClass && FoundBP->GeneratedClass->IsChildOf(UActorComponent::StaticClass()))
				{
					FoundComponentClass = FoundBP->GeneratedClass;
				}
			}
		}
	}

	if (!FoundComponentClass)
	{
		OutError = FString::Printf(TEXT("Component class '%s' could not be found. Did you mean one of: BoxComponent, SphereComponent, CapsuleComponent, StaticMeshComponent, SkeletalMeshComponent, CameraComponent, AudioComponent, PointLightComponent, SpotLightComponent, DirectionalLightComponent, WidgetComponent, StateTreeAIComponent, BehaviorTreeComponent? Or pass a Blueprint asset name (e.g. 'BP_InventoryComponent') / full /Game path. Use the EXACT class name."), *ComponentClass);
		return;
	}

	if (!FoundComponentClass->IsChildOf(UActorComponent::StaticClass()))
	{
		const FString FoundClassName = FoundComponentClass->GetName();
		const bool bIsSubsystem = FoundClassName.Contains(TEXT("Subsystem"));
		const bool bIsInputMapping = FoundClassName.Contains(TEXT("InputMappingContext")) || FoundClassName.Contains(TEXT("InputAction"));

		if (bIsSubsystem)
		{
			if (FoundClassName.Contains(TEXT("EnhancedInputLocalPlayerSubsystem")))
			{
				OutError = FString::Printf(
					TEXT("'%s' is a LOCAL PLAYER SUBSYSTEM, not an Actor Component. Subsystems are NOT added to actors — the engine creates them automatically per local player. ")
					TEXT("CORRECT pattern to register an Input Mapping Context (do NOT add anything via add_component): build_blueprint_graph on EventGraph with nodes ")
					TEXT("[{id:'evCtrl',handle:'ev.ReceiveControllerChanged'},{id:'castPC',handle:'k2.Cast To PlayerController'},{id:'getSubsys',handle:'k2.Get EnhancedInputLocalPlayerSubsystem'},{id:'addMC',handle:'fn.EnhancedInputLocalPlayerSubsystem.AddMappingContext'}] and ")
					TEXT("connections [{from:'evCtrl',from_pin:'then',to:'castPC',to_pin:'execute'},{from:'evCtrl',from_pin:'NewController',to:'castPC',to_pin:'Object'},{from:'castPC',from_pin:'then',to:'addMC',to_pin:'execute'},{from:'getSubsys',from_pin:'ReturnValue',to:'addMC',to_pin:'self'}] and defaults ")
					TEXT("[{node_id:'addMC',pin_name:'MappingContext',value:'/Game/Input/IMC_PlayerControls'},{node_id:'addMC',pin_name:'Priority',value:0}]. ")
					TEXT("Call get_handle_reference(sections='Enhanced Input, IMC Registration Pattern (EXACT — copy this, do NOT modify)') for the canonical pattern."),
					*FoundClassName);
			}
			else
			{
				OutError = FString::Printf(
					TEXT("'%s' is a SUBSYSTEM, not an Actor Component. Subsystems live on the Engine, GameInstance, World, LocalPlayer, or Editor — they are created automatically and are NEVER added as components. ")
					TEXT("Access them via fn.*.GetSubsystem (class=%s) as a pure node in EventGraph, not via add_component."),
					*FoundClassName, *FoundClassName);
			}
			return;
		}

		if (bIsInputMapping)
		{
			OutError = FString::Printf(
				TEXT("'%s' is an INPUT ASSET, not a Component. Use create_input_mapping_context / create_input_action to make the asset, then register it on BeginPlay via Cast To PlayerController -> GetLocalPlayer -> GetSubsystem(EnhancedInputLocalPlayerSubsystem) -> AddMappingContext."),
				*FoundClassName);
			return;
		}

		OutError = FString::Printf(
			TEXT("Class '%s' exists but is not an ActorComponent (it is a '%s'). Only UActorComponent subclasses can be added via add_component. ")
			TEXT("If you want to use this class as data (e.g. a DataAsset), add it as a variable via blueprint(action='add_variable') instead."),
			*ComponentClass, *FoundComponentClass->GetSuperClass()->GetName());
		return;
	}

	if (FoundComponentClass->HasAnyClassFlags(CLASS_Abstract))
	{
		const FString ClassName = FoundComponentClass->GetName();
		FString Suggestion;
		if (ClassName == TEXT("ActorComponent"))
		{
			Suggestion = TEXT("Pick a concrete subclass: 'SceneComponent' (transformable), 'StaticMeshComponent' (mesh), "
			                  "'SphereComponent' / 'BoxComponent' / 'CapsuleComponent' (collision), 'CameraComponent', "
			                  "'AudioComponent', 'WidgetComponent', or pass a project-defined Blueprint/C++ component name.");
		}
		else if (ClassName == TEXT("SceneComponent"))
		{
			Suggestion = TEXT("SceneComponent is the abstract transform base. Pick a concrete subclass: 'StaticMeshComponent', "
			                  "'SkeletalMeshComponent', 'SphereComponent', 'BoxComponent', 'CapsuleComponent', etc.");
		}
		else if (ClassName == TEXT("PrimitiveComponent") || ClassName == TEXT("ShapeComponent"))
		{
			Suggestion = FString::Printf(
				TEXT("'%s' is an abstract base. Pick a concrete subclass like 'StaticMeshComponent' or 'BoxComponent'."),
				*ClassName);
		}
		else
		{
			Suggestion = FString::Printf(
				TEXT("'%s' is abstract — it can't be instantiated. Either pick a concrete subclass, or create one via "
				     "cpp_tools(action='create_actor_component') / blueprint(action='create_blueprint', parent_class='%s')."),
				*ClassName, *ClassName);
		}
		OutError = FString::Printf(TEXT("Component class '%s' is abstract. %s"), *ClassName, *Suggestion);
		return;
	}

	if (TargetBlueprint->SimpleConstructionScript)
	{
		for (USCS_Node* ExistingNode : TargetBlueprint->SimpleConstructionScript->GetAllNodes())
		{
			if (ExistingNode && ExistingNode->GetVariableName().ToString().Equals(ComponentName, ESearchCase::IgnoreCase))
			{
				const FString ExistingClass = ExistingNode->ComponentTemplate
					? ExistingNode->ComponentTemplate->GetClass()->GetName()
					: TEXT("(unknown)");
				if (ExistingClass.Equals(FoundComponentClass->GetName(), ESearchCase::IgnoreCase))
				{
					OutError = FString::Printf(
						TEXT("Component '%s' already exists on '%s' with the same class (%s). Skip the add_component or call edit_component_property to modify it."),
						*ComponentName, *BpPath, *ExistingClass);
				}
				else
				{
					OutError = FString::Printf(
						TEXT("Component '%s' already exists on '%s' as a %s — refusing to recreate as %s. Call remove_component(component_name='%s') first if you really want to replace it, or rename_component to free up the name."),
						*ComponentName, *BpPath, *ExistingClass, *FoundComponentClass->GetName(), *ComponentName);
				}
				return;
			}
		}
	}

	if (UObject* StaleTransient = StaticFindObject(nullptr, GetTransientPackage(), *ComponentName))
	{
		if (StaleTransient->GetClass() != FoundComponentClass)
		{
			StaleTransient->Rename(nullptr, GetTransientPackage(),
				REN_DoNotDirty | REN_DontCreateRedirectors | REN_NonTransactional);
		}
	}

	UActorComponent* NewComponentInstance = NewObject<UActorComponent>(GetTransientPackage(), FoundComponentClass, FName(*ComponentName));
	if (!NewComponentInstance)
	{
		OutError = FString::Printf(TEXT("Failed to create new component instance for '%s'."), *ComponentName);
		return;
	}

	USCS_Node* ParentSCSNode = nullptr;
	USceneComponent* NativeParentComponent = nullptr;
	if (!AttachTo.IsEmpty())
	{
		if (TargetBlueprint->SimpleConstructionScript)
		{
			for (USCS_Node* Node : TargetBlueprint->SimpleConstructionScript->GetAllNodes())
			{
				if (Node && Node->GetVariableName().ToString().Equals(AttachTo, ESearchCase::IgnoreCase))
				{ ParentSCSNode = Node; break; }
			}
		}
		if (!ParentSCSNode && TargetBlueprint->ParentClass)
		{
			if (UBlueprint* ParentBP = Cast<UBlueprint>(TargetBlueprint->ParentClass->ClassGeneratedBy))
			{
				if (ParentBP->SimpleConstructionScript)
				{
					for (USCS_Node* Node : ParentBP->SimpleConstructionScript->GetAllNodes())
					{
						if (Node && Node->GetVariableName().ToString().Equals(AttachTo, ESearchCase::IgnoreCase))
						{ ParentSCSNode = Node; break; }
					}
				}
			}
		}
		if (!ParentSCSNode && TargetBlueprint->ParentClass)
		{
			UClass* SearchClass = TargetBlueprint->ParentClass;
			while (SearchClass && SearchClass != UObject::StaticClass())
			{
				for (TFieldIterator<FObjectProperty> It(SearchClass, EFieldIteratorFlags::ExcludeSuper); It; ++It)
				{
					if (It->GetName().Equals(AttachTo, ESearchCase::IgnoreCase) &&
						It->PropertyClass && It->PropertyClass->IsChildOf(USceneComponent::StaticClass()))
					{
						if (AActor* CDO = Cast<AActor>(SearchClass->GetDefaultObject()))
						{
							UObject* Obj = It->GetObjectPropertyValue_InContainer(CDO);
							NativeParentComponent = Cast<USceneComponent>(Obj);
						}
						break;
					}
				}
				if (NativeParentComponent) break;
				SearchClass = SearchClass->GetSuperClass();
			}
		}
		if (!ParentSCSNode && !NativeParentComponent && TargetBlueprint->ParentClass)
		{
			if (AActor* CDO = Cast<AActor>(TargetBlueprint->ParentClass->GetDefaultObject()))
			{
				TArray<UActorComponent*> CDOComps;
				CDO->GetComponents(CDOComps);
				for (UActorComponent* Comp : CDOComps)
				{
					if (USceneComponent* SC = Cast<USceneComponent>(Comp))
					{
						if (SC->GetName().Equals(AttachTo, ESearchCase::IgnoreCase))
						{
							NativeParentComponent = SC;
							break;
						}
					}
				}
				if (!NativeParentComponent && AttachTo.Equals(TEXT("RootComponent"), ESearchCase::IgnoreCase))
				{
					NativeParentComponent = CDO->GetRootComponent();
				}
			}
		}
	}

	TArray<UActorComponent*> ComponentsToAdd;
	ComponentsToAdd.Add(NewComponentInstance);
	FKismetEditorUtilities::FAddComponentsToBlueprintParams Params;
	Params.OptionalNewRootNode = ParentSCSNode;
	FKismetEditorUtilities::AddComponentsToBlueprint(TargetBlueprint, ComponentsToAdd, Params);

	if (NativeParentComponent && TargetBlueprint->SimpleConstructionScript)
	{
		for (USCS_Node* Node : TargetBlueprint->SimpleConstructionScript->GetAllNodes())
		{
			if (Node && Node->GetVariableName().ToString().Equals(ComponentName, ESearchCase::IgnoreCase))
			{
				Node->SetParent(NativeParentComponent);
				TargetBlueprint->MarkPackageDirty();
				break;
			}
		}
	}

	const bool bAttachSuccess = ParentSCSNode != nullptr || NativeParentComponent != nullptr;
	TSharedPtr<FJsonObject> ResultObject = MakeShareable(new FJsonObject);
	ResultObject->SetBoolField(TEXT("success"), true);
	ResultObject->SetStringField(TEXT("component_name"), ComponentName);
	ResultObject->SetStringField(TEXT("component_class"), ComponentClass);
	ResultObject->SetStringField(TEXT("blueprint_path"), BpPath);
	if (!AttachTo.IsEmpty())
	{
		if (bAttachSuccess)
		{
			ResultObject->SetStringField(TEXT("attached_to"), AttachTo);
		}
		else
		{
			TArray<FString> Candidates;
			if (TargetBlueprint->SimpleConstructionScript)
			{
				for (USCS_Node* Node : TargetBlueprint->SimpleConstructionScript->GetAllNodes())
				{
					if (Node) Candidates.Add(Node->GetVariableName().ToString());
				}
			}
			if (TargetBlueprint->ParentClass)
			{
				if (AActor* CDO = Cast<AActor>(TargetBlueprint->ParentClass->GetDefaultObject()))
				{
					TArray<UActorComponent*> CDOComps;
					CDO->GetComponents(CDOComps);
					for (UActorComponent* Comp : CDOComps)
					{
						if (Cast<USceneComponent>(Comp)) Candidates.AddUnique(Comp->GetName());
					}
				}
			}
			ResultObject->SetStringField(TEXT("attached_to"),
				FString::Printf(TEXT("WARNING: parent '%s' not found — added at root. Valid attach targets: %s"),
					*AttachTo, Candidates.Num() > 0 ? *FString::Join(Candidates, TEXT(", ")) : TEXT("(none — actor has no scene components)")));
		}
	}
	ResultObject->SetStringField(TEXT("message"), FString::Printf(TEXT("Added '%s' to '%s'"), *ComponentName, *BpPath));

	FString ResultString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResultString);
	FJsonSerializer::Serialize(ResultObject.ToSharedRef(), Writer);
	OutJsonString = ResultString;
}

void HandleAddComponentFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
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
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString CompClass = BatchToolHelper::GetItemString(Item, TEXT("component_class"), TEXT("component_type"));
			if (CompClass.IsEmpty()) CompClass = BatchToolHelper::GetItemString(Item, TEXT("class"));
			FString CompName = BatchToolHelper::GetItemString(Item, TEXT("component_name"), TEXT("name"));
			FString AttachTo;
			if (!Item->TryGetStringField(TEXT("attach_to"), AttachTo))
				if (!Item->TryGetStringField(TEXT("attach_parent"), AttachTo))
					if (!Item->TryGetStringField(TEXT("parent"), AttachTo))
						Item->TryGetStringField(TEXT("parent_component"), AttachTo);
			if (CompClass.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing component_class")); continue; }
			if (CompName.IsEmpty()) CompName = CompClass;
			FString ItemOut, ItemErr;
			HandleAddComponent(BpPath, CompClass, CompName, AttachTo, ItemOut, ItemErr);
			if (ItemErr.IsEmpty())
			{
				auto Extra = MakeShared<FJsonObject>();
				Extra->SetStringField(TEXT("component_name"), CompName);
				Extra->SetStringField(TEXT("component_class"), CompClass);
				Batch.AddSuccess(i, Extra);
			}
			else Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJsonString);
		return;
	}

	FString CompClass, CompName, AttachTo;
	if (!Args->TryGetStringField(TEXT("component_class"), CompClass))
		if (!Args->TryGetStringField(TEXT("component_type"), CompClass))
			Args->TryGetStringField(TEXT("class"), CompClass);
	if (!Args->TryGetStringField(TEXT("component_name"), CompName))
		Args->TryGetStringField(TEXT("name"), CompName);
	if (!Args->TryGetStringField(TEXT("attach_to"), AttachTo))
		if (!Args->TryGetStringField(TEXT("attach_parent"), AttachTo))
			if (!Args->TryGetStringField(TEXT("parent"), AttachTo))
				Args->TryGetStringField(TEXT("parent_component"), AttachTo);
	HandleAddComponent(BpPath, CompClass, CompName, AttachTo, OutJsonString, OutError);
}

FString HandleEditComponentProperty(const FString& BpPath, const FString& ComponentName, const FString& PropertyName, const FString& PropertyValue)
{
	UBlueprint* TargetBlueprint = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(BpPath));
	if (!TargetBlueprint)
	{
		return FString::Printf(TEXT("{\"success\": false, \"error\": \"Could not load Blueprint at path: %s\"}"), *BpPath);
	}

	UActorComponent* ComponentTemplate = nullptr;
	if (TargetBlueprint->SimpleConstructionScript)
	{
		for (USCS_Node* Node : TargetBlueprint->SimpleConstructionScript->GetAllNodes())
		{
			if (Node && Node->GetVariableName().ToString().Equals(ComponentName, ESearchCase::IgnoreCase))
			{ ComponentTemplate = Node->ComponentTemplate; break; }
		}
		if (!ComponentTemplate)
		{
			for (USCS_Node* Node : TargetBlueprint->SimpleConstructionScript->GetAllNodes())
			{
				if (Node && Node->ComponentTemplate &&
					Node->ComponentTemplate->GetClass()->GetName().Equals(ComponentName, ESearchCase::IgnoreCase))
				{ ComponentTemplate = Node->ComponentTemplate; break; }
			}
		}
	}

	if (!ComponentTemplate)
	{
		UBlueprint* WalkBP = TargetBlueprint;
		USCS_Node* InheritedNode = nullptr;
		UBlueprint* OwningParentBP = nullptr;

		WalkBP = (WalkBP->ParentClass && WalkBP->ParentClass->ClassGeneratedBy)
			? Cast<UBlueprint>(WalkBP->ParentClass->ClassGeneratedBy) : nullptr;

		while (WalkBP && !InheritedNode)
		{
			if (USimpleConstructionScript* SCS = WalkBP->SimpleConstructionScript)
			{
				for (USCS_Node* Node : SCS->GetAllNodes())
				{
					if (!Node || !Node->ComponentTemplate) continue;
					if (Node->GetVariableName().ToString().Equals(ComponentName, ESearchCase::IgnoreCase) ||
						Node->ComponentTemplate->GetClass()->GetName().Equals(ComponentName, ESearchCase::IgnoreCase))
					{
						InheritedNode = Node;
						OwningParentBP = WalkBP;
						break;
					}
				}
			}
			if (!InheritedNode)
			{
				WalkBP = (WalkBP->ParentClass && WalkBP->ParentClass->ClassGeneratedBy)
					? Cast<UBlueprint>(WalkBP->ParentClass->ClassGeneratedBy) : nullptr;
			}
		}

		if (InheritedNode && OwningParentBP)
		{
			UInheritableComponentHandler* ICH = TargetBlueprint->GetInheritableComponentHandler(true);
			if (ICH)
			{
				FComponentKey Key(InheritedNode);
				ComponentTemplate = ICH->GetOverridenComponentTemplate(Key);
				if (!ComponentTemplate)
				{
					ComponentTemplate = ICH->CreateOverridenComponentTemplate(Key);
				}
			}
		}
	}
	if (!ComponentTemplate)
	{
		UClass* SearchClass = TargetBlueprint->GeneratedClass;
		if (!SearchClass) SearchClass = TargetBlueprint->ParentClass;
		if (SearchClass)
		{
			if (AActor* CDO = Cast<AActor>(SearchClass->GetDefaultObject()))
			{
				TArray<UActorComponent*> Comps;
				CDO->GetComponents(Comps);
				for (FProperty* Prop = SearchClass->PropertyLink; Prop; Prop = Prop->PropertyLinkNext)
				{
					FObjectProperty* ObjProp = CastField<FObjectProperty>(Prop);
					if (!ObjProp) continue;
					if (Prop->GetName().Equals(ComponentName, ESearchCase::IgnoreCase) ||
						Prop->GetName().Contains(ComponentName, ESearchCase::IgnoreCase))
					{
						UObject* Value = ObjProp->GetObjectPropertyValue_InContainer(CDO);
						if (UActorComponent* C = Cast<UActorComponent>(Value))
						{ ComponentTemplate = C; break; }
					}
				}
				if (!ComponentTemplate)
					for (UActorComponent* C : Comps)
						if (C->GetName().Equals(ComponentName, ESearchCase::IgnoreCase)) { ComponentTemplate = C; break; }
				if (!ComponentTemplate)
					for (UActorComponent* C : Comps)
						if (C->GetName().Contains(ComponentName, ESearchCase::IgnoreCase)) { ComponentTemplate = C; break; }
				if (!ComponentTemplate)
					for (UActorComponent* C : Comps)
						if (C->GetClass()->GetName().Equals(ComponentName, ESearchCase::IgnoreCase)) { ComponentTemplate = C; break; }
				if (!ComponentTemplate)
					for (UActorComponent* C : Comps)
						if (C->GetClass()->GetName().Contains(ComponentName, ESearchCase::IgnoreCase)) { ComponentTemplate = C; break; }
				if (!ComponentTemplate)
				{
					FString Stripped = ComponentName;
					while (Stripped.Len() > 1 && FChar::IsDigit(Stripped[Stripped.Len() - 1]))
					{
						Stripped = Stripped.LeftChop(1);
					}
					if (!Stripped.Equals(ComponentName) && Stripped.Len() > 1)
					{
						for (FProperty* Prop = SearchClass->PropertyLink; Prop; Prop = Prop->PropertyLinkNext)
						{
							FObjectProperty* ObjProp = CastField<FObjectProperty>(Prop);
							if (!ObjProp) continue;
							if (Prop->GetName().Equals(Stripped, ESearchCase::IgnoreCase))
							{
								UObject* Value = ObjProp->GetObjectPropertyValue_InContainer(CDO);
								if (UActorComponent* C = Cast<UActorComponent>(Value))
								{ ComponentTemplate = C; break; }
							}
						}
					}
				}
			}
		}
	}
	if (!ComponentTemplate)
	{
		TArray<FString> Available;
		if (TargetBlueprint->SimpleConstructionScript)
			for (USCS_Node* N : TargetBlueprint->SimpleConstructionScript->GetAllNodes())
				if (N) Available.Add(N->GetVariableName().ToString() + TEXT("(SCS)"));

		UBlueprint* WalkParent = TargetBlueprint->ParentClass && TargetBlueprint->ParentClass->ClassGeneratedBy
			? Cast<UBlueprint>(TargetBlueprint->ParentClass->ClassGeneratedBy) : nullptr;
		while (WalkParent)
		{
			if (USimpleConstructionScript* SCS = WalkParent->SimpleConstructionScript)
			{
				for (USCS_Node* N : SCS->GetAllNodes())
				{
					if (N) Available.Add(FString::Printf(TEXT("%s(SCS<-%s)"), *N->GetVariableName().ToString(), *WalkParent->GetName()));
				}
			}
			WalkParent = WalkParent->ParentClass && WalkParent->ParentClass->ClassGeneratedBy
				? Cast<UBlueprint>(WalkParent->ParentClass->ClassGeneratedBy) : nullptr;
		}

		UClass* ErrClass = TargetBlueprint->GeneratedClass ? TargetBlueprint->GeneratedClass : TargetBlueprint->ParentClass;
		if (ErrClass)
			if (AActor* CDO = Cast<AActor>(ErrClass->GetDefaultObject()))
			{
				TArray<UActorComponent*> Comps; CDO->GetComponents(Comps);
				for (UActorComponent* C : Comps) Available.Add(C->GetName() + TEXT("(") + C->GetClass()->GetName() + TEXT(")"));
			}
		return FString::Printf(TEXT("{\"success\": false, \"error\": \"Component '%s' not found in Blueprint '%s'\", \"available\": \"%s\"}"),
			*ComponentName, *BpPath, *FString::Join(Available, TEXT(", ")));
	}

	FProperty* Property = nullptr;
	void* PropertyContainer = ComponentTemplate;
	if (PropertyName.Contains(TEXT(".")))
	{
		TArray<FString> Parts;
		PropertyName.ParseIntoArray(Parts, TEXT("."));
		UStruct* CurrentStruct = ComponentTemplate->GetClass();
		void* CurrentContainer = ComponentTemplate;
		for (int32 i = 0; i < Parts.Num(); i++)
		{
			FProperty* PartProp = CurrentStruct->FindPropertyByName(FName(*Parts[i]));
			if (!PartProp) break;
			if (i == Parts.Num() - 1)
			{
				Property = PartProp;
				PropertyContainer = CurrentContainer;
			}
			else
			{
				FStructProperty* StructProp = CastField<FStructProperty>(PartProp);
				if (!StructProp) break;
				CurrentContainer = StructProp->ContainerPtrToValuePtr<void>(CurrentContainer);
				CurrentStruct = StructProp->Struct;
			}
		}
	}
	else
	{
		Property = ComponentTemplate->GetClass()->FindPropertyByName(FName(*PropertyName));
	}
	if (!Property)
	{
		FString AltName;
		{
			FString Stripped = PropertyName;
			if (Stripped.StartsWith(TEXT("Set "), ESearchCase::IgnoreCase) ||
				Stripped.StartsWith(TEXT("Set"), ESearchCase::IgnoreCase))
			{
				Stripped = Stripped.RightChop(Stripped.StartsWith(TEXT("Set "), ESearchCase::IgnoreCase) ? 4 : 3);
				Stripped.RemoveSpacesInline();
				TArray<FString> Candidates = { TEXT("b") + Stripped, Stripped, TEXT("bIs") + Stripped };
				for (const FString& C : Candidates)
				{
					if (ComponentTemplate->GetClass()->FindPropertyByName(FName(*C)))
					{ AltName = C; break; }
				}
			}
		}

		TArray<FString> Suggestions;
		if (!AltName.IsEmpty()) Suggestions.Add(AltName);
		for (TFieldIterator<FProperty> PropIt(ComponentTemplate->GetClass()); PropIt; ++PropIt)
		{
			FString PropName = PropIt->GetName();
			if (PropName.Contains(PropertyName, ESearchCase::IgnoreCase) && !Suggestions.Contains(PropName))
				Suggestions.Add(PropName);
			if (Suggestions.Num() >= 8) break;
		}
		FString SuggestionStr = FString::Join(Suggestions, TEXT(", "));
		FString Hint;
		if (!AltName.IsEmpty())
		{
			Hint = FString::Printf(TEXT(" Did you mean the UPROPERTY '%s' instead of the setter function name?"), *AltName);
		}
		return FString::Printf(TEXT("{\"success\": false, \"error\": \"Property '%s' not found on component '%s'.%s\", \"suggestions\": \"%s\"}"),
			*PropertyName, *ComponentName, *Hint, *SuggestionStr);
	}

	bool bSuccess = false;
	void* PropertyData = Property->ContainerPtrToValuePtr<void>(PropertyContainer);

	if (PropertyData && CastField<FClassProperty>(Property))
	{
		FClassProperty* CP = CastField<FClassProperty>(Property);
		FString Cleaned = PropertyValue;
		Cleaned.TrimStartAndEndInline();
		int32 QuoteStart, QuoteEnd;
		if (Cleaned.FindChar(TEXT('\''), QuoteStart) && Cleaned.FindLastChar(TEXT('\''), QuoteEnd) && QuoteEnd > QuoteStart)
		{
			Cleaned = Cleaned.Mid(QuoteStart + 1, QuoteEnd - QuoteStart - 1);
		}

		UClass* ResolvedClass = nullptr;
		ResolvedClass = LoadObject<UClass>(nullptr, *Cleaned);
		if (!ResolvedClass)
		{
			if (UBlueprint* BP = LoadObject<UBlueprint>(nullptr, *Cleaned))
				if (BP->GeneratedClass) ResolvedClass = BP->GeneratedClass;
		}
		if (!ResolvedClass)
		{
			FString WithC = Cleaned + TEXT("_C");
			ResolvedClass = LoadObject<UClass>(nullptr, *WithC);
		}
		if (!ResolvedClass)
		{
			if (UObject* Asset = LoadObject<UObject>(nullptr, *Cleaned))
			{
				if (UBlueprint* BP = Cast<UBlueprint>(Asset))
					if (BP->GeneratedClass) ResolvedClass = BP->GeneratedClass;
			}
		}
		if (ResolvedClass)
		{
			CP->SetObjectPropertyValue(PropertyData, ResolvedClass);
			bSuccess = true;
		}
	}

	if (!bSuccess && PropertyData)
	{
		if (Property->ImportText_Direct(*PropertyValue, PropertyData, nullptr, PPF_None))
		{
			if (FClassProperty* CP = CastField<FClassProperty>(Property))
			{
				UObject* Stored = CP->GetObjectPropertyValue(PropertyData);
				if (Stored) bSuccess = true;
			}
			else
			{
				bSuccess = true;
			}
		}
	}

	if (!bSuccess)
	{
		if (FStructProperty* StructProp = CastField<FStructProperty>(Property))
		{
			auto ParseVecLoose = [](const FString& S, FVector& Out) -> bool
			{
				if (S.IsEmpty()) return false;
				if (Out.InitFromString(S)) return true;
				FString Stripped = S.TrimStartAndEnd();
				if ((Stripped.StartsWith(TEXT("(")) && Stripped.EndsWith(TEXT(")"))) ||
					(Stripped.StartsWith(TEXT("[")) && Stripped.EndsWith(TEXT("]"))))
					Stripped = Stripped.Mid(1, Stripped.Len() - 2);
				TArray<FString> Parts;
				Stripped.ParseIntoArray(Parts, TEXT(","));
				if (Parts.Num() == 3)
				{
					Out.X = FCString::Atof(*Parts[0].TrimStartAndEnd());
					Out.Y = FCString::Atof(*Parts[1].TrimStartAndEnd());
					Out.Z = FCString::Atof(*Parts[2].TrimStartAndEnd());
					return true;
				}
				return false;
			};
			auto ParseRotLoose = [](const FString& S, FRotator& Out) -> bool
			{
				if (S.IsEmpty()) return false;
				if (Out.InitFromString(S)) return true;
				FString Stripped = S.TrimStartAndEnd();
				if ((Stripped.StartsWith(TEXT("(")) && Stripped.EndsWith(TEXT(")"))) ||
					(Stripped.StartsWith(TEXT("[")) && Stripped.EndsWith(TEXT("]"))))
					Stripped = Stripped.Mid(1, Stripped.Len() - 2);
				TArray<FString> Parts;
				Stripped.ParseIntoArray(Parts, TEXT(","));
				if (Parts.Num() == 3)
				{
					Out.Pitch = FCString::Atof(*Parts[0].TrimStartAndEnd());
					Out.Yaw   = FCString::Atof(*Parts[1].TrimStartAndEnd());
					Out.Roll  = FCString::Atof(*Parts[2].TrimStartAndEnd());
					return true;
				}
				return false;
			};

			if (StructProp->Struct == TBaseStructure<FVector>::Get())
			{
				FVector VectorValue;
				if (ParseVecLoose(PropertyValue, VectorValue))
				{
					*static_cast<FVector*>(PropertyData) = VectorValue;
					bSuccess = true;
				}
			}
			else if (StructProp->Struct == TBaseStructure<FRotator>::Get())
			{
				FRotator RotatorValue;
				if (ParseRotLoose(PropertyValue, RotatorValue))
				{
					*static_cast<FRotator*>(PropertyData) = RotatorValue;
					bSuccess = true;
				}
			}
			else if (StructProp->Struct == TBaseStructure<FLinearColor>::Get())
			{
				FString Stripped = PropertyValue.TrimStartAndEnd();
				if ((Stripped.StartsWith(TEXT("(")) && Stripped.EndsWith(TEXT(")"))) ||
					(Stripped.StartsWith(TEXT("[")) && Stripped.EndsWith(TEXT("]"))))
					Stripped = Stripped.Mid(1, Stripped.Len() - 2);
				TArray<FString> Parts;
				Stripped.ParseIntoArray(Parts, TEXT(","));
				if (Parts.Num() == 3 || Parts.Num() == 4)
				{
					FLinearColor LC;
					LC.R = FCString::Atof(*Parts[0].TrimStartAndEnd());
					LC.G = FCString::Atof(*Parts[1].TrimStartAndEnd());
					LC.B = FCString::Atof(*Parts[2].TrimStartAndEnd());
					LC.A = (Parts.Num() == 4) ? FCString::Atof(*Parts[3].TrimStartAndEnd()) : 1.0f;
					*static_cast<FLinearColor*>(PropertyData) = LC;
					bSuccess = true;
				}
			}
		}
	}

	if (!bSuccess && PropertyData)
	{
		UEnum* EnumObj = nullptr;
		FNumericProperty* UnderlyingNumeric = nullptr;
		if (FEnumProperty* EP = CastField<FEnumProperty>(Property))
		{
			EnumObj = EP->GetEnum();
			UnderlyingNumeric = EP->GetUnderlyingProperty();
		}
		else if (FByteProperty* ByteProp = CastField<FByteProperty>(Property))
		{
			if (ByteProp->Enum) { EnumObj = ByteProp->Enum; UnderlyingNumeric = ByteProp; }
		}

		if (EnumObj && UnderlyingNumeric)
		{
			const FString Trimmed = PropertyValue.TrimStartAndEnd();
			int64 Resolved = INDEX_NONE;
			for (int32 i = 0; i < EnumObj->NumEnums() - 1; ++i)
			{
				const FString NameShort = EnumObj->GetNameStringByIndex(i);
				const FString DisplayName = EnumObj->GetDisplayNameTextByIndex(i).ToString();
				if (NameShort.Equals(Trimmed, ESearchCase::IgnoreCase) ||
					DisplayName.Equals(Trimmed, ESearchCase::IgnoreCase) ||
					EnumObj->GetNameByIndex(i).ToString().Equals(Trimmed, ESearchCase::IgnoreCase))
				{
					Resolved = EnumObj->GetValueByIndex(i);
					break;
				}
			}
			if (Resolved != INDEX_NONE)
			{
				if (CastField<FEnumProperty>(Property))
				{
					UnderlyingNumeric->SetIntPropertyValue(PropertyData, Resolved);
				}
				else if (FByteProperty* ByteProp = CastField<FByteProperty>(Property))
				{
					ByteProp->SetPropertyValue(PropertyData, (uint8)Resolved);
				}
				bSuccess = true;
			}
		}
	}

	if (!bSuccess)
	{
		FString Hint = TEXT("Use 'x,y,z' format for vectors/rotators (e.g. '0,0,-97') or '(X=0,Y=0,Z=-97)'.");
		if (FEnumProperty* EP = CastField<FEnumProperty>(Property))
		{
			TArray<FString> Names;
			if (UEnum* E = EP->GetEnum())
				for (int32 i = 0; i < E->NumEnums() - 1 && Names.Num() < 8; ++i)
					Names.Add(E->GetNameStringByIndex(i));
			Hint = FString::Printf(TEXT("Property is enum '%s' — pass an enumerator short name (e.g. %s)."),
				*(EP->GetEnum() ? EP->GetEnum()->GetName() : FString(TEXT("?"))),
				*FString::Join(Names, TEXT(" | ")));
		}
		else if (FByteProperty* ByteProp = CastField<FByteProperty>(Property); ByteProp && ByteProp->Enum)
		{
			TArray<FString> Names;
			for (int32 i = 0; i < ByteProp->Enum->NumEnums() - 1 && Names.Num() < 8; ++i)
				Names.Add(ByteProp->Enum->GetNameStringByIndex(i));
			Hint = FString::Printf(TEXT("Property is enum '%s' — pass an enumerator short name (e.g. %s)."),
				*ByteProp->Enum->GetName(), *FString::Join(Names, TEXT(" | ")));
		}
		else if (CastField<FBoolProperty>(Property))
		{
			Hint = TEXT("Property is bool — pass 'true' or 'false'.");
		}
		else if (CastField<FNumericProperty>(Property))
		{
			Hint = TEXT("Property is numeric — pass a number (int or float).");
		}
		else if (CastField<FClassProperty>(Property) || CastField<FSoftClassProperty>(Property))
		{
			Hint = TEXT("Property is a class reference (TSubclassOf) — pass the asset path '/Game/Path/BP_Name.BP_Name' (the Blueprint's generated class is resolved automatically).");
		}
		else if (CastField<FObjectProperty>(Property) || CastField<FSoftObjectProperty>(Property))
		{
			Hint = TEXT("Property is an object/asset reference — pass the asset path '/Game/Path/Asset.Asset'.");
		}
		return FString::Printf(TEXT("{\"success\": false, \"error\": \"Failed to set property '%s' to value '%s'. %s\"}"), *PropertyName, *PropertyValue, *Hint);
	}

	ComponentTemplate->PostEditChange();
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(TargetBlueprint);

	const FString ActualName = ComponentTemplate->GetName();
	FString AppliedVal = UECPProps::ExportPropertyValueString(Property, PropertyData);
	AppliedVal.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
	AppliedVal.ReplaceInline(TEXT("\""), TEXT("\\\""));
	if (!ActualName.Equals(ComponentName, ESearchCase::IgnoreCase))
	{
		return FString::Printf(TEXT("{\"success\": true, \"message\": \"Set property '%s' on component '%s' to '%s'\", \"applied_value\": \"%s\", \"requested_component\": \"%s\", \"matched_component\": \"%s\", \"matched_via\": \"partial_name_or_class\"}"),
			*PropertyName, *ActualName, *PropertyValue, *AppliedVal, *ComponentName, *ActualName);
	}
	return FString::Printf(TEXT("{\"success\": true, \"message\": \"Set property '%s' on component '%s' to '%s'\", \"applied_value\": \"%s\"}"), *PropertyName, *ActualName, *PropertyValue, *AppliedVal);
}

static UActorComponent* FindComponentByName(const FString& BpPathOrActor, const FString& ComponentName, FString& OutError)
{
	if (BpPathOrActor.StartsWith(TEXT("/Game/")) || BpPathOrActor.StartsWith(TEXT("/Engine/")))
	{
		UBlueprint* BP = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(BpPathOrActor));
		if (BP && BP->SimpleConstructionScript)
		{
			for (USCS_Node* Node : BP->SimpleConstructionScript->GetAllNodes())
			{
				if (Node && Node->GetVariableName().ToString().Equals(ComponentName, ESearchCase::IgnoreCase))
					return Node->ComponentTemplate;
			}
			for (USCS_Node* Node : BP->SimpleConstructionScript->GetAllNodes())
			{
				if (Node && Node->ComponentTemplate &&
					Node->ComponentTemplate->GetClass()->GetName().Equals(ComponentName, ESearchCase::IgnoreCase))
					return Node->ComponentTemplate;
			}
		}
		UClass* ClassToSearch = BP ? BP->GeneratedClass : nullptr;
		if (!ClassToSearch && BP && BP->ParentClass)
			ClassToSearch = BP->ParentClass;
		if (ClassToSearch)
		{
			if (AActor* CDO = Cast<AActor>(ClassToSearch->GetDefaultObject()))
			{
				TArray<UActorComponent*> Comps;
				CDO->GetComponents(Comps);

				for (FProperty* Prop = ClassToSearch->PropertyLink; Prop; Prop = Prop->PropertyLinkNext)
				{
					FObjectProperty* ObjProp = CastField<FObjectProperty>(Prop);
					if (!ObjProp) continue;
					UObject* PropValue = ObjProp->GetObjectPropertyValue_InContainer(CDO);
					if (Prop->GetName().Equals(ComponentName, ESearchCase::IgnoreCase) ||
						Prop->GetName().Contains(ComponentName, ESearchCase::IgnoreCase))
					{
						if (UActorComponent* C = Cast<UActorComponent>(PropValue))
							return C;
					}
				}

				for (UActorComponent* C : Comps)
				{
					if (C->GetName().Equals(ComponentName, ESearchCase::IgnoreCase))
						return C;
				}
				for (UActorComponent* C : Comps)
				{
					if (C->GetName().Contains(ComponentName, ESearchCase::IgnoreCase))
						return C;
				}
				for (UActorComponent* C : Comps)
				{
					if (C->GetClass()->GetName().Equals(ComponentName, ESearchCase::IgnoreCase))
						return C;
				}
				for (UActorComponent* C : Comps)
				{
					const FString ClassName = C->GetClass()->GetName();
					if (ClassName.Contains(ComponentName, ESearchCase::IgnoreCase) ||
						ComponentName.Contains(ClassName, ESearchCase::IgnoreCase))
						return C;
				}
			}
		}
		OutError = FString::Printf(TEXT("Component '%s' not found in Blueprint '%s'"), *ComponentName, *BpPathOrActor);
		return nullptr;
	}

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (World)
	{
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (It->GetActorLabel().Equals(BpPathOrActor, ESearchCase::IgnoreCase))
			{
				TArray<UActorComponent*> Comps;
				It->GetComponents(Comps);
				for (UActorComponent* C : Comps)
					if (C->GetName().Equals(ComponentName, ESearchCase::IgnoreCase)) return C;
				for (UActorComponent* C : Comps)
					if (C->GetName().Contains(ComponentName, ESearchCase::IgnoreCase)) return C;
				for (UActorComponent* C : Comps)
					if (C->GetClass()->GetName().Equals(ComponentName, ESearchCase::IgnoreCase)) return C;
				OutError = FString::Printf(TEXT("Component '%s' not found on actor '%s'"), *ComponentName, *BpPathOrActor);
				return nullptr;
			}
		}
	}
	OutError = FString::Printf(TEXT("Target '%s' not found as Blueprint or level actor"), *BpPathOrActor);
	return nullptr;
}

static void NotifyBPModified(const FString& BpPathOrActor)
{
	if (BpPathOrActor.StartsWith(TEXT("/Game/")) || BpPathOrActor.StartsWith(TEXT("/Engine/")))
	{
		UBlueprint* BP = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(BpPathOrActor));
		if (BP) FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
	}
}

void HandleSetComponentCollisionProfile(const FString& BpPathOrActor, const FString& ComponentName,
	const FString& ProfileName, FString& OutJsonString, FString& OutError)
{
	UActorComponent* Comp = FindComponentByName(BpPathOrActor, ComponentName, OutError);
	if (!Comp) return;
	UPrimitiveComponent* PC = Cast<UPrimitiveComponent>(Comp);
	if (!PC) { OutError = FString::Printf(TEXT("Component '%s' is not a PrimitiveComponent"), *ComponentName); return; }

	PC->BodyInstance.SetCollisionProfileName(FName(*ProfileName));
	PC->PostEditChange();
	NotifyBPModified(BpPathOrActor);

	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"component\":\"%s\",\"profile\":\"%s\"}"), *ComponentName, *ProfileName);
}

void HandleSetComponentCollisionResponse(const FString& BpPathOrActor, const FString& ComponentName,
	const FString& ChannelName, const FString& ResponseType, FString& OutJsonString, FString& OutError)
{
	UActorComponent* Comp = FindComponentByName(BpPathOrActor, ComponentName, OutError);
	if (!Comp) return;
	UPrimitiveComponent* PC = Cast<UPrimitiveComponent>(Comp);
	if (!PC) { OutError = FString::Printf(TEXT("Component '%s' is not a PrimitiveComponent"), *ComponentName); return; }

	ECollisionChannel Channel = ECC_WorldStatic;
	UEnum* ChannelEnum = StaticEnum<ECollisionChannel>();
	if (ChannelEnum)
	{
		int64 Val = ChannelEnum->GetValueByNameString(ChannelName);
		if (Val != INDEX_NONE) Channel = (ECollisionChannel)Val;
		else
		{
			Val = ChannelEnum->GetValueByNameString(TEXT("ECC_") + ChannelName);
			if (Val != INDEX_NONE) Channel = (ECollisionChannel)Val;
		}
	}

	ECollisionResponse Response = ECR_Block;
	if (ResponseType.Equals(TEXT("Overlap"), ESearchCase::IgnoreCase)) Response = ECR_Overlap;
	else if (ResponseType.Equals(TEXT("Ignore"), ESearchCase::IgnoreCase)) Response = ECR_Ignore;

	PC->BodyInstance.SetResponseToChannel(Channel, Response);
	PC->PostEditChange();
	NotifyBPModified(BpPathOrActor);

	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"component\":\"%s\",\"channel\":\"%s\",\"response\":\"%s\"}"),
		*ComponentName, *ChannelName, *ResponseType);
}

void HandleSetComponentCollisionEnabled(const FString& BpPathOrActor, const FString& ComponentName,
	const FString& EnabledMode, FString& OutJsonString, FString& OutError)
{
	UActorComponent* Comp = FindComponentByName(BpPathOrActor, ComponentName, OutError);
	if (!Comp) return;
	UPrimitiveComponent* PC = Cast<UPrimitiveComponent>(Comp);
	if (!PC) { OutError = FString::Printf(TEXT("Component '%s' is not a PrimitiveComponent"), *ComponentName); return; }

	ECollisionEnabled::Type Mode = ECollisionEnabled::QueryAndPhysics;
	FString AppliedMode = TEXT("QueryAndPhysics");
	if (EnabledMode.Equals(TEXT("NoCollision"), ESearchCase::IgnoreCase))          { Mode = ECollisionEnabled::NoCollision;     AppliedMode = TEXT("NoCollision"); }
	else if (EnabledMode.Equals(TEXT("QueryOnly"), ESearchCase::IgnoreCase))       { Mode = ECollisionEnabled::QueryOnly;       AppliedMode = TEXT("QueryOnly"); }
	else if (EnabledMode.Equals(TEXT("PhysicsOnly"), ESearchCase::IgnoreCase))     { Mode = ECollisionEnabled::PhysicsOnly;     AppliedMode = TEXT("PhysicsOnly"); }
	else if (EnabledMode.Equals(TEXT("QueryAndPhysics"), ESearchCase::IgnoreCase)) { Mode = ECollisionEnabled::QueryAndPhysics; AppliedMode = TEXT("QueryAndPhysics"); }
	else
	{
		OutError = FString::Printf(
			TEXT("Unknown collision mode '%s' on component '%s'. Valid: NoCollision | QueryOnly | PhysicsOnly | QueryAndPhysics."),
			*EnabledMode, *ComponentName);
		return;
	}

	PC->BodyInstance.SetCollisionEnabled(Mode, false);
	PC->PostEditChange();
	NotifyBPModified(BpPathOrActor);

	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"component\":\"%s\",\"mode\":\"%s\"}"), *ComponentName, *AppliedMode);
}

void HandleSetComponentMobility(const FString& BpPathOrActor, const FString& ComponentName,
	const FString& MobilityMode, FString& OutJsonString, FString& OutError)
{
	UActorComponent* Comp = FindComponentByName(BpPathOrActor, ComponentName, OutError);
	if (!Comp) return;
	USceneComponent* SC = Cast<USceneComponent>(Comp);
	if (!SC) { OutError = FString::Printf(TEXT("Component '%s' is not a SceneComponent"), *ComponentName); return; }

	EComponentMobility::Type Mobility = EComponentMobility::Movable;
	if (MobilityMode.Equals(TEXT("Static"), ESearchCase::IgnoreCase))       Mobility = EComponentMobility::Static;
	else if (MobilityMode.Equals(TEXT("Stationary"), ESearchCase::IgnoreCase)) Mobility = EComponentMobility::Stationary;

	SC->SetMobility(Mobility);
	SC->PostEditChange();
	NotifyBPModified(BpPathOrActor);

	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"component\":\"%s\",\"mobility\":\"%s\"}"), *ComponentName, *MobilityMode);
}

void HandleSetSkeletalMeshComponent(const FString& BpPathOrActor, const FString& ComponentName,
	const FString& MeshPath, const FString& AnimBPPath, FString& OutJsonString, FString& OutError)
{
	UActorComponent* Comp = FindComponentByName(BpPathOrActor, ComponentName, OutError);
	if (!Comp) return;
	USkeletalMeshComponent* SMC = Cast<USkeletalMeshComponent>(Comp);
	if (!SMC) { OutError = FString::Printf(TEXT("Component '%s' is not a SkeletalMeshComponent"), *ComponentName); return; }

	USkeletalMesh* Mesh = nullptr;
	if (!MeshPath.IsEmpty())
	{
		Mesh = Cast<USkeletalMesh>(UEditorAssetLibrary::LoadAsset(MeshPath));
		if (!Mesh) { OutError = FString::Printf(TEXT("Could not load SkeletalMesh at: %s"), *MeshPath); return; }
		SMC->SetSkeletalMeshAsset(Mesh);
	}

	if (!AnimBPPath.IsEmpty())
	{
		FString ClassPath = AnimBPPath.EndsWith(TEXT("_C")) ? AnimBPPath : AnimBPPath + TEXT("_C");
		UClass* AnimClass = LoadObject<UClass>(nullptr, *ClassPath);
		USkeleton* AnimSkeleton = nullptr;
		if (!AnimClass)
		{
			UBlueprint* AnimBP = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(AnimBPPath));
			if (AnimBP) AnimClass = AnimBP->GeneratedClass;
			if (UAnimBlueprint* ABP = Cast<UAnimBlueprint>(AnimBP)) AnimSkeleton = ABP->TargetSkeleton;
		}
		if (!AnimSkeleton)
		{
			if (UAnimBlueprintGeneratedClass* AnimGen = Cast<UAnimBlueprintGeneratedClass>(AnimClass))
				AnimSkeleton = AnimGen->GetTargetSkeleton();
		}

		USkeletalMesh* EffectiveMesh = Mesh ? Mesh : SMC->GetSkeletalMeshAsset();
		if (AnimSkeleton && EffectiveMesh && !AnimSkeleton->IsCompatibleMesh(EffectiveMesh))
		{
			USkeleton* MeshSkel = EffectiveMesh->GetSkeleton();
			OutError = FString::Printf(
				TEXT("Skeleton mismatch: AnimBP '%s' targets skeleton '%s', but mesh '%s' uses skeleton '%s'. ")
				TEXT("Incompatible mesh+AnimBP leaves the character in a T-pose at runtime even though it compiles. ")
				TEXT("Pick a mesh and AnimBP that share a skeleton."),
				*AnimBPPath, *AnimSkeleton->GetName(), *EffectiveMesh->GetPathName(),
				MeshSkel ? *MeshSkel->GetName() : TEXT("<none>"));
			return;
		}

		if (AnimClass) SMC->SetAnimInstanceClass(AnimClass);
		else { OutError = FString::Printf(TEXT("Could not load Anim Blueprint class at: %s"), *AnimBPPath); return; }
	}

	SMC->PostEditChange();
	NotifyBPModified(BpPathOrActor);

	FString CurrentAnimClass;
	if (UClass* CurClass = SMC->GetAnimClass())
	{
		CurrentAnimClass = CurClass->GetPathName();
	}

	const FString CurrentMeshPath = SMC->GetSkeletalMeshAsset()
		? SMC->GetSkeletalMeshAsset()->GetPathName()
		: FString();

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"component\":\"%s\",\"mesh\":\"%s\",\"anim_class\":\"%s\"}"),
		*ComponentName, *CurrentMeshPath, *CurrentAnimClass);
}

void HandleSetCameraComponentProperties(const FString& BpPathOrActor, const FString& ComponentName,
	float FOV, const FString& ProjectionMode, float OrthoWidth, FString& OutJsonString, FString& OutError)
{
	UActorComponent* Comp = FindComponentByName(BpPathOrActor, ComponentName, OutError);
	if (!Comp) return;
	UCameraComponent* Cam = Cast<UCameraComponent>(Comp);
	if (!Cam) { OutError = FString::Printf(TEXT("Component '%s' is not a CameraComponent"), *ComponentName); return; }

	if (FOV > 0.0f) Cam->SetFieldOfView(FOV);

	if (!ProjectionMode.IsEmpty())
	{
		if (ProjectionMode.Equals(TEXT("Orthographic"), ESearchCase::IgnoreCase))
			Cam->SetProjectionMode(ECameraProjectionMode::Orthographic);
		else
			Cam->SetProjectionMode(ECameraProjectionMode::Perspective);
	}

	if (OrthoWidth > 0.0f) Cam->OrthoWidth = OrthoWidth;

	Cam->PostEditChange();
	NotifyBPModified(BpPathOrActor);

	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"component\":\"%s\",\"fov\":%f,\"projection\":\"%s\",\"ortho_width\":%f}"),
		*ComponentName, Cam->FieldOfView, *ProjectionMode, Cam->OrthoWidth);
}

void HandleSetAudioComponentProperties(const FString& BpPathOrActor, const FString& ComponentName,
	float VolumeMultiplier, float PitchMultiplier, const FString& AttenuationPath,
	const FString& SoundClassPath, const FString& AutoActivate, FString& OutJsonString, FString& OutError)
{
	UActorComponent* Comp = FindComponentByName(BpPathOrActor, ComponentName, OutError);
	if (!Comp) return;
	UAudioComponent* AC = Cast<UAudioComponent>(Comp);
	if (!AC) { OutError = FString::Printf(TEXT("Component '%s' is not an AudioComponent"), *ComponentName); return; }

	if (VolumeMultiplier >= 0.0f) AC->VolumeMultiplier = VolumeMultiplier;
	if (PitchMultiplier >= 0.0f)  AC->PitchMultiplier = PitchMultiplier;

	if (!AutoActivate.IsEmpty())
		AC->bAutoActivate = AutoActivate.Equals(TEXT("true"), ESearchCase::IgnoreCase)
			|| AutoActivate.Equals(TEXT("yes"), ESearchCase::IgnoreCase)
			|| AutoActivate.Equals(TEXT("1"), ESearchCase::IgnoreCase);

	if (!AttenuationPath.IsEmpty())
	{
		USoundAttenuation* Att = Cast<USoundAttenuation>(UEditorAssetLibrary::LoadAsset(AttenuationPath));
		if (!Att) { OutError = FString::Printf(TEXT("Could not load SoundAttenuation at: %s"), *AttenuationPath); return; }
		AC->AttenuationSettings = Att;
	}

	if (!SoundClassPath.IsEmpty())
	{
		USoundClass* SC = Cast<USoundClass>(UEditorAssetLibrary::LoadAsset(SoundClassPath));
		if (!SC) { OutError = FString::Printf(TEXT("Could not load SoundClass at: %s"), *SoundClassPath); return; }
		AC->SoundClassOverride = SC;
	}

	AC->PostEditChange();
	NotifyBPModified(BpPathOrActor);

	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"component\":\"%s\",\"volume\":%f,\"pitch\":%f}"),
		*ComponentName, AC->VolumeMultiplier, AC->PitchMultiplier);
}

void HandleSetComponentCastShadows(const FString& BpPathOrActor, const FString& ComponentName,
	bool bCastShadow, FString& OutJsonString, FString& OutError)
{
	UActorComponent* Comp = FindComponentByName(BpPathOrActor, ComponentName, OutError);
	if (!Comp) return;
	UPrimitiveComponent* PC = Cast<UPrimitiveComponent>(Comp);
	if (!PC) { OutError = FString::Printf(TEXT("Component '%s' is not a PrimitiveComponent"), *ComponentName); return; }

	PC->SetCastShadow(bCastShadow);
	PC->PostEditChange();
	NotifyBPModified(BpPathOrActor);

	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"component\":\"%s\",\"cast_shadow\":%s}"),
		*ComponentName, bCastShadow ? TEXT("true") : TEXT("false"));
}

void HandleSetComponentActive(const FString& BpPathOrActor, const FString& ComponentName, bool bActive, FString& OutJsonString, FString& OutError)
{

	UActorComponent* Comp = FindComponentByName(BpPathOrActor, ComponentName, OutError);
	if (!Comp) return;

	if (FBoolProperty* Prop = FindFProperty<FBoolProperty>(Comp->GetClass(), TEXT("bAutoActivate")))
		Prop->SetPropertyValue_InContainer(Comp, bActive);

	if (!BpPathOrActor.StartsWith(TEXT("/Game/")) && !BpPathOrActor.StartsWith(TEXT("/Engine/")))
		Comp->SetActive(bActive);

	Comp->PostEditChange();
	NotifyBPModified(BpPathOrActor);

	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"component\":\"%s\",\"active\":%s}"),
		*ComponentName, bActive ? TEXT("true") : TEXT("false"));
}

void HandleSetComponentReplication(const FString& BpPathOrActor, const FString& ComponentName, bool bReplicate, FString& OutJsonString, FString& OutError)
{

	UActorComponent* Comp = FindComponentByName(BpPathOrActor, ComponentName, OutError);
	if (!Comp) return;

	Comp->SetIsReplicated(bReplicate);

	Comp->PostEditChange();
	NotifyBPModified(BpPathOrActor);

	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"component\":\"%s\",\"replicates\":%s}"),
		*ComponentName, bReplicate ? TEXT("true") : TEXT("false"));
}

void HandleSetComponentTransform(const FString& BpPathOrActor, const FString& ComponentName,
	const FString& LocationStr, const FString& RotationStr, const FString& ScaleStr,
	FString& OutJsonString, FString& OutError)
{

	UActorComponent* Comp = FindComponentByName(BpPathOrActor, ComponentName, OutError);
	if (!Comp) return;

	USceneComponent* SC = Cast<USceneComponent>(Comp);
	if (!SC)
	{
		OutError = FString::Printf(TEXT("Component '%s' is not a SceneComponent — only SceneComponents have a transform"), *ComponentName);
		return;
	}

	auto ParseVec = [](const FString& S, FVector& Out) -> bool
	{
		if (S.IsEmpty()) return false;
		if (Out.InitFromString(S)) return true;
		TArray<FString> Parts;
		S.ParseIntoArray(Parts, TEXT(","));
		if (Parts.Num() == 3)
		{
			Out.X = FCString::Atof(*Parts[0].TrimStartAndEnd());
			Out.Y = FCString::Atof(*Parts[1].TrimStartAndEnd());
			Out.Z = FCString::Atof(*Parts[2].TrimStartAndEnd());
			return true;
		}
		return false;
	};

	auto ParseRot = [](const FString& S, FRotator& Out) -> bool
	{
		if (S.IsEmpty()) return false;
		if (Out.InitFromString(S)) return true;
		TArray<FString> Parts;
		S.ParseIntoArray(Parts, TEXT(","));
		if (Parts.Num() == 3)
		{
			Out.Pitch = FCString::Atof(*Parts[0].TrimStartAndEnd());
			Out.Yaw   = FCString::Atof(*Parts[1].TrimStartAndEnd());
			Out.Roll  = FCString::Atof(*Parts[2].TrimStartAndEnd());
			return true;
		}
		return false;
	};

	bool bAny = false;
	TArray<FString> Applied;

	if (!LocationStr.IsEmpty())
	{
		FVector Loc;
		if (!ParseVec(LocationStr, Loc))
		{
			OutError = FString::Printf(TEXT("Cannot parse location '%s'. Use 'x,y,z' e.g. '0,0,-97'"), *LocationStr);
			return;
		}
		SC->SetRelativeLocation(Loc);
		Applied.Add(FString::Printf(TEXT("location=(%.1f,%.1f,%.1f)"), Loc.X, Loc.Y, Loc.Z));
		bAny = true;
	}

	if (!RotationStr.IsEmpty())
	{
		FRotator Rot;
		if (!ParseRot(RotationStr, Rot))
		{
			OutError = FString::Printf(TEXT("Cannot parse rotation '%s'. Use 'pitch,yaw,roll' e.g. '0,-90,0'"), *RotationStr);
			return;
		}
		SC->SetRelativeRotation(Rot);
		Applied.Add(FString::Printf(TEXT("rotation=(P=%.1f,Y=%.1f,R=%.1f)"), Rot.Pitch, Rot.Yaw, Rot.Roll));
		bAny = true;
	}

	if (!ScaleStr.IsEmpty())
	{
		FVector Scale;
		if (!ParseVec(ScaleStr, Scale))
		{
			OutError = FString::Printf(TEXT("Cannot parse scale '%s'. Use 'x,y,z' e.g. '1,1,1'"), *ScaleStr);
			return;
		}
		SC->SetRelativeScale3D(Scale);
		Applied.Add(FString::Printf(TEXT("scale=(%.2f,%.2f,%.2f)"), Scale.X, Scale.Y, Scale.Z));
		bAny = true;
	}

	if (!bAny)
	{
		OutError = TEXT("No transform values provided. Supply at least one of: location, rotation, scale");
		return;
	}

	SC->PostEditChange();
	NotifyBPModified(BpPathOrActor);

	FString AppliedStr = FString::Join(Applied, TEXT(", "));
	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"component\":\"%s\",\"applied\":\"%s\"}"),
		*ComponentName, *AppliedStr);
}

void HandleReparentComponent(const FString& BpPath, const FString& ComponentName, const FString& NewParent,
	FString& OutJsonString, FString& OutError)
{
	UBlueprint* BP = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(BpPath));
	if (!BP) { OutError = TEXT("Blueprint not found: ") + BpPath; return; }
	if (!BP->SimpleConstructionScript) { OutError = TEXT("Blueprint has no SimpleConstructionScript"); return; }
	if (NewParent.IsEmpty()) { OutError = TEXT("Missing required parameter: new_parent / attach_to / parent"); return; }

	USCS_Node* TargetNode = nullptr;
	for (USCS_Node* N : BP->SimpleConstructionScript->GetAllNodes())
	{
		if (N && N->GetVariableName().ToString().Equals(ComponentName, ESearchCase::IgnoreCase))
		{ TargetNode = N; break; }
	}
	if (!TargetNode)
	{
		OutError = FString::Printf(TEXT("Component '%s' not found in SCS"), *ComponentName);
		return;
	}

	USCS_Node* ParentSCSNode = nullptr;
	USceneComponent* NativeParentComponent = nullptr;
	for (USCS_Node* N : BP->SimpleConstructionScript->GetAllNodes())
	{
		if (N == TargetNode) continue;
		if (N && N->GetVariableName().ToString().Equals(NewParent, ESearchCase::IgnoreCase))
		{ ParentSCSNode = N; break; }
	}
	if (!ParentSCSNode && BP->ParentClass)
	{
		if (UBlueprint* ParentBP = Cast<UBlueprint>(BP->ParentClass->ClassGeneratedBy))
		{
			if (ParentBP->SimpleConstructionScript)
			{
				for (USCS_Node* N : ParentBP->SimpleConstructionScript->GetAllNodes())
				{
					if (N && N->GetVariableName().ToString().Equals(NewParent, ESearchCase::IgnoreCase))
					{ ParentSCSNode = N; break; }
				}
			}
		}
	}
	if (!ParentSCSNode && BP->ParentClass)
	{
		UClass* SearchClass = BP->ParentClass;
		while (SearchClass && SearchClass != UObject::StaticClass())
		{
			for (TFieldIterator<FObjectProperty> It(SearchClass, EFieldIteratorFlags::ExcludeSuper); It; ++It)
			{
				if (It->GetName().Equals(NewParent, ESearchCase::IgnoreCase) &&
					It->PropertyClass && It->PropertyClass->IsChildOf(USceneComponent::StaticClass()))
				{
					if (AActor* CDO = Cast<AActor>(SearchClass->GetDefaultObject()))
					{
						UObject* Obj = It->GetObjectPropertyValue_InContainer(CDO);
						NativeParentComponent = Cast<USceneComponent>(Obj);
					}
					break;
				}
			}
			if (NativeParentComponent) break;
			SearchClass = SearchClass->GetSuperClass();
		}
	}
	if (!ParentSCSNode && !NativeParentComponent && BP->ParentClass)
	{
		if (AActor* CDO = Cast<AActor>(BP->ParentClass->GetDefaultObject()))
		{
			TArray<UActorComponent*> CDOComps;
			CDO->GetComponents(CDOComps);
			for (UActorComponent* Comp : CDOComps)
			{
				if (USceneComponent* SC = Cast<USceneComponent>(Comp))
				{
					if (SC->GetName().Equals(NewParent, ESearchCase::IgnoreCase))
					{ NativeParentComponent = SC; break; }
				}
			}
			if (!NativeParentComponent && NewParent.Equals(TEXT("RootComponent"), ESearchCase::IgnoreCase))
			{
				NativeParentComponent = CDO->GetRootComponent();
			}
		}
	}

	if (!ParentSCSNode && !NativeParentComponent)
	{
		TArray<FString> Candidates;
		for (USCS_Node* N : BP->SimpleConstructionScript->GetAllNodes())
		{
			if (N && N != TargetNode) Candidates.Add(N->GetVariableName().ToString());
		}
		if (BP->ParentClass)
		{
			if (AActor* CDO = Cast<AActor>(BP->ParentClass->GetDefaultObject()))
			{
				TArray<UActorComponent*> CDOComps;
				CDO->GetComponents(CDOComps);
				for (UActorComponent* Comp : CDOComps)
				{
					if (USceneComponent* SC = Cast<USceneComponent>(Comp))
						Candidates.AddUnique(SC->GetName());
				}
			}
		}
		OutError = FString::Printf(
			TEXT("Could not resolve new parent '%s'. Available: %s"),
			*NewParent,
			Candidates.Num() > 0 ? *FString::Join(Candidates, TEXT(", ")) : TEXT("(none)"));
		return;
	}

	if (ParentSCSNode)
	{
		USCS_Node* Walk = ParentSCSNode;
		TSet<USCS_Node*> Visited;
		while (Walk && !Visited.Contains(Walk))
		{
			if (Walk == TargetNode)
			{
				OutError = FString::Printf(
					TEXT("Reparent would create a cycle: '%s' is already an ancestor of '%s'."),
					*ComponentName, *NewParent);
				return;
			}
			Visited.Add(Walk);
			USCS_Node* WalkParent = nullptr;
			for (USCS_Node* N : BP->SimpleConstructionScript->GetAllNodes())
			{
				if (N && N != Walk && N->GetChildNodes().Contains(Walk))
				{ WalkParent = N; break; }
			}
			Walk = WalkParent;
		}
	}

	BP->SimpleConstructionScript->RemoveNode(TargetNode);

	if (ParentSCSNode)
	{
		ParentSCSNode->AddChildNode(TargetNode);
	}
	else
	{
		TargetNode->SetParent(NativeParentComponent);
		BP->SimpleConstructionScript->AddNode(TargetNode);
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
	UEditorAssetLibrary::SaveAsset(BpPath, false);

	TSharedRef<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetBoolField(TEXT("success"), true);
	R->SetStringField(TEXT("component"), ComponentName);
	R->SetStringField(TEXT("attached_to"), NewParent);
	FString Out;
	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(R, W);
	OutJsonString = Out;
}

void HandleReparentComponentFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString BpPath, CompName, NewParent;
	if (!Args->TryGetStringField(TEXT("blueprint_path"), BpPath) || BpPath.IsEmpty())
	{ OutError = TEXT("Missing required parameter: blueprint_path"); return; }
	if (!Args->TryGetStringField(TEXT("component_name"), CompName))
		Args->TryGetStringField(TEXT("name"), CompName);
	if (CompName.IsEmpty())
	{ OutError = TEXT("Missing required parameter: component_name"); return; }
	if (!Args->TryGetStringField(TEXT("new_parent"), NewParent))
		if (!Args->TryGetStringField(TEXT("attach_to"), NewParent))
			if (!Args->TryGetStringField(TEXT("parent"), NewParent))
				if (!Args->TryGetStringField(TEXT("parent_name"), NewParent))
					Args->TryGetStringField(TEXT("parent_component"), NewParent);

	HandleReparentComponent(BpPath, CompName, NewParent, OutJsonString, OutError);
}

void HandleSetRootComponent(const FString& BpPath, const FString& ComponentName, FString& OutJsonString, FString& OutError)
{
	UBlueprint* BP = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(BpPath));
	if (!BP) { OutError = TEXT("Blueprint not found: ") + BpPath; return; }
	USimpleConstructionScript* SCS = BP->SimpleConstructionScript;
	if (!SCS) { OutError = TEXT("Blueprint has no SimpleConstructionScript"); return; }

	USCS_Node* NewRoot = nullptr;
	for (USCS_Node* N : SCS->GetAllNodes())
	{
		if (N && N->GetVariableName().ToString().Equals(ComponentName, ESearchCase::IgnoreCase))
		{ NewRoot = N; break; }
	}
	if (!NewRoot)
	{
		TArray<FString> Candidates;
		for (USCS_Node* N : SCS->GetAllNodes()) if (N) Candidates.Add(N->GetVariableName().ToString());
		OutError = FString::Printf(TEXT("Component '%s' not found in SCS. Available: %s"),
			*ComponentName, Candidates.Num() ? *FString::Join(Candidates, TEXT(", ")) : TEXT("(none)"));
		return;
	}

	USceneComponent* NewRootTemplate = Cast<USceneComponent>(NewRoot->ComponentTemplate);
	if (!NewRootTemplate)
	{
		OutError = FString::Printf(TEXT("Component '%s' is a %s, not a SceneComponent — only scene components can be the root."),
			*ComponentName, NewRoot->ComponentTemplate ? *NewRoot->ComponentTemplate->GetClass()->GetName() : TEXT("null"));
		return;
	}

	USCS_Node* OldRoot = nullptr;
	SCS->GetSceneRootComponentTemplate(true, &OldRoot);
	const bool bWasDefault = (OldRoot == SCS->GetDefaultSceneRootNode());

	if (OldRoot == NewRoot)
	{
		OutJsonString = FString::Printf(TEXT("{\"success\":true,\"component\":\"%s\",\"is_root\":true,\"note\":\"already the root\"}"), *ComponentName);
		return;
	}

	BP->Modify();
	NewRoot->Modify();
	NewRootTemplate->Modify();
	if (OldRoot) OldRoot->Modify();

	NewRoot->AttachToName = NAME_None;
	NewRootTemplate->SetupAttachment(nullptr, NAME_None);
	NewRootTemplate->SetRelativeLocation(FVector::ZeroVector);
	NewRootTemplate->SetRelativeRotation(FRotator::ZeroRotator);

	SCS->RemoveNode(NewRoot);

	if (OldRoot)
	{
		TArray<USCS_Node*> OldRootChildren = OldRoot->GetChildNodes();
		SCS->RemoveNode(OldRoot, false);
		SCS->AddNode(NewRoot);

		if (!bWasDefault)
		{
			NewRoot->AddChildNode(OldRoot);
		}
		else
		{
			for (USCS_Node* Child : OldRootChildren)
			{
				if (!Child || Child == NewRoot) continue;
				OldRoot->RemoveChildNode(Child);
				NewRoot->AddChildNode(Child);
			}
		}
	}
	else
	{
		SCS->AddNode(NewRoot);
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
	UEditorAssetLibrary::SaveAsset(BpPath, false);

	TSharedRef<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetBoolField(TEXT("success"), true);
	R->SetStringField(TEXT("component"), ComponentName);
	R->SetBoolField(TEXT("is_root"), true);
	if (OldRoot) R->SetStringField(TEXT("previous_root"), OldRoot->GetVariableName().ToString());
	R->SetBoolField(TEXT("replaced_default_root"), bWasDefault);
	FString Out;
	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(R, W);
	OutJsonString = Out;
}

void HandleSetRootComponentFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString BpPath, CompName;
	if (!Args->TryGetStringField(TEXT("blueprint_path"), BpPath) || BpPath.IsEmpty())
	{ OutError = TEXT("Missing required parameter: blueprint_path"); return; }
	if (!Args->TryGetStringField(TEXT("component_name"), CompName))
		if (!Args->TryGetStringField(TEXT("name"), CompName))
			Args->TryGetStringField(TEXT("component"), CompName);
	if (CompName.IsEmpty())
	{ OutError = TEXT("Missing required parameter: component_name"); return; }

	HandleSetRootComponent(BpPath, CompName, OutJsonString, OutError);
}

void HandleRemoveComponent(const FString& BpPath, const FString& ComponentName, FString& OutJsonString, FString& OutError)
{
	UBlueprint* BP = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(BpPath));
	if (!BP) { OutError = TEXT("Blueprint not found: ") + BpPath; return; }
	if (!BP->SimpleConstructionScript) { OutError = TEXT("No SCS"); return; }

	USCS_Node* TargetNode = nullptr;
	for (USCS_Node* Node : BP->SimpleConstructionScript->GetAllNodes())
	{
		if (Node && Node->GetVariableName().ToString().Equals(ComponentName, ESearchCase::IgnoreCase))
		{
			TargetNode = Node;
			break;
		}
	}
	if (!TargetNode) { OutError = FString::Printf(TEXT("Component '%s' not found"), *ComponentName); return; }

	BP->SimpleConstructionScript->RemoveNode(TargetNode);
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
	UEditorAssetLibrary::SaveAsset(BpPath, false);

	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"removed\":\"%s\"}"), *ComponentName);
}

void HandleRenameComponent(const FString& BpPath, const FString& OldName, const FString& NewName, FString& OutJsonString, FString& OutError)
{
	UBlueprint* BP = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(BpPath));
	if (!BP) { OutError = TEXT("Blueprint not found: ") + BpPath; return; }
	if (!BP->SimpleConstructionScript) { OutError = TEXT("No SCS"); return; }

	USCS_Node* TargetNode = nullptr;
	for (USCS_Node* Node : BP->SimpleConstructionScript->GetAllNodes())
	{
		if (Node && Node->GetVariableName().ToString().Equals(OldName, ESearchCase::IgnoreCase))
		{
			TargetNode = Node;
			break;
		}
	}
	if (!TargetNode) { OutError = FString::Printf(TEXT("Component '%s' not found"), *OldName); return; }

	FBlueprintEditorUtils::RenameComponentMemberVariable(BP, TargetNode, FName(*NewName));
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
	UEditorAssetLibrary::SaveAsset(BpPath, false);

	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"old_name\":\"%s\",\"new_name\":\"%s\"}"), *OldName, *NewName);
}

namespace
{
	FString ResolveTargetPath(const TSharedPtr<FJsonObject>& Args)
	{
		FString Target;
		if (!Args->TryGetStringField(TEXT("bp_path_or_actor"), Target))
			Args->TryGetStringField(TEXT("blueprint_path"), Target);
		return Target;
	}

	FString GetComponentName(const TSharedPtr<FJsonObject>& Args)
	{
		FString Name;
		if (!Args->TryGetStringField(TEXT("component_name"), Name))
			Args->TryGetStringField(TEXT("component"), Name);
		return Name;
	}

	bool ParseBoolAuto(const TSharedPtr<FJsonObject>& Args, const TCHAR* Field, bool Default)
	{
		bool bVal = Default;
		if (Args->TryGetBoolField(Field, bVal)) return bVal;
		FString S;
		if (Args->TryGetStringField(Field, S))
		{
			return !S.Equals(TEXT("false"), ESearchCase::IgnoreCase)
			    && !S.Equals(TEXT("0"), ESearchCase::IgnoreCase)
			    && !S.Equals(TEXT("no"), ESearchCase::IgnoreCase);
		}
		return Default;
	}
}

FString HandleGetComponentProperty(const FString& BpPath, const FString& ComponentName, const FString& PropertyName)
{
	UBlueprint* TargetBlueprint = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(BpPath));
	if (!TargetBlueprint)
		return FString::Printf(TEXT("{\"success\": false, \"error\": \"Could not load Blueprint at path: %s\"}"), *BpPath);

	UActorComponent* ComponentTemplate = nullptr;
	for (UBlueprint* WalkBP = TargetBlueprint; WalkBP && !ComponentTemplate;
		WalkBP = (WalkBP->ParentClass && WalkBP->ParentClass->ClassGeneratedBy) ? Cast<UBlueprint>(WalkBP->ParentClass->ClassGeneratedBy) : nullptr)
	{
		if (USimpleConstructionScript* SCS = WalkBP->SimpleConstructionScript)
			for (USCS_Node* Node : SCS->GetAllNodes())
				if (Node && Node->ComponentTemplate &&
					(Node->GetVariableName().ToString().Equals(ComponentName, ESearchCase::IgnoreCase) ||
					 Node->ComponentTemplate->GetClass()->GetName().Equals(ComponentName, ESearchCase::IgnoreCase)))
				{ ComponentTemplate = Node->ComponentTemplate; break; }
	}
	if (!ComponentTemplate)
	{
		UClass* SearchClass = TargetBlueprint->GeneratedClass ? TargetBlueprint->GeneratedClass : TargetBlueprint->ParentClass;
		if (SearchClass)
			if (AActor* CDO = Cast<AActor>(SearchClass->GetDefaultObject()))
			{
				TArray<UActorComponent*> Comps; CDO->GetComponents(Comps);
				for (UActorComponent* C : Comps)
					if (C && (C->GetName().Equals(ComponentName, ESearchCase::IgnoreCase) ||
						C->GetClass()->GetName().Equals(ComponentName, ESearchCase::IgnoreCase)))
					{ ComponentTemplate = C; break; }
			}
	}
	if (!ComponentTemplate)
		return FString::Printf(TEXT("{\"success\": false, \"error\": \"Component '%s' not found in Blueprint '%s'\"}"), *ComponentName, *BpPath);

	FProperty* Property = nullptr;
	void* PropertyContainer = ComponentTemplate;
	if (PropertyName.Contains(TEXT(".")))
	{
		TArray<FString> Parts; PropertyName.ParseIntoArray(Parts, TEXT("."));
		UStruct* CurrentStruct = ComponentTemplate->GetClass();
		void* CurrentContainer = ComponentTemplate;
		for (int32 i = 0; i < Parts.Num(); i++)
		{
			FProperty* PartProp = CurrentStruct->FindPropertyByName(FName(*Parts[i]));
			if (!PartProp) break;
			if (i == Parts.Num() - 1) { Property = PartProp; PropertyContainer = CurrentContainer; }
			else
			{
				FStructProperty* StructProp = CastField<FStructProperty>(PartProp);
				if (!StructProp) break;
				CurrentContainer = StructProp->ContainerPtrToValuePtr<void>(CurrentContainer);
				CurrentStruct = StructProp->Struct;
			}
		}
	}
	else
	{
		Property = ComponentTemplate->GetClass()->FindPropertyByName(FName(*PropertyName));
	}
	if (!Property)
	{
		TArray<FString> Suggestions;
		for (TFieldIterator<FProperty> It(ComponentTemplate->GetClass()); It && Suggestions.Num() < 8; ++It)
			if (It->GetName().Contains(PropertyName, ESearchCase::IgnoreCase)) Suggestions.Add(It->GetName());
		return FString::Printf(TEXT("{\"success\": false, \"error\": \"Property '%s' not found on component '%s'\", \"suggestions\": \"%s\"}"),
			*PropertyName, *ComponentName, *FString::Join(Suggestions, TEXT(", ")));
	}

	void* PropertyData = Property->ContainerPtrToValuePtr<void>(PropertyContainer);
	FString ValueStr;
	Property->ExportTextItem_Direct(ValueStr, PropertyData, nullptr, ComponentTemplate, PPF_None);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("matched_component"), ComponentTemplate->GetName());
	Result->SetStringField(TEXT("property_name"), PropertyName);
	Result->SetStringField(TEXT("value"), ValueStr);
	Result->SetStringField(TEXT("type"), Property->GetCPPType());
	FString Out;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
	return Out;
}

void HandleGetComponentPropertyFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Missing arguments"); return; }
	FString BlueprintPath = ResolveTargetPath(Args);
	if (BlueprintPath.IsEmpty()) { OutError = TEXT("Missing required parameter: blueprint_path"); return; }
	FString ComponentName = GetComponentName(Args);
	if (ComponentName.IsEmpty()) { OutError = TEXT("Missing required parameter: component_name"); return; }
	FString PropertyName;
	if ((!Args->TryGetStringField(TEXT("property_name"), PropertyName) || PropertyName.IsEmpty())
		&& (!Args->TryGetStringField(TEXT("property"), PropertyName) || PropertyName.IsEmpty()))
	{ OutError = TEXT("Missing required parameter: property_name"); return; }

	OutJsonString = HandleGetComponentProperty(BlueprintPath, ComponentName, PropertyName);

	TSharedPtr<FJsonObject> ResultObj;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(OutJsonString);
	if (FJsonSerializer::Deserialize(Reader, ResultObj) && ResultObj.IsValid())
	{
		bool bOk = false; ResultObj->TryGetBoolField(TEXT("success"), bOk);
		if (!bOk) ResultObj->TryGetStringField(TEXT("error"), OutError);
	}
}

void HandleEditComponentPropertyFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Missing arguments"); return; }

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("properties"), ItemsArray))
	{
		const FString OuterBp = ResolveTargetPath(Args);
		const FString OuterCN = GetComponentName(Args);
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); ++i)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item (expected object)")); continue; }

			FString ItemBp = ResolveTargetPath(Item); if (ItemBp.IsEmpty()) ItemBp = OuterBp;
			if (ItemBp.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing blueprint_path")); continue; }

			FString ItemCN = GetComponentName(Item); if (ItemCN.IsEmpty()) ItemCN = OuterCN;
			if (ItemCN.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing component_name")); continue; }

			FString PropName;
			if (!Item->TryGetStringField(TEXT("property_name"), PropName) || PropName.IsEmpty())
			{
				if (!Item->TryGetStringField(TEXT("property"), PropName) || PropName.IsEmpty())
				{
					if (!Item->TryGetStringField(TEXT("name"), PropName) || PropName.IsEmpty())
					{
						Batch.AddFailure(i, TEXT("Missing property_name"));
						continue;
					}
				}
			}

			FString PropValue;
			if (!CoerceJsonValueToString(Item, TEXT("value"), PropValue)
				&& !CoerceJsonValueToString(Item, TEXT("property_value"), PropValue))
			{
				Batch.AddFailure(i, TEXT("Missing value"));
				continue;
			}

			const FString ItemResult = HandleEditComponentProperty(ItemBp, ItemCN, PropName, PropValue);
			TSharedPtr<FJsonObject> R;
			TSharedRef<TJsonReader<>> Rdr = TJsonReaderFactory<>::Create(ItemResult);
			if (FJsonSerializer::Deserialize(Rdr, R) && R.IsValid())
			{
				bool bOk = false; R->TryGetBoolField(TEXT("success"), bOk);
				if (bOk)
				{
					TSharedPtr<FJsonObject> Extra = MakeShared<FJsonObject>();
					Extra->SetStringField(TEXT("component_name"), ItemCN);
					Extra->SetStringField(TEXT("property_name"), PropName);
					Batch.AddSuccess(i, Extra);
				}
				else
				{
					FString Err; R->TryGetStringField(TEXT("error"), Err);
					Batch.AddFailure(i, Err.IsEmpty() ? TEXT("Edit failed") : Err);
				}
			}
			else
			{
				Batch.AddFailure(i, TEXT("Edit returned malformed result"));
			}
		}
		Batch.Finalize(OutJsonString);
		return;
	}

	FString BlueprintPath = ResolveTargetPath(Args);
	if (BlueprintPath.IsEmpty()) { OutError = TEXT("Missing required parameter: blueprint_path"); return; }
	FString ComponentName = GetComponentName(Args);
	if (ComponentName.IsEmpty()) { OutError = TEXT("Missing required parameter: component_name"); return; }
	FString PropertyName;
	if (!Args->TryGetStringField(TEXT("property_name"), PropertyName))
	{
		OutError = TEXT("Missing required parameter: property_name");
		return;
	}
	FString PropertyValue;
	if (!CoerceJsonValueToString(Args, TEXT("value"), PropertyValue)
		&& !CoerceJsonValueToString(Args, TEXT("property_value"), PropertyValue))
	{
		OutError = TEXT("Missing required parameter: value. (To READ the current value instead of setting it, use component(action='get_component_property', blueprint_path, component_name, property_name).)");
		return;
	}

	OutJsonString = HandleEditComponentProperty(BlueprintPath, ComponentName, PropertyName, PropertyValue);

	TSharedPtr<FJsonObject> ResultObj;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(OutJsonString);
	if (FJsonSerializer::Deserialize(Reader, ResultObj) && ResultObj.IsValid())
	{
		bool bSuccess = false;
		ResultObj->TryGetBoolField(TEXT("success"), bSuccess);
		if (!bSuccess) ResultObj->TryGetStringField(TEXT("error"), OutError);
	}
}

void HandleRemoveComponentFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Missing arguments"); return; }
	FString BP, CN;
	Args->TryGetStringField(TEXT("blueprint_path"), BP);
	CN = GetComponentName(Args);
	HandleRemoveComponent(BP, CN, OutJsonString, OutError);
}

void HandleRenameComponentFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Missing arguments"); return; }
	FString BP, ON, NN;
	Args->TryGetStringField(TEXT("blueprint_path"), BP);
	Args->TryGetStringField(TEXT("old_name"), ON);
	Args->TryGetStringField(TEXT("new_name"), NN);
	HandleRenameComponent(BP, ON, NN, OutJsonString, OutError);
}

void HandleSetComponentCollisionProfileFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Missing arguments"); return; }
	FString Target = ResolveTargetPath(Args);
	FString Component = GetComponentName(Args);
	FString Profile;
	if (!Args->TryGetStringField(TEXT("collision_profile"), Profile))
		if (!Args->TryGetStringField(TEXT("profile_name"), Profile))
			Args->TryGetStringField(TEXT("profile"), Profile);
	if (Profile.IsEmpty())
	{
		OutError = TEXT("No collision profile supplied. Pass `collision_profile: 'OverlapAllDynamic'` (or `profile_name` / `profile` alias). Valid profile names include: NoCollision, OverlapAll, OverlapAllDynamic, OverlapOnlyPawn, BlockAll, BlockAllDynamic, Pawn, CharacterMesh, PhysicsActor, IgnoreOnlyPawn, plus any project-defined profiles in DefaultEngine.ini.");
		return;
	}
	HandleSetComponentCollisionProfile(Target, Component, Profile, OutJsonString, OutError);
}

void HandleSetComponentCollisionResponseFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Missing arguments"); return; }
	FString Target = ResolveTargetPath(Args);
	FString Component = GetComponentName(Args);
	FString Channel, Response;
	Args->TryGetStringField(TEXT("channel"), Channel);
	Args->TryGetStringField(TEXT("response"), Response);
	if (Response.IsEmpty()) Response = TEXT("Block");
	HandleSetComponentCollisionResponse(Target, Component, Channel, Response, OutJsonString, OutError);
}

void HandleSetComponentCollisionEnabledFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Missing arguments"); return; }
	FString Target = ResolveTargetPath(Args);
	FString Component = GetComponentName(Args);
	FString Mode;
	if (!Args->TryGetStringField(TEXT("collision_enabled"), Mode))
	{
		if (!Args->TryGetStringField(TEXT("enabled_mode"), Mode))
		{
			Args->TryGetStringField(TEXT("mode"), Mode);
		}
	}
	if (Mode.IsEmpty()) Mode = TEXT("QueryAndPhysics");
	HandleSetComponentCollisionEnabled(Target, Component, Mode, OutJsonString, OutError);
}

void HandleSetComponentMobilityFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Missing arguments"); return; }
	FString Target = ResolveTargetPath(Args);
	FString Component = GetComponentName(Args);
	FString Mobility;
	Args->TryGetStringField(TEXT("mobility"), Mobility);
	if (Mobility.IsEmpty()) Mobility = TEXT("Movable");
	HandleSetComponentMobility(Target, Component, Mobility, OutJsonString, OutError);
}

void HandleSetSkeletalMeshComponentFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Missing arguments"); return; }
	FString Target = ResolveTargetPath(Args);
	FString Component = GetComponentName(Args);
	FString MeshPath, AnimBPPath;
	if (!Args->TryGetStringField(TEXT("mesh_path"), MeshPath))
		Args->TryGetStringField(TEXT("skeletal_mesh_path"), MeshPath);
	Args->TryGetStringField(TEXT("anim_bp_path"), AnimBPPath);
	HandleSetSkeletalMeshComponent(Target, Component, MeshPath, AnimBPPath, OutJsonString, OutError);
}

void HandleSetStaticMeshComponentFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Missing arguments"); return; }
	FString Target = ResolveTargetPath(Args);
	FString Component = GetComponentName(Args);
	FString MeshPath;
	if (!Args->TryGetStringField(TEXT("mesh_path"), MeshPath))
		if (!Args->TryGetStringField(TEXT("static_mesh_path"), MeshPath))
			Args->TryGetStringField(TEXT("mesh"), MeshPath);

	UActorComponent* Comp = FindComponentByName(Target, Component, OutError);
	if (!Comp) return;
	UStaticMeshComponent* SMC = Cast<UStaticMeshComponent>(Comp);
	if (!SMC)
	{
		OutError = FString::Printf(TEXT("Component '%s' is not a StaticMeshComponent"), *Component);
		return;
	}
	if (MeshPath.IsEmpty())
	{
		OutError = TEXT("mesh_path is required");
		return;
	}
	UStaticMesh* Mesh = Cast<UStaticMesh>(UEditorAssetLibrary::LoadAsset(MeshPath));
	if (!Mesh)
	{
		OutError = FString::Printf(TEXT("Could not load StaticMesh at: %s"), *MeshPath);
		return;
	}
	SMC->SetStaticMesh(Mesh);
	SMC->PostEditChange();
	NotifyBPModified(Target);

	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"component\":\"%s\",\"mesh\":\"%s\"}"),
		*Component, *MeshPath);
}

void HandleSetCameraComponentPropertiesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Missing arguments"); return; }
	FString Target = ResolveTargetPath(Args);
	FString Component = GetComponentName(Args);
	FString ProjectionMode;
	double FOV = -1.0, OrthoWidth = -1.0;
	Args->TryGetStringField(TEXT("projection_mode"), ProjectionMode);
	Args->TryGetNumberField(TEXT("fov"), FOV);
	Args->TryGetNumberField(TEXT("ortho_width"), OrthoWidth);
	HandleSetCameraComponentProperties(Target, Component, (float)FOV, ProjectionMode, (float)OrthoWidth, OutJsonString, OutError);
}

void HandleSetAudioComponentPropertiesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Missing arguments"); return; }
	FString Target = ResolveTargetPath(Args);
	FString Component = GetComponentName(Args);
	FString AttenuationPath, SoundClassPath, AutoActivate;
	double Volume = -1.0, Pitch = -1.0;
	Args->TryGetStringField(TEXT("attenuation_path"), AttenuationPath);
	Args->TryGetStringField(TEXT("sound_class_path"), SoundClassPath);
	Args->TryGetStringField(TEXT("auto_activate"), AutoActivate);
	Args->TryGetNumberField(TEXT("volume_multiplier"), Volume);
	Args->TryGetNumberField(TEXT("pitch_multiplier"), Pitch);
	HandleSetAudioComponentProperties(Target, Component, (float)Volume, (float)Pitch, AttenuationPath, SoundClassPath, AutoActivate, OutJsonString, OutError);
}

void HandleSetComponentCastShadowsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Missing arguments"); return; }
	FString Target = ResolveTargetPath(Args);
	FString Component = GetComponentName(Args);
	bool bCastShadow = ParseBoolAuto(Args, TEXT("cast_shadow"), true);
	HandleSetComponentCastShadows(Target, Component, bCastShadow, OutJsonString, OutError);
}

void HandleSetComponentActiveFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Missing arguments"); return; }
	FString Target = ResolveTargetPath(Args);
	FString Component = GetComponentName(Args);
	bool bActive = ParseBoolAuto(Args, TEXT("active"), true);
	HandleSetComponentActive(Target, Component, bActive, OutJsonString, OutError);
}

void HandleSetComponentReplicationFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Missing arguments"); return; }
	FString Target;
	Args->TryGetStringField(TEXT("bp_path_or_actor"), Target);
	if (Target.IsEmpty()) Args->TryGetStringField(TEXT("blueprint_path"), Target);

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("components"), ItemsArray))
	{
		if (Target.IsEmpty()) { OutError = TEXT("Missing bp_path_or_actor / blueprint_path"); return; }
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString CompName = BatchToolHelper::GetItemString(Item, TEXT("component_name"), TEXT("name"));
			if (CompName.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing component_name")); continue; }
			bool bReplicate = false;
			if (!Item->TryGetBoolField(TEXT("replicate"), bReplicate))
				Item->TryGetBoolField(TEXT("replicates"), bReplicate);
			FString ItemOut, ItemErr;
			HandleSetComponentReplication(Target, CompName, bReplicate, ItemOut, ItemErr);
			if (ItemErr.IsEmpty())
			{
				auto Extra = MakeShared<FJsonObject>();
				Extra->SetStringField(TEXT("component"), CompName);
				Extra->SetBoolField(TEXT("replicates"), bReplicate);
				Batch.AddSuccess(i, Extra);
			}
			else Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJsonString);
		return;
	}

	FString Component = GetComponentName(Args);
	bool bReplicate = ParseBoolAuto(Args, TEXT("replicate"), false);
	HandleSetComponentReplication(Target, Component, bReplicate, OutJsonString, OutError);
}

void HandleSetComponentTransformFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Missing arguments"); return; }
	FString Target = ResolveTargetPath(Args);
	FString Component = GetComponentName(Args);
	FString Location, Rotation, Scale;
	Args->TryGetStringField(TEXT("location"), Location);
	Args->TryGetStringField(TEXT("rotation"), Rotation);
	Args->TryGetStringField(TEXT("scale"), Scale);
	HandleSetComponentTransform(Target, Component, Location, Rotation, Scale, OutJsonString, OutError);
}

void HandleSetCharacterAnimClassFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }

	FString BpPath, AnimClassPath, SkeletalMeshPath;
	Args->TryGetStringField(TEXT("blueprint_path"), BpPath);
	Args->TryGetStringField(TEXT("anim_class_path"), AnimClassPath);
	if (AnimClassPath.IsEmpty()) Args->TryGetStringField(TEXT("anim_blueprint_path"), AnimClassPath);
	if (AnimClassPath.IsEmpty()) Args->TryGetStringField(TEXT("anim_class"), AnimClassPath);
	Args->TryGetStringField(TEXT("skeletal_mesh_path"), SkeletalMeshPath);
	if (SkeletalMeshPath.IsEmpty()) Args->TryGetStringField(TEXT("mesh_path"), SkeletalMeshPath);

	if (BpPath.IsEmpty()) { OutError = TEXT("blueprint_path is required"); return; }
	if (AnimClassPath.IsEmpty()) { OutError = TEXT("anim_class_path is required (path to your ABP_X asset)"); return; }

	const FString AnimAssetName = FPaths::GetBaseFilename(AnimClassPath);
	if (AnimAssetName.Contains(TEXT("PostProcess"), ESearchCase::IgnoreCase) ||
		AnimAssetName.Contains(TEXT("PostProc"),    ESearchCase::IgnoreCase))
	{
		OutError = FString::Printf(
			TEXT("'%s' looks like a PostProcess AnimBP (path contains 'PostProcess'). PostProcess AnimBPs run as a post-pass and have no main update graph — assigning one as the AnimClass leaves the rig in T-pose. Pick the main locomotion AnimBP (e.g. ABP_Manny, not ABP_Manny_PostProcess). Set Mesh.PostProcessAnimBlueprint separately if you actually want a post pass."),
			*AnimClassPath);
		return;
	}

	UBlueprint* BP = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(BpPath));
	if (!BP) { OutError = FString::Printf(TEXT("Could not load Blueprint at path: %s"), *BpPath); return; }

	UObject* AnimAsset = UEditorAssetLibrary::LoadAsset(AnimClassPath);
	UClass* AnimClass = nullptr;
	USkeleton* AnimSkeleton = nullptr;
	if (UAnimBlueprint* AnimBP = Cast<UAnimBlueprint>(AnimAsset))
	{
		AnimClass = AnimBP->GeneratedClass;
		AnimSkeleton = AnimBP->TargetSkeleton;
	}
	else if (UClass* DirectClass = Cast<UClass>(AnimAsset))
	{
		AnimClass = DirectClass;
		if (UAnimBlueprintGeneratedClass* AnimGen = Cast<UAnimBlueprintGeneratedClass>(DirectClass))
			AnimSkeleton = AnimGen->GetTargetSkeleton();
	}
	if (!AnimClass)
	{
		OutError = FString::Printf(
			TEXT("'%s' is not an AnimBlueprint. Pass the asset path of an ABP_X (UAnimBlueprint), not the generated _C class or anything else."),
			*AnimClassPath);
		return;
	}

	USkeletalMesh* MeshAsset = nullptr;
	if (!SkeletalMeshPath.IsEmpty())
	{
		MeshAsset = Cast<USkeletalMesh>(UEditorAssetLibrary::LoadAsset(SkeletalMeshPath));
		if (!MeshAsset)
		{
			OutError = FString::Printf(TEXT("Could not load SkeletalMesh at path: %s"), *SkeletalMeshPath);
			return;
		}
	}

	UClass* ClassToSearch = BP->GeneratedClass ? BP->GeneratedClass : BP->ParentClass;
	if (!ClassToSearch) { OutError = TEXT("Blueprint has no GeneratedClass yet — compile it once first"); return; }

	USkeletalMeshComponent* MeshComp = nullptr;
	FString MatchedComponentName;
	if (AActor* CDO = Cast<AActor>(ClassToSearch->GetDefaultObject()))
	{
		if (ACharacter* CharCDO = Cast<ACharacter>(CDO))
		{
			MeshComp = CharCDO->GetMesh();
			if (MeshComp) MatchedComponentName = MeshComp->GetName();
		}
		if (!MeshComp)
		{
			TArray<UActorComponent*> Comps;
			CDO->GetComponents(Comps);
			for (UActorComponent* C : Comps)
			{
				if (USkeletalMeshComponent* SMC = Cast<USkeletalMeshComponent>(C))
				{
					MeshComp = SMC;
					MatchedComponentName = SMC->GetName();
					break;
				}
			}
		}
	}
	if (!MeshComp)
	{
		OutError = FString::Printf(TEXT("Blueprint '%s' has no SkeletalMeshComponent — is the parent class a Character / SkeletalMeshActor?"), *BpPath);
		return;
	}

	{
		USkeletalMesh* EffectiveMesh = MeshAsset ? MeshAsset : MeshComp->GetSkeletalMeshAsset();
		if (AnimSkeleton && EffectiveMesh && !AnimSkeleton->IsCompatibleMesh(EffectiveMesh))
		{
			USkeleton* MeshSkel = EffectiveMesh->GetSkeleton();
			OutError = FString::Printf(
				TEXT("Skeleton mismatch: AnimBP '%s' targets skeleton '%s', but mesh '%s' uses skeleton '%s'. ")
				TEXT("Assigning incompatible mesh+AnimBP leaves the character in a T-pose at runtime even though the Blueprint compiles. ")
				TEXT("Pick a mesh and AnimBP that share a skeleton (e.g. SKM_Manny + ABP_Manny, or SK_Mannequin + a UE4-skeleton AnimBP)%s."),
				*AnimClassPath,
				*AnimSkeleton->GetName(),
				*EffectiveMesh->GetPathName(),
				MeshSkel ? *MeshSkel->GetName() : TEXT("<none>"),
				MeshAsset ? TEXT("") : TEXT(", or pass skeletal_mesh_path to assign a matching mesh in this same call"));
			return;
		}
	}

	MeshComp->SetAnimInstanceClass(AnimClass);
	if (MeshAsset)
	{
		MeshComp->SetSkeletalMesh(MeshAsset);
	}
	MeshComp->PostEditChange();
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);

	FKismetEditorUtilities::CompileBlueprint(BP);
	UEditorAssetLibrary::SaveAsset(BpPath, false);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("blueprint_path"), BpPath);
	Result->SetStringField(TEXT("anim_class"), AnimClass->GetPathName());
	Result->SetStringField(TEXT("matched_component"), MatchedComponentName);
	if (MeshAsset) Result->SetStringField(TEXT("skeletal_mesh"), MeshAsset->GetPathName());
	FString ResultStr;
	const TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&ResultStr);
	FJsonSerializer::Serialize(Result.ToSharedRef(), W);
	OutJsonString = ResultStr;
}

}
