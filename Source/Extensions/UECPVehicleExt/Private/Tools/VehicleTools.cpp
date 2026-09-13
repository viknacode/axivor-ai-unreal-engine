// Copyright 2026, BlueprintsLab, All rights reserved.

#include "Tools/VehicleTools.h"
#include "Tools/BatchToolHelper.h"
#include "Managers/SettingsManager.h"

#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "EditorAssetLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"

#include "WheeledVehiclePawn.h"
#include "ChaosVehicleWheel.h"
#include "ChaosWheeledVehicleMovementComponent.h"

namespace VehicleTools
{

static UChaosWheeledVehicleMovementComponent* GetVehicleMovementCDO(
	const FString& BlueprintPath, UBlueprint*& OutBP, FString& OutError)
{
	OutBP = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(BlueprintPath));
	if (!OutBP) { OutError = FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintPath); return nullptr; }

	FKismetEditorUtilities::CompileBlueprint(OutBP, EBlueprintCompileOptions::SkipGarbageCollection);

	if (!OutBP->GeneratedClass)
	{
		OutError = TEXT("Blueprint has no GeneratedClass after compile");
		return nullptr;
	}

	AWheeledVehiclePawn* CDO = Cast<AWheeledVehiclePawn>(OutBP->GeneratedClass->GetDefaultObject());
	if (!CDO) { OutError = TEXT("Blueprint CDO is not an AWheeledVehiclePawn"); return nullptr; }

	UChaosWheeledVehicleMovementComponent* MoveComp =
		Cast<UChaosWheeledVehicleMovementComponent>(CDO->GetVehicleMovementComponent());
	if (!MoveComp) { OutError = TEXT("No UChaosWheeledVehicleMovementComponent found on CDO"); return nullptr; }

	return MoveComp;
}

void HandleCreateVehicleBlueprint(const FString& Name, const FString& SavePath,
	FString& OutJsonString, FString& OutError)
{

	if (Name.IsEmpty()) { OutError = TEXT("name is required"); return; }

	FString PackagePath = SavePath;
	while (PackagePath.EndsWith(TEXT("/"))) PackagePath = PackagePath.LeftChop(1);
	PackagePath += TEXT("/") + Name;

	if (FPackageName::DoesPackageExist(PackagePath))
	{
		OutError = FString::Printf(TEXT("Asset already exists at '%s'"), *PackagePath);
		return;
	}

	UClass* VehicleClass = AWheeledVehiclePawn::StaticClass();

	UBlueprint* NewBP = FKismetEditorUtilities::CreateBlueprint(
		VehicleClass,
		CreatePackage(*PackagePath),
		FName(*Name),
		BPTYPE_Normal,
		UBlueprint::StaticClass(),
		UBlueprintGeneratedClass::StaticClass());

	if (!NewBP)
	{
		OutError = TEXT("FKismetEditorUtilities::CreateBlueprint returned null");
		return;
	}

	FAssetRegistryModule::AssetCreated(NewBP);
	NewBP->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(PackagePath, false);

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("status"), TEXT("created"));
	Root->SetStringField(TEXT("asset_path"), PackagePath);
	Root->SetStringField(TEXT("parent_class"), TEXT("AWheeledVehiclePawn"));
	FJsonSerializer::Serialize(Root.ToSharedRef(), TJsonWriterFactory<>::Create(&OutJsonString));
}

void HandleAddVehicleWheel(const FString& BlueprintPath, const FString& WheelClass,
	const FString& BoneName, float OffsetX, float OffsetY, float OffsetZ,
	FString& OutJsonString, FString& OutError)
{

	UBlueprint* BP = nullptr;
	UChaosWheeledVehicleMovementComponent* MoveComp = GetVehicleMovementCDO(BlueprintPath, BP, OutError);
	if (!MoveComp) return;

	FChaosWheelSetup Setup;
	Setup.BoneName = FName(*BoneName);
	Setup.AdditionalOffset = FVector(OffsetX, OffsetY, OffsetZ);

	if (!WheelClass.IsEmpty())
	{
		UBlueprint* WheelBP = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(WheelClass));
		if (WheelBP && WheelBP->GeneratedClass && WheelBP->GeneratedClass->IsChildOf(UChaosVehicleWheel::StaticClass()))
		{
			Setup.WheelClass = WheelBP->GeneratedClass;
		}
		else
		{
			UClass* NativeClass = FindObject<UClass>(nullptr, *WheelClass);
			if (NativeClass && NativeClass->IsChildOf(UChaosVehicleWheel::StaticClass()))
				Setup.WheelClass = NativeClass;
			else
			{
				OutError = FString::Printf(TEXT("wheel_class '%s' not found or not a UChaosVehicleWheel subclass"), *WheelClass);
				return;
			}
		}
	}
	else
	{
		Setup.WheelClass = UChaosVehicleWheel::StaticClass();
	}

	MoveComp->WheelSetups.Add(Setup);
	MoveComp->MarkPackageDirty();
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
	UEditorAssetLibrary::SaveAsset(BlueprintPath, false);

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("status"), TEXT("wheel_added"));
	Root->SetStringField(TEXT("bone_name"), BoneName);
	Root->SetStringField(TEXT("wheel_class"), Setup.WheelClass->GetName());
	Root->SetNumberField(TEXT("total_wheels"), MoveComp->WheelSetups.Num());
	FJsonSerializer::Serialize(Root.ToSharedRef(), TJsonWriterFactory<>::Create(&OutJsonString));
}

