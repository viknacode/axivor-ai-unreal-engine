// Copyright 2026, BlueprintsLab, All rights reserved.

#include "Tools/MoverTools.h"
#include "Tools/MoverToolsShared.h"
#include "Misc/EngineVersionComparison.h"

#include "MoverComponent.h"
#include "MoverSimulationTypes.h"
#include "MoverTypes.h"
#if !UE_VERSION_OLDER_THAN(5, 5, 0)
#include "MoveLibrary/ConstrainedMoveUtils.h"
#endif
#include "DefaultMovementSet/CharacterMoverComponent.h"
#include "DefaultMovementSet/Modes/WalkingMode.h"
#include "DefaultMovementSet/Modes/FallingMode.h"
#include "DefaultMovementSet/Modes/FlyingMode.h"
#include "DefaultMovementSet/Modes/SwimmingMode.h"
#if !UE_VERSION_OLDER_THAN(5, 5, 0)
#include "DefaultMovementSet/Modes/NavWalkingMode.h"
#endif
#include "DefaultMovementSet/Settings/CommonLegacyMovementSettings.h"
#if !UE_VERSION_OLDER_THAN(5, 5, 0)
#include "DefaultMovementSet/Settings/StanceSettings.h"
#endif

#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "EditorAssetLibrary.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Character.h"

#include "MCPToolsLog.h"
#include "UECPCoreModule.h"
#include "Services/IUECPToolDispatcher.h"
#include "Tools/BatchToolHelper.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/UnrealType.h"
#include "UObject/PropertyOptional.h"

