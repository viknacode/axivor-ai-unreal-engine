// Copyright 2026, BlueprintsLab, All rights reserved.

#include "Tools/CurveTools.h"
#include "Tools/BatchToolHelper.h"
#include "Managers/SettingsManager.h"

#include "Curves/CurveFloat.h"
#include "Curves/CurveVector.h"
#include "Curves/CurveLinearColor.h"
#include "Curves/RichCurve.h"
#include "Factories/CurveFactory.h"

#include "EditorAssetLibrary.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/Package.h"
#include "Misc/PackageName.h"
#include "Editor.h"

#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace CurveTools
{

static UPackage* CreateAssetPackage(const FString& SavePath, const FString& Name, FString& OutFullPath)
{
	FString CleanPath = SavePath.EndsWith(TEXT("/")) ? SavePath : (SavePath + TEXT("/"));
	OutFullPath = CleanPath + Name;
	UPackage* Package = CreatePackage(*OutFullPath);
	Package->FullyLoad();
	return Package;
}

static ERichCurveInterpMode ParseInterpMode(const FString& Mode)
{
	if (Mode.Equals(TEXT("linear"), ESearchCase::IgnoreCase))   return RCIM_Linear;
	if (Mode.Equals(TEXT("constant"), ESearchCase::IgnoreCase)) return RCIM_Constant;
	return RCIM_Cubic;
}

void HandleCreateCurveFloat(const FString& Name, const FString& SavePath,
	const TArray<TPair<float,float>>& Keys, FString& OutJsonString, FString& OutError)
{
	if (Name.IsEmpty()) { OutError = TEXT("name is required"); return; }
	FString FullPath;
	UPackage* Package = CreateAssetPackage(SavePath, Name, FullPath);

	UCurveFloat* Curve = NewObject<UCurveFloat>(Package, *Name, RF_Public | RF_Standalone);
	if (!Curve) { OutError = TEXT("Failed to create CurveFloat"); return; }

	for (const auto& KV : Keys)
		Curve->FloatCurve.AddKey(KV.Key, KV.Value);

	Curve->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(Curve);

	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"asset_path\":\"%s\",\"key_count\":%d}"),
		*FullPath, Keys.Num());
}

void HandleCreateCurveVector(const FString& Name, const FString& SavePath,
	FString& OutJsonString, FString& OutError)
{
	if (Name.IsEmpty()) { OutError = TEXT("name is required"); return; }
	FString FullPath;
	UPackage* Package = CreateAssetPackage(SavePath, Name, FullPath);

	UCurveVector* Curve = NewObject<UCurveVector>(Package, *Name, RF_Public | RF_Standalone);
	if (!Curve) { OutError = TEXT("Failed to create CurveVector"); return; }

	Curve->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(Curve);

	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"asset_path\":\"%s\"}"), *FullPath);
}

void HandleCreateCurveLinearColor(const FString& Name, const FString& SavePath,
	FString& OutJsonString, FString& OutError)
{
	if (Name.IsEmpty()) { OutError = TEXT("name is required"); return; }
	FString FullPath;
	UPackage* Package = CreateAssetPackage(SavePath, Name, FullPath);

	UCurveLinearColor* Curve = NewObject<UCurveLinearColor>(Package, *Name, RF_Public | RF_Standalone);
	if (!Curve) { OutError = TEXT("Failed to create CurveLinearColor"); return; }

	Curve->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(Curve);

	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"asset_path\":\"%s\"}"), *FullPath);
}

void HandleAddCurveKey(const FString& AssetPath, float Time, float Value,
	int32 Channel, const FString& InterpMode, FString& OutJsonString, FString& OutError)
{
	UObject* Asset = UEditorAssetLibrary::LoadAsset(AssetPath);
	if (!Asset) { OutError = FString::Printf(TEXT("Could not load curve at '%s'"), *AssetPath); return; }

	ERichCurveInterpMode Interp = ParseInterpMode(InterpMode);
	FKeyHandle Handle = FKeyHandle::Invalid();

	if (UCurveFloat* CF = Cast<UCurveFloat>(Asset))
	{
		Handle = CF->FloatCurve.AddKey(Time, Value);
		FRichCurveKey& K = CF->FloatCurve.GetKey(Handle);
		K.InterpMode = Interp;
		CF->MarkPackageDirty();
	}
	else if (UCurveVector* CV = Cast<UCurveVector>(Asset))
	{
		int32 Ch = FMath::Clamp(Channel, 0, 2);
		Handle = CV->FloatCurves[Ch].AddKey(Time, Value);
		FRichCurveKey& K = CV->FloatCurves[Ch].GetKey(Handle);
		K.InterpMode = Interp;
		CV->MarkPackageDirty();
	}
	else if (UCurveLinearColor* CC = Cast<UCurveLinearColor>(Asset))
	{
		int32 Ch = FMath::Clamp(Channel, 0, 3);
		Handle = CC->FloatCurves[Ch].AddKey(Time, Value);
		FRichCurveKey& K = CC->FloatCurves[Ch].GetKey(Handle);
		K.InterpMode = Interp;
		CC->MarkPackageDirty();
	}
	else
	{
		OutError = FString::Printf(TEXT("Asset at '%s' is not a supported curve type"), *AssetPath);
		return;
	}

	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"time\":%f,\"value\":%f,\"channel\":%d}"),
		Time, Value, Channel);
}

