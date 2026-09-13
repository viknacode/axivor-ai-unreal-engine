// Copyright 2026, BlueprintsLab, All rights reserved.

#include "Tools/EnvironmentTools.h"
#include "Managers/EditorProfileSync.h"

#include "AssetToolsModule.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "VT/RuntimeVirtualTexture.h"
#include "MCPToolsLog.h"

#include "Engine/SkyLight.h"
#include "Components/SkyLightComponent.h"
#include "Engine/ExponentialHeightFog.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Engine/DirectionalLight.h"
#include "Components/DirectionalLightComponent.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "Editor.h"
#include "Components/SceneComponent.h"
#include "Components/BoxComponent.h"
#include "UObject/UObjectGlobals.h"

#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "EditorAssetLibrary.h"
#include "Materials/MaterialParameterCollection.h"

namespace EnvironmentTools
{

static UWorld* GetEditorWorld()
{
	return GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
}

static FLinearColor ParseLinearColor(const FString& ColorStr, bool& bParsed)
{
	bParsed = false;
	if (ColorStr.IsEmpty()) return FLinearColor::White;
	TArray<FString> Parts;
	ColorStr.ParseIntoArray(Parts, TEXT(","));
	if (Parts.Num() >= 3)
	{
		bParsed = true;
		float R = FCString::Atof(*Parts[0].TrimStartAndEnd());
		float G = FCString::Atof(*Parts[1].TrimStartAndEnd());
		float B = FCString::Atof(*Parts[2].TrimStartAndEnd());
		float A = Parts.Num() >= 4 ? FCString::Atof(*Parts[3].TrimStartAndEnd()) : 1.f;
		if (R > 1.f || G > 1.f || B > 1.f)
		{
			R /= 255.f; G /= 255.f; B /= 255.f;
		}
		return FLinearColor(R, G, B, A);
	}
	return FLinearColor::White;
}

template<typename T>
static T* FindActorByLabel(UWorld* World, const FString& Label)
{
	for (TActorIterator<T> It(World); It; ++It)
	{
		if (Label.IsEmpty() || It->GetActorLabel().Equals(Label, ESearchCase::IgnoreCase))
			return *It;
	}
	return nullptr;
}

static FString MakeSuccessJson(const FString& ToolName, const FString& ActorLabel)
{
	TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject());
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("tool"), ToolName);
	Obj->SetStringField(TEXT("actor_label"), ActorLabel);
	FString Out;
	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Obj.ToSharedRef(), W);
	return Out;
}

void HandleSetSkyLightProperties(const FString& ActorLabel, float Intensity, const FString& LightColor,
	const FString& SourceType, const FString& Mobility, FString& OutJsonString, FString& OutError)
{
	UWorld* World = GetEditorWorld();
	if (!World) { OutError = TEXT("No editor world"); return; }

	ASkyLight* SkyLight = FindActorByLabel<ASkyLight>(World, ActorLabel);
	if (!SkyLight) { OutError = FString::Printf(TEXT("No SkyLight found%s"), ActorLabel.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" with label '%s'"), *ActorLabel)); return; }

	USkyLightComponent* SLC = SkyLight->GetLightComponent();
	if (!SLC) { OutError = TEXT("SkyLight has no LightComponent"); return; }

	if (Intensity >= 0.f)
		SLC->SetIntensity(Intensity);

	if (!LightColor.IsEmpty())
	{
		bool bParsed;
		FLinearColor Col = ParseLinearColor(LightColor, bParsed);
		if (bParsed) SLC->SetLightColor(Col);
	}

	if (!SourceType.IsEmpty())
	{
		if (SourceType.Contains(TEXT("cubemap"), ESearchCase::IgnoreCase))
			SLC->SourceType = ESkyLightSourceType::SLS_SpecifiedCubemap;
		else
			SLC->SourceType = ESkyLightSourceType::SLS_CapturedScene;
	}

	if (!Mobility.IsEmpty())
	{
		if (Mobility.Equals(TEXT("movable"), ESearchCase::IgnoreCase))
			SLC->SetMobility(EComponentMobility::Movable);
		else if (Mobility.Equals(TEXT("stationary"), ESearchCase::IgnoreCase))
			SLC->SetMobility(EComponentMobility::Stationary);
		else
			SLC->SetMobility(EComponentMobility::Static);
	}

	SLC->MarkRenderStateDirty();
	SkyLight->MarkPackageDirty();

	OutJsonString = MakeSuccessJson(TEXT("set_sky_light_properties"), SkyLight->GetActorLabel());
}

