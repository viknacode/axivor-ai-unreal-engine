// Copyright 2026, BlueprintsLab, All rights reserved.

#include "Tools/SequencerWorkflowTools.h"
#include "Tools/BatchToolHelper.h"

#include "LevelSequence.h"
#include "LevelSequenceEditorBlueprintLibrary.h"
#include "LevelSequenceEditorSubsystem.h"
#include "MovieScene.h"
#include "MovieSceneSection.h"
#include "MovieSceneTrack.h"
#include "MovieSceneFolder.h"
#include "MovieScenePossessable.h"
#include "MovieSceneSpawnable.h"
#include "MovieSceneBindingProxy.h"
#include "SequencerUtilities.h"
#include "Misc/EngineVersionComparison.h"
#if !UE_VERSION_OLDER_THAN(5, 5, 0)
#include "Conditions/MovieSceneCondition.h"
#endif
#include "Sections/MovieSceneEventTriggerSection.h"
#include "Sections/MovieSceneEventRepeaterSection.h"
#include "Tracks/MovieSceneEventTrack.h"

#include "EditorAssetLibrary.h"
#include "Editor.h"
#include "UObject/UObjectIterator.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace SequencerWorkflowTools
{

namespace
{
	ULevelSequence* LoadSeq(const FString& Path, FString& OutError)
	{
		ULevelSequence* Seq = Cast<ULevelSequence>(UEditorAssetLibrary::LoadAsset(Path));
		if (!Seq) OutError = FString::Printf(TEXT("LevelSequence not found at '%s'"), *Path);
		return Seq;
	}

	UMovieScene* GetMovieScene(const FString& Path, FString& OutError, ULevelSequence** OutSeq = nullptr)
	{
		ULevelSequence* Seq = LoadSeq(Path, OutError); if (!Seq) return nullptr;
		if (OutSeq) *OutSeq = Seq;
		UMovieScene* MS = Seq->GetMovieScene();
		if (!MS) OutError = TEXT("Sequence has no MovieScene");
		return MS;
	}

	FString SerializeJson(const TSharedPtr<FJsonObject>& Obj)
	{
		FString Out;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
		FJsonSerializer::Serialize(Obj.ToSharedRef(), Writer);
		return Out;
	}

	FGuid FindBindingGuidByLabel(UMovieScene* MS, const FString& Label)
	{
		if (Label.IsEmpty()) return FGuid();
		for (int32 i = 0; i < MS->GetPossessableCount(); ++i)
		{
			if (MS->GetPossessable(i).GetName() == Label) return MS->GetPossessable(i).GetGuid();
		}
		for (int32 i = 0; i < MS->GetSpawnableCount(); ++i)
		{
			if (MS->GetSpawnable(i).GetName() == Label) return MS->GetSpawnable(i).GetGuid();
		}
		return FGuid();
	}

	UMovieSceneTrack* ResolveTrack(UMovieScene* MS, const FString& BindingLabel, int32 TrackIndex, FString& OutError)
	{
		if (BindingLabel.IsEmpty())
		{
			const TArray<UMovieSceneTrack*>& Master = MS->GetTracks();
			if (!Master.IsValidIndex(TrackIndex))
			{
				OutError = FString::Printf(TEXT("track_index %d out of range [0,%d) on master tracks"), TrackIndex, Master.Num());
				return nullptr;
			}
			return Master[TrackIndex];
		}
		const FGuid Guid = FindBindingGuidByLabel(MS, BindingLabel);
		if (!Guid.IsValid()) { OutError = FString::Printf(TEXT("binding '%s' not found"), *BindingLabel); return nullptr; }
		const TArray<UMovieSceneTrack*> Tracks = MS->FindTracks(UMovieSceneTrack::StaticClass(), Guid);
		if (!Tracks.IsValidIndex(TrackIndex))
		{
			OutError = FString::Printf(TEXT("track_index %d out of range [0,%d) on binding '%s'"), TrackIndex, Tracks.Num(), *BindingLabel);
			return nullptr;
		}
		return Tracks[TrackIndex];
	}

	ULevelSequenceEditorSubsystem* GetEditorSubsystem(FString& OutError)
	{
		ULevelSequenceEditorSubsystem* Sub = GEditor ? GEditor->GetEditorSubsystem<ULevelSequenceEditorSubsystem>() : nullptr;
		if (!Sub) OutError = TEXT("LevelSequenceEditorSubsystem not available");
		return Sub;
	}

	UClass* ResolveConditionClass(const FString& ClassPath)
	{
		if (ClassPath.IsEmpty()) return nullptr;
		if (UClass* Direct = FindObject<UClass>(nullptr, *ClassPath)) return Direct;
		if (UClass* Loaded = LoadObject<UClass>(nullptr, *ClassPath)) return Loaded;
		const FString WithSuffix = ClassPath.EndsWith(TEXT("_C")) ? ClassPath : ClassPath + TEXT("_C");
		return LoadObject<UClass>(nullptr, *WithSuffix);
	}

#if !UE_VERSION_OLDER_THAN(5, 5, 0)
	bool ApplyCondition(FMovieSceneConditionContainer& Container, UObject* Outer, const FString& ClassPath, FString& OutError)
	{
		if (ClassPath.IsEmpty())
		{
			Container.Condition = nullptr;
			return true;
		}
		UClass* Cls = ResolveConditionClass(ClassPath);
		if (!Cls || !Cls->IsChildOf(UMovieSceneCondition::StaticClass()))
		{
			OutError = FString::Printf(TEXT("condition_class '%s' did not resolve to a UMovieSceneCondition subclass"), *ClassPath);
			return false;
		}
		Container.Condition = NewObject<UMovieSceneCondition>(Outer, Cls);
		return true;
	}
#endif
}

void HandleCopyBindingsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SequencePath; Args->TryGetStringField(TEXT("sequence_path"), SequencePath);
	ULevelSequence* Seq = nullptr;
	UMovieScene* MS = GetMovieScene(SequencePath, OutError, &Seq); if (!MS) return;
	ULevelSequenceEditorBlueprintLibrary::OpenLevelSequence(Seq);
	ULevelSequenceEditorSubsystem* Sub = GetEditorSubsystem(OutError); if (!Sub) return;

	const TArray<TSharedPtr<FJsonValue>>* Labels = nullptr;
	TArray<FMovieSceneBindingProxy> Proxies;
	if (Args->TryGetArrayField(TEXT("binding_labels"), Labels) && Labels)
	{
		for (const TSharedPtr<FJsonValue>& V : *Labels)
		{
			const FGuid G = FindBindingGuidByLabel(MS, V->AsString());
			if (G.IsValid()) Proxies.Add(FMovieSceneBindingProxy(G, Seq));
		}
	}

	FString Exported;
	Sub->CopyBindings(Proxies, Exported);

	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetBoolField(TEXT("success"), true);
	R->SetNumberField(TEXT("copied_bindings"), Proxies.Num());
	R->SetStringField(TEXT("exported_text"), Exported);
	OutJson = SerializeJson(R);
}

void HandlePasteBindingsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SequencePath; Args->TryGetStringField(TEXT("sequence_path"), SequencePath);
	ULevelSequence* Seq = LoadSeq(SequencePath, OutError); if (!Seq) return;
	ULevelSequenceEditorBlueprintLibrary::OpenLevelSequence(Seq);
	ULevelSequenceEditorSubsystem* Sub = GetEditorSubsystem(OutError); if (!Sub) return;

	FString Exported; Args->TryGetStringField(TEXT("exported_text"), Exported);
	bool bDup = false; Args->TryGetBoolField(TEXT("duplicate_existing_actors"), bDup);

	FMovieScenePasteBindingsParams Params;
	Params.bDuplicateExistingActors = bDup;
	TArray<FMovieSceneBindingProxy> Out;
	const bool bOk = Sub->PasteBindings(Exported, Params, Out);

	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetBoolField(TEXT("success"), bOk);
	R->SetNumberField(TEXT("pasted_bindings"), Out.Num());
	OutJson = SerializeJson(R);
}

void HandleCopyTracksFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SequencePath; Args->TryGetStringField(TEXT("sequence_path"), SequencePath);
	ULevelSequence* Seq = nullptr;
	UMovieScene* MS = GetMovieScene(SequencePath, OutError, &Seq); if (!MS) return;
	ULevelSequenceEditorBlueprintLibrary::OpenLevelSequence(Seq);
	ULevelSequenceEditorSubsystem* Sub = GetEditorSubsystem(OutError); if (!Sub) return;

	FString BindingLabel; Args->TryGetStringField(TEXT("binding_label"), BindingLabel);
	const TArray<TSharedPtr<FJsonValue>>* Indices = nullptr;
	if (!Args->TryGetArrayField(TEXT("track_indices"), Indices) || !Indices)
	{
		OutError = TEXT("track_indices (array) required");
		return;
	}

	TArray<UMovieSceneTrack*> Tracks;
	for (const TSharedPtr<FJsonValue>& V : *Indices)
	{
		const int32 Idx = static_cast<int32>(V->AsNumber());
		FString Err;
		if (UMovieSceneTrack* T = ResolveTrack(MS, BindingLabel, Idx, Err)) Tracks.Add(T);
	}

	FString Exported;
	Sub->CopyTracks(Tracks, Exported);

	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetBoolField(TEXT("success"), true);
	R->SetNumberField(TEXT("copied_tracks"), Tracks.Num());
	R->SetStringField(TEXT("exported_text"), Exported);
	OutJson = SerializeJson(R);
}

void HandlePasteTracksFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SequencePath; Args->TryGetStringField(TEXT("sequence_path"), SequencePath);
	ULevelSequence* Seq = nullptr;
	UMovieScene* MS = GetMovieScene(SequencePath, OutError, &Seq); if (!MS) return;
	ULevelSequenceEditorBlueprintLibrary::OpenLevelSequence(Seq);
	ULevelSequenceEditorSubsystem* Sub = GetEditorSubsystem(OutError); if (!Sub) return;

	FString Exported; Args->TryGetStringField(TEXT("exported_text"), Exported);
	FString BindingLabel; Args->TryGetStringField(TEXT("binding_label"), BindingLabel);

	FMovieScenePasteTracksParams Params;
	Params.Sequence = Seq;
	if (!BindingLabel.IsEmpty())
	{
		const FGuid Guid = FindBindingGuidByLabel(MS, BindingLabel);
		if (Guid.IsValid()) Params.Bindings.Add(FMovieSceneBindingProxy(Guid, Seq));
	}

	TArray<UMovieSceneTrack*> Out;
	const bool bOk = Sub->PasteTracks(Exported, Params, Out);

	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetBoolField(TEXT("success"), bOk);
	R->SetNumberField(TEXT("pasted_tracks"), Out.Num());
	OutJson = SerializeJson(R);
}

void HandleCopySectionsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SequencePath; Args->TryGetStringField(TEXT("sequence_path"), SequencePath);
	ULevelSequence* Seq = nullptr;
	UMovieScene* MS = GetMovieScene(SequencePath, OutError, &Seq); if (!MS) return;
	ULevelSequenceEditorBlueprintLibrary::OpenLevelSequence(Seq);
	ULevelSequenceEditorSubsystem* Sub = GetEditorSubsystem(OutError); if (!Sub) return;

	FString BindingLabel; Args->TryGetStringField(TEXT("binding_label"), BindingLabel);
	int32 TrackIdx = -1; Args->TryGetNumberField(TEXT("track_index"), TrackIdx);
	UMovieSceneTrack* Track = ResolveTrack(MS, BindingLabel, TrackIdx, OutError);
	if (!Track) return;

	const TArray<UMovieSceneSection*>& AllSections = Track->GetAllSections();
	const TArray<TSharedPtr<FJsonValue>>* SecIndices = nullptr;
	TArray<UMovieSceneSection*> Sections;
	if (Args->TryGetArrayField(TEXT("section_indices"), SecIndices) && SecIndices)
	{
		for (const TSharedPtr<FJsonValue>& V : *SecIndices)
		{
			const int32 Idx = static_cast<int32>(V->AsNumber());
			if (AllSections.IsValidIndex(Idx)) Sections.Add(AllSections[Idx]);
		}
	}
	else
	{
		Sections = AllSections;
	}

	FString Exported;
	Sub->CopySections(Sections, Exported);

	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetBoolField(TEXT("success"), true);
	R->SetNumberField(TEXT("copied_sections"), Sections.Num());
	R->SetStringField(TEXT("exported_text"), Exported);
	OutJson = SerializeJson(R);
}

void HandlePasteSectionsFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SequencePath; Args->TryGetStringField(TEXT("sequence_path"), SequencePath);
	ULevelSequence* Seq = nullptr;
	UMovieScene* MS = GetMovieScene(SequencePath, OutError, &Seq); if (!MS) return;
	ULevelSequenceEditorBlueprintLibrary::OpenLevelSequence(Seq);
	ULevelSequenceEditorSubsystem* Sub = GetEditorSubsystem(OutError); if (!Sub) return;

	FString Exported; Args->TryGetStringField(TEXT("exported_text"), Exported);
	FString BindingLabel; Args->TryGetStringField(TEXT("binding_label"), BindingLabel);
	int32 TrackIdx = -1; Args->TryGetNumberField(TEXT("track_index"), TrackIdx);
	int32 TimeFrame = 0; Args->TryGetNumberField(TEXT("time_frame"), TimeFrame);

	UMovieSceneTrack* Track = ResolveTrack(MS, BindingLabel, TrackIdx, OutError);
	if (!Track) return;

	FMovieScenePasteSectionsParams Params;
	Params.Tracks.Add(Track);
	Params.Time = FFrameTime(FFrameNumber(TimeFrame));

	TArray<UMovieSceneSection*> Out;
	const bool bOk = Sub->PasteSections(Exported, Params, Out);

	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetBoolField(TEXT("success"), bOk);
	R->SetNumberField(TEXT("pasted_sections"), Out.Num());
	OutJson = SerializeJson(R);
}

