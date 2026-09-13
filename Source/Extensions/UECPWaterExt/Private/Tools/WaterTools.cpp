// Copyright 2026, BlueprintsLab, All rights reserved.

#include "Tools/WaterTools.h"

#include "Editor.h"
#include "EngineUtils.h"
#include "EditorAssetLibrary.h"
#include "Engine/World.h"

#if WITH_EDITOR
#if defined(WATER_API) || __has_include("WaterBodyRiverActor.h")
#include "WaterBodyRiverActor.h"
#include "WaterBodyLakeActor.h"
#include "WaterBodyOceanActor.h"
#include "WaterSplineComponent.h"
#define WATER_PLUGIN_AVAILABLE 1
#else
#define WATER_PLUGIN_AVAILABLE 0
#endif
#endif

namespace WaterTools
{

static UWorld* GetEditorWorld()
{
	return GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
}

static bool CheckWaterPluginAvailable(FString& OutError)
{
#if WATER_PLUGIN_AVAILABLE
	return true;
#else
	OutError = TEXT("Water plugin is not enabled. Enable it in Edit > Plugins > Water and restart the editor.");
	return false;
#endif
}

void HandleCreateWaterBodyRiver(const FString& ActorLabel, const TArray<FVector>& SplinePoints,
	FString& OutJsonString, FString& OutError)
{

	if (!CheckWaterPluginAvailable(OutError)) return;

	UWorld* World = GetEditorWorld();
	if (!World) { OutError = TEXT("No editor world available"); return; }

#if WATER_PLUGIN_AVAILABLE
	FActorSpawnParameters SpawnParams;

	FVector SpawnLoc = SplinePoints.Num() > 0 ? SplinePoints[0] : FVector::ZeroVector;
	AWaterBodyRiver* RiverActor = World->SpawnActor<AWaterBodyRiver>(SpawnLoc, FRotator::ZeroRotator, SpawnParams);
	if (!RiverActor) { OutError = TEXT("Failed to spawn AWaterBodyRiver"); return; }

	RiverActor->SetActorLabel(ActorLabel);

	if (SplinePoints.Num() >= 2)
	{
		UWaterSplineComponent* Spline = RiverActor->GetWaterSpline();
		if (Spline)
		{
			Spline->SetSplinePoints(SplinePoints, ESplineCoordinateSpace::World, true);
		}
	}

	RiverActor->MarkComponentsRenderStateDirty();

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"actor_label\":\"%s\",\"spline_points\":%d,\"message\":\"WaterBodyRiver created. Set material with set_water_material.\"}"),
		*ActorLabel, SplinePoints.Num());
#endif
}

void HandleCreateWaterBodyLake(const FString& ActorLabel, const FVector& Location, float Radius,
	FString& OutJsonString, FString& OutError)
{

	if (!CheckWaterPluginAvailable(OutError)) return;

	UWorld* World = GetEditorWorld();
	if (!World) { OutError = TEXT("No editor world available"); return; }

#if WATER_PLUGIN_AVAILABLE
	FActorSpawnParameters SpawnParams;

	AWaterBodyLake* LakeActor = World->SpawnActor<AWaterBodyLake>(Location, FRotator::ZeroRotator, SpawnParams);
	if (!LakeActor) { OutError = TEXT("Failed to spawn AWaterBodyLake"); return; }

	LakeActor->SetActorLabel(ActorLabel);

	if (Radius > 0.f)
	{
		UWaterSplineComponent* Spline = LakeActor->GetWaterSpline();
		if (Spline)
		{
			constexpr int32 NumPoints = 8;
			TArray<FVector> CirclePoints;
			CirclePoints.Reserve(NumPoints);
			for (int32 i = 0; i < NumPoints; ++i)
			{
				float Angle = (2.f * PI * i) / NumPoints;
				CirclePoints.Add(Location + FVector(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius, 0.f));
			}
			Spline->SetSplinePoints(CirclePoints, ESplineCoordinateSpace::World, true);
			Spline->SetClosedLoop(true);
		}
	}

	LakeActor->MarkComponentsRenderStateDirty();

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"actor_label\":\"%s\",\"radius\":%.0f,\"message\":\"WaterBodyLake created with %d-point spline.\"}"),
		*ActorLabel, Radius, Radius > 0.f ? 8 : 0);
