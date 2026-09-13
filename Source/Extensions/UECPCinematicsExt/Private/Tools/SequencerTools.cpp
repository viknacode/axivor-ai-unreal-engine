// Copyright 2026, BlueprintsLab, All rights reserved.

#include "Tools/SequencerTools.h"
#include "Misc/EngineVersionComparison.h"
#include "Managers/SettingsManager.h"
#include "Managers/EditorProfileSync.h"
#include "Tools/BatchToolHelper.h"

#include "LevelSequence.h"
#include "MovieScene.h"
#include "MovieSceneSection.h"
#include "Tracks/MovieScene3DTransformTrack.h"
#include "Sections/MovieScene3DTransformSection.h"
#include "Channels/MovieSceneDoubleChannel.h"
#include "Channels/MovieSceneChannelProxy.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "EditorAssetLibrary.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "UObject/Package.h"
#include "Misc/PackageName.h"
#include "UObject/UObjectIterator.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "Tracks/MovieSceneCameraCutTrack.h"
#include "Sections/MovieSceneCameraCutSection.h"
#include "Tracks/MovieSceneAudioTrack.h"
#include "Sections/MovieSceneAudioSection.h"
#include "Tracks/MovieSceneEventTrack.h"
#include "Tracks/MovieSceneFloatTrack.h"
#include "Tracks/MovieSceneVisibilityTrack.h"
#include "Tracks/MovieSceneSkeletalAnimationTrack.h"
#include "Sections/MovieSceneSkeletalAnimationSection.h"
#include "Animation/AnimSequenceBase.h"
#include "Sound/SoundBase.h"
#include "Tracks/MovieSceneSubTrack.h"
#include "Sections/MovieSceneSubSection.h"
#include "MovieSceneObjectBindingID.h"
#include "Serialization/JsonSerializer.h"
#include "MovieSceneSequenceID.h"
#include "GameFramework/Actor.h"

#include "Sections/MovieSceneFloatSection.h"
#include "Sections/MovieSceneBoolSection.h"
#include "Tracks/MovieSceneSpawnTrack.h"

#include "Tracks/MovieSceneColorTrack.h"
#include "Sections/MovieSceneColorSection.h"
#include "Tracks/MovieSceneLevelVisibilityTrack.h"
#include "Sections/MovieSceneLevelVisibilitySection.h"
#include "Tracks/MovieSceneBoolTrack.h"
#include "Tracks/MovieSceneIntegerTrack.h"
#include "Tracks/MovieScenePropertyTrack.h"
#include "MovieSceneNameableTrack.h"
#include "MovieScenePossessable.h"
#include "MovieSceneSpawnable.h"
#include "Tracks/MovieSceneCameraShakeTrack.h"
#include "Sections/MovieSceneCameraShakeSection.h"
#include "Camera/CameraShakeBase.h"
#include "Sections/MovieSceneIntegerSection.h"
#include "Evaluation/Blending/MovieSceneBlendType.h"
#include "MoviePipelineRenderPass.h"

#include "MoviePipelineQueue.h"
#include "MoviePipelinePrimaryConfig.h"
#include "MoviePipelineOutputSetting.h"
#include "MoviePipelineAntiAliasingSetting.h"
#include "MoviePipelineQueueEngineSubsystem.h"
#include "MoviePipelineDeferredPasses.h"
#include "MoviePipelineImageSequenceOutput.h"
#include "MoviePipelinePIEExecutor.h"

#include "CameraRig_Rail.h"
#include "CameraRig_Crane.h"
#include "Subsystems/EditorActorSubsystem.h"

#include "ControlRig.h"
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7)
#include "ControlRigBlueprintLegacy.h"
#else
#include "ControlRigBlueprint.h"
#endif
#include "ControlRigBlueprintGeneratedClass.h"
#include "ControlRigSequencerEditorLibrary.h"
#include "Sequencer/MovieSceneControlRigParameterTrack.h"
#include "Sequencer/MovieSceneControlRigParameterSection.h"
#include "Rigs/RigHierarchy.h"
#include "Rigs/RigHierarchyElements.h"
#include "Rigs/RigHierarchyDefines.h"
#include "MovieSceneBindingProxy.h"

namespace SequencerTools
{

void HandleCreateLevelSequence(const FString& AssetName, const FString& SavePath,
	FString& OutJsonString, FString& OutError)
{

	if (!UEditorAssetLibrary::DoesDirectoryExist(SavePath))
		UEditorAssetLibrary::MakeDirectory(SavePath);

	if (AssetName.IsEmpty()) { OutError = TEXT("name is required"); return; }
	FString PackagePath = SavePath;
	while (PackagePath.EndsWith(TEXT("/"))) PackagePath = PackagePath.LeftChop(1);
	PackagePath += TEXT("/") + AssetName;

	if (FPackageName::DoesPackageExist(PackagePath))
	{
		OutJsonString = FString::Printf(
			TEXT("{\"success\":true,\"asset_path\":\"%s\",\"message\":\"LevelSequence already exists.\"}"),
			*PackagePath);
		return;
	}

	UClass* FactoryClass = nullptr;
	for (TObjectIterator<UClass> It; It; ++It)
	{
		if (It->GetName() == TEXT("LevelSequenceFactoryNew"))
		{
			FactoryClass = *It;
			break;
		}
	}

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	UObject* NewAsset = nullptr;

	if (FactoryClass)
	{
		UFactory* Factory = NewObject<UFactory>(GetTransientPackage(), FactoryClass);
		NewAsset = AssetTools.CreateAsset(AssetName, SavePath, ULevelSequence::StaticClass(), Factory);
	}

	if (!NewAsset)
	{
		UPackage* Package = CreatePackage(*PackagePath);
		ULevelSequence* LS = NewObject<ULevelSequence>(Package, FName(*AssetName), RF_Public | RF_Standalone | RF_Transactional);
		if (LS)
		{
			LS->Initialize();
			FAssetRegistryModule::AssetCreated(LS);
			LS->MarkPackageDirty();
			NewAsset = LS;
		}
	}

	if (!NewAsset) { OutError = TEXT("Failed to create LevelSequence"); return; }

	UEditorAssetLibrary::SaveAsset(NewAsset->GetPathName(), false);

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"asset_path\":\"%s\",\"message\":\"LevelSequence created. Use add_sequence_actor_binding, add_sequence_transform_track, and add_sequence_keyframe to animate actors.\"}"),
		*NewAsset->GetPathName());
}

void HandleAddSequenceActorBinding(const FString& SequencePath, const FString& ActorLabel,
	FString& OutJsonString, FString& OutError)
{

	ULevelSequence* Sequence = Cast<ULevelSequence>(UEditorAssetLibrary::LoadAsset(SequencePath));
	if (!Sequence) { OutError = TEXT("LevelSequence not found: ") + SequencePath; return; }

	UMovieScene* MovieScene = Sequence->GetMovieScene();
	if (!MovieScene) { OutError = TEXT("MovieScene is null"); return; }

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World) { OutError = TEXT("No editor world available"); return; }

	AActor* TargetActor = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->GetActorLabel() == ActorLabel) { TargetActor = *It; break; }
	}

	if (!TargetActor) { OutError = FString::Printf(TEXT("Actor '%s' not found in level"), *ActorLabel); return; }

	FGuid BindingGuid = MovieScene->AddPossessable(ActorLabel, TargetActor->GetClass());
	Sequence->BindPossessableObject(BindingGuid, *TargetActor, World);

	MovieScene->Modify();
	Sequence->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(SequencePath, false);

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"actor\":\"%s\",\"binding_guid\":\"%s\",\"message\":\"Actor binding added. Use add_sequence_transform_track to add animation tracks.\"}"),
		*ActorLabel, *BindingGuid.ToString());
}

void HandleAddSequenceTransformTrack(const FString& SequencePath, const FString& ActorLabel,
	FString& OutJsonString, FString& OutError)
{

	ULevelSequence* Sequence = Cast<ULevelSequence>(UEditorAssetLibrary::LoadAsset(SequencePath));
	if (!Sequence) { OutError = TEXT("LevelSequence not found: ") + SequencePath; return; }

	UMovieScene* MovieScene = Sequence->GetMovieScene();
	if (!MovieScene) { OutError = TEXT("MovieScene is null"); return; }

	FGuid BindingGuid;
	for (int32 i = 0; i < MovieScene->GetPossessableCount(); ++i)
	{
		if (MovieScene->GetPossessable(i).GetName() == ActorLabel)
		{
			BindingGuid = MovieScene->GetPossessable(i).GetGuid();
			break;
		}
	}
	if (!BindingGuid.IsValid())
	{
		for (int32 i = 0; i < MovieScene->GetSpawnableCount(); ++i)
		{
			if (MovieScene->GetSpawnable(i).GetName() == ActorLabel)
			{
				BindingGuid = MovieScene->GetSpawnable(i).GetGuid();
				break;
			}
		}
	}

	if (!BindingGuid.IsValid()) { OutError = FString::Printf(TEXT("No binding found for actor '%s'. Call add_sequence_actor_binding first."), *ActorLabel); return; }

	UMovieScene3DTransformTrack* TransformTrack = MovieScene->AddTrack<UMovieScene3DTransformTrack>(BindingGuid);
	if (!TransformTrack) { OutError = TEXT("Failed to add transform track (already exists or invalid binding)"); return; }

	UMovieScene3DTransformSection* Section = Cast<UMovieScene3DTransformSection>(TransformTrack->CreateNewSection());
	if (Section)
	{
		Section->SetRange(MovieScene->GetPlaybackRange());
		TransformTrack->AddSection(*Section);
	}

	MovieScene->Modify();
	Sequence->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(SequencePath, false);

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"actor\":\"%s\",\"track\":\"Transform\",\"message\":\"Transform track added. Use add_sequence_keyframe to set position/rotation at specific times.\"}"),
		*ActorLabel);
}

void HandleAddSequenceKeyframe(const FString& SequencePath, const FString& ActorLabel,
	float TimeSeconds,
	float LocationX, float LocationY, float LocationZ,
	float RotationPitch, float RotationYaw, float RotationRoll,
	float ScaleX, float ScaleY, float ScaleZ, bool bSetScale,
	FString& OutJsonString, FString& OutError)
{

	ULevelSequence* Sequence = Cast<ULevelSequence>(UEditorAssetLibrary::LoadAsset(SequencePath));
	if (!Sequence) { OutError = TEXT("LevelSequence not found: ") + SequencePath; return; }

	UMovieScene* MovieScene = Sequence->GetMovieScene();
	if (!MovieScene) { OutError = TEXT("MovieScene is null"); return; }

	FGuid BindingGuid;
	for (int32 i = 0; i < MovieScene->GetPossessableCount(); ++i)
	{
		if (MovieScene->GetPossessable(i).GetName() == ActorLabel)
		{
			BindingGuid = MovieScene->GetPossessable(i).GetGuid(); break;
		}
	}
	if (!BindingGuid.IsValid())
	{
		for (int32 i = 0; i < MovieScene->GetSpawnableCount(); ++i)
		{
			if (MovieScene->GetSpawnable(i).GetName() == ActorLabel)
			{
				BindingGuid = MovieScene->GetSpawnable(i).GetGuid(); break;
			}
		}
	}
	if (!BindingGuid.IsValid())
	{
		TArray<FString> Bindings;
		for (int32 i = 0; i < MovieScene->GetPossessableCount(); ++i)
			Bindings.Add(MovieScene->GetPossessable(i).GetName());
		for (int32 i = 0; i < MovieScene->GetSpawnableCount(); ++i)
			Bindings.Add(MovieScene->GetSpawnable(i).GetName() + TEXT(" (spawnable)"));
		FString BindingList = Bindings.Num() > 0 ? FString::Join(Bindings, TEXT(", ")) : TEXT("none");
		OutError = FString::Printf(TEXT("No binding for '%s'. Call add_sequence_actor_binding first. Available bindings: [%s]"), *ActorLabel, *BindingList);
		return;
	}

	UMovieScene3DTransformTrack* TransformTrack = MovieScene->FindTrack<UMovieScene3DTransformTrack>(BindingGuid);
	if (!TransformTrack) { OutError = TEXT("No transform track found. Call add_sequence_transform_track first."); return; }

	TArray<UMovieSceneSection*> Sections = TransformTrack->GetAllSections();
	UMovieScene3DTransformSection* Section = Sections.Num() > 0 ? Cast<UMovieScene3DTransformSection>(Sections[0]) : nullptr;
	if (!Section) { OutError = TEXT("No transform section found"); return; }

	FFrameNumber Frame = MovieScene->GetTickResolution().AsFrameNumber(TimeSeconds);

	FMovieSceneChannelProxy& ChannelProxy = Section->GetChannelProxy();
	TArrayView<FMovieSceneDoubleChannel*> Channels = ChannelProxy.GetChannels<FMovieSceneDoubleChannel>();
	if (Channels.Num() >= 6)
	{
		Channels[0]->AddLinearKey(Frame, LocationX);
		Channels[1]->AddLinearKey(Frame, LocationY);
		Channels[2]->AddLinearKey(Frame, LocationZ);
		Channels[3]->AddLinearKey(Frame, RotationRoll);
		Channels[4]->AddLinearKey(Frame, RotationPitch);
		Channels[5]->AddLinearKey(Frame, RotationYaw);
	}
	if (bSetScale && Channels.Num() >= 9)
	{
		Channels[6]->AddLinearKey(Frame, ScaleX);
		Channels[7]->AddLinearKey(Frame, ScaleY);
		Channels[8]->AddLinearKey(Frame, ScaleZ);
	}

	Section->Modify();
	MovieScene->Modify();
	Sequence->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(SequencePath, false);

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"actor\":\"%s\",\"time_seconds\":%.2f,\"frame\":%d,\"location\":[%.0f,%.0f,%.0f],\"rotation\":[%.1f,%.1f,%.1f],\"scale_set\":%s}"),
		*ActorLabel, TimeSeconds, Frame.Value, LocationX, LocationY, LocationZ, RotationPitch, RotationYaw, RotationRoll,
		bSetScale ? TEXT("true") : TEXT("false"));
}

void HandleGetSequenceSummary(const FString& SequencePath,
	FString& OutJsonString, FString& OutError)
{

	ULevelSequence* Sequence = Cast<ULevelSequence>(UEditorAssetLibrary::LoadAsset(SequencePath));
	if (!Sequence) { OutError = TEXT("LevelSequence not found: ") + SequencePath; return; }

	UMovieScene* MovieScene = Sequence->GetMovieScene();
	if (!MovieScene) { OutError = TEXT("MovieScene is null"); return; }

	FFrameRate Rate = MovieScene->GetDisplayRate();
	TRange<FFrameNumber> PlaybackRange = MovieScene->GetPlaybackRange();
	float DurationSeconds = PlaybackRange.HasUpperBound() && PlaybackRange.HasLowerBound() ?
		(float)(PlaybackRange.GetUpperBoundValue() - PlaybackRange.GetLowerBoundValue()).Value / Rate.AsDecimal() : 0.f;

	FString BindingsJson = TEXT("[");
	bool bFirst = true;
	for (int32 i = 0; i < MovieScene->GetPossessableCount(); ++i)
	{
		const FMovieScenePossessable& P = MovieScene->GetPossessable(i);
		if (!bFirst) BindingsJson += TEXT(",");
		BindingsJson += FString::Printf(TEXT("\"%s\""), *P.GetName());
		bFirst = false;
	}
	BindingsJson += TEXT("]");

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"asset_path\":\"%s\",\"frame_rate\":%.0f,\"duration_seconds\":%.2f,\"bindings\":%s}"),
		*SequencePath, Rate.AsDecimal(), DurationSeconds, *BindingsJson);
}

static ULevelSequence* LoadSeq(const FString& Path, FString& OutError)
{
	ULevelSequence* Seq = Cast<ULevelSequence>(UEditorAssetLibrary::LoadAsset(Path));
	if (!Seq) OutError = FString::Printf(TEXT("Could not load LevelSequence at '%s'"), *Path);
	return Seq;
}

static FFrameNumber SecondsToFrame(UMovieScene* MS, float Seconds)
{
	return MS->GetTickResolution().AsFrameNumber(Seconds);
}

static FGuid FindOrAddBinding(ULevelSequence* Seq, const FString& ActorLabel, FString& OutError)
{
	UMovieScene* MS = Seq->GetMovieScene();

	for (int32 i = 0; i < MS->GetPossessableCount(); ++i)
		if (MS->GetPossessable(i).GetName().Equals(ActorLabel, ESearchCase::IgnoreCase))
			return MS->GetPossessable(i).GetGuid();

	for (int32 i = 0; i < MS->GetSpawnableCount(); ++i)
		if (MS->GetSpawnable(i).GetName().Equals(ActorLabel, ESearchCase::IgnoreCase))
			return MS->GetSpawnable(i).GetGuid();

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (World)
	{
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (It->GetActorLabel().Equals(ActorLabel, ESearchCase::IgnoreCase))
			{
				FGuid Guid = MS->AddPossessable(ActorLabel, It->GetClass());
				Seq->BindPossessableObject(Guid, **It, World);
				return Guid;
			}
		}
	}
	OutError = FString::Printf(TEXT("Actor '%s' not found in level"), *ActorLabel);
	return FGuid();
}

void HandleAddSequenceCameraCutTrack(const FString& SequencePath, const FString& CameraActorLabel,
	float StartTimeSec, float EndTimeSec, FString& OutJsonString, FString& OutError)
{
	ULevelSequence* Seq = LoadSeq(SequencePath, OutError);
	if (!Seq) return;
	UMovieScene* MS = Seq->GetMovieScene();

	UMovieSceneCameraCutTrack* CutTrack = MS->FindTrack<UMovieSceneCameraCutTrack>();
	if (!CutTrack)
		CutTrack = MS->AddTrack<UMovieSceneCameraCutTrack>();
	if (!CutTrack) { OutError = TEXT("Failed to create camera cut track"); return; }

	FGuid CameraGuid;
	if (!CameraActorLabel.IsEmpty())
		CameraGuid = FindOrAddBinding(Seq, CameraActorLabel, OutError);

	UMovieSceneCameraCutSection* Section = Cast<UMovieSceneCameraCutSection>(CutTrack->CreateNewSection());
	if (!Section) { OutError = TEXT("Failed to create camera cut section"); return; }

	FFrameNumber StartFrame = SecondsToFrame(MS, StartTimeSec);
	FFrameNumber EndFrame   = SecondsToFrame(MS, FMath::Max(EndTimeSec, StartTimeSec + 1.f));
	Section->SetRange(TRange<FFrameNumber>(StartFrame, EndFrame));

	if (CameraGuid.IsValid())
		Section->SetCameraBindingID(UE::MovieScene::FRelativeObjectBindingID(CameraGuid));

	CutTrack->AddSection(*Section);
	Seq->MarkPackageDirty();

	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"sequence\":\"%s\",\"camera\":\"%s\",\"start\":%.2f,\"end\":%.2f}"),
		*SequencePath, *CameraActorLabel, StartTimeSec, EndTimeSec);
}

void HandleAddSequenceCameraTrack(const FString& SequencePath, const FString& CameraActorLabel,
	FString& OutJsonString, FString& OutError)
{
	ULevelSequence* Seq = LoadSeq(SequencePath, OutError);
	if (!Seq) return;

	FGuid CameraGuid = FindOrAddBinding(Seq, CameraActorLabel, OutError);
	if (!CameraGuid.IsValid()) return;

	Seq->MarkPackageDirty();
	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"sequence\":\"%s\",\"camera_binding\":\"%s\",\"note\":\"Camera bound. Use add_sequence_keyframe for animation.\"}"),
		*SequencePath, *CameraGuid.ToString());
}

void HandleAddSequenceAudioTrack(const FString& SequencePath, const FString& SoundPath,
	float StartTimeSec, float EndTimeSec, FString& OutJsonString, FString& OutError)
{
	ULevelSequence* Seq = LoadSeq(SequencePath, OutError);
	if (!Seq) return;
	UMovieScene* MS = Seq->GetMovieScene();

	USoundBase* Sound = nullptr;
	if (!SoundPath.IsEmpty())
	{
		Sound = Cast<USoundBase>(UEditorAssetLibrary::LoadAsset(SoundPath));
		if (!Sound) { OutError = FString::Printf(TEXT("Could not load sound at '%s'"), *SoundPath); return; }
	}

	UMovieSceneAudioTrack* AudioTrack = MS->AddTrack<UMovieSceneAudioTrack>();
	if (!AudioTrack) { OutError = TEXT("Failed to create audio track"); return; }

	UMovieSceneAudioSection* Section = Cast<UMovieSceneAudioSection>(AudioTrack->CreateNewSection());
	if (!Section) { OutError = TEXT("Failed to create audio section"); return; }

	FFrameNumber StartFrame = SecondsToFrame(MS, StartTimeSec);
	FFrameNumber EndFrame   = SecondsToFrame(MS, FMath::Max(EndTimeSec, StartTimeSec + 1.f));
	Section->SetRange(TRange<FFrameNumber>(StartFrame, EndFrame));
	if (Sound) Section->SetSound(Sound);

	AudioTrack->AddSection(*Section);
	Seq->MarkPackageDirty();

	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"sequence\":\"%s\",\"sound\":\"%s\",\"start\":%.2f,\"end\":%.2f}"),
		*SequencePath, *SoundPath, StartTimeSec, EndTimeSec);
}

void HandleAddSequenceEventTrack(const FString& SequencePath, FString& OutJsonString, FString& OutError)
{
	ULevelSequence* Seq = LoadSeq(SequencePath, OutError);
	if (!Seq) return;
	UMovieScene* MS = Seq->GetMovieScene();

	UMovieSceneEventTrack* EventTrack = MS->AddTrack<UMovieSceneEventTrack>();
	if (!EventTrack) { OutError = TEXT("Failed to create event track"); return; }

	Seq->MarkPackageDirty();
	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"sequence\":\"%s\",\"track\":\"EventTrack\"}"), *SequencePath);
}

void HandleSetSequencePlaybackSettings(const FString& SequencePath,
	float FrameRate, float DurationSeconds, FString& OutJsonString, FString& OutError)
{
	ULevelSequence* Seq = LoadSeq(SequencePath, OutError);
	if (!Seq) return;
	UMovieScene* MS = Seq->GetMovieScene();

	if (FrameRate > 0.f)
	{
		FFrameRate Rate = FFrameRate(FMath::RoundToInt(FrameRate), 1);
		MS->SetDisplayRate(Rate);
		MS->SetTickResolutionDirectly(FFrameRate(Rate.Numerator * 1000, Rate.Denominator));
	}

	if (DurationSeconds > 0.f)
	{
		FFrameRate TickRes = MS->GetTickResolution();
		FFrameNumber DurationFrames = TickRes.AsFrameNumber(DurationSeconds);
		MS->SetPlaybackRange(TRange<FFrameNumber>(FFrameNumber(0), DurationFrames));
	}

	Seq->MarkPackageDirty();
	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"sequence\":\"%s\",\"frame_rate\":%.0f,\"duration\":%.2f}"),
		*SequencePath, FrameRate, DurationSeconds);
}

void HandleAddSequenceFloatTrack(const FString& SequencePath, const FString& ActorLabel,
	const FString& PropertyName, const FString& PropertyPath,
	FString& OutJsonString, FString& OutError)
{

	ULevelSequence* Seq = LoadSeq(SequencePath, OutError);
	if (!Seq) return;
	UMovieScene* MS = Seq->GetMovieScene();

	FGuid Guid = FindOrAddBinding(Seq, ActorLabel, OutError);
	if (!Guid.IsValid()) return;

	UMovieSceneFloatTrack* FT = MS->AddTrack<UMovieSceneFloatTrack>(Guid);
	if (!FT) { OutError = TEXT("Failed to add float track (may already exist for this property)"); return; }

	FString PropPath = PropertyPath.IsEmpty() ? PropertyName : PropertyPath;
	FT->SetPropertyNameAndPath(FName(*PropertyName), PropPath);

	UMovieSceneSection* Section = FT->CreateNewSection();
	if (Section)
	{
		Section->SetRange(MS->GetPlaybackRange());
		FT->AddSection(*Section);
	}

	Seq->MarkPackageDirty();
	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"sequence\":\"%s\",\"actor\":\"%s\",\"property\":\"%s\",\"track_type\":\"Float\",\"message\":\"Float track added. Use Sequencer to keyframe the property.\"}"),
		*SequencePath, *ActorLabel, *PropertyName);
}

void HandleAddSequenceVisibilityTrack(const FString& SequencePath, const FString& ActorLabel,
	FString& OutJsonString, FString& OutError)
{

	ULevelSequence* Seq = LoadSeq(SequencePath, OutError);
	if (!Seq) return;
	UMovieScene* MS = Seq->GetMovieScene();

	FGuid Guid = FindOrAddBinding(Seq, ActorLabel, OutError);
	if (!Guid.IsValid()) return;

	UMovieSceneVisibilityTrack* VT = MS->AddTrack<UMovieSceneVisibilityTrack>(Guid);
	if (!VT) { OutError = TEXT("Failed to add visibility track (may already exist)"); return; }

	VT->SetPropertyNameAndPath(FName("bHidden"), TEXT("bHidden"));

	UMovieSceneSection* Section = VT->CreateNewSection();
	if (Section)
	{
		Section->SetRange(MS->GetPlaybackRange());
		VT->AddSection(*Section);
	}

	Seq->MarkPackageDirty();
	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"sequence\":\"%s\",\"actor\":\"%s\",\"track_type\":\"Visibility\",\"message\":\"Visibility track added. Open Sequencer to keyframe actor visibility.\"}"),
		*SequencePath, *ActorLabel);
}

