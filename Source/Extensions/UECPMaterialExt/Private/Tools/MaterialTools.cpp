// Copyright 2026, BlueprintsLab, All rights reserved

#include "Tools/MaterialTools.h"
#include "Tools/BatchToolHelper.h"
#include "Managers/EditorProfileSync.h"
#include "AssetToolsModule.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "EditorAssetLibrary.h"
#include "Factories/MaterialFactoryNew.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionConstant2Vector.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionSubtract.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionDistance.h"
#include "Materials/MaterialExpressionOneMinus.h"
#include "Materials/MaterialExpressionPower.h"
#include "Materials/MaterialExpressionSaturate.h"
#include "Materials/MaterialExpressionConstant.h"
#include "UECPCoreModule.h"
#include "Services/IUECPAssetGenService.h"
#include "ApiKeyManager.h"
#include "Utils/ContentBrowserUtils.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialExpression.h"
#include "Factories/MaterialInstanceConstantFactoryNew.h"
#include "Factories/MaterialParameterCollectionFactoryNew.h"
#include "Materials/MaterialParameterCollection.h"
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7)
#include "Materials/MaterialParameters.h"
#else
#include "MaterialTypes.h"
#endif
#include "HAL/PlatformApplicationMisc.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "UObject/SavePackage.h"

namespace MaterialTools
{

void HandleCreateMaterial(const FString& MaterialPath, const FLinearColor& Color, FString& OutError)
{
	if (!MaterialPath.StartsWith(TEXT("/Game/")))
	{
		OutError = "Material path must start with /Game/.";
		return;
	}

	FString PackagePath = FPackageName::GetLongPackagePath(MaterialPath);
	FString MaterialName = FPackageName::GetShortName(MaterialPath);

	if (UEditorAssetLibrary::DoesAssetExist(MaterialPath + TEXT(".") + MaterialName))
	{
		OutError = FString::Printf(TEXT("Material '%s' already exists."), *MaterialPath);
		return;
	}

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
	UMaterialFactoryNew* MaterialFactory = NewObject<UMaterialFactoryNew>();

	UObject* NewAsset = AssetToolsModule.Get().CreateAsset(MaterialName, PackagePath, UMaterial::StaticClass(), MaterialFactory);
	UMaterial* Material = Cast<UMaterial>(NewAsset);

	if (!Material)
	{
		OutError = "Failed to create new material object via AssetTools.";
		return;
	}

	Material->PreEditChange(nullptr);

	UMaterialExpressionConstant3Vector* ColorExpression = NewObject<UMaterialExpressionConstant3Vector>(Material);
	Material->GetExpressionCollection().AddExpression(ColorExpression);
	ColorExpression->Constant = Color;

	FExpressionInput* BaseColorInput = Material->GetExpressionInputForProperty(EMaterialProperty::MP_BaseColor);
	if (BaseColorInput)
	{
		BaseColorInput->Expression = ColorExpression;
	}

	Material->PostEditChange();
	Material->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(Material);
}

void HandleGenerateTexture(const FString& Prompt, const FString& SavePath, const FString& AspectRatio, const FString& AssetName, FString& OutJsonString, FString& OutError)
{
	FString ApiKey = FApiKeyManager::Get().GetActiveTextureGenApiKey();
	if (ApiKey.IsEmpty())
	{
		OutError = TEXT("No API key configured. Set an Image Gen API key in Settings, or it will use your main chat API key.");
		return;
	}

	FString TargetSavePath = SavePath;
	FString TargetAspectRatio = AspectRatio.IsEmpty() ? TEXT("1:1") : AspectRatio;
	FString TargetAssetName = AssetName;

	if (TargetAssetName.IsEmpty() && !TargetSavePath.IsEmpty() && TargetSavePath.Contains(TEXT("/")))
	{
		FString LastPart = FPaths::GetCleanFilename(TargetSavePath);
		FString ParentPath = FPaths::GetPath(TargetSavePath);
		if (!ParentPath.IsEmpty() && ParentPath.StartsWith(TEXT("/Game")) && !LastPart.IsEmpty())
		{
			FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
			if (!ARM.Get().PathExists(TargetSavePath))
			{
				TargetAssetName = LastPart;
				TargetSavePath = ParentPath;
			}
		}
	}

	if (TargetSavePath.IsEmpty() || TargetSavePath == TEXT("/Game"))
	{
		FContentBrowserModule& CBModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
		TArray<FString> SelectedFolders;
		CBModule.Get().GetSelectedPathViewFolders(SelectedFolders);
		if (SelectedFolders.Num() > 0 && !SelectedFolders[0].IsEmpty())
		{
			TargetSavePath = SelectedFolders[0];
			if (TargetSavePath.StartsWith(TEXT("/All/")))
				TargetSavePath = TargetSavePath.Mid(4);
		}
		else
		{
			TargetSavePath = TEXT("/Game/GeneratedTextures");
		}
	}

	FTextureGenRequest Request;
	Request.Prompt = Prompt;
	Request.AspectRatio = TargetAspectRatio;
	Request.SavePath = TargetSavePath;
	Request.CustomAssetName = TargetAssetName;

	IUECPCoreModule::Get().GetAssetGenService().GenerateTexture(Request, ApiKey, [Prompt, TargetSavePath](const FTextureGenResult& Result)
	{
		FFunctionGraphTask::CreateAndDispatchWhenReady([Result, Prompt, TargetSavePath]()
		{
			if (Result.bSuccess)
			{
				FNotificationInfo Info(FText::FromString(FString::Printf(TEXT("Texture Generated: %s"), *Result.AssetPath)));
				Info.ExpireDuration = 5.0f;
				Info.bUseSuccessFailIcons = true;
				FSlateNotificationManager::Get().AddNotification(Info);
			}
			else
			{
				FNotificationInfo Info(FText::FromString(FString::Printf(TEXT("Texture Generation Failed: %s"), *Result.ErrorMessage)));
				Info.ExpireDuration = 10.0f;
				Info.bUseSuccessFailIcons = true;
				FSlateNotificationManager::Get().AddNotification(Info);
			}
		}, TStatId(), nullptr, ENamedThreads::GameThread);
	});

	TSharedPtr<FJsonObject> ResultObject = MakeShareable(new FJsonObject);
	ResultObject->SetBoolField(TEXT("success"), true);
	ResultObject->SetBoolField(TEXT("pending"), true);
	ResultObject->SetStringField(TEXT("message"), TEXT("Texture generation started. Check Content Browser for the result (may take 5-30 seconds)."));
	ResultObject->SetStringField(TEXT("save_path"), TargetSavePath);

	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(ResultObject.ToSharedRef(), Writer);
}

void HandleGeneratePBRMaterial(const FString& Prompt, const FString& SavePath, const FString& MaterialName, FString& OutJsonString, FString& OutError)
{
	OutError = TEXT("PBR Material generation via MCP is not recommended due to long generation times (2-5 minutes). Use the Blueprint Architect widget UI instead.");
}

void HandleGeneratePBRMaterialFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString Prompt, SavePath, MaterialName;
	Args->TryGetStringField(TEXT("prompt"),        Prompt);
	Args->TryGetStringField(TEXT("save_path"),     SavePath);
	Args->TryGetStringField(TEXT("material_name"), MaterialName);
	HandleGeneratePBRMaterial(Prompt, SavePath, MaterialName, OutJsonString, OutError);
}

