// Copyright 2026, BlueprintsLab, All rights reserved

#include "Tools/InputSystemTools.h"
#include "Utils/MountResolver.h"
#include "Tools/BatchToolHelper.h"
#include "Utils/ContentBrowserUtils.h"
#include "EditorAssetLibrary.h"
#include "AssetToolsModule.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputTriggers.h"
#include "InputModifiers.h"
#include "Serialization/JsonSerializer.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"

namespace InputSystemTools
{

static FString NormalizeKeyName(const FString& Key)
{
	FString N = Key;
	N.ReplaceInline(TEXT("Mouse XY 2D-Axis"), TEXT("Mouse2D"));
	N.ReplaceInline(TEXT("Mouse XY 2D Axis"), TEXT("Mouse2D"));
	N.ReplaceInline(TEXT("Mouse XY"),         TEXT("Mouse2D"));
	N.ReplaceInline(TEXT("MouseXY"),          TEXT("Mouse2D"));
	N.ReplaceInline(TEXT("Mouse 2D"),         TEXT("Mouse2D"));
	N.ReplaceInline(TEXT("Mouse_X"),   TEXT("MouseX"));
	N.ReplaceInline(TEXT("Mouse_Y"),   TEXT("MouseY"));
	N.ReplaceInline(TEXT("Mouse X"),   TEXT("MouseX"));
	N.ReplaceInline(TEXT("Mouse Y"),   TEXT("MouseY"));
	N.ReplaceInline(TEXT("Left_Control"),  TEXT("LeftControl"));
	N.ReplaceInline(TEXT("Right_Control"), TEXT("RightControl"));
	N.ReplaceInline(TEXT("Left_Shift"),    TEXT("LeftShift"));
	N.ReplaceInline(TEXT("Right_Shift"),   TEXT("RightShift"));
	N.ReplaceInline(TEXT("Left_Alt"),      TEXT("LeftAlt"));
	N.ReplaceInline(TEXT("Right_Alt"),     TEXT("RightAlt"));
	N.ReplaceInline(TEXT("Left_"),         TEXT("Left"));
	N.ReplaceInline(TEXT("Right_"),        TEXT("Right"));
	N.ReplaceInline(TEXT("left_"),         TEXT("Left"));
	N.ReplaceInline(TEXT("right_"),        TEXT("Right"));
	N.ReplaceInline(TEXT("Left "),         TEXT("Left"));
	N.ReplaceInline(TEXT("Right "),        TEXT("Right"));
	N.ReplaceInline(TEXT("left "),         TEXT("Left"));
	N.ReplaceInline(TEXT("right "),        TEXT("Right"));
	N.ReplaceInline(TEXT("Space_Bar"), TEXT("SpaceBar"));
	N.ReplaceInline(TEXT("Space Bar"), TEXT("SpaceBar"));
	return N;
}

static EInputActionValueType ParseValueType(const FString& VT)
{
	if (VT.Equals(TEXT("bool"), ESearchCase::IgnoreCase) ||
		VT.Equals(TEXT("digital"), ESearchCase::IgnoreCase) ||
		VT.Equals(TEXT("boolean"), ESearchCase::IgnoreCase))
		return EInputActionValueType::Boolean;
	if (VT.Equals(TEXT("float"), ESearchCase::IgnoreCase) ||
		VT.Equals(TEXT("axis1d"), ESearchCase::IgnoreCase))
		return EInputActionValueType::Axis1D;
	if (VT.Contains(TEXT("2d"), ESearchCase::IgnoreCase) ||
		VT.Equals(TEXT("axis2d"), ESearchCase::IgnoreCase))
		return EInputActionValueType::Axis2D;
	if (VT.Contains(TEXT("3d"), ESearchCase::IgnoreCase) ||
		VT.Equals(TEXT("axis3d"), ESearchCase::IgnoreCase))
		return EInputActionValueType::Axis3D;
	return EInputActionValueType::Boolean;
}

static FString ValueTypeToString(EInputActionValueType VT)
{
	if (VT == EInputActionValueType::Axis1D)  return TEXT("Axis1D");
	if (VT == EInputActionValueType::Axis2D)  return TEXT("Axis2D");
	if (VT == EInputActionValueType::Axis3D)  return TEXT("Axis3D");
	return TEXT("Digital");
}

void HandleCreateInputAction(const FString& ActionName, const FString& SavePath, const FString& ValueType, FString& OutJsonString, FString& OutError)
{
	FString TargetSavePath = SavePath;

	if (TargetSavePath.IsEmpty())
	{
		FString DummyError;
		UECPContentBrowserUtils::GetFocusedContentBrowserPath(TargetSavePath, DummyError);
	}
	if (TargetSavePath.IsEmpty()) TargetSavePath = TEXT("/Game/Input");

	while (TargetSavePath.EndsWith(TEXT("/"))) TargetSavePath = TargetSavePath.LeftChop(1);
	if (TargetSavePath.EndsWith(TEXT("/") + ActionName, ESearchCase::IgnoreCase))
		TargetSavePath = TargetSavePath.LeftChop(ActionName.Len() + 1);
	while (TargetSavePath.EndsWith(TEXT("/"))) TargetSavePath = TargetSavePath.LeftChop(1);

	if (!UEditorAssetLibrary::DoesDirectoryExist(TargetSavePath))
	{
		UEditorAssetLibrary::MakeDirectory(TargetSavePath);
	}

	const FString FullAssetPath = FString::Printf(TEXT("%s/%s"), *TargetSavePath, *ActionName);
	if (UEditorAssetLibrary::DoesAssetExist(FullAssetPath))
	{
		TSharedPtr<FJsonObject> ResultObject = MakeShareable(new FJsonObject);
		ResultObject->SetBoolField(TEXT("success"), true);
		ResultObject->SetStringField(TEXT("asset_path"), FullAssetPath);
		ResultObject->SetStringField(TEXT("note"), TEXT("Asset already exists — reusing"));
		FString ResultString;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResultString);
		FJsonSerializer::Serialize(ResultObject.ToSharedRef(), Writer);
		OutJsonString = ResultString;
		return;
	}

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");

	UInputAction* NewAction = Cast<UInputAction>(AssetToolsModule.Get().CreateAsset(ActionName, TargetSavePath, UInputAction::StaticClass(), nullptr));
	if (!NewAction)
	{
		OutError = TEXT("Failed to create Input Action asset");
		TSharedPtr<FJsonObject> ResultObject = MakeShareable(new FJsonObject);
		ResultObject->SetBoolField(TEXT("success"), false);
		ResultObject->SetStringField(TEXT("error"), OutError);
		FString ResultString;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResultString);
		FJsonSerializer::Serialize(ResultObject.ToSharedRef(), Writer);
		OutJsonString = ResultString;
		return;
	}

	EInputActionValueType ActionType = ValueType.IsEmpty() ? EInputActionValueType::Boolean : ParseValueType(ValueType);

	NewAction->ValueType = ActionType;
	NewAction->MarkPackageDirty();

	TSharedPtr<FJsonObject> ResultObject = MakeShareable(new FJsonObject);
	ResultObject->SetBoolField(TEXT("success"), true);
	ResultObject->SetStringField(TEXT("asset_path"), NewAction->GetPathName());
	ResultObject->SetStringField(TEXT("value_type"), ValueTypeToString(ActionType));
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(ResultObject.ToSharedRef(), Writer);
}

