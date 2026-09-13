// Copyright 2026, BlueprintsLab, All rights reserved.

#include "Tools/RenderingTools.h"
#include "MCPToolsLog.h"
#include "Tools/BatchToolHelper.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Engine/RendererSettings.h"
#include "HAL/IConsoleManager.h"
#include "Scalability.h"
#include "AssetToolsModule.h"
#include "AssetImportTask.h"
#include "Subsystems/EditorActorSubsystem.h"
#include "Engine/World.h"
#include "Editor.h"
#include "UObject/UObjectIterator.h"

static void SetCVarInt(const TCHAR* Name, int32 Value)
{
	if (IConsoleVariable* CV = IConsoleManager::Get().FindConsoleVariable(Name))
		CV->Set(Value, ECVF_SetByGameSetting);
}

static void SetCVarFloat(const TCHAR* Name, float Value)
{
	if (IConsoleVariable* CV = IConsoleManager::Get().FindConsoleVariable(Name))
		CV->Set(Value, ECVF_SetByGameSetting);
}

static int32 GetCVarInt(const TCHAR* Name, int32 Default = 0)
{
	if (const IConsoleVariable* CV = IConsoleManager::Get().FindConsoleVariable(Name))
		return CV->GetInt();
	return Default;
}

static float GetCVarFloat(const TCHAR* Name, float Default = 0.f)
{
	if (const IConsoleVariable* CV = IConsoleManager::Get().FindConsoleVariable(Name))
		return CV->GetFloat();
	return Default;
}

static FString MakeOkJson(const FString& Msg)
{
	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetBoolField(TEXT("success"), true);
	R->SetStringField(TEXT("message"), Msg);
	FString S; TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&S);
	FJsonSerializer::Serialize(R.ToSharedRef(), W); return S;
}

void RenderingTools::HandleConfigureLumen(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	bool bEnabled = true, bReflections = true;
	FString Quality;
	if (Args->HasField(TEXT("enabled")))     Args->TryGetBoolField(TEXT("enabled"), bEnabled);
	if (Args->HasField(TEXT("reflections"))) Args->TryGetBoolField(TEXT("reflections"), bReflections);
	Args->TryGetStringField(TEXT("quality"), Quality);

	URendererSettings* S = GetMutableDefault<URendererSettings>();

	S->DynamicGlobalIllumination = bEnabled
		? EDynamicGlobalIlluminationMethod::Lumen
		: EDynamicGlobalIlluminationMethod::None;
	SetCVarInt(TEXT("r.DynamicGlobalIlluminationMethod"), bEnabled ? 1 : 0);

	if (bEnabled)
	{
		S->Reflections = bReflections ? EReflectionMethod::Lumen : EReflectionMethod::ScreenSpace;
		SetCVarInt(TEXT("r.ReflectionMethod"), bReflections ? 1 : 2);
	}

	if (!Quality.IsEmpty())
	{
		int32 Q = Quality.Equals(TEXT("low"), ESearchCase::IgnoreCase) ? 0
			: Quality.Equals(TEXT("medium"), ESearchCase::IgnoreCase) ? 1
			: Quality.Equals(TEXT("high"), ESearchCase::IgnoreCase) ? 2 : 3;
		SetCVarFloat(TEXT("r.Lumen.FinalGather.Quality"), (float)(Q + 1));
		SetCVarFloat(TEXT("r.Lumen.Reflections.Quality"), (float)(Q + 1));
	}

	S->SaveConfig();
	UE_LOG(LogMCPTool, Log, TEXT("configure_lumen: enabled=%d reflections=%d"), bEnabled, bReflections);

	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetBoolField(TEXT("success"), true);
	R->SetBoolField(TEXT("lumen_enabled"), bEnabled);
	R->SetBoolField(TEXT("lumen_reflections"), bEnabled && bReflections);
	if (!Quality.IsEmpty()) R->SetStringField(TEXT("quality"), Quality);
	FString S2; TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&S2);
	FJsonSerializer::Serialize(R.ToSharedRef(), W); OutJson = S2;
}