namespace
{
	void AddEventSectionImpl(UClass* SectionClass, const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
	{
		if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
		FString SequencePath; Args->TryGetStringField(TEXT("sequence_path"), SequencePath);
		UMovieScene* MS = GetMovieScene(SequencePath, OutError); if (!MS) return;
		FString BindingLabel; Args->TryGetStringField(TEXT("binding_label"), BindingLabel);
		int32 TrackIdx = -1; Args->TryGetNumberField(TEXT("track_index"), TrackIdx);

		UMovieSceneTrack* Track = ResolveTrack(MS, BindingLabel, TrackIdx, OutError);
		if (!Track) return;
		if (!Cast<UMovieSceneEventTrack>(Track))
		{
			OutError = FString::Printf(TEXT("Track at index %d is not a UMovieSceneEventTrack"), TrackIdx);
			return;
		}

		const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
		const bool bIsRepeater = (SectionClass == UMovieSceneEventRepeaterSection::StaticClass());

		auto AddOne = [&](int32 StartFrame, int32 EndFrame, FString& OutErr) -> UMovieSceneSection*
		{
			Track->Modify();
			UMovieSceneSection* Section = NewObject<UMovieSceneSection>(Track, SectionClass, NAME_None, RF_Transactional);
			if (!Section) { OutErr = TEXT("Failed to create event section"); return nullptr; }
			if (bIsRepeater)
			{
				Section->SetRange(TRange<FFrameNumber>(FFrameNumber(StartFrame), FFrameNumber(EndFrame)));
			}
			else
			{
				Section->SetRange(TRange<FFrameNumber>(FFrameNumber(StartFrame), FFrameNumber(StartFrame + 1)));
			}
			Track->AddSection(*Section);
			return Section;
		};

		if (BatchToolHelper::TryGetBatchItems(Args, TEXT("items"), Items))
		{
			BatchToolHelper::FBatchResultBuilder Batch;
			for (int32 i = 0; i < Items->Num(); ++i)
			{
				const TSharedPtr<FJsonObject> Entry = (*Items)[i]->AsObject();
				if (!Entry.IsValid()) { Batch.AddFailure(i, TEXT("Invalid item")); continue; }
				int32 Start = 0, End = 0;
				Entry->TryGetNumberField(TEXT("frame"), Start);
				Entry->TryGetNumberField(TEXT("start_frame"), Start);
				Entry->TryGetNumberField(TEXT("end_frame"), End);
				if (End <= Start) End = Start + 1;
				FString Err;
				UMovieSceneSection* S = AddOne(Start, End, Err);
				if (!S) { Batch.AddFailure(i, Err); continue; }
				TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
				R->SetNumberField(TEXT("start_frame"), Start);
				R->SetNumberField(TEXT("end_frame"), End);
				Batch.AddSuccess(i, R);
			}
			Batch.Finalize(OutJson);
			return;
		}

		int32 Frame = 0; Args->TryGetNumberField(TEXT("frame"), Frame);
		int32 Start = 0; Args->TryGetNumberField(TEXT("start_frame"), Start);
		int32 End = Frame > 0 ? Frame + 1 : Start + 1;
		Args->TryGetNumberField(TEXT("end_frame"), End);
		const int32 ActualStart = Frame > 0 ? Frame : Start;
		FString Err;
		UMovieSceneSection* S = AddOne(ActualStart, End, Err);
		if (!S) { OutError = Err; return; }

		TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
		R->SetBoolField(TEXT("success"), true);
		R->SetNumberField(TEXT("start_frame"), ActualStart);
		R->SetNumberField(TEXT("end_frame"), End);
		OutJson = SerializeJson(R);
	}
}

void HandleAddEventTriggerSectionFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	AddEventSectionImpl(UMovieSceneEventTriggerSection::StaticClass(), Args, OutJson, OutError);
}

void HandleAddEventRepeaterSectionFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	AddEventSectionImpl(UMovieSceneEventRepeaterSection::StaticClass(), Args, OutJson, OutError);
}

#if !UE_VERSION_OLDER_THAN(5, 5, 0)

void HandleSetSectionConditionFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SequencePath; Args->TryGetStringField(TEXT("sequence_path"), SequencePath);
	UMovieScene* MS = GetMovieScene(SequencePath, OutError); if (!MS) return;
	FString BindingLabel; Args->TryGetStringField(TEXT("binding_label"), BindingLabel);
	int32 TrackIdx = -1; Args->TryGetNumberField(TEXT("track_index"), TrackIdx);
	int32 SectionIdx = -1; Args->TryGetNumberField(TEXT("section_index"), SectionIdx);
	FString ConditionClass; Args->TryGetStringField(TEXT("condition_class"), ConditionClass);

	UMovieSceneTrack* Track = ResolveTrack(MS, BindingLabel, TrackIdx, OutError); if (!Track) return;
	const TArray<UMovieSceneSection*>& Secs = Track->GetAllSections();
	if (!Secs.IsValidIndex(SectionIdx)) { OutError = FString::Printf(TEXT("section_index %d out of range [0,%d)"), SectionIdx, Secs.Num()); return; }
	UMovieSceneSection* Section = Secs[SectionIdx];

	Section->Modify();
	if (!ApplyCondition(Section->ConditionContainer, Section, ConditionClass, OutError)) return;

	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetBoolField(TEXT("success"), true);
	R->SetStringField(TEXT("condition_class"), ConditionClass);
	OutJson = SerializeJson(R);
}

void HandleGetSectionConditionFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SequencePath; Args->TryGetStringField(TEXT("sequence_path"), SequencePath);
	UMovieScene* MS = GetMovieScene(SequencePath, OutError); if (!MS) return;
	FString BindingLabel; Args->TryGetStringField(TEXT("binding_label"), BindingLabel);
	int32 TrackIdx = -1; Args->TryGetNumberField(TEXT("track_index"), TrackIdx);
	int32 SectionIdx = -1; Args->TryGetNumberField(TEXT("section_index"), SectionIdx);

	UMovieSceneTrack* Track = ResolveTrack(MS, BindingLabel, TrackIdx, OutError); if (!Track) return;
	const TArray<UMovieSceneSection*>& Secs = Track->GetAllSections();
	if (!Secs.IsValidIndex(SectionIdx)) { OutError = FString::Printf(TEXT("section_index %d out of range [0,%d)"), SectionIdx, Secs.Num()); return; }

	const FString CondClass = Secs[SectionIdx]->ConditionContainer.Condition
		? Secs[SectionIdx]->ConditionContainer.Condition->GetClass()->GetPathName()
		: FString();

	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetBoolField(TEXT("success"), true);
	R->SetStringField(TEXT("condition_class"), CondClass);
	OutJson = SerializeJson(R);
}

void HandleSetTrackConditionFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SequencePath; Args->TryGetStringField(TEXT("sequence_path"), SequencePath);
	UMovieScene* MS = GetMovieScene(SequencePath, OutError); if (!MS) return;
	FString BindingLabel; Args->TryGetStringField(TEXT("binding_label"), BindingLabel);
	int32 TrackIdx = -1; Args->TryGetNumberField(TEXT("track_index"), TrackIdx);
	FString ConditionClass; Args->TryGetStringField(TEXT("condition_class"), ConditionClass);

	UMovieSceneTrack* Track = ResolveTrack(MS, BindingLabel, TrackIdx, OutError); if (!Track) return;

	Track->Modify();
	if (!ApplyCondition(Track->ConditionContainer, Track, ConditionClass, OutError)) return;

	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetBoolField(TEXT("success"), true);
	R->SetStringField(TEXT("condition_class"), ConditionClass);
	OutJson = SerializeJson(R);
}

void HandleGetTrackConditionFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SequencePath; Args->TryGetStringField(TEXT("sequence_path"), SequencePath);
	UMovieScene* MS = GetMovieScene(SequencePath, OutError); if (!MS) return;
	FString BindingLabel; Args->TryGetStringField(TEXT("binding_label"), BindingLabel);
	int32 TrackIdx = -1; Args->TryGetNumberField(TEXT("track_index"), TrackIdx);

	UMovieSceneTrack* Track = ResolveTrack(MS, BindingLabel, TrackIdx, OutError); if (!Track) return;

	const FString CondClass = Track->ConditionContainer.Condition
		? Track->ConditionContainer.Condition->GetClass()->GetPathName()
		: FString();

	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetBoolField(TEXT("success"), true);
	R->SetStringField(TEXT("condition_class"), CondClass);
	OutJson = SerializeJson(R);
}

void HandleSetTrackRowConditionFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SequencePath; Args->TryGetStringField(TEXT("sequence_path"), SequencePath);
	UMovieScene* MS = GetMovieScene(SequencePath, OutError); if (!MS) return;
	FString BindingLabel; Args->TryGetStringField(TEXT("binding_label"), BindingLabel);
	int32 TrackIdx = -1; Args->TryGetNumberField(TEXT("track_index"), TrackIdx);
	int32 RowIdx = -1; Args->TryGetNumberField(TEXT("row_index"), RowIdx);
	FString ConditionClass; Args->TryGetStringField(TEXT("condition_class"), ConditionClass);

	UMovieSceneTrack* Track = ResolveTrack(MS, BindingLabel, TrackIdx, OutError); if (!Track) return;
	Track->Modify();
	FMovieSceneTrackRowMetadata& Meta = Track->FindOrAddTrackRowMetadata(RowIdx);
	if (!ApplyCondition(Meta.ConditionContainer, Track, ConditionClass, OutError)) return;

	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetBoolField(TEXT("success"), true);
	R->SetNumberField(TEXT("row_index"), RowIdx);
	R->SetStringField(TEXT("condition_class"), ConditionClass);
	OutJson = SerializeJson(R);
}

void HandleGetTrackRowConditionFromArgs(const TSharedPtr<FJsonObject>& Args, FString& OutJson, FString& OutError)
{
	if (!Args.IsValid()) { OutError = TEXT("Invalid args"); return; }
	FString SequencePath; Args->TryGetStringField(TEXT("sequence_path"), SequencePath);
	UMovieScene* MS = GetMovieScene(SequencePath, OutError); if (!MS) return;
	FString BindingLabel; Args->TryGetStringField(TEXT("binding_label"), BindingLabel);
	int32 TrackIdx = -1; Args->TryGetNumberField(TEXT("track_index"), TrackIdx);
	int32 RowIdx = -1; Args->TryGetNumberField(TEXT("row_index"), RowIdx);

	UMovieSceneTrack* Track = ResolveTrack(MS, BindingLabel, TrackIdx, OutError); if (!Track) return;
	const FMovieSceneTrackRowMetadata* Meta = Track->FindTrackRowMetadata(RowIdx);
	const FString CondClass = (Meta && Meta->ConditionContainer.Condition)
		? Meta->ConditionContainer.Condition->GetClass()->GetPathName()
		: FString();

	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetBoolField(TEXT("success"), true);
	R->SetNumberField(TEXT("row_index"), RowIdx);
	R->SetStringField(TEXT("condition_class"), CondClass);
	OutJson = SerializeJson(R);
}

#else

void HandleSetSectionConditionFromArgs   (const TSharedPtr<FJsonObject>&, FString&, FString& OutError) { OutError = TEXT("set_section_condition requires UE 5.5+ (UMovieSceneCondition was added in 5.5)."); }
void HandleGetSectionConditionFromArgs   (const TSharedPtr<FJsonObject>&, FString&, FString& OutError) { OutError = TEXT("get_section_condition requires UE 5.5+ (UMovieSceneCondition was added in 5.5)."); }
void HandleSetTrackConditionFromArgs     (const TSharedPtr<FJsonObject>&, FString&, FString& OutError) { OutError = TEXT("set_track_condition requires UE 5.5+ (UMovieSceneCondition was added in 5.5)."); }
void HandleGetTrackConditionFromArgs     (const TSharedPtr<FJsonObject>&, FString&, FString& OutError) { OutError = TEXT("get_track_condition requires UE 5.5+ (UMovieSceneCondition was added in 5.5)."); }
void HandleSetTrackRowConditionFromArgs  (const TSharedPtr<FJsonObject>&, FString&, FString& OutError) { OutError = TEXT("set_track_row_condition requires UE 5.5+ (UMovieSceneCondition was added in 5.5)."); }
void HandleGetTrackRowConditionFromArgs  (const TSharedPtr<FJsonObject>&, FString&, FString& OutError) { OutError = TEXT("get_track_row_condition requires UE 5.5+ (UMovieSceneCondition was added in 5.5)."); }

#endif

}
