// Copyright 2026, BlueprintsLab, All rights reserved

#include "SUECPMainWidget.h"
#include "LearningManager.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Factories/MaterialFactoryNew.h"
#include "Factories/MaterialInstanceConstantFactoryNew.h"
#include "Engine/Texture2D.h"
#include "Utils/TextureProcessingUtils.h"
#include "MeshAssetManager.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionWorldPosition.h"
#include "Materials/MaterialExpressionDivide.h"
#include "Materials/MaterialExpressionNoise.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionClamp.h"
#include "Materials/MaterialExpressionAdd.h"
#include "TextureGenManager.h"
#include "ApiKeyManager.h"
#include "UObject/Package.h"
#include "Widget/UUECPAppBridge.h"

FToolExecutionResult SUECPMainWidget::ExecuteTool_GeneratePBRMaterial(const TSharedPtr<FJsonObject>& Args)
{
	FToolExecutionResult Result;
	TSharedPtr<FJsonObject> ResultObject = MakeShareable(new FJsonObject);

	auto WriteResultJson = [&ResultObject, &Result]()
	{
		FString S;
		TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&S);
		FJsonSerializer::Serialize(ResultObject.ToSharedRef(), W);
		Result.ResultJson = S;
	};

	if (ActivePBRMaterialGen.bIsGenerating)
	{
		Result.bSuccess = false;
		Result.ErrorMessage = TEXT("A PBR material is already being generated. Please wait for it to complete.");
		ResultObject->SetBoolField(TEXT("success"), false);
		ResultObject->SetStringField(TEXT("error"), Result.ErrorMessage);
		WriteResultJson();
		return Result;
	}

	FString MaterialName;
	if (!Args->TryGetStringField(TEXT("name"), MaterialName))
	{
		Result.bSuccess = false;
		Result.ErrorMessage = TEXT("Missing required parameter: name");
		ResultObject->SetBoolField(TEXT("success"), false);
		ResultObject->SetStringField(TEXT("error"), Result.ErrorMessage);
		WriteResultJson();
		return Result;
	}

	FString Description;
	if (!Args->TryGetStringField(TEXT("description"), Description))
	{
		Result.bSuccess = false;
		Result.ErrorMessage = TEXT("Missing required parameter: description");
		ResultObject->SetBoolField(TEXT("success"), false);
		ResultObject->SetStringField(TEXT("error"), Result.ErrorMessage);
		WriteResultJson();
		return Result;
	}

	ActivePBRMaterialGen.MaterialName = MaterialName;
	ActivePBRMaterialGen.Description = Description;
	ActivePBRMaterialGen.SavePath = TEXT("/Game/GeneratedMaterials");
	ActivePBRMaterialGen.bIsMetallic = false;
	ActivePBRMaterialGen.bGenerateAO = true;
	ActivePBRMaterialGen.CurrentStep = 0;
	ActivePBRMaterialGen.TotalSteps = 1;
	ActivePBRMaterialGen.BaseColorPath.Empty();
	ActivePBRMaterialGen.NormalPath.Empty();
	ActivePBRMaterialGen.RoughnessPath.Empty();
	ActivePBRMaterialGen.MetallicPath.Empty();
	ActivePBRMaterialGen.AOPath.Empty();
	ActivePBRMaterialGen.bIsGenerating = true;

	if (!Args->TryGetStringField(TEXT("save_path"), ActivePBRMaterialGen.SavePath) || ActivePBRMaterialGen.SavePath.IsEmpty())
	{
		ActivePBRMaterialGen.SavePath = TEXT("/Game/GeneratedMaterials");
	}
	while (ActivePBRMaterialGen.SavePath.EndsWith(TEXT("/")))
		ActivePBRMaterialGen.SavePath.LeftChopInline(1, EAllowShrinking::No);

	Args->TryGetBoolField(TEXT("is_metallic"), ActivePBRMaterialGen.bIsMetallic);
	Args->TryGetBoolField(TEXT("generate_ao"), ActivePBRMaterialGen.bGenerateAO);

	ProcessPBRMaterialGenerationStep();

	FString FinalMatName = MaterialName.StartsWith(TEXT("M_")) ? MaterialName : (TEXT("M_") + MaterialName);
	FString ExpectedMaterialPath = FString::Printf(TEXT("%s/%s"), *ActivePBRMaterialGen.SavePath, *FinalMatName);
	int32 TotalTextures = ActivePBRMaterialGen.bGenerateAO ? 5 : 4;

	FString MIName = TEXT("MI_") + (MaterialName.StartsWith(TEXT("M_")) ? MaterialName.Mid(2) : MaterialName);
	FString ExpectedMIPath = FString::Printf(TEXT("%s/%s"), *ActivePBRMaterialGen.SavePath, *MIName);

	Result.bSuccess = true;
	ResultObject->SetBoolField(TEXT("success"), true);
	ResultObject->SetStringField(TEXT("message"), FString::Printf(
		TEXT("Generating PBR material '%s' with %d textures. ASYNC — pipeline runs ~30-90s after this call returns. The material/instance/textures are NOT readable yet. Wait for the next user message before calling get_material_summary, get_material_instance_parameters, validate_material, or any other read on these paths. Continue with non-material work in the meantime, or end this turn."),
		*MaterialName, TotalTextures));
	ResultObject->SetStringField(TEXT("material_path"), ExpectedMaterialPath);
	ResultObject->SetStringField(TEXT("material_instance_path"), ExpectedMIPath);
	ResultObject->SetNumberField(TEXT("total_textures"), TotalTextures);
	ResultObject->SetStringField(TEXT("material_name"), MaterialName);
	ResultObject->SetBoolField(TEXT("async"), true);
	ResultObject->SetBoolField(TEXT("read_available"), false);
	ResultObject->SetStringField(TEXT("read_available_when"), TEXT("Pipeline emits a chat notification when complete. Do not poll — wait for the user."));
	WriteResultJson();
	return Result;
}