void RenderingTools::HandleConfigureNanite(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	double MaxPixels = -1.0, ImposterMaxPixels = -1.0;
	Args->TryGetNumberField(TEXT("max_pixels_per_edge"), MaxPixels);
	Args->TryGetNumberField(TEXT("imposter_max_pixels"), ImposterMaxPixels);

	if (MaxPixels < 0.0 && ImposterMaxPixels < 0.0)
	{
		OutError = TEXT("Provide at least one of: max_pixels_per_edge, imposter_max_pixels");
		return;
	}

	if (MaxPixels >= 0.0) SetCVarFloat(TEXT("r.Nanite.MaxPixelsPerEdge"), (float)MaxPixels);
	if (ImposterMaxPixels >= 0.0) SetCVarFloat(TEXT("r.Nanite.Imposters.MaxPixelsPerEdge"), (float)ImposterMaxPixels);

	UE_LOG(LogMCPTool, Log, TEXT("configure_nanite: MaxPixelsPerEdge=%.2f"), (float)MaxPixels);

	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetBoolField(TEXT("success"), true);
	if (MaxPixels >= 0.0) R->SetNumberField(TEXT("max_pixels_per_edge"), MaxPixels);
	if (ImposterMaxPixels >= 0.0) R->SetNumberField(TEXT("imposter_max_pixels"), ImposterMaxPixels);
	FString S; TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&S);
	FJsonSerializer::Serialize(R.ToSharedRef(), W); OutJson = S;
}

void RenderingTools::HandleSetRayTracing(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	bool bEnabled = false;
	if (!Args->TryGetBoolField(TEXT("enabled"), bEnabled))
	{
		OutError = TEXT("enabled (bool) is required");
		return;
	}

	URendererSettings* S = GetMutableDefault<URendererSettings>();
	S->bEnableRayTracing = bEnabled ? 1 : 0;
	S->SaveConfig();

	UE_LOG(LogMCPTool, Log, TEXT("set_ray_tracing: enabled=%d (restart required)"), bEnabled);

	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetBoolField(TEXT("success"), true);
	R->SetBoolField(TEXT("ray_tracing_enabled"), bEnabled);
	R->SetStringField(TEXT("note"), TEXT("Engine restart required for ray tracing changes to take full effect"));
	FString S2; TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&S2);
	FJsonSerializer::Serialize(R.ToSharedRef(), W); OutJson = S2;
}

void RenderingTools::HandleConfigureGlobalIllumination(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	FString Method;
	double QualityLevel = -1.0;
	Args->TryGetStringField(TEXT("method"), Method);
	Args->TryGetNumberField(TEXT("quality_level"), QualityLevel);

	URendererSettings* S = GetMutableDefault<URendererSettings>();
	FString Applied;

	if (!Method.IsEmpty())
	{
		if (Method.Equals(TEXT("Lumen"), ESearchCase::IgnoreCase))
		{
			S->DynamicGlobalIllumination = EDynamicGlobalIlluminationMethod::Lumen;
			SetCVarInt(TEXT("r.DynamicGlobalIlluminationMethod"), 1);
		}
		else if (Method.Equals(TEXT("SSGI"), ESearchCase::IgnoreCase) || Method.Equals(TEXT("ScreenSpace"), ESearchCase::IgnoreCase))
		{
			S->DynamicGlobalIllumination = EDynamicGlobalIlluminationMethod::ScreenSpace;
			SetCVarInt(TEXT("r.DynamicGlobalIlluminationMethod"), 2);
		}
		else
		{
			S->DynamicGlobalIllumination = EDynamicGlobalIlluminationMethod::None;
			SetCVarInt(TEXT("r.DynamicGlobalIlluminationMethod"), 0);
		}
		Applied = Method;
		S->SaveConfig();
	}

	if (QualityLevel >= 0.0)
	{
		int32 Q = FMath::Clamp((int32)QualityLevel, 0, 4);
		SetCVarInt(TEXT("sg.GlobalIlluminationQuality"), Q);
	}

	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetBoolField(TEXT("success"), true);
	if (!Applied.IsEmpty()) R->SetStringField(TEXT("method"), Applied);
	if (QualityLevel >= 0.0) R->SetNumberField(TEXT("quality_level"), QualityLevel);
	FString Out; TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(R.ToSharedRef(), W); OutJson = Out;
}