void HandleSetVehicleEngineConfig(const FString& BlueprintPath,
	float MaxTorque, float MaxRPM, float EngineIdleRPM,
	FString& OutJsonString, FString& OutError)
{

	UBlueprint* BP = nullptr;
	UChaosWheeledVehicleMovementComponent* MoveComp = GetVehicleMovementCDO(BlueprintPath, BP, OutError);
	if (!MoveComp) return;

	if (MaxTorque >= 0.f)      MoveComp->EngineSetup.MaxTorque      = MaxTorque;
	if (MaxRPM >= 0.f)         MoveComp->EngineSetup.MaxRPM         = MaxRPM;
	if (EngineIdleRPM >= 0.f)  MoveComp->EngineSetup.EngineIdleRPM  = EngineIdleRPM;

	MoveComp->MarkPackageDirty();
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
	UEditorAssetLibrary::SaveAsset(BlueprintPath, false);

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("status"), TEXT("engine_config_set"));
	Root->SetNumberField(TEXT("max_torque"),       MoveComp->EngineSetup.MaxTorque);
	Root->SetNumberField(TEXT("max_rpm"),          MoveComp->EngineSetup.MaxRPM);
	Root->SetNumberField(TEXT("engine_idle_rpm"),  MoveComp->EngineSetup.EngineIdleRPM);
	FJsonSerializer::Serialize(Root.ToSharedRef(), TJsonWriterFactory<>::Create(&OutJsonString));
}

void HandleSetVehicleTransmissionConfig(const FString& BlueprintPath,
	bool bAutomatic, bool bAutomaticSet,
	const TArray<float>& ForwardGearRatios, const TArray<float>& ReverseGearRatios,
	float FinalRatio, float ChangeUpRPM, float ChangeDownRPM,
	FString& OutJsonString, FString& OutError)
{

	UBlueprint* BP = nullptr;
	UChaosWheeledVehicleMovementComponent* MoveComp = GetVehicleMovementCDO(BlueprintPath, BP, OutError);
	if (!MoveComp) return;

	if (bAutomaticSet)              MoveComp->TransmissionSetup.bUseAutomaticGears = bAutomatic;
	if (ForwardGearRatios.Num() > 0) MoveComp->TransmissionSetup.ForwardGearRatios = ForwardGearRatios;
	if (ReverseGearRatios.Num() > 0) MoveComp->TransmissionSetup.ReverseGearRatios = ReverseGearRatios;
	if (FinalRatio >= 0.f)           MoveComp->TransmissionSetup.FinalRatio         = FinalRatio;
	if (ChangeUpRPM >= 0.f)          MoveComp->TransmissionSetup.ChangeUpRPM        = ChangeUpRPM;
	if (ChangeDownRPM >= 0.f)        MoveComp->TransmissionSetup.ChangeDownRPM      = ChangeDownRPM;

	MoveComp->MarkPackageDirty();
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
	UEditorAssetLibrary::SaveAsset(BlueprintPath, false);

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("status"), TEXT("transmission_config_set"));
	Root->SetBoolField(TEXT("automatic"), MoveComp->TransmissionSetup.bUseAutomaticGears);
	Root->SetNumberField(TEXT("final_ratio"), MoveComp->TransmissionSetup.FinalRatio);
	Root->SetNumberField(TEXT("change_up_rpm"), MoveComp->TransmissionSetup.ChangeUpRPM);
	Root->SetNumberField(TEXT("change_down_rpm"), MoveComp->TransmissionSetup.ChangeDownRPM);
	Root->SetNumberField(TEXT("forward_gear_count"), MoveComp->TransmissionSetup.ForwardGearRatios.Num());
	Root->SetNumberField(TEXT("reverse_gear_count"), MoveComp->TransmissionSetup.ReverseGearRatios.Num());
	FJsonSerializer::Serialize(Root.ToSharedRef(), TJsonWriterFactory<>::Create(&OutJsonString));
}