void SUECPMainWidget::ProcessPBRMaterialGenerationStep()
{
	if (ActivePBRMaterialGen.CurrentStep >= ActivePBRMaterialGen.TotalSteps)
	{
		CreatePBRMaterialFromGeneratedTextures();
		return;
	}

	FString Prompt = FString::Printf(
		TEXT("photorealistic %s texture, seamless tileable, high quality, detailed, top-down flat surface"),
		*ActivePBRMaterialGen.Description);
	FString AssetName = TEXT("T_") + ActivePBRMaterialGen.MaterialName + TEXT("_BC");

	FTextureGenRequest Request;
	Request.Prompt = Prompt;
	Request.AspectRatio = TEXT("1:1");
	Request.CustomAssetName = AssetName;
	Request.SavePath = ActivePBRMaterialGen.SavePath.IsEmpty() ? TEXT("/Game/GeneratedTextures") : ActivePBRMaterialGen.SavePath;

	FString ApiKey = FApiKeyManager::Get().GetActiveTextureGenApiKey();
	FTextureGenManager::Get().GenerateTexture(Request, ApiKey,
		[this](const FTextureGenResult& TexResult)
		{
			OnPBRTextureGenerated(TexResult);
		}
	);
}

void SUECPMainWidget::OnPBRTextureGenerated(const FTextureGenResult& Result)
{
	if (!Result.bSuccess)
	{
		ActivePBRMaterialGen.bIsGenerating = false;
		UE_LOG(LogTemp, Error, TEXT("PBR Material Generation Error: %s"), *Result.ErrorMessage);
		return;
	}

	ActivePBRMaterialGen.BaseColorPath = Result.AssetPath;

	UTexture2D* BaseColorTex = LoadObject<UTexture2D>(nullptr, *Result.AssetPath);
	if (!BaseColorTex)
	{
		ActivePBRMaterialGen.bIsGenerating = false;
		UE_LOG(LogTemp, Error, TEXT("PBR: Failed to load base color texture for map derivation: %s"), *Result.AssetPath);
		return;
	}

	FString SavePath = ActivePBRMaterialGen.SavePath.IsEmpty() ? TEXT("/Game/GeneratedTextures") : ActivePBRMaterialGen.SavePath;
	FString MatName = ActivePBRMaterialGen.MaterialName;

	if (UTexture2D* NormalTex = FTextureProcessingUtils::GenerateNormalMap(
		BaseColorTex, TEXT("T_") + MatName + TEXT("_N"), SavePath, 2.0f))
	{
		ActivePBRMaterialGen.NormalPath = NormalTex->GetPathName();
	}
	if (UTexture2D* RoughnessTex = FTextureProcessingUtils::GenerateRoughnessMap(
		BaseColorTex, TEXT("T_") + MatName + TEXT("_R"), SavePath, 1.0f))
	{
		ActivePBRMaterialGen.RoughnessPath = RoughnessTex->GetPathName();
	}
	if (UTexture2D* MetallicTex = FTextureProcessingUtils::GenerateMetallicMap(
		BaseColorTex, TEXT("T_") + MatName + TEXT("_M"), SavePath, ActivePBRMaterialGen.bIsMetallic, 0.5f))
	{
		ActivePBRMaterialGen.MetallicPath = MetallicTex->GetPathName();
	}
	if (ActivePBRMaterialGen.bGenerateAO)
	{
		if (UTexture2D* AOTex = FTextureProcessingUtils::GenerateAOMap(
			BaseColorTex, TEXT("T_") + MatName + TEXT("_AO"), SavePath, 1.0f))
		{
			ActivePBRMaterialGen.AOPath = AOTex->GetPathName();
		}
	}

	CreatePBRMaterialFromGeneratedTextures();
}