#endif
}

void HandleCreateWaterBodyOcean(const FString& ActorLabel, const FVector& Location,
	FString& OutJsonString, FString& OutError)
{

	if (!CheckWaterPluginAvailable(OutError)) return;

	UWorld* World = GetEditorWorld();
	if (!World) { OutError = TEXT("No editor world available"); return; }

#if WATER_PLUGIN_AVAILABLE
	FActorSpawnParameters SpawnParams;

	AWaterBodyOcean* OceanActor = World->SpawnActor<AWaterBodyOcean>(Location, FRotator::ZeroRotator, SpawnParams);
	if (!OceanActor) { OutError = TEXT("Failed to spawn AWaterBodyOcean"); return; }

	OceanActor->SetActorLabel(ActorLabel);

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"actor_label\":\"%s\",\"message\":\"WaterBodyOcean created. Set material with set_water_material.\"}"),
		*ActorLabel);
#endif
}

void HandleSetWaterMaterial(const FString& ActorLabel, const FString& MaterialPath,
	FString& OutJsonString, FString& OutError)
{

	if (!CheckWaterPluginAvailable(OutError)) return;

	UWorld* World = GetEditorWorld();
	if (!World) { OutError = TEXT("No editor world available"); return; }

	UMaterialInterface* Material = Cast<UMaterialInterface>(UEditorAssetLibrary::LoadAsset(MaterialPath));
	if (!Material) { OutError = FString::Printf(TEXT("Material not found: %s"), *MaterialPath); return; }

#if WATER_PLUGIN_AVAILABLE
	AActor* TargetActor = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->GetActorLabel() == ActorLabel) { TargetActor = *It; break; }
	}
	if (!TargetActor) { OutError = FString::Printf(TEXT("Actor '%s' not found"), *ActorLabel); return; }

	UFunction* SetMatFunc = TargetActor->FindFunction(FName(TEXT("SetWaterMaterial")));
	if (SetMatFunc)
	{
		struct { UMaterialInterface* Mat; } Params;
		Params.Mat = Material;
		TargetActor->ProcessEvent(SetMatFunc, &Params);
	}
	else
	{
		TArray<UPrimitiveComponent*> Prims;
		TargetActor->GetComponents<UPrimitiveComponent>(Prims);
		for (UPrimitiveComponent* Prim : Prims)
		{
			Prim->SetMaterial(0, Material);
		}
	}

	TargetActor->MarkComponentsRenderStateDirty();

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"actor_label\":\"%s\",\"material\":\"%s\"}"),
		*ActorLabel, *MaterialPath);
#endif
}