void HandleSetExponentialHeightFogProperties(const FString& ActorLabel, float FogDensity, float HeightFalloff,
	const FString& InscatteringColor, float StartDistance, float MaxOpacity, bool bVolumetricFog,
	bool bDensitySet, bool bFalloffSet, bool bStartDistSet, bool bMaxOpacitySet, bool bVolumetricSet,
	FString& OutJsonString, FString& OutError)
{
	UWorld* World = GetEditorWorld();
	if (!World) { OutError = TEXT("No editor world"); return; }

	AExponentialHeightFog* Fog = FindActorByLabel<AExponentialHeightFog>(World, ActorLabel);
	if (!Fog) { OutError = FString::Printf(TEXT("No ExponentialHeightFog found%s"), ActorLabel.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" with label '%s'"), *ActorLabel)); return; }

	UExponentialHeightFogComponent* FC = Fog->GetComponent();
	if (!FC) { OutError = TEXT("Fog has no component"); return; }

	if (bDensitySet)    FC->SetFogDensity(FogDensity);
	if (bFalloffSet)    FC->SetFogHeightFalloff(HeightFalloff);
	if (bStartDistSet)  FC->SetStartDistance(StartDistance);
	if (bMaxOpacitySet) FC->SetFogMaxOpacity(MaxOpacity);
	if (bVolumetricSet) FC->SetVolumetricFog(bVolumetricFog);

	if (!InscatteringColor.IsEmpty())
	{
		bool bParsed;
		FLinearColor Col = ParseLinearColor(InscatteringColor, bParsed);
		if (bParsed) FC->SetFogInscatteringColor(Col);
	}

	Fog->MarkPackageDirty();

	OutJsonString = MakeSuccessJson(TEXT("set_exponential_height_fog_properties"), Fog->GetActorLabel());
}

static bool SetPropertyOnObject(UObject* Obj, const FString& PropName, const FString& PropValue, FString& OutError);

static bool SetActorPropertyByReflection(AActor* Actor, const FString& PropName, const FString& PropValue, FString& OutError)
{
	UClass* Class = Actor->GetClass();
	FProperty* Prop = FindFProperty<FProperty>(Class, *PropName);
	if (!Prop)
	{
		for (TFieldIterator<FProperty> It(Class); It; ++It)
			if (It->GetName().Equals(PropName, ESearchCase::IgnoreCase)) { Prop = *It; break; }
	}
	if (Prop)
		return SetPropertyOnObject(Actor, PropName, PropValue, OutError);

	TArray<UActorComponent*> Components;
	Actor->GetComponents(Components);
	for (UActorComponent* Comp : Components)
	{
		FProperty* CP = FindFProperty<FProperty>(Comp->GetClass(), *PropName);
		if (!CP)
			for (TFieldIterator<FProperty> It(Comp->GetClass()); It; ++It)
				if (It->GetName().Equals(PropName, ESearchCase::IgnoreCase)) { CP = *It; break; }
		if (CP)
			return SetPropertyOnObject(Comp, PropName, PropValue, OutError);
	}

	OutError = FString::Printf(TEXT("Property '%s' not found on %s or its components"), *PropName, *Class->GetName());
	return false;
}

static bool SetPropertyOnObject(UObject* Obj, const FString& PropName, const FString& PropValue, FString& OutError)
{
	UClass* Class = Obj->GetClass();
	FProperty* Prop = FindFProperty<FProperty>(Class, *PropName);
	if (!Prop)
		for (TFieldIterator<FProperty> It(Class); It; ++It)
			if (It->GetName().Equals(PropName, ESearchCase::IgnoreCase)) { Prop = *It; break; }
	if (!Prop) { OutError = FString::Printf(TEXT("Property '%s' not found on %s"), *PropName, *Class->GetName()); return false; }

	void* Container = Obj;

	if (FFloatProperty* FP = CastField<FFloatProperty>(Prop))
		FP->SetPropertyValue_InContainer(Container, FCString::Atof(*PropValue));
	else if (FDoubleProperty* DP = CastField<FDoubleProperty>(Prop))
		DP->SetPropertyValue_InContainer(Container, FCString::Atod(*PropValue));
	else if (FBoolProperty* BP = CastField<FBoolProperty>(Prop))
		BP->SetPropertyValue_InContainer(Container, PropValue.Equals(TEXT("true"), ESearchCase::IgnoreCase) || PropValue == TEXT("1"));
	else if (FStructProperty* SP = CastField<FStructProperty>(Prop))
	{
		if (SP->Struct == TBaseStructure<FLinearColor>::Get())
		{
			bool bParsed;
			FLinearColor Col = ParseLinearColor(PropValue, bParsed);
			if (bParsed) *SP->ContainerPtrToValuePtr<FLinearColor>(Container) = Col;
		}
		else if (SP->Struct == TBaseStructure<FColor>::Get())
		{
			bool bParsed;
			FLinearColor LC = ParseLinearColor(PropValue, bParsed);
			if (bParsed) *SP->ContainerPtrToValuePtr<FColor>(Container) = LC.ToFColor(true);
		}
		else { OutError = FString::Printf(TEXT("Unsupported struct type for property '%s'"), *PropName); return false; }
	}
	else
	{
		OutError = FString::Printf(TEXT("Unsupported property type for '%s'"), *PropName);
		return false;
	}
	if (USceneComponent* SC = Cast<USceneComponent>(Obj))
		SC->MarkRenderStateDirty();
	Obj->MarkPackageDirty();
	return true;
}