namespace MoverTools
{

UMoverComponent* FindMoverTemplate(UBlueprint* BP)
{
	if (!BP || !BP->SimpleConstructionScript) return nullptr;
	for (USCS_Node* Node : BP->SimpleConstructionScript->GetAllNodes())
	{
		if (Node && Node->ComponentClass && Node->ComponentClass->IsChildOf(UMoverComponent::StaticClass()))
			return Cast<UMoverComponent>(Node->ComponentTemplate);
	}
	return nullptr;
}

static UClass* ResolveModeClass(const FString& ModeName)
{
	static TMap<FString, FString> ModeClassMap;
	if (ModeClassMap.Num() == 0)
	{
		ModeClassMap.Add(TEXT("walking"),    TEXT("/Script/Mover.WalkingMode"));
		ModeClassMap.Add(TEXT("falling"),    TEXT("/Script/Mover.FallingMode"));
		ModeClassMap.Add(TEXT("flying"),     TEXT("/Script/Mover.FlyingMode"));
		ModeClassMap.Add(TEXT("swimming"),   TEXT("/Script/Mover.SwimmingMode"));
		ModeClassMap.Add(TEXT("navwalking"), TEXT("/Script/Mover.NavWalkingMode"));
	}
	const FString* PathPtr = ModeClassMap.Find(ModeName.ToLower());
	if (!PathPtr) return nullptr;
	UClass* Cls = FindObject<UClass>(nullptr, **PathPtr);
	if (!Cls) Cls = LoadObject<UClass>(nullptr, **PathPtr);
	return Cls;
}

FString GetBpPath(const TSharedPtr<FJsonObject>& Args)
{
	FString BpPath;
	if (Args->TryGetStringField(TEXT("blueprint_path"), BpPath) && !BpPath.IsEmpty()) return BpPath;
	FString Name, SavePath = TEXT("/Game");
	Args->TryGetStringField(TEXT("name"), Name);
	Args->TryGetStringField(TEXT("save_path"), SavePath);
	while (SavePath.EndsWith(TEXT("/"))) SavePath = SavePath.LeftChop(1);
	if (!Name.IsEmpty()) return SavePath + TEXT("/") + Name;
	return FString();
}

static FString JsonStringArray(const TArray<FString>& Arr)
{
	FString Out;
	for (int32 i = 0; i < Arr.Num(); i++)
	{
		if (i > 0) Out += TEXT(",");
		Out += TEXT("\"") + Arr[i].Replace(TEXT("\""), TEXT("\\\"")) + TEXT("\"");
	}
	return Out;
}

UClass* ResolveClassByPath(const TCHAR* Path)
{
	UClass* Cls = FindObject<UClass>(nullptr, Path);
	if (!Cls) Cls = LoadObject<UClass>(nullptr, Path);
	return Cls;
}

UClass* GetChaosMoverComponentClass()
{
	return ResolveClassByPath(TEXT("/Script/ChaosMover.ChaosCharacterMoverComponent"));
}

UClass* GetChaosSharedSettingsClass()
{
	return ResolveClassByPath(TEXT("/Script/ChaosMover.SharedChaosCharacterMovementSettings"));
}

bool IsChaosMover(UMoverComponent* MoverTemplate)
{
	UClass* ChaosCls = GetChaosMoverComponentClass();
	return ChaosCls && MoverTemplate && MoverTemplate->GetClass()->IsChildOf(ChaosCls);
}

UClass* ResolveChaosModeClass(const FString& ModeName)
{
	static TMap<FString, FString> M;
	if (M.Num() == 0)
	{
		M.Add(TEXT("walking"),  TEXT("/Script/ChaosMover.ChaosWalkingMode"));
		M.Add(TEXT("falling"),  TEXT("/Script/ChaosMover.ChaosFallingMode"));
		M.Add(TEXT("flying"),   TEXT("/Script/ChaosMover.ChaosFlyingMode"));
		M.Add(TEXT("swimming"), TEXT("/Script/ChaosMover.ChaosSwimmingMode"));
	}
	FString Key = ModeName.ToLower().Replace(TEXT("chaos"), TEXT("")).Replace(TEXT("mode"), TEXT("")).TrimStartAndEnd();
	const FString* P = M.Find(Key);
	return P ? ResolveClassByPath(**P) : nullptr;
}

enum class EMoverPropKind : uint8 { Float, Bool, Name, OptionalFloat };
struct FModePropMap { const TCHAR* Json; const TCHAR* Prop; EMoverPropKind Kind; };
static const FModePropMap GModeProps[] =
{
	{ TEXT("cancel_vertical_speed_on_landing"),          TEXT("bCancelVerticalSpeedOnLanding"),       EMoverPropKind::Bool  },
	{ TEXT("air_control"),                               TEXT("AirControlPercentage"),                EMoverPropKind::Float },
	{ TEXT("air_control_percentage"),                    TEXT("AirControlPercentage"),                EMoverPropKind::Float },
	{ TEXT("falling_deceleration"),                      TEXT("FallingDeceleration"),                 EMoverPropKind::Float },
	{ TEXT("over_terminal_speed_falling_deceleration"),  TEXT("OverTerminalSpeedFallingDeceleration"),EMoverPropKind::Float },
	{ TEXT("terminal_movement_plane_speed"),             TEXT("TerminalMovementPlaneSpeed"),          EMoverPropKind::Float },
	{ TEXT("clamp_terminal_vertical_speed"),             TEXT("bShouldClampTerminalVerticalSpeed"),   EMoverPropKind::Bool  },
	{ TEXT("vertical_falling_deceleration"),             TEXT("VerticalFallingDeceleration"),         EMoverPropKind::Float },
	{ TEXT("terminal_vertical_speed"),                   TEXT("TerminalVerticalSpeed"),               EMoverPropKind::Float },
	{ TEXT("radial_force_limit"),                        TEXT("RadialForceLimit"),                    EMoverPropKind::Float },
	{ TEXT("twist_torque_limit"),                        TEXT("TwistTorqueLimit"),                    EMoverPropKind::Float },
	{ TEXT("swing_torque_limit"),                        TEXT("SwingTorqueLimit"),                    EMoverPropKind::Float },
	{ TEXT("remain_upright"),                            TEXT("bShouldCharacterRemainUpright"),       EMoverPropKind::Bool  },
	{ TEXT("target_height"),                             TEXT("TargetHeightOverride"),                EMoverPropKind::OptionalFloat },
	{ TEXT("query_radius"),                              TEXT("QueryRadiusOverride"),                 EMoverPropKind::OptionalFloat },
	{ TEXT("ground_damping"),                            TEXT("GroundDamping"),                       EMoverPropKind::Float },
	{ TEXT("friction_force_limit"),                      TEXT("FrictionForceLimit"),                  EMoverPropKind::Float },
	{ TEXT("fractional_radial_force_limit_scaling"),     TEXT("FractionalRadialForceLimitScaling"),   EMoverPropKind::Float },
	{ TEXT("fractional_ground_reaction"),                TEXT("FractionalGroundReaction"),            EMoverPropKind::Float },
	{ TEXT("fractional_downward_velocity_to_target"),    TEXT("FractionalDownwardVelocityToTarget"),  EMoverPropKind::Float },
	{ TEXT("swimming_ideal_immersion_depth"),            TEXT("SwimmingIdealImmersionDepth"),         EMoverPropKind::Float },
};

bool RefSetFloat(UObject* Container, const TCHAR* PropName, double Val)
{
	if (FFloatProperty* P = FindFProperty<FFloatProperty>(Container->GetClass(), PropName))
		{ P->SetPropertyValue_InContainer(Container, (float)Val); return true; }
	if (FDoubleProperty* DP = FindFProperty<FDoubleProperty>(Container->GetClass(), PropName))
		{ DP->SetPropertyValue_InContainer(Container, Val); return true; }
	return false;
}

bool RefSetBool(UObject* Container, const TCHAR* PropName, bool Val)
{
	if (FBoolProperty* P = FindFProperty<FBoolProperty>(Container->GetClass(), PropName))
		{ P->SetPropertyValue_InContainer(Container, Val); return true; }
	return false;
}

bool RefSetName(UObject* Container, const TCHAR* PropName, const FString& Val)
{
	if (FNameProperty* P = FindFProperty<FNameProperty>(Container->GetClass(), PropName))
		{ P->SetPropertyValue_InContainer(Container, FName(*Val)); return true; }
	return false;
}

static bool RefSetOptionalFloat(UObject* Container, const TCHAR* PropName, double Val)
{
	FOptionalProperty* OP = FindFProperty<FOptionalProperty>(Container->GetClass(), PropName);
	if (!OP) return false;
	void* OptPtr = OP->ContainerPtrToValuePtr<void>(Container);
	void* ValPtr = OP->MarkSetAndGetInitializedValuePointerToReplace(OptPtr);
	if (FFloatProperty* Inner = CastField<FFloatProperty>(OP->GetValueProperty()))
		{ Inner->SetPropertyValue(ValPtr, (float)Val); return true; }
	if (FDoubleProperty* InnerD = CastField<FDoubleProperty>(OP->GetValueProperty()))
		{ InnerD->SetPropertyValue(ValPtr, Val); return true; }
	return false;
}

static bool RefGetFloat(UObject* Container, const TCHAR* PropName, double& Out)
{
	if (FFloatProperty* P = FindFProperty<FFloatProperty>(Container->GetClass(), PropName))
		{ Out = P->GetPropertyValue_InContainer(Container); return true; }
	if (FDoubleProperty* DP = FindFProperty<FDoubleProperty>(Container->GetClass(), PropName))
		{ Out = DP->GetPropertyValue_InContainer(Container); return true; }
	return false;
}

static bool RefGetBool(UObject* Container, const TCHAR* PropName, bool& Out)
{
	if (FBoolProperty* P = FindFProperty<FBoolProperty>(Container->GetClass(), PropName))
		{ Out = P->GetPropertyValue_InContainer(Container); return true; }
	return false;
}

static bool RefGetName(UObject* Container, const TCHAR* PropName, FString& Out)
{
	if (FNameProperty* P = FindFProperty<FNameProperty>(Container->GetClass(), PropName))
		{ Out = P->GetPropertyValue_InContainer(Container).ToString(); return true; }
	return false;
}

static bool RefGetOptionalFloat(UObject* Container, const TCHAR* PropName, double& Out)
{
	FOptionalProperty* OP = FindFProperty<FOptionalProperty>(Container->GetClass(), PropName);
	if (!OP) return false;
	const void* ValPtr = OP->GetValuePointerForReadIfSet(OP->ContainerPtrToValuePtr<void>(Container));
	if (!ValPtr) return false;
	if (FFloatProperty* Inner = CastField<FFloatProperty>(OP->GetValueProperty()))  { Out = Inner->GetPropertyValue(ValPtr);  return true; }
	if (FDoubleProperty* InnerD = CastField<FDoubleProperty>(OP->GetValueProperty())){ Out = InnerD->GetPropertyValue(ValPtr); return true; }
	return false;
}

static void SerializeModePropsInto(UObject* Mode, const TSharedPtr<FJsonObject>& MObj)
{
	if (!Mode) return;
	TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
	TSet<FString> Emitted;
	bool bAny = false;
	for (const FModePropMap& E : GModeProps)
	{
		const FString PropKey(E.Prop);
		if (Emitted.Contains(PropKey)) continue;
		switch (E.Kind)
		{
		case EMoverPropKind::Bool:          { bool B;    if (RefGetBool(Mode, E.Prop, B))          { P->SetBoolField  (E.Json, B); Emitted.Add(PropKey); bAny = true; } break; }
		case EMoverPropKind::Name:          { FString S; if (RefGetName(Mode, E.Prop, S))          { P->SetStringField(E.Json, S); Emitted.Add(PropKey); bAny = true; } break; }
		case EMoverPropKind::OptionalFloat: { double D;  if (RefGetOptionalFloat(Mode, E.Prop, D)) { P->SetNumberField(E.Json, D); Emitted.Add(PropKey); bAny = true; } break; }
		default:                            { double D;  if (RefGetFloat(Mode, E.Prop, D))         { P->SetNumberField(E.Json, D); Emitted.Add(PropKey); bAny = true; } break; }
		}
	}
	if (bAny) MObj->SetObjectField(TEXT("properties"), P);
}

UObject* FindSharedSettingsByClass(UMoverComponent* MoverTemplate, UClass* SettingsClass)
{
	if (!MoverTemplate || !SettingsClass) return nullptr;
	FArrayProperty* SharedProp = FindFProperty<FArrayProperty>(UMoverComponent::StaticClass(), TEXT("SharedSettings"));
	if (!SharedProp) return nullptr;
	FObjectProperty* ElemProp = CastField<FObjectProperty>(SharedProp->Inner);
	if (!ElemProp) return nullptr;
	void* ArrPtr = SharedProp->ContainerPtrToValuePtr<void>(MoverTemplate);
	FScriptArrayHelper ArrHelper(SharedProp, ArrPtr);
	for (int32 i = 0; i < ArrHelper.Num(); i++)
	{
		UObject* Obj = ElemProp->GetObjectPropertyValue(ArrHelper.GetRawPtr(i));
		if (Obj && Obj->GetClass()->IsChildOf(SettingsClass)) return Obj;
	}
	return nullptr;
}

static FMapProperty* GetMovementModesProp();

static UObject* FindModeByName(UMoverComponent* MoverTemplate, const FName& ModeKey)
{
	FMapProperty* Prop = GetMovementModesProp();
	if (!Prop) return nullptr;
	void* MapPtr = Prop->ContainerPtrToValuePtr<void>(MoverTemplate);
	FScriptMapHelper MapHelper(Prop, MapPtr);
	FNameProperty*   KeyProp = CastField<FNameProperty>  (Prop->KeyProp);
	FObjectProperty* ValProp = CastField<FObjectProperty>(Prop->ValueProp);
	if (!KeyProp || !ValProp) return nullptr;
	for (int32 i = 0; i < MapHelper.GetMaxIndex(); i++)
	{
		if (!MapHelper.IsValidIndex(i)) continue;
		if (KeyProp->GetPropertyValue(MapHelper.GetKeyPtr(i)) == ModeKey)
			return ValProp->GetObjectPropertyValue(MapHelper.GetValuePtr(i));
	}
	return nullptr;
}

static FMapProperty* GetMovementModesProp()
{
	static FMapProperty* CachedProp = FindFProperty<FMapProperty>(
		UMoverComponent::StaticClass(), TEXT("MovementModes"));
	return CachedProp;
}

bool SafeContainsMode(UMoverComponent* MoverTemplate, const FName& ModeKey)
{
	FMapProperty* Prop = GetMovementModesProp();
	if (!Prop) return false;
	void* MapPtr = Prop->ContainerPtrToValuePtr<void>(MoverTemplate);
	FScriptMapHelper MapHelper(Prop, MapPtr);
	FNameProperty* KeyProp = CastField<FNameProperty>(Prop->KeyProp);
	if (!KeyProp) return false;
	for (int32 i = 0; i < MapHelper.GetMaxIndex(); i++)
	{
		if (!MapHelper.IsValidIndex(i)) continue;
		if (KeyProp->GetPropertyValue(MapHelper.GetKeyPtr(i)) == ModeKey)
			return true;
	}
	return false;
}

bool SafeAddMode(UMoverComponent* MoverTemplate, const FName& ModeKey,
	UClass* ModeClass, FString& OutError)
{
	FMapProperty* Prop = GetMovementModesProp();
	if (!Prop) { OutError = TEXT("MovementModes property not found via reflection"); return false; }

	UBaseMovementMode* ModeObj = NewObject<UBaseMovementMode>(MoverTemplate, ModeClass, ModeKey);
	if (!ModeObj)
	{
		OutError = FString::Printf(TEXT("NewObject failed for mode class %s"), *ModeClass->GetName());
		return false;
	}

	void* MapPtr = Prop->ContainerPtrToValuePtr<void>(MoverTemplate);
	FScriptMapHelper MapHelper(Prop, MapPtr);

	int32 NewIdx = MapHelper.AddDefaultValue_Invalid_NeedsRehash();
	FNameProperty*   KeyProp = CastField<FNameProperty>  (Prop->KeyProp);
	FObjectProperty* ValProp = CastField<FObjectProperty>(Prop->ValueProp);
	if (KeyProp) KeyProp->SetPropertyValue(MapHelper.GetKeyPtr(NewIdx), ModeKey);
	if (ValProp) ValProp->SetObjectPropertyValue(MapHelper.GetValuePtr(NewIdx), ModeObj);

	MapHelper.Rehash();
	return true;
}

static void SafeRemoveMode(UMoverComponent* MoverTemplate, const FName& ModeKey)
{
	FMapProperty* Prop = GetMovementModesProp();
	if (!Prop) return;
	void* MapPtr = Prop->ContainerPtrToValuePtr<void>(MoverTemplate);
	FScriptMapHelper MapHelper(Prop, MapPtr);
	MapHelper.RemovePair(&ModeKey);
}

void RefreshMoverSharedSettings(UMoverComponent* MoverTemplate)
{
	if (!MoverTemplate) return;
	FMapProperty* Prop = GetMovementModesProp();
	if (!Prop) return;
	FPropertyChangedEvent ModesPCE(Prop, EPropertyChangeType::ArrayAdd);
	static_cast<UObject*>(MoverTemplate)->PostEditChangeProperty(ModesPCE);
}

void ScaffoldInputProducer(const FString& BpPath, TArray<FString>& OutSteps, TArray<FString>& OutWarnings, bool bChaosPhysicsBody)
{
	if (!IUECPCoreModule::IsAvailable()) { OutWarnings.Add(TEXT("Core unavailable — input-producer scaffold skipped.")); return; }
	IUECPToolDispatcher& D = IUECPCoreModule::Get().GetToolDispatcher();

	auto Run = [&](const TCHAR* Tool, const TSharedPtr<FJsonObject>& Args, const TCHAR* Label) -> bool
	{
		if (!D.IsRegistered(FName(Tool)))
		{
			OutWarnings.Add(FString::Printf(TEXT("%s: tool '%s' not registered — enable the Blueprint extension."), Label, Tool));
			return false;
		}
		const FUECPToolResult R = D.ExecuteFromArgs(FName(Tool), Args);
		if (!R.bSuccess) { OutWarnings.Add(FString::Printf(TEXT("%s failed: %s"), Label, *R.ErrorMessage)); return false; }
		OutSteps.Add(Label);
		return true;
	};

	{
		TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>();
		A->SetStringField(TEXT("blueprint_path"), BpPath);
		A->SetStringField(TEXT("component_class"), TEXT("CapsuleComponent"));
		A->SetStringField(TEXT("component_name"), TEXT("Capsule"));
		Run(TEXT("add_component"), A, TEXT("add Capsule"));
	}
	{
		TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>();
		A->SetStringField(TEXT("blueprint_path"), BpPath);
		A->SetStringField(TEXT("component_name"), TEXT("Capsule"));
		Run(TEXT("set_root_component"), A, TEXT("Capsule -> root"));
	}
	{
		TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>();
		A->SetStringField(TEXT("blueprint_path"), BpPath);
		A->SetStringField(TEXT("component_name"), TEXT("Capsule"));
		A->SetStringField(TEXT("profile_name"), TEXT("Pawn"));
		Run(TEXT("set_component_collision_profile"), A, TEXT("Capsule collision=Pawn"));
	}
	if (bChaosPhysicsBody)
	{
		auto SetBody = [&](const TCHAR* Prop, const TCHAR* Label)
		{
			TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>();
			A->SetStringField(TEXT("blueprint_path"), BpPath);
			A->SetStringField(TEXT("component_name"), TEXT("Capsule"));
			A->SetStringField(TEXT("property_name"), Prop);
			A->SetStringField(TEXT("value"), TEXT("true"));
			Run(TEXT("edit_component_property"), A, Label);
		};
		SetBody(TEXT("BodyInstance.bSimulatePhysics"), TEXT("Capsule simulate physics"));
		SetBody(TEXT("BodyInstance.bUpdateKinematicFromSimulation"), TEXT("Capsule update-kinematic-from-sim"));
	}
	{
		TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>();
		A->SetStringField(TEXT("blueprint_path"), BpPath);
		A->SetStringField(TEXT("interface_path"), TEXT("/Script/Mover.MoverInputProducerInterface"));
		Run(TEXT("implement_interface"), A, TEXT("implement MoverInputProducerInterface"));
	}
	{
		TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>();
		A->SetStringField(TEXT("blueprint_path"), BpPath);
		A->SetStringField(TEXT("variable_name"), TEXT("MoveInput"));
		A->SetStringField(TEXT("variable_type"), TEXT("vector"));
		Run(TEXT("add_variable"), A, TEXT("add MoveInput var"));
	}
	{
		TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>();
		A->SetStringField(TEXT("blueprint_path"), BpPath);
		A->SetStringField(TEXT("property_name"), TEXT("bReplicateMovement"));
		A->SetStringField(TEXT("value"), TEXT("false"));
		Run(TEXT("set_cdo_property"), A, TEXT("disable bReplicateMovement"));
	}
	{
		TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>();
		A->SetStringField(TEXT("blueprint_path"), BpPath);
		A->SetStringField(TEXT("function_name"), TEXT("ProduceInput"));
		TArray<TSharedPtr<FJsonValue>> Items;
		{ auto I = MakeShared<FJsonObject>(); I->SetStringField(TEXT("name"), TEXT("Inputs")); I->SetStringField(TEXT("type"), TEXT("FCharacterDefaultInputs")); Items.Add(MakeShared<FJsonValueObject>(I)); }
		{ auto I = MakeShared<FJsonObject>(); I->SetStringField(TEXT("name"), TEXT("Coll"));   I->SetStringField(TEXT("type"), TEXT("FMoverDataCollection"));    Items.Add(MakeShared<FJsonValueObject>(I)); }
		A->SetArrayField(TEXT("items"), Items);
		Run(TEXT("add_local_variable"), A, TEXT("add ProduceInput locals"));
	}
	{
		auto Node = [](const TCHAR* Id, const TCHAR* Handle) { auto O = MakeShared<FJsonObject>(); O->SetStringField(TEXT("id"), Id); O->SetStringField(TEXT("handle"), Handle); return MakeShared<FJsonValueObject>(O); };
		auto Conn = [](const TCHAR* F, const TCHAR* FP, const TCHAR* T, const TCHAR* TP) { auto O = MakeShared<FJsonObject>(); O->SetStringField(TEXT("from"), F); O->SetStringField(TEXT("from_pin"), FP); O->SetStringField(TEXT("to"), T); O->SetStringField(TEXT("to_pin"), TP); return MakeShared<FJsonValueObject>(O); };

		TArray<TSharedPtr<FJsonValue>> Nodes = {
			Node(TEXT("getInputs"), TEXT("var.get.Inputs")),
			Node(TEXT("getColl"),   TEXT("var.get.Coll")),
			Node(TEXT("getMove"),   TEXT("var.get.MoveInput")),
			Node(TEXT("setDir"),    TEXT("fn.MoverDataModelBlueprintLibrary.SetDirectionalInput")),
			Node(TEXT("addData"),   TEXT("fn.MoverDataCollectionLibrary.K2_AddDataToCollection")),
			Node(TEXT("makeCmd"),   TEXT("k2.Make MoverInputCmdContext")),
		};
		TArray<TSharedPtr<FJsonValue>> Conns = {
			Conn(TEXT("entry"),     TEXT("then"),               TEXT("setDir"),  TEXT("execute")),
			Conn(TEXT("setDir"),    TEXT("then"),               TEXT("addData"), TEXT("execute")),
			Conn(TEXT("addData"),   TEXT("then"),               TEXT("return"),  TEXT("execute")),
			Conn(TEXT("getInputs"), TEXT("Inputs"),             TEXT("setDir"),  TEXT("Inputs")),
			Conn(TEXT("getMove"),   TEXT("MoveInput"),          TEXT("setDir"),  TEXT("DirectionInput")),
			Conn(TEXT("getColl"),   TEXT("Coll"),               TEXT("addData"), TEXT("Collection")),
			Conn(TEXT("getInputs"), TEXT("Inputs"),             TEXT("addData"), TEXT("SourceAsRawBytes")),
			Conn(TEXT("getColl"),   TEXT("Coll"),               TEXT("makeCmd"), TEXT("InputCollection")),
			Conn(TEXT("makeCmd"),   TEXT("MoverInputCmdContext"),TEXT("return"), TEXT("InputCmdResult")),
		};
		TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>();
		A->SetStringField(TEXT("blueprint_path"), BpPath);
		A->SetStringField(TEXT("graph_name"), TEXT("ProduceInput"));
		A->SetBoolField(TEXT("clear_before_build"), true);
		A->SetArrayField(TEXT("nodes"), Nodes);
		A->SetArrayField(TEXT("connections"), Conns);
		Run(TEXT("build_blueprint_graph"), A, TEXT("build ProduceInput graph"));
	}

	UE_LOG(LogMCPTool, Log, TEXT("[Mover] ScaffoldInputProducer %s: %d steps, %d warnings"), *BpPath, OutSteps.Num(), OutWarnings.Num());
}

static void SetupMoverCharacterSingle(const TSharedPtr<FJsonObject>& Item, FString& OutJson, FString& OutError)
{
	FString BpPath = GetBpPath(Item);
	if (BpPath.IsEmpty()) { OutError = TEXT("name or blueprint_path required"); return; }

	FString Name, SavePath = TEXT("/Game/Characters");
	Item->TryGetStringField(TEXT("name"), Name);
	Item->TryGetStringField(TEXT("save_path"), SavePath);
	if (Name.IsEmpty())
	{
		int32 Idx; BpPath.FindLastChar('/', Idx);
		Name     = (Idx != INDEX_NONE) ? BpPath.Mid(Idx + 1) : BpPath;
		SavePath = (Idx != INDEX_NONE) ? BpPath.Left(Idx)    : TEXT("/Game/Characters");
	}
	while (SavePath.EndsWith(TEXT("/"))) SavePath = SavePath.LeftChop(1);

	FString ParentClassStr = TEXT("Pawn");
	FString StartingMode   = TEXT("Walking");
	Item->TryGetStringField(TEXT("parent_class"),  ParentClassStr);
	Item->TryGetStringField(TEXT("starting_mode"), StartingMode);

	TArray<FString> Modes;
	const TArray<TSharedPtr<FJsonValue>>* ModesArr = nullptr;
	if (Item->TryGetArrayField(TEXT("modes"), ModesArr) && ModesArr)
		for (auto& V : *ModesArr) Modes.Add(V->AsString());
	if (Modes.IsEmpty()) { Modes.Add(TEXT("Walking")); Modes.Add(TEXT("Falling")); }

	UClass* ParentClass = APawn::StaticClass();
	{
		const FString PC = ParentClassStr.ToLower();
		if      (PC == TEXT("character"))  ParentClass = ACharacter::StaticClass();
		else if (PC == TEXT("actor"))      ParentClass = AActor::StaticClass();
		else
		{
			UClass* Found = FindFirstObjectSafe<UClass>(*ParentClassStr);
			if (!Found) Found = LoadObject<UClass>(nullptr, *(TEXT("/Script/Engine.") + ParentClassStr));
			if (!Found) Found = LoadObject<UClass>(nullptr, *(TEXT("/Script/Mover.") + ParentClassStr));
			if (Found) ParentClass = Found;
		}
	}

	UBlueprint* BP = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(BpPath));
	const bool bExisted = (BP != nullptr);
	if (!BP)
	{
		FString PackagePath = SavePath + TEXT("/") + Name;
		if (!FPackageName::DoesPackageExist(PackagePath))
		{
			BP = FKismetEditorUtilities::CreateBlueprint(
				ParentClass,
				CreatePackage(*PackagePath),
				FName(*Name),
				BPTYPE_Normal,
				UBlueprint::StaticClass(),
				UBlueprintGeneratedClass::StaticClass());
		}
		else
		{
			BP = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(BpPath));
		}
	}
	if (!BP) { OutError = FString::Printf(TEXT("Failed to create or load Blueprint at: %s"), *BpPath); return; }

	UMoverComponent* MoverTemplate = FindMoverTemplate(BP);
	if (!MoverTemplate)
	{
		UCharacterMoverComponent* NewComp = NewObject<UCharacterMoverComponent>(
			GetTransientPackage(), UCharacterMoverComponent::StaticClass(), TEXT("MoverComponent"));
		TArray<UActorComponent*> Comps; Comps.Add(NewComp);
		FKismetEditorUtilities::FAddComponentsToBlueprintParams Params;
		FKismetEditorUtilities::AddComponentsToBlueprint(BP, Comps, Params);
		MoverTemplate = FindMoverTemplate(BP);
	}
	if (!MoverTemplate) { OutError = TEXT("Failed to add UCharacterMoverComponent to blueprint"); return; }

	MoverTemplate->Modify();

	TArray<FString> Added, AlreadyPresent, Failed;
	for (const FString& ModeStr : Modes)
	{
		FName ModeKey(*ModeStr);
		if (SafeContainsMode(MoverTemplate, ModeKey)) { AlreadyPresent.Add(ModeStr); continue; }
		UClass* ModeClass = ResolveModeClass(ModeStr);
		if (!ModeClass) { Failed.Add(ModeStr); continue; }
		FString AddError;
		if (!SafeAddMode(MoverTemplate, ModeKey, ModeClass, AddError)) { Failed.Add(ModeStr + TEXT(": ") + AddError); continue; }
		Added.Add(ModeStr);
	}

	MoverTemplate->StartingMovementMode = FName(*StartingMode);

	{
		FPropertyChangedEvent ModesPCE(GetMovementModesProp(), EPropertyChangeType::ArrayAdd);
		static_cast<UObject*>(MoverTemplate)->PostEditChangeProperty(ModesPCE);
	}

	FKismetEditorUtilities::CompileBlueprint(BP);
	UEditorAssetLibrary::SaveAsset(BpPath, false);

	bool bScaffoldInput = true;
	Item->TryGetBoolField(TEXT("scaffold_input"), bScaffoldInput);
	TArray<FString> ScaffoldSteps, ScaffoldWarnings;
	if (bScaffoldInput && !bExisted)
	{
		ScaffoldInputProducer(BpPath, ScaffoldSteps, ScaffoldWarnings);
	}

	UE_LOG(LogMCPTool, Log, TEXT("[Mover] setup_mover_character: %s — modes added: [%s]"),
		*BpPath, *FString::Join(Added, TEXT(",")));

	TSharedPtr<FJsonObject> Res = MakeShared<FJsonObject>();
	Res->SetBoolField(TEXT("success"), true);
	Res->SetStringField(TEXT("blueprint_path"), BpPath);
	Res->SetStringField(TEXT("starting_mode"), StartingMode);
	if (bScaffoldInput && !bExisted)
	{
		Res->SetBoolField(TEXT("input_producer_scaffolded"), ScaffoldWarnings.Num() == 0);
		Res->SetStringField(TEXT("input_hint"), TEXT("Functional Mover character: feed the 'MoveInput' variable (world-space direction) each frame to drive it — e.g. from Enhanced Input, or set its default for a constant-move test."));
		if (ScaffoldWarnings.Num() > 0)
		{
			TArray<TSharedPtr<FJsonValue>> W; for (auto& S : ScaffoldWarnings) W.Add(MakeShared<FJsonValueString>(S));
			Res->SetArrayField(TEXT("scaffold_warnings"), W);
		}
	}
	TArray<TSharedPtr<FJsonValue>> AddedArr;
	for (auto& S : Added) AddedArr.Add(MakeShared<FJsonValueString>(S));
	Res->SetArrayField(TEXT("modes_added"), AddedArr);
	if (AlreadyPresent.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> SkipArr;
		for (auto& S : AlreadyPresent) SkipArr.Add(MakeShared<FJsonValueString>(S));
		Res->SetArrayField(TEXT("modes_already_present"), SkipArr);
	}
	if (Failed.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> FailArr;
		for (auto& S : Failed) FailArr.Add(MakeShared<FJsonValueString>(S));
		Res->SetArrayField(TEXT("modes_failed_to_resolve"), FailArr);
	}
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJson);
	FJsonSerializer::Serialize(Res.ToSharedRef(), Writer);
}

void HandleSetupMoverCharacterFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("blueprints"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString ItemOut, ItemErr;
			SetupMoverCharacterSingle(Item, ItemOut, ItemErr);
			if (ItemErr.IsEmpty()) { auto E = MakeShared<FJsonObject>(); E->SetStringField(TEXT("blueprint_path"), GetBpPath(Item)); Batch.AddSuccess(i, E); }
			else Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJson);
		return;
	}
	SetupMoverCharacterSingle(Args, OutJson, OutError);
}

void HandleConfigureMoverSettingsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	const FString BpPath = GetBpPath(Args);
	if (BpPath.IsEmpty()) { OutError = TEXT("blueprint_path required"); return; }

	UBlueprint* BP = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(BpPath));
	if (!BP) { OutError = FString::Printf(TEXT("Could not load Blueprint: %s"), *BpPath); return; }

	UMoverComponent* MoverTemplate = FindMoverTemplate(BP);
	if (!MoverTemplate) { OutError = TEXT("No MoverComponent found. Call setup_mover_character first."); return; }

	UCommonLegacyMovementSettings* Settings = MoverTemplate->FindSharedSettings_Mutable<UCommonLegacyMovementSettings>();
	if (!Settings) { OutError = TEXT("No CommonLegacyMovementSettings found. Call setup_mover_character (adds Walking/Falling modes) first."); return; }

	Settings->Modify();
	TArray<FString> Changed;

	auto TrySetFloat = [&](const TCHAR* Key, float& Field)
	{
		double Val = -1.0;
		if (Args->TryGetNumberField(Key, Val) && Val >= 0.0) { Field = (float)Val; Changed.Add(FString::Printf(TEXT("%s=%.2f"), Key, Field)); }
	};
	auto TrySetFloatAny = [&](const TCHAR* Key, float& Field)
	{
		double Val;
		if (Args->TryGetNumberField(Key, Val)) { Field = (float)Val; Changed.Add(FString::Printf(TEXT("%s=%.2f"), Key, Field)); }
	};
	auto TrySetBool = [&](const TCHAR* Key, bool& Field)
	{
		bool Val;
		if (Args->TryGetBoolField(Key, Val)) { Field = Val; Changed.Add(FString::Printf(TEXT("%s=%s"), Key, Val ? TEXT("true") : TEXT("false"))); }
	};

	TrySetFloat   (TEXT("max_speed"),                    Settings->MaxSpeed);
	TrySetFloat   (TEXT("acceleration"),                 Settings->Acceleration);
	TrySetFloat   (TEXT("deceleration"),                 Settings->Deceleration);
	TrySetFloat   (TEXT("jump_speed"),                   Settings->JumpUpwardsSpeed);
	TrySetFloatAny(TEXT("turning_rate"),                 Settings->TurningRate);
	TrySetFloat   (TEXT("turning_boost"),                Settings->TurningBoost);
	TrySetFloat   (TEXT("ground_friction"),              Settings->GroundFriction);
	TrySetFloat   (TEXT("braking_friction"),             Settings->BrakingFriction);
	TrySetFloat   (TEXT("braking_friction_factor"),      Settings->BrakingFrictionFactor);
	TrySetFloat   (TEXT("max_step_height"),              Settings->MaxStepHeight);
	TrySetFloat   (TEXT("floor_sweep_distance"),         Settings->FloorSweepDistance);
	TrySetFloat   (TEXT("walk_slope_cosine"),            Settings->MaxWalkSlopeCosine);
	TrySetBool    (TEXT("ignore_base_rotation"),         Settings->bIgnoreBaseRotation);
	TrySetFloat   (TEXT("swimming_start_immersion_depth"), Settings->SwimmingStartImmersionDepth);
	TrySetFloat   (TEXT("swimming_ideal_immersion_depth"), Settings->SwimmingIdealImmersionDepth);
	TrySetFloat   (TEXT("swimming_stop_immersion_depth"),  Settings->SwimmingStopImmersionDepth);
	{
		FString MN;
		if (Args->TryGetStringField(TEXT("ground_movement_mode_name"), MN) && !MN.IsEmpty())
			{ Settings->GroundMovementModeName = FName(*MN); Changed.Add(TEXT("ground_movement_mode_name=") + MN); }
		if (Args->TryGetStringField(TEXT("air_movement_mode_name"), MN) && !MN.IsEmpty())
			{ Settings->AirMovementModeName = FName(*MN); Changed.Add(TEXT("air_movement_mode_name=") + MN); }
#if !UE_VERSION_OLDER_THAN(5, 5, 0)
		if (Args->TryGetStringField(TEXT("swimming_movement_mode_name"), MN) && !MN.IsEmpty())
			{ Settings->SwimmingMovementModeName = FName(*MN); Changed.Add(TEXT("swimming_movement_mode_name=") + MN); }
#endif
	}
	{
		bool Val;
		if (Args->TryGetBoolField(TEXT("use_separate_braking_friction"), Val))
		{
			Settings->bUseSeparateBrakingFriction = Val ? 1 : 0;
			Changed.Add(FString::Printf(TEXT("use_separate_braking_friction=%s"), Val ? TEXT("true") : TEXT("false")));
		}
	}

	MoverTemplate->Modify();
	FKismetEditorUtilities::CompileBlueprint(BP);
	UEditorAssetLibrary::SaveAsset(BpPath, false);

	UE_LOG(LogMCPTool, Log, TEXT("[Mover] configure_mover_settings: %s — [%s]"), *BpPath, *FString::Join(Changed, TEXT(",")));

	OutJson = FString::Printf(TEXT("{\"success\":true,\"blueprint_path\":\"%s\",\"changed\":[%s]}"),
		*BpPath, *JsonStringArray(Changed));
}

void HandleConfigureStanceSettingsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
#if UE_VERSION_OLDER_THAN(5, 5, 0)
	OutError = TEXT("configure_stance_settings requires UE 5.5+ (UStanceSettings was added in 5.5).");
	return;
#else
	const FString BpPath = GetBpPath(Args);
	if (BpPath.IsEmpty()) { OutError = TEXT("blueprint_path required"); return; }

	UBlueprint* BP = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(BpPath));
	if (!BP) { OutError = FString::Printf(TEXT("Could not load Blueprint: %s"), *BpPath); return; }

	UMoverComponent* MoverTemplate = FindMoverTemplate(BP);
	if (!MoverTemplate) { OutError = TEXT("No MoverComponent found. Call setup_mover_character first."); return; }

	UStanceSettings* Settings = MoverTemplate->FindSharedSettings_Mutable<UStanceSettings>();
	if (!Settings)
	{
		FArrayProperty* SharedProp = FindFProperty<FArrayProperty>(UMoverComponent::StaticClass(), TEXT("SharedSettings"));
		if (SharedProp)
		{
			MoverTemplate->Modify();
			void* ArrPtr  = SharedProp->ContainerPtrToValuePtr<void>(MoverTemplate);
			FScriptArrayHelper ArrHelper(SharedProp, ArrPtr);
			UStanceSettings* NewStance = NewObject<UStanceSettings>(MoverTemplate, TEXT("StanceSettings"), RF_Transactional);
			int32 NewIdx  = ArrHelper.AddValue();
			FObjectProperty* ElemProp = CastField<FObjectProperty>(SharedProp->Inner);
			if (ElemProp) ElemProp->SetObjectPropertyValue(ArrHelper.GetRawPtr(NewIdx), NewStance);
			Settings = NewStance;
		}
	}
	if (!Settings) { OutError = TEXT("Failed to create or find StanceSettings on the MoverComponent."); return; }

	Settings->Modify();
	TArray<FString> Changed;

	auto TrySetFloat = [&](const TCHAR* Key, float& Field)
	{
		double Val;
		if (Args->TryGetNumberField(Key, Val) && Val >= 0.0) { Field = (float)Val; Changed.Add(FString::Printf(TEXT("%s=%.2f"), Key, Field)); }
	};

	TrySetFloat(TEXT("crouch_max_speed"),        Settings->CrouchingMaxSpeed);
	TrySetFloat(TEXT("crouch_max_acceleration"), Settings->CrouchingMaxAcceleration);
	TrySetFloat(TEXT("crouch_half_height"),      Settings->CrouchHalfHeight);
	TrySetFloat(TEXT("crouched_eye_height"),     Settings->CrouchedEyeHeight);

	MoverTemplate->Modify();
	FKismetEditorUtilities::CompileBlueprint(BP);
	UEditorAssetLibrary::SaveAsset(BpPath, false);

	UE_LOG(LogMCPTool, Log, TEXT("[Mover] configure_stance_settings: %s — [%s]"), *BpPath, *FString::Join(Changed, TEXT(",")));

	OutJson = FString::Printf(TEXT("{\"success\":true,\"blueprint_path\":\"%s\",\"changed\":[%s]}"),
		*BpPath, *JsonStringArray(Changed));