void SUECPMainWidget::CreatePBRMaterialFromGeneratedTextures()
{
	FString MaterialName = ActivePBRMaterialGen.MaterialName.StartsWith(TEXT("M_"))
		? ActivePBRMaterialGen.MaterialName
		: (TEXT("M_") + ActivePBRMaterialGen.MaterialName);
	FString SavePath = ActivePBRMaterialGen.SavePath.IsEmpty() ? TEXT("/Game/GeneratedMaterials") : ActivePBRMaterialGen.SavePath;
	FString MaterialPath = FString::Printf(TEXT("%s/%s"), *SavePath, *MaterialName);

	UPackage* Package = CreatePackage(*MaterialPath);
	if (!Package)
	{
		ActivePBRMaterialGen.bIsGenerating = false;
		UE_LOG(LogTemp, Error, TEXT("Failed to create package at path: %s"), *MaterialPath);
		return;
	}

	UMaterialFactoryNew* MaterialFactory = NewObject<UMaterialFactoryNew>();
	UMaterial* NewMaterial = Cast<UMaterial>(MaterialFactory->FactoryCreateNew(
		UMaterial::StaticClass(),
		Package,
		FName(*MaterialName),
		RF_Public | RF_Standalone,
		nullptr,
		GWarn
	));

	if (!NewMaterial)
	{
		ActivePBRMaterialGen.bIsGenerating = false;
		UE_LOG(LogTemp, Error, TEXT("Failed to create material asset."));
		return;
	}

	NewMaterial->PreEditChange(nullptr);

	UTexture2D* BaseColorTex = LoadObject<UTexture2D>(nullptr, *ActivePBRMaterialGen.BaseColorPath);
	UTexture2D* NormalTex = !ActivePBRMaterialGen.NormalPath.IsEmpty() ? LoadObject<UTexture2D>(nullptr, *ActivePBRMaterialGen.NormalPath) : nullptr;
	UTexture2D* RoughnessTex = !ActivePBRMaterialGen.RoughnessPath.IsEmpty() ? LoadObject<UTexture2D>(nullptr, *ActivePBRMaterialGen.RoughnessPath) : nullptr;
	UTexture2D* MetallicTex = !ActivePBRMaterialGen.MetallicPath.IsEmpty() ? LoadObject<UTexture2D>(nullptr, *ActivePBRMaterialGen.MetallicPath) : nullptr;
	UTexture2D* AOTex = (ActivePBRMaterialGen.bGenerateAO && !ActivePBRMaterialGen.AOPath.IsEmpty()) ? LoadObject<UTexture2D>(nullptr, *ActivePBRMaterialGen.AOPath) : nullptr;

	auto& Expressions = NewMaterial->GetEditorOnlyData()->ExpressionCollection.Expressions;

	UMaterialExpressionScalarParameter* TilingParam = NewObject<UMaterialExpressionScalarParameter>(NewMaterial);
	TilingParam->ParameterName = TEXT("Tiling");
	TilingParam->DefaultValue = 1.0f;
	TilingParam->MaterialExpressionEditorX = -1200;
	TilingParam->MaterialExpressionEditorY = 200;
	Expressions.Add(TilingParam);

	UMaterialExpressionTextureCoordinate* TexCoord = NewObject<UMaterialExpressionTextureCoordinate>(NewMaterial);
	TexCoord->CoordinateIndex = 0;
	TexCoord->MaterialExpressionEditorX = -1200;
	TexCoord->MaterialExpressionEditorY = 300;
	Expressions.Add(TexCoord);

	UMaterialExpressionMultiply* TilingMul = NewObject<UMaterialExpressionMultiply>(NewMaterial);
	TilingMul->MaterialExpressionEditorX = -950;
	TilingMul->MaterialExpressionEditorY = 250;
	TilingMul->A.Expression = TexCoord;
	TilingMul->B.Expression = TilingParam;
	Expressions.Add(TilingMul);

	int32 NodeX = -600;
	int32 NodeY = 0;
	int32 NodeSpacing = 300;
	int32 TexCount = 0;

	if (BaseColorTex)
	{
		UMaterialExpressionTextureSample* BCExpression = NewObject<UMaterialExpressionTextureSample>(NewMaterial);
		BCExpression->Texture = BaseColorTex;
		BCExpression->SamplerType = SAMPLERTYPE_Color;
		BCExpression->Coordinates.Expression = TilingMul;
		BCExpression->MaterialExpressionEditorX = NodeX;
		BCExpression->MaterialExpressionEditorY = NodeY;
		Expressions.Add(BCExpression);

		int32 MacroX = -1600;
		int32 MacroY = -400;

		UMaterialExpressionWorldPosition* WorldPos = NewObject<UMaterialExpressionWorldPosition>(NewMaterial);
		WorldPos->MaterialExpressionEditorX = MacroX;
		WorldPos->MaterialExpressionEditorY = MacroY;
		Expressions.Add(WorldPos);

		UMaterialExpressionScalarParameter* MacroScaleParam = NewObject<UMaterialExpressionScalarParameter>(NewMaterial);
		MacroScaleParam->ParameterName = TEXT("MacroScale");
		MacroScaleParam->DefaultValue = 8000.0f;
		MacroScaleParam->MaterialExpressionEditorX = MacroX + 200;
		MacroScaleParam->MaterialExpressionEditorY = MacroY + 100;
		Expressions.Add(MacroScaleParam);

		UMaterialExpressionDivide* MacroDiv = NewObject<UMaterialExpressionDivide>(NewMaterial);
		MacroDiv->A.Expression = WorldPos;
		MacroDiv->B.Expression = MacroScaleParam;
		MacroDiv->MaterialExpressionEditorX = MacroX + 450;
		MacroDiv->MaterialExpressionEditorY = MacroY;
		Expressions.Add(MacroDiv);

		UMaterialExpressionNoise* MacroNoise = NewObject<UMaterialExpressionNoise>(NewMaterial);
		MacroNoise->Position.Expression = MacroDiv;
		MacroNoise->Scale = 1.0f;
		MacroNoise->Levels = 3;
		MacroNoise->OutputMin = 0.0f;
		MacroNoise->OutputMax = 1.0f;
		MacroNoise->MaterialExpressionEditorX = MacroX + 700;
		MacroNoise->MaterialExpressionEditorY = MacroY;
		Expressions.Add(MacroNoise);

		UMaterialExpressionScalarParameter* MacroIntensityParam = NewObject<UMaterialExpressionScalarParameter>(NewMaterial);
		MacroIntensityParam->ParameterName = TEXT("MacroVariation");
		MacroIntensityParam->DefaultValue = 0.25f;
		MacroIntensityParam->MaterialExpressionEditorX = MacroX + 700;
		MacroIntensityParam->MaterialExpressionEditorY = MacroY + 150;
		Expressions.Add(MacroIntensityParam);

		UMaterialExpressionMultiply* NoiseScaled = NewObject<UMaterialExpressionMultiply>(NewMaterial);
		NoiseScaled->A.Expression = MacroNoise;
		NoiseScaled->B.Expression = MacroIntensityParam;
		NoiseScaled->MaterialExpressionEditorX = MacroX + 950;
		NoiseScaled->MaterialExpressionEditorY = MacroY;
		Expressions.Add(NoiseScaled);

		UMaterialExpressionConstant* OneConst = NewObject<UMaterialExpressionConstant>(NewMaterial);
		OneConst->R = 1.0f;
		OneConst->MaterialExpressionEditorX = MacroX + 950;
		OneConst->MaterialExpressionEditorY = MacroY + 100;
		Expressions.Add(OneConst);

		UMaterialExpressionAdd* OneMinus = NewObject<UMaterialExpressionAdd>(NewMaterial);
		OneMinus->A.Expression = OneConst;
		OneMinus->B.Expression = NoiseScaled;
		OneMinus->MaterialExpressionEditorX = MacroX + 1150;
		OneMinus->MaterialExpressionEditorY = MacroY;
		Expressions.Add(OneMinus);

		UMaterialExpressionClamp* MacroClamp = NewObject<UMaterialExpressionClamp>(NewMaterial);
		MacroClamp->Input.Expression = OneMinus;
		MacroClamp->MinDefault = 0.5f;
		MacroClamp->MaxDefault = 1.0f;
		MacroClamp->MaterialExpressionEditorX = MacroX + 1350;
		MacroClamp->MaterialExpressionEditorY = MacroY;
		Expressions.Add(MacroClamp);

		UMaterialExpressionMultiply* FinalBC = NewObject<UMaterialExpressionMultiply>(NewMaterial);
		FinalBC->A.Expression = BCExpression;
		FinalBC->B.Expression = MacroClamp;
		FinalBC->MaterialExpressionEditorX = NodeX + 200;
		FinalBC->MaterialExpressionEditorY = NodeY;
		Expressions.Add(FinalBC);

		NewMaterial->GetEditorOnlyData()->BaseColor.Expression = FinalBC;
		NodeY += NodeSpacing;
		TexCount++;
	}

	if (NormalTex)
	{
		UMaterialExpressionTextureSample* NExpression = NewObject<UMaterialExpressionTextureSample>(NewMaterial);
		NExpression->Texture = NormalTex;
		NExpression->SamplerType = SAMPLERTYPE_Normal;
		NExpression->Coordinates.Expression = TilingMul;
		NExpression->MaterialExpressionEditorX = NodeX;
		NExpression->MaterialExpressionEditorY = NodeY;
		Expressions.Add(NExpression);
		NewMaterial->GetEditorOnlyData()->Normal.Expression = NExpression;
		NodeY += NodeSpacing;
		TexCount++;
	}

	if (RoughnessTex)
	{
		UMaterialExpressionTextureSample* RExpression = NewObject<UMaterialExpressionTextureSample>(NewMaterial);
		RExpression->Texture = RoughnessTex;
		RExpression->SamplerType = SAMPLERTYPE_LinearGrayscale;
		RExpression->Coordinates.Expression = TilingMul;
		RExpression->MaterialExpressionEditorX = NodeX;
		RExpression->MaterialExpressionEditorY = NodeY;
		Expressions.Add(RExpression);
		NewMaterial->GetEditorOnlyData()->Roughness.Expression = RExpression;
		NodeY += NodeSpacing;
		TexCount++;
	}

	if (MetallicTex)
	{
		UMaterialExpressionTextureSample* MExpression = NewObject<UMaterialExpressionTextureSample>(NewMaterial);
		MExpression->Texture = MetallicTex;
		MExpression->SamplerType = SAMPLERTYPE_LinearGrayscale;
		MExpression->Coordinates.Expression = TilingMul;
		MExpression->MaterialExpressionEditorX = NodeX;
		MExpression->MaterialExpressionEditorY = NodeY;
		Expressions.Add(MExpression);
		NewMaterial->GetEditorOnlyData()->Metallic.Expression = MExpression;
		NodeY += NodeSpacing;
		TexCount++;
	}

	if (AOTex)
	{
		UMaterialExpressionTextureSample* AOExpression = NewObject<UMaterialExpressionTextureSample>(NewMaterial);
		AOExpression->Texture = AOTex;
		AOExpression->SamplerType = SAMPLERTYPE_LinearGrayscale;
		AOExpression->Coordinates.Expression = TilingMul;
		AOExpression->MaterialExpressionEditorX = NodeX;
		AOExpression->MaterialExpressionEditorY = NodeY;
		Expressions.Add(AOExpression);
		NewMaterial->GetEditorOnlyData()->AmbientOcclusion.Expression = AOExpression;
		TexCount++;
	}

	NewMaterial->PostEditChange();
	Package->MarkPackageDirty();

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	AssetRegistryModule.AssetCreated(NewMaterial);

	FString InstanceName = TEXT("MI_") + ActivePBRMaterialGen.MaterialName;
	FString InstancePath = FString::Printf(TEXT("%s/%s"), *SavePath, *InstanceName);
	if (UPackage* InstPackage = CreatePackage(*InstancePath))
	{
		UMaterialInstanceConstantFactoryNew* MICFactory = NewObject<UMaterialInstanceConstantFactoryNew>();
		MICFactory->InitialParent = NewMaterial;
		UMaterialInstanceConstant* MIC = Cast<UMaterialInstanceConstant>(MICFactory->FactoryCreateNew(
			UMaterialInstanceConstant::StaticClass(),
			InstPackage,
			FName(*InstanceName),
			RF_Public | RF_Standalone,
			nullptr,
			GWarn
		));
		if (MIC)
		{
			MIC->SetParentEditorOnly(NewMaterial);
			MIC->PostEditChange();
			InstPackage->MarkPackageDirty();
			AssetRegistryModule.AssetCreated(MIC);
		}
	}

	ActivePBRMaterialGen.bIsGenerating = false;

	if (AppBridgeObject)
	{
		AppBridgeObject->PushToast(FString::Printf(TEXT("PBR Material Generated: %s (%d textures)"), *MaterialName, TexCount), TEXT("success"));
	}

	if (FLearningManager::Get().IsInitialized())
	{
		TSharedPtr<FJsonObject> D = MakeShareable(new FJsonObject);
		D->SetStringField(TEXT("material_name"), MaterialName);
		D->SetNumberField(TEXT("texture_count"), TexCount);
		FLearningManager::Get().TrackEvent(TEXT("texture_generated"), D);
	}
}

