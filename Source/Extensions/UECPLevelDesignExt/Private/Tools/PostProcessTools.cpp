// Copyright 2026, BlueprintsLab, All rights reserved.

#include "Tools/PostProcessTools.h"
#include "Managers/SettingsManager.h"
#include "Tools/BatchToolHelper.h"

#include "Engine/PostProcessVolume.h"
#include "Materials/MaterialParameterCollection.h"
#include "Engine/TextureRenderTarget2D.h"

#include "Subsystems/EditorActorSubsystem.h"
#include "EditorAssetLibrary.h"
#include "Materials/MaterialInterface.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Serialization/JsonSerializer.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Editor.h"

#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace PostProcessTools
{

static UPackage* CreateAssetPackage(const FString& SavePath, const FString& Name, FString& OutFullPath)
{
	FString CleanPath = SavePath.EndsWith(TEXT("/")) ? SavePath : (SavePath + TEXT("/"));
	OutFullPath = CleanPath + Name;
	UPackage* Package = CreatePackage(*OutFullPath);
	Package->FullyLoad();
	return Package;
}

static APostProcessVolume* FindPPVByLabel(const FString& Label, FString& OutError)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World) { OutError = TEXT("No editor world"); return nullptr; }
	for (TActorIterator<APostProcessVolume> It(World); It; ++It)
	{
		if (It->GetActorLabel().Equals(Label, ESearchCase::IgnoreCase))
			return *It;
	}
	OutError = FString::Printf(TEXT("No PostProcessVolume found with label '%s'"), *Label);
	return nullptr;
}

static FString SnakeToCamel(const FString& Snake)
{
	FString Result;
	bool bCapNext = true;
	for (TCHAR C : Snake)
	{
		if (C == '_') { bCapNext = true; }
		else if (bCapNext) { Result += FChar::ToUpper(C); bCapNext = false; }
		else { Result += C; }
	}
	return Result;
}

void HandleCreatePostProcessVolume(const FString& Label, float LocX, float LocY, float LocZ,
	bool bUnbound, FString& OutJsonString, FString& OutError)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World) { OutError = TEXT("No editor world available"); return; }

	UEditorActorSubsystem* EAS = GEditor->GetEditorSubsystem<UEditorActorSubsystem>();
	if (!EAS) { OutError = TEXT("EditorActorSubsystem unavailable"); return; }

	FActorSpawnParameters Params;
	APostProcessVolume* PPV = World->SpawnActor<APostProcessVolume>(
		APostProcessVolume::StaticClass(), FVector(LocX, LocY, LocZ), FRotator::ZeroRotator, Params);
	if (!PPV) { OutError = TEXT("Failed to spawn PostProcessVolume"); return; }

	PPV->SetActorLabel(Label);
	PPV->bUnbound = bUnbound;
	PPV->bEnabled = true;
	PPV->BlendWeight = 1.0f;
	World->MarkPackageDirty();
	GEditor->NoteSelectionChange();

	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"label\":\"%s\",\"unbound\":%s}"),
		*Label, bUnbound ? TEXT("true") : TEXT("false"));
}

static FString ResolvePPPropertyName(const FString& InName)
{
	static const TMap<FString, FString> Aliases = {
		{ TEXT("DepthOfFieldAperture"),           TEXT("DepthOfFieldFstop") },
		{ TEXT("DepthOfFieldFStop"),               TEXT("DepthOfFieldFstop") },
		{ TEXT("Aperture"),                        TEXT("DepthOfFieldFstop") },
		{ TEXT("FStop"),                           TEXT("DepthOfFieldFstop") },
		{ TEXT("DepthOfFieldFocalLength"),         TEXT("DepthOfFieldFocalDistance") },
		{ TEXT("AutoExposureBias"),                TEXT("AutoExposureBias") },
		{ TEXT("ExposureBias"),                    TEXT("AutoExposureBias") },
		{ TEXT("AutoExposureMinBrightness"),       TEXT("AutoExposureMinBrightness") },
		{ TEXT("AutoExposureMaxBrightness"),       TEXT("AutoExposureMaxBrightness") },
		{ TEXT("BloomSize"),                       TEXT("BloomSizeScale") },
		{ TEXT("AmbientOcclusion"),                TEXT("AmbientOcclusionIntensity") },
		{ TEXT("AOIntensity"),                     TEXT("AmbientOcclusionIntensity") },
		{ TEXT("MotionBlur"),                      TEXT("MotionBlurAmount") },
		{ TEXT("Vignette"),                        TEXT("VignetteIntensity") },
	};
	if (const FString* Found = Aliases.Find(InName))
		return *Found;
	return InName;
}