void HandleCreateInputMappingContext(const FString& ContextName, const FString& SavePath, const TArray<TSharedPtr<FJsonValue>>& Mappings, FString& OutJsonString, FString& OutError)
{
	FString TargetSavePath = SavePath;

	if (TargetSavePath.IsEmpty())
	{
		FString DummyError;
		UECPContentBrowserUtils::GetFocusedContentBrowserPath(TargetSavePath, DummyError);
	}
	if (TargetSavePath.IsEmpty()) TargetSavePath = TEXT("/Game/Input");

	while (TargetSavePath.EndsWith(TEXT("/"))) TargetSavePath.LeftChopInline(1);
	if (TargetSavePath.EndsWith(TEXT(".uasset"))) TargetSavePath.LeftChopInline(7);

	if (ContextName.IsEmpty())
	{
		OutError = TEXT("create_input_mapping_context: 'asset_name' is required and cannot be empty");
		return;
	}
	if (!UECPMountResolver::IsValidMountedPath(TargetSavePath))
	{
		OutError = FString::Printf(TEXT("create_input_mapping_context: save_path '%s' must be under a mounted content path (/Game/... or /<PluginName>/...)"), *TargetSavePath);
		return;
	}

	const FString FullAssetPath = FString::Printf(TEXT("%s/%s"), *TargetSavePath, *ContextName);
	if (UEditorAssetLibrary::DoesAssetExist(FullAssetPath))
	{
		if (UObject* Existing = UEditorAssetLibrary::LoadAsset(FullAssetPath))
		{
			if (UInputMappingContext* ExistingIMC = Cast<UInputMappingContext>(Existing))
			{
				if (Mappings.Num() > 0)
				{
					HandleApplyMappingsToContext(ExistingIMC, Mappings);
					ExistingIMC->MarkPackageDirty();
				}
				TSharedPtr<FJsonObject> ResultObject = MakeShareable(new FJsonObject);
				ResultObject->SetBoolField(TEXT("success"), true);
				ResultObject->SetStringField(TEXT("asset_path"), ExistingIMC->GetPathName());
				ResultObject->SetNumberField(TEXT("mappings_added"), Mappings.Num());
				ResultObject->SetStringField(TEXT("note"), TEXT("Asset already existed at target path — reused it. Mappings applied if provided."));
				TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
				FJsonSerializer::Serialize(ResultObject.ToSharedRef(), Writer);
				return;
			}
			OutError = FString::Printf(TEXT("Asset exists at '%s' but is not a UInputMappingContext — pick a different name or delete the existing asset"), *FullAssetPath);
			return;
		}
	}

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
	UInputMappingContext* NewContext = Cast<UInputMappingContext>(AssetToolsModule.Get().CreateAsset(ContextName, TargetSavePath, UInputMappingContext::StaticClass(), nullptr));

	if (!NewContext)
	{
		const FString PackageName = FString::Printf(TEXT("%s/%s"), *TargetSavePath, *ContextName);
		UPackage* Package = CreatePackage(*PackageName);
		if (Package)
		{
			Package->FullyLoad();
			NewContext = NewObject<UInputMappingContext>(Package, FName(*ContextName), RF_Public | RF_Standalone | RF_Transactional);
			if (NewContext)
			{
				FAssetRegistryModule::AssetCreated(NewContext);
				NewContext->MarkPackageDirty();
			}
		}
	}

	if (!NewContext)
	{
		OutError = FString::Printf(TEXT("Failed to create Input Mapping Context '%s' at '%s' — both IAssetTools::CreateAsset and direct NewObject fallback failed. Verify the save_path is a valid /Game subfolder and the name does not collide with an existing asset of a different type."),
			*ContextName, *TargetSavePath);
		TSharedPtr<FJsonObject> ResultObject = MakeShareable(new FJsonObject);
		ResultObject->SetBoolField(TEXT("success"), false);
		ResultObject->SetStringField(TEXT("error"), OutError);
		ResultObject->SetStringField(TEXT("save_path_used"), TargetSavePath);
		ResultObject->SetStringField(TEXT("asset_name_used"), ContextName);
		FString ResultString;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResultString);
		FJsonSerializer::Serialize(ResultObject.ToSharedRef(), Writer);
		OutJsonString = ResultString;
		return;
	}

	NewContext->MarkPackageDirty();

	if (Mappings.Num() > 0)
	{
		HandleApplyMappingsToContext(NewContext, Mappings);
		NewContext->MarkPackageDirty();
	}

	TSharedPtr<FJsonObject> ResultObject = MakeShareable(new FJsonObject);
	ResultObject->SetBoolField(TEXT("success"), true);
	ResultObject->SetStringField(TEXT("asset_path"), NewContext->GetPathName());
	ResultObject->SetNumberField(TEXT("mappings_added"), Mappings.Num());
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(ResultObject.ToSharedRef(), Writer);
}

static UInputTrigger* CreateTrigger(const TSharedPtr<FJsonValue>& TriggerValue, UObject* Outer)
{
	FString TypeStr;
	const TSharedPtr<FJsonObject>* TriggerObj = nullptr;
	if (TriggerValue->TryGetObject(TriggerObj))
		(*TriggerObj)->TryGetStringField(TEXT("type"), TypeStr);
	else
		TriggerValue->TryGetString(TypeStr);
	TypeStr = TypeStr.ToLower();

	if (TypeStr == TEXT("pressed"))  return NewObject<UInputTriggerPressed>(Outer);
	if (TypeStr == TEXT("released")) return NewObject<UInputTriggerReleased>(Outer);
	if (TypeStr == TEXT("tap"))      return NewObject<UInputTriggerTap>(Outer);
	if (TypeStr == TEXT("holdandrelease")) return NewObject<UInputTriggerHoldAndRelease>(Outer);
	if (TypeStr == TEXT("hold"))
	{
		UInputTriggerHold* Hold = NewObject<UInputTriggerHold>(Outer);
		if (TriggerObj)
		{
			double V; bool B;
			if ((*TriggerObj)->TryGetNumberField(TEXT("hold_time_threshold"), V) || (*TriggerObj)->TryGetNumberField(TEXT("hold_time"), V)) Hold->HoldTimeThreshold = (float)V;
			if ((*TriggerObj)->TryGetBoolField(TEXT("is_one_shot"), B) || (*TriggerObj)->TryGetBoolField(TEXT("one_shot"), B)) Hold->bIsOneShot = B;
		}
		return Hold;
	}
	if (TypeStr == TEXT("pulse"))
	{
		UInputTriggerPulse* Pulse = NewObject<UInputTriggerPulse>(Outer);
		if (TriggerObj)
		{
			double V; bool B;
			if ((*TriggerObj)->TryGetNumberField(TEXT("interval"), V)) Pulse->Interval = (float)V;
			if ((*TriggerObj)->TryGetBoolField(TEXT("trigger_on_start"), B)) Pulse->bTriggerOnStart = B;
		}
		return Pulse;
	}
	return nullptr;
}