#endif
}

static void AddMovementModeSingle(const FString& BpPath, const FString& ModeName, const FString& ModeClassHint, FString& OutJson, FString& OutError)
{
	UBlueprint* BP = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(BpPath));
	if (!BP) { OutError = FString::Printf(TEXT("Could not load Blueprint: %s"), *BpPath); return; }

	UMoverComponent* MoverTemplate = FindMoverTemplate(BP);
	if (!MoverTemplate) { OutError = TEXT("No MoverComponent found. Call setup_mover_character first."); return; }

	FName ModeKey(*ModeName);
	if (SafeContainsMode(MoverTemplate, ModeKey))
	{
		OutJson = FString::Printf(TEXT("{\"success\":true,\"note\":\"Mode '%s' already present\",\"blueprint_path\":\"%s\"}"), *ModeName, *BpPath);
		return;
	}

	const bool bChaos = IsChaosMover(MoverTemplate);
	UClass* ModeClass = bChaos ? ResolveChaosModeClass(ModeName) : ResolveModeClass(ModeName);
	if (!ModeClass && !ModeClassHint.IsEmpty())
	{
		ModeClass = FindObject<UClass>(nullptr, *ModeClassHint);
		if (!ModeClass) ModeClass = LoadObject<UClass>(nullptr, *ModeClassHint);
	}
	if (!ModeClass)
	{
		OutError = bChaos
			? FString::Printf(TEXT("Unknown Chaos mode '%s'. Valid: Walking, Falling, Flying, Swimming (or supply mode_class path)"), *ModeName)
			: FString::Printf(TEXT("Unknown mode '%s'. Valid: Walking, Falling, Flying, Swimming, NavWalking (or supply mode_class path)"), *ModeName);
		return;
	}

	MoverTemplate->Modify();
	if (!SafeAddMode(MoverTemplate, ModeKey, ModeClass, OutError)) return;
	FPropertyChangedEvent ModesPCE(GetMovementModesProp(), EPropertyChangeType::ArrayAdd);
	static_cast<UObject*>(MoverTemplate)->PostEditChangeProperty(ModesPCE);
	FKismetEditorUtilities::CompileBlueprint(BP);
	UEditorAssetLibrary::SaveAsset(BpPath, false);

	OutJson = FString::Printf(TEXT("{\"success\":true,\"mode_name\":\"%s\",\"blueprint_path\":\"%s\"}"), *ModeName, *BpPath);
}

void HandleAddMovementModeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	const FString BpPath = GetBpPath(Args);
	if (BpPath.IsEmpty()) { OutError = TEXT("blueprint_path required"); return; }

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("modes"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			FString ModeName, ModeClass;
			if ((*ItemsArray)[i]->Type == EJson::String)
			{
				ModeName = (*ItemsArray)[i]->AsString();
			}
			else
			{
				TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
				if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
				if (!Item->TryGetStringField(TEXT("mode_name"), ModeName)) Item->TryGetStringField(TEXT("name"), ModeName);
				Item->TryGetStringField(TEXT("mode_class"), ModeClass);
			}
			FString ItemOut, ItemErr;
			AddMovementModeSingle(BpPath, ModeName, ModeClass, ItemOut, ItemErr);
			if (ItemErr.IsEmpty()) { auto E = MakeShared<FJsonObject>(); E->SetStringField(TEXT("mode_name"), ModeName); Batch.AddSuccess(i, E); }
			else Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJson);
		return;
	}

	FString ModeName, ModeClass;
	if (!Args->TryGetStringField(TEXT("mode_name"), ModeName)) Args->TryGetStringField(TEXT("name"), ModeName);
	Args->TryGetStringField(TEXT("mode_class"), ModeClass);
	AddMovementModeSingle(BpPath, ModeName, ModeClass, OutJson, OutError);
}

static void RemoveMovementModeSingle(const FString& BpPath, const FString& ModeName, FString& OutJson, FString& OutError)
{
	UBlueprint* BP = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(BpPath));
	if (!BP) { OutError = FString::Printf(TEXT("Could not load Blueprint: %s"), *BpPath); return; }

	UMoverComponent* MoverTemplate = FindMoverTemplate(BP);
	if (!MoverTemplate) { OutError = TEXT("No MoverComponent found."); return; }

	FName ModeKey(*ModeName);
	if (!SafeContainsMode(MoverTemplate, ModeKey))
	{
		OutJson = FString::Printf(TEXT("{\"success\":true,\"note\":\"Mode '%s' not found (nothing removed)\",\"blueprint_path\":\"%s\"}"), *ModeName, *BpPath);
		return;
	}

	MoverTemplate->Modify();
	SafeRemoveMode(MoverTemplate, ModeKey);
	FKismetEditorUtilities::CompileBlueprint(BP);
	UEditorAssetLibrary::SaveAsset(BpPath, false);

	OutJson = FString::Printf(TEXT("{\"success\":true,\"removed\":\"%s\",\"blueprint_path\":\"%s\"}"), *ModeName, *BpPath);
}

void HandleRemoveMovementModeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	const FString BpPath = GetBpPath(Args);
	if (BpPath.IsEmpty()) { OutError = TEXT("blueprint_path required"); return; }

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("modes"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			FString ModeName;
			if ((*ItemsArray)[i]->Type == EJson::String) ModeName = (*ItemsArray)[i]->AsString();
			else
			{
				TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
				if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
				if (!Item->TryGetStringField(TEXT("mode_name"), ModeName)) Item->TryGetStringField(TEXT("name"), ModeName);
			}
			FString ItemOut, ItemErr;
			RemoveMovementModeSingle(BpPath, ModeName, ItemOut, ItemErr);
			if (ItemErr.IsEmpty()) { auto E = MakeShared<FJsonObject>(); E->SetStringField(TEXT("mode_name"), ModeName); Batch.AddSuccess(i, E); }
			else Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJson);
		return;
	}

	FString ModeName;
	if (!Args->TryGetStringField(TEXT("mode_name"), ModeName)) Args->TryGetStringField(TEXT("name"), ModeName);
	RemoveMovementModeSingle(BpPath, ModeName, OutJson, OutError);
}

void HandleSetStartingMovementModeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	const FString BpPath = GetBpPath(Args);
	if (BpPath.IsEmpty()) { OutError = TEXT("blueprint_path required"); return; }
	FString StartingMode;
	if (!Args->TryGetStringField(TEXT("starting_mode"), StartingMode) || StartingMode.IsEmpty())
		if (!Args->TryGetStringField(TEXT("mode_name"), StartingMode) || StartingMode.IsEmpty())
		{ OutError = TEXT("starting_mode required"); return; }

	UBlueprint* BP = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(BpPath));
	if (!BP) { OutError = FString::Printf(TEXT("Could not load Blueprint: %s"), *BpPath); return; }

	UMoverComponent* MoverTemplate = FindMoverTemplate(BP);
	if (!MoverTemplate) { OutError = TEXT("No MoverComponent found. Call setup_mover_character first."); return; }

	MoverTemplate->Modify();
	MoverTemplate->StartingMovementMode = FName(*StartingMode);
	FKismetEditorUtilities::CompileBlueprint(BP);
	UEditorAssetLibrary::SaveAsset(BpPath, false);

	OutJson = FString::Printf(TEXT("{\"success\":true,\"starting_mode\":\"%s\",\"blueprint_path\":\"%s\"}"), *StartingMode, *BpPath);
}

void HandleConfigureMoverComponentFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	const FString BpPath = GetBpPath(Args);
	if (BpPath.IsEmpty()) { OutError = TEXT("blueprint_path required"); return; }

	UBlueprint* BP = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(BpPath));
	if (!BP) { OutError = FString::Printf(TEXT("Could not load Blueprint: %s"), *BpPath); return; }

	UMoverComponent* MoverTemplate = FindMoverTemplate(BP);
	if (!MoverTemplate) { OutError = TEXT("No MoverComponent found. Call setup_mover_character first."); return; }

	MoverTemplate->Modify();
	TArray<FString> Changed;

	auto TryGetVec = [&](const TCHAR* Key, FVector& Out) -> bool
	{
		const TSharedPtr<FJsonObject>* VObj;
		if (!Args->TryGetObjectField(Key, VObj)) return false;
		double X = 0, Y = 0, Z = 0;
		(*VObj)->TryGetNumberField(TEXT("x"), X); (*VObj)->TryGetNumberField(TEXT("X"), X);
		(*VObj)->TryGetNumberField(TEXT("y"), Y); (*VObj)->TryGetNumberField(TEXT("Y"), Y);
		(*VObj)->TryGetNumberField(TEXT("z"), Z); (*VObj)->TryGetNumberField(TEXT("Z"), Z);
		Out = FVector(X, Y, Z);
		return true;
	};

#if !UE_VERSION_OLDER_THAN(5, 5, 0)
	FString SmoothingStr;
	if (Args->TryGetStringField(TEXT("smoothing_mode"), SmoothingStr))
	{
		EMoverSmoothingMode NewMode = EMoverSmoothingMode::VisualComponentOffset;
		if (SmoothingStr.Equals(TEXT("None"), ESearchCase::IgnoreCase))
			NewMode = EMoverSmoothingMode::None;
		MoverTemplate->SmoothingMode = NewMode;
		Changed.Add(TEXT("smoothing_mode=") + SmoothingStr);
	}