void HandleSetWaterWaveSettings(const FString& ActorLabel,
	float Amplitude, float WaveLength, float DirectionDeg, float Steepness,
	FString& OutJsonString, FString& OutError)
{

	if (!CheckWaterPluginAvailable(OutError)) return;

	UWorld* World = GetEditorWorld();
	if (!World) { OutError = TEXT("No editor world available"); return; }

#if WATER_PLUGIN_AVAILABLE
	AActor* TargetActor = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->GetActorLabel() == ActorLabel) { TargetActor = *It; break; }
	}
	if (!TargetActor) { OutError = FString::Printf(TEXT("Actor '%s' not found in level"), *ActorLabel); return; }

	static UClass* WBCompClass = nullptr;
	static UClass* GerstnerClass = nullptr;
	if (!WBCompClass) WBCompClass = FindObject<UClass>(nullptr, TEXT("/Script/Water.WaterBodyComponent"));
	if (!GerstnerClass) GerstnerClass = FindObject<UClass>(nullptr, TEXT("/Script/Water.GerstnerWaterWaves"));

	if (!WBCompClass) { OutError = TEXT("WaterBodyComponent class not found — ensure Water plugin is enabled and built"); return; }
	if (!GerstnerClass) { OutError = TEXT("GerstnerWaterWaves class not found"); return; }

	UActorComponent* WBComp = TargetActor->FindComponentByClass(WBCompClass);
	if (!WBComp) { OutError = FString::Printf(TEXT("Actor '%s' has no WaterBodyComponent"), *ActorLabel); return; }

	FObjectPropertyBase* WavesProp = FindFProperty<FObjectPropertyBase>(WBCompClass, TEXT("WaterWaves"));
	UObject* WavesContainer = WBComp;
	if (!WavesProp)
	{
		WavesProp = FindFProperty<FObjectPropertyBase>(WBCompClass, TEXT("WaterWavesAsset"));
	}
	if (!WavesProp)
	{
		WavesProp = FindFProperty<FObjectPropertyBase>(TargetActor->GetClass(), TEXT("WaterWaves"));
		if (WavesProp) WavesContainer = TargetActor;
	}
	if (!WavesProp)
	{
		WavesProp = FindFProperty<FObjectPropertyBase>(TargetActor->GetClass(), TEXT("WaterWavesAsset"));
		if (WavesProp) WavesContainer = TargetActor;
	}
	if (!WavesProp) { OutError = TEXT("WaterWaves property not found on WaterBodyComponent or WaterBody actor"); return; }

	UObject* WavesObj = WavesProp->GetObjectPropertyValue_InContainer(WavesContainer);
	if (!WavesObj || !WavesObj->IsA(GerstnerClass))
	{
		WavesObj = NewObject<UObject>(WavesContainer, GerstnerClass);
		WavesProp->SetObjectPropertyValue_InContainer(WavesContainer, WavesObj);
	}

	FArrayProperty* WavesArrayProp = FindFProperty<FArrayProperty>(GerstnerClass, TEXT("GerstnerWaves"));
	if (!WavesArrayProp) { OutError = TEXT("GerstnerWaves array property not found"); return; }

	FScriptArrayHelper Helper(WavesArrayProp, WavesArrayProp->ContainerPtrToValuePtr<void>(WavesObj));
	int32 NewIdx = Helper.AddValue();
	void* WavePtr = Helper.GetRawPtr(NewIdx);

	UScriptStruct* WaveStruct = CastField<FStructProperty>(WavesArrayProp->Inner)->Struct;

	auto SetFloat = [&](const TCHAR* PropName, float Val)
	{
		if (FFloatProperty* P = FindFProperty<FFloatProperty>(WaveStruct, PropName))
			P->SetPropertyValue_InContainer(WavePtr, Val);
	};
	SetFloat(TEXT("Amplitude"), Amplitude > 0.f ? Amplitude : 100.f);
	SetFloat(TEXT("WaveLength"), WaveLength > 0.f ? WaveLength : 1000.f);
	SetFloat(TEXT("Steepness"), FMath::Clamp(Steepness, 0.f, 1.f));

	float DirRad = FMath::DegreesToRadians(DirectionDeg);
	float DirX = FMath::Cos(DirRad);
	float DirY = FMath::Sin(DirRad);
	FProperty* DirProp = FindFProperty<FProperty>(WaveStruct, TEXT("Direction"));
	if (DirProp)
	{
		void* DirPtr = DirProp->ContainerPtrToValuePtr<void>(WavePtr);
		FString DirStr = FString::Printf(TEXT("(X=%f,Y=%f)"), DirX, DirY);
		DirProp->ImportText_Direct(*DirStr, DirPtr, nullptr, PPF_None);
	}

	WBComp->MarkRenderStateDirty();
	TargetActor->MarkPackageDirty();

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"actor_label\":\"%s\",\"amplitude\":%.2f,\"wave_length\":%.2f,\"direction_deg\":%.1f,\"steepness\":%.2f,\"total_waves\":%d}"),
		*ActorLabel, Amplitude, WaveLength, DirectionDeg, Steepness, Helper.Num());
