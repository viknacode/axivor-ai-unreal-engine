// Copyright 2026, BlueprintsLab, All rights reserved.

#include "Tools/ChaosMoverTools.h"
#include "Tools/MoverToolsShared.h"

#include "MoverComponent.h"

#include "Kismet2/KismetEditorUtilities.h"
#include "EditorAssetLibrary.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Character.h"
#include "Misc/PackageName.h"

#include "MCPToolsLog.h"
#include "Tools/BatchToolHelper.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/UnrealType.h"
#include "PhysicsEngine/PhysicsSettings.h"

namespace ChaosMoverTools
{

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

bool CheckChaosPhysicsProjectSettings(TArray<FString>& OutMissing)
{
	const UPhysicsSettings* PS = GetDefault<UPhysicsSettings>();
	if (!PS) return true;

	if (!PS->bTickPhysicsAsync)
		OutMissing.Add(TEXT("Tick Physics Async (bTickPhysicsAsync)"));
	if (!PS->PhysicsPrediction.bEnablePhysicsPrediction)
		OutMissing.Add(TEXT("Physics Prediction (PhysicsPrediction.bEnablePhysicsPrediction)"));
	return OutMissing.Num() == 0;
}

static void SetupChaosMoverCharacterSingle(const TSharedPtr<FJsonObject>& Item, FString& OutJson, FString& OutError)
{
	UClass* ChaosCompClass = MoverTools::GetChaosMoverComponentClass();
	if (!ChaosCompClass)
	{
		OutError = TEXT("Chaos Mover is unavailable. It requires UE 5.6+ with the 'Chaos Mover' plugin enabled (Edit > Plugins).");
		return;
	}

	FString BpPath = MoverTools::GetBpPath(Item);
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
	Item->TryGetStringField(TEXT("parent_class"), ParentClassStr);

	FString StartingMode;
	const bool bHasStartingMode = Item->TryGetStringField(TEXT("starting_mode"), StartingMode) && !StartingMode.IsEmpty();

	TArray<FString> Modes;
	const TArray<TSharedPtr<FJsonValue>>* ModesArr = nullptr;
	if (Item->TryGetArrayField(TEXT("modes"), ModesArr) && ModesArr)
		for (auto& V : *ModesArr) Modes.Add(V->AsString());

	UClass* ParentClass = APawn::StaticClass();
	{
		const FString PC = ParentClassStr.ToLower();
		if      (PC == TEXT("character")) ParentClass = ACharacter::StaticClass();
		else if (PC == TEXT("actor"))     ParentClass = AActor::StaticClass();
		else if (PC != TEXT("pawn"))
		{
			UClass* Found = FindFirstObjectSafe<UClass>(*ParentClassStr);
			if (!Found) Found = LoadObject<UClass>(nullptr, *(TEXT("/Script/Engine.") + ParentClassStr));
			if (Found) ParentClass = Found;
		}
	}

	UBlueprint* BP = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(BpPath));
	const bool bExisted = (BP != nullptr);
	if (!BP)
	{
		const FString PackagePath = SavePath + TEXT("/") + Name;
		if (!FPackageName::DoesPackageExist(PackagePath))
		{
			BP = FKismetEditorUtilities::CreateBlueprint(
				ParentClass, CreatePackage(*PackagePath), FName(*Name),
				BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
		}
		else
		{
			BP = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(BpPath));
		}
	}
	if (!BP) { OutError = FString::Printf(TEXT("Failed to create or load Blueprint at: %s"), *BpPath); return; }