#endif

	auto SetBoolProp = [&](const TCHAR* PropName, bool Val)
	{
		FBoolProperty* P = FindFProperty<FBoolProperty>(UMoverComponent::StaticClass(), PropName);
		if (P) P->SetPropertyValue_InContainer(MoverTemplate, Val);
	};

	bool bVal;
	if (Args->TryGetBoolField(TEXT("gravity_override"), bVal))
	{
		SetBoolProp(TEXT("bHasGravityOverride"), bVal);
		Changed.Add(FString::Printf(TEXT("gravity_override=%s"), bVal ? TEXT("true") : TEXT("false")));
	}
	FVector GravVec;
	if (TryGetVec(TEXT("gravity_vector"), GravVec))
	{
		FStructProperty* P = FindFProperty<FStructProperty>(UMoverComponent::StaticClass(), TEXT("GravityAccelOverride"));
		if (P) *P->ContainerPtrToValuePtr<FVector>(MoverTemplate) = GravVec;
		Changed.Add(FString::Printf(TEXT("gravity_vector=(%.1f,%.1f,%.1f)"), GravVec.X, GravVec.Y, GravVec.Z));
	}
	if (Args->TryGetBoolField(TEXT("supports_kinematic_based_movement"), bVal))
	{
		SetBoolProp(TEXT("bSupportsKinematicBasedMovement"), bVal);
		Changed.Add(FString::Printf(TEXT("supports_kinematic_based_movement=%s"), bVal ? TEXT("true") : TEXT("false")));
	}

#if !UE_VERSION_OLDER_THAN(5, 5, 0)
	FStructProperty* PlanarProp = FindFProperty<FStructProperty>(UMoverComponent::StaticClass(), TEXT("PlanarConstraint"));
	if (PlanarProp)
	{
		FPlanarConstraint* PC = PlanarProp->ContainerPtrToValuePtr<FPlanarConstraint>(MoverTemplate);
		if (PC)
		{
			if (Args->TryGetBoolField(TEXT("planar_constraint_enabled"), bVal))
				{ PC->bConstrainToPlane = bVal; Changed.Add(FString::Printf(TEXT("planar_constraint_enabled=%s"), bVal ? TEXT("true") : TEXT("false"))); }
			FVector PlaneNorm;
			if (TryGetVec(TEXT("plane_normal"), PlaneNorm))
				{ PC->PlaneConstraintNormal = PlaneNorm; Changed.Add(FString::Printf(TEXT("plane_normal=(%.2f,%.2f,%.2f)"), PlaneNorm.X, PlaneNorm.Y, PlaneNorm.Z)); }
			FVector PlaneOrigin;
			if (TryGetVec(TEXT("plane_origin"), PlaneOrigin))
				{ PC->PlaneConstraintOrigin = PlaneOrigin; Changed.Add(FString::Printf(TEXT("plane_origin=(%.1f,%.1f,%.1f)"), PlaneOrigin.X, PlaneOrigin.Y, PlaneOrigin.Z)); }
		}
	}
#endif

	if (Changed.IsEmpty()) { OutError = TEXT("No recognised parameters provided"); return; }

	FKismetEditorUtilities::CompileBlueprint(BP);
	UEditorAssetLibrary::SaveAsset(BpPath, false);

	UE_LOG(LogMCPTool, Log, TEXT("[Mover] configure_mover_component: %s — [%s]"), *BpPath, *FString::Join(Changed, TEXT(",")));
	OutJson = FString::Printf(TEXT("{\"success\":true,\"blueprint_path\":\"%s\",\"changed\":[%s]}"),
		*BpPath, *JsonStringArray(Changed));
}

void HandleGetMoverSummaryFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	const FString BpPath = GetBpPath(Args);
	if (BpPath.IsEmpty()) { OutError = TEXT("blueprint_path required"); return; }

	UBlueprint* BP = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(BpPath));
	if (!BP) { OutError = FString::Printf(TEXT("Could not load Blueprint: %s"), *BpPath); return; }

	TSharedPtr<FJsonObject> Res = MakeShared<FJsonObject>();
	Res->SetBoolField(TEXT("success"), true);
	Res->SetStringField(TEXT("blueprint_path"), BpPath);

	UMoverComponent* MoverTemplate = FindMoverTemplate(BP);
	if (!MoverTemplate)
	{
		Res->SetBoolField(TEXT("has_mover_component"), false);
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJson);
		FJsonSerializer::Serialize(Res.ToSharedRef(), Writer);
		return;
	}

	const bool bChaos = IsChaosMover(MoverTemplate);
	Res->SetBoolField(TEXT("has_mover_component"), true);
	Res->SetBoolField(TEXT("is_chaos"), bChaos);
	Res->SetStringField(TEXT("component_class"), MoverTemplate->GetClass()->GetName());
	Res->SetStringField(TEXT("starting_mode"),   MoverTemplate->StartingMovementMode.ToString());

#if !UE_VERSION_OLDER_THAN(5, 5, 0)
	Res->SetStringField(TEXT("smoothing_mode"),
		MoverTemplate->SmoothingMode == EMoverSmoothingMode::None ? TEXT("None") : TEXT("VisualComponentOffset"));
#endif
	{
		FBoolProperty* P = FindFProperty<FBoolProperty>(UMoverComponent::StaticClass(), TEXT("bHasGravityOverride"));
		if (P) Res->SetBoolField(TEXT("gravity_override"), P->GetPropertyValue_InContainer(MoverTemplate));
		FStructProperty* VP = FindFProperty<FStructProperty>(UMoverComponent::StaticClass(), TEXT("GravityAccelOverride"));
		if (VP) { FVector GV = *VP->ContainerPtrToValuePtr<FVector>(MoverTemplate);
			TSharedPtr<FJsonObject> GJ = MakeShared<FJsonObject>();
			GJ->SetNumberField(TEXT("x"), GV.X); GJ->SetNumberField(TEXT("y"), GV.Y); GJ->SetNumberField(TEXT("z"), GV.Z);
			Res->SetObjectField(TEXT("gravity_vector"), GJ); }
		FBoolProperty* KP = FindFProperty<FBoolProperty>(UMoverComponent::StaticClass(), TEXT("bSupportsKinematicBasedMovement"));
		if (KP) Res->SetBoolField(TEXT("supports_kinematic_based_movement"), KP->GetPropertyValue_InContainer(MoverTemplate));
#if !UE_VERSION_OLDER_THAN(5, 5, 0)
		FStructProperty* PlanarP = FindFProperty<FStructProperty>(UMoverComponent::StaticClass(), TEXT("PlanarConstraint"));
		if (PlanarP) {
			FPlanarConstraint* PC = PlanarP->ContainerPtrToValuePtr<FPlanarConstraint>(MoverTemplate);
			if (PC) {
				TSharedPtr<FJsonObject> PCJ = MakeShared<FJsonObject>();
				PCJ->SetBoolField(TEXT("enabled"), PC->bConstrainToPlane);
				TSharedPtr<FJsonObject> NJ = MakeShared<FJsonObject>(); NJ->SetNumberField(TEXT("x"), PC->PlaneConstraintNormal.X); NJ->SetNumberField(TEXT("y"), PC->PlaneConstraintNormal.Y); NJ->SetNumberField(TEXT("z"), PC->PlaneConstraintNormal.Z);
				PCJ->SetObjectField(TEXT("plane_normal"), NJ);
				Res->SetObjectField(TEXT("planar_constraint"), PCJ);
			}
		}
#endif
	}

	TArray<TSharedPtr<FJsonValue>> ModeArr;
	{
		FMapProperty* ModesProp = GetMovementModesProp();
		if (ModesProp)
		{
			void* MapPtr = ModesProp->ContainerPtrToValuePtr<void>(MoverTemplate);
			FScriptMapHelper MapHelper(ModesProp, MapPtr);
			FNameProperty*   KeyProp = CastField<FNameProperty>  (ModesProp->KeyProp);
			FObjectProperty* ValProp = CastField<FObjectProperty>(ModesProp->ValueProp);
			for (int32 i = 0; i < MapHelper.GetMaxIndex(); i++)
			{
				if (!MapHelper.IsValidIndex(i)) continue;
				FName  ModeKey = KeyProp  ? KeyProp->GetPropertyValue(MapHelper.GetKeyPtr(i))   : NAME_None;
				UObject* ModeObj = ValProp ? ValProp->GetObjectPropertyValue(MapHelper.GetValuePtr(i)) : nullptr;
				TSharedPtr<FJsonObject> MObj = MakeShared<FJsonObject>();
				MObj->SetStringField(TEXT("name"),  ModeKey.ToString());
				MObj->SetStringField(TEXT("class"), ModeObj ? ModeObj->GetClass()->GetName() : TEXT("null"));
				SerializeModePropsInto(ModeObj, MObj);
				ModeArr.Add(MakeShared<FJsonValueObject>(MObj));
			}
		}
	}
	Res->SetArrayField(TEXT("movement_modes"), ModeArr);

	UCommonLegacyMovementSettings* CLM = MoverTemplate->FindSharedSettings_Mutable<UCommonLegacyMovementSettings>();
	if (CLM)
	{
		TSharedPtr<FJsonObject> S = MakeShared<FJsonObject>();
		S->SetNumberField(TEXT("max_speed"),            CLM->MaxSpeed);
		S->SetNumberField(TEXT("acceleration"),         CLM->Acceleration);
		S->SetNumberField(TEXT("deceleration"),         CLM->Deceleration);
		S->SetNumberField(TEXT("jump_speed"),           CLM->JumpUpwardsSpeed);
		S->SetNumberField(TEXT("turning_rate"),         CLM->TurningRate);
		S->SetNumberField(TEXT("turning_boost"),        CLM->TurningBoost);
		S->SetNumberField(TEXT("ground_friction"),      CLM->GroundFriction);
		S->SetNumberField(TEXT("braking_friction"),     CLM->BrakingFriction);
		S->SetNumberField(TEXT("max_step_height"),      CLM->MaxStepHeight);
		S->SetNumberField(TEXT("walk_slope_cosine"),    CLM->MaxWalkSlopeCosine);
		S->SetNumberField(TEXT("floor_sweep_distance"), CLM->FloorSweepDistance);
		Res->SetObjectField(TEXT("movement_settings"), S);
	}

	if (bChaos)
	{
		if (UObject* CS = FindSharedSettingsByClass(MoverTemplate, GetChaosSharedSettingsClass()))
		{
			TSharedPtr<FJsonObject> S = MakeShared<FJsonObject>();
			double D; bool B; FString N;
			if (RefGetFloat(CS, TEXT("MaxSpeed"), D))             S->SetNumberField(TEXT("max_speed"), D);
			if (RefGetFloat(CS, TEXT("Acceleration"), D))         S->SetNumberField(TEXT("acceleration"), D);
			if (RefGetFloat(CS, TEXT("Deceleration"), D))         S->SetNumberField(TEXT("deceleration"), D);
			if (RefGetFloat(CS, TEXT("TurningRate"), D))          S->SetNumberField(TEXT("turning_rate"), D);
			if (RefGetFloat(CS, TEXT("TurningBoost"), D))         S->SetNumberField(TEXT("turning_boost"), D);
			if (RefGetFloat(CS, TEXT("GroundFriction"), D))       S->SetNumberField(TEXT("ground_friction"), D);
			if (RefGetFloat(CS, TEXT("BrakingFriction"), D))      S->SetNumberField(TEXT("braking_friction"), D);
			if (RefGetFloat(CS, TEXT("BrakingFrictionFactor"), D))S->SetNumberField(TEXT("braking_friction_factor"), D);
			if (RefGetFloat(CS, TEXT("MaxStepHeight"), D))        S->SetNumberField(TEXT("max_step_height"), D);
			if (RefGetFloat(CS, TEXT("MaxWalkableSlopeAngle"), D))S->SetNumberField(TEXT("max_walkable_slope_angle"), D);
			if (RefGetBool (CS, TEXT("bUseSeparateBrakingFriction"), B))      S->SetBoolField(TEXT("use_separate_braking_friction"), B);
			if (RefGetBool (CS, TEXT("bUseAccelerationForVelocityMove"), B))  S->SetBoolField(TEXT("use_acceleration_for_velocity_move"), B);
			if (RefGetName (CS, TEXT("DefaultFallingMode"), N))   S->SetStringField(TEXT("default_falling_mode"), N);
			Res->SetObjectField(TEXT("chaos_movement_settings"), S);
		}
	}