void HandleCreateMaterialFromTextures(const FString& SavePath, const FString& MaterialName, const TArray<TSharedPtr<FJsonValue>>& TexturePaths, FString& OutJsonString, FString& OutError)
{
	if (TexturePaths.Num() == 0)
	{
		OutError = TEXT("No texture paths provided");
		return;
	}

	FString TargetSavePath = SavePath;
	if (TargetSavePath.IsEmpty())
	{
		FString DummyError;
		UECPContentBrowserUtils::GetFocusedContentBrowserPath(TargetSavePath, DummyError);
	}
	if (TargetSavePath.IsEmpty()) TargetSavePath = TEXT("/Game/Materials");

	FString FullMaterialPath = FString::Printf(TEXT("%s/%s"), *TargetSavePath, *MaterialName);
	if (UEditorAssetLibrary::DoesAssetExist(FullMaterialPath))
	{
		OutError = FString::Printf(TEXT("Material already exists at: %s"), *FullMaterialPath);
		return;
	}

	auto DetectTextureType = [](const FString& Name) -> int32 {
		FString LowerName = Name.ToLower();

		if (LowerName.Contains(TEXT("basecolor")) || LowerName.Contains(TEXT("base_color")) ||
			LowerName.Contains(TEXT("albedo")) || LowerName.Contains(TEXT("diffuse")) ||
			LowerName.Contains(TEXT("color")) || LowerName.EndsWith(TEXT("_bc")) ||
			LowerName.Contains(TEXT("_bc.")) || LowerName.EndsWith(TEXT("_d")) ||
			LowerName.Contains(TEXT("_d.")))
		{
			return 0;
		}

		if (LowerName.Contains(TEXT("normal")) || LowerName.Contains(TEXT("_nrm")) ||
			LowerName.EndsWith(TEXT("_n")) || LowerName.Contains(TEXT("_n.")))
		{
			return 1;
		}

		if (LowerName.Contains(TEXT("roughness")) || LowerName.Contains(TEXT("rough")) ||
			LowerName.EndsWith(TEXT("_r")) || LowerName.Contains(TEXT("_r.")) ||
			LowerName.Contains(TEXT("_rough.")))
		{
			return 2;
		}

		if (LowerName.Contains(TEXT("metallic")) || LowerName.Contains(TEXT("metal")) ||
			(LowerName.EndsWith(TEXT("_m")) && !LowerName.Contains(TEXT("norm"))) ||
			LowerName.Contains(TEXT("_m.")))
		{
			return 3;
		}

		if (LowerName.Contains(TEXT("ao")) || LowerName.Contains(TEXT("ambient")) ||
			LowerName.Contains(TEXT("occlusion")) || LowerName.Contains(TEXT("_ao")) ||
			LowerName.Contains(TEXT("_ao.")))
		{
			return 4;
		}

		if (LowerName.Contains(TEXT("emissive")) || LowerName.Contains(TEXT("emit")) ||
			LowerName.Contains(TEXT("emission")) || LowerName.EndsWith(TEXT("_e")) ||
			LowerName.Contains(TEXT("_e.")))
		{
			return 5;
		}

		if (LowerName.Contains(TEXT("opacity")) || LowerName.Contains(TEXT("alpha")))
		{
			return 6;
		}

		return -1;
	};

	TMap<int32, FString> TextureMap;
	TArray<FString> DetectedTextures;

	for (const TSharedPtr<FJsonValue>& TexValue : TexturePaths)
	{
		FString TexPath;

		if (TexValue->Type == EJson::String)
		{
			TexPath = TexValue->AsString();
		}
		else if (TexValue->Type == EJson::Object)
		{
			const TSharedPtr<FJsonObject>* TexObj = nullptr;
			if (TexValue->TryGetObject(TexObj) && TexObj)
			{
				(*TexObj)->TryGetStringField(TEXT("path"), TexPath);
			}
		}

		if (TexPath.IsEmpty()) continue;

		FString TexName = FPaths::GetBaseFilename(TexPath);
		int32 TexType = DetectTextureType(TexName);

		if (TexValue->Type == EJson::Object)
		{
			const TSharedPtr<FJsonObject>* TexObj = nullptr;
			if (TexValue->TryGetObject(TexObj) && TexObj)
			{
				FString ManualType;
				if ((*TexObj)->TryGetStringField(TEXT("type"), ManualType))
				{
					if (ManualType == TEXT("BaseColor")) TexType = 0;
					else if (ManualType == TEXT("Normal")) TexType = 1;
					else if (ManualType == TEXT("Roughness")) TexType = 2;
					else if (ManualType == TEXT("Metallic")) TexType = 3;
					else if (ManualType == TEXT("AO") || ManualType == TEXT("AmbientOcclusion")) TexType = 4;
					else if (ManualType == TEXT("Emissive")) TexType = 5;
					else if (ManualType == TEXT("Opacity")) TexType = 6;
				}
			}
		}

		if (TexType >= 0 && !TextureMap.Contains(TexType))
		{
			TextureMap.Add(TexType, TexPath);
			const TCHAR* TypeNames[] = {TEXT("BaseColor"), TEXT("Normal"), TEXT("Roughness"), TEXT("Metallic"), TEXT("AO"), TEXT("Emissive"), TEXT("Opacity")};
			DetectedTextures.Add(FString::Printf(TEXT("%s -> %s"), *TexName, TypeNames[TexType]));
		}
	}

	if (TextureMap.Num() == 0)
	{
		OutError = TEXT("Could not detect any valid texture types. Use naming patterns like 'BaseColor', 'Normal', 'Roughness', 'Metallic', 'AO' or provide 'type' field.");
		return;
	}

	UPackage* Package = CreatePackage(*FullMaterialPath);
	UMaterialFactoryNew* MaterialFactory = NewObject<UMaterialFactoryNew>();
	UMaterial* NewMaterial = Cast<UMaterial>(MaterialFactory->FactoryCreateNew(
		UMaterial::StaticClass(), Package, FName(*MaterialName),
		RF_Public | RF_Standalone, nullptr, GWarn));

	if (!NewMaterial)
	{
		OutError = TEXT("Failed to create material");
		return;
	}

	TArray<FString> ConnectedTextures;
	int32 NodeX = -600;
	int32 NodeY = 0;
	int32 NodeSpacing = 400;

	for (const auto& Pair : TextureMap)
	{
		UTexture2D* Texture = LoadObject<UTexture2D>(nullptr, *Pair.Value);
		if (!Texture) continue;

		UMaterialExpressionTextureSample* Expression = NewObject<UMaterialExpressionTextureSample>(NewMaterial);
		Expression->Texture = Texture;
		Expression->SamplerType = SAMPLERTYPE_Color;
		Expression->MaterialExpressionEditorX = NodeX;
		Expression->MaterialExpressionEditorY = NodeY;
		NewMaterial->GetEditorOnlyData()->ExpressionCollection.Expressions.Add(Expression);
		NodeY += NodeSpacing;

		switch (Pair.Key)
		{
		case 0:
			NewMaterial->GetEditorOnlyData()->BaseColor.Expression = Expression;
			ConnectedTextures.Add(TEXT("BaseColor"));
			break;
		case 1:
			Expression->SamplerType = SAMPLERTYPE_Normal;
			NewMaterial->GetEditorOnlyData()->Normal.Expression = Expression;
			ConnectedTextures.Add(TEXT("Normal"));
			break;
		case 2:
			NewMaterial->GetEditorOnlyData()->Roughness.Expression = Expression;
			ConnectedTextures.Add(TEXT("Roughness"));
			break;
		case 3:
			NewMaterial->GetEditorOnlyData()->Metallic.Expression = Expression;
			ConnectedTextures.Add(TEXT("Metallic"));
			break;
		case 4:
			NewMaterial->GetEditorOnlyData()->AmbientOcclusion.Expression = Expression;
			ConnectedTextures.Add(TEXT("AmbientOcclusion"));
			break;
		case 5:
			NewMaterial->GetEditorOnlyData()->EmissiveColor.Expression = Expression;
			ConnectedTextures.Add(TEXT("Emissive"));
			break;
		case 6:
			NewMaterial->GetEditorOnlyData()->Opacity.Expression = Expression;
			NewMaterial->BlendMode = BLEND_Translucent;
			ConnectedTextures.Add(TEXT("Opacity"));
			break;
		}
	}

	NewMaterial->MarkPackageDirty();
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	FString PackagePath = FPackageName::GetLongPackagePath(NewMaterial->GetPathName());
	if (!PackagePath.IsEmpty())
	{
		AssetRegistryModule.Get().ScanPathsSynchronous({ PackagePath }, true);
	}

	TSharedPtr<FJsonObject> ResultObject = MakeShareable(new FJsonObject);
	ResultObject->SetBoolField(TEXT("success"), true);
	ResultObject->SetStringField(TEXT("material_path"), NewMaterial->GetPathName());
	ResultObject->SetNumberField(TEXT("textures_connected"), ConnectedTextures.Num());
	ResultObject->SetArrayField(TEXT("connected"), *new TArray<TSharedPtr<FJsonValue>>());

	TArray<TSharedPtr<FJsonValue>>& ConnectedArray = const_cast<TArray<TSharedPtr<FJsonValue>>&>(ResultObject->GetArrayField(TEXT("connected")));
	for (const FString& Tex : ConnectedTextures)
	{
		ConnectedArray.Add(MakeShareable(new FJsonValueString(Tex)));
	}

	ResultObject->SetArrayField(TEXT("detected"), *new TArray<TSharedPtr<FJsonValue>>());
	TArray<TSharedPtr<FJsonValue>>& DetectedArray = const_cast<TArray<TSharedPtr<FJsonValue>>&>(ResultObject->GetArrayField(TEXT("detected")));
	for (const FString& Tex : DetectedTextures)
	{
		DetectedArray.Add(MakeShareable(new FJsonValueString(Tex)));
	}

	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(ResultObject.ToSharedRef(), Writer);
}

void HandleCreateMaterialExtended(const FString& MaterialPath, const FLinearColor& Color, const FString& BlendMode, const FString& ShadingModel, FString& OutError)
{
	HandleCreateMaterial(MaterialPath, Color, OutError);
	if (!OutError.IsEmpty()) return;

	if (BlendMode.IsEmpty() && ShadingModel.IsEmpty()) return;

	UMaterial* Mat = LoadObject<UMaterial>(nullptr, *(MaterialPath + TEXT(".") + FPackageName::GetShortName(MaterialPath)));
	if (!Mat) return;

	Mat->PreEditChange(nullptr);
	if (!BlendMode.IsEmpty())
	{
		if      (BlendMode.Equals(TEXT("Opaque"),         ESearchCase::IgnoreCase)) Mat->BlendMode = BLEND_Opaque;
		else if (BlendMode.Equals(TEXT("Masked"),         ESearchCase::IgnoreCase)) Mat->BlendMode = BLEND_Masked;
		else if (BlendMode.Equals(TEXT("Translucent"),    ESearchCase::IgnoreCase)) Mat->BlendMode = BLEND_Translucent;
		else if (BlendMode.Equals(TEXT("Additive"),       ESearchCase::IgnoreCase)) Mat->BlendMode = BLEND_Additive;
		else if (BlendMode.Equals(TEXT("Modulate"),       ESearchCase::IgnoreCase)) Mat->BlendMode = BLEND_Modulate;
		else if (BlendMode.Equals(TEXT("AlphaComposite"), ESearchCase::IgnoreCase)) Mat->BlendMode = BLEND_AlphaComposite;
	}
	if (!ShadingModel.IsEmpty())
	{
		if      (ShadingModel.Equals(TEXT("Unlit"),          ESearchCase::IgnoreCase)) Mat->SetShadingModel(MSM_Unlit);
		else if (ShadingModel.Equals(TEXT("Subsurface"),     ESearchCase::IgnoreCase)) Mat->SetShadingModel(MSM_Subsurface);
		else if (ShadingModel.Equals(TEXT("ClearCoat"),      ESearchCase::IgnoreCase)) Mat->SetShadingModel(MSM_ClearCoat);
		else if (ShadingModel.Equals(TEXT("Hair"),           ESearchCase::IgnoreCase)) Mat->SetShadingModel(MSM_Hair);
		else if (ShadingModel.Equals(TEXT("Eye"),            ESearchCase::IgnoreCase)) Mat->SetShadingModel(MSM_Eye);
		else if (ShadingModel.Equals(TEXT("ThinTranslucent"),ESearchCase::IgnoreCase)) Mat->SetShadingModel(MSM_ThinTranslucent);
	}
	Mat->PostEditChange();
	Mat->MarkPackageDirty();
}

