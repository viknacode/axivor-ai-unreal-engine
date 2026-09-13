// Copyright 2026, BlueprintsLab, All rights reserved.

#include "Tools/PlayTestTools.h"
#include "Tools/EditorUtilityTools.h"

#include "Editor.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "EngineUtils.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/DateTime.h"
#include "InputCoreTypes.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/InputSettings.h"
#include "GenericPlatform/GenericApplication.h"
#include "HighResScreenshot.h"
#include "Slate/SceneViewport.h"
#include "Engine/GameViewportClient.h"
#include "CollisionQueryParams.h"
#include "CollisionShape.h"
#include "WorldCollision.h"
#include "Engine/OverlapResult.h"
#include "Async/Async.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "UObject/UObjectIterator.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"

#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "GameplayTagAssetInterface.h"
#include "GameplayTagContainer.h"
#include "AbilitySystemComponent.h"
#include "AttributeSet.h"

#include "EnhancedInputSubsystems.h"
#include "InputAction.h"

namespace PlayTestTools
{

static UWorld* GetPIEWorld(FString& OutError)
{
	if (!GEditor)
	{
		OutError = TEXT("GEditor not available");
		return nullptr;
	}
	UWorld* PlayWorld = GEditor->PlayWorld;
	if (!PlayWorld)
	{
		OutError = TEXT("No PIE session is running. Call begin_play_in_editor first.");
		return nullptr;
	}
	return PlayWorld;
}

static AActor* FindPIEActor(UWorld* World, const FString& Label)
{
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->GetActorLabel().Equals(Label, ESearchCase::IgnoreCase) ||
			It->GetName().Equals(Label, ESearchCase::IgnoreCase))
		{
			return *It;
		}
	}
	return nullptr;
}

void HandleGetPieStatus(FString& OutJsonString, FString& OutError)
{

	TSharedPtr<FJsonObject> Res = MakeShareable(new FJsonObject());

	if (!GEditor || !GEditor->PlayWorld)
	{
		Res->SetBoolField(TEXT("success"), true);
		Res->SetBoolField(TEXT("pie_running"), false);
		TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJsonString);
		FJsonSerializer::Serialize(Res.ToSharedRef(), W);
		return;
	}

	UWorld* PlayWorld = GEditor->PlayWorld;
	Res->SetBoolField(TEXT("success"), true);
	Res->SetBoolField(TEXT("pie_running"), true);
	Res->SetStringField(TEXT("map_name"), PlayWorld->GetMapName());
	Res->SetNumberField(TEXT("time_seconds"), PlayWorld->GetTimeSeconds());
	Res->SetNumberField(TEXT("real_time_seconds"), PlayWorld->GetRealTimeSeconds());

	float DeltaTime = FApp::GetDeltaTime();
	float FPS = DeltaTime > 0.f ? 1.f / DeltaTime : 0.f;
	Res->SetNumberField(TEXT("fps"), FMath::RoundToInt(FPS));
	Res->SetNumberField(TEXT("frame_time_ms"), DeltaTime * 1000.f);

	int32 ActorCount = 0;
	for (TActorIterator<AActor> It(PlayWorld); It; ++It) ActorCount++;
	Res->SetNumberField(TEXT("actor_count"), ActorCount);

	APlayerController* PC = PlayWorld->GetFirstPlayerController();
	if (PC)
	{
		Res->SetBoolField(TEXT("has_player"), true);
		APawn* Pawn = PC->GetPawn();
		if (Pawn)
		{
			FVector Loc = Pawn->GetActorLocation();
			FRotator Rot = Pawn->GetActorRotation();
			Res->SetStringField(TEXT("player_location"), FString::Printf(TEXT("(%.1f, %.1f, %.1f)"), Loc.X, Loc.Y, Loc.Z));
			Res->SetStringField(TEXT("player_rotation"), FString::Printf(TEXT("(%.1f, %.1f, %.1f)"), Rot.Pitch, Rot.Yaw, Rot.Roll));
			Res->SetStringField(TEXT("player_class"), Pawn->GetClass()->GetName());

			ACharacter* Char = Cast<ACharacter>(Pawn);
			if (Char)
			{
				Res->SetStringField(TEXT("player_type"), TEXT("Character"));
			}
		}
		else
		{
			Res->SetBoolField(TEXT("player_pawn_exists"), false);
		}
	}

	AGameModeBase* GM = PlayWorld->GetAuthGameMode();
	if (GM)
	{
		Res->SetStringField(TEXT("game_mode"), GM->GetClass()->GetName());
	}

	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Res.ToSharedRef(), W);
}

void HandleGetPieLog(int32 LineCount, const FString& CategoryFilter, const FString& SeverityFilter,
	FString& OutJsonString, FString& OutError)
{
	EditorUtilityTools::HandleGetOutputLog(LineCount, CategoryFilter, SeverityFilter, OutJsonString, OutError);
}

void HandleGetPieActors(const FString& ClassFilter, int32 MaxCount,
	FString& OutJsonString, FString& OutError)
{

	UWorld* PlayWorld = GetPIEWorld(OutError);
	if (!PlayWorld) return;

	if (MaxCount <= 0) MaxCount = 100;

	TSharedPtr<FJsonObject> Res = MakeShareable(new FJsonObject());
	Res->SetBoolField(TEXT("success"), true);

	TArray<TSharedPtr<FJsonValue>> ActorArray;
	int32 Count = 0;
	for (TActorIterator<AActor> It(PlayWorld); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor) continue;

		if (!ClassFilter.IsEmpty())
		{
			FString ClassName = Actor->GetClass()->GetName();
			if (!ClassName.Contains(ClassFilter, ESearchCase::IgnoreCase)) continue;
		}

		if (Count >= MaxCount) break;
		Count++;

		TSharedPtr<FJsonObject> ActorObj = MakeShareable(new FJsonObject());
		ActorObj->SetStringField(TEXT("name"), Actor->GetName());
		ActorObj->SetStringField(TEXT("label"), Actor->GetActorLabel());
		ActorObj->SetStringField(TEXT("class"), Actor->GetClass()->GetName());

		FVector Loc = Actor->GetActorLocation();
		ActorObj->SetStringField(TEXT("location"), FString::Printf(TEXT("(%.1f, %.1f, %.1f)"), Loc.X, Loc.Y, Loc.Z));
		ActorObj->SetBoolField(TEXT("hidden"), Actor->IsHidden());

		ActorArray.Add(MakeShareable(new FJsonValueObject(ActorObj)));
	}

	Res->SetArrayField(TEXT("actors"), ActorArray);
	Res->SetNumberField(TEXT("count"), ActorArray.Num());

	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Res.ToSharedRef(), W);
}

void HandleGetPieActorState(const FString& ActorLabel, const TArray<FString>& PropertyNames,
	FString& OutJsonString, FString& OutError)
{

	UWorld* PlayWorld = GetPIEWorld(OutError);
	if (!PlayWorld) return;

	AActor* Actor = FindPIEActor(PlayWorld, ActorLabel);
	if (!Actor)
	{
		OutError = FString::Printf(TEXT("Actor '%s' not found in PIE world"), *ActorLabel);
		return;
	}

	TSharedPtr<FJsonObject> Res = MakeShareable(new FJsonObject());
	Res->SetBoolField(TEXT("success"), true);
	Res->SetStringField(TEXT("actor"), Actor->GetName());
	Res->SetStringField(TEXT("class"), Actor->GetClass()->GetName());

	FVector Loc = Actor->GetActorLocation();
	FRotator Rot = Actor->GetActorRotation();
	FVector Vel = Actor->GetVelocity();
	Res->SetStringField(TEXT("location"), FString::Printf(TEXT("(%.1f, %.1f, %.1f)"), Loc.X, Loc.Y, Loc.Z));
	Res->SetStringField(TEXT("rotation"), FString::Printf(TEXT("(%.1f, %.1f, %.1f)"), Rot.Pitch, Rot.Yaw, Rot.Roll));
	Res->SetStringField(TEXT("velocity"), FString::Printf(TEXT("(%.1f, %.1f, %.1f)"), Vel.X, Vel.Y, Vel.Z));

	if (PropertyNames.Num() > 0)
	{
		TSharedPtr<FJsonObject> PropsObj = MakeShareable(new FJsonObject());
		for (const FString& PropName : PropertyNames)
		{
			FProperty* Prop = Actor->GetClass()->FindPropertyByName(FName(*PropName));
			if (Prop)
			{
				FString ValueStr;
				const void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Actor);
				Prop->ExportTextItem_Direct(ValueStr, ValuePtr, nullptr, Actor, PPF_None);
				PropsObj->SetStringField(PropName, ValueStr);
			}
			else
			{
				bool bFound = false;
				for (UActorComponent* Comp : Actor->GetComponents())
				{
					if (!Comp) continue;
					FProperty* CompProp = Comp->GetClass()->FindPropertyByName(FName(*PropName));
					if (CompProp)
					{
						FString ValueStr;
						const void* ValuePtr = CompProp->ContainerPtrToValuePtr<void>(Comp);
						CompProp->ExportTextItem_Direct(ValueStr, ValuePtr, nullptr, Comp, PPF_None);
						PropsObj->SetStringField(PropName, ValueStr);
						bFound = true;
						break;
					}
				}
				if (!bFound)
				{
					PropsObj->SetStringField(PropName, TEXT("<not found>"));
				}
			}
		}
		Res->SetObjectField(TEXT("properties"), PropsObj);
	}
	else
	{
		TSharedPtr<FJsonObject> PropsObj = MakeShareable(new FJsonObject());
		PropsObj->SetBoolField(TEXT("is_hidden"), Actor->IsHidden());
		PropsObj->SetBoolField(TEXT("can_be_damaged"), Actor->CanBeDamaged());

		APawn* Pawn = Cast<APawn>(Actor);
		if (Pawn)
		{
			APlayerController* PC = Cast<APlayerController>(Pawn->GetController());
			PropsObj->SetBoolField(TEXT("is_player_controlled"), PC != nullptr);
		}
		Res->SetObjectField(TEXT("properties"), PropsObj);
	}

	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Res.ToSharedRef(), W);
}