void HandleAddSequenceSkeletalAnimTrack(const FString& SequencePath, const FString& ActorLabel,
	const FString& AnimPath, float StartTimeSec,
	FString& OutJsonString, FString& OutError)
{

	ULevelSequence* Seq = LoadSeq(SequencePath, OutError);
	if (!Seq) return;
	UMovieScene* MS = Seq->GetMovieScene();

	FGuid Guid = FindOrAddBinding(Seq, ActorLabel, OutError);
	if (!Guid.IsValid()) return;

	UAnimSequenceBase* AnimSeq = nullptr;
	if (!AnimPath.IsEmpty())
	{
		AnimSeq = Cast<UAnimSequenceBase>(UEditorAssetLibrary::LoadAsset(AnimPath));
		if (!AnimSeq) { OutError = FString::Printf(TEXT("Could not load animation asset at '%s'"), *AnimPath); return; }
	}

	UMovieSceneSkeletalAnimationTrack* AT = MS->AddTrack<UMovieSceneSkeletalAnimationTrack>(Guid);
	if (!AT) { OutError = TEXT("Failed to add skeletal animation track"); return; }

	FFrameNumber StartFrame = SecondsToFrame(MS, StartTimeSec);
	if (AnimSeq)
	{
		AT->AddNewAnimation(StartFrame, AnimSeq);
	}
	else
	{
		UMovieSceneSection* Section = AT->CreateNewSection();
		if (Section)
		{
			Section->SetRange(TRange<FFrameNumber>(StartFrame, MS->GetPlaybackRange().GetUpperBoundValue()));
			AT->AddSection(*Section);
		}
	}

	Seq->MarkPackageDirty();
	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"sequence\":\"%s\",\"actor\":\"%s\",\"animation\":\"%s\",\"start_frame\":%d,\"track_type\":\"SkeletalAnimation\"}"),
		*SequencePath, *ActorLabel, *AnimPath, StartFrame.Value);
}

void HandleRemoveSequenceTrack(const FString& SequencePath, const FString& ActorLabel,
	const FString& TrackType, FString& OutJsonString, FString& OutError)
{

	ULevelSequence* Seq = LoadSeq(SequencePath, OutError);
	if (!Seq) return;
	UMovieScene* MS = Seq->GetMovieScene();

	if (ActorLabel.IsEmpty())
	{
		bool bRemoved = false;
		for (UMovieSceneTrack* Track : MS->GetTracks())
		{
			FString ClassName = Track->GetClass()->GetName();
			if (ClassName.Contains(TrackType, ESearchCase::IgnoreCase))
			{
				MS->RemoveTrack(*Track);
				bRemoved = true;
				break;
			}
		}
		if (!bRemoved) { OutError = FString::Printf(TEXT("No master track of type '%s' found"), *TrackType); return; }
	}
	else
	{
		FGuid Guid = FindOrAddBinding(Seq, ActorLabel, OutError);
		if (!Guid.IsValid()) return;

		bool bRemoved = false;
		for (UMovieSceneTrack* Track : MS->FindTracks(UMovieSceneTrack::StaticClass(), Guid, NAME_None))
		{
			FString ClassName = Track->GetClass()->GetName();
			if (TrackType.IsEmpty() || ClassName.Contains(TrackType, ESearchCase::IgnoreCase))
			{
				MS->RemoveTrack(*Track);
				bRemoved = true;
				break;
			}
		}
		if (!bRemoved) { OutError = FString::Printf(TEXT("No track of type '%s' found for actor '%s'"), *TrackType, *ActorLabel); return; }
	}

	Seq->MarkPackageDirty();
	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"sequence\":\"%s\",\"removed_track_type\":\"%s\"}"),
		*SequencePath, *TrackType);
}

void HandleSetSequenceSectionRange(const FString& SequencePath,
	float StartSeconds, float EndSeconds, FString& OutJsonString, FString& OutError)
{

	ULevelSequence* Seq = LoadSeq(SequencePath, OutError);
	if (!Seq) return;
	UMovieScene* MS = Seq->GetMovieScene();

	FFrameNumber StartFrame = SecondsToFrame(MS, StartSeconds);
	FFrameNumber EndFrame = SecondsToFrame(MS, EndSeconds);

	MS->SetPlaybackRange(TRange<FFrameNumber>(StartFrame, EndFrame));
	MS->SetWorkingRange(StartSeconds, EndSeconds);
	MS->SetViewRange(StartSeconds, EndSeconds);

	Seq->MarkPackageDirty();
	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"sequence\":\"%s\",\"start\":%.2f,\"end\":%.2f}"),
		*SequencePath, StartSeconds, EndSeconds);
}

void HandleAddSequenceSubSequence(const FString& SequencePath, const FString& SubSequencePath,
	float StartTimeSec, float EndTimeSec, FString& OutJsonString, FString& OutError)
{

	ULevelSequence* Seq = LoadSeq(SequencePath, OutError);
	if (!Seq) return;

	ULevelSequence* SubSeq = Cast<ULevelSequence>(UEditorAssetLibrary::LoadAsset(SubSequencePath));
	if (!SubSeq) { OutError = FString::Printf(TEXT("Could not load sub-sequence at '%s'"), *SubSequencePath); return; }

	UMovieScene* MS = Seq->GetMovieScene();

	UMovieSceneSubTrack* SubTrack = MS->FindTrack<UMovieSceneSubTrack>();
	if (!SubTrack) SubTrack = MS->AddTrack<UMovieSceneSubTrack>();
	if (!SubTrack) { OutError = TEXT("Failed to create/find sub track"); return; }

	FFrameNumber StartFrame = SecondsToFrame(MS, StartTimeSec);
	FFrameNumber Duration = SecondsToFrame(MS, FMath::Max(EndTimeSec - StartTimeSec, 1.f));

	UMovieSceneSubSection* Section = SubTrack->AddSequence(SubSeq, StartFrame, Duration.Value);
	if (!Section) { OutError = TEXT("Failed to add sub-sequence section"); return; }

	Seq->MarkPackageDirty();
	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"sequence\":\"%s\",\"sub_sequence\":\"%s\",\"start\":%.2f,\"end\":%.2f}"),
		*SequencePath, *SubSequencePath, StartTimeSec, EndTimeSec);
}

void HandleSetKeyframeInterpolation(const FString& SequencePath, const FString& ActorLabel,
	float TimeSeconds, const FString& InterpMode, FString& OutJsonString, FString& OutError)
{

	ULevelSequence* Seq = LoadSeq(SequencePath, OutError);
	if (!Seq) return;
	UMovieScene* MS = Seq->GetMovieScene();

	FGuid Guid = FindOrAddBinding(Seq, ActorLabel, OutError);
	if (!Guid.IsValid()) return;

	ERichCurveInterpMode TargetMode = RCIM_Cubic;
	if (InterpMode.Contains(TEXT("linear"), ESearchCase::IgnoreCase)) TargetMode = RCIM_Linear;
	else if (InterpMode.Contains(TEXT("constant"), ESearchCase::IgnoreCase) || InterpMode.Contains(TEXT("step"), ESearchCase::IgnoreCase)) TargetMode = RCIM_Constant;

	UMovieScene3DTransformTrack* TTrack = MS->FindTrack<UMovieScene3DTransformTrack>(Guid);
	if (!TTrack) { OutError = TEXT("No transform track found for this actor"); return; }

	FFrameNumber TargetFrame = SecondsToFrame(MS, TimeSeconds);
	int32 ChangedCount = 0;

	for (UMovieSceneSection* Section : TTrack->GetAllSections())
	{
		UMovieScene3DTransformSection* TS = Cast<UMovieScene3DTransformSection>(Section);
		if (!TS) continue;

		TArrayView<FMovieSceneDoubleChannel*> Channels = TS->GetChannelProxy().GetChannels<FMovieSceneDoubleChannel>();
		for (FMovieSceneDoubleChannel* Channel : Channels)
		{
			TArray<FFrameNumber> KeyTimes;
			TArray<FKeyHandle> KeyHandles;
			Channel->GetKeys(TRange<FFrameNumber>(TargetFrame - 1, TargetFrame + 1), &KeyTimes, &KeyHandles);
			for (const FKeyHandle& Handle : KeyHandles)
			{
				const int32 KeyIndex = Channel->GetData().GetIndex(Handle);
				if (KeyIndex != INDEX_NONE)
				{
					TArrayView<FMovieSceneDoubleValue> Values = Channel->GetData().GetValues();
					Values[KeyIndex].InterpMode = TargetMode;
					ChangedCount++;
				}
			}
		}
	}

	for (UMovieSceneTrack* T : MS->FindTracks(UMovieSceneFloatTrack::StaticClass(), Guid, NAME_None))
	{
		for (UMovieSceneSection* Sec : T->GetAllSections())
		{
			UMovieSceneFloatSection* FS = Cast<UMovieSceneFloatSection>(Sec);
			if (!FS) continue;
			TArray<FFrameNumber> KeyTimes; TArray<FKeyHandle> KeyHandles;
			FS->GetChannel().GetKeys(TRange<FFrameNumber>(TargetFrame - 1, TargetFrame + 1), &KeyTimes, &KeyHandles);
			for (const FKeyHandle& Handle : KeyHandles)
			{
				const int32 KeyIndex = FS->GetChannel().GetData().GetIndex(Handle);
				if (KeyIndex != INDEX_NONE)
				{
					TArrayView<FMovieSceneFloatValue> FVals = FS->GetChannel().GetData().GetValues();
					FVals[KeyIndex].InterpMode = TargetMode;
					ChangedCount++;
				}
			}
		}
	}

	if (ChangedCount == 0) { OutError = FString::Printf(TEXT("No keys found near time %.2f on actor '%s'"), TimeSeconds, *ActorLabel); return; }

	Seq->MarkPackageDirty();
	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"sequence\":\"%s\",\"actor\":\"%s\",\"time\":%.2f,\"interp_mode\":\"%s\",\"keys_changed\":%d}"),
		*SequencePath, *ActorLabel, TimeSeconds, *InterpMode, ChangedCount);
}

void HandleGetSequenceBindings(const FString& SequencePath, FString& OutJsonString, FString& OutError)
{

	ULevelSequence* Seq = LoadSeq(SequencePath, OutError);
	if (!Seq) return;
	UMovieScene* MS = Seq->GetMovieScene();

	TSharedPtr<FJsonObject> Res = MakeShareable(new FJsonObject());
	Res->SetBoolField(TEXT("success"), true);
	Res->SetStringField(TEXT("sequence_path"), SequencePath);

	TArray<TSharedPtr<FJsonValue>> BindingsArray;
	for (int32 i = 0; i < MS->GetPossessableCount(); ++i)
	{
		const FMovieScenePossessable& Poss = MS->GetPossessable(i);
		TSharedPtr<FJsonObject> BindObj = MakeShareable(new FJsonObject());
		BindObj->SetStringField(TEXT("name"), Poss.GetName());
		BindObj->SetStringField(TEXT("guid"), Poss.GetGuid().ToString());
		BindObj->SetStringField(TEXT("class"), Poss.GetPossessedObjectClass() ? Poss.GetPossessedObjectClass()->GetName() : TEXT("Unknown"));

		TArray<TSharedPtr<FJsonValue>> TracksArr;
		for (UMovieSceneTrack* Track : MS->FindTracks(UMovieSceneTrack::StaticClass(), Poss.GetGuid(), NAME_None))
		{
			TracksArr.Add(MakeShareable(new FJsonValueString(Track->GetClass()->GetName())));
		}
		BindObj->SetArrayField(TEXT("tracks"), TracksArr);

		BindingsArray.Add(MakeShareable(new FJsonValueObject(BindObj)));
	}

	TArray<TSharedPtr<FJsonValue>> MasterTracksArr;
	for (UMovieSceneTrack* Track : MS->GetTracks())
	{
		MasterTracksArr.Add(MakeShareable(new FJsonValueString(Track->GetClass()->GetName())));
	}

	Res->SetArrayField(TEXT("bindings"), BindingsArray);
	Res->SetArrayField(TEXT("master_tracks"), MasterTracksArr);
	Res->SetNumberField(TEXT("binding_count"), MS->GetPossessableCount());

	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Res.ToSharedRef(), W);
}

void HandleAddSequenceMaterialTrack(const FString& SequencePath, const FString& ActorLabel,
	int32 MaterialIndex, const FString& ParameterName,
	FString& OutJsonString, FString& OutError)
{

	ULevelSequence* Seq = LoadSeq(SequencePath, OutError);
	if (!Seq) return;
	UMovieScene* MS = Seq->GetMovieScene();

	FGuid Guid = FindOrAddBinding(Seq, ActorLabel, OutError);
	if (!Guid.IsValid()) return;

	FString PropertyPath = FString::Printf(TEXT("Materials[%d].%s"), MaterialIndex, *ParameterName);
	UMovieSceneFloatTrack* FTrack = MS->AddTrack<UMovieSceneFloatTrack>(Guid);
	if (!FTrack) { OutError = TEXT("Failed to create float track for material parameter"); return; }

	FTrack->SetPropertyNameAndPath(FName(*ParameterName), PropertyPath);

	UMovieSceneSection* Section = FTrack->CreateNewSection();
	if (Section)
	{
		Section->SetRange(MS->GetPlaybackRange());
		FTrack->AddSection(*Section);
	}

	Seq->MarkPackageDirty();
	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"sequence\":\"%s\",\"actor\":\"%s\",\"material_index\":%d,\"parameter\":\"%s\"}"),
		*SequencePath, *ActorLabel, MaterialIndex, *ParameterName);
}

void HandleRemoveSequenceKeyframe(const FString& SequencePath, const FString& ActorLabel,
	float TimeSeconds, FString& OutJsonString, FString& OutError)
{

	ULevelSequence* Seq = LoadSeq(SequencePath, OutError);
	if (!Seq) return;
	UMovieScene* MS = Seq->GetMovieScene();

	FGuid Guid = FindOrAddBinding(Seq, ActorLabel, OutError);
	if (!Guid.IsValid()) return;

	UMovieScene3DTransformTrack* TTrack = MS->FindTrack<UMovieScene3DTransformTrack>(Guid);
	if (!TTrack) { OutError = TEXT("No transform track found for this actor"); return; }

	FFrameNumber TargetFrame = SecondsToFrame(MS, TimeSeconds);
	int32 RemovedCount = 0;

	for (UMovieSceneSection* Section : TTrack->GetAllSections())
	{
		UMovieScene3DTransformSection* TS = Cast<UMovieScene3DTransformSection>(Section);
		if (!TS) continue;

		TArrayView<FMovieSceneDoubleChannel*> Channels = TS->GetChannelProxy().GetChannels<FMovieSceneDoubleChannel>();
		for (FMovieSceneDoubleChannel* Channel : Channels)
		{
			TArray<FFrameNumber> KeyTimes;
			TArray<FKeyHandle> KeyHandles;
			Channel->GetKeys(TRange<FFrameNumber>(TargetFrame - 1, TargetFrame + 1), &KeyTimes, &KeyHandles);
			for (const FKeyHandle& Handle : KeyHandles)
			{
				Channel->DeleteKeys(MakeArrayView(&Handle, 1));
				RemovedCount++;
			}
		}
	}

	if (RemovedCount == 0) { OutError = FString::Printf(TEXT("No keys found near time %.2f on actor '%s'"), TimeSeconds, *ActorLabel); return; }

	Seq->MarkPackageDirty();
	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"sequence\":\"%s\",\"actor\":\"%s\",\"time\":%.2f,\"keys_removed\":%d}"),
		*SequencePath, *ActorLabel, TimeSeconds, RemovedCount);
}

void HandleAddSequenceFadeTrack(const FString& SequencePath, float FadeInDuration, float FadeOutDuration,
	FString& OutJsonString, FString& OutError)
{

	ULevelSequence* Seq = LoadSeq(SequencePath, OutError);
	if (!Seq) return;
	UMovieScene* MS = Seq->GetMovieScene();

	UMovieSceneFloatTrack* FadeTrack = MS->FindTrack<UMovieSceneFloatTrack>();
	if (!FadeTrack)
	{
		FadeTrack = MS->AddTrack<UMovieSceneFloatTrack>();
		if (!FadeTrack) { OutError = TEXT("Failed to create fade track"); return; }
	}
	FadeTrack->SetTrackRowDisplayName(FText::FromString(TEXT("Fade")), 0);

	TRange<FFrameNumber> PlaybackRange = MS->GetPlaybackRange();
	UMovieSceneSection* Section = FadeTrack->CreateNewSection();
	if (!Section) { OutError = TEXT("Failed to create fade section"); return; }
	Section->SetRange(PlaybackRange);
	FadeTrack->AddSection(*Section);

	TArrayView<FMovieSceneFloatChannel*> Channels = Section->GetChannelProxy().GetChannels<FMovieSceneFloatChannel>();
	if (Channels.Num() > 0)
	{
		FMovieSceneFloatChannel* Ch = Channels[0];
		FFrameRate TickRes = MS->GetTickResolution();

		FFrameNumber StartFrame = PlaybackRange.GetLowerBoundValue();
		Ch->AddCubicKey(StartFrame, 1.0f);

		if (FadeInDuration > 0.f)
		{
			FFrameNumber FadeInEnd = StartFrame + TickRes.AsFrameNumber(FadeInDuration);
			Ch->AddCubicKey(FadeInEnd, 0.0f);
		}
		else
		{
			Ch->AddCubicKey(StartFrame + 1, 0.0f);
		}

		if (FadeOutDuration > 0.f && PlaybackRange.HasUpperBound())
		{
			FFrameNumber EndFrame = PlaybackRange.GetUpperBoundValue();
			FFrameNumber FadeOutStart = EndFrame - TickRes.AsFrameNumber(FadeOutDuration);
			Ch->AddCubicKey(FadeOutStart, 0.0f);
			Ch->AddCubicKey(EndFrame, 1.0f);
		}
	}

	Seq->MarkPackageDirty();
	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"sequence\":\"%s\",\"fade_in\":%.2f,\"fade_out\":%.2f}"),
		*SequencePath, FadeInDuration, FadeOutDuration);
}

void HandleSetSequenceDisplayRate(const FString& SequencePath, float DisplayFPS,
	FString& OutJsonString, FString& OutError)
{

	ULevelSequence* Seq = LoadSeq(SequencePath, OutError);
	if (!Seq) return;
	UMovieScene* MS = Seq->GetMovieScene();

	if (DisplayFPS <= 0.f) { OutError = TEXT("display_fps must be > 0"); return; }

	FFrameRate NewRate((int32)DisplayFPS, 1);
	MS->SetDisplayRate(NewRate);

	Seq->MarkPackageDirty();
	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"sequence\":\"%s\",\"display_rate\":%d}"),
		*SequencePath, (int32)DisplayFPS);
}

void HandleAddSequenceColorTrack(const FString& SequencePath, const FString& ActorLabel,
	const FString& PropertyName, FString& OutJsonString, FString& OutError)
{
	ULevelSequence* Seq = LoadSeq(SequencePath, OutError);
	if (!Seq) return;
	UMovieScene* MS = Seq->GetMovieScene();

	FGuid Guid = FindOrAddBinding(Seq, ActorLabel, OutError);
	if (!Guid.IsValid()) return;

	FString PropPath = PropertyName.IsEmpty() ? TEXT("LightColor") : PropertyName;

	UMovieSceneColorTrack* Track = MS->AddTrack<UMovieSceneColorTrack>(Guid);
	if (!Track) { OutError = TEXT("Failed to create color track (may already exist for this property)"); return; }

	Track->SetPropertyNameAndPath(FName(*PropPath), PropPath);

	UMovieSceneSection* Section = Track->CreateNewSection();
	if (Section)
	{
		Section->SetRange(MS->GetPlaybackRange());
		Track->AddSection(*Section);
	}

	Seq->MarkPackageDirty();
	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"sequence\":\"%s\",\"actor\":\"%s\",\"property\":\"%s\",\"message\":\"Color track added (RGBA). Use add_color_keyframe to set keyframes.\"}"),
		*SequencePath, *ActorLabel, *PropPath);
}

static UMoviePipelineQueueEngineSubsystem* GetMRQSubsystem(FString& OutError)
{
	if (!GEngine) { OutError = TEXT("GEngine is null"); return nullptr; }
	UMoviePipelineQueueEngineSubsystem* Sub = GEngine->GetEngineSubsystem<UMoviePipelineQueueEngineSubsystem>();
	if (!Sub) { OutError = TEXT("UMoviePipelineQueueEngineSubsystem not found — ensure MovieRenderPipeline plugin is enabled"); return nullptr; }
	return Sub;
}

static UMoviePipelineExecutorJob* FindJobByName(UMoviePipelineQueue* Queue, const FString& JobName)
{
	for (UMoviePipelineExecutorJob* Job : Queue->GetJobs())
		if (Job && (Job->JobName == JobName || JobName.IsEmpty()))
			return Job;
	return nullptr;
}

void HandleCreateMRQJob(const FString& SequencePath, const FString& JobName,
	const FString& MapPath, FString& OutJsonString, FString& OutError)
{

	UMoviePipelineQueueEngineSubsystem* Sub = GetMRQSubsystem(OutError);
	if (!Sub) return;

	UMoviePipelineQueue* Queue = Sub->GetQueue();
	if (!Queue) { OutError = TEXT("MRQ Queue is null"); return; }

	UMoviePipelineExecutorJob* Job = Queue->AllocateNewJob(UMoviePipelineExecutorJob::StaticClass());
	if (!Job) { OutError = TEXT("AllocateNewJob returned null"); return; }

	Job->JobName = JobName.IsEmpty() ? FPaths::GetBaseFilename(SequencePath) : JobName;
	Job->Sequence = FSoftObjectPath(SequencePath);
	if (!MapPath.IsEmpty())
		Job->Map = FSoftObjectPath(MapPath);

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("status"), TEXT("job_created"));
	Root->SetStringField(TEXT("job_name"), Job->JobName);
	Root->SetStringField(TEXT("sequence"), SequencePath);
	FJsonSerializer::Serialize(Root.ToSharedRef(), TJsonWriterFactory<>::Create(&OutJsonString));
}

void HandleSetMRQOutputSettings(const FString& JobName, const FString& OutputDirectory,
	const FString& FileNameFormat, const FString& FileFormat, float FrameRate,
	FString& OutJsonString, FString& OutError)
{

	UMoviePipelineQueueEngineSubsystem* Sub = GetMRQSubsystem(OutError);
	if (!Sub) return;
	UMoviePipelineQueue* Queue = Sub->GetQueue();
	if (!Queue) { OutError = TEXT("MRQ Queue is null"); return; }

	UMoviePipelineExecutorJob* Job = FindJobByName(Queue, JobName);
	if (!Job) { OutError = FString::Printf(TEXT("Job '%s' not found in MRQ queue"), *JobName); return; }

	UMoviePipelinePrimaryConfig* Config = Job->GetConfiguration();
	if (!Config) { OutError = TEXT("Job has no configuration"); return; }

	UMoviePipelineOutputSetting* OutSetting = Cast<UMoviePipelineOutputSetting>(
		Config->FindOrAddSettingByClass(UMoviePipelineOutputSetting::StaticClass()));
	if (!OutSetting) { OutError = TEXT("Failed to find/create UMoviePipelineOutputSetting"); return; }

	if (!OutputDirectory.IsEmpty())
		OutSetting->OutputDirectory.Path = OutputDirectory;
	if (!FileNameFormat.IsEmpty())
		OutSetting->FileNameFormat = FileNameFormat;
	if (FrameRate > 0.f)
	{
		OutSetting->bUseCustomFrameRate = true;
		OutSetting->OutputFrameRate = FFrameRate(FMath::RoundToInt(FrameRate), 1);
	}

	if (!FileFormat.IsEmpty())
	{
		UClass* FormatClass = nullptr;
		FString FmtLower = FileFormat.ToLower();
		if (FmtLower == TEXT("png"))
			FormatClass = UMoviePipelineImageSequenceOutput_PNG::StaticClass();
		else if (FmtLower == TEXT("exr"))
			FormatClass = FindObject<UClass>(nullptr, TEXT("/Script/MovieRenderPipelineRenderPasses.MoviePipelineImageSequenceOutput_EXR"));
		else if (FmtLower == TEXT("jpg") || FmtLower == TEXT("jpeg"))
			FormatClass = UMoviePipelineImageSequenceOutput_JPG::StaticClass();
		if (FormatClass)
			Config->FindOrAddSettingByClass(FormatClass);
	}

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("status"), TEXT("output_settings_set"));
	Root->SetStringField(TEXT("job_name"), Job->JobName);
	Root->SetStringField(TEXT("output_directory"), OutSetting->OutputDirectory.Path);
	Root->SetStringField(TEXT("file_name_format"), OutSetting->FileNameFormat);
	FJsonSerializer::Serialize(Root.ToSharedRef(), TJsonWriterFactory<>::Create(&OutJsonString));
}