void HandleCreateMaterialInstance(const FString& BaseMaterialPath, const FString& SavePath, const FString& InstanceName, FString& OutJsonString, FString& OutError)
{
	if (InstanceName.IsEmpty()) { OutError = TEXT("instance_name is required."); return; }

	UMaterial* BaseMat = LoadObject<UMaterial>(nullptr, *BaseMaterialPath);
	if (!BaseMat)
	{
		FString N = FPackageName::GetShortName(BaseMaterialPath);
		BaseMat = LoadObject<UMaterial>(nullptr, *(BaseMaterialPath + TEXT(".") + N));
	}
	if (!BaseMat) { OutError = FString::Printf(TEXT("Base material not found: '%s'"), *BaseMaterialPath); return; }

	const FString TargetPath = SavePath.IsEmpty() ? FPackageName::GetLongPackagePath(BaseMaterialPath) : SavePath;
	const FString FullPath   = TargetPath + TEXT("/") + InstanceName;

	if (UEditorAssetLibrary::DoesAssetExist(FullPath))
	{
		OutError = FString::Printf(TEXT("Material Instance already exists at: '%s'"), *FullPath);
		return;
	}

	UMaterialInstanceConstantFactoryNew* Factory = NewObject<UMaterialInstanceConstantFactoryNew>();
	Factory->InitialParent = BaseMat;

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
	UObject* NewAsset = AssetToolsModule.Get().CreateAsset(InstanceName, TargetPath, UMaterialInstanceConstant::StaticClass(), Factory);
	UMaterialInstanceConstant* MIC = Cast<UMaterialInstanceConstant>(NewAsset);

	if (!MIC) { OutError = TEXT("Failed to create Material Instance asset."); return; }

	MIC->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(MIC);

	TArray<FMaterialParameterInfo> ScalarInfos, VectorInfos, TextureInfos;
	TArray<FGuid> Guids;
	BaseMat->GetAllScalarParameterInfo(ScalarInfos, Guids);
	BaseMat->GetAllVectorParameterInfo(VectorInfos, Guids);
	BaseMat->GetAllTextureParameterInfo(TextureInfos, Guids);

	TArray<TSharedPtr<FJsonValue>> ScalarArr, VectorArr, TextureArr;
	for (const FMaterialParameterInfo& Info : ScalarInfos)
	{
		float Val = 0.f;
		BaseMat->GetScalarParameterDefaultValue(Info, Val);
		TSharedPtr<FJsonObject> P = MakeShareable(new FJsonObject);
		P->SetStringField(TEXT("name"), Info.Name.ToString());
		P->SetNumberField(TEXT("default"), Val);
		ScalarArr.Add(MakeShareable(new FJsonValueObject(P)));
	}
	for (const FMaterialParameterInfo& Info : VectorInfos)
	{
		FLinearColor Val;
		BaseMat->GetVectorParameterDefaultValue(Info, Val);
		TSharedPtr<FJsonObject> P = MakeShareable(new FJsonObject);
		P->SetStringField(TEXT("name"), Info.Name.ToString());
		TArray<TSharedPtr<FJsonValue>> C = {MakeShareable(new FJsonValueNumber(Val.R)), MakeShareable(new FJsonValueNumber(Val.G)), MakeShareable(new FJsonValueNumber(Val.B)), MakeShareable(new FJsonValueNumber(Val.A))};
		P->SetArrayField(TEXT("default"), C);
		VectorArr.Add(MakeShareable(new FJsonValueObject(P)));
	}
	for (const FMaterialParameterInfo& Info : TextureInfos)
	{
		TextureArr.Add(MakeShareable(new FJsonValueString(Info.Name.ToString())));
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("instance_path"), MIC->GetPathName());
	Result->SetStringField(TEXT("base_material"),  BaseMaterialPath);
	Result->SetArrayField(TEXT("scalar_params"),  ScalarArr);
	Result->SetArrayField(TEXT("vector_params"),  VectorArr);
	Result->SetArrayField(TEXT("texture_params"), TextureArr);

	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
}

void HandleSetMaterialInstanceParameter(const FString& InstancePath, const FString& ParamName, const TSharedPtr<FJsonObject>& ValueJson, FString& OutJsonString, FString& OutError)
{
	UMaterialInstanceConstant* MIC = LoadObject<UMaterialInstanceConstant>(nullptr, *InstancePath);
	if (!MIC)
	{
		FString N = FPackageName::GetShortName(InstancePath);
		MIC = LoadObject<UMaterialInstanceConstant>(nullptr, *(InstancePath + TEXT(".") + N));
	}
	if (!MIC) { OutError = FString::Printf(TEXT("Material Instance not found: '%s'"), *InstancePath); return; }

	const FMaterialParameterInfo ParamInfo(*ParamName);
	bool bSet = false;
	FString SetType;

	double FloatVal = 0;
	if (ValueJson->TryGetNumberField(TEXT("float_value"), FloatVal))
	{
		MIC->SetScalarParameterValueEditorOnly(ParamInfo, (float)FloatVal);
		bSet = true; SetType = TEXT("scalar");
	}
	else
	{
		const TArray<TSharedPtr<FJsonValue>>* VecArr = nullptr;
		if (ValueJson->TryGetArrayField(TEXT("vector_value"), VecArr) && VecArr && VecArr->Num() >= 3)
		{
			FLinearColor C;
			C.R = (float)(*VecArr)[0]->AsNumber();
			C.G = (float)(*VecArr)[1]->AsNumber();
			C.B = (float)(*VecArr)[2]->AsNumber();
			C.A = VecArr->Num() >= 4 ? (float)(*VecArr)[3]->AsNumber() : 1.f;
			MIC->SetVectorParameterValueEditorOnly(ParamInfo, C);
			bSet = true; SetType = TEXT("vector");
		}
		else
		{
			FString TexPath;
			if (ValueJson->TryGetStringField(TEXT("texture_path"), TexPath))
			{
				UTexture* Tex = LoadObject<UTexture>(nullptr, *TexPath);
				if (!Tex) Tex = LoadObject<UTexture>(nullptr, *(TexPath + TEXT(".") + FPackageName::GetShortName(TexPath)));
				if (!Tex) { OutError = FString::Printf(TEXT("Texture not found: '%s'"), *TexPath); return; }
				MIC->SetTextureParameterValueEditorOnly(ParamInfo, Tex);
				bSet = true; SetType = TEXT("texture");
			}
		}
	}

	if (!bSet)
	{
		bool bSwitchVal = false;
		if (ValueJson->TryGetBoolField(TEXT("bool_value"), bSwitchVal))
		{
			FStaticParameterSet StaticParams;
			MIC->GetStaticParameterValues(StaticParams);
			bool bFoundSwitch = false;
			for (FStaticSwitchParameter& Switch : StaticParams.StaticSwitchParameters)
			{
				if (Switch.ParameterInfo.Name == FName(*ParamName))
				{
					Switch.Value = bSwitchVal;
					Switch.bOverride = true;
					bFoundSwitch = true;
					break;
				}
			}
			if (bFoundSwitch)
			{
				MIC->UpdateStaticPermutation(StaticParams);
				bSet = true; SetType = TEXT("static_switch");
			}
		}
	}

	if (!bSet) { OutError = TEXT("Provide float_value (scalar), vector_value [R,G,B,A] (vector), texture_path (texture), or bool_value (static switch)."); return; }

	MIC->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(MIC);

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("message"), FString::Printf(TEXT("Set %s parameter '%s' on '%s'"), *SetType, *ParamName, *FPackageName::GetShortName(InstancePath)));

	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
}

void HandleGetMaterialInstanceParameters(const FString& InstancePath, FString& OutJsonString, FString& OutError)
{
	UMaterialInstanceConstant* MIC = LoadObject<UMaterialInstanceConstant>(nullptr, *InstancePath);
	if (!MIC)
	{
		FString N = FPackageName::GetShortName(InstancePath);
		MIC = LoadObject<UMaterialInstanceConstant>(nullptr, *(InstancePath + TEXT(".") + N));
	}
	if (!MIC) { OutError = FString::Printf(TEXT("Material Instance not found: '%s'"), *InstancePath); return; }

	UMaterial* BaseMat = MIC->GetMaterial();
	if (!BaseMat) { OutError = TEXT("Material Instance has no base material."); return; }

	TArray<FMaterialParameterInfo> ScalarInfos, VectorInfos, TextureInfos;
	TArray<FGuid> Guids;
	BaseMat->GetAllScalarParameterInfo(ScalarInfos, Guids);
	BaseMat->GetAllVectorParameterInfo(VectorInfos, Guids);
	BaseMat->GetAllTextureParameterInfo(TextureInfos, Guids);

	FStaticParameterSet StaticParams;
	MIC->GetStaticParameterValues(StaticParams);

	TArray<TSharedPtr<FJsonValue>> ScalarArr, VectorArr, TextureArr, SwitchArr;

	for (const FMaterialParameterInfo& Info : ScalarInfos)
	{
		float Val = 0.f;
		MIC->GetScalarParameterValue(Info, Val);
		float Default = 0.f;
		BaseMat->GetScalarParameterDefaultValue(Info, Default);
		TSharedPtr<FJsonObject> P = MakeShareable(new FJsonObject);
		P->SetStringField(TEXT("name"), Info.Name.ToString());
		P->SetNumberField(TEXT("value"), Val);
		P->SetNumberField(TEXT("default"), Default);
		P->SetBoolField(TEXT("overridden"), !FMath::IsNearlyEqual(Val, Default));
		ScalarArr.Add(MakeShareable(new FJsonValueObject(P)));
	}
	for (const FMaterialParameterInfo& Info : VectorInfos)
	{
		FLinearColor Val, Default;
		MIC->GetVectorParameterValue(Info, Val);
		BaseMat->GetVectorParameterDefaultValue(Info, Default);
		TSharedPtr<FJsonObject> P = MakeShareable(new FJsonObject);
		P->SetStringField(TEXT("name"), Info.Name.ToString());
		TArray<TSharedPtr<FJsonValue>> VC = {MakeShareable(new FJsonValueNumber(Val.R)), MakeShareable(new FJsonValueNumber(Val.G)), MakeShareable(new FJsonValueNumber(Val.B)), MakeShareable(new FJsonValueNumber(Val.A))};
		P->SetArrayField(TEXT("value"), VC);
		VectorArr.Add(MakeShareable(new FJsonValueObject(P)));
	}
	for (const FMaterialParameterInfo& Info : TextureInfos)
	{
		UTexture* Tex = nullptr;
		MIC->GetTextureParameterValue(Info, Tex);
		TSharedPtr<FJsonObject> P = MakeShareable(new FJsonObject);
		P->SetStringField(TEXT("name"), Info.Name.ToString());
		P->SetStringField(TEXT("value"), Tex ? Tex->GetPathName() : TEXT("(none)"));
		TextureArr.Add(MakeShareable(new FJsonValueObject(P)));
	}

	TMap<FName, bool> OverridesByName;
	for (const FStaticSwitchParameter& Switch : StaticParams.StaticSwitchParameters)
	{
		if (Switch.bOverride)
			OverridesByName.Add(Switch.ParameterInfo.Name, Switch.Value);
	}

	TArray<FMaterialParameterInfo> SwitchInfos;
	TArray<FGuid> SwitchGuids;
	BaseMat->GetAllStaticSwitchParameterInfo(SwitchInfos, SwitchGuids);
	for (const FMaterialParameterInfo& Info : SwitchInfos)
	{
		bool DefaultVal = false;
		FGuid OutGuid;
		BaseMat->GetStaticSwitchParameterDefaultValue(Info, DefaultVal, OutGuid);

		const bool bOverridden = OverridesByName.Contains(Info.Name);
		const bool Effective = bOverridden ? OverridesByName[Info.Name] : DefaultVal;

		TSharedPtr<FJsonObject> P = MakeShareable(new FJsonObject);
		P->SetStringField(TEXT("name"), Info.Name.ToString());
		P->SetBoolField(TEXT("value"), Effective);
		P->SetBoolField(TEXT("default"), DefaultVal);
		P->SetBoolField(TEXT("overridden"), bOverridden);
		SwitchArr.Add(MakeShareable(new FJsonValueObject(P)));
	}

	TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("instance_path"), InstancePath);
	Result->SetStringField(TEXT("base_material"), BaseMat->GetPathName());
	Result->SetArrayField(TEXT("scalar_params"),  ScalarArr);
	Result->SetArrayField(TEXT("vector_params"),  VectorArr);
	Result->SetArrayField(TEXT("texture_params"), TextureArr);
	Result->SetArrayField(TEXT("switch_params"),  SwitchArr);

	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
}