void HandleSimulateInput(const FString& Key, const FString& EventType, float Duration,
	FString& OutJsonString, FString& OutError)
{

	UWorld* PlayWorld = GetPIEWorld(OutError);
	if (!PlayWorld) return;

	APlayerController* PC = PlayWorld->GetFirstPlayerController();
	if (!PC)
	{
		OutError = TEXT("No PlayerController found in PIE world");
		return;
	}

	FKey KeyToPress(*Key);
	if (!KeyToPress.IsValid())
	{
		OutError = FString::Printf(TEXT("Invalid key name: '%s'. Use UE key names like: W, A, S, D, SpaceBar, "
			"LeftMouseButton, RightMouseButton, Gamepad_LeftThumbstick, etc."), *Key);
		return;
	}

	EInputEvent IE = IE_Pressed;
	FString EvtLower = EventType.ToLower();
	if (EvtLower == TEXT("pressed") || EvtLower == TEXT("press") || EvtLower.IsEmpty())
		IE = IE_Pressed;
	else if (EvtLower == TEXT("released") || EvtLower == TEXT("release"))
		IE = IE_Released;
	else if (EvtLower == TEXT("repeat"))
		IE = IE_Repeat;
	else
	{
		OutError = FString::Printf(TEXT("Invalid event type '%s'. Use: pressed, released, repeat"), *EventType);
		return;
	}

#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6)
	PC->InputKey(FInputKeyEventArgs::CreateSimulated(KeyToPress, IE,  1.0f));
#else
	PC->InputKey(FInputKeyParams(KeyToPress, IE, (double)1.0, false));
#endif

	if (Duration > 0.f && IE == IE_Pressed)
	{
		FTimerHandle TimerHandle;
		FTimerDelegate TimerDelegate;
		FKey CapturedKey = KeyToPress;
		TimerDelegate.BindLambda([PC, CapturedKey]()
		{
			if (PC && PC->IsValidLowLevel())
			{
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6)
				PC->InputKey(FInputKeyEventArgs::CreateSimulated(CapturedKey, IE_Released,  1.0f));
#else
				PC->InputKey(FInputKeyParams(CapturedKey, IE_Released, (double)1.0, false));
#endif
			}
		});
		PlayWorld->GetTimerManager().SetTimer(TimerHandle, TimerDelegate, Duration, false);

		OutJsonString = FString::Printf(TEXT("{\"success\":true,\"key\":\"%s\",\"event\":\"pressed\",\"auto_release_after\":%.2f}"),
			*Key, Duration);
	}
	else
	{
		OutJsonString = FString::Printf(TEXT("{\"success\":true,\"key\":\"%s\",\"event\":\"%s\"}"),
			*Key, *EventType);
	}
}

void HandleExecutePieConsoleCommand(const FString& Command,
	FString& OutJsonString, FString& OutError)
{

	if (Command.IsEmpty()) { OutError = TEXT("command parameter is required"); return; }

	UWorld* PlayWorld = GetPIEWorld(OutError);
	if (!PlayWorld) return;

	GEngine->Exec(PlayWorld, *Command, *GLog);

	FString Safe = Command.Replace(TEXT("\""), TEXT("\\\""));
	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"command\":\"%s\",\"context\":\"pie_world\"}"), *Safe);
}

void HandleTakePieScreenshot(const FString& FilePath,
	FString& OutJsonString, FString& OutError)
{

	UWorld* PlayWorld = GetPIEWorld(OutError);
	if (!PlayWorld) return;

	UGameViewportClient* GVC = PlayWorld->GetGameViewport();
	if (!GVC)
	{
		OutError = TEXT("No GameViewportClient found in PIE world");
		return;
	}

	FViewport* VP = GVC->Viewport;
	if (!VP)
	{
		OutError = TEXT("No Viewport found on GameViewportClient");
		return;
	}

	FHighResScreenshotConfig& Config = GetHighResScreenshotConfig();
	if (!FilePath.IsEmpty())
		Config.FilenameOverride = FilePath;

	VP->TakeHighResScreenShot();

	FString ResolvedPath = FilePath.IsEmpty()
		? FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Screenshots"))
		: FilePath;
	FString Safe = ResolvedPath.Replace(TEXT("\\"), TEXT("/")).Replace(TEXT("\""), TEXT("\\\""));
	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"file_path\":\"%s\",\"source\":\"pie_viewport\",\"note\":\"Screenshot write is asynchronous\"}"),
		*Safe);
}

void HandleGetPiePerformance(FString& OutJsonString, FString& OutError)
{

	UWorld* PlayWorld = GetPIEWorld(OutError);
	if (!PlayWorld) return;

	TSharedPtr<FJsonObject> Res = MakeShareable(new FJsonObject());
	Res->SetBoolField(TEXT("success"), true);

	float DeltaTime = FApp::GetDeltaTime();
	float FPS = DeltaTime > 0.f ? 1.f / DeltaTime : 0.f;
	Res->SetNumberField(TEXT("fps"), FMath::RoundToInt(FPS));
	Res->SetNumberField(TEXT("frame_time_ms"), DeltaTime * 1000.f);

	if (GEngine && GEngine->GameViewport && GEngine->GameViewport->Viewport)
	{
		Res->SetNumberField(TEXT("target_fps"), GEngine->GetMaxFPS());
	}

	int32 ActorCount = 0;
	int32 ComponentCount = 0;
	for (TActorIterator<AActor> It(PlayWorld); It; ++It)
	{
		ActorCount++;
		ComponentCount += It->GetComponents().Num();
	}
	Res->SetNumberField(TEXT("actor_count"), ActorCount);
	Res->SetNumberField(TEXT("component_count"), ComponentCount);

	FPlatformMemoryStats MemStats = FPlatformMemory::GetStats();
	Res->SetNumberField(TEXT("used_physical_mb"), MemStats.UsedPhysical / (1024 * 1024));
	Res->SetNumberField(TEXT("used_virtual_mb"), MemStats.UsedVirtual / (1024 * 1024));

	Res->SetNumberField(TEXT("play_time_seconds"), PlayWorld->GetTimeSeconds());

	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Res.ToSharedRef(), W);
}

void HandleSetPieActorProperty(const FString& ActorLabel, const FString& PropertyName, const FString& PropertyValue,
	FString& OutJsonString, FString& OutError)
{

	UWorld* PlayWorld = GetPIEWorld(OutError);
	if (!PlayWorld) return;

	AActor* Actor = FindPIEActor(PlayWorld, ActorLabel);
	if (!Actor)
	{
		OutError = FString::Printf(TEXT("Actor '%s' not found in PIE world"), *ActorLabel);
		return;
	}

	FProperty* Prop = Actor->GetClass()->FindPropertyByName(FName(*PropertyName));
	UObject* Container = Actor;

	if (!Prop)
	{
		for (UActorComponent* Comp : Actor->GetComponents())
		{
			if (!Comp) continue;
			Prop = Comp->GetClass()->FindPropertyByName(FName(*PropertyName));
			if (Prop) { Container = Comp; break; }
		}
	}

	if (!Prop)
	{
		OutError = FString::Printf(TEXT("Property '%s' not found on actor '%s' or its components"), *PropertyName, *ActorLabel);
		return;
	}

	void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Container);
	bool bOk = Prop->ImportText_Direct(*PropertyValue, ValuePtr, Container, PPF_None) != nullptr;

	if (bOk)
	{
		OutJsonString = FString::Printf(TEXT("{\"success\":true,\"actor\":\"%s\",\"property\":\"%s\",\"value\":\"%s\"}"),
			*ActorLabel, *PropertyName, *PropertyValue);
	}
	else
	{
		OutError = FString::Printf(TEXT("Failed to set property '%s' to '%s' on actor '%s'"), *PropertyName, *PropertyValue, *ActorLabel);
	}
}

void HandlePieLineTrace(const FString& StartStr, const FString& EndStr, const FString& Channel,
	FString& OutJsonString, FString& OutError)
{

	UWorld* PlayWorld = GetPIEWorld(OutError);
	if (!PlayWorld) return;

	auto ParseVec = [](const FString& Str, FVector& Out) -> bool
	{
		FString Clean = Str;
		Clean.ReplaceInline(TEXT("("), TEXT(""));
		Clean.ReplaceInline(TEXT(")"), TEXT(""));
		TArray<FString> Parts;
		Clean.ParseIntoArray(Parts, TEXT(","));
		if (Parts.Num() >= 3)
		{
			Out.X = FCString::Atof(*Parts[0]);
			Out.Y = FCString::Atof(*Parts[1]);
			Out.Z = FCString::Atof(*Parts[2]);
			return true;
		}
		return false;
	};

	FVector Start, End;
	if (!ParseVec(StartStr, Start)) { OutError = TEXT("Invalid start vector. Use format: (X,Y,Z)"); return; }
	if (!ParseVec(EndStr, End)) { OutError = TEXT("Invalid end vector. Use format: (X,Y,Z)"); return; }

	ECollisionChannel TraceChannel = ECC_Visibility;
	FString ChLower = Channel.ToLower();
	if (ChLower == TEXT("camera")) TraceChannel = ECC_Camera;
	else if (ChLower == TEXT("pawn")) TraceChannel = ECC_Pawn;
	else if (ChLower == TEXT("worldstatic")) TraceChannel = ECC_WorldStatic;
	else if (ChLower == TEXT("worlddynamic")) TraceChannel = ECC_WorldDynamic;
	else if (ChLower == TEXT("destructible")) TraceChannel = ECC_Destructible;
	else if (ChLower == TEXT("physicsbody")) TraceChannel = ECC_PhysicsBody;

	FHitResult Hit;
	FCollisionQueryParams Params;
	Params.bTraceComplex = true;
	bool bHit = PlayWorld->LineTraceSingleByChannel(Hit, Start, End, TraceChannel, Params);

	TSharedPtr<FJsonObject> Res = MakeShareable(new FJsonObject());
	Res->SetBoolField(TEXT("success"), true);
	Res->SetBoolField(TEXT("hit"), bHit);

	if (bHit)
	{
		Res->SetStringField(TEXT("hit_location"), FString::Printf(TEXT("(%.1f, %.1f, %.1f)"), Hit.Location.X, Hit.Location.Y, Hit.Location.Z));
		Res->SetStringField(TEXT("hit_normal"), FString::Printf(TEXT("(%.2f, %.2f, %.2f)"), Hit.Normal.X, Hit.Normal.Y, Hit.Normal.Z));
		Res->SetNumberField(TEXT("distance"), Hit.Distance);
		if (Hit.GetActor())
		{
			Res->SetStringField(TEXT("hit_actor"), Hit.GetActor()->GetName());
			Res->SetStringField(TEXT("hit_actor_class"), Hit.GetActor()->GetClass()->GetName());
		}
		if (Hit.GetComponent())
		{
			Res->SetStringField(TEXT("hit_component"), Hit.GetComponent()->GetName());
		}
		Res->SetStringField(TEXT("phys_material"), Hit.PhysMaterial.IsValid() ? Hit.PhysMaterial->GetName() : TEXT("None"));
	}

	Res->SetStringField(TEXT("trace_start"), StartStr);
	Res->SetStringField(TEXT("trace_end"), EndStr);
	Res->SetStringField(TEXT("channel"), Channel.IsEmpty() ? TEXT("Visibility") : Channel);

	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Res.ToSharedRef(), W);
}