static UInputModifier* CreateModifier(const TSharedPtr<FJsonValue>& ModifierValue, UObject* Outer)
{
	FString TypeStr;
	FString InlineParam;
	const TSharedPtr<FJsonObject>* ModObj = nullptr;
	if (ModifierValue->TryGetObject(ModObj))
		(*ModObj)->TryGetStringField(TEXT("type"), TypeStr);
	else
	{
		FString RawStr;
		ModifierValue->TryGetString(RawStr);
		int32 ParenIdx = INDEX_NONE;
		if (RawStr.FindChar('(', ParenIdx))
		{
			TypeStr = RawStr.Left(ParenIdx).TrimEnd();
			InlineParam = RawStr.Mid(ParenIdx + 1).TrimEnd();
			if (InlineParam.EndsWith(TEXT(")")))
				InlineParam = InlineParam.LeftChop(1);
		}
		else
			TypeStr = RawStr;
	}
	TypeStr = TypeStr.ToLower();

	if (TypeStr.StartsWith(TEXT("swizzle_")) || TypeStr.StartsWith(TEXT("swizzleaxis_")))
	{
		int32 UnderscoreIdx = INDEX_NONE;
		TypeStr.FindChar('_', UnderscoreIdx);
		if (UnderscoreIdx != INDEX_NONE && UnderscoreIdx + 1 < TypeStr.Len())
		{
			InlineParam = TypeStr.Mid(UnderscoreIdx + 1).ToUpper();
			TypeStr = TEXT("swizzleaxis");
		}
	}

	if (TypeStr.StartsWith(TEXT("negate_")))
	{
		int32 UnderscoreIdx = INDEX_NONE;
		TypeStr.FindChar('_', UnderscoreIdx);
		if (UnderscoreIdx != INDEX_NONE && UnderscoreIdx + 1 < TypeStr.Len())
		{
			InlineParam = TypeStr.Mid(UnderscoreIdx + 1).ToUpper();
			TypeStr = TEXT("negate");
		}
	}

	if (TypeStr == TEXT("negate"))
	{
		UInputModifierNegate* Neg = NewObject<UInputModifierNegate>(Outer);
		if (ModObj)
		{
			bool B;
			if ((*ModObj)->TryGetBoolField(TEXT("x"), B)) Neg->bX = B;
			if ((*ModObj)->TryGetBoolField(TEXT("y"), B)) Neg->bY = B;
			if ((*ModObj)->TryGetBoolField(TEXT("z"), B)) Neg->bZ = B;
		}
		else if (!InlineParam.IsEmpty())
		{
			Neg->bX = InlineParam.Contains(TEXT("X"));
			Neg->bY = InlineParam.Contains(TEXT("Y"));
			Neg->bZ = InlineParam.Contains(TEXT("Z"));
		}
		return Neg;
	}
	if (TypeStr == TEXT("swizzleaxis") || TypeStr == TEXT("swizzle") ||
	    TypeStr == TEXT("swizzleinputaxisvalues") || TypeStr == TEXT("swizzleaxisvalues"))
	{
		FString OrderStr;
		if (ModObj) (*ModObj)->TryGetStringField(TEXT("order"), OrderStr);
		if (OrderStr.IsEmpty()) OrderStr = InlineParam;
		OrderStr = OrderStr.ToUpper();
		if (OrderStr.Len() == 2) OrderStr += TEXT("Z");
		if (OrderStr == TEXT("XYZ") || OrderStr.IsEmpty()) return nullptr;

		UInputModifierSwizzleAxis* Swizzle = NewObject<UInputModifierSwizzleAxis>(Outer);
		if      (OrderStr == TEXT("YXZ")) Swizzle->Order = EInputAxisSwizzle::YXZ;
		else if (OrderStr == TEXT("ZYX")) Swizzle->Order = EInputAxisSwizzle::ZYX;
		else if (OrderStr == TEXT("XZY")) Swizzle->Order = EInputAxisSwizzle::XZY;
		else if (OrderStr == TEXT("YZX")) Swizzle->Order = EInputAxisSwizzle::YZX;
		else if (OrderStr == TEXT("ZXY")) Swizzle->Order = EInputAxisSwizzle::ZXY;
		else return nullptr;
		return Swizzle;
	}
	if (TypeStr == TEXT("scalar"))
	{
		UInputModifierScalar* Scalar = NewObject<UInputModifierScalar>(Outer);
		if (ModObj)
		{
			FVector V(1.f, 1.f, 1.f);
			double D;
			if ((*ModObj)->TryGetNumberField(TEXT("scalar"), D)) { V = FVector((float)D); }
			else
			{
				if ((*ModObj)->TryGetNumberField(TEXT("x"), D)) V.X = (float)D;
				if ((*ModObj)->TryGetNumberField(TEXT("y"), D)) V.Y = (float)D;
				if ((*ModObj)->TryGetNumberField(TEXT("z"), D)) V.Z = (float)D;
			}
			Scalar->Scalar = V;
		}
		return Scalar;
	}
	if (TypeStr == TEXT("deadzone"))
	{
		UInputModifierDeadZone* DZ = NewObject<UInputModifierDeadZone>(Outer);
		if (ModObj)
		{
			double D;
			if ((*ModObj)->TryGetNumberField(TEXT("lower_threshold"), D)) DZ->LowerThreshold = (float)D;
			if ((*ModObj)->TryGetNumberField(TEXT("upper_threshold"), D)) DZ->UpperThreshold = (float)D;
			FString DZT;
			if ((*ModObj)->TryGetStringField(TEXT("dead_zone_type"), DZT) && DZT.ToLower() == TEXT("radial"))
				DZ->Type = EDeadZoneType::Radial;
		}
		return DZ;
	}
	return nullptr;
}

void HandleApplyMappingsToContext(UInputMappingContext* Context, const TArray<TSharedPtr<FJsonValue>>& Mappings)
{
	for (const auto& MappingValue : Mappings)
	{
		const TSharedPtr<FJsonObject>* MappingObj;
		if (!MappingValue->TryGetObject(MappingObj)) continue;

		FString ActionPath, KeyName;
		if (!(*MappingObj)->TryGetStringField(TEXT("action_path"), ActionPath))
			(*MappingObj)->TryGetStringField(TEXT("action"), ActionPath);
		if (ActionPath.IsEmpty()) continue;
		if (!(*MappingObj)->TryGetStringField(TEXT("key"), KeyName) || KeyName.IsEmpty()) continue;

		UInputAction* InputAction = LoadObject<UInputAction>(nullptr, *ActionPath);
		if (!InputAction) continue;

		FKey MappingKey(*NormalizeKeyName(KeyName));
		if (!MappingKey.IsValid()) continue;

		Context->UnmapKey(InputAction, MappingKey);
		FEnhancedActionKeyMapping& Mapping = Context->MapKey(InputAction, MappingKey);

		TArray<TSharedPtr<FJsonValue>> Modifiers, Triggers;
		FString SwizzleOrder;
		if ((*MappingObj)->TryGetStringField(TEXT("swizzle_axis"), SwizzleOrder) && !SwizzleOrder.IsEmpty() &&
			!SwizzleOrder.Equals(TEXT("XYZ"), ESearchCase::IgnoreCase))
		{
			TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject());
			Obj->SetStringField(TEXT("type"), TEXT("SwizzleAxis"));
			Obj->SetStringField(TEXT("order"), SwizzleOrder);
			Modifiers.Insert(MakeShareable(new FJsonValueObject(Obj)), 0);
		}
		bool bNegate = false;
		if ((*MappingObj)->TryGetBoolField(TEXT("negate"), bNegate) && bNegate)
		{
			TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject());
			Obj->SetStringField(TEXT("type"), TEXT("Negate"));
			Modifiers.Add(MakeShareable(new FJsonValueObject(Obj)));
		}
		double ScaleNum = 0.0;
		const TSharedPtr<FJsonObject>* ScaleObjPtr = nullptr;
		if ((*MappingObj)->TryGetNumberField(TEXT("scale"), ScaleNum))
		{
			TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject());
			Obj->SetStringField(TEXT("type"), TEXT("Scalar"));
			Obj->SetNumberField(TEXT("scalar"), ScaleNum);
			Modifiers.Add(MakeShareable(new FJsonValueObject(Obj)));
		}
		else if ((*MappingObj)->TryGetObjectField(TEXT("scale"), ScaleObjPtr))
		{
			TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject());
			Obj->SetStringField(TEXT("type"), TEXT("Scalar"));
			double X = 1.0, Y = 1.0, Z = 1.0;
			(*ScaleObjPtr)->TryGetNumberField(TEXT("x"), X);
			(*ScaleObjPtr)->TryGetNumberField(TEXT("y"), Y);
			(*ScaleObjPtr)->TryGetNumberField(TEXT("z"), Z);
			Obj->SetNumberField(TEXT("x"), X);
			Obj->SetNumberField(TEXT("y"), Y);
			Obj->SetNumberField(TEXT("z"), Z);
			Modifiers.Add(MakeShareable(new FJsonValueObject(Obj)));
		}
		const TArray<TSharedPtr<FJsonValue>>* ModifiersArr;
		if ((*MappingObj)->TryGetArrayField(TEXT("modifiers"), ModifiersArr))
			for (const auto& MV : *ModifiersArr) Modifiers.Add(MV);

		FString TrigType;
		if (!(*MappingObj)->TryGetStringField(TEXT("trigger_type"), TrigType))
			(*MappingObj)->TryGetStringField(TEXT("trigger"), TrigType);
		if (!TrigType.IsEmpty())
		{
			if (TrigType.ToLower() == TEXT("hold"))
			{
				TSharedPtr<FJsonObject> HoldObj = MakeShareable(new FJsonObject);
				HoldObj->SetStringField(TEXT("type"), TEXT("hold"));
				double HoldTime = 1.0;
				if (!(*MappingObj)->TryGetNumberField(TEXT("hold_time_threshold"), HoldTime))
					(*MappingObj)->TryGetNumberField(TEXT("hold_time"), HoldTime);
				HoldObj->SetNumberField(TEXT("hold_time_threshold"), HoldTime);
				Triggers.Add(MakeShareable(new FJsonValueObject(HoldObj)));
			}
			else
				Triggers.Add(MakeShareable(new FJsonValueString(TrigType)));
		}

		const TArray<TSharedPtr<FJsonValue>>* TriggersArr;
		if ((*MappingObj)->TryGetArrayField(TEXT("triggers"), TriggersArr))
			for (const auto& TV : *TriggersArr) Triggers.Add(TV);

		for (const auto& TV : Triggers)
			if (UInputTrigger* T = CreateTrigger(TV, Context)) Mapping.Triggers.Add(T);
		for (const auto& MV : Modifiers)
			if (UInputModifier* M = CreateModifier(MV, Context)) Mapping.Modifiers.Add(M);
	}
}

