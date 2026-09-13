// Copyright 2026, BlueprintsLab, All rights reserved.

#include "Tools/ControlRigAnimLayerTools.h"
#include "Misc/EngineVersionComparison.h"

#if !UE_VERSION_OLDER_THAN(5, 5, 0)

#include "ControlRig.h"
#include "ControlRigSequencerEditorLibrary.h"
#include "Sequencer/MovieSceneControlRigParameterTrack.h"
#include "MovieScene.h"
#include "MovieSceneSection.h"
#include "MovieSceneTrack.h"
#include "MovieSceneBindingProxy.h"
#include "MovieScenePossessable.h"
#include "MovieSceneSpawnable.h"
#include "LevelSequence.h"

#include "EditorAssetLibrary.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace ControlRigAnimLayerTools
{

namespace
{
	FString SerializeJson(const TSharedPtr<FJsonObject>& Obj)
	{
		FString Out;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
		FJsonSerializer::Serialize(Obj.ToSharedRef(), Writer);
		return Out;
	}

	ULevelSequence* LoadSeq(const FString& Path, FString& OutError)
	{
		ULevelSequence* Seq = Cast<ULevelSequence>(UEditorAssetLibrary::LoadAsset(Path));
		if (!Seq) OutError = FString::Printf(TEXT("LevelSequence not found at '%s'"), *Path);
		return Seq;
	}

	UWorld* GetEditorWorld(FString& OutError)
	{
		UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
		if (!World) OutError = TEXT("No editor world available");
		return World;
	}

	FGuid ResolveBindingGuid(UMovieScene* MS, const TSharedPtr<FJsonObject>& Args, FString& OutError)
	{
		FString GuidStr;
		if (Args->TryGetStringField(TEXT("binding_guid"), GuidStr) && !GuidStr.IsEmpty())
		{
			FGuid G;
			if (FGuid::Parse(GuidStr, G)) return G;
			OutError = FString::Printf(TEXT("invalid binding_guid '%s'"), *GuidStr);
			return FGuid();
		}
		FString Label;
		if (Args->TryGetStringField(TEXT("binding_label"), Label) && !Label.IsEmpty())
		{
			for (int32 i = 0; i < MS->GetPossessableCount(); ++i)
			{
				if (MS->GetPossessable(i).GetName() == Label) return MS->GetPossessable(i).GetGuid();
			}
			for (int32 i = 0; i < MS->GetSpawnableCount(); ++i)
			{
				if (MS->GetSpawnable(i).GetName() == Label) return MS->GetSpawnable(i).GetGuid();
			}
			OutError = FString::Printf(TEXT("binding '%s' not found"), *Label);
			return FGuid();
		}
		OutError = TEXT("binding_guid or binding_label required");
		return FGuid();
	}

	UMovieSceneControlRigParameterTrack* ResolveControlRigTrack(UMovieScene* MS, const FGuid& BindingGuid, int32 TrackIndex, FString& OutError)
	{
		const TArray<UMovieSceneTrack*> Tracks = MS->FindTracks(UMovieSceneControlRigParameterTrack::StaticClass(), BindingGuid);
		if (!Tracks.IsValidIndex(TrackIndex))
		{
			OutError = FString::Printf(TEXT("track_index %d out of range [0,%d) on UMovieSceneControlRigParameterTrack list"), TrackIndex, Tracks.Num());
			return nullptr;
		}
		return Cast<UMovieSceneControlRigParameterTrack>(Tracks[TrackIndex]);
	}

	UControlRig* ResolveControlRigInstance(UMovieSceneControlRigParameterTrack* Track, FString& OutError)
	{
		if (!Track) { OutError = TEXT("Track is null"); return nullptr; }
		UControlRig* Rig = Track->GetControlRig();
		if (!Rig) OutError = TEXT("ControlRig instance not bound on this track");
		return Rig;
	}
}

void HandleGetAnimLayersFromArgs(const TSharedPtr<FJsonObject>& , FString& OutJson, FString& )
{
	const TArray<UAnimLayer*> Layers = UControlRigSequencerEditorLibrary::GetAnimLayers();
	TArray<TSharedPtr<FJsonValue>> Arr;
	for (int32 i = 0; i < Layers.Num(); ++i)
	{
		UObject* AsObj = (UObject*)Layers[i];
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetNumberField(TEXT("index"), i);
		Entry->SetStringField(TEXT("name"), AsObj ? AsObj->GetName() : FString());
		Entry->SetStringField(TEXT("class"), AsObj ? AsObj->GetClass()->GetName() : FString());
		Arr.Add(MakeShared<FJsonValueObject>(Entry));
	}

	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetBoolField(TEXT("success"), true);
	R->SetNumberField(TEXT("count"), Arr.Num());
	R->SetArrayField(TEXT("layers"), Arr);
	OutJson = SerializeJson(R);
}

void HandleAddAnimLayerFromSelectionFromArgs(const TSharedPtr<FJsonObject>& , FString& OutJson, FString& )
{
	const int32 NewIndex = UControlRigSequencerEditorLibrary::AddAnimLayerFromSelection();
	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetBoolField(TEXT("success"), NewIndex != INDEX_NONE);
	R->SetNumberField(TEXT("index"), NewIndex);
	OutJson = SerializeJson(R);
}

void HandleDeleteAnimLayerFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	int32 Index = -1; Args->TryGetNumberField(TEXT("index"), Index);
	if (Index < 0) { OutError = TEXT("index required"); return; }
	const bool bOk = UControlRigSequencerEditorLibrary::DeleteAnimLayer(Index);
	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetBoolField(TEXT("success"), bOk);
	R->SetNumberField(TEXT("index"), Index);
	OutJson = SerializeJson(R);
}

void HandleDuplicateAnimLayerFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	int32 Index = -1; Args->TryGetNumberField(TEXT("index"), Index);
	if (Index < 0) { OutError = TEXT("index required"); return; }
	const int32 NewIndex = UControlRigSequencerEditorLibrary::DuplicateAnimLayer(Index);
	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetBoolField(TEXT("success"), NewIndex != INDEX_NONE);
	R->SetNumberField(TEXT("source_index"), Index);
	R->SetNumberField(TEXT("new_index"), NewIndex);
	OutJson = SerializeJson(R);
}

void HandleMergeAnimLayersFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	const TArray<TSharedPtr<FJsonValue>>* Indices = nullptr;
	if (!Args->TryGetArrayField(TEXT("indices"), Indices) || !Indices || Indices->Num() < 2)
	{
		OutError = TEXT("indices (array of >=2 layer indices) required");
		return;
	}
	TArray<int32> IdxArr;
	for (const TSharedPtr<FJsonValue>& V : *Indices) IdxArr.Add(static_cast<int32>(V->AsNumber()));
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	const bool bOk = UControlRigSequencerEditorLibrary::MergeAnimLayers(IdxArr);
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetBoolField(TEXT("success"), bOk);
	R->SetNumberField(TEXT("merged_count"), IdxArr.Num());
	OutJson = SerializeJson(R);
}

void HandleSetLayeredModeFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SequencePath; Args->TryGetStringField(TEXT("sequence_path"), SequencePath);
	int32 TrackIndex = -1; Args->TryGetNumberField(TEXT("track_index"), TrackIndex);
	bool bLayered = false; Args->TryGetBoolField(TEXT("layered"), bLayered);

	ULevelSequence* Seq = LoadSeq(SequencePath, OutError); if (!Seq) return;
	UMovieScene* MS = Seq->GetMovieScene(); if (!MS) { OutError = TEXT("No MovieScene"); return; }
	const FGuid Guid = ResolveBindingGuid(MS, Args, OutError); if (!Guid.IsValid()) return;
	UMovieSceneControlRigParameterTrack* Track = ResolveControlRigTrack(MS, Guid, TrackIndex, OutError);
	if (!Track) return;

	const bool bOk = UControlRigSequencerEditorLibrary::SetControlRigLayeredMode(Track, bLayered);

	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetBoolField(TEXT("success"), bOk);
	R->SetBoolField(TEXT("layered"), bLayered);
	OutJson = SerializeJson(R);
}

void HandleIsLayeredControlRigFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SequencePath; Args->TryGetStringField(TEXT("sequence_path"), SequencePath);
	int32 TrackIndex = -1; Args->TryGetNumberField(TEXT("track_index"), TrackIndex);

	ULevelSequence* Seq = LoadSeq(SequencePath, OutError); if (!Seq) return;
	UMovieScene* MS = Seq->GetMovieScene(); if (!MS) { OutError = TEXT("No MovieScene"); return; }
	const FGuid Guid = ResolveBindingGuid(MS, Args, OutError); if (!Guid.IsValid()) return;
	UMovieSceneControlRigParameterTrack* Track = ResolveControlRigTrack(MS, Guid, TrackIndex, OutError);
	if (!Track) return;
	UControlRig* Rig = ResolveControlRigInstance(Track, OutError); if (!Rig) return;

	const bool bIsLayered = UControlRigSequencerEditorLibrary::IsLayeredControlRig(Rig);

	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetBoolField(TEXT("success"), true);
	R->SetBoolField(TEXT("is_layered"), bIsLayered);
	OutJson = SerializeJson(R);
}

void HandleBakeToControlRigFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SequencePath, ControlRigClassPath;
	Args->TryGetStringField(TEXT("sequence_path"), SequencePath);
	Args->TryGetStringField(TEXT("control_rig_class"), ControlRigClassPath);
	bool bReduceKeys = false; Args->TryGetBoolField(TEXT("reduce_keys"), bReduceKeys);
	double Tolerance = 0.001; Args->TryGetNumberField(TEXT("tolerance"), Tolerance);

	if (ControlRigClassPath.IsEmpty()) { OutError = TEXT("control_rig_class required"); return; }
	ULevelSequence* Seq = LoadSeq(SequencePath, OutError); if (!Seq) return;
	UMovieScene* MS = Seq->GetMovieScene(); if (!MS) { OutError = TEXT("No MovieScene"); return; }
	const FGuid Guid = ResolveBindingGuid(MS, Args, OutError); if (!Guid.IsValid()) return;

	UClass* RigClass = LoadObject<UClass>(nullptr, *ControlRigClassPath);
	if (!RigClass)
	{
		const FString WithSuffix = ControlRigClassPath.EndsWith(TEXT("_C")) ? ControlRigClassPath : ControlRigClassPath + TEXT("_C");
		RigClass = LoadObject<UClass>(nullptr, *WithSuffix);
	}
	if (!RigClass) { OutError = FString::Printf(TEXT("control_rig_class '%s' did not resolve"), *ControlRigClassPath); return; }

	UWorld* World = GetEditorWorld(OutError); if (!World) return;

	FMovieSceneBindingProxy Proxy(Guid, Seq);
	const bool bOk = UControlRigSequencerEditorLibrary::BakeToControlRig(
		World, Seq, RigClass,  nullptr,
		bReduceKeys, static_cast<float>(Tolerance), Proxy);

	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetBoolField(TEXT("success"), bOk);
	R->SetStringField(TEXT("control_rig_class"), RigClass->GetPathName());
	OutJson = SerializeJson(R);
}

void HandleCollapseAnimLayersFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SequencePath; Args->TryGetStringField(TEXT("sequence_path"), SequencePath);
	int32 TrackIndex = -1; Args->TryGetNumberField(TEXT("track_index"), TrackIndex);
	bool bReduceKeys = false; Args->TryGetBoolField(TEXT("reduce_keys"), bReduceKeys);
	double Tolerance = 0.001; Args->TryGetNumberField(TEXT("tolerance"), Tolerance);

	ULevelSequence* Seq = LoadSeq(SequencePath, OutError); if (!Seq) return;
	UMovieScene* MS = Seq->GetMovieScene(); if (!MS) { OutError = TEXT("No MovieScene"); return; }
	const FGuid Guid = ResolveBindingGuid(MS, Args, OutError); if (!Guid.IsValid()) return;
	UMovieSceneControlRigParameterTrack* Track = ResolveControlRigTrack(MS, Guid, TrackIndex, OutError);
	if (!Track) return;

	const bool bOk = UControlRigSequencerEditorLibrary::CollapseControlRigAnimLayers(
		Seq, Track, bReduceKeys, static_cast<float>(Tolerance));

	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetBoolField(TEXT("success"), bOk);
	OutJson = SerializeJson(R);
}

void HandleTweenControlRigFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SequencePath; Args->TryGetStringField(TEXT("sequence_path"), SequencePath);
	int32 TrackIndex = -1; Args->TryGetNumberField(TEXT("track_index"), TrackIndex);
	double Tween = 0.0; Args->TryGetNumberField(TEXT("tween_value"), Tween);

	ULevelSequence* Seq = LoadSeq(SequencePath, OutError); if (!Seq) return;
	UMovieScene* MS = Seq->GetMovieScene(); if (!MS) { OutError = TEXT("No MovieScene"); return; }
	const FGuid Guid = ResolveBindingGuid(MS, Args, OutError); if (!Guid.IsValid()) return;
	UMovieSceneControlRigParameterTrack* Track = ResolveControlRigTrack(MS, Guid, TrackIndex, OutError);
	if (!Track) return;
	UControlRig* Rig = ResolveControlRigInstance(Track, OutError); if (!Rig) return;

	const bool bOk = UControlRigSequencerEditorLibrary::TweenControlRig(Seq, Rig, static_cast<float>(Tween));

	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetBoolField(TEXT("success"), bOk);
	R->SetNumberField(TEXT("tween_value"), Tween);
	OutJson = SerializeJson(R);
}

}

#else

#include "Dom/JsonObject.h"

namespace ControlRigAnimLayerTools
{
	static const TCHAR* kRequires55 =
		TEXT("Control-rig anim-layer tools require UE 5.5+ (UAnimLayer + UControlRigSequencerEditorLibrary's "
		     "anim-layer surface didn't exist in 5.4).");

	void HandleGetAnimLayersFromArgs            (const TSharedPtr<FJsonObject>&, FString&, FString& OutError) { OutError = kRequires55; }
	void HandleAddAnimLayerFromSelectionFromArgs(const TSharedPtr<FJsonObject>&, FString&, FString& OutError) { OutError = kRequires55; }
	void HandleDeleteAnimLayerFromArgs          (const TSharedPtr<FJsonObject>&, FString&, FString& OutError) { OutError = kRequires55; }
	void HandleDuplicateAnimLayerFromArgs       (const TSharedPtr<FJsonObject>&, FString&, FString& OutError) { OutError = kRequires55; }
	void HandleMergeAnimLayersFromArgs          (const TSharedPtr<FJsonObject>&, FString&, FString& OutError) { OutError = kRequires55; }
	void HandleSetLayeredModeFromArgs           (const TSharedPtr<FJsonObject>&, FString&, FString& OutError) { OutError = kRequires55; }
	void HandleIsLayeredControlRigFromArgs      (const TSharedPtr<FJsonObject>&, FString&, FString& OutError) { OutError = kRequires55; }
	void HandleBakeToControlRigFromArgs         (const TSharedPtr<FJsonObject>&, FString&, FString& OutError) { OutError = kRequires55; }
	void HandleCollapseAnimLayersFromArgs       (const TSharedPtr<FJsonObject>&, FString&, FString& OutError) { OutError = kRequires55; }
	void HandleTweenControlRigFromArgs          (const TSharedPtr<FJsonObject>&, FString&, FString& OutError) { OutError = kRequires55; }
}

#endif