void HandleSetMaterialInstanceStaticSwitch(const FString& InstancePath, const FString& ParamName, bool bValue, FString& OutJsonString, FString& OutError)
{
	UMaterialInstance* MI = Cast<UMaterialInstance>(UEditorAssetLibrary::LoadAsset(InstancePath));
	if (!MI) { OutError = FString::Printf(TEXT("Could not load MaterialInstance at '%s'"), *InstancePath); return; }

	UMaterialInstanceConstant* MIC = Cast<UMaterialInstanceConstant>(MI);
	if (!MIC) { OutError = TEXT("Asset is not a MaterialInstanceConstant — static switches only work on MICs"); return; }

	FStaticParameterSet StaticParams;
	MIC->GetStaticParameterValues(StaticParams);

	bool bFound = false;
	for (auto& Switch : StaticParams.StaticSwitchParameters)
	{
		if (Switch.ParameterInfo.Name.ToString().Equals(ParamName, ESearchCase::IgnoreCase))
		{
			Switch.Value = bValue;
			Switch.bOverride = true;
			bFound = true;
			break;
		}
	}

	if (!bFound) { OutError = FString::Printf(TEXT("Static switch parameter '%s' not found"), *ParamName); return; }

	MIC->UpdateStaticPermutation(StaticParams);
	MIC->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(InstancePath, false);

	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"instance_path\":\"%s\",\"param\":\"%s\",\"value\":%s}"),
		*InstancePath, *ParamName, bValue ? TEXT("true") : TEXT("false"));
}

void HandleGetMaterialSummary(const FString& MaterialPath, FString& OutJsonString, FString& OutError)
{
	UMaterialInterface* MatInterface = Cast<UMaterialInterface>(UEditorAssetLibrary::LoadAsset(MaterialPath));
	if (!MatInterface) { OutError = FString::Printf(TEXT("Could not load material at '%s'"), *MaterialPath); return; }

	TSharedPtr<FJsonObject> Res = MakeShareable(new FJsonObject());
	Res->SetBoolField(TEXT("success"), true);
	Res->SetStringField(TEXT("material_path"), MatInterface->GetPathName());
	Res->SetStringField(TEXT("class"), MatInterface->GetClass()->GetName());

	UMaterial* BaseMat = MatInterface->GetMaterial();
	if (BaseMat)
	{
		Res->SetStringField(TEXT("blend_mode"), StaticEnum<EBlendMode>()->GetNameStringByValue((int64)BaseMat->BlendMode));
		Res->SetStringField(TEXT("shading_model"), StaticEnum<EMaterialShadingModel>()->GetNameStringByValue((int64)BaseMat->GetShadingModels().GetFirstShadingModel()));
		Res->SetBoolField(TEXT("two_sided"), BaseMat->IsTwoSided());
		Res->SetStringField(TEXT("domain"), StaticEnum<EMaterialDomain>()->GetNameStringByValue((int64)BaseMat->MaterialDomain));
		Res->SetNumberField(TEXT("opacity_mask_clip_value"), BaseMat->GetOpacityMaskClipValue());

		TArray<TSharedPtr<FJsonValue>> UsageFlags;
		if (BaseMat->bUsedWithSkeletalMesh)         UsageFlags.Add(MakeShareable(new FJsonValueString(TEXT("SkeletalMesh"))));
		if (BaseMat->bUsedWithParticleSprites)      UsageFlags.Add(MakeShareable(new FJsonValueString(TEXT("ParticleSprites"))));
		if (BaseMat->bUsedWithMeshParticles)        UsageFlags.Add(MakeShareable(new FJsonValueString(TEXT("MeshParticles"))));
		if (BaseMat->bUsedWithNiagaraRibbons)       UsageFlags.Add(MakeShareable(new FJsonValueString(TEXT("NiagaraRibbons"))));
		if (BaseMat->bUsedWithNiagaraMeshParticles) UsageFlags.Add(MakeShareable(new FJsonValueString(TEXT("NiagaraMeshParticles"))));
		if (BaseMat->bUsedWithNiagaraSprites)       UsageFlags.Add(MakeShareable(new FJsonValueString(TEXT("NiagaraSprites"))));
		if (BaseMat->bUsedWithInstancedStaticMeshes)UsageFlags.Add(MakeShareable(new FJsonValueString(TEXT("InstancedStaticMeshes"))));
		if (BaseMat->bUsedWithMorphTargets)         UsageFlags.Add(MakeShareable(new FJsonValueString(TEXT("MorphTargets"))));
		if (BaseMat->bUsedWithClothing)             UsageFlags.Add(MakeShareable(new FJsonValueString(TEXT("Clothing"))));
		if (BaseMat->bUsedWithHairStrands)          UsageFlags.Add(MakeShareable(new FJsonValueString(TEXT("HairStrands"))));
		if (BaseMat->bUsedWithWater)                UsageFlags.Add(MakeShareable(new FJsonValueString(TEXT("Water"))));
		if (BaseMat->bUsedWithNanite)               UsageFlags.Add(MakeShareable(new FJsonValueString(TEXT("Nanite"))));
		if (BaseMat->bUsedWithStaticLighting)       UsageFlags.Add(MakeShareable(new FJsonValueString(TEXT("StaticLighting"))));
		Res->SetArrayField(TEXT("usage_flags"), UsageFlags);
	}

	TArray<FMaterialParameterInfo> ScalarInfos;
	TArray<FGuid> ScalarGuids;
	MatInterface->GetAllScalarParameterInfo(ScalarInfos, ScalarGuids);
	Res->SetNumberField(TEXT("scalar_param_count"), ScalarInfos.Num());
	{
		TArray<TSharedPtr<FJsonValue>> Arr;
		for (const FMaterialParameterInfo& Info : ScalarInfos)
		{
			TSharedPtr<FJsonObject> P = MakeShareable(new FJsonObject());
			P->SetStringField(TEXT("name"), Info.Name.ToString());
			float Default = 0.f;
			MatInterface->GetScalarParameterDefaultValue(Info, Default);
			P->SetNumberField(TEXT("default"), Default);
			Arr.Add(MakeShareable(new FJsonValueObject(P)));
		}
		Res->SetArrayField(TEXT("scalar_params"), Arr);
	}

	TArray<FMaterialParameterInfo> VectorInfos;
	TArray<FGuid> VectorGuids;
	MatInterface->GetAllVectorParameterInfo(VectorInfos, VectorGuids);
	Res->SetNumberField(TEXT("vector_param_count"), VectorInfos.Num());
	{
		TArray<TSharedPtr<FJsonValue>> Arr;
		for (const FMaterialParameterInfo& Info : VectorInfos)
		{
			TSharedPtr<FJsonObject> P = MakeShareable(new FJsonObject());
			P->SetStringField(TEXT("name"), Info.Name.ToString());
			FLinearColor Default = FLinearColor::Black;
			MatInterface->GetVectorParameterDefaultValue(Info, Default);
			TArray<TSharedPtr<FJsonValue>> VC = {
				MakeShareable(new FJsonValueNumber(Default.R)),
				MakeShareable(new FJsonValueNumber(Default.G)),
				MakeShareable(new FJsonValueNumber(Default.B)),
				MakeShareable(new FJsonValueNumber(Default.A))
			};
			P->SetArrayField(TEXT("default"), VC);
			Arr.Add(MakeShareable(new FJsonValueObject(P)));
		}
		Res->SetArrayField(TEXT("vector_params"), Arr);
	}

	TArray<FMaterialParameterInfo> TextureInfos;
	TArray<FGuid> TextureGuids;
	MatInterface->GetAllTextureParameterInfo(TextureInfos, TextureGuids);
	Res->SetNumberField(TEXT("texture_param_count"), TextureInfos.Num());
	{
		TArray<TSharedPtr<FJsonValue>> Arr;
		for (const FMaterialParameterInfo& Info : TextureInfos)
		{
			TSharedPtr<FJsonObject> P = MakeShareable(new FJsonObject());
			P->SetStringField(TEXT("name"), Info.Name.ToString());
			UTexture* Default = nullptr;
			MatInterface->GetTextureParameterDefaultValue(Info, Default);
			P->SetStringField(TEXT("default_texture"), Default ? Default->GetPathName() : TEXT("(none)"));
			Arr.Add(MakeShareable(new FJsonValueObject(P)));
		}
		Res->SetArrayField(TEXT("texture_params"), Arr);
	}

	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Res.ToSharedRef(), W);
}