void HandleAddMRQRenderPass(const FString& JobName, const FString& PassType,
	FString& OutJsonString, FString& OutError)
{

	UMoviePipelineQueueEngineSubsystem* Sub = GetMRQSubsystem(OutError);
	if (!Sub) return;
	UMoviePipelineQueue* Queue = Sub->GetQueue();
	if (!Queue) { OutError = TEXT("MRQ Queue is null"); return; }

	UMoviePipelineExecutorJob* Job = FindJobByName(Queue, JobName);
	if (!Job) { OutError = FString::Printf(TEXT("Job '%s' not found"), *JobName); return; }

	UMoviePipelinePrimaryConfig* Config = Job->GetConfiguration();
	if (!Config) { OutError = TEXT("Job has no configuration"); return; }

	FString PassLower = PassType.ToLower();
	UClass* SettingClass = nullptr;
	if (PassLower == TEXT("deferred"))
		SettingClass = UMoviePipelineDeferredPassBase::StaticClass();
	else if (PassLower == TEXT("png"))
		SettingClass = UMoviePipelineImageSequenceOutput_PNG::StaticClass();
	else if (PassLower == TEXT("exr"))
		SettingClass = FindObject<UClass>(nullptr, TEXT("/Script/MovieRenderPipelineRenderPasses.MoviePipelineImageSequenceOutput_EXR"));
	else if (PassLower == TEXT("jpg") || PassLower == TEXT("jpeg"))
		SettingClass = UMoviePipelineImageSequenceOutput_JPG::StaticClass();
	else if (PassLower == TEXT("anti_alias") || PassLower == TEXT("aa"))
		SettingClass = UMoviePipelineAntiAliasingSetting::StaticClass();

	if (!SettingClass)
	{
		OutError = FString::Printf(TEXT("Unknown pass_type '%s'. Valid: deferred, png, exr, jpg, anti_alias"), *PassType);
		return;
	}

	UMoviePipelineSetting* Setting = Config->FindOrAddSettingByClass(SettingClass);
	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("status"), TEXT("render_pass_added"));
	Root->SetStringField(TEXT("job_name"), Job->JobName);
	Root->SetStringField(TEXT("pass_type"), PassType);
	Root->SetStringField(TEXT("setting_class"), Setting ? Setting->GetClass()->GetName() : TEXT("None"));
	FJsonSerializer::Serialize(Root.ToSharedRef(), TJsonWriterFactory<>::Create(&OutJsonString));
}

void HandleExecuteMRQRender(FString& OutJsonString, FString& OutError)
{

	UMoviePipelineQueueEngineSubsystem* Sub = GetMRQSubsystem(OutError);
	if (!Sub) return;

	UMoviePipelineQueue* Queue = Sub->GetQueue();
	if (!Queue || Queue->GetJobs().Num() == 0)
	{
		OutError = TEXT("MRQ queue is empty — add jobs first");
		return;
	}

	Sub->RenderQueueWithExecutor(UMoviePipelinePIEExecutor::StaticClass());

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("status"), TEXT("render_started"));
	Root->SetNumberField(TEXT("job_count"), Queue->GetJobs().Num());
	Root->SetStringField(TEXT("executor"), TEXT("PIE"));
	FJsonSerializer::Serialize(Root.ToSharedRef(), TJsonWriterFactory<>::Create(&OutJsonString));
}

void HandleGetMRQQueueSummary(FString& OutJsonString, FString& OutError)
{

	UMoviePipelineQueueEngineSubsystem* Sub = GetMRQSubsystem(OutError);
	if (!Sub) return;
	UMoviePipelineQueue* Queue = Sub->GetQueue();

	TArray<TSharedPtr<FJsonValue>> JobsArr;
	if (Queue)
	{
		for (UMoviePipelineExecutorJob* Job : Queue->GetJobs())
		{
			if (!Job) continue;
			TSharedPtr<FJsonObject> JObj = MakeShared<FJsonObject>();
			JObj->SetStringField(TEXT("job_name"), Job->JobName);
			JObj->SetStringField(TEXT("sequence"), Job->Sequence.ToString());
			JObj->SetStringField(TEXT("map"), Job->Map.ToString());
			JObj->SetStringField(TEXT("status"), Job->GetStatusMessage());

			if (UMoviePipelinePrimaryConfig* Cfg = Job->GetConfiguration())
			{
				UMoviePipelineOutputSetting* OS = Cast<UMoviePipelineOutputSetting>(
					Cfg->FindSettingByClass(UMoviePipelineOutputSetting::StaticClass()));
				JObj->SetStringField(TEXT("output_directory"), OS ? OS->OutputDirectory.Path : TEXT(""));
			}
			JobsArr.Add(MakeShared<FJsonValueObject>(JObj));
		}
	}

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("job_count"), JobsArr.Num());
	Root->SetArrayField(TEXT("jobs"), JobsArr);
	FJsonSerializer::Serialize(Root.ToSharedRef(), TJsonWriterFactory<>::Create(&OutJsonString));
}

void HandleClearMRQQueue(FString& OutJsonString, FString& OutError)
{

	UMoviePipelineQueueEngineSubsystem* Sub = GetMRQSubsystem(OutError);
	if (!Sub) return;
	UMoviePipelineQueue* Queue = Sub->GetQueue();
	if (!Queue) { OutError = TEXT("MRQ Queue is null"); return; }

	int32 Count = Queue->GetJobs().Num();
	Queue->DeleteAllJobs();

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("status"), TEXT("queue_cleared"));
	Root->SetNumberField(TEXT("jobs_deleted"), Count);
	FJsonSerializer::Serialize(Root.ToSharedRef(), TJsonWriterFactory<>::Create(&OutJsonString));
}

static UClass* ResolveControlRigClass(const FString& ControlRigPath, FString& OutError)
{
	UObject* Asset = UEditorAssetLibrary::LoadAsset(ControlRigPath);
	if (!Asset) { OutError = FString::Printf(TEXT("Could not load asset at '%s'"), *ControlRigPath); return nullptr; }

	if (UControlRigBlueprint* CRBP = Cast<UControlRigBlueprint>(Asset))
	{
		UClass* C = CRBP->GetControlRigClass();
		if (!C) OutError = FString::Printf(TEXT("ControlRigBlueprint '%s' has no generated class — compile it first"), *ControlRigPath);
		return C;
	}
	if (UClass* AsClass = Cast<UClass>(Asset))
	{
		if (AsClass->IsChildOf(UControlRig::StaticClass())) return AsClass;
	}
	OutError = FString::Printf(TEXT("Asset at '%s' is not a Control Rig blueprint or class"), *ControlRigPath);
	return nullptr;
}

static UMovieSceneControlRigParameterTrack* FindCRTrackForBinding(UMovieScene* MS, const FGuid& Binding)
{
	if (!MS) return nullptr;
	const UMovieScene* CMS = MS;
	for (const FMovieSceneBinding& B : CMS->GetBindings())
	{
		if (B.GetObjectGuid() != Binding) continue;
		for (UMovieSceneTrack* T : B.GetTracks())
			if (UMovieSceneControlRigParameterTrack* CR = Cast<UMovieSceneControlRigParameterTrack>(T))
				return CR;
	}
	return nullptr;
}

static const TCHAR* ControlTypeToString(ERigControlType T)
{
	switch (T)
	{
		case ERigControlType::Bool:             return TEXT("Bool");
		case ERigControlType::Float:            return TEXT("Float");
		case ERigControlType::Integer:          return TEXT("Integer");
		case ERigControlType::Vector2D:         return TEXT("Vector2D");
		case ERigControlType::Position:         return TEXT("Position");
		case ERigControlType::Scale:            return TEXT("Scale");
		case ERigControlType::Rotator:          return TEXT("Rotator");
		case ERigControlType::Transform:        return TEXT("Transform");
		case ERigControlType::TransformNoScale: return TEXT("TransformNoScale");
		case ERigControlType::EulerTransform:   return TEXT("EulerTransform");
		case ERigControlType::ScaleFloat:       return TEXT("ScaleFloat");
		default:                                return TEXT("Unknown");
	}
}

void HandleAddControlRigSequencerTrack(const FString& SequencePath, const FString& BindingLabel,
	const FString& ControlRigPath, FString& OutJsonString, FString& OutError)
{
	ULevelSequence* Seq = LoadSeq(SequencePath, OutError);
	if (!Seq) return;

	FGuid Binding = FindOrAddBinding(Seq, BindingLabel, OutError);
	if (!Binding.IsValid()) return;

	UClass* CRClass = ResolveControlRigClass(ControlRigPath, OutError);
	if (!CRClass) return;

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World) { OutError = TEXT("No editor world"); return; }

	FMovieSceneBindingProxy Proxy(Binding, Seq);
	UMovieSceneTrack* Track = UControlRigSequencerEditorLibrary::FindOrCreateControlRigTrack(World, Seq, CRClass, Proxy, false);
	if (!Track) { OutError = FString::Printf(TEXT("Failed to create Control Rig track for binding '%s'"), *BindingLabel); return; }

	UMovieSceneControlRigParameterTrack* CRTrack = Cast<UMovieSceneControlRigParameterTrack>(Track);
	UControlRig* CR = CRTrack ? CRTrack->GetControlRig() : nullptr;

	Seq->MarkPackageDirty();

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"sequence_path\":\"%s\",\"binding\":\"%s\",\"control_rig_class\":\"%s\",\"control_rig_instance\":\"%s\"}"),
		*SequencePath, *BindingLabel, *CRClass->GetName(),
		CR ? *CR->GetName() : TEXT(""));
}

void HandleListControlRigControls(const FString& SequencePath, const FString& BindingLabel,
	FString& OutJsonString, FString& OutError)
{
	ULevelSequence* Seq = LoadSeq(SequencePath, OutError);
	if (!Seq) return;
	UMovieScene* MS = Seq->GetMovieScene();

	FGuid Binding = FindOrAddBinding(Seq, BindingLabel, OutError);
	if (!Binding.IsValid()) return;

	UMovieSceneControlRigParameterTrack* CRTrack = FindCRTrackForBinding(MS, Binding);
	if (!CRTrack) { OutError = FString::Printf(TEXT("No Control Rig track on binding '%s' — call add_control_rig_track first"), *BindingLabel); return; }

	UControlRig* CR = CRTrack->GetControlRig();
	if (!CR) { OutError = TEXT("Control Rig instance not yet initialized — open the sequence in editor once or recompile the rig"); return; }

	URigHierarchy* Hier = CR->GetHierarchy();
	if (!Hier) { OutError = TEXT("Control Rig has no hierarchy"); return; }

	TArray<TSharedPtr<FJsonValue>> Controls;
	TArray<FRigControlElement*> Elements = Hier->GetElementsOfType<FRigControlElement>(true);
	for (FRigControlElement* E : Elements)
	{
		if (!E) continue;
		TSharedPtr<FJsonObject> Obj = MakeShareable(new FJsonObject);
		Obj->SetStringField(TEXT("name"), E->GetKey().Name.ToString());
		Obj->SetStringField(TEXT("control_type"), ControlTypeToString(E->Settings.ControlType));
		Obj->SetBoolField(TEXT("animatable"), E->Settings.AnimationType == ERigControlAnimationType::AnimationControl);
		Controls.Add(MakeShareable(new FJsonValueObject(Obj)));
	}

	TSharedPtr<FJsonObject> Root = MakeShareable(new FJsonObject);
	Root->SetBoolField(TEXT("success"), true);
	Root->SetStringField(TEXT("control_rig_class"), CR->GetClass()->GetName());
	Root->SetNumberField(TEXT("count"), Controls.Num());
	Root->SetArrayField(TEXT("controls"), Controls);
	FJsonSerializer::Serialize(Root.ToSharedRef(), TJsonWriterFactory<>::Create(&OutJsonString));
}

void HandleAddControlRigSection(const FString& SequencePath, const FString& BindingLabel,
	int32 StartFrame, int32 EndFrame, FString& OutJsonString, FString& OutError)
{
	if (EndFrame <= StartFrame) { OutError = TEXT("end_frame must be greater than start_frame"); return; }

	ULevelSequence* Seq = LoadSeq(SequencePath, OutError);
	if (!Seq) return;
	UMovieScene* MS = Seq->GetMovieScene();

	FGuid Binding = FindOrAddBinding(Seq, BindingLabel, OutError);
	if (!Binding.IsValid()) return;

	UMovieSceneControlRigParameterTrack* CRTrack = FindCRTrackForBinding(MS, Binding);
	if (!CRTrack) { OutError = FString::Printf(TEXT("No Control Rig track on binding '%s'"), *BindingLabel); return; }

	UControlRig* CR = CRTrack->GetControlRig();
	if (!CR) { OutError = TEXT("Control Rig instance not initialized"); return; }

	UMovieSceneSection* NewSec = CRTrack->CreateControlRigSection(FFrameNumber(StartFrame), CR, false);
	if (!NewSec) { OutError = TEXT("CreateControlRigSection returned null"); return; }
	NewSec->SetRange(TRange<FFrameNumber>(FFrameNumber(StartFrame), FFrameNumber(EndFrame)));
	Seq->MarkPackageDirty();

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"sequence_path\":\"%s\",\"binding\":\"%s\",\"start_frame\":%d,\"end_frame\":%d,\"total_sections\":%d}"),
		*SequencePath, *BindingLabel, StartFrame, EndFrame, CRTrack->GetAllSections().Num());
}

void HandleSetControlRigKeyframe(const FString& SequencePath, const FString& BindingLabel,
	const FString& ControlName, int32 Frame, const FString& ValueType,
	const TSharedPtr<FJsonValue>& Value, FString& OutJsonString, FString& OutError)
{
	if (ControlName.IsEmpty()) { OutError = TEXT("control_name is required"); return; }
	if (!Value.IsValid())      { OutError = TEXT("value is required"); return; }

	ULevelSequence* Seq = LoadSeq(SequencePath, OutError);
	if (!Seq) return;
	UMovieScene* MS = Seq->GetMovieScene();

	FGuid Binding = FindOrAddBinding(Seq, BindingLabel, OutError);
	if (!Binding.IsValid()) return;

	UMovieSceneControlRigParameterTrack* CRTrack = FindCRTrackForBinding(MS, Binding);
	if (!CRTrack) { OutError = FString::Printf(TEXT("No Control Rig track on binding '%s'"), *BindingLabel); return; }

	UControlRig* CR = CRTrack->GetControlRig();
	if (!CR) { OutError = TEXT("Control Rig instance not initialized"); return; }

	const FName CtrlName(*ControlName);
	const FFrameNumber FN(Frame);
	const FString TypeLower = ValueType.ToLower();

	auto AsArr = [&](int32 N) -> const TArray<TSharedPtr<FJsonValue>>* {
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (Value->TryGetArray(Arr) && Arr && Arr->Num() >= N) return Arr;
		return nullptr;
	};

	if (TypeLower == TEXT("float"))
	{
		double V = 0; Value->TryGetNumber(V);
		UControlRigSequencerEditorLibrary::SetLocalControlRigFloat(Seq, CR, CtrlName, FN, (float)V);
	}
	else if (TypeLower == TEXT("bool"))
	{
		bool B = false; Value->TryGetBool(B);
		UControlRigSequencerEditorLibrary::SetLocalControlRigBool(Seq, CR, CtrlName, FN, B);
	}
	else if (TypeLower == TEXT("int") || TypeLower == TEXT("integer"))
	{
		double V = 0; Value->TryGetNumber(V);
		UControlRigSequencerEditorLibrary::SetLocalControlRigInt(Seq, CR, CtrlName, FN, (int32)V);
	}
	else if (TypeLower == TEXT("vector2d"))
	{
		const auto* Arr = AsArr(2);
		if (!Arr) { OutError = TEXT("vector2d requires value=[X,Y]"); return; }
		FVector2D V((*Arr)[0]->AsNumber(), (*Arr)[1]->AsNumber());
		UControlRigSequencerEditorLibrary::SetLocalControlRigVector2D(Seq, CR, CtrlName, FN, V);
	}
	else if (TypeLower == TEXT("rotator"))
	{
		const auto* Arr = AsArr(3);
		if (!Arr) { OutError = TEXT("rotator requires value=[Pitch,Yaw,Roll]"); return; }
		FRotator R((*Arr)[0]->AsNumber(), (*Arr)[1]->AsNumber(), (*Arr)[2]->AsNumber());
		UControlRigSequencerEditorLibrary::SetLocalControlRigRotator(Seq, CR, CtrlName, FN, R);
	}
	else if (TypeLower == TEXT("transform"))
	{
		const auto* Arr = AsArr(6);
		if (!Arr) { OutError = TEXT("transform requires value=[LX,LY,LZ,Pitch,Yaw,Roll] or [...,SX,SY,SZ]"); return; }
		FTransform T;
		T.SetLocation(FVector((*Arr)[0]->AsNumber(), (*Arr)[1]->AsNumber(), (*Arr)[2]->AsNumber()));
		T.SetRotation(FRotator((*Arr)[3]->AsNumber(), (*Arr)[4]->AsNumber(), (*Arr)[5]->AsNumber()).Quaternion());
		if (Arr->Num() >= 9)
			T.SetScale3D(FVector((*Arr)[6]->AsNumber(), (*Arr)[7]->AsNumber(), (*Arr)[8]->AsNumber()));
		UControlRigSequencerEditorLibrary::SetLocalControlRigTransform(Seq, CR, CtrlName, FN, T);
	}
	else
	{
		OutError = FString::Printf(TEXT("Unknown value_type '%s'. Use: float | bool | int | vector2d | rotator | transform"), *ValueType);
		return;
	}

	Seq->MarkPackageDirty();
	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"control_name\":\"%s\",\"frame\":%d,\"value_type\":\"%s\"}"),
		*ControlName, Frame, *ValueType);
}

void HandleAddControlRigSequencerTrackFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SP, BL, CRP;
	Args->TryGetStringField(TEXT("sequence_path"), SP);
	if (!Args->TryGetStringField(TEXT("binding_label"), BL)) Args->TryGetStringField(TEXT("actor_label"), BL);
	if (!Args->TryGetStringField(TEXT("control_rig_path"), CRP)) Args->TryGetStringField(TEXT("rig_path"), CRP);
	HandleAddControlRigSequencerTrack(SP, BL, CRP, OutJsonString, OutError);
}

void HandleListControlRigControlsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SP, BL;
	Args->TryGetStringField(TEXT("sequence_path"), SP);
	if (!Args->TryGetStringField(TEXT("binding_label"), BL)) Args->TryGetStringField(TEXT("actor_label"), BL);
	HandleListControlRigControls(SP, BL, OutJsonString, OutError);
}

void HandleAddControlRigSectionFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SP, BL;
	Args->TryGetStringField(TEXT("sequence_path"), SP);
	if (!Args->TryGetStringField(TEXT("binding_label"), BL)) Args->TryGetStringField(TEXT("actor_label"), BL);
	int32 SF = 0, EF = 0;
	double DSF = 0, DEF = 0;
	Args->TryGetNumberField(TEXT("start_frame"), DSF);
	Args->TryGetNumberField(TEXT("end_frame"), DEF);
	SF = (int32)DSF; EF = (int32)DEF;
	HandleAddControlRigSection(SP, BL, SF, EF, OutJsonString, OutError);
}

void HandleSetControlRigKeyframeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SP, BL, CN, VT;
	Args->TryGetStringField(TEXT("sequence_path"), SP);
	if (!Args->TryGetStringField(TEXT("binding_label"), BL)) Args->TryGetStringField(TEXT("actor_label"), BL);
	Args->TryGetStringField(TEXT("control_name"), CN);
	if (!Args->TryGetStringField(TEXT("value_type"), VT)) VT = TEXT("float");
	double DF = 0; Args->TryGetNumberField(TEXT("frame"), DF);

	TSharedPtr<FJsonValue> ValueField;
	const TSharedPtr<FJsonValue>* Found = Args->Values.Find(TEXT("value"));
	if (Found) ValueField = *Found;

	HandleSetControlRigKeyframe(SP, BL, CN, (int32)DF, VT, ValueField, OutJsonString, OutError);
}

void HandleAddSequenceActorBindingFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString SequencePath;
	Args->TryGetStringField(TEXT("sequence_path"), SequencePath);

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("bindings"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			FString ActorLabel;
			if ((*ItemsArray)[i]->Type == EJson::String)
				ActorLabel = (*ItemsArray)[i]->AsString();
			else if (auto Item = (*ItemsArray)[i]->AsObject())
			{
				ActorLabel = BatchToolHelper::GetItemString(Item, TEXT("actor_label"), TEXT("actor_name"));
				if (ActorLabel.IsEmpty()) ActorLabel = BatchToolHelper::GetItemString(Item, TEXT("label"), TEXT("name"));
			}
			if (ActorLabel.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing actor_label")); continue; }
			FString ItemOut, ItemErr;
			HandleAddSequenceActorBinding(SequencePath, ActorLabel, ItemOut, ItemErr);
			if (ItemErr.IsEmpty()) { auto E = MakeShared<FJsonObject>(); E->SetStringField(TEXT("actor_label"), ActorLabel); Batch.AddSuccess(i, E); }
			else Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJsonString);
		return;
	}
	FString ActorLabel;
	Args->TryGetStringField(TEXT("actor_label"), ActorLabel);
	if (ActorLabel.IsEmpty()) Args->TryGetStringField(TEXT("actor_name"), ActorLabel);
	HandleAddSequenceActorBinding(SequencePath, ActorLabel, OutJsonString, OutError);
}

void HandleAddSequenceKeyframeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString SequencePath;
	Args->TryGetStringField(TEXT("sequence_path"), SequencePath);

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("keyframes"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString AL = BatchToolHelper::GetItemString(Item, TEXT("actor_label"), TEXT("label"));
			double TS=0, LX=0, LY=0, LZ=0, RP=0, RY=0, RR=0, SX=1, SY=1, SZ=1;
			Item->TryGetNumberField(TEXT("time_seconds"), TS);
			Item->TryGetNumberField(TEXT("location_x"), LX); Item->TryGetNumberField(TEXT("location_y"), LY); Item->TryGetNumberField(TEXT("location_z"), LZ);
			Item->TryGetNumberField(TEXT("rotation_pitch"), RP); Item->TryGetNumberField(TEXT("rotation_yaw"), RY); Item->TryGetNumberField(TEXT("rotation_roll"), RR);
			Item->TryGetNumberField(TEXT("scale_x"), SX); Item->TryGetNumberField(TEXT("scale_y"), SY); Item->TryGetNumberField(TEXT("scale_z"), SZ);
			bool bSetScale = Item->HasField(TEXT("scale_x")) || Item->HasField(TEXT("scale_y")) || Item->HasField(TEXT("scale_z"));
			if (AL.IsEmpty()) Args->TryGetStringField(TEXT("actor_label"), AL);
			if (AL.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing actor_label")); continue; }
			FString ItemOut, ItemErr;
			HandleAddSequenceKeyframe(SequencePath, AL, (float)TS, (float)LX,(float)LY,(float)LZ, (float)RP,(float)RY,(float)RR, (float)SX,(float)SY,(float)SZ, bSetScale, ItemOut, ItemErr);
			if (ItemErr.IsEmpty()) { auto E = MakeShared<FJsonObject>(); E->SetNumberField(TEXT("time"), TS); Batch.AddSuccess(i, E); }
			else Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJsonString);
		return;
	}
	FString ActorLabel;
	Args->TryGetStringField(TEXT("actor_label"), ActorLabel);
	double TS=0, LX=0, LY=0, LZ=0, RP=0, RY=0, RR=0, SX=1, SY=1, SZ=1;
	Args->TryGetNumberField(TEXT("time_seconds"), TS);
	Args->TryGetNumberField(TEXT("location_x"), LX); Args->TryGetNumberField(TEXT("location_y"), LY); Args->TryGetNumberField(TEXT("location_z"), LZ);
	Args->TryGetNumberField(TEXT("rotation_pitch"), RP); Args->TryGetNumberField(TEXT("rotation_yaw"), RY); Args->TryGetNumberField(TEXT("rotation_roll"), RR);
	Args->TryGetNumberField(TEXT("scale_x"), SX); Args->TryGetNumberField(TEXT("scale_y"), SY); Args->TryGetNumberField(TEXT("scale_z"), SZ);
	bool bSetScale = Args->HasField(TEXT("scale_x")) || Args->HasField(TEXT("scale_y")) || Args->HasField(TEXT("scale_z"));
	HandleAddSequenceKeyframe(SequencePath, ActorLabel, (float)TS, (float)LX,(float)LY,(float)LZ, (float)RP,(float)RY,(float)RR, (float)SX,(float)SY,(float)SZ, bSetScale, OutJsonString, OutError);
}

void HandleAddSequenceTransformTrackFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString SequencePath;
	Args->TryGetStringField(TEXT("sequence_path"), SequencePath);

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("tracks"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			FString ActorLabel;
			if ((*ItemsArray)[i]->Type == EJson::String)
				ActorLabel = (*ItemsArray)[i]->AsString();
			else if (auto Item = (*ItemsArray)[i]->AsObject())
				ActorLabel = BatchToolHelper::GetItemString(Item, TEXT("actor_label"), TEXT("label"));
			if (ActorLabel.IsEmpty()) { Batch.AddFailure(i, TEXT("Missing actor_label")); continue; }
			FString ItemOut, ItemErr;
			HandleAddSequenceTransformTrack(SequencePath, ActorLabel, ItemOut, ItemErr);
			if (ItemErr.IsEmpty()) { auto E = MakeShared<FJsonObject>(); E->SetStringField(TEXT("actor_label"), ActorLabel); Batch.AddSuccess(i, E); }
			else Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJsonString);
		return;
	}
	FString ActorLabel;
	Args->TryGetStringField(TEXT("actor_label"), ActorLabel);
	HandleAddSequenceTransformTrack(SequencePath, ActorLabel, OutJsonString, OutError);
}

void HandleAddSequenceFloatTrackFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString SequencePath;
	Args->TryGetStringField(TEXT("sequence_path"), SequencePath);

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("tracks"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString AL = BatchToolHelper::GetItemString(Item, TEXT("actor_label"), TEXT("label"));
			FString PN = BatchToolHelper::GetItemString(Item, TEXT("property_name"));
			FString PP = BatchToolHelper::GetItemString(Item, TEXT("property_path"));
			if (AL.IsEmpty()) Args->TryGetStringField(TEXT("actor_label"), AL);
			FString ItemOut, ItemErr;
			HandleAddSequenceFloatTrack(SequencePath, AL, PN, PP, ItemOut, ItemErr);
			if (ItemErr.IsEmpty()) { auto E = MakeShared<FJsonObject>(); E->SetStringField(TEXT("property_name"), PN); Batch.AddSuccess(i, E); }
			else Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJsonString);
		return;
	}
	FString ActorLabel, PropertyName, PropertyPath;
	Args->TryGetStringField(TEXT("actor_label"), ActorLabel);
	Args->TryGetStringField(TEXT("property_name"), PropertyName);
	Args->TryGetStringField(TEXT("property_path"), PropertyPath);
	HandleAddSequenceFloatTrack(SequencePath, ActorLabel, PropertyName, PropertyPath, OutJsonString, OutError);
}

void HandleRemoveSequenceTrackFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString SequencePath;
	Args->TryGetStringField(TEXT("sequence_path"), SequencePath);

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("tracks"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString AL = BatchToolHelper::GetItemString(Item, TEXT("actor_label"));
			FString TT = BatchToolHelper::GetItemString(Item, TEXT("track_type"));
			FString ItemOut, ItemErr;
			HandleRemoveSequenceTrack(SequencePath, AL, TT, ItemOut, ItemErr);
			if (ItemErr.IsEmpty()) { auto E = MakeShared<FJsonObject>(); E->SetStringField(TEXT("track_type"), TT); Batch.AddSuccess(i, E); }
			else Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJsonString);
		return;
	}
	FString ActorLabel, TrackType;
	Args->TryGetStringField(TEXT("actor_label"), ActorLabel);
	Args->TryGetStringField(TEXT("track_type"), TrackType);
	HandleRemoveSequenceTrack(SequencePath, ActorLabel, TrackType, OutJsonString, OutError);
}

static FGuid FindBindingGuid(UMovieScene* MS, const FString& ActorLabel)
{
	for (int32 i = 0; i < MS->GetPossessableCount(); ++i)
		if (MS->GetPossessable(i).GetName().Equals(ActorLabel, ESearchCase::IgnoreCase))
			return MS->GetPossessable(i).GetGuid();
	for (int32 i = 0; i < MS->GetSpawnableCount(); ++i)
		if (MS->GetSpawnable(i).GetName().Equals(ActorLabel, ESearchCase::IgnoreCase))
			return MS->GetSpawnable(i).GetGuid();
	return FGuid();
}

void HandleAddFloatKeyframe(const FString& SequencePath, const FString& ActorLabel,
	const FString& PropertyName, float TimeSeconds, float Value,
	FString& OutJsonString, FString& OutError)
{
	ULevelSequence* Seq = LoadSeq(SequencePath, OutError); if (!Seq) return;
	UMovieScene* MS = Seq->GetMovieScene();

	FGuid Guid = FindBindingGuid(MS, ActorLabel);
	if (!Guid.IsValid()) { OutError = FString::Printf(TEXT("No binding for actor '%s'. Call add_sequence_actor_binding first."), *ActorLabel); return; }

	UMovieSceneFloatTrack* Track = nullptr;
	for (UMovieSceneTrack* T : MS->FindTracks(UMovieSceneFloatTrack::StaticClass(), Guid, NAME_None))
	{
		UMovieSceneFloatTrack* FT = Cast<UMovieSceneFloatTrack>(T);
		if (!FT) continue;
		if (PropertyName.IsEmpty() ||
			FT->GetPropertyName().ToString().Equals(PropertyName, ESearchCase::IgnoreCase) ||
			FT->GetPropertyPath().ToString().Equals(PropertyName, ESearchCase::IgnoreCase))
		{ Track = FT; break; }
	}
	if (!Track) { OutError = FString::Printf(TEXT("No float track for property '%s'. Call add_sequence_float_track first."), *PropertyName); return; }

	UMovieSceneFloatSection* Section = Track->GetAllSections().Num() > 0
		? Cast<UMovieSceneFloatSection>(Track->GetAllSections()[0]) : nullptr;
	if (!Section) { OutError = TEXT("Float track has no section"); return; }

	FFrameNumber Frame = SecondsToFrame(MS, TimeSeconds);
	Section->GetChannel().GetData().UpdateOrAddKey(Frame, FMovieSceneFloatValue(Value));

	Section->Modify();
	Seq->MarkPackageDirty();
	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"actor\":\"%s\",\"property\":\"%s\",\"time_seconds\":%.3f,\"value\":%.4f,\"frame\":%d}"),
		*ActorLabel, *PropertyName, TimeSeconds, Value, Frame.Value);
}

void HandleAddVisibilityKeyframe(const FString& SequencePath, const FString& ActorLabel,
	float TimeSeconds, bool bVisible, FString& OutJsonString, FString& OutError)
{
	ULevelSequence* Seq = LoadSeq(SequencePath, OutError); if (!Seq) return;
	UMovieScene* MS = Seq->GetMovieScene();

	FGuid Guid = FindBindingGuid(MS, ActorLabel);
	if (!Guid.IsValid()) { OutError = FString::Printf(TEXT("No binding for actor '%s'"), *ActorLabel); return; }

	UMovieSceneVisibilityTrack* Track = nullptr;
	for (UMovieSceneTrack* T : MS->FindTracks(UMovieSceneVisibilityTrack::StaticClass(), Guid, NAME_None))
	{ Track = Cast<UMovieSceneVisibilityTrack>(T); if (Track) break; }
	if (!Track) { OutError = TEXT("No visibility track. Call add_sequence_visibility_track first."); return; }

	UMovieSceneBoolSection* Section = Track->GetAllSections().Num() > 0
		? Cast<UMovieSceneBoolSection>(Track->GetAllSections()[0]) : nullptr;
	if (!Section) { OutError = TEXT("Visibility track has no section"); return; }

	FFrameNumber Frame = SecondsToFrame(MS, TimeSeconds);
	Section->GetChannel().GetData().UpdateOrAddKey(Frame, !bVisible);

	Section->Modify();
	Seq->MarkPackageDirty();
	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"actor\":\"%s\",\"time_seconds\":%.3f,\"visible\":%s,\"frame\":%d}"),
		*ActorLabel, TimeSeconds, bVisible ? TEXT("true") : TEXT("false"), Frame.Value);
}

void HandleAddSequenceSpawnable(const FString& SequencePath, const FString& ActorClassPath,
	const FString& Label, FString& OutJsonString, FString& OutError)
{
	ULevelSequence* Seq = LoadSeq(SequencePath, OutError); if (!Seq) return;
	UMovieScene* MS = Seq->GetMovieScene();

	UClass* ActorClass = FindObject<UClass>(nullptr, *ActorClassPath);
	if (!ActorClass) ActorClass = LoadObject<UClass>(nullptr, *ActorClassPath);
	if (!ActorClass)
	{
		for (TObjectIterator<UClass> It; It; ++It)
			if (It->GetName().Equals(ActorClassPath, ESearchCase::IgnoreCase) && It->IsChildOf(AActor::StaticClass()))
			{ ActorClass = *It; break; }
	}
	if (!ActorClass || !ActorClass->IsChildOf(AActor::StaticClass()))
	{ OutError = FString::Printf(TEXT("Invalid actor class: '%s'. Use full path e.g. /Script/CinematicCamera.CineCameraActor"), *ActorClassPath); return; }

	FString SpawnName = Label.IsEmpty() ? ActorClass->GetName() : Label;

	for (int32 i = 0; i < MS->GetSpawnableCount(); ++i)
		if (MS->GetSpawnable(i).GetName().Equals(SpawnName, ESearchCase::IgnoreCase))
		{
			OutJsonString = FString::Printf(
				TEXT("{\"success\":true,\"message\":\"Spawnable already exists\",\"label\":\"%s\",\"guid\":\"%s\"}"),
				*SpawnName, *MS->GetSpawnable(i).GetGuid().ToString());
			return;
		}

	AActor* Template = NewObject<AActor>(GetTransientPackage(), ActorClass, NAME_None, RF_Transient);
	if (!Template) { OutError = TEXT("Failed to create template actor"); return; }

	FGuid Guid = MS->AddSpawnable(SpawnName, *Template);
	if (!Guid.IsValid()) { OutError = TEXT("Failed to add spawnable to sequence"); return; }

	UMovieSceneSpawnTrack* SpawnTrack = MS->AddTrack<UMovieSceneSpawnTrack>(Guid);
	if (SpawnTrack)
	{
		UMovieSceneSection* SpawnSection = SpawnTrack->CreateNewSection();
		if (SpawnSection)
		{
			SpawnSection->SetRange(MS->GetPlaybackRange());
			SpawnTrack->AddSection(*SpawnSection);
		}
	}

	Seq->MarkPackageDirty();
	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"label\":\"%s\",\"class\":\"%s\",\"guid\":\"%s\",\"message\":\"Spawnable added. Use add_sequence_transform_track/add_sequence_keyframe with this label.\"}"),
		*SpawnName, *ActorClassPath, *Guid.ToString());
}

void HandleGetSequenceKeyframes(const FString& SequencePath, const FString& ActorLabel,
	const FString& PropertyName, FString& OutJsonString, FString& OutError)
{
	ULevelSequence* Seq = LoadSeq(SequencePath, OutError); if (!Seq) return;
	UMovieScene* MS = Seq->GetMovieScene();
	FFrameRate TickRes = MS->GetTickResolution();

	FGuid Guid = FindBindingGuid(MS, ActorLabel);
	if (!Guid.IsValid()) { OutError = FString::Printf(TEXT("No binding for actor '%s'"), *ActorLabel); return; }

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("success"), true);
	Root->SetStringField(TEXT("actor"), ActorLabel);

	TArray<TSharedPtr<FJsonValue>> TracksArr;

	for (UMovieSceneTrack* T : MS->FindTracks(UMovieSceneTrack::StaticClass(), Guid, NAME_None))
	{
		if (UMovieSceneFloatTrack* FT = Cast<UMovieSceneFloatTrack>(T))
		{
			FString PropName = FT->GetPropertyName().ToString();
			if (!PropertyName.IsEmpty() && !PropName.Equals(PropertyName, ESearchCase::IgnoreCase))
				continue;

			for (UMovieSceneSection* Sec : FT->GetAllSections())
			{
				UMovieSceneFloatSection* FS = Cast<UMovieSceneFloatSection>(Sec);
				if (!FS) continue;

				const FMovieSceneFloatChannel& Ch = FS->GetChannel();
				TArrayView<const FFrameNumber> Times = Ch.GetData().GetTimes();
				TArrayView<const FMovieSceneFloatValue> Values = Ch.GetData().GetValues();

				TSharedPtr<FJsonObject> TrackObj = MakeShared<FJsonObject>();
				TrackObj->SetStringField(TEXT("property"), PropName);
				TrackObj->SetStringField(TEXT("track_type"), TEXT("float"));

				TArray<TSharedPtr<FJsonValue>> KeysArr;
				for (int32 k = 0; k < Times.Num(); ++k)
				{
					TSharedPtr<FJsonObject> Key = MakeShared<FJsonObject>();
					Key->SetNumberField(TEXT("time_seconds"), (double)Times[k].Value / TickRes.AsDecimal());
					Key->SetNumberField(TEXT("frame"), Times[k].Value);
					Key->SetNumberField(TEXT("value"), Values[k].Value);
					KeysArr.Add(MakeShared<FJsonValueObject>(Key));
				}
				TrackObj->SetArrayField(TEXT("keyframes"), KeysArr);
				TracksArr.Add(MakeShared<FJsonValueObject>(TrackObj));
			}
		}
		else if (UMovieSceneVisibilityTrack* VT = Cast<UMovieSceneVisibilityTrack>(T))
		{
			if (!PropertyName.IsEmpty() &&
				!FString(TEXT("visibility")).Equals(PropertyName, ESearchCase::IgnoreCase) &&
				!FString(TEXT("bHidden")).Equals(PropertyName, ESearchCase::IgnoreCase))
				continue;

			for (UMovieSceneSection* Sec : VT->GetAllSections())
			{
				UMovieSceneBoolSection* BS = Cast<UMovieSceneBoolSection>(Sec);
				if (!BS) continue;

				const FMovieSceneBoolChannel& Ch = BS->GetChannel();
				TArrayView<const FFrameNumber> Times = Ch.GetData().GetTimes();
				TArrayView<const bool> BoolValues = Ch.GetData().GetValues();

				TSharedPtr<FJsonObject> TrackObj = MakeShared<FJsonObject>();
				TrackObj->SetStringField(TEXT("property"), TEXT("visibility"));
				TrackObj->SetStringField(TEXT("track_type"), TEXT("visibility"));

				TArray<TSharedPtr<FJsonValue>> KeysArr;
				for (int32 k = 0; k < Times.Num(); ++k)
				{
					TSharedPtr<FJsonObject> Key = MakeShared<FJsonObject>();
					Key->SetNumberField(TEXT("time_seconds"), (double)Times[k].Value / TickRes.AsDecimal());
					Key->SetNumberField(TEXT("frame"), Times[k].Value);
					Key->SetBoolField(TEXT("visible"), !BoolValues[k]);
					KeysArr.Add(MakeShared<FJsonValueObject>(Key));
				}
				TrackObj->SetArrayField(TEXT("keyframes"), KeysArr);
				TracksArr.Add(MakeShared<FJsonValueObject>(TrackObj));
			}
		}
		else if (UMovieScene3DTransformTrack* TT = Cast<UMovieScene3DTransformTrack>(T))
		{
			if (!PropertyName.IsEmpty() && !FString(TEXT("transform")).Equals(PropertyName, ESearchCase::IgnoreCase))
				continue;

			for (UMovieSceneSection* Sec : TT->GetAllSections())
			{
				UMovieScene3DTransformSection* TS = Cast<UMovieScene3DTransformSection>(Sec);
				if (!TS) continue;

				TArrayView<FMovieSceneDoubleChannel*> Ch = TS->GetChannelProxy().GetChannels<FMovieSceneDoubleChannel>();
				if (Ch.Num() < 9) continue;

				TSet<FFrameNumber> FrameSet;
				for (int32 ci = 0; ci < 9; ++ci)
					for (const FFrameNumber& F : Ch[ci]->GetData().GetTimes())
						FrameSet.Add(F);

				TArray<FFrameNumber> Frames = FrameSet.Array();
				Frames.Sort([](const FFrameNumber& A, const FFrameNumber& B){ return A.Value < B.Value; });

				TArray<TSharedPtr<FJsonValue>> KeysArr;
				for (const FFrameNumber& F : Frames)
				{
					double Val[9] = {};
					for (int32 ci = 0; ci < 9; ++ci)
						Ch[ci]->Evaluate(F, Val[ci]);

					TSharedPtr<FJsonObject> Key = MakeShared<FJsonObject>();
					Key->SetNumberField(TEXT("time_seconds"), (double)F.Value / TickRes.AsDecimal());
					Key->SetNumberField(TEXT("frame"), F.Value);

					TArray<TSharedPtr<FJsonValue>> Loc, Rot, Sc;
					Loc.Add(MakeShared<FJsonValueNumber>(Val[0]));
					Loc.Add(MakeShared<FJsonValueNumber>(Val[1]));
					Loc.Add(MakeShared<FJsonValueNumber>(Val[2]));
					Key->SetArrayField(TEXT("location"), Loc);

					Rot.Add(MakeShared<FJsonValueNumber>(Val[3]));
					Rot.Add(MakeShared<FJsonValueNumber>(Val[4]));
					Rot.Add(MakeShared<FJsonValueNumber>(Val[5]));
					Key->SetArrayField(TEXT("rotation"), Rot);

					Sc.Add(MakeShared<FJsonValueNumber>(Val[6]));
					Sc.Add(MakeShared<FJsonValueNumber>(Val[7]));
					Sc.Add(MakeShared<FJsonValueNumber>(Val[8]));
					Key->SetArrayField(TEXT("scale"), Sc);

					KeysArr.Add(MakeShared<FJsonValueObject>(Key));
				}

				TSharedPtr<FJsonObject> TrackObj = MakeShared<FJsonObject>();
				TrackObj->SetStringField(TEXT("property"), TEXT("transform"));
				TrackObj->SetStringField(TEXT("track_type"), TEXT("transform"));
				TrackObj->SetArrayField(TEXT("keyframes"), KeysArr);
				TracksArr.Add(MakeShared<FJsonValueObject>(TrackObj));
			}
		}
	}

	Root->SetArrayField(TEXT("tracks"), TracksArr);
	FString Str; TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Str);
	FJsonSerializer::Serialize(Root.ToSharedRef(), W);
	OutJsonString = Str;
}

void HandleAddFloatKeyframeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString SequencePath, ActorLabel, PropertyName;
	Args->TryGetStringField(TEXT("sequence_path"), SequencePath);
	Args->TryGetStringField(TEXT("actor_label"), ActorLabel);
	Args->TryGetStringField(TEXT("property_name"), PropertyName);

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("keyframes"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString ItemActor = BatchToolHelper::GetItemString(Item, TEXT("actor_label"), TEXT("actor"));
			if (ItemActor.IsEmpty()) ItemActor = ActorLabel;
			FString ItemProp = BatchToolHelper::GetItemString(Item, TEXT("property_name"), TEXT("property"));
			if (ItemProp.IsEmpty()) ItemProp = PropertyName;
			double TimeSec = 0, Val = 0;
			Item->TryGetNumberField(TEXT("time_seconds"), TimeSec);
			Item->TryGetNumberField(TEXT("value"), Val);
			FString ItemOut, ItemErr;
			HandleAddFloatKeyframe(SequencePath, ItemActor, ItemProp, (float)TimeSec, (float)Val, ItemOut, ItemErr);
			if (ItemErr.IsEmpty()) { auto E = MakeShared<FJsonObject>(); E->SetNumberField(TEXT("time_seconds"), TimeSec); Batch.AddSuccess(i, E); }
			else Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJsonString);
		return;
	}
	double TimeSec = 0, Val = 0;
	Args->TryGetNumberField(TEXT("time_seconds"), TimeSec);
	Args->TryGetNumberField(TEXT("value"), Val);
	HandleAddFloatKeyframe(SequencePath, ActorLabel, PropertyName, (float)TimeSec, (float)Val, OutJsonString, OutError);
}

void HandleAddVisibilityKeyframeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString SequencePath, ActorLabel;
	Args->TryGetStringField(TEXT("sequence_path"), SequencePath);
	Args->TryGetStringField(TEXT("actor_label"), ActorLabel);

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("keyframes"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString ItemActor = BatchToolHelper::GetItemString(Item, TEXT("actor_label"), TEXT("actor"));
			if (ItemActor.IsEmpty()) ItemActor = ActorLabel;
			double TimeSec = 0; bool bVisible = true;
			Item->TryGetNumberField(TEXT("time_seconds"), TimeSec);
			Item->TryGetBoolField(TEXT("visible"), bVisible);
			FString ItemOut, ItemErr;
			HandleAddVisibilityKeyframe(SequencePath, ItemActor, (float)TimeSec, bVisible, ItemOut, ItemErr);
			if (ItemErr.IsEmpty()) { auto E = MakeShared<FJsonObject>(); E->SetNumberField(TEXT("time_seconds"), TimeSec); Batch.AddSuccess(i, E); }
			else Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJsonString);
		return;
	}
	double TimeSec = 0; bool bVisible = true;
	Args->TryGetNumberField(TEXT("time_seconds"), TimeSec);
	Args->TryGetBoolField(TEXT("visible"), bVisible);
	HandleAddVisibilityKeyframe(SequencePath, ActorLabel, (float)TimeSec, bVisible, OutJsonString, OutError);
}

void HandleAddSequenceSpawnableFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString SequencePath;
	Args->TryGetStringField(TEXT("sequence_path"), SequencePath);

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("spawnables"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString ClassPath = BatchToolHelper::GetItemString(Item, TEXT("actor_class"), TEXT("class"));
			FString LabelStr = BatchToolHelper::GetItemString(Item, TEXT("label"), TEXT("name"));
			FString ItemOut, ItemErr;
			HandleAddSequenceSpawnable(SequencePath, ClassPath, LabelStr, ItemOut, ItemErr);
			if (ItemErr.IsEmpty()) { auto E = MakeShared<FJsonObject>(); E->SetStringField(TEXT("label"), LabelStr); Batch.AddSuccess(i, E); }
			else Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJsonString);
		return;
	}
	FString ActorClass, Label;
	Args->TryGetStringField(TEXT("actor_class"), ActorClass);
	Args->TryGetStringField(TEXT("label"), Label);
	HandleAddSequenceSpawnable(SequencePath, ActorClass, Label, OutJsonString, OutError);
}

static AActor* SpawnRigActor(UClass* Class, const FString& Label, double LocX, double LocY, double LocZ, FString& OutError)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World) { OutError = TEXT("No editor world"); return nullptr; }
	if (!Class) { OutError = TEXT("Invalid actor class"); return nullptr; }

	FActorSpawnParameters P;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AActor* Actor = World->SpawnActor<AActor>(Class, FVector((float)LocX, (float)LocY, (float)LocZ), FRotator::ZeroRotator, P);
	if (!Actor) { OutError = TEXT("SpawnActor failed"); return nullptr; }
	if (!Label.IsEmpty()) Actor->SetActorLabel(Label);
	return Actor;
}

void HandleSpawnCameraRigRailFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("rigs"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString Label; double X=0,Y=0,Z=0;
			Item->TryGetStringField(TEXT("actor_label"),Label);
			Item->TryGetNumberField(TEXT("location_x"),X); Item->TryGetNumberField(TEXT("location_y"),Y); Item->TryGetNumberField(TEXT("location_z"),Z);
			FString IOut, IErr;
			FString IOuter;
			AActor* A = SpawnRigActor(ACameraRig_Rail::StaticClass(), Label, X, Y, Z, IErr);
			if (!IErr.IsEmpty()) { Batch.AddFailure(i, IErr); continue; }
			auto E = MakeShared<FJsonObject>(); E->SetStringField(TEXT("actor_label"), A->GetActorLabel()); Batch.AddSuccess(i, E);
		}
		Batch.Finalize(OutJsonString); return;
	}
	FString Label; double X=0,Y=0,Z=0;
	Args->TryGetStringField(TEXT("actor_label"),Label);
	Args->TryGetNumberField(TEXT("location_x"),X); Args->TryGetNumberField(TEXT("location_y"),Y); Args->TryGetNumberField(TEXT("location_z"),Z);
	AActor* A = SpawnRigActor(ACameraRig_Rail::StaticClass(), Label, X, Y, Z, OutError);
	if (OutError.IsEmpty())
	{
		TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
		R->SetBoolField(TEXT("success"), true);
		R->SetStringField(TEXT("actor_label"), A->GetActorLabel());
		FString S; TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&S);
		FJsonSerializer::Serialize(R.ToSharedRef(), W); OutJsonString = S;
	}
}

void HandleSpawnCameraRigCraneFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("rigs"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString Label; double X=0,Y=0,Z=0,ArmLen=500;
			Item->TryGetStringField(TEXT("actor_label"),Label);
			Item->TryGetNumberField(TEXT("location_x"),X); Item->TryGetNumberField(TEXT("location_y"),Y); Item->TryGetNumberField(TEXT("location_z"),Z);
			Item->TryGetNumberField(TEXT("crane_arm_length"),ArmLen);
			FString IErr;
			AActor* A = SpawnRigActor(ACameraRig_Crane::StaticClass(), Label, X, Y, Z, IErr);
			if (!IErr.IsEmpty()) { Batch.AddFailure(i, IErr); continue; }
			if (ACameraRig_Crane* Crane = Cast<ACameraRig_Crane>(A)) Crane->CraneArmLength = (float)ArmLen;
			auto E = MakeShared<FJsonObject>(); E->SetStringField(TEXT("actor_label"), A->GetActorLabel()); Batch.AddSuccess(i, E);
		}
		Batch.Finalize(OutJsonString); return;
	}
	FString Label; double X=0,Y=0,Z=0,ArmLen=500;
	Args->TryGetStringField(TEXT("actor_label"),Label);
	Args->TryGetNumberField(TEXT("location_x"),X); Args->TryGetNumberField(TEXT("location_y"),Y); Args->TryGetNumberField(TEXT("location_z"),Z);
	Args->TryGetNumberField(TEXT("crane_arm_length"),ArmLen);
	AActor* A = SpawnRigActor(ACameraRig_Crane::StaticClass(), Label, X, Y, Z, OutError);
	if (OutError.IsEmpty())
	{
		if (ACameraRig_Crane* Crane = Cast<ACameraRig_Crane>(A)) Crane->CraneArmLength = (float)ArmLen;
		TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
		R->SetBoolField(TEXT("success"), true);
		R->SetStringField(TEXT("actor_label"), A->GetActorLabel());
		R->SetNumberField(TEXT("crane_arm_length"), ArmLen);
		FString S; TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&S);
		FJsonSerializer::Serialize(R.ToSharedRef(), W); OutJsonString = S;
	}
}

void HandleAttachCameraToRigFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World) { OutError = TEXT("No editor world"); return; }

	auto FindActorByLabel = [World](const FString& Label) -> AActor* {
		for (TActorIterator<AActor> It(World); It; ++It)
			if ((*It)->GetActorLabel().Equals(Label, ESearchCase::IgnoreCase)) return *It;
		return nullptr;
	};

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("attachments"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString CamLabel, RigLabel;
			Item->TryGetStringField(TEXT("camera_label"),CamLabel); Item->TryGetStringField(TEXT("rig_label"),RigLabel);
			AActor* Cam = FindActorByLabel(CamLabel); AActor* Rig = FindActorByLabel(RigLabel);
			if (!Cam) { Batch.AddFailure(i, FString::Printf(TEXT("Camera '%s' not found"), *CamLabel)); continue; }
			if (!Rig) { Batch.AddFailure(i, FString::Printf(TEXT("Rig '%s' not found"), *RigLabel)); continue; }
			Cam->AttachToActor(Rig, FAttachmentTransformRules::KeepRelativeTransform);
			auto E = MakeShared<FJsonObject>(); E->SetStringField(TEXT("camera_label"),CamLabel); E->SetStringField(TEXT("rig_label"),RigLabel); Batch.AddSuccess(i, E);
		}
		Batch.Finalize(OutJsonString); return;
	}
	FString CamLabel, RigLabel;
	Args->TryGetStringField(TEXT("camera_label"),CamLabel); Args->TryGetStringField(TEXT("rig_label"),RigLabel);
	AActor* Cam = FindActorByLabel(CamLabel); AActor* Rig = FindActorByLabel(RigLabel);
	if (!Cam) { OutError = FString::Printf(TEXT("Camera '%s' not found"), *CamLabel); return; }
	if (!Rig) { OutError = FString::Printf(TEXT("Rig '%s' not found"), *RigLabel); return; }
	Cam->AttachToActor(Rig, FAttachmentTransformRules::KeepRelativeTransform);
	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetBoolField(TEXT("success"), true); R->SetStringField(TEXT("camera_label"),CamLabel); R->SetStringField(TEXT("rig_label"),RigLabel);
	FString S; TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&S);
	FJsonSerializer::Serialize(R.ToSharedRef(), W); OutJsonString = S;
}

void HandleSetRigRailPositionFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World) { OutError = TEXT("No editor world"); return; }

	auto FindActorByLabel = [World](const FString& Label) -> AActor* {
		for (TActorIterator<AActor> It(World); It; ++It)
			if ((*It)->GetActorLabel().Equals(Label, ESearchCase::IgnoreCase)) return *It;
		return nullptr;
	};

	const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("rigs"), ItemsArray))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArray->Num(); i++)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArray)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			FString Label; double Pos = 0;
			Item->TryGetStringField(TEXT("actor_label"),Label); Item->TryGetNumberField(TEXT("position"),Pos);
			ACameraRig_Rail* Rail = Cast<ACameraRig_Rail>(FindActorByLabel(Label));
			if (!Rail) { Batch.AddFailure(i, FString::Printf(TEXT("CameraRigRail '%s' not found"), *Label)); continue; }
			Rail->CurrentPositionOnRail = FMath::Clamp((float)Pos, 0.f, 1.f);
			auto E = MakeShared<FJsonObject>(); E->SetStringField(TEXT("actor_label"),Label); E->SetNumberField(TEXT("position"),(float)Pos); Batch.AddSuccess(i, E);
		}
		Batch.Finalize(OutJsonString); return;
	}
	FString Label; double Pos = 0;
	Args->TryGetStringField(TEXT("actor_label"),Label); Args->TryGetNumberField(TEXT("position"),Pos);
	ACameraRig_Rail* Rail = Cast<ACameraRig_Rail>(FindActorByLabel(Label));
	if (!Rail) { OutError = FString::Printf(TEXT("CameraRigRail '%s' not found"), *Label); return; }
	Rail->CurrentPositionOnRail = FMath::Clamp((float)Pos, 0.f, 1.f);
	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetBoolField(TEXT("success"), true); R->SetStringField(TEXT("actor_label"),Label); R->SetNumberField(TEXT("position"),(float)Pos);
	FString S; TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&S);
	FJsonSerializer::Serialize(R.ToSharedRef(), W); OutJsonString = S;
}

void HandleGetRigSummary(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World) { OutError = TEXT("No editor world"); return; }

	TArray<TSharedPtr<FJsonValue>> RigArray;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* A = *It;
		TSharedPtr<FJsonObject> RigObj = MakeShared<FJsonObject>();
		if (ACameraRig_Rail* Rail = Cast<ACameraRig_Rail>(A))
		{
			RigObj->SetStringField(TEXT("type"), TEXT("CameraRigRail"));
			RigObj->SetStringField(TEXT("actor_label"), A->GetActorLabel());
			RigObj->SetNumberField(TEXT("position_on_rail"), Rail->CurrentPositionOnRail);
			RigObj->SetBoolField(TEXT("lock_orientation"), Rail->bLockOrientationToRail);
		}
		else if (ACameraRig_Crane* Crane = Cast<ACameraRig_Crane>(A))
		{
			RigObj->SetStringField(TEXT("type"), TEXT("CameraRigCrane"));
			RigObj->SetStringField(TEXT("actor_label"), A->GetActorLabel());
			RigObj->SetNumberField(TEXT("crane_arm_length"), Crane->CraneArmLength);
			RigObj->SetNumberField(TEXT("crane_pitch"), Crane->CranePitch);
			RigObj->SetNumberField(TEXT("crane_yaw"), Crane->CraneYaw);
		}
		else continue;
		RigArray.Add(MakeShared<FJsonValueObject>(RigObj));
	}

	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetBoolField(TEXT("success"), true);
	R->SetArrayField(TEXT("rigs"), RigArray);
	R->SetNumberField(TEXT("count"), RigArray.Num());
	FString S; TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&S);
	FJsonSerializer::Serialize(R.ToSharedRef(), W); OutJsonString = S;
}

void HandleRemoveSequenceBinding(const FString& SequencePath, const FString& ActorLabel,
	FString& OutJsonString, FString& OutError)
{
	ULevelSequence* Seq = LoadSeq(SequencePath, OutError); if (!Seq) return;
	UMovieScene* MS = Seq->GetMovieScene();

	FGuid Guid = FindBindingGuid(MS, ActorLabel);
	if (!Guid.IsValid()) { OutError = FString::Printf(TEXT("No binding found for actor '%s'"), *ActorLabel); return; }

	bool bRemoved = MS->RemoveSpawnable(Guid);
	if (!bRemoved) bRemoved = MS->RemovePossessable(Guid);
	if (!bRemoved) { OutError = FString::Printf(TEXT("Failed to remove binding for '%s'"), *ActorLabel); return; }

	Seq->MarkPackageDirty();
	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"sequence\":\"%s\",\"removed_actor\":\"%s\"}"),
		*SequencePath, *ActorLabel);
}

static UMovieSceneSection* FindSectionByTrackType(UMovieScene* MS, const FGuid& Guid,
	const FString& TrackType, int32 SectionIndex)
{
	for (UMovieSceneTrack* T : MS->FindTracks(UMovieSceneTrack::StaticClass(), Guid, NAME_None))
	{
		if (TrackType.IsEmpty() || T->GetClass()->GetName().Contains(TrackType, ESearchCase::IgnoreCase))
		{
			const TArray<UMovieSceneSection*>& Sections = T->GetAllSections();
			if (Sections.IsValidIndex(SectionIndex)) return Sections[SectionIndex];
		}
	}
	return nullptr;
}

void HandleSetSectionBlendType(const FString& SequencePath, const FString& ActorLabel,
	const FString& TrackType, int32 SectionIndex, const FString& BlendTypeStr,
	FString& OutJsonString, FString& OutError)
{
	ULevelSequence* Seq = LoadSeq(SequencePath, OutError); if (!Seq) return;
	UMovieScene* MS = Seq->GetMovieScene();

	FGuid Guid = FindOrAddBinding(Seq, ActorLabel, OutError); if (!Guid.IsValid()) return;

	UMovieSceneSection* Section = FindSectionByTrackType(MS, Guid, TrackType, SectionIndex);
	if (!Section) { OutError = FString::Printf(TEXT("No section found: actor='%s' track_type='%s' index=%d"), *ActorLabel, *TrackType, SectionIndex); return; }

	EMovieSceneBlendType BlendType = EMovieSceneBlendType::Absolute;
	FString ResolvedBlend;
	if (BlendTypeStr.Contains(TEXT("additive_from"), ESearchCase::IgnoreCase)) { BlendType = EMovieSceneBlendType::AdditiveFromBase; ResolvedBlend = TEXT("AdditiveFromBase"); }
	else if (BlendTypeStr.Contains(TEXT("additive"), ESearchCase::IgnoreCase))  { BlendType = EMovieSceneBlendType::Additive; ResolvedBlend = TEXT("Additive"); }
	else if (BlendTypeStr.Contains(TEXT("relative"), ESearchCase::IgnoreCase))  { BlendType = EMovieSceneBlendType::Relative; ResolvedBlend = TEXT("Relative"); }
	else if (BlendTypeStr.Contains(TEXT("absolute"), ESearchCase::IgnoreCase))  { BlendType = EMovieSceneBlendType::Absolute; ResolvedBlend = TEXT("Absolute"); }
#if !UE_VERSION_OLDER_THAN(5, 5, 0)
	else if (BlendTypeStr.Contains(TEXT("override"), ESearchCase::IgnoreCase))  { BlendType = EMovieSceneBlendType::Override; ResolvedBlend = TEXT("Override"); }
#endif

	if (ResolvedBlend.IsEmpty())
	{
#if !UE_VERSION_OLDER_THAN(5, 5, 0)
		OutError = FString::Printf(TEXT("Unknown blend_type '%s'. Valid values: Absolute, Additive, Relative, AdditiveFromBase, Override."), *BlendTypeStr);
#else
		OutError = FString::Printf(TEXT("Unknown blend_type '%s'. Valid values: Absolute, Additive, Relative, AdditiveFromBase."), *BlendTypeStr);
#endif
		return;
	}

	Section->SetBlendType(BlendType);
	Section->Modify();
	Seq->MarkPackageDirty();
	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"sequence\":\"%s\",\"actor\":\"%s\",\"blend_type\":\"%s\"}"),
		*SequencePath, *ActorLabel, *ResolvedBlend);
}

void HandleSetSectionCompletionMode(const FString& SequencePath, const FString& ActorLabel,
	const FString& TrackType, int32 SectionIndex, const FString& ModeStr,
	FString& OutJsonString, FString& OutError)
{
	ULevelSequence* Seq = LoadSeq(SequencePath, OutError); if (!Seq) return;
	UMovieScene* MS = Seq->GetMovieScene();

	FGuid Guid = FindOrAddBinding(Seq, ActorLabel, OutError); if (!Guid.IsValid()) return;

	UMovieSceneSection* Section = FindSectionByTrackType(MS, Guid, TrackType, SectionIndex);
	if (!Section) { OutError = FString::Printf(TEXT("No section found: actor='%s' track_type='%s' index=%d"), *ActorLabel, *TrackType, SectionIndex); return; }

	EMovieSceneCompletionMode Mode = EMovieSceneCompletionMode::ProjectDefault;
	FString ResolvedMode;
	if (ModeStr.Contains(TEXT("keep"), ESearchCase::IgnoreCase))         { Mode = EMovieSceneCompletionMode::KeepState; ResolvedMode = TEXT("KeepState"); }
	else if (ModeStr.Contains(TEXT("restore"), ESearchCase::IgnoreCase)) { Mode = EMovieSceneCompletionMode::RestoreState; ResolvedMode = TEXT("RestoreState"); }
	else if (ModeStr.Contains(TEXT("project"), ESearchCase::IgnoreCase) || ModeStr.Contains(TEXT("default"), ESearchCase::IgnoreCase)) { Mode = EMovieSceneCompletionMode::ProjectDefault; ResolvedMode = TEXT("ProjectDefault"); }

	if (ResolvedMode.IsEmpty())
	{
		OutError = FString::Printf(TEXT("Unknown completion_mode '%s'. Valid values: KeepState, RestoreState, ProjectDefault."), *ModeStr);
		return;
	}

	Section->EvalOptions.bCanEditCompletionMode = true;
	Section->SetCompletionMode(Mode);
	Section->Modify();
	Seq->MarkPackageDirty();
	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"sequence\":\"%s\",\"actor\":\"%s\",\"completion_mode\":\"%s\"}"),
		*SequencePath, *ActorLabel, *ResolvedMode);
}

void HandleAddSequenceBoolTrack(const FString& SequencePath, const FString& ActorLabel,
	const FString& PropertyName, FString& OutJsonString, FString& OutError)
{
	ULevelSequence* Seq = LoadSeq(SequencePath, OutError); if (!Seq) return;
	UMovieScene* MS = Seq->GetMovieScene();

	FGuid Guid = FindOrAddBinding(Seq, ActorLabel, OutError); if (!Guid.IsValid()) return;

	UMovieSceneBoolTrack* Track = MS->AddTrack<UMovieSceneBoolTrack>(Guid);
	if (!Track) { OutError = TEXT("Failed to create bool track (may already exist)"); return; }

	Track->SetPropertyNameAndPath(FName(*PropertyName), PropertyName);

	UMovieSceneSection* Section = Track->CreateNewSection();
	if (Section) { Section->SetRange(MS->GetPlaybackRange()); Track->AddSection(*Section); }

	Seq->MarkPackageDirty();
	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"sequence\":\"%s\",\"actor\":\"%s\",\"property\":\"%s\",\"message\":\"Bool track added. Use add_bool_keyframe.\"}"),
		*SequencePath, *ActorLabel, *PropertyName);
}

void HandleAddBoolKeyframe(const FString& SequencePath, const FString& ActorLabel,
	const FString& PropertyName, float TimeSeconds, bool bValue,
	FString& OutJsonString, FString& OutError)
{
	ULevelSequence* Seq = LoadSeq(SequencePath, OutError); if (!Seq) return;
	UMovieScene* MS = Seq->GetMovieScene();

	FGuid Guid = FindBindingGuid(MS, ActorLabel);
	if (!Guid.IsValid()) { OutError = FString::Printf(TEXT("No binding for actor '%s'"), *ActorLabel); return; }

	UMovieSceneBoolTrack* Track = nullptr;
	for (UMovieSceneTrack* T : MS->FindTracks(UMovieSceneBoolTrack::StaticClass(), Guid, NAME_None))
	{
		UMovieSceneBoolTrack* BT = Cast<UMovieSceneBoolTrack>(T);
		if (!BT) continue;
		if (PropertyName.IsEmpty() || BT->GetPropertyName().ToString().Equals(PropertyName, ESearchCase::IgnoreCase))
		{ Track = BT; break; }
	}
	if (!Track) { OutError = FString::Printf(TEXT("No bool track for property '%s'. Use add_sequence_bool_track first."), *PropertyName); return; }

	UMovieSceneBoolSection* Section = Track->GetAllSections().Num() > 0 ? Cast<UMovieSceneBoolSection>(Track->GetAllSections()[0]) : nullptr;
	if (!Section) { OutError = TEXT("Bool track has no section"); return; }

	FFrameNumber Frame = SecondsToFrame(MS, TimeSeconds);
	Section->GetChannel().GetData().UpdateOrAddKey(Frame, bValue);
	Section->Modify();
	Seq->MarkPackageDirty();
	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"actor\":\"%s\",\"property\":\"%s\",\"time_seconds\":%.3f,\"value\":%s}"),
		*ActorLabel, *PropertyName, TimeSeconds, bValue ? TEXT("true") : TEXT("false"));
}

void HandleAddSequenceIntegerTrack(const FString& SequencePath, const FString& ActorLabel,
	const FString& PropertyName, FString& OutJsonString, FString& OutError)
{
	ULevelSequence* Seq = LoadSeq(SequencePath, OutError); if (!Seq) return;
	UMovieScene* MS = Seq->GetMovieScene();

	FGuid Guid = FindOrAddBinding(Seq, ActorLabel, OutError); if (!Guid.IsValid()) return;

	UMovieSceneIntegerTrack* Track = MS->AddTrack<UMovieSceneIntegerTrack>(Guid);
	if (!Track) { OutError = TEXT("Failed to create integer track (may already exist)"); return; }

	Track->SetPropertyNameAndPath(FName(*PropertyName), PropertyName);

	UMovieSceneSection* Section = Track->CreateNewSection();
	if (Section) { Section->SetRange(MS->GetPlaybackRange()); Track->AddSection(*Section); }

	Seq->MarkPackageDirty();
	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"sequence\":\"%s\",\"actor\":\"%s\",\"property\":\"%s\",\"message\":\"Integer track added. Use add_integer_keyframe.\"}"),
		*SequencePath, *ActorLabel, *PropertyName);
}

void HandleAddIntegerKeyframe(const FString& SequencePath, const FString& ActorLabel,
	const FString& PropertyName, float TimeSeconds, int32 IntValue,
	FString& OutJsonString, FString& OutError)
{
	ULevelSequence* Seq = LoadSeq(SequencePath, OutError); if (!Seq) return;
	UMovieScene* MS = Seq->GetMovieScene();

	FGuid Guid = FindBindingGuid(MS, ActorLabel);
	if (!Guid.IsValid()) { OutError = FString::Printf(TEXT("No binding for actor '%s'"), *ActorLabel); return; }

	UMovieSceneIntegerTrack* Track = nullptr;
	for (UMovieSceneTrack* T : MS->FindTracks(UMovieSceneIntegerTrack::StaticClass(), Guid, NAME_None))
	{
		UMovieSceneIntegerTrack* IT = Cast<UMovieSceneIntegerTrack>(T);
		if (!IT) continue;
		if (PropertyName.IsEmpty() || IT->GetPropertyName().ToString().Equals(PropertyName, ESearchCase::IgnoreCase))
		{ Track = IT; break; }
	}
	if (!Track) { OutError = FString::Printf(TEXT("No integer track for property '%s'. Use add_sequence_integer_track first."), *PropertyName); return; }

	UMovieSceneIntegerSection* Section = Track->GetAllSections().Num() > 0 ? Cast<UMovieSceneIntegerSection>(Track->GetAllSections()[0]) : nullptr;
	if (!Section) { OutError = TEXT("Integer track has no section"); return; }

	FFrameNumber Frame = SecondsToFrame(MS, TimeSeconds);
	FMovieSceneIntegerChannel* Ch = Section->GetChannelProxy().GetChannel<FMovieSceneIntegerChannel>(0);
	if (!Ch) { OutError = TEXT("Failed to access integer channel"); return; }
	Ch->GetData().UpdateOrAddKey(Frame, IntValue);
	Section->Modify();
	Seq->MarkPackageDirty();
	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"actor\":\"%s\",\"property\":\"%s\",\"time_seconds\":%.3f,\"value\":%d}"),
		*ActorLabel, *PropertyName, TimeSeconds, IntValue);
}

void HandleAddBoolKeyframeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString SP, AL, PN; Args->TryGetStringField(TEXT("sequence_path"), SP); Args->TryGetStringField(TEXT("actor_label"), AL); Args->TryGetStringField(TEXT("property_name"), PN);
	const TArray<TSharedPtr<FJsonValue>>* ItemsArr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("bool_keyframes"), ItemsArr))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArr->Num(); ++i)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArr)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			double TS = 0; Item->TryGetNumberField(TEXT("time_seconds"), TS);
			bool BV = false; Item->TryGetBoolField(TEXT("bool_value"), BV);
			FString ItemOut, ItemErr;
			HandleAddBoolKeyframe(SP, AL, PN, (float)TS, BV, ItemOut, ItemErr);
			if (ItemErr.IsEmpty()) { auto E = MakeShared<FJsonObject>(); E->SetNumberField(TEXT("time"), TS); E->SetBoolField(TEXT("value"), BV); Batch.AddSuccess(i, E); }
			else Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJsonString); return;
	}
	double TS = 0; bool BV = false;
	Args->TryGetNumberField(TEXT("time_seconds"), TS); Args->TryGetBoolField(TEXT("bool_value"), BV);
	HandleAddBoolKeyframe(SP, AL, PN, (float)TS, BV, OutJsonString, OutError);
}

void HandleAddColorKeyframe(const FString& SequencePath, const FString& ActorLabel,
	const FString& PropertyName, float TimeSeconds,
	float R, float G, float B, float A,
	FString& OutJsonString, FString& OutError)
{
	ULevelSequence* Seq = LoadSeq(SequencePath, OutError); if (!Seq) return;
	UMovieScene* MS = Seq->GetMovieScene();

	FGuid Guid = FindBindingGuid(MS, ActorLabel);
	if (!Guid.IsValid()) { OutError = FString::Printf(TEXT("No binding for actor '%s'"), *ActorLabel); return; }

	UMovieSceneColorTrack* Track = nullptr;
	for (UMovieSceneTrack* T : MS->FindTracks(UMovieSceneColorTrack::StaticClass(), Guid, NAME_None))
	{
		UMovieSceneColorTrack* CT = Cast<UMovieSceneColorTrack>(T);
		if (!CT) continue;
		if (PropertyName.IsEmpty() || CT->GetPropertyName().ToString().Equals(PropertyName, ESearchCase::IgnoreCase))
		{ Track = CT; break; }
	}
	if (!Track) { OutError = FString::Printf(TEXT("No color track for property '%s'. Use add_sequence_color_track first."), *PropertyName); return; }

	UMovieSceneColorSection* Section = Track->GetAllSections().Num() > 0 ? Cast<UMovieSceneColorSection>(Track->GetAllSections()[0]) : nullptr;
	if (!Section) { OutError = TEXT("Color track has no section"); return; }

	FFrameNumber Frame = SecondsToFrame(MS, TimeSeconds);
	Section->GetRedChannel().GetData().UpdateOrAddKey(Frame,   FMovieSceneFloatValue(R));
	Section->GetGreenChannel().GetData().UpdateOrAddKey(Frame, FMovieSceneFloatValue(G));
	Section->GetBlueChannel().GetData().UpdateOrAddKey(Frame,  FMovieSceneFloatValue(B));
	Section->GetAlphaChannel().GetData().UpdateOrAddKey(Frame, FMovieSceneFloatValue(A));
	Section->Modify();
	Seq->MarkPackageDirty();
	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"actor\":\"%s\",\"property\":\"%s\",\"time_seconds\":%.3f,\"r\":%.4f,\"g\":%.4f,\"b\":%.4f,\"a\":%.4f}"),
		*ActorLabel, *PropertyName, TimeSeconds, R, G, B, A);
}

void HandleAddColorKeyframeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	FString SP, AL, PN; Args->TryGetStringField(TEXT("sequence_path"), SP); Args->TryGetStringField(TEXT("actor_label"), AL); Args->TryGetStringField(TEXT("property_name"), PN);
	const TArray<TSharedPtr<FJsonValue>>* ItemsArr;
	if (BatchToolHelper::TryGetBatchItems(Args, TEXT("color_keyframes"), ItemsArr))
	{
		BatchToolHelper::FBatchResultBuilder Batch;
		for (int32 i = 0; i < ItemsArr->Num(); ++i)
		{
			TSharedPtr<FJsonObject> Item = (*ItemsArr)[i]->AsObject();
			if (!Item.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
			double TS=0, RR=1, GG=1, BB=1, AA=1;
			Item->TryGetNumberField(TEXT("time_seconds"), TS);
			Item->TryGetNumberField(TEXT("r"), RR); Item->TryGetNumberField(TEXT("g"), GG);
			Item->TryGetNumberField(TEXT("b"), BB); Item->TryGetNumberField(TEXT("a"), AA);
			FString ItemOut, ItemErr;
			HandleAddColorKeyframe(SP, AL, PN, (float)TS, (float)RR, (float)GG, (float)BB, (float)AA, ItemOut, ItemErr);
			if (ItemErr.IsEmpty()) { auto E = MakeShared<FJsonObject>(); E->SetNumberField(TEXT("time"), TS); Batch.AddSuccess(i, E); }
			else Batch.AddFailure(i, ItemErr);
		}
		Batch.Finalize(OutJsonString); return;
	}
	double TS=0, RR=1, GG=1, BB=1, AA=1;
	Args->TryGetNumberField(TEXT("time_seconds"), TS); Args->TryGetNumberField(TEXT("r"), RR);
	Args->TryGetNumberField(TEXT("g"), GG); Args->TryGetNumberField(TEXT("b"), BB); Args->TryGetNumberField(TEXT("a"), AA);
	HandleAddColorKeyframe(SP, AL, PN, (float)TS, (float)RR, (float)GG, (float)BB, (float)AA, OutJsonString, OutError);
}