#endif
}

void HandleGetWaterBodyInfo(const FString& ActorLabel, FString& OutJsonString, FString& OutError)
{

	if (!CheckWaterPluginAvailable(OutError)) return;

	UWorld* World = GetEditorWorld();
	if (!World) { OutError = TEXT("No editor world available"); return; }

#if WATER_PLUGIN_AVAILABLE
	AActor* TargetActor = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->GetActorLabel().Equals(ActorLabel, ESearchCase::IgnoreCase) ||
			It->GetName().Equals(ActorLabel, ESearchCase::IgnoreCase))
		{
			TargetActor = *It;
			break;
		}
	}
	if (!TargetActor) { OutError = FString::Printf(TEXT("Actor '%s' not found in level"), *ActorLabel); return; }

	TSharedPtr<FJsonObject> Res = MakeShareable(new FJsonObject());
	Res->SetBoolField(TEXT("success"), true);
	Res->SetStringField(TEXT("actor_label"), TargetActor->GetActorLabel());
	Res->SetStringField(TEXT("class"), TargetActor->GetClass()->GetName());

	FVector Loc = TargetActor->GetActorLocation();
	Res->SetStringField(TEXT("location"), FString::Printf(TEXT("(%.1f, %.1f, %.1f)"), Loc.X, Loc.Y, Loc.Z));

	if (TargetActor->IsA<AWaterBodyRiver>()) Res->SetStringField(TEXT("water_type"), TEXT("River"));
	else if (TargetActor->IsA<AWaterBodyLake>()) Res->SetStringField(TEXT("water_type"), TEXT("Lake"));
	else if (TargetActor->IsA<AWaterBodyOcean>()) Res->SetStringField(TEXT("water_type"), TEXT("Ocean"));
	else Res->SetStringField(TEXT("water_type"), TEXT("Unknown"));

	static UClass* WBCompClass = nullptr;
	if (!WBCompClass) WBCompClass = FindObject<UClass>(nullptr, TEXT("/Script/Water.WaterBodyComponent"));
	if (WBCompClass)
	{
		UActorComponent* WBComp = TargetActor->FindComponentByClass(WBCompClass);
		if (WBComp)
		{
			auto ReadFloat = [&](const TCHAR* Name) -> float
			{
				if (FFloatProperty* P = FindFProperty<FFloatProperty>(WBCompClass, Name))
					return P->GetPropertyValue_InContainer(WBComp);
				if (FDoubleProperty* P = FindFProperty<FDoubleProperty>(WBCompClass, Name))
					return (float)P->GetPropertyValue_InContainer(WBComp);
				return 0.f;
			};
			auto ReadBool = [&](const TCHAR* Name) -> bool
			{
				if (FBoolProperty* P = FindFProperty<FBoolProperty>(WBCompClass, Name))
					return P->GetPropertyValue_InContainer(WBComp);
				return false;
			};

			TSharedPtr<FJsonObject> PropsObj = MakeShareable(new FJsonObject());
			PropsObj->SetBoolField(TEXT("affects_landscape"), ReadBool(TEXT("bAffectsLandscape")));
			PropsObj->SetBoolField(TEXT("generates_water_mesh"), ReadBool(TEXT("bGenerateCollisions")));

			FObjectProperty* MatProp = FindFProperty<FObjectProperty>(WBCompClass, TEXT("WaterMaterial"));
			if (MatProp)
			{
				UObject* Mat = MatProp->GetObjectPropertyValue_InContainer(WBComp);
				if (Mat)
					PropsObj->SetStringField(TEXT("water_material"), Mat->GetPathName());
			}

			FObjectProperty* WavesProp = FindFProperty<FObjectProperty>(WBCompClass, TEXT("WaterWaves"));
			if (WavesProp)
			{
				UObject* WavesObj = WavesProp->GetObjectPropertyValue_InContainer(WBComp);
				if (WavesObj)
				{
					FArrayProperty* WavesArrayProp = FindFProperty<FArrayProperty>(WavesObj->GetClass(), TEXT("GerstnerWaves"));
					if (WavesArrayProp)
					{
						FScriptArrayHelper Helper(WavesArrayProp, WavesArrayProp->ContainerPtrToValuePtr<void>(WavesObj));
						PropsObj->SetNumberField(TEXT("wave_count"), Helper.Num());
					}
				}
			}

			Res->SetObjectField(TEXT("properties"), PropsObj);
		}
	}

	if (TargetActor->IsA<AWaterBodyRiver>())
	{
		UWaterSplineComponent* Spline = TargetActor->FindComponentByClass<UWaterSplineComponent>();
		if (Spline)
		{
			Res->SetNumberField(TEXT("spline_point_count"), Spline->GetNumberOfSplinePoints());
			Res->SetNumberField(TEXT("spline_length"), Spline->GetSplineLength());
		}
	}

	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Res.ToSharedRef(), W);