void RenderingTools::HandleSetShadowQuality(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	double MaxRes = -1.0, DistScale = -1.0;
	int32 MaxCascades = -1;
	Args->TryGetNumberField(TEXT("max_resolution"), MaxRes);
	Args->TryGetNumberField(TEXT("max_cascades"), MaxRes);
	Args->TryGetNumberField(TEXT("distance_scale"), DistScale);

	double CascadesDouble = -1.0;
	if (Args->TryGetNumberField(TEXT("max_cascades"), CascadesDouble)) MaxCascades = (int32)CascadesDouble;

	if (MaxRes > 0.0)   SetCVarInt(TEXT("r.Shadow.MaxResolution"), (int32)MaxRes);
	if (MaxCascades > 0) SetCVarInt(TEXT("r.Shadow.CSM.MaxCascades"), MaxCascades);
	if (DistScale > 0.0) SetCVarFloat(TEXT("r.Shadow.DistanceScale"), (float)DistScale);

	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetBoolField(TEXT("success"), true);
	if (MaxRes > 0.0)     R->SetNumberField(TEXT("max_resolution"), MaxRes);
	if (MaxCascades > 0)  R->SetNumberField(TEXT("max_cascades"), MaxCascades);
	if (DistScale > 0.0)  R->SetNumberField(TEXT("distance_scale"), DistScale);
	FString S; TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&S);
	FJsonSerializer::Serialize(R.ToSharedRef(), W); OutJson = S;
}

void RenderingTools::HandleSetAntiAliasing(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	FString Method;
	if (!Args->TryGetStringField(TEXT("method"), Method) || Method.IsEmpty())
	{
		OutError = TEXT("method is required (TAA | TSR | FXAA | MSAA | None)");
		return;
	}

	URendererSettings* S = GetMutableDefault<URendererSettings>();
	int32 CVarVal = 2;

	if (Method.Equals(TEXT("None"), ESearchCase::IgnoreCase))      { S->DefaultFeatureAntiAliasing = AAM_None;       CVarVal = 0; }
	else if (Method.Equals(TEXT("FXAA"), ESearchCase::IgnoreCase)) { S->DefaultFeatureAntiAliasing = AAM_FXAA;       CVarVal = 1; }
	else if (Method.Equals(TEXT("TAA"), ESearchCase::IgnoreCase))  { S->DefaultFeatureAntiAliasing = AAM_TemporalAA; CVarVal = 2; }
	else if (Method.Equals(TEXT("MSAA"), ESearchCase::IgnoreCase)) { S->DefaultFeatureAntiAliasing = AAM_MSAA;       CVarVal = 3; }
	else if (Method.Equals(TEXT("TSR"), ESearchCase::IgnoreCase))  { S->DefaultFeatureAntiAliasing = AAM_TSR;        CVarVal = 4; }
	else { OutError = FString::Printf(TEXT("Unknown AA method '%s'. Use TAA | TSR | FXAA | MSAA | None"), *Method); return; }

	S->SaveConfig();
	SetCVarInt(TEXT("r.AntiAliasingMethod"), CVarVal);

	UE_LOG(LogMCPTool, Log, TEXT("set_anti_aliasing: method=%s"), *Method);
	OutJson = MakeOkJson(FString::Printf(TEXT("Anti-aliasing set to %s"), *Method));
}

void RenderingTools::HandleSetScreenPercentage(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	double Pct = 0.0;
	if (!Args->TryGetNumberField(TEXT("percentage"), Pct) || Pct <= 0.0)
		Args->TryGetNumberField(TEXT("screen_percentage"), Pct);
	if (Pct <= 0.0)
	{
		OutError = TEXT("percentage (float 10-200) is required");
		return;
	}
	Pct = FMath::Clamp(Pct, 10.0, 200.0);
	SetCVarFloat(TEXT("r.ScreenPercentage"), (float)Pct);
	OutJson = MakeOkJson(FString::Printf(TEXT("Screen percentage set to %.1f%%"), Pct));
}

void RenderingTools::HandleSetRenderingQuality(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	FString LevelStr;
	double LevelNum = -1.0;
	Args->TryGetStringField(TEXT("quality_level"), LevelStr);
	Args->TryGetNumberField(TEXT("quality_level"), LevelNum);

	int32 Level = -1;
	if (!LevelStr.IsEmpty())
	{
		if      (LevelStr.Equals(TEXT("Low"),        ESearchCase::IgnoreCase)) Level = 0;
		else if (LevelStr.Equals(TEXT("Medium"),     ESearchCase::IgnoreCase)) Level = 1;
		else if (LevelStr.Equals(TEXT("High"),       ESearchCase::IgnoreCase)) Level = 2;
		else if (LevelStr.Equals(TEXT("Epic"),       ESearchCase::IgnoreCase)) Level = 3;
		else if (LevelStr.Equals(TEXT("Cinematic"),  ESearchCase::IgnoreCase)) Level = 4;
	}
	if (Level < 0 && LevelNum >= 0.0) Level = FMath::Clamp((int32)LevelNum, 0, 4);
	if (Level < 0) { OutError = TEXT("quality_level required: Low|Medium|High|Epic|Cinematic (or 0-4)"); return; }

	Scalability::FQualityLevels Levels;
	Levels.SetFromSingleQualityLevel(Level);
	Scalability::SetQualityLevels(Levels);

	static const TCHAR* Names[] = { TEXT("Low"), TEXT("Medium"), TEXT("High"), TEXT("Epic"), TEXT("Cinematic") };
	UE_LOG(LogMCPTool, Log, TEXT("set_rendering_quality: %s (%d)"), Names[Level], Level);
	OutJson = MakeOkJson(FString::Printf(TEXT("Rendering quality set to %s"), Names[Level]));
}