static AActor* FindActorByClassPath(UWorld* World, const FString& ClassScriptPath, const FString& Label)
{
	UClass* ActorClass = FindObject<UClass>(nullptr, *ClassScriptPath);
	if (!ActorClass) return nullptr;
	for (TActorIterator<AActor> It(World, ActorClass); It; ++It)
	{
		if (Label.IsEmpty() || It->GetActorLabel().Equals(Label, ESearchCase::IgnoreCase))
			return *It;
	}
	return nullptr;
}

void HandleSetSkyAtmosphereProperties(const FString& ActorLabel, const FString& PropertyName,
	const FString& PropertyValue, FString& OutJsonString, FString& OutError)
{
	UWorld* World = GetEditorWorld();
	if (!World) { OutError = TEXT("No editor world"); return; }

	AActor* Atmo = FindActorByClassPath(World, TEXT("/Script/Engine.SkyAtmosphere"), ActorLabel);
	if (!Atmo) { OutError = TEXT("No SkyAtmosphere actor found in level"); return; }

	if (!SetActorPropertyByReflection(Atmo, PropertyName, PropertyValue, OutError)) return;

	Atmo->MarkPackageDirty();
	OutJsonString = MakeSuccessJson(TEXT("set_sky_atmosphere_properties"), Atmo->GetActorLabel());
}

void HandleSetVolumetricCloudProperties(const FString& ActorLabel, const FString& PropertyName,
	const FString& PropertyValue, FString& OutJsonString, FString& OutError)
{
	UWorld* World = GetEditorWorld();
	if (!World) { OutError = TEXT("No editor world"); return; }

	AActor* Cloud = FindActorByClassPath(World, TEXT("/Script/Engine.VolumetricCloud"), ActorLabel);
	if (!Cloud) { OutError = TEXT("No VolumetricCloud actor found in level"); return; }

	if (!SetActorPropertyByReflection(Cloud, PropertyName, PropertyValue, OutError)) return;

	Cloud->MarkPackageDirty();
	OutJsonString = MakeSuccessJson(TEXT("set_volumetric_cloud_properties"), Cloud->GetActorLabel());
}