void HandleAddInputMapping(const FString& ContextPath, const FString& ActionPath, const FString& Key, const TArray<TSharedPtr<FJsonValue>>& Modifiers, const TArray<TSharedPtr<FJsonValue>>& Triggers, FString& OutJsonString, FString& OutError)
{
	UInputMappingContext* Context = LoadObject<UInputMappingContext>(nullptr, *ContextPath);
	if (!Context)
	{
		OutError = FString::Printf(TEXT("Could not load Input Mapping Context: %s"), *ContextPath);
		TSharedPtr<FJsonObject> ResultObject = MakeShareable(new FJsonObject);
		ResultObject->SetBoolField(TEXT("success"), false);
		ResultObject->SetStringField(TEXT("error"), OutError);
		FString ResultString;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResultString);
		FJsonSerializer::Serialize(ResultObject.ToSharedRef(), Writer);
		OutJsonString = ResultString;
		return;
	}

	UInputAction* Action = LoadObject<UInputAction>(nullptr, *ActionPath);
	if (!Action)
	{
		OutError = FString::Printf(TEXT("Could not load Input Action: %s"), *ActionPath);
		TSharedPtr<FJsonObject> ResultObject = MakeShareable(new FJsonObject);
		ResultObject->SetBoolField(TEXT("success"), false);
		ResultObject->SetStringField(TEXT("error"), OutError);
		FString ResultString;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResultString);
		FJsonSerializer::Serialize(ResultObject.ToSharedRef(), Writer);
		OutJsonString = ResultString;
		return;
	}

	FString NormalizedKey = NormalizeKeyName(Key);

	FKey MappingKey(*NormalizedKey);
	if (!MappingKey.IsValid())
	{
		OutError = FString::Printf(TEXT("Invalid key: %s"), *Key);
		TSharedPtr<FJsonObject> ResultObject = MakeShareable(new FJsonObject);
		ResultObject->SetBoolField(TEXT("success"), false);
		ResultObject->SetStringField(TEXT("error"), OutError);
		FString ResultString;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResultString);
		FJsonSerializer::Serialize(ResultObject.ToSharedRef(), Writer);
		OutJsonString = ResultString;
		return;
	}

	Context->UnmapKey(Action, MappingKey);
	FEnhancedActionKeyMapping& Mapping = Context->MapKey(Action, MappingKey);

	TArray<FString> DroppedTriggers;
	for (int32 Ti = 0; Ti < Triggers.Num(); ++Ti)
	{
		const TSharedPtr<FJsonValue>& TV = Triggers[Ti];
		if (UInputTrigger* T = CreateTrigger(TV, Context)) { Mapping.Triggers.Add(T); }
		else
		{
			FString Raw;
			if (TV.IsValid()) TV->TryGetString(Raw);
			DroppedTriggers.Add(Raw.IsEmpty() ? FString::Printf(TEXT("<#%d>"), Ti) : Raw);
		}
	}

	TArray<FString> DroppedModifiers;
	for (int32 Mi = 0; Mi < Modifiers.Num(); ++Mi)
	{
		const TSharedPtr<FJsonValue>& MV = Modifiers[Mi];
		if (UInputModifier* M = CreateModifier(MV, Context)) { Mapping.Modifiers.Add(M); }
		else
		{
			FString Raw;
			if (MV.IsValid()) MV->TryGetString(Raw);
			DroppedModifiers.Add(Raw.IsEmpty() ? FString::Printf(TEXT("<#%d>"), Mi) : Raw);
		}
	}

	Context->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(ContextPath, false);

	TSharedPtr<FJsonObject> ResultObject = MakeShareable(new FJsonObject);
	ResultObject->SetBoolField(TEXT("success"), true);
	ResultObject->SetStringField(TEXT("message"), FString::Printf(TEXT("Added mapping: %s -> %s (%d triggers, %d modifiers)"),
		*ActionPath, *Key, Mapping.Triggers.Num(), Mapping.Modifiers.Num()));
	if (DroppedTriggers.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> Arr;
		for (const FString& S : DroppedTriggers) Arr.Add(MakeShared<FJsonValueString>(S));
		ResultObject->SetArrayField(TEXT("triggers_dropped"), Arr);
	}
	if (DroppedModifiers.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> Arr;
		for (const FString& S : DroppedModifiers) Arr.Add(MakeShared<FJsonValueString>(S));
		ResultObject->SetArrayField(TEXT("modifiers_dropped"), Arr);
	}
	// Axivor: a partially applied mapping is not a success — surface the dropped items as an
	// error so the agent loop retries with corrected trigger/modifier specs instead of moving on.
	if (DroppedTriggers.Num() > 0 || DroppedModifiers.Num() > 0)
	{
		ResultObject->SetBoolField(TEXT("success"), false);
		ResultObject->SetStringField(TEXT("code"), TEXT("PARTIAL_APPLY"));
		OutError = FString::Printf(
			TEXT("Mapping %s -> %s was added, but %d trigger(s) and %d modifier(s) were not recognised and were dropped: [%s] [%s]. "
			     "Valid trigger types: Pressed, Released, Down, Hold, HoldAndRelease, Tap, Pulse, ChordAction, Combo. "
			     "Valid modifiers: Negate, SwizzleAxis, Scalar, DeadZone, Smooth, FOVScaling, ToWorldSpace, ResponseCurveExponential. "
			     "Re-send only the dropped items with corrected names/params."),
			*ActionPath, *Key, DroppedTriggers.Num(), DroppedModifiers.Num(),
			*FString::Join(DroppedTriggers, TEXT(", ")), *FString::Join(DroppedModifiers, TEXT(", ")));
	}
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(ResultObject.ToSharedRef(), Writer);
}