FToolExecutionResult SUECPMainWidget::ExecuteTool_RetextureMesh(const TSharedPtr<FJsonObject>& Args)
{
	FToolExecutionResult Result;
	TSharedPtr<FJsonObject> ResultObject = MakeShareable(new FJsonObject);

	auto WriteResultJson = [&ResultObject, &Result]()
	{
		FString S; TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&S);
		FJsonSerializer::Serialize(ResultObject.ToSharedRef(), W);
		Result.ResultJson = S;
	};

	FString MeshPath, Description, SavePath, MaterialName;
	if (!Args->TryGetStringField(TEXT("mesh_path"), MeshPath) || MeshPath.IsEmpty())
	{
		Result.bSuccess = false;
		Result.ErrorMessage = TEXT("Missing required parameter: mesh_path");
		ResultObject->SetBoolField(TEXT("success"), false);
		ResultObject->SetStringField(TEXT("error"), Result.ErrorMessage);
		WriteResultJson();
		return Result;
	}
	if (!Args->TryGetStringField(TEXT("description"), Description) || Description.IsEmpty())
	{
		Result.bSuccess = false;
		Result.ErrorMessage = TEXT("Missing required parameter: description");
		ResultObject->SetBoolField(TEXT("success"), false);
		ResultObject->SetStringField(TEXT("error"), Result.ErrorMessage);
		WriteResultJson();
		return Result;
	}

	Args->TryGetStringField(TEXT("save_path"), SavePath);
	Args->TryGetStringField(TEXT("material_name"), MaterialName);
	if (MaterialName.IsEmpty()) Args->TryGetStringField(TEXT("name"), MaterialName);
	if (MaterialName.IsEmpty()) MaterialName = TEXT("M_Retextured");

	FRetextureRequest RetexReq;
	RetexReq.MeshAssetPath = MeshPath;
	RetexReq.StylePrompt = Description;
	RetexReq.SavePath = SavePath.IsEmpty() ? TEXT("/Game/GeneratedTextures") : SavePath;
	RetexReq.MaterialName = MaterialName;
	RetexReq.bEnablePBR = true;

	FString ApiKey = FApiKeyManager::Get().GetActiveMeshGenApiKey();

	FMeshAssetManager::Get().RetextureMesh(RetexReq, ApiKey,
		[this, MaterialName, SavePath](const FRetextureResult& RetexResult)
		{
			if (!RetexResult.bSuccess)
			{
				UE_LOG(LogTemp, Error, TEXT("Retexture failed: %s"), *RetexResult.ErrorMessage);
				return;
			}

			if (!RetexResult.MaterialPath.IsEmpty())
			{
				if (AppBridgeObject)
				{
					AppBridgeObject->PushToast(FString::Printf(TEXT("Mesh retextured: %s (material from GLB)"), *MaterialName), TEXT("success"));
				}
				return;
			}

			if (!RetexResult.BaseColorPath.IsEmpty())
			{
				ActivePBRMaterialGen.MaterialName = MaterialName;
				ActivePBRMaterialGen.BaseColorPath = RetexResult.BaseColorPath;
				ActivePBRMaterialGen.NormalPath = RetexResult.NormalPath;
				ActivePBRMaterialGen.MetallicPath = RetexResult.MetallicPath;
				ActivePBRMaterialGen.RoughnessPath = RetexResult.RoughnessPath;
				ActivePBRMaterialGen.AOPath = FString();
				ActivePBRMaterialGen.bGenerateAO = false;
				ActivePBRMaterialGen.SavePath = SavePath;

				CreatePBRMaterialFromGeneratedTextures();

				if (AppBridgeObject)
				{
					AppBridgeObject->PushToast(FString::Printf(TEXT("Mesh retextured: %s (PBR textures + material)"), *MaterialName), TEXT("success"));
				}
			}
			else
			{
				UE_LOG(LogTemp, Error, TEXT("Retexture: No textures found in imported GLB"));
			}
		}
	);

	Result.bSuccess = true;
	ResultObject->SetBoolField(TEXT("success"), true);
	ResultObject->SetStringField(TEXT("message"), FString::Printf(TEXT("Started retexturing mesh '%s' via Meshy API — this may take 1-3 minutes..."), *MeshPath));
	ResultObject->SetStringField(TEXT("mesh_path"), MeshPath);
	ResultObject->SetStringField(TEXT("material_name"), MaterialName);
	WriteResultJson();
	return Result;
}