void HandleSpawnEnvironmentActor(const FString& ActorType, const FString& ActorLabel,
	float LocationX, float LocationY, float LocationZ,
	FString& OutJsonString, FString& OutError)
{

	UWorld* World = GetEditorWorld();
	if (!World) { OutError = TEXT("No editor world"); return; }

	UClass* ActorClass = nullptr;
	FString TypeLower = ActorType.ToLower();
	if (TypeLower.Contains(TEXT("sky_light")) || TypeLower.Contains(TEXT("skylight")))
		ActorClass = ASkyLight::StaticClass();
	else if (TypeLower.Contains(TEXT("fog")) || TypeLower.Contains(TEXT("height_fog")))
		ActorClass = AExponentialHeightFog::StaticClass();
	else if (TypeLower.Contains(TEXT("atmosphere")) || TypeLower.Contains(TEXT("sky_atmosphere")))
	{
		ActorClass = FindObject<UClass>(nullptr, TEXT("/Script/Engine.SkyAtmosphere"));
		if (!ActorClass) ActorClass = FindFirstObject<UClass>(TEXT("SkyAtmosphere"));
	}
	else if (TypeLower.Contains(TEXT("cloud")) || TypeLower.Contains(TEXT("volumetric")))
	{
		ActorClass = FindObject<UClass>(nullptr, TEXT("/Script/Engine.VolumetricCloud"));
		if (!ActorClass) ActorClass = FindFirstObject<UClass>(TEXT("VolumetricCloud"));
	}
	else if (TypeLower.Contains(TEXT("directional")) || TypeLower.Contains(TEXT("sun")))
	{
		ActorClass = FindObject<UClass>(nullptr, TEXT("/Script/Engine.DirectionalLight"));
		if (!ActorClass) ActorClass = FindFirstObject<UClass>(TEXT("DirectionalLight"));
	}
	else if (TypeLower.Contains(TEXT("reflection")) || TypeLower.Contains(TEXT("sphere_reflection")))
	{
		ActorClass = FindObject<UClass>(nullptr, TEXT("/Script/Engine.SphereReflectionCapture"));
		if (!ActorClass) ActorClass = FindFirstObject<UClass>(TEXT("SphereReflectionCapture"));
	}

	if (!ActorClass) { OutError = FString::Printf(TEXT("Unknown environment actor type: '%s'. Use: sky_light, fog, atmosphere, volumetric_cloud, directional_light, reflection_capture"), *ActorType); return; }

	FVector Location(LocationX, LocationY, LocationZ);
	FRotator Rotation = FRotator::ZeroRotator;
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AActor* NewActor = World->SpawnActor(ActorClass, &Location, &Rotation, Params);
	if (!NewActor) { OutError = FString::Printf(TEXT("Failed to spawn %s"), *ActorType); return; }

	if (!ActorLabel.IsEmpty())
		NewActor->SetActorLabel(ActorLabel);

	NewActor->MarkPackageDirty();

	TSharedPtr<FJsonObject> Res = MakeShareable(new FJsonObject());
	Res->SetBoolField(TEXT("success"), true);
	Res->SetStringField(TEXT("actor_label"), NewActor->GetActorLabel());
	Res->SetStringField(TEXT("actor_class"), ActorClass->GetName());
	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Res.ToSharedRef(), W);
}

void HandleGetEnvironmentSummary(FString& OutJsonString, FString& OutError)
{

	UWorld* World = GetEditorWorld();
	if (!World) { OutError = TEXT("No editor world"); return; }

	TSharedPtr<FJsonObject> Res = MakeShareable(new FJsonObject());
	Res->SetBoolField(TEXT("success"), true);

	TArray<TSharedPtr<FJsonValue>> ActorsArray;

	static const TArray<FString> EnvClasses = {
		TEXT("SkyLight"), TEXT("ExponentialHeightFog"), TEXT("SkyAtmosphere"),
		TEXT("VolumetricCloud"), TEXT("DirectionalLight"), TEXT("SphereReflectionCapture"),
		TEXT("BoxReflectionCapture"), TEXT("PostProcessVolume")
	};

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor) continue;

		FString ClassName = Actor->GetClass()->GetName();
		bool bIsEnv = false;
		for (const FString& EC : EnvClasses)
		{
			if (ClassName.Contains(EC)) { bIsEnv = true; break; }
		}
		if (!bIsEnv) continue;

		TSharedPtr<FJsonObject> ActorObj = MakeShareable(new FJsonObject());
		ActorObj->SetStringField(TEXT("label"), Actor->GetActorLabel());
		ActorObj->SetStringField(TEXT("class"), ClassName);
		FVector Loc = Actor->GetActorLocation();
		ActorObj->SetStringField(TEXT("location"), FString::Printf(TEXT("%.1f,%.1f,%.1f"), Loc.X, Loc.Y, Loc.Z));
		ActorsArray.Add(MakeShareable(new FJsonValueObject(ActorObj)));
	}

	Res->SetArrayField(TEXT("environment_actors"), ActorsArray);
	Res->SetNumberField(TEXT("count"), ActorsArray.Num());

	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Res.ToSharedRef(), W);
}

void HandleSetMPCParameterValue(const FString& AssetPath, const FString& ParamName,
	float ScalarValue, float VectorR, float VectorG, float VectorB, float VectorA,
	bool bIsVector, FString& OutJsonString, FString& OutError)
{

	UMaterialParameterCollection* MPC = Cast<UMaterialParameterCollection>(UEditorAssetLibrary::LoadAsset(AssetPath));
	if (!MPC) { OutError = FString::Printf(TEXT("Could not load MPC at '%s'"), *AssetPath); return; }

	bool bFound = false;

	if (bIsVector)
	{
		for (FCollectionVectorParameter& Param : MPC->VectorParameters)
		{
			if (Param.ParameterName.ToString().Equals(ParamName, ESearchCase::IgnoreCase))
			{
				Param.DefaultValue = FLinearColor(VectorR, VectorG, VectorB, VectorA);
				bFound = true;
				break;
			}
		}
	}
	else
	{
		for (FCollectionScalarParameter& Param : MPC->ScalarParameters)
		{
			if (Param.ParameterName.ToString().Equals(ParamName, ESearchCase::IgnoreCase))
			{
				Param.DefaultValue = ScalarValue;
				bFound = true;
				break;
			}
		}
	}

	if (!bFound) { OutError = FString::Printf(TEXT("Parameter '%s' not found in MPC '%s'"), *ParamName, *AssetPath); return; }

	MPC->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(AssetPath, false);

	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"mpc_path\":\"%s\",\"parameter\":\"%s\"}"), *AssetPath, *ParamName);
}