void HandleGetInputMappingSummary(const FString& ContextPath, FString& OutJsonString, FString& OutError)
{

	UInputMappingContext* IMC = Cast<UInputMappingContext>(UEditorAssetLibrary::LoadAsset(ContextPath));
	if (!IMC) { OutError = FString::Printf(TEXT("Could not load InputMappingContext at '%s'"), *ContextPath); return; }

	TSharedPtr<FJsonObject> Res = MakeShareable(new FJsonObject());
	Res->SetBoolField(TEXT("success"), true);
	Res->SetStringField(TEXT("context_path"), IMC->GetPathName());

	TArray<TSharedPtr<FJsonValue>> MappingsArr;
	for (const FEnhancedActionKeyMapping& Mapping : IMC->GetMappings())
	{
		TSharedPtr<FJsonObject> MapObj = MakeShareable(new FJsonObject());
		MapObj->SetStringField(TEXT("action"), Mapping.Action ? Mapping.Action->GetPathName() : TEXT("None"));
		if (Mapping.Action)
			MapObj->SetStringField(TEXT("value_type"), ValueTypeToString(Mapping.Action->ValueType));
		MapObj->SetStringField(TEXT("key"), Mapping.Key.GetFName().ToString());

		auto DumpProps = [](UObject* Obj) -> TSharedPtr<FJsonObject>
		{
			TSharedPtr<FJsonObject> Props = MakeShareable(new FJsonObject());
			for (TFieldIterator<FProperty> It(Obj->GetClass()); It; ++It)
			{
				FProperty* Prop = *It;
				if (!Prop || !Prop->HasAnyPropertyFlags(CPF_Edit) || Prop->HasAnyPropertyFlags(CPF_Transient)) continue;
				const FString PName = Prop->GetName();
				if (const FFloatProperty* FP = CastField<FFloatProperty>(Prop))
					Props->SetNumberField(PName, FP->GetPropertyValue_InContainer(Obj));
				else if (const FDoubleProperty* DP = CastField<FDoubleProperty>(Prop))
					Props->SetNumberField(PName, DP->GetPropertyValue_InContainer(Obj));
				else if (const FBoolProperty* BP = CastField<FBoolProperty>(Prop))
					Props->SetBoolField(PName, BP->GetPropertyValue_InContainer(Obj));
				else if (const FIntProperty* IP = CastField<FIntProperty>(Prop))
					Props->SetNumberField(PName, IP->GetPropertyValue_InContainer(Obj));
				else if (const FByteProperty* ByteP = CastField<FByteProperty>(Prop))
					Props->SetNumberField(PName, ByteP->GetPropertyValue_InContainer(Obj));
				else if (const FEnumProperty* EP = CastField<FEnumProperty>(Prop))
				{
					int64 V = EP->GetUnderlyingProperty()->GetSignedIntPropertyValue(EP->ContainerPtrToValuePtr<void>(Obj));
					Props->SetStringField(PName, EP->GetEnum()->GetNameStringByValue(V));
				}
			}
			return Props;
		};

		TArray<TSharedPtr<FJsonValue>> TriggersArr;
		for (const TObjectPtr<UInputTrigger>& Trig : Mapping.Triggers)
		{
			if (!Trig) continue;
			TSharedPtr<FJsonObject> TObj = MakeShareable(new FJsonObject());
			FString ClassName = Trig->GetClass()->GetName();
			ClassName.RemoveFromStart(TEXT("InputTrigger"));
			TObj->SetStringField(TEXT("type"), ClassName.ToLower());
			TSharedPtr<FJsonObject> Props = DumpProps(Trig);
			if (Props->Values.Num() > 0) TObj->SetObjectField(TEXT("properties"), Props);
			TriggersArr.Add(MakeShareable(new FJsonValueObject(TObj)));
		}
		MapObj->SetArrayField(TEXT("triggers"), TriggersArr);

		TArray<TSharedPtr<FJsonValue>> ModifiersArr;
		for (const TObjectPtr<UInputModifier>& Mod : Mapping.Modifiers)
		{
			if (!Mod) continue;
			TSharedPtr<FJsonObject> MObj = MakeShareable(new FJsonObject());
			FString ClassName = Mod->GetClass()->GetName();
			ClassName.RemoveFromStart(TEXT("InputModifier"));
			MObj->SetStringField(TEXT("type"), ClassName.ToLower());
			TSharedPtr<FJsonObject> Props = DumpProps(Mod);
			if (Props->Values.Num() > 0) MObj->SetObjectField(TEXT("properties"), Props);
			ModifiersArr.Add(MakeShareable(new FJsonValueObject(MObj)));
		}
		MapObj->SetArrayField(TEXT("modifiers"), ModifiersArr);

		MappingsArr.Add(MakeShareable(new FJsonValueObject(MapObj)));
	}
	Res->SetArrayField(TEXT("mappings"), MappingsArr);
	Res->SetNumberField(TEXT("mapping_count"), MappingsArr.Num());

	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Res.ToSharedRef(), W);
}

void HandleRemoveInputMapping(const FString& ContextPath, const FString& ActionPath, const FString& Key, FString& OutJsonString, FString& OutError)
{

	UInputMappingContext* IMC = Cast<UInputMappingContext>(UEditorAssetLibrary::LoadAsset(ContextPath));
	if (!IMC) { OutError = FString::Printf(TEXT("Could not load InputMappingContext at '%s'"), *ContextPath); return; }

	UInputAction* Action = Cast<UInputAction>(UEditorAssetLibrary::LoadAsset(ActionPath));
	if (!Action) { OutError = FString::Printf(TEXT("Could not load InputAction at '%s'"), *ActionPath); return; }

	FKey TargetKey(*Key);
	IMC->UnmapKey(Action, TargetKey);
	IMC->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(ContextPath, false);

	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"context_path\":\"%s\",\"removed_action\":\"%s\",\"removed_key\":\"%s\"}"),
		*ContextPath, *ActionPath, *Key);
}

void HandleSetInputActionProperties(const FString& ActionPath, const FString& ValueType,
	bool bSetConsumeInput, bool bConsumeInput,
	bool bSetTriggerWhenPaused, bool bTriggerWhenPaused,
	FString& OutJsonString, FString& OutError)
{

	UInputAction* Action = Cast<UInputAction>(UEditorAssetLibrary::LoadAsset(ActionPath));
	if (!Action) { OutError = FString::Printf(TEXT("Could not load InputAction at '%s'"), *ActionPath); return; }

	if (!ValueType.IsEmpty())
		Action->ValueType = ParseValueType(ValueType);

	if (bSetConsumeInput)
		Action->bConsumeInput = bConsumeInput;
	if (bSetTriggerWhenPaused)
		Action->bTriggerWhenPaused = bTriggerWhenPaused;

	Action->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(ActionPath, false);

	TSharedPtr<FJsonObject> Res = MakeShareable(new FJsonObject());
	Res->SetBoolField(TEXT("success"), true);
	Res->SetStringField(TEXT("action_path"), ActionPath);
	Res->SetStringField(TEXT("value_type"), ValueTypeToString(Action->ValueType));
	Res->SetBoolField(TEXT("consume_input"), Action->bConsumeInput);
	Res->SetBoolField(TEXT("trigger_when_paused"), Action->bTriggerWhenPaused);
	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Res.ToSharedRef(), W);
}