FToolExecutionResult SUECPMainWidget::ExecuteTool_GenerateTexture(const TSharedPtr<FJsonObject>& Args)
{
	FToolExecutionResult Result;
	TSharedPtr<FJsonObject> ResultObject = MakeShareable(new FJsonObject);

	auto WriteResultJson = [&ResultObject, &Result]()
	{
		FString S; TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&S);
		FJsonSerializer::Serialize(ResultObject.ToSharedRef(), W);
		Result.ResultJson = S;
	};

	FString Prompt;
	if (!Args->TryGetStringField(TEXT("prompt"), Prompt) || Prompt.IsEmpty())
	{
		Result.bSuccess = false;
		Result.ErrorMessage = TEXT("Missing required parameter: prompt");
		ResultObject->SetBoolField(TEXT("success"), false);
		ResultObject->SetStringField(TEXT("error"), Result.ErrorMessage);
		WriteResultJson();
		return Result;
	}

	FString TextureName;
	Args->TryGetStringField(TEXT("texture_name"), TextureName);

	FString SavePath = TEXT("/Game/GeneratedTextures");
	Args->TryGetStringField(TEXT("save_path"), SavePath);

	if (TextureName.IsEmpty() && !SavePath.IsEmpty() && SavePath.Contains(TEXT("/")))
	{
		FString LastPart = FPaths::GetCleanFilename(SavePath);
		FString ParentPath = FPaths::GetPath(SavePath);
		if (!ParentPath.IsEmpty() && ParentPath.StartsWith(TEXT("/Game")) && !LastPart.IsEmpty() && !LastPart.Contains(TEXT(".")))
		{
			FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
			if (!ARM.Get().PathExists(SavePath))
			{
				TextureName = LastPart;
				SavePath = ParentPath;
			}
		}
	}

	FString AspectRatio = TEXT("1024x1024");
	Args->TryGetStringField(TEXT("aspect_ratio"), AspectRatio);

	FString ApiKey = FApiKeyManager::Get().GetActiveTextureGenApiKey();
	if (ApiKey.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("Texture Generation Failed: No API key configured"));
		Result.bSuccess = false;
		Result.ErrorMessage = TEXT("No API key configured. Set an Image Gen API key in Settings, or it will use your main chat API key.");
		ResultObject->SetBoolField(TEXT("success"), false);
		ResultObject->SetStringField(TEXT("error"), Result.ErrorMessage);
		WriteResultJson();
		return Result;
	}

	FTextureGenRequest Request;
	Request.Prompt = Prompt;
	Request.CustomAssetName = TextureName;
	Request.AspectRatio = AspectRatio;
	Request.SavePath = SavePath;

	FTextureGenManager::Get().GenerateTexture(Request, ApiKey,
		[this, ResultObject, Prompt, SavePath, TextureName](const FTextureGenResult& GenResult)
		{
			if (GenResult.bSuccess)
			{
				ResultObject->SetBoolField(TEXT("success"), true);
				ResultObject->SetStringField(TEXT("texture_path"), GenResult.AssetPath);
				ResultObject->SetStringField(TEXT("message"), FString::Printf(TEXT("Texture generated successfully: %s"), *GenResult.AssetPath));
				if (AppBridgeObject) AppBridgeObject->PushToast(FString::Printf(TEXT("Texture Generated: %s"), *GenResult.AssetPath), TEXT("success"));
			}
			else
			{
				ResultObject->SetBoolField(TEXT("success"), false);
				ResultObject->SetStringField(TEXT("error"), GenResult.ErrorMessage);
				UE_LOG(LogTemp, Error, TEXT("Texture Generation Failed: %s"), *GenResult.ErrorMessage);
				if (AppBridgeObject) AppBridgeObject->PushToast(FString::Printf(TEXT("Texture Generation Failed: %s"), *GenResult.ErrorMessage), TEXT("error"));
			}
		});

	ResultObject->SetBoolField(TEXT("success"), true);
	ResultObject->SetBoolField(TEXT("pending"), true);
	ResultObject->SetStringField(TEXT("message"), TEXT("Texture generation started..."));
	ResultObject->SetStringField(TEXT("prompt"), Prompt);
	WriteResultJson();
	return Result;
}

bool SUECPMainWidget::TryDispatchMaterialTool(const FString& ToolName, const TSharedPtr<FJsonObject>& Arguments, FToolExecutionResult& OutResult)
{
	if (ToolName == TEXT("generate_pbr_material"))   { OutResult = ExecuteTool_GeneratePBRMaterial(Arguments); return true; }
	if (ToolName == TEXT("retexture_mesh"))          { OutResult = ExecuteTool_RetextureMesh(Arguments);       return true; }
	if (ToolName == TEXT("generate_texture"))        { OutResult = ExecuteTool_GenerateTexture(Arguments);     return true; }
	return false;
}