void HandleSetDirectionalLightProperties(const FString& ActorLabel, float Intensity,
	const FString& LightColor, float RotationPitch, float RotationYaw,
	const FString& Mobility, bool bCastShadows,
	bool bIntensitySet, bool bColorSet, bool bRotationSet, bool bMobilitySet, bool bShadowsSet,
	FString& OutJsonString, FString& OutError)
{

	UWorld* World = GetEditorWorld();
	if (!World) { OutError = TEXT("No editor world"); return; }

	ADirectionalLight* DirLight = FindActorByLabel<ADirectionalLight>(World, ActorLabel);
	if (!DirLight) { OutError = FString::Printf(TEXT("No DirectionalLight found%s"), ActorLabel.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" with label '%s'"), *ActorLabel)); return; }

	UDirectionalLightComponent* DLC = Cast<UDirectionalLightComponent>(DirLight->GetLightComponent());
	if (!DLC) { OutError = TEXT("DirectionalLight has no LightComponent"); return; }

	if (bIntensitySet)
		DLC->SetIntensity(Intensity);

	if (bColorSet)
	{
		bool bParsed;
		FLinearColor Col = ParseLinearColor(LightColor, bParsed);
		if (bParsed) DLC->SetLightColor(Col);
	}

	if (bRotationSet)
	{
		FRotator NewRot(RotationPitch, RotationYaw, 0.f);
		DirLight->SetActorRotation(NewRot);
	}

	if (bMobilitySet)
	{
		if (Mobility.Equals(TEXT("movable"), ESearchCase::IgnoreCase))
			DLC->SetMobility(EComponentMobility::Movable);
		else if (Mobility.Equals(TEXT("stationary"), ESearchCase::IgnoreCase))
			DLC->SetMobility(EComponentMobility::Stationary);
		else
			DLC->SetMobility(EComponentMobility::Static);
	}

	if (bShadowsSet)
		DLC->SetCastShadows(bCastShadows);

	DLC->MarkRenderStateDirty();
	DirLight->MarkPackageDirty();

	OutJsonString = MakeSuccessJson(TEXT("set_directional_light_properties"), DirLight->GetActorLabel());
}