void HandleGetPiePlayerState(const TArray<FString>& PropertyNames,
	FString& OutJsonString, FString& OutError)
{

	UWorld* PlayWorld = GetPIEWorld(OutError);
	if (!PlayWorld) return;

	APlayerController* PC = PlayWorld->GetFirstPlayerController();
	if (!PC) { OutError = TEXT("No PlayerController found in PIE world"); return; }

	TSharedPtr<FJsonObject> Res = MakeShareable(new FJsonObject());
	Res->SetBoolField(TEXT("success"), true);
	Res->SetStringField(TEXT("controller_class"), PC->GetClass()->GetName());

	APawn* Pawn = PC->GetPawn();
	if (!Pawn)
	{
		Res->SetBoolField(TEXT("has_pawn"), false);
		FVector CamLoc; FRotator CamRot;
		PC->GetPlayerViewPoint(CamLoc, CamRot);
		Res->SetStringField(TEXT("camera_location"), FString::Printf(TEXT("(%.1f, %.1f, %.1f)"), CamLoc.X, CamLoc.Y, CamLoc.Z));
		Res->SetStringField(TEXT("camera_rotation"), FString::Printf(TEXT("(%.1f, %.1f, %.1f)"), CamRot.Pitch, CamRot.Yaw, CamRot.Roll));
		TSharedRef<TJsonWriter<>> W0 = TJsonWriterFactory<>::Create(&OutJsonString);
		FJsonSerializer::Serialize(Res.ToSharedRef(), W0);
		return;
	}

	Res->SetBoolField(TEXT("has_pawn"), true);
	Res->SetStringField(TEXT("pawn_class"), Pawn->GetClass()->GetName());

	FVector Loc = Pawn->GetActorLocation();
	FRotator Rot = Pawn->GetActorRotation();
	FVector Vel = Pawn->GetVelocity();
	float Speed = Vel.Size();
	Res->SetStringField(TEXT("location"), FString::Printf(TEXT("(%.1f, %.1f, %.1f)"), Loc.X, Loc.Y, Loc.Z));
	Res->SetStringField(TEXT("rotation"), FString::Printf(TEXT("(%.1f, %.1f, %.1f)"), Rot.Pitch, Rot.Yaw, Rot.Roll));
	Res->SetStringField(TEXT("velocity"), FString::Printf(TEXT("(%.1f, %.1f, %.1f)"), Vel.X, Vel.Y, Vel.Z));
	Res->SetNumberField(TEXT("speed"), Speed);

	ACharacter* Char = Cast<ACharacter>(Pawn);
	if (Char)
	{
		Res->SetBoolField(TEXT("is_jumping"), Char->JumpCurrentCount > 0);
		Res->SetBoolField(TEXT("is_crouching"), Char->bIsCrouched);
		UCharacterMovementComponent* CMC = Char->GetCharacterMovement();
		if (CMC)
			Res->SetBoolField(TEXT("is_falling"), CMC->IsFalling());
	}

	if (PropertyNames.Num() > 0)
	{
		TSharedPtr<FJsonObject> PropsObj = MakeShareable(new FJsonObject());
		for (const FString& PropName : PropertyNames)
		{
			FProperty* Prop = Pawn->GetClass()->FindPropertyByName(FName(*PropName));
			if (Prop)
			{
				FString ValStr;
				const void* VP = Prop->ContainerPtrToValuePtr<void>(Pawn);
				Prop->ExportTextItem_Direct(ValStr, VP, nullptr, Pawn, PPF_None);
				PropsObj->SetStringField(PropName, ValStr);
			}
			else
			{
				PropsObj->SetStringField(PropName, TEXT("<not found>"));
			}
		}
		Res->SetObjectField(TEXT("extra_properties"), PropsObj);
	}

	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Res.ToSharedRef(), W);
}

void HandleWaitForPieEvent(const FString& ConditionType, const FString& ActorLabel,
	const FString& PropertyName, const FString& ExpectedValue, float TimeoutSeconds,
	FString& OutJsonString, FString& OutError)
{
	if (IsInGameThread())
	{
		OutError = TEXT("wait_for_pie_event blocks the calling thread and cannot run from the in-editor agent path. ")
			TEXT("Poll the condition yourself across turns: call get_pie_status / get_pie_actor_state / get_pie_player_state ")
			TEXT("between Architect responses until the value matches.");
		return;
	}

	if (TimeoutSeconds <= 0.f) TimeoutSeconds = 30.f;

	double StartTime = FPlatformTime::Seconds();
	bool bConditionMet = false;
	bool bTimedOut = false;

	FString InitialValue;
	if (ConditionType == TEXT("property_changes"))
	{
		FEvent* InitEvent = FPlatformProcess::GetSynchEventFromPool(false);
		FString LocalInitValue;
		AsyncTask(ENamedThreads::GameThread, [&]()
		{
			if (GEditor && GEditor->PlayWorld)
			{
				AActor* Actor = FindPIEActor(GEditor->PlayWorld, ActorLabel);
				if (Actor)
				{
					FProperty* Prop = Actor->GetClass()->FindPropertyByName(FName(*PropertyName));
					if (Prop)
					{
						const void* VP = Prop->ContainerPtrToValuePtr<void>(Actor);
						Prop->ExportTextItem_Direct(LocalInitValue, VP, nullptr, Actor, PPF_None);
					}
				}
			}
			InitEvent->Trigger();
		});
		InitEvent->Wait();
		FPlatformProcess::ReturnSynchEventToPool(InitEvent);
		InitialValue = LocalInitValue;
	}

	while (true)
	{
		double Elapsed = FPlatformTime::Seconds() - StartTime;
		if (Elapsed >= TimeoutSeconds)
		{
			bTimedOut = true;
			break;
		}

		FEvent* CheckEvent = FPlatformProcess::GetSynchEventFromPool(false);
		bool bMet = false;

		AsyncTask(ENamedThreads::GameThread, [&]()
		{
			UWorld* PW = GEditor ? GEditor->PlayWorld : nullptr;
			if (!PW) { CheckEvent->Trigger(); return; }

			if (ConditionType == TEXT("time_elapsed"))
			{
				float WaitSecs = FCString::Atof(*ExpectedValue);
				bMet = (float)(FPlatformTime::Seconds() - StartTime) >= WaitSecs;
			}
			else if (ConditionType == TEXT("actor_exists"))
			{
				bMet = FindPIEActor(PW, ActorLabel) != nullptr;
			}
			else if (ConditionType == TEXT("actor_destroyed"))
			{
				bMet = FindPIEActor(PW, ActorLabel) == nullptr;
			}
			else if (ConditionType == TEXT("property_equals"))
			{
				AActor* Actor = FindPIEActor(PW, ActorLabel);
				if (Actor)
				{
					FProperty* Prop = Actor->GetClass()->FindPropertyByName(FName(*PropertyName));
					if (Prop)
					{
						FString CurVal;
						const void* VP = Prop->ContainerPtrToValuePtr<void>(Actor);
						Prop->ExportTextItem_Direct(CurVal, VP, nullptr, Actor, PPF_None);
						bMet = CurVal.Equals(ExpectedValue, ESearchCase::IgnoreCase);
					}
				}
			}
			else if (ConditionType == TEXT("property_changes"))
			{
				AActor* Actor = FindPIEActor(PW, ActorLabel);
				if (Actor)
				{
					FProperty* Prop = Actor->GetClass()->FindPropertyByName(FName(*PropertyName));
					if (Prop)
					{
						FString CurVal;
						const void* VP = Prop->ContainerPtrToValuePtr<void>(Actor);
						Prop->ExportTextItem_Direct(CurVal, VP, nullptr, Actor, PPF_None);
						bMet = !CurVal.Equals(InitialValue, ESearchCase::IgnoreCase);
					}
				}
			}
			CheckEvent->Trigger();
		});

		CheckEvent->Wait();
		FPlatformProcess::ReturnSynchEventToPool(CheckEvent);

		if (bMet) { bConditionMet = true; break; }

		FPlatformProcess::Sleep(0.15f);
	}

	double Elapsed = FPlatformTime::Seconds() - StartTime;
	TSharedPtr<FJsonObject> Res = MakeShareable(new FJsonObject());
	Res->SetBoolField(TEXT("success"), true);
	Res->SetBoolField(TEXT("condition_met"), bConditionMet);
	Res->SetBoolField(TEXT("timed_out"), bTimedOut);
	Res->SetNumberField(TEXT("elapsed_seconds"), Elapsed);
	Res->SetStringField(TEXT("condition_type"), ConditionType);

	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Res.ToSharedRef(), W);
}