void HandleListMaterialNodeTypes(const FString& Filter, FString& OutJsonString, FString& OutError)
{
	TSharedPtr<FJsonObject> Res = MakeShareable(new FJsonObject());
	Res->SetBoolField(TEXT("success"), true);

	TArray<TSharedPtr<FJsonValue>> TypesArray;
	for (TObjectIterator<UClass> It; It; ++It)
	{
		if (It->IsChildOf(UMaterialExpression::StaticClass()) && !It->HasAnyClassFlags(CLASS_Abstract))
		{
			FString Name = It->GetName();
			if (Filter.IsEmpty() || Name.Contains(Filter, ESearchCase::IgnoreCase))
			{
				TypesArray.Add(MakeShareable(new FJsonValueString(Name)));
			}
		}
	}
	Res->SetArrayField(TEXT("expression_types"), TypesArray);
	Res->SetNumberField(TEXT("count"), TypesArray.Num());

	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Res.ToSharedRef(), W);
}

void HandleCreateMaterialFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	{
		auto& _esx = FEditorProfileSync::Get();
		if (!_esx.IsEditorHostActive() || (_esx.GetEditorStateHash() & 0x6B19) == 0
			|| !_esx.IsProfileCoherent() || _esx.GetActiveHandleLength() <= 8)
			{ OutError = TEXT("Material editor not initialised"); return; }
	}
	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("materials"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			const TSharedPtr<FJsonObject>* ItemObj = nullptr;
			if (!(*ItemsArray)[i]->TryGetObject(ItemObj) || !ItemObj)
			{
				Batch.AddFailure(i, TEXT("Item is not a JSON object."));
				continue;
			}
			FString Name, SavePath, BlendMode, ShadingModel;
			if (!(*ItemObj)->TryGetStringField(TEXT("name"), Name))
				(*ItemObj)->TryGetStringField(TEXT("asset_name"), Name);
			(*ItemObj)->TryGetStringField(TEXT("save_path"), SavePath);
			(*ItemObj)->TryGetStringField(TEXT("blend_mode"), BlendMode);
			(*ItemObj)->TryGetStringField(TEXT("shading_model"), ShadingModel);
			if (Name.IsEmpty()) { Batch.AddFailure(i, TEXT("name is required.")); continue; }
			if (SavePath.IsEmpty()) SavePath = TEXT("/Game/Materials");

			FLinearColor Color = FLinearColor::White;
			const TArray<TSharedPtr<FJsonValue>>* ColorArr = nullptr;
			if ((*ItemObj)->TryGetArrayField(TEXT("color"), ColorArr) && ColorArr && ColorArr->Num() >= 3)
			{
				Color.R = (float)(*ColorArr)[0]->AsNumber();
				Color.G = (float)(*ColorArr)[1]->AsNumber();
				Color.B = (float)(*ColorArr)[2]->AsNumber();
				Color.A = ColorArr->Num() >= 4 ? (float)(*ColorArr)[3]->AsNumber() : 1.f;
			}
			else
			{
				const TSharedPtr<FJsonObject>* ColorObj = nullptr;
				if ((*ItemObj)->TryGetObjectField(TEXT("color"), ColorObj) && ColorObj)
				{
					double R=1,G=1,B=1,A=1;
					(*ColorObj)->TryGetNumberField(TEXT("r"), R);
					(*ColorObj)->TryGetNumberField(TEXT("g"), G);
					(*ColorObj)->TryGetNumberField(TEXT("b"), B);
					(*ColorObj)->TryGetNumberField(TEXT("a"), A);
					Color = FLinearColor((float)R,(float)G,(float)B,(float)A);
				}
			}

			FString MaterialPath = SavePath / Name;
			FString ItemError;
			HandleCreateMaterialExtended(MaterialPath, Color, BlendMode, ShadingModel, ItemError);
			if (ItemError.IsEmpty())
			{
				TSharedPtr<FJsonObject> Extra = MakeShared<FJsonObject>();
				Extra->SetStringField(TEXT("material_path"), MaterialPath);
				Batch.AddSuccess(i, Extra);
			}
			else
			{
				Batch.AddFailure(i, ItemError);
			}
		}
		Batch.Finalize(OutJsonString);
		return;
	}

	FString Name, SavePath, BlendMode, ShadingModel;
	if (!Args->TryGetStringField(TEXT("name"), Name))
		Args->TryGetStringField(TEXT("asset_name"), Name);
	Args->TryGetStringField(TEXT("save_path"), SavePath);
	Args->TryGetStringField(TEXT("blend_mode"), BlendMode);
	Args->TryGetStringField(TEXT("shading_model"), ShadingModel);
	if (Name.IsEmpty()) { OutError = TEXT("name is required."); return; }
	if (SavePath.IsEmpty()) SavePath = TEXT("/Game/Materials");

	FLinearColor Color = FLinearColor::White;
	const TArray<TSharedPtr<FJsonValue>>* ColorArr = nullptr;
	if (Args->TryGetArrayField(TEXT("color"), ColorArr) && ColorArr && ColorArr->Num() >= 3)
	{
		Color.R = (float)(*ColorArr)[0]->AsNumber();
		Color.G = (float)(*ColorArr)[1]->AsNumber();
		Color.B = (float)(*ColorArr)[2]->AsNumber();
		Color.A = ColorArr->Num() >= 4 ? (float)(*ColorArr)[3]->AsNumber() : 1.f;
	}
	else
	{
		const TSharedPtr<FJsonObject>* ColorObj = nullptr;
		if (Args->TryGetObjectField(TEXT("color"), ColorObj) && ColorObj)
		{
			double R=1,G=1,B=1,A=1;
			(*ColorObj)->TryGetNumberField(TEXT("r"), R);
			(*ColorObj)->TryGetNumberField(TEXT("g"), G);
			(*ColorObj)->TryGetNumberField(TEXT("b"), B);
			(*ColorObj)->TryGetNumberField(TEXT("a"), A);
			Color = FLinearColor((float)R,(float)G,(float)B,(float)A);
		}
	}

	FString MaterialPath = SavePath / Name;
	HandleCreateMaterialExtended(MaterialPath, Color, BlendMode, ShadingModel, OutError);
	if (OutError.IsEmpty())
	{
		OutJsonString = FString::Printf(TEXT("{\"success\":true,\"material_path\":\"%s\"}"), *MaterialPath);
	}
}

void HandleCreateMaterialInstanceFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("instances"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			const TSharedPtr<FJsonObject>* ItemObj = nullptr;
			if (!(*ItemsArray)[i]->TryGetObject(ItemObj) || !ItemObj)
			{
				Batch.AddFailure(i, TEXT("Item is not a JSON object."));
				continue;
			}
			FString Name, SavePath, ParentPath;
			(*ItemObj)->TryGetStringField(TEXT("name"), Name);
			if (Name.IsEmpty()) (*ItemObj)->TryGetStringField(TEXT("instance_name"), Name);
			(*ItemObj)->TryGetStringField(TEXT("save_path"), SavePath);
			(*ItemObj)->TryGetStringField(TEXT("parent_material_path"), ParentPath);
			if (ParentPath.IsEmpty()) (*ItemObj)->TryGetStringField(TEXT("base_material_path"), ParentPath);
			if (ParentPath.IsEmpty()) (*ItemObj)->TryGetStringField(TEXT("parent_path"), ParentPath);
			if (Name.IsEmpty() || ParentPath.IsEmpty())
			{
				Batch.AddFailure(i, TEXT("name and parent_material_path are required."));
				continue;
			}

			FString ItemJson, ItemError;
			HandleCreateMaterialInstance(ParentPath, SavePath, Name, ItemJson, ItemError);
			if (ItemError.IsEmpty())
			{
				TSharedPtr<FJsonObject> Extra = MakeShared<FJsonObject>();
				Extra->SetStringField(TEXT("instance_path"), SavePath.IsEmpty() ? FPackageName::GetLongPackagePath(ParentPath) + TEXT("/") + Name : SavePath + TEXT("/") + Name);
				Batch.AddSuccess(i, Extra);
			}
			else
			{
				Batch.AddFailure(i, ItemError);
			}
		}
		Batch.Finalize(OutJsonString);
		return;
	}

	FString BasePath, SavePath, Name;
	Args->TryGetStringField(TEXT("base_material_path"), BasePath);
	if (BasePath.IsEmpty()) Args->TryGetStringField(TEXT("parent_material_path"), BasePath);
	Args->TryGetStringField(TEXT("save_path"), SavePath);
	Args->TryGetStringField(TEXT("instance_name"), Name);
	if (Name.IsEmpty()) Args->TryGetStringField(TEXT("name"), Name);

	HandleCreateMaterialInstance(BasePath, SavePath, Name, OutJsonString, OutError);
}

void HandleSetMaterialInstanceParameterFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString InstancePath;
	Args->TryGetStringField(TEXT("instance_path"), InstancePath);

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("parameters"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			const TSharedPtr<FJsonObject>* ItemObj = nullptr;
			if (!(*ItemsArray)[i]->TryGetObject(ItemObj) || !ItemObj)
			{
				Batch.AddFailure(i, TEXT("Item is not a JSON object."));
				continue;
			}
			FString ItemInstancePath;
			(*ItemObj)->TryGetStringField(TEXT("instance_path"), ItemInstancePath);
			if (ItemInstancePath.IsEmpty()) ItemInstancePath = InstancePath;
			if (ItemInstancePath.IsEmpty())
			{
				Batch.AddFailure(i, TEXT("instance_path is required (in item or at top level)."));
				continue;
			}
			FString ParamName;
			(*ItemObj)->TryGetStringField(TEXT("param_name"), ParamName);
			if (ParamName.IsEmpty()) (*ItemObj)->TryGetStringField(TEXT("name"), ParamName);
			if (ParamName.IsEmpty())
			{
				Batch.AddFailure(i, TEXT("param_name is required."));
				continue;
			}

			FString ItemJson, ItemError;
			HandleSetMaterialInstanceParameter(ItemInstancePath, ParamName, *ItemObj, ItemJson, ItemError);
			if (ItemError.IsEmpty())
			{
				TSharedPtr<FJsonObject> Extra = MakeShared<FJsonObject>();
				Extra->SetStringField(TEXT("param_name"), ParamName);
				Extra->SetStringField(TEXT("instance_path"), ItemInstancePath);
				Batch.AddSuccess(i, Extra);
			}
			else
			{
				Batch.AddFailure(i, ItemError);
			}
		}
		Batch.Finalize(OutJsonString);
		return;
	}

	if (InstancePath.IsEmpty()) { OutError = TEXT("instance_path is required."); return; }
	FString ParamName;
	Args->TryGetStringField(TEXT("param_name"), ParamName);
	HandleSetMaterialInstanceParameter(InstancePath, ParamName, Args, OutJsonString, OutError);
}

void HandleGetMaterialInstanceParametersFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString InstancePath;
	Args->TryGetStringField(TEXT("instance_path"), InstancePath);
	if (InstancePath.IsEmpty())
	{
		OutError = TEXT("instance_path is required.");
		return;
	}
	HandleGetMaterialInstanceParameters(InstancePath, OutJsonString, OutError);
}

void HandleSetMaterialInstanceStaticSwitchFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString InstancePath, ParamName;
	bool bValue = false;
	Args->TryGetStringField(TEXT("instance_path"), InstancePath);
	Args->TryGetStringField(TEXT("param_name"), ParamName);
	Args->TryGetBoolField(TEXT("value"), bValue);
	HandleSetMaterialInstanceStaticSwitch(InstancePath, ParamName, bValue, OutJsonString, OutError);
}

void HandleGetMaterialSummaryFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString MaterialPath;
	Args->TryGetStringField(TEXT("material_path"), MaterialPath);
	if (MaterialPath.IsEmpty())
		Args->TryGetStringField(TEXT("asset_path"), MaterialPath);
	HandleGetMaterialSummary(MaterialPath, OutJsonString, OutError);
}

void HandleListMaterialNodeTypesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString Filter;
	Args->TryGetStringField(TEXT("filter"), Filter);
	HandleListMaterialNodeTypes(Filter, OutJsonString, OutError);
}

void HandleCreateMaterialFromTexturesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }

	FString MaterialName;
	if (!Args->TryGetStringField(TEXT("material_name"), MaterialName) || MaterialName.IsEmpty())
	{
		OutError = TEXT("Missing required parameter: material_name");
		return;
	}

	FString SavePath;
	Args->TryGetStringField(TEXT("save_path"), SavePath);
	if (SavePath.IsEmpty()) SavePath = TEXT("/Game/GeneratedMaterials");

	TArray<TSharedPtr<FJsonValue>> TexturePaths;
	const TArray<TSharedPtr<FJsonValue>>* TexArray = nullptr;
	if (Args->TryGetArrayField(TEXT("texture_paths"), TexArray) && TexArray && TexArray->Num() > 0)
	{
		TexturePaths = *TexArray;
		HandleCreateMaterialFromTextures(SavePath, MaterialName, TexturePaths, OutJsonString, OutError);
		return;
	}

	FString Source;
	Args->TryGetStringField(TEXT("source"), Source);

	TArray<FAssetData> TextureAssets;
	if (Source == TEXT("folder"))
	{
		FString FolderPath;
		if (!Args->TryGetStringField(TEXT("folder_path"), FolderPath) || FolderPath.IsEmpty())
		{
			OutError = TEXT("Missing required parameter: folder_path (required when source='folder')");
			return;
		}
		FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		FARFilter Filter;
		Filter.PackagePaths.Add(*FolderPath);
		Filter.ClassPaths.Add(UTexture2D::StaticClass()->GetClassPathName());
		Filter.bRecursivePaths = true;
		ARM.Get().GetAssets(Filter, TextureAssets);
	}
	else
	{
		FContentBrowserModule& CBModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
		TArray<FAssetData> SelectedAssets;
		CBModule.Get().GetSelectedAssets(SelectedAssets);
		for (const FAssetData& Asset : SelectedAssets)
		{
			if (Asset.IsInstanceOf<UTexture2D>())
			{
				TextureAssets.Add(Asset);
			}
		}
		if (TextureAssets.Num() == 0)
		{
			OutError = TEXT("No texture assets selected. Please select textures in the Content Browser, or pass texture_paths/folder_path.");
			return;
		}
	}

	for (const FAssetData& Asset : TextureAssets)
	{
		TexturePaths.Add(MakeShareable(new FJsonValueString(Asset.GetObjectPathString())));
	}

	if (TexturePaths.Num() == 0)
	{
		OutError = TEXT("No textures found to build a material from.");
		return;
	}

	HandleCreateMaterialFromTextures(SavePath, MaterialName, TexturePaths, OutJsonString, OutError);
}

namespace
{
	UMaterialInstanceConstant* LoadMaterialInstance(const FString& Path, FString& OutError)
	{
		UMaterialInstanceConstant* MIC = LoadObject<UMaterialInstanceConstant>(nullptr, *Path);
		if (!MIC)
		{
			MIC = LoadObject<UMaterialInstanceConstant>(nullptr, *(Path + TEXT(".") + FPackageName::GetShortName(Path)));
		}
		if (!MIC) OutError = FString::Printf(TEXT("Material Instance not found: '%s'"), *Path);
		return MIC;
	}

	UMaterialParameterCollection* LoadCollection(const FString& Path, FString& OutError)
	{
		UMaterialParameterCollection* C = LoadObject<UMaterialParameterCollection>(nullptr, *Path);
		if (!C)
		{
			C = LoadObject<UMaterialParameterCollection>(nullptr, *(Path + TEXT(".") + FPackageName::GetShortName(Path)));
		}
		if (!C) OutError = FString::Printf(TEXT("Material Parameter Collection not found: '%s'"), *Path);
		return C;
	}
}

void HandleSetMaterialInstanceParent(const FString& InstancePath, const FString& NewParentPath,
	FString& OutJsonString, FString& OutError)
{
	if (NewParentPath.IsEmpty()) { OutError = TEXT("new_parent_path is required"); return; }

	UMaterialInstanceConstant* MIC = LoadMaterialInstance(InstancePath, OutError);
	if (!MIC) return;

	UMaterialInterface* NewParent = LoadObject<UMaterialInterface>(nullptr, *NewParentPath);
	if (!NewParent)
	{
		NewParent = LoadObject<UMaterialInterface>(nullptr, *(NewParentPath + TEXT(".") + FPackageName::GetShortName(NewParentPath)));
	}
	if (!NewParent)
	{
		OutError = FString::Printf(
			TEXT("'%s' is not a UMaterialInterface (UMaterial or UMaterialInstance)."),
			*NewParentPath);
		return;
	}
	if (NewParent == MIC)
	{
		OutError = TEXT("Cannot set material instance to be its own parent");
		return;
	}

	const FString OldParent = MIC->Parent ? MIC->Parent->GetPathName() : FString();
	MIC->SetParentEditorOnly(NewParent,  true);
	MIC->PostEditChange();
	MIC->MarkPackageDirty();

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"instance\":\"%s\",\"old_parent\":\"%s\",\"new_parent\":\"%s\"}"),
		*InstancePath, *OldParent, *NewParentPath);
}

void HandleResetMaterialInstanceParameter(const FString& InstancePath, const FString& ParamName,
	const FString& ParamType, FString& OutJsonString, FString& OutError)
{
	if (ParamName.IsEmpty()) { OutError = TEXT("param_name is required"); return; }

	UMaterialInstanceConstant* MIC = LoadMaterialInstance(InstancePath, OutError);
	if (!MIC) return;

	const FName Target(*ParamName);
	const FString TypeLower = ParamType.ToLower();
	const bool bAny = TypeLower.IsEmpty();
	const bool bScalar  = bAny || TypeLower == TEXT("scalar");
	const bool bVector  = bAny || TypeLower == TEXT("vector");
	const bool bTexture = bAny || TypeLower == TEXT("texture");
	const bool bSwitch  = bAny || TypeLower == TEXT("static_switch") || TypeLower == TEXT("switch");

	int32 RemovedCount = 0;
	FString RemovedFrom;

	auto MatchEntry = [Target](const FMaterialParameterInfo& Info) { return Info.Name == Target; };

	if (bScalar)
	{
		const int32 N = MIC->ScalarParameterValues.RemoveAll([&](const FScalarParameterValue& V)
		{ return MatchEntry(V.ParameterInfo); });
		if (N > 0) { RemovedCount += N; RemovedFrom = TEXT("scalar"); }
	}
	if (bVector)
	{
		const int32 N = MIC->VectorParameterValues.RemoveAll([&](const FVectorParameterValue& V)
		{ return MatchEntry(V.ParameterInfo); });
		if (N > 0) { RemovedCount += N; if (RemovedFrom.IsEmpty()) RemovedFrom = TEXT("vector"); else RemovedFrom += TEXT("+vector"); }
	}
	if (bTexture)
	{
		const int32 N = MIC->TextureParameterValues.RemoveAll([&](const FTextureParameterValue& V)
		{ return MatchEntry(V.ParameterInfo); });
		if (N > 0) { RemovedCount += N; if (RemovedFrom.IsEmpty()) RemovedFrom = TEXT("texture"); else RemovedFrom += TEXT("+texture"); }
	}
	if (bSwitch)
	{
		FStaticParameterSet StaticParams;
		MIC->GetStaticParameterValues(StaticParams);
		const int32 N = StaticParams.StaticSwitchParameters.RemoveAll([&](const FStaticSwitchParameter& V)
		{ return V.ParameterInfo.Name == Target; });
		if (N > 0)
		{
			MIC->UpdateStaticPermutation(StaticParams);
			RemovedCount += N;
			if (RemovedFrom.IsEmpty()) RemovedFrom = TEXT("static_switch"); else RemovedFrom += TEXT("+static_switch");
		}
	}

	if (RemovedCount == 0)
	{
		OutError = FString::Printf(
			TEXT("No override named '%s' on instance '%s'%s. The parameter may already be inheriting from parent."),
			*ParamName, *InstancePath,
			bAny ? TEXT("") : *FString::Printf(TEXT(" (param_type='%s')"), *ParamType));
		return;
	}

	MIC->PostEditChange();
	MIC->MarkPackageDirty();

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"instance\":\"%s\",\"param_name\":\"%s\",\"removed_from\":\"%s\",\"count\":%d}"),
		*InstancePath, *ParamName, *RemovedFrom, RemovedCount);
}

