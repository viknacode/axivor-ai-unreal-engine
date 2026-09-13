// Copyright 2026, BlueprintsLab, All rights reserved.

#include "Tools/LevelStreamingTools.h"

#include "Engine/LevelStreamingDynamic.h"
#include "Engine/LevelStreamingAlwaysLoaded.h"
#include "Engine/LevelStreamingVolume.h"
#include "EditorLevelUtils.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "Editor.h"
#include "Components/BoxComponent.h"
#include "Serialization/JsonSerializer.h"

#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"

namespace LevelStreamingTools
{

static UWorld* GetEditorWorld(FString& OutError)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World) OutError = TEXT("No editor world available");
	return World;
}

static ULevelStreaming* FindStreamingLevel(UWorld* World, const FString& PackagePath)
{
	for (ULevelStreaming* LS : World->GetStreamingLevels())
	{
		if (LS && LS->GetWorldAssetPackageName().Equals(PackagePath, ESearchCase::IgnoreCase))
			return LS;
	}
	return nullptr;
}

void HandleAddStreamingLevel(const FString& LevelPackagePath, const FString& StreamingType,
	float OffsetX, float OffsetY, float OffsetZ,
	FString& OutJsonString, FString& OutError)
{
	UWorld* World = GetEditorWorld(OutError);
	if (!World) return;

	if (FindStreamingLevel(World, LevelPackagePath))
	{
		OutJsonString = FString::Printf(TEXT("{\"success\":true,\"message\":\"Level already streaming\",\"package\":\"%s\"}"),
			*LevelPackagePath);
		return;
	}

	TSubclassOf<ULevelStreaming> StreamingClass = ULevelStreamingDynamic::StaticClass();
	if (StreamingType.Equals(TEXT("AlwaysLoaded"), ESearchCase::IgnoreCase))
		StreamingClass = ULevelStreamingAlwaysLoaded::StaticClass();

	ULevelStreaming* NewStreaming = UEditorLevelUtils::AddLevelToWorld(
		World, *LevelPackagePath, StreamingClass);

	if (!NewStreaming)
	{
		OutError = FString::Printf(TEXT("Failed to add streaming level '%s'. Ensure the level asset exists."), *LevelPackagePath);
		return;
	}

	FTransform LevelTransform(FRotator::ZeroRotator, FVector(OffsetX, OffsetY, OffsetZ));
	NewStreaming->LevelTransform = LevelTransform;
	World->MarkPackageDirty();

	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"package\":\"%s\",\"streaming_type\":\"%s\",\"offset\":[%f,%f,%f]}"),
		*LevelPackagePath, *StreamingType, OffsetX, OffsetY, OffsetZ);
}

void HandleSetStreamingLevelTransform(const FString& LevelPackagePath,
	float OffsetX, float OffsetY, float OffsetZ,
	float RotPitch, float RotYaw, float RotRoll,
	float ScaleX, float ScaleY, float ScaleZ,
	FString& OutJsonString, FString& OutError)
{
	UWorld* World = GetEditorWorld(OutError);
	if (!World) return;

	ULevelStreaming* LS = FindStreamingLevel(World, LevelPackagePath);
	if (!LS)
	{
		OutError = FString::Printf(TEXT("Streaming level '%s' not found in world"), *LevelPackagePath);
		return;
	}

	LS->LevelTransform = FTransform(
		FRotator(RotPitch, RotYaw, RotRoll),
		FVector(OffsetX, OffsetY, OffsetZ),
		FVector(ScaleX, ScaleY, ScaleZ));
	World->MarkPackageDirty();

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"package\":\"%s\",\"offset\":[%f,%f,%f],\"rotation\":[%f,%f,%f],\"scale\":[%f,%f,%f]}"),
		*LevelPackagePath, OffsetX, OffsetY, OffsetZ, RotPitch, RotYaw, RotRoll, ScaleX, ScaleY, ScaleZ);
}