void HandleCallPieBlueprintFunction(const FString& ActorLabel, const FString& FunctionName,
	const TSharedPtr<FJsonObject>& Params, FString& OutJsonString, FString& OutError)
{

	UWorld* PlayWorld = GetPIEWorld(OutError);
	if (!PlayWorld) return;

	AActor* Actor = FindPIEActor(PlayWorld, ActorLabel);
	if (!Actor) { OutError = FString::Printf(TEXT("Actor '%s' not found in PIE world"), *ActorLabel); return; }

	UFunction* Func = Actor->FindFunction(FName(*FunctionName));
	if (!Func) { OutError = FString::Printf(TEXT("Function '%s' not found on '%s'"), *FunctionName, *ActorLabel); return; }

	TArray<uint8> ParamBuffer;
	ParamBuffer.SetNumZeroed(Func->ParmsSize);
	uint8* ParamStorage = ParamBuffer.GetData();

	for (TFieldIterator<FProperty> It(Func); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
	{
		FProperty* Prop = *It;
		if (Prop->HasAnyPropertyFlags(CPF_ReturnParm)) continue;
		if (!Params.IsValid()) continue;

		FString PropName = Prop->GetName();
		FString ValueStr;
		double NumVal = 0.0; bool BoolVal = false;

		if (Params->TryGetStringField(*PropName, ValueStr) ||
			(Params->TryGetNumberField(*PropName, NumVal) && !(ValueStr = FString::SanitizeFloat(NumVal)).IsEmpty()) ||
			(Params->TryGetBoolField(*PropName, BoolVal) && !(ValueStr = BoolVal ? TEXT("true") : TEXT("false")).IsEmpty()))
		{
			void* VP = Prop->ContainerPtrToValuePtr<void>(ParamStorage);
			Prop->ImportText_Direct(*ValueStr, VP, nullptr, PPF_None);
		}
	}

	Actor->ProcessEvent(Func, ParamStorage);

	FString ReturnValue, ReturnType;
	for (TFieldIterator<FProperty> It(Func); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
	{
		FProperty* Prop = *It;
		if (Prop->HasAnyPropertyFlags(CPF_ReturnParm))
		{
			const void* VP = Prop->ContainerPtrToValuePtr<void>(ParamStorage);
			Prop->ExportTextItem_Direct(ReturnValue, VP, nullptr, nullptr, PPF_None);
			ReturnType = Prop->GetCPPType();
			break;
		}
	}

	TSharedPtr<FJsonObject> Res = MakeShareable(new FJsonObject());
	Res->SetBoolField(TEXT("success"), true);
	Res->SetStringField(TEXT("function_name"), FunctionName);
	Res->SetStringField(TEXT("actor"), ActorLabel);
	if (!ReturnValue.IsEmpty()) Res->SetStringField(TEXT("return_value"), ReturnValue);
	if (!ReturnType.IsEmpty()) Res->SetStringField(TEXT("return_type"), ReturnType);

	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Res.ToSharedRef(), W);
}

static void DispatchPieTestStep(const FString& Action, const TSharedPtr<FJsonObject>& Step,
	FString& OutJson, FString& OutErr)
{
	if (Action == TEXT("get_pie_status"))
	{
		HandleGetPieStatus(OutJson, OutErr);
	}
	else if (Action == TEXT("get_pie_actors"))
	{
		FString CF; double MC = 100;
		Step->TryGetStringField(TEXT("class_filter"), CF); Step->TryGetNumberField(TEXT("max_count"), MC);
		HandleGetPieActors(CF, (int32)MC, OutJson, OutErr);
	}
	else if (Action == TEXT("get_pie_actor_state"))
	{
		FString AL; Step->TryGetStringField(TEXT("actor_label"), AL);
		TArray<FString> Props;
		const TArray<TSharedPtr<FJsonValue>>* PA;
		if (Step->TryGetArrayField(TEXT("property_names"), PA))
			for (auto& V : *PA) { FString S; if (V->TryGetString(S)) Props.Add(S); }
		HandleGetPieActorState(AL, Props, OutJson, OutErr);
	}
	else if (Action == TEXT("get_pie_player_state"))
	{
		TArray<FString> Props;
		const TArray<TSharedPtr<FJsonValue>>* PA;
		if (Step->TryGetArrayField(TEXT("property_names"), PA))
			for (auto& V : *PA) { FString S; if (V->TryGetString(S)) Props.Add(S); }
		HandleGetPiePlayerState(Props, OutJson, OutErr);
	}
	else if (Action == TEXT("simulate_input"))
	{
		FString K, ET; double D = 0;
		Step->TryGetStringField(TEXT("key"), K); Step->TryGetStringField(TEXT("event_type"), ET);
		Step->TryGetNumberField(TEXT("duration"), D);
		HandleSimulateInput(K, ET, (float)D, OutJson, OutErr);
	}
	else if (Action == TEXT("execute_pie_console_command"))
	{
		FString Cmd; Step->TryGetStringField(TEXT("command"), Cmd);
		HandleExecutePieConsoleCommand(Cmd, OutJson, OutErr);
	}
	else if (Action == TEXT("take_pie_screenshot"))
	{
		FString FP; Step->TryGetStringField(TEXT("file_path"), FP);
		HandleTakePieScreenshot(FP, OutJson, OutErr);
	}
	else if (Action == TEXT("get_pie_performance"))
	{
		HandleGetPiePerformance(OutJson, OutErr);
	}
	else if (Action == TEXT("set_pie_actor_property"))
	{
		FString AL, PN, PV;
		Step->TryGetStringField(TEXT("actor_label"), AL);
		Step->TryGetStringField(TEXT("property_name"), PN); Step->TryGetStringField(TEXT("property_value"), PV);
		HandleSetPieActorProperty(AL, PN, PV, OutJson, OutErr);
	}
	else if (Action == TEXT("pie_line_trace"))
	{
		FString S, En, Ch;
		Step->TryGetStringField(TEXT("start"), S); Step->TryGetStringField(TEXT("end"), En);
		Step->TryGetStringField(TEXT("channel"), Ch);
		HandlePieLineTrace(S, En, Ch, OutJson, OutErr);
	}
	else if (Action == TEXT("call_pie_blueprint_function"))
	{
		FString AL, FN; Step->TryGetStringField(TEXT("actor_label"), AL); Step->TryGetStringField(TEXT("function_name"), FN);
		TSharedPtr<FJsonObject> ParamsObj;
		const TSharedPtr<FJsonObject>* PO;
		if (Step->TryGetObjectField(TEXT("params"), PO)) ParamsObj = *PO;
		HandleCallPieBlueprintFunction(AL, FN, ParamsObj, OutJson, OutErr);
	}
	else if (Action == TEXT("get_pie_log"))
	{
		double LC = 50; FString CF, SF;
		Step->TryGetNumberField(TEXT("line_count"), LC);
		Step->TryGetStringField(TEXT("category_filter"), CF); Step->TryGetStringField(TEXT("severity_filter"), SF);
		HandleGetPieLog((int32)LC, CF, SF, OutJson, OutErr);
	}
	else
	{
		OutErr = FString::Printf(TEXT("Unknown step action: '%s'"), *Action);
	}
}

void HandleRunPieTestSequence(const FString& StepsJson, bool bStopOnFailure,
	FString& OutJsonString, FString& OutError)
{
	if (IsInGameThread())
	{
		OutError = TEXT("run_pie_test_sequence orchestrates blocking PIE steps and cannot run from the in-editor agent path. ")
			TEXT("Issue the actions one at a time across turns instead — most steps (get_pie_*, simulate_input, ")
			TEXT("execute_pie_console_command, set_pie_actor_property, pie_line_trace, take_pie_screenshot) ")
			TEXT("are individually safe.");
		return;
	}

	TSharedPtr<FJsonValue> RootValue;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(StepsJson);
	if (!FJsonSerializer::Deserialize(Reader, RootValue) || !RootValue.IsValid())
	{
		OutError = TEXT("Failed to parse steps JSON array");
		return;
	}

	const TArray<TSharedPtr<FJsonValue>>* StepsArray = nullptr;
	TArray<TSharedPtr<FJsonValue>> LocalArray;
	if (RootValue->Type == EJson::Array)
	{
		LocalArray = RootValue->AsArray();
		StepsArray = &LocalArray;
	}
	else if (RootValue->Type == EJson::Object)
	{
		RootValue->AsObject()->TryGetArrayField(TEXT("steps"), StepsArray);
	}

	if (!StepsArray || StepsArray->Num() == 0)
	{
		OutError = TEXT("steps must be a non-empty JSON array");
		return;
	}

	TSharedPtr<FJsonObject> ResultObj = MakeShareable(new FJsonObject());
	TArray<TSharedPtr<FJsonValue>> StepResults;
	int32 StepsCompleted = 0;

	for (int32 i = 0; i < StepsArray->Num(); i++)
	{
		TSharedPtr<FJsonObject> Step = (*StepsArray)[i]->AsObject();
		if (!Step.IsValid()) continue;

		FString Action;
		Step->TryGetStringField(TEXT("action"), Action);
		double DelayAfter = 0.0;
		Step->TryGetNumberField(TEXT("delay_after"), DelayAfter);

		FString StepOut, StepErr;

		if (Action == TEXT("wait_for_pie_event"))
		{
			FString CT, AL, PN, EV; double TO = 30.0;
			Step->TryGetStringField(TEXT("condition_type"), CT);
			Step->TryGetStringField(TEXT("actor_label"), AL);
			Step->TryGetStringField(TEXT("property_name"), PN);
			Step->TryGetStringField(TEXT("expected_value"), EV);
			Step->TryGetNumberField(TEXT("timeout_seconds"), TO);
			HandleWaitForPieEvent(CT, AL, PN, EV, (float)TO, StepOut, StepErr);
		}
		else
		{
			FEvent* Event = FPlatformProcess::GetSynchEventFromPool(false);
			AsyncTask(ENamedThreads::GameThread, [&]()
			{
				DispatchPieTestStep(Action, Step, StepOut, StepErr);
				Event->Trigger();
			});
			Event->Wait();
			FPlatformProcess::ReturnSynchEventToPool(Event);
		}

		TSharedPtr<FJsonObject> StepRes = MakeShareable(new FJsonObject());
		StepRes->SetNumberField(TEXT("step_index"), i);
		StepRes->SetStringField(TEXT("action"), Action);
		bool bStepOk = StepErr.IsEmpty();
		StepRes->SetBoolField(TEXT("success"), bStepOk);
		if (!StepOut.IsEmpty()) StepRes->SetStringField(TEXT("result"), StepOut);
		if (!StepErr.IsEmpty()) StepRes->SetStringField(TEXT("error"), StepErr);
		StepResults.Add(MakeShareable(new FJsonValueObject(StepRes)));
		StepsCompleted++;

		if (!bStepOk && bStopOnFailure) break;
		if (DelayAfter > 0.0) FPlatformProcess::Sleep((float)DelayAfter);
	}

	ResultObj->SetBoolField(TEXT("success"), true);
	ResultObj->SetNumberField(TEXT("steps_completed"), StepsCompleted);
	ResultObj->SetNumberField(TEXT("steps_total"), StepsArray->Num());
	ResultObj->SetArrayField(TEXT("results"), StepResults);

	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(ResultObj.ToSharedRef(), W);
}

void HandleGetPieStatusFromArgs(const TSharedPtr<FJsonObject>& ,
	FString& OutJsonString, FString& OutError)
{
	HandleGetPieStatus(OutJsonString, OutError);
}

void HandleGetPieLogFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	FString CategoryFilter, SeverityFilter;
	double LineCount = 50;
	double Since = 0.0;
	if (Args.IsValid())
	{
		Args->TryGetNumberField(TEXT("line_count"), LineCount);
		Args->TryGetStringField(TEXT("category_filter"), CategoryFilter);
		Args->TryGetStringField(TEXT("severity_filter"), SeverityFilter);
		Args->TryGetNumberField(TEXT("since_timestamp"), Since);
	}
	EditorUtilityTools::HandleGetOutputLogSince((int32)LineCount, CategoryFilter, SeverityFilter,
		Since, OutJsonString, OutError);
}

void HandleGetPieActorsFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	FString ClassFilter;
	double MaxCount = 100;
	if (Args.IsValid())
	{
		Args->TryGetStringField(TEXT("class_filter"), ClassFilter);
		Args->TryGetNumberField(TEXT("max_count"), MaxCount);
	}
	HandleGetPieActors(ClassFilter, (int32)MaxCount, OutJsonString, OutError);
}

void HandleGetPieActorStateFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString ActorLabel;
	Args->TryGetStringField(TEXT("actor_label"), ActorLabel);
	TArray<FString> PropertyNames;
	const TArray<TSharedPtr<FJsonValue>>* PA = nullptr;
	if (Args->TryGetArrayField(TEXT("property_names"), PA))
	{
		for (const auto& V : *PA) { FString S; if (V->TryGetString(S)) PropertyNames.Add(S); }
	}
	HandleGetPieActorState(ActorLabel, PropertyNames, OutJsonString, OutError);
}

void HandleSimulateInputFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString Key, EventType;
	double Duration = 0;
	Args->TryGetStringField(TEXT("key"), Key);
	Args->TryGetStringField(TEXT("event_type"), EventType);
	Args->TryGetNumberField(TEXT("duration"), Duration);
	HandleSimulateInput(Key, EventType, (float)Duration, OutJsonString, OutError);
}

void HandleExecutePieConsoleCommandFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString Command;
	Args->TryGetStringField(TEXT("command"), Command);
	HandleExecutePieConsoleCommand(Command, OutJsonString, OutError);
}

void HandleTakePieScreenshotFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString FilePath;
	Args->TryGetStringField(TEXT("file_path"), FilePath);
	HandleTakePieScreenshot(FilePath, OutJsonString, OutError);
}

void HandleGetPiePerformanceFromArgs(const TSharedPtr<FJsonObject>& ,
	FString& OutJsonString, FString& OutError)
{
	HandleGetPiePerformance(OutJsonString, OutError);
}

void HandleSetPieActorPropertyFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString ActorLabel, PropertyName, PropertyValue;
	Args->TryGetStringField(TEXT("actor_label"), ActorLabel);
	Args->TryGetStringField(TEXT("property_name"), PropertyName);
	Args->TryGetStringField(TEXT("property_value"), PropertyValue);
	HandleSetPieActorProperty(ActorLabel, PropertyName, PropertyValue, OutJsonString, OutError);
}

void HandlePieLineTraceFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }

	auto ReadVecField = [&Args](const TCHAR* FieldName, FString& Out) -> void
	{
		if (Args->TryGetStringField(FieldName, Out)) return;

		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (Args->TryGetArrayField(FieldName, Arr) && Arr && Arr->Num() >= 3)
		{
			double X = (*Arr)[0]->AsNumber();
			double Y = (*Arr)[1]->AsNumber();
			double Z = (*Arr)[2]->AsNumber();
			Out = FString::Printf(TEXT("(%f, %f, %f)"), X, Y, Z);
		}
	};

	FString Start, End, Channel;
	ReadVecField(TEXT("start"), Start);
	ReadVecField(TEXT("end"),   End);
	Args->TryGetStringField(TEXT("channel"), Channel);
	HandlePieLineTrace(Start, End, Channel, OutJsonString, OutError);
}

void HandleGetPiePlayerStateFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	TArray<FString> PropertyNames;
	if (Args.IsValid())
	{
		const TArray<TSharedPtr<FJsonValue>>* PA = nullptr;
		if (Args->TryGetArrayField(TEXT("property_names"), PA))
		{
			for (const auto& V : *PA) { FString S; if (V->TryGetString(S)) PropertyNames.Add(S); }
		}
	}
	HandleGetPiePlayerState(PropertyNames, OutJsonString, OutError);
}

void HandleWaitForPieEventFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString ConditionType, ActorLabel, PropertyName, ExpectedValue;
	double Timeout = 30, Seconds = 0;
	Args->TryGetStringField(TEXT("condition_type"), ConditionType);
	Args->TryGetStringField(TEXT("actor_label"), ActorLabel);
	Args->TryGetStringField(TEXT("property_name"), PropertyName);
	Args->TryGetStringField(TEXT("expected_value"), ExpectedValue);
	Args->TryGetNumberField(TEXT("timeout_seconds"), Timeout);
	if (Args->TryGetNumberField(TEXT("seconds"), Seconds))
		ExpectedValue = FString::SanitizeFloat((float)Seconds);
	HandleWaitForPieEvent(ConditionType, ActorLabel, PropertyName, ExpectedValue, (float)Timeout,
		OutJsonString, OutError);
}

void HandleCallPieBlueprintFunctionFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString ActorLabel, FunctionName;
	Args->TryGetStringField(TEXT("actor_label"), ActorLabel);
	Args->TryGetStringField(TEXT("function_name"), FunctionName);
	TSharedPtr<FJsonObject> Params;
	const TSharedPtr<FJsonObject>* ParamsObj;
	if (Args->TryGetObjectField(TEXT("params"), ParamsObj)) Params = *ParamsObj;
	HandleCallPieBlueprintFunction(ActorLabel, FunctionName, Params, OutJsonString, OutError);
}

void HandleRunPieTestSequenceFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString StepsJson;
	bool bStop = false;
	const TArray<TSharedPtr<FJsonValue>>* StepsArr = nullptr;
	if (Args->TryGetArrayField(TEXT("steps"), StepsArr))
	{
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&StepsJson);
		FJsonSerializer::Serialize(*StepsArr, Writer);
	}
	else
	{
		Args->TryGetStringField(TEXT("steps_json"), StepsJson);
	}
	Args->TryGetBoolField(TEXT("stop_on_failure"), bStop);
	HandleRunPieTestSequence(StepsJson, bStop, OutJsonString, OutError);
}

namespace
{
	bool ParseVector3(const FString& Str, FVector& Out)
	{
		FString Clean = Str;
		Clean.ReplaceInline(TEXT("("), TEXT(""));
		Clean.ReplaceInline(TEXT(")"), TEXT(""));
		TArray<FString> Parts;
		Clean.ParseIntoArray(Parts, TEXT(","));
		if (Parts.Num() < 3) return false;
		Out.X = FCString::Atod(*Parts[0]);
		Out.Y = FCString::Atod(*Parts[1]);
		Out.Z = FCString::Atod(*Parts[2]);
		return true;
	}

	bool ParseRotator(const FString& Str, FRotator& Out)
	{
		FString Clean = Str;
		Clean.ReplaceInline(TEXT("("), TEXT(""));
		Clean.ReplaceInline(TEXT(")"), TEXT(""));
		TArray<FString> Parts;
		Clean.ParseIntoArray(Parts, TEXT(","));
		if (Parts.Num() < 3) return false;
		Out.Pitch = FCString::Atof(*Parts[0]);
		Out.Yaw   = FCString::Atof(*Parts[1]);
		Out.Roll  = FCString::Atof(*Parts[2]);
		return true;
	}

	ECollisionChannel ResolveTraceChannel(const FString& Channel)
	{
		FString L = Channel.ToLower();
		if (L == TEXT("camera"))         return ECC_Camera;
		if (L == TEXT("pawn"))           return ECC_Pawn;
		if (L == TEXT("worldstatic"))    return ECC_WorldStatic;
		if (L == TEXT("worlddynamic"))   return ECC_WorldDynamic;
		if (L == TEXT("destructible"))   return ECC_Destructible;
		if (L == TEXT("physicsbody"))    return ECC_PhysicsBody;
		return ECC_Visibility;
	}

	struct FStuckTracker
	{
		bool       bHasBaseline = false;
		FVector    LastLocation = FVector::ZeroVector;
		double     StuckSinceTime = 0.0;
		FCriticalSection Lock;
	};
	FStuckTracker& GetStuckTracker()
	{
		static FStuckTracker T;
		return T;
	}
}

void HandlePausePie(bool bPause, FString& OutJsonString, FString& OutError)
{
	UWorld* W = GetPIEWorld(OutError);
	if (!W) return;
	const bool bOk = UGameplayStatics::SetGamePaused(W, bPause);
	OutJsonString = FString::Printf(TEXT("{\"success\":%s,\"paused\":%s}"),
		bOk ? TEXT("true") : TEXT("false"),
		bPause ? TEXT("true") : TEXT("false"));
}

void HandleSetTimeDilation(float Dilation, FString& OutJsonString, FString& OutError)
{
	UWorld* W = GetPIEWorld(OutError);
	if (!W) return;
	if (Dilation <= 0.f) Dilation = 1.f;
	UGameplayStatics::SetGlobalTimeDilation(W, Dilation);
	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"time_dilation\":%.4f}"), Dilation);
}

void HandleLookAtTarget(float MaxDistance, const FString& Channel,
	FString& OutJsonString, FString& OutError)
{
	UWorld* W = GetPIEWorld(OutError);
	if (!W) return;

	APlayerController* PC = W->GetFirstPlayerController();
	if (!PC) { OutError = TEXT("No PlayerController in PIE world"); return; }

	if (MaxDistance <= 0.f) MaxDistance = 10000.f;

	FVector CamLoc; FRotator CamRot;
	PC->GetPlayerViewPoint(CamLoc, CamRot);
	FVector End = CamLoc + CamRot.Vector() * MaxDistance;

	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(LookAtTarget), true);
	if (APawn* Pawn = PC->GetPawn()) Params.AddIgnoredActor(Pawn);
	const bool bHit = W->LineTraceSingleByChannel(Hit, CamLoc, End, ResolveTraceChannel(Channel), Params);

	TSharedPtr<FJsonObject> Res = MakeShareable(new FJsonObject);
	Res->SetBoolField(TEXT("success"), true);
	Res->SetBoolField(TEXT("hit"), bHit);
	Res->SetStringField(TEXT("camera_location"), FString::Printf(TEXT("(%.1f, %.1f, %.1f)"), CamLoc.X, CamLoc.Y, CamLoc.Z));
	Res->SetStringField(TEXT("camera_forward"), FString::Printf(TEXT("(%.3f, %.3f, %.3f)"),
		CamRot.Vector().X, CamRot.Vector().Y, CamRot.Vector().Z));
	Res->SetNumberField(TEXT("max_distance"), MaxDistance);
	if (bHit)
	{
		Res->SetStringField(TEXT("hit_location"), FString::Printf(TEXT("(%.1f, %.1f, %.1f)"),
			Hit.Location.X, Hit.Location.Y, Hit.Location.Z));
		Res->SetNumberField(TEXT("distance"), Hit.Distance);
		if (AActor* HitActor = Hit.GetActor())
		{
			Res->SetStringField(TEXT("hit_actor"), HitActor->GetActorLabel());
			Res->SetStringField(TEXT("hit_actor_name"), HitActor->GetName());
			Res->SetStringField(TEXT("hit_actor_class"), HitActor->GetClass()->GetName());
		}
		if (UPrimitiveComponent* HitComp = Hit.GetComponent())
		{
			Res->SetStringField(TEXT("hit_component"), HitComp->GetName());
		}
	}

	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Res.ToSharedRef(), Writer);
}