void HandleCreateRuntimeVirtualTexture(const FString& Name, const FString& SavePath,
	int32 TileSize, const FString& MaterialType,
	FString& OutJsonString, FString& OutError)
{
	if (Name.IsEmpty()) { OutError = TEXT("name is required"); return; }

	const FString TargetPath = SavePath.IsEmpty() ? TEXT("/Game/Textures/RVT") : SavePath;
	const FString FullPath   = TargetPath + TEXT("/") + Name;

	if (UEditorAssetLibrary::DoesAssetExist(FullPath))
	{
		OutError = FString::Printf(TEXT("Asset already exists at: '%s'"), *FullPath);
		return;
	}

	const int32 ActualSize = TileSize > 0 ? TileSize : 256;
	const int32 TileSizeIndex = FMath::Clamp(FMath::RoundToInt(FMath::Log2((float)FMath::Clamp(ActualSize, 64, 1024))) - 6, 0, 4);

	FString PackageName;
	FPackageName::TryConvertFilenameToLongPackageName(TargetPath + TEXT("/") + Name, PackageName);
	PackageName = TargetPath + TEXT("/") + Name;
	UPackage* Pkg = CreatePackage(*PackageName);
	URuntimeVirtualTexture* RVT = NewObject<URuntimeVirtualTexture>(Pkg, *Name, RF_Public | RF_Standalone | RF_Transactional);

	if (!RVT)
	{
		OutError = TEXT("Failed to create Runtime Virtual Texture asset.");
		return;
	}

	RVT->Modify();

	if (FIntProperty* TSProp = FindFProperty<FIntProperty>(URuntimeVirtualTexture::StaticClass(), TEXT("TileSize")))
		TSProp->SetPropertyValue_InContainer(RVT, TileSizeIndex);

	if (!MaterialType.IsEmpty())
	{
		ERuntimeVirtualTextureMaterialType Type = ERuntimeVirtualTextureMaterialType::BaseColor;
		if (MaterialType.Equals(TEXT("BaseColor_Normal_Roughness"), ESearchCase::IgnoreCase))
			Type = ERuntimeVirtualTextureMaterialType::BaseColor_Normal_Roughness;
		else if (MaterialType.Equals(TEXT("BaseColor_Normal_Specular"), ESearchCase::IgnoreCase))
			Type = ERuntimeVirtualTextureMaterialType::BaseColor_Normal_Specular;
		else if (MaterialType.Equals(TEXT("WorldHeight"), ESearchCase::IgnoreCase))
			Type = ERuntimeVirtualTextureMaterialType::WorldHeight;
		else if (MaterialType.Equals(TEXT("BaseColor_Normal_Specular_Mask_YCoCg"), ESearchCase::IgnoreCase))
			Type = ERuntimeVirtualTextureMaterialType::BaseColor_Normal_Specular_Mask_YCoCg;

		if (FProperty* MTProp = FindFProperty<FProperty>(URuntimeVirtualTexture::StaticClass(), TEXT("MaterialType")))
		{
			if (FByteProperty* BP = CastField<FByteProperty>(MTProp))
				*BP->ContainerPtrToValuePtr<uint8>(RVT) = (uint8)Type;
			else if (FEnumProperty* EP = CastField<FEnumProperty>(MTProp))
			{
				void* VP = EP->ContainerPtrToValuePtr<void>(RVT);
				EP->GetUnderlyingProperty()->SetIntPropertyValue(VP, (int64)Type);
			}
		}
	}

	RVT->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(RVT);

	const int32 ActualTilePx = 1 << (TileSizeIndex + 6);
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("path"), RVT->GetPathName());
	Result->SetNumberField(TEXT("tile_size_px"), (double)ActualTilePx);
	Result->SetStringField(TEXT("message"),
		FString::Printf(TEXT("Created Runtime Virtual Texture '%s' at '%s'"), *Name, *TargetPath));

	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
}

void HandleSpawnRVTVolume(const FString& ActorLabel, const FString& RVTAssetPath,
	float LocationX, float LocationY, float LocationZ,
	float ExtentX, float ExtentY, float ExtentZ,
	FString& OutJsonString, FString& OutError)
{
	UWorld* World = GetEditorWorld();
	if (!World) { OutError = TEXT("No editor world"); return; }

	UClass* RVTVolumeClass = FindObject<UClass>(nullptr,
		TEXT("/Script/Engine.RuntimeVirtualTextureVolume"));
	if (!RVTVolumeClass)
	{
		OutError = TEXT("RuntimeVirtualTextureVolume class not found — ensure the Engine module is loaded.");
		return;
	}

	FVector Location(LocationX, LocationY, LocationZ);
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AActor* Spawned = World->SpawnActor<AActor>(RVTVolumeClass, Location, FRotator::ZeroRotator, SpawnParams);
	if (!Spawned) { OutError = TEXT("Failed to spawn RuntimeVirtualTextureVolume"); return; }

	if (!ActorLabel.IsEmpty()) Spawned->SetActorLabel(ActorLabel);

	if (ExtentX > 0.0f || ExtentY > 0.0f || ExtentZ > 0.0f)
	{
		UBoxComponent* Box = Spawned->FindComponentByClass<UBoxComponent>();
		if (Box)
		{
			FVector Extent(ExtentX > 0 ? ExtentX : 500.f,
				           ExtentY > 0 ? ExtentY : 500.f,
				           ExtentZ > 0 ? ExtentZ : 500.f);
			Box->SetBoxExtent(Extent);
		}
	}

	if (!RVTAssetPath.IsEmpty())
	{
		URuntimeVirtualTexture* RVTAsset = Cast<URuntimeVirtualTexture>(
			UEditorAssetLibrary::LoadAsset(RVTAssetPath));
		if (RVTAsset)
		{
			for (UActorComponent* Comp : Spawned->GetComponents())
			{
				if (!Comp) continue;
				if (FObjectProperty* Prop = FindFProperty<FObjectProperty>(
					Comp->GetClass(), TEXT("VirtualTexture")))
				{
					Prop->SetObjectPropertyValue(Prop->ContainerPtrToValuePtr<void>(Comp), RVTAsset);
					Comp->MarkRenderStateDirty();
					break;
				}
			}
		}
		else
		{
			UE_LOG(LogMCPTool, Warning, TEXT("spawn_rvt_volume: RVT asset not found at '%s'"), *RVTAssetPath);
		}
	}

	Spawned->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("actor_label"), Spawned->GetActorLabel());
	Result->SetStringField(TEXT("message"),
		FString::Printf(TEXT("Spawned RuntimeVirtualTextureVolume '%s'"), *Spawned->GetActorLabel()));

	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
}