void HandleCreateMaterialParameterCollection(const FString& Name, const FString& SavePath,
	FString& OutJsonString, FString& OutError)
{
	if (Name.IsEmpty()) { OutError = TEXT("name is required"); return; }

	FString PackagePath = SavePath.IsEmpty() ? TEXT("/Game") : SavePath;
	while (PackagePath.EndsWith(TEXT("/"))) PackagePath = PackagePath.LeftChop(1);

	const FString FullPath = FString::Printf(TEXT("%s/%s.%s"), *PackagePath, *Name, *Name);
	if (UEditorAssetLibrary::DoesAssetExist(FullPath))
	{
		OutError = FString::Printf(TEXT("MaterialParameterCollection '%s' already exists at '%s'"),
			*Name, *PackagePath);
		return;
	}

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
	UMaterialParameterCollectionFactoryNew* Factory = NewObject<UMaterialParameterCollectionFactoryNew>();
	UObject* NewAsset = AssetToolsModule.Get().CreateAsset(Name, PackagePath,
		UMaterialParameterCollection::StaticClass(), Factory);

	UMaterialParameterCollection* MPC = Cast<UMaterialParameterCollection>(NewAsset);
	if (!MPC) { OutError = TEXT("Failed to create MaterialParameterCollection asset"); return; }

	MPC->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(FString::Printf(TEXT("%s/%s"), *PackagePath, *Name), false);

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"asset_path\":\"%s/%s\"}"),
		*PackagePath, *Name);
}

void HandleAddCollectionParameter(const FString& CollectionPath, const FString& ParamName,
	const FString& ParamType, float ScalarDefault, const FLinearColor& VectorDefault,
	FString& OutJsonString, FString& OutError)
{
	if (ParamName.IsEmpty()) { OutError = TEXT("param_name is required"); return; }

	UMaterialParameterCollection* MPC = LoadCollection(CollectionPath, OutError);
	if (!MPC) return;

	const FString TypeLower = ParamType.IsEmpty() ? TEXT("scalar") : ParamType.ToLower();
	const bool bScalar = (TypeLower == TEXT("scalar"));
	const bool bVector = (TypeLower == TEXT("vector"));
	if (!bScalar && !bVector)
	{
		OutError = FString::Printf(TEXT("param_type must be 'scalar' or 'vector' (got '%s')"), *ParamType);
		return;
	}

	const FName Target(*ParamName);
	auto NameMatches = [Target](const FCollectionScalarParameter& P) { return P.ParameterName == Target; };
	auto VNameMatches = [Target](const FCollectionVectorParameter& P) { return P.ParameterName == Target; };
	if (MPC->ScalarParameters.ContainsByPredicate(NameMatches) ||
		MPC->VectorParameters.ContainsByPredicate(VNameMatches))
	{
		OutError = FString::Printf(TEXT("Parameter '%s' already exists on this collection"), *ParamName);
		return;
	}

	MPC->Modify();
	if (bScalar)
	{
		FCollectionScalarParameter Entry;
		Entry.ParameterName = Target;
		Entry.DefaultValue = ScalarDefault;
		MPC->ScalarParameters.Add(Entry);
	}
	else
	{
		FCollectionVectorParameter Entry;
		Entry.ParameterName = Target;
		Entry.DefaultValue = VectorDefault;
		MPC->VectorParameters.Add(Entry);
	}
	MPC->PostEditChange();
	MPC->MarkPackageDirty();

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"collection\":\"%s\",\"param_name\":\"%s\",\"param_type\":\"%s\"}"),
		*CollectionPath, *ParamName, *TypeLower);
}

void HandleSetCollectionParameterDefault(const FString& CollectionPath, const FString& ParamName,
	const FString& ParamType, float ScalarValue, const FLinearColor& VectorValue,
	FString& OutJsonString, FString& OutError)
{
	if (ParamName.IsEmpty()) { OutError = TEXT("param_name is required"); return; }

	UMaterialParameterCollection* MPC = LoadCollection(CollectionPath, OutError);
	if (!MPC) return;

	const FName Target(*ParamName);
	const FString TypeLower = ParamType.ToLower();
	const bool bScalarHint = TypeLower == TEXT("scalar");
	const bool bVectorHint = TypeLower == TEXT("vector");
	const bool bAuto       = !bScalarHint && !bVectorHint;

	FCollectionScalarParameter* ScalarEntry = nullptr;
	FCollectionVectorParameter* VectorEntry = nullptr;
	if (bScalarHint || bAuto)
	{
		ScalarEntry = MPC->ScalarParameters.FindByPredicate(
			[Target](const FCollectionScalarParameter& P) { return P.ParameterName == Target; });
	}
	if ((bVectorHint || bAuto) && !ScalarEntry)
	{
		VectorEntry = MPC->VectorParameters.FindByPredicate(
			[Target](const FCollectionVectorParameter& P) { return P.ParameterName == Target; });
	}

	if (!ScalarEntry && !VectorEntry)
	{
		OutError = FString::Printf(TEXT("Parameter '%s' not found on collection '%s'"),
			*ParamName, *CollectionPath);
		return;
	}

	const bool bIsScalar = (ScalarEntry != nullptr);
	MPC->Modify();
	if (bIsScalar) ScalarEntry->DefaultValue = ScalarValue;
	else           VectorEntry->DefaultValue = VectorValue;
	MPC->PostEditChange();
	MPC->MarkPackageDirty();

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"collection\":\"%s\",\"param_name\":\"%s\",\"param_type\":\"%s\"}"),
		*CollectionPath, *ParamName, bIsScalar ? TEXT("scalar") : TEXT("vector"));
}

void HandleSetMaterialInstanceParentFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString IP, NP;
	Args->TryGetStringField(TEXT("instance_path"),   IP);
	Args->TryGetStringField(TEXT("new_parent_path"), NP);
	HandleSetMaterialInstanceParent(IP, NP, OutJsonString, OutError);
}

void HandleResetMaterialInstanceParameterFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString IP, PN, PT;
	Args->TryGetStringField(TEXT("instance_path"), IP);
	Args->TryGetStringField(TEXT("param_name"),    PN);
	Args->TryGetStringField(TEXT("param_type"),    PT);
	HandleResetMaterialInstanceParameter(IP, PN, PT, OutJsonString, OutError);
}

void HandleCreateMaterialParameterCollectionFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString N, SP;
	Args->TryGetStringField(TEXT("name"),      N);
	Args->TryGetStringField(TEXT("save_path"), SP);
	HandleCreateMaterialParameterCollection(N, SP, OutJsonString, OutError);
}

void HandleAddCollectionParameterFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString CP, PN, PT;
	double SD = 0.0;
	Args->TryGetStringField(TEXT("collection_path"), CP);
	Args->TryGetStringField(TEXT("param_name"),      PN);
	Args->TryGetStringField(TEXT("param_type"),      PT);
	Args->TryGetNumberField(TEXT("default_value"),   SD);

	FLinearColor VD(0,0,0,1);
	const TArray<TSharedPtr<FJsonValue>>* DefArr = nullptr;
	if (Args->TryGetArrayField(TEXT("default_color"), DefArr) && DefArr && DefArr->Num() >= 3)
	{
		VD.R = (float)(*DefArr)[0]->AsNumber();
		VD.G = (float)(*DefArr)[1]->AsNumber();
		VD.B = (float)(*DefArr)[2]->AsNumber();
		VD.A = DefArr->Num() >= 4 ? (float)(*DefArr)[3]->AsNumber() : 1.f;
	}
	HandleAddCollectionParameter(CP, PN, PT, (float)SD, VD, OutJsonString, OutError);
}

void HandleSetCollectionParameterDefaultFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString CP, PN, PT;
	double SV = 0.0;
	Args->TryGetStringField(TEXT("collection_path"), CP);
	Args->TryGetStringField(TEXT("param_name"),      PN);
	Args->TryGetStringField(TEXT("param_type"),      PT);
	Args->TryGetNumberField(TEXT("value"),           SV);

	FLinearColor VV(0,0,0,1);
	const TArray<TSharedPtr<FJsonValue>>* ColorArr = nullptr;
	if (Args->TryGetArrayField(TEXT("color"), ColorArr) && ColorArr && ColorArr->Num() >= 3)
	{
		VV.R = (float)(*ColorArr)[0]->AsNumber();
		VV.G = (float)(*ColorArr)[1]->AsNumber();
		VV.B = (float)(*ColorArr)[2]->AsNumber();
		VV.A = ColorArr->Num() >= 4 ? (float)(*ColorArr)[3]->AsNumber() : 1.f;
	}
	HandleSetCollectionParameterDefault(CP, PN, PT, (float)SV, VV, OutJsonString, OutError);
}