void HandleIsPlayerStuck(float WindowSeconds, float MoveThreshold,
	FString& OutJsonString, FString& OutError)
{
	UWorld* W = GetPIEWorld(OutError);
	if (!W) return;

	APlayerController* PC = W->GetFirstPlayerController();
	if (!PC) { OutError = TEXT("No PlayerController"); return; }
	APawn* Pawn = PC->GetPawn();
	if (!Pawn)
	{
		OutJsonString = TEXT("{\"success\":true,\"stuck\":false,\"has_pawn\":false,\"reason\":\"no pawn to track\"}");
		return;
	}

	if (WindowSeconds <= 0.f) WindowSeconds = 3.f;
	if (MoveThreshold <= 0.f) MoveThreshold = 50.f;

	const FVector NowLoc = Pawn->GetActorLocation();
	const double  NowTime = FPlatformTime::Seconds();
	const float   Speed = Pawn->GetVelocity().Size();

	FStuckTracker& T = GetStuckTracker();
	FScopeLock SL(&T.Lock);

	double StuckDuration = 0.0;
	bool bStuck = false;
	if (!T.bHasBaseline)
	{
		T.bHasBaseline = true;
		T.LastLocation = NowLoc;
		T.StuckSinceTime = NowTime;
	}
	else
	{
		const float Moved = (NowLoc - T.LastLocation).Size();
		if (Moved > MoveThreshold)
		{
			T.LastLocation = NowLoc;
			T.StuckSinceTime = NowTime;
		}
		else
		{
			StuckDuration = NowTime - T.StuckSinceTime;
			bStuck = (StuckDuration >= WindowSeconds);
		}
	}

	TSharedPtr<FJsonObject> Res = MakeShareable(new FJsonObject);
	Res->SetBoolField(TEXT("success"), true);
	Res->SetBoolField(TEXT("stuck"), bStuck);
	Res->SetBoolField(TEXT("has_pawn"), true);
	Res->SetNumberField(TEXT("stuck_for_seconds"), StuckDuration);
	Res->SetNumberField(TEXT("window_seconds"), WindowSeconds);
	Res->SetNumberField(TEXT("move_threshold"), MoveThreshold);
	Res->SetNumberField(TEXT("current_speed"), Speed);
	Res->SetStringField(TEXT("location"), FString::Printf(TEXT("(%.1f, %.1f, %.1f)"), NowLoc.X, NowLoc.Y, NowLoc.Z));

	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Res.ToSharedRef(), Writer);
}

void HandleSimulateMouseDelta(float DeltaX, float DeltaY,
	FString& OutJsonString, FString& OutError)
{
	UWorld* W = GetPIEWorld(OutError);
	if (!W) return;
	APlayerController* PC = W->GetFirstPlayerController();
	if (!PC) { OutError = TEXT("No PlayerController"); return; }

	PC->AddYawInput(DeltaX);
	PC->AddPitchInput(-DeltaY);

	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"yaw_delta\":%.3f,\"pitch_delta\":%.3f}"),
		DeltaX, DeltaY);
}

void HandleSimulateInputAxis(const FString& InputActionPath,
	float ValueX, float ValueY, float ValueZ, FString& OutJsonString, FString& OutError)
{
	UWorld* W = GetPIEWorld(OutError);
	if (!W) return;
	APlayerController* PC = W->GetFirstPlayerController();
	if (!PC) { OutError = TEXT("No PlayerController"); return; }
	ULocalPlayer* LP = PC->GetLocalPlayer();
	if (!LP) { OutError = TEXT("PlayerController has no LocalPlayer"); return; }

	UEnhancedInputLocalPlayerSubsystem* Sub = LP->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
	if (!Sub) { OutError = TEXT("EnhancedInputLocalPlayerSubsystem not available"); return; }

	UInputAction* IA = LoadObject<UInputAction>(nullptr, *InputActionPath);
	if (!IA) { OutError = FString::Printf(TEXT("Could not load InputAction at '%s'"), *InputActionPath); return; }

	FInputActionValue Value;
	if (FMath::IsNearlyZero(ValueY) && FMath::IsNearlyZero(ValueZ))
		Value = FInputActionValue(ValueX);
	else if (FMath::IsNearlyZero(ValueZ))
		Value = FInputActionValue(FVector2D(ValueX, ValueY));
	else
		Value = FInputActionValue(FVector(ValueX, ValueY, ValueZ));

	Sub->InjectInputForAction(IA, Value, {}, {});

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"input_action\":\"%s\",\"value\":[%f,%f,%f]}"),
		*InputActionPath, ValueX, ValueY, ValueZ);
}

void HandleSimulateInputBurst(const TArray<TSharedPtr<FJsonValue>>& Steps,
	FString& OutJsonString, FString& OutError)
{
	UWorld* W = GetPIEWorld(OutError);
	if (!W) return;
	APlayerController* PC = W->GetFirstPlayerController();
	if (!PC) { OutError = TEXT("No PlayerController"); return; }

	if (Steps.Num() == 0) { OutError = TEXT("steps array is empty"); return; }
	if (Steps.Num() > 64) { OutError = TEXT("burst limited to 64 steps"); return; }

	int32 Scheduled = 0;
	float MaxOffsetMs = 0.f;
	for (const TSharedPtr<FJsonValue>& V : Steps)
	{
		TSharedPtr<FJsonObject> Step = V->AsObject();
		if (!Step.IsValid()) continue;
		FString KeyName, EventStr;
		double OffsetMs = 0.0;
		Step->TryGetStringField(TEXT("key"), KeyName);
		Step->TryGetStringField(TEXT("event_type"), EventStr);
		Step->TryGetNumberField(TEXT("offset_ms"), OffsetMs);

		FKey K(*KeyName);
		if (!K.IsValid()) continue;
		EInputEvent IE = IE_Pressed;
		FString E = EventStr.ToLower();
		if (E == TEXT("released") || E == TEXT("release")) IE = IE_Released;
		else if (E == TEXT("repeat")) IE = IE_Repeat;

		const float DelaySeconds = FMath::Max(0.f, (float)OffsetMs / 1000.f);
		MaxOffsetMs = FMath::Max(MaxOffsetMs, (float)OffsetMs);

		if (DelaySeconds <= 0.f)
		{
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6)
			PC->InputKey(FInputKeyEventArgs::CreateSimulated(K, IE,  1.0f));
#else
			PC->InputKey(FInputKeyParams(K, IE, (double)1.0, false));
#endif
		}
		else
		{
			FTimerHandle Handle;
			TWeakObjectPtr<APlayerController> WeakPC = PC;
			FTimerDelegate D;
			D.BindLambda([WeakPC, K, IE]()
			{
				if (APlayerController* PCC = WeakPC.Get())
				{
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6)
					PCC->InputKey(FInputKeyEventArgs::CreateSimulated(K, IE,  1.0f));
#else
					PCC->InputKey(FInputKeyParams(K, IE, (double)1.0, false));
#endif
				}
			});
			W->GetTimerManager().SetTimer(Handle, D, DelaySeconds, false);
		}
		++Scheduled;
	}

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"scheduled\":%d,\"finishes_in_ms\":%.0f}"),
		Scheduled, MaxOffsetMs);
}

namespace
{
	void DescribeWidget(UWidget* Widget, TArray<TSharedPtr<FJsonValue>>& Out)
	{
		if (!Widget) return;
		const FString WName = Widget->GetName();
		if (UTextBlock* T = Cast<UTextBlock>(Widget))
		{
			TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
			Obj->SetStringField(TEXT("kind"), TEXT("text"));
			Obj->SetStringField(TEXT("name"), WName);
			Obj->SetStringField(TEXT("text"), T->GetText().ToString().Left(300));
			Out.Add(MakeShareable(new FJsonValueObject(Obj)));
		}
		else if (UButton* B = Cast<UButton>(Widget))
		{
			TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
			Obj->SetStringField(TEXT("kind"), TEXT("button"));
			Obj->SetStringField(TEXT("name"), WName);
			FString Label;
			if (B->GetChildrenCount() > 0)
			{
				if (UTextBlock* InnerText = Cast<UTextBlock>(B->GetChildAt(0)))
				{
					Label = InnerText->GetText().ToString();
				}
			}
			Obj->SetStringField(TEXT("label"), Label.Left(200));
			Obj->SetBoolField(TEXT("enabled"), B->GetIsEnabled());
			Out.Add(MakeShareable(new FJsonValueObject(Obj)));
		}
		if (UPanelWidget* Panel = Cast<UPanelWidget>(Widget))
		{
			for (int32 i = 0; i < Panel->GetChildrenCount(); ++i)
			{
				DescribeWidget(Panel->GetChildAt(i), Out);
			}
		}
	}
}

void HandleGetVisibleWidgets(int32 MaxWidgets, FString& OutJsonString, FString& OutError)
{
	UWorld* W = GetPIEWorld(OutError);
	if (!W) return;
	if (MaxWidgets <= 0) MaxWidgets = 50;

	TArray<TSharedPtr<FJsonValue>> Widgets;
	int32 Count = 0;
	for (TObjectIterator<UUserWidget> It; It; ++It)
	{
		UUserWidget* UW = *It;
		if (!UW || UW->GetWorld() != W) continue;
		if (!UW->IsInViewport()) continue;
		if (Count >= MaxWidgets) break;

		TSharedPtr<FJsonObject> Entry = MakeShareable(new FJsonObject);
		Entry->SetStringField(TEXT("class"), UW->GetClass()->GetName());
		Entry->SetStringField(TEXT("name"), UW->GetName());

		TArray<TSharedPtr<FJsonValue>> Contents;
		if (UWidgetTree* WT = UW->WidgetTree)
		{
			DescribeWidget(WT->RootWidget, Contents);
		}
		Entry->SetArrayField(TEXT("contents"), Contents);
		Widgets.Add(MakeShareable(new FJsonValueObject(Entry)));
		++Count;
	}

	TSharedPtr<FJsonObject> Res = MakeShareable(new FJsonObject);
	Res->SetBoolField(TEXT("success"), true);
	Res->SetArrayField(TEXT("widgets"), Widgets);
	Res->SetNumberField(TEXT("count"), Widgets.Num());
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Res.ToSharedRef(), Writer);
}