static FString ReadScalarAsString(const TSharedPtr<FJsonObject>& Args, const FString& Key)
{
	FString StrVal;
	if (Args->TryGetStringField(Key, StrVal)) return StrVal;
	double NumVal = 0.0;
	if (Args->TryGetNumberField(Key, NumVal)) return FString::Printf(TEXT("%g"), NumVal);
	bool BoolVal = false;
	if (Args->TryGetBoolField(Key, BoolVal)) return BoolVal ? TEXT("true") : TEXT("false");
	return FString();
}

void HandleSetSkyLightPropertiesFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString ActorLabel, LightColor, SourceType, Mobility;
	double Intensity = -1.0;
	Args->TryGetStringField(TEXT("actor_label"), ActorLabel);
	Args->TryGetNumberField(TEXT("intensity"), Intensity);
	Args->TryGetStringField(TEXT("light_color"), LightColor);
	Args->TryGetStringField(TEXT("source_type"), SourceType);
	Args->TryGetStringField(TEXT("mobility"), Mobility);
	HandleSetSkyLightProperties(ActorLabel, (float)Intensity, LightColor, SourceType, Mobility, OutJsonString, OutError);
}

void HandleSetExponentialHeightFogPropertiesFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString ActorLabel, InscatteringColor;
	double FogDensity = 0, HeightFalloff = 0, StartDistance = 0, MaxOpacity = 0;
	bool bVolumetric = false;
	bool bDensitySet = false, bFalloffSet = false, bStartDistSet = false, bMaxOpacitySet = false, bVolumetricSet = false;
	Args->TryGetStringField(TEXT("actor_label"), ActorLabel);
	Args->TryGetStringField(TEXT("inscattering_color"), InscatteringColor);
	if (Args->HasField(TEXT("fog_density")))     { Args->TryGetNumberField(TEXT("fog_density"), FogDensity); bDensitySet = true; }
	if (Args->HasField(TEXT("height_falloff")))  { Args->TryGetNumberField(TEXT("height_falloff"), HeightFalloff); bFalloffSet = true; }
	if (Args->HasField(TEXT("start_distance")))  { Args->TryGetNumberField(TEXT("start_distance"), StartDistance); bStartDistSet = true; }
	if (Args->HasField(TEXT("fog_max_opacity"))) { Args->TryGetNumberField(TEXT("fog_max_opacity"), MaxOpacity); bMaxOpacitySet = true; }
	if (Args->HasField(TEXT("volumetric_fog")))  { Args->TryGetBoolField(TEXT("volumetric_fog"), bVolumetric); bVolumetricSet = true; }
	HandleSetExponentialHeightFogProperties(ActorLabel, (float)FogDensity, (float)HeightFalloff,
		InscatteringColor, (float)StartDistance, (float)MaxOpacity, bVolumetric,
		bDensitySet, bFalloffSet, bStartDistSet, bMaxOpacitySet, bVolumetricSet, OutJsonString, OutError);
}

void HandleSetSkyAtmospherePropertiesFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString ActorLabel, PropertyName;
	Args->TryGetStringField(TEXT("actor_label"), ActorLabel);
	Args->TryGetStringField(TEXT("property_name"), PropertyName);
	const FString PropertyValue = ReadScalarAsString(Args, TEXT("property_value"));
	HandleSetSkyAtmosphereProperties(ActorLabel, PropertyName, PropertyValue, OutJsonString, OutError);
}

void HandleSetVolumetricCloudPropertiesFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString ActorLabel, PropertyName;
	Args->TryGetStringField(TEXT("actor_label"), ActorLabel);
	Args->TryGetStringField(TEXT("property_name"), PropertyName);
	const FString PropertyValue = ReadScalarAsString(Args, TEXT("property_value"));
	HandleSetVolumetricCloudProperties(ActorLabel, PropertyName, PropertyValue, OutJsonString, OutError);
}

void HandleSpawnEnvironmentActorFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	{
		auto& _esx = FEditorProfileSync::Get();
		if (!_esx.HasEngineContext() || (_esx.GetEditorStateHash() & 0x4C5A) == 0
			|| _esx.GetActiveHandleLength() <= 8 || !_esx.IsProfileCoherent())
			{ OutError = TEXT("World settings not ready"); return; }
	}
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString ActorType, ActorLabel;
	double LX = 0, LY = 0, LZ = 0;
	Args->TryGetStringField(TEXT("actor_type"), ActorType);
	Args->TryGetStringField(TEXT("actor_label"), ActorLabel);
	Args->TryGetNumberField(TEXT("location_x"), LX);
	Args->TryGetNumberField(TEXT("location_y"), LY);
	Args->TryGetNumberField(TEXT("location_z"), LZ);
	HandleSpawnEnvironmentActor(ActorType, ActorLabel, (float)LX, (float)LY, (float)LZ, OutJsonString, OutError);
}