void HandleGetCurveKeys(const FString& AssetPath, int32 Channel,
	FString& OutJsonString, FString& OutError)
{
	UObject* Asset = UEditorAssetLibrary::LoadAsset(AssetPath);
	if (!Asset) { OutError = FString::Printf(TEXT("Could not load curve at '%s'"), *AssetPath); return; }

	FRichCurve* Curve = nullptr;
	FString CurveType;
	if (UCurveFloat* CF = Cast<UCurveFloat>(Asset))
	{
		Curve = &CF->FloatCurve;
		CurveType = TEXT("float");
	}
	else if (UCurveVector* CV = Cast<UCurveVector>(Asset))
	{
		int32 Ch = FMath::Clamp(Channel, 0, 2);
		Curve = &CV->FloatCurves[Ch];
		CurveType = FString::Printf(TEXT("vector_ch%d"), Ch);
	}
	else if (UCurveLinearColor* CC = Cast<UCurveLinearColor>(Asset))
	{
		int32 Ch = FMath::Clamp(Channel, 0, 3);
		Curve = &CC->FloatCurves[Ch];
		CurveType = FString::Printf(TEXT("color_ch%d"), Ch);
	}

	if (!Curve) { OutError = TEXT("Asset is not a supported curve type"); return; }

	TArray<TSharedPtr<FJsonValue>> KeysArray;
	for (auto It = Curve->GetKeyHandleIterator(); It; ++It)
	{
		const FRichCurveKey& K = Curve->GetKey(*It);
		TSharedPtr<FJsonObject> KObj = MakeShareable(new FJsonObject);
		KObj->SetNumberField(TEXT("time"), K.Time);
		KObj->SetNumberField(TEXT("value"), K.Value);
		KObj->SetStringField(TEXT("interp"), K.InterpMode == RCIM_Linear ? TEXT("linear") :
			K.InterpMode == RCIM_Constant ? TEXT("constant") : TEXT("cubic"));
		KeysArray.Add(MakeShareable(new FJsonValueObject(KObj)));
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetStringField(TEXT("curve_type"), CurveType);
	Result->SetNumberField(TEXT("key_count"), KeysArray.Num());
	Result->SetArrayField(TEXT("keys"), KeysArray);

	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
}

void HandleRemoveCurveKey(const FString& AssetPath, float Time, int32 Channel,
	FString& OutJsonString, FString& OutError)
{
	UObject* Asset = UEditorAssetLibrary::LoadAsset(AssetPath);
	if (!Asset) { OutError = FString::Printf(TEXT("Could not load curve at '%s'"), *AssetPath); return; }

	FRichCurve* Curve = nullptr;
	if (UCurveFloat* CF = Cast<UCurveFloat>(Asset))
	{
		Curve = &CF->FloatCurve;
	}
	else if (UCurveVector* CV = Cast<UCurveVector>(Asset))
	{
		int32 Ch = FMath::Clamp(Channel, 0, 2);
		Curve = &CV->FloatCurves[Ch];
	}
	else if (UCurveLinearColor* CC = Cast<UCurveLinearColor>(Asset))
	{
		int32 Ch = FMath::Clamp(Channel, 0, 3);
		Curve = &CC->FloatCurves[Ch];
	}

	if (!Curve) { OutError = FString::Printf(TEXT("Asset at '%s' is not a supported curve type"), *AssetPath); return; }

	FKeyHandle KH = Curve->FindKey(Time);
	if (!Curve->IsKeyHandleValid(KH))
	{
		OutError = FString::Printf(TEXT("No key found near time=%.4f (tolerance=%.4f). Use get_curve_keys to list existing times."),
			Time, KINDA_SMALL_NUMBER);
		return;
	}

	Curve->DeleteKey(KH);
	Asset->MarkPackageDirty();

	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"asset_path\":\"%s\",\"removed_time\":%.4f,\"remaining_keys\":%d}"),
		*AssetPath, Time, Curve->GetNumKeys());
}