void HandleActorsNearPlayer(float Radius, const FString& ClassFilter,
	FString& OutJsonString, FString& OutError)
{
	UWorld* W = GetPIEWorld(OutError);
	if (!W) return;
	APlayerController* PC = W->GetFirstPlayerController();
	if (!PC) { OutError = TEXT("No PlayerController"); return; }
	APawn* Pawn = PC->GetPawn();
	FVector Origin;
	if (Pawn) Origin = Pawn->GetActorLocation();
	else { FRotator R; PC->GetPlayerViewPoint(Origin, R); }

	if (Radius <= 0.f) Radius = 500.f;

	TArray<FOverlapResult> Overlaps;
	FCollisionQueryParams P(SCENE_QUERY_STAT(ActorsNearPlayer), false);
	if (Pawn) P.AddIgnoredActor(Pawn);
	W->OverlapMultiByObjectType(Overlaps,
		Origin,
		FQuat::Identity,
		FCollisionObjectQueryParams::AllObjects,
		FCollisionShape::MakeSphere(Radius),
		P);

	TArray<TSharedPtr<FJsonValue>> Found;
	TSet<AActor*> Seen;
	for (const FOverlapResult& OR : Overlaps)
	{
		AActor* A = OR.GetActor();
		if (!A || Seen.Contains(A)) continue;
		Seen.Add(A);
		const FString ClassName = A->GetClass()->GetName();
		if (!ClassFilter.IsEmpty() && !ClassName.Contains(ClassFilter, ESearchCase::IgnoreCase)) continue;
		const float Dist = FVector::Dist(Origin, A->GetActorLocation());
		TSharedPtr<FJsonObject> O = MakeShareable(new FJsonObject);
		O->SetStringField(TEXT("name"), A->GetName());
		O->SetStringField(TEXT("label"), A->GetActorLabel());
		O->SetStringField(TEXT("class"), ClassName);
		O->SetNumberField(TEXT("distance"), Dist);
		const FVector Loc = A->GetActorLocation();
		O->SetStringField(TEXT("location"), FString::Printf(TEXT("(%.1f, %.1f, %.1f)"), Loc.X, Loc.Y, Loc.Z));
		Found.Add(MakeShareable(new FJsonValueObject(O)));
	}

	Found.Sort([](const TSharedPtr<FJsonValue>& A, const TSharedPtr<FJsonValue>& B) {
		double DA = 0, DB = 0;
		if (A.IsValid() && A->AsObject().IsValid()) A->AsObject()->TryGetNumberField(TEXT("distance"), DA);
		if (B.IsValid() && B->AsObject().IsValid()) B->AsObject()->TryGetNumberField(TEXT("distance"), DB);
		return DA < DB;
	});

	TSharedPtr<FJsonObject> Res = MakeShareable(new FJsonObject);
	Res->SetBoolField(TEXT("success"), true);
	Res->SetStringField(TEXT("origin"), FString::Printf(TEXT("(%.1f, %.1f, %.1f)"), Origin.X, Origin.Y, Origin.Z));
	Res->SetNumberField(TEXT("radius"), Radius);
	Res->SetArrayField(TEXT("actors"), Found);
	Res->SetNumberField(TEXT("count"), Found.Num());
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Res.ToSharedRef(), Writer);
}

void HandleGetPlayerRuntimeState(FString& OutJsonString, FString& OutError)
{
	UWorld* W = GetPIEWorld(OutError);
	if (!W) return;
	APlayerController* PC = W->GetFirstPlayerController();
	if (!PC) { OutError = TEXT("No PlayerController"); return; }
	APawn* Pawn = PC->GetPawn();

	TSharedPtr<FJsonObject> Res = MakeShareable(new FJsonObject);
	Res->SetBoolField(TEXT("success"), true);
	Res->SetBoolField(TEXT("has_pawn"), Pawn != nullptr);
	if (!Pawn)
	{
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
		FJsonSerializer::Serialize(Res.ToSharedRef(), Writer);
		return;
	}

	Res->SetStringField(TEXT("pawn_class"), Pawn->GetClass()->GetName());

	if (IGameplayTagAssetInterface* TagOwner = Cast<IGameplayTagAssetInterface>(Pawn))
	{
		FGameplayTagContainer Tags;
		TagOwner->GetOwnedGameplayTags(Tags);
		TArray<TSharedPtr<FJsonValue>> TagArr;
		for (const FGameplayTag& T : Tags)
		{
			TagArr.Add(MakeShareable(new FJsonValueString(T.ToString())));
		}
		Res->SetArrayField(TEXT("gameplay_tags"), TagArr);
	}

	if (ACharacter* Char = Cast<ACharacter>(Pawn))
	{
		if (USkeletalMeshComponent* Mesh = Char->GetMesh())
		{
			if (UAnimInstance* AI = Mesh->GetAnimInstance())
			{
				if (UAnimMontage* M = AI->GetCurrentActiveMontage())
				{
					TSharedPtr<FJsonObject> MontageObj = MakeShareable(new FJsonObject);
					MontageObj->SetStringField(TEXT("name"), M->GetName());
					MontageObj->SetStringField(TEXT("section"), AI->Montage_GetCurrentSection(M).ToString());
					MontageObj->SetNumberField(TEXT("position"), AI->Montage_GetPosition(M));
					MontageObj->SetBoolField(TEXT("playing"), AI->Montage_IsPlaying(M));
					Res->SetObjectField(TEXT("active_montage"), MontageObj);
				}
			}
		}
		Res->SetBoolField(TEXT("is_jumping"), Char->JumpCurrentCount > 0);
		Res->SetBoolField(TEXT("is_crouching"), Char->bIsCrouched);
		if (UCharacterMovementComponent* CMC = Char->GetCharacterMovement())
			Res->SetBoolField(TEXT("is_falling"), CMC->IsFalling());
	}

	if (UAbilitySystemComponent* ASC = Pawn->FindComponentByClass<UAbilitySystemComponent>())
	{
		TArray<TSharedPtr<FJsonValue>> AttrArr;
		for (const UAttributeSet* AS : ASC->GetSpawnedAttributes())
		{
			if (!AS) continue;
			for (TFieldIterator<FProperty> It(AS->GetClass()); It; ++It)
			{
				FProperty* P = *It;
				if (FStructProperty* SP = CastField<FStructProperty>(P))
				{
					if (SP->Struct == FGameplayAttributeData::StaticStruct())
					{
						const FGameplayAttributeData* AttrData = SP->ContainerPtrToValuePtr<const FGameplayAttributeData>(AS);
						if (!AttrData) continue;
						TSharedPtr<FJsonObject> A = MakeShareable(new FJsonObject);
						A->SetStringField(TEXT("set"), AS->GetClass()->GetName());
						A->SetStringField(TEXT("name"), P->GetName());
						A->SetNumberField(TEXT("base"), AttrData->GetBaseValue());
						A->SetNumberField(TEXT("current"), AttrData->GetCurrentValue());
						AttrArr.Add(MakeShareable(new FJsonValueObject(A)));
					}
				}
			}
		}
		Res->SetArrayField(TEXT("gas_attributes"), AttrArr);
	}

	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Res.ToSharedRef(), Writer);
}

void HandleTeleportPlayer(const FString& LocationStr, const FString& RotationStr,
	FString& OutJsonString, FString& OutError)
{
	UWorld* W = GetPIEWorld(OutError);
	if (!W) return;
	APlayerController* PC = W->GetFirstPlayerController();
	if (!PC) { OutError = TEXT("No PlayerController"); return; }
	APawn* Pawn = PC->GetPawn();

	FVector Loc;
	if (!ParseVector3(LocationStr, Loc)) { OutError = TEXT("Invalid location. Use (X,Y,Z)"); return; }
	FRotator Rot = FRotator::ZeroRotator;
	if (!RotationStr.IsEmpty()) ParseRotator(RotationStr, Rot);

	bool bOk = false;
	if (Pawn)
	{
		bOk = Pawn->TeleportTo(Loc, Rot, false, true);
		if (bOk) PC->SetControlRotation(Rot);
	}
	else
	{
		if (AActor* VT = PC->GetViewTarget())
		{
			bOk = VT->TeleportTo(Loc, Rot, false, true);
		}
	}

	OutJsonString = FString::Printf(
		TEXT("{\"success\":%s,\"location\":\"(%.1f,%.1f,%.1f)\",\"rotation\":\"(%.1f,%.1f,%.1f)\"}"),
		bOk ? TEXT("true") : TEXT("false"),
		Loc.X, Loc.Y, Loc.Z, Rot.Pitch, Rot.Yaw, Rot.Roll);
	if (!bOk) OutError = TEXT("TeleportTo failed (collision / no movable target)");
}

void HandleSpawnTestActor(const FString& ClassPath, const FString& LocationStr,
	const FString& RotationStr, FString& OutJsonString, FString& OutError)
{
	UWorld* W = GetPIEWorld(OutError);
	if (!W) return;
	if (ClassPath.IsEmpty()) { OutError = TEXT("class_path is required"); return; }

	UClass* Cls = LoadObject<UClass>(nullptr, *ClassPath);
	if (!Cls)
	{
		FString Adjusted = ClassPath.EndsWith(TEXT("_C")) ? ClassPath : (ClassPath + TEXT("_C"));
		Cls = LoadObject<UClass>(nullptr, *Adjusted);
	}
	if (!Cls) { OutError = FString::Printf(TEXT("Could not load class '%s'"), *ClassPath); return; }
	if (!Cls->IsChildOf<AActor>()) { OutError = TEXT("class is not an AActor"); return; }

	FVector Loc; if (!ParseVector3(LocationStr, Loc)) { OutError = TEXT("Invalid location. Use (X,Y,Z)"); return; }
	FRotator Rot = FRotator::ZeroRotator;
	if (!RotationStr.IsEmpty()) ParseRotator(RotationStr, Rot);

	FActorSpawnParameters SP;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	AActor* Spawned = W->SpawnActor<AActor>(Cls, Loc, Rot, SP);
	if (!Spawned) { OutError = TEXT("SpawnActor returned null"); return; }

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"actor\":\"%s\",\"label\":\"%s\",\"class\":\"%s\"}"),
		*Spawned->GetName(), *Spawned->GetActorLabel(), *Cls->GetName());
}