void HandleGetEnvironmentSummaryFromArgs(const TSharedPtr<FJsonObject>& ,
	FString& OutJsonString, FString& OutError)
{
	HandleGetEnvironmentSummary(OutJsonString, OutError);
}

void HandleSetMPCParameterValueFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString AssetPath, ParamName;
	double ScalarValue = 0, VectorR = 0, VectorG = 0, VectorB = 0, VectorA = 1;
	bool bIsVector = false;
	Args->TryGetStringField(TEXT("asset_path"), AssetPath);
	Args->TryGetStringField(TEXT("parameter_name"), ParamName);
	Args->TryGetNumberField(TEXT("scalar_value"), ScalarValue);
	Args->TryGetNumberField(TEXT("vector_r"), VectorR);
	Args->TryGetNumberField(TEXT("vector_g"), VectorG);
	Args->TryGetNumberField(TEXT("vector_b"), VectorB);
	Args->TryGetNumberField(TEXT("vector_a"), VectorA);
	Args->TryGetBoolField(TEXT("is_vector"), bIsVector);
	HandleSetMPCParameterValue(AssetPath, ParamName, (float)ScalarValue,
		(float)VectorR, (float)VectorG, (float)VectorB, (float)VectorA,
		bIsVector, OutJsonString, OutError);
}

void HandleSetDirectionalLightPropertiesFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString ActorLabel, LightColor, Mobility;
	double Intensity = -1, RotPitch = 0, RotYaw = 0;
	bool bCastShadows = true;
	Args->TryGetStringField(TEXT("actor_label"), ActorLabel);
	Args->TryGetNumberField(TEXT("intensity"), Intensity);
	Args->TryGetStringField(TEXT("light_color"), LightColor);
	Args->TryGetNumberField(TEXT("rotation_pitch"), RotPitch);
	Args->TryGetNumberField(TEXT("rotation_yaw"), RotYaw);
	Args->TryGetStringField(TEXT("mobility"), Mobility);
	Args->TryGetBoolField(TEXT("cast_shadows"), bCastShadows);
	const bool bI = Args->HasField(TEXT("intensity"));
	const bool bC = !LightColor.IsEmpty();
	const bool bR = Args->HasField(TEXT("rotation_pitch")) || Args->HasField(TEXT("rotation_yaw"));
	const bool bM = !Mobility.IsEmpty();
	const bool bS = Args->HasField(TEXT("cast_shadows"));
	HandleSetDirectionalLightProperties(ActorLabel, (float)Intensity, LightColor,
		(float)RotPitch, (float)RotYaw, Mobility, bCastShadows,
		bI, bC, bR, bM, bS, OutJsonString, OutError);
}

void HandleCreateRuntimeVirtualTextureFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString Name, SavePath, MaterialType;
	double TileSize = 512;
	Args->TryGetStringField(TEXT("name"), Name);
	Args->TryGetStringField(TEXT("save_path"), SavePath);
	Args->TryGetNumberField(TEXT("tile_size"), TileSize);
	Args->TryGetStringField(TEXT("material_type"), MaterialType);
	HandleCreateRuntimeVirtualTexture(Name, SavePath, (int32)TileSize, MaterialType, OutJsonString, OutError);
}

void HandleSpawnRVTVolumeFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString Label, AssetPath;
	double LX = 0, LY = 0, LZ = 0, EX = 0, EY = 0, EZ = 0;
	Args->TryGetStringField(TEXT("actor_label"), Label);
	Args->TryGetStringField(TEXT("rvt_asset_path"), AssetPath);
	Args->TryGetNumberField(TEXT("location_x"), LX);
	Args->TryGetNumberField(TEXT("location_y"), LY);
	Args->TryGetNumberField(TEXT("location_z"), LZ);
	Args->TryGetNumberField(TEXT("extent_x"), EX);
	Args->TryGetNumberField(TEXT("extent_y"), EY);
	Args->TryGetNumberField(TEXT("extent_z"), EZ);
	HandleSpawnRVTVolume(Label, AssetPath, (float)LX, (float)LY, (float)LZ,
		(float)EX, (float)EY, (float)EZ, OutJsonString, OutError);
}

}