void HandleSetPostProcessProperty(const FString& ActorLabel, const FString& PropertyName,
	const FString& PropertyValue, FString& OutJsonString, FString& OutError)
{
	APostProcessVolume* PPV = FindPPVByLabel(ActorLabel, OutError);
	if (!PPV) return;

	FString CamelName = PropertyName.Contains(TEXT("_")) ? SnakeToCamel(PropertyName) : PropertyName;
	CamelName = ResolvePPPropertyName(CamelName);

	UScriptStruct* PPSStruct = FPostProcessSettings::StaticStruct();
	FProperty* Prop = FindFProperty<FProperty>(PPSStruct, *CamelName);
	if (!Prop)
	{
		TArray<FString> Suggestions;
		for (TFieldIterator<FProperty> It(PPSStruct); It; ++It)
		{
			FString PName = It->GetName();
			if (!PName.StartsWith(TEXT("bOverride_")) && PName.Contains(CamelName, ESearchCase::IgnoreCase))
				Suggestions.Add(PName);
		}
		FString SuggestStr = Suggestions.Num() > 0
			? FString::Printf(TEXT(" Suggestions: %s"), *FString::Join(Suggestions, TEXT(", ")))
			: TEXT("");
		OutError = FString::Printf(TEXT("Property '%s' not found on FPostProcessSettings.%s"), *CamelName, *SuggestStr);
		return;
	}

	void* SettingsPtr = &PPV->Settings;
	const TCHAR* ValueStr = *PropertyValue;
	if (Prop->ImportText_Direct(ValueStr, Prop->ContainerPtrToValuePtr<void>(SettingsPtr), nullptr, PPF_None) == nullptr)
	{
		OutError = FString::Printf(TEXT("Failed to set '%s' to '%s'"), *CamelName, *PropertyValue);
		return;
	}

	FString OverrideName = TEXT("bOverride_") + CamelName;
	FBoolProperty* OverrideProp = FindFProperty<FBoolProperty>(PPSStruct, *OverrideName);
	if (OverrideProp)
		OverrideProp->SetPropertyValue(OverrideProp->ContainerPtrToValuePtr<void>(SettingsPtr), true);

	PPV->MarkPackageDirty();
	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"property\":\"%s\",\"value\":\"%s\"}"),
		*CamelName, *PropertyValue);
}

void HandleCreateMaterialParameterCollection(const FString& Name, const FString& SavePath,
	FString& OutJsonString, FString& OutError)
{
	FString FullPath;
	UPackage* Package = CreateAssetPackage(SavePath, Name, FullPath);

	UMaterialParameterCollection* MPC = NewObject<UMaterialParameterCollection>(
		Package, *Name, RF_Public | RF_Standalone);
	if (!MPC) { OutError = TEXT("Failed to create MaterialParameterCollection"); return; }

	MPC->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(MPC);

	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"asset_path\":\"%s\"}"), *FullPath);
}