void HandleGetInputActionSummary(const FString& ActionPath, FString& OutJsonString, FString& OutError)
{

	UInputAction* Action = Cast<UInputAction>(UEditorAssetLibrary::LoadAsset(ActionPath));
	if (!Action) { OutError = FString::Printf(TEXT("Could not load InputAction at '%s'"), *ActionPath); return; }

	TSharedPtr<FJsonObject> Res = MakeShareable(new FJsonObject());
	Res->SetBoolField(TEXT("success"), true);
	Res->SetStringField(TEXT("action_path"), Action->GetPathName());
	Res->SetStringField(TEXT("value_type"), ValueTypeToString(Action->ValueType));
	Res->SetBoolField(TEXT("consume_input"), Action->bConsumeInput);
	Res->SetBoolField(TEXT("trigger_when_paused"), Action->bTriggerWhenPaused);

	TArray<TSharedPtr<FJsonValue>> TriggersArr;
	for (const TObjectPtr<UInputTrigger>& Trig : Action->Triggers)
	{
		if (!Trig) continue;
		FString CN = Trig->GetClass()->GetName();
		CN.RemoveFromStart(TEXT("InputTrigger"));
		TriggersArr.Add(MakeShareable(new FJsonValueString(CN.ToLower())));
	}
	Res->SetArrayField(TEXT("default_triggers"), TriggersArr);

	TArray<TSharedPtr<FJsonValue>> ModsArr;
	for (const TObjectPtr<UInputModifier>& Mod : Action->Modifiers)
	{
		if (!Mod) continue;
		FString CN = Mod->GetClass()->GetName();
		CN.RemoveFromStart(TEXT("InputModifier"));
		ModsArr.Add(MakeShareable(new FJsonValueString(CN.ToLower())));
	}
	Res->SetArrayField(TEXT("default_modifiers"), ModsArr);

	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Res.ToSharedRef(), W);
}

void HandleSetupEnhancedInput(const FString& SavePath, const TArray<TSharedPtr<FJsonValue>>& InputActionsJson, const FString& IMCName, const TArray<TSharedPtr<FJsonValue>>& MappingsJson, FString& OutJsonString, FString& OutError)
{

	FString EffectiveSavePath = SavePath;
	if (EffectiveSavePath.IsEmpty())
	{
		FString DummyError;
		UECPContentBrowserUtils::GetFocusedContentBrowserPath(EffectiveSavePath, DummyError);
	}
	if (EffectiveSavePath.IsEmpty()) EffectiveSavePath = TEXT("/Game/Input");
	while (EffectiveSavePath.EndsWith(TEXT("/"))) EffectiveSavePath.RemoveFromEnd(TEXT("/"));

	if (!UEditorAssetLibrary::DoesDirectoryExist(EffectiveSavePath))
		UEditorAssetLibrary::MakeDirectory(EffectiveSavePath);

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	TArray<TSharedPtr<FJsonValue>> CreatedActions;
	TMap<FString, FString> ActionNameToPath;

	for (const auto& IAVal : InputActionsJson)
	{
		const TSharedPtr<FJsonObject>* IAObj;
		if (!IAVal->TryGetObject(IAObj)) continue;

		FString IAName, IAValueType;
		(*IAObj)->TryGetStringField(TEXT("name"), IAName);
		(*IAObj)->TryGetStringField(TEXT("value_type"), IAValueType);
		if (IAName.IsEmpty()) continue;

		EInputActionValueType ActionType = IAValueType.IsEmpty() ? EInputActionValueType::Boolean : ParseValueType(IAValueType);

		UInputAction* NewAction = Cast<UInputAction>(AssetToolsModule.Get().CreateAsset(IAName, EffectiveSavePath, UInputAction::StaticClass(), nullptr));
		if (!NewAction)
		{
			FString ExistingPath = FString::Printf(TEXT("%s/%s.%s"), *EffectiveSavePath, *IAName, *IAName);
			NewAction = LoadObject<UInputAction>(nullptr, *ExistingPath);
		}
		if (!NewAction)
		{
			continue;
		}

		NewAction->ValueType = ActionType;
		NewAction->MarkPackageDirty();

		FString AssetPath = NewAction->GetPathName();
		ActionNameToPath.Add(IAName, AssetPath);

		TSharedPtr<FJsonObject> IAResult = MakeShareable(new FJsonObject);
		IAResult->SetStringField(TEXT("name"), IAName);
		IAResult->SetStringField(TEXT("path"), AssetPath);
		IAResult->SetStringField(TEXT("value_type"), ValueTypeToString(ActionType));
		CreatedActions.Add(MakeShareable(new FJsonValueObject(IAResult)));
	}

	if (IMCName.IsEmpty())
	{
		Result->SetBoolField(TEXT("success"), true);
		Result->SetArrayField(TEXT("created_actions"), CreatedActions);
		Result->SetStringField(TEXT("imc_path"), TEXT(""));
		Result->SetNumberField(TEXT("mappings_added"), 0);
		TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJsonString);
		FJsonSerializer::Serialize(Result.ToSharedRef(), W);
		return;
	}

	UInputMappingContext* IMC = Cast<UInputMappingContext>(AssetToolsModule.Get().CreateAsset(IMCName, EffectiveSavePath, UInputMappingContext::StaticClass(), nullptr));
	if (!IMC)
	{
		FString IMCPath = FString::Printf(TEXT("%s/%s.%s"), *EffectiveSavePath, *IMCName, *IMCName);
		IMC = LoadObject<UInputMappingContext>(nullptr, *IMCPath);
	}
	if (!IMC)
	{
		OutError = FString::Printf(TEXT("Failed to create InputMappingContext '%s'"), *IMCName);
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error"), OutError);
		Result->SetArrayField(TEXT("created_actions"), CreatedActions);
		TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJsonString);
		FJsonSerializer::Serialize(Result.ToSharedRef(), W);
		return;
	}

	int32 MappingsAdded = 0;
	TArray<TSharedPtr<FJsonValue>> FailedKeys;

	for (const auto& MapVal : MappingsJson)
	{
		const TSharedPtr<FJsonObject>* MapObj;
		if (!MapVal->TryGetObject(MapObj)) continue;

		FString ActionName, KeyName;
		if (!(*MapObj)->TryGetStringField(TEXT("action_name"), ActionName))
			(*MapObj)->TryGetStringField(TEXT("action"), ActionName);
		(*MapObj)->TryGetStringField(TEXT("key"), KeyName);
		if (KeyName.IsEmpty()) continue;

		FString ActionPath;
		if (!(*MapObj)->TryGetStringField(TEXT("action_path"), ActionPath))
		{
			if (ActionNameToPath.Contains(ActionName))
				ActionPath = ActionNameToPath[ActionName];
			else if (!ActionName.IsEmpty())
				ActionPath = FString::Printf(TEXT("%s/%s.%s"), *EffectiveSavePath, *ActionName, *ActionName);
		}
		if (ActionPath.IsEmpty()) continue;

		UInputAction* Action = LoadObject<UInputAction>(nullptr, *ActionPath);
		if (!Action) continue;

		FKey MKey(*NormalizeKeyName(KeyName));
		if (!MKey.IsValid())
		{
			TSharedPtr<FJsonObject> Fail = MakeShareable(new FJsonObject);
			Fail->SetStringField(TEXT("key"), KeyName);
			Fail->SetStringField(TEXT("error"), FString::Printf(TEXT("Invalid key: %s"), *KeyName));
			FailedKeys.Add(MakeShareable(new FJsonValueObject(Fail)));
			continue;
		}

		TArray<TSharedPtr<FJsonValue>> Modifiers, Triggers;

		FString SwizzleOrder;
		if ((*MapObj)->TryGetStringField(TEXT("swizzle_axis"), SwizzleOrder) && !SwizzleOrder.IsEmpty() &&
			!SwizzleOrder.Equals(TEXT("XYZ"), ESearchCase::IgnoreCase))
		{
			TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject());
			Obj->SetStringField(TEXT("type"), TEXT("SwizzleAxis"));
			Obj->SetStringField(TEXT("order"), SwizzleOrder);
			Modifiers.Insert(MakeShareable(new FJsonValueObject(Obj)), 0);
		}
		bool bNegate = false;
		if ((*MapObj)->TryGetBoolField(TEXT("negate"), bNegate) && bNegate)
		{
			TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject());
			Obj->SetStringField(TEXT("type"), TEXT("Negate"));
			Modifiers.Add(MakeShareable(new FJsonValueObject(Obj)));
		}
		double ScaleNum = 0.0;
		const TSharedPtr<FJsonObject>* ScaleObjPtr = nullptr;
		if ((*MapObj)->TryGetNumberField(TEXT("scale"), ScaleNum))
		{
			TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject());
			Obj->SetStringField(TEXT("type"), TEXT("Scalar"));
			Obj->SetNumberField(TEXT("scalar"), ScaleNum);
			Modifiers.Add(MakeShareable(new FJsonValueObject(Obj)));
		}
		else if ((*MapObj)->TryGetObjectField(TEXT("scale"), ScaleObjPtr))
		{
			TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject());
			Obj->SetStringField(TEXT("type"), TEXT("Scalar"));
			double X = 1.0, Y = 1.0, Z = 1.0;
			(*ScaleObjPtr)->TryGetNumberField(TEXT("x"), X);
			(*ScaleObjPtr)->TryGetNumberField(TEXT("y"), Y);
			(*ScaleObjPtr)->TryGetNumberField(TEXT("z"), Z);
			Obj->SetNumberField(TEXT("x"), X);
			Obj->SetNumberField(TEXT("y"), Y);
			Obj->SetNumberField(TEXT("z"), Z);
			Modifiers.Add(MakeShareable(new FJsonValueObject(Obj)));
		}
		const TArray<TSharedPtr<FJsonValue>>* ModsArr;
		if ((*MapObj)->TryGetArrayField(TEXT("modifiers"), ModsArr))
			for (const auto& MV : *ModsArr) Modifiers.Add(MV);

		FString TrigType;
		if (!(*MapObj)->TryGetStringField(TEXT("trigger_type"), TrigType))
			(*MapObj)->TryGetStringField(TEXT("trigger"), TrigType);
		if (!TrigType.IsEmpty())
		{
			if (TrigType.ToLower() == TEXT("hold"))
			{
				TSharedPtr<FJsonObject> HoldObj = MakeShareable(new FJsonObject);
				HoldObj->SetStringField(TEXT("type"), TEXT("hold"));
				double HoldTime = 1.0;
				if (!(*MapObj)->TryGetNumberField(TEXT("hold_time_threshold"), HoldTime))
					(*MapObj)->TryGetNumberField(TEXT("hold_time"), HoldTime);
				HoldObj->SetNumberField(TEXT("hold_time_threshold"), HoldTime);
				Triggers.Add(MakeShareable(new FJsonValueObject(HoldObj)));
			}
			else
				Triggers.Add(MakeShareable(new FJsonValueString(TrigType)));
		}
		const TArray<TSharedPtr<FJsonValue>>* TrigsArr;
		if ((*MapObj)->TryGetArrayField(TEXT("triggers"), TrigsArr))
			for (const auto& TV : *TrigsArr) Triggers.Add(TV);

		IMC->UnmapKey(Action, MKey);
		FEnhancedActionKeyMapping& Mapping = IMC->MapKey(Action, MKey);
		for (const auto& TV : Triggers)
			if (UInputTrigger* T = CreateTrigger(TV, IMC)) Mapping.Triggers.Add(T);
		for (const auto& MV : Modifiers)
			if (UInputModifier* M = CreateModifier(MV, IMC)) Mapping.Modifiers.Add(M);

		MappingsAdded++;
	}

	IMC->MarkPackageDirty();

	Result->SetBoolField(TEXT("success"), true);
	Result->SetArrayField(TEXT("created_actions"), CreatedActions);
	Result->SetStringField(TEXT("imc_path"), IMC->GetPathName());
	Result->SetNumberField(TEXT("mappings_added"), MappingsAdded);
	if (FailedKeys.Num() > 0)
		Result->SetArrayField(TEXT("failed_keys"), FailedKeys);
	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Result.ToSharedRef(), W);
}

void HandleCreateInputActionFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("actions"), ItemsArray))
	{
		FString SavePath;
		Args->TryGetStringField(TEXT("save_path"), SavePath);
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString Name = BatchToolHelper::GetItemString(Item, TEXT("name"), TEXT("action_name"));
			FString SP = BatchToolHelper::GetItemString(Item, TEXT("save_path"));
			if (SP.IsEmpty()) SP = SavePath;
			FString VT = BatchToolHelper::GetItemString(Item, TEXT("value_type"));
			if (Name.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing name")); continue; }
			FString ItemOut, ItemErr;
			HandleCreateInputAction(Name, SP, VT, ItemOut, ItemErr);
			if (ItemErr.IsEmpty()) { auto E = MakeShared<FJsonObject>(); E->SetStringField(TEXT("name"), Name); Batch.AddSuccess(i, E); }
			else Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJsonString);
		return;
	}
	FString Name, SavePath, ValueType;
	Args->TryGetStringField(TEXT("name"), Name);
	if (Name.IsEmpty()) Args->TryGetStringField(TEXT("action_name"), Name);
	Args->TryGetStringField(TEXT("save_path"), SavePath);
	Args->TryGetStringField(TEXT("value_type"), ValueType);
	HandleCreateInputAction(Name, SavePath, ValueType, OutJsonString, OutError);
}

void HandleAddInputMappingFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString ContextPath;
	if (!Args->TryGetStringField(TEXT("context_path"), ContextPath))
		Args->TryGetStringField(TEXT("mapping_context_path"), ContextPath);
	if (ContextPath.IsEmpty())
		Args->TryGetStringField(TEXT("imc_path"), ContextPath);

	auto ResolveActionName = [](const FString& ActionName) -> FString
	{
		IAssetRegistry& AR = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
		TArray<FAssetData> Found;
		AR.GetAssetsByClass(UInputAction::StaticClass()->GetClassPathName(), Found, true);
		for (const FAssetData& A : Found)
			if (A.AssetName.ToString().Equals(ActionName, ESearchCase::IgnoreCase))
				return A.GetSoftObjectPath().ToString();
		return FString();
	};

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("mappings"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString AP = BatchToolHelper::GetItemString(Item, TEXT("action_path"), TEXT("ia_path"));
			if (AP.IsEmpty())
			{
				FString AN = BatchToolHelper::GetItemString(Item, TEXT("action_name"), TEXT("action"));
				if (!AN.IsEmpty()) AP = ResolveActionName(AN);
			}
			FString Key = BatchToolHelper::GetItemString(Item, TEXT("key"), TEXT("key_name"));
			if (AP.IsEmpty() || Key.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing action_path or key")); continue; }
			TArray<TSharedPtr<FJsonValue>> Mods, Trigs;
			const TArray<TSharedPtr<FJsonValue>>* Ptr;
			if (Item->TryGetArrayField(TEXT("modifiers"), Ptr)) Mods = *Ptr;
			if (Item->TryGetArrayField(TEXT("triggers"), Ptr)) Trigs = *Ptr;
			FString ItemOut, ItemErr;
			HandleAddInputMapping(ContextPath, AP, Key, Mods, Trigs, ItemOut, ItemErr);
			if (ItemErr.IsEmpty()) { auto E = MakeShared<FJsonObject>(); E->SetStringField(TEXT("key"), Key); Batch.AddSuccess(i, E); }
			else Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJsonString);
		return;
	}
	FString AP, Key;
	if (!Args->TryGetStringField(TEXT("action_path"), AP))
		if (!Args->TryGetStringField(TEXT("ia_path"), AP))
			Args->TryGetStringField(TEXT("input_action_path"), AP);
	if (AP.IsEmpty())
	{
		FString AN;
		if (!Args->TryGetStringField(TEXT("action_name"), AN))
			Args->TryGetStringField(TEXT("action"), AN);
		if (!AN.IsEmpty()) AP = ResolveActionName(AN);
	}
	if (!Args->TryGetStringField(TEXT("key"), Key))
		Args->TryGetStringField(TEXT("key_name"), Key);
	TArray<TSharedPtr<FJsonValue>> Mods, Trigs;
	const TArray<TSharedPtr<FJsonValue>>* Ptr;
	if (Args->TryGetArrayField(TEXT("modifiers"), Ptr)) Mods = *Ptr;
	if (Args->TryGetArrayField(TEXT("triggers"), Ptr)) Trigs = *Ptr;

	FString SwizzleOrder;
	if (Args->TryGetStringField(TEXT("swizzle_axis"), SwizzleOrder) && !SwizzleOrder.IsEmpty() &&
		!SwizzleOrder.Equals(TEXT("XYZ"), ESearchCase::IgnoreCase))
	{
		TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject());
		Obj->SetStringField(TEXT("type"), TEXT("SwizzleAxis"));
		Obj->SetStringField(TEXT("order"), SwizzleOrder);
		Mods.Insert(MakeShareable(new FJsonValueObject(Obj)), 0);
	}
	double ScaleNum = 0.0;
	const TSharedPtr<FJsonObject>* ScaleObjPtr = nullptr;
	if (Args->TryGetNumberField(TEXT("scale"), ScaleNum))
	{
		TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject());
		Obj->SetStringField(TEXT("type"), TEXT("Scalar"));
		Obj->SetNumberField(TEXT("scalar"), ScaleNum);
		Mods.Add(MakeShareable(new FJsonValueObject(Obj)));
	}
	else if (Args->TryGetObjectField(TEXT("scale"), ScaleObjPtr))
	{
		TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject());
		Obj->SetStringField(TEXT("type"), TEXT("Scalar"));
		double X = 1.0, Y = 1.0, Z = 1.0;
		(*ScaleObjPtr)->TryGetNumberField(TEXT("x"), X);
		(*ScaleObjPtr)->TryGetNumberField(TEXT("y"), Y);
		(*ScaleObjPtr)->TryGetNumberField(TEXT("z"), Z);
		Obj->SetNumberField(TEXT("x"), X);
		Obj->SetNumberField(TEXT("y"), Y);
		Obj->SetNumberField(TEXT("z"), Z);
		Mods.Add(MakeShareable(new FJsonValueObject(Obj)));
	}
	bool bNegate = false;
	if (Args->TryGetBoolField(TEXT("negate"), bNegate) && bNegate)
	{
		TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject());
		Obj->SetStringField(TEXT("type"), TEXT("Negate"));
		Mods.Add(MakeShareable(new FJsonValueObject(Obj)));
	}
	FString TriggerType;
	if (!Args->TryGetStringField(TEXT("trigger_type"), TriggerType))
		Args->TryGetStringField(TEXT("trigger"), TriggerType);
	if (!TriggerType.IsEmpty())
		Trigs.Add(MakeShareable(new FJsonValueString(TriggerType)));

	HandleAddInputMapping(ContextPath, AP, Key, Mods, Trigs, OutJsonString, OutError);
}

void HandleRemoveInputMappingFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString ContextPath;
	if (!Args->TryGetStringField(TEXT("context_path"), ContextPath))
		Args->TryGetStringField(TEXT("mapping_context_path"), ContextPath);

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("mappings"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString AP = BatchToolHelper::GetItemString(Item, TEXT("action_path"), TEXT("ia_path"));
			FString Key = BatchToolHelper::GetItemString(Item, TEXT("key"), TEXT("key_name"));
			if (AP.IsEmpty() || Key.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing action_path or key")); continue; }
			FString ItemOut, ItemErr;
			HandleRemoveInputMapping(ContextPath, AP, Key, ItemOut, ItemErr);
			if (ItemErr.IsEmpty()) { auto E = MakeShared<FJsonObject>(); E->SetStringField(TEXT("key"), Key); Batch.AddSuccess(i, E); }
			else Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJsonString);
		return;
	}
	FString AP, Key;
	Args->TryGetStringField(TEXT("action_path"), AP);
	Args->TryGetStringField(TEXT("key"), Key);
	HandleRemoveInputMapping(ContextPath, AP, Key, OutJsonString, OutError);
}

void HandleCreateInputMappingContextFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString ContextName;
	Args->TryGetStringField(TEXT("name"), ContextName);
	if (ContextName.IsEmpty()) Args->TryGetStringField(TEXT("context_name"), ContextName);

	FString SavePath;
	Args->TryGetStringField(TEXT("save_path"), SavePath);
	if (SavePath.IsEmpty())
	{
		FString DummyErr;
		UECPContentBrowserUtils::GetFocusedContentBrowserPath(SavePath, DummyErr);
	}

	TArray<TSharedPtr<FJsonValue>> Mappings;
	const TArray<TSharedPtr<FJsonValue>>* MP = nullptr;
	if (Args->TryGetArrayField(TEXT("mappings"), MP))
	{
		Mappings = *MP;
	}
	else
	{
		FString MappingsStr;
		if (Args->TryGetStringField(TEXT("mappings"), MappingsStr) && !MappingsStr.IsEmpty())
		{
			TArray<TSharedPtr<FJsonValue>> Parsed;
			TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(MappingsStr);
			if (FJsonSerializer::Deserialize(Reader, Parsed))
				Mappings = MoveTemp(Parsed);
		}
	}

	HandleCreateInputMappingContext(ContextName, SavePath, Mappings, OutJsonString, OutError);
}

void HandleGetInputMappingSummaryFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString ContextPath;
	Args->TryGetStringField(TEXT("context_path"), ContextPath);
	HandleGetInputMappingSummary(ContextPath, OutJsonString, OutError);
}

void HandleSetInputActionPropertiesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString AP, VT;
	Args->TryGetStringField(TEXT("action_path"), AP);
	Args->TryGetStringField(TEXT("value_type"), VT);
	bool bCI = false, bTP = false;
	const bool bHasCI = Args->TryGetBoolField(TEXT("consume_input"), bCI);
	const bool bHasTP = Args->TryGetBoolField(TEXT("trigger_when_paused"), bTP);
	HandleSetInputActionProperties(AP, VT, bHasCI, bCI, bHasTP, bTP, OutJsonString, OutError);
}

void HandleGetInputActionSummaryFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString AP;
	Args->TryGetStringField(TEXT("action_path"), AP);
	HandleGetInputActionSummary(AP, OutJsonString, OutError);
}

void HandleSetupEnhancedInputFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString SP, IMCName;
	Args->TryGetStringField(TEXT("save_path"), SP);
	if (!Args->TryGetStringField(TEXT("imc_name"), IMCName))
		Args->TryGetStringField(TEXT("name"), IMCName);

	auto ParseArrayArg = [&](std::initializer_list<const TCHAR*> Keys) -> TArray<TSharedPtr<FJsonValue>>
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		const TArray<TSharedPtr<FJsonValue>>* P = nullptr;
		for (const TCHAR* Key : Keys)
		{
			if (Args->TryGetArrayField(Key, P)) { Out = *P; return Out; }
		}
		for (const TCHAR* Key : Keys)
		{
			FString S;
			if (Args->TryGetStringField(Key, S) && !S.IsEmpty())
			{
				TArray<TSharedPtr<FJsonValue>> Parsed;
				TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(S);
				if (FJsonSerializer::Deserialize(R, Parsed)) return Parsed;
			}
		}
		return Out;
	};

	TArray<TSharedPtr<FJsonValue>> IAs   = ParseArrayArg({TEXT("input_actions"), TEXT("actions")});
	TArray<TSharedPtr<FJsonValue>> Maps  = ParseArrayArg({TEXT("mappings")});
	HandleSetupEnhancedInput(SP, IAs, IMCName, Maps, OutJsonString, OutError);
}

}