void HandleSetVehicleSuspensionConfig(const FString& WheelClassPath,
	float MaxRaise, float MaxDrop, float DampingRatio, float WheelRadiusVal, float MaxSteerAngle,
	FString& OutJsonString, FString& OutError)
{

	UBlueprint* WheelBP = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(WheelClassPath));
	if (!WheelBP)
	{
		OutError = FString::Printf(TEXT("Wheel Blueprint not found: %s"), *WheelClassPath);
		return;
	}

	FKismetEditorUtilities::CompileBlueprint(WheelBP, EBlueprintCompileOptions::SkipGarbageCollection);
	if (!WheelBP->GeneratedClass)
	{
		OutError = TEXT("Wheel Blueprint has no GeneratedClass after compile");
		return;
	}

	UChaosVehicleWheel* WheelCDO = Cast<UChaosVehicleWheel>(WheelBP->GeneratedClass->GetDefaultObject());
	if (!WheelCDO)
	{
		OutError = TEXT("Blueprint CDO is not a UChaosVehicleWheel");
		return;
	}

	if (MaxRaise >= 0.f)      WheelCDO->SuspensionMaxRaise    = MaxRaise;
	if (MaxDrop >= 0.f)       WheelCDO->SuspensionMaxDrop     = MaxDrop;
	if (DampingRatio >= 0.f)  WheelCDO->SuspensionDampingRatio = DampingRatio;
	if (WheelRadiusVal >= 0.f) WheelCDO->WheelRadius           = WheelRadiusVal;
	if (MaxSteerAngle >= 0.f)  WheelCDO->MaxSteerAngle         = MaxSteerAngle;

	WheelCDO->MarkPackageDirty();
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WheelBP);
	UEditorAssetLibrary::SaveAsset(WheelClassPath, false);

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("status"), TEXT("suspension_config_set"));
	Root->SetNumberField(TEXT("suspension_max_raise"),    WheelCDO->SuspensionMaxRaise);
	Root->SetNumberField(TEXT("suspension_max_drop"),     WheelCDO->SuspensionMaxDrop);
	Root->SetNumberField(TEXT("suspension_damping_ratio"), WheelCDO->SuspensionDampingRatio);
	Root->SetNumberField(TEXT("wheel_radius"),            WheelCDO->WheelRadius);
	Root->SetNumberField(TEXT("max_steer_angle"),         WheelCDO->MaxSteerAngle);
	FJsonSerializer::Serialize(Root.ToSharedRef(), TJsonWriterFactory<>::Create(&OutJsonString));
}

void HandleSetVehicleSteeringConfig(const FString& BlueprintPath, float MaxSteerAngle,
	FString& OutJsonString, FString& OutError)
{

	UBlueprint* BP = nullptr;
	UChaosWheeledVehicleMovementComponent* MoveComp = GetVehicleMovementCDO(BlueprintPath, BP, OutError);
	if (!MoveComp) return;

	if (MaxSteerAngle >= 0.f)
		MoveComp->SteeringSetup.AngleRatio = MaxSteerAngle;

	MoveComp->MarkPackageDirty();
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
	UEditorAssetLibrary::SaveAsset(BlueprintPath, false);

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("status"), TEXT("steering_config_set"));
	Root->SetNumberField(TEXT("angle_ratio"), (double)MoveComp->SteeringSetup.AngleRatio);
	FJsonSerializer::Serialize(Root.ToSharedRef(), TJsonWriterFactory<>::Create(&OutJsonString));
}