#if !UE_VERSION_OLDER_THAN(5, 5, 0)
	UStanceSettings* SS = MoverTemplate->FindSharedSettings_Mutable<UStanceSettings>();
	if (SS)
	{
		TSharedPtr<FJsonObject> St = MakeShared<FJsonObject>();
		St->SetNumberField(TEXT("crouch_max_speed"),        SS->CrouchingMaxSpeed);
		St->SetNumberField(TEXT("crouch_max_acceleration"), SS->CrouchingMaxAcceleration);
		St->SetNumberField(TEXT("crouch_half_height"),      SS->CrouchHalfHeight);
		St->SetNumberField(TEXT("crouched_eye_height"),     SS->CrouchedEyeHeight);
		Res->SetObjectField(TEXT("stance_settings"), St);
	}
#endif

	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJson);
	FJsonSerializer::Serialize(Res.ToSharedRef(), Writer);
}

void HandleConfigureMovementModeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	const FString BpPath = GetBpPath(Args);
	if (BpPath.IsEmpty()) { OutError = TEXT("blueprint_path required"); return; }

	FString ModeName;
	if (!Args->TryGetStringField(TEXT("mode_name"), ModeName))
		if (!Args->TryGetStringField(TEXT("mode"), ModeName))
			Args->TryGetStringField(TEXT("name"), ModeName);
	if (ModeName.IsEmpty()) { OutError = TEXT("mode_name required (e.g. Walking, Falling)"); return; }

	UBlueprint* BP = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(BpPath));
	if (!BP) { OutError = FString::Printf(TEXT("Could not load Blueprint: %s"), *BpPath); return; }

	UMoverComponent* MoverTemplate = FindMoverTemplate(BP);
	if (!MoverTemplate) { OutError = TEXT("No MoverComponent found. Call setup_mover_character / setup_chaos_mover_character first."); return; }

	UObject* Mode = FindModeByName(MoverTemplate, FName(*ModeName));
	if (!Mode)
	{
		OutError = FString::Printf(TEXT("Mode '%s' not found on this component. Add it with add_movement_mode first."), *ModeName);
		return;
	}

	Mode->Modify();
	TArray<FString> Applied, Unsupported;

	for (const FModePropMap& Entry : GModeProps)
	{
		switch (Entry.Kind)
		{
		case EMoverPropKind::Bool:
		{
			bool B;
			if (Args->TryGetBoolField(Entry.Json, B))
			{
				if (RefSetBool(Mode, Entry.Prop, B)) Applied.Add(FString::Printf(TEXT("%s=%s"), Entry.Json, B ? TEXT("true") : TEXT("false")));
				else Unsupported.Add(Entry.Json);
			}
			break;
		}
		case EMoverPropKind::Name:
		{
			FString S;
			if (Args->TryGetStringField(Entry.Json, S))
			{
				if (RefSetName(Mode, Entry.Prop, S)) Applied.Add(FString::Printf(TEXT("%s=%s"), Entry.Json, *S));
				else Unsupported.Add(Entry.Json);
			}
			break;
		}
		case EMoverPropKind::OptionalFloat:
		{
			double D;
			if (Args->TryGetNumberField(Entry.Json, D))
			{
				if (RefSetOptionalFloat(Mode, Entry.Prop, D)) Applied.Add(FString::Printf(TEXT("%s=%.3f"), Entry.Json, D));
				else Unsupported.Add(Entry.Json);
			}
			break;
		}
		case EMoverPropKind::Float:
		default:
		{
			double D;
			if (Args->TryGetNumberField(Entry.Json, D))
			{
				if (RefSetFloat(Mode, Entry.Prop, D)) Applied.Add(FString::Printf(TEXT("%s=%.3f"), Entry.Json, D));
				else Unsupported.Add(Entry.Json);
			}
			break;
		}
		}
	}

	if (Applied.IsEmpty() && Unsupported.IsEmpty())
	{
		OutError = TEXT("No recognised mode parameters provided (e.g. air_control, falling_deceleration, radial_force_limit, ground_damping, target_height)");
		return;
	}

	MoverTemplate->Modify();
	FKismetEditorUtilities::CompileBlueprint(BP);
	UEditorAssetLibrary::SaveAsset(BpPath, false);

	UE_LOG(LogMCPTool, Log, TEXT("[Mover] configure_movement_mode: %s/%s — applied [%s]%s"),
		*BpPath, *ModeName, *FString::Join(Applied, TEXT(",")),
		Unsupported.Num() ? *FString::Printf(TEXT(" (unsupported: %s)"), *FString::Join(Unsupported, TEXT(","))) : TEXT(""));

	TSharedPtr<FJsonObject> Res = MakeShared<FJsonObject>();
	Res->SetBoolField(TEXT("success"), true);
	Res->SetStringField(TEXT("blueprint_path"), BpPath);
	Res->SetStringField(TEXT("mode_name"), ModeName);
	Res->SetStringField(TEXT("mode_class"), Mode->GetClass()->GetName());
	TArray<TSharedPtr<FJsonValue>> AArr; for (auto& S : Applied) AArr.Add(MakeShared<FJsonValueString>(S));
	Res->SetArrayField(TEXT("applied"), AArr);
	if (Unsupported.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> UArr; for (auto& S : Unsupported) UArr.Add(MakeShared<FJsonValueString>(S));
		Res->SetArrayField(TEXT("unsupported_for_mode"), UArr);
	}
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJson);
	FJsonSerializer::Serialize(Res.ToSharedRef(), Writer);
}

}