void RenderingTools::HandleGetRenderSettings(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	const URendererSettings* S = GetDefault<URendererSettings>();
	Scalability::FQualityLevels Levels = Scalability::GetQualityLevels();

	static const TCHAR* GINames[] = { TEXT("None"), TEXT("Lumen"), TEXT("ScreenSpace"), TEXT("Plugin") };
	static const TCHAR* RefNames[] = { TEXT("None"), TEXT("Lumen"), TEXT("ScreenSpace") };
	static const TCHAR* AANames[]  = { TEXT("None"), TEXT("FXAA"), TEXT("TAA"), TEXT("MSAA"), TEXT("TSR") };

	int32 GIIdx = (int32)S->DynamicGlobalIllumination.GetValue();
	int32 RefIdx = (int32)S->Reflections.GetValue();
	int32 AAIdx  = (int32)S->DefaultFeatureAntiAliasing.GetValue();

	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetBoolField(TEXT("success"), true);
	R->SetStringField(TEXT("global_illumination_method"), GIIdx >= 0 && GIIdx < 4 ? GINames[GIIdx] : TEXT("Unknown"));
	R->SetStringField(TEXT("reflection_method"),          RefIdx >= 0 && RefIdx < 3 ? RefNames[RefIdx] : TEXT("Unknown"));
	R->SetBoolField(TEXT("ray_tracing_enabled"),          S->bEnableRayTracing != 0);
	R->SetStringField(TEXT("anti_aliasing"),              AAIdx >= 0 && AAIdx < 5 ? AANames[AAIdx] : TEXT("Unknown"));
	R->SetNumberField(TEXT("screen_percentage"),          GetCVarFloat(TEXT("r.ScreenPercentage"), 100.f));
	R->SetNumberField(TEXT("shadow_max_resolution"),      (double)GetCVarInt(TEXT("r.Shadow.MaxResolution"), 2048));
	R->SetNumberField(TEXT("shadow_max_cascades"),        (double)GetCVarInt(TEXT("r.Shadow.CSM.MaxCascades"), 3));
	R->SetNumberField(TEXT("nanite_max_pixels_per_edge"), (double)GetCVarFloat(TEXT("r.Nanite.MaxPixelsPerEdge"), 1.f));
	R->SetNumberField(TEXT("scalability_gi_quality"),     (double)Levels.GlobalIlluminationQuality);
	R->SetNumberField(TEXT("scalability_shadow_quality"), (double)Levels.ShadowQuality);
	R->SetNumberField(TEXT("scalability_aa_quality"),     (double)Levels.AntiAliasingQuality);

	FString Out; TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(R.ToSharedRef(), W); OutJson = Out;
}

static void CreateIESProfileSingle(const TSharedPtr<FJsonObject>& Item, FString& OutJson, FString& OutError)
{
	FString Name, FilePath, SavePath;
	Item->TryGetStringField(TEXT("name"), Name);
	Item->TryGetStringField(TEXT("file_path"), FilePath);
	Item->TryGetStringField(TEXT("save_path"), SavePath);

	if (Name.IsEmpty() || FilePath.IsEmpty() || SavePath.IsEmpty())
	{
		OutError = TEXT("name, file_path, and save_path are required");
		return;
	}

	if (!FPaths::FileExists(FilePath))
	{
		OutError = FString::Printf(TEXT("IES file not found: %s"), *FilePath);
		return;
	}

	IAssetTools& AT = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
	UAssetImportTask* Task = NewObject<UAssetImportTask>();
	Task->Filename = FilePath;
	Task->DestinationPath = SavePath;
	Task->DestinationName = Name;
	Task->bReplaceExisting = true;
	Task->bAutomated = true;
	Task->bSave = true;

	TArray<UAssetImportTask*> Tasks = { Task };
	AT.ImportAssetTasks(Tasks);

	if (Task->ImportedObjectPaths.Num() == 0)
	{
		OutError = FString::Printf(TEXT("IES import failed for: %s"), *FilePath);
		return;
	}

	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetBoolField(TEXT("success"), true);
	R->SetStringField(TEXT("asset_path"), Task->ImportedObjectPaths[0]);
	R->SetStringField(TEXT("name"), Name);
	FString S; TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&S);
	FJsonSerializer::Serialize(R.ToSharedRef(), W); OutJson = S;
}