void HandleAddMPCParameter(const FString& AssetPath, const FString& ParamName,
	const FString& ParamType, const FString& DefaultValue, FString& OutJsonString, FString& OutError)
{
	UMaterialParameterCollection* MPC = Cast<UMaterialParameterCollection>(
		UEditorAssetLibrary::LoadAsset(AssetPath));
	if (!MPC)
	{
		OutError = FString::Printf(TEXT("Could not load MPC at '%s'"), *AssetPath);
		return;
	}

	FName PName(*ParamName);
	if (ParamType.Equals(TEXT("scalar"), ESearchCase::IgnoreCase) ||
		ParamType.Equals(TEXT("float"), ESearchCase::IgnoreCase))
	{
		for (const auto& P : MPC->ScalarParameters)
			if (P.ParameterName == PName) { OutError = TEXT("Scalar parameter already exists"); return; }

		FCollectionScalarParameter Param;
		Param.ParameterName = PName;
		Param.Id = FGuid::NewGuid();
		Param.DefaultValue = FCString::Atof(*DefaultValue);
		MPC->ScalarParameters.Add(Param);
	}
	else
	{
		for (const auto& P : MPC->VectorParameters)
			if (P.ParameterName == PName) { OutError = TEXT("Vector parameter already exists"); return; }

		FCollectionVectorParameter Param;
		Param.ParameterName = PName;
		Param.Id = FGuid::NewGuid();
		TArray<FString> Parts;
		DefaultValue.ParseIntoArray(Parts, TEXT(","), true);
		Param.DefaultValue = FLinearColor(
			Parts.Num() > 0 ? FCString::Atof(*Parts[0]) : 0.f,
			Parts.Num() > 1 ? FCString::Atof(*Parts[1]) : 0.f,
			Parts.Num() > 2 ? FCString::Atof(*Parts[2]) : 0.f,
			Parts.Num() > 3 ? FCString::Atof(*Parts[3]) : 1.f);
		MPC->VectorParameters.Add(Param);
	}

	MPC->PostEditChange();
	MPC->MarkPackageDirty();
	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"param_name\":\"%s\",\"type\":\"%s\"}"),
		*ParamName, *ParamType);
}

void HandleCreateRenderTarget(const FString& Name, const FString& SavePath,
	int32 Width, int32 Height, const FString& Format, FString& OutJsonString, FString& OutError)
{
	FString FullPath;
	UPackage* Package = CreateAssetPackage(SavePath, Name, FullPath);

	UTextureRenderTarget2D* RT = NewObject<UTextureRenderTarget2D>(
		Package, *Name, RF_Public | RF_Standalone);
	if (!RT) { OutError = TEXT("Failed to create RenderTarget2D"); return; }

	ETextureRenderTargetFormat RTFormat = RTF_RGBA8;
	if (Format.Equals(TEXT("RGBA16f"), ESearchCase::IgnoreCase))      RTFormat = RTF_RGBA16f;
	else if (Format.Equals(TEXT("RGBA32f"), ESearchCase::IgnoreCase)) RTFormat = RTF_RGBA32f;
	else if (Format.Equals(TEXT("R32f"), ESearchCase::IgnoreCase))    RTFormat = RTF_R32f;
	else if (Format.Equals(TEXT("R16f"), ESearchCase::IgnoreCase))    RTFormat = RTF_R16f;

	RT->RenderTargetFormat = RTFormat;
	RT->InitAutoFormat(FMath::Max(1, Width), FMath::Max(1, Height));
	RT->ClearColor = FLinearColor::Black;
	RT->bAutoGenerateMips = false;
	RT->UpdateResourceImmediate(true);

	RT->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(RT);

	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"asset_path\":\"%s\",\"width\":%d,\"height\":%d,\"format\":\"%s\"}"),
		*FullPath, Width, Height, *Format);
}