void HandleGetGasAttributes(const FString& ActorLabel,
	FString& OutJsonString, FString& OutError)
{
	UWorld* W = GetPIEWorld(OutError);
	if (!W) return;

	AActor* Actor = nullptr;
	if (ActorLabel.IsEmpty())
	{
		if (APlayerController* PC = W->GetFirstPlayerController()) Actor = PC->GetPawn();
	}
	else
	{
		Actor = FindPIEActor(W, ActorLabel);
	}
	if (!Actor) { OutError = TEXT("Actor not found (or no player pawn)"); return; }

	UAbilitySystemComponent* ASC = Actor->FindComponentByClass<UAbilitySystemComponent>();
	if (!ASC) { OutError = FString::Printf(TEXT("Actor '%s' has no AbilitySystemComponent"), *Actor->GetName()); return; }

	TArray<TSharedPtr<FJsonValue>> AttrArr;
	for (const UAttributeSet* AS : ASC->GetSpawnedAttributes())
	{
		if (!AS) continue;
		for (TFieldIterator<FProperty> It(AS->GetClass()); It; ++It)
		{
			FStructProperty* SP = CastField<FStructProperty>(*It);
			if (!SP || SP->Struct != FGameplayAttributeData::StaticStruct()) continue;
			const FGameplayAttributeData* AttrData = SP->ContainerPtrToValuePtr<const FGameplayAttributeData>(AS);
			if (!AttrData) continue;
			TSharedPtr<FJsonObject> A = MakeShareable(new FJsonObject);
			A->SetStringField(TEXT("set"), AS->GetClass()->GetName());
			A->SetStringField(TEXT("name"), SP->GetName());
			A->SetNumberField(TEXT("base"), AttrData->GetBaseValue());
			A->SetNumberField(TEXT("current"), AttrData->GetCurrentValue());
			AttrArr.Add(MakeShareable(new FJsonValueObject(A)));
		}
	}

	TSharedPtr<FJsonObject> Res = MakeShareable(new FJsonObject);
	Res->SetBoolField(TEXT("success"), true);
	Res->SetStringField(TEXT("actor"), Actor->GetName());
	Res->SetArrayField(TEXT("attributes"), AttrArr);
	Res->SetNumberField(TEXT("count"), AttrArr.Num());
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Res.ToSharedRef(), Writer);
}

void HandleGenerateTestReport(const FString& Title, const FString& OutputPath,
	const TSharedPtr<FJsonObject>& Sections, FString& OutJsonString, FString& OutError)
{
	const FString FinalTitle = Title.IsEmpty() ? TEXT("Play Test Report") : Title;

	FString Path = OutputPath;
	if (Path.IsEmpty())
	{
		const FString Stamp = FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
		Path = FPaths::ProjectSavedDir() / TEXT("PlayTestReports") / FString::Printf(TEXT("report_%s.md"), *Stamp);
	}
	Path = FPaths::ConvertRelativePathToFull(Path);

	FString Body;
	Body += FString::Printf(TEXT("# %s\n\n"), *FinalTitle);
	Body += FString::Printf(TEXT("_Generated %s_\n\n"),
		*FDateTime::Now().ToString(TEXT("%Y-%m-%d %H:%M:%S")));

	if (Sections.IsValid())
	{
		for (const auto& KV : Sections->Values)
		{
			Body += FString::Printf(TEXT("## %s\n\n"), *KV.Key);
			if (!KV.Value.IsValid()) { Body += TEXT("\n"); continue; }
			switch (KV.Value->Type)
			{
			case EJson::String:
				Body += KV.Value->AsString();
				Body += TEXT("\n\n");
				break;
			case EJson::Number:
				Body += FString::SanitizeFloat(KV.Value->AsNumber());
				Body += TEXT("\n\n");
				break;
			case EJson::Boolean:
				Body += KV.Value->AsBool() ? TEXT("true") : TEXT("false");
				Body += TEXT("\n\n");
				break;
			case EJson::Array:
				for (const TSharedPtr<FJsonValue>& V : KV.Value->AsArray())
				{
					Body += TEXT("- ");
					Body += V.IsValid() && V->Type == EJson::String ? V->AsString() : FString();
					Body += TEXT("\n");
				}
				Body += TEXT("\n");
				break;
			case EJson::Object:
				{
					FString Pretty;
					TSharedRef<TJsonWriter<>> JW = TJsonWriterFactory<>::Create(&Pretty);
					FJsonSerializer::Serialize(KV.Value->AsObject().ToSharedRef(), JW);
					Body += TEXT("```json\n");
					Body += Pretty;
					Body += TEXT("\n```\n\n");
				}
				break;
			default:
				break;
			}
		}
	}

	if (!FFileHelper::SaveStringToFile(Body, *Path))
	{
		OutError = FString::Printf(TEXT("Failed to write report to '%s'"), *Path);
		return;
	}

	const FString Safe = Path.Replace(TEXT("\\"), TEXT("/"));
	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"file_path\":\"%s\",\"bytes\":%d}"),
		*Safe, Body.Len());
}

namespace
{
	void ReadVecArg(const TSharedPtr<FJsonObject>& Args, const TCHAR* Field, FString& Out)
	{
		if (!Args.IsValid()) return;
		if (Args->TryGetStringField(Field, Out)) return;
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (Args->TryGetArrayField(Field, Arr) && Arr && Arr->Num() >= 3)
		{
			Out = FString::Printf(TEXT("(%f, %f, %f)"),
				(*Arr)[0]->AsNumber(), (*Arr)[1]->AsNumber(), (*Arr)[2]->AsNumber());
		}
	}
}

void HandlePausePieFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	bool bPause = true;
	if (Args.IsValid()) Args->TryGetBoolField(TEXT("paused"), bPause);
	HandlePausePie(bPause, OutJsonString, OutError);
}

void HandleSetTimeDilationFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	double D = 1.0;
	if (Args.IsValid()) Args->TryGetNumberField(TEXT("dilation"), D);
	HandleSetTimeDilation((float)D, OutJsonString, OutError);
}

void HandleLookAtTargetFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	double Dist = 0.0;
	FString Channel;
	if (Args.IsValid())
	{
		Args->TryGetNumberField(TEXT("max_distance"), Dist);
		Args->TryGetStringField(TEXT("channel"), Channel);
	}
	HandleLookAtTarget((float)Dist, Channel, OutJsonString, OutError);
}

void HandleIsPlayerStuckFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	double Win = 3.0, Thresh = 50.0;
	if (Args.IsValid())
	{
		Args->TryGetNumberField(TEXT("window_seconds"), Win);
		Args->TryGetNumberField(TEXT("move_threshold"), Thresh);
	}
	HandleIsPlayerStuck((float)Win, (float)Thresh, OutJsonString, OutError);
}

void HandleSimulateMouseDeltaFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	double DX = 0, DY = 0;
	Args->TryGetNumberField(TEXT("delta_x"), DX);
	Args->TryGetNumberField(TEXT("delta_y"), DY);
	HandleSimulateMouseDelta((float)DX, (float)DY, OutJsonString, OutError);
}

void HandleSimulateInputAxisFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString IAPath;
	Args->TryGetStringField(TEXT("input_action"), IAPath);
	if (IAPath.IsEmpty()) Args->TryGetStringField(TEXT("ia_path"), IAPath);
	double VX = 0, VY = 0, VZ = 0;
	const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
	if (Args->TryGetArrayField(TEXT("value"), Arr) && Arr)
	{
		if (Arr->Num() >= 1) VX = (*Arr)[0]->AsNumber();
		if (Arr->Num() >= 2) VY = (*Arr)[1]->AsNumber();
		if (Arr->Num() >= 3) VZ = (*Arr)[2]->AsNumber();
	}
	else
	{
		Args->TryGetNumberField(TEXT("value"), VX);
	}
	Args->TryGetNumberField(TEXT("value_x"), VX);
	Args->TryGetNumberField(TEXT("value_y"), VY);
	Args->TryGetNumberField(TEXT("value_z"), VZ);
	HandleSimulateInputAxis(IAPath, (float)VX, (float)VY, (float)VZ, OutJsonString, OutError);
}

void HandleSimulateInputBurstFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	const TArray<TSharedPtr<FJsonValue>>* StepsArr = nullptr;
	if (!Args->TryGetArrayField(TEXT("steps"), StepsArr) || !StepsArr)
	{
		OutError = TEXT("steps array required: [{key, event_type, offset_ms}]");
		return;
	}
	HandleSimulateInputBurst(*StepsArr, OutJsonString, OutError);
}

void HandleGetVisibleWidgetsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	double Max = 50.0;
	if (Args.IsValid()) Args->TryGetNumberField(TEXT("max_widgets"), Max);
	HandleGetVisibleWidgets((int32)Max, OutJsonString, OutError);
}

void HandleActorsNearPlayerFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	double Radius = 500.0;
	FString ClassFilter;
	if (Args.IsValid())
	{
		Args->TryGetNumberField(TEXT("radius"), Radius);
		Args->TryGetStringField(TEXT("class_filter"), ClassFilter);
	}
	HandleActorsNearPlayer((float)Radius, ClassFilter, OutJsonString, OutError);
}

void HandleGetPlayerRuntimeStateFromArgs(const TSharedPtr<FJsonObject>& ,
	FString& OutJsonString, FString& OutError)
{
	HandleGetPlayerRuntimeState(OutJsonString, OutError);
}

void HandleTeleportPlayerFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString Loc, Rot;
	ReadVecArg(Args, TEXT("location"), Loc);
	ReadVecArg(Args, TEXT("rotation"), Rot);
	HandleTeleportPlayer(Loc, Rot, OutJsonString, OutError);
}

void HandleSpawnTestActorFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString ClassPath, Loc, Rot;
	Args->TryGetStringField(TEXT("class_path"), ClassPath);
	if (ClassPath.IsEmpty()) Args->TryGetStringField(TEXT("class"), ClassPath);
	ReadVecArg(Args, TEXT("location"), Loc);
	ReadVecArg(Args, TEXT("rotation"), Rot);
	HandleSpawnTestActor(ClassPath, Loc, Rot, OutJsonString, OutError);
}

void HandleGetGasAttributesFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString Label;
	if (Args.IsValid()) Args->TryGetStringField(TEXT("actor_label"), Label);
	HandleGetGasAttributes(Label, OutJsonString, OutError);
}

void HandleGenerateTestReportFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString Title, OutPath;
	Args->TryGetStringField(TEXT("title"), Title);
	Args->TryGetStringField(TEXT("output_path"), OutPath);
	const TSharedPtr<FJsonObject>* SectionsObj = nullptr;
	TSharedPtr<FJsonObject> Sections;
	if (Args->TryGetObjectField(TEXT("sections"), SectionsObj)) Sections = *SectionsObj;
	HandleGenerateTestReport(Title, OutPath, Sections, OutJsonString, OutError);
}

}