void RenderingTools::HandleCreateIESProfileFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("profiles"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString IOut, IErr;
			CreateIESProfileSingle(Item, IOut, IErr);
			if (IErr.IsEmpty()) { auto E = MakeShared<FJsonObject>(); FString N; Item->TryGetStringField(TEXT("name"), N); E->SetStringField(TEXT("name"), N); Batch.AddSuccess(i, E); }
			else Batch.AddFailure(i, IErr);
		}
		Batch.Finalize(OutJson);
		return;
	}
	CreateIESProfileSingle(Args, OutJson, OutError);
}

static void SpawnHDRIBackdropSingle(const TSharedPtr<FJsonObject>& Item, FString& OutJson, FString& OutError)
{
	FString ActorLabel, CubemapPath;
	double LocX = 0, LocY = 0, LocZ = 0, Intensity = 1.0, Size = 1000.0;
	Item->TryGetStringField(TEXT("actor_label"), ActorLabel);
	Item->TryGetStringField(TEXT("cubemap_path"), CubemapPath);
	Item->TryGetNumberField(TEXT("location_x"), LocX);
	Item->TryGetNumberField(TEXT("location_y"), LocY);
	Item->TryGetNumberField(TEXT("location_z"), LocZ);
	Item->TryGetNumberField(TEXT("intensity"), Intensity);
	Item->TryGetNumberField(TEXT("size"), Size);

	UClass* HDRIClass = FindObject<UClass>(nullptr, TEXT("/Script/HDRIBackdrop.HDRIBackdrop"));
	if (!HDRIClass)
	{
		OutError = TEXT("HDRIBackdrop plugin not enabled. Enable it in Plugins > Rendering > HDRI Backdrop.");
		return;
	}

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World) { OutError = TEXT("No editor world available"); return; }

	FVector Location((float)LocX, (float)LocY, (float)LocZ);
	FRotator Rotation = FRotator::ZeroRotator;
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AActor* Actor = World->SpawnActor<AActor>(HDRIClass, Location, Rotation, Params);
	if (!Actor) { OutError = TEXT("Failed to spawn HDRIBackdrop actor"); return; }

	if (!ActorLabel.IsEmpty()) Actor->SetActorLabel(*ActorLabel);

	auto SetProp = [&](const TCHAR* PropName, float Val) {
		if (FFloatProperty* Prop = FindFProperty<FFloatProperty>(HDRIClass, PropName))
			Prop->SetPropertyValue_InContainer(Actor, Val);
	};
	SetProp(TEXT("Intensity"), (float)Intensity);
	SetProp(TEXT("Size"), (float)Size);

	if (!CubemapPath.IsEmpty())
	{
		UObject* Cubemap = StaticLoadObject(UObject::StaticClass(), nullptr, *CubemapPath);
		if (Cubemap)
		{
			if (FObjectPropertyBase* TexProp = FindFProperty<FObjectPropertyBase>(HDRIClass, TEXT("Texture")))
				TexProp->SetObjectPropertyValue_InContainer(Actor, Cubemap);
		}
	}

	UE_LOG(LogMCPTool, Log, TEXT("spawn_hdri_backdrop: %s"), *Actor->GetActorLabel());

	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetBoolField(TEXT("success"), true);
	R->SetStringField(TEXT("actor_label"), Actor->GetActorLabel());
	R->SetStringField(TEXT("actor_path"), Actor->GetPathName());
	FString S; TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&S);
	FJsonSerializer::Serialize(R.ToSharedRef(), W); OutJson = S;
}

void RenderingTools::HandleSpawnHDRIBackdropFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("backdrops"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString IOut, IErr;
			SpawnHDRIBackdropSingle(Item, IOut, IErr);
			if (IErr.IsEmpty()) { auto E = MakeShared<FJsonObject>(); FString L; Item->TryGetStringField(TEXT("actor_label"), L); E->SetStringField(TEXT("actor_label"), L); Batch.AddSuccess(i, E); }
			else Batch.AddFailure(i, IErr);
		}
		Batch.Finalize(OutJson);
		return;
	}
	SpawnHDRIBackdropSingle(Args, OutJson, OutError);
}