void HandleGetPostProcessSummary(const FString& ActorLabel, FString& OutJsonString, FString& OutError)
{

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World) { OutError = TEXT("No editor world"); return; }

	APostProcessVolume* PPV = nullptr;
	for (TActorIterator<APostProcessVolume> It(World); It; ++It)
	{
		if (ActorLabel.IsEmpty() || It->GetActorLabel().Equals(ActorLabel, ESearchCase::IgnoreCase))
		{
			PPV = *It;
			break;
		}
	}
	if (!PPV) { OutError = FString::Printf(TEXT("PostProcessVolume '%s' not found"), *ActorLabel); return; }

	TSharedPtr<FJsonObject> Res = MakeShareable(new FJsonObject());
	Res->SetBoolField(TEXT("success"), true);
	Res->SetStringField(TEXT("actor_label"), PPV->GetActorLabel());
	Res->SetBoolField(TEXT("infinite_extent"), PPV->bUnbound);
	Res->SetBoolField(TEXT("enabled"), PPV->bEnabled);
	Res->SetNumberField(TEXT("priority"), PPV->Priority);
	Res->SetNumberField(TEXT("blend_radius"), PPV->BlendRadius);
	Res->SetNumberField(TEXT("blend_weight"), PPV->BlendWeight);

	TArray<TSharedPtr<FJsonValue>> Overrides;
	UScriptStruct* PPSStruct = FPostProcessSettings::StaticStruct();
	const void* SettingsPtr = &PPV->Settings;
	for (TFieldIterator<FProperty> It(PPSStruct); It; ++It)
	{
		FProperty* OverrideProp = *It;
		const FString OverrideName = OverrideProp->GetName();
		if (!OverrideName.StartsWith(TEXT("bOverride_"))) continue;
		FBoolProperty* BoolProp = CastField<FBoolProperty>(OverrideProp);
		if (!BoolProp) continue;
		const bool bSet = BoolProp->GetPropertyValue(BoolProp->ContainerPtrToValuePtr<void>(SettingsPtr));
		if (!bSet) continue;
		const FString FieldName = OverrideName.Mid(10);
		FProperty* ValueProp = FindFProperty<FProperty>(PPSStruct, *FieldName);
		if (!ValueProp) continue;
		FString Exported;
		ValueProp->ExportTextItem_Direct(Exported, ValueProp->ContainerPtrToValuePtr<void>(SettingsPtr), nullptr, nullptr, PPF_None);
		TSharedPtr<FJsonObject> O = MakeShareable(new FJsonObject());
		O->SetStringField(TEXT("name"), FieldName);
		O->SetStringField(TEXT("value"), Exported);
		Overrides.Add(MakeShareable(new FJsonValueObject(O)));
	}
	Res->SetArrayField(TEXT("overrides"), Overrides);
	Res->SetNumberField(TEXT("override_count"), Overrides.Num());

	TArray<TSharedPtr<FJsonValue>> Blendables;
	for (const FWeightedBlendable& WB : PPV->Settings.WeightedBlendables.Array)
	{
		TSharedPtr<FJsonObject> O = MakeShareable(new FJsonObject());
		O->SetStringField(TEXT("path"), WB.Object ? WB.Object->GetPathName() : FString());
		O->SetNumberField(TEXT("weight"), WB.Weight);
		Blendables.Add(MakeShareable(new FJsonValueObject(O)));
	}
	Res->SetArrayField(TEXT("blendables"), Blendables);

	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Res.ToSharedRef(), W);
}

void HandleAddPostProcessBlendable(const FString& ActorLabel, const FString& MaterialPath, float Weight, FString& OutJsonString, FString& OutError)
{

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World) { OutError = TEXT("No editor world"); return; }

	APostProcessVolume* PPV = nullptr;
	for (TActorIterator<APostProcessVolume> It(World); It; ++It)
	{
		if (ActorLabel.IsEmpty() || It->GetActorLabel().Equals(ActorLabel, ESearchCase::IgnoreCase))
		{
			PPV = *It;
			break;
		}
	}
	if (!PPV) { OutError = TEXT("PostProcessVolume not found"); return; }

	UMaterialInterface* Mat = Cast<UMaterialInterface>(UEditorAssetLibrary::LoadAsset(MaterialPath));
	if (!Mat) { OutError = FString::Printf(TEXT("Could not load material at '%s'"), *MaterialPath); return; }

	PPV->Settings.AddBlendable(Mat, Weight);
	PPV->MarkPackageDirty();

	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"actor_label\":\"%s\",\"material\":\"%s\",\"weight\":%.2f}"),
		*PPV->GetActorLabel(), *MaterialPath, Weight);
}

void HandleSetMPCDefaultValue(const FString& AssetPath, const FString& ParamName,
	float ScalarValue, bool bIsScalar, FString& OutJsonString, FString& OutError)
{

	UMaterialParameterCollection* MPC = Cast<UMaterialParameterCollection>(UEditorAssetLibrary::LoadAsset(AssetPath));
	if (!MPC) { OutError = FString::Printf(TEXT("Could not load MPC at '%s'"), *AssetPath); return; }

	bool bFound = false;
	for (FCollectionScalarParameter& Param : MPC->ScalarParameters)
	{
		if (Param.ParameterName.ToString().Equals(ParamName, ESearchCase::IgnoreCase))
		{
			Param.DefaultValue = ScalarValue;
			bFound = true;
			break;
		}
	}

	if (!bFound) { OutError = FString::Printf(TEXT("Scalar parameter '%s' not found in MPC"), *ParamName); return; }

	MPC->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(AssetPath, false);

	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"mpc_path\":\"%s\",\"parameter\":\"%s\",\"value\":%g}"),
		*AssetPath, *ParamName, ScalarValue);
}