void HandleGetVehicleConfigSummary(const FString& BlueprintPath,
	FString& OutJsonString, FString& OutError)
{

	UBlueprint* BP = nullptr;
	UChaosWheeledVehicleMovementComponent* MoveComp = GetVehicleMovementCDO(BlueprintPath, BP, OutError);
	if (!MoveComp) return;

	TSharedPtr<FJsonObject> Engine = MakeShared<FJsonObject>();
	Engine->SetNumberField(TEXT("max_torque"),      MoveComp->EngineSetup.MaxTorque);
	Engine->SetNumberField(TEXT("max_rpm"),         MoveComp->EngineSetup.MaxRPM);
	Engine->SetNumberField(TEXT("engine_idle_rpm"), MoveComp->EngineSetup.EngineIdleRPM);

	TSharedPtr<FJsonObject> Trans = MakeShared<FJsonObject>();
	Trans->SetBoolField(TEXT("automatic"),         MoveComp->TransmissionSetup.bUseAutomaticGears);
	Trans->SetNumberField(TEXT("final_ratio"),     MoveComp->TransmissionSetup.FinalRatio);
	Trans->SetNumberField(TEXT("change_up_rpm"),   MoveComp->TransmissionSetup.ChangeUpRPM);
	Trans->SetNumberField(TEXT("change_down_rpm"), MoveComp->TransmissionSetup.ChangeDownRPM);
	TArray<TSharedPtr<FJsonValue>> FwdArr;
	for (float R : MoveComp->TransmissionSetup.ForwardGearRatios)
		FwdArr.Add(MakeShared<FJsonValueNumber>(R));
	Trans->SetArrayField(TEXT("forward_gear_ratios"), FwdArr);
	TArray<TSharedPtr<FJsonValue>> RevArr;
	for (float R : MoveComp->TransmissionSetup.ReverseGearRatios)
		RevArr.Add(MakeShared<FJsonValueNumber>(R));
	Trans->SetArrayField(TEXT("reverse_gear_ratios"), RevArr);

	TSharedPtr<FJsonObject> Steer = MakeShared<FJsonObject>();
	Steer->SetNumberField(TEXT("angle_ratio"), (double)MoveComp->SteeringSetup.AngleRatio);

	TArray<TSharedPtr<FJsonValue>> WheelsArr;
	for (const FChaosWheelSetup& W : MoveComp->WheelSetups)
	{
		TSharedPtr<FJsonObject> WObj = MakeShared<FJsonObject>();
		WObj->SetStringField(TEXT("bone_name"),   W.BoneName.ToString());
		WObj->SetStringField(TEXT("wheel_class"), W.WheelClass ? W.WheelClass->GetName() : TEXT("None"));
		WObj->SetNumberField(TEXT("offset_x"), W.AdditionalOffset.X);
		WObj->SetNumberField(TEXT("offset_y"), W.AdditionalOffset.Y);
		WObj->SetNumberField(TEXT("offset_z"), W.AdditionalOffset.Z);
		WheelsArr.Add(MakeShared<FJsonValueObject>(WObj));
	}

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetObjectField(TEXT("engine"),       Engine);
	Root->SetObjectField(TEXT("transmission"), Trans);
	Root->SetObjectField(TEXT("steering"),     Steer);
	Root->SetArrayField(TEXT("wheels"),        WheelsArr);
	FJsonSerializer::Serialize(Root.ToSharedRef(), TJsonWriterFactory<>::Create(&OutJsonString));
}

void HandleAddVehicleWheelFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString BlueprintPath; Args->TryGetStringField(TEXT("blueprint_path"), BlueprintPath);

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("wheels"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString WC = BatchToolHelper::GetItemString(Item, TEXT("wheel_class"));
			FString BN = BatchToolHelper::GetItemString(Item, TEXT("bone_name"), TEXT("bone"));
			double OX = 0, OY = 0, OZ = 0;
			Item->TryGetNumberField(TEXT("additional_offset_x"), OX);
			Item->TryGetNumberField(TEXT("additional_offset_y"), OY);
			Item->TryGetNumberField(TEXT("additional_offset_z"), OZ);
			FString ItemOut, ItemErr;
			HandleAddVehicleWheel(BlueprintPath, WC, BN, (float)OX, (float)OY, (float)OZ, ItemOut, ItemErr);
			if (ItemErr.IsEmpty())
			{
				auto Extra = MakeShared<FJsonObject>(); Extra->SetStringField(TEXT("bone_name"), BN);
				Batch.AddSuccess(i, Extra);
			}
			else Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJsonString);
		return;
	}
	FString WC, BN; double OX = 0, OY = 0, OZ = 0;
	Args->TryGetStringField(TEXT("wheel_class"), WC); Args->TryGetStringField(TEXT("bone_name"), BN);
	Args->TryGetNumberField(TEXT("additional_offset_x"), OX);
	Args->TryGetNumberField(TEXT("additional_offset_y"), OY);
	Args->TryGetNumberField(TEXT("additional_offset_z"), OZ);
	HandleAddVehicleWheel(BlueprintPath, WC, BN, (float)OX, (float)OY, (float)OZ, OutJsonString, OutError);
}

void HandleCreateVehicleBlueprintFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString Name, SavePath;
	Args->TryGetStringField(TEXT("name"), Name);
	Args->TryGetStringField(TEXT("save_path"), SavePath);
	if (SavePath.IsEmpty()) SavePath = FSettingsManager::GetDefaultSavePath();
	HandleCreateVehicleBlueprint(Name, SavePath, OutJsonString, OutError);
}

void HandleSetVehicleEngineConfigFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString BlueprintPath;
	double MaxTorque = -1.0, MaxRPM = -1.0, EngineIdleRPM = -1.0;
	Args->TryGetStringField(TEXT("blueprint_path"), BlueprintPath);
	Args->TryGetNumberField(TEXT("max_torque"), MaxTorque);
	Args->TryGetNumberField(TEXT("max_rpm"), MaxRPM);
	Args->TryGetNumberField(TEXT("engine_idle_rpm"), EngineIdleRPM);
	HandleSetVehicleEngineConfig(BlueprintPath, (float)MaxTorque, (float)MaxRPM, (float)EngineIdleRPM, OutJsonString, OutError);
}

void HandleSetVehicleTransmissionConfigFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString BlueprintPath;
	bool bAutomatic = true; bool bAutomaticSet = false;
	double FinalRatio = -1.0, ChangeUpRPM = -1.0, ChangeDownRPM = -1.0;
	TArray<float> ForwardGearRatios, ReverseGearRatios;

	Args->TryGetStringField(TEXT("blueprint_path"), BlueprintPath);
	if (Args->HasField(TEXT("automatic"))) { Args->TryGetBoolField(TEXT("automatic"), bAutomatic); bAutomaticSet = true; }
	Args->TryGetNumberField(TEXT("final_ratio"), FinalRatio);
	Args->TryGetNumberField(TEXT("change_up_rpm"), ChangeUpRPM);
	Args->TryGetNumberField(TEXT("change_down_rpm"), ChangeDownRPM);

	const TArray<TSharedPtr<FJsonValue>>* FwdArr = nullptr;
	if (Args->TryGetArrayField(TEXT("forward_gear_ratios"), FwdArr) && FwdArr)
		for (auto& V : *FwdArr) ForwardGearRatios.Add((float)V->AsNumber());
	const TArray<TSharedPtr<FJsonValue>>* RevArr = nullptr;
	if (Args->TryGetArrayField(TEXT("reverse_gear_ratios"), RevArr) && RevArr)
		for (auto& V : *RevArr) ReverseGearRatios.Add((float)V->AsNumber());

	HandleSetVehicleTransmissionConfig(BlueprintPath, bAutomatic, bAutomaticSet,
		ForwardGearRatios, ReverseGearRatios,
		(float)FinalRatio, (float)ChangeUpRPM, (float)ChangeDownRPM,
		OutJsonString, OutError);
}

void HandleSetVehicleSuspensionConfigFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString WheelClassPath;
	double MaxRaise = -1.0, MaxDrop = -1.0, DampingRatio = -1.0, WheelRadius = -1.0, MaxSteerAngle = -1.0;
	Args->TryGetStringField(TEXT("wheel_class_path"), WheelClassPath);
	Args->TryGetNumberField(TEXT("suspension_max_raise"), MaxRaise);
	Args->TryGetNumberField(TEXT("suspension_max_drop"), MaxDrop);
	Args->TryGetNumberField(TEXT("suspension_damping_ratio"), DampingRatio);
	Args->TryGetNumberField(TEXT("wheel_radius"), WheelRadius);
	Args->TryGetNumberField(TEXT("max_steer_angle"), MaxSteerAngle);
	HandleSetVehicleSuspensionConfig(WheelClassPath, (float)MaxRaise, (float)MaxDrop, (float)DampingRatio, (float)WheelRadius, (float)MaxSteerAngle, OutJsonString, OutError);
}

void HandleSetVehicleSteeringConfigFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString BlueprintPath;
	double MaxSteerAngle = -1.0;
	Args->TryGetStringField(TEXT("blueprint_path"), BlueprintPath);
	Args->TryGetNumberField(TEXT("max_steer_angle"), MaxSteerAngle);
	HandleSetVehicleSteeringConfig(BlueprintPath, (float)MaxSteerAngle, OutJsonString, OutError);
}

void HandleGetVehicleConfigSummaryFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString BlueprintPath;
	Args->TryGetStringField(TEXT("blueprint_path"), BlueprintPath);
	HandleGetVehicleConfigSummary(BlueprintPath, OutJsonString, OutError);
}

}