	UMoverComponent* MoverTemplate = MoverTools::FindMoverTemplate(BP);
	if (!MoverTemplate || !MoverTools::IsChaosMover(MoverTemplate))
	{
		UMoverComponent* NewComp = NewObject<UMoverComponent>(GetTransientPackage(), ChaosCompClass, TEXT("MoverComponent"));
		TArray<UActorComponent*> Comps; Comps.Add(NewComp);
		FKismetEditorUtilities::FAddComponentsToBlueprintParams Params;
		FKismetEditorUtilities::AddComponentsToBlueprint(BP, Comps, Params);
		MoverTemplate = MoverTools::FindMoverTemplate(BP);
	}
	if (!MoverTemplate) { OutError = TEXT("Failed to add UChaosCharacterMoverComponent to blueprint"); return; }

	MoverTemplate->Modify();

	TArray<FString> Added, AlreadyPresent, Failed;
	for (const FString& ModeStr : Modes)
	{
		FName ModeKey(*ModeStr);
		if (MoverTools::SafeContainsMode(MoverTemplate, ModeKey)) { AlreadyPresent.Add(ModeStr); continue; }
		UClass* ModeClass = MoverTools::ResolveChaosModeClass(ModeStr);
		if (!ModeClass) { Failed.Add(ModeStr); continue; }
		FString AddErr;
		if (!MoverTools::SafeAddMode(MoverTemplate, ModeKey, ModeClass, AddErr)) { Failed.Add(ModeStr + TEXT(": ") + AddErr); continue; }
		Added.Add(ModeStr);
	}

	if (bHasStartingMode) MoverTemplate->StartingMovementMode = FName(*StartingMode);

	MoverTools::RefreshMoverSharedSettings(MoverTemplate);

	FKismetEditorUtilities::CompileBlueprint(BP);
	UEditorAssetLibrary::SaveAsset(BpPath, false);

	bool bScaffoldInput = true;
	Item->TryGetBoolField(TEXT("scaffold_input"), bScaffoldInput);
	TArray<FString> ScaffoldSteps, ScaffoldWarnings;
	if (bScaffoldInput && !bExisted)
	{
		MoverTools::ScaffoldInputProducer(BpPath, ScaffoldSteps, ScaffoldWarnings, true);
	}

	TArray<FString> MissingPhysics;
	const bool bPhysicsReady = CheckChaosPhysicsProjectSettings(MissingPhysics);

	UE_LOG(LogMCPTool, Log, TEXT("[ChaosMover] setup_chaos_mover_character: %s — extra modes added: [%s]"),
		*BpPath, *FString::Join(Added, TEXT(",")));

	TSharedPtr<FJsonObject> Res = MakeShared<FJsonObject>();
	Res->SetBoolField(TEXT("success"), true);
	Res->SetStringField(TEXT("blueprint_path"), BpPath);
	Res->SetStringField(TEXT("component_class"), MoverTemplate->GetClass()->GetName());
	Res->SetStringField(TEXT("starting_mode"), MoverTemplate->StartingMovementMode.ToString());
	Res->SetBoolField(TEXT("is_chaos"), true);

	if (bScaffoldInput && !bExisted)
	{
		Res->SetBoolField(TEXT("input_producer_scaffolded"), ScaffoldWarnings.Num() == 0);
		Res->SetStringField(TEXT("input_hint"), TEXT("Functional Chaos Mover character: feed the 'MoveInput' variable (world-space direction) each frame to drive it — e.g. from Enhanced Input, or set its default for a constant-move test."));
		if (ScaffoldWarnings.Num() > 0)
		{
			TArray<TSharedPtr<FJsonValue>> W; for (auto& S : ScaffoldWarnings) W.Add(MakeShared<FJsonValueString>(S));
			Res->SetArrayField(TEXT("scaffold_warnings"), W);
		}
	}