void HandleRemovePostProcessBlendable(const FString& ActorLabel, const FString& MaterialPath,
	FString& OutJsonString, FString& OutError)
{

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World) { OutError = TEXT("No editor world"); return; }

	APostProcessVolume* PPV = nullptr;
	for (TActorIterator<APostProcessVolume> It(World); It; ++It)
	{
		if (ActorLabel.IsEmpty() || It->GetActorLabel().Equals(ActorLabel, ESearchCase::IgnoreCase))
		{
			PPV = *It;
			break;
		}
	}
	if (!PPV) { OutError = TEXT("PostProcessVolume not found"); return; }

	int32 RemovedCount = 0;
	for (int32 i = PPV->Settings.WeightedBlendables.Array.Num() - 1; i >= 0; --i)
	{
		UObject* Obj = PPV->Settings.WeightedBlendables.Array[i].Object;
		if (Obj && (MaterialPath.IsEmpty() || Obj->GetPathName().Contains(MaterialPath, ESearchCase::IgnoreCase)))
		{
			PPV->Settings.WeightedBlendables.Array.RemoveAt(i);
			RemovedCount++;
		}
	}

	if (RemovedCount == 0) { OutError = TEXT("No matching blendable found to remove"); return; }

	PPV->MarkPackageDirty();
	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"actor_label\":\"%s\",\"removed\":%d}"),
		*PPV->GetActorLabel(), RemovedCount);
}

void HandleSetPostProcessPriority(const FString& ActorLabel, float Priority, float BlendRadius, float BlendWeight,
	bool bPrioritySet, bool bRadiusSet, bool bWeightSet,
	FString& OutJsonString, FString& OutError)
{

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World) { OutError = TEXT("No editor world"); return; }

	APostProcessVolume* PPV = nullptr;
	for (TActorIterator<APostProcessVolume> It(World); It; ++It)
	{
		if (ActorLabel.IsEmpty() || It->GetActorLabel().Equals(ActorLabel, ESearchCase::IgnoreCase))
		{
			PPV = *It;
			break;
		}
	}
	if (!PPV) { OutError = TEXT("PostProcessVolume not found"); return; }

	if (bPrioritySet) PPV->Priority = Priority;
	if (bRadiusSet) PPV->BlendRadius = BlendRadius;
	if (bWeightSet) PPV->BlendWeight = BlendWeight;

	PPV->MarkPackageDirty();
	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"actor_label\":\"%s\",\"priority\":%g,\"blend_radius\":%g,\"blend_weight\":%g}"),
		*PPV->GetActorLabel(), PPV->Priority, PPV->BlendRadius, PPV->BlendWeight);
}

void HandleCreatePostProcessVolumeFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString Label;
	double LocX = 0, LocY = 0, LocZ = 0;
	bool bUnbound = true;
	Args->TryGetStringField(TEXT("label"), Label);
	if (Label.IsEmpty()) Label = TEXT("PostProcessVolume");
	Args->TryGetNumberField(TEXT("location_x"), LocX);
	Args->TryGetNumberField(TEXT("location_y"), LocY);
	Args->TryGetNumberField(TEXT("location_z"), LocZ);
	Args->TryGetBoolField(TEXT("unbound"), bUnbound);
	HandleCreatePostProcessVolume(Label, (float)LocX, (float)LocY, (float)LocZ, bUnbound, OutJsonString, OutError);
}

void HandleSetPostProcessPropertyFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString ActorLabel;
	Args->TryGetStringField(TEXT("actor_label"), ActorLabel);
	if (ActorLabel.IsEmpty()) Args->TryGetStringField(TEXT("asset_path"), ActorLabel);
	if (ActorLabel.IsEmpty()) Args->TryGetStringField(TEXT("volume_label"), ActorLabel);

	auto NormaliseName = [](FString Name) -> FString
	{
		if (Name.Contains(TEXT("."))) Name.ReplaceInline(TEXT("."), TEXT(""), ESearchCase::IgnoreCase);
		return Name;
	};

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("properties"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString PName = BatchToolHelper::GetItemString(Item, TEXT("property_name"), TEXT("name"));
			FString PValue = BatchToolHelper::GetItemString(Item, TEXT("property_value"), TEXT("value"));
			if (PName.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing property_name")); continue; }
			PName = NormaliseName(PName);
			FString ItemOut, ItemErr;
			HandleSetPostProcessProperty(ActorLabel, PName, PValue, ItemOut, ItemErr);
			if (ItemErr.IsEmpty())
			{
				TSharedPtr<FJsonObject> Extra = MakeShared<FJsonObject>();
				Extra->SetStringField(TEXT("property"), PName);
				Batch.AddSuccess(i, Extra);
			}
			else Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJsonString);
		return;
	}

	FString PropertyName, PropertyValue;
	Args->TryGetStringField(TEXT("property_name"), PropertyName);
	Args->TryGetStringField(TEXT("property_value"), PropertyValue);
	PropertyName = NormaliseName(PropertyName);
	HandleSetPostProcessProperty(ActorLabel, PropertyName, PropertyValue, OutJsonString, OutError);
}

void HandleCreateMaterialParameterCollectionFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString Name, SavePath;
	Args->TryGetStringField(TEXT("name"), Name);
	Args->TryGetStringField(TEXT("save_path"), SavePath);
	if (SavePath.IsEmpty()) SavePath = FSettingsManager::GetDefaultSavePath();
	HandleCreateMaterialParameterCollection(Name, SavePath, OutJsonString, OutError);
}

void HandleAddMPCParameterFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString AssetPath, ParamName, ParamType, DefaultValue;
	Args->TryGetStringField(TEXT("asset_path"), AssetPath);
	Args->TryGetStringField(TEXT("param_name"), ParamName);
	Args->TryGetStringField(TEXT("param_type"), ParamType);
	if (ParamType.IsEmpty()) ParamType = TEXT("scalar");
	Args->TryGetStringField(TEXT("default_value"), DefaultValue);
	if (DefaultValue.IsEmpty()) DefaultValue = TEXT("0");
	HandleAddMPCParameter(AssetPath, ParamName, ParamType, DefaultValue, OutJsonString, OutError);
}

void HandleCreateRenderTargetFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString Name, SavePath, Format;
	double Width = 512, Height = 512;
	Args->TryGetStringField(TEXT("name"), Name);
	Args->TryGetStringField(TEXT("save_path"), SavePath);
	if (SavePath.IsEmpty()) SavePath = FSettingsManager::GetDefaultSavePath();
	Args->TryGetNumberField(TEXT("width"), Width);
	Args->TryGetNumberField(TEXT("height"), Height);
	Args->TryGetStringField(TEXT("format"), Format);
	if (Format.IsEmpty()) Format = TEXT("RGBA8");
	HandleCreateRenderTarget(Name, SavePath, (int32)Width, (int32)Height, Format, OutJsonString, OutError);
}

void HandleGetPostProcessSummaryFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString ActorLabel;
	Args->TryGetStringField(TEXT("actor_label"), ActorLabel);
	HandleGetPostProcessSummary(ActorLabel, OutJsonString, OutError);
}

void HandleAddPostProcessBlendableFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString ActorLabel, MaterialPath;
	double Weight = 1.0;
	Args->TryGetStringField(TEXT("actor_label"), ActorLabel);
	Args->TryGetStringField(TEXT("material_path"), MaterialPath);
	Args->TryGetNumberField(TEXT("weight"), Weight);
	HandleAddPostProcessBlendable(ActorLabel, MaterialPath, (float)Weight, OutJsonString, OutError);
}

void HandleSetMPCDefaultValueFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString AssetPath, ParamName, VectorValue;
	double ScalarValue = 0;
	bool bIsScalar = true;
	Args->TryGetStringField(TEXT("asset_path"), AssetPath);
	Args->TryGetStringField(TEXT("parameter_name"), ParamName);
	if (ParamName.IsEmpty()) Args->TryGetStringField(TEXT("param_name"), ParamName);
	Args->TryGetNumberField(TEXT("scalar_value"), ScalarValue);
	Args->TryGetBoolField(TEXT("is_scalar"), bIsScalar);
	Args->TryGetStringField(TEXT("vector_value"), VectorValue);

	if (!bIsScalar || !VectorValue.IsEmpty())
	{
		UMaterialParameterCollection* MPC = Cast<UMaterialParameterCollection>(UEditorAssetLibrary::LoadAsset(AssetPath));
		if (!MPC) { OutError = FString::Printf(TEXT("Could not load MPC at '%s'"), *AssetPath); return; }
		bool bFound = false;
		FLinearColor NewColor(0, 0, 0, 1);
		TArray<FString> Parts;
		VectorValue.ParseIntoArray(Parts, TEXT(","), true);
		if (Parts.Num() > 0) NewColor.R = FCString::Atof(*Parts[0]);
		if (Parts.Num() > 1) NewColor.G = FCString::Atof(*Parts[1]);
		if (Parts.Num() > 2) NewColor.B = FCString::Atof(*Parts[2]);
		if (Parts.Num() > 3) NewColor.A = FCString::Atof(*Parts[3]);
		for (FCollectionVectorParameter& Param : MPC->VectorParameters)
		{
			if (Param.ParameterName.ToString().Equals(ParamName, ESearchCase::IgnoreCase))
			{ Param.DefaultValue = NewColor; bFound = true; break; }
		}
		if (!bFound) { OutError = FString::Printf(TEXT("Vector parameter '%s' not found in MPC"), *ParamName); return; }
		MPC->MarkPackageDirty();
		UEditorAssetLibrary::SaveAsset(AssetPath, false);
		OutJsonString = FString::Printf(
			TEXT("{\"success\":true,\"mpc_path\":\"%s\",\"parameter\":\"%s\",\"value\":\"%g,%g,%g,%g\"}"),
			*AssetPath, *ParamName, NewColor.R, NewColor.G, NewColor.B, NewColor.A);
		return;
	}

	HandleSetMPCDefaultValue(AssetPath, ParamName, (float)ScalarValue, bIsScalar, OutJsonString, OutError);
}

void HandleGetMPCSummaryFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString AssetPath;
	Args->TryGetStringField(TEXT("asset_path"), AssetPath);

	UMaterialParameterCollection* MPC = Cast<UMaterialParameterCollection>(UEditorAssetLibrary::LoadAsset(AssetPath));
	if (!MPC) { OutError = FString::Printf(TEXT("Could not load MPC at '%s'"), *AssetPath); return; }

	TSharedPtr<FJsonObject> Res = MakeShareable(new FJsonObject());
	Res->SetBoolField(TEXT("success"), true);
	Res->SetStringField(TEXT("asset_path"), AssetPath);

	TArray<TSharedPtr<FJsonValue>> Scalars;
	for (const FCollectionScalarParameter& P : MPC->ScalarParameters)
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("name"), P.ParameterName.ToString());
		O->SetNumberField(TEXT("default"), P.DefaultValue);
		Scalars.Add(MakeShared<FJsonValueObject>(O));
	}
	Res->SetArrayField(TEXT("scalar_parameters"), Scalars);

	TArray<TSharedPtr<FJsonValue>> Vectors;
	for (const FCollectionVectorParameter& P : MPC->VectorParameters)
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("name"), P.ParameterName.ToString());
		O->SetStringField(TEXT("default"), FString::Printf(TEXT("%g,%g,%g,%g"),
			P.DefaultValue.R, P.DefaultValue.G, P.DefaultValue.B, P.DefaultValue.A));
		Vectors.Add(MakeShared<FJsonValueObject>(O));
	}
	Res->SetArrayField(TEXT("vector_parameters"), Vectors);

	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Res.ToSharedRef(), W);
}

void HandleRemovePostProcessBlendableFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString ActorLabel, MaterialPath;
	Args->TryGetStringField(TEXT("actor_label"), ActorLabel);
	Args->TryGetStringField(TEXT("material_path"), MaterialPath);
	HandleRemovePostProcessBlendable(ActorLabel, MaterialPath, OutJsonString, OutError);
}

void HandleSetPostProcessPriorityFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString ActorLabel;
	double Priority = 0, BlendRadius = 0, BlendWeight = 1;
	Args->TryGetStringField(TEXT("actor_label"), ActorLabel);
	Args->TryGetNumberField(TEXT("priority"), Priority);
	Args->TryGetNumberField(TEXT("blend_radius"), BlendRadius);
	Args->TryGetNumberField(TEXT("blend_weight"), BlendWeight);
	const bool bP = Args->HasField(TEXT("priority"));
	const bool bR = Args->HasField(TEXT("blend_radius"));
	const bool bW = Args->HasField(TEXT("blend_weight"));
	HandleSetPostProcessPriority(ActorLabel, (float)Priority, (float)BlendRadius, (float)BlendWeight,
		bP, bR, bW, OutJsonString, OutError);
}

}