void HandleCreateLevelStreamingVolume(const FString& LevelPackagePath,
	float LocX, float LocY, float LocZ,
	float ExtentX, float ExtentY, float ExtentZ,
	const FString& Usage, FString& OutJsonString, FString& OutError)
{
	UWorld* World = GetEditorWorld(OutError);
	if (!World) return;

	FActorSpawnParameters Params;
	ALevelStreamingVolume* Vol = World->SpawnActor<ALevelStreamingVolume>(
		ALevelStreamingVolume::StaticClass(), FVector(LocX, LocY, LocZ), FRotator::ZeroRotator, Params);
	if (!Vol) { OutError = TEXT("Failed to spawn LevelStreamingVolume"); return; }

	Vol->SetActorScale3D(FVector(ExtentX / 100.0f, ExtentY / 100.0f, ExtentZ / 100.0f));

	Vol->StreamingLevelNames.Add(FName(*LevelPackagePath));

	EStreamingVolumeUsage UsageEnum = SVB_LoadingAndVisibility;
	if (Usage.Equals(TEXT("Loading"), ESearchCase::IgnoreCase))                    UsageEnum = SVB_Loading;
	else if (Usage.Equals(TEXT("VisibilityBlockingOnLoad"), ESearchCase::IgnoreCase)) UsageEnum = SVB_VisibilityBlockingOnLoad;
	else if (Usage.Equals(TEXT("BlockingOnLoad"), ESearchCase::IgnoreCase))           UsageEnum = SVB_BlockingOnLoad;
	else if (Usage.Equals(TEXT("LoadingNotVisible"), ESearchCase::IgnoreCase))        UsageEnum = SVB_LoadingNotVisible;

	Vol->StreamingUsage = UsageEnum;
	World->MarkPackageDirty();
	GEditor->NoteSelectionChange();

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"level_package\":\"%s\",\"usage\":\"%s\",\"location\":[%f,%f,%f]}"),
		*LevelPackagePath, *Usage, LocX, LocY, LocZ);
}

void HandleRemoveStreamingLevel(const FString& LevelPackagePath, FString& OutJsonString, FString& OutError)
{

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World) { OutError = TEXT("No editor world"); return; }

	bool bFound = false;
	for (ULevelStreaming* SL : World->GetStreamingLevels())
	{
		if (!SL) continue;
		if (SL->GetWorldAssetPackageName().Equals(LevelPackagePath, ESearchCase::IgnoreCase) ||
			SL->GetWorldAssetPackageName().EndsWith(LevelPackagePath))
		{
			EditorLevelUtils::RemoveLevelFromWorld(SL->GetLoadedLevel());
			bFound = true;
			break;
		}
	}

	if (!bFound) { OutError = FString::Printf(TEXT("Streaming level '%s' not found"), *LevelPackagePath); return; }

	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"removed_level\":\"%s\"}"), *LevelPackagePath);
}

void HandleGetStreamingLevels(FString& OutJsonString, FString& OutError)
{

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World) { OutError = TEXT("No editor world"); return; }

	TSharedPtr<FJsonObject> Res = MakeShareable(new FJsonObject());
	Res->SetBoolField(TEXT("success"), true);

	TArray<TSharedPtr<FJsonValue>> LevelsArray;
	for (ULevelStreaming* SL : World->GetStreamingLevels())
	{
		if (!SL) continue;
		TSharedPtr<FJsonObject> LevelObj = MakeShareable(new FJsonObject());
		LevelObj->SetStringField(TEXT("package_name"), SL->GetWorldAssetPackageName());
		LevelObj->SetStringField(TEXT("class"), SL->GetClass()->GetName());
		LevelObj->SetBoolField(TEXT("should_be_visible"), SL->GetShouldBeVisibleFlag());
		LevelObj->SetBoolField(TEXT("is_loaded"), SL->GetLoadedLevel() != nullptr);

		FTransform Transform = SL->LevelTransform;
		FVector Loc = Transform.GetLocation();
		LevelObj->SetStringField(TEXT("offset"), FString::Printf(TEXT("%.0f,%.0f,%.0f"), Loc.X, Loc.Y, Loc.Z));

		LevelsArray.Add(MakeShareable(new FJsonValueObject(LevelObj)));
	}

	Res->SetArrayField(TEXT("streaming_levels"), LevelsArray);
	Res->SetNumberField(TEXT("count"), LevelsArray.Num());

	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Res.ToSharedRef(), W);
}

void HandleSetStreamingLevelVisibility(const FString& LevelPackagePath, bool bVisible, FString& OutJsonString, FString& OutError)
{

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World) { OutError = TEXT("No editor world"); return; }

	bool bFound = false;
	for (ULevelStreaming* SL : World->GetStreamingLevels())
	{
		if (!SL) continue;
		if (SL->GetWorldAssetPackageName().Equals(LevelPackagePath, ESearchCase::IgnoreCase) ||
			SL->GetWorldAssetPackageName().EndsWith(LevelPackagePath))
		{
			SL->SetShouldBeVisible(bVisible);
			bFound = true;
			break;
		}
	}

	if (!bFound) { OutError = FString::Printf(TEXT("Streaming level '%s' not found"), *LevelPackagePath); return; }

	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"level\":\"%s\",\"visible\":%s}"),
		*LevelPackagePath, bVisible ? TEXT("true") : TEXT("false"));
}

void HandleAddStreamingLevelFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString LevelPath, StreamingType;
	double OffX = 0, OffY = 0, OffZ = 0;
	Args->TryGetStringField(TEXT("level_path"), LevelPath);
	Args->TryGetStringField(TEXT("streaming_type"), StreamingType);
	if (StreamingType.IsEmpty()) StreamingType = TEXT("Dynamic");
	Args->TryGetNumberField(TEXT("offset_x"), OffX);
	Args->TryGetNumberField(TEXT("offset_y"), OffY);
	Args->TryGetNumberField(TEXT("offset_z"), OffZ);
	HandleAddStreamingLevel(LevelPath, StreamingType, (float)OffX, (float)OffY, (float)OffZ, OutJsonString, OutError);
}

void HandleSetStreamingLevelTransformFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString LevelPath;
	double OffX = 0, OffY = 0, OffZ = 0;
	double RotPitch = 0, RotYaw = 0, RotRoll = 0;
	double ScaleX = 1, ScaleY = 1, ScaleZ = 1;
	Args->TryGetStringField(TEXT("level_path"), LevelPath);
	Args->TryGetNumberField(TEXT("offset_x"), OffX);
	Args->TryGetNumberField(TEXT("offset_y"), OffY);
	Args->TryGetNumberField(TEXT("offset_z"), OffZ);
	Args->TryGetNumberField(TEXT("rot_pitch"), RotPitch);
	Args->TryGetNumberField(TEXT("rot_yaw"), RotYaw);
	Args->TryGetNumberField(TEXT("rot_roll"), RotRoll);
	Args->TryGetNumberField(TEXT("scale_x"), ScaleX);
	Args->TryGetNumberField(TEXT("scale_y"), ScaleY);
	Args->TryGetNumberField(TEXT("scale_z"), ScaleZ);
	HandleSetStreamingLevelTransform(LevelPath,
		(float)OffX, (float)OffY, (float)OffZ,
		(float)RotPitch, (float)RotYaw, (float)RotRoll,
		(float)ScaleX, (float)ScaleY, (float)ScaleZ,
		OutJsonString, OutError);
}

void HandleSetStreamingLevelLoadedFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString LevelPath;
	bool bShouldBeLoaded = true;
	Args->TryGetStringField(TEXT("level_path"), LevelPath);
	Args->TryGetBoolField(TEXT("loaded"), bShouldBeLoaded);

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World) { OutError = TEXT("No editor world"); return; }

	for (ULevelStreaming* SL : World->GetStreamingLevels())
	{
		if (!SL) continue;
		if (SL->GetWorldAssetPackageName().Equals(LevelPath, ESearchCase::IgnoreCase) ||
			SL->GetWorldAssetPackageName().EndsWith(LevelPath))
		{
			SL->SetShouldBeLoaded(bShouldBeLoaded);
			OutJsonString = FString::Printf(TEXT("{\"success\":true,\"level\":\"%s\",\"loaded\":%s}"),
				*LevelPath, bShouldBeLoaded ? TEXT("true") : TEXT("false"));
			return;
		}
	}
	OutError = FString::Printf(TEXT("Streaming level '%s' not found"), *LevelPath);
}

void HandleCreateLevelStreamingVolumeFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString LevelPath, Usage;
	double LocX = 0, LocY = 0, LocZ = 0, ExtX = 500, ExtY = 500, ExtZ = 500;
	Args->TryGetStringField(TEXT("level_path"), LevelPath);
	Args->TryGetStringField(TEXT("usage"), Usage);
	if (Usage.IsEmpty()) Usage = TEXT("LoadingAndVisibility");
	Args->TryGetNumberField(TEXT("location_x"), LocX);
	Args->TryGetNumberField(TEXT("location_y"), LocY);
	Args->TryGetNumberField(TEXT("location_z"), LocZ);
	Args->TryGetNumberField(TEXT("extent_x"), ExtX);
	Args->TryGetNumberField(TEXT("extent_y"), ExtY);
	Args->TryGetNumberField(TEXT("extent_z"), ExtZ);
	HandleCreateLevelStreamingVolume(LevelPath, (float)LocX, (float)LocY, (float)LocZ,
		(float)ExtX, (float)ExtY, (float)ExtZ, Usage, OutJsonString, OutError);
}

void HandleRemoveStreamingLevelFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString LevelPath;
	Args->TryGetStringField(TEXT("level_path"), LevelPath);
	HandleRemoveStreamingLevel(LevelPath, OutJsonString, OutError);
}

void HandleGetStreamingLevelsFromArgs(const TSharedPtr<FJsonObject>& ,
	FString& OutJsonString, FString& OutError)
{
	HandleGetStreamingLevels(OutJsonString, OutError);
}

void HandleSetStreamingLevelVisibilityFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString LevelPath;
	bool bVisible = true;
	Args->TryGetStringField(TEXT("level_path"), LevelPath);
	Args->TryGetBoolField(TEXT("visible"), bVisible);
	HandleSetStreamingLevelVisibility(LevelPath, bVisible, OutJsonString, OutError);
}

}