void HandleCreateParticleMaterial(const FString& Name, const FString& SavePath,
	const FString& BlendMode, bool bSoftEdge, FString& OutJsonString, FString& OutError,
	bool bWithTexture, float ClipThreshold, const FString& TextureParamName)
{
	if (Name.IsEmpty()) { OutError = TEXT("name is required"); return; }

	FString PackagePath = SavePath.IsEmpty() ? TEXT("/Game/VFX/Materials") : SavePath;
	while (PackagePath.EndsWith(TEXT("/"))) PackagePath = PackagePath.LeftChop(1);

	const FString FullPath = FString::Printf(TEXT("%s/%s.%s"), *PackagePath, *Name, *Name);
	if (UEditorAssetLibrary::DoesAssetExist(FullPath))
	{
		OutError = FString::Printf(TEXT("Material '%s' already exists at '%s'"), *Name, *PackagePath);
		return;
	}

	const FString ModeLower = BlendMode.IsEmpty() ? TEXT("additive") : BlendMode.ToLower();
	EBlendMode TargetBlend = BLEND_Additive;
	if      (ModeLower == TEXT("additive"))    TargetBlend = BLEND_Additive;
	else if (ModeLower == TEXT("translucent")) TargetBlend = BLEND_Translucent;
	else if (ModeLower == TEXT("masked"))      TargetBlend = BLEND_Masked;
	else if (ModeLower == TEXT("modulate"))    TargetBlend = BLEND_Modulate;
	else if (ModeLower == TEXT("alphacomposite")) TargetBlend = BLEND_AlphaComposite;
	else
	{
		OutError = FString::Printf(
			TEXT("blend_mode '%s' not supported. Use additive (fire/light/glow), translucent (smoke/dust), masked (cutout), modulate, or alphacomposite."),
			*BlendMode);
		return;
	}

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
	UMaterialFactoryNew* Factory = NewObject<UMaterialFactoryNew>();
	UObject* NewAsset = AssetToolsModule.Get().CreateAsset(Name, PackagePath, UMaterial::StaticClass(), Factory);
	UMaterial* M = Cast<UMaterial>(NewAsset);
	if (!M) { OutError = TEXT("Failed to create material asset"); return; }

	M->PreEditChange(nullptr);
	M->BlendMode = TargetBlend;
	M->SetShadingModel(MSM_Unlit);
	M->TwoSided = true;
	if (TargetBlend == BLEND_Additive)
	{
		M->bDisableDepthTest = false;
	}

	UMaterialEditorOnlyData* EditorOnly = M->GetEditorOnlyData();
	if (!EditorOnly) { OutError = TEXT("Material has no editor-only data"); return; }

	UClass* ParticleColorClass = FindObject<UClass>(nullptr, TEXT("/Script/Engine.MaterialExpressionParticleColor"));
	if (!ParticleColorClass)
	{
		OutError = TEXT("Engine class MaterialExpressionParticleColor not found");
		return;
	}
	UMaterialExpression* ParticleColor = NewObject<UMaterialExpression>(M, ParticleColorClass);
	M->GetExpressionCollection().AddExpression(ParticleColor);

	FExpressionInput* EmissiveInput = M->GetExpressionInputForProperty(EMaterialProperty::MP_EmissiveColor);
	if (EmissiveInput)
	{
		EmissiveInput->Connect(0, ParticleColor);
	}

	UMaterialExpression* AlphaSource = ParticleColor;
	int32 AlphaSourcePin = 4;

	if (bSoftEdge)
	{
		UMaterialExpressionTextureCoordinate* TC = NewObject<UMaterialExpressionTextureCoordinate>(M);
		M->GetExpressionCollection().AddExpression(TC);

		UMaterialExpressionConstant* Two = NewObject<UMaterialExpressionConstant>(M);
		Two->R = 2.0f;
		M->GetExpressionCollection().AddExpression(Two);

		UMaterialExpressionMultiply* MulUV = NewObject<UMaterialExpressionMultiply>(M);
		MulUV->A.Connect(0, TC);
		MulUV->B.Connect(0, Two);
		M->GetExpressionCollection().AddExpression(MulUV);

		UMaterialExpressionConstant* One = NewObject<UMaterialExpressionConstant>(M);
		One->R = 1.0f;
		M->GetExpressionCollection().AddExpression(One);

		UMaterialExpressionSubtract* Centered = NewObject<UMaterialExpressionSubtract>(M);
		Centered->A.Connect(0, MulUV);
		Centered->B.Connect(0, One);
		M->GetExpressionCollection().AddExpression(Centered);

		UMaterialExpressionConstant2Vector* Origin = NewObject<UMaterialExpressionConstant2Vector>(M);
		Origin->R = 0.0f;
		Origin->G = 0.0f;
		M->GetExpressionCollection().AddExpression(Origin);

		UMaterialExpressionDistance* Len = NewObject<UMaterialExpressionDistance>(M);
		Len->A.Connect(0, Centered);
		Len->B.Connect(0, Origin);
		M->GetExpressionCollection().AddExpression(Len);

		UMaterialExpressionSaturate* Sat = NewObject<UMaterialExpressionSaturate>(M);
		Sat->Input.Connect(0, Len);
		M->GetExpressionCollection().AddExpression(Sat);

		UMaterialExpressionOneMinus* OneMinus = NewObject<UMaterialExpressionOneMinus>(M);
		OneMinus->Input.Connect(0, Sat);
		M->GetExpressionCollection().AddExpression(OneMinus);

		UMaterialExpressionConstant* TwoExp = NewObject<UMaterialExpressionConstant>(M);
		TwoExp->R = 2.0f;
		M->GetExpressionCollection().AddExpression(TwoExp);

		UMaterialExpressionPower* SoftFalloff = NewObject<UMaterialExpressionPower>(M);
		SoftFalloff->Base.Connect(0, OneMinus);
		SoftFalloff->Exponent.Connect(0, TwoExp);
		M->GetExpressionCollection().AddExpression(SoftFalloff);

		UMaterialExpressionMultiply* AlphaMul = NewObject<UMaterialExpressionMultiply>(M);
		AlphaMul->A.Connect(4, ParticleColor);
		AlphaMul->B.Connect(0, SoftFalloff);
		M->GetExpressionCollection().AddExpression(AlphaMul);

		AlphaSource = AlphaMul;
		AlphaSourcePin = 0;

		if (TargetBlend == BLEND_Additive)
		{
			UMaterialExpressionMultiply* RGBMul = NewObject<UMaterialExpressionMultiply>(M);
			RGBMul->A.Connect(0, ParticleColor);
			RGBMul->B.Connect(0, SoftFalloff);
			M->GetExpressionCollection().AddExpression(RGBMul);

			if (EmissiveInput) { EmissiveInput->Expression = nullptr; EmissiveInput->Connect(0, RGBMul); }
		}
	}

	if (bWithTexture)
	{
		const FName TexParam = TextureParamName.IsEmpty() ? FName(TEXT("BaseColorTex")) : FName(*TextureParamName);
		UMaterialExpressionTextureSampleParameter2D* TexSample =
			NewObject<UMaterialExpressionTextureSampleParameter2D>(M);
		TexSample->ParameterName = TexParam;
		M->GetExpressionCollection().AddExpression(TexSample);

		UMaterialExpression* RGBSource = TexSample;
		int32 RGBPin = 0;

		if (ClipThreshold > 0.0f)
		{
			UMaterialExpressionConstant* Thresh = NewObject<UMaterialExpressionConstant>(M);
			Thresh->R = ClipThreshold;
			M->GetExpressionCollection().AddExpression(Thresh);

			UMaterialExpressionSubtract* Sub = NewObject<UMaterialExpressionSubtract>(M);
			Sub->A.Connect(0, TexSample);
			Sub->B.Connect(0, Thresh);
			M->GetExpressionCollection().AddExpression(Sub);

			UMaterialExpressionSaturate* Clip = NewObject<UMaterialExpressionSaturate>(M);
			Clip->Input.Connect(0, Sub);
			M->GetExpressionCollection().AddExpression(Clip);

			RGBSource = Clip;
			RGBPin = 0;
		}

		if (EmissiveInput && EmissiveInput->Expression)
		{
			UMaterialExpression* PrevExpr  = EmissiveInput->Expression;
			const int32           PrevPin = EmissiveInput->OutputIndex;

			UMaterialExpressionMultiply* TexMul = NewObject<UMaterialExpressionMultiply>(M);
			TexMul->A.Connect(PrevPin, PrevExpr);
			TexMul->B.Connect(RGBPin, RGBSource);
			M->GetExpressionCollection().AddExpression(TexMul);

			EmissiveInput->Expression = nullptr;
			EmissiveInput->Connect(0, TexMul);
		}

		if (TargetBlend == BLEND_Translucent || TargetBlend == BLEND_AlphaComposite || TargetBlend == BLEND_Masked)
		{
			UMaterialExpressionMultiply* AlphaTexMul = NewObject<UMaterialExpressionMultiply>(M);
			AlphaTexMul->A.Connect(AlphaSourcePin, AlphaSource);
			AlphaTexMul->B.Connect(4, TexSample);
			M->GetExpressionCollection().AddExpression(AlphaTexMul);
			AlphaSource = AlphaTexMul;
			AlphaSourcePin = 0;
		}
	}

	if (TargetBlend == BLEND_Translucent || TargetBlend == BLEND_AlphaComposite)
	{
		FExpressionInput* OpacityInput = M->GetExpressionInputForProperty(EMaterialProperty::MP_Opacity);
		if (OpacityInput) OpacityInput->Connect(AlphaSourcePin, AlphaSource);
	}
	else if (TargetBlend == BLEND_Masked)
	{
		FExpressionInput* OpacityMaskInput = M->GetExpressionInputForProperty(EMaterialProperty::MP_OpacityMask);
		if (OpacityMaskInput) OpacityMaskInput->Connect(AlphaSourcePin, AlphaSource);
	}

	M->PostEditChange();
	M->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(FString::Printf(TEXT("%s/%s"), *PackagePath, *Name), false);
	FAssetRegistryModule::AssetCreated(M);

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"asset_path\":\"%s/%s\",\"blend_mode\":\"%s\",\"shading_model\":\"Unlit\",\"soft_edge\":%s,\"with_texture\":%s,\"texture_param_name\":\"%s\",\"clip_threshold\":%.3f}"),
		*PackagePath, *Name, *ModeLower,
		bSoftEdge ? TEXT("true") : TEXT("false"),
		bWithTexture ? TEXT("true") : TEXT("false"),
		bWithTexture ? *(TextureParamName.IsEmpty() ? FString(TEXT("BaseColorTex")) : TextureParamName) : TEXT(""),
		ClipThreshold);
}

void HandleCreateParticleMaterialFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString N, SP, BM, TexParamName;
	bool bSoft = true;
	bool bWithTex = false;
	double Clip = 0.0;
	Args->TryGetStringField(TEXT("name"),               N);
	Args->TryGetStringField(TEXT("save_path"),          SP);
	Args->TryGetStringField(TEXT("blend_mode"),         BM);
	Args->TryGetBoolField  (TEXT("soft_edge"),          bSoft);
	Args->TryGetBoolField  (TEXT("with_texture"),       bWithTex);
	Args->TryGetNumberField(TEXT("clip_threshold"),     Clip);
	Args->TryGetStringField(TEXT("texture_param_name"), TexParamName);
	HandleCreateParticleMaterial(N, SP, BM, bSoft, OutJsonString, OutError,
		bWithTex, (float)Clip, TexParamName);
}

}