void HandleAddLevelVisibilitySection(const FString& SequencePath,
	const TArray<FString>& LevelNames, const FString& VisibilityStr,
	float StartTimeSec, float EndTimeSec,
	FString& OutJsonString, FString& OutError)
{
	ULevelSequence* Seq = LoadSeq(SequencePath, OutError); if (!Seq) return;
	UMovieScene* MS = Seq->GetMovieScene();

	UMovieSceneLevelVisibilityTrack* Track = MS->FindTrack<UMovieSceneLevelVisibilityTrack>();
	if (!Track)
	{
		Track = MS->AddTrack<UMovieSceneLevelVisibilityTrack>();
		if (!Track) { OutError = TEXT("Failed to create level visibility track"); return; }
	}

	UMovieSceneLevelVisibilitySection* Section = Cast<UMovieSceneLevelVisibilitySection>(Track->CreateNewSection());
	if (!Section) { OutError = TEXT("Failed to create level visibility section"); return; }

	ELevelVisibility Visibility = VisibilityStr.Contains(TEXT("hidden"), ESearchCase::IgnoreCase)
		? ELevelVisibility::Hidden : ELevelVisibility::Visible;
	Section->SetVisibility(Visibility);

	TArray<FName> LevelFNames;
	for (const FString& LN : LevelNames) LevelFNames.Add(FName(*LN));
	Section->SetLevelNames(LevelFNames);

	FFrameNumber StartFrame = SecondsToFrame(MS, StartTimeSec);
	FFrameNumber EndFrame   = SecondsToFrame(MS, EndTimeSec > StartTimeSec ? EndTimeSec : StartTimeSec + 1.f);
	Section->SetRange(TRange<FFrameNumber>(StartFrame, EndFrame));

	Track->AddSection(*Section);
	Seq->MarkPackageDirty();

	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"sequence\":\"%s\",\"visibility\":\"%s\",\"level_count\":%d,\"start\":%.2f,\"end\":%.2f}"),
		*SequencePath, *VisibilityStr, LevelNames.Num(), StartTimeSec, EndTimeSec);
}

void HandleListSubsequences(const FString& SequencePath, FString& OutJsonString, FString& OutError)
{
	ULevelSequence* Seq = LoadSeq(SequencePath, OutError); if (!Seq) return;
	UMovieScene* MS = Seq->GetMovieScene();
	FFrameRate TickRes = MS->GetTickResolution();

	UMovieSceneSubTrack* SubTrack = MS->FindTrack<UMovieSceneSubTrack>();
	TArray<TSharedPtr<FJsonValue>> SubsArr;

	if (SubTrack)
	{
		const TArray<UMovieSceneSection*>& Sections = SubTrack->GetAllSections();
		for (int32 i = 0; i < Sections.Num(); ++i)
		{
			UMovieSceneSubSection* Sub = Cast<UMovieSceneSubSection>(Sections[i]);
			if (!Sub) continue;

			TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
			Obj->SetNumberField(TEXT("index"), i);

			UMovieSceneSequence* SubSeq = Sub->GetSequence();
			Obj->SetStringField(TEXT("sequence"), SubSeq ? SubSeq->GetPathName() : TEXT(""));
			Obj->SetStringField(TEXT("sequence_name"), SubSeq ? SubSeq->GetName() : TEXT(""));

			TRange<FFrameNumber> Range = Sub->GetRange();
			double StartSec = Range.GetLowerBound().IsInclusive() ? (double)Range.GetLowerBoundValue().Value / TickRes.AsDecimal() : 0.0;
			double EndSec   = Range.GetUpperBound().IsInclusive() ? (double)Range.GetUpperBoundValue().Value / TickRes.AsDecimal() : StartSec;
			Obj->SetNumberField(TEXT("start_seconds"), StartSec);
			Obj->SetNumberField(TEXT("end_seconds"),   EndSec);
			Obj->SetNumberField(TEXT("start_offset_frames"), Sub->Parameters.StartFrameOffset.Value);
#if UE_VERSION_OLDER_THAN(5, 5, 0)
			const float TSVal = Sub->Parameters.TimeScale;
#else
			const float TSVal = (Sub->Parameters.TimeScale.GetType() == EMovieSceneTimeWarpType::FixedPlayRate)
				? Sub->Parameters.TimeScale.AsFixedPlayRateFloat() : 1.0f;
#endif
			Obj->SetNumberField(TEXT("time_scale"), TSVal);

			SubsArr.Add(MakeShared<FJsonValueObject>(Obj));
		}
	}

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("success"), true);
	Root->SetStringField(TEXT("sequence"), SequencePath);
	Root->SetNumberField(TEXT("count"), SubsArr.Num());
	Root->SetArrayField(TEXT("subsequences"), SubsArr);
	FString S; TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&S);
	FJsonSerializer::Serialize(Root.ToSharedRef(), W); OutJsonString = S;
}

void HandleRemoveSubsequence(const FString& SequencePath, int32 SubsequenceIndex,
	FString& OutJsonString, FString& OutError)
{
	ULevelSequence* Seq = LoadSeq(SequencePath, OutError); if (!Seq) return;
	UMovieScene* MS = Seq->GetMovieScene();

	UMovieSceneSubTrack* SubTrack = MS->FindTrack<UMovieSceneSubTrack>();
	if (!SubTrack) { OutError = TEXT("No sub-sequence track in this sequence"); return; }

	const TArray<UMovieSceneSection*>& Sections = SubTrack->GetAllSections();
	if (!Sections.IsValidIndex(SubsequenceIndex))
	{
		OutError = FString::Printf(TEXT("Invalid subsequence_index %d (count=%d)"), SubsequenceIndex, Sections.Num());
		return;
	}

	UMovieSceneSection* Section = Sections[SubsequenceIndex];
	SubTrack->RemoveSection(*Section);
	Seq->MarkPackageDirty();
	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"sequence\":\"%s\",\"removed_index\":%d}"), *SequencePath, SubsequenceIndex);
}

void HandleSetSubsequenceParams(const FString& SequencePath, int32 SubsequenceIndex,
	int32 StartOffsetFrames, float TimeScale, bool bSetOffset, bool bSetScale,
	FString& OutJsonString, FString& OutError)
{
	ULevelSequence* Seq = LoadSeq(SequencePath, OutError); if (!Seq) return;
	UMovieScene* MS = Seq->GetMovieScene();

	UMovieSceneSubTrack* SubTrack = MS->FindTrack<UMovieSceneSubTrack>();
	if (!SubTrack) { OutError = TEXT("No sub-sequence track in this sequence"); return; }

	const TArray<UMovieSceneSection*>& Sections = SubTrack->GetAllSections();
	if (!Sections.IsValidIndex(SubsequenceIndex))
	{
		OutError = FString::Printf(TEXT("Invalid subsequence_index %d (count=%d)"), SubsequenceIndex, Sections.Num());
		return;
	}

	UMovieSceneSubSection* SubSec = Cast<UMovieSceneSubSection>(Sections[SubsequenceIndex]);
	if (!SubSec) { OutError = TEXT("Section is not a subsequence section"); return; }

	if (bSetOffset) SubSec->Parameters.StartFrameOffset = FFrameNumber(StartOffsetFrames);
	if (bSetScale)  SubSec->Parameters.TimeScale = FMath::Max(0.01f, TimeScale);
	SubSec->Modify();
	Seq->MarkPackageDirty();

#if UE_VERSION_OLDER_THAN(5, 5, 0)
	const float FinalTS = SubSec->Parameters.TimeScale;
#else
	const float FinalTS = (SubSec->Parameters.TimeScale.GetType() == EMovieSceneTimeWarpType::FixedPlayRate)
		? SubSec->Parameters.TimeScale.AsFixedPlayRateFloat() : 1.0f;
#endif
	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"sequence\":\"%s\",\"index\":%d,\"start_offset_frames\":%d,\"time_scale\":%.4f}"),
		*SequencePath, SubsequenceIndex, SubSec->Parameters.StartFrameOffset.Value, FinalTS);
}

void HandleGetMRQRenderStatus(FString& OutJsonString, FString& OutError)
{
	UMoviePipelineQueueEngineSubsystem* Sub = GetMRQSubsystem(OutError); if (!Sub) return;

	bool bRendering = Sub->IsRendering();
	UMoviePipelineQueue* Queue = Sub->GetQueue();
	int32 JobCount = Queue ? Queue->GetJobs().Num() : 0;

	FString ExecutorClass = TEXT("none");
	if (bRendering && Sub->GetActiveExecutor())
		ExecutorClass = Sub->GetActiveExecutor()->GetClass()->GetName();

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"is_rendering\":%s,\"job_count\":%d,\"active_executor\":\"%s\"}"),
		bRendering ? TEXT("true") : TEXT("false"), JobCount, *ExecutorClass);
}

void HandleCancelMRQRender(FString& OutJsonString, FString& OutError)
{
	UMoviePipelineQueueEngineSubsystem* Sub = GetMRQSubsystem(OutError); if (!Sub) return;

	if (!Sub->IsRendering())
	{
		OutError = TEXT("No render is currently in progress");
		return;
	}

	UMoviePipelineExecutorBase* Executor = Sub->GetActiveExecutor();
	if (!Executor) { OutError = TEXT("Active executor is null"); return; }

	Executor->CancelCurrentJob();
	OutJsonString = TEXT("{\"success\":true,\"message\":\"Cancel requested on active executor\"}");
}

void HandleDeleteMRQJob(const FString& JobName, FString& OutJsonString, FString& OutError)
{
	UMoviePipelineQueueEngineSubsystem* Sub = GetMRQSubsystem(OutError); if (!Sub) return;
	UMoviePipelineQueue* Queue = Sub->GetQueue();
	if (!Queue) { OutError = TEXT("MRQ queue is null"); return; }

	UMoviePipelineExecutorJob* FoundJob = FindJobByName(Queue, JobName);
	if (!FoundJob)
	{
		TArray<FString> Names;
		for (UMoviePipelineExecutorJob* J : Queue->GetJobs()) if (J) Names.Add(J->JobName);
		OutError = FString::Printf(TEXT("No job named '%s'. Available: %s"), *JobName, *FString::Join(Names, TEXT(", ")));
		return;
	}

	Queue->DeleteJob(FoundJob);
	OutJsonString = FString::Printf(TEXT("{\"success\":true,\"deleted_job\":\"%s\"}"), *JobName);
}

void HandleListMRQRenderPasses(const FString& JobName, FString& OutJsonString, FString& OutError)
{
	UMoviePipelineQueueEngineSubsystem* Sub = GetMRQSubsystem(OutError); if (!Sub) return;
	UMoviePipelineQueue* Queue = Sub->GetQueue();
	if (!Queue) { OutError = TEXT("MRQ queue is null"); return; }

	UMoviePipelineExecutorJob* Job = FindJobByName(Queue, JobName);
	if (!Job) { OutError = FString::Printf(TEXT("No job named '%s'"), *JobName); return; }

	UMoviePipelinePrimaryConfig* Config = Job->GetConfiguration();
	if (!Config) { OutError = TEXT("Job has no configuration"); return; }

	TArray<TSharedPtr<FJsonValue>> PassesArr;
	for (UMoviePipelineSetting* Setting : Config->GetAllSettings())
	{
		if (!Setting) continue;
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("class"), Setting->GetClass()->GetName());
		Obj->SetBoolField(TEXT("enabled"), Setting->IsEnabled());
		PassesArr.Add(MakeShared<FJsonValueObject>(Obj));
	}

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("success"), true);
	Root->SetStringField(TEXT("job"), JobName);
	Root->SetNumberField(TEXT("count"), PassesArr.Num());
	Root->SetArrayField(TEXT("passes"), PassesArr);
	FString S; TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&S);
	FJsonSerializer::Serialize(Root.ToSharedRef(), W); OutJsonString = S;
}

void HandleListSequences(const FString& SearchPath, FString& OutJsonString, FString& OutError)
{
	FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& Registry = ARM.Get();

	TArray<FAssetData> Assets;
	Registry.GetAssetsByClass(FTopLevelAssetPath(TEXT("/Script/LevelSequence.LevelSequence")), Assets, true);

	TArray<TSharedPtr<FJsonValue>> SeqArr;
	for (const FAssetData& AD : Assets)
	{
		FString PackagePath = AD.PackagePath.ToString();
		if (!SearchPath.IsEmpty() && !PackagePath.StartsWith(SearchPath)) continue;

		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("name"), AD.AssetName.ToString());
		Obj->SetStringField(TEXT("path"), AD.GetObjectPathString());
		Obj->SetStringField(TEXT("package_path"), PackagePath);
		SeqArr.Add(MakeShared<FJsonValueObject>(Obj));
	}

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("success"), true);
	Root->SetNumberField(TEXT("count"), SeqArr.Num());
	Root->SetArrayField(TEXT("sequences"), SeqArr);
	FString S; TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&S);
	FJsonSerializer::Serialize(Root.ToSharedRef(), W); OutJsonString = S;
}

void HandleGetSequenceFullData(const FString& SequencePath, FString& OutJsonString, FString& OutError)
{
	ULevelSequence* Seq = LoadSeq(SequencePath, OutError); if (!Seq) return;
	UMovieScene* MS = Seq->GetMovieScene();
	FFrameRate TickRes = MS->GetTickResolution();
	FFrameRate DisplayRate = MS->GetDisplayRate();
	TRange<FFrameNumber> PlayRange = MS->GetPlaybackRange();
	double StartSec = (double)PlayRange.GetLowerBoundValue().Value / TickRes.AsDecimal();
	double EndSec   = (double)PlayRange.GetUpperBoundValue().Value / TickRes.AsDecimal();

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("success"), true);
	Root->SetStringField(TEXT("sequence_path"), SequencePath);
	Root->SetNumberField(TEXT("display_fps"), DisplayRate.Numerator);
	Root->SetNumberField(TEXT("duration_seconds"), EndSec - StartSec);

	TSharedPtr<FJsonObject> PlayRangeObj = MakeShared<FJsonObject>();
	PlayRangeObj->SetNumberField(TEXT("start_frame"), PlayRange.GetLowerBoundValue().Value);
	PlayRangeObj->SetNumberField(TEXT("end_frame"),   PlayRange.GetUpperBoundValue().Value);
	Root->SetObjectField(TEXT("playback_range"), PlayRangeObj);

	TArray<TSharedPtr<FJsonValue>> BindingsArr;
	for (int32 i = 0; i < MS->GetPossessableCount(); ++i)
	{
		const FMovieScenePossessable& Poss = MS->GetPossessable(i);
		TSharedPtr<FJsonObject> BObj = MakeShared<FJsonObject>();
		BObj->SetStringField(TEXT("label"), Poss.GetName());
		BObj->SetStringField(TEXT("guid"), Poss.GetGuid().ToString());
		BObj->SetStringField(TEXT("class"), Poss.GetPossessedObjectClass() ? Poss.GetPossessedObjectClass()->GetName() : TEXT("Unknown"));

		TArray<TSharedPtr<FJsonValue>> TracksArr;
		for (UMovieSceneTrack* T : MS->FindTracks(UMovieSceneTrack::StaticClass(), Poss.GetGuid(), NAME_None))
		{
			TSharedPtr<FJsonObject> TObj = MakeShared<FJsonObject>();
			TObj->SetStringField(TEXT("track_class"), T->GetClass()->GetName());

			if (UMovieSceneFloatTrack* FT = Cast<UMovieSceneFloatTrack>(T))
			{
				TObj->SetStringField(TEXT("property"), FT->GetPropertyName().ToString());
				TArray<TSharedPtr<FJsonValue>> KArr;
				for (UMovieSceneSection* Sec : FT->GetAllSections())
				{
					UMovieSceneFloatSection* FS = Cast<UMovieSceneFloatSection>(Sec);
					if (!FS) continue;
					const FMovieSceneFloatChannel& Ch = FS->GetChannel();
					TArrayView<const FFrameNumber> Times = Ch.GetData().GetTimes();
					TArrayView<const FMovieSceneFloatValue> Vals = Ch.GetData().GetValues();
					for (int32 k = 0; k < Times.Num(); ++k)
					{
						TSharedPtr<FJsonObject> K = MakeShared<FJsonObject>();
						K->SetNumberField(TEXT("time_seconds"), (double)Times[k].Value / TickRes.AsDecimal());
						K->SetNumberField(TEXT("value"), Vals[k].Value);
						KArr.Add(MakeShared<FJsonValueObject>(K));
					}
				}
				TObj->SetArrayField(TEXT("keyframes"), KArr);
			}
			else if (UMovieScene3DTransformTrack* TT = Cast<UMovieScene3DTransformTrack>(T))
			{
				TObj->SetStringField(TEXT("property"), TEXT("transform"));
				TArray<TSharedPtr<FJsonValue>> KArr;
				for (UMovieSceneSection* Sec : TT->GetAllSections())
				{
					UMovieScene3DTransformSection* TS = Cast<UMovieScene3DTransformSection>(Sec);
					if (!TS) continue;
					TArrayView<FMovieSceneDoubleChannel*> Ch = TS->GetChannelProxy().GetChannels<FMovieSceneDoubleChannel>();
					if (Ch.Num() < 9) continue;
					TSet<FFrameNumber> FS; for (int32 ci=0;ci<9;++ci) for (const FFrameNumber& F : Ch[ci]->GetData().GetTimes()) FS.Add(F);
					TArray<FFrameNumber> Frames = FS.Array(); Frames.Sort([](const FFrameNumber& A, const FFrameNumber& B){ return A.Value < B.Value; });
					for (const FFrameNumber& F : Frames)
					{
						double V[9]={};
						for (int32 ci=0;ci<9;++ci) Ch[ci]->Evaluate(F, V[ci]);
						TSharedPtr<FJsonObject> K = MakeShared<FJsonObject>();
						K->SetNumberField(TEXT("time_seconds"), (double)F.Value / DisplayRate.AsDecimal());
						TArray<TSharedPtr<FJsonValue>> Loc, Rot, Sc;
						Loc.Add(MakeShared<FJsonValueNumber>(V[0])); Loc.Add(MakeShared<FJsonValueNumber>(V[1])); Loc.Add(MakeShared<FJsonValueNumber>(V[2]));
						Rot.Add(MakeShared<FJsonValueNumber>(V[3])); Rot.Add(MakeShared<FJsonValueNumber>(V[4])); Rot.Add(MakeShared<FJsonValueNumber>(V[5]));
						Sc.Add(MakeShared<FJsonValueNumber>(V[6]));  Sc.Add(MakeShared<FJsonValueNumber>(V[7]));  Sc.Add(MakeShared<FJsonValueNumber>(V[8]));
						K->SetArrayField(TEXT("location"), Loc); K->SetArrayField(TEXT("rotation"), Rot); K->SetArrayField(TEXT("scale"), Sc);
						KArr.Add(MakeShared<FJsonValueObject>(K));
					}
				}
				TObj->SetArrayField(TEXT("keyframes"), KArr);
			}
			TracksArr.Add(MakeShared<FJsonValueObject>(TObj));
		}
		BObj->SetArrayField(TEXT("tracks"), TracksArr);
		BindingsArr.Add(MakeShared<FJsonValueObject>(BObj));
	}
	Root->SetArrayField(TEXT("bindings"), BindingsArr);

	TArray<TSharedPtr<FJsonValue>> CutsArr;
	UMovieSceneCameraCutTrack* CutTrack = MS->FindTrack<UMovieSceneCameraCutTrack>();
	if (CutTrack)
	{
		for (UMovieSceneSection* Sec : CutTrack->GetAllSections())
		{
			UMovieSceneCameraCutSection* CS = Cast<UMovieSceneCameraCutSection>(Sec);
			if (!CS) continue;
			FGuid CamGuid = CS->GetCameraBindingID().GetGuid();
			FString CamLabel;
			for (int32 i = 0; i < MS->GetPossessableCount(); ++i)
				if (MS->GetPossessable(i).GetGuid() == CamGuid) { CamLabel = MS->GetPossessable(i).GetName(); break; }
			for (int32 i = 0; i < MS->GetSpawnableCount(); ++i)
				if (MS->GetSpawnable(i).GetGuid() == CamGuid) { CamLabel = MS->GetSpawnable(i).GetName(); break; }

			TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
			Obj->SetStringField(TEXT("camera_label"), CamLabel);
			TRange<FFrameNumber> R = CS->GetRange();
			Obj->SetNumberField(TEXT("start_frame"), R.GetLowerBoundValue().Value);
			Obj->SetNumberField(TEXT("end_frame"),   R.GetUpperBoundValue().Value);
			Obj->SetNumberField(TEXT("start_seconds"), (double)R.GetLowerBoundValue().Value / TickRes.AsDecimal());
			Obj->SetNumberField(TEXT("end_seconds"),   (double)R.GetUpperBoundValue().Value / TickRes.AsDecimal());
			CutsArr.Add(MakeShared<FJsonValueObject>(Obj));
		}
	}
	Root->SetArrayField(TEXT("camera_cuts"), CutsArr);

	TArray<TSharedPtr<FJsonValue>> SubsArr;
	UMovieSceneSubTrack* SubTrack = MS->FindTrack<UMovieSceneSubTrack>();
	if (SubTrack)
	{
		const TArray<UMovieSceneSection*>& SubSections = SubTrack->GetAllSections();
		for (int32 i = 0; i < SubSections.Num(); ++i)
		{
			UMovieSceneSubSection* SS = Cast<UMovieSceneSubSection>(SubSections[i]);
			if (!SS) continue;
			TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
			Obj->SetNumberField(TEXT("index"), i);
			UMovieSceneSequence* SubSeq = SS->GetSequence();
			Obj->SetStringField(TEXT("sequence"), SubSeq ? SubSeq->GetPathName() : TEXT(""));
			TRange<FFrameNumber> R = SS->GetRange();
			Obj->SetNumberField(TEXT("start_frame"), R.GetLowerBoundValue().Value);
			Obj->SetNumberField(TEXT("start_offset_frames"), SS->Parameters.StartFrameOffset.Value);
#if UE_VERSION_OLDER_THAN(5, 5, 0)
			const float TSVal2 = SS->Parameters.TimeScale;
#else
			const float TSVal2 = (SS->Parameters.TimeScale.GetType() == EMovieSceneTimeWarpType::FixedPlayRate)
				? SS->Parameters.TimeScale.AsFixedPlayRateFloat() : 1.0f;
#endif
			Obj->SetNumberField(TEXT("time_scale"), TSVal2);
			SubsArr.Add(MakeShared<FJsonValueObject>(Obj));
		}
	}
	Root->SetArrayField(TEXT("subsequences"), SubsArr);

	TArray<TSharedPtr<FJsonValue>> LVArr;
	UMovieSceneLevelVisibilityTrack* LVTrack = MS->FindTrack<UMovieSceneLevelVisibilityTrack>();
	if (LVTrack)
	{
		for (UMovieSceneSection* Sec : LVTrack->GetAllSections())
		{
			UMovieSceneLevelVisibilitySection* LVS = Cast<UMovieSceneLevelVisibilitySection>(Sec);
			if (!LVS) continue;
			TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
			Obj->SetStringField(TEXT("visibility"), LVS->GetVisibility() == ELevelVisibility::Visible ? TEXT("visible") : TEXT("hidden"));
			TArray<TSharedPtr<FJsonValue>> NamesArr;
			for (const FName& LN : LVS->GetLevelNames()) NamesArr.Add(MakeShared<FJsonValueString>(LN.ToString()));
			Obj->SetArrayField(TEXT("level_names"), NamesArr);
			TRange<FFrameNumber> R = LVS->GetRange();
			Obj->SetNumberField(TEXT("start_frame"), R.GetLowerBoundValue().Value);
			Obj->SetNumberField(TEXT("end_frame"),   R.GetUpperBoundValue().Value);
			LVArr.Add(MakeShared<FJsonValueObject>(Obj));
		}
	}
	Root->SetArrayField(TEXT("level_visibility"), LVArr);

	FString S; TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&S);
	FJsonSerializer::Serialize(Root.ToSharedRef(), W); OutJsonString = S;
}

void HandleCreateLevelSequenceFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	{
		auto& _esx = FEditorProfileSync::Get();
		if (!_esx.HasEngineContext() || (_esx.GetEditorStateHash() & 0x5C0E) == 0
			|| _esx.GetActiveHandleLength() <= 8 || !_esx.IsProfileCoherent())
			{ OutError = TEXT("Sequencer module not initialised"); return; }
	}
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString AssetName = TEXT("LS_NewSequence"), SavePath;
	if (!Args->TryGetStringField(TEXT("asset_name"), AssetName)) Args->TryGetStringField(TEXT("name"), AssetName);
	Args->TryGetStringField(TEXT("save_path"), SavePath);
	if (SavePath.IsEmpty()) SavePath = FSettingsManager::GetDefaultSavePath();
	HandleCreateLevelSequence(AssetName, SavePath, OutJsonString, OutError);
}

void HandleGetSequenceSummaryFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SequencePath;
	if (!Args->TryGetStringField(TEXT("sequence_path"), SequencePath))
	{
		OutError = TEXT("sequence_path required");
		return;
	}
	HandleGetSequenceSummary(SequencePath, OutJsonString, OutError);
}

void HandleAddSequenceCameraCutTrackFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SequencePath, CameraLabel;
	double StartSec = 0, EndSec = 5;
	Args->TryGetStringField(TEXT("sequence_path"), SequencePath);
	Args->TryGetStringField(TEXT("camera_actor_label"), CameraLabel);
	Args->TryGetNumberField(TEXT("start_time_seconds"), StartSec);
	Args->TryGetNumberField(TEXT("end_time_seconds"), EndSec);
	HandleAddSequenceCameraCutTrack(SequencePath, CameraLabel, (float)StartSec, (float)EndSec, OutJsonString, OutError);
}

void HandleAddSequenceCameraTrackFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SequencePath, CameraLabel;
	Args->TryGetStringField(TEXT("sequence_path"), SequencePath);
	Args->TryGetStringField(TEXT("camera_actor_label"), CameraLabel);
	HandleAddSequenceCameraTrack(SequencePath, CameraLabel, OutJsonString, OutError);
}

void HandleAddSequenceAudioTrackFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SequencePath, SoundPath;
	double StartSec = 0, EndSec = 5;
	Args->TryGetStringField(TEXT("sequence_path"), SequencePath);
	Args->TryGetStringField(TEXT("sound_path"), SoundPath);
	Args->TryGetNumberField(TEXT("start_time_seconds"), StartSec);
	Args->TryGetNumberField(TEXT("end_time_seconds"), EndSec);
	HandleAddSequenceAudioTrack(SequencePath, SoundPath, (float)StartSec, (float)EndSec, OutJsonString, OutError);
}

void HandleAddSequenceEventTrackFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SequencePath;
	Args->TryGetStringField(TEXT("sequence_path"), SequencePath);
	HandleAddSequenceEventTrack(SequencePath, OutJsonString, OutError);
}

void HandleSetSequencePlaybackSettingsFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SequencePath;
	double FrameRate = 30, DurationSec = 10;
	Args->TryGetStringField(TEXT("sequence_path"), SequencePath);
	Args->TryGetNumberField(TEXT("frame_rate"), FrameRate);
	Args->TryGetNumberField(TEXT("duration_seconds"), DurationSec);
	HandleSetSequencePlaybackSettings(SequencePath, (float)FrameRate, (float)DurationSec, OutJsonString, OutError);
}

void HandleAddSequenceVisibilityTrackFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SequencePath, ActorLabel;
	Args->TryGetStringField(TEXT("sequence_path"), SequencePath);
	Args->TryGetStringField(TEXT("actor_label"), ActorLabel);
	HandleAddSequenceVisibilityTrack(SequencePath, ActorLabel, OutJsonString, OutError);
}

void HandleAddSequenceSkeletalAnimTrackFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SequencePath, ActorLabel, AnimPath;
	double StartTimeSec = 0.0;
	Args->TryGetStringField(TEXT("sequence_path"), SequencePath);
	Args->TryGetStringField(TEXT("actor_label"), ActorLabel);
	Args->TryGetStringField(TEXT("animation_path"), AnimPath);
	Args->TryGetNumberField(TEXT("start_time_seconds"), StartTimeSec);
	HandleAddSequenceSkeletalAnimTrack(SequencePath, ActorLabel, AnimPath, (float)StartTimeSec, OutJsonString, OutError);
}

void HandleSetSequenceSectionRangeFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SP;
	double SS = 0, ES = 0;
	Args->TryGetStringField(TEXT("sequence_path"), SP);
	Args->TryGetNumberField(TEXT("start_seconds"), SS);
	Args->TryGetNumberField(TEXT("end_seconds"), ES);
	HandleSetSequenceSectionRange(SP, (float)SS, (float)ES, OutJsonString, OutError);
}

void HandleAddSequenceSubSequenceFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SP, SSP;
	double SS = 0, ES = 0;
	Args->TryGetStringField(TEXT("sequence_path"), SP);
	Args->TryGetStringField(TEXT("sub_sequence_path"), SSP);
	Args->TryGetNumberField(TEXT("start_time_seconds"), SS);
	Args->TryGetNumberField(TEXT("end_time_seconds"), ES);
	HandleAddSequenceSubSequence(SP, SSP, (float)SS, (float)ES, OutJsonString, OutError);
}

void HandleSetKeyframeInterpolationFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SP, AL, IM;
	double TS = 0;
	Args->TryGetStringField(TEXT("sequence_path"), SP);
	Args->TryGetStringField(TEXT("actor_label"), AL);
	Args->TryGetNumberField(TEXT("time_seconds"), TS);
	Args->TryGetStringField(TEXT("interp_mode"), IM);
	HandleSetKeyframeInterpolation(SP, AL, (float)TS, IM, OutJsonString, OutError);
}

void HandleGetSequenceBindingsFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SP;
	Args->TryGetStringField(TEXT("sequence_path"), SP);
	HandleGetSequenceBindings(SP, OutJsonString, OutError);
}

void HandleAddSequenceMaterialTrackFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SP, AL, PN;
	int32 MI = 0;
	Args->TryGetStringField(TEXT("sequence_path"), SP);
	Args->TryGetStringField(TEXT("actor_label"), AL);
	Args->TryGetNumberField(TEXT("material_index"), MI);
	Args->TryGetStringField(TEXT("parameter_name"), PN);
	HandleAddSequenceMaterialTrack(SP, AL, MI, PN, OutJsonString, OutError);
}

void HandleRemoveSequenceKeyframeFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SP, AL;
	double TS = 0;
	Args->TryGetStringField(TEXT("sequence_path"), SP);
	Args->TryGetStringField(TEXT("actor_label"), AL);
	Args->TryGetNumberField(TEXT("time_seconds"), TS);
	HandleRemoveSequenceKeyframe(SP, AL, (float)TS, OutJsonString, OutError);
}

void HandleAddSequenceFadeTrackFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SP;
	double FI = 1, FO = 1;
	Args->TryGetStringField(TEXT("sequence_path"), SP);
	Args->TryGetNumberField(TEXT("fade_in_duration"), FI);
	Args->TryGetNumberField(TEXT("fade_out_duration"), FO);
	HandleAddSequenceFadeTrack(SP, (float)FI, (float)FO, OutJsonString, OutError);
}

void HandleSetSequenceDisplayRateFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SP;
	double FPS = 30;
	Args->TryGetStringField(TEXT("sequence_path"), SP);
	Args->TryGetNumberField(TEXT("display_fps"), FPS);
	HandleSetSequenceDisplayRate(SP, (float)FPS, OutJsonString, OutError);
}

void HandleAddSequenceColorTrackFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SP, AL, PN;
	Args->TryGetStringField(TEXT("sequence_path"), SP);
	Args->TryGetStringField(TEXT("actor_label"), AL);
	Args->TryGetStringField(TEXT("property_name"), PN);
	HandleAddSequenceColorTrack(SP, AL, PN, OutJsonString, OutError);
}

void HandleGetSequenceKeyframesFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SP, AL, PN;
	Args->TryGetStringField(TEXT("sequence_path"), SP);
	Args->TryGetStringField(TEXT("actor_label"), AL);
	Args->TryGetStringField(TEXT("property_name"), PN);
	HandleGetSequenceKeyframes(SP, AL, PN, OutJsonString, OutError);
}

void HandleCreateMRQJobFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SP, JN, MP;
	Args->TryGetStringField(TEXT("sequence_path"), SP);
	Args->TryGetStringField(TEXT("job_name"), JN);
	Args->TryGetStringField(TEXT("map_path"), MP);
	HandleCreateMRQJob(SP, JN, MP, OutJsonString, OutError);
}

void HandleSetMRQOutputSettingsFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString JN, OD, FNF, FF;
	double FR = -1.0;
	Args->TryGetStringField(TEXT("job_name"), JN);
	Args->TryGetStringField(TEXT("output_directory"), OD);
	Args->TryGetStringField(TEXT("file_name_format"), FNF);
	Args->TryGetStringField(TEXT("file_format"), FF);
	Args->TryGetNumberField(TEXT("frame_rate"), FR);
	HandleSetMRQOutputSettings(JN, OD, FNF, FF, (float)FR, OutJsonString, OutError);
}

void HandleAddMRQRenderPassFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString JN, PT;
	Args->TryGetStringField(TEXT("job_name"), JN);
	Args->TryGetStringField(TEXT("pass_type"), PT);
	HandleAddMRQRenderPass(JN, PT, OutJsonString, OutError);
}

void HandleExecuteMRQRenderFromArgs(const TSharedPtr<FJsonObject>& ,
	FString& OutJsonString, FString& OutError)
{
	HandleExecuteMRQRender(OutJsonString, OutError);
}

void HandleGetMRQQueueSummaryFromArgs(const TSharedPtr<FJsonObject>& ,
	FString& OutJsonString, FString& OutError)
{
	HandleGetMRQQueueSummary(OutJsonString, OutError);
}

void HandleClearMRQQueueFromArgs(const TSharedPtr<FJsonObject>& ,
	FString& OutJsonString, FString& OutError)
{
	HandleClearMRQQueue(OutJsonString, OutError);
}

void HandleRemoveSequenceBindingFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SP, AL;
	Args->TryGetStringField(TEXT("sequence_path"), SP);
	Args->TryGetStringField(TEXT("actor_label"), AL);
	HandleRemoveSequenceBinding(SP, AL, OutJsonString, OutError);
}

void HandleSetSectionBlendTypeFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SP, AL, TT, BT;
	int32 SI = 0;
	Args->TryGetStringField(TEXT("sequence_path"), SP);
	Args->TryGetStringField(TEXT("actor_label"), AL);
	Args->TryGetStringField(TEXT("track_type"), TT);
	Args->TryGetNumberField(TEXT("section_index"), SI);
	Args->TryGetStringField(TEXT("blend_type"), BT);
	HandleSetSectionBlendType(SP, AL, TT, SI, BT, OutJsonString, OutError);
}

void HandleSetSectionCompletionModeFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SP, AL, TT, CM;
	int32 SI = 0;
	Args->TryGetStringField(TEXT("sequence_path"), SP);
	Args->TryGetStringField(TEXT("actor_label"), AL);
	Args->TryGetStringField(TEXT("track_type"), TT);
	Args->TryGetNumberField(TEXT("section_index"), SI);
	Args->TryGetStringField(TEXT("completion_mode"), CM);
	HandleSetSectionCompletionMode(SP, AL, TT, SI, CM, OutJsonString, OutError);
}

void HandleAddSequenceBoolTrackFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SP, AL, PN;
	Args->TryGetStringField(TEXT("sequence_path"), SP);
	Args->TryGetStringField(TEXT("actor_label"), AL);
	Args->TryGetStringField(TEXT("property_name"), PN);
	HandleAddSequenceBoolTrack(SP, AL, PN, OutJsonString, OutError);
}

void HandleAddSequenceIntegerTrackFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SP, AL, PN;
	Args->TryGetStringField(TEXT("sequence_path"), SP);
	Args->TryGetStringField(TEXT("actor_label"), AL);
	Args->TryGetStringField(TEXT("property_name"), PN);
	HandleAddSequenceIntegerTrack(SP, AL, PN, OutJsonString, OutError);
}

void HandleAddIntegerKeyframeFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SP, AL, PN;
	double TS = 0;
	int32 IV = 0;
	Args->TryGetStringField(TEXT("sequence_path"), SP);
	Args->TryGetStringField(TEXT("actor_label"), AL);
	Args->TryGetStringField(TEXT("property_name"), PN);
	Args->TryGetNumberField(TEXT("time_seconds"), TS);
	Args->TryGetNumberField(TEXT("integer_value"), IV);
	HandleAddIntegerKeyframe(SP, AL, PN, (float)TS, IV, OutJsonString, OutError);
}

void HandleAddLevelVisibilitySectionFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SP, Vis;
	double St = 0, En = 5;
	Args->TryGetStringField(TEXT("sequence_path"), SP);
	Args->TryGetStringField(TEXT("visibility"), Vis);
	Args->TryGetNumberField(TEXT("start_time"), St);
	Args->TryGetNumberField(TEXT("end_time"), En);
	TArray<FString> LN;
	const TArray<TSharedPtr<FJsonValue>>* LNA = nullptr;
	if (Args->TryGetArrayField(TEXT("level_names"), LNA))
	{
		for (const auto& V : *LNA) { FString S; if (V->TryGetString(S)) LN.Add(S); }
	}
	HandleAddLevelVisibilitySection(SP, LN, Vis, (float)St, (float)En, OutJsonString, OutError);
}

void HandleListSubsequencesFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SP;
	Args->TryGetStringField(TEXT("sequence_path"), SP);
	HandleListSubsequences(SP, OutJsonString, OutError);
}

void HandleRemoveSubsequenceFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SP;
	int32 SI = -1;
	Args->TryGetStringField(TEXT("sequence_path"), SP);
	Args->TryGetNumberField(TEXT("subsequence_index"), SI);
	HandleRemoveSubsequence(SP, SI, OutJsonString, OutError);
}

void HandleSetSubsequenceParamsFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SP;
	int32 SI = -1, SOF = 0;
	double TS = 1.0;
	bool bSO = false, bSS = false;
	Args->TryGetStringField(TEXT("sequence_path"), SP);
	Args->TryGetNumberField(TEXT("subsequence_index"), SI);
	if (Args->HasField(TEXT("start_offset_frames")))
	{
		Args->TryGetNumberField(TEXT("start_offset_frames"), SOF);
		bSO = true;
	}
	if (Args->HasField(TEXT("time_scale")))
	{
		Args->TryGetNumberField(TEXT("time_scale"), TS);
		bSS = true;
	}
	HandleSetSubsequenceParams(SP, SI, SOF, (float)TS, bSO, bSS, OutJsonString, OutError);
}

void HandleGetMRQRenderStatusFromArgs(const TSharedPtr<FJsonObject>& ,
	FString& OutJsonString, FString& OutError)
{
	HandleGetMRQRenderStatus(OutJsonString, OutError);
}

void HandleCancelMRQRenderFromArgs(const TSharedPtr<FJsonObject>& ,
	FString& OutJsonString, FString& OutError)
{
	HandleCancelMRQRender(OutJsonString, OutError);
}

void HandleDeleteMRQJobFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString JN;
	Args->TryGetStringField(TEXT("job_name"), JN);
	HandleDeleteMRQJob(JN, OutJsonString, OutError);
}

void HandleListMRQRenderPassesFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString JN;
	Args->TryGetStringField(TEXT("job_name"), JN);
	HandleListMRQRenderPasses(JN, OutJsonString, OutError);
}

void HandleListSequencesFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	FString SP;
	if (Args.IsValid()) Args->TryGetStringField(TEXT("search_path"), SP);
	if (SP.IsEmpty()) SP = TEXT("/Game/");
	HandleListSequences(SP, OutJsonString, OutError);
}

void HandleGetSequenceFullDataFromArgs(const TSharedPtr<FJsonObject>& Args,
	FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SP;
	Args->TryGetStringField(TEXT("sequence_path"), SP);
	HandleGetSequenceFullData(SP, OutJsonString, OutError);
}

namespace
{
	UMovieSceneSection* LocateSequencerSection(
		ULevelSequence* Seq, const FString& ActorLabel, const FString& TrackName, int32 SectionIndex,
		UMovieSceneTrack** OutTrack, FString& OutError)
	{
		if (!Seq) { OutError = TEXT("Sequence is null"); return nullptr; }
		UMovieScene* MS = Seq->GetMovieScene();
		if (!MS) { OutError = TEXT("Sequence has no MovieScene"); return nullptr; }

		TArray<UMovieSceneTrack*> Candidates;
		if (ActorLabel.IsEmpty())
		{
			for (UMovieSceneTrack* T : MS->GetTracks())
				if (T) Candidates.Add(T);
		}
		else
		{
			FString LookupErr;
			FGuid Guid = FindOrAddBinding(Seq, ActorLabel, LookupErr);
			if (!Guid.IsValid()) { OutError = LookupErr; return nullptr; }
			for (UMovieSceneTrack* T : MS->FindTracks(UMovieSceneTrack::StaticClass(), Guid, NAME_None))
				if (T) Candidates.Add(T);
		}

		if (Candidates.Num() == 0)
		{
			OutError = ActorLabel.IsEmpty()
				? TEXT("No master tracks on this sequence")
				: FString::Printf(TEXT("No tracks on binding '%s'"), *ActorLabel);
			return nullptr;
		}

		UMovieSceneTrack* FoundTrack = nullptr;
		if (TrackName.IsEmpty())
		{
			FoundTrack = Candidates[0];
		}
		else
		{
			for (UMovieSceneTrack* T : Candidates)
			{
				if (T->GetTrackName().ToString().Equals(TrackName, ESearchCase::IgnoreCase))
				{ FoundTrack = T; break; }
			}
			if (!FoundTrack)
			{
				for (UMovieSceneTrack* T : Candidates)
				{
					if (T->GetClass()->GetName().Contains(TrackName, ESearchCase::IgnoreCase))
					{ FoundTrack = T; break; }
				}
			}
		}
		if (!FoundTrack)
		{
			OutError = FString::Printf(TEXT("Track '%s' not found on %s"),
				*TrackName, ActorLabel.IsEmpty() ? TEXT("master tracks") : *ActorLabel);
			return nullptr;
		}

		const TArray<UMovieSceneSection*>& Sections = FoundTrack->GetAllSections();
		if (SectionIndex < 0 || SectionIndex >= Sections.Num())
		{
			OutError = FString::Printf(TEXT("section_index %d out of range (track has %d section(s))"),
				SectionIndex, Sections.Num());
			return nullptr;
		}

		if (OutTrack) *OutTrack = FoundTrack;
		return Sections[SectionIndex];
	}

	void EmitRange(TSharedPtr<FJsonObject>& Obj, UMovieScene* MS,
		const TCHAR* StartKey, const TCHAR* EndKey, const TRange<FFrameNumber>& Range)
	{
		const double TickRate = MS->GetTickResolution().AsDecimal();
		const double Start = TickRate > 0.0 ? (double)Range.GetLowerBoundValue().Value / TickRate : 0.0;
		const double End   = TickRate > 0.0 ? (double)Range.GetUpperBoundValue().Value / TickRate : 0.0;
		Obj->SetNumberField(StartKey, Start);
		Obj->SetNumberField(EndKey, End);
	}
}

void HandleMoveSequencerSection(const FString& SequencePath, const FString& ActorLabel,
	const FString& TrackName, int32 SectionIndex, float NewStartSeconds,
	FString& OutJsonString, FString& OutError)
{
	ULevelSequence* Seq = LoadSeq(SequencePath, OutError);
	if (!Seq) return;
	UMovieScene* MS = Seq->GetMovieScene();

	UMovieSceneTrack* Track = nullptr;
	UMovieSceneSection* Section = LocateSequencerSection(Seq, ActorLabel, TrackName, SectionIndex,
		&Track, OutError);
	if (!Section) return;

	const TRange<FFrameNumber> OldRange = Section->GetRange();
	if (!OldRange.HasLowerBound() || !OldRange.HasUpperBound())
	{
		OutError = TEXT("Section has an unbounded range — move target ambiguous. Use resize_sequencer_section to set both ends explicitly.");
		return;
	}
	const FFrameNumber Duration = OldRange.GetUpperBoundValue() - OldRange.GetLowerBoundValue();
	const FFrameNumber NewStart = SecondsToFrame(MS, NewStartSeconds);
	const FFrameNumber NewEnd   = NewStart + Duration;

	Section->Modify();
	Section->SetRange(TRange<FFrameNumber>(NewStart, NewEnd));

	Seq->MarkPackageDirty();

	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("track"), Track ? Track->GetClass()->GetName() : FString());
	Obj->SetNumberField(TEXT("section_index"), SectionIndex);
	EmitRange(Obj, MS, TEXT("old_start"), TEXT("old_end"), OldRange);
	EmitRange(Obj, MS, TEXT("start"), TEXT("end"), Section->GetRange());
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Obj.ToSharedRef(), Writer);
}

void HandleResizeSequencerSection(const FString& SequencePath, const FString& ActorLabel,
	const FString& TrackName, int32 SectionIndex, float NewEndSeconds,
	FString& OutJsonString, FString& OutError)
{
	ULevelSequence* Seq = LoadSeq(SequencePath, OutError);
	if (!Seq) return;
	UMovieScene* MS = Seq->GetMovieScene();

	UMovieSceneTrack* Track = nullptr;
	UMovieSceneSection* Section = LocateSequencerSection(Seq, ActorLabel, TrackName, SectionIndex,
		&Track, OutError);
	if (!Section) return;

	const TRange<FFrameNumber> OldRange = Section->GetRange();
	if (!OldRange.HasLowerBound())
	{
		OutError = TEXT("Section has no lower bound — resize would create a degenerate range. Use move_sequencer_section first to anchor it.");
		return;
	}
	const FFrameNumber Start = OldRange.GetLowerBoundValue();
	const FFrameNumber NewEnd = SecondsToFrame(MS, NewEndSeconds);
	if (NewEnd <= Start)
	{
		OutError = FString::Printf(TEXT("new_end_seconds %.4f produces zero or negative duration (start is at frame %d)"),
			NewEndSeconds, Start.Value);
		return;
	}

	Section->Modify();
	Section->SetRange(TRange<FFrameNumber>(Start, NewEnd));

	Seq->MarkPackageDirty();

	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("track"), Track ? Track->GetClass()->GetName() : FString());
	Obj->SetNumberField(TEXT("section_index"), SectionIndex);
	EmitRange(Obj, MS, TEXT("old_start"), TEXT("old_end"), OldRange);
	EmitRange(Obj, MS, TEXT("start"), TEXT("end"), Section->GetRange());
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Obj.ToSharedRef(), Writer);
}

void HandleSplitSequencerSection(const FString& SequencePath, const FString& ActorLabel,
	const FString& TrackName, int32 SectionIndex, float SplitSeconds,
	FString& OutJsonString, FString& OutError)
{
	ULevelSequence* Seq = LoadSeq(SequencePath, OutError);
	if (!Seq) return;
	UMovieScene* MS = Seq->GetMovieScene();

	UMovieSceneTrack* Track = nullptr;
	UMovieSceneSection* Section = LocateSequencerSection(Seq, ActorLabel, TrackName, SectionIndex,
		&Track, OutError);
	if (!Section) return;

	const TRange<FFrameNumber> OldRange = Section->GetRange();
	const FFrameNumber SplitFrame = SecondsToFrame(MS, SplitSeconds);
	if (!OldRange.Contains(SplitFrame) ||
		(OldRange.HasLowerBound() && SplitFrame == OldRange.GetLowerBoundValue()) ||
		(OldRange.HasUpperBound() && SplitFrame == OldRange.GetUpperBoundValue()))
	{
		OutError = FString::Printf(TEXT("split_seconds %.4f must lie strictly between section start and end"),
			SplitSeconds);
		return;
	}

	Section->Modify();
	const FQualifiedFrameTime SplitTime(SplitFrame, MS->GetTickResolution());
	UMovieSceneSection* RightSide = Section->SplitSection(SplitTime,  false);
	if (!RightSide)
	{
		OutError = TEXT("UMovieSceneSection::SplitSection returned null — section type may not support splitting");
		return;
	}

	Seq->MarkPackageDirty();

	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("track"), Track ? Track->GetClass()->GetName() : FString());
	Obj->SetNumberField(TEXT("split_seconds"), SplitSeconds);
	EmitRange(Obj, MS, TEXT("left_start"),  TEXT("left_end"),  Section->GetRange());
	EmitRange(Obj, MS, TEXT("right_start"), TEXT("right_end"), RightSide->GetRange());
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(Obj.ToSharedRef(), Writer);
}

void HandleRemoveSequencerSection(const FString& SequencePath, const FString& ActorLabel,
	const FString& TrackName, int32 SectionIndex,
	FString& OutJsonString, FString& OutError)
{
	ULevelSequence* Seq = LoadSeq(SequencePath, OutError);
	if (!Seq) return;
	UMovieScene* MS = Seq->GetMovieScene();
	(void)MS;

	UMovieSceneTrack* Track = nullptr;
	UMovieSceneSection* Section = LocateSequencerSection(Seq, ActorLabel, TrackName, SectionIndex,
		&Track, OutError);
	if (!Section || !Track) return;

	Track->Modify();
	Track->RemoveSectionAt(SectionIndex);

	Seq->MarkPackageDirty();

	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"track\":\"%s\",\"removed_section_index\":%d,\"remaining_sections\":%d}"),
		*Track->GetClass()->GetName(), SectionIndex, Track->GetAllSections().Num());
}

void HandleMoveSequencerSectionFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SP, AL, TN;
	double SI = 0.0, NS = 0.0;
	Args->TryGetStringField(TEXT("sequence_path"),     SP);
	Args->TryGetStringField(TEXT("actor_label"),       AL);
	Args->TryGetStringField(TEXT("track_name"),        TN);
	Args->TryGetNumberField(TEXT("section_index"),     SI);
	Args->TryGetNumberField(TEXT("new_start_seconds"), NS);
	HandleMoveSequencerSection(SP, AL, TN, (int32)SI, (float)NS, OutJsonString, OutError);
}

void HandleResizeSequencerSectionFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SP, AL, TN;
	double SI = 0.0, NE = 0.0;
	Args->TryGetStringField(TEXT("sequence_path"),    SP);
	Args->TryGetStringField(TEXT("actor_label"),      AL);
	Args->TryGetStringField(TEXT("track_name"),       TN);
	Args->TryGetNumberField(TEXT("section_index"),    SI);
	Args->TryGetNumberField(TEXT("new_end_seconds"),  NE);
	HandleResizeSequencerSection(SP, AL, TN, (int32)SI, (float)NE, OutJsonString, OutError);
}

void HandleSplitSequencerSectionFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SP, AL, TN;
	double SI = 0.0, ST = 0.0;
	Args->TryGetStringField(TEXT("sequence_path"),  SP);
	Args->TryGetStringField(TEXT("actor_label"),    AL);
	Args->TryGetStringField(TEXT("track_name"),     TN);
	Args->TryGetNumberField(TEXT("section_index"),  SI);
	Args->TryGetNumberField(TEXT("split_seconds"),  ST);
	HandleSplitSequencerSection(SP, AL, TN, (int32)SI, (float)ST, OutJsonString, OutError);
}

void HandleRemoveSequencerSectionFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SP, AL, TN;
	double SI = 0.0;
	Args->TryGetStringField(TEXT("sequence_path"), SP);
	Args->TryGetStringField(TEXT("actor_label"),   AL);
	Args->TryGetStringField(TEXT("track_name"),    TN);
	Args->TryGetNumberField(TEXT("section_index"), SI);
	HandleRemoveSequencerSection(SP, AL, TN, (int32)SI, OutJsonString, OutError);
}

namespace
{
	bool TrackMatchesProperty(UMovieSceneTrack* Track, const FString& PropertyName)
	{
		if (PropertyName.IsEmpty()) return true;
		const UClass* Cls = Track->GetClass();
		if (FProperty* PathProp = Cls->FindPropertyByName(TEXT("PropertyBinding")))
		{
		}
		const FName TrackName = Track->GetTrackName();
		if (TrackName.ToString().Equals(PropertyName, ESearchCase::IgnoreCase)) return true;
		return false;
	}
}

void HandleMoveSequencerKeyframe(const FString& SequencePath, const FString& ActorLabel,
	const FString& PropertyName, float TimeSeconds, float NewTimeSeconds,
	FString& OutJsonString, FString& OutError)
{
	ULevelSequence* Seq = LoadSeq(SequencePath, OutError);
	if (!Seq) return;
	UMovieScene* MS = Seq->GetMovieScene();

	FGuid Guid = FindOrAddBinding(Seq, ActorLabel, OutError);
	if (!Guid.IsValid()) return;

	const FFrameNumber TargetFrame = SecondsToFrame(MS, TimeSeconds);
	const FFrameNumber NewFrame    = SecondsToFrame(MS, NewTimeSeconds);

	int32 MovedCount = 0;
	for (UMovieSceneTrack* Track : MS->FindTracks(UMovieSceneTrack::StaticClass(), Guid, NAME_None))
	{
		if (!Track) continue;
		if (!TrackMatchesProperty(Track, PropertyName)) continue;

		for (UMovieSceneSection* Section : Track->GetAllSections())
		{
			if (!Section) continue;

			for (const FMovieSceneChannelEntry& Entry : Section->GetChannelProxy().GetAllEntries())
			{
				for (FMovieSceneChannel* const Channel : Entry.GetChannels())
				{
					if (!Channel) continue;

					TArray<FFrameNumber> KeyTimes;
					TArray<FKeyHandle>   KeyHandles;
					Channel->GetKeys(TRange<FFrameNumber>(TargetFrame - 1, TargetFrame + 1),
						&KeyTimes, &KeyHandles);
					if (KeyHandles.Num() == 0) continue;

					TArray<FFrameNumber> NewTimes;
					NewTimes.Init(NewFrame, KeyHandles.Num());
					Channel->SetKeyTimes(KeyHandles, NewTimes);
					MovedCount += KeyHandles.Num();
				}
			}
			Section->Modify();
		}
	}

	if (MovedCount == 0)
	{
		OutError = FString::Printf(
			TEXT("No keys found near time %.4f on actor '%s'%s"),
			TimeSeconds, *ActorLabel,
			PropertyName.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" property '%s'"), *PropertyName));
		return;
	}

	Seq->MarkPackageDirty();
	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"actor\":\"%s\",\"old_time\":%.4f,\"time\":%.4f,\"keys_moved\":%d}"),
		*ActorLabel, TimeSeconds, NewTimeSeconds, MovedCount);
}

void HandleSetSequencerKeyframeValue(const FString& SequencePath, const FString& ActorLabel,
	const FString& PropertyName, float TimeSeconds, float Value, const FString& ValueType,
	FString& OutJsonString, FString& OutError)
{
	if (PropertyName.IsEmpty())
	{
		OutError = TEXT("property_name is required so the right channel can be located unambiguously");
		return;
	}

	const FString TypeLower = ValueType.IsEmpty() ? TEXT("float") : ValueType.ToLower();
	const bool bFloat = (TypeLower == TEXT("float"));
	const bool bBool  = (TypeLower == TEXT("bool")) || (TypeLower == TEXT("visibility"));
	const bool bInt   = (TypeLower == TEXT("int"))  || (TypeLower == TEXT("integer"));
	if (!bFloat && !bBool && !bInt)
	{
		OutError = FString::Printf(
			TEXT("value_type '%s' not supported. Supported: float, bool, int, visibility (alias for bool). "
			     "Transform/Color keys are multi-channel — not yet exposed."),
			*ValueType);
		return;
	}

	ULevelSequence* Seq = LoadSeq(SequencePath, OutError);
	if (!Seq) return;
	UMovieScene* MS = Seq->GetMovieScene();

	FGuid Guid = FindOrAddBinding(Seq, ActorLabel, OutError);
	if (!Guid.IsValid()) return;

	const FFrameNumber TargetFrame = SecondsToFrame(MS, TimeSeconds);

	auto MatchTrack = [&](UMovieSceneTrack* Track) -> bool
	{
		if (!Track) return false;
		const UClass* Cls = Track->GetClass();
		if (bFloat && Cls != UMovieSceneFloatTrack::StaticClass()) return false;
		if (bBool  && Cls != UMovieSceneVisibilityTrack::StaticClass()
		           && Cls != UMovieSceneBoolTrack::StaticClass())   return false;
		if (bInt   && Cls != UMovieSceneIntegerTrack::StaticClass()) return false;
		if (UMovieScenePropertyTrack* PT = Cast<UMovieScenePropertyTrack>(Track))
		{
			return PT->GetPropertyName().ToString().Equals(PropertyName, ESearchCase::IgnoreCase) ||
			       PT->GetPropertyPath().ToString().Equals(PropertyName, ESearchCase::IgnoreCase) ||
			       PT->GetTrackName().ToString().Equals(PropertyName, ESearchCase::IgnoreCase);
		}
		return Track->GetTrackName().ToString().Equals(PropertyName, ESearchCase::IgnoreCase);
	};

	int32 SetCount = 0;
	for (UMovieSceneTrack* Track : MS->FindTracks(UMovieSceneTrack::StaticClass(), Guid, NAME_None))
	{
		if (!MatchTrack(Track)) continue;

		for (UMovieSceneSection* Section : Track->GetAllSections())
		{
			if (!Section) continue;

			if (bFloat)
			{
				if (UMovieSceneFloatSection* FS = Cast<UMovieSceneFloatSection>(Section))
				{
					FMovieSceneFloatChannel& Channel = FS->GetChannel();
					TArray<FFrameNumber> KT; TArray<FKeyHandle> KH;
					Channel.GetKeys(TRange<FFrameNumber>(TargetFrame - 1, TargetFrame + 1), &KT, &KH);
					for (const FKeyHandle& H : KH)
					{
						const int32 Idx = Channel.GetData().GetIndex(H);
						if (Idx == INDEX_NONE) continue;
						Channel.GetData().GetValues()[Idx].Value = Value;
						SetCount++;
					}
					Section->Modify();
				}
			}
			else if (bBool)
			{
				if (UMovieSceneBoolSection* BS = Cast<UMovieSceneBoolSection>(Section))
				{
					FMovieSceneBoolChannel& Channel = BS->GetChannel();
					TArray<FFrameNumber> KT; TArray<FKeyHandle> KH;
					Channel.GetKeys(TRange<FFrameNumber>(TargetFrame - 1, TargetFrame + 1), &KT, &KH);
					const bool BoolVal = Value != 0.f;
					for (const FKeyHandle& H : KH)
					{
						const int32 Idx = Channel.GetData().GetIndex(H);
						if (Idx == INDEX_NONE) continue;
						Channel.GetData().GetValues()[Idx] = BoolVal;
						SetCount++;
					}
					Section->Modify();
				}
			}
			else if (bInt)
			{
				if (UMovieSceneIntegerSection* IS = Cast<UMovieSceneIntegerSection>(Section))
				{
					TArrayView<FMovieSceneIntegerChannel*> Channels =
						IS->GetChannelProxy().GetChannels<FMovieSceneIntegerChannel>();
					if (Channels.Num() == 0 || !Channels[0]) continue;
					FMovieSceneIntegerChannel* Channel = Channels[0];

					TArray<FFrameNumber> KT; TArray<FKeyHandle> KH;
					Channel->GetKeys(TRange<FFrameNumber>(TargetFrame - 1, TargetFrame + 1), &KT, &KH);
					const int32 IntVal = (int32)Value;
					for (const FKeyHandle& H : KH)
					{
						const int32 Idx = Channel->GetData().GetIndex(H);
						if (Idx == INDEX_NONE) continue;
						Channel->GetData().GetValues()[Idx] = IntVal;
						SetCount++;
					}
					Section->Modify();
				}
			}
		}
	}

	if (SetCount == 0)
	{
		OutError = FString::Printf(
			TEXT("No %s keys found near time %.4f on actor '%s' property '%s'"),
			*TypeLower, TimeSeconds, *ActorLabel, *PropertyName);
		return;
	}

	Seq->MarkPackageDirty();
	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"actor\":\"%s\",\"property\":\"%s\",\"time\":%.4f,\"value_type\":\"%s\",\"keys_updated\":%d}"),
		*ActorLabel, *PropertyName, TimeSeconds, *TypeLower, SetCount);
}

void HandleMoveSequencerKeyframeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SP, AL, PN;
	double TS = 0.0, NTS = 0.0;
	Args->TryGetStringField(TEXT("sequence_path"),    SP);
	Args->TryGetStringField(TEXT("actor_label"),      AL);
	Args->TryGetStringField(TEXT("property_name"),    PN);
	Args->TryGetNumberField(TEXT("time_seconds"),     TS);
	Args->TryGetNumberField(TEXT("new_time_seconds"), NTS);
	HandleMoveSequencerKeyframe(SP, AL, PN, (float)TS, (float)NTS, OutJsonString, OutError);
}

void HandleSetSequencerKeyframeValueFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SP, AL, PN, VT;
	double TS = 0.0, V = 0.0;
	Args->TryGetStringField(TEXT("sequence_path"), SP);
	Args->TryGetStringField(TEXT("actor_label"),   AL);
	Args->TryGetStringField(TEXT("property_name"), PN);
	Args->TryGetNumberField(TEXT("time_seconds"),  TS);
	Args->TryGetNumberField(TEXT("value"),         V);
	Args->TryGetStringField(TEXT("value_type"),    VT);
	HandleSetSequencerKeyframeValue(SP, AL, PN, (float)TS, (float)V, VT, OutJsonString, OutError);
}

namespace
{
	UMovieSceneTrack* LocateSequencerTrack(ULevelSequence* Seq,
		const FString& ActorLabel, const FString& TrackName, FString& OutError)
	{
		UMovieSceneSection* Unused = nullptr;
		UMovieSceneTrack* Track = nullptr;
		FString SilentErr;
		Unused = LocateSequencerSection(Seq, ActorLabel, TrackName, 0, &Track, SilentErr);
		if (Track) return Track;

		UMovieScene* MS = Seq ? Seq->GetMovieScene() : nullptr;
		if (!MS) { OutError = TEXT("Sequence has no MovieScene"); return nullptr; }
		TArray<UMovieSceneTrack*> Candidates;
		if (ActorLabel.IsEmpty())
		{
			for (UMovieSceneTrack* T : MS->GetTracks()) if (T) Candidates.Add(T);
		}
		else
		{
			FString LookupErr;
			FGuid Guid = FindOrAddBinding(Seq, ActorLabel, LookupErr);
			if (!Guid.IsValid()) { OutError = LookupErr; return nullptr; }
			for (UMovieSceneTrack* T : MS->FindTracks(UMovieSceneTrack::StaticClass(), Guid, NAME_None))
				if (T) Candidates.Add(T);
		}
		if (TrackName.IsEmpty() && Candidates.Num() > 0) return Candidates[0];
		for (UMovieSceneTrack* T : Candidates)
		{
			if (T->GetTrackName().ToString().Equals(TrackName, ESearchCase::IgnoreCase)) return T;
		}
		for (UMovieSceneTrack* T : Candidates)
		{
			if (T->GetClass()->GetName().Contains(TrackName, ESearchCase::IgnoreCase)) return T;
		}
		OutError = FString::Printf(TEXT("Track '%s' not found"), *TrackName);
		return nullptr;
	}
}

void HandleRenameSequencerTrack(const FString& SequencePath, const FString& ActorLabel,
	const FString& TrackName, const FString& NewDisplayName,
	FString& OutJsonString, FString& OutError)
{
	if (NewDisplayName.IsEmpty()) { OutError = TEXT("new_display_name is required"); return; }

	ULevelSequence* Seq = LoadSeq(SequencePath, OutError);
	if (!Seq) return;

	UMovieSceneTrack* Track = LocateSequencerTrack(Seq, ActorLabel, TrackName, OutError);
	if (!Track) return;

	UMovieSceneNameableTrack* Nameable = Cast<UMovieSceneNameableTrack>(Track);
	if (!Nameable)
	{
		OutError = FString::Printf(
			TEXT("Track type '%s' is not nameable (only UMovieSceneNameableTrack subclasses support display-name editing)."),
			*Track->GetClass()->GetName());
		return;
	}

	Nameable->Modify();
	Nameable->SetDisplayName(FText::FromString(NewDisplayName));

	Seq->MarkPackageDirty();
	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"track\":\"%s\",\"new_display_name\":\"%s\"}"),
		*Track->GetClass()->GetName(), *NewDisplayName);
}

void HandleSetSequencerTrackEvalDisabled(const FString& SequencePath, const FString& ActorLabel,
	const FString& TrackName, bool bDisabled, FString& OutJsonString, FString& OutError)
{
	ULevelSequence* Seq = LoadSeq(SequencePath, OutError);
	if (!Seq) return;

	UMovieSceneTrack* Track = LocateSequencerTrack(Seq, ActorLabel, TrackName, OutError);
	if (!Track) return;

	const bool bWas = Track->IsEvalDisabled();
	Track->Modify();
	Track->SetEvalDisabled(bDisabled);

	Seq->MarkPackageDirty();
	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"track\":\"%s\",\"old_disabled\":%s,\"disabled\":%s}"),
		*Track->GetClass()->GetName(),
		bWas ? TEXT("true") : TEXT("false"),
		bDisabled ? TEXT("true") : TEXT("false"));
}

void HandleSetSequencerTrackSortOrder(const FString& SequencePath, const FString& ActorLabel,
	const FString& TrackName, int32 SortOrder, FString& OutJsonString, FString& OutError)
{
	ULevelSequence* Seq = LoadSeq(SequencePath, OutError);
	if (!Seq) return;

	UMovieSceneTrack* Track = LocateSequencerTrack(Seq, ActorLabel, TrackName, OutError);
	if (!Track) return;

	const int32 OldOrder = Track->GetSortingOrder();
	Track->Modify();
	Track->SetSortingOrder(SortOrder);

	Seq->MarkPackageDirty();
	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"track\":\"%s\",\"old_sort_order\":%d,\"sort_order\":%d}"),
		*Track->GetClass()->GetName(), OldOrder, SortOrder);
}

void HandleRenameSequencerBinding(const FString& SequencePath, const FString& CurrentLabel,
	const FString& NewLabel, FString& OutJsonString, FString& OutError)
{
	if (CurrentLabel.IsEmpty() || NewLabel.IsEmpty())
	{
		OutError = TEXT("current_label and new_label are both required");
		return;
	}
	if (CurrentLabel.Equals(NewLabel, ESearchCase::IgnoreCase))
	{
		OutError = TEXT("new_label is identical to current_label");
		return;
	}

	ULevelSequence* Seq = LoadSeq(SequencePath, OutError);
	if (!Seq) return;
	UMovieScene* MS = Seq->GetMovieScene();

	for (int32 i = 0; i < MS->GetPossessableCount(); ++i)
	{
		if (MS->GetPossessable(i).GetName().Equals(NewLabel, ESearchCase::IgnoreCase))
		{
			OutError = FString::Printf(TEXT("A possessable named '%s' already exists"), *NewLabel);
			return;
		}
	}
	for (int32 i = 0; i < MS->GetSpawnableCount(); ++i)
	{
		if (MS->GetSpawnable(i).GetName().Equals(NewLabel, ESearchCase::IgnoreCase))
		{
			OutError = FString::Printf(TEXT("A spawnable named '%s' already exists"), *NewLabel);
			return;
		}
	}

	for (int32 i = 0; i < MS->GetPossessableCount(); ++i)
	{
		FMovieScenePossessable& P = MS->GetPossessable(i);
		if (P.GetName().Equals(CurrentLabel, ESearchCase::IgnoreCase))
		{
			MS->Modify();
			P.SetName(NewLabel);
			Seq->MarkPackageDirty();
			OutJsonString = FString::Printf(
				TEXT("{\"success\":true,\"kind\":\"possessable\",\"old_label\":\"%s\",\"new_label\":\"%s\"}"),
				*CurrentLabel, *NewLabel);
			return;
		}
	}
	for (int32 i = 0; i < MS->GetSpawnableCount(); ++i)
	{
		FMovieSceneSpawnable& S = MS->GetSpawnable(i);
		if (S.GetName().Equals(CurrentLabel, ESearchCase::IgnoreCase))
		{
			MS->Modify();
			S.SetName(NewLabel);
			Seq->MarkPackageDirty();
			OutJsonString = FString::Printf(
				TEXT("{\"success\":true,\"kind\":\"spawnable\",\"old_label\":\"%s\",\"new_label\":\"%s\"}"),
				*CurrentLabel, *NewLabel);
			return;
		}
	}

	OutError = FString::Printf(TEXT("No binding named '%s' on this sequence"), *CurrentLabel);
}

void HandleRenameSequencerTrackFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SP, AL, TN, NDN;
	Args->TryGetStringField(TEXT("sequence_path"),     SP);
	Args->TryGetStringField(TEXT("actor_label"),       AL);
	Args->TryGetStringField(TEXT("track_name"),        TN);
	Args->TryGetStringField(TEXT("new_display_name"),  NDN);
	HandleRenameSequencerTrack(SP, AL, TN, NDN, OutJsonString, OutError);
}

void HandleSetSequencerTrackEvalDisabledFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SP, AL, TN;
	bool bDisabled = false;
	Args->TryGetStringField(TEXT("sequence_path"), SP);
	Args->TryGetStringField(TEXT("actor_label"),   AL);
	Args->TryGetStringField(TEXT("track_name"),    TN);
	Args->TryGetBoolField  (TEXT("disabled"),      bDisabled);
	HandleSetSequencerTrackEvalDisabled(SP, AL, TN, bDisabled, OutJsonString, OutError);
}

void HandleSetSequencerTrackSortOrderFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SP, AL, TN;
	double SO = 0.0;
	Args->TryGetStringField(TEXT("sequence_path"), SP);
	Args->TryGetStringField(TEXT("actor_label"),   AL);
	Args->TryGetStringField(TEXT("track_name"),    TN);
	Args->TryGetNumberField(TEXT("sort_order"),    SO);
	HandleSetSequencerTrackSortOrder(SP, AL, TN, (int32)SO, OutJsonString, OutError);
}

void HandleRenameSequencerBindingFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SP, CL, NL;
	Args->TryGetStringField(TEXT("sequence_path"), SP);
	Args->TryGetStringField(TEXT("current_label"), CL);
	Args->TryGetStringField(TEXT("new_label"),     NL);
	HandleRenameSequencerBinding(SP, CL, NL, OutJsonString, OutError);
}

void HandleAddSequenceCameraShake(const FString& SequencePath, const FString& ActorLabel,
	const FString& ShakeClassPath, float StartSeconds, float DurationSeconds, float PlayScale,
	FString& OutJsonString, FString& OutError)
{
	if (ShakeClassPath.IsEmpty()) { OutError = TEXT("shake_class_path is required"); return; }
	if (DurationSeconds <= 0.f)   DurationSeconds = 2.0f;
	if (PlayScale <= 0.f)         PlayScale = 1.0f;

	ULevelSequence* Seq = LoadSeq(SequencePath, OutError);
	if (!Seq) return;
	UMovieScene* MS = Seq->GetMovieScene();

	UClass* ShakeClass = LoadClass<UCameraShakeBase>(nullptr, *ShakeClassPath);
	if (!ShakeClass && !ShakeClassPath.EndsWith(TEXT("_C")))
	{
		ShakeClass = LoadClass<UCameraShakeBase>(nullptr, *(ShakeClassPath + TEXT("_C")));
	}
	if (!ShakeClass)
	{
		OutError = FString::Printf(
			TEXT("Could not load camera shake class at '%s'. Use the full asset path (Blueprint: '/Game/Path/Asset.Asset_C'; native: '/Script/Module.ShakeClass')."),
			*ShakeClassPath);
		return;
	}

	FGuid Guid = FindOrAddBinding(Seq, ActorLabel, OutError);
	if (!Guid.IsValid()) return;

	UMovieSceneCameraShakeTrack* Track = nullptr;
	for (UMovieSceneTrack* T : MS->FindTracks(UMovieSceneCameraShakeTrack::StaticClass(), Guid, NAME_None))
	{
		Track = Cast<UMovieSceneCameraShakeTrack>(T);
		if (Track) break;
	}
	if (!Track)
	{
		Track = MS->AddTrack<UMovieSceneCameraShakeTrack>(Guid);
		if (!Track) { OutError = TEXT("Failed to add UMovieSceneCameraShakeTrack to binding"); return; }
	}

	const FFrameNumber StartFrame = SecondsToFrame(MS, StartSeconds);
	UMovieSceneSection* Section = Track->AddNewCameraShake(StartFrame, ShakeClass);
	if (!Section)
	{
		OutError = TEXT("UMovieSceneCameraShakeTrack::AddNewCameraShake returned null");
		return;
	}

	const FFrameNumber EndFrame = SecondsToFrame(MS, StartSeconds + DurationSeconds);
	Section->SetRange(TRange<FFrameNumber>(StartFrame, EndFrame));
	if (UMovieSceneCameraShakeSection* ShakeSection = Cast<UMovieSceneCameraShakeSection>(Section))
	{
		ShakeSection->ShakeData.PlayScale = PlayScale;
	}

	Seq->MarkPackageDirty();
	OutJsonString = FString::Printf(
		TEXT("{\"success\":true,\"actor\":\"%s\",\"shake_class\":\"%s\",\"start_seconds\":%.4f,\"duration_seconds\":%.4f,\"play_scale\":%.4f}"),
		*ActorLabel, *ShakeClass->GetPathName(), StartSeconds, DurationSeconds, PlayScale);
}

void HandleAddSequenceCameraShakeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJsonString, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SP, AL, SC;
	double Start = 0.0, Duration = 2.0, Scale = 1.0;
	Args->TryGetStringField(TEXT("sequence_path"),     SP);
	Args->TryGetStringField(TEXT("actor_label"),       AL);
	Args->TryGetStringField(TEXT("shake_class_path"),  SC);
	Args->TryGetNumberField(TEXT("start_seconds"),     Start);
	Args->TryGetNumberField(TEXT("duration_seconds"),  Duration);
	Args->TryGetNumberField(TEXT("play_scale"),        Scale);
	HandleAddSequenceCameraShake(SP, AL, SC, (float)Start, (float)Duration, (float)Scale,
		OutJsonString, OutError);
}

}