	Res->SetBoolField(TEXT("physics_prerequisites_met"), bPhysicsReady);
	if (!bPhysicsReady)
	{
		TArray<TSharedPtr<FJsonValue>> M; for (auto& S : MissingPhysics) M.Add(MakeShared<FJsonValueString>(S));
		Res->SetArrayField(TEXT("physics_prerequisites_missing"), M);
		Res->SetStringField(TEXT("physics_prerequisite"),
			TEXT("ACTION REQUIRED: a Chaos Mover character stays FROZEN until two project-wide physics settings are on. "
			     "The user must enable them in Project Settings > Engine - Physics: 'Tick Physics Async' and "
			     "'Physics Prediction' (or DefaultEngine.ini [/Script/Engine.PhysicsSettings] bTickPhysicsAsync=True + "
			     "PhysicsPrediction.bEnablePhysicsPrediction=True), then RESTART the editor. These are project-wide "
			     "(all maps/actors), so this tool does NOT change project config for you. Standard kinematic "
			     "setup_mover_character needs neither."));
	}
	else
	{
		Res->SetStringField(TEXT("physics_prerequisite"),
			TEXT("Chaos physics prerequisites already enabled (Tick Physics Async + Physics Prediction). Feed MoveInput to drive the character."));
	}

	if (Added.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> A; for (auto& S : Added) A.Add(MakeShared<FJsonValueString>(S));
		Res->SetArrayField(TEXT("modes_added"), A);
	}
	if (Failed.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> A; for (auto& S : Failed) A.Add(MakeShared<FJsonValueString>(S));
		Res->SetArrayField(TEXT("modes_failed_to_resolve"), A);
	}
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJson);
	FJsonSerializer::Serialize(Res.ToSharedRef(), Writer);
}

void HandleSetupChaosMoverCharacterFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
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
			SetupChaosMoverCharacterSingle(Item, ItemOut, ItemErr);
			if (ItemErr.IsEmpty()) { auto E = MakeShared<FJsonObject>(); E->SetStringField(TEXT("blueprint_path"), MoverTools::GetBpPath(Item)); Batch.AddSuccess(i, E); }
			else Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJson);
		return;
	}
	SetupChaosMoverCharacterSingle(Args, OutJson, OutError);
}

void HandleConfigureChaosMoverSettingsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	UClass* SettingsClass = MoverTools::GetChaosSharedSettingsClass();
	if (!SettingsClass)
	{
		OutError = TEXT("Chaos Mover is unavailable. It requires UE 5.6+ with the 'Chaos Mover' plugin enabled (Edit > Plugins).");
		return;
	}

	const FString BpPath = MoverTools::GetBpPath(Args);
	if (BpPath.IsEmpty()) { OutError = TEXT("blueprint_path required"); return; }

	UBlueprint* BP = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(BpPath));
	if (!BP) { OutError = FString::Printf(TEXT("Could not load Blueprint: %s"), *BpPath); return; }

	UMoverComponent* MoverTemplate = MoverTools::FindMoverTemplate(BP);
	if (!MoverTemplate) { OutError = TEXT("No MoverComponent found. Call setup_chaos_mover_character first."); return; }
	if (!MoverTools::IsChaosMover(MoverTemplate)) { OutError = TEXT("This is a standard Mover character — use configure_mover_settings. configure_chaos_mover_settings only applies to Chaos Mover characters."); return; }

	UObject* Settings = MoverTools::FindSharedSettingsByClass(MoverTemplate, SettingsClass);
	if (!Settings)
	{
		MoverTools::RefreshMoverSharedSettings(MoverTemplate);
		Settings = MoverTools::FindSharedSettingsByClass(MoverTemplate, SettingsClass);
	}
	if (!Settings)
	{
		if (FArrayProperty* SharedProp = FindFProperty<FArrayProperty>(UMoverComponent::StaticClass(), TEXT("SharedSettings")))
		{
			MoverTemplate->Modify();
			void* ArrPtr = SharedProp->ContainerPtrToValuePtr<void>(MoverTemplate);
			FScriptArrayHelper ArrHelper(SharedProp, ArrPtr);
			UObject* NewSettings = NewObject<UObject>(MoverTemplate, SettingsClass, NAME_None, RF_Transactional);
			const int32 NewIdx = ArrHelper.AddValue();
			if (FObjectProperty* ElemProp = CastField<FObjectProperty>(SharedProp->Inner))
				ElemProp->SetObjectPropertyValue(ArrHelper.GetRawPtr(NewIdx), NewSettings);
			Settings = NewSettings;
		}
	}
	if (!Settings) { OutError = TEXT("Failed to create or find SharedChaosCharacterMovementSettings on the component."); return; }

	Settings->Modify();
	TArray<FString> Changed;

	auto Flt = [&](const TCHAR* Json, const TCHAR* Prop, bool bAllowNegative)
	{
		double V;
		if (Args->TryGetNumberField(Json, V) && (bAllowNegative || V >= 0.0))
			if (MoverTools::RefSetFloat(Settings, Prop, V)) Changed.Add(FString::Printf(TEXT("%s=%.2f"), Json, V));
	};
	auto Bln = [&](const TCHAR* Json, const TCHAR* Prop)
	{
		bool B;
		if (Args->TryGetBoolField(Json, B))
			if (MoverTools::RefSetBool(Settings, Prop, B)) Changed.Add(FString::Printf(TEXT("%s=%s"), Json, B ? TEXT("true") : TEXT("false")));
	};

	Flt(TEXT("max_speed"),               TEXT("MaxSpeed"),              false);
	Flt(TEXT("acceleration"),            TEXT("Acceleration"),          false);
	Flt(TEXT("deceleration"),            TEXT("Deceleration"),          false);
	Flt(TEXT("turning_rate"),            TEXT("TurningRate"),           true);
	Flt(TEXT("turning_boost"),           TEXT("TurningBoost"),          false);
	Flt(TEXT("ground_friction"),         TEXT("GroundFriction"),        false);
	Flt(TEXT("braking_friction"),        TEXT("BrakingFriction"),       false);
	Flt(TEXT("braking_friction_factor"), TEXT("BrakingFrictionFactor"), false);
	Flt(TEXT("max_step_height"),         TEXT("MaxStepHeight"),         false);
	Bln(TEXT("use_separate_braking_friction"),      TEXT("bUseSeparateBrakingFriction"));
	Bln(TEXT("use_acceleration_for_velocity_move"), TEXT("bUseAccelerationForVelocityMove"));
	{
		double Angle;
		if (Args->TryGetNumberField(TEXT("max_walkable_slope_angle"), Angle) && Angle >= 0.0)
		{
			if (MoverTools::RefSetFloat(Settings, TEXT("MaxWalkableSlopeAngle"), Angle))
			{
				Changed.Add(FString::Printf(TEXT("max_walkable_slope_angle=%.1f"), Angle));
				MoverTools::RefSetFloat(Settings, TEXT("MaxWalkSlopeCosine"), FMath::Cos(FMath::DegreesToRadians(Angle)));
			}
		}
	}
	{
		FString FallingMode;
		if (Args->TryGetStringField(TEXT("default_falling_mode"), FallingMode) && !FallingMode.IsEmpty())
			if (MoverTools::RefSetName(Settings, TEXT("DefaultFallingMode"), FallingMode)) Changed.Add(TEXT("default_falling_mode=") + FallingMode);
	}

	if (Changed.IsEmpty()) { OutError = TEXT("No recognised parameters provided"); return; }

	MoverTemplate->Modify();
	FKismetEditorUtilities::CompileBlueprint(BP);
	UEditorAssetLibrary::SaveAsset(BpPath, false);

	UE_LOG(LogMCPTool, Log, TEXT("[ChaosMover] configure_chaos_mover_settings: %s — [%s]"), *BpPath, *FString::Join(Changed, TEXT(",")));
	OutJson = FString::Printf(TEXT("{\"success\":true,\"blueprint_path\":\"%s\",\"changed\":[%s]}"),
		*BpPath, *JsonStringArray(Changed));
}

}