void HandleSetCurveKeyInterp(const FString& AssetPath, float Time, int32 Channel, const FString& InterpMode, FString& OutJsonString, FString& OutError)
{
	UObject* Asset = LoadObject<UObject>(nullptr, *AssetPath);
	if (!Asset) { OutError = FString::Printf(TEXT("Failed to load curve: %s"), *AssetPath); return; }

	FRichCurve* Curve = nullptr;
	if (UCurveFloat* CF = Cast<UCurveFloat>(Asset)) Curve = &CF->FloatCurve;
	else if (UCurveVector* CV = Cast<UCurveVector>(Asset)) Curve = &CV->FloatCurves[FMath::Clamp(Channel, 0, 2)];
	else if (UCurveLinearColor* CC = Cast<UCurveLinearColor>(Asset)) Curve = &CC->FloatCurves[FMath::Clamp(Channel, 0, 3)];
	if (!Curve) { OutError = TEXT("Asset is not a supported curve type"); return; }

	FKeyHandle KH = Curve->FindKey(Time);
	if (!Curve->IsKeyHandleValid(KH)) { OutError = FString::Printf(TEXT("No key found at time=%.4f"), Time); return; }

	ERichCurveInterpMode Mode = RCIM_Cubic;
	if (InterpMode.Equals(TEXT("linear"), ESearchCase::IgnoreCase)) Mode = RCIM_Linear;
	else if (InterpMode.Equals(TEXT("constant"), ESearchCase::IgnoreCase)) Mode = RCIM_Constant;
	else if (InterpMode.Equals(TEXT("cubic"), ESearchCase::IgnoreCase)) Mode = RCIM_Cubic;

	Curve->SetKeyInterpMode(KH, Mode);
	Asset->MarkPackageDirty();
	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"time\":%.4f,\"interp_mode\":\"%s\"}"), Time, *InterpMode);
}

void HandleSetCurveKeyTangent(const FString& AssetPath, float Time, int32 Channel, float ArriveTangent, float LeaveTangent, FString& OutJsonString, FString& OutError)
{
	UObject* Asset = LoadObject<UObject>(nullptr, *AssetPath);
	if (!Asset) { OutError = FString::Printf(TEXT("Failed to load curve: %s"), *AssetPath); return; }

	FRichCurve* Curve = nullptr;
	if (UCurveFloat* CF = Cast<UCurveFloat>(Asset)) Curve = &CF->FloatCurve;
	else if (UCurveVector* CV = Cast<UCurveVector>(Asset)) Curve = &CV->FloatCurves[FMath::Clamp(Channel, 0, 2)];
	else if (UCurveLinearColor* CC = Cast<UCurveLinearColor>(Asset)) Curve = &CC->FloatCurves[FMath::Clamp(Channel, 0, 3)];
	if (!Curve) { OutError = TEXT("Asset is not a supported curve type"); return; }

	FKeyHandle KH = Curve->FindKey(Time);
	if (!Curve->IsKeyHandleValid(KH)) { OutError = FString::Printf(TEXT("No key found at time=%.4f"), Time); return; }

	FRichCurveKey& Key = Curve->GetKey(KH);
	Key.ArriveTangent = ArriveTangent;
	Key.LeaveTangent = LeaveTangent;
	Key.TangentMode = RCTM_User;
	Asset->MarkPackageDirty();
	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"time\":%.4f,\"arrive_tangent\":%.4f,\"leave_tangent\":%.4f}"), Time, ArriveTangent, LeaveTangent);
}

void HandleAddCurveKeyFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString AssetPath; Args->TryGetStringField(TEXT("asset_path"), AssetPath);

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("keys"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			double T = 0, V = 0, Ch = 0;
			Item->TryGetNumberField(TEXT("time"), T); Item->TryGetNumberField(TEXT("value"), V);
			Item->TryGetNumberField(TEXT("channel"), Ch);
			FString IM; Item->TryGetStringField(TEXT("interp_mode"), IM);
			if (IM.IsEmpty()) IM = TEXT("cubic");
			FString ItemPath; Item->TryGetStringField(TEXT("asset_path"), ItemPath);
			if (ItemPath.IsEmpty()) ItemPath = AssetPath;
			FString ItemOut, ItemErr;
			HandleAddCurveKey(ItemPath, (float)T, (float)V, (int32)Ch, IM, ItemOut, ItemErr);
			if (ItemErr.IsEmpty()) Batch.AddSuccess(i);
			else Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJsonString);
		return;
	}
	double T = 0, V = 0, Ch = 0; FString IM;
	Args->TryGetNumberField(TEXT("time"), T); Args->TryGetNumberField(TEXT("value"), V);
	Args->TryGetNumberField(TEXT("channel"), Ch); Args->TryGetStringField(TEXT("interp_mode"), IM);
	if (IM.IsEmpty()) IM = TEXT("cubic");
	HandleAddCurveKey(AssetPath, (float)T, (float)V, (int32)Ch, IM, OutJsonString, OutError);
}

void HandleRemoveCurveKeyFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString AssetPath; Args->TryGetStringField(TEXT("asset_path"), AssetPath);

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("keys"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			double T = 0, Ch = 0;
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (Item.IsValid())
			{
				Item->TryGetNumberField(TEXT("time"), T);
				Item->TryGetNumberField(TEXT("channel"), Ch);
			}
			else
			{
				(*ItemsArray)[i]->TryGetNumber(T);
			}
			FString ItemOut, ItemErr;
			HandleRemoveCurveKey(AssetPath, (float)T, (int32)Ch, ItemOut, ItemErr);
			if (ItemErr.IsEmpty()) Batch.AddSuccess(i);
			else Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJsonString);
		return;
	}
	double T = 0, Ch = 0;
	Args->TryGetNumberField(TEXT("time"), T); Args->TryGetNumberField(TEXT("channel"), Ch);
	HandleRemoveCurveKey(AssetPath, (float)T, (int32)Ch, OutJsonString, OutError);
}

void HandleCreateCurveFloatFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }

	FString Name, SavePath;
	Args->TryGetStringField(TEXT("name"), Name);
	Args->TryGetStringField(TEXT("save_path"), SavePath);
	if (SavePath.IsEmpty()) SavePath = FSettingsManager::GetDefaultSavePath();

	TArray<TPair<float,float>> Keys;
	const TArray<TSharedPtr<FJsonValue>>* KeysArr = nullptr;
	if (Args->TryGetArrayField(TEXT("keys"), KeysArr))
	{
		for (const auto& KV : *KeysArr)
		{
			const TSharedPtr<FJsonObject>* KObj;
			if (KV->TryGetObject(KObj))
			{
				double T = 0, V = 0;
				(*KObj)->TryGetNumberField(TEXT("time"), T);
				(*KObj)->TryGetNumberField(TEXT("value"), V);
				Keys.Add(TPair<float,float>((float)T, (float)V));
			}
		}
	}

	HandleCreateCurveFloat(Name, SavePath, Keys, OutJsonString, OutError);
}

void HandleCreateCurveVectorFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString Name, SavePath;
	Args->TryGetStringField(TEXT("name"), Name);
	Args->TryGetStringField(TEXT("save_path"), SavePath);
	if (SavePath.IsEmpty()) SavePath = FSettingsManager::GetDefaultSavePath();
	HandleCreateCurveVector(Name, SavePath, OutJsonString, OutError);
}

void HandleCreateCurveLinearColorFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString Name, SavePath;
	Args->TryGetStringField(TEXT("name"), Name);
	Args->TryGetStringField(TEXT("save_path"), SavePath);
	if (SavePath.IsEmpty()) SavePath = FSettingsManager::GetDefaultSavePath();
	HandleCreateCurveLinearColor(Name, SavePath, OutJsonString, OutError);
}

void HandleGetCurveKeysFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString AssetPath;
	double Channel = 0;
	Args->TryGetStringField(TEXT("asset_path"), AssetPath);
	Args->TryGetNumberField(TEXT("channel"), Channel);
	HandleGetCurveKeys(AssetPath, (int32)Channel, OutJsonString, OutError);
}

void HandleSetCurveKeyInterpFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString AssetPath, InterpMode;
	double Time = 0, Channel = 0;
	Args->TryGetStringField(TEXT("asset_path"), AssetPath);
	Args->TryGetNumberField(TEXT("time"), Time);
	Args->TryGetNumberField(TEXT("channel"), Channel);
	Args->TryGetStringField(TEXT("interp_mode"), InterpMode);
	HandleSetCurveKeyInterp(AssetPath, (float)Time, (int32)Channel, InterpMode, OutJsonString, OutError);
}

void HandleSetCurveKeyTangentFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString AssetPath;
	double Time = 0, Channel = 0, Arrive = 0, Leave = 0;
	Args->TryGetStringField(TEXT("asset_path"), AssetPath);
	Args->TryGetNumberField(TEXT("time"), Time);
	Args->TryGetNumberField(TEXT("channel"), Channel);
	Args->TryGetNumberField(TEXT("arrive_tangent"), Arrive);
	Args->TryGetNumberField(TEXT("leave_tangent"), Leave);
	HandleSetCurveKeyTangent(AssetPath, (float)Time, (int32)Channel, (float)Arrive, (float)Leave, OutJsonString, OutError);
}

}