#endif
}

void HandleSetWaterBodyProperties(const FString& ActorLabel, const FString& PropertyName,
	const FString& PropertyValue, FString& OutJsonString, FString& OutError)
{

	if (!CheckWaterPluginAvailable(OutError)) return;

	UWorld* World = GetEditorWorld();
	if (!World) { OutError = TEXT("No editor world available"); return; }

#if WATER_PLUGIN_AVAILABLE
	AActor* TargetActor = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->GetActorLabel().Equals(ActorLabel, ESearchCase::IgnoreCase) ||
			It->GetName().Equals(ActorLabel, ESearchCase::IgnoreCase))
		{
			TargetActor = *It;
			break;
		}
	}
	if (!TargetActor) { OutError = FString::Printf(TEXT("Actor '%s' not found in level"), *ActorLabel); return; }

	FProperty* Prop = TargetActor->GetClass()->FindPropertyByName(FName(*PropertyName));
	UObject* Container = TargetActor;

	if (!Prop)
	{
		static UClass* WBCompClass = nullptr;
		if (!WBCompClass) WBCompClass = FindObject<UClass>(nullptr, TEXT("/Script/Water.WaterBodyComponent"));
		if (WBCompClass)
		{
			UActorComponent* WBComp = TargetActor->FindComponentByClass(WBCompClass);
			if (WBComp)
			{
				Prop = WBCompClass->FindPropertyByName(FName(*PropertyName));
				if (Prop) Container = WBComp;
			}
		}
	}

	if (!Prop)
	{
		OutError = FString::Printf(TEXT("Property '%s' not found on actor or WaterBodyComponent"), *PropertyName);
		return;
	}

	void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Container);
	if (Prop->ImportText_Direct(*PropertyValue, ValuePtr, Container, PPF_None) == nullptr)
	{
		OutError = FString::Printf(TEXT("Failed to set '%s' to '%s'"), *PropertyName, *PropertyValue);
		return;
	}

	TargetActor->MarkPackageDirty();
	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"actor_label\":\"%s\",\"property\":\"%s\",\"value\":\"%s\"}"),
		*ActorLabel, *PropertyName, *PropertyValue);
#endif
}

static TArray<FVector> ExtractSplinePoints(const TSharedPtr<FJsonObject>& Args, const FString& FieldName)
{
	TArray<FVector> Points;
	const TArray<TSharedPtr<FJsonValue>>* PointArray;
	if (!Args->TryGetArrayField(FieldName, PointArray)) return Points;
	for (const TSharedPtr<FJsonValue>& Val : *PointArray)
	{
		const TArray<TSharedPtr<FJsonValue>>* Coords;
		if (Val->TryGetArray(Coords) && Coords->Num() >= 3)
		{
			const float X = (float)(*Coords)[0]->AsNumber();
			const float Y = (float)(*Coords)[1]->AsNumber();
			const float Z = (float)(*Coords)[2]->AsNumber();
			Points.Add(FVector(X, Y, Z));
		}
	}
	return Points;
}

void HandleCreateWaterBodyRiverFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString ActorLabel = TEXT("WaterBodyRiver");
	Args->TryGetStringField(TEXT("actor_label"), ActorLabel);
	HandleCreateWaterBodyRiver(ActorLabel, ExtractSplinePoints(Args, TEXT("spline_points")), OutJsonString, OutError);
}

void HandleCreateWaterBodyLakeFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString ActorLabel = TEXT("WaterBodyLake");
	double LocX = 0, LocY = 0, LocZ = 0, Radius = 2000.0;
	Args->TryGetStringField(TEXT("actor_label"), ActorLabel);
	Args->TryGetNumberField(TEXT("location_x"), LocX);
	Args->TryGetNumberField(TEXT("location_y"), LocY);
	Args->TryGetNumberField(TEXT("location_z"), LocZ);
	Args->TryGetNumberField(TEXT("radius"), Radius);
	HandleCreateWaterBodyLake(ActorLabel, FVector((float)LocX, (float)LocY, (float)LocZ), (float)Radius, OutJsonString, OutError);
}

void HandleCreateWaterBodyOceanFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString ActorLabel = TEXT("WaterBodyOcean");
	double LocX = 0, LocY = 0, LocZ = 0;
	Args->TryGetStringField(TEXT("actor_label"), ActorLabel);
	Args->TryGetNumberField(TEXT("location_x"), LocX);
	Args->TryGetNumberField(TEXT("location_y"), LocY);
	Args->TryGetNumberField(TEXT("location_z"), LocZ);
	HandleCreateWaterBodyOcean(ActorLabel, FVector((float)LocX, (float)LocY, (float)LocZ), OutJsonString, OutError);
}

void HandleSetWaterMaterialFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString ActorLabel, MaterialPath;
	if (!Args->TryGetStringField(TEXT("actor_label"), ActorLabel) ||
		!Args->TryGetStringField(TEXT("material_path"), MaterialPath))
	{
		OutError = TEXT("actor_label and material_path required");
		return;
	}
	HandleSetWaterMaterial(ActorLabel, MaterialPath, OutJsonString, OutError);
}

#if WATER_PLUGIN_AVAILABLE
static UActorComponent* ClearGerstnerWaves(AActor* TargetActor, FString& OutError)
{
	static UClass* WBCompClass = nullptr;
	static UClass* GerstnerClass = nullptr;
	if (!WBCompClass)   WBCompClass   = FindObject<UClass>(nullptr, TEXT("/Script/Water.WaterBodyComponent"));
	if (!GerstnerClass) GerstnerClass = FindObject<UClass>(nullptr, TEXT("/Script/Water.GerstnerWaterWaves"));
	if (!WBCompClass)   { OutError = TEXT("WaterBodyComponent class not found"); return nullptr; }
	if (!GerstnerClass) { OutError = TEXT("GerstnerWaterWaves class not found"); return nullptr; }

	UActorComponent* WBComp = TargetActor->FindComponentByClass(WBCompClass);
	if (!WBComp) { OutError = TEXT("Actor has no WaterBodyComponent"); return nullptr; }

	FObjectPropertyBase* WavesProp = FindFProperty<FObjectPropertyBase>(WBCompClass, TEXT("WaterWaves"));
	UObject* WavesContainer = WBComp;
	if (!WavesProp)
	{
		WavesProp = FindFProperty<FObjectPropertyBase>(TargetActor->GetClass(), TEXT("WaterWaves"));
		if (WavesProp) WavesContainer = TargetActor;
	}
	if (!WavesProp) { OutError = TEXT("WaterWaves property not found"); return nullptr; }

	UObject* WavesObj = WavesProp->GetObjectPropertyValue_InContainer(WavesContainer);
	if (!WavesObj || !WavesObj->IsA(GerstnerClass)) return WBComp;
	FArrayProperty* WavesArrayProp = FindFProperty<FArrayProperty>(GerstnerClass, TEXT("GerstnerWaves"));
	if (!WavesArrayProp) { OutError = TEXT("GerstnerWaves array property not found"); return nullptr; }
	FScriptArrayHelper Helper(WavesArrayProp, WavesArrayProp->ContainerPtrToValuePtr<void>(WavesObj));
	Helper.EmptyValues();
	return WBComp;
}
#endif

void HandleSetWaterWaveSettingsFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString ActorLabel;
	Args->TryGetStringField(TEXT("actor_label"), ActorLabel);

	bool bClearExisting = false;
	Args->TryGetBoolField(TEXT("clear_existing"), bClearExisting);

	const TArray<TSharedPtr<FJsonValue>>* WavesArr = nullptr;
	if (Args->TryGetArrayField(TEXT("waves"), WavesArr) && WavesArr && WavesArr->Num() > 0)
	{
		if (!CheckWaterPluginAvailable(OutError)) return;
		UWorld* World = GetEditorWorld();
		if (!World) { OutError = TEXT("No editor world available"); return; }
#if WATER_PLUGIN_AVAILABLE
		AActor* TargetActor = nullptr;
		for (TActorIterator<AActor> It(World); It; ++It)
			if (It->GetActorLabel() == ActorLabel) { TargetActor = *It; break; }
		if (!TargetActor) { OutError = FString::Printf(TEXT("Actor '%s' not found"), *ActorLabel); return; }
		if (bClearExisting)
		{
			FString ClearErr;
			if (!ClearGerstnerWaves(TargetActor, ClearErr)) { OutError = ClearErr; return; }
		}
#endif
		int32 Added = 0;
		FString ItemErr;
		for (const TSharedPtr<FJsonValue>& V : *WavesArr)
		{
			TSharedPtr<FJsonObject> W = V->AsObject();
			if (!W.IsValid()) continue;
			double Amp = 100.0, Len = 1000.0, Dir = 0.0, Steep = 0.5;
			W->TryGetNumberField(TEXT("amplitude"), Amp);
			W->TryGetNumberField(TEXT("wave_length"), Len);
			W->TryGetNumberField(TEXT("direction_deg"), Dir);
			W->TryGetNumberField(TEXT("steepness"), Steep);
			FString Out;
			HandleSetWaterWaveSettings(ActorLabel, (float)Amp, (float)Len, (float)Dir, (float)Steep, Out, ItemErr);
			if (ItemErr.IsEmpty()) Added++;
			else break;
		}
		if (!ItemErr.IsEmpty()) { OutError = ItemErr; return; }
		OutJsonString = FString::Printf(
			TEXT("{\"success\":true,\"actor_label\":\"%s\",\"waves_added\":%d,\"cleared_existing\":%s}"),
			*ActorLabel, Added, bClearExisting ? TEXT("true") : TEXT("false"));
		return;
	}

	double Amplitude = 100.0, WaveLength = 1000.0, DirectionDeg = 0.0, Steepness = 0.5;
	Args->TryGetNumberField(TEXT("amplitude"), Amplitude);
	Args->TryGetNumberField(TEXT("wave_length"), WaveLength);
	Args->TryGetNumberField(TEXT("direction_deg"), DirectionDeg);
	Args->TryGetNumberField(TEXT("steepness"), Steepness);

	if (bClearExisting)
	{
#if WATER_PLUGIN_AVAILABLE
		if (!CheckWaterPluginAvailable(OutError)) return;
		UWorld* World = GetEditorWorld();
		AActor* TargetActor = nullptr;
		if (World) for (TActorIterator<AActor> It(World); It; ++It)
			if (It->GetActorLabel() == ActorLabel) { TargetActor = *It; break; }
		if (TargetActor) { FString ClearErr; ClearGerstnerWaves(TargetActor, ClearErr); }
#endif
	}

	HandleSetWaterWaveSettings(ActorLabel, (float)Amplitude, (float)WaveLength,
		(float)DirectionDeg, (float)Steepness, OutJsonString, OutError);
}

void HandleClearWaterWavesFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	if (!CheckWaterPluginAvailable(OutError)) return;
	FString ActorLabel;
	Args->TryGetStringField(TEXT("actor_label"), ActorLabel);
	UWorld* World = GetEditorWorld();
	if (!World) { OutError = TEXT("No editor world available"); return; }
#if WATER_PLUGIN_AVAILABLE
	AActor* TargetActor = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
		if (It->GetActorLabel() == ActorLabel) { TargetActor = *It; break; }
	if (!TargetActor) { OutError = FString::Printf(TEXT("Actor '%s' not found"), *ActorLabel); return; }
	FString ClearErr;
	UActorComponent* WBComp = ClearGerstnerWaves(TargetActor, ClearErr);
	if (!WBComp) { OutError = ClearErr; return; }
	if (Cast<UPrimitiveComponent>(WBComp)) Cast<UPrimitiveComponent>(WBComp)->MarkRenderStateDirty();
	TargetActor->MarkPackageDirty();
#endif
	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"actor_label\":\"%s\",\"waves_cleared\":true}"), *ActorLabel);
}

void HandleSetWaterBodySplinePointsFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	if (!CheckWaterPluginAvailable(OutError)) return;
	FString ActorLabel;
	Args->TryGetStringField(TEXT("actor_label"), ActorLabel);
	bool bClosedLoop = false;
	Args->TryGetBoolField(TEXT("closed_loop"), bClosedLoop);
	const TArray<FVector> Points = ExtractSplinePoints(Args, TEXT("spline_points"));
	if (Points.Num() < 2)
	{ OutError = TEXT("spline_points must contain at least 2 [x,y,z] points"); return; }

	UWorld* World = GetEditorWorld();
	if (!World) { OutError = TEXT("No editor world available"); return; }
#if WATER_PLUGIN_AVAILABLE
	AActor* TargetActor = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
		if (It->GetActorLabel() == ActorLabel) { TargetActor = *It; break; }
	if (!TargetActor) { OutError = FString::Printf(TEXT("Actor '%s' not found"), *ActorLabel); return; }

	UWaterSplineComponent* Spline = TargetActor->FindComponentByClass<UWaterSplineComponent>();
	if (!Spline) { OutError = TEXT("Actor has no UWaterSplineComponent (rivers + lakes have splines; ocean does not)"); return; }
	Spline->SetSplinePoints(Points, ESplineCoordinateSpace::World, true);
	Spline->SetClosedLoop(bClosedLoop);
	TargetActor->MarkComponentsRenderStateDirty();
	TargetActor->MarkPackageDirty();
#endif
	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"actor_label\":\"%s\",\"point_count\":%d,\"closed_loop\":%s}"),
		*ActorLabel, Points.Num(), bClosedLoop ? TEXT("true") : TEXT("false"));
}

void HandleGetWaterBodyInfoFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString ActorLabel;
	Args->TryGetStringField(TEXT("actor_label"), ActorLabel);
	HandleGetWaterBodyInfo(ActorLabel, OutJsonString, OutError);
}

void HandleSetWaterBodyPropertiesFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString ActorLabel, PropertyName, PropertyValue;
	Args->TryGetStringField(TEXT("actor_label"), ActorLabel);
	Args->TryGetStringField(TEXT("property_name"), PropertyName);
	Args->TryGetStringField(TEXT("property_value"), PropertyValue);
	HandleSetWaterBodyProperties(ActorLabel, PropertyName, PropertyValue, OutJsonString, OutError);
}

}
